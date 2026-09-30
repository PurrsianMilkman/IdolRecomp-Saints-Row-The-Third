// Synthetic tests for the shared "Mesh" sub-block reader (`sr3mesh`).
//
// Fixtures are built from `spec-vertex-format.md`'s stated layout, never
// from `src/mesh_block.cpp`. The ctdg `sourceNameTruncated` bug is the
// standing reason: a fixture derived from the parser only proves the
// parser agrees with itself.
//
// Built here directly from the spec:
//   * §2 pre-header — version 9 at +0x00, check value at +0x04, c-length
//     at +0x08, g-length at +0x0C, the 0x70 header at +0x10;
//   * §2 header — flags at +0x00, channel count at +0x10, index count at
//     +0x20, index element size at +0x30;
//   * §3 channel record, 24 bytes — element count, sizeA, layout code,
//     texcoord count, sizeB;
//   * §4 g-segment walk — check value, align 16, index buffer, then each
//     channel aligned to 16, align 4, check value again, total equal to
//     the declared g-length;
//   * §5 stride law — stride = base(code) + 4 × texcoord_count;
//   * §6 element encodings — float3 position, UBYTE4N normal/tangent,
//     weights-then-indices with 255 as the unused-influence sentinel,
//     int16/1024 SIGNED texture coordinates;
//   * §8.1 triangle strips stitched with degenerate triangles, §8.2 draw
//     ranges.
//
// Two properties are asserted because the spec proves them with a control
// and a reader could plausibly get them backwards:
//   * a weight lane is zero exactly when its index lane is 255
//     (70,054/70,054 in the population, control 0.1498);
//   * texture coordinates are SIGNED — the unsigned reading differs by a
//     wrap, and the fixture includes a negative coordinate so a reader
//     that drops the sign produces 63.99 instead of -0.01.

#include "alloc_guard.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3mesh/errors.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/bone_palette.h"

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

