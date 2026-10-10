// lua_spec_confirmed_stubs_b06_playerrt_sec6_7.cpp
//
// The player runtime batch (player_controls_disable/enable, player_movement_disable) + the 2026-10-07 Section 6 full-document-sweep batch + the 2026-10-07 Section 7 full-document-sweep batch (which includes player_movement_enable, the 4th player-runtime name).
//
// One of 7 files split 2026-10-07 from the former single 8,700+ line
// lua_spec_confirmed_stubs.cpp (a pure structural refactor, zero behaviour
// change - every stub body, comment and citation below is reproduced
// verbatim from that file). See lua_spec_confirmed_stubs_registry.cpp's own
// top comment for the full split rationale and the shared-registration
// mechanism (lua_spec_confirmed_stubs_sections.h/_common.h).
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// =======================================================================
// Player runtime batch (2026-10-03): player_controls_disable (Sec7.22),
// player_controls_enable (Sec7.7), player_movement_disable (Sec7.25),
// wired to the real control-state stack and input context
// (spec-player-runtime.md Sec2.5/Sec2.6/Sec2.9 and Sec1.11.1;
// include/sr3player/). All three resolve through the generic/#PLAYER#
// chain 0x00a280c0 (this project's shared objectResolves() step) and
// share the single-gate (0x008addb0, variant 2) idiom on the byte +0x1d99:
//   * gate TRUE -> direct apply (modelled in full below);
//   * gate FALSE -> "checks a co-op session object (0x0087ba20) is non-null
//     AND a session state-counter comparison" before opening an opcode-0x32
//     record (numeric modes 0/1 disable, 2 enable, 3/4 movement-disable).
// The gate is CharacterState::controlLockGatePasses, seeded TRUE (CHOSEN,
// this project's single-player-only simplification; see that field's doc
// comment). The gate-false branch is still modelled structurally: no
// session -> silent no-op; a session present -> the state-counter
// comparison is not modelled, so it refuses as OPEN instead of guessing.
//
// Real mission-drive call shape (verdict_stub_hits_with_missions.tsv,
// results/verify_getdist_playernames_mission_drive, 721 ticks):
// player_controls_disable 37 calls / player_controls_enable 30 calls, both
// last_argc=1 (name only); player_movement_disable 0 calls (absent).
// =======================================================================

// Gate-false branch shared by all three (see the batch header above).
void controlLockCoopRecord(EngineState* st, const char* tag, int mode) {
    if (!st->coopSession().present.get()) return;  // CONFIRMED initial false (Sec26.28): no session -> no record, no write
    throw OpenStateError(std::string("co-op session state-counter comparison before the opcode-0x32 record (") + tag +
                             ", mode " + std::to_string(mode) + ")",
                         "spec-lua-api-behaviour.md Sec7.7/Sec7.22/Sec7.25");
}

// 0x009e2dd0's direct-apply branch (Sec7.22), shared with player_controls_
// enable's delegation (Sec7.7). Writes bit 0x08 of +0x1d99 to `disable`;
// when the value changes:
//   -> disabled, and the lock handle 0x0263ae3c was null: push state 1 on
//      the control-state stack (0x005aa780(1), spec-player-runtime.md
//      Sec2.5) storing the returned handle, then 0x00db8680 (not read in any
//      spec - not modelled) and "clear latched" 0x005bbf60 (Sec1.11.1),
//      then - if the getter 0x00941f40 succeeds - 0x009bc0d0 and 0x009c7cc0
//      (roles OPEN in Sec7.22 - not modelled);
//   -> enabled, and the handle was non-null: pop that exact handle
//      (0x005aa740) and clear it.
// The closing notify 0x0083bf20 (role OPEN) is not modelled. Every read
// happens before any write, so a refusal would leave no half-applied state
// if either ever read OPEN - but both are now CONFIRMED/HIGH seeded at
// construction/session-start (spec-player-runtime.md Sec2.10,
// CharacterState::controlsDisabled and EngineState::playerControlsLockHandle_
// own doc comments), so a plain .get() is used directly; the earlier probe-
// mode placeholder (EngineState::controlLockOr()) this comment used to cite
// is gone - it existed only to widen these same two fields while they were
// genuinely OPEN, and nothing else in this codebase ever used it.
void applyControlsDisabledDirect(EngineState* st, CharacterState& c, bool disable) {
    const bool wasDisabled = c.controlsDisabled.get();
    if (wasDisabled != disable) {
        auto& handle = st->playerControlsLockHandle();
        const uint32_t h = handle.get();
        if (disable && h == 0) {
            handle.set(st->controlStateMachine().push(sr3player::kLockedStateUi));
            st->inputSystem().clearLatched();
        } else if (!disable && h != 0) {
            st->controlStateMachine().pop(h);
            handle.set(0);
        }
    }
    c.controlsDisabled.set(disable);
}

