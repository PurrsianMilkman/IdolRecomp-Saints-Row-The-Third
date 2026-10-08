// Reader for `.cvtf_pc` — the per-vehicle COMPILED CUSTOMIZATION CATALOGUE
// (component/color/wheel slot definitions), built from
// spec-geometry-format.md §5/§5.1. NOT a geometry format despite the
// directory this file's sibling headers live in — §5's own text
// reclassifies it, and this reader is placed under sr3vehicle (not
// sr3geometry/sr3mesh) for that reason, alongside `.ccar_pc`'s own reader
// (vehicle.h), which already documents this exact file as its own open
// "where does vehicle PAINT come from" thread.
//
// SCOPE: this is the ID-2 `'ftvc'`-tagged format only (spec §5.1). The
// DIFFERENT, deeper structure registered under type ID 8 ("Character cvtf",
// spec §5.2) is NOT this format — tryParse()/parse() reject it via the
// magic/section_count gate below, same as any other non-applicable buffer.
//
// CORRECTED 2026-09-29, SAME DAY AS THIS READER'S FIRST DRAFT: this
// reader's own first version used a "snap to nearest preceding NUL byte"
// heuristic against Region 1 quad field `b`, because at the time §5.1 only
// documented `b` as "real and close but not reliably byte-exact" (a small-
// sample overclaim later retracted in the same section). Team A separately
// decompiled the real parser (`FUN_00aaf9a0`, spec §5.1, same day) and
// found the REAL, disassembly-confirmed, population-verified (2,757/2,757
// real Region-1 entries across 120 distinct real files) formula, which
// THIS version implements instead — the heuristic version was wrong for
// several entries in the very sample this reader's own author scored well
// on (it looked plausible, but "plausible on inspection" is exactly the
// failure mode this project's own "no invented fixes" discipline warns
// about). Independently re-verified directly against real
// `car_4dr_genki.cvtf_pc` bytes before adopting: 29/30 real Region-1
// entries land the corrected formula's `a+8` on a clean, printable,
// null-terminated name (the one exception is one of the "2 leading
// header-like quads" §5.1 itself flags as not necessarily name-bearing) —
// a materially better result than the retracted heuristic ever produced.
//
// THE REAL FORMULA (spec §5.1, `FUN_00aaf9a0`): every Region-1 quad field
// resolves against ONE SHARED BASE, file offset 0x08 (the address of
// `entry_count_1` itself) — the same "-1 sentinel -> null, else += base"
// idiom this project has already documented for other formats. For a raw
// field value `v != 0xFFFFFFFF`, the resolved absolute file offset is
// `v + 8`. No extra per-field offset beyond this one shared +8:
//   `a` -> this entry's own NAME string, at `a + 8`.
//   `b` -> the variant/type token string immediately after the name, at
//          `b + 8` (landing exactly one byte past the name's own NUL).
//   `c` -> this entry's option-list ARRAY, at `c + 8`, elements each 0x3C
//          (60) bytes. The array's own per-element internal layout is
//          NOT further resolved by spec §5.1 ("a deeper structure than
//          this task needed to fully unpack") — exposed raw here, plus one
//          convenience field THIS reader's own direct real-byte check
//          found (not itself spec-stated): element word[1], put through
//          the SAME sentinel/+8 idiom, frequently lands on a clean
//          printable string (e.g. "COMPONENT_PAINT") — offered at lower
//          confidence than a/b/c/tag themselves, flagged as such.
//   `tag & 0xFF` -> the element COUNT of the array `c` points to. The
//          upper three bytes of `tag` are never read by the real parser
//          itself (spec §5.1: OPEN, would need a further consumer trace).
//
// A REAL, DIRECT CHECK ALREADY DONE (this session, not spec text): every
// color-family slot's OWN option array in `car_4dr_genki.cvtf_pc`
// (`Body Color1/2/3`, plain `Body Color`, `Rim Color`, `Trim Color1/2`,
// `Trim Color`, `Interior Color`) has `optionCount == 1`, and that single
// 60-byte element's first word is the IDENTICAL raw value `0x9158730A`
// across every one of those 9 different slots, with two further words
// resolving (same +8 idiom) to enum-shaped strings like `COMPONENT_PAINT`
// — i.e. a shared, generic "this slot accepts a PAINT-type color
// assignment" descriptor, not a per-slot packed color. No plausible packed
// RGB (as raw bytes, or reinterpreted as IEEE-754 floats in a 0..1 or
// 0..255-ish range) appears anywhere in these real option elements. This
// is a real, checked negative for "does the option array carry an RGB
// itself" — the answer, for this file, is no; the real per-preset color
// NAME lives in Region 3 instead, and the real RGB for that name lives in
// the separate base-game `vehicle_cust_color_pool.xtbl` (694 real rows,
// `misc_tables.vpp_pc`, XML text, not decoded by this reader — see this
// session's own write-up for the exact real values found this way).
//
// CORRECTED 2026-10-03 (TEAM B cross-check task, reader-correctness pass
// against spec §5.1's own 2026-10-03 HIGH CONFIDENCE header correction):
// this reader's ENTIRE header/Region-1-boundary model below was built on
// the ORIGINAL (now-superseded) "16-byte header, entry_count_2 is a real
// count" reading. Spec §5.1 flagged this as HIGH CONFIDENCE, NOT YET
// CONFIRMED, and said "a one-file hex check of +0x0C, +0x14, +0x24 and
// +0x2C would make this CONFIRMED" — that exact check was done here,
// directly against all 5 real byte-identical copies of
// `car_4dr_standard03.cvtf_pc` (16,089 bytes each, extracted from
// `vehicles.vpp_pc` via `tools/vpp_extract.cpp`), and CONFIRMS the real
// header is 48 bytes (`0x30`), not 16:
//   +0x00 magic 'ftvc', +0x04 section_count (==2)
//   +0x08 region1Count  - count of Region 1's quad table (28 in this file)
//   +0x0C region1OffsetRaw - OFFSET of Region 1's quad table (NOT a count;
//          this project's reader used to call this "entryCount2" - real
//          value in this file is 40 (0x28), resolving via the same +8
//          idiom as every other field to 48 - i.e. BYTE-IDENTICAL to the
//          old reader's hand-rolled "16-byte header + 2 leading
//          header-like quads = 48" computation, but for the RIGHT reason;
//          the old computation only ever worked by the exact coincidence
//          spec §5.1 itself calls out, and silently breaks on any file
//          where this offset isn't 40)
//   +0x10 table2OffsetRaw, +0x14 table2Count - a second 16-byte-record
//          table (sentinel/0 in this file - absent)
//   +0x18 table3OffsetRaw, +0x1C table3Count - a third table, "processed
//          last" (offset 9748 raw / 9756 resolved, count 5 in this file -
//          this reader does NOT further decode its record shape, see
//          Region 3 note below)
//   +0x20 table size "array1" offset/+0x24 count - a dword-offset array,
//          each element through the Component-name resolver `0x00a96040`
//   +0x28 "array2" offset/+0x2C count - a second such array, resolver
//          `0x00a960e0`
// All verified directly against real bytes (not just the spec's own
// citation): array1 (offset 8440 raw / 8448 resolved, count 45) resolves
// EVERY ONE of its 45 elements (each itself a +8-fixed-up offset, same
// idiom) to a clean, printable, null-terminated string — and they are
// EXACTLY the Region-2 wheel-name vocabulary spec §5.1 already documents
// by content (`W_000_F_Extra_High_Profile` ... `W_006_R_Extra_Low_Profile`,
// 20 front + 25 rear = 45) — which also directly SETTLES spec §5.1's own
// still-open "Region 2 40 vs 45" composition question in favour of **45**,
// not 40 (the old reader's `entryCount2` — really the Region-1 offset
// field, coincidentally 40 in this file — was never a real count of this
// array at all). array2 (offset 9648 raw / 9656 resolved, count 7)
// likewise resolves all 7 elements to `Family001`..`Family007`, exactly
// matching spec §5.1's own HYPOTHESIS for this array's purpose. Given
// this, Region 2 below is now read via the REAL array1 mechanism (a dword
// array, each element independently resolved), not the old content-shape
// scanning heuristic — see the Region 2 note below for exactly what
// changed and what's still not modelled (the two resolver functions
// themselves are not re-derived here, only the plain string each element
// resolves to).
//
// REGION 3 / table2 / table3 REMAIN HONESTLY OPEN: this reader exposes
// table2Offset()/table2Count()/table3Offset()/table3Count() as raw,
// resolved-but-undecoded fields (table3's count of 5 in the worked sample
// matches Region 3's own already-documented "5 complete style presets",
// strongly suggesting table3 IS Region 3's real index — but this reader
// does not attempt to decode table3's per-record layout, since spec §5.1
// itself does not give one and inventing one here would violate this
// project's own no-invented-fixes discipline). `allStrings()` remains the
// reliable, boundary-agnostic fallback for locating anything by content
// search regardless of which region it falls in.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3vehicle/errors.h"
#include "vpp/byte_view.h"