#define CHECK_THROWS(expr)                                                   \
    do {                                                                     \
        bool threw = false;                                                  \
        try { expr; }                                                        \
        catch (const sr3mesh::FormatError&) { threw = true; }                \
        catch (const std::out_of_range&) { threw = true; }                   \
        if (!threw) {                                                        \
            std::cerr << "CHECK FAILED (expected throw): " #expr             \
                      << " at " __FILE__ ":" << __LINE__ << "\n";            \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void putU16At(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
void putF32At(std::vector<uint8_t>& b, size_t off, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    putU32At(b, off, bits);
}
void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.resize(b.size() + 4);
    putU32At(b, b.size() - 4, v);
}
size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

// spec §5: stride = base(code) + 4 * texcoord_count.
size_t specBaseFor(uint8_t code) {
    switch (code) {
        case 0: return 16;
        case 1: return 24;
        case 2: return 20;
        case 3: return 28;
        case 4: return 12;
        case 100: return 20;
        case 101: return 24;
        default: return 0;
    }
}

struct SynthVertex {
    float pos[3];
    uint8_t normal[4];   // UBYTE4N, fourth byte is real content, not a sign
    uint8_t tangent[4];  // fourth byte is 0 or 255 — the handedness sign
    uint8_t weights[4];  // 8-bit partition of unity, sums to ~255
    uint8_t indices[4];  // 255 = unused influence
    std::vector<int16_t> uv; // 2 per texcoord set, SIGNED, /1024
};

struct MeshOptions {
    uint16_t version = 9;
    bool breakLeadingCheck = false;
    bool breakTrailingCheck = false;
    bool wrongGLength = false;
    bool flagsBit2 = false;
    uint8_t indexElementSize = 2;
    // Bone palette (HANDOFF Sec9.62): u16 count at header +0x38, u8
    // entries immediately after the channel records. Empty = none.
    std::vector<uint8_t> bonePalette;
    // Declare the count but omit the bytes (a truncated file).
    bool omitPaletteBytes = false;
    // Palette SET descriptors: u16 count at header +0x48, then one
    // [u8 count][u8 start] pair per set right after the palette bytes
    // (unaligned). Empty = declare 0 sets.
    std::vector<std::pair<uint8_t, uint8_t>> bonePaletteSets;
    bool omitSetBytes = false;

    // §8.2 draw groups and ranges, plus the per-draw-range palette-set
    // array HANDOFF Sec9.63.10 decodes. Written from the stated layout:
    // groupCount at header +0x04, 0x30-byte group records 16-byte aligned
    // after the channel array (here: after the palette/set bytes), each
    // group's first u32 its range count; then 20-byte ranges in group
    // order; then - Sec9.63.10 - one 8-byte record per range, u32 set
    // index at +0x00; then the block's check value again.
    struct SynthRange {
        uint32_t materialId = 0;
        uint32_t startIndex = 0;
        uint32_t indexCount = 0;
        uint32_t minVertex = 0;
        uint32_t maxVertex = 0;
        uint32_t paletteSet = 0;
    };
    std::vector<std::vector<SynthRange>> drawGroups;
    // Declare the ranges but stop the file right after them, so the
    // per-range set array is not there at all.
    bool omitRangeSetArray = false;
    // Write byte +0x01 of every set record non-zero. Real files have
    // +0x01..+0x07 zero in 6,402/6,402 records, so this cannot be
    // distinguished from a u8-plus-padding reading by the population -
    // the reader takes the u32, and this fixture pins that choice: under
    // a u32 reading the value is huge and the array is REJECTED, under a
    // u8 reading it would be silently accepted.
    bool rangeSetHighByteNoise = false;
    // Second word of each 8-byte record (zero in every shipped record).
    // A decoy: changing it must not change what the reader returns.
    uint32_t rangeSetSecondWord = 0;
};

// Builds the c-file bytes and g-file bytes for one Mesh sub-block placed at
// offset 0 of the c-buffer, with bulk in the g-file (flags bit 0 set), so
// the g-segment base is 0 exactly as the parser expects.
void buildMesh(const std::vector<SynthVertex>& verts,
               const std::vector<uint16_t>& indices,
               uint8_t layoutCode, uint8_t texcoordCount,
               std::vector<uint8_t>& cOut, std::vector<uint8_t>& gOut,
               const MeshOptions& opt = MeshOptions{}) {
    const uint32_t kCheck = 0xC0FFEE01u;
    const size_t stride = specBaseFor(layoutCode) + 4u * texcoordCount;
    const size_t headerAt = 0x10;
    const size_t headerSize = 0x70;
    const size_t recordsAt = headerAt + headerSize;
    const uint32_t channelCount = 1;

    // ---- g-segment, spec §4 ----
    std::vector<uint8_t> g;
    appendU32(g, opt.breakLeadingCheck ? 0xDEADBEEFu : kCheck);
    g.resize(alignUp(g.size(), 16), 0);
    for (uint16_t idx : indices) {
        g.resize(g.size() + 2);
        putU16At(g, g.size() - 2, idx);
    }
    g.resize(alignUp(g.size(), 16), 0);
    const size_t channelDataAt = g.size();
    g.resize(g.size() + verts.size() * stride, 0);
    for (size_t v = 0; v < verts.size(); ++v) {
        const size_t at = channelDataAt + v * stride;
        const SynthVertex& sv = verts[v];
        if (layoutCode != 4) {
            for (int c = 0; c < 3; ++c) putF32At(g, at + static_cast<size_t>(c) * 4, sv.pos[c]);
            for (int c = 0; c < 4; ++c) g[at + 12 + static_cast<size_t>(c)] = sv.normal[c];
            if (layoutCode == 2 || layoutCode == 3 || layoutCode == 101)
                for (int c = 0; c < 4; ++c) g[at + 16 + static_cast<size_t>(c)] = sv.tangent[c];
            if (layoutCode == 3) {
                for (int c = 0; c < 4; ++c) g[at + 20 + static_cast<size_t>(c)] = sv.weights[c];
                for (int c = 0; c < 4; ++c) g[at + 24 + static_cast<size_t>(c)] = sv.indices[c];
            } else if (layoutCode == 1) {
                for (int c = 0; c < 4; ++c) g[at + 16 + static_cast<size_t>(c)] = sv.weights[c];
                for (int c = 0; c < 4; ++c) g[at + 20 + static_cast<size_t>(c)] = sv.indices[c];
            }
        }
        const size_t uvAt = at + specBaseFor(layoutCode);
        for (size_t t = 0; t < texcoordCount && t * 2 + 1 < sv.uv.size(); ++t) {
            putU16At(g, uvAt + t * 4 + 0, static_cast<uint16_t>(sv.uv[t * 2 + 0]));
            putU16At(g, uvAt + t * 4 + 2, static_cast<uint16_t>(sv.uv[t * 2 + 1]));
        }
    }
    g.resize(alignUp(g.size(), 4), 0);
    appendU32(g, opt.breakTrailingCheck ? 0xDEADBEEFu : kCheck);
    const uint32_t gLength = static_cast<uint32_t>(g.size());

    // ---- c-file: pre-header + header + channel records ----
    std::vector<uint8_t> c(recordsAt + channelCount * 24, 0);
    putU16At(c, 0x00, opt.version);
    putU32At(c, 0x04, kCheck);
    putU32At(c, 0x08, static_cast<uint32_t>(c.size()));
    putU32At(c, 0x0C, opt.wrongGLength ? gLength + 16 : gLength);

    uint8_t flags = 0x01; // bit 0: bulk lives in the paired g-file
    if (opt.flagsBit2) flags |= 0x04;
    c[headerAt + 0x00] = flags;
    putU32At(c, headerAt + 0x10, channelCount);
    putU32At(c, headerAt + 0x18, 0);
    putU32At(c, headerAt + 0x20, static_cast<uint32_t>(indices.size()));
    putU32At(c, headerAt + 0x28, 0);
    c[headerAt + 0x30] = opt.indexElementSize;

    putU32At(c, recordsAt + 0x00, static_cast<uint32_t>(verts.size()));
    c[recordsAt + 0x04] = static_cast<uint8_t>(specBaseFor(layoutCode)); // sizeA
    c[recordsAt + 0x05] = layoutCode;
    c[recordsAt + 0x06] = texcoordCount;
    c[recordsAt + 0x07] = static_cast<uint8_t>(4u * texcoordCount);      // sizeB

    if (!opt.bonePalette.empty()) {
        putU16At(c, headerAt + 0x38, static_cast<uint16_t>(opt.bonePalette.size()));
        if (!opt.omitPaletteBytes) {
            c.insert(c.end(), opt.bonePalette.begin(), opt.bonePalette.end());
            if (!opt.bonePaletteSets.empty()) {
                putU16At(c, headerAt + 0x48, static_cast<uint16_t>(opt.bonePaletteSets.size()));
                if (!opt.omitSetBytes) {
                    for (const auto& s : opt.bonePaletteSets) {
                        c.push_back(s.first);   // count
                        c.push_back(s.second);  // start offset into the palette
                    }
                }
            }
            putU32At(c, 0x08, static_cast<uint32_t>(c.size()));
        }
    }

    if (!opt.drawGroups.empty()) {
        putU32At(c, headerAt + 0x04, static_cast<uint32_t>(opt.drawGroups.size()));
        c.resize(alignUp(c.size(), 16), 0);
        for (const auto& group : opt.drawGroups) {
            size_t at = c.size();
            c.resize(at + 0x30, 0);
            putU32At(c, at, static_cast<uint32_t>(group.size()));
        }
        for (const auto& group : opt.drawGroups) {
            for (const auto& r : group) {
                size_t at = c.size();
                c.resize(at + 20, 0);
                putU32At(c, at + 0x00, r.materialId);
                putU32At(c, at + 0x04, r.startIndex);
                putU32At(c, at + 0x08, r.indexCount);
                putU32At(c, at + 0x0C, r.minVertex);
                putU32At(c, at + 0x10, r.maxVertex);
            }
        }
        if (!opt.omitRangeSetArray) {
            for (const auto& group : opt.drawGroups) {
                for (const auto& r : group) {
                    size_t at = c.size();
                    c.resize(at + 8, 0);
                    putU32At(c, at + 0x00, r.paletteSet);
                    if (opt.rangeSetHighByteNoise) c[at + 0x01] = 0x5A;
                    putU32At(c, at + 0x04, opt.rangeSetSecondWord);
                }
            }
            appendU32(c, kCheck); // the block's own trailing bookend
        }
        putU32At(c, 0x08, static_cast<uint32_t>(c.size()));
    }

    cOut = std::move(c);
    gOut = std::move(g);
}

std::vector<SynthVertex> sampleVerts() {
    return {
        // NEGATIVE in BOTH lanes. -10/1024 = -0.0098 is the median low end
        // the spec reports; a reader treating uv as unsigned turns it into
        // +63.99. Both u and v carry a negative deliberately: an earlier
        // version of this fixture put one only in v, and a mutation that
        // dropped the sign on the u lane alone went UNDETECTED. A property
        // has to be exercised in every lane that carries it.
        {{0.0f, 0.0f, 0.0f}, {255, 128, 128, 112}, {128, 255, 128, 0},
         {255, 0, 0, 0}, {7, 255, 255, 255}, {-3, -10}},
        {{1.0f, 0.0f, 0.0f}, {128, 255, 128, 40}, {255, 128, 128, 255},
         {128, 127, 0, 0}, {7, 9, 255, 255}, {1024, 0}},
        {{0.0f, 1.0f, 0.0f}, {128, 128, 255, 200}, {128, 128, 255, 0},
         {100, 100, 55, 0}, {2, 5, 9, 255}, {512, 512}},
        {{1.0f, 1.0f, 0.0f}, {255, 255, 128, 7}, {128, 255, 255, 255},
         {64, 64, 64, 63}, {0, 1, 2, 3}, {1024, 1024}},
    };
}

void testRoundTripAndStrideLaw() {
    std::vector<uint8_t> c, g;
    std::vector<uint16_t> idx = {0, 1, 2, 3};
    buildMesh(sampleVerts(), idx, 3, 1, c, g);

    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(
        vpp::ByteView(c.data(), c.size()), 0, vpp::ByteView(g.data(), g.size()));

    CHECK(m.checkValue() == 0xC0FFEE01u);
    CHECK(m.bulkInGFile());
    CHECK(m.indexCount() == 4);
    CHECK(m.indexElementSize() == 2);
    CHECK(m.indices().size() == 4);
    CHECK(m.channels().size() == 1);

    const auto& ch = m.channels()[0];
    CHECK(ch.elementCount == 4);
    CHECK(ch.layoutCode == 3);
    CHECK(ch.texcoordCount == 1);
    // spec §5: base(3) = 28, plus 4 per texcoord set.
    CHECK(ch.stride() == 32);
    CHECK(ch.predictedStride == 32);
    CHECK(ch.strideMatchesLaw);
}

// The slope is the genuinely predictive half of spec §5 (the bases are
// measured). Walk one code across several texcoord counts and check each
// increment costs exactly 4 bytes.
void testStrideLawSlope() {
    for (uint8_t code : {uint8_t(0), uint8_t(2), uint8_t(3)}) {
        size_t previous = 0;
        for (uint8_t tc = 0; tc <= 3; ++tc) {
            std::vector<uint8_t> c, g;
            std::vector<uint16_t> idx = {0, 1, 2};
            buildMesh(sampleVerts(), idx, code, tc, c, g);
            sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(
                vpp::ByteView(c.data(), c.size()), 0, vpp::ByteView(g.data(), g.size()));
            size_t s = m.channels()[0].stride();
            CHECK(s == specBaseFor(code) + 4u * tc);
            CHECK(m.channels()[0].strideMatchesLaw);
            if (tc > 0) CHECK(s - previous == 4);
            previous = s;
        }
    }
}

void testVertexDecode() {
    std::vector<uint8_t> c, g;
    std::vector<uint16_t> idx = {0, 1, 2, 3};
    std::vector<SynthVertex> verts = sampleVerts();
    buildMesh(verts, idx, 3, 1, c, g);
    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(
        vpp::ByteView(c.data(), c.size()), 0, vpp::ByteView(g.data(), g.size()));

    std::vector<sr3mesh::Vertex> out = m.decodeChannel(0);
    CHECK(out.size() == 4);

    CHECK(std::fabs(out[1].position[0] - 1.0f) < 1e-6f);
    CHECK(std::fabs(out[2].position[1] - 1.0f) < 1e-6f);

    // spec §6.2: UBYTE4N, (b/255)*2 - 1.
    CHECK(std::fabs(out[0].normal[0] - 1.0f) < 1e-5f);        // 255 -> +1
    CHECK(std::fabs(out[0].normal[1] - (128.0f / 255.0f * 2.0f - 1.0f)) < 1e-5f);
    // The normal's fourth byte is real content, NOT a sign — surfaced raw.
    CHECK(out[0].normalW == 112);
    CHECK(out[2].normalW == 200);

    // spec §6.2: the tangent's fourth byte is 0 or 255 — a handedness sign.
    CHECK(out[0].tangentW == 0);
    CHECK(out[1].tangentW == 255);
    CHECK(out[0].tangentHandedness != out[1].tangentHandedness);

    // spec §6.3: weights are 8-bit fixed point over 255.
    CHECK(std::fabs(out[0].blendWeights[0] - 1.0f) < 1e-5f);
    CHECK(std::fabs(out[1].blendWeights[0] - (128.0f / 255.0f)) < 1e-5f);
    CHECK(out[1].blendIndices[0] == 7);
    CHECK(out[1].blendIndices[1] == 9);

    // spec §6.5: int16 / 1024, SIGNED. -10 must read as -0.0098, not 63.99.
    // Asserted on BOTH lanes - see the fixture comment.
    CHECK(std::fabs(out[0].texcoords[0][0] - (-3.0f / 1024.0f)) < 1e-6f);
    CHECK(std::fabs(out[0].texcoords[0][1] - (-10.0f / 1024.0f)) < 1e-6f);
    CHECK(out[0].texcoords[0][0] < 0.0f);
    CHECK(out[0].texcoords[0][1] < 0.0f);
    CHECK(out[0].texcoordsRaw[0][0] == -3);
    CHECK(out[0].texcoordsRaw[0][1] == -10);
    CHECK(std::fabs(out[1].texcoords[0][0] - 1.0f) < 1e-6f);
}

// spec §6.3's controlled result: a weight lane is zero exactly when the
// corresponding index lane is 255 (70,054/70,054; control 0.1498). The
// fixture satisfies it by construction, so a reader reading the two fields
// from different offsets breaks the agreement.
void testWeightIndexLaneAgreement() {
    std::vector<uint8_t> c, g;
    std::vector<uint16_t> idx = {0, 1, 2, 3};
    buildMesh(sampleVerts(), idx, 3, 1, c, g);
    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(
        vpp::ByteView(c.data(), c.size()), 0, vpp::ByteView(g.data(), g.size()));

    long long agree = 0, total = 0;
    for (const auto& v : m.decodeChannel(0)) {
        for (int k = 0; k < 4; ++k) {
            ++total;
            bool zeroWeight = v.blendWeights[static_cast<size_t>(k)] <= 0.0f;
            bool sentinel = v.blendIndices[static_cast<size_t>(k)] == 255;
            if (zeroWeight == sentinel) ++agree;
        }
    }
    CHECK(total == 16);
    CHECK(agree == total);
}

// spec §8.1: triangle strips stitched with degenerate triangles. A strip
// 0,1,2,3 yields two triangles with alternating winding.
void testStripExpansion() {
    std::vector<uint8_t> c, g;
    std::vector<uint16_t> idx = {0, 1, 2, 3};
    buildMesh(sampleVerts(), idx, 3, 1, c, g);
    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(
        vpp::ByteView(c.data(), c.size()), 0, vpp::ByteView(g.data(), g.size()));

    sr3mesh::MeshBlock::DrawRange r;
    r.startIndex = 0;
    r.indexCount = 4;
    std::vector<uint32_t> tris = m.triangleListForRange(r);
    CHECK(tris.size() == 6);
    CHECK(tris[0] == 0 && tris[1] == 1 && tris[2] == 2);
    // odd step: winding flipped, so b and c swap
    CHECK(tris[3] == 1 && tris[4] == 3 && tris[5] == 2);
}

// spec §8.1: a degenerate triangle (a repeated index) is a stitch and must
// be dropped, not emitted as a zero-area triangle.
void testDegenerateStitchDropped() {
    std::vector<uint8_t> c, g;
    // 0,1,2, 2,2, 2,3,4 - the doubled 2 stitches two strips together
    std::vector<uint16_t> idx = {0, 1, 2, 2, 2, 2, 3, 4};
    std::vector<SynthVertex> verts = sampleVerts();
    verts.push_back(verts[3]);
    buildMesh(verts, idx, 3, 1, c, g);
    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(
        vpp::ByteView(c.data(), c.size()), 0, vpp::ByteView(g.data(), g.size()));

    sr3mesh::MeshBlock::DrawRange r;
    r.startIndex = 0;
    r.indexCount = 8;
    std::vector<uint32_t> tris = m.triangleListForRange(r);
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        CHECK(tris[t] != tris[t + 1]);
        CHECK(tris[t + 1] != tris[t + 2]);
        CHECK(tris[t] != tris[t + 2]);
    }
    CHECK(tris.size() < 6 * 3);
}

void testRejections() {
    std::vector<uint8_t> c, g;
    std::vector<uint16_t> idx = {0, 1, 2, 3};

    // spec §2: version reads 9. Anything else is not this block.
    MeshOptions ver;
    ver.version = 8;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, ver);
    CHECK_THROWS(sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                           vpp::ByteView(g.data(), g.size())));

    // spec §4: the leading check value must equal the block's own.
    MeshOptions lead;
    lead.breakLeadingCheck = true;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, lead);
    CHECK_THROWS(sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                           vpp::ByteView(g.data(), g.size())));

    // spec §4: and so must the trailing bookend. This is the half a reader
    // is most likely to skip, since the walk already "worked" by then.
    MeshOptions trail;
    trail.breakTrailingCheck = true;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, trail);
    CHECK_THROWS(sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                           vpp::ByteView(g.data(), g.size())));

    // spec §4: the walk must consume exactly the declared g-length.
    MeshOptions glen;
    glen.wrongGLength = true;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, glen);
    CHECK_THROWS(sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                           vpp::ByteView(g.data(), g.size())));

    // spec §2: flags bit 2 selects a multi-stream representation this
    // reader does not implement - it must refuse, not misparse.
    MeshOptions bit2;
    bit2.flagsBit2 = true;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, bit2);
    CHECK_THROWS(sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                           vpp::ByteView(g.data(), g.size())));

    // A g-buffer truncated below the declared length.
    buildMesh(sampleVerts(), idx, 3, 1, c, g);
    std::vector<uint8_t> shortG(g.begin(), g.begin() + static_cast<long>(g.size() / 2));
    CHECK_THROWS(sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                           vpp::ByteView(shortG.data(), shortG.size())));
}