// ---------------------------------------------------------------------
// player_controls_disable (Sec7.22, 0x00a59630). 1 mandatory string; 1
// boolean read with NO nil/absence gate (lua_toboolean: absent -> false).
// No return. CHOSEN reading of the boolean: the spec ties it only to the
// gate-false record's numeric mode (0 for false, 1 for true); the
// direct-apply branch "writes the bit directly" and player_controls_enable
// re-enters that same branch "passing false", so the delegate's own value
// argument is the DISABLED state - read here as always true from this
// native (a disable call that enabled controls would contradict its name).
// What, if anything, the Lua boolean changes on the direct branch is not
// stated by the spec and is not modelled.
// ---------------------------------------------------------------------
int stub_player_controls_disable(lua_State* L) {
    logCall(L, upLog(L), "player_controls_disable", upStateTag(L));
    std::string name = argString(L, 1);
    const bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0;  // OPEN until set; unresolved -> no-op
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.controlLockGatePasses.get()) {
        applyControlsDisabledDirect(st, c, true);
    } else {
        controlLockCoopRecord(st, "player_controls_disable", flag ? 1 : 0);
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_controls_enable (Sec7.7, 0x00a59670). 1 mandatory string, no
// boolean at all. No return. Gate true (0x009e4940): does NOT
// unconditionally clear the bit - only if bit 0x08 is currently set AND the
// lock handle 0x0263ae3c is non-null does it re-enter 0x009e2dd0's direct
// branch with false; otherwise a no-op (so an enable with the bit set but
// no handle leaves the bit set - reproduced as specified).
// ---------------------------------------------------------------------
int stub_player_controls_enable(lua_State* L) {
    logCall(L, upLog(L), "player_controls_enable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0;  // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.controlLockGatePasses.get()) {
        if (c.controlsDisabled.get() && st->playerControlsLockHandle().get() != 0)
            applyControlsDisabledDirect(st, c, false);
    } else {
        controlLockCoopRecord(st, "player_controls_enable", 2);
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_movement_disable (Sec7.25, 0x00a59780). 1 mandatory string; 1
// boolean, no nil/absence gate. No return. Gate true (0x009e2fa0): writes
// bit 0x10 of +0x1d99 directly - no control-state-stack bookkeeping. Same
// CHOSEN reading of the boolean as player_controls_disable (record mode 3
// for false / 4 for true on the gate-false branch only; the direct write is
// "disabled").
// ---------------------------------------------------------------------
int stub_player_movement_disable(lua_State* L) {
    logCall(L, upLog(L), "player_movement_disable", upStateTag(L));
    std::string name = argString(L, 1);
    const bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0;  // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.controlLockGatePasses.get()) {
        c.movementDisabled.set(true);
    } else {
        controlLockCoopRecord(st, "player_movement_disable", flag ? 4 : 3);
    }
    return 0;
}

// ---------------------------------------------------------------------
// Native-implementation batch 2026-10-07 (fresh mission-drive stub-hit
// ranking against a from-scratch build, cross-referenced against spec
// coverage - see this merge's own commit message for the full ranked list
// and selection reasoning). 16 names: audio_object_post_event (Sec2.4),
// character_is_ready (Sec7.27), character_playing_combat_move (Sec25.27),
// mesh_mover_hide/_show/_reset/_set_invulnerable (Sec9.16/9.17/9.12/14.9),
// door_close/door_lock (Sec9.14/9.15), object_spawn_pause/
// mip_streaming_pause (Sec60.1), city_zone_swap (Sec1.9),
// traffic_disable_lanes (Sec12.19), party_dismiss_all (Sec14.17),
// sidewalk_disable_nodes (Sec15.3), group_create_check_done (Sec22.18).
// Sec1.9/Sec2.4 both sit inside the manager's own 2026-09-30 "spec-lua-
// api-behaviour.md Sec1-5 NOT yet cleared for implementation" freeze
// (spec_confirmed_stubs.h's own top comment) but are individually
// executable-re-derived and cleared by Sec60.3's own text ("re-verified...
// no corrections needed") - implemented here on that specific clearance,
// not the general freeze. See engine_state.h's own per-accessor doc
// comments (added this same batch) for the shared-state reasoning; each
// function below cites its own specific behaviour and OPEN gaps.
//
// Deliberately NOT implemented this batch, despite real measured call
// volume and/or spec text existing: trigger_enable/on_trigger/
// on_trigger_exit/door_open (Sec1.1/1.2/1.7/1.10) and character_is_dead/
// notoriety_force_no_spawn (Sec3.5/3.6) - all sit inside the Sec1-5 freeze
// and, unlike Sec1.9/Sec2.4 above, have NOT been individually re-derived/
// cleared by any later section (checked directly against every later
// changelog entry before excluding); vint_object_next_sibling/
// vint_insert_values_in_string - name-confirmed only (spec-lua-bindings.md
// Sec13.7/15's own text: the 54 sibling names are "not individually
// behavior-traced beyond create/destroy's shared resolver"), no real
// behaviour to implement without guessing; roadblock_create/effect_play/
// audio_persona_load_2d/on_mover_destroyed - real spec text exists but
// each needs either a genuinely OPEN success/failure determination
// (roadblock_create's own placement-acceptance test), a new handle/
// template-registry subsystem disproportionate to its call count, or (on_
// mover_destroyed) hook-array registration semantics this batch's time
// budget did not reach; character_is_in_vehicle (Sec3.10, the single
// highest real call count this project has ever measured for an
// unimplemented name) - at the time of THIS batch, a Team A agent was
// still closing this section's one remaining OPEN item, so it was not
// duplicated here. UPDATE 2026-10-07 (later the same day): that item was
// resolved and Sec3.10 was implemented in its own separate batch - see
// stub_character_is_in_vehicle, further down this file.
// ---------------------------------------------------------------------

// audio_object_post_event (Sec2.4, 0x00a3cb00). Arguments: 1 mandatory
// string (event name) + up to 4 further optional (arg2/arg3: string-or-
// table-of-<=2; arg4: string, a game-object/position name; arg5: boolean,
// default true). This project models no real Wwise ID table anywhere
// (same stated gap game_audio_get_audio_id/Sec8.22 already documents) -
// rather than throw on every real call (which would regress every mission
// that currently passes this call silently today, unlike game_audio_get_
// audio_id whose whole job IS the id), this follows game_UI_audio_play's
// own precedent (Sec2.2): every event name is treated as "resolves,"
// handing back a fresh instance handle from the SAME shared counter
// (EngineState::nextEffectPostHandle()) rather than modelling Wwise
// resolution failure. CHOSEN simplification, not a guess at a specific
// Wwise id. CONFIRMED, independent of the id question: the arg2/arg3
// table-walk helper's own t.n-then-pairs-count shape, and the "a table
// reporting MORE than 2 elements aborts the whole function with push 0;
// return 1" failure path (the one failure mode here that does NOT depend
// on Wwise id resolution at all). Explicit gap: arg4's position
// resolution and arg5's boolean are read (to match real argument
// consumption) but not acted on further - this project does not model
// audio-object positions anywhere (same stated boundary as every other
// audio native in scope). The mismatched-arg2/arg3-count "exceptional 2
// values" path (Sec2.4's own OPEN item, not resolved by Sec60.3's text
// either) is settled via the C API's own actual mechanics rather than
// guessed: an extra push before the handle plus declaring exactly 1
// return value makes Lua see only the handle regardless, per the spec's
// own observation ("a literal return 1 Lua sees only the handle and the
// extra 0 is discarded") - so this always returns exactly 1 value.
int tableWalkCountForAudioEvent(lua_State* L, int argIndex) {
    // -1 = absent/nil (or not string/table - not a described branch);
    // -2 = invalid shape (more than 2 resolved elements, the CONFIRMED
    // immediate-failure condition); else the resolved count (0/1/2).
    if (lua_gettop(L) < argIndex || lua_isnil(L, argIndex)) return -1;
    int t = lua_type(L, argIndex);
    if (t == LUA_TSTRING) return 1;
    if (t != LUA_TTABLE) return -1;
    lua_getfield(L, argIndex, "n");
    int count = -1;
    if (lua_type(L, -1) == LUA_TNUMBER) count = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    if (count < 0) {
        count = 0;
        lua_pushnil(L);
        while (lua_next(L, argIndex) != 0) {
            ++count;
            lua_pop(L, 1);
        }
    }
    return count > 2 ? -2 : count;
}

int stub_audio_object_post_event(lua_State* L) {
    logCall(L, upLog(L), "audio_object_post_event", upStateTag(L));
    if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) {
        lua_pushnumber(L, 0);
        return 1;
    }
    if (tableWalkCountForAudioEvent(L, 2) == -2 || tableWalkCountForAudioEvent(L, 3) == -2) {
        lua_pushnumber(L, 0);
        return 1;
    }
    if (lua_gettop(L) >= 4 && lua_isstring(L, 4)) (void)argString(L, 4); // consumed, not modelled (see doc comment)
    int64_t handle = upState(L)->nextEffectPostHandle();
    lua_pushnumber(L, static_cast<lua_Number>(handle));
    return 1;
}

// character_is_ready (Sec7.27, 0x00a41920). CONFIRMED early-exit: a dead
// (not isAlive) resolved character always answers false - reuses
// CharacterState::isAlive, the SAME 0x0096f4f0 predicate Sec7.27's own
// text says character_is_dead (Sec3.5) uses; character_is_dead itself
// sits inside the Sec1-5 freeze and is NOT individually re-cleared, so it
// is NOT implemented as its own native this batch, but reusing the
// already-modelled STATE FIELD here is not new behaviour from that frozen
// section, only from Sec7.27 itself. The second OR-condition (0x009a1dc0,
// not decompiled) and the whole further AND-chain (class-descriptor +10
// bit 0x04, global debug flag 0x014ff6b8, an unconfirmed name-tag marker-
// character convention, then gate 0x008addb0's own deep branch) are
// genuinely OPEN past the dead-check - refused rather than guessed.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_is_ready(lua_State* L) {
    logCall(L, upLog(L), "character_is_ready", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (!c.isAlive.get()) { // OPEN until set (0x0096f4f0)
        lua_pushboolean(L, 0);
        return 1;
    }
    if (st->characterReadyBlocked().get(resolvedName)) { // OPEN until set (0x009a1dc0)
        lua_pushboolean(L, 0);
        return 1;
    }
    throw OpenStateError(
        "character_is_ready AND-chain (class-descriptor +10 bit 0x04 / debug flag 0x014ff6b8 / gate 0x008addb0 deep branch)",
        "spec-lua-api-behaviour.md Sec7.27");
}

// character_playing_combat_move (Sec25.27, 0x00a41c60). CONFIRMED: resolve
// failure pushes false; on success, queries a per-character predicate
// (0x009634f0, not decompiled) - OPEN until a test seeds it.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_playing_combat_move(lua_State* L) {
    logCall(L, upLog(L), "character_playing_combat_move", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    bool playing = st->characterPlayingCombatMove().get(resolvedName); // OPEN until set (0x009634f0)
    lua_pushboolean(L, playing ? 1 : 0);
    return 1;
}

// mesh_mover_hide (Sec9.16, 0x00a52f90). CONFIRMED: resolve+liveness via
// the shared placed-object resolver (0x006434a0, gated through the SAME
// objectResolves() map every other object kind in scope already uses -
// see EngineState::getOrCreatePlacedObject()'s own doc comment); on
// success sets bit 0x1 of the mover's own +0x82 flags byte and pushes
// true; false on resolve failure. The secondary-object visibility-toggle/
// type-row-notify internals (OPEN, secondary object's exact class per
// WALLS.md) are not modelled - this project tracks only the one CONFIRMED
// own-object flags-byte bit.
int stub_mesh_mover_hide(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_hide", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    PlacedObjectState& o = st->getOrCreatePlacedObject(name);
    o.flags082 |= PlacedObjectState::kMeshMoverHiddenBit;
    lua_pushboolean(L, 1);
    return 1;
}

// mesh_mover_show (Sec9.17, 0x00a53440) - the structural mirror of
// mesh_mover_hide (Sec9.16) above: same resolver, same flags-byte bit,
// cleared here rather than set.
int stub_mesh_mover_show(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_show", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    PlacedObjectState& o = st->getOrCreatePlacedObject(name);
    o.flags082 &= static_cast<uint8_t>(~PlacedObjectState::kMeshMoverHiddenBit);
    lua_pushboolean(L, 1);
    return 1;
}

// mesh_mover_reset (Sec9.12, 0x00a53250). CONFIRMED: resolve+liveness via
// the shared placed-object resolver, then a virtual method (+0x60) resets
// the mover - true on success, false on resolve failure. The resolved
// object's exact class and hence what "reset" concretely restores is OPEN
// - not modelled (this project tracks no mesh-mover transform/geometry
// state at all).
int stub_mesh_mover_reset(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_reset", upStateTag(L));
    std::string name = argString(L, 1);
    bool resolved = upState(L)->objectResolves().get(name); // OPEN until set
    lua_pushboolean(L, resolved ? 1 : 0);
    return 1;
}

// mesh_mover_set_invulnerable (Sec14.9, 0x00a533b0). CONFIRMED: 1
// mandatory string + 1 mandatory boolean. Return reflects resolve success
// (NOT the invulnerable state itself, per spec). On success, unconditionally
// sets bit 0x8 of the mover's own +0x82 flags byte to arg2 (CONFIRMED,
// independent of any gate). The conditional network-replicate (gated on an
// unidentified global, 0x027038c0, OPEN) and the vtable-+0x68-gated +0xc0
// sub-object bit 0x80 write are not modelled - this project builds no real
// networking layer (EngineState::replicateStateChange's own doc comment)
// and the vtable predicate is undecompiled.
int stub_mesh_mover_set_invulnerable(lua_State* L) {
    logCall(L, upLog(L), "mesh_mover_set_invulnerable", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name); // OPEN until set
    if (resolved) {
        PlacedObjectState& o = st->getOrCreatePlacedObject(name);
        if (value) o.flags082 |= PlacedObjectState::kMeshMoverInvulnerableBit;
        else o.flags082 &= static_cast<uint8_t>(~PlacedObjectState::kMeshMoverInvulnerableBit);
    }
    lua_pushboolean(L, resolved ? 1 : 0);
    return 1;
}

// door_close (Sec9.14, 0x00a48630). 1 mandatory string + 1 optional
// boolean (default true). CONFIRMED: resolve+liveness via the shared
// placed-object resolver, then clears bits 0x10/0x20 of the door's own
// +0x82 flags byte (shared byte with door_lock's own bit 0x4, Sec9.15).
// The pushed return value's own exact source is OPEN (the setter's own
// decompiled body shows no explicit return statement) - this follows the
// SAME family's sibling convention (mesh_mover_reset/_hide/_show, all
// CONFIRMED "true on success, false on resolve failure") as the best-
// available, non-invented reading (HIGH CONFIDENCE, not independently
// confirmed for door_close specifically). The boolean's own forward to any
// attached lock/mechanism object (+0xc8, corroborating door_open's own
// OPEN reading there) is not modelled - this project tracks no attached-
// mechanism linkage between placed objects.
int stub_door_close(lua_State* L) {
    logCall(L, upLog(L), "door_close", upStateTag(L));
    std::string name = argString(L, 1);
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) (void)(lua_toboolean(L, 2) != 0); // consumed, not modelled further (see doc comment)
    EngineState* st = upState(L);
    bool resolved = st->objectResolves().get(name); // OPEN until set
    if (resolved) {
        PlacedObjectState& o = st->getOrCreatePlacedObject(name);
        o.flags082 &= static_cast<uint8_t>(~PlacedObjectState::kDoorCloseTransientBits);
    }
    lua_pushboolean(L, resolved ? 1 : 0);
    return 1;
}

