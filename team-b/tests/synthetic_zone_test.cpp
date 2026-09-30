// Synthetic tests for the `.czh_pc`/`.czn_pc`/`.gzn_pc` zone reader
// (sr3zone), covering ONLY the CONFIRMED parts of these formats - the
// SR3Z zone header (spec-ctorless-types.md Sec5) and the embedded Mesh
// sub-block geometry (spec-zone-data-format.md Sec7/Sec9.4). There is
// deliberately no test here for `.czn_pc`'s top-level `{id, length}`
// record chain, since there is no reader for it - see
// include/sr3zone/zone_header.h and zone_geometry.h.
//
// Fixtures are built directly from the spec text (the material block's
// mandatory pad formula, the SR3Z fixed-header field table, and the Mesh
// sub-block's own pre-header/header/g-segment layout - the same layout
// tests/synthetic_ccmesh_vertex_test.cpp already builds from), never by
// calling into src/zone_header.cpp or src/zone_geometry.cpp - a fixture
// derived from the parser only proves the parser agrees with itself.

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"

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
        catch (const std::exception&) { threw = true; }                     \
        if (!threw) {                                                        \
            std::cerr << "CHECK FAILED (expected throw): " #expr             \
                      << " at " __FILE__ ":" << __LINE__ << "\n";            \
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

void appendCString(std::vector<uint8_t>& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0x00);
}

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

void putS16At(std::vector<uint8_t>& b, size_t off, int16_t v) {
    putU16At(b, off, static_cast<uint16_t>(v));
}

void putF32At(std::vector<uint8_t>& b, size_t off, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    putU32At(b, off, bits);
}

size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

// spec-geometry-format.md Sec3.1.1: the material block's trailing pad is
// MANDATORY, always 1-16 bytes, a full 16 when `end` is already
// 16-aligned. Built from the spec's own formula, independently of
// src/zone_header.cpp's private helper of the same shape.
size_t mandatoryPad16(size_t end) { return end + (16 - (end % 16)); }

// ============================================================
// Part A: ZoneHeader (.czh_pc) - material block + SR3Z.
// ============================================================

struct CzhOptions {
    std::vector<std::string> materialNames = {"dirt_tile_01_d_dt.tga", "fol_fern_a01.fmeshx"};
    uint32_t version = 29;
    uint16_t recordCount = 3;
    uint16_t fieldAt0x1E = 2;
    uint32_t runtimePointerSlot = 0; // "zero on disk in 2,971/2,971"
    bool corruptRecordTrailingByte = false; // appends one extra stray byte after the records
    // +0x0C..+0x17 (spec-world-streaming.md Sec10.7 bullet 2): the header
    // origin. Defaults to zero, matching every pre-existing test's fixture
    // (none of them cares about this field).
    float originX = 0.0f, originY = 0.0f, originZ = 0.0f;
    // When non-empty, used verbatim as the record array instead of the
    // auto-generated per-index byte pattern below, and its size DRIVES
    // recordCount (the `recordCount` field above is ignored in that case).
    std::vector<sr3zone::ZoneHeaderRecord> explicitRecords;
};

std::vector<uint8_t> buildCzh(const CzhOptions& opt) {
    std::vector<uint8_t> b;

    // --- Shared material-reference block (spec-geometry-format.md Sec3.1). ---
    appendU32(b, sr3geometry::kMaterialBlockMagic);
    appendU32(b, 0); // name-table-length placeholder, patched below
    appendU32(b, 0); // +0x08, confirmed zero
    appendU32(b, static_cast<uint32_t>(opt.materialNames.size()));
    b.insert(b.end(), 16, 0x00); // +0x10-+0x1F padding
    size_t nameTableStart = b.size();
    for (const auto& n : opt.materialNames) appendCString(b, n);
    uint32_t nameTableLength = static_cast<uint32_t>(b.size() - nameTableStart);
    putU32At(b, 0x04, nameTableLength);

    // --- Mandatory 16-byte pad, then SR3Z (spec-ctorless-types.md Sec5). ---
    size_t sr3zAt = mandatoryPad16(b.size());
    b.resize(sr3zAt, 0x00);

    b.resize(sr3zAt + 0x40, 0x00); // reserve the whole fixed header
    putU32At(b, sr3zAt + 0x00, sr3zone::kSR3ZMagic);
    putU32At(b, sr3zAt + 0x04, opt.version);
    // +0x08-+0x0B deliberately left zero - runtime, see zone_header.h.
    // +0x0C-+0x17: header origin (spec-world-streaming.md Sec10.7 bullet 2).
    putF32At(b, sr3zAt + 0x0C, opt.originX);
    putF32At(b, sr3zAt + 0x10, opt.originY);
    putF32At(b, sr3zAt + 0x14, opt.originZ);
    putU32At(b, sr3zAt + 0x18, opt.runtimePointerSlot);
    const uint16_t effectiveRecordCount =
        opt.explicitRecords.empty() ? opt.recordCount : static_cast<uint16_t>(opt.explicitRecords.size());
    putU16At(b, sr3zAt + 0x1C, effectiveRecordCount);
    putU16At(b, sr3zAt + 0x1E, opt.fieldAt0x1E);

    // --- record array. Explicit records win when given (used by the
    // record-field-accessor tests below); otherwise recordCount x 14
    // opaque bytes, content distinguishable per-record so a test can
    // confirm raw byte pass-through. ---
    if (!opt.explicitRecords.empty()) {
        for (const auto& rec : opt.explicitRecords) b.insert(b.end(), rec.begin(), rec.end());
    } else {
        for (uint16_t i = 0; i < opt.recordCount; ++i) {
            for (uint8_t k = 0; k < 14; ++k) {
                appendU8(b, static_cast<uint8_t>((i * 14 + k) & 0xFF));
            }
        }
    }
    if (opt.corruptRecordTrailingByte) appendU8(b, 0xEE);

    return b;
}

