#pragma once

// sr3font - Vint `text` element string building, markup and layout
// (spec-vint-render.md Sec14.2-Sec14.5, UI render unit U5), 2026-10-03.
//
// Turns a text element's source string into the 4-opcode command stream
// the text draw replays (Sec12.2): glyph runs (opcode 2), inline sheet
// images (opcode 3) and colour changes (opcode 4), each already placed by
// the final placement pass (Sec14.3), plus the measured block size that
// becomes `unscaled_size`.
//
// Font metrics come in through LayoutMetrics (the two resource callbacks
// layout calls - line height and per-character advance, Sec14.3 / spec-
// vint-font-format.md Sec6), so this file has no font-file dependency and
// can be tested with synthetic metrics as well as the real fonts.
//
// SCOPE NOTES (each also at its point of use):
//   * line frames (`line_frame_*`, Sec14.6) - NOT implemented: "exercised by
//     no shipped document" (line_frame_enable has 0 records); layout never
//     emits W/M/E pieces here. A host that turns them on gets no frame.
//   * `force_case` - dead in the shipped build (Sec14.2, CONFIRMED): never
//     read, never changes anything. Deliberately absent from LayoutParams.
//   * the localisation-key form of `text_tag` (key hash) is resolved by the
//     caller before the source string reaches this file (the caller's own
//     sr3locstr::LeStringsLookup - see tools/sr3_viewer.cpp's `drawText`).
//     Within formatInsertions itself, Sec14.2's `{N}` row (a string value
//     "first tried as a localisation key") and `{N:text_tag_crc}` row (a
//     hash looked up directly) are wired to a LeStringsLookup parameter -
//     spec-localization-format.md Sec9 item 3: slot +0x00 (lookupByKey)
//     and slot +0x04 (lookup(hash)) respectively. Still OPEN, reported
//     never fabricated: a named insertion symbol (`{NAME}`, slot +0x0c,
//     outside sr3locstr's scope) and the plain number/boolean-to-text
//     conversion (`0x00e23f40`, no spec section documents its format).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sr3locstr/le_strings.h"