// door_lock (Sec9.15, 0x00a487e0). 2 mandatory: string + boolean
// (unconditional, no nil-gate). CONFIRMED: resolve+liveness via the shared
// placed-object resolver, then writes the boolean into bit 0x4 of the
// door's own +0x82 flags byte (shared with door_close's own transient
// bits). No return value (return 0). The re-entrancy guard, network-
// replicate (opcode 0x3a), and the +0xc8 attached-mechanism forward are
// not modelled (same boundary as door_close above).
int stub_door_lock(lua_State* L) {
    logCall(L, upLog(L), "door_lock", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        PlacedObjectState& o = st->getOrCreatePlacedObject(name);
        if (value) o.flags082 |= PlacedObjectState::kDoorLockBit;
        else o.flags082 &= static_cast<uint8_t>(~PlacedObjectState::kDoorLockBit);
    }
    return 0;
}

// object_spawn_pause (Sec60.1) - CONFIRMED the shared no-op stub (the SAME
// inert handler this document's Sec50/Sec57 changelog entries already
// identify backing dozens of other registered names project-wide). Real
// observed call shape (verdict_stub_hits_with_missions.tsv): 2 args,
// `boolean,string`. Consumes nothing further; does nothing; no return.
int stub_object_spawn_pause(lua_State* L) {
    logCall(L, upLog(L), "object_spawn_pause", upStateTag(L));
    return 0;
}

