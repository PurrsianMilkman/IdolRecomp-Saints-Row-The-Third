// lua_spec_confirmed_stubs_b07_sec8_9_teamA.cpp
//
// The 2026-10-07 Section 9 full-document-sweep batch + the dedicated vehicle_chase (Sec9.4) pass + the 2026-10-07 Section 8 full-document-sweep batch + character_is_in_vehicle (Sec3.10) + the Team-A-cleared batch (commit 97a9b8d, Sec1.1/1.2/1.4/1.5/1.6/2.1/2.5/2.6/3.2/3.3/3.5/3.6).
//
// One of 7 files split 2026-10-07 from the former single 8,700+ line
// lua_spec_confirmed_stubs.cpp (a pure structural refactor, zero behaviour
// change - every stub body, comment and citation below is reproduced
// verbatim from that file). See lua_spec_confirmed_stubs_registry.cpp's own
// top comment for the full split rationale and the shared-registration
// mechanism (lua_spec_confirmed_stubs_sections.h/_common.h).
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include "sr3luahost/save_game_bridge.h"

namespace sr3luahost {
namespace {

// =======================================================================
// Section 9 batch (2026-10-07, full-document spec sweep): vehicle_speed_
// override (Sec9.5), vehicle_infinite_mass (Sec9.8), set_unjackable_flag
// (Sec9.13), vehicle_exists (Sec9.9), teleport_vehicle (Sec9.11),
// effect_play (Sec9.1), effect_stop (Sec9.2), inv_weapon_disable_all_slots
// (Sec9.20). See each new/reused field's own doc comment (engine_state.h,
// VehicleState/EngineState) for the full citation/reasoning this batch
// rests on.
//
// Deliberately NOT in this batch (left for a future pass): vehicle_chase
// (Sec9.4)/vehicle_max_speed (Sec9.6)/helicopter_set_path_orientation
// (Sec9.18) - all MEDIUM, each needing a small new multi-field "raw
// request" record (a packed flag byte explicitly NOT bit-decoded per
// WALLS.md, or a dedicated setter this pass did not reach) this batch's
// time budget did not reach, not a data gap; explosion_create (Sec9.3) -
// MEDIUM, a deliberately thin "resolve + sentinel-or-handle" shell would
// be honest but the real damage/explosion application
// (0x00594460, ~1,900 untraced instructions) must NOT be modeled or even
// implied - left for a pass that can scope that distinction carefully;
// vehicle_detonate (Sec9.7)/inv_item_equip (Sec9.19) - HARD, the former
// has no in-scope observable effect to implement beyond a no-op (nothing
// states what a SUCCESSFUL detonate changes), the latter needs a real
// fixed weapon/item-definition catalog (names + category bytes) this spec
// text does not supply - inventing it would be inventing data.
// =======================================================================

// vehicle_speed_override (Sec9.5): CONFIRMED no record-and-replicate call
// at this call depth (a real, confirmed absence) - always a direct local
// write once resolved. Converts mph->m/s (literal 0.44703999161720276),
// clamps to 1000.0, writes the SAME per-vehicle sub-record field
// vehicle_set_ambient (Sec33.1) also writes - see VehicleState::
// speedCapRaw's own doc comment.
int stub_vehicle_speed_override(lua_State* L) {
    logCall(L, upLog(L), "vehicle_speed_override", upStateTag(L));
    std::string name = argString(L, 1);
    double mph = lua_tonumber(L, 2); // 1 mandatory number, no nil-gate (CONFIRMED)
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        VehicleState& v = st->getOrCreateVehicle(name);
        double mps = mph * 0.44703999161720276; // CONFIRMED literal mph->m/s constant, 0x01119df8
        if (mps > 1000.0) mps = 1000.0;         // CONFIRMED clamp, 0x012a2d90/0x012a2f68
        v.hasSpeedCap = true;
        v.speedCapRaw = mps;
    }
    return 0;
}

// vehicle_infinite_mass (Sec9.8): double-gate (variant 1) setter, direct-
// write path (this project's forceFlagGatePasses CONFIRMED true in single
// player) - bit 0x2 of the shared +0x1d7b byte.
int stub_vehicle_infinite_mass(lua_State* L) {
    logCall(L, upLog(L), "vehicle_infinite_mass", upStateTag(L));
    std::string name = argString(L, 1);
    bool infinite = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.forceFlags1d7b.setBits(VehicleState::kInfiniteMassBit1d7b,
                                  infinite ? VehicleState::kInfiniteMassBit1d7b : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_infinite_mass", name);
    }
    return 0;
}

// set_unjackable_flag (Sec9.13): same double-gate shape as
// vehicle_infinite_mass, bit 0x40 of the SAME shared +0x1d7b byte, PLUS an
// unconditional, ungated, unreplicated secondary write (the wrapper's own
// asymmetric +0xbf8/+0xac bit 0x2 side effect, CONFIRMED by independent
// re-verification - see VehicleState::unjackableSecondarySubObjectFlag's
// own doc comment).
int stub_set_unjackable_flag(lua_State* L) {
    logCall(L, upLog(L), "set_unjackable_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool unjackable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    v.unjackableSecondarySubObjectFlag = unjackable; // unconditional, every call (CONFIRMED)
    if (v.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        v.forceFlags1d7b.setBits(VehicleState::kUnjackableBit1d7b,
                                  unjackable ? VehicleState::kUnjackableBit1d7b : 0u);
    } else {
        EngineState::replicateStateChange("set_unjackable_flag", name);
    }
    return 0;
}

// vehicle_exists (Sec9.9): pure query, resolve-only - this project's
// objectResolves() map already IS the project-wide "one shared name-
// resolves map" stand-in for every per-kind resolver (Sec29's own
// finding), regardless of this function's own distinct resolver address.
int stub_vehicle_exists(lua_State* L) {
    logCall(L, upLog(L), "vehicle_exists", upStateTag(L));
    std::string name = argString(L, 1);
    bool exists = upState(L)->objectResolves().get(name); // OPEN until set
    lua_pushboolean(L, exists ? 1 : 0);
    return 1;
}

// teleport_vehicle (Sec9.11): reuses the SAME dual-typed-resolver
// simplification stub_get_dist already establishes (no name-to-type
// registry - every resolved name is treated as character-shaped via
// CharacterState::worldPosition). Occupant-clearing and the destination's
// own +0x84 boolean are OPEN/unobservable in this project's scope, not
// modeled.
int stub_teleport_vehicle(lua_State* L) {
    logCall(L, upLog(L), "teleport_vehicle", upStateTag(L));
    std::string source = argString(L, 1);
    std::string dest = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(source) && st->objectResolves().get(dest)) { // OPEN until set
        // REAL fallback (2026-10-10, m07 live-playtest follow-up) - see
        // EngineState::tryResolveWorldPositionFromZoneData()'s own doc comment.
        CharacterState& destChar = st->getOrCreateCharacter(dest);
        st->tryResolveWorldPositionFromZoneData(dest, destChar.worldPosition);
        st->getOrCreateCharacter(source).worldPosition.set(
            destChar.worldPosition.get()); // OPEN until set
    }
    return 0;
}

// effect_play (Sec9.1): CONFIRMED fixed -1.0 sentinel on resolve failure
// (the SAME 0x012a3038 constant action_play_do/explosion_create also use,
// EngineState::kActionPlaySentinelResolveFail); on success, a fresh
// monotonically-increasing handle stands in for the real packed-instance
// identifier (not reconstructed - see EngineState::nextVisualEffectHandle's
// own doc comment). Arg 2/arg 3 are consumed but not modelled further (no
// in-scope consumer reads them back).
int stub_effect_play(lua_State* L) {
    logCall(L, upLog(L), "effect_play", upStateTag(L));
    std::string name = argString(L, 1);
    (void)optionalBoolDefault(L, 2, false);
    (void)optionalNumberDefault(L, 3, 0.0);
    EngineState* st = upState(L);
    // Spec 9.1 blocker dlc3_m01 (FINAL): the by-name table (0x004588f0) hashes the lower-cased name and compares
    // case-insensitively, and every navpoint of a loaded zone is registered in it, so a name found among the loaded
    // zone objects (case-insensitively) resolves. Anything else keeps the shared objectResolves() lookup.
    const bool resolves = st->resolveZoneObject(name) != nullptr || st->objectResolves().get(name); // OPEN until set
    if (!resolves) {
        lua_pushnumber(L, EngineState::kActionPlaySentinelResolveFail);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(st->nextVisualEffectHandle()));
    return 1;
}

// effect_stop (Sec9.2): CONFIRMED first argument is a NUMERIC handle, not
// a name - this project's effect_play above returns only an opaque
// placeholder handle with no real handle->object table behind it, so
// there is nothing concrete to resolve this against. Parses both
// arguments per their documented shape; no further effect is modelled
// (no networking layer, no real handle table).
int stub_effect_stop(lua_State* L) {
    logCall(L, upLog(L), "effect_stop", upStateTag(L));
    (void)lua_tonumber(L, 1);
    (void)optionalNumberDefault(L, 2, 3.0);
    return 0;
}

// inv_weapon_disable_all_slots (Sec9.20): CONFIRMED the apparent "per-
// category lookup" is a uniform broadcast of the single input bool across
// 12 identical fake slots, not a real table (so this does NOT hit the
// "invented table" boundary) - but the actual re-apply effect depends on
// "the local player currently has an equipped weapon," a weapon-category
// system this project does not model anywhere (the same gap left open for
// inv_item_equip/Sec9.19 and inv_weapon_add_temporary/remove_temporary).
// Parses both arguments per their documented shape; no further effect is
// modelled. No return value.
int stub_inv_weapon_disable_all_slots(lua_State* L) {
    logCall(L, upLog(L), "inv_weapon_disable_all_slots", upStateTag(L));
    (void)lua_toboolean(L, 1); // 1 mandatory bool, no nil-gate (CONFIRMED)
    (void)optionalNumberDefault(L, 2, 3.0);
    return 0;
}

// =======================================================================
// vehicle_chase (Sec9.4), dedicated pass, 2026-10-07: this document's own
// single highest real call-volume CONFIRMED-but-unimplemented native (72
// static call sites) at the time this was written. The Section 9 batch
// above deliberately left this one out ("MEDIUM complexity... needing a
// small new multi-field 'raw request' record (a packed flag byte
// explicitly NOT bit-decoded per WALLS.md...) this batch's time budget
// did not reach, not a data gap") - this is that follow-up pass. See
// VehicleState::aiDriveModeIsChasing/chaseTargetName/chaseFlagArg3..6
// (engine_state.h) for the new state and the full per-field reasoning.
// =======================================================================

// vehicle_chase (Sec9.4): starts, or updates an in-progress, AI vehicle-
// chase behaviour targeting a named entity. CONFIRMED: arg 1 (vehicle)
// gates the whole call through this project's shared objectResolves()
// stand-in (Sec29, the SAME convention every vehicle function in this
// header's own top note already establishes - Sec4.5's own dedicated
// vehicle-instance resolver, 0x00a281e0, is not separately modeled). Arg
// 2 (chase-target name, default the literal "#PLAYER1#" from .rdata) and
// args 3-6 (booleans, defaults true/false/true/false, each individually
// nil-gated per the standard idiom) are (re)stored on EVERY successful
// call on EITHER path (CONFIRMED - "Either way, the four optional
// booleans ... are packed into one flag byte alongside the resolved
// chase-target record before being written").
//
// The two-path branch (CONFIRMED by disassembly): a per-vehicle drive-
// mode predicate (0x00ad31a0, true only for internal state values 3/4)
// selects between a fresh-chase-record init (local write only, no
// record-and-replicate cited at this call depth) and an "already
// chasing" position-update path (a SEPARATE record-and-replicate-gated
// updater, opcode 0x42 - EngineState::replicateStateChange's own no-op
// network stand-in fires on this path only, alongside the local write
// already applied above). See VehicleState::aiDriveModeIsChasing's own
// doc comment for the JUDGMENT CALL this project makes to let that
// predicate ever become true from this native's own repeat calls (the
// EXACT internal state value, 3 vs 4, stays OPEN - not independently
// pinned down which 0x00b47110 writes).
//
// NOT modeled (genuinely out of this project's scope, not an oversight):
// the per-occupant/attached-part notify loop (enumerating every occupant
// via the count/index accessor pair vehicle_pathfind_check_done, Sec9.10,
// also uses, then calling 0x004e2350(1) on any occupant whose type-
// descriptor row has bit 0x4 at offset +10 set) - this project has no
// vehicle-resolution/occupant registry anywhere (the SAME deliberate
// boundary stub_vehicle_pathfind_check_done's own doc comment already
// states: "no occupant/part in slot 0" is simply unmodeled there too),
// and the spec text itself flags the real-world class 0x004e2350 notifies
// as OPEN, so there is nothing concrete to even attach a count to.
int stub_vehicle_chase(lua_State* L) {
    logCall(L, upLog(L), "vehicle_chase", upStateTag(L));
    std::string name = argString(L, 1);
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#"); // CONFIRMED literal default (distinct from "#PLAYER#", Sec4.13)
    bool arg3 = optionalBoolDefault(L, 3, true);
    bool arg4 = optionalBoolDefault(L, 4, false);
    bool arg5 = optionalBoolDefault(L, 5, true);
    bool arg6 = optionalBoolDefault(L, 6, false);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    // Either way: the resolved chase-target name + the 4 raw booleans are
    // (re)stored (CONFIRMED, see doc comment above).
    v.chaseTargetName = target;
    v.chaseFlagArg3 = arg3;
    v.chaseFlagArg4 = arg4;
    v.chaseFlagArg5 = arg5;
    v.chaseFlagArg6 = arg6;
    if (!v.aiDriveModeIsChasing) {
        // Not yet chasing: fresh chase-record init, local write only
        // (0x00b47110 then the +0xc68 sub-record writer 0x00b4d260) - no
        // record-and-replicate call cited at this call depth.
        v.aiDriveModeIsChasing = true; // JUDGMENT CALL - see VehicleState's own doc comment
    } else {
        // Already chasing: the SEPARATE record-and-replicate-gated
        // updater (opcode 0x42) pushes the position fields directly.
        EngineState::replicateStateChange("vehicle_chase", name);
    }
    return 0;
}

// =======================================================================
// Section 8 batch (2026-10-07, full-document spec sweep): audio_
// conversation_end (Sec8.2), audio_conversation_load_direct (Sec8.1),
// audio_persona_remove_2d (Sec8.6), audio_stop/audio_stop_emitting_id
// (Sec8.9), audio_is_playing (Sec8.3), audio_persona_line_playing
// (Sec8.4), audio_remove_mission_cellphone (Sec8.8). See each new field's
// own doc comment (engine_state.h) for the full citation/reasoning this
// batch rests on. audio_stop's dual-registration into the UI state is
// handled in src/lua_host.cpp (specUiNames.push_back("audio_stop")), the
// SAME targeted fix already applied to audio_object_post_event.
//
// Deliberately NOT in this batch (left for a future pass): hud_bar_off/
// hud_bar_set_value/hud_bar_on/hud_timer_set/hud_timer_stop/hud_prompt_
// clear/hud_set_fake_notoriety (Sec8.14/8.16/8.15/8.19/8.20/8.17/8.18) -
// all real and implementable, but share 1-2 new multi-field slot-array
// structs (HudBarSlot/HudTimerSlot) this pass's time budget did not
// reach, not a data gap; camera_restrict_set_active (Sec8.12) - the
// spec's OWN "Review status (2026-10-01): ... NOT yet cleared for
// implementation" line on this specific entry (only its one helper was
// re-derived) is a stronger bar than "CONFIRMED but unimplemented" -
// held pending that re-derivation, same discipline as the Sec1-5 freeze;
// vint_set_mouse_cursor (Sec8.25) - real and trivially easy (3 optional
// args, no observable effect either way - this host renders no cursor)
// but genuinely zero test value, held for a pass that wants the
// forward-compatible plumbing anyway; audio_persona_load_2d/audio_play_
// persona_line_2d/audio_ambient_emitter_start/audio_ambient_emitter_stop/
// push_screen (Sec8.5/8.7/8.10/8.11/8.26) - HARD, each needs either a
// real fixed slot-population table this spec text does not supply
// (ambient-emitter slots, registered-screen-type roster) or has its own
// core Lua-visible return dominated by OPEN items (audio_persona_load_2d's
// own single-player return shape is itself an explicit spec OPEN item,
// matching this project's own prior deferral of this exact name).
// =======================================================================

// audio_conversation_end (Sec8.2): CONFIRMED host-only (coopLocalIsHost(),
// CONFIRMED false in single player) - the queue branch never fires in
// this project's scope, so this reduces to argument parsing only.
int stub_audio_conversation_end(lua_State* L) {
    logCall(L, upLog(L), "audio_conversation_end", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1)); // 1 mandatory number, round-to-int (CONFIRMED)
    return 0;
}

