// Synthetic tests for the .cmorph_pc structural reader
// (spec-morph-format.md). The builder mirrors the loader's own walk, so
// these lock in the two things easiest to get wrong: the CONDITIONAL
// alignment (pad 0 when already aligned - NOT the mandatory-minimum pad
// .ccmesh_pc uses) and the exact-size/sentinel invariant that spec Sec4
// proves across all 1,541 real files.

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3morph/morph_file.h"

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

void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendF32(std::vector<uint8_t>& b, float v) {
    uint32_t raw = 0;
    std::memcpy(&raw, &v, sizeof(raw));
    appendU32(b, raw);
}

void alignTo(std::vector<uint8_t>& b, size_t a) {
    while (b.size() % a != 0) b.push_back(0x00);
}

struct TargetSpec {
    uint32_t id = 0;
    std::vector<uint16_t> recordVertexCounts; // one entry per descriptor record
};

// Builds a well-formed mode-1 .cmorph_pc exactly per spec Sec3.
std::vector<uint8_t> buildMorph(const std::vector<TargetSpec>& targets, uint32_t mode = 1,
                                 uint32_t outerVersion = sr3morph::kOuterVersion,
                                 uint32_t blockVersion = sr3morph::kBlockVersion) {
    std::vector<uint8_t> b;
    // Outer header (0x10)
    appendU32(b, sr3morph::kOuterMagic);
    appendU32(b, outerVersion);
    appendU32(b, 0);
    appendU32(b, 0);
    // Morph block header (0x18 at +0x10)
    appendU32(b, sr3morph::kBlockMagic);
    appendU32(b, blockVersion);
    appendU32(b, mode);
    appendU32(b, static_cast<uint32_t>(targets.size()));
    appendU32(b, 0);
    appendU32(b, 0);

    // Directory: count x 0x10
    for (const auto& t : targets) {
        appendU32(b, t.id);
        appendU32(b, static_cast<uint32_t>(t.recordVertexCounts.size()));
        appendU32(b, 0);
        appendU32(b, 0);
    }

    // Descriptor records, in target order, 0x28 each
    std::vector<uint16_t> flatCounts;
    for (const auto& t : targets) {
        for (uint16_t n : t.recordVertexCounts) {
            flatCounts.push_back(n);
            appendU32(b, 0);        // +0x00 zero
            appendU16(b, n);        // +0x04 N
            appendU16(b, n == 0 ? uint16_t{0} : static_cast<uint16_t>(n - 1)); // +0x06 max index
            appendF32(b, 0.015f); appendF32(b, 0.072f); appendF32(b, 0.066f);  // +0x08 paramsA
            appendF32(b, 1.72f);  appendF32(b, 1.52f);  appendF32(b, 1.23f);   // +0x14 paramsB
            appendU32(b, 0); appendU32(b, 0); // +0x20 runtime slot
        }
    }

    // Bulk runs: per descriptor, align 16, N x 12 bytes, align 16.
    for (uint16_t n : flatCounts) {
        alignTo(b, 16);
        for (uint16_t v = 0; v < n; ++v) {
            appendU16(b, 0x1111);  // +0 quantised
            appendU16(b, 0x2222);  // +2
            appendU16(b, 0x3333);  // +4
            appendU16(b, v);       // +6 vertex index (ascending, unique - as real files are)
            appendU16(b, 0x4444);  // +8
            appendU16(b, 100);     // +10 (< 4096)
        }
        alignTo(b, 16);
    }

    // Trailer: align 4, then the sentinel
    alignTo(b, 4);
    appendU32(b, sr3morph::kBlockMagic);
    return b;
}

} // namespace

