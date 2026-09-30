#include "sr3texture/content_validation.h"

#include <algorithm>
#include <cctype>

#include "sr3texture/texture_pair.h"

namespace sr3texture {

bool looksLikeCpegFilename(const std::string& filename) {
    // Exactly two shipped aliases (spec-texture-format.md Sec7, Sec12.2), not a
    // prefix heuristic like sr3fxo's - the spec names these two specific
    // extensions explicitly, so an exact-match set is better justified
    // here than a looser "starts with" match would be.
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return false;
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "cpeg_pc" || ext == "cvbm_pc";
}

CpegValidationResult validateCpegContent(const std::vector<uint8_t>& content) {
    CpegValidationResult result;

    try {
        TexturePair::parse(ByteView(content.data(), content.size()));
    } catch (const FormatError& ex) {
        result.status = CpegValidation::NotWellFormed;
        result.diagnostic = std::string("failed to parse as a valid .cpeg_pc: ") + ex.what();
        return result;
    }

    result.status = CpegValidation::WellFormed;
    return result;
}

vpp::DecompressResult refineWithCpegValidation(vpp::DecompressResult result) {
    // OkUnconfirmedContent is never produced by decompressEntry any more
    // (HANDOFF.md §9.78); this function is retained for API compatibility
    // but is now a permanent no-op.
    return result;
}

} // namespace sr3texture