// mip_streaming_pause (Sec60.1) - CONFIRMED real cross-script collision:
// every call toggles the SAME single shared bit regardless of the string
// argument (see EngineState::mipStreamingPaused()'s own doc comment). Real
// observed call shape: 2 args, `boolean,string` - arg1 is the pause/resume
// flag. No return value is stated either way; mirrors this project's
// convention for an unstated-return command (no push).
int stub_mip_streaming_pause(lua_State* L) {
    logCall(L, upLog(L), "mip_streaming_pause", upStateTag(L));
    bool pause = lua_toboolean(L, 1) != 0;
    upState(L)->setMipStreamingPaused(pause);
    return 0;
}

// city_zone_swap (Sec1.9, 0x00a43460; individually executable-re-derived
// and cleared by Sec60.3 despite the manager's own Sec1-5 freeze - see
// this file's own batch header comment above). 1 mandatory string + 1
// optional boolean (default true). See EngineState::recordCityZoneSwap()'s
// own doc comment for why neither internal path (immediate-apply vs
// queued) needs to be chosen here.
int stub_city_zone_swap(lua_State* L) {
    logCall(L, upLog(L), "city_zone_swap", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = true;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) value = lua_toboolean(L, 2) != 0;
    upState(L)->recordCityZoneSwap(name, value);
    return 0;
}

// traffic_disable_lanes (Sec12.19, 0x00a61760; individually cleared by
// Sec60.3). 2 mandatory: string (trigger name) + boolean (enable/disable),
// no nil-gates. CONFIRMED: resolve via the shared trigger/object resolver,
// then enable/disable the named lane set. The terminal dispatch's own
// internal behaviour (0x00a6dd70, shared with traffic_disable_drawbridge_
// area/Sec6.4) is OPEN - not modelled further.
int stub_traffic_disable_lanes(lua_State* L) {
    logCall(L, upLog(L), "traffic_disable_lanes", upStateTag(L));
    std::string name = argString(L, 1);
    bool enabled = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->setTrafficLanesEnabled(name, enabled);
    }
    return 0;
}

// party_dismiss_all (Sec14.17, 0x00a593f0; individually cleared by
// Sec60.3). Zero-argument command - dismisses the local player's party
// roster (both representations when a co-op session is present, per the
// SAME 0x009df3d0 check satellite_weapon_mode_exit/Sec32.1 already
// reuses - not separately re-checked here, since this project has no real
// roster-membership walk for either representation; same "no registry"
// simplification class as groupsCreated_ below). Call count only.
int stub_party_dismiss_all(lua_State* L) {
    logCall(L, upLog(L), "party_dismiss_all", upStateTag(L));
    upState(L)->recordPartyDismissAll();
    return 0;
}

// sidewalk_disable_nodes (Sec15.3, 0x00a5f710; individually cleared by
// Sec60.3). 2 arguments: string (mandatory) + boolean (read unconditionally,
// effectively mandatory - no nil-gate, per spec). CONFIRMED: resolve via
// the shared trigger/object resolver, then enable/disable the named node
// group. The vtable `+0x58` method's own identity and the exact semantics
// of the final apply (`0x00bc0970`) are OPEN - not modelled further.
int stub_sidewalk_disable_nodes(lua_State* L) {
    logCall(L, upLog(L), "sidewalk_disable_nodes", upStateTag(L));
    std::string name = argString(L, 1);
    bool disabled = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->setSidewalkNodesDisabled(name, disabled);
    }
    return 0;
}

