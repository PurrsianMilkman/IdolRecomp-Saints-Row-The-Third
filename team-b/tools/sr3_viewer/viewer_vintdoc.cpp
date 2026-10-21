#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_city_support.h"
#include "viewer_common.h"
#include "viewer_vint_quad.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3luahost/character_damage_sink.h"
#include "sr3luahost/auto_combat.h"
#include "vpp/container.h"
#include "sr3luahost/engine_state.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3luahost/minimap_live_transform.h"
#include "sr3tables_environment/tables.h"
#include "sr3vintdoc/vint_doc.h"
#include "sr3xtbl/xtbl.h"
#include "sr3luahost/host.h"
#include "sr3luahost/menu_runtime.h"
#include "sr3luahost/stub_registry.h"
#include "sr3font/vint_font.h"
#include "sr3font/vint_text.h"
#include "sr3locstr/le_strings.h"

using namespace sr3viewer;

namespace {

// ===========================================================================
// `vint-doc` - the first on-screen Vint UI render (UI render unit U3,
// spec-vint-render.md Sec12 + the shared render core Sec2-Sec6), 2026-10-03.
//
// Loads ONE real shipped .vint_doc through the existing reader and loader
// (sr3vintdoc::parseDocument -> EngineState::loadVintDocument), walks the
// live element tree the way the render walk does, and draws every `bitmap`
// element's textured quad through D3D11 into an offscreen target written to
// a PNG (headless and deterministic, the same shape as `texture`).
//
// REAL (spec text, CONFIRMED unless noted):
//   * the element tree, every stored property and the element defaults -
//     the existing loader (spec-vint-doc-format.md, spec-vint-render.md Sec8);
//   * the image setter's side effects at load time (Sec12.1): source_nw/
//     source_se reset to (0,0)/(w,h), unscaled_size re-seeded from the
//     image's own size, then the auto_offset applier (Sec5.4) - run by the
//     loader at the `image` record's own position, so later file records
//     (every shipped bitmap stores source_se, and offset when it has one,
//     AFTER image - measured over all 1,820 shipped bitmap records) win
//     exactly as they do in the real loader;
//   * the image -> bitmap-resource pool: a sheet sub-image (mode 1) comes
//     from bitmap_sheets.xtbl -> <sheet>.xtbl Images/Properties StartX/
//     StartY/ImageWidth/ImageHeight (spec-tables-environment.md Sec11.7/
//     Sec17.4, CONFIRMED - caps 0x180 sheets / 0x680 sub-images honoured);
//     the texture is the sheet page, whose name is the sheet name with
//     `.tga` appended (Sec11.7 CONFIRMED for the suffix; that this names the
//     .cpeg_pc/.cvbm_pc record is HIGH there - and it is exactly how the
//     shipped records are named, e.g. ui_bms_01.tga in misc.vpp_pc >
//     interface-backend.cpeg_pc), found through this file's existing
//     TextureSearchCache/findAndUploadTexture path;
//   * the render walk (Sec2.3): cull on visible / parent alpha / own alpha
//     <= FLT_EPSILON (Sec3, NaN not culled), multiplicative tint/alpha,
//     nearest-non-default render_mode, sticky mask/background flags
//     (Sec4.1), depth-first pre-order, is-an-element children only, the
//     256-slot scratch capacity (Sec2.3 step 6), siblings sorted by the
//     Sec6.2 comparator (class key clip/mask, then depth larger first, then
//     name hash smaller first), pass 0 (draws every type, Sec2.3 step 5);
//   * the geometry: the query-time pos/M chain (EngineState::
//     vintQueryTransform, op-for-op the render compose, Sec7.1) mapped to
//     the device by the screen fit (Sec5.1: uniform scale, centred offset);
//     origin = pos + M*offset (Sec5.2);
//   * `bitmap` draw (Sec12.1/Sec12.4): no image or no texture -> no draw at
//     all; colour = tint*255 / alpha*255 truncated, low byte kept, NO clamp;
//     quad o, o+M*(se.x,0), o+M*(0,se.y), o+M*se - source_se is an EXTENT,
//     not a corner; UV u0=(p.x+nw.x)/T.w, u1=u0+se.x/T.w (same for v), p =
//     the sheet sub-image's page position (0 for a standalone image), T =
//     the page texture's own size; NO half-texel offset; a 4-vertex strip
//     in the order (u0,v0),(u1,v0),(u0,v1),(u1,v1); the vertex colour
//     modulates the texture (the quad reads the current colour back as its
//     vertex colour);
//   * `group` draw (Sec12.3): a literal empty stub - nothing is drawn.
//
// CHOSEN (engineering decisions, NOT spec facts - each also in the banner):
//   * the document's authored canvas size (document +0x5a0/+0x5a4, Sec5.1):
//     no clean text says where it comes from -> --doc-size, default
//     1280 x 720; the render target --size defaults to the same, so the fit
//     is the identity. Fit-to-height is the default (Sec5.1: HIGH that the
//     global 0x0132bbfc is set in the shipped build); --fit width flips it;
//   * sRGB: (a) the Sec12.4 colour-space switch (context byte +0x09 -
//     sRGB->linear decode of the r/g/b of every colour set through slot
//     +0x04, alpha untouched, re-quantised x255+0.5): DEFAULT OFF - HIGH
//     CONFIDENCE per spec-vint-render.md Sec11 item 9 as narrowed
//     2026-10-03 (no writer found; the byte most likely stays 0) - still
//     CHOSEN, not CONFIRMED; --srgb-colour on implements the Sec12.4 decode
//     exactly. (b) whether the UI layer samples its textures with an
//     sRGB->linear decode: NOT TRACED at all (the 2D submission wrappers
//     0x00e82d30/... are unopened, spec-render-pipeline.md Sec2 note) -
//     DEFAULT OFF, --srgb-texture on decodes the sampled r/g/b in the pixel
//     shader;
//   * blending: every draw is ordinary alpha blending (src alpha, 1 - src
//     alpha). Sec4.1 says the seeded render_mode 1 is HIGH `filter`, but the
//     game-side meaning of the per-element render_mode push (callback +0x18)
//     is OPEN (Sec11 item 2) - non-filter modes are counted, not honoured;
//   * sampler: linear filtering, clamp addressing (the UI layer's sampler
//     state is not traced);
//   * device-pixel mapping: positions go straight to D3D11 pixel space. D3D9
//     (the game's API) puts pixel centres at integers, D3D11 at +0.5; whether
//     the game's 2D wrapper compensates is OPEN (unopened wrapper bodies) -
//     no compensation is applied here;
//   * source_nw/source_se are int32 fields (Sec12.1) stored from float
//     records - converted by truncation (non-integral values are counted;
//     none occur in the shipped documents measured);
//   * the image pool's choice between a sheet sub-image (mode 1) and a
//     standalone image (mode 0): the sheet registry is consulted first; a
//     standalone image is looked up as the texture record `<image>.tga`
//     (the resource-callback channel, Sec12.1 slot +0x00, is not specced);
//   * duplicate sub-image names across sheets: first registered wins
//     (counted); the open-addressing collision rule is not traced;
//   * the document root (+0x594) is modelled as an implicit default element
//     whose children are the header +0x1A top-level elements; a full
//     comparator tie keeps file order (the real comparator never returns 0
//     - Sec6.2 - so the engine's own order there is unspecified);
//   * clear colour (40, 40, 48).
//
// `text` - REAL since 2026-10-03 (UI render unit U5: spec-vint-render.md
// Sec12.2 + Sec14, spec-vint-font-format.md; library sr3font):
//   * REAL: the shipped .vf3_pc fonts (font_body / font_header_pc /
//     ug-debug, the DEFAULT language arrangement's load order) and their
//     `_nobdr` glyph textures; the named font table (draw = secondary then
//     primary if different, measure = primary); the metric callbacks incl.
//     the shipped kerning-lookup quirk; the insertion syntax; layout (break
//     rules, wrap, kept spaces, alignment within the widest line, vertical
//     centring per line, size = widest x summed heights); markup
//     [format]/[/format]/[color:]/[scale:]/[image:] with explicit colour
//     components REPLACING the composed tint/alpha and -1 inheriting; the
//     glyph quads (font origin offsets, near-edge half-texel UV inset,
//     stacked with the pixel shader's own half-texel, Sec14.1/Sec15.7);
//     the shadow pass (shadow_tint x tint, shadow_alpha x alpha, offset in
//     device pixels, colour records suppressed); unscaled_size = the
//     measured size with auto_offset re-applied from it (Sec12.2/Sec5.4).
//     Text goes through the same VintQuadPipeline::quad() as bitmaps, so
//     render_mode blending and the mask modes apply identically.
//   * REAL since 2026-10-03 (spec-localization-format.md, library
//     sr3locstr): a STRING text_tag is first tried as a localisation key
//     against the 10-11 US `.le_strings` tables (Sec5.2 step 4's load
//     order; patch0 only if a --textures/--fonts archive holds
//     patch_compressed.vpp_pc) - a real key draws the resolved text; any
//     other string (a non-key placeholder, or no table loaded at all)
//     falls back to the raw text_tag string exactly as before this unit.
//     A key-hash text_tag / text_tag_crc now resolves the same way: a hit
//     draws the resolved text, a miss (never exercised by any shipped
//     document, Sec8: "no text_tag_crc record ships") falls to "no source
//     text, nothing drawn" rather than staying OPEN, since the lookup is
//     now a real, determinate answer either way.
//   * REAL since 2026-10-07 (spec-vint-render.md Sec14.2's insertion-
//     formatter table, wired to the same sr3locstr::LeStringsLookup -
//     spec-localization-format.md Sec9 item 3): `insert_values`' `{N}` row,
//     a STRING value is first tried as a localisation key too (slot +0x00,
//     lookupByKey) and falls back to the raw string on a miss; `{N:
//     text_tag_crc}` looks its value up directly as a key HASH (slot
//     +0x04, lookup(hash) - never re-hashed as a string), giving
//     "{Hash not found!}" on a miss and "{Invalid insertion value for a
//     hash!}" for a non-integer value, per Sec14.2's own row text.
//   * CHOSEN: layout is settled once before the walk (the refresh hook's
//     timing relative to the draw is not specced - the settled state is
//     what a running game shows after its first frame); fonts and
//     `.le_strings` tables are read from --fonts archives, else the
//     --textures archives; every other CHOSEN edge is labelled in
//     src/vint_text.cpp / src/vint_font.cpp / src/le_strings.cpp.
//   * OPEN (counted, element not drawn): a 0x80-tagged string (decoder not
//     specced byte-exactly); insertions still needing a game table this
//     library does not model - a named insertion symbol (`{NAME}`, slot
//     +0x0c, outside sr3locstr's scope) and a non-string insert_values
//     value's conversion to text (no modifier, or `:not_localized`, or
//     `%s` - all 0x00e23f40, which has no documented format in any clean
//     spec section found in this repo).
//   * NOT IMPLEMENTED: the three non-default language arrangements and
//     derived game fonts (out of scope); line frames (Sec14.6, no shipped
//     use); force_case is dead in the shipped build and deliberately
//     ignored (Sec14.2).
//   * --text-placeholder keeps the pre-U5 behaviour available: a magenta
//     24x12 outline at the composed origin (stored offset, no layout)
//     INSTEAD of text - a comparison aid that reproduces the *_pre_text
//     golden baselines. --no-text-placeholder draws nothing for text.
//
// HONESTLY STUBBED / NOT DRAWN (counted and listed on stdout):
//   * masks: elements inside a mask subtree (the mask-write pass, Sec4.2)
//     are NOT drawn, and their masked siblings are drawn UNMASKED - the mask
//     modes' game-side meaning is HIGH only and stencil semantics are OPEN;
//   * clip (Sec4.3) is not applied (its pop indexing is OPEN, item 3);
//   * every other type (bitmap_circle, clip, document, gradient - whose
//     corner-colour route is OPEN, item 15 - point, sr2_map, video) draws
//     nothing here: their draw units are not specced (U4+). A `document`
//     element's referenced widget document is not instanced (the loader
//     does not model it).
// ===========================================================================

struct VintSheetSub {
    std::string sheet;
    int32_t x = 0, y = 0, w = 0, h = 0;
};


// Sec12.1 / Sec12.5 colour packing: x255 via a double, truncated, low byte
// kept, NO clamp. HOST-SAFETY: a value outside the 32-bit integer range (or
// NaN) has no defined C++ conversion; it yields 0x80000000 here (the x87
// "integer indefinite" a truncating store produces), so its low byte is 0.
uint8_t vintPackComponent(double c) {
    const double v = c * 255.0;
    if (!(v > -2147483649.0 && v < 2147483648.0)) return 0;
    return static_cast<uint8_t>(static_cast<uint32_t>(static_cast<int32_t>(v)) & 0xFFu);
}


// Sec12.4 slot +0x04 body when context byte +0x09 is set: r/g/b sRGB ->
// linear, alpha unchanged, all four re-quantised (x255 + 0.5, truncated).
uint8_t vintSrgbDecodeByte(uint8_t b) {
    const double c = b / 255.0;
    const double lin = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    return static_cast<uint8_t>(static_cast<int>(lin * 255.0 + 0.5));
}


std::optional<std::vector<double>> vintNums(const sr3luahost::EngineState& es, uint32_t h, const char* name, size_t n) {
    const sr3luahost::VintStoredProperty* sp = es.findVintStoredProperty(h, name);
    if (!sp || sp->typedAs == 0 || sp->values.size() != n) return std::nullopt;
    std::vector<double> out;
    for (const auto& v : sp->values) {
        if (v.kind != sr3luahost::VintTaggedValue::Kind::Number) return std::nullopt;
        out.push_back(v.number);
    }
    return out;
}

std::optional<bool> vintBool(const sr3luahost::EngineState& es, uint32_t h, const char* name) {
    const sr3luahost::VintStoredProperty* sp = es.findVintStoredProperty(h, name);
    if (!sp || sp->values.empty() || sp->values[0].kind != sr3luahost::VintTaggedValue::Kind::Boolean) return std::nullopt;
    return sp->values[0].boolean;
}

std::string vintStr(const sr3luahost::EngineState& es, uint32_t h, const char* name) {
    const sr3luahost::VintStoredProperty* sp = es.findVintStoredProperty(h, name);
    if (!sp || sp->values.empty() || sp->values[0].kind != sr3luahost::VintTaggedValue::Kind::String) return std::string();
    return sp->values[0].text;
}


// The per-element state the render walk carries (Sec4.1 subset this draw
// path reads; matrix/position come from vintQueryTransform per element).
struct VintWalkState {
    double tint[3] = {1, 1, 1};
    double alpha = 1.0;
    int64_t renderMode = 1; // seeded 1 (Sec2.2)
    bool inMask = false;
    bool inBackground = false;
    // The stencil mask mode (0/1/2, Sec15.6) active while drawing this
    // element, inherited unchanged once inMask is set (Sec11: nested child
    // loops below a mask child do no further mode switching of their own -
    // only the outermost sibling loop not itself already inside a mask
    // subtree runs the real mode state machine, see the walk's own sibling
    // loop below).
    int activeMaskMode = 0;
};


std::string vintFmt(double v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.3f", v);
    std::string s = b;
    if (s == "-0.000") s = "0.000";
    return s;
}

} // namespace


