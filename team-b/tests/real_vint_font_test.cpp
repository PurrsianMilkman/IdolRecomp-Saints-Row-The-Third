// Real-data test for sr3font (2026-10-03, UI render unit U5 - text). Reads
// the six shipped .vf3_pc fonts out of misc.vpp_pc in CRREISH_GAME_CACHE_DIR
// (a CMake cache variable, default the owner's install path) and exits 77 -
// reported by ctest as SKIPPED, never as a pass - when the archive is not
// there. Nothing from the archive is copied into the repository.
//
// Every expected number below is quoted from clean spec text, and most are
// the spec's own *emulated* results (the real shipped functions run on the
// real files) - so this checks this project's independent implementation
// against the real code's output, not against itself:
//   * spec-vint-font-format.md Sec1 (bytes / glyphs / kerning pairs /
//     texture name per file), Sec2.1 (per-file header values), Sec2.2/2.3
//     (pad bytes, width == advance, out-of-range right indices, sort
//     order), Sec6 (measure/draw mismatch), Sec7 (worked numbers);
//   * spec-vint-render.md Sec14.1 (pen with spacing), Sec14.2 (insertion
//     vectors), Sec14.3 (the emulated reference-vector table, the wrap
//     vectors, trailing-space centring, vertical centring, stream bytes),
//     Sec14.4/14.5 (markup colour records, `gre` prefix, unrecognized tag,
//     absolute [scale:]).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "sr3font/vint_font.h"
#include "sr3font/vint_text.h"
#include "sr3locstr/le_strings.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

using namespace sr3font;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }

bool readEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name != name) continue;
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            vpp::ByteView r = c.rawEntryBytes(i);
            out.assign(r.data(), r.data() + r.size());
            return true;
        }
        vpp::DecompressResult r = c.decompressEntry(i);
        if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
            r.status != vpp::DecodeStatus::ContentValidated)
            return false;
        out = std::move(r.data);
        return true;
    }
    return false;
}

std::u16string u(const char* s) {
    std::u16string o;
    for (; *s; ++s) o.push_back(static_cast<char16_t>(static_cast<unsigned char>(*s)));
    return o;
}

LayoutMetrics metricsFor(const Font& f) {
    LayoutMetrics m;
    m.lineHeight = lineHeightMetric(f);
    m.advance = [&f](uint32_t ch, uint32_t next) { return advanceForLayout(f, ch, next); };
    m.breakAnywhere = false;
    return m;
}

LayoutResult lay(const Font& f, const std::u16string& text, LayoutParams p = LayoutParams()) {
    LayoutMetrics m = metricsFor(f);
    return layoutText(text, p, m, InlineImageLookup());
}

std::vector<const StreamRecord*> runs(const LayoutResult& r) {
    std::vector<const StreamRecord*> out;
    for (const auto& rec : r.records)
        if (rec.op == StreamRecord::Op::Run) out.push_back(&rec);
    return out;
}
std::vector<const StreamRecord*> colours(const LayoutResult& r) {
    std::vector<const StreamRecord*> out;
    for (const auto& rec : r.records)
        if (rec.op == StreamRecord::Op::Colour) out.push_back(&rec);
    return out;
}
uint8_t pack(double c) { return static_cast<uint8_t>(static_cast<int32_t>(c * 255.0) & 0xff); }

struct Expect {
    const char* file;
    size_t bytes;
    uint32_t glyphs;
    int32_t pairs;
    const char* texture;
    int32_t defAdv;
    uint16_t lineH, cellH;
    int32_t adj;
    int16_t originY, originX;
    size_t pairsBeyondGlyphs; // -1 = not stated
};

} // namespace

