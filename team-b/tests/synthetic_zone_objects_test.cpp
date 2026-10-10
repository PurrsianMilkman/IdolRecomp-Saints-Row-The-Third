// Synthetic tests for the `.czn_pc` Objects (0x2234) placement/property
// stream reader (sr3zone::ZoneObjects, include/sr3zone/zone_objects.h) -
// spec-zone-data-format.md Sec11 (units U0+U1, 2026-10-07).
//
// Fixtures are built directly from Sec11's own field tables (the FBOW/
// version-5 payload header Sec11.3, the 28-byte record head Sec11.4, the
// property-tail entry shape Sec11.7), never by calling into
// src/zone_objects.cpp - a fixture derived from the parser only proves the
// parser agrees with itself, the same discipline synthetic_zone_test.cpp
// already documents for this project's other zone-format fixtures.
//
// The ClassHashBucket() reference values below were NOT hand-derived (32-
// bit wraparound by hand is exactly the kind of arithmetic this project's
// own lessons warn is error-prone) - they were computed by compiling the
// identical six-line mix from zone_objects.h's own doc comment as a tiny
// standalone C program and running it, independently of src/zone_objects.cpp.
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3save/save_crc.h"
#include "sr3zone/zone_objects.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"         \
                      << __LINE__ << "\n";                                  \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