// The alignment steps in spec §4 are easy to drop because a fixture whose
// sizes happen to be 16-aligned passes either way. Three indices is 6
// bytes, so the channel data must be padded to 16 before it starts.
void testAlignmentActuallyExercised() {
    std::vector<uint8_t> c, g;
    std::vector<uint16_t> idx = {0, 1, 2};
    CHECK((idx.size() * 2) % 16 != 0);
    buildMesh(sampleVerts(), idx, 3, 1, c, g);
    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(
        vpp::ByteView(c.data(), c.size()), 0, vpp::ByteView(g.data(), g.size()));
    CHECK(m.indices().size() == 3);
    std::vector<sr3mesh::Vertex> out = m.decodeChannel(0);
    CHECK(out.size() == 4);
    // If the align-to-16 were dropped, the channel would be read 10 bytes
    // early and the first position would not be the origin.
    CHECK(std::fabs(out[0].position[0]) < 1e-6f);
    CHECK(std::fabs(out[1].position[0] - 1.0f) < 1e-6f);
}

// Layout code 4 is position-only; it is the code that breaks the
// bit-flag reading of 0-3 (spec §5), so it is worth its own case.
void testPositionOnlyLayout() {
    std::vector<uint8_t> c, g;
    std::vector<uint16_t> idx = {0, 1, 2};
    buildMesh(sampleVerts(), idx, 4, 0, c, g);
    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(
        vpp::ByteView(c.data(), c.size()), 0, vpp::ByteView(g.data(), g.size()));
    CHECK(m.channels()[0].stride() == 12);
    CHECK(m.channels()[0].strideMatchesLaw);
    CHECK(!sr3mesh::layoutInfoFor(4).hasSkinning);
    CHECK(sr3mesh::layoutInfoFor(3).hasSkinning);
    CHECK(!sr3mesh::layoutInfoFor(0).hasSkinning);
}

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const sr3mesh::FormatError& ex) {
        std::cerr << "CHECK FAILED: " << name
                  << " rejected a valid fixture: " << ex.what() << "\n";
        ++g_failures;
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

} // namespace