namespace sr3vehicle {

constexpr uint32_t kCvtfMagic = 0x63767466u;   // 'ftvc' (extension spelled backwards), spec §5.1
constexpr uint32_t kCvtfSectionCount = 2u;     // spec §5.1: confirmed constant in every real sample
// 48 bytes: magic, section_count, region1Count/region1OffsetRaw,
// table2OffsetRaw/table2Count, table3OffsetRaw/table3Count,
// array1OffsetRaw/array1Count, array2OffsetRaw/array2Count. CORRECTED
// 2026-10-03 from this reader's own original (wrong) 16-byte assumption -
// see this header's own top comment for the real-byte check that
// confirmed spec §5.1's HIGH CONFIDENCE 48-byte finding.
constexpr size_t kCvtfHeaderSize = 0x30;
constexpr size_t kCvtfRegion1QuadSize = 0x10;  // 16 bytes: {u32 a, u32 b, u32 c, u32 tag}
constexpr uint32_t kCvtfSentinel = 0xFFFFFFFFu; // spec §5.1: raw field value meaning "absent", not resolved via +8
constexpr size_t kCvtfFixupBase = 0x08;         // spec §5.1, FUN_00aaf9a0: every field (header offsets AND quad
                                                 // fields alike - this reader's own real-byte check confirms the
                                                 // SAME +8 idiom applies to the header's own offset fields, not
                                                 // just Region-1's a/b/c) resolves as v+8 (v != sentinel)
constexpr size_t kCvtfOptionElementSize = 0x3C; // 60 bytes, spec §5.1

// One raw 0x3C-byte element of a Region-1 entry's option-list array (spec
// §5.1: existence/stride/count CONFIRMED by disassembly; per-element
// internal layout beyond word[1] OPEN, not further resolved even by
// Team A's own decompile pass). See this header's own top comment for the
// real, direct check already done against `car_4dr_genki.cvtf_pc`'s own
// color-slot elements (a shared type-tag, no packed RGB found).
struct CvtfOptionElement {
    std::array<uint32_t, 15> words{}; // the 0x3C bytes, 15 little-endian u32s, raw