#define CHECK_THROWS(expr)                                                  \
    do {                                                                    \
        bool threw = false;                                                 \
        try { expr; }                                                       \
        catch (const std::exception&) { threw = true; }                    \
        if (!threw) {                                                       \
            std::cerr << "CHECK FAILED (expected throw): " #expr            \
                      << " at " __FILE__ ":" << __LINE__ << "\n";           \
            ++g_failures;                                                   \
        }                                                                   \
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

// Little-endian float32 append, matching this project's standing
// little-endian-host assumption (used by the position/transform property
// tests below, Sec11.7's "Empirical confirmation" paragraph).
void appendF32(std::vector<uint8_t>& b, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    appendU32(b, bits);
}

size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

// ============================================================
// Part A: ClassHashBucket() - the Sec11.5 mixing function.
// ============================================================

void testClassHashBucketPinnedValues() {
    // Independently computed (see file header) - NOT derived from
    // src/zone_objects.cpp.
    CHECK(sr3zone::ClassHashBucket(0x00000000u) == 255u);
    CHECK(sr3zone::ClassHashBucket(0x00000001u) == 434u);
    CHECK(sr3zone::ClassHashBucket(0xDEADBEEFu) == 449u);
    CHECK(sr3zone::ClassHashBucket(0x12345678u) == 423u);
    CHECK(sr3zone::ClassHashBucket(0xC0FFEE01u) == 293u);
    CHECK(sr3zone::ClassHashBucket(0xFFFFFFFFu) == 6u);
    CHECK(sr3zone::ClassHashBucket(3u) == 333u);
    CHECK(sr3zone::ClassHashBucket(42u) == 475u);
    CHECK(sr3zone::ClassHashBucket(999999u) == 298u);

    // Sec11.5: "mixes the stored integer modulo 510 buckets" - every
    // result must be in [0, 510), over a wide sample, not just the
    // pinned values above.
    for (uint32_t v = 0; v < 100000; v += 37) {
        CHECK(sr3zone::ClassHashBucket(v) < sr3zone::kClassHashBucketCount);
    }
    CHECK(sr3zone::kClassHashBucketCount == 510u);

    // Deterministic: same input, same output, every time.
    CHECK(sr3zone::ClassHashBucket(0xC0FFEE01u) == sr3zone::ClassHashBucket(0xC0FFEE01u));
}

// ============================================================
// Part B: ObjectsSection / ObjectRecord - the FBOW/version-5 payload.
// ============================================================

// One record's authoring-time inputs, matching zone_objects.h's own field
// table (Sec11.4/Sec11.7).
struct RecordSpec {
    uint32_t handleLo = 11, handleHi = 0;
    uint32_t ownerLo = 0, ownerHi = 0;
    uint32_t classHash = 0;
    uint16_t fieldAt0x14 = 0;
    std::string name; // empty == unnamed (nameOffsetInTail stays 0)
    // Extra raw bytes appended to the tail AFTER the name property (if
    // any), verbatim - lets a test build a tail with more than one
    // property, or a malformed trailing entry, without the builder having
    // an opinion about what it means.
    std::vector<uint8_t> extraTailBytes;
    // When true, `extraTailBytes` is prepended instead (BEFORE the name
    // property) - used by the overrun test, where the one property that
    // must be found by its byte offset should not itself be the thing that
    // overruns.
    bool extraBeforeName = false;
};

// Appends one property-list entry (Sec11.7): u16 kind, u16 size, u32 id,
// then `data`, then pads to the next 4-byte boundary. Mirrors 0x00456660's
// own write order for the 8-byte header, plus the 4-byte entry-STRIDE this
// reader's own whole-population real-data run found (156,101 / 156,101 real
// records tile exactly under it, zero exceptions - see zone_objects.h's
// "tail-interior decode" comment and src/zone_objects.cpp's
// decodeTailEntries()). `size` in the written header is still the UNPADDED
// data length, matching what decodeTailEntries() reports back as each
// entry's own `size`/`data` - only the STRIDE to the next entry is padded.
void appendPropertyEntry(std::vector<uint8_t>& tail, uint16_t kind, uint32_t id,
                         const std::vector<uint8_t>& data) {
    appendU16(tail, kind);
    appendU16(tail, static_cast<uint16_t>(data.size()));
    appendU32(tail, id);
    tail.insert(tail.end(), data.begin(), data.end());
    while (tail.size() % 4 != 0) tail.push_back(0x00); // pad the ENTRY, not the whole tail
}

// Builds one 28-byte record head + variable tail from a RecordSpec, per
// Sec11.4/Sec11.7's own field tables. `outNameOffset` reports where the
// name property's DATA landed within the tail, exactly what Sec11.4 says
// the on-disk +0x18 field holds (0 if unnamed).
std::vector<uint8_t> buildRecord(const RecordSpec& spec) {
    std::vector<uint8_t> tail;

    auto appendNameProp = [&]() {
        if (spec.name.empty()) return 0u;
        std::vector<uint8_t> data(spec.name.begin(), spec.name.end());
        data.push_back(0x00); // NUL terminator, Sec11.7: "a NUL-terminated string"
        uint32_t dataStart = static_cast<uint32_t>(tail.size() + 8); // past this entry's own 8-byte header
        // id CORRECTED 2026-10-07 (Sec11.7): ctg_object_name's real on-disk id is
        // 0x355ef946 (sr3zone::kCtgObjectNamePropertyId), not the literal 0 an
        // earlier draft of this fixture assumed.
        appendPropertyEntry(tail, /*kind=*/7, sr3zone::kCtgObjectNamePropertyId, data);
        return dataStart;
    };

    uint32_t nameOffset = 0;
    if (spec.extraBeforeName) {
        tail.insert(tail.end(), spec.extraTailBytes.begin(), spec.extraTailBytes.end());
        nameOffset = appendNameProp();
    } else {
        nameOffset = appendNameProp();
        tail.insert(tail.end(), spec.extraTailBytes.begin(), spec.extraTailBytes.end());
    }

    std::vector<uint8_t> rec(0x1c, 0x00);
    putU32At(rec, 0x00, spec.handleLo);
    putU32At(rec, 0x04, spec.handleHi);
    putU32At(rec, 0x08, spec.ownerLo);
    putU32At(rec, 0x0c, spec.ownerHi);
    putU32At(rec, 0x10, spec.classHash);
    putU16At(rec, 0x14, spec.fieldAt0x14);
    putU16At(rec, 0x16, static_cast<uint16_t>(tail.size())); // tail length, CONFIRMED on-disk offset
    putU16At(rec, 0x18, static_cast<uint16_t>(nameOffset));
    rec.insert(rec.end(), tail.begin(), tail.end());
    return rec;
}

struct ObjectsSpec {
    std::vector<std::pair<uint32_t, uint32_t>> directory; // {handleLo, handleHi}
    std::vector<RecordSpec> records;
    // Overrides, for rejection tests only - when set (nonnegative), used
    // in place of the natural value derived from the vectors above.
    int overrideRecordCount = -1;
    int overrideDirectoryCount = -1;
    uint32_t magic = sr3zone::kObjectsPayloadMagic;
    uint32_t version = sr3zone::kObjectsPayloadVersion;
};

// Builds a complete FBOW/version-5 payload at buffer offset `at` inside
// `buf` (buf must already be `at` bytes long; this appends to it), per
// Sec11.3's own layout: magic, version, recordCount, directoryEntryCount,
// 16 more header bytes (+0x08-+0x1f, unidentified beyond the counts
// already read - Sec11.9 item 3), the directory array, then the record
// stream.
void appendObjectsPayload(std::vector<uint8_t>& buf, const ObjectsSpec& spec) {
    const size_t payloadOffset = buf.size();
    appendU32(buf, spec.magic);
    appendU32(buf, spec.version);
    appendU32(buf, spec.overrideRecordCount >= 0 ? static_cast<uint32_t>(spec.overrideRecordCount)
                                                  : static_cast<uint32_t>(spec.records.size()));
    appendU32(buf, spec.overrideDirectoryCount >= 0
                      ? static_cast<uint32_t>(spec.overrideDirectoryCount)
                      : static_cast<uint32_t>(spec.directory.size()));
    buf.resize(payloadOffset + sr3zone::kObjectsFixedHeaderSize, 0x00); // +0x08-+0x1f, OPEN per Sec11.9 item 3

    for (const auto& d : spec.directory) {
        appendU32(buf, d.first);
        appendU32(buf, d.second);
    }
    for (const auto& r : spec.records) {
        std::vector<uint8_t> rec = buildRecord(r);
        buf.insert(buf.end(), rec.begin(), rec.end());
    }
}

void testObjectsSectionWellFormed() {
    ObjectsSpec spec;
    spec.directory = {{100, 0}, {101, 0}};

    RecordSpec a;
    a.handleLo = 55; a.handleHi = 0;
    a.ownerLo = 0; a.ownerHi = 0;
    a.classHash = 0xC0FFEE01u;
    a.fieldAt0x14 = 0xABCD;
    a.name = "TestObj";
    spec.records.push_back(a);

    RecordSpec b;
    b.handleLo = 56; b.handleHi = 0;
    b.ownerLo = 55; b.ownerHi = 0; // owned by record A
    b.classHash = 0; // unnamed, no tail at all
    spec.records.push_back(b);

    std::vector<uint8_t> buf(16, 0xAB); // unrelated leading content
    const size_t payloadAt = buf.size();
    appendObjectsPayload(buf, spec);

    sr3zone::ObjectsSection sec =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), payloadAt);

    CHECK(sec.payloadOffset == payloadAt);
    CHECK(sec.version == 5);
    CHECK(sec.recordCount == 2);
    CHECK(sec.directoryEntryCount == 2);
    CHECK(sec.directoryOffset == payloadAt + 0x20);
    CHECK(sec.complete);
    CHECK(sec.directory.size() == 2);
    CHECK(sec.directory[0].handleLo == 100);
    CHECK(sec.directory[1].handleLo == 101);
    CHECK(sec.records.size() == 2);

    const sr3zone::ObjectRecord& ra = sec.records[0];
    CHECK(ra.handleLo == 55);
    CHECK(ra.ownerHandleLo == 0);
    CHECK(ra.classHash == 0xC0FFEE01u);
    CHECK(ra.fieldAt0x14 == 0xABCD);
    CHECK(ra.classHashBucket == 293u); // pinned, see Part A
    CHECK(ra.hasName);
    CHECK(ra.nameOffsetInTail != 0);
    CHECK(ra.nameText.has_value());
    CHECK(*ra.nameText == "TestObj");
    CHECK(ra.tailEntries.size() == 1);
    CHECK(ra.tailEntries[0].kind == 7);
    CHECK(ra.tailEntries[0].id == sr3zone::kCtgObjectNamePropertyId); // CORRECTED 2026-10-07, Sec11.7
    CHECK(ra.tailEntries[0].data.size() == 8); // "TestObj\0"
    CHECK(ra.tailExactFit);
    CHECK(!ra.tailOverrun);
    CHECK(ra.rawTail.size() == ra.tailLength);

    const sr3zone::ObjectRecord& rb = sec.records[1];
    CHECK(rb.handleLo == 56);
    CHECK(rb.ownerHandleLo == 55);
    CHECK(rb.classHash == 0);
    CHECK(rb.classHashBucket == 255u); // pinned, classHash==0
    CHECK(!rb.hasName);
    CHECK(!rb.nameText.has_value());
    CHECK(rb.tailLength == 0);
    CHECK(rb.rawTail.empty());
    CHECK(rb.tailEntries.empty());
    CHECK(rb.tailExactFit); // zero-length tail trivially "exact"
    CHECK(!rb.tailOverrun);

    // Record B must sit exactly where A's head+tail, 4-aligned, ends.
    CHECK(rb.recordOffset == alignUp(ra.recordOffset + 0x1c + ra.tailLength, 4));
}

