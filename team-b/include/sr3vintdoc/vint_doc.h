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
//    array - NOT header +0x16 (Sec3.1). DOWNGRADED 2026-09-30: the spec now
//    labels this layout HYPOTHESIS, contradicted by this project's sweep
//    (bridge jobs zlbw/puhd); parseStringTable() is kept unchanged as the
//    pre-review reading, see its own note;
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
// UPDATE 2026-09-30 (spec desk review, synced): the spec answered item 1
// below as HIGH CONFIDENCE (inferred): header +0x16 is the ABSOLUTE file
// offset of the first critical-resource entry. DOWNGRADED the same day to
// HYPOTHESIS (that answer rests on Sec3.1's string-array layout, which real
// data contradicts - Requests to Team A, item 7). Items 2 and 3 remain
// HYPOTHESIS; item 3's text tension is acknowledged there in favour of the
// reading this library already uses (tag, hash, value). The full walk stays
// behind the labelled HYPOTHESIS grid in tools/vintdoc_validate.cpp (combos
// A-*, i.e. +0x16 absolute, are the spec's current reading) until the
// population sweep (bridge job 03) or the executable settles 2 and 3.
//
// UPDATE 2026-10-03 - FULL-DOCUMENT WALK NOW IMPLEMENTED (parseDocument(),
// bottom of this header). The layout is CONFIRMED by Team A's disassembly
// of the binary loader (spec-vint-doc-format.md as synced at main commit
// 67455c3: dispatch 0x00e212a0, reader 0x00e20500, string table
// 0x00e1fbe0, element reader 0x00e2a280, property block 0x00e29860), and
// INDEPENDENTLY reproduced from real data by this project before that sync
// arrived - the Team B cross-check spec Sec8 item 12 asks for. How the
// data side went (kept because it is the independent half): tools/vintdoc_validate.cpp's
// original 12-combo grid was finally run (results/baseline_2026-10-01.md
// already held its output): 0/146 landings for every combo, because every
// combo assumed the string table sits at 0x1E. Reading real bytes directly
// (interface_startup.vpp_pc's smallest files first) gave a different,
// simpler layout that lands EXACTLY on end-of-file for 159/159 distinct
// documents (777/777 entries across interface_startup.vpp_pc and
// interface.vpp_pc - interface.vpp_pc's 618 copies are byte-identical to
// the 159), with every one of the 5,326 distinct element records' type
// reference resolving to one of the 13 registered type names. The answers to the
// three items below (data-derived first, then matched point for point by
// 67455c3's CONFIRMED text; see parseDocument's own comment for the full
// evidence list):
//  1. header +0x16 is the ABSOLUTE offset of the STRING TABLE, which is
//     the LAST section of the file (u32 count, u32 pool byte size, count x
//     u32 pool offsets, then the pool; ends exactly at EOF 159/159). The
//     critical-resource entries start at 0x1E, then metadata, then the
//     element tree, which ends exactly at header +0x16 (159/159).
//  2. baseline/override offsets are file-absolute; every list of a block
//     is stored inline, back to back, starting right after the override
//     table (overrides first, the baseline last, 504/504 distinct override
//     blocks). CONFIRMED semantics (67455c3): the loader reads the matching
//     override list (if any), then ALWAYS jumps to the baseline and reads
//     it (records already applied by the override are skipped), and leaves
//     the cursor just past the BASELINE list - which, the baseline being
//     stored last, is also the end of the block's stored lists.
//  3. readPropertyList()'s tag/hash/value order is right (it is the only
//     order under which the walk lands; tag-first; CONFIRMED 67455c3). Tag 1
//     is a signed and tag 2 an unsigned 32-bit integer, applied through the
//     ordinary descriptor setter (CONFIRMED 67455c3, closing Sec8 item 10).
// One correction to Sec4 found on the way (also CONFIRMED by 67455c3): the element record's FIRST u32
// is the instance NAME and the SECOND is the TYPE (readElementRecordHead();
// readElementHead() below keeps its original Sec4 field naming unchanged
// for compatibility - see its own note).
//
// (Original note, kept for history.) NOT implemented - a full-document walk. Three facts it needs are OPEN or
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
    uint32_t secondaryOffsetRaw = 0;    // +0x16: absolute offset of the critical-resource section is the spec's HYPOTHESIS (§3.2, downgraded from HIGH CONFIDENCE 2026-09-30); < file size on 159/159; NOT the string-pool base
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