// HANDOFF Sec9.62: the bone palette - u16 count at header +0x38, u8 rig
// bone indices immediately after the channel records. Built into the
// fixture from that statement of the layout, not from the parser.
void testBonePalette() {
    std::vector<uint16_t> idx = {0, 1, 2, 3};
    std::vector<uint8_t> c, g;

    // A brad-shaped list: ascending rig indices with gaps, one set.
    MeshOptions opt;
    opt.bonePalette = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 19, 20};
    opt.bonePaletteSets = {{uint8_t{18}, uint8_t{0}}};
    buildMesh(sampleVerts(), idx, 3, 1, c, g, opt);
    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                     vpp::ByteView(g.data(), g.size()));
    CHECK(m.bonePaletteDeclaredCount() == 18);
    CHECK(m.bonePalette() == opt.bonePalette);
    CHECK(m.bonePaletteSetCountDeclared() == 1);
    CHECK(m.bonePaletteSets().size() == 1 && m.bonePaletteSets()[0].count == 18 && m.bonePaletteSets()[0].start == 0);
    // The palette must not disturb anything already decoded from the block.
    CHECK(m.channels().size() == 1);
    CHECK(m.decodeChannel(0).size() == 4);
    CHECK(m.indices().size() == 4);

    // MUTATION 1: a reader that took either count from a neighbouring
    // header field would see these decoys. Poke the unused u16s at +0x3A
    // and +0x4A and re-parse: both counts must be unchanged.
    std::vector<uint8_t> decoy = c;
    putU16At(decoy, 0x10 + 0x3A, 77);
    putU16At(decoy, 0x10 + 0x4A, 99);
    sr3mesh::MeshBlock md = sr3mesh::MeshBlock::parse(vpp::ByteView(decoy.data(), decoy.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(md.bonePaletteDeclaredCount() == 18);
    CHECK(md.bonePalette() == opt.bonePalette);
    CHECK(md.bonePaletteSetCountDeclared() == 1);
    CHECK(md.bonePaletteSets().size() == 1 && md.bonePaletteSets()[0].count == 18);

    // Two sets (the 46/318 multi-set shape, e.g. angie {59,0},{13,59}):
    // here {10,0},{8,10}: 10 + 8 == 18, each run ascending on its own.
    MeshOptions two;
    two.bonePalette = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 3, 4, 11, 12, 13, 14, 15, 17};
    two.bonePaletteSets = {{uint8_t{10}, uint8_t{0}}, {uint8_t{8}, uint8_t{10}}};
    buildMesh(sampleVerts(), idx, 3, 1, c, g, two);
    sr3mesh::MeshBlock m2 = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(m2.bonePalette() == two.bonePalette);
    CHECK(m2.bonePaletteSetCountDeclared() == 2);
    CHECK(m2.bonePaletteSets().size() == 2);
    if (m2.bonePaletteSets().size() == 2) {
        CHECK(m2.bonePaletteSets()[0].count == 10 && m2.bonePaletteSets()[0].start == 0);
        CHECK(m2.bonePaletteSets()[1].count == 8 && m2.bonePaletteSets()[1].start == 10);
        // MUTATION 2: a reader that took each descriptor as one u16 SIZE
        // (the first reading tried on real data) would report the second
        // set's "size" as 8 + 10*256 = 2568, not 8 - and a reader with the
        // two bytes swapped would report count 10 / start 8 for set 1.
        CHECK(m2.bonePaletteSets()[1].count != 10);
    }

    // Set table declared but truncated: count kept, descriptors empty.
    MeshOptions ts;
    ts.bonePalette = {1, 2, 3};
    ts.bonePaletteSets = {{uint8_t{3}, uint8_t{0}}};
    ts.omitSetBytes = true;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, ts);
    sr3mesh::MeshBlock mts = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                       vpp::ByteView(g.data(), g.size()));
    CHECK(mts.bonePalette() == ts.bonePalette);
    CHECK(mts.bonePaletteSetCountDeclared() == 1);
    CHECK(mts.bonePaletteSets().empty());

    // Palette but NO set table declared (count 0 at +0x48): descriptors empty, count 0.
    MeshOptions noSets;
    noSets.bonePalette = {1, 2, 3};
    buildMesh(sampleVerts(), idx, 3, 1, c, g, noSets);
    sr3mesh::MeshBlock mns = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                       vpp::ByteView(g.data(), g.size()));
    CHECK(mns.bonePaletteSetCountDeclared() == 0);
    CHECK(mns.bonePaletteSets().empty());
    // Re-establish `m`'s fixture for the checks below.
    buildMesh(sampleVerts(), idx, 3, 1, c, g, opt);

    // MUTATION 2: a reader that started the entries one byte early or late
    // would report a shifted list. The fixture's first entry is 1 and its
    // last is 20; assert both ends explicitly.
    CHECK(!m.bonePalette().empty() && m.bonePalette().front() == 1 && m.bonePalette().back() == 20);

    // No palette declared: count 0, empty list.
    buildMesh(sampleVerts(), idx, 3, 1, c, g);
    sr3mesh::MeshBlock m0 = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(m0.bonePaletteDeclaredCount() == 0);
    CHECK(m0.bonePalette().empty());

    // Declared but truncated: the declared count is still reported and the
    // list is EMPTY - not a partial/garbage list of some other length. A
    // consumer compares the two (see mesh_block.h's bonePalette() comment).
    MeshOptions trunc;
    trunc.bonePalette = {1, 2, 3, 4};
    trunc.omitPaletteBytes = true;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, trunc);
    sr3mesh::MeshBlock mt = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(mt.bonePaletteDeclaredCount() == 4);
    CHECK(mt.bonePalette().empty());
}