void testObjectsSectionZeroRecordsIsNotAnError() {
    ObjectsSpec spec; // no directory, no records
    std::vector<uint8_t> buf;
    appendObjectsPayload(buf, spec);

    sr3zone::ObjectsSection sec =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), 0);
    CHECK(sec.recordCount == 0);
    CHECK(sec.directoryEntryCount == 0);
    CHECK(sec.records.empty());
    CHECK(sec.directory.empty());
    CHECK(sec.complete);
}

// Direct test of the 4-byte entry-STRIDE finding (see appendPropertyEntry's
// own comment and zone_objects.h's "tail-interior decode" comment): a name
// whose data length is NOT itself a multiple of 4 forces real padding
// bytes between the first entry and the second, and the decode must skip
// exactly that padding - not read the second entry's header out of it, and
// not report an overrun or a leftover.
void testObjectsSectionTailEntryStrideIsFourByteAligned() {
    RecordSpec r;
    r.classHash = 5;
    r.name = "Ed"; // 2 chars + NUL = 3 bytes of data -> entry is 8+3=11, padded to 12
    // One more entry after the (padded) name entry, built the same way so
    // its own correctness is end-to-end verified too, not just "the walk
    // didn't crash".
    appendPropertyEntry(r.extraTailBytes, /*kind=*/9, /*id=*/77, {0xAA, 0xBB, 0xCC}); // 3 bytes -> padded to 12

    ObjectsSpec spec;
    spec.records.push_back(r);
    std::vector<uint8_t> buf;
    appendObjectsPayload(buf, spec);

    sr3zone::ObjectsSection sec =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), 0);
    CHECK(sec.records.size() == 1);
    const sr3zone::ObjectRecord& rec = sec.records[0];

    CHECK(rec.tailLength == 24); // 12 (name entry, padded) + 12 (second entry, padded)
    CHECK(rec.tailEntries.size() == 2);
    if (rec.tailEntries.size() == 2) {
        CHECK(rec.tailEntries[0].kind == 7);
        CHECK(rec.tailEntries[0].id == sr3zone::kCtgObjectNamePropertyId); // CORRECTED 2026-10-07, Sec11.7
        CHECK(rec.tailEntries[0].data.size() == 3); // "Ed\0" - UNPADDED size, exactly as written
        CHECK(rec.tailEntries[1].kind == 9);
        CHECK(rec.tailEntries[1].id == 77);
        CHECK(rec.tailEntries[1].data.size() == 3);
        CHECK(rec.tailEntries[1].data[0] == 0xAA);
        CHECK(rec.tailEntries[1].data[1] == 0xBB);
        CHECK(rec.tailEntries[1].data[2] == 0xCC);
    }
    CHECK(rec.tailExactFit);
    CHECK(!rec.tailOverrun);
    CHECK(rec.tailBytesDecoded == 24);

    CHECK(rec.nameText.value_or("") == "Ed"); // unaffected by the padding that follows it
}

void testObjectDirectoryArrayDecoded() {
    ObjectsSpec spec;
    spec.directory = {{0xAAAA0001u, 0u}, {0xAAAA0002u, 1u}, {0xAAAA0003u, 2u}};
    std::vector<uint8_t> buf;
    appendObjectsPayload(buf, spec);

    sr3zone::ObjectsSection sec =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), 0);
    CHECK(sec.directory.size() == 3);
    CHECK(sec.directory[0].handleLo == 0xAAAA0001u && sec.directory[0].handleHi == 0u);
    CHECK(sec.directory[1].handleLo == 0xAAAA0002u && sec.directory[1].handleHi == 1u);
    CHECK(sec.directory[2].handleLo == 0xAAAA0003u && sec.directory[2].handleHi == 2u);
}

// An entry whose declared `size` would read past the record's own
// tailLength must stop the best-effort decode WITHOUT throwing and without
// disturbing the record's own structural fields (rawTail, nameText -
// Sec11.4's name-offset mechanism is independent of the generic entry
// walk, see zone_objects.h).
void testObjectsSectionTailOverrun() {
    RecordSpec r;
    r.classHash = 1;
    r.name = "Named"; // appended first - decoded via +0x18, not the entry walk
    // A malformed trailing entry: declares size 200 but the tail will only
    // actually hold a few more bytes after it.
    appendU16(r.extraTailBytes, /*kind=*/3);
    appendU16(r.extraTailBytes, /*size=*/200);
    appendU32(r.extraTailBytes, /*id=*/42);
    r.extraTailBytes.push_back(0x11); // a few real bytes, nowhere near 200
    r.extraTailBytes.push_back(0x22);

    ObjectsSpec spec;
    spec.records.push_back(r);
    std::vector<uint8_t> buf;
    appendObjectsPayload(buf, spec);

    sr3zone::ObjectsSection sec =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), 0);
    CHECK(sec.complete);
    CHECK(sec.records.size() == 1);
    const sr3zone::ObjectRecord& rec = sec.records[0];

    CHECK(rec.hasName);
    CHECK(rec.nameText.has_value());
    CHECK(*rec.nameText == "Named"); // unaffected by the overrun below

    CHECK(rec.tailOverrun);
    CHECK(!rec.tailExactFit);
    CHECK(rec.tailEntries.size() == 1); // the name property only; the bad entry was never accepted
    CHECK(rec.rawTail.size() == rec.tailLength); // the raw container is still exactly right
}

void testObjectsSectionRejections() {
    // Bad magic.
    {
        ObjectsSpec spec;
        spec.magic = 0xDEADBEEFu;
        std::vector<uint8_t> buf;
        appendObjectsPayload(buf, spec);
        CHECK_THROWS(sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), 0));
    }
    // Bad version (4 and 6, both must reject - Sec11.3: "must equal
    // exactly 5").
    {
        ObjectsSpec spec4; spec4.version = 4;
        std::vector<uint8_t> buf4;
        appendObjectsPayload(buf4, spec4);
        CHECK_THROWS(sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf4.data(), buf4.size()), 0));

        ObjectsSpec spec6; spec6.version = 6;
        std::vector<uint8_t> buf6;
        appendObjectsPayload(buf6, spec6);
        CHECK_THROWS(sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf6.data(), buf6.size()), 0));
    }
    // Directory array overruns the buffer.
    {
        ObjectsSpec spec;
        spec.overrideDirectoryCount = 5; // claims 5 entries, provides none
        std::vector<uint8_t> buf;
        appendObjectsPayload(buf, spec);
        CHECK_THROWS(sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), 0));
    }
    // Truncated: not even 8 bytes for the magic/version pair.
    {
        std::vector<uint8_t> tiny = {0x46, 0x42, 0x4F};
        CHECK_THROWS(sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(tiny.data(), tiny.size()), 0));
    }
}

