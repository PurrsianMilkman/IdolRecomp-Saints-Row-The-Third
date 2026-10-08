#pragma once

// sr3font - the `.vf3_pc` bitmap-font file, the game's named font table in
// its DEFAULT language arrangement, the metric callbacks Vint text layout
// measures with, and per-glyph draw geometry (2026-10-03, UI render unit
// U5's font half).
//
// Sources (clean spec text only):
//   * spec-vint-font-format.md Sec1-Sec7 - file layout, kerning lookup (and
//     its shipped quirk), loader rules, named font table, Vint font pool,
//     metric callbacks, worked numbers (*emulated* = the real shipped
//     functions run under CPU emulation against the real files - ground
//     truth for matching behaviour exactly);
//   * spec-vint-render.md Sec14.1 - the glyph-run drawer's per-glyph
//     geometry (pen, quad corners, near-edge half-texel UV inset, control /
//     out-of-font characters, the two-layer secondary/primary draw).
//
// SCOPE - what is deliberately NOT here (each labelled where it would sit):
//   * derived (scaled) GAME fonts (spec-vint-font-format.md Sec3 "Derived
//     fonts", 0x00dcaee0) - OUT OF SCOPE by the task brief. Every font here
//     is a plain font: scale 1, vertical draw offset 0, line-height
//     adjustment 0 (the plain-id behaviour Sec3 itself states). This is NOT
//     the same thing as a text element's `text_scale`/`[scale:]` markup,
//     which lives in the layout (vint_text.h) and the draw matrix.
//   * the three non-default language arrangements (codes 3/0xd/0xb, needing
//     jap/font_sk/fnt_chinese and derived fonts) - OUT OF SCOPE, CHOSEN: this
//     host has no language-code concept; only the "anything else" row of
//     Sec4.2 is implemented (DefaultFontTable below).
//   * resource-callback slot +0x28 (string width) - its caller is OPEN
//     (Sec6/Sec8 item 3) and layout never calls it; not implemented.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sr3font {

// Thrown only for a violation of a CONFIRMED hard rule: the file is too
// short for the 0xd0-byte header, the version is not 4 (the loader's own
// failure condition, Sec2.1/Sec3 step 2), or the four tables do not fit
// in the file (Sec1's layout check: size = 0xd0 + 6K + 24C exactly - a
// file LONGER than that is still parsed and reported via trailingBytes).
class FontFormatError : public std::runtime_error {
public:
    explicit FontFormatError(const std::string& what) : std::runtime_error(what) {}
};

// spec-vint-font-format.md Sec2.2 item 1: 6 bytes on disk.
struct KerningPair {
    uint16_t left = 0;   // left glyph index
    uint16_t right = 0;  // right glyph index
    int8_t adjust = 0;   // font units, added to the left glyph's advance
    uint8_t pad = 0;     // 0xCD in every shipped record, never read
};

// Sec2.2 item 2: 16 bytes on disk.
struct GlyphRecord {
    int32_t advance = 0;
    int32_t width = 0;        // the drawn quad's width (== advance in every shipped glyph)
    uint16_t unknown08 = 0;   // 0 in every shipped glyph, never read (Sec8 item 2)
    uint16_t unknown0a = 0;   // same
    int16_t firstKerning = -1; // index of this glyph's first kerning record, -1 = none
    uint16_t unknown0e = 0;   // 0, never read
};

