// Pause-map zone engine state: ZoneMember, PauseMapHoveredZone.
//
// Split out of engine_state.h (2026-10-08) as a zero-behavior-change
// structural move, so that a merge can only ever land a new field in its
// own domain's file - the monolithic header kept having fields from one
// struct merged into a DIFFERENT struct whose closing brace sat next to
// it. engine_state.h includes this header, so callers keep including
// engine_state.h only; add new fields to these structs HERE.
//
// In the doc comments below, "this header", "this document", "this file"
// and "this header's own top note" keep their pre-split meaning: the
// engine-state header set as a whole (engine_state.h plus the per-domain
// headers it includes), whose top note is at the head of engine_state.h.
// "above"/"below" pointers to a type or member now in another file were
// re-worded to name that file; every other line moved verbatim.
#pragma once

#include <cstdint>

namespace sr3luahost {

// pause_map_stag_current_district_control / pause_map_stag_takeover_do_reward
// (spec-lua-api-behaviour.md Sec31.2/Sec31.4/Sec31.6): one zone-membership
// record from the world singleton's own typed sub-list (0x03171a64, count
// +0x198, u16 indices +0x190, object pointers +0x58 - a different sub-list
// from this document's OTHER uses of the same global, e.g. the zscene
// "nearest world object" note in engine_state.h or Sec30.3's ambient-emitter list - this
// project has no real world-object-manager loader at all (same "no world
// objects in this host" precedent already established for
// zsceneNearestWorldObjectScene_ in engine_state.h) - populated ONLY via
// EngineState::registerZoneMemberForTesting().
struct ZoneMember {
    float weight = 0.0f; // +0x3c, CONFIRMED (Sec31.2) - the member's weighted contribution to the zone total
    bool owned = false;  // +0x3a bit 0x02, CONFIRMED (Sec31.4) - "owned by the player" (claimed via pause_map_stag_takeover_do_reward)
};

// pause_map_set_gps's own stag-mode branch (Sec31.3): the hovered-zone
// global 0x0229a2e4, written by the pause map's per-update refresher from
// the real cursor position (0x0084ba60, a zone-containment lookup this
// project does not run). This project has no cursor/pointer-input system -
// a test sets this fixture directly, standing in for "the player is
// currently hovering this zone on the pause map." zoneHandle 0 = "nothing
// hovered" (this project's own chosen sentinel, matching the selected-zone
// global's own "0 = none selected" reading, Sec31.2).
struct PauseMapHoveredZone {
    uint32_t zoneHandle = 0;
    bool hasParentDistrict = false; // the hovered zone's own +0x44 non-null (CONFIRMED gate, Sec31.3)
    int32_t fieldPlus0x54 = 0;      // the hovered zone's own +0x54 (CONFIRMED gate: must be > 0, Sec31.3)
};

} // namespace sr3luahost
