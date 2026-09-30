// Synthetic tests for the .fxo_pc shader-wrapper reader and its content
// validator. Two generations of test live here:
//
//  1. The original scan-based tests: small wrappers built from the first-pass
//     spec's facts (magic, version token shape, end token, 8-byte alignment
//     between shaders - the alignment is corrected to 16 by the 2026-09-20
//     spec rewrite, but the scan reader does not depend on it), deliberately
//     varying the header size to prove the scan-based reader doesn't depend
//     on any particular header size. Unchanged.
//
//  2. The header-driven tests at the end (spec-fxo-format.md §6-§8): the
//     variable-size header formula, 16-byte blob placement, the middle
//     (geometry) table order, applicability conditions, roles, the CRC-32
//     name hash, the public D3D9 token reader, and the exact-consumption
//     validator. Every wrapper is built from the spec's text, including
//     negative cases (8-byte alignment, unaligned first blob, wrong stage
//     token, truncated content) that must FAIL.

#include <iostream>
#include <string>
#include <vector>

#include "sr3fxo/content_validation.h"
#include "sr3fxo/d3d9_blob.h"
#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/wrapper_header.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"          \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

struct SyntheticShaderSpec {
    bool isVertex;
    uint8_t major;
    uint8_t minor;
    std::vector<uint8_t> middleBytes; // stand-in for instructions/CTAB - content irrelevant to the scanner
};

// Builds a wrapper with `headerPadding` filler bytes after the 8-byte
// lead-in (simulating spec §5's unconfirmed/variable header size), then
// each shader in sequence, 8-byte-aligned between shaders and NOT padded
// after the last one (spec §3: "confirmed... to land exactly at
// end-of-file for the last shader in the file").
std::vector<uint8_t> buildFxo(size_t headerPadding, const std::vector<SyntheticShaderSpec>& shaders) {
    std::vector<uint8_t> blob;
    appendU32(blob, sr3fxo::kMagic);
    appendU32(blob, 14); // header field @0x04, arbitrary plausible value, not interpreted by the reader
    blob.insert(blob.end(), headerPadding, 0xAB); // non-zero filler, deliberately NOT shaped like a version token anywhere

    for (size_t i = 0; i < shaders.size(); ++i) {
        const auto& s = shaders[i];
        uint32_t token = (s.isVertex ? 0xFFFE0000u : 0xFFFF0000u) |
                          (static_cast<uint32_t>(s.major) << 8) | s.minor;
        appendU32(blob, token);
        blob.insert(blob.end(), s.middleBytes.begin(), s.middleBytes.end());
        appendU32(blob, 0x0000FFFFu);

        if (i + 1 < shaders.size()) {
            while (blob.size() % 8 != 0) blob.push_back(0x00);
        }
    }

    return blob;
}