    // Best-effort, THIS READER'S OWN observation (not spec-stated): applying
    // the same sentinel/+8 idiom to word[1] frequently lands on a clean
    // printable string (e.g. "COMPONENT_PAINT"). Empty if word[1] is the
    // sentinel or does not resolve to a printable NUL-terminated string.
    std::string word1AsString;
};

// One 16-byte Region 1 quad (spec §5.1) plus this reader's own resolved
// name/variant/option-array fields, using the real, disassembly-confirmed
// formula (this header's own top comment).
struct CvtfRegion1Entry {
    uint32_t a = 0, b = 0, c = 0, tag = 0; // raw quad fields, exactly as stored
    size_t quadIndex = 0;                  // position in the raw 16-byte quad table (file order)

    bool aIsSentinel = false; // a == kCvtfSentinel: this entry has no name (rare - spec §5.1 notes a "small number" exist)
    bool bIsSentinel = false; // b == kCvtfSentinel: no variant token - the real parser defaults to a shared global instead
    bool cIsSentinel = false; // c == kCvtfSentinel: no option array

    std::string name;    // a+8, when !aIsSentinel (spec §5.1: CONFIRMED, 2,757/2,757 population-verified)
    std::string variant; // b+8, when !bIsSentinel (spec §5.1: CONFIRMED, 2,757/2,757 population-verified)

    uint32_t optionCount = 0;   // tag & 0xFF (spec §5.1: CONFIRMED)
    uint32_t tagUpperBits = 0;  // tag >> 8 - OPEN, never read by the real parser itself per spec §5.1
    std::vector<CvtfOptionElement> options; // c+8, optionCount * 0x3C bytes (spec §5.1: CONFIRMED existence/stride/count)
};

// One string in the raw, ordered, offset-tagged dump of everything from the
// end of Region 1's quad table to EOF — a boundary-agnostic fallback for
// locating any specific string (Region 2 wheel names, Region 3 style-preset
// content) by content search when a region boundary is genuinely open (see
// this header's own top comment).
struct CvtfRawString {
    size_t offset = 0;
    std::string text;
};

class CvtfCatalog {
public:
    // Non-throwing. Returns true and fills `out` iff the buffer is at least
    // the 48-byte header, the magic matches, section_count == 2, the
    // region1 offset field resolves to a non-sentinel absolute offset, and
    // region1Count() makes Region 1's quad table fit inside the buffer.
    // Otherwise returns false and sets `whyNot`.
    static bool tryParse(vpp::ByteView bytes, CvtfCatalog& out, std::string& whyNot);

    // Throwing convenience over tryParse().
    static CvtfCatalog parse(vpp::ByteView bytes);

    uint32_t sectionCount() const { return sectionCount_; }