// HANDOFF Sec9.63.10: the per-draw-range BONE PALETTE SET selector - an
// 8-byte record per draw range immediately after the 20-byte range
// records, u32 set index at +0x00. The fixture writes it from that
// statement of the layout; every expectation below is the literal value
// the fixture put there, and each rule carries a case that must FAIL.
void testDrawRangePaletteSets() {
    // 6 indices -> a strip the ranges can partition: [0,3) and [3,6).
    std::vector<uint16_t> idx = {0, 1, 2, 1, 2, 3};

    MeshOptions opt;
    opt.bonePalette = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 3, 4, 11, 12, 13, 14, 15, 17};
    opt.bonePaletteSets = {{uint8_t{10}, uint8_t{0}}, {uint8_t{8}, uint8_t{10}}};
    opt.drawGroups = {
        {{/*mat*/ 0, /*start*/ 0, /*count*/ 3, /*min*/ 0, /*max*/ 2, /*set*/ 0}},
        {{/*mat*/ 1, /*start*/ 3, /*count*/ 3, /*min*/ 1, /*max*/ 3, /*set*/ 1}},
    };
    std::vector<uint8_t> c, g;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, opt);
    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                     vpp::ByteView(g.data(), g.size()));
    CHECK(m.drawGroupsLocated());
    CHECK(m.drawGroups().size() == 2);
    CHECK(m.drawRangePaletteSets().size() == 2);
    if (m.drawRangePaletteSets().size() == 2) {
        // The fixture wrote 0 then 1, in FLAT file order (group 0's range,
        // then group 1's). A reader that read them per group, or reversed,
        // would give {1,0}.
        CHECK(m.drawRangePaletteSets()[0] == 0);
        CHECK(m.drawRangePaletteSets()[1] == 1);
    }

    // DECOY: the second u32 of each record is zero in every shipped record
    // (6,402/6,402) and is NOT the selector. Fill it and re-parse: the
    // answer must not move. A reader that took +0x04 would now see 7.
    MeshOptions decoy = opt;
    decoy.rangeSetSecondWord = 7;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, decoy);
    sr3mesh::MeshBlock md = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(md.drawRangePaletteSets().size() == 2);
    if (md.drawRangePaletteSets().size() == 2) {
        CHECK(md.drawRangePaletteSets()[0] == 0);
        CHECK(md.drawRangePaletteSets()[1] == 1);
    }

    // MUTATION: a record naming a set that does not exist. The array is
    // REJECTED WHOLE (left empty) rather than handed over with one bad
    // entry - a consumer must not have to filter it. Deliberately wrong
    // case: set 5 against a 2-set table.
    MeshOptions bad = opt;
    bad.drawGroups[1][0].paletteSet = 5;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, bad);
    sr3mesh::MeshBlock mb = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(mb.drawGroupsLocated());
    CHECK(mb.drawRangePaletteSets().empty());

    // The u8-vs-u32 question the shipped population cannot answer (+0x01
    // is zero in 6,402/6,402 records). This reader takes the u32, so a
    // non-zero +0x01 makes the value enormous and the array is rejected.
    // A u8 reading would silently accept it. Pinned here so the choice is
    // a decision on the record, not an accident.
    MeshOptions noise = opt;
    noise.rangeSetHighByteNoise = true;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, noise);
    sr3mesh::MeshBlock mn = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(mn.drawRangePaletteSets().empty());

    // Array absent entirely (file stops after the 20-byte ranges): empty,
    // non-fatal, and everything else still parses.
    MeshOptions missing = opt;
    missing.omitRangeSetArray = true;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, missing);
    sr3mesh::MeshBlock mm = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(mm.drawGroupsLocated());
    CHECK(mm.drawRangePaletteSets().empty());
    CHECK(mm.decodeChannel(0).size() == 4);

    // Single-set mesh: the array is present and all zero (272/272 measured),
    // so a consumer can use it unconditionally.
    MeshOptions one;
    one.bonePalette = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 19, 20};
    one.bonePaletteSets = {{uint8_t{18}, uint8_t{0}}};
    one.drawGroups = {
        {{0, 0, 3, 0, 2, 0}, {1, 3, 3, 1, 3, 0}},
    };
    buildMesh(sampleVerts(), idx, 3, 1, c, g, one);
    sr3mesh::MeshBlock m1 = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(m1.drawRangePaletteSets().size() == 2);
    if (m1.drawRangePaletteSets().size() == 2) {
        CHECK(m1.drawRangePaletteSets()[0] == 0);
        CHECK(m1.drawRangePaletteSets()[1] == 0);
    }
}

