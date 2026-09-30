// Synthetic tests for the .cfmesh_pc foliage-mesh reader and its
// content validator. Every buffer is built from scratch from the TEXT of
// spec-foliage-format.md (re-synced 2026-09-20) - never from the reader's own
// source or constants - so a reader bug and a builder bug cannot cancel:
//
//   * the shared material block (reused from sr3geometry) and the mandatory
//     strictly-greater 16-byte pad to the outer 0x0FF1C1A1 block (Sec3);
//   * the outer header: +0x00 magic, +0x04 version 5, +0x08 Mesh offset,
//     +0x18 runtime slot array offset, +0x20 material count, +0x28 name-table
//     offset, +0x30 LOD RECORD COUNT, +0x34 an OPEN u32, and +0x38 a SIGNED
//     offset RELATIVE TO THE OUTER-BLOCK BASE B to the LOD/fade table (Sec4,
//     Sec11.2: "the table starts at B + i32(B+0x38), not at B+0x38 itself");
//   * the embedded Mesh sub-block (a version-9 marker only - its internals
//     are out of scope);
//   * the material-handle table (Sec3, Sec11.3): 8-aligned after the Mesh
//     block, a 0x10-byte header {u32 ptr-slot, 0, count, ?} + count x u32,
//     ending exactly where the runtime slot array begins;
//   * the material sub-record chain (u32 size prefix, 8-aligned 0x30 header,
//     then bindings/constant-hashes/vec4s at 4/4/16 alignment) that must end
//     EXACTLY at the +0x28 target, then the texture-name table;
//   * the LOD/fade table: `count` records of 0x18 bytes {f32 fadeInStart,
//     f32 fadeInEnd, f32 fadeOutStart, f32 fadeOutEnd, u32 drawGroupIndex,
//     u32 billboardFlag}, and the tail from the table start to EOF is exactly
//     count x 24 bytes.
//
// The reader is additionally mutation-checked from outside this file (see the
// agent's build scripts): a reader that takes the table at B+0x38 instead of
// B+i32(B+0x38), one with a wrong record stride, one that takes the count from
// +0x34, and one that tolerates tail slack must each FAIL this suite.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3foliage/content_validation.h"
#include "sr3foliage/foliage_mesh.h"
#include "sr3geometry/material_block.h"

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

// Spec constants, restated here from the spec text (NOT taken from the reader).
constexpr uint32_t kSpecOuterMagic = 0x0FF1C1A1u; // Sec4 +0x00
constexpr uint32_t kSpecOuterVersion = 5;         // Sec4 +0x04
constexpr uint32_t kSpecMeshVersion = 9;          // Sec1/Sec5
constexpr size_t kSpecLodRecordBytes = 0x18;      // Sec11.2: per-LOD record, 0x18 bytes
constexpr size_t kSpecHandleHeaderBytes = 0x10;   // Sec3: 0x10 header + count x u32
constexpr size_t kSpecMaterialHeaderBytes = 0x30; // Sec6
constexpr uint32_t kSpecNull = 0xFFFFFFFFu;       // Sec4: -1 = none

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