// HIGH CONFIDENCE, not CONFIRMED (Sec3.1), and it rests on the string-array
// layout downgraded to HYPOTHESIS 2026-09-30 (see parseStringTable): the string at base +
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
//
// 2026-10-03, REAL DATA DISAGREES WITH THE FIELD ORDER: across the 5,326
// element records of the 159 distinct real documents the FIRST u32
// resolves to the instance name and the SECOND to one of the 13 registered
// type names (5,326/5,326; the first u32 resolves to a registered type
// name on only 12/5,326 - instance names that happen to equal a type name).
// This decoder and its field names are left exactly as Sec4 states them so
// existing callers/tests keep their meaning; parseDocument() uses
// readElementRecordHead() below, which names the fields by the real order.
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

// =========================================================================
// Full-document walk (2026-10-03). The layout is CONFIRMED by disassembly
// (spec-vint-doc-format.md Sec2-Sec5 as synced at main 67455c3 - this
// worktree's own copy of that file predates the sync), and independently
// reproduced from real data by this project (the Team B cross-check of
// Sec8 item 12). Team B's own figures, measured with this parser and the
// throwaway probes that preceded it, on interface_startup.vpp_pc (159
// distinct documents) and interface.vpp_pc (618 .vint_doc entries, every
// one byte-identical to one of the 159, so 777/777 for every line):
//  * string table at absolute header +0x16 (u32 count N, u32 pool size L,
//    N x u32 offsets, pool): ends EXACTLY at EOF 159/159; the offsets tile
//    the pool exactly (offset[0] == 0, each next == previous + strlen + 1)
//    159/159; 8,893/8,893 strings NUL-terminated inside the pool;
//  * critical resources (Sec3.2) from 0x1E, then metadata (Sec3.2), then
//    elements + animations (Sec4), and the tree ends EXACTLY at header
//    +0x16: 159/159;
//  * element record: u32 name index, u32 type index, u16 child count, u8
//    raw byte (0 on 5,281, 1 on 45 of 5,326 records), then the Sec5
//    property block inline; type resolves to a registered name 5,326/5,326;
//  * property block: baseline offset == position right after the 5-byte
//    table on 4,822/4,822 blocks without overrides; all 504 blocks with
//    overrides have exactly one ("640x480"), its list right after the
//    13-byte table and the baseline list right after the override list;
//  * property record = tag u8, name hash u32, value (tags 1-7 only); every
//    name hash is the engine's lower-cased seed-0 CRC-32 (sr3save::nameHash)
//    of a real name: 49 of the 55 distinct hashes match a string literal
//    in the shipped Lua scripts, including 13 of spec-lua-bindings.md
//    Sec9.2's 17 `element` names - the other 4 are exactly Sec9.2's
//    read-only ones (screen_size/screen_nw/screen_se/unscaled_size), never
//    stored on disk.
// =========================================================================

// The string table as it is really stored (Sec3.1, CONFIRMED 67455c3).
struct DocumentStringTable {
    size_t start = 0;        // absolute file offset (header +0x16)
    uint32_t poolSize = 0;   // L, the second u32 of the table
    std::vector<uint32_t> offsets;
    std::vector<std::string> strings; // offsets resolved, NUL-terminated inside the pool
    bool endsAtEof = false;  // pool ends exactly at end of file (159/159 real documents)
};