void testResolveVertexSetConflictsByDuplication() {
    // HANDOFF Sec9.68 (`reynolds`): reuses testDrawRangePaletteSets()'s
    // OWN fixture shape unchanged - idx={0,1,2, 1,2,3} already makes
    // range 0 (indices[0..2]={0,1,2}) and range 1 (indices[3..5]={1,2,3})
    // share vertices 1 and 2 despite being non-overlapping SLICES of the
    // index buffer, which is exactly the shape a real vertex/set conflict
    // has. Only the two ranges' own set assignment changes: range 0 -> set
    // 0 (unchanged), range 1 -> set 1 (was 1 already) - so this is the
    // SAME real file shape §9.63.10 already validated the raw field
    // decode against, now also exercised through the conflict resolver.
    std::vector<uint16_t> idx = {0, 1, 2, 1, 2, 3};
    MeshOptions opt;
    opt.bonePalette = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 3, 4, 11, 12, 13, 14, 15, 17};
    opt.bonePaletteSets = {{uint8_t{10}, uint8_t{0}}, {uint8_t{8}, uint8_t{10}}};
    opt.drawGroups = {
        {{/*mat*/ 0, /*start*/ 0, /*count*/ 3, /*min*/ 0, /*max*/ 2, /*set*/ 0}},
        {{/*mat*/ 1, /*start*/ 3, /*count*/ 3, /*min*/ 1, /*max*/ 3, /*set*/ 1}},
    };
    std::vector<uint8_t> c, g;
    buildMesh(sampleVerts(), idx, 3, 1, c, g, opt);
    sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                     vpp::ByteView(g.data(), g.size()));
    CHECK(m.drawGroupsLocated());
    CHECK(m.drawGroups().size() == 2);

    // Group 0 alone (groupIndex=0): only range 0 (set 0) is considered, no
    // second range to conflict with it - must be usable, zero duplicates,
    // and must agree EXACTLY with assignPaletteSetsPerVertex() (the two
    // functions must never drift apart where there is nothing to resolve).
    {
        sr3rig::VertexDuplicationResult r0 =
            sr3rig::resolveVertexSetConflictsByDuplication(m, sampleVerts().size(), 0);
        CHECK(r0.usable);
        CHECK(r0.duplicatesCreated == 0);
        CHECK(r0.duplicateSourceIndex.empty());
        sr3rig::PaletteSetAssignment a0 = sr3rig::assignPaletteSetsPerVertex(m, sampleVerts().size(), 0);
        CHECK(a0.usable);
        CHECK(a0.conflicts == 0);
        CHECK(r0.setOfVertex.size() == a0.setOfVertex.size());
        for (size_t i = 0; i < r0.setOfVertex.size() && i < a0.setOfVertex.size(); ++i) {
            CHECK(r0.setOfVertex[i] == a0.setOfVertex[i]);
        }
    }

    // groupIndex=-1 is out of this function's contract (only >=0 groups,
    // unlike assignPaletteSetsPerVertex's -1-means-all) - not exercised
    // here; the real conflict is groupIndex covering BOTH ranges. This
    // fixture's draw groups are ALREADY two separate groups (0 and 1), so
    // to get both ranges considered TOGETHER (as one drawn LOD does when
    // it has multiple ranges) this test instead builds a single-group,
    // two-range fixture below - the shape reynolds itself actually has
    // (both conflicting ranges in the SAME group 0).
    MeshOptions conflictOpt = opt;
    conflictOpt.drawGroups = {
        {{/*mat*/ 0, /*start*/ 0, /*count*/ 3, /*min*/ 0, /*max*/ 2, /*set*/ 0},
         {/*mat*/ 1, /*start*/ 3, /*count*/ 3, /*min*/ 1, /*max*/ 3, /*set*/ 1}},
    };
    buildMesh(sampleVerts(), idx, 3, 1, c, g, conflictOpt);
    sr3mesh::MeshBlock mc = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                      vpp::ByteView(g.data(), g.size()));
    CHECK(mc.drawGroupsLocated());
    CHECK(mc.drawGroups().size() == 1);
    CHECK(mc.drawGroups()[0].size() == 2);

    // The OLD function must refuse - this is precisely the conflict shape
    // it exists to refuse on.
    sr3rig::PaletteSetAssignment refused =
        sr3rig::assignPaletteSetsPerVertex(mc, sampleVerts().size(), 0);
    CHECK(!refused.usable);
    CHECK(refused.conflicts > 0);

    // The NEW function must resolve it instead: vertices 1 and 2 are each
    // claimed first by range 0 (set 0), then range 1 (set 1) wants them
    // too - two DISTINCT duplicates (one per vertex, since two different
    // original indices need a set-1 copy), both attributed to range 1's
    // own redirect map, range 0's redirect map empty (it went first).
    sr3rig::VertexDuplicationResult res =
        sr3rig::resolveVertexSetConflictsByDuplication(mc, sampleVerts().size(), 0);
    CHECK(res.usable);
    CHECK(res.duplicatesCreated == 2);
    CHECK(res.duplicateSourceIndex.size() == 2);
    CHECK(res.duplicateSetOfVertex.size() == 2);
    for (uint8_t s : res.duplicateSetOfVertex) CHECK(s == 1);
    // Both duplicate sources are from {1, 2} (order not asserted - a map
    // iteration/insertion-order detail, not a contract).
    for (uint32_t src : res.duplicateSourceIndex) CHECK(src == 1 || src == 2);
    CHECK(res.duplicateSourceIndex[0] != res.duplicateSourceIndex[1]);
    CHECK(res.redirectPerRange.size() == 2);
    CHECK(res.redirectPerRange[0].empty());
    CHECK(res.redirectPerRange[1].size() == 2);
    CHECK(res.redirectPerRange[1].count(1) == 1);
    CHECK(res.redirectPerRange[1].count(2) == 1);
    // The redirected GPU indices are the new duplicate slots, >= the
    // original vertex count, and distinct from each other.
    const size_t original = sampleVerts().size();
    uint32_t r1 = res.redirectPerRange[1].at(1);
    uint32_t r2 = res.redirectPerRange[1].at(2);
    CHECK(r1 >= original && r2 >= original && r1 != r2);
    // setOfVertex (the ORIGINAL vertex count) still reflects the FIRST
    // claim (set 0 for 0/1/2), unchanged by the conflict resolution - the
    // duplicates carry set 1, not the originals.
    CHECK(res.setOfVertex.size() == original);
    if (res.setOfVertex.size() > 2) {
        CHECK(res.setOfVertex[0] == 0);
        CHECK(res.setOfVertex[1] == 0);
        CHECK(res.setOfVertex[2] == 0);
    }

    // MUTATION: a structurally-refusing case (no bone palette sets at all)
    // must refuse here too, not silently "resolve" zero sets into
    // something usable - same contract as assignPaletteSetsPerVertex().
    MeshOptions noSets = conflictOpt;
    noSets.bonePaletteSets.clear();
    buildMesh(sampleVerts(), idx, 3, 1, c, g, noSets);
    sr3mesh::MeshBlock mNoSets = sr3mesh::MeshBlock::parse(vpp::ByteView(c.data(), c.size()), 0,
                                                           vpp::ByteView(g.data(), g.size()));
    sr3rig::VertexDuplicationResult resNoSets =
        sr3rig::resolveVertexSetConflictsByDuplication(mNoSets, sampleVerts().size(), 0);
    CHECK(!resNoSets.usable);
    CHECK(!resNoSets.problem.empty());
}