uint32_t getU32At(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

void padTo(std::vector<uint8_t>& b, size_t target) {
    while (b.size() < target) b.push_back(0x00);
}

size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Spec Sec3: the pad before the outer block goes "to the next 16-byte
// boundary, always >= 1 byte" - strictly greater, even when already aligned.
size_t padStrictlyTo16(size_t value) { return value / 16 * 16 + 16; }

// Offsets into the texture-name table for each name, in the order the
// names are written back-to-back (null-terminated).
std::vector<uint32_t> computeNameOffsets(const std::vector<std::string>& names) {
    std::vector<uint32_t> offsets;
    uint32_t at = 0;
    for (const auto& n : names) {
        offsets.push_back(at);
        at += static_cast<uint32_t>(n.size()) + 1;
    }
    return offsets;
}

struct SyntheticBinding {
    uint32_t nameOffset = 0;
    uint32_t slotHash = 0;
    uint16_t resolvedIndex = 0;
    uint16_t flags = 0;
};

struct SyntheticMaterial {
    uint32_t shaderHash = 0;
    uint32_t variantHash = 0;
    uint32_t flags = 0;
    std::vector<SyntheticBinding> bindings;
    std::vector<uint32_t> constantHashes;
    std::vector<std::array<float, 4>> vec4s;
};

// One LOD/fade record exactly as the spec's Sec11.2 table lays it out.
struct SyntheticLod {
    float fadeInStart, fadeInEnd, fadeOutStart, fadeOutEnd;
    uint32_t drawGroupIndex;
    uint32_t billboardFlag;
};

struct FoliageBuildOptions {
    std::vector<std::string> materialBlockNames = {"leaf_atlas.tga"};
    std::vector<SyntheticMaterial> materials;
    std::vector<std::string> textureNames;
    std::vector<SyntheticLod> lodRecords;
    bool hasMesh = true;
    bool hasLod = true;
    uint32_t meshVersionOverride = kSpecMeshVersion;

    // Outer +0x34 (OPEN in the spec): a value that is deliberately NOT the
    // record count, so a reader that takes the count from +0x34 is caught.
    uint32_t field34 = 96;
    // -1: write lodRecords.size() into +0x30; otherwise write this instead.
    int64_t lodCountOverride = -1;
    // Added to the (correct) relative offset written at +0x38.
    int32_t lodOffsetDelta = 0;
    // Extra bytes appended after the LOD table (breaks the exact tail).
    size_t trailingBytes = 0;

    // Material-handle table: header {ptr-slot, 0, count, ?} + count x u32.
    // Empty handles = identity 0..n-1 (what 19/19 real files carry).
    std::vector<uint32_t> handles;
    uint32_t handleHeaderWord0 = 0;
    uint32_t handleHeaderWord3 = 0x01;
};

// Builds a well-formed .cfmesh_pc buffer per the spec's Sec3-Sec11.
std::vector<uint8_t> buildCfmesh(const FoliageBuildOptions& opt) {
    std::vector<uint8_t> b;

    // --- Material block (shared sr3geometry structure). ---
    appendU32(b, sr3geometry::kMaterialBlockMagic);
    appendU32(b, 0); // name-table-length placeholder, patched below
    appendU32(b, 0); // +0x08, unused
    appendU32(b, static_cast<uint32_t>(opt.materialBlockNames.size()));
    b.insert(b.end(), 16, 0x00); // +0x10-+0x1F padding
    size_t nameTableStart = b.size();
    for (const auto& n : opt.materialBlockNames) appendCString(b, n);
    uint32_t nameTableLength = static_cast<uint32_t>(b.size() - nameTableStart);
    putU32At(b, 0x04, nameTableLength);

    // --- Mandatory strictly-greater 16-byte pad to the outer block. ---
    size_t base = padStrictlyTo16(b.size());
    padTo(b, base);

    // --- Outer header: the full 0x40 bytes, patched as regions are placed. ---
    size_t headerAt = b.size();
    b.insert(b.end(), 0x40, 0x00);
    putU32At(b, headerAt + 0x00, kSpecOuterMagic);
    putU32At(b, headerAt + 0x04, kSpecOuterVersion);

    // --- Optional inline Mesh sub-block: a version marker only. ---
    if (opt.hasMesh) {
        size_t meshAt = b.size();
        appendU32(b, opt.meshVersionOverride);
        putU32At(b, headerAt + 0x08, static_cast<uint32_t>(meshAt - base));
    } else {
        putU32At(b, headerAt + 0x08, kSpecNull);
    }

    // --- Material-handle table (Sec3): 8-aligned after the Mesh block;
    // 0x10 header {u32 ptr-slot, 0, count, ?} + count x u32. ---
    const size_t matCount = opt.materials.size();
    padTo(b, alignUp(b.size(), 8));
    appendU32(b, opt.handleHeaderWord0);
    appendU32(b, 0);
    appendU32(b, static_cast<uint32_t>(matCount));
    appendU32(b, opt.handleHeaderWord3);
    for (size_t i = 0; i < matCount; ++i) {
        appendU32(b, opt.handles.empty() ? static_cast<uint32_t>(i) : opt.handles[i]);
    }

    // --- Runtime slot array: begins exactly where the handle table ends;
    // count x 8 bytes, zero-filled (Sec3). ---
    size_t runtimeSlotsAt = b.size();
    putU32At(b, headerAt + 0x18, static_cast<uint32_t>(runtimeSlotsAt - base));
    b.insert(b.end(), matCount * 8, 0x00);
    putU32At(b, headerAt + 0x20, static_cast<uint32_t>(matCount));

    // --- Pad to 16, then the material sub-record chain (Sec3, Sec6). ---
    padTo(b, alignUp(b.size(), 16));
    for (const auto& m : opt.materials) {
        size_t pos = b.size();
        appendU32(b, 0); // size prefix placeholder

        size_t hdr = alignUp(pos + 4, 8);
        padTo(b, hdr);
        appendU32(b, m.shaderHash);
        appendU32(b, m.variantHash);
        appendU32(b, m.flags);
        appendU16(b, static_cast<uint16_t>(m.bindings.size()));
        appendU8(b, static_cast<uint8_t>(m.constantHashes.size()));
        appendU8(b, static_cast<uint8_t>(m.vec4s.size()));
        padTo(b, hdr + kSpecMaterialHeaderBytes); // +0x10/+0x18/+0x20 runtime pointers, zero on disk

        padTo(b, alignUp(b.size(), 4));
        for (const auto& tb : m.bindings) {
            appendU32(b, tb.nameOffset);
            appendU32(b, tb.slotHash);
            appendU16(b, tb.resolvedIndex);
            appendU16(b, tb.flags);
        }

        padTo(b, alignUp(b.size(), 4));
        for (uint32_t h : m.constantHashes) appendU32(b, h);

        padTo(b, alignUp(b.size(), 16));
        for (const auto& v : m.vec4s) {
            for (float f : v) appendF32(b, f);
        }

        putU32At(b, pos, static_cast<uint32_t>(b.size() - pos));
    }

    // --- Texture-name table (outer +0x28). ---
    size_t nameTableAt = b.size();
    putU32At(b, headerAt + 0x28, static_cast<uint32_t>(nameTableAt - base));
    for (const auto& n : opt.textureNames) appendCString(b, n);

    // --- +0x30 record count, +0x34 the OPEN word, +0x38 the table offset,
    // relative to the outer-block base B (Sec11.2). ---
    putU32At(b, headerAt + 0x30,
             opt.lodCountOverride >= 0 ? static_cast<uint32_t>(opt.lodCountOverride)
                                       : static_cast<uint32_t>(opt.lodRecords.size()));
    putU32At(b, headerAt + 0x34, opt.field34);
    if (opt.hasLod) {
        size_t lodAt = b.size();
        putU32At(b, headerAt + 0x38,
                 static_cast<uint32_t>(static_cast<int64_t>(lodAt - base) + opt.lodOffsetDelta));
        for (const auto& r : opt.lodRecords) {
            appendF32(b, r.fadeInStart);
            appendF32(b, r.fadeInEnd);
            appendF32(b, r.fadeOutStart);
            appendF32(b, r.fadeOutEnd);
            appendU32(b, r.drawGroupIndex); // u32, not a float
            appendU32(b, r.billboardFlag);  // u32, not a float
        }
        b.insert(b.end(), opt.trailingBytes, 0x00);
    } else {
        putU32At(b, headerAt + 0x38, kSpecNull);
    }

    return b;
}

bool throwsFormatError(const std::vector<uint8_t>& blob) {
    try {
        sr3foliage::FoliageMesh::parse(sr3foliage::ByteView(blob.data(), blob.size()));
    } catch (const sr3foliage::FormatError&) {
        return true;
    }
    return false;
}

// Parses a buffer the spec says is well-formed. If the reader rejects it that
// is a test failure, reported and then the process stops (there is nothing
// meaningful to continue with), rather than an uncaught-exception abort.
sr3foliage::FoliageMesh parseOk(const std::vector<uint8_t>& blob) {
    try {
        return sr3foliage::FoliageMesh::parse(sr3foliage::ByteView(blob.data(), blob.size()));
    } catch (const std::exception& ex) {
        std::cerr << "UNEXPECTED REJECTION of a spec-well-formed buffer: " << ex.what() << "\n";
        std::cout << (g_failures + 1) << " check(s) FAILED.\n";
        std::exit(1);
    }
}

// Three cross-fading LODs in the shape spec Sec11.2 describes: fadeIn(i+1)
// == fadeOut(i) at both ends; draw groups 0/1/2; the last one a billboard.
std::vector<SyntheticLod> threeLods() {
    return {
        {0.0f, 0.0f, 12.0f, 15.0f, 0, 0},
        {12.0f, 15.0f, 20.0f, 25.0f, 1, 0},
        {20.0f, 25.0f, 260.0f, 270.0f, 2, 1},
    };
}

} // namespace