// A declared recordCount the buffer cannot actually satisfy does NOT
// throw from parseAt() - it is reported structurally via complete==false
// and a short records() vector, exactly as ZoneHeader reports a
// zero-record empty file without treating it as an error. This is
// deliberately different from the directory-overrun case above (which
// DOES throw) - the directory length is known and checked up front, while
// the record stream can only be discovered to be too short one record at
// a time, and a partially-read stream is still useful diagnostic data for
// the per-zone object counts this reader's own tool is for.
void testObjectsSectionTruncatedRecordStreamIsReportedNotThrown() {
    ObjectsSpec spec;
    spec.overrideRecordCount = 3; // claims 3 records, provides only 1
    RecordSpec r;
    r.classHash = 7;
    spec.records.push_back(r);
    std::vector<uint8_t> buf;
    appendObjectsPayload(buf, spec);

    sr3zone::ObjectsSection sec =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), 0);
    CHECK(sec.recordCount == 3);
    CHECK(sec.records.size() == 1); // only what the buffer actually held
    CHECK(!sec.complete);
}

// ============================================================
// Part C: ZoneObjects::locate() - signature scan over a whole .czn_pc
// buffer (see zone_objects.h for why this is a locate-by-signature
// strategy, not a replay of the top-level {tag,length} walker).
// ============================================================

void testLocateFindsPayloadAtArbitraryOffset() {
    ObjectsSpec spec;
    RecordSpec r;
    r.classHash = 123;
    r.name = "Zed";
    spec.records.push_back(r);

    // cznPrefixPad must be 4-aligned, matching every other structure in
    // this engine (spec Sec1: ".czn_pc is always a multiple of 4") and
    // ZoneGeometry::locate()'s own scan granularity.
    std::vector<uint8_t> buf(28, 0xCD); // unrelated top-level content before it
    const size_t payloadAt = buf.size();
    appendObjectsPayload(buf, spec);
    buf.insert(buf.end(), 12, 0xEF); // unrelated content after it

    auto found = sr3zone::ZoneObjects::locate(sr3zone::ByteView(buf.data(), buf.size()));
    CHECK(found.size() == 1);
    if (found.size() == 1) {
        CHECK(found[0].payloadOffset == payloadAt);
        CHECK(found[0].records.size() == 1);
        CHECK(found[0].records[0].classHash == 123);
        CHECK(found[0].records[0].nameText.value_or("") == "Zed");
    }
}

// A coincidental 'FBOW' word not followed by version 5 must not be
// reported - mirrors
// testZoneGeometryRejectsCoincidentalVersionWord's discipline for the
// sibling Mesh-block reader.
void testLocateRejectsMagicWithoutVersionFive() {
    std::vector<uint8_t> buf(64, 0x00);
    putU32At(buf, 20, sr3zone::kObjectsPayloadMagic);
    putU32At(buf, 24, 3); // wrong version

    auto found = sr3zone::ZoneObjects::locate(sr3zone::ByteView(buf.data(), buf.size()));
    CHECK(found.empty());
}

// A genuine payload followed immediately by a truncated directory/record
// stream must not be reported by locate() even though the magic/version
// signature matches - locate() must actually validate, not just signature-
// match.
void testLocateRejectsTruncatedCandidate() {
    std::vector<uint8_t> buf;
    appendU32(buf, sr3zone::kObjectsPayloadMagic);
    appendU32(buf, sr3zone::kObjectsPayloadVersion);
    appendU32(buf, 0); // recordCount
    appendU32(buf, 9999); // directoryEntryCount - far more than the buffer can hold
    // deliberately no more bytes

    auto found = sr3zone::ZoneObjects::locate(sr3zone::ByteView(buf.data(), buf.size()));
    CHECK(found.empty());
}

void testLocateEmptyBufferFindsNothing() {
    std::vector<uint8_t> buf;
    auto found = sr3zone::ZoneObjects::locate(sr3zone::ByteView(buf.data(), buf.size()));
    CHECK(found.empty());
}

// ============================================================
// Part D: corrections 3-5 (2026-10-07, Team A's own whole-population
// re-replay of Sec11 against the real 383,346-object population) -
// ctg_object_name's real on-disk id, class-name resolution for the 21
// named classes this reader can cite, and the script_npc_*/
// script_vehicle_* spawn-override property family.
// ============================================================

void testCtgObjectNamePropertyIdMatchesNameHash() {
    // Sec11.7's own literal 0x355ef946 must agree with this project's
    // already-existing, independently-confirmed CRC-32 helper
    // (sr3save::nameHash) fed the exact literal string Sec11.7 names - the
    // cross-check that lets kCtgObjectNamePropertyId be trusted as a
    // literal constant rather than a hand-transcription risk.
    CHECK(sr3save::nameHash("ctg_object_name") == sr3zone::kCtgObjectNamePropertyId);
    CHECK(sr3zone::kCtgObjectNamePropertyId == 0x355ef946u);
    CHECK(sr3zone::kCtgObjectNamePropertyId == 895416646u); // Sec11.7 states both forms
}

void testFindTailEntryByIdLocatesNameProperty() {
    RecordSpec r;
    r.classHash = 1;
    r.name = "Zed";
    ObjectsSpec spec;
    spec.records.push_back(r);
    std::vector<uint8_t> buf;
    appendObjectsPayload(buf, spec);

    sr3zone::ObjectsSection sec =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), 0);
    CHECK(sec.records.size() == 1);
    const sr3zone::ObjectRecord& rec = sec.records[0];

    const sr3zone::ObjectPropertyEntry* found =
        sr3zone::FindTailEntryById(rec, sr3zone::kCtgObjectNamePropertyId);
    CHECK(found != nullptr);
    if (found) {
        CHECK(found->id == sr3zone::kCtgObjectNamePropertyId);
        CHECK(std::string(found->data.begin(), found->data.end()) == std::string("Zed\0", 4));
    }

    // A property id never authored on this record must not be found.
    CHECK(sr3zone::FindTailEntryById(rec, 0xDEADBEEFu) == nullptr);
}