namespace sr3font {

// ---- Sec14.2: source text ------------------------------------------------

// The raw `text_tag` string (element +0xf8) as this host receives it: a
// byte string. Sec14.2 step 2: decoded with the tagged-string decoder
// 0x00e22ac0 if it is in that tagged form, else widened byte-for-byte
// (0x00db06e0, HIGH). The tagged form starts with a 0x80 header byte
// (spec-lua-api-behaviour.md Sec2.3's encoder 0x00e22a30: header 0x80, one
// three-byte record per character, escape markers 0x01/0x03/0x04, end
// 0x08) but its per-record decode is not given byte-exactly in clean text,
// so a 0x80-led string is returned as OPEN (nullopt + reason), not guessed.
std::optional<std::u16string> decodeRawTextTag(const std::string& raw, std::string& openReason);

// One `insert_values` entry.
struct InsertValue {
    enum class Kind { Nil, String, Number, Boolean } kind = Kind::Nil;
    std::u16string text;
    double number = 0;
    bool boolean = false;
};

struct FormatResult {
    std::u16string text;
    std::vector<std::string> open; // OPEN reasons (non-empty = the result is not trustworthy)
    size_t insertionsVanished = 0; // {N}/{}/%s whose index was at/beyond the value count
};

// The insertion formatter 0x00e22b30 (Sec14.2 table). `valuesSet` false =
// the element never set `insert_values`: the value count is 0, so EVERY
// {N}/{}/%s vanishes (specified behaviour - `{0} and {1}` -> ` and `).
// `valuesSet` true = the builder passes a fixed count of 32 (Sec14.2);
// entries past `values` are nil.
//
// `locStrings`: the real `.le_strings` lookup (spec-localization-format.md
// Sec6), used for two of Sec14.2's rows - spec-localization-format.md Sec9
// item 3 ("slot +0x00, not +0x04, is the 'game lookup' Sec14.2's insertion
// table means by 'a string value is first tried as a localisation key',
// call site 0x00e22e98"):
//   * `{N}` with a string value: `locStrings.lookupByKey(...)` (slot +0x00,
//     0x00842b40 -> 0x00849df0: hash the string, then look it up) - the
//     looked-up text on a hit, the string as plain text on a miss.
//   * `{N:text_tag_crc}`: `locStrings.lookup(hash)` directly (slot +0x04,
//     0x00849950, call site 0x00e22e49) - N is already a key HASH, never
//     re-hashed as a string.
// Pass a default-constructed (empty) LeStringsLookup when no tables are
// loaded: every lookup then misses, which is exactly the fallback/"{Hash
// not found!}" the spec describes for that case.
FormatResult formatInsertions(const std::u16string& src, bool valuesSet, const std::vector<InsertValue>& values,
                              const sr3locstr::LeStringsLookup& locStrings);

// ---- Sec14.3 layout ------------------------------------------------------

struct LayoutMetrics {
    // Resource slot +0x1c of the element's font (primary), already resolved.
    int lineHeight = 0;
    // Resource slot +0x20 via the Vint wrapper 0x00e2fdd0: advance of `ch`
    // before `next`, 0 when the character is not in the font.
    std::function<int(uint32_t ch, uint32_t next)> advance;
    // The Vint font pool's "break anywhere" flag (slot +0x24).
    bool breakAnywhere = false;
};

// Answer to an `[image:NAME]` lookup in the bitmap-sheet directory
// (0x00e22290). found = false -> the record is still emitted, sheet index
// -1, size 0, no advance (Sec14.4, HIGH for the size/advance).
struct InlineImageInfo {
    bool found = false;
    double width = 0, height = 0;
};
using InlineImageLookup = std::function<InlineImageInfo(const std::string& name)>;

enum class HorzAlign { Left = 0, Center = 1, Right = 2 };

struct LayoutParams {
    double textScaleX = 1, textScaleY = 1; // `text_scale` - style entry 0's scale
    double wrapWidth = 0;                  // the EFFECTIVE wrap width (+0xbc): wrap_width if word_wrap else 0
    HorzAlign align = HorzAlign::Left;
    int leading = 0;  // signed byte (+0xe4), already clamped by the setter rule
    int kerning = 0;  // signed byte (+0xe5), already clamped
    int32_t font = -1; // the font handle/entry the run records carry
};

struct StreamRecord {
    enum class Op { Run = 2, Image = 3, Colour = 4 } op = Op::Run;
    // Run / Image (placed: final x, y after Sec14.3's placement pass)
    double x = 0, y = 0, sx = 1, sy = 1;
    // Run
    int32_t font = -1;
    int spacing = 0;      // the kerning byte (low byte written, read back signed)
    std::u16string text;  // run text WITHOUT the odd-length padding NUL
    // Image
    std::string imageName; // low-byte-truncated, up to 63 characters
    InlineImageInfo image;
    // Colour: -1 = inherit the pass's base component
    double r = -1, g = -1, b = -1, a = -1;
    // Placement bookkeeping (Run/Image)
    int line = -1;
    double height = 0; // the registered height
};

struct LayoutLine {
    double width = 0, height = 0, x = 0, y = 0;
};

struct LayoutResult {
    std::vector<StreamRecord> records;
    std::vector<LayoutLine> lines;
    double width = 0, height = 0; // block size -> unscaled_size (Sec14.3 step 2)
    size_t streamBytes = 0;        // including the trailing zero dword
    size_t unrecognizedTags = 0;
    size_t formatDepthCapped = 0;  // [format] pushes refused at the 16-entry cap (CHOSEN, see .cpp)
    bool truncated = false;        // stream capacity (12,800 bytes) reached - records dropped (CHOSEN)
};

// Layout 0x00e36dd0 (run builder 0x00e36070, segment dispatcher 0x00e369f0)
// over the already-formatted string. `imageLookup` may be empty (every
// [image:] then resolves as unknown).
LayoutResult layoutText(const std::u16string& text, const LayoutParams& params, const LayoutMetrics& metrics,
                        const InlineImageLookup& imageLookup);

// ---- markup support exposed for tests ---------------------------------------

// The 26 registered colour names (Sec14.4), in registration order.
struct NamedColour {
    const char* name;
    uint8_t r, g, b;
};
const std::vector<NamedColour>& namedColours();
// Case-insensitive prefix lookup (only as many characters as `arg` has, the
// first registered name it starts). nullptr if none.
const NamedColour* findNamedColour(const std::string& arg);

} // namespace sr3font