void testZoneHeaderWellFormed() {
    CzhOptions opt;
    std::vector<uint8_t> blob = buildCzh(opt);
    sr3zone::ZoneHeader h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size()));

    CHECK(h.materialBlock().textureNames.size() == 2);
    CHECK(h.materialBlock().textureNames[0] == "dirt_tile_01_d_dt.tga");
    CHECK(h.materialBlock().textureNames[1] == "fol_fern_a01.fmeshx");
    CHECK(h.version() == 29);
    CHECK(h.recordCount() == 3);
    CHECK(h.fieldAt0x1E() == 2);
    CHECK(h.runtimePointerSlotOnDisk() == 0);
    CHECK(h.records().size() == 3);
    for (uint16_t i = 0; i < 3; ++i) {
        for (uint8_t k = 0; k < 14; ++k) {
            CHECK(h.records()[i][k] == static_cast<uint8_t>((i * 14 + k) & 0xFF));
        }
    }
    // sr3zOffset() must equal the mandatory-pad-adjusted material block end.
    CHECK(h.sr3zOffset() == mandatoryPad16(h.materialBlock().totalSize));
}

void testZoneHeaderZeroRecords() {
    CzhOptions opt;
    opt.recordCount = 0;
    std::vector<uint8_t> blob = buildCzh(opt);
    sr3zone::ZoneHeader h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size()));
    CHECK(h.recordCount() == 0);
    CHECK(h.records().empty());
    // Must land exactly at end of content with nothing left over.
    CHECK(h.sr3zOffset() + 0x40 == blob.size());
}

// spec-geometry-format.md Sec3.1.1: when the material block already ends
// 16-aligned, a FULL EXTRA 16 bytes of pad still follow - not a no-op.
// Zero material names makes totalSize exactly kMaterialBlockHeaderSize
// (0x20), which is already 16-aligned, so this exercises exactly that case.
void testMandatoryPadAppliesFullWhenAlreadyAligned() {
    CzhOptions opt;
    opt.materialNames.clear();
    std::vector<uint8_t> blob = buildCzh(opt);
    sr3zone::ZoneHeader h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size()));
    CHECK(h.materialBlock().totalSize == sr3geometry::kMaterialBlockHeaderSize); // 0x20, already 16-aligned
    CHECK(h.materialBlock().totalSize % 16 == 0);
    // A naive round-up-to-16 (no-op here) would land at 0x20; the mandatory
    // rule must land a full 16 further out, at 0x30.
    CHECK(h.sr3zOffset() == h.materialBlock().totalSize + 16);
    CHECK(h.sr3zOffset() == 0x30);
}

void testZoneHeaderRejections() {
    // Bad SR3Z magic.
    {
        CzhOptions opt;
        std::vector<uint8_t> blob = buildCzh(opt);
        size_t sr3zAt = mandatoryPad16(
            sr3geometry::MaterialBlock::parse(sr3zone::ByteView(blob.data(), blob.size())).totalSize);
        blob[sr3zAt] ^= 0xFF;
        CHECK_THROWS(sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size())));
    }
    // Version below the accepted range (27-29).
    {
        CzhOptions opt;
        opt.version = 26;
        std::vector<uint8_t> blob = buildCzh(opt);
        CHECK_THROWS(sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size())));
    }
    // Version above the accepted range.
    {
        CzhOptions opt;
        opt.version = 30;
        std::vector<uint8_t> blob = buildCzh(opt);
        CHECK_THROWS(sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size())));
    }
    // Both boundary versions must be ACCEPTED.
    {
        CzhOptions opt27;
        opt27.version = 27;
        std::vector<uint8_t> blob27 = buildCzh(opt27);
        sr3zone::ZoneHeader h27 =
            sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob27.data(), blob27.size()));
        CHECK(h27.version() == 27);

        CzhOptions opt29;
        opt29.version = 29;
        std::vector<uint8_t> blob29 = buildCzh(opt29);
        sr3zone::ZoneHeader h29 =
            sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob29.data(), blob29.size()));
        CHECK(h29.version() == 29);
    }
    // Record array not landing exactly at end of content (spec's "replay is
    // exact" invariant) - one stray trailing byte must be rejected.
    {
        CzhOptions opt;
        opt.corruptRecordTrailingByte = true;
        std::vector<uint8_t> blob = buildCzh(opt);
        CHECK_THROWS(sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size())));
    }
    // Truncated: not even enough bytes for the fixed SR3Z header.
    {
        CzhOptions opt;
        std::vector<uint8_t> blob = buildCzh(opt);
        blob.resize(blob.size() - 0x30); // cut well into the fixed header
        CHECK_THROWS(sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size())));
    }
    // Truncated: not even enough bytes for the material block itself -
    // propagates sr3geometry::FormatError, still a throw from this API.
    {
        std::vector<uint8_t> tiny = {0x00, 0x01, 0x02, 0x03};
        CHECK_THROWS(sr3zone::ZoneHeader::parse(sr3zone::ByteView(tiny.data(), tiny.size())));
    }
}

// ============================================================
// Part A2: the three spec-world-streaming.md Sec10.7 side results -
// zone-type -> container-kind mapping (+0x1E), header origin
// (+0x0C..+0x17), and the record accessors (+0/+2/+4/+6/+12).
// ============================================================