// group_create_check_done (Sec22.18, 0x00a4c810). See EngineState::
// groupCreateCheckDone()'s own doc comment for the full "no member
// registry -> vacuously done" reasoning. 1 mandatory string.
int stub_group_create_check_done(lua_State* L) {
    logCall(L, upLog(L), "group_create_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    bool done = upState(L)->groupCreateCheckDone(name);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// =======================================================================
// Section 6 batch (2026-10-07, full-document spec sweep): set_ped_density/
// set_traffic_density (Sec6.9/Sec6.14), notoriety_set_can_decay (Sec6.18),
// waypoint_remove (Sec6.17), spawning_pedestrians/spawning_vehicles
// (Sec6.13/Sec6.15), traffic_disable_drawbridge_area (Sec6.4),
// guardian_angel_register_script_group/guardian_angel_unregister_
// script_group (Sec6.8/Sec6.6). See each one's own EngineState accessor
// doc comment (engine_state.h, right before `private:`) for the full
// citation/reasoning this batch rests on, and that same block's own top
// comment for exactly which other Sec6 names were deliberately left out
// this pass and why (the notoriety track-value setters and group/activity
// resolvers needing a real alias/group-name table this spec text does not
// supply; the three `mission_*` entries sharing an unmodelled "mission is
// active" gate; `tutorial_start`'s disproportionate 210-entry dispatcher).
// =======================================================================

int stub_set_ped_density(lua_State* L) {
    logCall(L, upLog(L), "set_ped_density", upStateTag(L));
    upState(L)->setPedDensity(static_cast<float>(lua_tonumber(L, 1))); // 1 mandatory number, no nil-gate (CONFIRMED)
    return 0;
}

int stub_set_traffic_density(lua_State* L) {
    logCall(L, upLog(L), "set_traffic_density", upStateTag(L));
    upState(L)->setTrafficDensity(static_cast<float>(lua_tonumber(L, 1))); // 1 mandatory number, no nil-gate (CONFIRMED)
    return 0;
}

int stub_notoriety_set_can_decay(lua_State* L) {
    logCall(L, upLog(L), "notoriety_set_can_decay", upStateTag(L));
    bool canDecay = lua_toboolean(L, 1) != 0; // 1 mandatory boolean, no nil-gate (CONFIRMED)
    upState(L)->setNotorietyCanDecay(canDecay); // stored inverted - see EngineState's own doc comment
    return 0;
}

// waypoint_remove (Sec6.17): the mode argument is "passed unmodified" per
// the spec's own Body text (not put through the shared round-cast pair) -
// a direct cast is used here, not truncateEa2596, to avoid implying a
// rounding convention the spec does not state for this specific argument.
int stub_waypoint_remove(lua_State* L) {
    logCall(L, upLog(L), "waypoint_remove", upStateTag(L));
    double modeRaw = optionalNumberDefault(L, 1, 3.0); // 1 optional number, default 3 (CONFIRMED)
    upState(L)->recordWaypointRemove(static_cast<int32_t>(modeRaw));
    return 0;
}

int stub_spawning_pedestrians(lua_State* L) {
    logCall(L, upLog(L), "spawning_pedestrians", upStateTag(L));
    bool enable = lua_toboolean(L, 1) != 0; // arg1: mandatory boolean, no gate (CONFIRMED)
    // arg2 (optional boolean, default true) only gates a further cross-array
    // cleanup call whose own internal effect is OPEN (see EngineState::
    // pedestrianSpawningEnabled()'s own doc comment) - consumed, not modelled.
    (void)optionalBoolDefault(L, 2, true);
    upState(L)->setPedestrianSpawningEnabled(enable);
    return 0;
}

int stub_spawning_vehicles(lua_State* L) {
    logCall(L, upLog(L), "spawning_vehicles", upStateTag(L));
    bool enable = lua_toboolean(L, 1) != 0; // 1 mandatory boolean, no gate (CONFIRMED)
    upState(L)->setVehicleSpawningEnabled(enable);
    return 0;
}

// traffic_disable_drawbridge_area (Sec6.4): arg1 is explicitly "number,
// rounded" per spec Body text, the SAME shared round-cast-pair convention
// (truncateEa2596) this document uses throughout; builds the fixed
// "Spline_Disabler<NNN>" trigger name and resolves it through the SAME
// shared generic resolver objectResolves() already models project-wide.
int stub_traffic_disable_drawbridge_area(lua_State* L) {
    logCall(L, upLog(L), "traffic_disable_drawbridge_area", upStateTag(L));
    int32_t area = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 1)));
    bool disable = lua_toboolean(L, 2) != 0;
    char nameBuf[32];
    std::snprintf(nameBuf, sizeof(nameBuf), "Spline_Disabler<%03d>", area);
    EngineState* st = upState(L);
    if (st->objectResolves().get(nameBuf)) { // OPEN until set
        st->setDrawbridgeAreaDisabled(area, disable);
    }
    return 0;
}

int stub_guardian_angel_register_script_group(lua_State* L) {
    logCall(L, upLog(L), "guardian_angel_register_script_group", upStateTag(L));
    std::string name = argString(L, 1); // 1 mandatory string, no nil-gate (CONFIRMED)
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->setGuardianAngelGroupRegistered(name, true);
    }
    return 0;
}

int stub_guardian_angel_unregister_script_group(lua_State* L) {
    logCall(L, upLog(L), "guardian_angel_unregister_script_group", upStateTag(L));
    std::string name = argString(L, 1); // 1 mandatory string, no nil-gate (CONFIRMED)
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->setGuardianAngelGroupRegistered(name, false);
    }
    return 0;
}

// =======================================================================
// Section 7 batch (2026-10-07, full-document spec sweep): character_exists
// (Sec7.9), human_is_downed (Sec7.13), character_show (Sec7.15),
// character_ragdoll_set_last_resort_position (Sec7.18), on_damage (Sec7.4),
// on_mover_destroyed (Sec7.16), on_detection (Sec7.23), follower_make_
// independent (Sec7.2), follower_set_can_abandon (Sec7.6), set_dont_
// attack_me_on_sight_flag (Sec7.14), npc_suppress_persona (Sec7.19),
// clear_animation_state (Sec7.3), character_prevent_explosion_fling
// (Sec7.20), character_prevent_bumping (Sec7.26), character_set_never_
// catch_fire (Sec7.29), ai_scripted_action_is_complete (Sec7.28),
// player_movement_enable (Sec7.8). See each new field's own doc comment
// (engine_state.h, CharacterState/PlacedObjectState) for the full
// citation/reasoning this batch rests on.
//
// Deliberately NOT in this batch (left for a future pass): npc_leash_
// remove/npc_leash_to_nav (Sec7.1/Sec7.5) and set_attack_player_flag
// (Sec7.10)/human_gravity_enable (Sec7.32) - all MEDIUM complexity (real,
// implementable, but each needs a small new multi-field "raw request"
// record this batch's time budget did not reach, not a data gap);
// set_animation_state/set_script_animation_state (Sec7.11/Sec7.17) and
// ai_set_action_delay (Sec7.30) - all three need a real name-indexed table
// (0x004bf810's animation-state names, or 0x004f2610's 70-entry action-name
// table) whose actual CONTENTS this spec text does not supply - inventing
// them would be inventing data, forbidden by this project's clean-room
// discipline (same reasoning as Sec6's deferred notoriety-alias-table
// functions, see that batch's own header comment above).
// =======================================================================

int stub_character_exists(lua_State* L) {
    logCall(L, upLog(L), "character_exists", upStateTag(L));
    std::string name = argString(L, 1);
    // CONFIRMED structure: true if either the reserved-sentinel path
    // produced a handle or the ordinary character path's vtable query is
    // truthy - this project's objectResolves() map already IS the
    // project-wide "one shared name-resolves map" stand-in for every
    // per-kind resolver (Sec29's own finding), so both branches collapse
    // to one shared lookup here.
    bool exists = upState(L)->objectResolves().get(name); // OPEN until set
    lua_pushboolean(L, exists ? 1 : 0);
    return 1;
}