// audio_conversation_load_direct (Sec8.1): no real conversation table
// anywhere in this project (same class of gap as game_audio_get_audio_id,
// Sec8.22) - a fresh handle from the SAME shared counter
// nextAudioVoiceHandle() uses stands in, masked to a byte per the
// CONFIRMED "truncated to a single byte" return contract.
int stub_audio_conversation_load_direct(lua_State* L) {
    logCall(L, upLog(L), "audio_conversation_load_direct", upStateTag(L));
    (void)argString(L, 1); // 1 mandatory string, read unconditionally (CONFIRMED)
    int64_t handle = upState(L)->nextAudioVoiceHandle();
    lua_pushnumber(L, static_cast<lua_Number>(handle & 0xFF));
    return 1;
}

// audio_persona_remove_2d (Sec8.6): CONFIRMED host-only queue (never
// fires, same as audio_conversation_end above) plus an unconditional
// cleanup call (0x006de790, OPEN) - audio_persona_load_2d (the only
// producer of these handles) is not implemented in this project (HARD,
// deferred), so there is no tracked handle state to clean up either;
// reduces to argument parsing only.
int stub_audio_persona_remove_2d(lua_State* L) {
    logCall(L, upLog(L), "audio_persona_remove_2d", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1)); // 1 mandatory number, round-to-int (CONFIRMED)
    return 0;
}