// spec-world-streaming.md Sec10.7 bullet 1, quoted precisely from the
// switch's own case list. Every documented case value plus several
// "anything else" values (which must all land on Zone, the default
// bucket, the same as the common case value 2).
void testZoneTypeToContainerKindMapping() {
    using sr3zone::ZoneContainerKind;

    // The enum's own raw values must equal the engine's container-kind ids
    // (spec-world-streaming.md Sec10.2's kind-id table). Routed through a
    // runtime array rather than direct literal-vs-literal CHECKs, which
    // /W4 flags as C4127 ("conditional expression is constant").
    struct KindId { ZoneContainerKind kind; uint8_t expected; };
    const KindId kindIds[] = {
        {ZoneContainerKind::LevelAlwaysLoaded, 0x1B},
        {ZoneContainerKind::Zone, 0x1D},
        {ZoneContainerKind::InteriorZone, 0x1F},
        {ZoneContainerKind::LargeInteriorZone, 0x20},
        {ZoneContainerKind::Mission, 0x21},
        {ZoneContainerKind::LargeMission, 0x22},
    };
    for (const auto& kc : kindIds) {
        CHECK(static_cast<uint8_t>(kc.kind) == kc.expected);
    }

    const uint16_t levelAlwaysLoaded[] = {1, 3, 8, 10, 11, 13}; // 11: "never observed" but same bucket
    for (uint16_t v : levelAlwaysLoaded) {
        CHECK(sr3zone::ContainerKindForZoneType(v) == ZoneContainerKind::LevelAlwaysLoaded);
    }
    const uint16_t mission[] = {5, 6};
    for (uint16_t v : mission) {
        CHECK(sr3zone::ContainerKindForZoneType(v) == ZoneContainerKind::Mission);
    }
    CHECK(sr3zone::ContainerKindForZoneType(7) == ZoneContainerKind::InteriorZone);
    CHECK(sr3zone::ContainerKindForZoneType(9) == ZoneContainerKind::LargeInteriorZone);
    CHECK(sr3zone::ContainerKindForZoneType(12) == ZoneContainerKind::LargeMission);

    // "Anything else (incl. 2, the common case)" -> Zone. 2 is the
    // documented common case; 0, 4, 14, 100 and 65535 are NOT in any
    // documented bucket and must fall through to the same default.
    const uint16_t zoneBucket[] = {2, 0, 4, 14, 100, 65535};
    for (uint16_t v : zoneBucket) {
        CHECK(sr3zone::ContainerKindForZoneType(v) == ZoneContainerKind::Zone);
    }

    // Wired through ZoneHeader itself: fieldAt0x1E() stays the raw value
    // (backward compatibility), zoneContainerKind() resolves it.
    {
        CzhOptions opt;
        opt.fieldAt0x1E = 7;
        opt.recordCount = 0;
        std::vector<uint8_t> blob = buildCzh(opt);
        sr3zone::ZoneHeader h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size()));
        CHECK(h.fieldAt0x1E() == 7);
        CHECK(h.zoneContainerKind() == ZoneContainerKind::InteriorZone);
    }
    {
        CzhOptions opt;
        opt.fieldAt0x1E = 12;
        opt.recordCount = 0;
        std::vector<uint8_t> blob = buildCzh(opt);
        sr3zone::ZoneHeader h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size()));
        CHECK(h.fieldAt0x1E() == 12);
        CHECK(h.zoneContainerKind() == ZoneContainerKind::LargeMission);
    }
}

// spec-world-streaming.md Sec10.7 bullet 2: +0x0C..+0x17 read as three f32
// forming the header's world-space origin. Zero-by-default (matching every
// pre-existing fixture, none of which set it) and an exact round-trip for
// a representative non-zero value, including a value that is NOT exactly
// representable to catch any accidental double-narrowing bug (the parser
// must read the SAME 4 bytes back, not re-derive a nearby float).
void testHeaderOrigin() {
    // Zero records, zero origin - the documented non-error "empty file"
    // case, not a parse() failure.
    {
        CzhOptions opt;
        opt.recordCount = 0;
        std::vector<uint8_t> blob = buildCzh(opt);
        sr3zone::ZoneHeader h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size()));
        CHECK(h.headerOrigin()[0] == 0.0f);
        CHECK(h.headerOrigin()[1] == 0.0f);
        CHECK(h.headerOrigin()[2] == 0.0f);
        CHECK(h.recordCount() == 0);
    }
    // Non-zero origin, exact float round-trip (values chosen to include a
    // negative component and a non-integer component).
    {
        CzhOptions opt;
        opt.recordCount = 1;
        opt.originX = 1402.0f;
        opt.originY = -28.0f;
        opt.originZ = 0.125f; // exactly representable, catches truncation
        std::vector<uint8_t> blob = buildCzh(opt);
        sr3zone::ZoneHeader h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size()));
        CHECK(h.headerOrigin()[0] == 1402.0f);
        CHECK(h.headerOrigin()[1] == -28.0f);
        CHECK(h.headerOrigin()[2] == 0.125f);
    }
}