// human_is_downed (Sec7.13): reuses the SAME +0xcc8 state-enum field
// character_is_dead (Sec3.5)/character_kill (Sec3.15) already document -
// CharacterState::lifeState - value 6 = "downed," confirmed distinct from
// character_is_dead's own value 5.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_human_is_downed(lua_State* L) {
    logCall(L, upLog(L), "human_is_downed", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool downed = false;
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        downed = (st->getOrCreateCharacter(resolvedName).lifeState.get() == 6); // OPEN until set
    }
    lua_pushboolean(L, downed ? 1 : 0);
    return 1;
}

// character_show (Sec7.15): resolve-only - all 3 internal calls
// (0x009968c0/0x008ccb90/0x00941f40) are undecompiled OPEN bodies, same
// "resolve+arity, no modeled field write" precedent as
// ai_clear_scripted_action (Sec7.24) above. No return value.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_show(lua_State* L) {
    logCall(L, upLog(L), "character_show", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        (void)st->getOrCreateCharacter(resolvedName);
    }
    return 0;
}

// character_ragdoll_set_last_resort_position (Sec7.18): resolve via the
// 5th resolver (modeled via the SAME shared objectResolves() map, see
// Sec7.5/Sec7.11's own cross-reference) - 0x009a2110's own body is OPEN,
// not modelled further. Same resolve-only precedent as character_show.
int stub_character_ragdoll_set_last_resort_position(lua_State* L) {
    logCall(L, upLog(L), "character_ragdoll_set_last_resort_position", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        (void)st->getOrCreateCharacter(name);
    }
    return 0;
}

// on_damage (Sec7.4): a fourth on_* hook-registration function - see
// CharacterState::onDamageCallback's own doc comment for the "only
// character-shaped objects" simplification this mirrors from
// on_take_damage. Arg 3 (optional number, default an unread fixed
// constant) is consumed but not modelled further - no in-scope consumer
// reads it back.
int stub_on_damage(lua_State* L) {
    logCall(L, upLog(L), "on_damage", upStateTag(L));
    std::string cb = argString(L, 1);
    std::string target = argString(L, 2);
    (void)optionalNumberDefault(L, 3, 0.0);
    upState(L)->getOrCreateCharacter(target).onDamageCallback = cb;
    return 0;
}

// on_mover_destroyed (Sec7.16): resolves via the named-placed-object
// resolver (individually resolved identity for 0x006434a0, Sec9.21/
// Sec14.6) - registered unconditionally, same shape as on_take_damage.
int stub_on_mover_destroyed(lua_State* L) {
    logCall(L, upLog(L), "on_mover_destroyed", upStateTag(L));
    std::string cb = argString(L, 1);
    std::string target = argString(L, 2);
    upState(L)->getOrCreatePlacedObject(target).onMoverDestroyedCallback = cb;
    return 0;
}

// on_detection (Sec7.23): registered unconditionally, same shape as
// on_take_damage - the real double-write-to-the-same-slot/vtable-+0x70-
// requery detail is OPEN and not modelled (one field, last write wins).
int stub_on_detection(lua_State* L) {
    logCall(L, upLog(L), "on_detection", upStateTag(L));
    std::string cb = argString(L, 1);
    std::string target = argString(L, 2);
    upState(L)->getOrCreateCharacter(target).onDetectionCallback = cb;
    return 0;
}

// follower_make_independent (Sec7.2): resolves via 0x00a28150,
// resolveFollowerAwareObject() - wired 2026-10-08 (follower-sentinel-
// resolver pass). CONFIRMED double-gate (variant 1) setter - gated on
// CharacterState::forceFlagGatePasses (CONFIRMED true in single player,
// Sec3.7/Sec6.22).
int stub_follower_make_independent(lua_State* L) {
    logCall(L, upLog(L), "follower_make_independent", upStateTag(L));
    std::string name = argString(L, 1);
    bool independent = optionalBoolDefault(L, 2, false);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.independentFollower.set(independent);
    } else {
        EngineState::replicateStateChange("follower_make_independent", resolvedName);
    }
    return 0;
}

// follower_set_can_abandon (Sec7.6): resolves via 0x00a28150,
// resolveFollowerAwareObject() - wired 2026-10-08 (follower-sentinel-
// resolver pass). Same double-gate shape as follower_make_independent
// above.
int stub_follower_set_can_abandon(lua_State* L) {
    logCall(L, upLog(L), "follower_set_can_abandon", upStateTag(L));
    std::string name = argString(L, 1);
    bool canAbandon = optionalBoolDefault(L, 2, false);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.followerCanAbandon.set(canAbandon);
    } else {
        EngineState::replicateStateChange("follower_set_can_abandon", resolvedName);
    }
    return 0;
}

// set_dont_attack_me_on_sight_flag (Sec7.14): bool read with NO nil/
// absence gate (CONFIRMED) - same double-gate shape as above.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject(). (Supersedes
// the npc_suppress_persona note below, which predates this correction.)
int stub_set_dont_attack_me_on_sight_flag(lua_State* L) {
    logCall(L, upLog(L), "set_dont_attack_me_on_sight_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.dontAttackMeOnSight.set(value);
    } else {
        EngineState::replicateStateChange("set_dont_attack_me_on_sight_flag", resolvedName);
    }
    return 0;
}

// npc_suppress_persona (Sec7.19): resolves via 0x00a28150,
// resolveFollowerAwareObject() - wired 2026-10-08 (follower-sentinel-
// resolver pass); same double-gate shape as above. UPDATE 2026-10-08: the
// preceding set_dont_attack_me_on_sight_flag (Sec7.14) was thought to
// resolve via the DIFFERENT, PLAYER-only 0x00a281a0 and was deliberately
// left unwired - RE-CORRECTED (0x00a281a0 is itself a #PLAYER#-then-
// #FOLLOWER# two-stage wrapper, direct disassembly of its own body) and
// now wired identically, above.
int stub_npc_suppress_persona(lua_State* L) {
    logCall(L, upLog(L), "npc_suppress_persona", upStateTag(L));
    std::string name = argString(L, 1);
    bool suppress = optionalBoolDefault(L, 2, false);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.npcSuppressPersona.set(suppress);
    } else {
        EngineState::replicateStateChange("npc_suppress_persona", resolvedName);
    }
    return 0;
}

// clear_animation_state (Sec7.3): calls the SAME shared setter
// set_animation_state's own failure path falls back to (0x0094b070) with
// a fixed sentinel -1 - same double-gate shape as above, on a whole int
// field rather than a bit.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_clear_animation_state(lua_State* L) {
    logCall(L, upLog(L), "clear_animation_state", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.animationStateOverride.set(-1);
    } else {
        EngineState::replicateStateChange("clear_animation_state", resolvedName);
    }
    return 0;
}

