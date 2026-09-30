// Synthetic tests for the sr3geometry material-block + geometry-block
// (raw six-array) readers. Builds buffers from scratch using only the
// CONFIRMED spec-geometry-format.md facts plus this session's own
// findings: the geometry-magic search (spec Sec4.1's 16-byte pad is
// sometimes a mandatory minimum gap, not a no-op when already aligned)
// and "BlobA" (spec Sec4.1.2 finding 2, Team A/disassembly-confirmed) - a
// real, explicit length-prefixed blob at the geometry block's +0x88 that
// must be skipped before array 1's data begins. See geometry_block.h's
// file-level comment for the full story, including the earlier, wrong
// version of this reader that tried to re-derive BlobA's length by
// walking null-terminated names instead of reading its real length field.

#include <iostream>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
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

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendCString(std::vector<uint8_t>& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0x00);
}

void padTo16(std::vector<uint8_t>& b) {
    while (b.size() % 16 != 0) b.push_back(0x00);
}

void padTo8(std::vector<uint8_t>& b) {
    while (b.size() % 8 != 0) b.push_back(0x00);
}

// Builds a well-formed .ccmesh_pc buffer: material block, geometry block
// (fixed header + gating fields + BlobA with a real length field, per the
// CONFIRMED shape), six arrays (filled with 0xCC placeholder bytes -
// content is opaque to this reader by design), and a trailing Mesh
// sub-block stub (just the version==9 marker this reader actually
// checks). `blobAContentLength` lets tests vary BlobA's declared length
// independently of the material names, since parse() no longer derives
// it from them at all - it just trusts the length field.
std::vector<uint8_t> buildCcmesh(const std::vector<std::string>& names,
                                  const std::array<size_t, 6>& counts, uint16_t version = 0x29,
                                  size_t blobAContentLength = 20) {
    std::vector<uint8_t> b;

    // Material block.
    appendU32(b, sr3geometry::kMaterialBlockMagic);
    appendU32(b, 0); // name-table-length placeholder, patched below
    appendU32(b, 0); // +0x08, unused
    appendU32(b, static_cast<uint32_t>(names.size()));
    b.insert(b.end(), 16, 0x00); // +0x10-+0x1F padding
    size_t nameTableStart = b.size();
    for (const auto& n : names) appendCString(b, n);
    uint32_t nameTableLength = static_cast<uint32_t>(b.size() - nameTableStart);
    b[0x04] = static_cast<uint8_t>(nameTableLength & 0xFF);
    b[0x05] = static_cast<uint8_t>((nameTableLength >> 8) & 0xFF);
    b[0x06] = static_cast<uint8_t>((nameTableLength >> 16) & 0xFF);
    b[0x07] = static_cast<uint8_t>((nameTableLength >> 24) & 0xFF);

    padTo16(b);
    size_t geomOffset = b.size();

    // Geometry block fixed header + gating fields, laid out at their
    // CONFIRMED offsets (spec Sec4.1's table), zero-filled elsewhere, out
    // to exactly BlobA's length-field offset (+0x88).
    std::vector<uint8_t> header(sr3geometry::kBlobALengthFieldOffset, 0x00);
    auto putU16At = [&](size_t off, uint16_t v) {
        header[off] = static_cast<uint8_t>(v & 0xFF);
        header[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };
    auto putU32At = [&](size_t off, uint32_t v) {
        header[off] = static_cast<uint8_t>(v & 0xFF);
        header[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        header[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        header[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };
    putU32At(0x00, sr3geometry::kGeometryBlockMagic);
    putU16At(0x04, version);
    putU16At(0x06, 0); // flags
    putU16At(0x08, static_cast<uint16_t>(counts[0])); // array1
    putU16At(0x0A, static_cast<uint16_t>(counts[3])); // array4
    putU16At(0x10, 1);                                 // f10, part of array6's formula
    putU16At(0x12, static_cast<uint16_t>(counts[4]));  // array5 raw count (version >= 0x2A uses this directly)
    putU16At(0x38, static_cast<uint16_t>(counts[1]));  // array2
    putU16At(0x3A, static_cast<uint16_t>(counts[2]));  // array3
    // f70 drives array6's count (paired with f10=1, spec Sec4.1's
    // formula) AND, per that same spec row, array5's EFFECTIVE count too
    // whenever version < 0x2A (overriding the raw +0x12 field entirely) -
    // a real coupling, not a test bug. Set it from counts[5] so array6
    // always gets what the caller asked for; callers wanting an
    // independently-different array5 count must pass version >= 0x2A so
    // array5 uses its own raw +0x12 field (set below) instead.
    putU32At(0x70, static_cast<uint32_t>(counts[5]));
    putU32At(0x78, counts[4] != 0 ? 1u : 0u);           // array5 gate
    putU32At(0x80, counts[5] != 0 ? 1u : 0u);           // array6 gate
    b.insert(b.end(), header.begin(), header.end());

    // BlobA: a 4-byte length field, a 5-byte pad, then that many content
    // bytes (opaque to this reader - placeholder content here, since
    // parse() only reads the length field, never the content itself).
    appendU32(b, static_cast<uint32_t>(blobAContentLength));
    b.insert(b.end(), sr3geometry::kBlobAContentPad, 0x00);
    b.insert(b.end(), blobAContentLength, 0xAB);

    // Array space, laid out per spec Sec4.1's confirmed rules (mirroring
    // layOutArrays() in geometry_block.cpp): 16-byte align before each
    // NON-EMPTY array; an empty array occupies nothing and triggers no
    // padding at all; then an 8-byte (not 16) align for the final step to
    // the trailer.
    padTo16(b);
    for (size_t i = 0; i < 6; ++i) {
        size_t byteLen = counts[i] * sr3geometry::kArrayStride[i];
        if (byteLen == 0) continue;
        padTo16(b);
        b.insert(b.end(), byteLen, 0xCC);
    }
    padTo8(b);

    // Mesh sub-block stub: just the version==9 marker this reader checks.
    appendU32(b, 9);
    b.insert(b.end(), 12, 0x00); // a little trailing content, not read

    (void)geomOffset;
    return b;
}

} // namespace

int main() {
    // --- Real-shape regression: mirrors brad_head_lod.ccmesh_pc's counts
    // (array2=1, array3=1, array4=3, everything else 0) with different
    // (shorter) synthetic names, checking the parser reproduces the right
    // array offsets/sizes for this exact count combination. ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga", "b.tga", "c.tga"}, {0, 1, 1, 3, 0, 0});
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        CHECK(m.textureSlotCount == 3);
        CHECK(m.textureNames.size() == 3);
        CHECK(m.textureNames[0] == "a.tga");

        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        CHECK(g.version() == 0x29);
        CHECK(g.arrays()[0].count == 0);
        CHECK(g.arrays()[1].count == 1);
        CHECK(g.arrays()[1].byteLength() == 24);
        CHECK(g.arrays()[2].count == 1);
        CHECK(g.arrays()[2].byteLength() == 40);
        CHECK(g.arrays()[3].count == 3);
        CHECK(g.arrays()[3].byteLength() == 12);
        // Every POPULATED array's offset must be 16-byte aligned (spec
        // Sec4.1's confirmed alignment rule). Empty arrays report the
        // plain cursor instead - they have no real location - so they're
        // deliberately not held to this.
        for (const auto& a : g.arrays()) {
            if (a.byteLength() != 0) {
                CHECK(a.offset % 16 == 0);
            }
        }
        CHECK(g.hasMeshSubBlock());

        sr3geometry::ByteView a2 = g.arrayBytes(1, content);
        CHECK(a2.size() == 24);
        sr3geometry::ByteView a3 = g.arrayBytes(2, content);
        CHECK(a3.size() == 40);
    }

    // --- Real-shape regression: mirrors cm_hand_f_leather.ccmesh_pc's
    // counts (only array4 populated, count=68 - the ascending index/remap
    // table shape), with 5 synthetic names. ---
    {
        std::vector<uint8_t> blob = buildCcmesh(
            {"n1.tga", "n2.tga", "n3.tga", "n4.tga", "n5.tga"}, {0, 0, 0, 68, 0, 0});
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        CHECK(m.textureSlotCount == 5);

        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        CHECK(g.arrays()[3].count == 68);
        CHECK(g.arrays()[3].byteLength() == 272);
        CHECK(g.hasMeshSubBlock());
    }

    // --- Real-shape regression: mirrors the RICHER real meshes (helena/
    // nightblade.ccmesh_pc) that the earlier, name-walking version of
    // this reader failed on outright - array2=1, array3=11, array4 in the
    // 80s/90s, with a BIGGER BlobA content length than the simpler
    // samples above (228/244 bytes on the real files; use a comparably
    // large synthetic length here rather than the small ones above, since
    // that size difference is exactly what broke the old approach). ---
    {
        std::vector<uint8_t> blob = buildCcmesh(
            {"n1.tga", "n2.tga", "n3.tga", "n4.tga", "n5.tga", "n6.tga", "n7.tga", "n8.tga",
             "n9.tga", "n10.tga"},
            {0, 1, 11, 88, 0, 0}, 0x29, 236);
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        CHECK(g.arrays()[1].count == 1 && g.arrays()[1].byteLength() == 24);
        CHECK(g.arrays()[2].count == 11 && g.arrays()[2].byteLength() == 440);
        CHECK(g.arrays()[3].count == 88 && g.arrays()[3].byteLength() == 352);
        CHECK(g.hasMeshSubBlock());
    }

    // --- Locks in the 8-byte (NOT 16-byte) final trailer alignment, spec
    // Sec4.1. Only array4 is populated, with count=2 -> 8 bytes, so the
    // last array ends 8 past a 16-boundary: the trailer must sit exactly
    // there, NOT rounded on to the next 16-boundary. This is the exact
    // shape that made every richer real mesh fail when this step used 16
    // (real example: helena.ccmesh_pc's last array ends at 0x5a8, which
    // is 8 mod 16, with its Mesh sub-block right at 0x5a8). A regression
    // back to roundUp16 here fails this test. ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {0, 0, 0, 2, 0, 0});
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        const auto& a4 = g.arrays()[3];
        CHECK(a4.offset % 16 == 0);
        CHECK(a4.byteLength() == 8);
        CHECK((a4.offset + a4.byteLength()) % 16 == 8); // precondition: 8 mod 16, so 8-vs-16 is distinguishable
        CHECK(g.meshSubBlockOffset() == a4.offset + a4.byteLength());
    }

    // --- Empty arrays must occupy nothing AND trigger no padding: with
    // arrays 1-3 empty, array4 must start exactly at the array-space
    // start, not 3 alignment steps past it. ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {0, 0, 0, 4, 0, 0});
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        // The intent is "no empty array consumes an alignment step" - array4
        // must be ONE alignment from the array-space start, not three.
        //
        // This used to assert all four offsets were EQUAL, which quietly
        // also assumed the array space itself began 16-aligned. It did,
        // because the reader pre-aligned it - and that pre-alignment was
        // the bug HANDOFF §9.26 fixes (it cost vehicles 8 bytes). So the
        // old assertion was testing the reader's convention rather than the
        // format, the same trap §9.16 records for fixtures. Restated as the
        // invariant that actually holds: empties share the cursor, and the
        // populated array sits one 16-alignment past it.
        CHECK(g.arrays()[0].offset == g.arrays()[1].offset); // empties collapse together
        CHECK(g.arrays()[1].offset == g.arrays()[2].offset);
        CHECK(g.arrays()[3].offset == sr3geometry::roundUp16(g.arrays()[0].offset));
        CHECK(g.arrays()[3].offset - g.arrays()[0].offset < 16); // ONE step, not three
    }

    // --- Zero-texture material block (edge case: no names at all). ---
    {
        std::vector<uint8_t> blob = buildCcmesh({}, {1, 0, 0, 0, 0, 0});
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        CHECK(m.textureSlotCount == 0);
        CHECK(m.textureNames.empty());

        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        CHECK(g.arrays()[0].count == 1);
        CHECK(g.arrays()[0].byteLength() == 96);
    }

    // --- Zero-length BlobA (edge case: no embedded name copy at all). ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {0, 0, 0, 1, 0, 0}, 0x29, 0);
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        CHECK(g.arrays()[3].count == 1);
    }

    // --- All six arrays populated at once, with different array5/array6
    // counts - requires version >= 0x2A so array5 uses its own raw +0x12
    // field independently of array6's f70 formula input (spec Sec4.1's
    // array-5 row; see buildCcmesh's own comment on this coupling). ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {2, 3, 1, 5, 2, 4}, 0x2A);
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        CHECK(g.arrays()[0].count == 2 && g.arrays()[0].byteLength() == 192);
        CHECK(g.arrays()[1].count == 3 && g.arrays()[1].byteLength() == 72);
        CHECK(g.arrays()[2].count == 1 && g.arrays()[2].byteLength() == 40);
        CHECK(g.arrays()[3].count == 5 && g.arrays()[3].byteLength() == 20);
        CHECK(g.arrays()[4].count == 2 && g.arrays()[4].byteLength() == 8);
        CHECK(g.arrays()[5].count == 4 && g.arrays()[5].byteLength() == 8);
        CHECK(g.hasMeshSubBlock());
        // Arrays must not overlap: each one's offset must be >= the
        // previous one's end.
        size_t prevEnd = 0;
        for (const auto& a : g.arrays()) {
            CHECK(a.offset >= prevEnd);
            prevEnd = a.offset + a.byteLength();
        }
    }

    // --- Negative: bad material-block magic must be rejected. ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {0, 0, 0, 1, 0, 0});
        blob[0] ^= 0xFF;
        bool threw = false;
        try {
            sr3geometry::MaterialBlock::parse(sr3geometry::ByteView(blob.data(), blob.size()));
        } catch (const sr3geometry::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Negative: bad geometry-block magic must be rejected. ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {0, 0, 0, 1, 0, 0});
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        size_t geomOffset = sr3geometry::roundUp16(m.totalSize);
        blob[geomOffset] ^= 0xFF;
        bool threw = false;
        try {
            sr3geometry::GeometryBlock::parse(sr3geometry::ByteView(blob.data(), blob.size()), m);
        } catch (const sr3geometry::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Negative: material block's declared name-table length not
    // matching the actual name bytes must be rejected. ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {0, 0, 0, 1, 0, 0});
        blob[0x04] ^= 0xFF; // corrupt the length field
        bool threw = false;
        try {
            sr3geometry::MaterialBlock::parse(sr3geometry::ByteView(blob.data(), blob.size()));
        } catch (const sr3geometry::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Negative: a corrupted BlobA length field (making the computed
    // array-space start land on garbage instead of the real arrays) must
    // be rejected via the Mesh-sub-block sanity check, not silently
    // produce wrong array offsets. ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {0, 1, 0, 1, 0, 0});
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        size_t geomOffset = sr3geometry::roundUp16(m.totalSize);
        // Corrupt BlobA's length field to a wildly wrong value.
        blob[geomOffset + sr3geometry::kBlobALengthFieldOffset] = 0xFF;
        blob[geomOffset + sr3geometry::kBlobALengthFieldOffset + 1] = 0xFF;
        bool threw = false;
        try {
            sr3geometry::GeometryBlock::parse(sr3geometry::ByteView(blob.data(), blob.size()), m);
        } catch (const sr3geometry::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Out-of-range arrayBytes index must be rejected. ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {0, 0, 0, 1, 0, 0});
        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        bool threw = false;
        try {
            g.arrayBytes(6, content);
        } catch (const sr3geometry::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- A geometry block sitting a FULL extra 16 bytes out (the case
    // where the material block's end is already 16-aligned, so a
    // mandatory extra pad is inserted - 21 of 549 real files in
    // characters.vpp_pc do this) must still be found by parse()'s magic
    // search, not missed as if the block were absent. ---
    {
        std::vector<uint8_t> blob = buildCcmesh({"a.tga"}, {0, 0, 0, 3, 0, 0});
        sr3geometry::ByteView probe(blob.data(), blob.size());
        sr3geometry::MaterialBlock m0 = sr3geometry::MaterialBlock::parse(probe);
        size_t naive = sr3geometry::roundUp16(m0.totalSize);
        // Splice 16 zero bytes in at the naive position, pushing the whole
        // geometry block one extra 16-byte block further out.
        blob.insert(blob.begin() + static_cast<std::ptrdiff_t>(naive), 16, 0x00);

        sr3geometry::ByteView content(blob.data(), blob.size());
        sr3geometry::MaterialBlock m = sr3geometry::MaterialBlock::parse(content);
        sr3geometry::GeometryBlock g = sr3geometry::GeometryBlock::parse(content, m);
        CHECK(g.offset() == naive + 16); // found one full block past the naive position
        CHECK(g.arrays()[3].count == 3);
        CHECK(g.hasMeshSubBlock());
    }

    if (g_failures == 0) {
        std::cout << "All synthetic ccmesh/geometry-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