// Builds one raw 14-byte ZoneHeaderRecord from typed fields, exactly as
// spec-world-streaming.md Sec10.7 bullet 3 lays them out. `b8`/`b10` are
// arbitrary marker bytes for the still-OPEN fields, used only to confirm
// the new accessors do not disturb raw byte access to them.
sr3zone::ZoneHeaderRecord makeRecord(int16_t x, int16_t y, int16_t z, int16_t six, uint8_t b8lo,
                                     uint8_t b8hi, uint8_t b10lo, uint8_t b10hi, uint16_t nameIdx) {
    std::vector<uint8_t> tmp(14, 0x00);
    putS16At(tmp, 0, x);
    putS16At(tmp, 2, y);
    putS16At(tmp, 4, z);
    putS16At(tmp, 6, six);
    tmp[8] = b8lo; tmp[9] = b8hi;
    tmp[10] = b10lo; tmp[11] = b10hi;
    putU16At(tmp, 12, nameIdx);
    sr3zone::ZoneHeaderRecord rec{};
    for (size_t i = 0; i < 14; ++i) rec[i] = tmp[i];
    return rec;
}

// spec-world-streaming.md Sec10.7 bullet 3: +0/+2/+4 s16 position words
// (1/64 m per unit), +6 s16 raw AND its separate 2^-12 hypothesis scaling,
// +12 u16 name index, +8/+10 left as raw untouched bytes.
void testRecordFieldAccessors() {
    // Record 0: positive values, including a field-six value that scales
    // to an exact 1.0 under the hypothesis (4096 * 2^-12 == 1.0).
    sr3zone::ZoneHeaderRecord rec0 = makeRecord(/*x=*/64, /*y=*/-128, /*z=*/320, /*six=*/4096,
                                                /*b8lo=*/0xAA, /*b8hi=*/0xBB, /*b10lo=*/0xCC,
                                                /*b10hi=*/0xDD, /*nameIdx=*/4321);
    std::array<float, 3> pos0 = sr3zone::RecordPosition(rec0);
    CHECK(pos0[0] == 1.0f);   //  64 / 64
    CHECK(pos0[1] == -2.0f);  // -128 / 64
    CHECK(pos0[2] == 5.0f);   //  320 / 64
    CHECK(sr3zone::RecordFieldSixRaw(rec0) == 4096);
    CHECK(sr3zone::RecordFieldSixAsHypothesizedRadians(rec0) == 1.0f);
    CHECK(sr3zone::RecordNameIndex(rec0) == 4321);
    // +8/+10 are untouched by any accessor - still readable raw, exactly
    // as constructed, proving the new accessors did not repurpose them.
    CHECK(rec0[8] == 0xAA);
    CHECK(rec0[9] == 0xBB);
    CHECK(rec0[10] == 0xCC);
    CHECK(rec0[11] == 0xDD);

    // Record 1: negative field six, zero name index, zero position - the
    // other edge of the s16/u16 ranges exercised above.
    sr3zone::ZoneHeaderRecord rec1 = makeRecord(/*x=*/0, /*y=*/0, /*z=*/-64, /*six=*/-4096,
                                                /*b8lo=*/0x00, /*b8hi=*/0x00, /*b10lo=*/0x00,
                                                /*b10hi=*/0x00, /*nameIdx=*/0);
    std::array<float, 3> pos1 = sr3zone::RecordPosition(rec1);
    CHECK(pos1[0] == 0.0f);
    CHECK(pos1[1] == 0.0f);
    CHECK(pos1[2] == -1.0f); // -64 / 64
    CHECK(sr3zone::RecordFieldSixRaw(rec1) == -4096);
    CHECK(sr3zone::RecordFieldSixAsHypothesizedRadians(rec1) == -1.0f);
    CHECK(sr3zone::RecordNameIndex(rec1) == 0);

    // End-to-end through ZoneHeader::parse(): records travel through the
    // parser byte-for-byte (already covered by testZoneHeaderWellFormed's
    // pattern check), so confirm the accessors agree when read off a
    // parsed header's records() rather than a hand-built record directly.
    {
        CzhOptions opt;
        opt.explicitRecords = {rec0, rec1};
        opt.originX = 100.0f;
        opt.originY = 200.0f;
        opt.originZ = 300.0f;
        std::vector<uint8_t> blob = buildCzh(opt);
        sr3zone::ZoneHeader h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(blob.data(), blob.size()));
        CHECK(h.recordCount() == 2);
        CHECK(h.records().size() == 2);
        std::array<float, 3> p = sr3zone::RecordPosition(h.records()[0]);
        CHECK(p[0] == 1.0f);
        CHECK(p[1] == -2.0f);
        CHECK(p[2] == 5.0f);
        // The caller adds the header origin itself - RecordPosition() does
        // NOT do this (a record carries no back-reference to its header).
        CHECK(h.headerOrigin()[0] + p[0] == 101.0f);
        CHECK(h.headerOrigin()[1] + p[1] == 198.0f);
        CHECK(h.headerOrigin()[2] + p[2] == 305.0f);
        CHECK(sr3zone::RecordNameIndex(h.records()[1]) == 0);
    }
}

// ============================================================
// Part B: ZoneGeometry - embedded Mesh sub-blocks in .czn_pc/.gzn_pc.
// ============================================================
//
// Builds the same pre-header/header/channel-record/g-segment layout as
// tests/synthetic_ccmesh_vertex_test.cpp's buildMesh(), generalized to (a)
// place the block at an arbitrary offset inside a larger .czn_pc buffer
// (simulating it sitting nested inside an unrelated top-level record, per
// spec-zone-data-format.md Sec8.1's confirmed finding that it never sits
// at a chunk boundary) and (b) place its g-backed segment at an arbitrary
// 16-aligned offset inside .gzn_pc (simulating more than one Mesh block
// sharing one g-file, spec Sec7.3). Vertices are position-only (layout
// code 4) since ZoneGeometry::locate() only needs to find and structurally
// validate blocks - per-layout vertex decoding is sr3mesh's own,
// already-covered concern.

