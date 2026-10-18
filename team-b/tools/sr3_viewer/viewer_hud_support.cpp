#include "viewer_platform.h"
#include "viewer_hud_support.h"
#include "viewer_vint_quad.h"
#include "sr3luahost/character_damage_sink.h"
#include "sr3luahost/auto_combat.h"
#include "sr3luahost/engine_state.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3luahost/minimap_live_transform.h"
#include "sr3luahost/host.h"
#include "sr3luahost/menu_runtime.h"
#include "sr3luahost/stub_registry.h"
#include "sr3font/vint_font.h"
#include "sr3font/vint_text.h"
#include "sr3locstr/le_strings.h"

using namespace sr3viewer;

namespace sr3viewer {

// Minimal re-implementation of runVintDoc()'s drawText() for ONE hardcoded
// font and NO markup/wrap/shadow/[image:] support (this overlay's own
// literal strings need none of that) - same real calls
// (decodeRawTextTag/formatInsertions/layoutText/layoutGlyphRun), same quad
// draw (VintQuadPipeline::quad), same offset/vintQueryTransform positioning
// every loaded .vint_doc `text` element uses.
void drawHudLabel(sr3luahost::EngineState& es, uint32_t handle, const HudDemoFont& bodyFont, VintQuadPipeline& pipe,
                  ID3D11DeviceContext* ctx, const uint8_t rgba[4]) {
    const sr3luahost::VintStoredProperty* tagSp = es.findVintStoredProperty(handle, "text_tag");
    if (!tagSp || tagSp->values.empty() || tagSp->values[0].kind != sr3luahost::VintTaggedValue::Kind::String) return;
    std::string openReason;
    std::optional<std::u16string> src = sr3font::decodeRawTextTag(tagSp->values[0].text, openReason);
    if (!src) return; // OPEN (0x80-tagged form) - never produced by this command's own literal strings
    static const sr3locstr::LeStringsLookup kEmptyLookup; // no .le_strings loaded - every lookup misses by design
    sr3font::FormatResult fr = sr3font::formatInsertions(*src, false, {}, kEmptyLookup);
    if (!fr.open.empty()) return;

    sr3font::LayoutParams lp;
    lp.font = 0;
    sr3font::LayoutMetrics lm;
    lm.lineHeight = sr3font::lineHeightMetric(bodyFont.font);
    const sr3font::Font* fp = &bodyFont.font;
    lm.advance = [fp](uint32_t ch, uint32_t next) { return sr3font::advanceForLayout(*fp, ch, next); };
    lm.breakAnywhere = sr3font::breakAnywhereFlagDefaultArrangement();
    sr3font::InlineImageLookup noImages = [](const std::string&) { return sr3font::InlineImageInfo{}; };
    sr3font::LayoutResult lr = sr3font::layoutText(fr.text, lp, lm, noImages);

    sr3luahost::EngineState::VintQueryTransform t;
    try {
        t = es.vintQueryTransform(handle);
    } catch (const sr3luahost::OpenStateError&) {
        return; // never hit by this command's own parentless, always-seeded handles
    }
    const sr3luahost::VintStoredProperty* offSp = es.findVintStoredProperty(handle, "offset");
    double offx = 0, offy = 0;
    if (offSp && offSp->values.size() == 2) { offx = offSp->values[0].number; offy = offSp->values[1].number; }
    const double M[4] = {t.m[0], t.m[1], t.m[2], t.m[3]};
    const double oX = t.px + M[0] * offx + M[1] * offy, oY = t.py + M[2] * offx + M[3] * offy;

    for (const sr3font::StreamRecord& rec : lr.records) {
        if (rec.op != sr3font::StreamRecord::Op::Run) continue; // Colour/Image never emitted by plain, markup-free text
        const double G[4] = {M[0] * rec.sx, M[1] * rec.sy, M[2] * rec.sx, M[3] * rec.sy};
        const double pX = oX + M[0] * rec.x + M[1] * rec.y, pY = oY + M[2] * rec.x + M[3] * rec.y;
        auto dev = [&](double gx, double gy, double out[2]) {
            out[0] = pX + G[0] * gx + G[1] * gy;
            out[1] = pY + G[2] * gx + G[3] * gy;
        };
        const int spacing = static_cast<int8_t>(static_cast<uint8_t>(rec.spacing & 0xff));
        const std::vector<sr3font::GlyphQuad> qs = sr3font::layoutGlyphRun(bodyFont.font, rec.text, rec.text.size(),
                                                                           spacing, bodyFont.tex.texW, bodyFont.tex.texH);
        for (const sr3font::GlyphQuad& q : qs) {
            if (!q.visible()) continue;
            double pos[4][2];
            dev(q.x0, q.y0, pos[0]);
            dev(q.x1, q.y0, pos[1]);
            dev(q.x0, q.y1, pos[2]);
            dev(q.x1, q.y1, pos[3]);
            const double uv[4][2] = {{q.u0, q.v0}, {q.u1, q.v0}, {q.u0, q.v1}, {q.u1, q.v1}};
            pipe.quad(ctx, bodyFont.tex.srv, pos, uv, rgba, 1, bodyFont.tex.texW, bodyFont.tex.texH);
        }
    }
}


double readLuaGlobalNumber(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    const double v = lua_type(L, -1) == LUA_TNUMBER ? lua_tonumber(L, -1) : 0.0;
    lua_pop(L, 1);
    return v;
}


// =========================================================================
// minimap-demo (spec-hud-runtime.md Sec6/Sec6.4, 2026-10-08 minimap live-
// transform task) - a focused, scripted/deterministic demo of the corner
// minimap's own live background transform, modeled on hud-live-demo()'s own
// architecture immediately above (same offscreen RenderDevice/
// VintQuadPipeline/HudDemoFont/drawHudLabel machinery, same "N captured
// PNG frames with a printed CONFIRMED/CHOSEN breakdown" shape) rather than
// hooking into runPlaytest()'s own live WASD loop - a scripted sequence of
// world positions/headings is sufficient to prove the mechanism and stays
// fully deterministic/headless, matching this project's own existing
// offscreen-demo convention.
//
// PER-FACT, CONFIRMED vs CHOSEN (see spec-hud-runtime.md Sec6/Sec6.4 for the
// full citations; this command's own stdout repeats the short form):
//  * zoom/map_mode (Sec6): sr2_map's own 2 real instance properties -
//    CONFIRMED float/this+0xa0 and enum/this+0xa4 - round-trip through the
//    EXISTING typed VINT property store (EngineState::setVintProperty/
//    getVintProperty, vint_property_types.cpp's "sr2_map" row, zoom's own
//    code corrected from Open to Spec/kVintCodeFloat by this same task).
//  * Live map-centre / heading-gated rotation (Sec6.4): CONFIRMED-mechanism
//    - include/sr3luahost/minimap_live_transform.h's updateMinimapLiveTransform(),
//    reproduced faithfully (player position, vehicle-seated fallback, the
//    save-format "static minimap" option gate - spec-save-format.md
//    Sec7.5).
//  * The player's own marker glyph (Sec6.4's single most important
//    confirmed BEHAVIOURAL fact): drawn at the minimap box's own fixed
//    geometric centre EVERY frame, never a function of world position/
//    heading - NOT a pooled/addable blip (Sec6.1's shared blip pool was
//    exhaustively searched and ruled out). The literal draw instruction
//    itself was not traced (Sec6.4's flagged OPEN gap) - CHOSEN stand-in,
//    labelled as such in this command's own stdout.
//  * Base map tile texture bind (Sec6.3's own OPEN gap): CHOSEN - resolves
//    the live map-centre's own city tile via cityTileColRowForWorld()
//    (the EXACT algebraic inverse of this file's own existing
//    cityTileCentre(), not a new invented mechanism) and attempts
//    findAndUploadTexture() against every --textures archive given for a
//    "minimap_CCRR" record (spec-world-streaming.md Sec5's own confirmed
//    naming). When no archive is given, or none contains that record, this
//    command falls back to a flat, per-tile deterministic placeholder
//    colour (CHOSEN, explicitly logged as a fallback, never silently
//    presented as real texture content) - still enough to visually prove a
//    DIFFERENT tile gets resolved/bound as the player crosses a tile
//    boundary (frame 4 below).
//  * Visual "pan/rotate while the marker stays fixed" proof: 4 small
//    CHOSEN "landmark" markers at fixed world positions are drawn through
//    the SAME world->minimap-screen transform as the background box, so
//    their on-screen motion between frames is the visible proof of pan/
//    rotate, independent of whether a real tile texture was found.
//  * NOT reproduced this pass (noted, not silently dropped): sr2_map's own
//    CONFIRMED draw-slot mask primitive (Sec6: a circle in minimap mode
//    sized by zoom, a rect border in pause_map mode) and any clipping to
//    the minimap's own round viewport - landmark/background quads may
//    visually extend past the box outline in some frames; this does not
//    affect any of the facts verified above, only cosmetic framing.
// =========================================================================

// A simple, deterministic, CHOSEN placeholder colour per city tile (col,
// row) - used only when no real `minimap_CCRR` texture record was found in
// any given --textures archive (see this command's own top doc comment).
// Visually distinct between tiles so a tile-crossing frame is obviously a
// different colour, never mistaken for real art.
void tileFallbackColour(int col, int row, uint8_t rgba[4]) {
    const uint32_t h = static_cast<uint32_t>(col) * 73856093u ^ static_cast<uint32_t>(row) * 19349663u;
    rgba[0] = static_cast<uint8_t>(50 + (h & 0x6f));
    rgba[1] = static_cast<uint8_t>(50 + ((h >> 7) & 0x6f));
    rgba[2] = static_cast<uint8_t>(50 + ((h >> 14) & 0x6f));
    rgba[3] = 255;
}


// Shared minimap background-quad + fixed-centre player-marker draw (v0.3
// polish task, 2026-10-09: "wire the minimap onto the live playtest HUD"),
// factored out of runMinimapDemo()'s own renderFrame() lambda below (split
// in two, not one combined call, so that lambda can still draw its own
// demo-only landmark markers BETWEEN the background and the player marker,
// unchanged draw order) so runPlaytest()'s live --hud overlay can draw the
// SAME two primitives every frame without duplicating them. runPlaytest()
// calls both back-to-back with no landmarks in between (it has none).
//
// drawMinimapBackgroundQuad() is deliberately narrower than renderFrame()'s
// own former inline block: no real `minimap_CCRR` texture resolution
// attempt here - that stays demo-only (resolveTile() needs a per-demo
// --textures archive list runPlaytest() has no equivalent of for this task;
// see that function's own "MINIMAP OVERLAY" block for the CHOSEN flat-
// colour-only scope cut this implies there). tileSrv may be null (flat
// `tintOrFallback` colour quad, the only mode runPlaytest() currently uses).
void drawMinimapBackgroundQuad(ID3D11DeviceContext* ctx, VintQuadPipeline& pipe,
                               const sr3luahost::MinimapLiveTransform& t, double boxCx, double boxCy,
                               double boxHalfW, double boxHalfH, ID3D11ShaderResourceView* tileSrv,
                               uint32_t tileTexW, uint32_t tileTexH, const uint8_t tintOrFallback[4],
                               bool wasReal) {
    // The whole box, rotated about its own centre by -backgroundRotationRad
    // (the SAME heading-up convention worldToMinimapScreen() above uses for
    // per-point world->screen math).
    const double c = std::cos(-t.backgroundRotationRad), s = std::sin(-t.backgroundRotationRad);
    auto rot = [&](double lx, double ly, double out[2]) {
        out[0] = boxCx + lx * c - ly * s;
        out[1] = boxCy + lx * s + ly * c;
    };
    double pos[4][2];
    rot(-boxHalfW, -boxHalfH, pos[0]);
    rot(boxHalfW, -boxHalfH, pos[1]);
    rot(-boxHalfW, boxHalfH, pos[2]);
    rot(boxHalfW, boxHalfH, pos[3]);
    const double uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    const uint8_t white[4] = {255, 255, 255, 255};
    pipe.quad(ctx, tileSrv, pos, uv, wasReal ? white : tintOrFallback, 1, tileTexW, tileTexH);
}


// The player's own marker glyph (spec-hud-runtime.md Sec6.4's single most
// important confirmed fact): the minimap box's own fixed geometric centre,
// EVERY frame - NEVER a function of `t`/world position/heading. See
// include/sr3luahost/minimap_live_transform.h's own top comment for why
// kPlayerMarkerScreenOffsetX/Y are a CHOSEN stand-in, not a traced fact.
void drawMinimapPlayerMarker(ID3D11DeviceContext* ctx, VintQuadPipeline& pipe, double boxCx, double boxCy) {
    const uint8_t markerRgba[4] = {255, 255, 255, 255};
    pipe.rect(ctx, boxCx + sr3luahost::kPlayerMarkerScreenOffsetX - 5,
             boxCy + sr3luahost::kPlayerMarkerScreenOffsetY - 5,
             boxCx + sr3luahost::kPlayerMarkerScreenOffsetX + 5,
             boxCy + sr3luahost::kPlayerMarkerScreenOffsetY + 5, markerRgba);
}

} // namespace sr3viewer