// audio_stop (Sec8.9, also registered under the sibling name audio_
// stop_emitting_id - CONFIRMED the exact same C function bound to two
// Lua names at adjacent array slots) / audio_is_playing (Sec8.3): both
// key off the SAME fixed 32-slot table - see EngineState::audioPlaying()'s
// own doc comment for the "no in-scope writer" reasoning.
int stub_audio_stop(lua_State* L) {
    logCall(L, upLog(L), "audio_stop", upStateTag(L));
    int id = static_cast<int>(truncateEa2596(lua_tonumber(L, 1))); // 1 mandatory number, round-to-int (CONFIRMED)
    upState(L)->setAudioPlaying(id, false);
    return 0;
}

int stub_audio_is_playing(lua_State* L) {
    logCall(L, upLog(L), "audio_is_playing", upStateTag(L));
    int id = static_cast<int>(truncateEa2596(lua_tonumber(L, 1))); // 1 mandatory number, round-to-int (CONFIRMED)
    lua_pushboolean(L, upState(L)->audioPlaying(id) ? 1 : 0);
    return 1;
}

// audio_persona_line_playing (Sec8.4): resolves via the SAME shared
// objectResolves() map audio_play_persona_line (Sec2.5) also uses;
// CONFIRMED a null/unresolved handle reads as false by convention - see
// EngineState::personaLinePlaying()'s own doc comment for the resolved-
// case OPEN predicate.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it (this was one of the sites
// explicitly deferred pending that correction, follower-sentinel-resolver
// pass) - wired here via resolveFollowerAwareObject().
int stub_audio_persona_line_playing(lua_State* L) {
    logCall(L, upLog(L), "audio_persona_line_playing", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool playing = false;
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        // ce4556f Sec8.4 FINAL: true only while a voice instance is attached to the
        // character's emitter; none exists unless seeded -> false.
        playing = st->personaLinePlaying().known(resolvedName) ? st->personaLinePlaying().get(resolvedName) : false;
    }
    lua_pushboolean(L, playing ? 1 : 0);
    return 1;
}

// audio_remove_mission_cellphone (Sec8.8): this project has no "mission
// cellphone" association concept anywhere in scope (grepped directly -
// zero hits), so there is no durable state for this native to remove;
// reduces to argument parsing only.
int stub_audio_remove_mission_cellphone(lua_State* L) {
    logCall(L, upLog(L), "audio_remove_mission_cellphone", upStateTag(L));
    std::string callId = argString(L, 1); // 1 mandatory string, read unconditionally (CONFIRMED)
    // Spec V.3 (CONFIRMED, spec sync bfe0542): clears both hook slots, never fires cb2.
    upState(L)->removeMissionCellphoneCall(callId);
    return 0;
}