// fuzz/regressions/mesh reproducer (fuzzer-mutated from this suite's own
// synthetic seeds, no game data), in fuzz_mesh.cpp's input layout:
// u32 cLength, u32 meshOffset, u32 gSegmentOffset, u32 headerDisplacement,
// then c-content, then g-content.
static const unsigned char kFuzzMeshZeroStride[] = {
    0x4c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x01, 0xee, 0xff, 0xc0, 0x4c, 0x01, 0x00, 0x00, 0xa4, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe9,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x1c, 0x03, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xf7, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x73, 0x5f, 0x74, 0x79, 0x70, 0x65, 0x5f, 0x69, 0x6e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xee, 0xff, 0xc0, 0x01, 0xee, 0xff, 0xc0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x02, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x80, 0x80, 0x70, 0x80, 0xff, 0x80, 0x00,
    0xff, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xfd, 0xff, 0xf6, 0xff, 0x00, 0x00, 0x80, 0x3f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x80, 0x28, 0xff, 0x80, 0x80, 0xff,
    0x80, 0x7f, 0x00, 0x00, 0x07, 0x09, 0xff, 0xff, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0xff, 0xc8, 0x80, 0x80, 0xff, 0x00,
    0x64, 0x64, 0x37, 0x00, 0x02, 0x05, 0x09, 0xff, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x80, 0x3f,
    0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x80, 0x07, 0x80, 0xff, 0xff, 0xff,
    0x40, 0x40, 0x40, 0x3f, 0x00, 0x01, 0x02, 0x03, 0x00, 0x04, 0x00, 0x04, 0x01, 0xee, 0xff, 0xc0
};