int main() {
    // --- Baseline: one target, one record, a handful of vertices. ---
    {
        std::vector<uint8_t> blob = buildMorph({{0xAABBCCDDu, {4}}});
        sr3morph::ByteView content(blob.data(), blob.size());
        sr3morph::MorphFile m = sr3morph::MorphFile::parse(content);
        CHECK(m.mode() == 1);
        CHECK(m.targets().size() == 1);
        CHECK(m.targets()[0].id == 0xAABBCCDDu);
        CHECK(m.targets()[0].recordCount == 1);
        CHECK(m.descriptors().size() == 1);
        CHECK(m.descriptors()[0].affectedVertexCount == 4);
        CHECK(m.descriptors()[0].maxVertexIndex == 3);
        CHECK(m.descriptors()[0].bulkByteLength == 4 * 12);
        CHECK(m.descriptors()[0].bulkOffset % 16 == 0); // bulk runs are 16-aligned
        CHECK(m.totalElementCount() == 4);
        CHECK(m.trailerOffset() == blob.size() - 4);

        // The confirmed field: vertex index at element +6.
        for (size_t e = 0; e < 4; ++e) {
            sr3morph::Element el = m.elementAt(0, e, content);
            CHECK(el.vertexIndex == e);
            CHECK(el.component0 == 0x1111);
            CHECK(el.component1 == 0x2222);
            CHECK(el.component2 == 0x3333);
            CHECK(el.field_8 == 0x4444);
            CHECK(el.field_10 == 100);
        }
        CHECK(m.bulkBytes(0, content).size() == 4 * 12);
    }

    // --- Several targets with differing vertex counts, including counts
    // whose N x 12 is NOT 16-aligned - which is exactly what exercises the
    // trailing align-16 after each bulk run. ---
    {
        std::vector<uint8_t> blob =
            buildMorph({{1, {1}}, {2, {5}}, {3, {13}}, {4, {2}}});
        sr3morph::ByteView content(blob.data(), blob.size());
        sr3morph::MorphFile m = sr3morph::MorphFile::parse(content);
        CHECK(m.targets().size() == 4);
        CHECK(m.descriptors().size() == 4);
        CHECK(m.totalElementCount() == 1 + 5 + 13 + 2);
        for (const auto& d : m.descriptors()) {
            CHECK(d.bulkOffset % 16 == 0);
        }
        // Bulk runs must not overlap and must appear in descriptor order.
        for (size_t i = 1; i < m.descriptors().size(); ++i) {
            const auto& prev = m.descriptors()[i - 1];
            CHECK(m.descriptors()[i].bulkOffset >= prev.bulkOffset + prev.bulkByteLength);
        }
        // Spot-check the last target's indices.
        sr3morph::Element last = m.elementAt(3, 1, content);
        CHECK(last.vertexIndex == 1);
    }

    // --- A target carrying more than one descriptor record. The loader
    // supports n > 1 though no real file is known to use it (spec Sec11
    // item 4), so the walk must handle it rather than assume n == 1. ---
    {
        std::vector<uint8_t> blob = buildMorph({{7, {3, 6}}, {8, {2}}});
        sr3morph::ByteView content(blob.data(), blob.size());
        sr3morph::MorphFile m = sr3morph::MorphFile::parse(content);
        CHECK(m.targets().size() == 2);
        CHECK(m.targets()[0].recordCount == 2);
        CHECK(m.descriptors().size() == 3);
        CHECK(m.descriptors()[0].targetIndex == 0);
        CHECK(m.descriptors()[1].targetIndex == 0);
        CHECK(m.descriptors()[2].targetIndex == 1);
        CHECK(m.totalElementCount() == 3 + 6 + 2);
    }

    // --- A realistic NPC-head shape: 1 target, 1,426 affected vertices,
    // max index 1,425 (spec Sec2 - the 218 identical 17,220-byte files). ---
    {
        std::vector<uint8_t> blob = buildMorph({{0xFEEDFACEu, {1426}}});
        sr3morph::ByteView content(blob.data(), blob.size());
        sr3morph::MorphFile m = sr3morph::MorphFile::parse(content);
        CHECK(m.descriptors()[0].affectedVertexCount == 1426);
        CHECK(m.descriptors()[0].maxVertexIndex == 1425);
        CHECK(m.totalElementCount() == 1426);
    }

    // --- Negative: bad outer magic. ---
    {
        std::vector<uint8_t> blob = buildMorph({{1, {2}}});
        blob[0] ^= 0xFF;
        bool threw = false;
        try {
            sr3morph::MorphFile::parse(sr3morph::ByteView(blob.data(), blob.size()));
        } catch (const sr3morph::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Negative: bad inner ("Morph" block) magic. ---
    {
        std::vector<uint8_t> blob = buildMorph({{1, {2}}});
        blob[0x10] ^= 0xFF;
        bool threw = false;
        try {
            sr3morph::MorphFile::parse(sr3morph::ByteView(blob.data(), blob.size()));
        } catch (const sr3morph::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Negative: wrong versions must be rejected - the loader requires
    // outer 5 and block 3 exactly (spec Sec3.1/Sec3.2). ---
    {
        for (uint32_t bad : {0u, 4u, 6u}) {
            std::vector<uint8_t> blob = buildMorph({{1, {2}}}, 1, bad, sr3morph::kBlockVersion);
            bool threw = false;
            try {
                sr3morph::MorphFile::parse(sr3morph::ByteView(blob.data(), blob.size()));
            } catch (const sr3morph::FormatError&) {
                threw = true;
            }
            CHECK(threw);
        }
        for (uint32_t bad : {0u, 2u, 4u}) {
            std::vector<uint8_t> blob = buildMorph({{1, {2}}}, 1, sr3morph::kOuterVersion, bad);
            bool threw = false;
            try {
                sr3morph::MorphFile::parse(sr3morph::ByteView(blob.data(), blob.size()));
            } catch (const sr3morph::FormatError&) {
                threw = true;
            }
            CHECK(threw);
        }
    }

    // --- Negative: a mode outside 0..2 is rejected outright by the loader. ---
    {
        std::vector<uint8_t> blob = buildMorph({{1, {2}}}, 3);
        bool threw = false;
        try {
            sr3morph::MorphFile::parse(sr3morph::ByteView(blob.data(), blob.size()));
        } catch (const sr3morph::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Modes 0 and 2 are valid to the loader but put their bulk in a
    // .gmorph_pc, of which zero ship. This reader must refuse them
    // explicitly rather than mis-parse (spec Sec6). ---
    {
        for (uint32_t unsupported : {0u, 2u}) {
            std::vector<uint8_t> blob = buildMorph({{1, {2}}}, unsupported);
            bool threw = false;
            try {
                sr3morph::MorphFile::parse(sr3morph::ByteView(blob.data(), blob.size()));
            } catch (const sr3morph::FormatError&) {
                threw = true;
            }
            CHECK(threw);
        }
    }

    // --- Negative: the exact-size invariant. Appending a single trailing
    // byte must fail - the walk would no longer land on the end of the
    // file, and spec Sec4 proves it does for 1541/1541 real files. This is
    // the check that makes a successful parse a structural proof rather
    // than an assumption. ---
    {
        std::vector<uint8_t> blob = buildMorph({{1, {3}}});
        blob.push_back(0x00);
        bool threw = false;
        try {
            sr3morph::MorphFile::parse(sr3morph::ByteView(blob.data(), blob.size()));
        } catch (const sr3morph::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Negative: a corrupted trailing sentinel must fail even though
    // the size arithmetic still works out. ---
    {
        std::vector<uint8_t> blob = buildMorph({{1, {3}}});
        blob[blob.size() - 4] ^= 0xFF;
        bool threw = false;
        try {
            sr3morph::MorphFile::parse(sr3morph::ByteView(blob.data(), blob.size()));
        } catch (const sr3morph::FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // --- Negative: out-of-range accessor indices. ---
    {
        std::vector<uint8_t> blob = buildMorph({{1, {2}}});
        sr3morph::ByteView content(blob.data(), blob.size());
        sr3morph::MorphFile m = sr3morph::MorphFile::parse(content);
        bool threwA = false, threwB = false;
        try { m.bulkBytes(5, content); } catch (const sr3morph::FormatError&) { threwA = true; }
        try { m.elementAt(0, 99, content); } catch (const sr3morph::FormatError&) { threwB = true; }
        CHECK(threwA);
        CHECK(threwB);
    }

    if (g_failures == 0) {
        std::cout << "All synthetic morph-format tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
