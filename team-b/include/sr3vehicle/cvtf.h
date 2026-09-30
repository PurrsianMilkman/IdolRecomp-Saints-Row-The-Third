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
// REGION 2/3 BOUNDARIES REMAIN HONESTLY OPEN, NOT RESOLVED BY THE ABOVE:
// spec §5.1's own correction only resolved Region 1's INTERNAL structure.
// Where Region 1's own content ends and Region 2 (the flat 40-name wheel
// list) begins is still not given by any confirmed formula — this reader
// uses the highest real end-offset among every Region-1 entry's own
// name/variant/option-array span as a lower bound, then a content-shape
// heuristic (a string matching the wheel-name vocabulary spec §5.1 itself
// documents) to confirm the actual start, exactly the same honest,
// non-overfit spirit as this reader's own original design. `allStrings()`
// remains the reliable, boundary-agnostic fallback for locating anything
// by content search regardless of which region it falls in.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3vehicle/errors.h"
#include "vpp/byte_view.h"

namespace sr3vehicle {

constexpr uint32_t kCvtfMagic = 0x63767466u;   // 'ftvc' (extension spelled backwards), spec §5.1
constexpr uint32_t kCvtfSectionCount = 2u;     // spec §5.1: confirmed constant in every real sample
constexpr size_t kCvtfHeaderSize = 0x10;       // 16 bytes: magic, section_count, entry_count_1, entry_count_2
constexpr size_t kCvtfRegion1QuadSize = 0x10;  // 16 bytes: {u32 a, u32 b, u32 c, u32 tag}
constexpr size_t kCvtfRegion1LeadingQuads = 2; // spec §5.1: "28 real entries + 2 leading header-like quads"
constexpr uint32_t kCvtfSentinel = 0xFFFFFFFFu; // spec §5.1: raw field value meaning "absent", not resolved via +8
constexpr size_t kCvtfFixupBase = 0x08;         // spec §5.1, FUN_00aaf9a0: every quad field resolves as v+8 (v != sentinel)
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
    // the 16-byte header, the magic matches, section_count == 2, and
    // entry_count_1/entry_count_2 make Region 1's quad table fit inside the
    // buffer. Otherwise returns false and sets `whyNot`.
    static bool tryParse(vpp::ByteView bytes, CvtfCatalog& out, std::string& whyNot);

    // Throwing convenience over tryParse().
    static CvtfCatalog parse(vpp::ByteView bytes);

    uint32_t sectionCount() const { return sectionCount_; }
    uint32_t entryCount1() const { return entryCount1_; }
    uint32_t entryCount2() const { return entryCount2_; }

    // Region 1: entryCount1() + 2 entries, in RAW QUAD-TABLE order (index 0
    // and 1 are the "2 leading header-like quads" spec §5.1 flags).
    const std::vector<CvtfRegion1Entry>& region1() const { return region1_; }

    // Region 2: best-effort, entryCount2() consecutive strings — see this
    // header's own top comment for how the start is located (a structural
    // lower bound plus a content-shape confirmation, not a confirmed
    // formula). May be shorter than entryCount2() if EOF is hit first.
    const std::vector<std::string>& region2Names() const { return region2Names_; }

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
    uint32_t entryCount1_ = 0;
    uint32_t entryCount2_ = 0;
    std::vector<CvtfRegion1Entry> region1_;
    std::vector<std::string> region2Names_;
    std::vector<CvtfRawString> allStrings_;
};

} // namespace sr3vehicle
