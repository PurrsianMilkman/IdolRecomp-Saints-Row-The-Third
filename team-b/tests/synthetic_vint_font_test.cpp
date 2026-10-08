// Synthetic test for sr3font (2026-10-03, UI render unit U5 - text). No game
// data: a .vf3_pc is built byte by byte from spec-vint-font-format.md Sec2,
// and layout runs on fixed synthetic metrics, so each rule of spec-vint-
// render.md Sec14.2-Sec14.5 is pinned in isolation. The real-font numbers
// (the spec's emulated vectors) are in tests/real_vint_font_test.cpp.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3font/vint_font.h"
#include "sr3font/vint_text.h"

using namespace sr3font;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

template <typename T>
void put(std::vector<uint8_t>& b, size_t off, T v) {
    for (size_t i = 0; i < sizeof(T); ++i) b[off + i] = static_cast<uint8_t>((static_cast<uint64_t>(v) >> (8 * i)) & 0xff);
}

struct SynGlyph {
    int32_t adv;
    int16_t firstKern;
    uint32_t tx, ty;
};
struct SynKern {
    uint16_t l, r;
    int8_t adj;
};

std::vector<uint8_t> buildFont(const std::vector<SynGlyph>& g, const std::vector<SynKern>& k, uint32_t version = 4,
                               uint16_t lineH = 20, uint16_t cellH = 0) {
    std::vector<uint8_t> b(0xd0 + 6 * k.size() + 24 * g.size(), 0);
    std::memcpy(b.data(), "TNFV", 4);
    put<uint32_t>(b, 0x04, version);
    put<uint32_t>(b, 0x08, static_cast<uint32_t>(g.size()));
    put<uint32_t>(b, 0x0c, 32);
    put<int32_t>(b, 0x10, 7);   // default advance
    put<uint16_t>(b, 0x14, lineH);
    put<uint16_t>(b, 0x16, cellH);
    put<int32_t>(b, 0x1c, -1);  // advance adjustment
    put<int32_t>(b, 0x20, static_cast<int32_t>(k.size()));
    put<int16_t>(b, 0x24, -2);  // origin y
    put<int16_t>(b, 0x26, 1);   // origin x
    std::memcpy(b.data() + 0x28, "syn.peg", 7);
    std::memcpy(b.data() + 0x68, "syn_nobdr.tga", 13);
    size_t p = 0xd0;
    for (const auto& kp : k) {
        put<uint16_t>(b, p, kp.l);
        put<uint16_t>(b, p + 2, kp.r);
        b[p + 4] = static_cast<uint8_t>(kp.adj);
        b[p + 5] = 0xCD;
        p += 6;
    }
    for (const auto& gl : g) {
        put<int32_t>(b, p, gl.adv);
        put<int32_t>(b, p + 4, gl.adv);
        put<int16_t>(b, p + 12, gl.firstKern);
        p += 16;
    }
    for (const auto& gl : g) { put<uint32_t>(b, p, gl.tx); p += 4; }
    for (const auto& gl : g) { put<uint32_t>(b, p, gl.ty); p += 4; }
    return b;
}

std::u16string u(const char* s) {
    std::u16string o;
    for (; *s; ++s) o.push_back(static_cast<char16_t>(static_cast<unsigned char>(*s)));
    return o;
}

// Fixed metrics: every printable ASCII char is 10 wide, line height 20.
LayoutMetrics fixedMetrics(bool anywhere = false) {
    LayoutMetrics m;
    m.lineHeight = 20;
    m.advance = [](uint32_t ch, uint32_t) { return (ch >= 0x20 && ch < 0x7f) ? 10 : 0; };
    m.breakAnywhere = anywhere;
    return m;
}

LayoutResult lay(const std::u16string& s, LayoutParams p = LayoutParams(), bool anywhere = false,
                 InlineImageLookup img = InlineImageLookup()) {
    LayoutMetrics m = fixedMetrics(anywhere);
    return layoutText(s, p, m, img);
}

std::vector<std::u16string> lineTexts(const LayoutResult& r) {
    std::vector<std::u16string> lines(r.lines.size());
    for (const auto& rec : r.records)
        if (rec.op == StreamRecord::Op::Run && rec.line >= 0) lines[static_cast<size_t>(rec.line)] += rec.text;
    return lines;
}

} // namespace

