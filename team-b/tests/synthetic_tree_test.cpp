// Synthetic tests for the .csrt_pc/.gsrt_pc tree reader (spec-tree-format.md).
// Builds small buffers from scratch using only spec-confirmed facts: the
// shared material-reference block, the mandatory strictly-greater 16-byte
// pad (spec-geometry-format.md Sec3.1.1), the fixed 0x208-byte 'TREE'
// block, the fixed 0x170-byte geometry sub-structure, the material-set
// block (same inline-material routine as spec-foliage-format.md Sec6), the
// generic index list, up to 4 LOD-gated g-backed Mesh sub-blocks, the
// three opaque runtime arrays, and collision capsules - mirroring
// src/tree.cpp's own parse() step for step, the same way
// synthetic_clmesh_test.cpp's buildClmesh() mirrors src/level_mesh.cpp.
//
// The wind-state object, the LOD distances and the LOD / steady-state
// functions (spec-tree-format.md Sec4, Sec13.2-Sec13.5.2, re-synced
// 2026-09-20) are tested from the SPEC TEXT ONLY: every offset below is a
// literal taken from the spec's tables (float index k at block offset
// 0x38 + 4k, the runtime region from +0xD0, the four LOD distances at
// geometry +0xE0..+0xEC, derived +0xF0/+0xF4) and every expected value is
// computed from the spec's stated formulas by hand, never from the reader's
// own source or constants. The reader-side constants are used only for the
// pre-existing structural walk.

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "sr3geometry/material_block.h"
#include "sr3tree/tree.h"

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