// character_prevent_explosion_fling (Sec7.20): bool read with NO nil/
// absence gate - a 5th confirmed bit on the shared +0x1c98 flags field
// (CharacterState::forceFlags1c98).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_prevent_explosion_fling(lua_State* L) {
    logCall(L, upLog(L), "character_prevent_explosion_fling", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kPreventExplosionFlingBit1c98,
                                  value ? CharacterState::kPreventExplosionFlingBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_prevent_explosion_fling", resolvedName);
    }
    return 0;
}

// character_prevent_bumping (Sec7.26): a 6th confirmed bit on the same
// +0x1c98 flags field.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_prevent_bumping(lua_State* L) {
    logCall(L, upLog(L), "character_prevent_bumping", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kPreventBumpingBit1c98,
                                  value ? CharacterState::kPreventBumpingBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_prevent_bumping", resolvedName);
    }
    return 0;
}

// character_set_never_catch_fire (Sec7.29): a 7th confirmed bit on the
// same +0x1c98 flags field.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_never_catch_fire(lua_State* L) {
    logCall(L, upLog(L), "character_set_never_catch_fire", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kNeverCatchFireBit1c98,
                                  value ? CharacterState::kNeverCatchFireBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_set_never_catch_fire", resolvedName);
    }
    return 0;
}

// ai_scripted_action_is_complete (Sec7.28): CONFIRMED true on failed
// resolve (the only predicate in this batch defaulting true rather than
// false) - otherwise the "absence means complete" registry-lookup proxy,
// a single OpenValue-gated predicate (same shape as isAlive).
int stub_ai_scripted_action_is_complete(lua_State* L) {
    logCall(L, upLog(L), "ai_scripted_action_is_complete", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool complete = true;
    std::string resolvedName;
    // Resolved via 0x00a28150 (Sec7.28) - wired 2026-10-08 (follower-
    // sentinel-resolver pass).
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        complete = st->getOrCreateCharacter(resolvedName).scriptedActionLookupNonNull.get(); // OPEN until set
    }
    lua_pushboolean(L, complete ? 1 : 0);
    return 1;
}

// player_movement_enable (Sec7.8): the enable-side mirror of
// player_movement_disable - CONFIRMED no unconditional bit-flip on the
// direct-apply path, only a conditional clear-if-set (same shape as
// player_controls_enable, Sec7.7, but with no control-state-stack
// bookkeeping - "no extra session-lock bookkeeping is present here").
int stub_player_movement_enable(lua_State* L) {
    logCall(L, upLog(L), "player_movement_enable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.controlLockGatePasses.get()) {
        if (c.movementDisabled.get()) c.movementDisabled.set(false); // OPEN until set
    } else {
        controlLockCoopRecord(st, "player_movement_enable", 5);
    }
    return 0;
}


} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB06() {
    static const std::vector<std::string> names = {
        // Player runtime batch (2026-10-03): Sec7.22/Sec7.7/Sec7.25 wired to
        // spec-player-runtime.md's control-state stack. Registration was
        // briefly held back pending Team A's answer on two construction-time
        // values (character +0x1d99 byte, lock handle 0x0263ae3c) - a true
        // default-mode drive with strict OPEN refusal on both shifted 37/49
        // missions' own first_error_message from the Killbane-class blocker
        // to this pair's own OPEN item, confirmed directly before holding.
        // RESOLVED (spec-player-runtime.md Sec2.10, synced 2026-10-03):
        // handle 0x0263ae3c is CONFIRMED null (0) at session/mission start
        // (self-balancing - its only creator/destroyer is the disable/
        // enable apply itself); +0x1d99 is HIGH CONFIDENCE all-clear
        // (enabled) at spawn (no constructor touches it, rests on the pool
        // allocation being zero-filled - the same caveat Sec34.5 already
        // established for lifeState/killFinalizedFlagE3). Both fields now
        // seed real CONFIRMED/HIGH defaults at construction (CharacterState,
        // engine_state.h) instead of staying OpenValue-OPEN, so the strict
        // refusal this comment used to warn about no longer fires.
        "player_controls_disable",
        "player_controls_enable",
        "player_movement_disable",
        // Native-implementation batch 2026-10-07 (fresh mission-drive
        // stub-hit ranking) - see this file's own batch header comment
        // above stub_audio_object_post_event for the full citation list
        // and the deliberately-skipped names.
        "audio_object_post_event",
        "character_is_ready",
        "character_playing_combat_move",
        "mesh_mover_hide",
        "mesh_mover_show",
        "mesh_mover_reset",
        "mesh_mover_set_invulnerable",
        "door_close",
        "door_lock",
        "object_spawn_pause",
        "mip_streaming_pause",
        "city_zone_swap",
        "traffic_disable_lanes",
        "party_dismiss_all",
        "sidewalk_disable_nodes",
        "group_create_check_done",
        // Section 6 batch (2026-10-07, full-document spec sweep) - see this
        // file's own batch header comment above stub_set_ped_density for
        // the full citation/reasoning and the list of Sec6 names
        // deliberately NOT included. All 9 are `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (grepped directly, not
        // assumed).
        "set_ped_density",
        "set_traffic_density",
        "notoriety_set_can_decay",
        "waypoint_remove",
        "spawning_pedestrians",
        "spawning_vehicles",
        "traffic_disable_drawbridge_area",
        "guardian_angel_register_script_group",
        "guardian_angel_unregister_script_group",
        // Section 7 batch (2026-10-07, full-document spec sweep) - see this
        // file's own batch header comment above stub_character_exists for
        // the full citation/reasoning and the list of Sec7 names
        // deliberately NOT included. All 17 are `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (grepped directly, not
        // assumed).
        "character_exists",
        "human_is_downed",
        "character_show",
        "character_ragdoll_set_last_resort_position",
        "on_damage",
        "on_mover_destroyed",
        "on_detection",
        "follower_make_independent",
        "follower_set_can_abandon",
        "set_dont_attack_me_on_sight_flag",
        "npc_suppress_persona",
        "clear_animation_state",
        "character_prevent_explosion_fling",
        "character_prevent_bumping",
        "character_set_never_catch_fire",
        "ai_scripted_action_is_complete",
        "player_movement_enable",
    };
    return names;
}