int main() {
    // --- Fuzz regression (fuzz/regressions/mesh): a channel whose stride is
    // shorter than its layout's fixed fields (here 0) and whose element count
    // is huge must be refused, not reserved for / looped over.
    {
        const unsigned char* d = kFuzzMeshZeroStride;
        auto u32 = [](const unsigned char* p) { uint32_t v; std::memcpy(&v, p, 4); return v; };
        uint32_t cLen = u32(d), meshOffset = u32(d + 4), gOff = u32(d + 8), disp = u32(d + 12);
        size_t rest = sizeof(kFuzzMeshZeroStride) - 16;
        if (cLen > rest) cLen = static_cast<uint32_t>(rest);
        bool badAlloc = false, parsed = false, refused = false;
        {
            allocguard::AllocCap cap(256u << 20);
            try {
                auto m = sr3mesh::MeshBlock::parse(sr3mesh::ByteView(d + 16, cLen), meshOffset,
                                                   sr3mesh::ByteView(d + 16 + cLen, rest - cLen), gOff, disp);
                parsed = true;
                for (size_t i = 0; i < m.channels().size(); ++i) {
                    try {
                        (void)m.decodeChannel(i);
                    } catch (const sr3mesh::FormatError&) {
                        refused = true;
                    } catch (const std::out_of_range&) {
                    }
                }
            } catch (const std::bad_alloc&) {
                badAlloc = true;
            } catch (const std::exception&) {
            }
        }
        CHECK(!badAlloc);
        CHECK(parsed);   // the reproducer parses; the bug was in decodeChannel
        CHECK(refused);  // and its zero-stride channel is refused
    }

    run("testRoundTripAndStrideLaw", testRoundTripAndStrideLaw);
    run("testBonePalette", testBonePalette);
    run("testDrawRangePaletteSets", testDrawRangePaletteSets);
    run("testResolveVertexSetConflictsByDuplication", testResolveVertexSetConflictsByDuplication);
    run("testStrideLawSlope", testStrideLawSlope);
    run("testVertexDecode", testVertexDecode);
    run("testWeightIndexLaneAgreement", testWeightIndexLaneAgreement);
    run("testStripExpansion", testStripExpansion);
    run("testDegenerateStitchDropped", testDegenerateStitchDropped);
    run("testRejections", testRejections);
    run("testAlignmentActuallyExercised", testAlignmentActuallyExercised);
    run("testPositionOnlyLayout", testPositionOnlyLayout);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic mesh/vertex-format tests passed.\n";
    return 0;
}