    // +0x08: count of Region 1's quad table (CONFIRMED - empirical, this
    // reader's own 2026-10-03 real-byte check; was already correct under
    // the old 16-byte-header reading too, this field's meaning didn't
    // change).
    uint32_t region1Count() const { return region1Count_; }
    // +0x0C raw value: this project's reader used to call this
    // "entryCount2" and treat it as a count. CORRECTED 2026-10-03: it is
    // the OFFSET of Region 1's own quad table (resolved via the shared
    // +8 idiom, same as every other field here) - see this header's own
    // top comment. Exposed raw mainly for diagnostics; region1() already
    // starts at the resolved location.
    uint32_t region1OffsetRaw() const { return region1OffsetRaw_; }

    // Region 1: region1Count() entries, no more "2 leading header-like
    // quads" (that was an artifact of the old wrong 16-byte header
    // computation happening to land on the right byte offset by
    // coincidence in the one sample checked at the time - CORRECTED
    // 2026-10-03, see this header's own top comment).
    const std::vector<CvtfRegion1Entry>& region1() const { return region1_; }

    // +0x10/+0x14 - a second 16-byte-record table. OPEN: this reader
    // exposes the raw/resolved offset and count but does not decode this
    // table's record shape (spec §5.1 does not give one). nullopt when
    // the raw offset field is the sentinel (absent - the common case;
    // car_4dr_standard03.cvtf_pc has no table 2).
    std::optional<size_t> table2Offset() const { return table2Offset_; }
    uint32_t table2Count() const { return table2Count_; }

    // +0x18/+0x1C - a third table, "processed last" per spec §5.1. OPEN
    // for the same reason as table2 - NOT decoded here. In the worked
    // sample this table's count (5) matches Region 3's own
    // already-documented "5 complete style presets" exactly, which this
    // reader's own comment flags as a strong but unconfirmed hint that
    // this IS Region 3's real index table.
    std::optional<size_t> table3Offset() const { return table3Offset_; }
    uint32_t table3Count() const { return table3Count_; }

    // +0x20/+0x24 - a dword-offset array, each element individually
    // resolved (same sentinel/+8 idiom) to a plain string. Per spec §5.1,
    // elements are further resolved through the Component-name resolver
    // `0x00a96040` at runtime; this reader exposes only the resolved
    // string, not that further resolution. THIS READER'S OWN real-byte
    // check (2026-10-03, car_4dr_standard03.cvtf_pc, 5/5 byte-identical
    // real copies): all 45 elements resolve to clean, printable,
    // null-terminated strings exactly matching spec §5.1's own
    // documented Region-2 wheel-name vocabulary (20 front + 25 rear) -
    // this is almost certainly Region 2 itself, and directly settles
    // spec §5.1's own open "40 vs 45" composition question in favour of
    // 45. Region 2 below (`region2Names()`) is populated from this array.
    const std::vector<std::string>& wheelNames() const { return region2Names_; }
    // Back-compat alias for wheelNames() - kept because this was this
    // reader's pre-2026-10-03 name for the same concept (then populated
    // by a content-shape scanning heuristic instead of this real array;
    // no other code in this project references it, grep-checked).
    const std::vector<std::string>& region2Names() const { return region2Names_; }

    // +0x28/+0x2C - a second such dword-offset array, resolver
    // `0x00a960e0` per spec. THIS READER'S OWN real-byte check: all 7
    // elements resolve to `Family001`..`Family007`, exactly matching
    // spec §5.1's own HYPOTHESIS for this array's purpose.
    const std::vector<std::string>& familyNames() const { return familyNames_; }

    // The complete, ordered, offset-tagged string dump described in this
    // header's own top comment — the reliable fallback for locating any
    // specific string by content.
    const std::vector<CvtfRawString>& allStrings() const { return allStrings_; }

    // Finds a Region 1 entry by name: exact case-insensitive match first,
    // then (if none) a case-insensitive substring match. Returns nullptr if
    // neither finds anything.
    const CvtfRegion1Entry* findEntryByName(const std::string& name) const;

private:
    uint32_t magic_ = 0;
    uint32_t sectionCount_ = 0;
    uint32_t region1Count_ = 0;
    uint32_t region1OffsetRaw_ = 0;
    uint32_t table2Count_ = 0;
    uint32_t table3Count_ = 0;
    std::optional<size_t> table2Offset_;
    std::optional<size_t> table3Offset_;
    std::vector<CvtfRegion1Entry> region1_;
    std::vector<std::string> region2Names_;  // wheel names, from array1 (+0x20/+0x24)
    std::vector<std::string> familyNames_;   // from array2 (+0x28/+0x2C)
    std::vector<CvtfRawString> allStrings_;
};

} // namespace sr3vehicle
