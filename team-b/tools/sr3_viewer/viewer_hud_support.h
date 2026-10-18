#pragma once

#include "viewer_platform.h"
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

namespace sr3viewer {

// =========================================================================
// hud-live-demo (spec-hud-runtime.md, 2026-10-08 spec-sync task) - wires the
// 4 ALWAYS-ON core HUD stats (health/ammo/money/notoriety) into this
// project's own VINT UI renderer, driven by the REAL producer natives the
// spec traced, not hardcoded test values. See spec-hud-runtime.md Sec7 for
// the confidence summary this command's own stdout below follows exactly.
//
// WHY A NEW, FOCUSED OVERLAY, NOT THE REAL hud.vint_doc: spec-hud-runtime.md
// Sec1 is explicit and CONFIRMED (a full 15-real-C-engine-caller xref
// census) that NEITHER of this engine's two generic HUD-widget pools
// (hud_display_*/hud_bar_*) is wired to any of these 4 stats by any C-side
// code - every real caller of either pool is a transient minigame/QTE/
// diversion overlay. Which specific element of the REAL hud.vint_doc (if
// any) a Lua UI script writes health/ammo/money/notoriety into was NOT
// traced by that spec pass (Sec2/Sec4/Sec5's own "HIGH CONFIDENCE, not
// independently proven by tracing a render-side consumer" / "OPEN"
// verdicts) - naming a specific real element here would invent a fact the
// spec itself does not give. Instead, this command builds its OWN minimal
// document (4 `text` VdoObjects + 2 plain colour rects for the health bar)
// through this project's own real VINT property store
// (EngineState::registerTypedVdoObjectForTesting/setVintProperty - the
// exact same per-(object, name-hash) store and "offset" FieldType-coded
// property every loaded .vint_doc element uses) and this project's own real
// glyph pipeline (sr3font::formatInsertions/layoutText/layoutGlyphRun, the
// same calls runVintDoc()'s drawText() uses) - CHOSEN layout/graphics, REAL
// renderer mechanics.
//
// PER-STAT PRODUCER, CONFIRMED vs CHOSEN (see spec-hud-runtime.md for the
// full citations; this command's own stdout repeats the short form):
//  * Health (Sec2): get_current_hit_points/get_max_hit_points/set_current_
//    hit_points are REAL, already-implemented Lua natives, called here
//    through a real Host/lua_State exactly as a Lua UI script would -
//    CONFIRMED field (+0x1cb8/+0x1cac/+0x1cb0), HIGH-CONFIDENCE cadence
//    (polled every frame here - CHOSEN, the spec's own "exact cadence OPEN"
//    verdict, not a specific frame rate).
//  * Ammo (Sec3): inv_weapon_add_temporary (CONFIRMED writer) mutates the
//    real WeaponTemporaryInventorySlot::ammoCount this project already
//    tracks; game_hud_update_inventory (CONFIRMED event-driven trigger) is
//    the ONLY thing that makes this overlay re-read that field into its
//    displayed text - demonstrated on frame 3 (the live field changes,
//    display does NOT) vs frame 4 (the native fires, display catches up).
//  * Money (Sec4): cash_add (CONFIRMED writer) mutates playerCash(), the
//    live wallet value this project already tracks; the always-visible
//    counter here polls it every frame (CHOSEN - the spec's own "not
//    determined... not assumed identical to Sec2's/Sec3's pattern" verdict).
//  * Notoriety (Sec5): on_notoriety_event (CONFIRMED to exist) is called
//    once to prove the real hook registers; notoriety_set (CONFIRMED
//    writer) mutates NotorietyAliasTrackState::currentValue, which this
//    overlay polls every frame (CHOSEN hook-to-widget connection - the
//    spec's own consumer-is-OPEN verdict; Sec5's own cross-referenced
//    0x014A4F70 {level,fraction} record, per spec-ai-runtime.md Sec19, is a
//    DIFFERENT, not-yet-modeled field this project has no Lua-reachable
//    producer for in scope, so this command uses the writer/reader pair
//    this project ALREADY tracks instead of inventing a bridge to that
//    record).
//  * Minimap (Sec6): explicitly out of scope per the task brief.
// =========================================================================

// A trimmed-down VintGameFont - this command renders every label with ONE
// hardcoded font (game id 0, "font_body") rather than resolving a per-
// element `font` property/name the way runVintDoc()'s drawText() does
// (CHOSEN simplification: this overlay's own labels never set a `font`
// property at all).
struct HudDemoFont {
    bool loaded = false;
    sr3font::Font font;
    VintImageEntry tex;
};

void drawHudLabel(sr3luahost::EngineState& es, uint32_t handle, const HudDemoFont& bodyFont, VintQuadPipeline& pipe,
                  ID3D11DeviceContext* ctx, const uint8_t rgba[4]);

double readLuaGlobalNumber(lua_State* L, const char* name);

void tileFallbackColour(int col, int row, uint8_t rgba[4]);

void drawMinimapBackgroundQuad(ID3D11DeviceContext* ctx, VintQuadPipeline& pipe,
                               const sr3luahost::MinimapLiveTransform& t, double boxCx, double boxCy,
                               double boxHalfW, double boxHalfH, ID3D11ShaderResourceView* tileSrv,
                               uint32_t tileTexW, uint32_t tileTexH, const uint8_t tintOrFallback[4],
                               bool wasReal);

void drawMinimapPlayerMarker(ID3D11DeviceContext* ctx, VintQuadPipeline& pipe, double boxCx, double boxCy);

} // namespace sr3viewer
