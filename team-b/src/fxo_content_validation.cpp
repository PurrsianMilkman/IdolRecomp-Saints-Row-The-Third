#include "sr3fxo/content_validation.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "sr3fxo/d3d9_blob.h"
#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/wrapper_header.h"

namespace sr3fxo {

bool looksLikeFxoFilename(const std::string& filename) {
    // Real content (shaders.vpp_pc) shows the plain ".fxo_pc" extension
    // spec-fxo-format.md documents is only about half the picture: ~50%
    // of that archive's entries instead end in ".fxo_pc_dx11" (a DX11
    // shader variant - consistent with spec §5 item 5's own note that
    // "different shader model versions across the DX9 vs DX11 executable
    // builds" weren't sampled), plus a couple of bare ".fxo" entries.
    // Matching only the exact ".fxo_pc" suffix would silently skip both -
    // the same class of gap spec-xtbl-format.md §4 already flagged for
    // ".cte_xtbl" vs plain ".xtbl". Match on "the extension starts with
    // fxo" instead of an exact suffix, so it covers all three observed
    // variants (and any future one following the same convention)
    // without being so broad it could match an unrelated filename.
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return false;
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext.rfind("fxo", 0) == 0; // extension starts with "fxo"
}

FxoValidationResult validateFxoContent(const std::vector<uint8_t>& content) {
    FxoValidationResult result;

    ShaderWrapper wrapper;
    try {
        wrapper = ShaderWrapper::parse(ByteView(content.data(), content.size()));
    } catch (const FormatError& ex) {
        result.status = FxoValidation::NotWellFormed;
        result.diagnostic = std::string("failed to parse as a valid .fxo_pc wrapper: ") + ex.what();
        return result;
    }

    if (wrapper.shaders().empty()) {
        result.status = FxoValidation::NotWellFormed;
        result.diagnostic =
            "no D3D9 shader version token found via the scan-based recipe (spec-fxo-format.md "
            "§4.1)";
        return result;
    }

    const EmbeddedShader& last = wrapper.shaders().back();
    size_t lastEnd = last.offset + last.length;
    if (lastEnd != content.size()) {
        result.status = FxoValidation::NotWellFormed;
        result.diagnostic =
            "last embedded shader's end token does not land at end-of-content (spec-fxo-"
            "format.md §3's confirmed convention) - ends at " +
            std::to_string(lastEnd) + ", content is " + std::to_string(content.size()) +
            " bytes";
        return result;
    }

    result.status = FxoValidation::WellFormed;
    return result;
}

FxoPayload fxoPayloadForFilename(const std::string& filename) {
    std::string low = filename;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto ends = [&](const char* suf) {
        const size_t n = std::char_traits<char>::length(suf);
        return low.size() >= n && low.compare(low.size() - n, n, suf) == 0;
    };
    if (ends(".fxo_pc_dx11")) return FxoPayload::Dxbc;
    if (ends(".fxo_pc")) return FxoPayload::D3d9;
    return FxoPayload::Unchecked;
}

FxoHeaderValidationResult validateFxoHeaderLayout(const std::vector<uint8_t>& content, FxoPayload payload) {
    FxoHeaderValidationResult r;
    WrapperHeader h;
    std::string why;
    if (!WrapperHeader::tryParse(ByteView(content.data(), content.size()), h, why)) {
        r.diagnostic = "the spec's header-size formula is not applicable: " + why;
        return r;
    }
    r.headerSize = h.headerSize();
    LayoutOptions opt;
    opt.includeMiddle = payload != FxoPayload::D3d9; // §8.2 (b): the D3D9 loader does not step over middle blobs
    size_t end = 0;
    const std::vector<WrapperBlob> blobs = h.layoutBlobs(opt, end);
    r.endOfBlobs = end;
    r.blobCount = blobs.size();
    if (end != content.size()) {
        r.diagnostic = "header-derived length (header " + std::to_string(h.headerSize()) + " bytes, " +
                       std::to_string(blobs.size()) + " blobs, 16-byte placement) is " + std::to_string(end) +
                       " but the content is " + std::to_string(content.size()) +
                       " bytes (spec-fxo-format.md §7.2/§7.5 exact-consumption identity)";
        return r;
    }
    if (payload != FxoPayload::Unchecked) {
        for (const WrapperBlob& b : blobs) {
            const ByteView v(content.data() + b.offset, b.length);
            if (payload == FxoPayload::Dxbc) {
                if (b.length < 4 || std::memcmp(v.data(), "DXBC", 4) != 0) {
                    r.diagnostic = "a blob at offset " + std::to_string(b.offset) + " does not begin with \"DXBC\"";
                    return r;
                }
            } else if (b.stage != Stage::Middle) {
                const D3d9BlobInfo info = inspectD3d9Blob(v);
                if (!info.versionTokenValid || info.isVertex != (b.stage == Stage::Vertex) || !info.endTokenAtLastDword) {
                    r.diagnostic = "the blob at offset " + std::to_string(b.offset) +
                                   " is not a Direct3D 9 token stream of the stage its table says";
                    return r;
                }
            }
        }
    }
    r.status = FxoValidation::WellFormed;
    return r;
}

vpp::DecompressResult refineWithFxoHeaderValidation(vpp::DecompressResult result, FxoPayload payload) {
    // OkUnconfirmedContent is never produced by decompressEntry any more
    // (HANDOFF.md §9.78); this function is retained for API compatibility
    // but is now a permanent no-op. `payload` is intentionally unused now
    // that there is no check left to run with it.
    (void)payload;
    return result;
}

vpp::DecompressResult refineWithFxoValidation(vpp::DecompressResult result) {
    // OkUnconfirmedContent is never produced by decompressEntry any more
    // (HANDOFF.md §9.78); this function is retained for API compatibility
    // but is now a permanent no-op.
    return result;
}

} // namespace sr3fxo