int runVintDoc(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
                     "usage: sr3_viewer vint-doc <doc_archive.vpp_pc> <name.vint_doc> <tables_archive.vpp_pc> <out.png>\n"
                     "                           --textures <archive.vpp_pc> [--textures <archive.vpp_pc> ...]\n"
                     "                           [--tables <overlay_tables_archive.vpp_pc> ...]\n"
                     "                           [--size W H] [--doc-size W H] [--fit height|width]\n"
                     "                           [--srgb-colour on|off] [--srgb-texture on|off]\n"
                     "                           [--fonts <archive.vpp_pc> ...] [--text-placeholder | --no-text-placeholder]\n"
                     "       Vint UI render (spec-vint-render.md Sec12-Sec15, units U3-U6): draws every `bitmap`\n"
                     "       element of one real .vint_doc through D3D11 (real sheet sub-images, real composed\n"
                     "       geometry), and every `text` element as REAL glyphs from the shipped .vf3_pc fonts\n"
                     "       (Sec14, U5); `group` draws nothing. --text-placeholder draws the old magenta origin\n"
                     "       marker INSTEAD of text (comparison aid); --no-text-placeholder draws no text at all.\n"
                     "       See runVintDoc()'s own top comment for REAL / CHOSEN / stubbed.\n");
        return 1;
    }
    const std::string docArchivePath = argv[2];
    const std::string docName = argv[3];
    // Tables archives, in lookup order: --tables entries FIRST (in the order
    // given), then the positional one last - each table FILE comes from the
    // first archive that has it (CHOSEN layering, e.g. a partial patch
    // overlay such as patch_compressed.vpp_pc in front of misc_tables.vpp_pc;
    // the engine's own archive precedence is not modelled here).
    std::vector<std::string> tablesArchivePaths;
    const std::string positionalTables = argv[4];
    const std::string outPath = argv[5];
    std::vector<std::string> textureArchivePaths;
    std::vector<std::string> fontArchivePaths;
    uint32_t width = 1280, height = 720;
    double docW = 1280.0, docH = 720.0;
    bool fitToHeight = true, srgbColour = false, srgbTexture = false;
    // Text drawing mode (2026-10-03, U5). REAL (default) lays out and draws
    // the shipped glyphs. PLACEHOLDER is the pre-U5 behaviour - a magenta
    // 24x12 outline at each text element's composed origin, nothing else -
    // KEPT deliberately as an explicit opt-in (--text-placeholder): it is
    // the quickest way to see where every text element's origin lands
    // independently of layout/auto_offset, and it reproduces the pre-text
    // golden baselines (tests/golden/*_pre_text/) exactly for comparison.
    // OFF (--no-text-placeholder, the flag's pre-U5 meaning: no marker, no
    // text) draws nothing for text.
    enum class TextMode { Real, Placeholder, Off } textMode = TextMode::Real;
    for (int i = 6; i < argc; ++i) {
        const std::string a = argv[i];
        auto onOff = [&](bool& flag) {
            if (i + 1 >= argc) return false;
            const std::string v = argv[++i];
            if (v == "on") flag = true;
            else if (v == "off") flag = false;
            else return false;
            return true;
        };
        if (a == "--textures" && i + 1 < argc) textureArchivePaths.push_back(argv[++i]);
        else if (a == "--tables" && i + 1 < argc) tablesArchivePaths.push_back(argv[++i]);
        else if (a == "--size" && i + 2 < argc) {
            width = static_cast<uint32_t>(std::atoi(argv[++i]));
            height = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (a == "--doc-size" && i + 2 < argc) {
            docW = std::atof(argv[++i]);
            docH = std::atof(argv[++i]);
        } else if (a == "--fit" && i + 1 < argc) {
            const std::string v = argv[++i];
            if (v == "height") fitToHeight = true;
            else if (v == "width") fitToHeight = false;
            else { std::fprintf(stderr, "--fit takes height|width\n"); return 2; }
        } else if (a == "--srgb-colour") {
            if (!onOff(srgbColour)) { std::fprintf(stderr, "--srgb-colour takes on|off\n"); return 2; }
        } else if (a == "--srgb-texture") {
            if (!onOff(srgbTexture)) { std::fprintf(stderr, "--srgb-texture takes on|off\n"); return 2; }
        } else if (a == "--no-text-placeholder") textMode = TextMode::Off;
        else if (a == "--text-placeholder") textMode = TextMode::Placeholder;
        else if (a == "--fonts" && i + 1 < argc) fontArchivePaths.push_back(argv[++i]);
        else { std::fprintf(stderr, "unrecognised argument '%s'\n", a.c_str()); return 2; }
    }
    if (width == 0 || height == 0 || !(docW > 0) || !(docH > 0)) { std::fprintf(stderr, "bad --size/--doc-size\n"); return 2; }

    std::printf("=== vint-doc: Vint UI render (spec-vint-render.md Sec12/Sec15, units U3+U6) ===\n");
    std::printf("REAL       : element tree + properties (real .vint_doc), image-setter side effects (Sec12.1),\n"
                "             sheet sub-images (Sec17.4), render walk/cull/compose/sort (Sec2-Sec6), bitmap quad\n"
                "             (Sec12.1/12.4: source_se = EXTENT; Sec15.7: half-texel UV offset + -0.5px vertex\n"
                "             shift, both real now), group = empty stub (Sec12.3), mask (Sec15.6: real stencil\n"
                "             clear/write/test, modes 0/1/2), all 5 render_mode blend codes (Sec15.4: filter/\n"
                "             additive/additive_alpha/subtractive/multiply), wrap sampler addressing (Sec15.7)\n");
    if (textMode == TextMode::Real) {
        std::printf("REAL TEXT  : `text` (Sec14, U5): shipped .vf3_pc fonts (spec-vint-font-format.md Sec2), default\n"
                    "             font table (Sec4.2 \"anything else\" row), metrics incl. the kerning quirk (Sec2.3/Sec6),\n"
                    "             insertion syntax (Sec14.2), layout/wrap/align/vertical centring (Sec14.3), markup\n"
                    "             format/color/scale/image (Sec14.4/14.5: explicit colour REPLACES, -1 inherits), glyph\n"
                    "             quads + near-edge half-texel UV inset (Sec14.1), shadow pass (Sec12.2), auto_offset\n"
                    "             re-applied from the measured size (Sec12.2/Sec5.4); localisation-key text_tag/\n"
                    "             text_tag_crc resolution against the US .le_strings tables (spec-localization-format.md,\n"
                    "             library sr3locstr); insert_values' {N} (string value as a key) and {N:text_tag_crc}\n"
                    "             (value as a key hash) insertions resolve against the same tables (Sec9 item 3)\n");
        std::printf("CHOSEN TEXT: a STRING text_tag not found as a real key falls back to raw text (Sec14.2 step 2\n"
                    "             /spec-localization-format.md Sec7 item 2); a key-hash text_tag/text_tag_crc miss (never\n"
                    "             shipped) falls to \"no source text\"; a named insertion symbol ({NAME}) and a\n"
                    "             non-string insert_values value's text conversion (0x00e23f40, no spec section) and\n"
                    "             0x80-tagged strings = still OPEN (not drawn, not exercised by any shipped document);\n"
                    "             layout settled once before the walk (refresh hook timing not specced);\n"
                    "             only the DEFAULT language arrangement (no derived fonts); fonts and .le_strings tables\n"
                    "             read from --fonts archives, else the --textures archives; line frames (Sec14.6, no\n"
                    "             shipped use) not drawn; force_case ignored (dead, Sec14.2)\n");
    } else {
        std::printf("PLACEHOLDER: `text` elements are NOT real text - %s\n",
                    textMode == TextMode::Placeholder
                        ? "a magenta 24x12 outline marks each one's composed origin only (--text-placeholder)."
                        : "nothing is drawn for them (--no-text-placeholder).");
    }
    std::printf("CHOSEN     : doc canvas %sx%s (--doc-size; source of document +0x5a0/+0x5a4 not specced),\n"
                "             fit to %s (Sec5.1 HIGH: height), clear colour (40,40,48); `render_mode` above 5\n"
                "             falls back to filter rather than the real engine's own sticky \"previous code\"\n"
                "             (Sec13.5, not exercised by any real content found); scissor test not yet wired\n"
                "             (Sec15.9 item 1, separate from this pass); `background`'s own depth mode 1 not\n"
                "             yet wired (Sec15.5, separate from this pass - every draw uses depth mode 0)\n",
                vintFmt(docW).c_str(), vintFmt(docH).c_str(), fitToHeight ? "height" : "width");
    std::printf("CHOSEN     : sRGB colour decode (Sec12.4 context byte +0x09) = %s (default off, HIGH per Sec11 item 9);\n"
                "             sRGB texture decode (UI sampler, untraced) = %s (default off)\n",
                srgbColour ? "ON" : "off", srgbTexture ? "ON" : "off");
    std::printf("NOT DRAWN  : clip, bitmap_circle, gradient, document, point, sr2_map, video - counted below\n");

    // ---- the document --------------------------------------------------------
    std::vector<uint8_t> docArchiveBytes = readFileLargeC(docArchivePath);
    if (docArchiveBytes.empty()) { std::fprintf(stderr, "could not read %s\n", docArchivePath.c_str()); return 1; }
    vpp::Container docArchive(vpp::ByteView(docArchiveBytes.data(), docArchiveBytes.size()));
    std::vector<uint8_t> docBytes, unused;
    if (!findEntry(docArchive, docName, docBytes, unused, std::string()) || docBytes.empty()) {
        std::fprintf(stderr, "could not find '%s' in %s\n", docName.c_str(), docArchivePath.c_str());
        return 1;
    }
    sr3vintdoc::Document doc = sr3vintdoc::parseDocument(vpp::ByteView(docBytes.data(), docBytes.size()));
    std::printf("document   : %s (%zu bytes) - %zu records, lands exactly: %s\n", docName.c_str(), docBytes.size(),
                doc.totalRecordCount(), doc.landsExactly() ? "yes" : "NO");

    // ---- bitmap sheets (spec-tables-environment.md Sec11.7 / Sec17.4) -----------
    tablesArchivePaths.push_back(positionalTables);
    std::vector<std::unique_ptr<CityArchive>> tablesArchives;
    for (const auto& p : tablesArchivePaths) {
        auto a = std::make_unique<CityArchive>();
        a->path = p;
        const size_t slash = p.find_last_of("/\\");
        a->label = slash == std::string::npos ? p : p.substr(slash + 1);
        a->bytes = mapFileLargeC(p);
        if (a->bytes.empty()) { std::fprintf(stderr, "could not read %s\n", p.c_str()); return 1; }
        a->top = std::make_unique<vpp::Container>(vpp::ByteView(a->bytes.data(), a->bytes.size()));
        tablesArchives.push_back(std::move(a));
    }
    std::map<std::string, size_t> tableFilesFrom; // archive label -> files taken from it
    auto findTable = [&](const std::string& name, std::vector<uint8_t>& out) {
        for (auto& a : tablesArchives) {
            std::vector<uint8_t> sib;
            if (findEntry(*a->top, name, out, sib, std::string()) && !out.empty()) {
                ++tableFilesFrom[a->label];
                return true;
            }
        }
        return false;
    };
    std::map<std::string, VintSheetSub> subByName; // lower-cased name
    size_t sheetCount = 0, sheetFilesFound = 0, subRecords = 0, subDuplicates = 0, subOverCap = 0;
    {
        std::vector<uint8_t> bsBytes;
        if (!findTable("bitmap_sheets.xtbl", bsBytes)) {
            std::fprintf(stderr, "bitmap_sheets.xtbl not found in any tables archive\n");
            return 1;
        }
        sr3xtbl::Document bs = sr3xtbl::ParseDocument(bsBytes.data(), bsBytes.size());
        std::vector<sr3tables_environment::BitmapSheet> sheets = sr3tables_environment::ParseBitmapSheetsTable(bs);
        if (sheets.size() > 0x180) sheets.resize(0x180); // Sec11.7: at most 0x180 sheets
        sheetCount = sheets.size();
        for (const auto& s : sheets) {
            const std::string sheetName = s.NameOrDefault();
            if (sheetName.empty()) continue;
            std::vector<uint8_t> xb;
            if (!findTable(sheetName + ".xtbl", xb)) continue;
            ++sheetFilesFound;
            sr3xtbl::Document sx = sr3xtbl::ParseDocument(xb.data(), xb.size());
            for (const sr3xtbl::Node* bsRow : sr3xtbl::Children(sx.table(), "BitmapSheets")) {
                const sr3xtbl::Node* images = sr3xtbl::FindChild(bsRow, "Images");
                for (const sr3xtbl::Node* p : sr3xtbl::Children(images, "Properties")) {
                    const std::string* nm = sr3xtbl::ChildText(p, "Name");
                    if (!nm) continue;
                    ++subRecords;
                    if (subRecords > 0x680) { ++subOverCap; continue; } // Sec17.4 capacity 0x680
                    auto num = [&](const char* k) {
                        const std::string* t = sr3xtbl::ChildText(p, k);
                        return t ? sr3xtbl::ParseInt32(*t) : 0;
                    };
                    VintSheetSub sub{sheetName, num("StartX"), num("StartY"), num("ImageWidth"), num("ImageHeight")};
                    if (!subByName.emplace(toLowerLocal(*nm), sub).second) ++subDuplicates;
                }
            }
        }
    }
    std::printf("sheets     : %zu bitmap_sheets.xtbl rows, %zu sheet .xtbl files found, %zu sub-image records "
                "(%zu duplicate names - first wins, %zu over the 0x680 cap)\n",
                sheetCount, sheetFilesFound, subRecords, subDuplicates, subOverCap);
    std::printf("tables from:");
    for (const auto& a : tablesArchives) std::printf(" %s=%zu", a->label.c_str(), tableFilesFrom[a->label]);
    std::printf(" (first archive holding a file wins - CHOSEN layering)\n");

    // ---- device + texture archives -----------------------------------------
    sr3render::RenderDevice device;
    std::string error;
    if (!device.initialise(width, height, error)) { std::fprintf(stderr, "device init failed: %s\n", error.c_str()); return 1; }
    std::printf("device     : %s (%s), target %ux%u RGBA8 offscreen\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().empty() ? "unnamed adapter" : device.adapterName().c_str(), device.width(),
                device.height());
    std::vector<std::unique_ptr<CityArchive>> texArchives;
    for (const auto& p : textureArchivePaths) {
        auto a = std::make_unique<CityArchive>();
        a->path = p;
        const size_t slash = p.find_last_of("/\\");
        a->label = slash == std::string::npos ? p : p.substr(slash + 1);
        a->bytes = mapFileLargeC(p);
        if (a->bytes.empty()) { std::fprintf(stderr, "could not read %s\n", p.c_str()); return 1; }
        a->top = std::make_unique<vpp::Container>(vpp::ByteView(a->bytes.data(), a->bytes.size()));
        texArchives.push_back(std::move(a));
    }
    auto findTexture = [&](const std::string& recordName, VintImageEntry& e) {
        for (auto& a : texArchives) {
            sr3render::UploadedTexture ut;
            std::string foundIn;
            if (findAndUploadTexture(*a->top, recordName, device.device(), ut, foundIn, &a->texCache)) {
                e.srv = ut.srv;
                e.texW = ut.width;
                e.texH = ut.height;
                e.textureName = recordName;
                e.foundIn = a->label + " > " + foundIn;
                return true;
            }
        }
        return false;
    };

    // The bitmap-resource pool lookup (Sec12.1), memoised per image name.
    std::map<std::string, VintImageEntry> images;
    auto lookupImage = [&](const std::string& name) -> VintImageEntry& {
        const std::string key = toLowerLocal(name);
        auto it = images.find(key);
        if (it != images.end()) return it->second;
        VintImageEntry e;
        auto sit = subByName.find(key);
        if (sit != subByName.end()) {
            e.sized = true;
            e.mode = 1;
            e.sheet = sit->second.sheet;
            e.pageX = sit->second.x;
            e.pageY = sit->second.y;
            e.imgW = sit->second.w;
            e.imgH = sit->second.h;
            findTexture(sit->second.sheet + ".tga", e); // Sec11.7: sheet name + ".tga"
        } else if (findTexture(name + ".tga", e)) {    // CHOSEN standalone mapping (see top comment)
            e.sized = true;
            e.mode = 0;
            e.imgW = e.texW;
            e.imgH = e.texH;
        }
        return images.emplace(key, e).first->second;
    };

    sr3luahost::EngineState es;
    const uint32_t docHandle = es.loadVintDocument(docName, doc, std::string(), [&](const std::string& img) {
        VintImageEntry& e = lookupImage(img);
        return e.sized ? std::optional<std::pair<double, double>>(std::make_pair(e.imgW, e.imgH)) : std::nullopt;
    });
    const sr3luahost::LoadedVintDocument* ld = es.loadedVintDocument(docHandle);
    const auto& st = es.vintPropertyStats();
    std::printf("loaded     : %zu objects, %zu applied file records; image side effects (Sec12.1): %llu applied, "
                "%llu source-coord resets, %llu unresolved; auto_offset applier: %llu wrote offset, %llu left it\n",
                ld->objectCount, ld->propertyCount, static_cast<unsigned long long>(st.docImageSizesApplied),
                static_cast<unsigned long long>(st.docImageSourceCoordsReset),
                static_cast<unsigned long long>(st.docImageUnresolved),
                static_cast<unsigned long long>(st.docAutoOffsetApplied),
                static_cast<unsigned long long>(st.docAutoOffsetNotApplied));
    {
        size_t sheetHits = 0, standalone = 0, unsized = 0, noTexture = 0;
        for (const auto& kv : images) {
            if (!kv.second.sized) { ++unsized; continue; }
            (kv.second.mode == 1 ? sheetHits : standalone)++;
            if (!kv.second.srv) ++noTexture;
        }
        std::printf("images     : %zu distinct - %zu sheet sub-images, %zu standalone, %zu not in the pool, %zu with no "
                    "texture found\n",
                    images.size(), sheetHits, standalone, unsized, noTexture);
        for (const auto& kv : images) {
            const VintImageEntry& e = kv.second;
            if (!e.sized) { std::printf("  [image] %-36s NOT FOUND (no sheet sub-image, no <name>.tga record) -> no draw\n", kv.first.c_str()); continue; }
            std::printf("  [image] %-36s mode %d %s page (%d,%d) size %sx%s texture %s %s\n", kv.first.c_str(), e.mode,
                        e.mode == 1 ? e.sheet.c_str() : "-", e.pageX, e.pageY, vintFmt(e.imgW).c_str(),
                        vintFmt(e.imgH).c_str(), e.textureName.c_str(),
                        e.srv ? ("found " + std::to_string(e.texW) + "x" + std::to_string(e.texH) + " in " + e.foundIn).c_str()
                              : "NOT FOUND -> handle -1, no draw");
        }
    }

    // ---- fonts (spec-vint-font-format.md Sec3/Sec4.2, U5) ---------------------
    // The DEFAULT language arrangement's three game fonts, in load order
    // (font_body = 0, font_header_pc = 1, ug-debug = 2). Each file is the
    // stem + ".vf3_pc" (Sec3 step 1), searched in the --fonts archives if
    // any were given, else in the --textures archives (CHOSEN: the six
    // shipped fonts live in misc.vpp_pc, which every existing vint-doc
    // command line already passes as a --textures archive). The glyph
    // texture is the one named at header +0x68 (`<font>_nobdr.tga`, Sec3
    // step 4), found through the same texture lookup bitmaps use.
    struct VintGameFont {
        bool loaded = false;
        sr3font::Font font;
        std::string foundIn;
        VintImageEntry tex; // srv null = no texture -> glyphs not drawn
    };
    std::array<VintGameFont, sr3font::kGameFontCount> gameFonts;
    sr3locstr::LeStringsLookup locLookup;
    if (textMode == TextMode::Real) {
        std::vector<std::unique_ptr<CityArchive>> fontArchivesOwned;
        std::vector<CityArchive*> fontArchives;
        for (const auto& p : fontArchivePaths) {
            auto a = std::make_unique<CityArchive>();
            a->path = p;
            const size_t slash = p.find_last_of("/\\");
            a->label = slash == std::string::npos ? p : p.substr(slash + 1);
            a->bytes = mapFileLargeC(p);
            if (a->bytes.empty()) { std::fprintf(stderr, "could not read %s\n", p.c_str()); return 1; }
            a->top = std::make_unique<vpp::Container>(vpp::ByteView(a->bytes.data(), a->bytes.size()));
            fontArchives.push_back(a.get());
            fontArchivesOwned.push_back(std::move(a));
        }
        if (fontArchives.empty())
            for (auto& a : texArchives) fontArchives.push_back(a.get());
        for (int32_t id = 0; id < sr3font::kGameFontCount; ++id) {
            VintGameFont& gf = gameFonts[static_cast<size_t>(id)];
            const std::string file = std::string(sr3font::gameFontFileStem(id)) + ".vf3_pc";
            std::string parseError;
            for (CityArchive* a : fontArchives) {
                std::vector<uint8_t> bytes, sib;
                if (!findEntry(*a->top, file, bytes, sib, std::string()) || bytes.empty()) continue;
                try {
                    gf.font = sr3font::parseFont(bytes);
                    gf.loaded = true;
                    gf.foundIn = a->label;
                } catch (const sr3font::FontFormatError& ex) {
                    parseError = ex.what();
                }
                break;
            }
            if (!gf.loaded) {
                std::printf("  [font] game id %d %-22s NOT LOADED (%s) -> text measured with it is OPEN\n", id,
                            file.c_str(), parseError.empty() ? "not found in the font archives" : parseError.c_str());
                continue;
            }
            findTexture(gf.font.textureName, gf.tex);
            std::printf("  [font] game id %d %-22s from %s: %u glyphs, %d kerning pairs, line height %u, cell %u, "
                        "default advance %d, adjust %d, origin (%d,%d); texture %s %s\n",
                        id, file.c_str(), gf.foundIn.c_str(), gf.font.glyphCount, gf.font.kerningCount,
                        gf.font.lineHeight, gf.font.cellHeight, gf.font.defaultAdvance, gf.font.advanceAdjust,
                        gf.font.originX, gf.font.originY, gf.font.textureName.c_str(),
                        gf.tex.srv ? ("found " + std::to_string(gf.tex.texW) + "x" + std::to_string(gf.tex.texH) +
                                      " in " + gf.tex.foundIn).c_str()
                                   : "NOT FOUND -> its glyphs are not drawn (spec-vint-font-format.md Sec8 item 1)");
        }

        // ---- localisation strings (spec-localization-format.md, library
        // sr3locstr, 2026-10-03 follow-up to U5) -----------------------
        // The US tables only (this project's only modelled language),
        // searched in the same --fonts/--textures archives as the fonts
        // above (misc.vpp_pc holds both the fonts and 10 of these 11
        // tables), loaded in Sec5.2 step 4's order so a shared key resolves
        // from the same table the game itself would use first (Sec6.1).
        // patch0 additionally needs patch_compressed.vpp_pc among the given
        // archives; a missing table (patch0, or any other) is not fatal -
        // Sec5.1 step 2's own "a missing file... returns false" per table.
        {
            static const char* const kTableFiles[] = {
                "static_us.le_strings",      "hud_us.le_strings",       "activity_us.le_strings",
                "diversion_us.le_strings",   "mission_us.le_strings",   "menu_us.le_strings",
                "multiplayer_us.le_strings", "customize_us.le_strings", "subtitle_us.le_strings",
                "platform_pc_us.le_strings", "patch0_us.le_strings",
            };
            size_t tablesFound = 0, recordsTotal = 0;
            for (const char* file : kTableFiles) {
                for (CityArchive* a : fontArchives) {
                    std::vector<uint8_t> bytes, sib;
                    if (!findEntry(*a->top, file, bytes, sib, std::string()) || bytes.empty()) continue;
                    try {
                        sr3locstr::LeStringsTable t = sr3locstr::parseLeStrings(bytes);
                        recordsTotal += t.recordCount;
                        locLookup.addTable(std::move(t));
                        ++tablesFound;
                    } catch (const sr3locstr::LeStringsFormatError& ex) {
                        std::printf("  [loc] %-26s PARSE ERROR (%s) -> table not loaded\n", file, ex.what());
                    }
                    break;
                }
            }
            std::printf("  [loc] %zu/%zu US .le_strings tables loaded (%zu records total) -> text_tag/text_tag_crc "
                        "tried as a localisation key first (spec-localization-format.md Sec6.1/Sec7 item 2)\n",
                        tablesFound, sizeof(kTableFiles) / sizeof(kTableFiles[0]), recordsTotal);
        }
    }

    // ---- screen fit (Sec5.1) ----------------------------------------------
    const double aspect = docW / docH;
    double fitW, fitH;
    if (fitToHeight) { fitH = height; fitW = aspect * height; }
    else { fitW = width; fitH = width / aspect; }
    const double fitScale = fitW / docW; // uniform (Sec5.1)
    const double fitOffX = (static_cast<double>(width) - fitW) * 0.5, fitOffY = (static_cast<double>(height) - fitH) * 0.5;
    std::printf("screen fit : scale %s offset (%s,%s)\n", vintFmt(fitScale).c_str(), vintFmt(fitOffX).c_str(),
                vintFmt(fitOffY).c_str());

    // ---- render walk (Sec2.3) ----------------------------------------------
    VintQuadPipeline pipe;
    if (!pipe.initialise(device.device(), width, height, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    device.bindRenderTargetWithDepth(pipe.depthView());
    device.context()->ClearDepthStencilView(pipe.depthView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    device.clear({40, 40, 48, 255});
    pipe.begin(device.context(), width, height, srgbTexture);

    std::map<std::string, size_t> notDrawnByType;
    size_t culled = 0, bitmapsDrawn = 0, bitmapsNoImage = 0, bitmapsNoTexture = 0, groupsVisited = 0, textMarkers = 0,
           maskSkipped = 0, nonFilterModes = 0, backgroundElems = 0, scratchDropped = 0, geometryOpen = 0,
           nonIntegralSource = 0, visited = 0;
    // Real-text counters (U5).
    size_t textDrawn = 0, textGlyphs = 0, textInlineImages = 0, textOpen = 0, textNoFont = 0, textNoSource = 0,
           textGlyphsNoTexture = 0, textUnrecognizedTags = 0, textTruncated = 0, textKeyResolved = 0;
    const uint8_t kPlaceholder[4] = {255, 0, 255, 255};

    // ---- real text (spec-vint-render.md Sec12.2 + Sec14, U5) -----------------
    // Values of `text` properties are read by their stored SHAPE: most of the
    // text descriptor rows have an OPEN type code in this host's property
    // table (src/vint_property_types.cpp), so the loader stores the file's
    // own tagged values raw (typedAs 0) and vintNums() - which insists on a
    // typed value - would refuse them.
    auto rawProp = [&](uint32_t h, const char* name) { return es.findVintStoredProperty(h, name); };
    auto rawNums = [&](uint32_t h, const char* name, size_t n) -> std::optional<std::vector<double>> {
        const sr3luahost::VintStoredProperty* sp = rawProp(h, name);
        if (!sp || sp->values.size() != n) return std::nullopt;
        std::vector<double> out;
        for (const auto& v : sp->values) {
            if (v.kind != sr3luahost::VintTaggedValue::Kind::Number) return std::nullopt;
            out.push_back(v.number);
        }
        return out;
    };
    auto rawBool = [&](uint32_t h, const char* name) -> std::optional<bool> {
        const sr3luahost::VintStoredProperty* sp = rawProp(h, name);
        if (!sp || sp->values.empty()) return std::nullopt;
        if (sp->values[0].kind == sr3luahost::VintTaggedValue::Kind::Boolean) return sp->values[0].boolean;
        if (sp->values[0].kind == sr3luahost::VintTaggedValue::Kind::Number) return sp->values[0].number != 0;
        return std::nullopt;
    };
    // `leading`/`kerning` setters (Sec14.2 table): signed byte, <= -127 ->
    // -127, anything >= 128 stored as 0x80 = -128. CHOSEN: a non-integral
    // file value is truncated toward zero first (the setter's own argument
    // conversion is not in the spec; shipped values are integers).
    auto signedByteSetter = [](double v) {
        if (!(v == v)) return 0; // NaN: CHOSEN 0 (not specified)
        if (v >= 128) return -128;
        if (v <= -127) return -127;
        return static_cast<int>(std::trunc(v));
    };
    auto f32 = [](double v) { return static_cast<double>(static_cast<float>(v)); };

    auto drawText = [&](uint32_t h, const VintWalkState& s, const std::string& line, bool haveGeom, double px,
                        double py, const double M[4], double storedOx, double storedOy) {
        // Sec14.2 step 2: source text. spec-localization-format.md Sec6.1/
        // Sec7 item 2: a key-hash form (text_tag_crc, or text_tag set as a
        // number) resolves directly by hash with no raw-string fallback; a
        // STRING text_tag is tried as a localisation key first, falling
        // back to the raw string on a miss (Sec7 item 2: "for any non-key
        // text_tag string... the raw string is drawn").
        const sr3luahost::VintStoredProperty* tagSp = rawProp(h, "text_tag");
        const sr3luahost::VintStoredProperty* crcSp = rawProp(h, "text_tag_crc");
        const bool hashForm = crcSp || (tagSp && !tagSp->values.empty() &&
                                        tagSp->values[0].kind == sr3luahost::VintTaggedValue::Kind::Number);
        std::optional<std::u16string> src;
        if (hashForm) {
            const double raw = crcSp ? (crcSp->values.empty() ? 0.0 : crcSp->values[0].number)
                                      : tagSp->values[0].number;
            const uint32_t hash = static_cast<uint32_t>(static_cast<int64_t>(raw));
            src = locLookup.lookup(hash);
            if (!src) {
                ++textNoSource;
                std::printf("[elem] %s  text: key-hash text_tag/text_tag_crc 0x%08x not found in any loaded "
                            "localisation table -> no source text, nothing drawn (Sec6.1; never exercised by any "
                            "shipped document, Sec8)\n",
                            line.c_str(), hash);
                return;
            }
            ++textKeyResolved;
        } else if (!tagSp || tagSp->values.empty() || tagSp->values[0].kind != sr3luahost::VintTaggedValue::Kind::String) {
            ++textNoSource;
            std::printf("[elem] %s  text: no text_tag -> no source text, stream freed, nothing drawn (Sec14.2 step 2)\n",
                        line.c_str());
            return;
        } else {
            const std::string rawTag = tagSp->values[0].text;
            src = locLookup.lookupByKey(rawTag);
            if (src) {
                ++textKeyResolved;
            } else {
                std::string why;
                src = sr3font::decodeRawTextTag(rawTag, why);
                if (!src) {
                    ++textOpen;
                    std::printf("[elem] %s  text: OPEN - %s -> not drawn\n", line.c_str(), why.c_str());
                    return;
                }
            }
        }
        // Sec14.2 step 3: insertion. insert_values never set -> count 0.
        const sr3luahost::VintStoredProperty* ivSp = rawProp(h, "insert_values");
        std::vector<sr3font::InsertValue> ivs;
        if (ivSp) {
            for (const auto& v : ivSp->values) {
                sr3font::InsertValue iv;
                switch (v.kind) {
                    case sr3luahost::VintTaggedValue::Kind::String:
                        iv.kind = sr3font::InsertValue::Kind::String;
                        for (char c : v.text) iv.text.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
                        break;
                    case sr3luahost::VintTaggedValue::Kind::Number:
                        iv.kind = sr3font::InsertValue::Kind::Number;
                        iv.number = v.number;
                        break;
                    case sr3luahost::VintTaggedValue::Kind::Boolean:
                        iv.kind = sr3font::InsertValue::Kind::Boolean;
                        iv.boolean = v.boolean;
                        break;
                    default: break; // Nil
                }
                ivs.push_back(iv);
            }
        }
        sr3font::FormatResult fr = sr3font::formatInsertions(*src, ivSp != nullptr, ivs, locLookup);
        if (!fr.open.empty()) {
            ++textOpen;
            std::printf("[elem] %s  text: OPEN - %s -> not drawn\n", line.c_str(), fr.open[0].c_str());
            return;
        }
        // Font (Sec12.2 / spec-vint-font-format.md Sec5): no font -> the
        // builder frees the stream and nothing draws (Sec14.2 step 1); an
        // unregistered name fails the acquire the same way.
        const sr3luahost::VintStoredProperty* fontSp = rawProp(h, "font");
        const std::string fontName = (fontSp && !fontSp->values.empty() &&
                                      fontSp->values[0].kind == sr3luahost::VintTaggedValue::Kind::String)
                                         ? fontSp->values[0].text
                                         : std::string();
        const int32_t entry = fontName.empty() ? -1 : sr3font::findNamedFont(fontName);
        if (entry < 0) {
            ++textNoFont;
            std::printf("[elem] %s  text: font '%s' %s -> no font, nothing drawn (Sec14.2 step 1)\n", line.c_str(),
                        fontName.c_str(), fontName.empty() ? "unset" : "not a registered name");
            return;
        }
        const int32_t measureId = sr3font::measureGameFont(entry); // primary only (Sec4.1)
        const VintGameFont& mf = gameFonts[static_cast<size_t>(measureId)];
        if (!mf.loaded) {
            ++textOpen;
            std::printf("[elem] %s  text: OPEN - measuring font (game id %d) not loaded -> not drawn\n", line.c_str(),
                        measureId);
            return;
        }
        // Layout parameters (Sec14.2 table + constructor defaults).
        sr3font::LayoutParams lp;
        if (auto ts = rawNums(h, "text_scale", 2)) { lp.textScaleX = f32((*ts)[0]); lp.textScaleY = f32((*ts)[1]); }
        const bool wordWrap = rawBool(h, "word_wrap").value_or(false);
        double wrapWidth = 300; // constructor default
        if (auto ww = rawNums(h, "wrap_width", 1)) {
            wrapWidth = (*ww)[0];
            if (wrapWidth < 20) wrapWidth = 20; // setter: below 20 -> 20 (NaN kept: the comparison is false)
            wrapWidth = f32(wrapWidth);
        }
        lp.wrapWidth = wordWrap ? wrapWidth : 0; // +0xbc
        int64_t align = 0;
        if (const auto* ha = rawProp(h, "horz_align"); ha && !ha->values.empty()) {
            if (ha->values[0].kind == sr3luahost::VintTaggedValue::Kind::Number) align = static_cast<int64_t>(ha->values[0].number);
            else if (ha->values[0].text == "center") align = 1;
            else if (ha->values[0].text == "right") align = 2;
        }
        // CHOSEN: an out-of-range horz_align value aligns left (the
        // placement pass's behaviour for it is not in the spec).
        lp.align = align == 1 ? sr3font::HorzAlign::Center : (align == 2 ? sr3font::HorzAlign::Right : sr3font::HorzAlign::Left);
        if (auto v = rawNums(h, "leading", 1)) lp.leading = signedByteSetter((*v)[0]);
        if (auto v = rawNums(h, "kerning", 1)) lp.kerning = signedByteSetter((*v)[0]);
        lp.font = entry;
        sr3font::LayoutMetrics lm;
        lm.lineHeight = sr3font::lineHeightMetric(mf.font);
        const sr3font::Font* mfont = &mf.font;
        lm.advance = [mfont](uint32_t ch, uint32_t next) { return sr3font::advanceForLayout(*mfont, ch, next); };
        lm.breakAnywhere = sr3font::breakAnywhereFlagDefaultArrangement();
        // [image:] looks names up in the bitmap-SHEET directory only (Sec14.4).
        sr3font::InlineImageLookup imageLookup = [&](const std::string& name) {
            sr3font::InlineImageInfo info;
            auto it = subByName.find(toLowerLocal(name));
            if (it != subByName.end()) {
                info.found = true;
                info.width = it->second.w;
                info.height = it->second.h;
            }
            return info;
        };
        const sr3font::LayoutResult lr = sr3font::layoutText(fr.text, lp, lm, imageLookup);
        textUnrecognizedTags += lr.unrecognizedTags;
        if (lr.truncated) ++textTruncated;
        // The measured size becomes unscaled_size and auto_offset is
        // re-applied from it (Sec12.2 / Sec5.4).
        const double W = f32(lr.width), H = f32(lr.height);
        double offx = storedOx, offy = storedOy;
        int64_t autoOffset = 0;
        if (const auto* ao = rawProp(h, "auto_offset"); ao && !ao->values.empty() &&
                                                       ao->values[0].kind == sr3luahost::VintTaggedValue::Kind::Number)
            autoOffset = static_cast<int64_t>(ao->values[0].number);
        switch (autoOffset) {
            case 1: offx = -W / 2; offy = -H / 2; break; // c
            case 2: offx = 0; offy = -H / 2; break;      // w
            case 3: offx = -W / 2; offy = 0; break;      // n
            case 4: offx = -W; offy = -H / 2; break;     // e
            case 5: offx = -W / 2; offy = -H; break;     // s
            case 6: offx = 0; offy = 0; break;           // nw
            case 7: offx = -W; offy = 0; break;          // ne
            case 8: offx = -W; offy = -H; break;         // se
            case 9: offx = 0; offy = -H; break;          // sw
            default: break;                              // none / out of range: offset untouched
        }
        offx = f32(offx);
        offy = f32(offy);
        std::string shown;
        for (char16_t c : fr.text) shown.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        if (!haveGeom) {
            std::printf("[elem] %s  text: '%s' laid out %sx%s but geometry OPEN -> not drawn\n", line.c_str(),
                        shown.c_str(), vintFmt(W).c_str(), vintFmt(H).c_str());
            return;
        }
        const double tox = px + M[0] * offx + M[1] * offy, toy = py + M[2] * offx + M[3] * offy;

        // The stream player (Sec12.2): runs -> glyph runs through the
        // drawing font(s) (secondary first, then primary if different -
        // Sec4.1 / Sec14.1), inline images -> the bitmap quad shape, colour
        // records -> the current colour (exactly -1 inherits the pass's base
        // component, anything else REPLACES it - Sec12.2 / Sec14.5).
        size_t glyphsThisElement = 0, imagesThisElement = 0;
        auto play = [&](double oX, double oY, const double base[4], bool colourCommands) {
            double cur[4] = {base[0], base[1], base[2], base[3]};
            auto packCur = [&](uint8_t rgba[4]) {
                for (int i = 0; i < 4; ++i) rgba[i] = vintPackComponent(cur[i]);
                if (srgbColour) for (int i = 0; i < 3; ++i) rgba[i] = vintSrgbDecodeByte(rgba[i]);
            };
            for (const sr3font::StreamRecord& rec : lr.records) {
                if (rec.op == sr3font::StreamRecord::Op::Colour) {
                    if (!colourCommands) continue; // the shadow pass suppresses colour records
                    const double comp[4] = {rec.r, rec.g, rec.b, rec.a};
                    for (int i = 0; i < 4; ++i) cur[i] = comp[i] == -1.0 ? base[i] : comp[i];
                    continue;
                }
                // G = M.S(sx,sy); placement origin = o + M.(x,y)
                const double G[4] = {M[0] * rec.sx, M[1] * rec.sy, M[2] * rec.sx, M[3] * rec.sy};
                const double pX = oX + M[0] * rec.x + M[1] * rec.y, pY = oY + M[2] * rec.x + M[3] * rec.y;
                auto dev = [&](double gx, double gy, double out[2]) {
                    out[0] = pX + G[0] * gx + G[1] * gy;
                    out[1] = pY + G[2] * gx + G[3] * gy;
                };
                uint8_t rgba[4];
                packCur(rgba);
                if (rec.op == sr3font::StreamRecord::Op::Image) {
                    if (!rec.image.found) continue; // sheet index -1: the player skips it
                    VintImageEntry& e = lookupImage(rec.imageName);
                    if (!e.srv || e.mode != 1) continue; // texture handle -1 (not drawn)
                    const double ext[2] = {rec.image.width, rec.image.height};
                    const double Tw = e.texW, Th = e.texH;
                    const double u0 = e.pageX / Tw, u1 = u0 + ext[0] / Tw, v0 = e.pageY / Th, v1 = v0 + ext[1] / Th;
                    double pos[4][2];
                    dev(0, 0, pos[0]);
                    dev(ext[0], 0, pos[1]);
                    dev(0, ext[1], pos[2]);
                    dev(ext[0], ext[1], pos[3]);
                    const double uv[4][2] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
                    pipe.quad(device.context(), e.srv, pos, uv, rgba, s.renderMode, e.texW, e.texH);
                    ++imagesThisElement;
                    continue;
                }
                for (int32_t gid : sr3font::drawGameFonts(entry)) {
                    const VintGameFont& df = gameFonts[static_cast<size_t>(gid)];
                    if (!df.loaded || !df.tex.srv) { textGlyphsNoTexture += rec.text.size(); continue; }
                    // spacing: only the low byte is written, read back signed (Sec12.2/12.4)
                    const int spacing = static_cast<int8_t>(static_cast<uint8_t>(rec.spacing & 0xff));
                    const std::vector<sr3font::GlyphQuad> qs = sr3font::layoutGlyphRun(
                        df.font, rec.text, rec.text.size(), spacing, df.tex.texW, df.tex.texH);
                    for (const sr3font::GlyphQuad& q : qs) {
                        if (!q.visible()) continue; // zero-area quads draw nothing
                        double pos[4][2];
                        dev(q.x0, q.y0, pos[0]);
                        dev(q.x1, q.y0, pos[1]);
                        dev(q.x0, q.y1, pos[2]);
                        dev(q.x1, q.y1, pos[3]);
                        const double uv[4][2] = {{q.u0, q.v0}, {q.u1, q.v0}, {q.u0, q.v1}, {q.u1, q.v1}};
                        pipe.quad(device.context(), df.tex.srv, pos, uv, rgba, s.renderMode, df.tex.texW, df.tex.texH);
                        ++glyphsThisElement;
                    }
                }
            }
        };
        // Shadow pass (Sec12.2), only if shadow_enabled: colour = shadow_tint
        // x state tint, alpha = shadow_alpha x state alpha, origin + shadow_
        // offset in DEVICE pixels (after the whole transform chain).
        const bool shadow = rawBool(h, "shadow_enabled").value_or(false);
        if (shadow) {
            const std::vector<double> st = rawNums(h, "shadow_tint", 3).value_or(std::vector<double>{0, 0, 0});
            const std::vector<double> so = rawNums(h, "shadow_offset", 2).value_or(std::vector<double>{0, 0});
            const double sa = rawNums(h, "shadow_alpha", 1).value_or(std::vector<double>{1.0})[0];
            const double base[4] = {st[0] * s.tint[0], st[1] * s.tint[1], st[2] * s.tint[2], sa * s.alpha};
            play(tox + so[0], toy + so[1], base, false);
        }
        const double base[4] = {s.tint[0], s.tint[1], s.tint[2], s.alpha};
        play(tox, toy, base, true);
        ++textDrawn;
        textGlyphs += glyphsThisElement;
        textInlineImages += imagesThisElement;
        std::string fontsUsed;
        for (int32_t gid : sr3font::drawGameFonts(entry))
            fontsUsed += (fontsUsed.empty() ? "" : "+") + std::string(sr3font::gameFontFileStem(gid));
        std::printf("[elem] %s  text: DRAW '%s' font=%s (entry %d, drawn with %s) scale (%s,%s) wrap %s align %lld "
                    "leading %d kerning %d -> size %sx%s, %zu line(s), %zu record(s), %zu stream bytes; auto_offset %lld "
                    "offset (%s,%s) origin (%s,%s)%s; glyph quads %zu, inline images %zu\n",
                    line.c_str(), shown.c_str(), fontName.c_str(), entry, fontsUsed.c_str(),
                    vintFmt(lp.textScaleX).c_str(), vintFmt(lp.textScaleY).c_str(), vintFmt(lp.wrapWidth).c_str(),
                    static_cast<long long>(align), lp.leading, lp.kerning, vintFmt(W).c_str(), vintFmt(H).c_str(),
                    lr.lines.size(), lr.records.size(), lr.streamBytes, static_cast<long long>(autoOffset),
                    vintFmt(offx).c_str(), vintFmt(offy).c_str(), vintFmt(tox).c_str(), vintFmt(toy).c_str(),
                    shadow ? " +shadow pass" : "", glyphsThisElement, imagesThisElement);
        for (const sr3font::StreamRecord& rec : lr.records) {
            if (rec.op == sr3font::StreamRecord::Op::Colour) {
                std::printf("         [colour] (%s,%s,%s,%s)\n", vintFmt(rec.r).c_str(), vintFmt(rec.g).c_str(),
                            vintFmt(rec.b).c_str(), vintFmt(rec.a).c_str());
            } else if (rec.op == sr3font::StreamRecord::Op::Image) {
                std::printf("         [image] '%s' %s at (%s,%s) scale (%s,%s)\n", rec.imageName.c_str(),
                            rec.image.found ? "sheet" : "UNKNOWN (sheet index -1, skipped)", vintFmt(rec.x).c_str(),
                            vintFmt(rec.y).c_str(), vintFmt(rec.sx).c_str(), vintFmt(rec.sy).c_str());
            } else {
                std::string t;
                for (char16_t c : rec.text) t.push_back(c < 0x80 ? static_cast<char>(c) : '?');
                std::printf("         [run] '%s' at (%s,%s) scale (%s,%s) spacing %d line %d\n", t.c_str(),
                            vintFmt(rec.x).c_str(), vintFmt(rec.y).c_str(), vintFmt(rec.sx).c_str(),
                            vintFmt(rec.sy).c_str(), rec.spacing, rec.line);
            }
        }
    };

    auto sortChildren = [&](std::vector<uint32_t>& kids) {
        auto classKey = [&](uint32_t h) {
            const sr3luahost::VdoObject* o = es.vdoObject(h);
            int k = vintBool(es, h, "mask").value_or(false) ? 1 : 0;
            if (o && o->typeName == "clip") k += 2;
            return k;
        };
        auto depthOf = [&](uint32_t h) {
            auto d = vintNums(es, h, "depth", 1);
            return d ? static_cast<int64_t>((*d)[0]) : 0;
        };
        std::stable_sort(kids.begin(), kids.end(), [&](uint32_t a, uint32_t b) {
            const int ka = classKey(a), kb = classKey(b);
            if (ka != kb) return ka > kb;
            const int64_t da = depthOf(a), db = depthOf(b);
            if (da != db) return da > db;
            return es.vdoObject(a)->nameHash < es.vdoObject(b)->nameHash;
        });
    };

    std::function<void(uint32_t, const VintWalkState&, int, size_t)> walk =
        [&](uint32_t h, const VintWalkState& parent, int depth, size_t capacity) {
            const sr3luahost::VdoObject* o = es.vdoObject(h);
            ++visited;
            std::string line = std::string(static_cast<size_t>(depth) * 2, ' ') + o->name + " : " + o->typeName;
            // Sec3 cull.
            const bool visible = vintBool(es, h, "visible").value_or(true);
            auto ownAlphaV = vintNums(es, h, "alpha", 1);
            const double ownAlpha = ownAlphaV ? (*ownAlphaV)[0] : 1.0;
            if (!visible || parent.alpha <= FLT_EPSILON || ownAlpha <= FLT_EPSILON) {
                ++culled;
                std::printf("[elem] %s  CULLED (%s)\n", line.c_str(),
                            !visible ? "visible=false" : (parent.alpha <= FLT_EPSILON ? "parent alpha" : "own alpha"));
                return;
            }
            // Sec4.1 compose.
            VintWalkState s = parent;
            if (auto t = vintNums(es, h, "tint", 3)) for (int i = 0; i < 3; ++i) s.tint[i] = parent.tint[i] * (*t)[i];
            s.alpha = parent.alpha * ownAlpha;
            if (auto rm = vintNums(es, h, "render_mode", 1)) if ((*rm)[0] != 0) s.renderMode = static_cast<int64_t>((*rm)[0]);
            s.inMask = parent.inMask || vintBool(es, h, "mask").value_or(false);
            s.inBackground = parent.inBackground || vintBool(es, h, "background").value_or(false);
            if (s.renderMode != 1) ++nonFilterModes;
            if (s.inBackground) ++backgroundElems;

            // Geometry: query-time chain (Sec7.1) -> device via the fit (Sec5.1).
            bool haveGeom = false;
            double px = 0, py = 0, M[4] = {1, 0, 0, 1}, ox = 0, oy = 0, offx = 0, offy = 0;
            try {
                sr3luahost::EngineState::VintQueryTransform t = es.vintQueryTransform(h);
                px = fitOffX + fitScale * t.px;
                py = fitOffY + fitScale * t.py;
                for (int i = 0; i < 4; ++i) M[i] = fitScale * t.m[i];
                auto off = vintNums(es, h, "offset", 2);
                offx = off ? (*off)[0] : 0.0;
                offy = off ? (*off)[1] : 0.0;
                ox = px + M[0] * offx + M[1] * offy;
                oy = py + M[2] * offx + M[3] * offy;
                haveGeom = true;
            } catch (const sr3luahost::OpenStateError&) {
                ++geometryOpen;
            }
            std::string box;
            try {
                auto b = es.vintQueryBox(h);
                box = b ? "screen_nw (" + vintFmt(b->minX) + "," + vintFmt(b->minY) + ") screen_se (" + vintFmt(b->maxX) +
                              "," + vintFmt(b->maxY) + ")"
                        : "screen box: childless group (point, OPEN)";
            } catch (const sr3luahost::OpenStateError&) {
                box = "screen box OPEN";
            }

            // Pass 0 draws every type (Sec2.3 step 5); per-type draw (Sec12).
            // UPDATED 2026-10-03 (Sec15.6, U6): mask subtrees are now REALLY
            // drawn, through the real stencil state the sibling loop below
            // binds before recursing here (write-mode for the mask element
            // itself, test-mode for its masked siblings) - no more blanket
            // skip. maskSkipped keeps the same population it always counted
            // (every element under a mask, write or test) for the summary
            // line's own continuity, despite no longer meaning "skipped."
            const std::string& type = o->typeName;
            if (s.inMask) ++maskSkipped;
            if (type == "group") {
                ++groupsVisited;
                std::printf("[elem] %s  group: draws nothing (Sec12.3)  %s\n", line.c_str(), box.c_str());
            } else if (type == "bitmap") {
                const std::string img = vintStr(es, h, "image");
                const VintImageEntry* e = nullptr;
                if (!img.empty()) {
                    auto it = images.find(toLowerLocal(img));
                    if (it != images.end() && it->second.sized) e = &it->second;
                }
                if (!e) {
                    ++bitmapsNoImage;
                    std::printf("[elem] %s  bitmap: image '%s' not in the pool -> no draw (Sec12.1)\n", line.c_str(), img.c_str());
                } else if (!e->srv) {
                    ++bitmapsNoTexture;
                    std::printf("[elem] %s  bitmap: image '%s' texture handle -1 -> no draw (Sec12.1)\n", line.c_str(), img.c_str());
                } else if (!haveGeom) {
                    std::printf("[elem] %s  bitmap: geometry OPEN -> not drawn\n", line.c_str());
                } else {
                    auto nwV = vintNums(es, h, "source_nw", 2);
                    auto seV = vintNums(es, h, "source_se", 2);
                    std::vector<double> nwd = nwV ? *nwV : std::vector<double>{0, 0};
                    std::vector<double> sed = seV ? *seV : std::vector<double>{e->imgW, e->imgH};
                    int32_t nw[2], se[2];
                    for (int i = 0; i < 2; ++i) {
                        if (std::trunc(nwd[i]) != nwd[i] || std::trunc(sed[i]) != sed[i]) ++nonIntegralSource;
                        nw[i] = static_cast<int32_t>(nwd[i]);
                        se[i] = static_cast<int32_t>(sed[i]);
                    }
                    uint8_t rgba[4] = {vintPackComponent(s.tint[0]), vintPackComponent(s.tint[1]),
                                       vintPackComponent(s.tint[2]), vintPackComponent(s.alpha)};
                    if (srgbColour) for (int i = 0; i < 3; ++i) rgba[i] = vintSrgbDecodeByte(rgba[i]);
                    const double Tw = e->texW, Th = e->texH;
                    const double u0 = (e->pageX + nw[0]) / Tw, u1 = u0 + se[0] / Tw;
                    const double v0 = (e->pageY + nw[1]) / Th, v1 = v0 + se[1] / Th;
                    const double ex[2] = {M[0] * se[0], M[2] * se[0]}; // M*(se.x,0)
                    const double ey[2] = {M[1] * se[1], M[3] * se[1]}; // M*(0,se.y)
                    const double pos[4][2] = {{ox, oy}, {ox + ex[0], oy + ex[1]}, {ox + ey[0], oy + ey[1]},
                                              {ox + ex[0] + ey[0], oy + ex[1] + ey[1]}};
                    const double uv[4][2] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
                    pipe.quad(device.context(), e->srv, pos, uv, rgba, s.renderMode, static_cast<uint32_t>(e->texW),
                              static_cast<uint32_t>(e->texH));
                    ++bitmapsDrawn;
                    std::printf("[elem] %s  DRAW image=%s nw=(%d,%d) se=(%d,%d) colour=(%u,%u,%u,%u) quad (%s,%s) (%s,%s) "
                                "(%s,%s) (%s,%s)  %s\n",
                                line.c_str(), img.c_str(), nw[0], nw[1], se[0], se[1], rgba[0], rgba[1], rgba[2], rgba[3],
                                vintFmt(pos[0][0]).c_str(), vintFmt(pos[0][1]).c_str(), vintFmt(pos[1][0]).c_str(),
                                vintFmt(pos[1][1]).c_str(), vintFmt(pos[2][0]).c_str(), vintFmt(pos[2][1]).c_str(),
                                vintFmt(pos[3][0]).c_str(), vintFmt(pos[3][1]).c_str(), box.c_str());
                }
            } else if (type == "text") {
                if (textMode == TextMode::Real) {
                    drawText(h, s, line, haveGeom, px, py, M, offx, offy);
                } else {
                    // The pre-U5 PLACEHOLDER path, kept as an explicit opt-in
                    // (see TextMode's own comment): a magenta outline (24x12
                    // device px, CHOSEN) at the composed origin with the
                    // STORED offset (no layout, so no auto_offset re-apply).
                    const bool marker = textMode == TextMode::Placeholder;
                    if (marker && haveGeom) {
                        const double w = 24, hgt = 12;
                        pipe.rect(device.context(), ox, oy, ox + w, oy + 1, kPlaceholder);
                        pipe.rect(device.context(), ox, oy + hgt - 1, ox + w, oy + hgt, kPlaceholder);
                        pipe.rect(device.context(), ox, oy, ox + 1, oy + hgt, kPlaceholder);
                        pipe.rect(device.context(), ox + w - 1, oy, ox + w, oy + hgt, kPlaceholder);
                        ++textMarkers;
                    }
                    std::printf("[elem] %s  text: PLACEHOLDER %s at origin (%s,%s) text_tag='%s' (real text not drawn)\n",
                                line.c_str(), marker ? "marker" : "(markers off)", vintFmt(ox).c_str(),
                                vintFmt(oy).c_str(), vintStr(es, h, "text_tag").c_str());
                }
            } else {
                ++notDrawnByType[type];
                std::printf("[elem] %s  NOT DRAWN (draw unit not specced for this type)  %s\n", line.c_str(), box.c_str());
            }

            // Children: is-an-element only, capacity-bounded, sorted (Sec2.3 steps 6-8).
            std::vector<uint32_t> kids;
            for (uint32_t c = o->firstChildHandle; c != 0;) {
                const sr3luahost::VdoObject* co = es.vdoObject(c);
                if (!co) break;
                if (sr3luahost::findVintElementType(co->typeName) && sr3luahost::vintIsElementFamily(co->typeName)) {
                    if (kids.size() < capacity) kids.push_back(c);
                    else ++scratchDropped;
                }
                c = co->nextSiblingHandle;
            }
            if (kids.size() > 1) sortChildren(kids);
            // Sec11's own mode state machine, walking these sorted siblings
            // (mask children sort first, see sortChildren's own classKey):
            // a mask child in mode 0 fires "mask begin" (stencil clear) and
            // sets mode 2 (write); the first non-mask child seen in mode 2
            // sets mode 1 (test); mode resets to 0 once this loop ends. A
            // loop already running inside an ancestor's mask subtree
            // (s.inMask already true) does none of this - it inherits
            // whatever mode is already active and never switches again
            // (the same "nested loops below a mask child do no further
            // mode switching" finding U4 made for the clip-stack unwind).
            {
                int mode = s.inMask ? s.activeMaskMode : 0;
                const bool ownLoop = !s.inMask;
                for (uint32_t c : kids) {
                    VintWalkState childParent = s;
                    if (ownLoop) {
                        const bool childIsMask = vintBool(es, c, "mask").value_or(false);
                        if (childIsMask && mode == 0) {
                            pipe.clearMaskStencil(device.context());
                            mode = 2;
                        } else if (!childIsMask && mode == 2) {
                            mode = 1;
                        }
                        childParent.activeMaskMode = mode;
                        pipe.setMaskMode(device.context(), mode);
                    }
                    walk(c, childParent, depth + 1, capacity - kids.size());
                }
                if (ownLoop && mode != 0) pipe.setMaskMode(device.context(), 0);
            }
        };

    std::vector<uint32_t> roots;
    for (uint32_t h : ld->elementHandles) {
        const sr3luahost::VdoObject* o = es.vdoObject(h);
        if (o && sr3luahost::findVintElementType(o->typeName) && sr3luahost::vintIsElementFamily(o->typeName)) roots.push_back(h);
    }
    if (roots.size() > 1) sortChildren(roots);
    VintWalkState rootState;
    // Same Sec11 mode state machine as the inner sibling loop above - the
    // document's own root-level elements are one more "parent's sorted
    // children" loop, never themselves already inside a mask (rootState's
    // own inMask is always false), so this is always the loop's own owner.
    {
        int mode = 0;
        for (uint32_t h : roots) {
            VintWalkState childParent = rootState;
            const bool childIsMask = vintBool(es, h, "mask").value_or(false);
            if (childIsMask && mode == 0) {
                pipe.clearMaskStencil(device.context());
                mode = 2;
            } else if (!childIsMask && mode == 2) {
                mode = 1;
            }
            childParent.activeMaskMode = mode;
            pipe.setMaskMode(device.context(), mode);
            walk(h, childParent, 0, 256 - roots.size());
        }
        if (mode != 0) pipe.setMaskMode(device.context(), 0);
    }

    std::printf("summary    : %zu elements visited, %zu culled; bitmaps drawn %zu, no image %zu, no texture %zu; "
                "groups (empty draw) %zu; text PLACEHOLDER markers %zu; inside mask subtrees (real stencil mask "
                "now, Sec15.6) %zu\n",
                visited, culled, bitmapsDrawn, bitmapsNoImage, bitmapsNoTexture, groupsVisited, textMarkers, maskSkipped);
    if (textMode == TextMode::Real) {
        std::printf("summary    : text (Sec14, U5) drawn %zu element(s), %zu glyph quads, %zu inline images; "
                    "OPEN %zu, no font %zu, no source text %zu; glyphs skipped for a missing font texture %zu; "
                    "unrecognized tags %zu; stream truncations %zu; resolved via a real localisation key %zu "
                    "(spec-localization-format.md)\n",
                    textDrawn, textGlyphs, textInlineImages, textOpen, textNoFont, textNoSource, textGlyphsNoTexture,
                    textUnrecognizedTags, textTruncated, textKeyResolved);
    }
    std::printf("summary    : non-filter render_mode elements (real distinct blend codes, Sec15.4) %zu; "
                "background-flag elements %zu; "
                "geometry OPEN %zu; scratch-capacity drops %zu; non-integral source coords %zu\n",
                nonFilterModes, backgroundElems, geometryOpen, scratchDropped, nonIntegralSource);
    std::printf("not drawn  :");
    if (notDrawnByType.empty()) std::printf(" none");
    for (const auto& kv : notDrawnByType) std::printf(" %s=%zu", kv.first.c_str(), kv.second);
    std::printf("\n");

    std::vector<uint8_t> pixels;
    if (!device.readBack(pixels, error)) { std::fprintf(stderr, "readback failed: %s\n", error.c_str()); return 1; }
    for (auto& a : texArchives)
        for (auto& kv : a->texCache.uploadedByWantedName)
            sr3render::releaseSrv(kv.second.srv);
    if (!sr3render::writePng(outPath, device.width(), device.height(), pixels, error)) {
        std::fprintf(stderr, "png write failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("wrote      : %s\n", outPath.c_str());
    return 0;
}
