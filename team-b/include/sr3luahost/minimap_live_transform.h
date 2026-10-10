// Minimap (sr2_map) live background transform - spec-hud-runtime.md Sec6/
// Sec6.4 (2026-10-08 Team A pass, "minimap live data feed traced"). This
// header models ONLY the CONFIRMED-mechanism half of that section: the
// per-frame, indirectly-dispatched corner-minimap update entry point
// (0x00809910, reached through a function-pointer slot the spec traces to
// 0x02317a6c/0x00848c07, structurally separate from the pause map's own
// per-frame chain, Sec6.2) resolves the local player, reads the player's
// own world position (falling back to a linked vehicle's position if the
// player is seated) into a live "map-centre" pair, and reads the save-
// format "static minimap" option (spec-save-format.md Sec7.5, byte
// `+0x21`/`0x2025`, modeled on-disk by this project at
// include/sr3save/save_profile.h's ProfileOptions::staticMinimap) three
// times to gate (a) a live-heading read versus a fixed/zeroed orientation
// and (b) whether a computed rotation is applied to the on-screen draw-
// position math at all - both gated reads collapse to the SAME net
// behaviour (no live rotation while static), modeled here as one bool.
//
// Deliberately NOT modeled here: sr2_map's own 2 real instance properties
// (zoom/map_mode - spec-hud-runtime.md Sec6, modeled instead through this
// project's EXISTING name-hash VINT property store,
// EngineState::setVintProperty/getVintProperty +
// include/sr3luahost/vint_property_types.h's "sr2_map" descriptor row,
// corrected to Spec/kVintCodeFloat for zoom by this same task) and the
// player's own on-screen marker position - Sec6.4's own single most
// important confirmed BEHAVIOURAL fact is that the corner minimap's
// on-screen transform centres the view ON the player's position BY
// CONSTRUCTION, so the player's own marker glyph sits at a screen point
// that NEVER changes, regardless of this header's own
// MinimapLiveTransform output. That fixed point is therefore NOT a
// function of world position/heading at all and has no business living in
// this (pure world-space) transform - see kPlayerMarkerScreenOffsetX/Y
// below, which are deliberately plain constants, not derived from
// anything in MinimapLiveInput/MinimapLiveTransform.
//
// The literal "draw the player's own fixed glyph at the centre point"
// instruction itself was NOT traced (Sec6.4's own flagged OPEN gap, most
// plausibly inside 0x00809910's own tail or one of 5 uncalled helper
// functions) - kPlayerMarkerScreenOffsetX/Y is this project's own CHOSEN
// stand-in for that untraced draw call, not a reproduction of a traced
// fact.
#pragma once

#include <cstdint>

namespace sr3luahost {

// sr2_map.map_mode's own 2 real values (spec-lua-bindings.md Sec9.2a,
// spec-hud-runtime.md Sec6): pause_map=0 backs the full-screen pause map,
// minimap=1 backs the corner minimap - the SAME element type backs both.
// Provided here only as a typed convenience for callers of this header;
// the authoritative, Lua-reachable store is still the VINT property store
// (EngineState::setVintProperty(handle, "map_mode", ...)).
enum class MinimapMode : int32_t { PauseMap = 0, Minimap = 1 };

// One frame's live input to the corner-minimap's background transform
// (Sec6.4). `seatedInVehicle` is this project's own existing model of
// spec-player-runtime.md's vehicle-seated signal (the real engine's own
// `+0x16d4`==3 "seated" state, Sec6.1/Sec6.5 there) - tools/sr3_viewer.cpp's
// own runPlaytest() `inVehicle` flag is the established stand-in this
// project already uses for that signal; this struct takes the already-
// resolved position/heading rather than re-deriving "seated" itself, so it
// stays free of any GPU/window/EngineState dependency and is fully
// testable in isolation.
struct MinimapLiveInput {
    double playerWorldX = 0.0;
    double playerWorldZ = 0.0;
    double playerHeadingRad = 0.0;
    bool seatedInVehicle = false;
    double vehicleWorldX = 0.0;
    double vehicleWorldZ = 0.0;
    double vehicleHeadingRad = 0.0;
    // spec-save-format.md Sec7.5 "static minimap" option; real files'
    // CONFIRMED default is 0 (off) - ProfileOptions::staticMinimap's own
    // default-constructed value.
    bool staticMinimap = false;
};

// Sec6.4's own CONFIRMED output shape: a live map-centre in world space
// (the point the background art is centred on), plus the background
// rotation actually applied on screen this frame. `backgroundRotationRad`
// is exactly 0 whenever `staticMinimap` is true, in BOTH gated senses the
// spec describes (fixed/zeroed orientation AND no rotation applied to the
// draw-position math) - there is no daylight between those two gates'
// effects to model separately.
struct MinimapLiveTransform {
    double mapCentreX = 0.0;
    double mapCentreZ = 0.0;
    double backgroundRotationRad = 0.0;
};

// 0x00809910's own CONFIRMED mechanism (Sec6.4), reproduced faithfully:
// map-centre tracks the player's own position, or a seated vehicle's when
// `seatedInVehicle` is set; background rotation tracks live heading unless
// `staticMinimap` gates it to a fixed/zeroed orientation.
inline MinimapLiveTransform updateMinimapLiveTransform(const MinimapLiveInput& in) {
    MinimapLiveTransform out;
    if (in.seatedInVehicle) {
        out.mapCentreX = in.vehicleWorldX;
        out.mapCentreZ = in.vehicleWorldZ;
        out.backgroundRotationRad = in.staticMinimap ? 0.0 : in.vehicleHeadingRad;
    } else {
        out.mapCentreX = in.playerWorldX;
        out.mapCentreZ = in.playerWorldZ;
        out.backgroundRotationRad = in.staticMinimap ? 0.0 : in.playerHeadingRad;
    }
    return out;
}

// Sec6.4's single most important confirmed BEHAVIOURAL fact, turned into a
// concrete constant: the player's own marker glyph is drawn at a screen
// point that is the SAME every frame, regardless of world position or
// heading - NOT a pooled/addable blip (Sec6.1's shared blip pool was
// exhaustively searched and found to not carry the player at all), and NOT
// a function of MinimapLiveTransform's own output above. This project's
// own CHOSEN fixed point (the literal draw instruction was not traced,
// see this header's own top comment) is the minimap element's own
// geometric centre - offset (0,0) from it, forever.
constexpr double kPlayerMarkerScreenOffsetX = 0.0;
constexpr double kPlayerMarkerScreenOffsetY = 0.0;

} // namespace sr3luahost