// One parsed .vf3_pc file (Sec2). Field names follow the spec's own
// meaning column; offsets are noted on each.
struct Font {
    uint32_t magic = 0;          // +0x00, `TNFV` (0x56464e54) - NOT checked by the loader (Sec2.1)
    uint32_t version = 0;        // +0x04, must be 4
    uint32_t glyphCount = 0;     // +0x08, C
    uint32_t firstChar = 0;      // +0x0c, glyph index = UTF-16 unit - this
    int32_t defaultAdvance = 0;  // +0x10, advance of a character outside the font
    uint16_t lineHeight = 0;     // +0x14, what layout uses for one line
    uint16_t cellHeight = 0;     // +0x16, glyph quad/texture-rect height; a stored 0 is replaced by +0x14 (Sec3 step 3)
    uint16_t cellHeightStored = 0; // +0x16 as stored on disk (before the 0 -> +0x14 rule)
    uint32_t unknown18 = 0;      // +0x18, not read in this pass (OPEN, Sec8 item 2)
    int32_t advanceAdjust = 0;   // +0x1c, added to every glyph's advance (and the default advance)
    int32_t kerningCount = 0;    // +0x20, K
    int16_t originY = 0;         // +0x24, vertical glyph origin offset (font units)
    int16_t originX = 0;         // +0x26, horizontal glyph origin offset (font units)
    std::string pegName;         // +0x28 char[64], `<font>.peg` - not read by the loader
    std::string textureName;     // +0x68 char[64], `<font>_nobdr.tga` - the glyph texture (Sec3 step 4)
    std::vector<KerningPair> kerning; // K x 6 bytes at 0xd0
    std::vector<GlyphRecord> glyphs;  // C x 16 bytes
    std::vector<uint32_t> texelX;     // C x u32, glyph left edge in texels
    std::vector<uint32_t> texelY;     // C x u32, glyph top edge in texels
    size_t fileSize = 0;
    size_t trailingBytes = 0;    // bytes after the four tables (0 on all 6 shipped files, Sec1)

    // Glyph index of UTF-16 unit `ch`, or -1 when the character is outside
    // the font (index below 0 or at/after glyphCount) - the in-font test of
    // 0x00dcba50 (Sec2.1 +0x0c, Sec2.3).
    int32_t glyphIndex(uint32_t ch) const;
    bool contains(uint32_t ch) const { return glyphIndex(ch) >= 0; }
};

// Parses one .vf3_pc file (Sec2). Throws FontFormatError (see above).
Font parseFont(const uint8_t* data, size_t size);
inline Font parseFont(const std::vector<uint8_t>& bytes) { return parseFont(bytes.data(), bytes.size()); }

// Sec2.3 - the shipped kerning lookup, INCLUDING its quirk: it walks
// forward from the left glyph's first record while that record's left
// index is this glyph and its right index is below the next glyph's, then
// applies the adjustment of the record it stops on if THAT record's right
// index equals the next glyph's - without re-checking the stopped-on
// record's left index (so an exhausted glyph can pick up the following
// glyph's first record; *emulated* `'` + `T` in font_body = -2). Returns 0
// when the left glyph has no records (first index -1).
//
// HOST-SAFETY: Sec2.3 notes the walk "can also read one record past the end
// of the table when a glyph's records are the last ones; no shipped glyph is
// placed where this changes a result (HIGH)". Reading past the table here
// is treated as "no record" (adjustment 0) rather than reading foreign
// memory - consistent with that HIGH note for the shipped files.
int kerningAdjust(const Font& f, int32_t leftGlyph, int32_t rightGlyph);

// Resource-callback slot +0x1c (spec-vint-font-format.md Sec6): line
// height = trunc((+0x14 + derived line-height adjustment) x derived scale)
// = +0x14 for a plain font (the only kind implemented here).
int lineHeightMetric(const Font& f);

// Resource-callback slot +0x20 (Sec6): the advance of `ch` followed by
// `next` = glyph advance + +0x1c adjustment + kerning with `next` (Sec2.3;
// kerning only when `next` is not 0, not LF and is itself in the font).
// Returns nullopt for a character not in the font - the slot's FAILURE,
// which the Vint-side wrapper 0x00e2fdd0 turns into an advance of 0
// (advanceForLayout below). Control characters (below U+0020) are outside
// every shipped font (first char 32) and so also fail here.
std::optional<int> advanceMetric(const Font& f, uint32_t ch, uint32_t next);
// The Vint-side wrapper 0x00e2fdd0: slot failure -> 0.
inline int advanceForLayout(const Font& f, uint32_t ch, uint32_t next) {
    std::optional<int> a = advanceMetric(f, ch, next);
    return a ? *a : 0;
}

// ---- the game's named font table, DEFAULT arrangement (Sec4) ------------

// The game font ids the default arrangement loads, in load order (Sec4.2
// "anything else" row): font_body = 0, font_header_pc = 1, ug-debug = 2.
enum GameFontId : int32_t {
    kGameFontNone = -1,
    kGameFontBody = 0,
    kGameFontHeaderPc = 1,
    kGameFontUgDebug = 2,
};
constexpr int kGameFontCount = 3;
// The file stem each game id loads (".vf3_pc" appended by the loader, Sec3
// step 1).
const char* gameFontFileStem(int32_t gameFontId);