// ---------------------------------------------------------------------
// character_is_in_vehicle (Sec3.10, 0x00a45db0). CLEARED FOR
// IMPLEMENTATION 2026-10-07 (Team A resolved the one remaining OPEN item,
// the vtable +0x70 call's argument shape) - this project's own single
// highest measured real call count for any previously-unimplemented name
// (41,176 calls under --probe-unresolved-names) and a named blocker for
// missions m05/m07/m21.
//
// Arguments (Sec3.10 CONFIRMED): 1 mandatory string (character name,
// generic/#PLAYER# chain - objectResolves(), Sec29's shared named-object
// map, same resolver every other character-name native in this file
// uses). 1 optional string (a specific vehicle name to check against),
// standard nil-gated idiom - the one case in this cluster where the idiom
// gates a STRING rather than a boolean.
//
// Return: normally 1 boolean. CONFIRMED exception, read directly off both
// push sites and the "return count+1" arithmetic (Sec3.10's own text): if
// arg 2 is supplied but fails to resolve to a valid vehicle handle, this
// pushes false for that failure AND THEN still falls through into the
// normal end-of-function push of the real predicate result - TWO Lua
// return values on that one path, one otherwise.
//
// Body: the predicate itself (0x009b9290, fully disassembled per the
// 2026-10-07 OPEN-item resolution) unconditionally tests the character's
// own state-enum field (CharacterState::stateEnum, +0x16d4, == 3 - "in a
// vehicle", the SAME value set_ignore_ai_flag/Sec3.4 checks) FIRST; if
// that fails, the whole predicate is false regardless of arg 2
// (short-circuit AND, per 0x009b9290's own disassembled shape - reproduced
// below by evaluating the vehicle-pair comparison only when the state-enum
// test already passed). Only if the state-enum test passes AND a specific
// vehicle was requested does it additionally compare the character's own
// stored vehicle link against that vehicle's identity; a null/absent
// vehicle handle "short-circuits straight to true once the state-enum
// check passes" (Sec3.10's own resolved-OPEN-item text) - exactly the
// no-vehicle-arg path below, and also the arg-2-unresolved fallthrough
// path (no valid handle was ever produced for 0x009b9290's second
// argument there either).
//
// JUDGMENT CALL on the vehicle-identity comparison: Sec3.10 describes the
// character's own +0x16c0/+0x16c4 dword pair compared against the
// resolved vehicle's own +8/+0xc self-identity pair (the vtable +0x70
// accessor's return value). This EXACT physical field pair is already
// modeled in this project - CharacterState::currentVehicleName above
// (Sec30.5, vehicle_exit_group_do) - as a vehicle NAME rather than a raw
// dword pair, per this header's own stated, project-wide convention that
// object identity reduces to the plain name string a function was given
// (this file's own top comment: "no real handle-resolution mechanism
// anywhere in this project"). Reusing that SAME field for THIS comparison
// (character.currentVehicleName == the resolved arg-2 name) is therefore
// the minimal, already-precedented translation - checked against
// engine_state.h before writing this function; no new engine-state field
// was added for this native (see currentVehicleName's own doc comment,
// updated in place to note this second consumer).
//
// Side effects / subsystem: none - pure query (Sec3.10 CONFIRMED).
// ---------------------------------------------------------------------
int stub_character_is_in_vehicle(lua_State* L) {
    logCall(L, upLog(L), "character_is_in_vehicle", upStateTag(L));
    std::string name = argString(L, 1);
    bool hasVehicleArg = lua_gettop(L) >= 2 && lua_type(L, 2) != LUA_TNIL;
    std::string vehicleName = hasVehicleArg ? argString(L, 2) : std::string();

    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set (arg 1, generic/#PLAYER# chain)
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& character = st->getOrCreateCharacter(name);
    // 0x009b9290's unconditional first test - decides the WHOLE predicate
    // when it fails (short-circuit AND, see doc comment above).
    bool inVehicleState = character.stateEnum.get() == 3; // OPEN until set
    // feature/route-table-driving: harness bookkeeping only (no engine semantics).
    if (!inVehicleState && name == "#PLAYER1#") st->noteVehicleWaitQuery(vehicleName);

    if (!hasVehicleArg) {
        lua_pushboolean(L, inVehicleState ? 1 : 0);
        return 1;
    }

    if (!st->objectResolves().get(vehicleName)) { // OPEN until set (0x0062a190)
        // CONFIRMED double-push (Sec3.10): false for the resolve failure,
        // THEN the normal end-of-function push of the real predicate
        // result - here that is the null-handle short-circuit (no valid
        // handle was ever produced for 0x009b9290's second argument).
        lua_pushboolean(L, 0);
        lua_pushboolean(L, inVehicleState ? 1 : 0);
        return 2;
    }

    // Vehicle resolved: the short-circuit `&&` below skips the OPEN
    // currentVehicleName read entirely when inVehicleState is already
    // false, matching 0x009b9290's own short-circuit order (and avoiding
    // an unrelated OPEN refusal on a character that plainly isn't in any
    // vehicle at all).
    bool matchesVehicle = inVehicleState && character.currentVehicleName.get() == vehicleName; // OPEN until set
    lua_pushboolean(L, matchesVehicle ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// Team-A-cleared batch (commit 97a9b8d): spec-lua-api-behaviour.md
// Sec1.1/1.2/1.4/1.5/1.6/2.1/2.5/2.6/3.2/3.3/3.5/3.6. Each function below
// implements ONLY that one section's own CONFIRMED text, cited per
// function; a genuinely OPEN sub-item uses OpenStateError (via an
// EngineState OpenValue/OpenValueMap read) rather than inventing a value.
// See engine_state.h's own "Team-A-cleared batch" section (added this
// same pass) for the new state these 12 functions read/write.
// ---------------------------------------------------------------------

// trigger_enable (Sec1.1, 0x00a617d0) - 563 calls/59 scripts. CONFIRMED:
// arg 1 mandatory string (trigger name), arg 2 optional bool default true
// (enable/disable), arg 3 optional numeric/flag default 3 (read via the
// resolved Sec4.1 truncateEa2596 pair). Resolve+liveness via the shared
// named-object gate (0x005e4e30/0x00853b10, this project's flat
// objectResolves() stand-in, Sec29). On success, mutates trigger-subsystem
// state (an enable/disable toggle plus a flag-gated secondary behaviour
// switch, bit 0x2 of the flags argument) - this project has no real
// trigger-subsystem simulation, so only the raw (enabled, flags) pair is
// recorded (EngineState::recordTriggerEnable), honestly, as "a request,"
// never the two tail setters' own internal effects. Return: none.
int stub_trigger_enable(lua_State* L) {
    logCall(L, upLog(L), "trigger_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = optionalBoolDefault(L, 2, true);
    int64_t flags = truncateEa2596(optionalNumberDefault(L, 3, 3.0));
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->applyTriggerEnableTransition(name, enable); // spec V.4: disable emits EXIT for a human inside; enable of a disabled trigger = fresh phantom (reads the PREVIOUS enable state, so before the record below)
        st->recordTriggerEnable(name, enable, flags);
    }
    return 0;
}

// on_trigger (Sec1.2, 0x00a58680) - 513 calls/59 scripts. CONFIRMED: 2
// mandatory strings - arg 1 an opaque callback name, arg 2 a trigger name
// resolved via the SAME shared gate as trigger_enable above. Body
// (re-derived in full 2026-10-07): a per-trigger gate at handle offset
// +0xa8 is checked; CONFIRMED that nothing inside 0x00a58680/0x00a58720/
// 0x00a56cb0 itself ever writes +0xa8, and this project models no OTHER
// writer (out of this batch's 12-name scope) - so in this closed world the
// gate always reads its zero-filled-allocation default (unset), and
// registration always proceeds once the trigger resolves (see
// EngineState::registerOnTriggerCallback's own doc comment for the full
// reasoning). The deadline recompute (+0x88) has no in-scope reader among
// this batch's 12 names and is not modeled. Return: none.
int stub_on_trigger(lua_State* L) {
    logCall(L, upLog(L), "on_trigger", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string triggerName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(triggerName)) { // OPEN until set
        st->registerOnTriggerCallback(triggerName, callbackName);
    }
    return 0;
}

// objective_text_clear (Sec1.4, 0x00a56ab0) - 263 calls/45 scripts.
// CONFIRMED: 1 mandatory numeric slot index, read UNCONDITIONALLY (no
// nil/absence check at all - "so this argument is always read as if
// present") via the same Sec4.1 truncateEa2596 pair objective_text's own
// arg 1 uses. Body: a zero-argument housekeeping call (0x00d1e0c0, no
// in-scope observable effect) then a dispatch (0x007eda00 -> 0x007ed7d0,
// opcode 9) clearing the global objective-text/HUD slot - no name-resolver
// step at all, unlike every other function in this cluster. Return: none.
int stub_objective_text_clear(lua_State* L) {
    logCall(L, upLog(L), "objective_text_clear", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1)); // CONFIRMED: unconditional read, no nil-gate
    upState(L)->recordObjectiveTextClear(slot);
    return 0;
}

// mission_end_failure (Sec1.5, 0x00a523e0) - 160 calls/36 scripts.
// CONFIRMED: 2 mandatory strings - arg 1 a mission name (never
// handle-resolved itself, passed straight into the gate function
// 0x006d7240), arg 2 a failure-reason/localization key. Gate (0x006d7240,
// fully decompiled 2026-10-07): passes only when arg 1 names the CURRENTLY
// ACTIVE mission (EngineState::MissionContext::currentMissionName, this
// project's name-string stand-in for the real pointer-identity compare)
// AND the context is active with phase != 8 (MissionContext::
// activeAndRunning) AND that active mission's own liveness check comes
// back DEAD/"already ending" (MissionContext::currentMissionEnding) - "a
// mission 'found and still fully alive' does NOT pass." On a pass: UPGRADED
// (Sec21.27c, 2026-10-09, full disassembly of 0x006da400) - mission TYPE 2
// silently coerces this call into hardcoded success (0x006d9f70, see the
// gated body below), skipping the final presentation dispatch entirely;
// every OTHER type reaches it (0x006da400/
// EngineState::dispatchMissionEndPresentation()), which stores arg 2's
// resolved text into the real fail-text global, arms the shared mission-
// end-transition timer (EngineState::armMissionEndTransitionTimer(),
// 0x012f2630, 2000ms/4000ms-on-overlap) and, gated on a flags-byte bit,
// tears down every active HUD screen before a fade. Return: none (both the
// gated-out and normal paths).
int stub_mission_end_failure(lua_State* L) {
    logCall(L, upLog(L), "mission_end_failure", upStateTag(L));
    std::string missionName = argString(L, 1);
    // failure-reason/localization key - UPGRADED (Sec21.27c, 2026-10-09):
    // now modelled as the real text written into 0x014c7a08 on the
    // non-coerced path (see dispatchMissionEndPresentation()), not merely
    // consumed for call shape.
    std::string failureText = argString(L, 2);
    EngineState* st = upState(L);
    auto& mc = st->missionContext();
    // FIX 2026-10-08 (mission-context-name task): all three reads below are
    // now set for real by EngineState::bindMissionPhaseMachine() at mission-
    // bind time (0x006d6920, Sec21.27a/MissionContext's own field doc
    // comments) - a real mission reaching this native no longer throws
    // OpenStateError here; it correctly fails closed (silent no-op) until
    // something flips currentMissionEnding true.
    bool gatePasses = mc.activeAndRunning.get() &&
                      mc.currentMissionName.get() == missionName &&
                      mc.currentMissionEnding.get();
    if (gatePasses) {
        // NEW finding, CONFIRMED (Sec21.27c, 2026-10-09): for mission TYPE 2
        // specifically, this call is silently coerced into
        // 0x006d9f70(mission, 1) - hardcoded success via the SAME
        // end-of-mission super-dispatcher Sec21.27b's own missionEndDispatch()
        // already models - skipping the fail-text/debounce/HUD-clear
        // dispatch (0x006da400/dispatchMissionEndPresentation()) entirely.
        if (mc.boundMissionType == 2) {
            ++mc.missionEndFailureCoercedToSuccessCount;
            st->missionEndDispatch(missionName, /*successFlag=*/true);
        } else {
            st->dispatchMissionEndPresentation(/*isSuccess=*/false, failureText);
        }
    }
    return 0;
}

// mission_set_checkpoint (Sec1.6, 0x00a52600) - 157 calls/37 scripts.
// CONFIRMED: 1 mandatory string (checkpoint/mission name) + 2 optional
// bools (nil-gated, each default false). Body (0x006d5060/0x006d43d0,
// fully decompiled 2026-10-07): no-op unless a mission is active with
// phase != 8 (MissionContext::activeAndRunning, SHARED with
// mission_end_failure above - the identical two real globals) AND both
// adjacent debounce timers (0x012f2630/0x012f2634) are idle - if either is
// still armed, the call is silently dropped. Past the gate: the name is
// stashed (0xbd9c10, test-observable only via MissionCheckpoint::lastName);
// arg 2 selects which transform the checkpoint snapshots - false (default)
// uses the fixed, hardcoded checkpoint-vehicle object's transform IF it
// resolves AND the current player is confirmed inside it (the
// ALREADY-CONFIRMED occupancy predicate 0x009b9290, Sec3.10 - collapsed
// into MissionCheckpoint::usePlayerVehicleTransform, since the hardcoded
// object's own real name is not independently pinned to a human-readable
// string this project could resolve by name); true, or any failed check,
// always uses the player's own position instead - this project tracks no
// real transform data, only WHICH branch ran
// (MissionCheckpoint::lastUsedVehicleTransform). arg 3 selects whether a
// small loadout-slot array is zeroed (true) or populated from a fixed
// global template table this project has no real data for (false, default)
// - only WHICH branch ran is recorded (MissionCheckpoint::
// lastLoadoutSlotsZeroed), never fabricated template values. The
// unconditional full weapon/inventory loadout snapshot has no real
// inventory system in this project's scope and is not modeled. Return:
// none (unconditionally, per spec).
//
// WIRED 2026-10-08 (spec-save-format.md Sec15.3/Sec11.2): past the same
// gate above, this native's real body (0x006d5060 -> 0x006d43d0) calls the
// SAME real save entry (0x00b94a10) mission_autosave/game_autosave reach,
// but through its OTHER branch - "a pointer to caller-owned memory...
// producing the same 0x1AA00-byte layout in memory only - no hash, no file
// write." Modeled here as buildCheckpointSnapshotBytes() (computeHash =
// false, matching that real "no hash" buffer), stored on EngineState::
// lastCheckpointSnapshot() rather than discarded (this task's own step 4) -
// retrievable later via save_game_bridge.h's applyCheckpointSnapshot().
int stub_mission_set_checkpoint(lua_State* L) {
    logCall(L, upLog(L), "mission_set_checkpoint", upStateTag(L));
    std::string name = argString(L, 1);
    bool arg2 = optionalBoolDefault(L, 2, false); // "use player's own position" override
    bool arg3 = optionalBoolDefault(L, 3, false); // "zero loadout slots"
    EngineState* st = upState(L);
    auto& mc = st->missionContext();
    if (!mc.activeAndRunning.get()) return 0; // OPEN until set
    if (mc.missionEndTimer1Armed || mc.missionEndTimer2Armed) return 0; // debounce, CONFIRMED silent drop
    auto& cp = st->missionCheckpoint();
    cp.lastName = name;
    ++cp.setCount;
    // CONFIRMED short-circuit: only consult usePlayerVehicleTransform when
    // arg 2 is false - when arg 2 is true the function "always snapshots
    // from the generic default-character accessor" without checking it.
    // Spec a166141 Sec1.6: derived from real state - the local player counts only when seated
    // (+0x16d4 == 3) in a resolved vehicle matching its own vehicle link; otherwise the
    // player-position branch. An explicit test override (usePlayerVehicleTransform.set) wins.
    bool useVehicle = false;
    if (!arg2) {
        if (cp.usePlayerVehicleTransform.known()) {
            useVehicle = cp.usePlayerVehicleTransform.get();
        } else {
            const std::string player = st->localPlayerNameForTesting().known()
                                           ? st->localPlayerNameForTesting().get() : std::string("#PLAYER1#");
            if (st->hasCharacter(player)) {
                CharacterState& pc = st->getOrCreateCharacter(player);
                useVehicle = pc.stateEnum.known() && pc.stateEnum.get() == 3 &&
                             pc.currentVehicleName.known() && !pc.currentVehicleName.get().empty() &&
                             st->objectResolves().known(pc.currentVehicleName.get()) &&
                             st->objectResolves().get(pc.currentVehicleName.get());
            }
        }
    }
    cp.lastUsedVehicleTransform = useVehicle;
    cp.lastLoadoutSlotsZeroed = arg3;
    try { // never let a C++ exception cross this extern "C" Lua frame (common.h's own top rule)
        st->lastCheckpointSnapshot() = buildCheckpointSnapshotBytes(*st); // Sec15.3/Sec11.2: real in-memory snapshot
        ++st->checkpointSnapshotBuildCount();
    } catch (const sr3save::FormatError&) {
        // Defensive only - this project's own saveGame() fields never exceed
        // the writer's real format capacities in practice; leaves the
        // previous checkpoint (if any) untouched rather than a partial one.
    }
    return 0;
}

// game_get_platform (Sec2.1, 0x00841c50) - 659 calls/244 scripts.
// CONFIRMED: no arguments used (lua_gettop's result is read then
// discarded, same shared prologue every other function in this cluster
// calls). Pushes one fixed, compile-time literal string, "PC" (read
// directly from .rdata at 0x1162eb4) - no branching, no other engine
// state read or written. Return: 1 Lua value.
int stub_game_get_platform(lua_State* L) {
    logCall(L, upLog(L), "game_get_platform", upStateTag(L));
    (void)lua_gettop(L); // CONFIRMED: called, result never read again
    lua_pushstring(L, "PC");
    return 1;
}

// audio_play_persona_line (Sec2.5, 0x00a3d050) - 227 calls/36 scripts.
// CONFIRMED (full re-derivation 2026-10-07): arg 1 mandatory string
// (persona name, generic/#PLAYER# chain 0x00a281a0 - this project's flat
// objectResolves() gate). arg 2 mandatory string (the dialogue-line name -
// CORRECTED 2026-10-07 from an earlier "functionally inert" hypothesis:
// it IS used, held across the whole function and passed as the single
// explicit argument to the line-name resolver 0x0070a2f0). arg 3 optional
// number, read via lua_tonumber then genuinely discarded (fstp st(0), not
// a decompiler artefact) - consumed for real call shape only. Return: 1
// number - the CONFIRMED fallback literal 255.0 (0x012a2d88) if the
// persona fails to resolve, OR if the line-name resolver's own null-check
// fails (0x0070a2f0: "if (arg == 0) return 0"); otherwise the fresh,
// monotonically-increasing play-instance handle the dispatch chain
// allocates (0x00a2ad00 -> 0x00a2a850's own global counter 0x026ea798) -
// this project reuses EngineState::nextAudioVoiceHandle(), the SAME "no
// real Wwise voice table, a plain incrementing counter stands in" choice
// game_UI_audio_play (Sec2.2) already established, since both are the
// engine's own single incrementing play-instance counter. The downstream
// Wwise-ID resolver (0x00462960) itself is modeled as always succeeding
// for any non-null input - this project's existing "no real Wwise ID
// table" stand-in (Sec2.2/Sec2.4's own doc comments), so the only genuine
// failure path reachable from Lua is persona resolve failure or arg 2
// being entirely absent (lua_tolstring returns NULL only then, not for an
// empty string - a present "" still succeeds).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_audio_play_persona_line(lua_State* L) {
    logCall(L, upLog(L), "audio_play_persona_line", upStateTag(L));
    std::string persona = argString(L, 1);
    const char* lineNamePtr = lua_tolstring(L, 2, nullptr); // kept raw (not argString) to preserve the real null-vs-empty distinction 0x0070a2f0 tests
    if (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) {
        (void)lua_tonumber(L, 3); // CONFIRMED: read, then genuinely discarded
    }
    constexpr double kFallback = 255.0; // CONFIRMED .rdata literal, 0x012a2d88
    EngineState* st = upState(L);
    std::string resolvedPersona;
    if (!resolveFollowerAwareObject(st, persona, resolvedPersona)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushnumber(L, kFallback);
        return 1;
    }
    if (lineNamePtr == nullptr) { // 0x0070a2f0's own null-check - CONFIRMED
        lua_pushnumber(L, kFallback);
        return 1;
    }
    int64_t handle = st->nextAudioVoiceHandle();
    lua_pushnumber(L, static_cast<double>(handle));
    return 1;
}

// game_is_active_input_gamepad (Sec2.6, 0x00842270) - 238 calls/147
// scripts. CONFIRMED (full re-derivation 2026-10-07): no arguments used
// (same shared-prologue-discarded pattern as game_get_platform above).
// Calls the one-instruction getter 0x005bc5d0, which reads the global byte
// flag 0x0141250d (control context 0x01411b18+0x9f5, CONFIRMED zero-filled
// .data at construction) and pushes it as a genuine boolean (lua_pushboolean,
// not a number). This exact function pointer is ALSO dual-registered into
// the 113-entry UI-state loop registrar under the identical name
// (0x00845aa0 pair 41) - handled in src/lua_host.cpp (specUiNames), not
// here (every stub already reads its own stateTag as an upvalue per-call,
// so it reports "ui"/"gameplay" correctly regardless of which state it
// runs in). Return: 1 boolean.
int stub_game_is_active_input_gamepad(lua_State* L) {
    logCall(L, upLog(L), "game_is_active_input_gamepad", upStateTag(L));
    (void)lua_gettop(L); // CONFIRMED: called, result never read again
    lua_pushboolean(L, upState(L)->activeInputIsGamepad() ? 1 : 0);
    return 1;
}

// on_death (Sec3.2, 0x00a57c80) - 386 calls/37 scripts. CONFIRMED (full
// re-derivation 2026-10-07): 2 mandatory strings - arg 1 a callback name,
// arg 2 a character name, resolved through the generic/#PLAYER# chain
// (0x00a280c0) first, falling back to the plain character resolver
// (0x005e4dd0) if that fails - BOTH paths, if either succeeds, register
// the SAME callback into the SAME hook slot (0, via 0x005e4660) with no
// behavioral difference between them, so this project's single flat
// objectResolves() gate already covers the full resolve step (unlike
// on_vehicle_destroyed/character_is_dead below, where the two paths
// genuinely diverge and need to be told apart). Return: none.
int stub_on_death(lua_State* L) {
    logCall(L, upLog(L), "on_death", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(targetName)) { // OPEN until set
        st->getOrCreateCharacter(targetName).onDeathCallback = callbackName;
    }
    return 0;
}

// on_respawn (spec-player-runtime.md Sec18.15, 0x00a58380, FINAL): callback name + character name; player names register
// slot 5 of +0x1f00. Fired by the revive (0x009e6f20) with one string argument, the player's script name.
int stub_on_respawn(lua_State* L) {
    logCall(L, upLog(L), "on_respawn", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(targetName) && EngineState::isLocalPlayerName(targetName))
        st->playerOnRespawnCallback_ = callbackName;
    return 0;
}

// on_vehicle_destroyed (Sec3.3, 0x00a58840) - 284 calls/27 scripts.
// CONFIRMED (full re-derivation 2026-10-07): 2 mandatory strings (callback
// name, target name), same read shape as on_death. Body: arg 2 is resolved
// through the generic/#PLAYER# chain FIRST; if that succeeds, the callback
// registers into the CHARACTER's own hook sub-record at slot 0x12 (18) -
// NOT a vehicle slot, despite the function's own name. The
// "kind-agnostic" hypothesis for this generic branch is REFUTED
// (2026-10-07, direct disassembly of 0x00a280c0/0x00a27e20): the generic
// chain's first-tier resolver masks every hit against the object's own
// player-class descriptor bit, so a vehicle's own name can NEVER resolve
// there - only reachable via the literal "#PLAYER#" sentinel or an
// equivalent registered player-slot name (EngineState::
// playerClassChainResolves()). Only when that fails does the function fall
// back to resolving arg 2 as a vehicle name (0x0062a190) + liveness
// (0x00853b10, this project's flat objectResolves() stand-in), registering
// into the VEHICLE's own hook slot 0 (+0x30/+0x34 sub-record, base
// object+0xb0, via 0x00a56d50) instead. Return: none.
int stub_on_vehicle_destroyed(lua_State* L) {
    logCall(L, upLog(L), "on_vehicle_destroyed", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->playerClassChainResolves(targetName)) { // CHOSEN default now applies when no test .set() it - see engine_state.h
        st->getOrCreateCharacter(targetName).onVehicleDestroyedPlayerSlotCallback = callbackName; // slot 0x12 (18)
    } else if (st->objectResolves().get(targetName)) { // OPEN until set - vehicle-name fallback resolver
        st->getOrCreateVehicle(targetName).onVehicleDestroyedCallback = callbackName; // vehicle's own slot 0
    }
    return 0;
}

// character_is_dead (Sec3.5, 0x00a415f0) - 237 calls/37 scripts. CONFIRMED
// (full re-derivation 2026-10-07): 1 mandatory string (character name),
// resolved via the generic/#PLAYER#-then-fallback chain (0x00a280c0
// PRIMARY / 0x00a28150 fallback, same playerClassChainResolves() split as
// on_vehicle_destroyed above; the fallback's own #FOLLOWER#-family
// acceptance wired 2026-10-08, follower-sentinel-resolver pass, via
// resolveFollowerAwareObject() in place of a plain objectResolves() call -
// the PRIMARY branch is unaffected, since 0x00a280c0 is the FOLLOWER-free
// chain per this entry's own text), then queries
// 0x0096f4f0 directly (null pointer -> dead; else object +0xcc8 == 5
// (CharacterState::lifeState) OR flag byte +0xe3 bit 0 set
// (CharacterState::killFinalizedFlagE3) - the EXACT same two fields
// notDeadForActionPlay() already models for the SAME real predicate,
// reused directly here rather than duplicated, per CharacterState's own
// "character_is_dead" doc comment above lifeState/killFinalizedFlagE3).
// GENUINE FINDING (2026-10-07): on the PRIMARY-chain branch only, if
// 0x0096f4f0 reports "not dead," one more check
// (0x006cec90 -> 0x00d9e4c0 on the shared mission-end-transition timer,
// 0x012f2630 - the SAME timer mission_end_failure arms above,
// MissionContext::missionEndTimer1Armed) can still force "dead" - i.e. for
// roughly the first few seconds after any mission ends, a query about the
// player (the primary chain is itself player-class-gated) unconditionally
// answers "dead." The separate fallback branch (reached when the primary
// chain's own lookup misses) has no such override. Return: 1 boolean.
int stub_character_is_dead(lua_State* L) {
    logCall(L, upLog(L), "character_is_dead", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool primaryResolves = st->playerClassChainResolves(name); // CHOSEN default now applies when no test .set() it - see engine_state.h
    std::string resolvedName = name;
    // Fallback-only: primaryResolves short-circuits before this runs, so the
    // FOLLOWER-free primary chain (0x00a280c0) never touches
    // resolveFollowerAwareObject() - matches this entry's own text that only
    // the FALLBACK (0x00a28150) accepts the #FOLLOWER# family.
    bool objectExists = primaryResolves || resolveFollowerAwareObject(st, name, resolvedName); // OPEN until set when primary failed; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    bool dead;
    if (!objectExists) {
        dead = true; // CONFIRMED null-pointer branch of 0x0096f4f0
    } else {
        CharacterState& c = st->getOrCreateCharacter(primaryResolves ? name : resolvedName);
        // Short-circuit matches the real OR: killFinalizedFlagE3 is only
        // read when lifeState != 5, so a test need not set both.
        dead = (c.lifeState.get() == 5) || c.killFinalizedFlagE3.get(); // seeded at construction, not OPEN by default
    }
    if (primaryResolves && !dead && st->missionContext().missionEndTimer1Armed) {
        dead = true; // CONFIRMED mission-end-transition-timer override, primary branch only
    }
    if (!dead && objectExists && name.rfind("#PLAYER", 0) != 0) st->noteDeathPollQuery(primaryResolves ? name : resolvedName); // --auto-objective harness bookkeeping only (a live, non-player target a script is waiting on)
    lua_pushboolean(L, dead ? 1 : 0);
    return 1;
}

// notoriety_force_no_spawn (Sec3.6, 0x00a54db0) - 234 calls/31 scripts.
// CONFIRMED (re-confirmed and completed 2026-10-07): arg 1 names a
// notoriety/gang TRACK (not a character/vehicle) resolved via a lookup
// DISTINCT from the general named-object resolver - 0x0094cc60 - into a
// numeric code (EngineState::lookupNotorietyTrackCode(), CONFIRMED real
// failure sentinel 9 on a miss). arg 2 mandatory bool, read directly via
// lua_toboolean with NO nil/absence gate (an omitted arg 2 is read at an
// out-of-range stack index, which lua_toboolean's own real
// luaO_nilobject-sentinel handling resolves to "false" - so a raw,
// ungated read is the CONFIRMED-correct shape, not a bug). Dispatch:
// code 5 -> ten fixed track-record bases set directly to the boolean
// (0x008ecda0: "mov byte ptr [ecx+0x455], al" - a direct SET, not an XOR
// toggle); code 6 -> five different, disjoint bases, same direct set;
// code 1/2 -> one base each via the switch (0x008ed1d0); code 3 -> two
// paired bases; code 4 -> CONFIRMED hard no-op (dead default case); any
// other code (including the 9 not-found sentinel) -> silently a no-op.
// Every literal base address below is transcribed directly from Sec3.6's
// own re-confirmed disassembly (not invented). Return: none.
int stub_notoriety_force_no_spawn(lua_State* L) {
    logCall(L, upLog(L), "notoriety_force_no_spawn", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil/absence gate at all
    EngineState* st = upState(L);
    int32_t code = st->lookupNotorietyTrackCode(name); // defaults to 9 (CONFIRMED not-found sentinel) when never registered
    static const uint32_t kCode5Bases[10] = {0x025e8740, 0x025e8ba0, 0x025eaee0, 0x025eb340, 0x025ea190,
                                              0x025ea600, 0x025eaa70, 0x025eb7a0, 0x025ebc10, 0x025ec080};
    static const uint32_t kCode6Bases[5] = {0x025ec4e0, 0x025ec940, 0x025ecdb0, 0x025ed220, 0x025ed690};
    switch (code) {
        case 5:
            for (uint32_t base : kCode5Bases) st->setNotorietyTrackFlag(base, value);
            break;
        case 6:
            for (uint32_t base : kCode6Bases) st->setNotorietyTrackFlag(base, value);
            break;
        case 1:
            st->setNotorietyTrackFlag(0x025e9000, value);
            break;
        case 2:
            st->setNotorietyTrackFlag(0x025e9460, value);
            break;
        case 3:
            st->setNotorietyTrackFlag(0x025e98c0, value);
            st->setNotorietyTrackFlag(0x025e9d20, value);
            break;
        default:
            break; // code 4 (CONFIRMED hard no-op) and any unrecognized/not-found code
    }
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB07() {
    static const std::vector<std::string> names = {
        // character_is_in_vehicle (Sec3.10, 2026-10-07) - see this file's
        // own doc comment above stub_character_is_in_vehicle for the full
        // citation/reasoning.
        "character_is_in_vehicle",
        // Section 9 batch (2026-10-07, full-document spec sweep) - see this
        // file's own batch header comment above stub_vehicle_speed_override
        // for the full citation/reasoning and the list of Sec9 names
        // deliberately NOT included. All 8 are `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (grepped directly, not
        // assumed).
        "vehicle_speed_override",
        "vehicle_infinite_mass",
        "set_unjackable_flag",
        "vehicle_exists",
        "teleport_vehicle",
        "effect_play",
        "effect_stop",
        "inv_weapon_disable_all_slots",
        // vehicle_chase (Sec9.4), dedicated pass, 2026-10-07 - see this
        // file's own batch header comment above stub_vehicle_chase for the
        // full citation/reasoning. `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (grepped directly, not
        // assumed).
        "vehicle_chase",
        // Section 8 batch (2026-10-07, full-document spec sweep) - see this
        // file's own batch header comment above stub_audio_conversation_end
        // for the full citation/reasoning and the list of Sec8 names
        // deliberately NOT included. All 7 are `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (grepped directly, not
        // assumed) EXCEPT audio_stop, which is additionally dual-registered
        // into the UI state (src/lua_host.cpp's own specUiNames.push_back
        // fix this batch, mirroring audio_object_post_event's own real
        // CONFIRMED dual-registration, spec-lua-bindings.md Sec13.5).
        "audio_conversation_end",
        "audio_conversation_load_direct",
        "audio_persona_remove_2d",
        "audio_stop",
        "audio_stop_emitting_id",
        "audio_is_playing",
        "audio_persona_line_playing",
        "audio_remove_mission_cellphone",
        // Team-A-cleared batch (commit 97a9b8d): spec-lua-api-behaviour.md
        // Sec1.1/1.2/1.4/1.5/1.6/2.1/2.5/2.6/3.2/3.3/3.5/3.6 - see this
        // file's own doc comment above each stub_* function for the full
        // citation/reasoning. Real cluster tags verified directly against
        // tools/lua_all_registered_1490_tagged.txt (grepped, not assumed):
        // all 12 are `gameplay` EXCEPT game_get_platform, which is `ui`;
        // game_is_active_input_gamepad is ALSO dual-registered into the UI
        // state (src/lua_host.cpp's own specUiNames, same pattern as
        // coop_is_active/audio_object_post_event above).
        "trigger_enable",
        "on_trigger",
        "objective_text_clear",
        "mission_end_failure",
        "mission_set_checkpoint",
        "game_get_platform",
        "audio_play_persona_line",
        "game_is_active_input_gamepad",
        "on_death",
        "on_respawn",
        "on_vehicle_destroyed",
        "character_is_dead",
        "notoriety_force_no_spawn",
    };
    return names;
}

void registerB07(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // character_is_in_vehicle (Sec3.10, 2026-10-07).
    if (wants("character_is_in_vehicle")) registerOne(L, state, log, stateTag, "character_is_in_vehicle", openGuard<stub_character_is_in_vehicle>);
    // Section 9 batch (2026-10-07, full-document spec sweep).
    if (wants("vehicle_speed_override")) registerOne(L, state, log, stateTag, "vehicle_speed_override", openGuard<stub_vehicle_speed_override>);
    if (wants("vehicle_infinite_mass")) registerOne(L, state, log, stateTag, "vehicle_infinite_mass", openGuard<stub_vehicle_infinite_mass>);
    if (wants("set_unjackable_flag")) registerOne(L, state, log, stateTag, "set_unjackable_flag", openGuard<stub_set_unjackable_flag>);
    if (wants("vehicle_exists")) registerOne(L, state, log, stateTag, "vehicle_exists", openGuard<stub_vehicle_exists>);
    if (wants("teleport_vehicle")) registerOne(L, state, log, stateTag, "teleport_vehicle", openGuard<stub_teleport_vehicle>);
    if (wants("effect_play")) registerOne(L, state, log, stateTag, "effect_play", openGuard<stub_effect_play>);
    if (wants("effect_stop")) registerOne(L, state, log, stateTag, "effect_stop", openGuard<stub_effect_stop>);
    if (wants("inv_weapon_disable_all_slots")) registerOne(L, state, log, stateTag, "inv_weapon_disable_all_slots", openGuard<stub_inv_weapon_disable_all_slots>);
    // vehicle_chase (Sec9.4), dedicated pass, 2026-10-07.
    if (wants("vehicle_chase")) registerOne(L, state, log, stateTag, "vehicle_chase", openGuard<stub_vehicle_chase>);
    // Section 8 batch (2026-10-07, full-document spec sweep).
    if (wants("audio_conversation_end")) registerOne(L, state, log, stateTag, "audio_conversation_end", openGuard<stub_audio_conversation_end>);
    if (wants("audio_conversation_load_direct")) registerOne(L, state, log, stateTag, "audio_conversation_load_direct", openGuard<stub_audio_conversation_load_direct>);
    if (wants("audio_persona_remove_2d")) registerOne(L, state, log, stateTag, "audio_persona_remove_2d", openGuard<stub_audio_persona_remove_2d>);
    if (wants("audio_stop")) registerOne(L, state, log, stateTag, "audio_stop", openGuard<stub_audio_stop>);
    if (wants("audio_stop_emitting_id")) registerOne(L, state, log, stateTag, "audio_stop_emitting_id", openGuard<stub_audio_stop>);
    if (wants("audio_is_playing")) registerOne(L, state, log, stateTag, "audio_is_playing", openGuard<stub_audio_is_playing>);
    if (wants("audio_persona_line_playing")) registerOne(L, state, log, stateTag, "audio_persona_line_playing", openGuard<stub_audio_persona_line_playing>);
    if (wants("audio_remove_mission_cellphone")) registerOne(L, state, log, stateTag, "audio_remove_mission_cellphone", openGuard<stub_audio_remove_mission_cellphone>);

    // Team-A-cleared batch (commit 97a9b8d): spec-lua-api-behaviour.md
    // Sec1.1/1.2/1.4/1.5/1.6/2.1/2.5/2.6/3.2/3.3/3.5/3.6 - see this file's
    // own doc comment above each stub_* function for the full citation.
    if (wants("trigger_enable")) registerOne(L, state, log, stateTag, "trigger_enable", openGuard<stub_trigger_enable>);
    if (wants("on_trigger")) registerOne(L, state, log, stateTag, "on_trigger", openGuard<stub_on_trigger>);
    if (wants("objective_text_clear")) registerOne(L, state, log, stateTag, "objective_text_clear", openGuard<stub_objective_text_clear>);
    if (wants("mission_end_failure")) registerOne(L, state, log, stateTag, "mission_end_failure", openGuard<stub_mission_end_failure>);
    if (wants("mission_set_checkpoint")) registerOne(L, state, log, stateTag, "mission_set_checkpoint", openGuard<stub_mission_set_checkpoint>);
    if (wants("game_get_platform")) registerOne(L, state, log, stateTag, "game_get_platform", openGuard<stub_game_get_platform>);
    if (wants("audio_play_persona_line")) registerOne(L, state, log, stateTag, "audio_play_persona_line", openGuard<stub_audio_play_persona_line>);
    if (wants("game_is_active_input_gamepad")) registerOne(L, state, log, stateTag, "game_is_active_input_gamepad", openGuard<stub_game_is_active_input_gamepad>);
    if (wants("on_death")) registerOne(L, state, log, stateTag, "on_death", openGuard<stub_on_death>);
    if (wants("on_respawn")) registerOne(L, state, log, stateTag, "on_respawn", openGuard<stub_on_respawn>);
    if (wants("on_vehicle_destroyed")) registerOne(L, state, log, stateTag, "on_vehicle_destroyed", openGuard<stub_on_vehicle_destroyed>);
    if (wants("character_is_dead")) registerOne(L, state, log, stateTag, "character_is_dead", openGuard<stub_character_is_dead>);
    if (wants("notoriety_force_no_spawn")) registerOne(L, state, log, stateTag, "notoriety_force_no_spawn", openGuard<stub_notoriety_force_no_spawn>);
}

} // namespace stub_sections
} // namespace sr3luahost