int main() {
    // ---- reader -------------------------------------------------------------
    // Glyphs for ' ' (0) .. 'D' (36): only a few matter.
    std::vector<SynGlyph> g(37, SynGlyph{10, -1, 0, 0});
    // Kerning: glyph 33 ('A') has pairs with 34 ('B', -2) and 35 ('C', +1);
    // glyph 34 ('B') has one pair with 36 ('D', -4).
    g[33].firstKern = 0;
    g[34].firstKern = 2;
    g[33].tx = 100;
    g[33].ty = 50;
    std::vector<SynKern> k = {{33, 34, -2}, {33, 35, 1}, {34, 36, -4}};
    std::vector<uint8_t> bytes = buildFont(g, k);
    Font f = parseFont(bytes);
    CHECK(f.version == 4 && f.glyphCount == 37 && f.kerningCount == 3 && f.firstChar == 32);
    CHECK(f.cellHeightStored == 0 && f.cellHeight == 20); // a stored 0 is replaced by +0x14
    CHECK(f.textureName == "syn_nobdr.tga" && f.pegName == "syn.peg" && f.trailingBytes == 0);
    CHECK(f.kerning[0].pad == 0xCD && f.kerning[2].adjust == -4);
    CHECK(f.glyphIndex(' ') == 0 && f.glyphIndex('D') == 36 && f.glyphIndex('E') == -1 && f.glyphIndex(0x1f) == -1);

    // Errors: short file, wrong version, tables not fitting.
    bool threw = false;
    try { parseFont(bytes.data(), 0xcf); } catch (const FontFormatError&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { parseFont(buildFont(g, k, 3)); } catch (const FontFormatError&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { parseFont(bytes.data(), bytes.size() - 1); } catch (const FontFormatError&) { threw = true; }
    CHECK(threw);
    // Longer than the tables: parsed, trailing bytes reported.
    {
        std::vector<uint8_t> longer = bytes;
        longer.push_back(0);
        CHECK(parseFont(longer).trailingBytes == 1);
    }

    // ---- kerning lookup incl. the Sec2.3 quirk ------------------------------
    CHECK(kerningAdjust(f, 33, 34) == -2);
    CHECK(kerningAdjust(f, 33, 35) == 1);
    // 'A' before 'D' (36): A's records (right 34, 35) are both below 36, so
    // the walk stops on B's first record (34, 36, -4) and applies it on its
    // right index alone - the shipped quirk.
    CHECK(kerningAdjust(f, 33, 36) == -4);
    CHECK(kerningAdjust(f, 0, 34) == 0);  // no records
    CHECK(kerningAdjust(f, 34, 36) == -4);
    CHECK(kerningAdjust(f, 34, 40) == 0); // walk runs past the table end -> no record (host-safety)
    // advance metric: adv 10 + adj -1 + kerning; not when next is 0, LF, or outside the font.
    CHECK(advanceForLayout(f, 'A', 'B') == 7);
    CHECK(advanceForLayout(f, 'A', 0) == 9);
    CHECK(advanceForLayout(f, 'A', '\n') == 9);
    CHECK(advanceForLayout(f, 'A', 0x4E00) == 9);
    CHECK(!advanceMetric(f, 0x4E00, 0).has_value() && advanceForLayout(f, 0x4E00, 0) == 0);
    CHECK(!advanceMetric(f, '\t', 0).has_value());
    CHECK(lineHeightMetric(f) == 20);

    // ---- glyph run geometry (Sec14.1) ---------------------------------------
    {
        std::u16string s = u"A\tB";
        s.push_back(static_cast<char16_t>(0x4E00));
        s += u"A";
        auto q = layoutGlyphRun(f, s, s.size(), 2, 256, 128);
        CHECK(q.size() == 5);
        // 'A' at pen 0: origin offsets (+1, -2), size (10, 20); UV near-edge inset.
        CHECK(q[0].x0 == 1 && q[0].y0 == -2 && q[0].x1 == 11 && q[0].y1 == 18);
        CHECK(q[0].u0 == 100.5 / 256 && q[0].u1 == 110.0 / 256 && q[0].v0 == 50.5 / 128 && q[0].v1 == 70.0 / 128);
        // 'A' advance: kerning only with the next char of THIS run - next is TAB -> none: 10-1+2 = 11
        CHECK(q[1].kind == GlyphQuad::Kind::Control && q[1].penBefore == 11 && !q[1].visible());
        // TAB: no advance
        CHECK(q[2].penBefore == 11);
        // 'B' before U+4E00 (not in font): no kerning -> 9 + 2
        CHECK(q[3].kind == GlyphQuad::Kind::NotInFont && q[3].penBefore == 22 && !q[3].visible());
        // not in font: default advance 7 + adj -1 + spacing 2 = 8
        CHECK(q[4].penBefore == 30 && q[4].visible());
    }

    // ---- named font table, default arrangement (Sec4) -----------------------
    CHECK(findNamedFont("debug") == 0 && findNamedFont("thin") == 1 && findNamedFont("thin_overlay") == 2 &&
          findNamedFont("header_n") == 3 && findNamedFont("header_n_overlay") == 4 &&
          findNamedFont("bodyital_overlay") == 5 && findNamedFont("nope") == -1);
    CHECK(measureGameFont(findNamedFont("debug")) == kGameFontUgDebug);
    CHECK(measureGameFont(findNamedFont("header_n_overlay")) == kGameFontHeaderPc);
    CHECK(measureGameFont(99) == 0 && measureGameFont(-1) == 0); // out of range -> game id 0
    CHECK(drawGameFonts(findNamedFont("thin")) == std::vector<int32_t>{kGameFontBody});
    CHECK(drawGameFonts(findNamedFont("thin_overlay")) == std::vector<int32_t>{kGameFontBody}); // drawn once
    CHECK(drawGameFonts(99) == std::vector<int32_t>{kGameFontUgDebug}); // out of range -> entry 0 (debug)
    CHECK(std::string(gameFontFileStem(kGameFontHeaderPc)) == "font_header_pc");
    CHECK(!breakAnywhereFlagDefaultArrangement());

    // ---- layout rules (fixed 10-wide metrics) ---------------------------------
    {
        // `\n`/`\N` sequences break; a lone backslash is ordinary text.
        auto l = lineTexts(lay(u"a\\nb\\Nc\\d"));
        CHECK(l.size() == 3 && l[0] == u"a" && l[1] == u"b" && l[2] == u"c\\d");
        // trailing line break adds no line; leading LF gets a space line.
        LayoutResult r = lay(u"ab\n");
        CHECK(r.lines.size() == 1 && r.height == 20);
        l = lineTexts(lay(u"\nab"));
        CHECK(l.size() == 2 && l[0] == u" " && l[1] == u"ab");
        // `[[` is one literal `[`; a lone `]` is text; `[ [` is not a tag.
        l = lineTexts(lay(u"a[[b]c"));
        CHECK(l.size() == 1 && l[0] == u"a[b]c");
        // CR / tab are ordinary (measure 0 here)
        l = lineTexts(lay(u"a\rb"));
        CHECK(l.size() == 1 && l[0] == u"a\rb");
    }
    {
        LayoutParams p;
        p.wrapWidth = 45;
        // break at the space (dropped); spaces before it stay on the old line.
        auto l = lineTexts(lay(u"ab  cdef", p));
        CHECK(l.size() == 2 && l[0] == u"ab " && l[1] == u"cdef");
        LayoutResult r = lay(u"ab  cdef", p);
        CHECK(r.lines[0].width == 30); // "ab " = 30 (the dropped space is not counted)
        // hyphen: break after it (it stays on the first line)
        p.wrapWidth = 35;
        l = lineTexts(lay(u"ab-cd", p));
        CHECK(l.size() == 2 && l[0] == u"ab-" && l[1] == u"cd");
        // leading spaces are kept: only the one break-point space is dropped
        p.wrapWidth = 45;
        l = lineTexts(lay(u"abcd  ef", p));
        CHECK(l.size() == 2 && l[0] == u"abcd" && l[1] == u" ef");
        // exact fit stays (pen + advance == wrap)
        p.wrapWidth = 40;
        CHECK(lineTexts(lay(u"abcd", p)).size() == 1);
        // a glyph wider than the wrap still gets placed, one per line
        p.wrapWidth = 5;
        l = lineTexts(lay(u"abc", p));
        CHECK(l.size() == 3 && l[0] == u"a" && l[2] == u"c");
        // break-anywhere fonts: every char is an opportunity, spaces kept
        p.wrapWidth = 25;
        l = lineTexts(lay(u"a bcd", p, true));
        CHECK(l.size() == 3 && l[0] == u"a " && l[1] == u"bc" && l[2] == u"d");
    }
    {
        // Kerning byte: line width drops the trailing kerning x sx.
        LayoutParams p;
        p.kerning = -2;
        LayoutResult r = lay(u"abc", p);
        CHECK(r.width == 3 * 8 + 2);
        auto runsOnly = [](const LayoutResult& lr) {
            size_t n = 0;
            for (const auto& rec : lr.records) n += rec.op == StreamRecord::Op::Run;
            return n;
        };
        CHECK(runsOnly(r) == 1 && r.records[0].spacing == -2);
    }
    // ---- markup -------------------------------------------------------------
    {
        // #FFF: fewer than three pairs -> ignored entirely (no record).
        LayoutResult r = lay(u"[color:#FFF]a");
        CHECK(r.records.size() == 1 && r.records[0].op == StreamRecord::Op::Run);
        // #RRGGBB keeps the current alpha (-1), #RRGGBBAA sets it, nested
        // format restores with a colour record; colour without format
        // changes entry 0 and a stray [/format] re-emits it.
        r = lay(u"[color:#102030]a[format][color:#40506080]b[/format]c[/format]d");
        std::vector<const StreamRecord*> cs;
        for (const auto& rec : r.records) if (rec.op == StreamRecord::Op::Colour) cs.push_back(&rec);
        CHECK(cs.size() == 4);
        if (cs.size() == 4) {
            CHECK(cs[0]->r == static_cast<float>(0x10 / 255.0) && cs[0]->a == -1);
            CHECK(cs[1]->g == static_cast<float>(0x50 / 255.0) && cs[1]->a == static_cast<float>(0x80 / 255.0));
            CHECK(cs[2]->r == cs[0]->r && cs[2]->a == -1); // pop -> entry 0's colour
            CHECK(cs[3]->r == cs[0]->r);                   // stray pop re-emits the same
        }
        // names: case-insensitive, prefix, unknown ignored
        CHECK(findNamedColour("FULL_BRIGHT") && std::string(findNamedColour("FULL_BRIGHT")->name) == "full_bright");
        CHECK(findNamedColour("Purple") && findNamedColour("Purple")->r == 0x94);
        CHECK(!findNamedColour("greenish"));
        CHECK(namedColours().size() == 26);
        r = lay(u"[color:nosuch]a");
        CHECK(r.records.size() == 1);
        // tag names: case-insensitive prefix, optional leading spaces
        r = lay(u"[  FORMATTING][Color:RED]a[/Format]");
        size_t colourRecs = 0;
        for (const auto& rec : r.records) colourRecs += rec.op == StreamRecord::Op::Colour;
        CHECK(colourRecs == 2 && r.unrecognizedTags == 0);
        // [format] depth cap (CHOSEN): 20 pushes then 20 pops never corrupt.
        std::u16string deep;
        for (int i = 0; i < 20; ++i) deep += u"[format]";
        deep += u"[scale:3]x";
        for (int i = 0; i < 20; ++i) deep += u"[/format]";
        deep += u"y";
        r = lay(deep);
        CHECK(r.formatDepthCapped == 5);
        const StreamRecord* lastRun = nullptr;
        for (const auto& rec : r.records) if (rec.op == StreamRecord::Op::Run) lastRun = &rec;
        CHECK(lastRun && lastRun->text == u"y" && lastRun->sx == 1);
        // [scale:] with no digits: unchanged (CHOSEN)
        r = lay(u"[scale:]a");
        CHECK(r.records.size() == 1 && r.records[0].sx == 1);
    }
    {
        // inline images: placed at the pen, pen advances by w x sx, height h x sy;
        // overrun breaks the line first when the line is not empty; unknown
        // names still emit a record (size 0, no advance).
        InlineImageLookup img = [](const std::string& n) {
            InlineImageInfo i;
            if (n == "btn") { i.found = true; i.width = 30; i.height = 40; }
            return i;
        };
        LayoutParams p;
        LayoutResult r = lay(u"a[image:btn]b[image:nope]c", p, false, img);
        std::vector<const StreamRecord*> imgs;
        for (const auto& rec : r.records) if (rec.op == StreamRecord::Op::Image) imgs.push_back(&rec);
        CHECK(imgs.size() == 2);
        if (imgs.size() == 2) {
            CHECK(imgs[0]->x == 10 && imgs[0]->y == 0 && imgs[0]->height == 40 && imgs[0]->imageName == "btn");
            CHECK(!imgs[1]->image.found && imgs[1]->x == 50 && imgs[1]->height == 0);
        }
        CHECK(r.width == 60 && r.height == 40);
        // the text runs are centred on the 40-high line: y = (40 - 20) / 2
        for (const auto& rec : r.records) if (rec.op == StreamRecord::Op::Run) CHECK(rec.y == 10);
        p.wrapWidth = 35;
        r = lay(u"ab[image:btn]", p, false, img);
        CHECK(r.lines.size() == 2);
        for (const auto& rec : r.records) if (rec.op == StreamRecord::Op::Image) CHECK(rec.line == 1 && rec.x == 0);
        p.textScaleX = p.textScaleY = 2;
        p.wrapWidth = 0;
        r = lay(u"[image:btn]", p, false, img);
        CHECK(r.width == 60 && r.height == 80);
    }
    {
        // unrecognized tag: shown, followed by a line break
        auto l = lineTexts(lay(u"x[ bad ]y"));
        CHECK(l.size() == 2 && l[0] == u"x[unrecognized: ' bad ']" && l[1] == u"y");
        // unterminated `[` is laid out as text (CHOSEN)
        l = lineTexts(lay(u"a[b"));
        CHECK(l.size() == 1 && l[0] == u"a[b");
    }
    {
        // stream bytes: run 4+28+2*even(count); colour 20; image 24; +4 terminator
        LayoutResult r = lay(u"abc[color:red]d");
        CHECK(r.streamBytes == (4 + 28 + 8) + 20 + (4 + 28 + 4) + 4);
        // truncation at 12,800 bytes (CHOSEN clean truncate)
        std::u16string big;
        for (int i = 0; i < 2000; ++i) big += u"[color:red]";
        r = lay(big);
        CHECK(r.truncated && r.streamBytes <= 0x3200);
    }

    // ---- insertion formatter (Sec14.2) --------------------------------------
    {
        CHECK(formatInsertions(u"{{x}%%%", false, {}).text == u"{x}%");
        FormatResult r = formatInsertions(u"a{}b%sc{ 2 }d", false, {});
        CHECK(r.text == u"abcd" && r.insertionsVanished == 3 && r.open.empty());
        std::vector<InsertValue> v(2);
        v[0].kind = InsertValue::Kind::String;
        v[0].text = u"raw";
        // nil value (value 1, and anything past the list up to 32) -> literal
        r = formatInsertions(u"[{0:Not_Localized}|{1}|{31}|{32}]", true, v);
        CHECK(r.text == u"[raw|{No insertion value specified!}|{No insertion value specified!}|]" && r.open.empty());
        // %s with nil inserts nothing
        CHECK(formatInsertions(u"<%s>", true, {}).text == u"<>");
        // text_tag_crc with a non-integer value
        r = formatInsertions(u"{0:text_tag_crc}", true, v);
        CHECK(r.text == u"{Invalid insertion value for a hash!}");
        // OPEN: a localisation-key lookup / named symbol / number conversion
        CHECK(!formatInsertions(u"{0}", true, v).open.empty());
        CHECK(!formatInsertions(u"{PLAYER}", false, {}).open.empty());
        std::vector<InsertValue> num(1);
        num[0].kind = InsertValue::Kind::Number;
        num[0].number = 5;
        CHECK(!formatInsertions(u"{0}", true, num).open.empty());
        CHECK(!formatInsertions(u"{0:text_tag_crc}", true, num).open.empty());
    }
    // ---- raw text_tag decode -----------------------------------------------
    {
        std::string why;
        auto d = decodeRawTextTag("$3,000", why);
        CHECK(d && *d == u"$3,000");
        std::string tagged = "\x80" "abc";
        CHECK(!decodeRawTextTag(tagged, why) && !why.empty());
        auto hi = decodeRawTextTag("\xe9", why);
        CHECK(hi && (*hi)[0] == 0xe9); // widened byte-for-byte
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_vint_font_test: all checks passed\n";
    return 0;
}