#define CHECK_THROWS(expr)                                                  \
    do {                                                                     \
        bool threw = false;                                                  \
        try {                                                                \
            (void)(expr);                                                    \
        } catch (const sr3tree::FormatError&) {                              \
            threw = true;                                                    \
        }                                                                    \
        if (!threw) {                                                        \
            std::cerr << "CHECK_THROWS FAILED: " #expr " at " __FILE__ ":"   \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void appendU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }

void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendF32(std::vector<uint8_t>& b, float v) {
    uint32_t raw;
    static_assert(sizeof(raw) == sizeof(v), "f32 must be 4 bytes");
    std::memcpy(&raw, &v, sizeof(raw));
    appendU32(b, raw);
}

void appendCString(std::vector<uint8_t>& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0x00);
}

void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void putF32At(std::vector<uint8_t>& b, size_t off, float v) {
    uint32_t raw;
    std::memcpy(&raw, &v, sizeof(raw));
    putU32At(b, off, raw);
}

void padTo(std::vector<uint8_t>& b, size_t target) {
    while (b.size() < target) b.push_back(0x00);
}

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

vpp::ByteView view(const std::vector<uint8_t>& b) {
    return vpp::ByteView(b.data(), b.size());
}

// One real g-backed Mesh sub-block, version 9, one position-only channel.
// `headerAt` is computed from the TRUE anchor alignment (mirrors
// src/tree.cpp's own headerDisplacement fix and src/zone_geometry.cpp's
// established rule), not hardcoded - so this builder can place a SECOND
// back-to-back block correctly regardless of where the first one's own
// length landed.
void appendMeshSubBlock(std::vector<uint8_t>& c, std::vector<uint8_t>& g, uint32_t checkValue,
                        const std::vector<std::array<float, 3>>& positions) {
    const size_t anchor = c.size();
    const size_t headerAt = alignUp(anchor + 16, 8) - anchor;
    const size_t headerSize = 0x70;
    const size_t recordsAt = headerAt + headerSize;
    const uint32_t channelCount = 1;
    const size_t stride = 12; // layout code 4, position only

    std::vector<uint8_t> block(recordsAt + channelCount * 24, 0x00);
    putU32At(block, 0x00, 9);
    putU32At(block, 0x04, checkValue);
    block[headerAt + 0x00] = 0x01; // bulk in the g-file
    putU32At(block, headerAt + 0x10, channelCount);
    putU32At(block, headerAt + 0x20, 3); // index count
    block[headerAt + 0x30] = 2;
    putU32At(block, recordsAt + 0x00, static_cast<uint32_t>(positions.size()));
    block[recordsAt + 0x04] = static_cast<uint8_t>(stride);
    block[recordsAt + 0x05] = 4;
    block[recordsAt + 0x06] = 0;
    block[recordsAt + 0x07] = 0;

    const size_t gStart = g.size(); // true absolute start - purely additive chain, no rounding
    auto segAlign16 = [gStart](size_t cur) { return alignUp(gStart + cur, 16) - gStart; };

    std::vector<uint8_t> seg;
    appendU32(seg, checkValue);
    seg.resize(segAlign16(seg.size()), 0x00);
    for (int i = 0; i < 3; ++i) {
        seg.resize(seg.size() + 2);
        seg[seg.size() - 2] = static_cast<uint8_t>(i);
        seg[seg.size() - 1] = 0;
    }
    seg.resize(segAlign16(seg.size()), 0x00);
    const size_t channelAt = seg.size();
    seg.resize(seg.size() + positions.size() * stride, 0x00);
    for (size_t v = 0; v < positions.size(); ++v) {
        for (size_t k = 0; k < 3; ++k) {
            uint32_t bits = 0;
            std::memcpy(&bits, &positions[v][k], 4);
            putU32At(seg, channelAt + v * stride + k * 4, bits);
        }
    }
    seg.resize(alignUp(seg.size(), 4), 0x00);
    appendU32(seg, checkValue);

    block.resize(block.size() + 4, 0x00);
    putU32At(block, block.size() - 4, checkValue);

    putU32At(block, 0x08, static_cast<uint32_t>(block.size()));
    putU32At(block, 0x0C, static_cast<uint32_t>(seg.size()));

    c.insert(c.end(), block.begin(), block.end());
    g.insert(g.end(), seg.begin(), seg.end());
}

struct SyntheticBinding {
    uint32_t nameOffset = 0;
    uint32_t slotHash = 0;
};

struct SyntheticMaterial {
    uint32_t shaderHash = 0;
    uint32_t variantHash = 0;
    uint32_t flags = 0;
    std::vector<SyntheticBinding> bindings;
    std::vector<uint32_t> constantHashes;
    std::vector<std::array<float, 4>> vec4s;
};

std::vector<uint32_t> computeNameOffsets(const std::vector<std::string>& names) {
    std::vector<uint32_t> offsets;
    uint32_t at = 0;
    for (const auto& n : names) {
        offsets.push_back(at);
        at += static_cast<uint32_t>(n.size()) + 1;
    }
    return offsets;
}

struct TreeBuildOptions {
    std::vector<std::string> materialRefNames = {"bark_01_d.tga"};
    std::vector<SyntheticMaterial> materials;
    std::vector<std::string> matSetNames; // material-set block's OWN name table
    std::array<bool, 4> lodPresent = {true, false, false, false};
    std::vector<std::vector<std::array<float, 3>>> lodPositions; // one entry per present slot, in slot order
    uint32_t collisionCapsuleCount = 0;
    uint32_t sixteenByteRecordCount = 0;
    bool mutateTreeMagic = false;
    bool mutateTreeVersionTooLow = false;
    bool mutateLodSlotCountWrong = false;
    bool mutateMaterialCountMismatch = false;

    // Spec Sec13.2: when non-empty (exactly 0x26 entries), float index k
    // (k = 0x00..0x25) of the wind-state object is written at its LITERAL spec
    // offset 0x38 + 4k of the 'TREE' block.
    std::vector<float> windFloats;
    // Extra raw dwords written at LITERAL 'TREE'-block offsets (block offset, value),
    // used to probe the file-loaded / runtime boundary at +0xD0.
    std::vector<std::pair<size_t, uint32_t>> treeBlockDwords;
    // Spec Sec13.5 / Sec13.5.2: geometry sub-structure +0xE0..+0xEC (A, B, C, D)
    // and +0xF0 / +0xF4 (derived at load, zero on disk).
    std::array<float, 4> lodDistances = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 2> lodDerived = {0.0f, 0.0f};
};

struct TreeLayout {
    size_t treeBlockAt = 0;
    size_t geometryAt = 0;
    std::vector<size_t> meshAt;
    size_t capsulesAt = 0;
};

std::vector<uint8_t> buildTree(const TreeBuildOptions& opt, TreeLayout& lay, std::vector<uint8_t>& g) {
    std::vector<uint8_t> b;

    // --- Shared material-reference block. ---
    appendU32(b, sr3geometry::kMaterialBlockMagic);
    appendU32(b, 0); // name-table length placeholder
    appendU32(b, 0);
    appendU32(b, static_cast<uint32_t>(opt.materialRefNames.size()));
    b.insert(b.end(), 16, 0x00);
    size_t nameTableStart = b.size();
    for (const auto& n : opt.materialRefNames) appendCString(b, n);
    putU32At(b, 0x04, static_cast<uint32_t>(b.size() - nameTableStart));

    // --- Mandatory strictly-greater 16-byte pad. ---
    size_t base = b.size() / 16 * 16 + 16;
    padTo(b, base);

    // --- 'TREE' block, fixed 0x208 bytes. ---
    lay.treeBlockAt = b.size();
    b.insert(b.end(), sr3tree::kTreeBlockSize, 0x00);
    putU32At(b, lay.treeBlockAt + 0x00,
             opt.mutateTreeMagic ? 0xDEADBEEFu : sr3tree::kTreeMagic);
    putU32At(b, lay.treeBlockAt + 0x04, opt.mutateTreeVersionTooLow ? 0xCBu : 0xCCu);
    putU32At(b, lay.treeBlockAt + sr3tree::kTreeCapsuleCountOffset, opt.collisionCapsuleCount);
    // Wind-state object (+0x38..+0x207): zero unless a test asks otherwise
    // (spec Sec13.2 - zero runtime region on disk, 11/11).
    for (size_t k = 0; k < opt.windFloats.size(); ++k) {
        putF32At(b, lay.treeBlockAt + 0x38 + 4 * k, opt.windFloats[k]);
    }
    for (const auto& pr : opt.treeBlockDwords) putU32At(b, lay.treeBlockAt + pr.first, pr.second);
    padTo(b, alignUp(b.size(), 16));

    // --- Geometry sub-structure, fixed 0x170 bytes. ---
    lay.geometryAt = b.size();
    b.insert(b.end(), sr3tree::kGeometrySize, 0x00);
    const uint32_t lodSlotCountOnDisk = opt.mutateLodSlotCountWrong ? 3u : 4u;
    putU32At(b, lay.geometryAt + sr3tree::kGeoLodSlotCountOffset, lodSlotCountOnDisk);
    const uint32_t materialCountForGeometry =
        opt.mutateMaterialCountMismatch ? static_cast<uint32_t>(opt.materials.size()) + 1
                                        : static_cast<uint32_t>(opt.materials.size());
    putU32At(b, lay.geometryAt + sr3tree::kGeoMaterialCountOffset, materialCountForGeometry);
    putU32At(b, lay.geometryAt + sr3tree::kGeoSixteenByteCountOffset, opt.sixteenByteRecordCount);
    for (size_t i = 0; i < 4; ++i) putF32At(b, lay.geometryAt + 0xE0 + 4 * i, opt.lodDistances[i]);
    for (size_t i = 0; i < 2; ++i) putF32At(b, lay.geometryAt + 0xF0 + 4 * i, opt.lodDerived[i]);
    padTo(b, alignUp(b.size(), 8));

    // --- Material-set block. ---
    size_t H = b.size();
    b.insert(b.end(), sr3tree::kMatSetHeaderSize, 0x00);
    putU32At(b, H + sr3tree::kMatSetCountOffset, static_cast<uint32_t>(opt.materials.size()));

    size_t matNameTableStart = alignUp(H + sr3tree::kMatSetHeaderSize, 16) + 1;
    padTo(b, matNameTableStart);
    size_t matNameStart = b.size();
    for (const auto& n : opt.matSetNames) appendCString(b, n);
    putU32At(b, H + sr3tree::kMatSetNameTableLengthOffset,
             static_cast<uint32_t>(b.size() - matNameStart));

    padTo(b, alignUp(b.size(), 8));
    b.insert(b.end(), opt.materials.size() * sr3tree::kMatSetSlotStride, 0x00);

    for (const auto& m : opt.materials) {
        size_t pos = b.size();
        appendU32(b, 0); // declaredSize placeholder

        size_t hdr = alignUp(pos + 4, 8);
        padTo(b, hdr);
        appendU32(b, m.shaderHash);
        appendU32(b, m.variantHash);
        appendU32(b, m.flags);
        appendU16(b, static_cast<uint16_t>(m.bindings.size()));
        appendU8(b, static_cast<uint8_t>(m.constantHashes.size()));
        appendU8(b, static_cast<uint8_t>(m.vec4s.size()));
        padTo(b, hdr + sr3tree::kMatRecordHeaderSize);

        padTo(b, alignUp(b.size(), 4));
        for (const auto& tb : m.bindings) {
            appendU32(b, tb.nameOffset);
            appendU32(b, tb.slotHash);
            appendU16(b, 0);
            appendU16(b, 0);
        }

        padTo(b, alignUp(b.size(), 4));
        for (uint32_t h : m.constantHashes) appendU32(b, h);

        padTo(b, alignUp(b.size(), 16));
        for (const auto& v : m.vec4s) {
            for (float f : v) appendF32(b, f);
        }

        putU32At(b, pos, static_cast<uint32_t>(b.size() - pos));
    }

    // --- Index list: identity 0..materialCount-1. ---
    padTo(b, alignUp(b.size(), 8));
    size_t idxAt = b.size();
    b.insert(b.end(), sr3tree::kIndexListHeaderSize, 0x00);
    putU32At(b, idxAt + sr3tree::kIndexListCountOffset, static_cast<uint32_t>(opt.materials.size()));
    for (uint32_t i = 0; i < opt.materials.size(); ++i) appendU32(b, i);

    // --- LOD presence entries: 4 x 8. ---
    padTo(b, alignUp(b.size(), 8));
    size_t presenceBase = b.size();
    b.insert(b.end(), 4 * sr3tree::kPresenceEntrySize, 0x00);
    for (size_t i = 0; i < 4; ++i) {
        putU32At(b, presenceBase + i * sr3tree::kPresenceEntrySize, opt.lodPresent[i] ? 1u : 0u);
    }

    // --- Mesh sub-blocks, one per present slot, back-to-back. ---
    size_t posIdx = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (!opt.lodPresent[i]) continue;
        lay.meshAt.push_back(b.size());
        const std::vector<std::array<float, 3>>& positions =
            posIdx < opt.lodPositions.size() ? opt.lodPositions[posIdx]
                                             : std::vector<std::array<float, 3>>{{0, 0, 0}};
        appendMeshSubBlock(b, g, 0xC0FFEE00u + static_cast<uint32_t>(posIdx), positions);
        ++posIdx;
    }

    // --- Runtime arrays: materialCount x 8, then 4 x 8, then n16 x 16. ---
    padTo(b, alignUp(b.size(), 8));
    b.insert(b.end(), opt.materials.size() * 8, 0x00);
    b.insert(b.end(), 4 * 8, 0x00);
    b.insert(b.end(), opt.sixteenByteRecordCount * 16, 0x00);

    // --- Collision capsules. ---
    padTo(b, alignUp(b.size(), 16));
    lay.capsulesAt = b.size();
    for (uint32_t i = 0; i < opt.collisionCapsuleCount; ++i) {
        size_t at = b.size();
        b.insert(b.end(), sr3tree::kCapsuleSize, 0x00);
        putU32At(b, at + 0x00, 0x11111111u + i); // header bytes, OPEN, just a marker
        float sx = static_cast<float>(i) + 0.5f;
        uint32_t bits;
        std::memcpy(&bits, &sx, 4);
        putU32At(b, at + sr3tree::kCapsuleStartOffset, bits);
        float radius = 0.25f + static_cast<float>(i);
        std::memcpy(&bits, &radius, 4);
        putU32At(b, at + sr3tree::kCapsuleRadiusOffset, bits);
    }

    return b;
}

} // namespace