struct SynthZoneMesh {
    uint32_t checkValue = 0xC0FFEE01u;
    std::vector<uint16_t> indices = {0, 1, 2};
    uint32_t vertexCount = 3;
    bool gBacked = true;
};

// Where the 0x70-byte header starts relative to a 4-aligned anchor:
// `round_up(anchor + 16, 8) - anchor`, i.e. +0x10 for an anchor at 0
// (mod 8) and +0x14 for one at 4 (mod 8), the extra word being a zero
// filler (spec-zone-data-format.md Sec10.1; HANDOFF Sec9.55.2 dumped the
// real bytes of one such block: anchor `czn+0xdc`, `+0xec` = 00 00 00 00
// filler, `+0xf0` = the real flags word).
//
// CORRECTED 2026-09-13: this fixture builder previously hard-coded +0x10
// for every anchor, which is a layout real `.czn_pc` content does not
// contain at a 4 (mod 8) anchor - so the fixtures at such anchors were
// modelling a file shape that does not exist. Two of the cases below sit
// at 4 (mod 8) and now emit the filler word, as shipped data does.
size_t headerDisplacementFor(size_t anchor) { return alignUp(anchor + 16, 8) - anchor; }

// Appends one Mesh sub-block to `czn` (after `cznPrefixPad` filler bytes),
// and - if gBacked - appends its segment to `gzn` after `gznPrefixPad`
// filler bytes. `gzn16Align` then rounds the segment start up to the next
// 16-byte boundary (the layout only segment 0 and `~al` zones' segments
// have); passing false places it exactly where the running total lands,
// which is the ordinary contiguous-chain layout (spec Sec10.4/Sec10.5).
// Returns the anchor offsets via out-params.
void appendZoneMesh(std::vector<uint8_t>& czn, std::vector<uint8_t>& gzn, size_t cznPrefixPad,
                    size_t gznPrefixPad, const SynthZoneMesh& m, size_t& outCznOffset,
                    size_t& outGznOffset, bool gzn16Align = true) {
    czn.insert(czn.end(), cznPrefixPad, 0xAB); // unrelated top-level content, per Sec8.1

    outCznOffset = czn.size();

    const size_t headerAt = headerDisplacementFor(outCznOffset);
    const size_t headerSize = 0x70;
    const size_t recordsAt = headerAt + headerSize;
    const uint32_t channelCount = 1;
    const size_t stride = 12; // layout code 4: position only, spec-vertex-format.md Sec5

    std::vector<uint8_t> block(recordsAt + channelCount * 24, 0x00);
    putU32At(block, 0x00, 9); // Mesh sub-block version, CONFIRMED anchor
    putU32At(block, 0x04, m.checkValue);
    uint8_t flags = m.gBacked ? 0x01 : 0x00;
    block[headerAt + 0x00] = flags;
    putU32At(block, headerAt + 0x10, channelCount);
    putU32At(block, headerAt + 0x18, 0);
    putU32At(block, headerAt + 0x20, static_cast<uint32_t>(m.indices.size()));
    putU32At(block, headerAt + 0x28, 0);
    block[headerAt + 0x30] = 2; // indexElementSize

    putU32At(block, recordsAt + 0x00, m.vertexCount);
    block[recordsAt + 0x04] = static_cast<uint8_t>(stride); // sizeA
    block[recordsAt + 0x05] = 4;                            // layoutCode 4 = position only
    block[recordsAt + 0x06] = 0;                            // texcoordCount
    block[recordsAt + 0x07] = 0;                             // sizeB

    // Where this segment will begin, decided BEFORE it is built: its
    // internal 16-byte padding is measured against the g-file's ABSOLUTE
    // grid, not against its own start, so a segment at 4 (mod 16) is
    // genuinely a different length from the same segment at 0 (mod 16)
    // (spec Sec10.4, re-derived byte-level on `sr3_city~s0715`).
    size_t gStart = 0;
    if (m.gBacked) {
        gStart = gzn.size() + gznPrefixPad;
        if (gzn16Align) gStart = alignUp(gStart, 16);
    }
    const size_t gridAnchor = m.gBacked ? gStart : 0; // inline stays segment-relative
    auto segAlign16 = [gridAnchor](size_t cursorIn) {
        return alignUp(gridAnchor + cursorIn, 16) - gridAnchor;
    };

    // Build the g-segment (spec-vertex-format.md Sec4): check value, align
    // 16, index buffer, align 16, channel data, align 4, check value again.
    std::vector<uint8_t> seg;
    appendU32(seg, m.checkValue);
    seg.resize(segAlign16(seg.size()), 0x00);
    for (uint16_t idx : m.indices) {
        seg.resize(seg.size() + 2);
        putU16At(seg, seg.size() - 2, idx);
    }
    seg.resize(segAlign16(seg.size()), 0x00);
    seg.resize(seg.size() + static_cast<size_t>(m.vertexCount) * stride, 0x00);
    seg.resize(alignUp(seg.size(), 4), 0x00);
    appendU32(seg, m.checkValue);
    uint32_t gLength = static_cast<uint32_t>(seg.size());

    if (m.gBacked) {
        putU32At(block, 0x08, static_cast<uint32_t>(block.size())); // cLength - not load-bearing
        putU32At(block, 0x0C, gLength);
        czn.insert(czn.end(), block.begin(), block.end());

        gzn.resize(gStart, 0x00);
        outGznOffset = gStart;
        gzn.insert(gzn.end(), seg.begin(), seg.end());
    } else {
        // Inline: the segment lives directly in `block`, right after the
        // channel records (spec-vertex-format.md Sec9 step 5).
        putU32At(block, 0x0C, gLength);
        block.insert(block.end(), seg.begin(), seg.end());
        putU32At(block, 0x08, static_cast<uint32_t>(block.size()));
        czn.insert(czn.end(), block.begin(), block.end());
        outGznOffset = 0; // unused for inline blocks
    }
}