int main() {
    // --- Simple well-formed case: one material, two bindings, Mesh block,
    // handle table, and a three-record LOD table. ---
    {
        std::vector<std::string> texNames = {"leaf_diffuse.tga", "leaf_normal.tga"};
        auto nameOffsets = computeNameOffsets(texNames);

        FoliageBuildOptions opt;
        opt.textureNames = texNames;
        SyntheticMaterial m;
        m.shaderHash = 0x1234ABCD;
        m.variantHash = 0xCAFEF00D;
        m.flags = 0xC2;
        m.bindings.push_back({nameOffsets[0], 0x1111, 0, 0});
        m.bindings.push_back({nameOffsets[1], 0x2222, 1, 0x08});
        m.constantHashes = {0xAAAAAAAA, 0xBBBBBBBB};
        m.vec4s.push_back({1.0f, 2.0f, 3.0f, 4.0f});
        opt.materials.push_back(m);
        opt.lodRecords = threeLods();

        std::vector<uint8_t> blob = buildCfmesh(opt);
        sr3foliage::FoliageMesh f = parseOk(blob);

        CHECK(f.version() == kSpecOuterVersion);
        CHECK(f.materialCount() == 1);
        CHECK(f.hasMeshSubBlock());
        CHECK(f.materials().size() == 1);
        CHECK(f.materials()[0].shaderHash == 0x1234ABCD);
        CHECK(f.materials()[0].variantHash == 0xCAFEF00D);
        CHECK(f.materials()[0].flags == 0xC2);
        CHECK(f.materials()[0].textureBindings.size() == 2);
        CHECK(f.materials()[0].textureBindings[0].name == "leaf_diffuse.tga");
        CHECK(f.materials()[0].textureBindings[1].name == "leaf_normal.tga");
        CHECK(f.materials()[0].textureBindings[1].flags == 0x08);
        CHECK(f.materials()[0].constantNameHashes.size() == 2);
        CHECK(f.materials()[0].constantValues.size() == 1);
        CHECK(f.materials()[0].constantValues[0][2] == 3.0f);
        CHECK(f.textureNames().size() == 2);
        CHECK(f.materialBlock().textureNames.size() == 1);

        // LOD/fade table: count from +0x30, table at B + i32(B+0x38), named
        // fields, u32 draw group / billboard (not floats).
        const size_t B = f.outerBlockOffset();
        CHECK(f.lodRecordCount() == 3);
        CHECK(f.hasLodTable());
        CHECK(f.lodRecords().size() == 3);
        CHECK(f.lodTableTailBytes() == 3 * kSpecLodRecordBytes); // exactly count x 24
        CHECK(f.lodTableOffsetRaw() > 0x38);                      // NOT the naive B+0x38
        CHECK(f.lodTableOffset() == B + static_cast<size_t>(f.lodTableOffsetRaw()));
        CHECK(f.lodTableOffset() != B + 0x38);
        CHECK(f.lodTableOffset() + f.lodTableTailBytes() == blob.size());
        CHECK(f.outerField34() == 96);                            // OPEN word surfaced raw

        const auto& r = f.lodRecords();
        CHECK(r[0].fadeInStart == 0.0f && r[0].fadeInEnd == 0.0f);
        CHECK(r[0].fadeOutStart == 12.0f && r[0].fadeOutEnd == 15.0f);
        CHECK(r[0].drawGroupIndex == 0 && r[0].billboardFlag == 0 && !r[0].billboard());
        CHECK(r[1].fadeInStart == 12.0f && r[1].fadeInEnd == 15.0f);
        CHECK(r[1].fadeOutStart == 20.0f && r[1].fadeOutEnd == 25.0f);
        CHECK(r[1].drawGroupIndex == 1 && r[1].billboardFlag == 0);
        CHECK(r[2].fadeInStart == 20.0f && r[2].fadeInEnd == 25.0f);
        CHECK(r[2].fadeOutStart == 260.0f && r[2].fadeOutEnd == 270.0f);
        CHECK(r[2].drawGroupIndex == 2 && r[2].billboardFlag == 1 && r[2].billboard());
        // Cross-fade: fadeIn(i+1) == fadeOut(i), both ends.
        CHECK(r[1].fadeInStart == r[0].fadeOutStart && r[1].fadeInEnd == r[0].fadeOutEnd);
        CHECK(r[2].fadeInStart == r[1].fadeOutStart && r[2].fadeInEnd == r[1].fadeOutEnd);

        // Material-handle table: located, identity, header {ptr-slot,0,count,?}.
        CHECK(f.hasMaterialHandleTable());
        const auto& t = f.materialHandleTable();
        CHECK(t.headerWord0 == 0);
        CHECK(t.headerWord1 == 0);
        CHECK(t.headerCount == 1);
        CHECK(t.headerWord3 == 0x01);
        CHECK(t.handles.size() == 1);
        CHECK(t.handles[0] == 0);
        CHECK(t.isIdentity());
        CHECK(f.materialHandlesAreIdentity());
        CHECK(t.offset % 8 == 0); // Sec3: 8-aligned
        CHECK(getU32At(blob, t.offset + 8) == 1); // the header count word really is where the reader says
    }

    // --- One-record and two-record tables: the count comes from +0x30 and
    // the tail is exactly count x 24 for each. ---
    const std::vector<SyntheticLod> allThree = threeLods();
    for (size_t n = 1; n <= 2; ++n) {
        FoliageBuildOptions opt;
        opt.lodRecords.assign(allThree.begin(), allThree.begin() + static_cast<std::ptrdiff_t>(n));
        std::vector<uint8_t> blob = buildCfmesh(opt);
        sr3foliage::FoliageMesh f = parseOk(blob);
        CHECK(f.lodRecordCount() == n);
        CHECK(f.lodRecords().size() == n);
        CHECK(f.lodTableTailBytes() == n * kSpecLodRecordBytes);
    }

    // --- Handle table is a checkable property, not a key set: a non-identity
    // table still parses, is reported as non-identity, and does not affect
    // which material record is which (materials stay in record order). ---
    {
        std::vector<std::string> texNames = {"a.tga", "b.tga"};
        auto nameOffsets = computeNameOffsets(texNames);
        FoliageBuildOptions opt;
        opt.textureNames = texNames;
        SyntheticMaterial m0, m1;
        m0.shaderHash = 0x11;
        m0.bindings.push_back({nameOffsets[0], 0xA, 0, 0});
        m1.shaderHash = 0x22;
        m1.bindings.push_back({nameOffsets[1], 0xB, 0, 0});
        opt.materials = {m0, m1};
        opt.handles = {7, 0}; // not identity
        opt.handleHeaderWord0 = 0xDEADBEEF;
        opt.handleHeaderWord3 = 0x2A;
        opt.lodRecords = threeLods();
        sr3foliage::FoliageMesh f = parseOk(buildCfmesh(opt));
        CHECK(f.hasMaterialHandleTable());
        CHECK(f.materialHandleTable().handles.size() == 2);
        CHECK(f.materialHandleTable().handles[0] == 7);
        CHECK(f.materialHandleTable().handles[1] == 0);
        CHECK(!f.materialHandleTable().isIdentity());
        CHECK(!f.materialHandlesAreIdentity());
        CHECK(f.materialHandleTable().headerWord0 == 0xDEADBEEF);
        CHECK(f.materialHandleTable().headerCount == 2);
        CHECK(f.materialHandleTable().headerWord3 == 0x2A);
        CHECK(f.materials().size() == 2);
        CHECK(f.materials()[0].shaderHash == 0x11); // record order, not handle order
        CHECK(f.materials()[1].shaderHash == 0x22);
    }

    // --- Identity table with several materials (also exercises the handle
    // table's 8-alignment when count x 4 is not a multiple of 8). ---
    {
        std::vector<std::string> texNames = {"a.tga"};
        auto nameOffsets = computeNameOffsets(texNames);
        FoliageBuildOptions opt;
        opt.textureNames = texNames;
        for (uint32_t i = 0; i < 3; ++i) {
            SyntheticMaterial m;
            m.shaderHash = 0x100 + i;
            m.bindings.push_back({nameOffsets[0], 0x9000, 0, 0});
            opt.materials.push_back(m);
        }
        opt.lodRecords = threeLods();
        sr3foliage::FoliageMesh f = parseOk(buildCfmesh(opt));
        CHECK(f.materialCount() == 3);
        CHECK(f.hasMaterialHandleTable());
        CHECK(f.materialHandleTable().handles.size() == 3);
        CHECK(f.materialHandlesAreIdentity());
        CHECK(f.materialHandleTable().headerCount == 3);
        CHECK(f.materials()[2].shaderHash == 0x102);
    }

    // --- Zero-material, no-Mesh-block, no-LOD-table edge case: must
    // parse cleanly, not throw. ---
    {
        FoliageBuildOptions opt;
        opt.hasMesh = false;
        opt.hasLod = false;
        std::vector<uint8_t> blob = buildCfmesh(opt);
        sr3foliage::FoliageMesh f = parseOk(blob);
        CHECK(f.materialCount() == 0);
        CHECK(!f.hasMeshSubBlock());
        CHECK(f.materials().empty());
        CHECK(!f.hasLodTable());
        CHECK(f.lodRecords().empty());
    }

    // --- Multi-material case, mirroring spec Sec6's two observed shapes
    // ((3,9,11) and (1,1,3) for (bindings, constants, vec4s)) - exercises
    // every alignment step across a chain of more than one record. ---
    {
        std::vector<std::string> texNames = {"a.tga", "b.tga", "c.tga"};
        auto nameOffsets = computeNameOffsets(texNames);

        FoliageBuildOptions opt;
        opt.textureNames = texNames;

        SyntheticMaterial big;
        big.shaderHash = 1;
        big.variantHash = 2;
        big.flags = 0x40;
        for (uint32_t i = 0; i < 3; ++i) {
            big.bindings.push_back({nameOffsets[i % texNames.size()], 0x9000 + i, 0, 0});
        }
        for (uint32_t i = 0; i < 9; ++i) big.constantHashes.push_back(0x1000 + i);
        for (uint32_t i = 0; i < 11; ++i) {
            big.vec4s.push_back({static_cast<float>(i), 0.0f, 0.0f, 1.0f});
        }
        opt.materials.push_back(big);

        SyntheticMaterial small;
        small.shaderHash = 3;
        small.variantHash = 4;
        small.flags = 0x00;
        small.bindings.push_back({nameOffsets[0], 0x8000, 0, 0});
        small.constantHashes.push_back(0x2000);
        small.vec4s.push_back({9.0f, 8.0f, 7.0f, 6.0f});
        opt.materials.push_back(small);
        opt.lodRecords = threeLods();

        sr3foliage::FoliageMesh f = parseOk(buildCfmesh(opt));
        CHECK(f.materials().size() == 2);
        CHECK(f.materials()[0].textureBindings.size() == 3);
        CHECK(f.materials()[0].constantNameHashes.size() == 9);
        CHECK(f.materials()[0].constantValues.size() == 11);
        CHECK(f.materials()[1].textureBindings.size() == 1);
        CHECK(f.materials()[1].constantNameHashes.size() == 1);
        CHECK(f.materials()[1].constantValues.size() == 1);
        CHECK(f.lodRecords().size() == 3);
        CHECK(f.materialHandlesAreIdentity());
    }

    // ===== LOD/fade table: exact-consumption negatives (spec Sec11.2) =====

    // The tail must be EXACTLY count x 24: extra trailing bytes are rejected...
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        opt.trailingBytes = 4;
        CHECK(throwsFormatError(buildCfmesh(opt)));
        opt.trailingBytes = 24; // a whole extra record's worth is still wrong
        CHECK(throwsFormatError(buildCfmesh(opt)));
    }
    // ...as is a missing tail byte (truncated last record)...
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        std::vector<uint8_t> blob = buildCfmesh(opt);
        blob.pop_back();
        CHECK(throwsFormatError(blob));
    }
    // ...as is a record count that disagrees with the bytes present, in
    // either direction.
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        opt.lodCountOverride = 2; // 3 records present
        CHECK(throwsFormatError(buildCfmesh(opt)));
        opt.lodCountOverride = 4; // only 3 present
        CHECK(throwsFormatError(buildCfmesh(opt)));
        opt.lodCountOverride = 0;
        CHECK(throwsFormatError(buildCfmesh(opt)));
    }
    // The offset is authoritative and exact: shifted either way, the tail no
    // longer equals count x 24.
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        opt.lodOffsetDelta = 4;
        CHECK(throwsFormatError(buildCfmesh(opt)));
        opt.lodOffsetDelta = -4;
        CHECK(throwsFormatError(buildCfmesh(opt)));
        opt.lodOffsetDelta = 24;
        CHECK(throwsFormatError(buildCfmesh(opt)));
    }
    // The +0x38 word is RELATIVE TO B: rewriting it to the ABSOLUTE position
    // of the table (as a reader treating it file-relative would need) breaks
    // the file, because B is nonzero.
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        std::vector<uint8_t> blob = buildCfmesh(opt);
        sr3foliage::FoliageMesh f = parseOk(blob);
        const size_t B = f.outerBlockOffset();
        CHECK(B > 0);
        putU32At(blob, B + 0x38, static_cast<uint32_t>(f.lodTableOffset())); // absolute, not B-relative
        CHECK(throwsFormatError(blob));
    }
    // A negative offset other than the -1 "absent" marker is rejected.
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        std::vector<uint8_t> blob = buildCfmesh(opt);
        size_t B = parseOk(blob).outerBlockOffset();
        putU32At(blob, B + 0x38, static_cast<uint32_t>(-8));
        CHECK(throwsFormatError(blob));
    }
    // The count is at +0x30, NOT +0x34: +0x34 is a different value in the
    // builder (96), and changing +0x34 alone must not change the parse.
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        opt.field34 = 3; // happens to equal the count
        sr3foliage::FoliageMesh f1 = parseOk(buildCfmesh(opt));
        CHECK(f1.lodRecords().size() == 3 && f1.outerField34() == 3);
        opt.field34 = 292; // a spec-listed +0x34 value
        sr3foliage::FoliageMesh f2 = parseOk(buildCfmesh(opt));
        CHECK(f2.lodRecords().size() == 3 && f2.outerField34() == 292);
        CHECK(f2.lodRecordCount() == 3);
    }

    // --- Negative: bad outer magic must be rejected. ---
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        std::vector<uint8_t> blob = buildCfmesh(opt);
        size_t base = parseOk(blob).outerBlockOffset();
        blob[base] ^= 0xFF;
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: unsupported outer version must be rejected. ---
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        std::vector<uint8_t> blob = buildCfmesh(opt);
        size_t base = parseOk(blob).outerBlockOffset();
        putU32At(blob, base + 0x04, kSpecOuterVersion + 1);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: embedded Mesh sub-block not reading version 9. ---
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        opt.meshVersionOverride = 8;
        CHECK(throwsFormatError(buildCfmesh(opt)));
    }

    // --- Negative: a required outer offset (+0x28 texture names) null. ---
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        std::vector<uint8_t> blob = buildCfmesh(opt);
        size_t base = parseOk(blob).outerBlockOffset();
        putU32At(blob, base + 0x28, kSpecNull);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: a material sub-record declaring size 0 (the signature
    // of misreading a runtime slot array as data, spec Sec9). ---
    {
        std::vector<std::string> texNames = {"a.tga"};
        auto nameOffsets = computeNameOffsets(texNames);
        FoliageBuildOptions opt;
        opt.textureNames = texNames;
        opt.lodRecords = threeLods();
        SyntheticMaterial m;
        m.bindings.push_back({nameOffsets[0], 0, 0, 0});
        opt.materials.push_back(m);
        std::vector<uint8_t> blob = buildCfmesh(opt);

        size_t base = parseOk(blob).outerBlockOffset();
        size_t runtimeSlotsOffset = getU32At(blob, base + 0x18);
        size_t materialStart = alignUp(base + runtimeSlotsOffset + opt.materials.size() * 8, 16);
        putU32At(blob, materialStart, 0);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: material sub-record chain not ending exactly at the
    // +0x28 target (spec Sec3/Sec9's whole-population oracle). ---
    {
        std::vector<std::string> texNames = {"a.tga"};
        auto nameOffsets = computeNameOffsets(texNames);
        FoliageBuildOptions opt;
        opt.textureNames = texNames;
        opt.lodRecords = threeLods();
        SyntheticMaterial m;
        m.bindings.push_back({nameOffsets[0], 0, 0, 0});
        opt.materials.push_back(m);
        std::vector<uint8_t> blob = buildCfmesh(opt);

        size_t base = parseOk(blob).outerBlockOffset();
        putU32At(blob, base + 0x28, getU32At(blob, base + 0x28) + 4);
        CHECK(throwsFormatError(blob));
    }

    // --- Negative: truncated content. ---
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        SyntheticMaterial m;
        opt.materials.push_back(m);
        std::vector<uint8_t> blob = buildCfmesh(opt);
        blob.resize(blob.size() / 2);
        CHECK(throwsFormatError(blob));
    }

    // --- Content validation: looksLikeCfmeshFilename. ---
    CHECK(sr3foliage::looksLikeCfmeshFilename("fern01.cfmesh_pc"));
    CHECK(sr3foliage::looksLikeCfmeshFilename("BUSH_LARGE.CFMESH_PC")); // case-insensitive
    CHECK(!sr3foliage::looksLikeCfmeshFilename("fern01.gfmesh_pc")); // spec Sec2: no g-side extension, 0/19 observed
    CHECK(!sr3foliage::looksLikeCfmeshFilename("fern01.ccmesh_pc"));
    CHECK(!sr3foliage::looksLikeCfmeshFilename("no_extension_at_all"));

    // --- validateCfmeshContent: well-formed buffer; and one whose LOD tail
    // is inexact must now be reported not-well-formed. ---
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        std::vector<uint8_t> blob = buildCfmesh(opt);
        auto v = sr3foliage::validateCfmeshContent(blob);
        CHECK(v.status == sr3foliage::CfmeshValidation::WellFormed);

        opt.trailingBytes = 4;
        auto bad = sr3foliage::validateCfmeshContent(buildCfmesh(opt));
        CHECK(bad.status == sr3foliage::CfmeshValidation::NotWellFormed);
    }

    // --- validateCfmeshContent: truncated/corrupt buffer must fail. ---
    {
        std::vector<uint8_t> notCfmesh = {0x00, 0x01, 0x02, 0x03};
        auto v = sr3foliage::validateCfmeshContent(notCfmesh);
        CHECK(v.status == sr3foliage::CfmeshValidation::NotWellFormed);
    }

    // --- refineWithCfmeshValidation: PERMANENT NO-OP (HANDOFF.md §9.78 -
    // decompressEntry never produces OkUnconfirmedContent any more), passes
    // EVERY status through completely unchanged, including
    // OkUnconfirmedContent itself. ---
    {
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::Ok;
        in.data = {0x00, 0x01, 0x02};
        vpp::DecompressResult out = sr3foliage::refineWithCfmeshValidation(in);
        CHECK(out.status == vpp::DecodeStatus::Ok); // untouched
    }
    {
        FoliageBuildOptions opt;
        opt.lodRecords = threeLods();
        std::vector<uint8_t> blob = buildCfmesh(opt);
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = blob;
        vpp::DecompressResult out = sr3foliage::refineWithCfmeshValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op
        CHECK(out.data == blob);
    }
    {
        std::vector<uint8_t> notCfmesh = {0x00, 0x01, 0x02, 0x03};
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = notCfmesh;
        vpp::DecompressResult out = sr3foliage::refineWithCfmeshValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent); // unchanged - no-op, even for content that would have failed the old check
        CHECK(out.data == notCfmesh);
    }

    if (g_failures == 0) {
        std::cout << "All synthetic foliage-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