int main() {
    const fs::path cache = CRREISH_GAME_CACHE_DIR;
    const fs::path misc = cache / "misc.vpp_pc";
    if (!fs::exists(misc)) {
        std::cout << "SKIP: " << misc.string() << " not present\n";
        return 77;
    }
    auto opened = vppcache::openArchive(misc);
    const vpp::Container& c = opened->container;

    // ---- spec-vint-font-format.md Sec1 + Sec2.1 -------------------------
    const size_t na = static_cast<size_t>(-1);
    const Expect expects[] = {
        {"font_body.vf3_pc", 18280, 336, 1668, "font_body_nobdr.tga", 14, 36, 36, -3, 0, 0, 165},
        {"font_header.vf3_pc", 20488, 336, 2036, "font_header_nobdr.tga", 22, 58, 100, -6, -20, -1, 169},
        {"font_header_pc.vf3_pc", 20488, 336, 2036, "font_header_pc_nobdr.tga", 24, 58, 100, -6, -20, -1, 169},
        {"font_sk.vf3_pc", 39424, 1607, 108, "font_sk_nobdr.tga", 23, 38, 30, -1, 3, 0, na},
        {"jap.vf3_pc", 52816, 2192, 0, "jap_nobdr.tga", 25, 36, 26, -2, 4, 0, na},
        {"ug-debug.vf3_pc", 5584, 224, 0, "ug-debug_nobdr.tga", 8, 17, 20, -5, -1, -1, na},
    };
    std::map<std::string, Font> fonts;
    size_t totalPairs = 0, padCd = 0;
    for (const Expect& e : expects) {
        std::vector<uint8_t> bytes;
        CHECK(readEntry(c, e.file, bytes));
        if (bytes.empty()) continue;
        CHECK(bytes.size() == e.bytes);
        Font f;
        try {
            f = parseFont(bytes);
        } catch (const std::exception& ex) {
            std::cerr << e.file << ": " << ex.what() << "\n";
            ++g_failures;
            continue;
        }
        CHECK(f.magic == 0x56464e54u);
        CHECK(f.version == 4);
        CHECK(f.glyphCount == e.glyphs);
        CHECK(f.kerningCount == e.pairs);
        CHECK(f.firstChar == 32);
        CHECK(f.defaultAdvance == e.defAdv);
        CHECK(f.lineHeight == e.lineH);
        CHECK(f.cellHeight == e.cellH);
        CHECK(f.unknown18 == 0);
        CHECK(f.advanceAdjust == e.adj);
        CHECK(f.originY == e.originY);
        CHECK(f.originX == e.originX);
        CHECK(f.textureName == e.texture);
        CHECK(f.pegName.size() > 4 && f.pegName.compare(f.pegName.size() - 4, 4, ".peg") == 0);
        // Sec1 layout check: size = 0xd0 + 6K + 24C exactly.
        CHECK(f.trailingBytes == 0);
        CHECK(e.bytes == 0xd0 + 6 * static_cast<size_t>(e.pairs) + 24 * static_cast<size_t>(e.glyphs));
        // Sec2.2: width == advance and the unread fields are 0 in every shipped glyph.
        size_t widthMismatch = 0, nonZeroUnread = 0;
        for (const GlyphRecord& g : f.glyphs) {
            if (g.width != g.advance) ++widthMismatch;
            if (g.unknown08 || g.unknown0a || g.unknown0e) ++nonZeroUnread;
        }
        CHECK(widthMismatch == 0);
        CHECK(nonZeroUnread == 0);
        size_t beyond = 0;
        for (const KerningPair& k : f.kerning) {
            ++totalPairs;
            if (k.pad == 0xCD) ++padCd;
            if (k.right >= f.glyphCount) ++beyond;
        }
        if (e.pairsBeyondGlyphs != na) CHECK(beyond == e.pairsBeyondGlyphs);
        // Sec2.3: ordered by (left, right) - every file with pairs fully
        // sorted except font_body, which has one record out of order at index 2.
        size_t descents = 0, firstDescent = 0;
        for (size_t i = 1; i < f.kerning.size(); ++i) {
            const auto& a = f.kerning[i - 1];
            const auto& b = f.kerning[i];
            if (a.left > b.left || (a.left == b.left && a.right > b.right)) {
                if (!descents) firstDescent = i;
                ++descents;
            }
        }
        if (std::string(e.file) == "font_body.vf3_pc") {
            CHECK(descents >= 1 && descents <= 2); // one misplaced record = 1 or 2 adjacent descents
            CHECK(firstDescent == 2 || firstDescent == 3);
        } else {
            CHECK(descents == 0);
        }
        std::printf("%-22s %6zu bytes, %4u glyphs, %4d pairs (%zu right>=C), texture %s, kerning descents %zu\n",
                    e.file, bytes.size(), f.glyphCount, f.kerningCount, beyond, f.textureName.c_str(), descents);
        fonts[e.file] = std::move(f);
    }
    CHECK(totalPairs == 5848);
    CHECK(padCd == 5848); // "0xCD in every one of the 5,848 shipped records"
    if (!fonts.count("font_body.vf3_pc") || !fonts.count("font_header.vf3_pc")) {
        std::cerr << "font_body/font_header missing - remaining checks need them\n";
        return 1;
    }
    const Font& body = fonts["font_body.vf3_pc"];
    const Font& header = fonts["font_header.vf3_pc"];

    // Sec2.2: font_body `A` (glyph 33) advance/width 21 at texel (60, 40);
    // furthest glyph corner (507, 427).
    CHECK(body.glyphIndex('A') == 33);
    CHECK(body.glyphs[33].advance == 21 && body.glyphs[33].width == 21);
    CHECK(body.texelX[33] == 60 && body.texelY[33] == 40);
    {
        uint32_t mx = 0, my = 0;
        for (size_t i = 0; i < body.glyphs.size(); ++i) {
            mx = std::max<uint32_t>(mx, body.texelX[i] + static_cast<uint32_t>(body.glyphs[i].width));
            my = std::max<uint32_t>(my, body.texelY[i] + body.cellHeight);
        }
        CHECK(mx == 507);
        CHECK(my == 427);
    }

    // ---- Sec7 worked numbers (emulated) ---------------------------------
    CHECK(advanceForLayout(body, 'A', 'B') == 18);
    CHECK(advanceForLayout(body, '-', 'A') == 9);
    CHECK(advanceForLayout(body, '\'', 'T') == 3); // the Sec2.3 cross-glyph quirk
    CHECK(lineHeightMetric(body) == 36);
    {
        auto q = layoutGlyphRun(body, u"A", 1, 0, 512, 512);
        CHECK(q.size() == 1 && q[0].visible());
        CHECK(q[0].x0 == 0 && q[0].y0 == 0 && q[0].x1 == 21 && q[0].y1 == 36);
        CHECK(near(q[0].u0 * 512, 60.5) && near(q[0].u1 * 512, 81) && near(q[0].v0 * 512, 40.5) &&
              near(q[0].v1 * 512, 76));
        // "for any texture size supplied"
        auto q2 = layoutGlyphRun(body, u"A", 1, 0, 2048, 1024);
        CHECK(near(q2[0].u0 * 2048, 60.5) && near(q2[0].u1 * 2048, 81) && near(q2[0].v0 * 1024, 40.5) &&
              near(q2[0].v1 * 1024, 76));
    }
    {
        auto q = layoutGlyphRun(header, u"A", 1, 0, 2048, 2048);
        CHECK(q[0].x0 == -1 && q[0].y0 == -20 && q[0].x1 == 56 && q[0].y1 == 80);
    }
    // Sec14.1: `AB` in font_body - B starts at 18; spacing +5 -> 23; -2 -> 16.
    {
        CHECK(layoutGlyphRun(body, u"AB", 2, 0, 512, 512)[1].penBefore == 18);
        CHECK(layoutGlyphRun(body, u"AB", 2, 5, 512, 512)[1].penBefore == 23);
        CHECK(layoutGlyphRun(body, u"AB", 2, -2, 512, 512)[1].penBefore == 16);
        // NUL ends the run even when count is larger.
        auto q = layoutGlyphRun(body, std::u16string(u"A\0B", 3), 3, 0, 512, 512);
        CHECK(q[1].kind == GlyphQuad::Kind::AfterNul && q[2].kind == GlyphQuad::Kind::AfterNul && !q[2].visible());
    }
    // Sec6: `A`, U+4E00, `B` measures 34 in layout; the drawer places `B`
    // 11 units further right (default advance 14 - 3).
    {
        std::u16string s = u"A";
        s.push_back(static_cast<char16_t>(0x4E00));
        s.push_back(u'B');
        LayoutResult r = lay(body, s);
        CHECK(near(r.width, 34) && near(r.height, 36));
        auto q = layoutGlyphRun(body, s, 3, 0, 512, 512);
        CHECK(q[1].kind == GlyphQuad::Kind::NotInFont && !q[1].visible());
        CHECK(q[2].penBefore == 18 + 11);
    }

    // ---- spec-vint-render.md Sec14.3 emulated reference vectors ---------
    {
        LayoutResult r = lay(body, u"Hello");
        auto rs = runs(r);
        CHECK(rs.size() == 1 && rs[0]->x == 0 && rs[0]->y == 0 && rs[0]->text == u"Hello");
        CHECK(near(r.width, 64) && near(r.height, 36));
        CHECK(r.streamBytes == 48); // 44-byte run record + 4-byte terminator
    }
    {
        LayoutResult r = lay(body, u"");
        CHECK(r.records.empty() && r.streamBytes == 4);
    }
    {
        LayoutResult r = lay(body, u"Hello\nWorld");
        auto rs = runs(r);
        CHECK(rs.size() == 2 && rs[0]->text == u"Hello" && rs[1]->text == u"World");
        CHECK(rs.size() == 2 && rs[0]->y == 0 && rs[1]->x == 0 && rs[1]->y == 36);
        CHECK(near(r.width, 71) && near(r.height, 72));
    }
    {
        LayoutParams p;
        p.align = HorzAlign::Center;
        LayoutResult r = lay(body, u"Hi\nHello", p);
        auto rs = runs(r);
        CHECK(rs.size() == 2 && near(rs[0]->x, 19.5) && rs[0]->y == 0 && rs[1]->x == 0 && rs[1]->y == 36);
        CHECK(near(r.width, 64) && near(r.height, 72));
        p.align = HorzAlign::Right;
        r = lay(body, u"Hi\nHello", p);
        rs = runs(r);
        CHECK(rs.size() == 2 && near(rs[0]->x, 39) && rs[1]->x == 0);
        CHECK(near(r.width, 64) && near(r.height, 72));
    }
    {
        LayoutParams p;
        p.leading = 10;
        LayoutResult r = lay(body, u"A\nB", p);
        auto rs = runs(r);
        CHECK(rs.size() == 2 && rs[0]->y == 0 && rs[1]->y == 46);
        CHECK(near(r.width, 18) && near(r.height, 92));
        p.textScaleY = 2;
        r = lay(body, u"A\nB", p);
        rs = runs(r);
        CHECK(rs.size() == 2 && rs[0]->y == 0 && rs[1]->y == 92);
        CHECK(near(r.width, 18) && near(r.height, 184));
    }
    {
        LayoutParams p;
        p.kerning = 3;
        LayoutResult r = lay(body, u"AB", p);
        auto rs = runs(r);
        CHECK(rs.size() == 1 && rs[0]->spacing == 3 && rs[0]->text == u"AB");
        CHECK(near(r.width, 37) && near(r.height, 36));
        p.textScaleX = 2;
        r = lay(body, u"AB", p);
        rs = runs(r);
        CHECK(rs.size() == 1 && rs[0]->sx == 2 && rs[0]->sy == 1);
        CHECK(near(r.width, 74) && near(r.height, 36));
    }
    {
        LayoutResult r = lay(body, u"ab\n\ncd");
        auto rs = runs(r);
        CHECK(rs.size() == 3);
        if (rs.size() == 3) {
            CHECK(rs[0]->text == u"ab" && rs[1]->text == u" " && rs[2]->text == u"cd");
            CHECK(rs[0]->y == 0 && rs[1]->y == 36 && rs[2]->y == 72);
        }
        CHECK(near(r.width, 34) && near(r.height, 108));
    }
    // Wrapping vectors.
    auto lineTexts = [](const LayoutResult& r) {
        std::vector<std::u16string> lines(r.lines.size());
        for (const auto& rec : r.records)
            if (rec.op == StreamRecord::Op::Run && rec.line >= 0) lines[static_cast<size_t>(rec.line)] += rec.text;
        return lines;
    };
    {
        LayoutParams p;
        p.wrapWidth = 200;
        auto l = lineTexts(lay(body, u"The quick brown fox jumps over the lazy dog", p));
        CHECK(l.size() == 3);
        if (l.size() == 3) CHECK(l[0] == u"The quick brown" && l[1] == u"fox jumps over" && l[2] == u"the lazy dog");
        p.wrapWidth = 34;
        CHECK(lineTexts(lay(body, u"AB", p)).size() == 1);
        p.wrapWidth = 33;
        CHECK(lineTexts(lay(body, u"AB", p)).size() == 2);
        p.wrapWidth = 50;
        l = lineTexts(lay(body, u"Supercalifragilistic", p));
        CHECK(l.size() == 5);
        if (l.size() == 5)
            CHECK(l[0] == u"Sup" && l[1] == u"erc" && l[2] == u"alifr" && l[3] == u"agil" && l[4] == u"istic");
    }
    // Trailing spaces count toward alignment: `ab  ` / `cd` centred -> cd at 5.5.
    {
        LayoutParams p;
        p.align = HorzAlign::Center;
        LayoutResult r = lay(body, u"ab  \ncd", p);
        auto rs = runs(r);
        CHECK(rs.size() == 2 && near(rs[1]->x, 5.5));
    }
    // Vertical centring: `a[format][scale:2.5]b[/format]c` -> line height 90,
    // b at y 0, a/c at y 27.
    {
        LayoutResult r = lay(body, u"a[format][scale:2.5]b[/format]c");
        auto rs = runs(r);
        CHECK(r.lines.size() == 1 && near(r.lines[0].height, 90));
        CHECK(rs.size() == 3);
        if (rs.size() == 3) CHECK(rs[0]->y == 27 && rs[1]->y == 0 && rs[2]->y == 27 && rs[1]->sx == 2.5f);
    }
    // [scale:] is absolute: under text_scale 3 it draws at 2.
    {
        LayoutParams p;
        p.textScaleX = p.textScaleY = 3;
        // NOTE: must bind the LayoutResult to a named local before calling
        // runs() on it, as every other check in this file does. runs()
        // returns pointers INTO its argument's r.records; `runs(lay(...))`
        // with no named local binds that LayoutResult only as a temporary
        // for the call to runs() itself, so it is destroyed at the end of
        // that statement - the next statement then reads through dangling
        // pointers into an already-freed vector<StreamRecord> buffer. A
        // Release build often reads the (stale but still-correct-looking)
        // freed bytes before they're reused; MSVC's Debug heap scribbles
        // 0xDD over freed blocks, which is what exposed this (confirmed by
        // direct inspection: sx/sy read back as -1.45682e+144, the double
        // bit-pattern of 0xDDDDDDDD repeated, and text.size() as
        // -572662307 = 0xDDDDDDDD as a signed int). Test-harness lifetime
        // bug, not a markup/layout bug - see this file's other ~20
        // `LayoutResult r = lay(...); auto rs = runs(r);` call sites for
        // the safe pattern this one now matches.
        LayoutResult scaleResult = lay(body, u"[scale:2]x", p);
        auto rs = runs(scaleResult);
        CHECK(rs.size() == 1 && rs[0]->sx == 2 && rs[0]->sy == 2);
    }

    // ---- Sec14.4 / Sec14.5 markup ------------------------------------------
    {
        LayoutResult r = lay(body, u"a[format][color:red]b[/format]c");
        auto cs = colours(r);
        CHECK(cs.size() == 2);
        if (cs.size() == 2) {
            // colour record (0.8, 0.134, 0, -1): compare as the packed bytes
            // (the stored floats themselves are not in the spec, see .cpp).
            CHECK(pack(cs[0]->r) == 204 && pack(cs[0]->g) == 34 && pack(cs[0]->b) == 0 && cs[0]->a == -1);
            CHECK(cs[1]->r == -1 && cs[1]->g == -1 && cs[1]->b == -1 && cs[1]->a == -1);
        }
        CHECK(runs(r).size() == 3);
    }
    {
        LayoutResult r = lay(body, u"[format][color:#edc300FF]x[/format]y");
        auto cs = colours(r);
        CHECK(cs.size() == 2);
        if (cs.size() == 2) {
            CHECK(near(cs[0]->r, 0.929, 1e-3) && near(cs[0]->g, 0.765, 1e-3) && cs[0]->b == 0 && cs[0]->a == 1.0);
            CHECK(cs[1]->r == -1 && cs[1]->a == -1);
        }
    }
    {
        const NamedColour* nc = findNamedColour("gre");
        CHECK(nc && std::string(nc->name) == "green");
    }
    {
        LayoutResult r = lay(body, u"a[bogus]b");
        auto l = lineTexts(r);
        CHECK(l.size() == 2);
        if (l.size() == 2) CHECK(l[0] == u"a[unrecognized: 'bogus']" && l[1] == u"b");
        CHECK(r.unrecognizedTags == 1);
    }
    // ---- Sec14.2 insertion vectors --------------------------------------
    {
        sr3locstr::LeStringsLookup emptyLoc; // no game tables in this host
        FormatResult f = formatInsertions(u"{0} and {1}", false, {}, emptyLoc);
        CHECK(f.text == u" and " && f.open.empty());
        CHECK(formatInsertions(u"p%dq", false, {}, emptyLoc).text == u"pdq");
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real_vint_font_test: all checks passed\n";
    return 0;
}