// One 12-byte named-table entry (Sec4.1), name kept as text (the real
// table stores the engine string hash of it).
struct NamedFontEntry {
    const char* name;
    int32_t primary;   // game font id (plain here)
    int32_t secondary; // game font id, or -1
};

// The six registered names of the default arrangement (Sec4.2's second
// table + the "anything else" row): debug -> ug-debug; thin, thin_overlay,
// bodyital_overlay -> font_body; header_n, header_n_overlay ->
// font_header_pc; every `_overlay` entry has secondary == primary (the
// "second copy" is literally the same id in this arrangement, Sec4.2).
const std::array<NamedFontEntry, 6>& defaultFontTable();

// Name lookup 0x00b9bb60 -> entry index or -1 (resource slot +0x10).
// CHOSEN: an exact, case-sensitive name compare. The real table compares
// engine string hashes (0x00d9e8b0) whose case behaviour is not in clean
// spec text; every font name found in the shipped documents is one of the
// six registered lower-case names (Sec4.2), so the choice does not change
// any shipped lookup.
int32_t findNamedFont(const std::string& name);

// Measuring (0x00b9b720): the PRIMARY id only; an out-of-range entry index
// gives game font id 0 - not entry 0's font (Sec4.1).
int32_t measureGameFont(int32_t entryIndex);
// Drawing (0x00b9cf20): the ordered list of game font ids to draw a run
// with - the SECONDARY first if not -1, then the PRIMARY if not -1 and
// different from the secondary; an out-of-range entry index falls back to
// ENTRY 0 (Sec4.1). General mechanism, not special-cased: in the default
// arrangement every `_overlay` name yields one id (secondary == primary).
std::vector<int32_t> drawGameFonts(int32_t entryIndex);

// Resource slot +0x24, the "break anywhere" flag (Sec6): 0 if the (stale,
// Sec5 quirk) entry value equals the debug font's game id, else 1 exactly
// when the language code is 3. This host has no language code and only the
// default arrangement (language code not 3), so the flag is always false.
// Kept as a function so the rule stays visible at its one call site.
bool breakAnywhereFlagDefaultArrangement();

// ---- per-glyph draw geometry (spec-vint-render.md Sec14.1) --------------

// One glyph of a run, in GLYPH SPACE (before G = M.S(sx,sy) and the pen
// origin p are applied), for a plain font (s = 1, dy = 0).
struct GlyphQuad {
    uint32_t ch = 0;
    enum class Kind { InFont, Control, NotInFont, AfterNul } kind = Kind::InFont;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0; // quad corners (glyph space)
    double u0 = 0, v0 = 0, u1 = 0, v1 = 0; // UVs (0..1); 0 for every non-InFont kind
    double penBefore = 0;                  // pen x when this glyph was placed
    // A zero-area quad draws nothing visible (control characters, characters
    // after a NUL, and the zero-width quad of a character the font lacks).
    bool visible() const { return kind == Kind::InFont && x1 > x0 && y1 > y0; }
};

// The glyph-run drawer 0x00b9bc10 for ONE font layer (Sec14.1), plain font:
//   * pen starts at 0 and moves along glyph-space x only;
//   * a character in the font: quad top-left (pen + originX, originY), size
//     (glyph width, cell height); UV u0 = (tx + 0.5)/T.w, u1 = (tx + w)/T.w,
//     v0 = (ty + 0.5)/T.h, v1 = (ty + cellHeight)/T.h - the half-texel inset
//     on the NEAR (top-left) edge only; pen += advance metric (kerning with
//     the next character of THIS run, i.e. only while i+1 < count - past the
//     run's count the drawer reads the record's padding NUL or the next
//     record's opcode dword, values 0..4, all below U+0020 and so never in
//     the font: no kerning) + `spacing` (the signed spacing byte, not scaled);
//   * below U+0020 (tab/CR/LF included): zero-size quad at the pen, no
//     advance;
//   * a NUL ends the run (remaining glyphs = zero-size quads at the last pen);
//   * a character the font lacks: zero-width, full-height quad at the pen,
//     pen += default advance (+0x10 + +0x1c) + spacing.
// texW/texH = the glyph texture's size in texels.
std::vector<GlyphQuad> layoutGlyphRun(const Font& f, const std::u16string& text, size_t count, int spacing,
                                      double texW, double texH);

} // namespace sr3font