void testClassNameForHashResolvesNamedClasses() {
    // A handful of the 21 named classes Sec11.7 gives by literal string -
    // each computed via sr3save::nameHash(), never hand-transcribed.
    CHECK(sr3zone::ClassNameForHash(sr3save::nameHash("general_mover")).value_or("") ==
          "general_mover");
    CHECK(sr3zone::ClassNameForHash(sr3save::nameHash("script_npc")).value_or("") == "script_npc");
    CHECK(sr3zone::ClassNameForHash(sr3save::nameHash("Navpoint")).value_or("") == "Navpoint");
    CHECK(sr3zone::ClassNameForHash(sr3save::nameHash("cover_node")).value_or("") == "cover_node");
    CHECK(sr3zone::ClassNameForHash(sr3save::nameHash("cutscene_slate_obj")).value_or("") ==
          "cutscene_slate_obj");

    // Case-insensitivity: the engine's own hash lowercases before hashing
    // (Sec11.5/Sec11.7), so hashing a different-case spelling of the same
    // name must resolve identically.
    CHECK(sr3zone::ClassNameForHash(sr3save::nameHash("NAVPOINT")).value_or("") == "Navpoint");

    // A hash that is not one of the 21 named classes must return
    // std::nullopt - never a guessed name (also true for one of the 2
    // genuinely-unresolved hashes or one of the ~19 resolved-but-unlisted
    // classes; this reader cannot and must not distinguish those cases).
    CHECK(!sr3zone::ClassNameForHash(0xDEADBEEFu).has_value());

    // KnownClassHashes() itself must list exactly the 21 names Sec11.7
    // spells out verbatim.
    CHECK(sr3zone::KnownClassHashes().size() == 21);
}

// Builds a single-record ObjectsSection with one extra property (beyond
// the optional name) carrying `propId`/`kind`/`data` verbatim - used by the
// presence tests below. Mirrors testObjectsSectionTailOverrun's own
// extraTailBytes technique.
sr3zone::ObjectsSection buildSingleRecordWithProperty(uint32_t propId, uint16_t kind,
                                                       const std::vector<uint8_t>& data,
                                                       std::vector<uint8_t>& bufOut) {
    RecordSpec r;
    r.classHash = 7;
    appendPropertyEntry(r.extraTailBytes, kind, propId, data);
    ObjectsSpec spec;
    spec.records.push_back(r);
    appendObjectsPayload(bufOut, spec);
    return sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(bufOut.data(), bufOut.size()), 0);
}

void testScriptNpcHpPresentAndAbsent() {
    std::vector<uint8_t> dataU32 = {0x64, 0x00, 0x00, 0x00}; // 100, little-endian u32 - kind
                                                              // is arbitrary here (kind=1):
                                                              // Sec11.7 does not state this
                                                              // property's own on-disk kind
    std::vector<uint8_t> buf;
    sr3zone::ObjectsSection sec =
        buildSingleRecordWithProperty(sr3zone::ScriptNpcHpPropertyId(), /*kind=*/1, dataU32, buf);
    CHECK(sec.records.size() == 1);
    auto hp = sr3zone::ScriptNpcHp(sec.records[0]);
    CHECK(hp.has_value());
    CHECK(hp.value_or(0) == 100u);

    // Absence case: a record with no script_npc_hp property at all - the
    // common case (Sec11.7: 1,028 of 383,346 real objects carry one).
    RecordSpec plain;
    plain.classHash = 7;
    ObjectsSpec spec2;
    spec2.records.push_back(plain);
    std::vector<uint8_t> buf2;
    appendObjectsPayload(buf2, spec2);
    sr3zone::ObjectsSection sec2 =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf2.data(), buf2.size()), 0);
    CHECK(!sr3zone::ScriptNpcHp(sec2.records[0]).has_value());
}

void testScriptNpcFlagsContainsIgnoreAi() {
    std::string flagsStr = "never_pickup_weapons ignore_ai should_not_be_grabbed dont_drop_cash "
                            "use_default_loadout"; // Sec11.7's own real base-game example
                                                   // (sr3_city_0.vpp_pc/_a_tm_dt_02.str2_pc/
                                                   // sr3_city~f_a_tm_dt_02.czn_pc, objects #21-25)
    std::vector<uint8_t> data(flagsStr.begin(), flagsStr.end());
    data.push_back(0x00);
    std::vector<uint8_t> buf;
    sr3zone::ObjectsSection sec =
        buildSingleRecordWithProperty(sr3zone::ScriptNpcFlagsPropertyId(), /*kind=*/7, data, buf);
    CHECK(sec.records.size() == 1);
    auto flags = sr3zone::ScriptNpcFlags(sec.records[0]);
    CHECK(flags.has_value());
    CHECK(flags.value_or("") == flagsStr);
    CHECK(flags.value_or("").find("ignore_ai") != std::string::npos);

    RecordSpec plain;
    plain.classHash = 7;
    ObjectsSpec spec2;
    spec2.records.push_back(plain);
    std::vector<uint8_t> buf2;
    appendObjectsPayload(buf2, spec2);
    sr3zone::ObjectsSection sec2 =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf2.data(), buf2.size()), 0);
    CHECK(!sr3zone::ScriptNpcFlags(sec2.records[0]).has_value());
}

void testScriptVehicleFamilyPresentAndAbsent() {
    std::vector<uint8_t> dataU32 = {0x10, 0x27, 0x00, 0x00}; // 10,000, little-endian u32
    std::vector<uint8_t> buf;
    sr3zone::ObjectsSection sec =
        buildSingleRecordWithProperty(sr3zone::ScriptVehicleHpPropertyId(), /*kind=*/1, dataU32, buf);
    CHECK(sec.records.size() == 1);
    CHECK(sr3zone::ScriptVehicleHp(sec.records[0]).value_or(0) == 10000u);
    CHECK(!sr3zone::ScriptVehicleFlags(sec.records[0]).has_value());      // absent on this record
    CHECK(!sr3zone::ScriptVehicleMpTeam(sec.records[0]).has_value());     // absent on this record
    CHECK(!sr3zone::ScriptVehicleTypeName(sec.records[0]).has_value());   // absent on this record
    CHECK(!sr3zone::ScriptVehicleVariantName(sec.records[0]).has_value()); // absent on this record

    std::string teamStr = "Luchadores"; // Sec11.7's own real gang-name example (script_npc_team)
    std::vector<uint8_t> dataTeam(teamStr.begin(), teamStr.end());
    dataTeam.push_back(0x00);
    std::vector<uint8_t> buf2;
    sr3zone::ObjectsSection sec2 =
        buildSingleRecordWithProperty(sr3zone::ScriptNpcTeamPropertyId(), /*kind=*/7, dataTeam, buf2);
    CHECK(sr3zone::ScriptNpcTeam(sec2.records[0]).value_or("") == teamStr);
}