void testZoneGeometrySingleGBackedBlock() {
    std::vector<uint8_t> czn, gzn;
    size_t cznOff = 0, gznOff = 0;
    SynthZoneMesh m;
    m.checkValue = 0xAAAA0001u;
    m.indices = {0, 1, 2, 3};
    m.vertexCount = 4;
    // cznPrefixPad must be 4-aligned: every other structure in this engine
    // is 4-aligned (spec-zone-data-format.md Sec1: ".czn_pc is always a
    // multiple of 4"), and ZoneGeometry::locate() scans 4-aligned offsets
    // only, matching that convention.
    appendZoneMesh(czn, gzn, /*cznPrefixPad=*/36, /*gznPrefixPad=*/5, m, cznOff, gznOff);

    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView(gzn.data(), gzn.size()));
    CHECK(found.size() == 1);
    if (found.size() == 1) {
        CHECK(found[0].cznOffset == cznOff);
        CHECK(found[0].fromGFile);
        CHECK(found[0].gznOffset == gznOff);
        CHECK(found[0].block.checkValue() == 0xAAAA0001u);
        CHECK(found[0].block.indices().size() == 4);
        CHECK(found[0].block.bulkInGFile());
    }
}

void testZoneGeometrySingleInlineBlock() {
    std::vector<uint8_t> czn, gzn; // no .gzn_pc at all - the 81/2,971 case
    size_t cznOff = 0, gznOff = 0;
    SynthZoneMesh m;
    m.checkValue = 0xBBBB0002u;
    m.indices = {0, 1, 2};
    m.vertexCount = 3;
    m.gBacked = false;
    appendZoneMesh(czn, gzn, /*cznPrefixPad=*/12, /*gznPrefixPad=*/0, m, cznOff, gznOff);
    CHECK(gzn.empty()); // never touched for an inline block

    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView()); // empty gContent
    CHECK(found.size() == 1);
    if (found.size() == 1) {
        CHECK(found[0].cznOffset == cznOff);
        CHECK(!found[0].fromGFile);
        CHECK(!found[0].block.bulkInGFile());
        CHECK(found[0].block.checkValue() == 0xBBBB0002u);
        CHECK(found[0].block.indices().size() == 3);
    }
}

// spec-zone-data-format.md Sec7.3/Sec10.4: a zone can embed several Mesh
// blocks sharing one `.gzn_pc`, and their g-segments form one CONTIGUOUS
// CHAIN in the same order as their c-side descriptors - segment k+1 starts
// exactly where segment k ended. Inline blocks sit in the c-file only and
// must not disturb the g-side cursor.
//
// REWRITTEN 2026-09-13. The previous version of this test placed the
// second g-backed segment 96 bytes AFTER the end of the first and asserted
// it was still found, "proving locate() does not depend on a threaded
// running g-cursor". That property is now refuted on real data (Sec10.4,
// re-derived here on `sr3_city~s0715`: 12 of 12 segments purely additive,
// tiling to EOF with no gap), and the independent 16-aligned search it was
// pinning reached only 7.28% of real blocks. The fixture now models the
// confirmed layout.
void testZoneGeometryMultipleBlocksMixedMode() {
    std::vector<uint8_t> czn, gzn;
    size_t off1c = 0, off1g = 0, off2c = 0, off2g = 0, off3c = 0, off3g = 0;

    SynthZoneMesh a;
    a.checkValue = 0x11110001u;
    a.indices = {0, 1, 2};
    a.vertexCount = 3;
    a.gBacked = true;
    appendZoneMesh(czn, gzn, 20, 0, a, off1c, off1g);

    SynthZoneMesh b; // inline, sandwiched between two g-backed blocks
    b.checkValue = 0x22220002u;
    b.indices = {0, 1, 2, 3, 4};
    b.vertexCount = 5;
    b.gBacked = false;
    appendZoneMesh(czn, gzn, 44, 0, b, off2c, off2g);

    SynthZoneMesh c;
    c.checkValue = 0x33330003u;
    c.indices = {0, 1, 2, 3};
    c.vertexCount = 4;
    c.gBacked = true;
    // Contiguous with `a`'s segment: no prefix pad, no 16-alignment.
    appendZoneMesh(czn, gzn, 8, 0, c, off3c, off3g, /*gzn16Align=*/false);

    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView(gzn.data(), gzn.size()));
    CHECK(found.size() == 3);

    bool sawA = false, sawB = false, sawC = false;
    for (const auto& e : found) {
        if (e.block.checkValue() == 0x11110001u) {
            sawA = true;
            CHECK(e.cznOffset == off1c);
            CHECK(e.fromGFile);
            CHECK(e.gznOffset == off1g);
            CHECK(e.block.indices().size() == 3);
        } else if (e.block.checkValue() == 0x22220002u) {
            sawB = true;
            CHECK(e.cznOffset == off2c);
            CHECK(!e.fromGFile);
            CHECK(e.block.indices().size() == 5);
        } else if (e.block.checkValue() == 0x33330003u) {
            sawC = true;
            CHECK(e.cznOffset == off3c);
            CHECK(e.fromGFile);
            CHECK(e.gznOffset == off3g);
            CHECK(e.block.indices().size() == 4);
        }
    }
    CHECK(sawA);
    CHECK(sawB);
    CHECK(sawC);
    // The chain must be exactly additive: segment 1 begins where segment 0
    // ended, and the inline block in between contributed nothing to it.
    CHECK(off1g == 0);
    CHECK(off3g == off1g + found[0].block.gLength());
    CHECK(gzn.size() == off3g + found[2].block.gLength()); // tiles to EOF
}