void registerB06(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // Player runtime batch (2026-10-03) - see specConfirmedStubNames()'s own
    // comment above these same 3 names for the construction-default history.
    if (wants("player_controls_disable")) registerOne(L, state, log, stateTag, "player_controls_disable", openGuard<stub_player_controls_disable>);
    if (wants("player_controls_enable")) registerOne(L, state, log, stateTag, "player_controls_enable", openGuard<stub_player_controls_enable>);
    if (wants("player_movement_disable")) registerOne(L, state, log, stateTag, "player_movement_disable", openGuard<stub_player_movement_disable>);
    // Native-implementation batch 2026-10-07 - see this file's own batch
    // header comment above stub_audio_object_post_event.
    if (wants("audio_object_post_event")) registerOne(L, state, log, stateTag, "audio_object_post_event", openGuard<stub_audio_object_post_event>);
    if (wants("character_is_ready")) registerOne(L, state, log, stateTag, "character_is_ready", openGuard<stub_character_is_ready>);
    if (wants("character_playing_combat_move")) registerOne(L, state, log, stateTag, "character_playing_combat_move", openGuard<stub_character_playing_combat_move>);
    if (wants("mesh_mover_hide")) registerOne(L, state, log, stateTag, "mesh_mover_hide", openGuard<stub_mesh_mover_hide>);
    if (wants("mesh_mover_show")) registerOne(L, state, log, stateTag, "mesh_mover_show", openGuard<stub_mesh_mover_show>);
    if (wants("mesh_mover_reset")) registerOne(L, state, log, stateTag, "mesh_mover_reset", openGuard<stub_mesh_mover_reset>);
    if (wants("mesh_mover_set_invulnerable")) registerOne(L, state, log, stateTag, "mesh_mover_set_invulnerable", openGuard<stub_mesh_mover_set_invulnerable>);
    if (wants("door_close")) registerOne(L, state, log, stateTag, "door_close", openGuard<stub_door_close>);
    if (wants("door_lock")) registerOne(L, state, log, stateTag, "door_lock", openGuard<stub_door_lock>);
    if (wants("object_spawn_pause")) registerOne(L, state, log, stateTag, "object_spawn_pause", openGuard<stub_object_spawn_pause>);
    if (wants("mip_streaming_pause")) registerOne(L, state, log, stateTag, "mip_streaming_pause", openGuard<stub_mip_streaming_pause>);
    if (wants("city_zone_swap")) registerOne(L, state, log, stateTag, "city_zone_swap", openGuard<stub_city_zone_swap>);
    if (wants("traffic_disable_lanes")) registerOne(L, state, log, stateTag, "traffic_disable_lanes", openGuard<stub_traffic_disable_lanes>);
    if (wants("party_dismiss_all")) registerOne(L, state, log, stateTag, "party_dismiss_all", openGuard<stub_party_dismiss_all>);
    if (wants("sidewalk_disable_nodes")) registerOne(L, state, log, stateTag, "sidewalk_disable_nodes", openGuard<stub_sidewalk_disable_nodes>);
    if (wants("group_create_check_done")) registerOne(L, state, log, stateTag, "group_create_check_done", openGuard<stub_group_create_check_done>);
    // Section 6 batch (2026-10-07, full-document spec sweep).
    if (wants("set_ped_density")) registerOne(L, state, log, stateTag, "set_ped_density", openGuard<stub_set_ped_density>);
    if (wants("set_traffic_density")) registerOne(L, state, log, stateTag, "set_traffic_density", openGuard<stub_set_traffic_density>);
    if (wants("notoriety_set_can_decay")) registerOne(L, state, log, stateTag, "notoriety_set_can_decay", openGuard<stub_notoriety_set_can_decay>);
    if (wants("waypoint_remove")) registerOne(L, state, log, stateTag, "waypoint_remove", openGuard<stub_waypoint_remove>);
    if (wants("spawning_pedestrians")) registerOne(L, state, log, stateTag, "spawning_pedestrians", openGuard<stub_spawning_pedestrians>);
    if (wants("spawning_vehicles")) registerOne(L, state, log, stateTag, "spawning_vehicles", openGuard<stub_spawning_vehicles>);
    if (wants("traffic_disable_drawbridge_area")) registerOne(L, state, log, stateTag, "traffic_disable_drawbridge_area", openGuard<stub_traffic_disable_drawbridge_area>);
    if (wants("guardian_angel_register_script_group")) registerOne(L, state, log, stateTag, "guardian_angel_register_script_group", openGuard<stub_guardian_angel_register_script_group>);
    if (wants("guardian_angel_unregister_script_group")) registerOne(L, state, log, stateTag, "guardian_angel_unregister_script_group", openGuard<stub_guardian_angel_unregister_script_group>);
    // Section 7 batch (2026-10-07, full-document spec sweep).
    if (wants("character_exists")) registerOne(L, state, log, stateTag, "character_exists", openGuard<stub_character_exists>);
    if (wants("human_is_downed")) registerOne(L, state, log, stateTag, "human_is_downed", openGuard<stub_human_is_downed>);
    if (wants("character_show")) registerOne(L, state, log, stateTag, "character_show", openGuard<stub_character_show>);
    if (wants("character_ragdoll_set_last_resort_position")) registerOne(L, state, log, stateTag, "character_ragdoll_set_last_resort_position", openGuard<stub_character_ragdoll_set_last_resort_position>);
    if (wants("on_damage")) registerOne(L, state, log, stateTag, "on_damage", openGuard<stub_on_damage>);
    if (wants("on_mover_destroyed")) registerOne(L, state, log, stateTag, "on_mover_destroyed", openGuard<stub_on_mover_destroyed>);
    if (wants("on_detection")) registerOne(L, state, log, stateTag, "on_detection", openGuard<stub_on_detection>);
    if (wants("follower_make_independent")) registerOne(L, state, log, stateTag, "follower_make_independent", openGuard<stub_follower_make_independent>);
    if (wants("follower_set_can_abandon")) registerOne(L, state, log, stateTag, "follower_set_can_abandon", openGuard<stub_follower_set_can_abandon>);
    if (wants("set_dont_attack_me_on_sight_flag")) registerOne(L, state, log, stateTag, "set_dont_attack_me_on_sight_flag", openGuard<stub_set_dont_attack_me_on_sight_flag>);
    if (wants("npc_suppress_persona")) registerOne(L, state, log, stateTag, "npc_suppress_persona", openGuard<stub_npc_suppress_persona>);
    if (wants("clear_animation_state")) registerOne(L, state, log, stateTag, "clear_animation_state", openGuard<stub_clear_animation_state>);
    if (wants("character_prevent_explosion_fling")) registerOne(L, state, log, stateTag, "character_prevent_explosion_fling", openGuard<stub_character_prevent_explosion_fling>);
    if (wants("character_prevent_bumping")) registerOne(L, state, log, stateTag, "character_prevent_bumping", openGuard<stub_character_prevent_bumping>);
    if (wants("character_set_never_catch_fire")) registerOne(L, state, log, stateTag, "character_set_never_catch_fire", openGuard<stub_character_set_never_catch_fire>);
    if (wants("ai_scripted_action_is_complete")) registerOne(L, state, log, stateTag, "ai_scripted_action_is_complete", openGuard<stub_ai_scripted_action_is_complete>);
    if (wants("player_movement_enable")) registerOne(L, state, log, stateTag, "player_movement_enable", openGuard<stub_player_movement_enable>);
}

} // namespace stub_sections
} // namespace sr3luahost