// NEW 2026-10-10 (vehicle-class xtbl wiring task): `script_vehicle_type_name`/
// `script_vehicle_variant_name` were spec-cited (Sec11.7's own "_type_name"/
// "_variant_name" naming of the rest of the script_vehicle_* family) but had
// no property-id/accessor pair implemented at all until this task - this is
// the real zone-spawn-to-vehicle-type join key EngineState::
// loadZoneObjectsSection() now uses (src/lua_engine_state.cpp) to populate
// VehicleState::flyingType from sr3vehicleinfo::ParseAllVehicles' own
// already-correct class data instead of leaving it OPEN.
void testScriptVehicleTypeNameAndVariantName() {
    std::string typeNameStr = "heli_variant_pierce"; // a plausible <name>_veh.xtbl key, not a cited real value
    std::vector<uint8_t> data(typeNameStr.begin(), typeNameStr.end());
    data.push_back(0x00);
    std::vector<uint8_t> buf;
    sr3zone::ObjectsSection sec =
        buildSingleRecordWithProperty(sr3zone::ScriptVehicleTypeNamePropertyId(), /*kind=*/7, data, buf);
    CHECK(sec.records.size() == 1);
    CHECK(sr3zone::ScriptVehicleTypeName(sec.records[0]).value_or("") == typeNameStr);
    CHECK(!sr3zone::ScriptVehicleVariantName(sec.records[0]).has_value()); // absent on this record

    std::string variantStr = "red";
    std::vector<uint8_t> dataVariant(variantStr.begin(), variantStr.end());
    dataVariant.push_back(0x00);
    std::vector<uint8_t> buf2;
    sr3zone::ObjectsSection sec2 =
        buildSingleRecordWithProperty(sr3zone::ScriptVehicleVariantNamePropertyId(), /*kind=*/7, dataVariant, buf2);
    CHECK(sr3zone::ScriptVehicleVariantName(sec2.records[0]).value_or("") == variantStr);
    CHECK(!sr3zone::ScriptVehicleTypeName(sec2.records[0]).has_value()); // absent on this record

    // The two ids are distinct (computed from distinct literal strings) -
    // never collapse to the same property by accident.
    CHECK(sr3zone::ScriptVehicleTypeNamePropertyId() != sr3zone::ScriptVehicleVariantNamePropertyId());
}

// NEW 2026-10-10 (Trigger_Volume on-disk shape/extent/radius wiring task):
// spec-lua-api-behaviour.md Sec1.14's own "On-disk gaps - RESOLVED
// 2026-10-10" paragraph / spec-zone-data-format.md Sec11.7's own "A third
// class resolved" paragraph. Values below for the box/cylinder cases mirror
// the REAL `on_boat_trigger` record tools/zone_object_resolver_probe.cpp
// read directly off the real shipped `sr3_city_0.vpp_pc` (box) and a
// plausible-but-not-cited cylinder example (Sec1.14 gives the field
// layout/widths, not a real cylinder trigger's own numbers).
void testTriggerShapeBoxBothParamsPresent() {
    RecordSpec r;
    r.classHash = 7;
    // trigger_shape_type = "bounding box" (the REAL on-disk string this
    // task's own population scan found, 905/1277 real Trigger_Volume
    // records - not the bare word "box").
    std::string shapeType = "bounding box";
    std::vector<uint8_t> shapeTypeData(shapeType.begin(), shapeType.end());
    shapeTypeData.push_back(0x00);
    appendPropertyEntry(r.extraTailBytes, /*kind=*/7, sr3zone::TriggerShapeTypePropertyId(), shapeTypeData);
    // REAL on_boat_trigger min/max corner (12 bytes each, 3 float32s).
    std::vector<uint8_t> minData(12), maxData(12);
    float minXyz[3] = {-87.8214f, 0.0f, -22.2829f};
    float maxXyz[3] = {87.8214f, 30.7825f, 22.2829f};
    std::memcpy(minData.data(), minXyz, 12);
    std::memcpy(maxData.data(), maxXyz, 12);
    appendPropertyEntry(r.extraTailBytes, /*kind=*/2, sr3zone::TriggerShapeParam1PropertyId(), minData);
    appendPropertyEntry(r.extraTailBytes, /*kind=*/2, sr3zone::TriggerShapeParam2PropertyId(), maxData);
    ObjectsSpec spec;
    spec.records.push_back(r);
    std::vector<uint8_t> buf;
    appendObjectsPayload(buf, spec);
    sr3zone::ObjectsSection sec = sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf.data(), buf.size()), 0);
    CHECK(sec.records.size() == 1);

    auto shapeTypeText = sr3zone::TriggerShapeType(sec.records[0]);
    CHECK(shapeTypeText.value_or("") == "bounding box");
    CHECK(sr3zone::TriggerShapeCodeForTypeString(shapeTypeText.value_or("")).value_or(-1) == 0);

    const sr3zone::ObjectPropertyEntry* p1 = sr3zone::TriggerShapeParam1(sec.records[0]);
    const sr3zone::ObjectPropertyEntry* p2 = sr3zone::TriggerShapeParam2(sec.records[0]);
    CHECK(p1 != nullptr && p2 != nullptr);
    auto vmin = p1 ? sr3zone::DecodePropertyAsVec3(*p1) : std::nullopt;
    auto vmax = p2 ? sr3zone::DecodePropertyAsVec3(*p2) : std::nullopt;
    CHECK(vmin.has_value() && vmax.has_value());
    CHECK(vmin->x == -87.8214f && vmin->y == 0.0f && vmin->z == -22.2829f);
    CHECK(vmax->x == 87.8214f && vmax->y == 30.7825f && vmax->z == 22.2829f);

    // Absence case: a plain record with no trigger_shape_* properties at all.
    RecordSpec plain;
    plain.classHash = 7;
    ObjectsSpec spec2;
    spec2.records.push_back(plain);
    std::vector<uint8_t> buf2;
    appendObjectsPayload(buf2, spec2);
    sr3zone::ObjectsSection sec2 = sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf2.data(), buf2.size()), 0);
    CHECK(!sr3zone::TriggerShapeType(sec2.records[0]).has_value());
    CHECK(sr3zone::TriggerShapeParam1(sec2.records[0]) == nullptr);
    CHECK(sr3zone::TriggerShapeParam2(sec2.records[0]) == nullptr);
}

void testTriggerShapeSphereAndCylinderScalars() {
    std::vector<uint8_t> radiusData(4);
    float radius = 5.5f;
    std::memcpy(radiusData.data(), &radius, 4);
    std::vector<uint8_t> buf;
    sr3zone::ObjectsSection sec =
        buildSingleRecordWithProperty(sr3zone::TriggerShapeParam1PropertyId(), /*kind=*/1, radiusData, buf);
    CHECK(sec.records.size() == 1);
    const sr3zone::ObjectPropertyEntry* p1 = sr3zone::TriggerShapeParam1(sec.records[0]);
    CHECK(p1 != nullptr);
    auto decoded = p1 ? sr3zone::DecodePropertyAsFloat(*p1) : std::nullopt;
    CHECK(decoded.has_value() && decoded.value() == 5.5f);
    CHECK(sr3zone::TriggerShapeParam2(sec.records[0]) == nullptr); // absent, sphere never reads param2

    // Cylinder length (param2), same scalar decode.
    std::vector<uint8_t> lengthData(4);
    float length = 12.0f;
    std::memcpy(lengthData.data(), &length, 4);
    std::vector<uint8_t> buf2;
    sr3zone::ObjectsSection sec2 =
        buildSingleRecordWithProperty(sr3zone::TriggerShapeParam2PropertyId(), /*kind=*/1, lengthData, buf2);
    const sr3zone::ObjectPropertyEntry* p2 = sr3zone::TriggerShapeParam2(sec2.records[0]);
    CHECK(p2 != nullptr);
    auto decodedLen = p2 ? sr3zone::DecodePropertyAsFloat(*p2) : std::nullopt;
    CHECK(decodedLen.has_value() && decodedLen.value() == 12.0f);
}