// The case the old 16-aligned search could not reach AT ALL, and the case
// a segment-RELATIVE 16-byte grid gets the wrong length for: a chain whose
// second segment lands at 4 (mod 16).
//
// Segment 0 here is built to be 84 bytes long (4 + pad to 16, 3 indices,
// pad to 16, 4 vertices x 12 bytes, + 4 trailing check), and 84 mod 16 = 4,
// so segment 1 starts at g-offset 84. That makes this test fail in two
// independent ways if either half of the 2026-09-13 fix is reverted:
//
//   * locate() searching 16-aligned g-offsets instead of chaining never
//     finds offset 84 at all - block 1 disappears;
//   * MeshBlock::parse() aligning to the segment's own start instead of
//     the g-file's absolute grid computes 72 bytes for segment 1 where the
//     descriptor declares 68, so the exact-length contract throws.
void testZoneGeometryChainSegmentNotSixteenAligned() {
    std::vector<uint8_t> czn, gzn;
    size_t off1c = 0, off1g = 0, off2c = 0, off2g = 0;

    SynthZoneMesh a;
    a.checkValue = 0x5A5A0001u;
    a.indices = {0, 1, 2};
    a.vertexCount = 4; // -> declared g-length 84, i.e. 4 (mod 16)
    appendZoneMesh(czn, gzn, 8, 0, a, off1c, off1g);

    SynthZoneMesh b;
    b.checkValue = 0x5A5A0002u;
    b.indices = {0, 1, 2};
    b.vertexCount = 3;
    appendZoneMesh(czn, gzn, 8, 0, b, off2c, off2g, /*gzn16Align=*/false);

    CHECK(off1g == 0);
    CHECK(off2g == 84);
    CHECK(off2g % 16 == 4); // the whole point: NOT reachable by a 16-aligned search

    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView(gzn.data(), gzn.size()));
    CHECK(found.size() == 2);
    if (found.size() == 2) {
        CHECK(found[0].gznOffset == 0);
        CHECK(found[0].block.gLength() == 84);
        CHECK(found[1].gznOffset == 84);
        CHECK(found[1].block.gLength() == 68); // 72 under a segment-relative grid
        CHECK(found[1].block.indices().size() == 3);
    }
}

// `~al` ("always loaded") zones pad between segments to the next 16-byte
// boundary (spec Sec10.5 sampled "97 of 928 tiling files use a skip", all
// `sr3_city~fNNNN~al`; superseded by this project's 1,002/1,002 tiling with
// 115 padded blocks, spec Sec10.8, which leaves which zones pad OPEN).
// The walk must try that one extra computed position - still arithmetic,
// still not a search.
void testZoneGeometryChainWithSixteenAlignedPadding() {
    std::vector<uint8_t> czn, gzn;
    size_t off1c = 0, off1g = 0, off2c = 0, off2g = 0;

    SynthZoneMesh a;
    a.checkValue = 0x7E7E0001u;
    a.indices = {0, 1, 2};
    a.vertexCount = 4; // declared g-length 84 -> chain cursor lands at 84
    appendZoneMesh(czn, gzn, 8, 0, a, off1c, off1g);

    SynthZoneMesh b;
    b.checkValue = 0x7E7E0002u;
    b.indices = {0, 1, 2};
    b.vertexCount = 3;
    appendZoneMesh(czn, gzn, 8, 0, b, off2c, off2g, /*gzn16Align=*/true); // pad 84 -> 96

    CHECK(off2g == 96);

    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView(gzn.data(), gzn.size()));
    CHECK(found.size() == 2);
    if (found.size() == 2) {
        CHECK(found[1].gznOffset == 96);
    }
}

// A block whose anchor sits at 4 (mod 8) carries a zero filler word at
// +0x10 and its real header at +0x14 (spec Sec10.1; HANDOFF Sec9.55.2
// measured 247 such anchors against 2,596 at +0x10, with zero ambiguous).
// Both parities must be located from the same scan.
void testZoneGeometryBothHeaderDisplacements() {
    std::vector<uint8_t> czn, gzn;
    size_t off1c = 0, off1g = 0, off2c = 0, off2g = 0;

    SynthZoneMesh a;
    a.checkValue = 0x9E9E0001u;
    a.indices = {0, 1, 2};
    a.vertexCount = 4;
    appendZoneMesh(czn, gzn, 8, 0, a, off1c, off1g); // anchor at 8 -> 0 (mod 8)

    SynthZoneMesh b;
    b.checkValue = 0x9E9E0002u;
    b.indices = {0, 1, 2};
    b.vertexCount = 3;
    appendZoneMesh(czn, gzn, 4, 0, b, off2c, off2g, /*gzn16Align=*/false);

    CHECK(off1c % 8 == 0);
    CHECK(off2c % 8 == 4); // the filler-word case

    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView(gzn.data(), gzn.size()));
    CHECK(found.size() == 2);
    if (found.size() == 2) {
        CHECK(found[0].block.checkValue() == 0x9E9E0001u);
        CHECK(found[1].block.checkValue() == 0x9E9E0002u);
    }
}

