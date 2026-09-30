// Reader for binary `.vint_doc` UI documents (spec-vint-doc-format.md).
//
// SCOPE - WHAT IS AND IS NOT IMPLEMENTED (cloud phase, 2026-09-30)
//
// Implemented, CONFIRMED in the spec:
//  * magic 0x00003027 at +0x00 and the fixed 30-byte header (Sec1.3, Sec2);
//    every header field is exposed raw, including the two whose role is
//    OPEN (+0x0A, +0x16);
//  * the string-offset array right after the header (u32 count, count x
//    u32) and the string-pool base = the cursor position right after that
//    array - NOT header +0x16 (Sec3.1);
//  * the per-record shapes, as positioned decoders over a caller-owned
//    Cursor: critical-resource entry (Sec3.2), metadata entry (Sec3.2),
//    element-record head (Sec4), property-block override header and its
//    resolution-selection rule (Sec5), tagged property list with tags 1-7
//    and a zero terminator (Sec5).
//
// HIGH CONFIDENCE only (Sec3.1), exposed separately and labelled:
//  * reading a pool string as NUL-terminated at base + offset
//    (resolveStringNulTerminated). The spec's own 159-file pass found about
//    half the files resolving under 90% of entries cleanly (possible
//    suffix sharing, OPEN), so a caller must treat a failed or odd string
//    as expected, not as a parse error.
//
// UPDATE 2026-09-30 (spec desk review, synced): the spec now answers item 1
// below as HIGH CONFIDENCE (inferred): header +0x16 is the ABSOLUTE file
// offset of the first critical-resource entry. Items 2 and 3 remain
// HYPOTHESIS; item 3's text tension is acknowledged there in favour of the
// reading this library already uses (tag, hash, value). The full walk stays
// behind the labelled HYPOTHESIS grid in tools/vintdoc_validate.cpp (combos
// A-*, i.e. +0x16 absolute, are the spec's current reading) until the
// population sweep (bridge job 03) or the executable settles 2 and 3.
//
// NOT implemented - a full-document walk. Three facts it needs are OPEN or
// ambiguous in the spec, so no parseDocument() exists yet:
//  1. where the critical-resource section starts. Sec3.2 says "after the
//     string-offset array", but Sec3.1 puts the string character data at
//     that same position, and header +0x16's role is OPEN (Sec2/Sec8.2);
//  2. what the property block's baseline/override byte offsets are
//     relative to, and where the main cursor resumes for an element's
//     children once the selected property list has been read (Sec5);
//  3. the byte order inside a property record. Sec5 says the list is
//     "terminated by a zero tag byte" and that the name hash is read
//     "before any of these tagged values". readPropertyList() takes the
//     reading consistent with a bare zero terminator: tag byte, then (for a
//     non-zero tag) the u32 name hash, then the value. Stated here as this
//     project's reading, requested from Team A for confirmation.
// tools/vintdoc_validate.cpp scores candidate answers to 1-3 against real
// files (landing exactly on end-of-file) as a HYPOTHESIS test; nothing in
// this library depends on its outcome. See team-b/HANDOFF.md, "Requests to
// Team A".
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sr3vintdoc/errors.h"
#include "vpp/byte_view.h"