void testTriggerShapeCodeForTypeStringRealMapping() {
    // Real on-disk strings this task's own population scan found (1,277 real
    // Trigger_Volume records, 0 missing, 0 unmapped): "sphere" (118),
    // "cylinder" (254), "bounding box" (905) - see zone_objects.cpp's own
    // citation for the full scan result.
    CHECK(sr3zone::TriggerShapeCodeForTypeString("sphere").value_or(-1) == 1);
    CHECK(sr3zone::TriggerShapeCodeForTypeString("cylinder").value_or(-1) == 2);
    CHECK(sr3zone::TriggerShapeCodeForTypeString("bounding box").value_or(-1) == 0);
    // Case-insensitive (defensive, Sec1.14 does not state the real lookup's
    // own case sensitivity).
    CHECK(sr3zone::TriggerShapeCodeForTypeString("SPHERE").value_or(-1) == 1);
    CHECK(sr3zone::TriggerShapeCodeForTypeString("Bounding Box").value_or(-1) == 0);
    // An unobserved string must never guess.
    CHECK(!sr3zone::TriggerShapeCodeForTypeString("tetrahedron").has_value());
}

void testDecodePropertyAsFloatAndVec3RejectOtherWidths() {
    sr3zone::ObjectPropertyEntry e;
    e.data = {0x01, 0x02, 0x03}; // 3 bytes - not a plain float32 width
    CHECK(!sr3zone::DecodePropertyAsFloat(e).has_value());
    sr3zone::ObjectPropertyEntry e2;
    e2.data = {0x01, 0x02, 0x03, 0x04, 0x05}; // 5 bytes - not 12
    CHECK(!sr3zone::DecodePropertyAsVec3(e2).has_value());
}

void testTriggerPropertyIdsAreDistinct() {
    // Every id in this family must be distinct - never collapse two
    // different property names to the same hash by accident.
    const std::vector<uint32_t> ids = {
        sr3zone::TriggerShapeTypePropertyId(),   sr3zone::TriggerShapeParam1PropertyId(),
        sr3zone::TriggerShapeParam2PropertyId(), sr3zone::TriggerFlagsPropertyId(),
        sr3zone::TriggerActionPropertyId(),      sr3zone::TriggerActionParamPropertyId(),
        sr3zone::TriggerActionParam2PropertyId(),sr3zone::TriggerUseMessagePropertyId(),
        sr3zone::TriggerMaxFiresPropertyId(),    sr3zone::TriggerDelayPropertyId(),
        sr3zone::TriggerSyncPropertyId()};
    for (size_t i = 0; i < ids.size(); ++i) {
        for (size_t j = i + 1; j < ids.size(); ++j) {
            CHECK(ids[i] != ids[j]);
        }
    }
}

void testDecodePropertyAsUnsignedIntRejectsOddWidths() {
    sr3zone::ObjectPropertyEntry e;
    e.data = {0x01, 0x02, 0x03}; // 3 bytes - not a plain machine-integer width
    CHECK(!sr3zone::DecodePropertyAsUnsignedInt(e).has_value());

    sr3zone::ObjectPropertyEntry e2;
    e2.data = {0xFF}; // 1 byte, valid width
    CHECK(sr3zone::DecodePropertyAsUnsignedInt(e2).value_or(0) == 0xFFu);
}

// ============================================================
// Part D: the position/transform property (kPositionPropertyId) and
// Navpoint's navpoint_flags - added 2026-10-08 for the Navpoint/cover_node
// movement-target-selection unit (spec-ai-runtime.md Sec15).
// ============================================================

void testPositionPropertyIdPinnedValue() {
    // Sec11.7's own "Empirical confirmation" paragraph states this exact
    // numeric id directly (a pinned literal, like kCtgObjectNamePropertyId -
    // not computed via sr3save::nameHash(), since Sec11.7 gives no name
    // string for it). RESOLVED 2026-10-08 (Team A spec-sync, commit
    // 47c8516): an earlier draft's hex parenthetical (0xC8B3B245) did not
    // agree with its own stated decimal (3367956165 decimal is actually
    // 0xC8BEEEC5 hex) - flagged by this test rather than silently picked.
    // Team A's own population replay (19,800 named objects) confirmed the
    // DECIMAL value is the real, load-bearing one; the hex parenthetical was
    // the mis-conversion. kPositionPropertyId now uses the confirmed value.
    CHECK(sr3zone::kPositionPropertyId == 0xC8BEEEC5u);
    CHECK(sr3zone::kPositionPropertyId == 3367956165u); // Sec11.7's confirmed decimal form
    CHECK(sr3zone::kPositionPropertyId != 0xC8B3B245u); // the earlier, now-refuted mis-conversion
}

void testDecodePropertyAsPositionKind2NoOrientation() {
    sr3zone::ObjectPropertyEntry e;
    e.kind = 2;
    appendF32(e.data, 100.5f);
    appendF32(e.data, -20.25f);
    appendF32(e.data, 3.0f);
    CHECK(e.data.size() == 12);

    auto pos = sr3zone::DecodePropertyAsPosition(e);
    CHECK(pos.has_value());
    if (pos) {
        CHECK(pos->x == 100.5f);
        CHECK(pos->y == -20.25f);
        CHECK(pos->z == 3.0f);
    }
}

void testDecodePropertyAsPositionKind3WithQuaternionIgnored() {
    sr3zone::ObjectPropertyEntry e;
    e.kind = 3;
    appendF32(e.data, 1.0f);
    appendF32(e.data, 2.0f);
    appendF32(e.data, 3.0f);
    // 4-float quaternion tail - present on-disk (Sec11.7), deliberately not
    // decoded (no confirmed component order/convention).
    appendF32(e.data, 0.0f);
    appendF32(e.data, 0.0f);
    appendF32(e.data, 0.0f);
    appendF32(e.data, 1.0f);
    CHECK(e.data.size() == 28);

    auto pos = sr3zone::DecodePropertyAsPosition(e);
    CHECK(pos.has_value());
    if (pos) {
        CHECK(pos->x == 1.0f);
        CHECK(pos->y == 2.0f);
        CHECK(pos->z == 3.0f);
    }
}