int main() {
    // --- Well-formed, single-LOD-slot case. ---
    {
        std::vector<std::string> names = {"bark_01_d.tga", "bark_01_n.tga"};
        auto off = computeNameOffsets(names);

        TreeBuildOptions opt;
        opt.matSetNames = names;
        SyntheticMaterial m;
        m.shaderHash = 0x967A81C0u;
        m.variantHash = 7;
        m.flags = 0x40;
        m.bindings.push_back({off[0], 0x69B48F91u});
        m.bindings.push_back({off[1], 0x2808EB90u});
        m.constantHashes = {0xAAAAAAAAu};
        m.vec4s.push_back({1.0f, 1.0f, 1.0f, 1.0f});
        opt.materials.push_back(m);
        opt.lodPresent = {true, false, false, false};
        opt.lodPositions.push_back(
            {{{-1.0f, -1.0f, -1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 2.0f, 0.0f}}});
        opt.collisionCapsuleCount = 1;

        TreeLayout lay;
        std::vector<uint8_t> g;
        std::vector<uint8_t> c = buildTree(opt, lay, g);

        sr3tree::Tree t = sr3tree::Tree::parse(view(c), view(g));
        CHECK(t.treeBlock().version == 0xCC);
        CHECK(t.treeBlock().collisionCapsuleCount == 1);
        CHECK(t.geometry().lodSlotCount == 4);
        CHECK(t.materialCount() == 1);
        CHECK(t.materials().size() == 1);
        CHECK(t.materials()[0].shaderHash == 0x967A81C0u);
        CHECK(t.materials()[0].textureBindings.size() == 2);
        CHECK(t.materials()[0].textureBindings[0].name == "bark_01_d.tga");
        CHECK(t.materials()[0].textureBindings[1].name == "bark_01_n.tga");
        CHECK(t.materialIndexList().size() == 1 && t.materialIndexList()[0] == 0);
        CHECK(t.presentLodCount() == 1);
        CHECK(t.lodSlots()[0].present);
        CHECK(!t.lodSlots()[1].present);
        CHECK(t.lodSlots()[0].mesh.checkValue() == 0xC0FFEE00u);
        CHECK(t.lodSlots()[0].mesh.bulkInGFile());
        CHECK(t.collisionCapsules().size() == 1);
        CHECK(std::fabs(t.collisionCapsules()[0].start[0] - 0.5f) < 1e-6f);
        CHECK(std::fabs(t.collisionCapsules()[0].radius - 0.25f) < 1e-6f);
    }

    // --- Zero LOD slots present, zero materials, zero capsules: must
    // parse cleanly, not throw. ---
    {
        TreeBuildOptions opt;
        opt.lodPresent = {false, false, false, false};
        TreeLayout lay;
        std::vector<uint8_t> g;
        std::vector<uint8_t> c = buildTree(opt, lay, g);
        sr3tree::Tree t = sr3tree::Tree::parse(view(c), view(g));
        CHECK(t.materialCount() == 0);
        CHECK(t.presentLodCount() == 0);
        CHECK(t.collisionCapsules().empty());
    }

    // --- Multiple present LOD slots, exercising the back-to-back Mesh
    // sub-block chain and its headerDisplacement handling (HANDOFF
    // Sec9.66's lesson, applied here from the start rather than found
    // later): the second slot's own cursor is NOT guaranteed 8-aligned. ---
    {
        std::vector<std::string> names = {"a.tga"};
        auto off = computeNameOffsets(names);
        TreeBuildOptions opt;
        opt.matSetNames = names;
        SyntheticMaterial m;
        m.shaderHash = 1;
        m.bindings.push_back({off[0], 0x69B48F91u});
        opt.materials.push_back(m);
        opt.lodPresent = {true, true, false, true};
        opt.lodPositions.push_back({{{0, 0, 0}}, {{1, 0, 0}}});
        opt.lodPositions.push_back({{{0, 0, 0}}, {{2, 0, 0}}, {{0, 2, 0}}});
        opt.lodPositions.push_back({{{0, 0, 0}}, {{3, 0, 0}}});

        TreeLayout lay;
        std::vector<uint8_t> g;
        std::vector<uint8_t> c = buildTree(opt, lay, g);
        sr3tree::Tree t = sr3tree::Tree::parse(view(c), view(g));
        CHECK(t.presentLodCount() == 3);
        CHECK(t.lodSlots()[0].present && t.lodSlots()[1].present && !t.lodSlots()[2].present &&
              t.lodSlots()[3].present);
        CHECK(t.lodSlots()[0].mesh.checkValue() == 0xC0FFEE00u);
        CHECK(t.lodSlots()[1].mesh.checkValue() == 0xC0FFEE01u);
        CHECK(t.lodSlots()[3].mesh.checkValue() == 0xC0FFEE02u);
        // The whole point of this case: at least one later slot's own
        // Mesh header must have landed on a non-8-aligned cursor, or this
        // test cannot distinguish a correct headerDisplacement from a
        // hardcoded one.
        bool anyMisaligned = false;
        for (size_t i = 1; i < lay.meshAt.size(); ++i) {
            if (lay.meshAt[i] % 8 != 0) anyMisaligned = true;
        }
        CHECK(anyMisaligned);
    }

    // =====================================================================
    // Spec-derived tests (spec-tree-format.md Sec4, Sec13.2 - Sec13.5.2).
    // =====================================================================
    auto parseOpt = [](const TreeBuildOptions& opt) {
        TreeLayout lay;
        std::vector<uint8_t> g;
        std::vector<uint8_t> c = buildTree(opt, lay, g);
        return sr3tree::Tree::parse(view(c), view(g));
    };

    // --- Wind-state object, Sec13.2: float index k lives at block offset
    // 0x38 + 4k. Every k = 0x00..0x25 gets a distinct value 100 + k so a
    // table that starts one float late (+0x3C, the pre-2026-09-20 reader) is
    // visible at k = 0 (+0x38, which is 5.0 in 11/11 real trees). ---
    {
        TreeBuildOptions opt;
        for (size_t k = 0; k < 0x26; ++k) opt.windFloats.push_back(100.0f + static_cast<float>(k));
        sr3tree::Tree t = parseOpt(opt);
        const auto& tb = t.treeBlock();
        CHECK(tb.windFloats.size() == 38);
        for (size_t k = 0; k < 38; ++k) CHECK(tb.windFloats[k] == 100.0f + static_cast<float>(k));
        CHECK(tb.windFloats[0] == 100.0f); // block +0x38 - the first float of the object
        CHECK(tb.windFloats[37] == 137.0f); // block +0xCC - the last file-loaded float

        // Sec13.2's role table, typed by hand (k hex -> role).
        const auto& w = tb.wind;
        CHECK(w.strengthResponseTime == 100.0f);      // k0x00 +0x38
        CHECK(w.directionResponseTime == 101.0f);     // k0x01 +0x3C
        CHECK(w.lengthLikeReciprocalToC2z == 102.0f); // k0x02 +0x40
        CHECK(w.copiedToC2w == 103.0f);               // k0x03 +0x44
        CHECK(w.copiedToC6x == 104.0f);               // k0x04 +0x48
        // four bands: 5+4g amp@0, 6+4g amp@1, 7+4g freq@0, 8+4g freq@1; exponent 0x20+g
        CHECK(w.bands[0].amplitudeAtLevel0 == 105.0f && w.bands[0].amplitudeAtLevel1 == 106.0f);
        CHECK(w.bands[0].frequencyAtLevel0 == 107.0f && w.bands[0].frequencyAtLevel1 == 108.0f);
        CHECK(w.bands[1].amplitudeAtLevel0 == 109.0f && w.bands[1].amplitudeAtLevel1 == 110.0f);
        CHECK(w.bands[1].frequencyAtLevel0 == 111.0f && w.bands[1].frequencyAtLevel1 == 112.0f);
        CHECK(w.bands[2].amplitudeAtLevel0 == 113.0f && w.bands[2].amplitudeAtLevel1 == 114.0f);
        CHECK(w.bands[2].frequencyAtLevel0 == 115.0f && w.bands[2].frequencyAtLevel1 == 116.0f);
        CHECK(w.bands[3].amplitudeAtLevel0 == 117.0f && w.bands[3].amplitudeAtLevel1 == 118.0f);
        CHECK(w.bands[3].frequencyAtLevel0 == 119.0f && w.bands[3].frequencyAtLevel1 == 120.0f);
        CHECK(w.bands[0].exponent == 132.0f); // k0x20 +0xB8
        CHECK(w.bands[1].exponent == 133.0f); // k0x21 +0xBC
        CHECK(w.bands[2].exponent == 134.0f); // k0x22 +0xC0
        CHECK(w.bands[3].exponent == 135.0f); // k0x23 +0xC4
        CHECK(w.gustOnsetRate == 121.0f);            // k0x15 +0x8C
        CHECK(w.scaleOfBand0ResponseToC5y == 122.0f); // k0x16 +0x90
        CHECK(w.copiedToC5z == 123.0f);              // k0x17 +0x94
        CHECK(w.gustTargetAmplitudeLo == 124.0f && w.gustTargetAmplitudeHi == 125.0f); // k0x18/19
        CHECK(w.gustPlateauDurationLo == 126.0f && w.gustPlateauDurationHi == 127.0f); // k0x1A/1B
        CHECK(w.scaleOfLevelToC6y == 128.0f && w.scaleOfLevelToC6z == 129.0f);          // k0x1C/1D
        CHECK(w.copiedToC4y == 130.0f && w.copiedToC4z == 131.0f);                      // k0x1E/1F
        CHECK(w.copiedToC3y == 136.0f && w.copiedToC3z == 137.0f);                      // k0x24/25

        // The pure mapping agrees with what the file walk produced.
        std::array<float, 38> ks{};
        for (size_t k = 0; k < 38; ++k) ks[k] = 100.0f + static_cast<float>(k);
        sr3tree::WindParameters direct = sr3tree::windParametersFromFloats(ks);
        CHECK(direct.strengthResponseTime == 100.0f && direct.copiedToC3z == 137.0f);
        CHECK(direct.bands[3].exponent == 135.0f && direct.bands[2].frequencyAtLevel1 == 116.0f);

        // No file-loaded float leaks into the runtime region: all still zero.
        CHECK(tb.windRuntimeRegionAllZero());
        CHECK(tb.windRuntimeNonZeroDwords == 0);
    }

    // --- Runtime region boundary, Sec13.2: k >= 0x26 is block +0xD0..+0x207
    // (0x138 bytes), zero on disk. +0xCC (k = 0x25) is the last FILE-LOADED
    // float and must not count; +0xD0 (k = 0x26) is the first runtime dword;
    // +0x204 is the last dword of the 0x208-byte block. ---
    {
        auto nonzeroAt = [&](size_t off) {
            TreeBuildOptions opt;
            opt.treeBlockDwords.push_back({off, 0x3F800000u});
            return parseOpt(opt); // must not throw: zero is an observation, not a loader rule
        };
        CHECK(nonzeroAt(0xD0).treeBlock().windRuntimeNonZeroDwords == 1);
        CHECK(!nonzeroAt(0xD0).treeBlock().windRuntimeRegionAllZero());
        CHECK(nonzeroAt(0x204).treeBlock().windRuntimeNonZeroDwords == 1);
        CHECK(!nonzeroAt(0x204).treeBlock().windRuntimeRegionAllZero());
        CHECK(nonzeroAt(0x1D8).treeBlock().windRuntimeNonZeroDwords == 1); // the tail overlay slots
        CHECK(nonzeroAt(0x1E0).treeBlock().windRuntimeNonZeroDwords == 1);
        CHECK(nonzeroAt(0xE0).treeBlock().windRuntimeNonZeroDwords == 1);  // k0x2A, the master pointer slot
        {
            sr3tree::Tree t = nonzeroAt(0xCC);
            CHECK(t.treeBlock().windRuntimeRegionAllZero()); // +0xCC is file-loaded k0x25, not runtime
            CHECK(t.treeBlock().windFloats[0x25] == 1.0f);   // 0x3F800000
        }
        {
            TreeBuildOptions opt;
            opt.treeBlockDwords = {{0xD0, 1u}, {0xD4, 2u}, {0x1FC, 3u}, {0xCC, 4u}};
            sr3tree::Tree t = parseOpt(opt);
            CHECK(t.treeBlock().windRuntimeNonZeroDwords == 3);
        }
        // pointer slots BEFORE the object (+0x08.. +0x37) are not part of it.
        {
            TreeBuildOptions opt;
            opt.treeBlockDwords = {{0x08, 0x12345678u}, {0x28, 0x9ABCDEF0u}, {0x34, 1u}};
            sr3tree::Tree t = parseOpt(opt);
            CHECK(t.treeBlock().windRuntimeRegionAllZero());
            CHECK(t.treeBlock().windFloats[0] == 0.0f);
        }
    }

    // --- Geometry LOD distances, Sec13.5 / Sec13.5.2: A,B,C,D at +0xE0,
    // +0xE4, +0xE8, +0xEC; +0xF0/+0xF4 derived at load, zero on disk. ---
    {
        TreeBuildOptions opt;
        opt.lodDistances = {10.0f, 20.0f, 30.0f, 40.0f};
        sr3tree::Tree t = parseOpt(opt);
        CHECK(t.geometry().lodDistances[0] == 10.0f);
        CHECK(t.geometry().lodDistances[1] == 20.0f);
        CHECK(t.geometry().lodDistances[2] == 30.0f);
        CHECK(t.geometry().lodDistances[3] == 40.0f);
        CHECK(t.geometry().cullDistance() == 40.0f); // D is the cull distance
        CHECK(t.geometry().lodDerivedOnDisk[0] == 0.0f && t.geometry().lodDerivedOnDisk[1] == 0.0f);
        CHECK(t.geometry().lodDerivedOnDiskZero());
        CHECK(sr3tree::lodDistancesValid(t.geometry().lodDistances));
        // Sec13.5.2 table through the Tree convenience accessor.
        CHECK(t.lodParameterAtDistance(15.0f).has_value());
        CHECK(std::fabs(*t.lodParameterAtDistance(15.0f) - 0.5833333f) < 1e-5f);

        TreeBuildOptions opt2;
        opt2.lodDistances = {1.5f, 2.5f, 3.5f, 4.5f};
        // +0xF0/+0xF4 are zero on real disk; a nonzero value here only proves the
        // two dwords are read from those offsets (F0 -> [0], F4 -> [1]).
        opt2.lodDerived = {7.25f, 9.5f};
        sr3tree::Tree t2 = parseOpt(opt2);
        CHECK(t2.geometry().lodDistances[3] == 4.5f);
        CHECK(t2.geometry().lodDerivedOnDisk[0] == 7.25f); // +0xF0
        CHECK(t2.geometry().lodDerivedOnDisk[1] == 9.5f);  // +0xF4
        CHECK(!t2.geometry().lodDerivedOnDiskZero());
    }

    // --- LOD parameter t, Sec13.5.2's table, for A=10, B=20, C=30, D=40
    // (so A^2=100, B^2=400, C^2=900, D^2=1600). The fade is linear in the
    // SQUARED distance, so at distance 15 t = 1 - (225-100)/(400-100) =
    // 0.58333 (a fade linear in distance would give 0.5), and at 35
    // t = -(1225-900)/(1600-900) = -0.46429 (linear in distance: -0.5). ---
    {
        const sr3tree::LodDistances d = {10.0f, 20.0f, 30.0f, 40.0f};
        auto T = [&](float dist) { return sr3tree::lodParameterForDistance(d, dist); };
        auto TQ = [&](float q) { return sr3tree::lodParameterForSquaredDistance(d, q); };
        auto nearv = [](std::optional<float> v, float want, float eps) {
            return v.has_value() && std::fabs(*v - want) <= eps;
        };
        CHECK(sr3tree::lodDistancesValid(d));
        // q < A^2 : 1
        CHECK(nearv(T(0.0f), 1.0f, 0.0f));
        CHECK(nearv(T(5.0f), 1.0f, 0.0f));
        CHECK(nearv(T(9.999f), 1.0f, 0.0f));
        // boundary A: 1 on both sides of it (the table is continuous)
        CHECK(nearv(TQ(100.0f), 1.0f, 1e-6f));
        CHECK(nearv(TQ(100.3f), 0.999f, 1e-5f));
        // A^2 <= q < B^2 : 1 - (q-A^2)/(B^2-A^2)
        CHECK(nearv(T(15.0f), 0.5833333f, 1e-5f));
        CHECK(nearv(TQ(250.0f), 0.5f, 1e-6f)); // midpoint in q: (250-100)/300 = 0.5 exactly
        CHECK(nearv(TQ(399.7f), 0.001f, 2e-5f));
        // boundary B: 0 from both sides
        CHECK(nearv(TQ(400.0f), 0.0f, 1e-6f));
        CHECK(nearv(T(20.0f), 0.0f, 1e-6f));
        // B^2 <= q < C^2 : 0
        CHECK(nearv(T(20.001f), 0.0f, 0.0f));
        CHECK(nearv(T(25.0f), 0.0f, 0.0f));
        CHECK(nearv(TQ(899.5f), 0.0f, 0.0f));
        // boundary C: 0 into the fade
        CHECK(nearv(TQ(900.0f), 0.0f, 1e-6f));
        // C^2 <= q < D^2 : -(q-C^2)/(D^2-C^2)
        CHECK(nearv(T(35.0f), -0.4642857f, 1e-5f));
        CHECK(nearv(TQ(1250.0f), -0.5f, 1e-6f)); // (1250-900)/700 = 0.5
        CHECK(nearv(TQ(1599.5f), -0.9992857f, 2e-5f));
        // boundary D and beyond: -1
        CHECK(nearv(TQ(1600.0f), -1.0f, 1e-6f));
        CHECK(nearv(T(40.0f), -1.0f, 1e-6f));
        CHECK(nearv(T(40.001f), -1.0f, 0.0f));
        CHECK(nearv(T(100.0f), -1.0f, 0.0f));
        CHECK(nearv(T(1.0e6f), -1.0f, 0.0f));
        // The five ranges are ordered: t never increases with distance.
        float prev = 2.0f;
        bool monotone = true;
        for (int i = 0; i <= 1000; ++i) {
            auto v = T(static_cast<float>(i) * 0.05f);
            if (!v || *v > prev) monotone = false;
            if (v) prev = *v;
        }
        CHECK(monotone);

        // Ranges that the spec's validity test (A<B<C<D, strictly) rejects.
        CHECK(!sr3tree::lodDistancesValid({10.0f, 10.0f, 30.0f, 40.0f})); // A == B
        CHECK(!sr3tree::lodDistancesValid({10.0f, 20.0f, 20.0f, 40.0f})); // B == C
        CHECK(!sr3tree::lodDistancesValid({10.0f, 20.0f, 30.0f, 30.0f})); // C == D
        CHECK(!sr3tree::lodDistancesValid({40.0f, 30.0f, 20.0f, 10.0f})); // descending
        CHECK(!sr3tree::lodDistancesValid({0.0f, 0.0f, 0.0f, 0.0f}));      // all-zero (the parse of an unfilled block)
        CHECK(sr3tree::lodDistancesValid({0.0f, 1.0f, 2.0f, 3.0f}));
        CHECK(!sr3tree::lodParameterForDistance({10.0f, 10.0f, 30.0f, 40.0f}, 15.0f).has_value());
        CHECK(!sr3tree::lodParameterForDistance({10.0f, 20.0f, 20.0f, 40.0f}, 15.0f).has_value());
        CHECK(!sr3tree::lodParameterForDistance({10.0f, 20.0f, 30.0f, 30.0f}, 15.0f).has_value());
        CHECK(!sr3tree::lodParameterForDistance({0.0f, 0.0f, 0.0f, 0.0f}, 15.0f).has_value());
        // NaN never validates.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(!sr3tree::lodDistancesValid({10.0f, nan, 30.0f, 40.0f}));
        CHECK(!T(nan).has_value());
        CHECK(!T(-1.0f).has_value());
        CHECK(!TQ(nan).has_value());
        CHECK(!TQ(-1.0f).has_value());
        // A = 0 is a valid set: below any distance t starts fading at once.
        CHECK(nearv(sr3tree::lodParameterForDistance({0.0f, 10.0f, 20.0f, 30.0f}, 0.0f), 1.0f, 1e-6f));
        CHECK(nearv(sr3tree::lodParameterForDistance({0.0f, 10.0f, 20.0f, 30.0f}, 5.0f), 0.75f, 1e-6f));
        // This reader's own guard (not a spec rule): a negative A has no squared table.
        CHECK(!sr3tree::lodParameterForDistance({-5.0f, 10.0f, 20.0f, 30.0f}, 5.0f).has_value());
    }

    // --- Steady-state wind, Sec13.4: osc_g = level^e_g, amplitude =
    // lerp(amp0, amp1, osc_g), frequency = lerp(freq0, freq1, osc_g), at the
    // shipped level 0.1. The five st_com-group trees have decreasing
    // amplitude pairs 2->1 and 4->3 (Sec13.8) and e = 2,2,2,2: live amplitude
    // 1.99 and 3.99, osc 0.01 (the Sec13.4 table). ---
    {
        static_assert(sr3tree::kShippedWindLevel == 0.1f, "Sec13.1: shipped wind level is 0.1");
        sr3tree::WindParameters p;
        for (auto& b : p.bands) b.exponent = 2.0f;
        p.bands[0].amplitudeAtLevel0 = 2.0f; p.bands[0].amplitudeAtLevel1 = 1.0f;
        p.bands[1].amplitudeAtLevel0 = 4.0f; p.bands[1].amplitudeAtLevel1 = 3.0f;
        p.bands[2].amplitudeAtLevel0 = 0.0f; p.bands[2].amplitudeAtLevel1 = 0.0f;
        p.bands[3].amplitudeAtLevel0 = 0.11f; p.bands[3].amplitudeAtLevel1 = 0.01f;
        p.bands[0].frequencyAtLevel0 = 1.0f; p.bands[0].frequencyAtLevel1 = 3.0f;
        auto ss = sr3tree::windSteadyState(p); // default level = 0.1
        CHECK(ss.has_value());
        if (ss) {
            for (size_t g = 0; g < 4; ++g) CHECK(std::fabs((*ss)[g].response - 0.01f) < 1e-6f);
            CHECK(std::fabs((*ss)[0].amplitude - 1.99f) < 1e-5f);
            CHECK(std::fabs((*ss)[1].amplitude - 3.99f) < 1e-5f);
            CHECK(std::fabs((*ss)[2].amplitude - 0.0f) < 1e-6f);
            CHECK(std::fabs((*ss)[3].amplitude - 0.109f) < 1e-5f); // 0.11 + (0.01-0.11)*0.01
            CHECK(std::fabs((*ss)[0].frequency - 1.02f) < 1e-5f);  // 1 + (3-1)*0.01
        }

        // The pine exponents 2, 3, 1, 0.75 give osc 0.01, 0.001, 0.1, 0.178 (Sec13.4).
        sr3tree::WindParameters q;
        q.bands[0].exponent = 2.0f; q.bands[1].exponent = 3.0f;
        q.bands[2].exponent = 1.0f; q.bands[3].exponent = 0.75f;
        auto sq = sr3tree::windSteadyState(q, 0.1f);
        CHECK(sq.has_value());
        if (sq) {
            CHECK(std::fabs((*sq)[0].response - 0.01f) < 1e-6f);
            CHECK(std::fabs((*sq)[1].response - 0.001f) < 1e-6f);
            CHECK(std::fabs((*sq)[2].response - 0.1f) < 1e-6f);
            CHECK(std::fabs((*sq)[3].response - 0.17782794f) < 1e-6f); // 10^-0.75; the spec prints 0.178
        }

        // Endpoints: level 0 -> the level-0 values, level 1 -> the level-1 values;
        // level 0.5 with e = 1 -> the midpoint.
        sr3tree::WindParameters r;
        for (auto& b : r.bands) {
            b.exponent = 1.0f;
            b.amplitudeAtLevel0 = 10.0f; b.amplitudeAtLevel1 = 20.0f;
            b.frequencyAtLevel0 = 3.0f;  b.frequencyAtLevel1 = 1.0f;
        }
        auto s0 = sr3tree::windSteadyState(r, 0.0f);
        auto s1 = sr3tree::windSteadyState(r, 1.0f);
        auto sh = sr3tree::windSteadyState(r, 0.5f);
        CHECK(s0.has_value() && s1.has_value() && sh.has_value());
        if (s0 && s1 && sh) {
            CHECK((*s0)[2].amplitude == 10.0f && (*s0)[2].frequency == 3.0f && (*s0)[2].response == 0.0f);
            CHECK((*s1)[2].amplitude == 20.0f && (*s1)[2].frequency == 1.0f && (*s1)[2].response == 1.0f);
            CHECK(std::fabs((*sh)[2].amplitude - 15.0f) < 1e-5f);
            CHECK(std::fabs((*sh)[2].frequency - 2.0f) < 1e-5f);
        }
        // A wind level is min(strength + gust, 1) - outside [0, 1] is not a level.
        CHECK(!sr3tree::windSteadyState(r, -0.01f).has_value());
        CHECK(!sr3tree::windSteadyState(r, 1.01f).has_value());
        CHECK(!sr3tree::windSteadyState(r, std::numeric_limits<float>::quiet_NaN()).has_value());

        // Through a parsed file (values written at the literal offsets).
        TreeBuildOptions opt;
        opt.windFloats.assign(0x26, 0.0f);
        opt.windFloats[5 + 4 * 1] = 4.0f;   // band 1 amp @0   (k 9)
        opt.windFloats[6 + 4 * 1] = 3.0f;   // band 1 amp @1   (k 0xA)
        opt.windFloats[7 + 4 * 1] = 2.0f;   // band 1 freq @0  (k 0xB)
        opt.windFloats[8 + 4 * 1] = 6.0f;   // band 1 freq @1  (k 0xC)
        opt.windFloats[0x20 + 1] = 2.0f;    // e_1 (k 0x21)
        sr3tree::Tree t = parseOpt(opt);
        auto st = t.windSteadyStateAtLevel();
        CHECK(st.has_value());
        if (st) {
            CHECK(std::fabs((*st)[1].amplitude - 3.99f) < 1e-5f);
            CHECK(std::fabs((*st)[1].frequency - 2.04f) < 1e-5f); // 2 + (6-2)*0.01
        }
    }

    // --- Mutations: each must throw. ---
    auto parseWithMagicMutated = [] {
        TreeBuildOptions opt;
        opt.mutateTreeMagic = true;
        TreeLayout lay;
        std::vector<uint8_t> g;
        std::vector<uint8_t> c = buildTree(opt, lay, g);
        return sr3tree::Tree::parse(view(c), view(g));
    };
    CHECK_THROWS(parseWithMagicMutated());

    auto parseWithVersionTooLow = [] {
        TreeBuildOptions opt;
        opt.mutateTreeVersionTooLow = true;
        TreeLayout lay;
        std::vector<uint8_t> g;
        std::vector<uint8_t> c = buildTree(opt, lay, g);
        return sr3tree::Tree::parse(view(c), view(g));
    };
    CHECK_THROWS(parseWithVersionTooLow());

    auto parseWithLodSlotCountWrong = [] {
        TreeBuildOptions opt;
        opt.mutateLodSlotCountWrong = true;
        TreeLayout lay;
        std::vector<uint8_t> g;
        std::vector<uint8_t> c = buildTree(opt, lay, g);
        return sr3tree::Tree::parse(view(c), view(g));
    };
    CHECK_THROWS(parseWithLodSlotCountWrong());

    auto parseWithMaterialCountMismatch = [] {
        TreeBuildOptions opt;
        opt.materials.push_back(SyntheticMaterial{});
        opt.mutateMaterialCountMismatch = true;
        TreeLayout lay;
        std::vector<uint8_t> g;
        std::vector<uint8_t> c = buildTree(opt, lay, g);
        return sr3tree::Tree::parse(view(c), view(g));
    };
    CHECK_THROWS(parseWithMaterialCountMismatch());

    // Truncation: cut the buffer mid-way through and confirm it throws
    // rather than reading past the end.
    auto parseTruncated = [] {
        TreeBuildOptions opt;
        opt.lodPresent = {true, false, false, false};
        opt.lodPositions.push_back({{{0, 0, 0}}});
        TreeLayout lay;
        std::vector<uint8_t> g;
        std::vector<uint8_t> c = buildTree(opt, lay, g);
        c.resize(c.size() / 2);
        return sr3tree::Tree::parse(view(c), view(g));
    };
    CHECK_THROWS(parseTruncated());

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic tree-format tests passed.\n";
    return 0;
}
