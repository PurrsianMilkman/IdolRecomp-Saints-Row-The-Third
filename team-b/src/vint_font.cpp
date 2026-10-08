// sr3font - .vf3_pc reader, default named font table, metric callbacks and
// per-glyph geometry. See include/sr3font/vint_font.h for the sources and
// the scope note (derived fonts and non-default language arrangements are
// deliberately out).
#include "sr3font/vint_font.h"

#include <cstring>

namespace sr3font {

namespace {

constexpr size_t kHeaderSize = 0xd0;

uint16_t rdU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rdU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
int32_t rdS32(const uint8_t* p) { return static_cast<int32_t>(rdU32(p)); }
int16_t rdS16(const uint8_t* p) { return static_cast<int16_t>(rdU16(p)); }

std::string rdName(const uint8_t* p, size_t cap) {
    size_t n = 0;
    while (n < cap && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

constexpr uint32_t kLF = 0x0a;

} // namespace

int32_t Font::glyphIndex(uint32_t ch) const {
    // Sec2.1 +0x0c: glyph index = UTF-16 unit - first char; in the font only
    // for 0 <= index < C (0x00dcba50's bound check).
    const int64_t idx = static_cast<int64_t>(ch) - static_cast<int64_t>(firstChar);
    if (idx < 0 || idx >= static_cast<int64_t>(glyphCount)) return -1;
    return static_cast<int32_t>(idx);
}

Font parseFont(const uint8_t* data, size_t size) {
    if (size < kHeaderSize) {
        throw FontFormatError(".vf3_pc: " + std::to_string(size) + " bytes is shorter than the 0xd0-byte header");
    }
    Font f;
    f.fileSize = size;
    f.magic = rdU32(data + 0x00);
    f.version = rdU32(data + 0x04);
    // Sec2.1/Sec3 step 2: the loader's only header check - version must be 4.
    if (f.version != 4) throw FontFormatError(".vf3_pc: version " + std::to_string(f.version) + " (the loader requires 4)");
    f.glyphCount = rdU32(data + 0x08);
    f.firstChar = rdU32(data + 0x0c);
    f.defaultAdvance = rdS32(data + 0x10);
    f.lineHeight = rdU16(data + 0x14);
    f.cellHeightStored = rdU16(data + 0x16);
    f.cellHeight = f.cellHeightStored == 0 ? f.lineHeight : f.cellHeightStored; // Sec3 step 3
    f.unknown18 = rdU32(data + 0x18);
    f.advanceAdjust = rdS32(data + 0x1c);
    f.kerningCount = rdS32(data + 0x20);
    f.originY = rdS16(data + 0x24);
    f.originX = rdS16(data + 0x26);
    f.pegName = rdName(data + 0x28, 64);
    f.textureName = rdName(data + 0x68, 64);

    // Sec2.2: the four tables back to back from 0xd0.
    const uint64_t k = f.kerningCount < 0 ? 0 : static_cast<uint64_t>(f.kerningCount);
    if (f.kerningCount < 0) throw FontFormatError(".vf3_pc: negative kerning pair count");
    const uint64_t c = f.glyphCount;
    const uint64_t need = kHeaderSize + 6 * k + 24 * c;
    if (need > size) {
        throw FontFormatError(".vf3_pc: tables need " + std::to_string(need) + " bytes, file has " + std::to_string(size));
    }
    f.trailingBytes = static_cast<size_t>(size - need);
    const uint8_t* p = data + kHeaderSize;
    f.kerning.resize(static_cast<size_t>(k));
    for (auto& kp : f.kerning) {
        kp.left = rdU16(p);
        kp.right = rdU16(p + 2);
        kp.adjust = static_cast<int8_t>(p[4]);
        kp.pad = p[5];
        p += 6;
    }
    f.glyphs.resize(static_cast<size_t>(c));
    for (auto& g : f.glyphs) {
        g.advance = rdS32(p);
        g.width = rdS32(p + 4);
        g.unknown08 = rdU16(p + 8);
        g.unknown0a = rdU16(p + 10);
        g.firstKerning = rdS16(p + 12);
        g.unknown0e = rdU16(p + 14);
        p += 16;
    }
    f.texelX.resize(static_cast<size_t>(c));
    for (auto& x : f.texelX) { x = rdU32(p); p += 4; }
    f.texelY.resize(static_cast<size_t>(c));
    for (auto& y : f.texelY) { y = rdU32(p); p += 4; }
    return f;
}

int kerningAdjust(const Font& f, int32_t leftGlyph, int32_t rightGlyph) {
    if (leftGlyph < 0 || leftGlyph >= static_cast<int32_t>(f.glyphs.size())) return 0;
    const int32_t first = f.glyphs[static_cast<size_t>(leftGlyph)].firstKerning;
    if (first < 0) return 0;
    const size_t n = f.kerning.size();
    size_t i = static_cast<size_t>(first);
    // Walk while the record is this glyph's and its right index is below the
    // next glyph's (Sec2.3).
    while (i < n && f.kerning[i].left == static_cast<uint32_t>(leftGlyph) &&
           f.kerning[i].right < static_cast<uint32_t>(rightGlyph)) {
        ++i;
    }
    if (i >= n) return 0; // HOST-SAFETY past-the-end read -> no record (see header)
    // The shipped quirk: the stopped-on record's LEFT index is not re-checked.
    if (f.kerning[i].right == static_cast<uint32_t>(rightGlyph)) return f.kerning[i].adjust;
    return 0;
}

int lineHeightMetric(const Font& f) { return static_cast<int>(f.lineHeight); }

std::optional<int> advanceMetric(const Font& f, uint32_t ch, uint32_t next) {
    const int32_t gi = f.glyphIndex(ch);
    if (gi < 0) return std::nullopt; // slot +0x20 failure (Sec6)
    int adv = f.glyphs[static_cast<size_t>(gi)].advance + f.advanceAdjust;
    // Sec2.3: kerning only when the next character is not 0, not LF, and in
    // the font.
    if (next != 0 && next != kLF) {
        const int32_t ni = f.glyphIndex(next);
        if (ni >= 0) adv += kerningAdjust(f, gi, ni);
    }
    return adv; // x derived scale 1, truncation a no-op for a plain font
}

const char* gameFontFileStem(int32_t gameFontId) {
    switch (gameFontId) {
        case kGameFontBody: return "font_body";
        case kGameFontHeaderPc: return "font_header_pc";
        case kGameFontUgDebug: return "ug-debug";
        default: return nullptr;
    }
}

const std::array<NamedFontEntry, 6>& defaultFontTable() {
    // Sec4.2: entry order and columns; body font = font_body, header font =
    // font_header_pc, body-italic font = font_body (default arrangement).
    static const std::array<NamedFontEntry, 6> table = {{
        {"debug", kGameFontUgDebug, kGameFontNone},
        {"thin", kGameFontBody, kGameFontNone},
        {"thin_overlay", kGameFontBody, kGameFontBody},
        {"header_n", kGameFontHeaderPc, kGameFontNone},
        {"header_n_overlay", kGameFontHeaderPc, kGameFontHeaderPc},
        {"bodyital_overlay", kGameFontBody, kGameFontBody},
    }};
    return table;
}

int32_t findNamedFont(const std::string& name) {
    const auto& t = defaultFontTable();
    for (size_t i = 0; i < t.size(); ++i)
        if (name == t[i].name) return static_cast<int32_t>(i);
    return -1;
}

int32_t measureGameFont(int32_t entryIndex) {
    const auto& t = defaultFontTable();
    if (entryIndex < 0 || entryIndex >= static_cast<int32_t>(t.size())) return 0; // game font id 0 (Sec4.1)
    return t[static_cast<size_t>(entryIndex)].primary;
}

std::vector<int32_t> drawGameFonts(int32_t entryIndex) {
    const auto& t = defaultFontTable();
    if (entryIndex < 0 || entryIndex >= static_cast<int32_t>(t.size())) entryIndex = 0; // entry 0 (Sec4.1)
    const NamedFontEntry& e = t[static_cast<size_t>(entryIndex)];
    std::vector<int32_t> out;
    if (e.secondary != kGameFontNone) out.push_back(e.secondary);
    if (e.primary != kGameFontNone && e.primary != e.secondary) out.push_back(e.primary);
    return out;
}

bool breakAnywhereFlagDefaultArrangement() { return false; }

std::vector<GlyphQuad> layoutGlyphRun(const Font& f, const std::u16string& text, size_t count, int spacing,
                                      double texW, double texH) {
    std::vector<GlyphQuad> out;
    out.reserve(count);
    double pen = 0;
    bool ended = false;
    const double cell = f.cellHeight;
    for (size_t i = 0; i < count; ++i) {
        GlyphQuad q;
        q.ch = i < text.size() ? text[i] : 0;
        q.penBefore = pen;
        if (ended || q.ch == 0) {
            // A NUL ends the run; the remaining reserved glyphs are
            // zero-size quads at the last pen position (Sec14.1).
            ended = true;
            q.kind = GlyphQuad::Kind::AfterNul;
            q.x0 = q.x1 = pen;
            q.y0 = q.y1 = 0;
            out.push_back(q);
            continue;
        }
        if (q.ch < 0x20) {
            // Below U+0020: zero-size quad at the pen, no advance.
            q.kind = GlyphQuad::Kind::Control;
            q.x0 = q.x1 = pen;
            q.y0 = q.y1 = 0;
            out.push_back(q);
            continue;
        }
        const int32_t gi = f.glyphIndex(q.ch);
        if (gi < 0) {
            // A character the font lacks: zero-width, full-height quad and the
            // font's DEFAULT advance (+0x10 + +0x1c, x s) - the measure/draw
            // mismatch of spec-vint-font-format.md Sec6. CHOSEN: the spacing
            // byte is added here too (Sec12.4 says it is added to "each
            // glyph's advance"; the emulated example in Sec6 has spacing 0, so
            // this case is not separately confirmed).
            q.kind = GlyphQuad::Kind::NotInFont;
            q.x0 = q.x1 = pen + f.originX;
            q.y0 = f.originY;
            q.y1 = f.originY + cell;
            out.push_back(q);
            pen += (f.defaultAdvance + f.advanceAdjust) + spacing;
            continue;
        }
        const GlyphRecord& g = f.glyphs[static_cast<size_t>(gi)];
        q.kind = GlyphQuad::Kind::InFont;
        const double w = g.width; // trunc(width x s), s = 1
        q.x0 = pen + f.originX;
        q.y0 = f.originY;
        q.x1 = q.x0 + w;
        q.y1 = q.y0 + cell;
        const double tx = f.texelX[static_cast<size_t>(gi)], ty = f.texelY[static_cast<size_t>(gi)];
        // Near-edge-only half-texel inset (Sec14.1); far edge exact.
        q.u0 = (tx + 0.5) / texW;
        q.u1 = (tx + w) / texW;
        q.v0 = (ty + 0.5) / texH;
        q.v1 = (ty + cell) / texH;
        out.push_back(q);
        // Kerning with the next character of this run only (see header).
        const uint32_t next = (i + 1 < count && i + 1 < text.size()) ? text[i + 1] : 0;
        pen += advanceForLayout(f, q.ch, next) + spacing;
    }
    return out;
}

} // namespace sr3font