void testDecodePropertyAsPositionRejectsOtherSizes() {
    sr3zone::ObjectPropertyEntry e;
    e.data = {0x01, 0x02, 0x03, 0x04}; // 4 bytes - neither 12 nor 28
    CHECK(!sr3zone::DecodePropertyAsPosition(e).has_value());
}

void testObjectPositionPresentAndAbsent() {
    std::vector<uint8_t> data;
    appendF32(data, 10.0f);
    appendF32(data, 20.0f);
    appendF32(data, 30.0f);
    std::vector<uint8_t> buf;
    sr3zone::ObjectsSection sec =
        buildSingleRecordWithProperty(sr3zone::kPositionPropertyId, /*kind=*/2, data, buf);
    CHECK(sec.records.size() == 1);
    auto pos = sr3zone::ObjectPosition(sec.records[0]);
    CHECK(pos.has_value());
    if (pos) {
        CHECK(pos->x == 10.0f);
        CHECK(pos->y == 20.0f);
        CHECK(pos->z == 30.0f);
    }

    // Absence case: a record with no position/transform property at all.
    RecordSpec plain;
    plain.classHash = 7;
    ObjectsSpec spec2;
    spec2.records.push_back(plain);
    std::vector<uint8_t> buf2;
    appendObjectsPayload(buf2, spec2);
    sr3zone::ObjectsSection sec2 =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf2.data(), buf2.size()), 0);
    CHECK(!sr3zone::ObjectPosition(sec2.records[0]).has_value());
}

void testNavpointFlagsPropertyIdMatchesNameHash() {
    CHECK(sr3zone::NavpointFlagsPropertyId() == sr3save::nameHash("navpoint_flags"));
}

void testNavpointFlagsPresentAndAbsent() {
    std::vector<uint8_t> data = {0x09}; // bits 0x1 | 0x8, one byte wide
    std::vector<uint8_t> buf;
    sr3zone::ObjectsSection sec = buildSingleRecordWithProperty(
        sr3zone::NavpointFlagsPropertyId(), /*kind=*/1, data, buf);
    CHECK(sec.records.size() == 1);
    auto flags = sr3zone::NavpointFlags(sec.records[0]);
    CHECK(flags.has_value());
    CHECK(flags.value_or(0) == 0x09u);

    // Absence case: Sec15.1's documented common case - 9.1% of real
    // Navpoint instances carry navpoint_flags at all.
    RecordSpec plain;
    plain.classHash = 7;
    ObjectsSpec spec2;
    spec2.records.push_back(plain);
    std::vector<uint8_t> buf2;
    appendObjectsPayload(buf2, spec2);
    sr3zone::ObjectsSection sec2 =
        sr3zone::ZoneObjects::parseAt(sr3zone::ByteView(buf2.data(), buf2.size()), 0);
    CHECK(!sr3zone::NavpointFlags(sec2.records[0]).has_value());
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
    run("testClassHashBucketPinnedValues", testClassHashBucketPinnedValues);

    run("testObjectsSectionWellFormed", testObjectsSectionWellFormed);
    run("testObjectsSectionZeroRecordsIsNotAnError", testObjectsSectionZeroRecordsIsNotAnError);
    run("testObjectsSectionTailEntryStrideIsFourByteAligned",
        testObjectsSectionTailEntryStrideIsFourByteAligned);
    run("testObjectDirectoryArrayDecoded", testObjectDirectoryArrayDecoded);
    run("testObjectsSectionTailOverrun", testObjectsSectionTailOverrun);
    run("testObjectsSectionRejections", testObjectsSectionRejections);
    run("testObjectsSectionTruncatedRecordStreamIsReportedNotThrown",
        testObjectsSectionTruncatedRecordStreamIsReportedNotThrown);

    run("testLocateFindsPayloadAtArbitraryOffset", testLocateFindsPayloadAtArbitraryOffset);
    run("testLocateRejectsMagicWithoutVersionFive", testLocateRejectsMagicWithoutVersionFive);
    run("testLocateRejectsTruncatedCandidate", testLocateRejectsTruncatedCandidate);
    run("testLocateEmptyBufferFindsNothing", testLocateEmptyBufferFindsNothing);

    run("testCtgObjectNamePropertyIdMatchesNameHash", testCtgObjectNamePropertyIdMatchesNameHash);
    run("testFindTailEntryByIdLocatesNameProperty", testFindTailEntryByIdLocatesNameProperty);
    run("testClassNameForHashResolvesNamedClasses", testClassNameForHashResolvesNamedClasses);
    run("testScriptNpcHpPresentAndAbsent", testScriptNpcHpPresentAndAbsent);
    run("testScriptNpcFlagsContainsIgnoreAi", testScriptNpcFlagsContainsIgnoreAi);
    run("testScriptVehicleFamilyPresentAndAbsent", testScriptVehicleFamilyPresentAndAbsent);
    run("testScriptVehicleTypeNameAndVariantName", testScriptVehicleTypeNameAndVariantName);
    run("testDecodePropertyAsUnsignedIntRejectsOddWidths", testDecodePropertyAsUnsignedIntRejectsOddWidths);

    run("testPositionPropertyIdPinnedValue", testPositionPropertyIdPinnedValue);
    run("testDecodePropertyAsPositionKind2NoOrientation", testDecodePropertyAsPositionKind2NoOrientation);
    run("testDecodePropertyAsPositionKind3WithQuaternionIgnored",
        testDecodePropertyAsPositionKind3WithQuaternionIgnored);
    run("testDecodePropertyAsPositionRejectsOtherSizes", testDecodePropertyAsPositionRejectsOtherSizes);
    run("testObjectPositionPresentAndAbsent", testObjectPositionPresentAndAbsent);
    run("testNavpointFlagsPropertyIdMatchesNameHash", testNavpointFlagsPropertyIdMatchesNameHash);
    run("testNavpointFlagsPresentAndAbsent", testNavpointFlagsPresentAndAbsent);

    run("testTriggerShapeBoxBothParamsPresent", testTriggerShapeBoxBothParamsPresent);
    run("testTriggerShapeSphereAndCylinderScalars", testTriggerShapeSphereAndCylinderScalars);
    run("testTriggerShapeCodeForTypeStringRealMapping", testTriggerShapeCodeForTypeStringRealMapping);
    run("testDecodePropertyAsFloatAndVec3RejectOtherWidths", testDecodePropertyAsFloatAndVec3RejectOtherWidths);
    run("testTriggerPropertyIdsAreDistinct", testTriggerPropertyIdsAreDistinct);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic zone-objects tests passed.\n";
    return 0;
}