namespace sr3vintdoc {

constexpr uint32_t kMagic = 0x00003027u;     // Sec1.3, CONFIRMED
constexpr size_t kHeaderSize = 0x1E;          // Sec2, CONFIRMED (30 bytes)

// True iff `bytes` is at least 4 bytes long and starts with kMagic. A
// non-match is a text/XML document or not a vint_doc at all (Sec1.2/1.3),
// never a parse error.
bool looksLikeVintDoc(vpp::ByteView bytes);

// Bounds-checked little-endian sequential reader, mirroring the engine's own
// "read u32, advance 4" primitives (Sec2). Every read past the end throws
// FormatError.
class Cursor {
public:
    Cursor(vpp::ByteView bytes, size_t pos) : bytes_(bytes), pos_(pos) {}
    size_t pos() const { return pos_; }
    void seek(size_t pos) { pos_ = pos; }
    size_t size() const { return bytes_.size(); }
    bool atEnd() const { return pos_ == bytes_.size(); }
    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    float f32();
    void skip(size_t n);

private:
    void need(size_t n) const;
    vpp::ByteView bytes_;
    size_t pos_;
};

// Sec2. Field names follow the spec; *Raw fields are OPEN and carry no
// interpretation.
struct Header {
    uint32_t magic = 0;                 // +0x00, 0x00003027
    uint32_t reserved04 = 0;            // +0x04, 0 on 159/159 (not enforced)
    uint16_t version = 0;               // +0x08, 1 or 2 on 159/159 (not enforced - a disagreement, not an error)
    uint32_t field0ARaw = 0;            // +0x0A, OPEN (0 on 154/159, float-like otherwise)
    uint32_t metadataCount = 0;         // +0x0E
    uint32_t criticalResourceCount = 0; // +0x12
    uint32_t secondaryOffsetRaw = 0;    // +0x16: absolute offset of the critical-resource section, HIGH CONFIDENCE (spec §3.2, 2026-09-30); < file size on 159/159; NOT the string-pool base
    uint16_t elementCount = 0;          // +0x1A, top-level elements
    uint16_t animationCount = 0;        // +0x1C, top-level animations
};

// Throws FormatError if `bytes` is shorter than the header or the magic does
// not match (check looksLikeVintDoc() first to route non-matches cleanly).
Header parseHeader(vpp::ByteView bytes);

// Sec3.1. `base` is the absolute file position right after the offset array.
struct StringTable {
    std::vector<uint32_t> offsets;
    size_t base = 0;
};

// Reads the string-offset array at kHeaderSize. Throws FormatError if the
// count-driven array runs past the end of `bytes`.
//
// DISPUTED BY REAL DATA (bridge jobs 20260930T213047-team-b-zlbw and
// 20260930T234543-team-b-puhd, 159 distinct files): the spec labels "u32 N
// at 0x1E, then N u32 offsets" CONFIRMED (Sec3.1), but the u32 at 0x1E is
// only ever 1, 256 or 257 (159/159; 13 files are too small for 256/257
// entries), the "offsets" include values like 0x3F800000, 23/159 fail
// Sec3.2's own refutation test (hdr[0x16] < 0x22 + 4N), and 0 files resolve
// >= 90% clean strings (spec: 74/159). Kept as the spec states it, unchanged, pending Team A's answer
// (team-b/HANDOFF.md "Requests to Team A", item 7). Do not build on it.
StringTable parseStringTable(vpp::ByteView bytes);

// HIGH CONFIDENCE, not CONFIRMED (Sec3.1): the string at base +
// offsets[index], read up to its NUL. Returns false (and leaves `out`
// empty) for an index out of range, a start position outside the file, or
// no NUL before end-of-file.
bool resolveStringNulTerminated(vpp::ByteView bytes, const StringTable& table, uint32_t index, std::string& out);

// Sec3.2: a 1-byte selector (meaning OPEN), a 4-byte raw value, and one
// further byte only when the header version is 2.
struct CriticalResource {
    uint8_t selectorRaw = 0;
    uint32_t valueRaw = 0;
    bool hasVersion2Byte = false;
    uint8_t version2ByteRaw = 0;
};
CriticalResource readCriticalResource(Cursor& c, uint16_t version);

// Sec3.2: two string-pool indices, name then value.
struct MetadataEntry {
    uint32_t nameIndex = 0;
    uint32_t valueIndex = 0;
};
MetadataEntry readMetadataEntry(Cursor& c);

// Sec4: the fixed part of an element record, before its property block.
// The type name is resolved through the string pool (never a numeric type
// id); it should be one of kRegisteredElementTypes.
struct ElementHead {
    uint32_t typeIndex = 0;
    uint32_t nameIndex = 0;
    uint16_t childCount = 0;
    uint8_t skippedByteRaw = 0; // read and skipped by the engine; role OPEN (Sec4, Sec8.4)
};
ElementHead readElementHead(Cursor& c);

// Implemented pre-review, pending clearance: spec-lua-bindings.md Sec9.1 is
// "NOT yet cleared for implementation" (manager rule 2026-09-30); unchanged.
// spec-lua-bindings.md Sec9.1: the 13 registered element type names (13/13
// registration call sites read directly).
extern const char* const kRegisteredElementTypes[13];
bool isRegisteredElementType(const std::string& name);

// Sec5: the per-resolution override lookup that opens every property block.
struct OverrideEntry {
    uint32_t resolutionNameIndex = 0;
    uint32_t blockOffsetRaw = 0; // what it is relative to is OPEN (see top note, item 2)
};
struct PropertyBlockHeader {
    uint32_t baselineOffsetRaw = 0; // same
    std::vector<OverrideEntry> overrides; // u8 count on disk
};
PropertyBlockHeader readPropertyBlockHeader(Cursor& c);

// Sec5, CONFIRMED rule: the first override whose resolution name matches
// the active resolution wins; otherwise the baseline. Returns the raw
// offset of the selected list. `matchesActive` is given each override's
// resolution-name string-pool index.
uint32_t selectPropertyListOffset(const PropertyBlockHeader& h,
                                  const std::function<bool(uint32_t resolutionNameIndex)>& matchesActive);

// Sec5 value tags. 1 and 2 are raw 4-byte values applied through a
// dedicated path (kind OPEN); the others have the stated value kinds.
enum class PropertyTag : uint8_t {
    End = 0,
    Raw1 = 1,
    Raw2 = 2,
    Float = 3,
    String = 4, // string-pool index
    Bool = 5,
    Vec3 = 6,
    Vec2 = 7,
};

// Value size in bytes for tags 1-7 (Sec5 table); 0 for End; -1 for any tag
// outside the shipped 0-7 set, whose size the spec does not give.
int propertyValueSize(uint8_t tag);

struct Property {
    uint8_t tag = 0;
    uint32_t nameHash = 0; // raw engine string hash of the property name, NOT a pool index (Sec5)
    uint8_t value[12] = {}; // first propertyValueSize(tag) bytes are meaningful, little-endian as stored

    uint32_t rawU32() const;       // tags 1, 2, 4 (tag 4: the string-pool index)
    float f32(size_t i = 0) const; // tag 3 (i=0), tag 6 (i<3), tag 7 (i<2)
    bool boolean() const;          // tag 5: the stored byte != 0
};

struct PropertyList {
    std::vector<Property> properties;
    // False when reading stopped at a tag outside 0-7: its value size is
    // unknown, so the rest of the list cannot be delimited. Reported, not
    // thrown (a disagreement for the validator, errors.h).
    bool terminated = false;
    uint8_t unknownTag = 0;
};

// Reads one tagged property list from the cursor's current position (see
// the top note, item 3, for the record byte order used). Stops after the
// zero tag (cursor left just past it) or at an unknown tag (cursor left on
// that tag byte). Throws FormatError on running off the end of the file.
PropertyList readPropertyList(Cursor& c);

} // namespace sr3vintdoc