// Throws FormatError when N == 0 (the loader fails the whole document),
// when L + 4N exceeds the bytes remaining (the loader's own check), or when
// an offset lies outside the pool or has no NUL before the pool's end (the
// loader does no such check; this parser refuses rather than read garbage).
DocumentStringTable parseTrailingStringTable(vpp::ByteView bytes, const Header& h);

// The element record head in its real on-disk order (Sec4, CONFIRMED
// 67455c3; see ElementHead's note): name first, then type.
struct ElementRecordHead {
    uint32_t nameIndex = 0;
    uint32_t typeIndex = 0;
    uint16_t childCount = 0;
    uint8_t rawByte = 0; // never read by the loader (67455c3); role OPEN (Sec8.4)
};
ElementRecordHead readElementRecordHead(Cursor& c);

struct ElementNode {
    uint32_t nameIndex = 0;
    uint32_t typeIndex = 0;
    std::string name;
    std::string type; // one of kRegisteredElementTypes on all real data
    uint8_t rawByte = 0;
    size_t fileOffset = 0; // where the record starts (diagnostics)

    struct ResolutionOverride {
        uint32_t resolutionNameIndex = 0;
        std::string resolutionName; // "640x480" on every real override
        PropertyList list;
    };
    PropertyList baseline;
    std::vector<ResolutionOverride> overrides; // every stored override list, in table order
    // Diagnostic only, never required: the override lists and then the
    // baseline list are stored back to back right after the override table
    // (true on 5,326/5,326 real records).
    bool listsInlineInOrder = true;
    std::vector<ElementNode> children;

    // The properties the loader applies (Sec5, CONFIRMED 67455c3): the first
    // override whose resolution name equals `activeResolution` (none when it
    // is empty) is applied in full, then the baseline, minus every baseline
    // record whose name hash that override already applied.
    std::vector<Property> effectiveProperties(const std::string& activeResolution) const;
};

struct ResolvedMetadata {
    std::string name;
    std::string value;
};

struct Document {
    Header header;
    DocumentStringTable strings;
    // Sec3.2: selector/value raw. 67455c3: the loader registers an entry
    // only when the selector is 0; in version 2 the trailing byte is the
    // autoload flag; what selector 1 means is OPEN. Not used by anything
    // in this project yet.
    std::vector<CriticalResource> criticalResources;
    std::vector<MetadataEntry> metadata;
    std::vector<ResolvedMetadata> metadataStrings; // same order as `metadata`
    std::vector<ElementNode> elements;   // header +0x1A records
    std::vector<ElementNode> animations; // header +0x1C records (Sec4: the same record shape)
    size_t treeEnd = 0; // main-cursor position after the last top-level record

    // The value of the first metadata entry named `name` (e.g.
    // "lua_script_file", "document_depth"), or nullptr.
    const std::string* metadataValue(const std::string& name) const;
    // Every element and animation record, recursively.
    size_t totalRecordCount() const;
    // The structural landing checks the validator scores (true on 159/159
    // real documents): the tree ends exactly at header +0x16 and the string
    // pool ends exactly at end of file. The loader itself checks neither.
    bool treeEndsAtStringTable() const { return treeEnd == strings.start; }
    bool landsExactly() const { return treeEndsAtStringTable() && strings.endsAtEof; }
};

// The full walk, following the loader's own read order (0x00e20500 ->
// 0x00e2a280 -> 0x00e29860). Every override list is read and kept; the
// main cursor continues after each block's BASELINE list. Throws
// FormatError on: bad magic or short header; a string-table failure (see
// parseTrailingStringTable); a string index outside the table (the
// loader's accessor would return null); a property-list offset outside the
// file; a tag outside 1-7 (the loader would desynchronise); running off the
// tree region [0, header +0x16) on the main cursor; or a hostile depth or
// record count.
Document parseDocument(vpp::ByteView bytes);

} // namespace sr3vintdoc