std::vector<uint8_t> exampleMiddle(const std::string& tag) {
    // Stand-in "instruction stream" bytes - just needs to not itself
    // contain a stray 0x0000FFFF or a version-token-shaped DWORD.
    std::vector<uint8_t> m(tag.begin(), tag.end());
    m.insert(m.end(), {0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    return m;
}

// ---- Builders for the spec-fxo-format.md §7 wrapper (header-driven layout) ----
// Everything below is written from the spec's text: the 0x80-byte fixed part
// (§7.1), nine back-to-back tables in the order T0, T1, T2, T3, T4, vertex,
// middle, pixel, T8 (§7.2), and blobs at the running offset rounded up to 16,
// vertex table first, then middle, then pixel (§6.4, §7.5, §8.2).

void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

// A D3D9 token stream: version token, a CTAB comment (optional), end token.
struct CtabConst {
    const char* name;
    int reg;
    int count;
};

std::vector<uint8_t> d3d9Blob(bool vertex, size_t padDwords,
                              const std::vector<CtabConst>& constants = {}) {
    std::vector<uint8_t> b;
    appendU32(b, (vertex ? 0xFFFE0300u : 0xFFFF0300u));
    if (!constants.empty()) {
        // CTAB payload: "CTAB", then D3DXSHADER_CONSTANTTABLE {Size 28, Creator, Version, Constants, ConstantInfo 28, Flags, Target}
        std::vector<uint8_t> t;
        appendU32(t, 0x42415443u);
        const size_t s = t.size(); // struct start
        for (int i = 0; i < 7; ++i) appendU32(t, 0);
        const uint32_t infoOff = 28;
        std::vector<uint8_t> names;
        const size_t infoBytes = constants.size() * 20;
        for (size_t i = 0; i < constants.size(); ++i) {
            const uint32_t nameOff = static_cast<uint32_t>(infoOff + infoBytes + names.size());
            const std::string nm = constants[i].name;
            names.insert(names.end(), nm.begin(), nm.end());
            names.push_back(0);
            const size_t e = t.size();
            t.resize(e + 20, 0);
            put32(t, e, nameOff);
            put16(t, e + 4, 2);
            put16(t, e + 6, static_cast<uint16_t>(constants[i].reg));    // register index
            put16(t, e + 8, static_cast<uint16_t>(constants[i].count)); // register count
        }
        t.insert(t.end(), names.begin(), names.end());
        while (t.size() % 4 != 0) t.push_back(0);
        put32(t, s + 12, static_cast<uint32_t>(constants.size()));
        put32(t, s + 16, infoOff);
        appendU32(b, 0x0000FFFEu | (static_cast<uint32_t>(t.size() / 4) << 16));
        b.insert(b.end(), t.begin(), t.end());
    }
    for (size_t i = 0; i < padDwords; ++i) appendU32(b, 0x11110000u + static_cast<uint32_t>(i));
    appendU32(b, 0x0000FFFFu);
    return b;
}

struct WrapSpec {
    int16_t c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0;
    std::vector<std::vector<uint8_t>> vs, mid, ps; // blob bytes; an EMPTY vector = a length-0 entry
    uint32_t c8 = 1;
    int32_t version = 14;
    uint32_t flags = 0x100;
};

std::vector<uint8_t> buildWrapper(const WrapSpec& s) {
    const size_t nVS = s.vs.size(), nMid = s.mid.size(), nPS = s.ps.size();
    const size_t H = 0x80 + 0x10u * static_cast<size_t>(s.c0) +
                     8u * static_cast<size_t>(s.c1 + s.c2 + s.c3 + s.c4) + 8u * (nVS + nMid + nPS) + 0x10u * s.c8;
    std::vector<uint8_t> f(H, 0);
    put32(f, 0, sr3fxo::kMagic);
    put32(f, 4, static_cast<uint32_t>(s.version));
    put32(f, 8, s.flags);
    put16(f, 0x0C, static_cast<uint16_t>(s.c0));
    put16(f, 0x0E, static_cast<uint16_t>(s.c1));
    put16(f, 0x10, static_cast<uint16_t>(s.c2));
    put16(f, 0x12, static_cast<uint16_t>(s.c3));
    put16(f, 0x14, static_cast<uint16_t>(s.c4));
    f[0x16] = static_cast<uint8_t>(nVS);
    f[0x17] = static_cast<uint8_t>(nMid);
    f[0x18] = static_cast<uint8_t>(nPS);
    for (size_t k = 0; k < 10; ++k) f[0x60 + k] = 0xFF; // all ten fixed roles absent
    f[0x6A] = 0;                                       // indexed group base
    put32(f, 0x78, s.c8);
    // stage tables
    size_t at = 0x80 + 0x10u * static_cast<size_t>(s.c0) + 8u * static_cast<size_t>(s.c1 + s.c2 + s.c3 + s.c4);
    auto lengths = [&](const std::vector<std::vector<uint8_t>>& v) {
        for (const auto& blob : v) {
            put32(f, at, static_cast<uint32_t>(blob.size()));
            at += 8;
        }
    };
    lengths(s.vs);
    lengths(s.mid);
    lengths(s.ps);
    // T8: first entry (vertex 0, -1) then pixel index 0 - the shape spec §7.3 describes for the sample
    for (uint32_t i = 0; i < s.c8; ++i) {
        put16(f, at, 0);
        put16(f, at + 2, 0xFFFF);
        put16(f, at + 4, 0);
        at += 0x10;
    }
    // blobs: running offset from the UNROUNDED header size, each rounded up to 16
    size_t pos = H;
    auto place = [&](const std::vector<std::vector<uint8_t>>& v) {
        for (const auto& blob : v) {
            if (blob.empty()) continue;
            pos = (pos + 15) / 16 * 16;
            if (f.size() < pos) f.resize(pos, 0);
            f.insert(f.end(), blob.begin(), blob.end());
            pos += blob.size();
        }
    };
    place(s.vs);
    place(s.mid);
    place(s.ps);
    return f;
}

sr3fxo::WrapperHeader parseHeader(const std::vector<uint8_t>& f) {
    return sr3fxo::WrapperHeader::parse(sr3fxo::ByteView(f.data(), f.size()));
}
bool applicable(const std::vector<uint8_t>& f, std::string* why = nullptr) {
    sr3fxo::WrapperHeader h;
    std::string w;
    const bool ok = sr3fxo::WrapperHeader::tryParse(sr3fxo::ByteView(f.data(), f.size()), h, w);
    if (why) *why = w;
    return ok;
}

} // namespace

int main() {
    // --- Single shader, minimal header. ---
    {
        std::vector<uint8_t> blob =
            buildFxo(0, {{true, 3, 0, exampleMiddle("vs")}});
        sr3fxo::ShaderWrapper w =
            sr3fxo::ShaderWrapper::parse(sr3fxo::ByteView(blob.data(), blob.size()));
        CHECK(w.shaders().size() == 1);
        CHECK(w.shaders()[0].isVertexShader);
        CHECK(w.shaders()[0].versionMajor == 3);
        CHECK(w.shaders()[0].versionMinor == 0);
        CHECK(w.shaders()[0].offset + w.shaders()[0].length == blob.size()); // last shader ends exactly at EOF
    }

    // --- Two shaders (vertex + pixel), matching spec §1's confirmed
    // sample shape, with a LARGER header padding than the first test -
    // proves the reader doesn't depend on a fixed header size. ---
    {
        std::vector<uint8_t> blob = buildFxo(
            200, {{true, 3, 0, exampleMiddle("vertex_shader_body")},
                  {false, 3, 0, exampleMiddle("pixel_shader_body")}});
        sr3fxo::ShaderWrapper w =
            sr3fxo::ShaderWrapper::parse(sr3fxo::ByteView(blob.data(), blob.size()));
        CHECK(w.shaders().size() == 2);
        CHECK(w.shaders()[0].isVertexShader);
        CHECK(!w.shaders()[1].isVertexShader);
        // Second shader must start 8-byte aligned (spec §3).
        CHECK(w.shaders()[1].offset % 8 == 0);
        // Last shader ends exactly at EOF.
        CHECK(w.shaders()[1].offset + w.shaders()[1].length == blob.size());

        sr3fxo::ByteView vs = w.shaderBytes(0);
        sr3fxo::ByteView ps = w.shaderBytes(1);
        CHECK(vs.readU32LE(0) == 0xFFFE0300u);
        CHECK(ps.readU32LE(0) == 0xFFFF0300u);
    }

    // --- Zero shaders (no version token anywhere) - must not throw, just
    // report an empty shader list. ---
    {
        std::vector<uint8_t> blob = buildFxo(0, {});
        blob.insert(blob.end(), 50, 0xCC); // filler with no valid token
        sr3fxo::ShaderWrapper w =
            sr3fxo::ShaderWrapper::parse(sr3fxo::ByteView(blob.data(), blob.size()));
        CHECK(w.shaders().empty());
    }

    // --- Negative: bad magic must be rejected. ---
    {
        std::vector<uint8_t> blob = buildFxo(0, {{true, 3, 0, exampleMiddle("vs")}});
        blob[0] ^= 0xFF;
        bool threw = false;
        try {
            sr3fxo::ShaderWrapper::parse(sr3fxo::ByteView(blob.data(), blob.size()));
        } catch (const sr3fxo::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Content validation: looksLikeFxoFilename. Real content
    // (shaders.vpp_pc) confirmed 3 distinct extension variants: plain
    // ".fxo_pc" (spec's own documented case), ".fxo_pc_dx11" (~50% of
    // that archive - a DX11 shader variant spec §5 item 5 didn't sample),
    // and a couple of bare ".fxo" entries. ---
    CHECK(sr3fxo::looksLikeFxoFilename("rfg-skybox-meteors.fxo_pc"));
    CHECK(sr3fxo::looksLikeFxoFilename("SOME_SHADER.FXO_PC")); // case-insensitive
    CHECK(sr3fxo::looksLikeFxoFilename("ir_at_window_reflectmask_fd.fxo_pc_dx11"));
    CHECK(sr3fxo::looksLikeFxoFilename("rl_bokeh_cs.fxo"));
    CHECK(!sr3fxo::looksLikeFxoFilename("texture.dds"));
    CHECK(!sr3fxo::looksLikeFxoFilename("no_extension_at_all"));

    // --- validateFxoContent: well-formed two-shader wrapper. ---
    {
        std::vector<uint8_t> blob = buildFxo(
            8, {{true, 3, 0, exampleMiddle("vs")}, {false, 3, 0, exampleMiddle("ps")}});
        auto v = sr3fxo::validateFxoContent(blob);
        CHECK(v.status == sr3fxo::FxoValidation::WellFormed);
    }

    // --- validateFxoContent: content with trailing garbage after the
    // last shader's end token - must fail (spec §3's "ends exactly at
    // EOF" convention violated). ---
    {
        std::vector<uint8_t> blob = buildFxo(0, {{true, 3, 0, exampleMiddle("vs")}});
        blob.insert(blob.end(), {0x01, 0x02, 0x03, 0x04}); // trailing junk past the real end
        auto v = sr3fxo::validateFxoContent(blob);
        CHECK(v.status == sr3fxo::FxoValidation::NotWellFormed);
    }

    // --- refineWithFxoValidation: PERMANENT NO-OP (HANDOFF.md §9.78 -
    // decompressEntry never produces OkUnconfirmedContent any more), passes
    // EVERY status through completely unchanged, including
    // OkUnconfirmedContent itself. ---
    {
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::Ok;
        in.data = {0x00, 0x01, 0x02};
        vpp::DecompressResult out = sr3fxo::refineWithFxoValidation(in);
        CHECK(out.status == vpp::DecodeStatus::Ok); // untouched
    }
    {
        std::vector<uint8_t> blob = buildFxo(8, {{true, 3, 0, exampleMiddle("vs")}});
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = blob;
        vpp::DecompressResult out = sr3fxo::refineWithFxoValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op
        CHECK(out.data == blob);
    }
    {
        std::vector<uint8_t> notFxo = {0x00, 0x01, 0x02, 0x03};
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = notFxo;
        vpp::DecompressResult out = sr3fxo::refineWithFxoValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op, even for content that would have failed the old check
        CHECK(out.data == notFxo);
    }

    // ===================================================================
    // Header-driven layout (spec-fxo-format.md §6-§8, 2026-09-20 rewrite)
    // ===================================================================

    // --- The trusted sample's shape (§7.2): c1=1, c3=4, nVS=nPS=1, c8=1 ->
    // H = 0x80 + 8*5 + 8*2 + 0x10 = 200, first blob at align16(200) = 208, VS
    // 732 bytes, pixel blob at align16(940) = 944, 464 bytes, end 1408. ---
    {
        WrapSpec s;
        s.c1 = 1;
        s.c3 = 4;
        s.vs = {d3d9Blob(true, 181)};   // 4 + 181*4 + 4 = 732
        s.ps = {d3d9Blob(false, 114)};  // 4 + 114*4 + 4 = 464
        CHECK(s.vs[0].size() == 732 && s.ps[0].size() == 464);
        std::vector<uint8_t> f = buildWrapper(s);
        CHECK(f.size() == 1408);
        sr3fxo::WrapperHeader h = parseHeader(f);
        CHECK(h.headerSize() == 200);
        CHECK(h.firstBlobOffset() == 208);
        CHECK(h.version() == 14);
        CHECK(h.count(1) == 1 && h.count(3) == 4 && h.vertexCount() == 1 && h.pixelCount() == 1 && h.passCount() == 1);
        // §7.2: the "length fields" at 0xA8 / 0xB0 are the first entries of the vertex / pixel tables.
        sr3fxo::ByteView v(f.data(), f.size());
        CHECK(v.readU32LE(0xA8) == 732);
        CHECK(v.readU32LE(0xB0) == 464);
        CHECK(h.vertexTable()[0].length == 732 && h.pixelTable()[0].length == 464);
        // §7.3: what the first pass read as "0xB8 = 0xFFFF0000" is T8[0] = (vertex 0, -1).
        CHECK(v.readU32LE(0xB8) == 0xFFFF0000u);
        CHECK(h.passes()[0].vertexIndex == 0 && h.passes()[0].word2 == -1 && h.passes()[0].pixelIndex == 0);
        size_t end = 0;
        auto blobs = h.layoutBlobs(end);
        CHECK(blobs.size() == 2);
        CHECK(blobs[0].stage == sr3fxo::Stage::Vertex && blobs[0].offset == 208 && blobs[0].length == 732);
        CHECK(blobs[1].stage == sr3fxo::Stage::Pixel && blobs[1].offset == 944 && blobs[1].length == 464);
        CHECK(end == 1408 && end == f.size());
        CHECK(blobs[0].offset % 16 == 0 && blobs[1].offset % 16 == 0);
        // The scan-based reader must agree on where the two blobs are.
        sr3fxo::ShaderWrapper w = sr3fxo::ShaderWrapper::parse(sr3fxo::ByteView(f.data(), f.size()));
        CHECK(w.shaders().size() == 2 && w.shaders()[0].offset == 208 && w.shaders()[1].offset == 944);
        // Roles (§7.4): flags 0x100, ten 0xFF bytes, base byte 0 -> only role 10 member 0.
        for (size_t k = 0; k < 10; ++k) CHECK(!h.rolePresent(k) && h.roleIndexByte(k) == -1);
        CHECK(h.rolePresent(10, 0) && !h.rolePresent(10, 1)); // base + n < c8 = 1
    }

    // --- The header size is a function of the counts alone (§7.2): more
    // constants / samplers / passes push the tables and the blobs later. ---
    {
        WrapSpec a;
        a.c0 = 2; a.c1 = 3; a.c2 = 1; a.c3 = 5; a.c4 = 2; a.c8 = 3;
        a.vs = {d3d9Blob(true, 4), d3d9Blob(true, 9)};
        a.ps = {d3d9Blob(false, 6), d3d9Blob(false, 2), d3d9Blob(false, 8)};
        std::vector<uint8_t> f = buildWrapper(a);
        sr3fxo::WrapperHeader h = parseHeader(f);
        const size_t H = 0x80 + 0x10 * 2 + 8 * (3 + 1 + 5 + 2) + 8 * (2 + 0 + 3) + 0x10 * 3;
        CHECK(h.headerSize() == H);
        CHECK(h.headerSize() % 8 == 0); // §7.2: "H is always a multiple of 8"
        CHECK(h.samplers().size() == 2 && h.constantsT1().size() == 3 && h.constantsT2().size() == 1 &&
              h.constantsT3().size() == 5 && h.constantsT4().size() == 2 && h.passes().size() == 3);
        size_t end = 0;
        auto blobs = h.layoutBlobs(end);
        CHECK(blobs.size() == 5 && end == f.size());
        for (const auto& b : blobs) CHECK(b.offset % 16 == 0);
        CHECK(blobs[0].offset == h.firstBlobOffset());
        // Table order: vertex entries first, then pixel, and blobs in that order.
        CHECK(blobs[0].stage == sr3fxo::Stage::Vertex && blobs[1].stage == sr3fxo::Stage::Vertex &&
              blobs[2].stage == sr3fxo::Stage::Pixel);
    }

    // --- 16-byte alignment, not 8: when H is already a multiple of 8 but not
    // of 16 (H = 200), the first blob still moves to 208 (§7.5). The same
    // file placed under an 8-byte rule would put it at 200. ---
    {
        WrapSpec s;
        s.c1 = 1; s.c3 = 4;
        // A 24-byte vertex blob: 208 + 24 = 232 -> pixel blob at 240, whereas
        // starting the vertex blob unaligned at 200 would end at 224 and place
        // the pixel blob at 224, so both wrong rules below must disagree.
        s.vs = {d3d9Blob(true, 4)};
        s.ps = {d3d9Blob(false, 5)};
        CHECK(s.vs[0].size() == 24);
        std::vector<uint8_t> f = buildWrapper(s);
        sr3fxo::WrapperHeader h = parseHeader(f);
        CHECK(h.headerSize() == 200 && h.firstBlobOffset() == 208);
        sr3fxo::LayoutOptions eight;
        eight.alignment = 8;
        size_t e8 = 0, e16 = 0;
        auto b8 = h.layoutBlobs(eight, e8);
        auto b16 = h.layoutBlobs(e16);
        CHECK(b8[0].offset == 200 && b16[0].offset == 208);
        CHECK(e16 == f.size() && e8 != f.size()); // the 8-byte rule cannot reproduce the file
        // Padding before the FIRST blob is real: not aligning it also fails.
        sr3fxo::LayoutOptions noFirst;
        noFirst.alignFirstBlob = false;
        size_t en = 0;
        h.layoutBlobs(noFirst, en);
        CHECK(en != f.size());
    }

    // --- Length 0 = "no shader here" (§6.4): skipped, occupies only its table slot. ---
    {
        WrapSpec s;
        s.c8 = 2;
        s.vs = {std::vector<uint8_t>{}, d3d9Blob(true, 3)};
        s.ps = {d3d9Blob(false, 3)};
        std::vector<uint8_t> f = buildWrapper(s);
        sr3fxo::WrapperHeader h = parseHeader(f);
        CHECK(h.vertexTable().size() == 2 && h.vertexTable()[0].length == 0);
        size_t end = 0;
        auto blobs = h.layoutBlobs(end);
        CHECK(blobs.size() == 2 && blobs[0].tableIndex == 1 && end == f.size());
    }

    // --- The middle (geometry) table sits between vertex and pixel (§7.2,
    // §8.2). The Direct3D 11 loader steps over its blobs; the Direct3D 9
    // loader does not (§8.2 (b)), so the pixel blob lands elsewhere. ---
    {
        WrapSpec s;
        s.vs = {d3d9Blob(true, 6)};
        s.mid = {d3d9Blob(true, 10)}; // stand-in geometry blob (content irrelevant to layout)
        s.ps = {d3d9Blob(false, 6)};
        std::vector<uint8_t> f = buildWrapper(s);
        sr3fxo::WrapperHeader h = parseHeader(f);
        CHECK(h.middleCount() == 1);
        size_t end = 0;
        auto blobs = h.layoutBlobs(end);
        CHECK(blobs.size() == 3 && blobs[1].stage == sr3fxo::Stage::Middle && blobs[2].stage == sr3fxo::Stage::Pixel);
        CHECK(blobs[0].offset < blobs[1].offset && blobs[1].offset < blobs[2].offset);
        CHECK(end == f.size());
        sr3fxo::LayoutOptions d9;
        d9.includeMiddle = false;
        size_t endD9 = 0;
        auto blobsD9 = h.layoutBlobs(d9, endD9);
        CHECK(blobsD9.size() == 2 && endD9 != f.size());
        CHECK(blobsD9[1].offset < blobs[2].offset); // pixel blob would be read too early
    }

    // --- Applicability (§6.3, §6.4): each failure says why and is never a throw from tryParse. ---
    {
        WrapSpec s;
        s.vs = {d3d9Blob(true, 3)};
        s.ps = {d3d9Blob(false, 3)};
        const std::vector<uint8_t> good = buildWrapper(s);
        CHECK(applicable(good));
        std::string why;

        std::vector<uint8_t> badMagic = good;
        badMagic[1] ^= 0xFF;
        CHECK(!applicable(badMagic, &why) && why == "magic mismatch");

        for (int32_t v : {13, 0, -1}) { // signed compare against 14; "below 14" is rejected
            WrapSpec t = s;
            t.version = v;
            CHECK(!applicable(buildWrapper(t), &why) && why == "version below 14");
        }
        for (int32_t v : {14, 15, 100}) { // no upper bound is checked by the loader
            WrapSpec t = s;
            t.version = v;
            CHECK(applicable(buildWrapper(t)));
        }

        std::vector<uint8_t> negS16 = good;
        put16(negS16, 0x0C, 0xFFFF); // c0 = -1
        CHECK(!applicable(negS16, &why) && why == "negative s16 table count");
        std::vector<uint8_t> negS8 = good;
        negS8[0x16] = 0x80; // nVS as a signed byte
        CHECK(!applicable(negS8, &why) && why == "negative s8 stage count");

        std::vector<uint8_t> hugeC8 = good;
        put32(hugeC8, 0x78, 0xFFFFFFFFu); // corrupt count: the tables cannot fit
        CHECK(!applicable(hugeC8, &why) && why == "tables extend past the end of the file");
        std::vector<uint8_t> tinyHdr(good.begin(), good.begin() + 0x40);
        CHECK(!applicable(tinyHdr, &why) && why == "smaller than the 0x80-byte fixed header");
        std::vector<uint8_t> cutTables(good.begin(), good.begin() + 0x86); // shorter than H
        CHECK(!applicable(cutTables, &why) && why == "tables extend past the end of the file");
        bool threw = false;
        try { parseHeader(badMagic); } catch (const sr3fxo::FormatError&) { threw = true; }
        CHECK(threw);
    }

    // --- Roles (§7.4): roles 0..9 test the flag bit alone; role 10 needs
    // flag 0x100, base >= 0 and base + n < c8. ---
    {
        WrapSpec s;
        s.c8 = 4;
        s.vs = {d3d9Blob(true, 2)};
        s.ps = {d3d9Blob(false, 2)};
        std::vector<uint8_t> f = buildWrapper(s);
        // Set only role 3's flag (0x400) and give it index byte 2; role 10 flag stays clear.
        put32(f, 8, 0x400);
        f[0x63] = 2;
        f[0x6A] = 1; // group base
        sr3fxo::WrapperHeader h = parseHeader(f);
        for (size_t k = 0; k < 10; ++k) CHECK(h.rolePresent(k) == (k == 3));
        CHECK(h.roleIndexByte(3) == 2 && h.roleIndexByte(2) == -1);
        CHECK(!h.rolePresent(10, 0)); // flag 0x100 clear
        put32(f, 8, 0x100);
        h = parseHeader(f);
        CHECK(h.groupBase() == 1);
        CHECK(h.rolePresent(10, 0) && h.rolePresent(10, 2) && !h.rolePresent(10, 3)); // 1 + 3 == c8 = 4, out of range
        f[0x6A] = 0xFF; // base -1
        h = parseHeader(f);
        CHECK(!h.rolePresent(10, 0));
        // The ten flag bits the spec lists, one per role, in the spec's order.
        const uint32_t bits[10] = {0x001, 0x200, 0x004, 0x400, 0x010, 0x020, 0x040, 0x080, 0x800, 0x1000};
        for (size_t k = 0; k < 10; ++k) CHECK(sr3fxo::kRoleFlagBits[k] == bits[k]);
    }

    // --- Hash primitive (§7.6): reflected CRC-32 step, initial value 0, NO
    // final inversion, over the LOWER-CASED name. ---
    {
        CHECK(sr3fxo::crc32Table(1) == 0x77073096u);
        CHECK(sr3fxo::crc32Table(2) == 0xEE0E612Cu);
        CHECK(sr3fxo::crc32Table(3) == 0x990951BAu);
        CHECK(sr3fxo::hashLowerName("") == 0);
        CHECK(sr3fxo::hashLowerName("Time") == sr3fxo::hashLowerName("tIME"));
        CHECK(sr3fxo::hashLowerName("a") == sr3fxo::crc32Table('a')); // one step from 0: table[byte ^ 0]
        // Independent bitwise reference (no table) for a longer string.
        auto ref = [](const std::string& s) {
            uint32_t crc = 0;
            for (char ch : s) {
                uint8_t b = static_cast<uint8_t>(ch);
                if (b >= 'A' && b <= 'Z') b = static_cast<uint8_t>(b + 32);
                crc ^= b;
                for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
            }
            return crc;
        };
        for (const char* n : {"objTM", "Meteor_strength", "a_much_longer_constant_name_0"})
            CHECK(sr3fxo::hashLowerName(n) == ref(n));
        // Values measured on the trusted sample's own header entries (5/5 match against the bytecode's constant table).
        CHECK(sr3fxo::hashLowerName("Meteor_strength") == 0x53125CECu);
        CHECK(sr3fxo::hashLowerName("objTM") == 0xB75D30F5u);
        CHECK(sr3fxo::hashLowerName("projTM") == 0xD665B6DAu);
        CHECK(sr3fxo::hashLowerName("Time") == 0x4ED04759u);
        CHECK(sr3fxo::hashLowerName("Tint_color") == 0x8F88517Au);
        // The seedless step over raw bytes equals the same table walk (blob identity, §6.4).
        const uint8_t raw[3] = {'a', 'b', 'c'};
        CHECK(sr3fxo::crc32Raw(raw, 3) == sr3fxo::hashLowerName("abc"));
    }

    // --- Public D3D9 token stream reader (§3): version token, CTAB, end token. ---
    {
        auto vsb = d3d9Blob(true, 2, {{"objTM", 32, 4}, {"Time", 40, 1}});
        sr3fxo::D3d9BlobInfo info = sr3fxo::inspectD3d9Blob(sr3fxo::ByteView(vsb.data(), vsb.size()));
        CHECK(info.versionTokenValid && info.isVertex && info.major == 3 && info.minor == 0);
        CHECK(info.endTokenAtLastDword && info.ctabFound);
        CHECK(info.constants.size() == 2);
        CHECK(info.constants[0].name == "objTM" && info.constants[0].registerIndex == 32 && info.constants[0].registerCount == 4);
        CHECK(info.constants[1].name == "Time" && info.constants[1].registerIndex == 40 && info.constants[1].registerCount == 1);
        auto psb = d3d9Blob(false, 1);
        auto pi = sr3fxo::inspectD3d9Blob(sr3fxo::ByteView(psb.data(), psb.size()));
        CHECK(pi.versionTokenValid && !pi.isVertex && !pi.ctabFound && pi.constants.empty());
        psb.back() ^= 0xFF; // damage the end token
        CHECK(!sr3fxo::inspectD3d9Blob(sr3fxo::ByteView(psb.data(), psb.size())).endTokenAtLastDword);
        std::vector<uint8_t> junk(64, 0xAB);
        CHECK(!sr3fxo::inspectD3d9Blob(sr3fxo::ByteView(junk.data(), junk.size())).versionTokenValid);
        // A table entry hash matches the constant table by name + register (the §9.2 test, on synthetic data).
        WrapSpec s;
        s.c3 = 1;
        s.vs = {vsb};
        s.ps = {d3d9Blob(false, 1)};
        std::vector<uint8_t> f = buildWrapper(s);
        const size_t t3 = 0x80; // c0 = c1 = c2 = 0, so T3 starts right after the fixed part
        put32(f, t3, sr3fxo::hashLowerName("Time"));
        f[t3 + 4] = 40;
        f[t3 + 6] = 1;
        f[t3 + 7] = 1;
        sr3fxo::WrapperHeader h = parseHeader(f);
        CHECK(h.constantsT3().size() == 1 && h.constantsT3()[0].nameHash == sr3fxo::hashLowerName("Time"));
        CHECK(h.constantsT3()[0].registerIndex == 40 && h.constantsT3()[0].registersPerElement == 1 &&
              h.constantsT3()[0].elementCount == 1);
    }

    // --- validateFxoHeaderLayout / refineWithFxoHeaderValidation (exact consumption). ---
    {
        WrapSpec s;
        s.c1 = 1; s.c3 = 2;
        s.vs = {d3d9Blob(true, 5)};
        s.ps = {d3d9Blob(false, 7)};
        const std::vector<uint8_t> good = buildWrapper(s);
        auto r = sr3fxo::validateFxoHeaderLayout(good);
        CHECK(r.status == sr3fxo::FxoValidation::WellFormed && r.endOfBlobs == good.size() && r.blobCount == 2);

        std::vector<uint8_t> shortBy1(good.begin(), good.end() - 1);
        r = sr3fxo::validateFxoHeaderLayout(shortBy1);
        CHECK(r.status == sr3fxo::FxoValidation::NotWellFormed && r.endOfBlobs == good.size());

        std::vector<uint8_t> longer = good;
        longer.push_back(0);
        CHECK(sr3fxo::validateFxoHeaderLayout(longer).status == sr3fxo::FxoValidation::NotWellFormed);

        // A truncated copy of a LONGER file: valid header, blobs that run past the data - the shape the container
        // library's wrong-stream decodes have.
        std::vector<uint8_t> truncated(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() * 6 / 10));
        CHECK(sr3fxo::validateFxoHeaderLayout(truncated).status == sr3fxo::FxoValidation::NotWellFormed);

        // Payload checks: a vertex-table blob carrying a PIXEL token is rejected for D3d9, accepted when unchecked.
        WrapSpec wrongStage = s;
        wrongStage.vs = {d3d9Blob(false, 5)};
        std::vector<uint8_t> ws = buildWrapper(wrongStage);
        CHECK(sr3fxo::validateFxoHeaderLayout(ws, sr3fxo::FxoPayload::D3d9).status == sr3fxo::FxoValidation::NotWellFormed);
        CHECK(sr3fxo::validateFxoHeaderLayout(ws, sr3fxo::FxoPayload::Unchecked).status == sr3fxo::FxoValidation::WellFormed);
        // Missing end token
        std::vector<uint8_t> noEnd = good;
        noEnd[noEnd.size() - 1] = 0x77;
        CHECK(sr3fxo::validateFxoHeaderLayout(noEnd).status == sr3fxo::FxoValidation::NotWellFormed);
        // Header not applicable -> NotWellFormed with the reason
        std::vector<uint8_t> bad = good;
        bad[0] ^= 1;
        r = sr3fxo::validateFxoHeaderLayout(bad);
        CHECK(r.status == sr3fxo::FxoValidation::NotWellFormed && r.diagnostic.find("magic mismatch") != std::string::npos);

        // DXBC payload: blobs must begin "DXBC"; middle blobs are stepped over.
        auto dxbc = [](size_t n) {
            std::vector<uint8_t> b = {'D', 'X', 'B', 'C'};
            b.resize(n, 0x5A);
            return b;
        };
        WrapSpec d;
        d.vs = {dxbc(64)};
        d.mid = {dxbc(48)};
        d.ps = {dxbc(80)};
        std::vector<uint8_t> df = buildWrapper(d);
        CHECK(sr3fxo::validateFxoHeaderLayout(df, sr3fxo::FxoPayload::Dxbc).status == sr3fxo::FxoValidation::WellFormed);
        // The same file judged as a Direct3D 9 payload does not consume exactly (middle blobs not stepped over).
        CHECK(sr3fxo::validateFxoHeaderLayout(df, sr3fxo::FxoPayload::D3d9).status == sr3fxo::FxoValidation::NotWellFormed);
        WrapSpec d2 = d;
        d2.ps = {std::vector<uint8_t>(80, 0x5A)}; // no DXBC magic
        CHECK(sr3fxo::validateFxoHeaderLayout(buildWrapper(d2), sr3fxo::FxoPayload::Dxbc).status ==
              sr3fxo::FxoValidation::NotWellFormed);

        CHECK(sr3fxo::fxoPayloadForFilename("a.fxo_pc") == sr3fxo::FxoPayload::D3d9);
        CHECK(sr3fxo::fxoPayloadForFilename("A.FXO_PC_DX11") == sr3fxo::FxoPayload::Dxbc);
        CHECK(sr3fxo::fxoPayloadForFilename("a.fxo") == sr3fxo::FxoPayload::Unchecked);

        // refineWithFxoHeaderValidation: PERMANENT NO-OP (HANDOFF.md §9.78
        // - decompressEntry never produces OkUnconfirmedContent any more;
        // same reasoning as refineWithFxoValidation above).
        vpp::DecompressResult okIn;
        okIn.status = vpp::DecodeStatus::Ok;
        okIn.data = good;
        CHECK(sr3fxo::refineWithFxoHeaderValidation(okIn).status == vpp::DecodeStatus::Ok);
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = good;
        auto out = sr3fxo::refineWithFxoHeaderValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent && out.data == good); // unchanged - no-op
        in.data = truncated;
        out = sr3fxo::refineWithFxoHeaderValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent && out.data == truncated); // unchanged - no-op, even for content that would have failed the old check
        // The pre-existing scan-based validator is unchanged and still rejects trailing junk.
        std::vector<uint8_t> tail = good;
        tail.insert(tail.end(), {1, 2, 3, 4});
        CHECK(sr3fxo::validateFxoContent(tail).status == sr3fxo::FxoValidation::NotWellFormed);
    }

    if (g_failures == 0) {
        std::cout << "All synthetic fxo-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