// A candidate whose declared g-length does not describe a real segment at
// the predicted chain position must be skipped WITHOUT moving the cursor,
// so the genuine block behind it still lands. This is the "handle
// gracefully" half of the chain walk: the bookend is a confirmation of an
// arithmetically derived offset, and when it fails the offset is simply
// not trusted.
void testZoneGeometryBadChainCandidateDoesNotDerailTheChain() {
    std::vector<uint8_t> czn, gzn;
    size_t off1c = 0, off1g = 0, off2c = 0, off2g = 0, off3c = 0, off3g = 0;

    SynthZoneMesh a;
    a.checkValue = 0x4D4D0001u;
    a.indices = {0, 1, 2};
    a.vertexCount = 4;
    appendZoneMesh(czn, gzn, 8, 0, a, off1c, off1g);

    // A well-formed-looking descriptor whose segment is NOT in the g-file
    // (its check value appears nowhere). Built g-backed, then its segment
    // bytes are dropped by rewinding the g-file to where it was.
    size_t gznSizeBefore = gzn.size();
    SynthZoneMesh bogus;
    bogus.checkValue = 0xDEAD0BAD;
    bogus.indices = {0, 1, 2};
    bogus.vertexCount = 3;
    appendZoneMesh(czn, gzn, 8, 0, bogus, off2c, off2g, /*gzn16Align=*/false);
    gzn.resize(gznSizeBefore);

    SynthZoneMesh c; // the real next link, still at the ORIGINAL cursor
    c.checkValue = 0x4D4D0003u;
    c.indices = {0, 1, 2};
    c.vertexCount = 3;
    appendZoneMesh(czn, gzn, 8, 0, c, off3c, off3g, /*gzn16Align=*/false);

    CHECK(off3g == 84); // unchanged by the bogus descriptor in between

    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView(gzn.data(), gzn.size()));
    CHECK(found.size() == 2);
    if (found.size() == 2) {
        CHECK(found[0].block.checkValue() == 0x4D4D0001u);
        CHECK(found[1].block.checkValue() == 0x4D4D0003u);
        CHECK(found[1].gznOffset == 84);
    }
}

// A coincidental "9" at a 4-aligned offset that is NOT a real Mesh
// sub-block must be silently skipped, not reported as a spurious block.
void testZoneGeometryRejectsCoincidentalVersionWord() {
    std::vector<uint8_t> czn, gzn;
    size_t off = 0, offg = 0;
    SynthZoneMesh m;
    appendZoneMesh(czn, gzn, /*cznPrefixPad=*/0, /*gznPrefixPad=*/0, m, off, offg);

    // Inject a bare "9" word, 4-aligned, into otherwise-unrelated trailing
    // content - not followed by anything resembling a real Mesh header.
    size_t junkAt = czn.size();
    czn.insert(czn.end(), 64, 0x00);
    putU32At(czn, junkAt + 8, 9); // 4-aligned, isolated

    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView(gzn.data(), gzn.size()));
    CHECK(found.size() == 1); // only the one real block, the injected "9" is rejected
}

// No .gzn_pc at all and a g-backed-only block: must find nothing (not
// guess, not crash) - the honest "cannot locate/validate" outcome.
void testZoneGeometryGBackedBlockWithoutGFileFindsNothing() {
    std::vector<uint8_t> czn, gzn;
    size_t off = 0, offg = 0;
    SynthZoneMesh m; // default gBacked = true
    appendZoneMesh(czn, gzn, 0, 0, m, off, offg);

    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView()); // no g-file supplied
    CHECK(found.empty());
}

void testZoneGeometryEmptyCznFindsNothing() {
    std::vector<uint8_t> czn, gzn;
    auto found = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(czn.data(), czn.size()),
                                               sr3zone::ByteView(gzn.data(), gzn.size()));
    CHECK(found.empty());
}

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    run("testZoneHeaderWellFormed", testZoneHeaderWellFormed);
    run("testZoneHeaderZeroRecords", testZoneHeaderZeroRecords);
    run("testMandatoryPadAppliesFullWhenAlreadyAligned",
        testMandatoryPadAppliesFullWhenAlreadyAligned);
    run("testZoneHeaderRejections", testZoneHeaderRejections);

    run("testZoneTypeToContainerKindMapping", testZoneTypeToContainerKindMapping);
    run("testHeaderOrigin", testHeaderOrigin);
    run("testRecordFieldAccessors", testRecordFieldAccessors);

    run("testZoneGeometrySingleGBackedBlock", testZoneGeometrySingleGBackedBlock);
    run("testZoneGeometrySingleInlineBlock", testZoneGeometrySingleInlineBlock);
    run("testZoneGeometryMultipleBlocksMixedMode", testZoneGeometryMultipleBlocksMixedMode);
    run("testZoneGeometryChainSegmentNotSixteenAligned",
        testZoneGeometryChainSegmentNotSixteenAligned);
    run("testZoneGeometryChainWithSixteenAlignedPadding",
        testZoneGeometryChainWithSixteenAlignedPadding);
    run("testZoneGeometryBothHeaderDisplacements", testZoneGeometryBothHeaderDisplacements);
    run("testZoneGeometryBadChainCandidateDoesNotDerailTheChain",
        testZoneGeometryBadChainCandidateDoesNotDerailTheChain);
    run("testZoneGeometryRejectsCoincidentalVersionWord",
        testZoneGeometryRejectsCoincidentalVersionWord);
    run("testZoneGeometryGBackedBlockWithoutGFileFindsNothing",
        testZoneGeometryGBackedBlockWithoutGFileFindsNothing);
    run("testZoneGeometryEmptyCznFindsNothing", testZoneGeometryEmptyCznFindsNothing);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic zone-format tests passed.\n";
    return 0;
}
