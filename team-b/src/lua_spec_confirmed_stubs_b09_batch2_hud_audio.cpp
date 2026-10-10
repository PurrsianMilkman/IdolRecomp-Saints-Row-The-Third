// lua_spec_confirmed_stubs_b09_batch2_hud_audio.cpp
//
// Wave 1, Batch 2 of a 7-agent parallel fan-out (2026-10-08) implementing
// cleared-but-unregistered Lua natives in spec-lua-api-behaviour.md, scoped
// by results/lua_natives_sec10_28_fanout_scoping_20261008.md (see that
// report's own Sec0/Sec3/Sec4/Sec5 for the full fan-out context). This
// batch: "Character/vehicle/HUD/audio residue (Sec5-9, ranked 51-150)" -
// 25 natives, all from spec-lua-api-behaviour.md Sec7/Sec8/Sec9.
//
// Per the refactor's own reviewer guidance (see
// lua_spec_confirmed_stubs_registry.cpp's own top comment): this is a NEW
// file, not a retrofit into b01-b07 - it follows the exact same pattern as
// the existing 7 (own anonymous-namespace stub_* functions, own
// namesB09()/registerB09(wants) pair declared in
// lua_spec_confirmed_stubs_sections.h, called from
// lua_spec_confirmed_stubs_registry.cpp).
//
// The 25 natives: npc_leash_remove/npc_leash_to_nav (Sec7.1/Sec7.5),
// set_attack_player_flag (Sec7.10), set_animation_state (Sec7.11),
// set_script_animation_state (Sec7.17), ai_set_action_delay (Sec7.30),
// human_gravity_enable (Sec7.32), audio_persona_load_2d/audio_play_
// persona_line_2d/audio_ambient_emitter_start/audio_ambient_emitter_stop
// (Sec8.5/8.7/8.10/8.11), hud_bar_off/hud_bar_on/hud_bar_set_value/
// hud_prompt_clear/hud_set_fake_notoriety/hud_timer_set/hud_timer_stop
// (Sec8.14/8.15/8.16/8.17/8.18/8.19/8.20), vint_set_mouse_cursor/
// push_screen (Sec8.25/8.26), explosion_create/vehicle_max_speed/
// vehicle_detonate/helicopter_set_path_orientation/inv_item_equip
// (Sec9.3/9.6/9.7/9.18/9.19).
//
// CROSS-BATCH NOTE (helicopter family): helicopter_set_path_orientation
// below shares a dispatcher with Batch 4's helicopter_shoot_navpoint/
// helicopter_shoot_human (a sibling agent, different worktree, not
// implemented here) - this file's own doc comment on
// stub_helicopter_set_path_orientation documents this batch's own
// call-shape findings (the 0x008addb0 gate, the +0x158 field, the
// waypoint-association chain) for whoever reconciles both batches.
//
// HARDENING POLICY finding (new this batch, not on the orchestrator's own
// pre-flagged memory-safety list): hud_bar_off/hud_bar_on/hud_bar_set_value
// (Sec8.14/8.15/8.16) and hud_timer_set/hud_timer_stop (Sec8.19/8.20) each
// index a small FIXED-SIZE array (4 bar slots, 4 timer slots) by a
// Lua-script-supplied numeric id with NO documented bounds check at any of
// the 5 call sites. A script passing an out-of-range id would read/write
// past that fixed array on the real engine. HARDENED here per the
// orchestrator's standing directive: every in-range id reproduces the
// documented behaviour exactly; an out-of-range id is refused (no read/
// write at all) rather than replicated, and every refusal is counted by a
// named, per-site counter in EngineState::hudHardeningCounters() - see
// each of the 5 stub_* functions below for its own CHOSEN (hardened
// divergence) comment.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include <cmath>

namespace sr3luahost {
namespace {

// =======================================================================
// Sec7 batch: npc_leash_remove (Sec7.1), npc_leash_to_nav (Sec7.5),
// set_attack_player_flag (Sec7.10), set_animation_state (Sec7.11),
// set_script_animation_state (Sec7.17), ai_set_action_delay (Sec7.30),
// human_gravity_enable (Sec7.32). See each new/reused field's own doc
// comment (engine_state.h, CharacterState's "Batch 2026-10-08" block) for
// the full citation/reasoning this batch rests on.
// =======================================================================

// npc_leash_remove (Sec7.1, 0x00a55570): 1 mandatory string (NPC/
// character name), resolved via the generic/#PLAYER#-then-#FOLLOWER#
// chain (0x00a28150) - wired 2026-10-08 (follower-sentinel-resolver pass)
// via resolveFollowerAwareObject() in place of a plain objectResolves()
// stand-in, Sec29. No return. CONFIRMED gate: single-gate, 0x008ae480-only
// (variant 3, Sec7's own front matter) - the exact same first-stage
// predicate the double-gate family's gate 1 is (Sec7's front matter:
// "0x008ae480 itself is not an independent predicate - it does its own
// object-resolution/type-check first, then calls the SAME underlying
// function, 0x00884760"), already CONFIRMED true in single player
// (Sec3.7) - reused directly as CharacterState::forceFlagGatePasses
// rather than a near-duplicate always-true field. True -> directly
// resets the leash anchor: radius to the CONFIRMED -1.0 literal
// (0x012a2d54, independently byte-confirmed elsewhere in this document -
// see CharacterState::leashRadius's own doc comment), position reverted
// to OPEN (this project does not have the real 0x01115d60 payload - see
// same doc comment, not an invented zero vector). False -> opens a
// network record (opcode 0x46, tags "human_ai_data"+"clear_leash").
int stub_npc_leash_remove(lua_State* L) {
    logCall(L, upLog(L), "npc_leash_remove", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set - generic/#PLAYER#-then-#FOLLOWER# chain (0x00a28150); may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.leashRadius.set(-1.0);       // CONFIRMED literal, 0x012a2d54
        c.leashPosition.forget();      // real payload (0x01115d60) not given - honestly OPEN, not invented
    } else {
        EngineState::replicateStateChange("npc_leash_remove", resolvedName);
    }
    return 0;
}

// npc_leash_to_nav (Sec7.5, 0x00a56330): 1 mandatory string (NPC name,
// resolved via 0x00a28150). 1 mandatory string (destination name,
// resolved via the 5th per-kind resolver 0x005982e0 - this project's
// flat objectResolves() stand-in, same collapsing convention every other
// per-kind resolver in this file already gets). 1 mandatory number
// (leash radius). 1 optional boolean (nav mode), standard nil-gated
// idiom, default false. No return. CONFIRMED: a negative radius removes
// the leash entirely via the SAME clear setter npc_leash_remove uses
// (0x004e3bf0) - not an error. Otherwise, on a live destination resolve,
// the destination's own position (4 floats, NaN-validated across x/y/z
// before use - a real, confirmed guard) feeds the leash-SET counterpart
// (0x004e39b0), sharing the SAME single-gate (0x008ae480-only, variant 3)
// idiom as npc_leash_remove: true -> writes the destination position,
// the radius, and the nav-mode byte directly; false -> opens a network
// record (opcode 0x46, tags "human_ai_data"+"leash_params"). The two
// fixed "+0x28/+0x2c" defaults (0x01115d50/0x01115d54) have no in-scope
// reader and are not modeled (see CharacterState::leashRadius's own doc
// comment).
int stub_npc_leash_to_nav(lua_State* L) {
    logCall(L, upLog(L), "npc_leash_to_nav", upStateTag(L));
    std::string name = argString(L, 1);
    std::string destName = argString(L, 2);
    double radius = lua_tonumber(L, 3); // 1 mandatory number, no nil-gate (CONFIRMED)
    bool navMode = optionalBoolDefault(L, 4, false);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set - arg1 resolved via 0x00a28150; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (radius < 0.0) {
        // CONFIRMED: negative radius removes the leash entirely, via the
        // SAME clear setter npc_leash_remove (Sec7.1) uses.
        if (c.forceFlagGatePasses.get()) {
            c.leashRadius.set(-1.0);
            c.leashPosition.forget();
        } else {
            EngineState::replicateStateChange("npc_leash_to_nav", resolvedName);
        }
        return 0;
    }
    // arg2 (destination) resolves via a DIFFERENT per-kind resolver
    // (0x005982e0, Sec7.5's own text) that does not accept the #FOLLOWER#
    // family - unchanged, plain objectResolves().
    if (!st->objectResolves().get(destName)) return 0; // OPEN until set - destination resolve
    WorldPosition destPos = st->getOrCreateCharacter(destName).worldPosition.get(); // OPEN until set
    if (std::isnan(destPos.x) || std::isnan(destPos.y) || std::isnan(destPos.z)) return 0; // CONFIRMED NaN guard
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.leashPosition.set(destPos);
        c.leashRadius.set(radius);
        c.leashNavMode = navMode;
    } else {
        EngineState::replicateStateChange("npc_leash_to_nav", resolvedName);
    }
    return 0;
}

// set_attack_player_flag (Sec7.10, 0x00a5f0b0): 1 mandatory string
// (resolved via the plain character resolver 0x005e4dd0 - this project's
// flat objectResolves() stand-in). 1 optional boolean, standard nil-gated
// idiom, default true. No return. CONFIRMED: genuinely splits across two
// flag locations. Bit 0x04 of +0xac is written UNCONDITIONALLY, with no
// gate and no replication, BEFORE a vtable +0x68 predicate (unidentified,
// OPEN) is even evaluated; only if that predicate is truthy does the
// SEPARATE double-gate (variant 1) setter 0x004e0100 run on bit 0x01 of
// +9 (tags "human_ai_data"+"ai_force_flagsattack_player_on_sight").
int stub_set_attack_player_flag(lua_State* L) {
    logCall(L, upLog(L), "set_attack_player_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    c.attackPlayerBitAc.set(flag); // CONFIRMED: unconditional, no gate, no replication, applies BEFORE the OPEN read below
    if (c.attackPlayerSecondaryGatePredicate.get()) { // OPEN until set - unidentified vtable +0x68 predicate
        if (c.forceFlagGatePasses.get()) {            // CONFIRMED true in single player, Sec3.7
            c.attackPlayerOnSightBit9.set(flag);
        } else {
            EngineState::replicateStateChange("set_attack_player_flag", name);
        }
    }
    return 0;
}

// set_animation_state (Sec7.11, 0x00a5edf0): 2 mandatory strings
// (character name, resolved via 0x00a281a0; a state name, looked up via
// the shared table 0x004bf810). 1 optional string (a third, secondary
// object name, resolved via the 5th per-kind resolver 0x005982e0),
// standard nil-gated idiom, default absent - consumed only, no in-scope
// observable effect (the arg-3-gated 0x004aac40/0x004b7d50 pair are OPEN,
// Sec7.11). No return. CHOSEN simplification (see
// EngineState::lookupAnimationStateId()'s own doc comment):
// lookupAnimationStateId() collapses the spec's own two real lookups
// (name->id, id->per-object-entry) into one test-registerable map; -1
// means "not found" either way. Not-found -> clears the override via the
// SAME shared setter clear_animation_state (Sec7.3) uses, through
// forceFlagGatePasses (CONFIRMED true in single player). Found -> checks
// the single gate controlLockGatePasses (0x008addb0, variant 2 -
// CONFIRMED by Sec7.11's own text to be "the SAME gate family as the
// player-controls/movement pair", Sec7.7/Sec7.22/Sec7.25): true ->
// unconditionally calls the shared setter, ITSELF gated by
// forceFlagGatePasses independently (Sec7.11's own "subject to ITS OWN
// independent double-gate" text); false -> opens a network record
// (opcode 0x43, numeric subtype 0x16) with no inner setter call at all.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_set_animation_state(lua_State* L) {
    logCall(L, upLog(L), "set_animation_state", upStateTag(L));
    std::string name = argString(L, 1);
    std::string stateName = argString(L, 2);
    if (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) {
        (void)argString(L, 3); // consumed only - no in-scope observable effect (0x004aac40/0x004b7d50 OPEN, Sec7.11)
    }
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    int32_t stateId = st->lookupAnimationStateId(stateName); // -1 = not found (CHOSEN collapse, see doc comment)
    if (stateId == -1) {
        if (c.forceFlagGatePasses.get()) c.animationStateOverride.set(-1);
        else EngineState::replicateStateChange("set_animation_state", resolvedName);
        return 0;
    }
    if (c.controlLockGatePasses.get()) { // SAME gate family as player-controls, Sec7.11's own cross-reference
        if (c.forceFlagGatePasses.get()) c.animationStateOverride.set(stateId);
        else EngineState::replicateStateChange("set_animation_state", resolvedName);
    } else {
        EngineState::replicateStateChange("set_animation_state", resolvedName); // opcode 0x43 outer record only - no inner setter call
    }
    return 0;
}

// set_script_animation_state (Sec7.17, 0x00a5e7f0): 1 mandatory string
// (resolved via 0x00a28150, resolveFollowerAwareObject() - wired
// 2026-10-08, follower-sentinel-resolver pass). 1 optional string
// (a second state name,
// looked up via the SAME shared table 0x004bf810 set_animation_state/
// character_kill use), standard nil-gated idiom, default absent
// (resolves to state id -1, CONFIRMED). No return. CONFIRMED: no gate or
// bypass branch at all - gated only on whether the character's own
// +0x1da8 sub-object pointer is non-null. This project already models
// that exact distinction as CharacterState::scriptNpcBound (see that
// field's own doc comment above CharacterState::attackPlayerBitAc for the
// Sec34.3 cross-reference this batch newly drew) - an ordinary
// (non-test-marked) character's +0x1da8 is null (CONFIRMED zeroed by the
// ordinary human constructor, Sec34.1), so this native is a no-op for it;
// a test can call markScriptNpcBoundForTesting() to exercise the
// write+record path.
int stub_set_script_animation_state(lua_State* L) {
    logCall(L, upLog(L), "set_script_animation_state", upStateTag(L));
    std::string name = argString(L, 1);
    std::string stateName = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set - resolved via 0x00a28150; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (!c.scriptNpcBound) return 0; // +0x1da8 null for an ordinary character (CONFIRMED, Sec34.1/Sec34.3)
    int32_t stateId = st->lookupAnimationStateId(stateName); // -1 for absent/unregistered (CONFIRMED default-absent shape)
    c.scriptAnimationStateId = stateId;                      // CONFIRMED: always written, unconditionally
    EngineState::replicateStateChange("set_script_animation_state", resolvedName); // CONFIRMED: always opens, no gate/bypass
    return 0;
}

// ai_set_action_delay (Sec7.30, 0x00a3c4e0): 1 mandatory string
// (character, resolved via the #PLAYER#-then-#FOLLOWER# chain 0x00a28150,
// resolveFollowerAwareObject() - wired 2026-10-08, follower-sentinel-
// resolver pass). 1 mandatory string (an action name, or the literal "ALL",
// case-insensitive). 1 optional number (delay) - standard nil-gated
// idiom but a distinct, NOT independently decompiled default-producer
// (0x00ea2560) - refused as OPEN when omitted, per this project's own
// discipline against inventing an unread constant. No return. "ALL"
// (case-insensitive __stricmp) applies the delay to every action at once
// (0x004f4b30); otherwise classifies the name through the CONFIRMED
// 70-entry table (0x012e1c60, EngineState::lookupActionNameIndex()) - a
// miss triggers the CONFIRMED diagnostic console-print path (tracked
// here only as a count, this project does not reproduce real console
// output - "a genuine, confirmed diagnostic path, not a silent no-op"), a
// hit applies the delay to that one action (0x004f4ae0).
int stub_ai_set_action_delay(lua_State* L) {
    logCall(L, upLog(L), "ai_set_action_delay", upStateTag(L));
    std::string name = argString(L, 1);
    std::string actionName = argString(L, 2);
    if (lua_gettop(L) < 3 || lua_type(L, 3) == LUA_TNIL) {
        throw OpenStateError("ai_set_action_delay's own default-delay producer 0x00ea2560 (not independently decompiled)",
                              "spec-lua-api-behaviour.md Sec7.30");
    }
    double delay = lua_tonumber(L, 3);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set - #PLAYER#-then-#FOLLOWER# chain (0x00a28150); may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (caseInsensitiveEquals(actionName, "ALL")) { // CONFIRMED real public __stricmp
        c.allActionDelay = delay;
        ++c.allActionDelayApplyCount;
        return 0;
    }
    int32_t idx = st->lookupActionNameIndex(actionName);
    if (idx == EngineState::kActionNameNotFound) {
        ++c.actionDelayUnrecognizedNameCount; // CONFIRMED diagnostic path, not a silent no-op - console print not reproduced
        return 0;
    }
    c.actionDelayByIndex[idx] = delay;
    return 0;
}

// human_gravity_enable (Sec7.32, 0x00a4d210): 1 mandatory string
// (resolved via 0x00a281a0). 1 boolean, read with NO nil/absence gate
// (default false via Lua's own nil-handling). No return. CONFIRMED: the
// one flagged name that skips the either/or gate idiom entirely - always
// writes bit 0x02 of the physics/ragdoll sub-object's own +0x150 field
// (gravityEnabled - see that field's own doc comment for the "+0xcc0
// pointer always treated as non-null" judgment call) AND always opens a
// record (the Lua wrapper hardcodes its own 3rd parameter to 1) - the two
// are never alternatives here, unlike every other setter in this batch.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_human_gravity_enable(lua_State* L) {
    logCall(L, upLog(L), "human_gravity_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool enable = lua_toboolean(L, 2) != 0; // CONFIRMED: no nil/absence gate at all
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    c.gravityEnabled.set(enable);
    EngineState::replicateStateChange("human_gravity_enable", resolvedName); // CONFIRMED: always opens, no gate ("always both")
    return 0;
}

// =======================================================================
// Sec8 batch: audio_persona_load_2d (Sec8.5), audio_play_persona_line_2d
// (Sec8.7), audio_ambient_emitter_start (Sec8.10), audio_ambient_emitter_
// stop (Sec8.11), hud_bar_off/hud_bar_on/hud_bar_set_value (Sec8.14/8.15/
// 8.16), hud_prompt_clear (Sec8.17), hud_set_fake_notoriety (Sec8.18),
// hud_timer_set/hud_timer_stop (Sec8.19/8.20), vint_set_mouse_cursor
// (Sec8.25), push_screen (Sec8.26).
// =======================================================================

// audio_persona_load_2d (Sec8.5, 0x00a3cea0): 1 mandatory string (an
// event/sound name), read unconditionally. Return: 1 number. CONFIRMED
// host-gated queue (opcode 0x51, subtype 0) - this project's game_get_is_
// host is CONFIRMED false in single player (Sec8.27), so the queue branch
// is dead in this project's scope (same reasoning class as audio_
// conversation_end, Sec8.2). JUDGMENT CALL (narrowing the prior pass's
// HARD classification - b07's own Section 8 batch header - flagged for
// reviewer attention): the pushed number is read here as the resolved
// Wwise-id value itself, independent of the host gate (only the
// ADDITIONAL queued record is host-gated, per Sec8.5's own separately-
// stated "Body"/"Return" split) - this project's own flat
// objectResolves() stands in for the always-succeeds Wwise-ID resolver
// (same convention audio_play_persona_line/Sec2.5 already establishes),
// and nextAudioVoiceHandle() stands in for the real id (no real Wwise
// voice table, same project-wide convention). Resolve failure has no
// CONFIRMED sentinel for this specific name, so this project reads it as
// the natural "resolver returned id 0" case.
int stub_audio_persona_load_2d(lua_State* L) {
    logCall(L, upLog(L), "audio_persona_load_2d", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushnumber(L, 0.0);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(st->nextAudioVoiceHandle()));
    return 1;
}

// audio_play_persona_line_2d (Sec8.7, 0x00a3cf20): 2 arguments, both read
// unconditionally - arg 1 (number, a persona/emitter handle, bounds-
// checked <0x6e into a fixed 0x28-byte-stride table at 0x01504088) and
// arg 2 (string, a line/event name). Return: 1 number. CHOSEN (this
// project's own scoping, not a confirmed real-engine fact, flagged for
// reviewer attention): nothing in this project's scope ever populates
// that fixed handle table (no in-scope producer ever creates a live "2D
// persona emitter" slot entry), so the live-entry lookup this native's
// own dispatch depends on always misses in this project's model - always
// returns 0, matching Sec8.7's own flagged-OPEN "whether this call's
// output is ever genuinely non-zero in practice" on its conservative
// side. Both arguments are still consumed for shape fidelity.
int stub_audio_play_persona_line_2d(lua_State* L) {
    logCall(L, upLog(L), "audio_play_persona_line_2d", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1)); // consumed, bounds-checked on the real engine (<0x6e) - no table ever populated in this project's scope
    (void)argString(L, 2);
    lua_pushnumber(L, 0.0);
    return 1;
}

// audio_ambient_emitter_start (Sec8.10, 0x00a3d130): 1 mandatory string
// (a sound/event name), read unconditionally. Return: 1 number. CHOSEN
// (same "no in-scope producer" reasoning as audio_play_persona_line_2d
// above, flagged for reviewer attention): the per-frame ambient-slot list
// this native scans (0x03171a64) is never populated by anything in this
// project's scope (no level/zone-data loader is modeled), so the match
// always misses and the function has no observable effect beyond
// consuming its argument - the spec's own text only ever states a return
// value ON a match, so 0 (no handle produced) is read as the natural
// "never matched" result, not an invented sentinel.
int stub_audio_ambient_emitter_start(lua_State* L) {
    logCall(L, upLog(L), "audio_ambient_emitter_start", upStateTag(L));
    (void)argString(L, 1);
    lua_pushnumber(L, 0.0);
    return 1;
}

// audio_ambient_emitter_stop (Sec8.11, 0x00a3c7d0): 1 mandatory string,
// read unconditionally. No return. Same "never-populated slot list"
// reasoning as audio_ambient_emitter_start above - the match always
// misses, so this reduces to argument parsing only.
int stub_audio_ambient_emitter_stop(lua_State* L) {
    logCall(L, upLog(L), "audio_ambient_emitter_stop", upStateTag(L));
    (void)argString(L, 1);
    return 0;
}

// hud_bar_off (Sec8.14, 0x00a47010): 1 mandatory numeric value (a
// bar-slot id, 0-3), read unconditionally via the round-to-int pair. No
// return. HARDENED (HARDENING POLICY, CHOSEN divergence - discovered this
// batch, not on the orchestrator's own pre-flagged list): the real engine
// indexes a fixed 4-slot array with no documented bounds check - a
// script-controlled out-of-range id would read/write past it on the real
// binary. This project refuses an out-of-range id instead, counted by
// hudHardeningCounters().barOffOobCount.
int stub_hud_bar_off(lua_State* L) {
    logCall(L, upLog(L), "hud_bar_off", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    HudBarSlot* slot = st->hudBarSlot(id);
    if (!slot) {
        ++st->hudHardeningCounters().barOffOobCount; // CHOSEN (hardened divergence), Sec8.14 - refused, never replicated
        return 0;
    }
    slot->on = false;
    return 0;
}

// hud_bar_on (Sec8.15, 0x00a4d9e0): up to 6 arguments - arg 1 (numeric,
// mandatory) bar-slot id; arg 2 (string, mandatory, read with NO presence
// check) a secondary id passed through unresolved; arg 3 (string,
// optional, default "") a raw text-key (the 3-stage localization chain it
// shares with objective_text, Sec1.3, is OUT of this batch's scope - kept
// raw, never localized); arg 4 (number, optional, default 0.0) a value;
// arg 5 (numeric/flag, optional, default 3); arg 6 (numeric/flag,
// optional, default -1, consumed only - no in-scope reader). No return
// (implicit void - 0 Lua values). Writes the resolved fields into the
// per-slot record (immediate AND animated-target fields both take arg 4's
// value, CONFIRMED by Sec8.15's own "+0x4c/+0x50" pair text); flag-bit
// 0x2 (no host gate, purely flag-gated, CONFIRMED) queues an opcode-0x3e
// command; flag-bit 0x1 marks dirty and calls a local refresh. HARDENED
// the same way as hud_bar_off (hudHardeningCounters().barOnOobCount).
int stub_hud_bar_on(lua_State* L) {
    logCall(L, upLog(L), "hud_bar_on", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    std::string secondaryId = argString(L, 2); // mandatory, no presence check (CONFIRMED)
    std::string label = optionalStringDefault(L, 3, "");
    double value = optionalNumberDefault(L, 4, 0.0);
    int64_t flags5 = truncateEa2596(optionalNumberDefault(L, 5, 3.0));
    (void)truncateEa2596(optionalNumberDefault(L, 6, -1.0)); // consumed, no in-scope reader beyond the flags already modelled
    EngineState* st = upState(L);
    HudBarSlot* slot = st->hudBarSlot(id);
    if (!slot) {
        ++st->hudHardeningCounters().barOnOobCount; // CHOSEN (hardened divergence), Sec8.15
        return 0;
    }
    slot->on = true;
    slot->secondaryId = secondaryId;
    slot->label = label;
    if (slot->immediateValue != value) ++slot->dirtyCount; // change-gated dirty (same shape Sec8.16 documents)
    slot->immediateValue = value;
    slot->animatedTargetValue = value; // CONFIRMED: Sec8.15's own +0x4c/+0x50 pair both take this call's value
    if (flags5 & 0x2) ++slot->queuedCommandCount;          // CONFIRMED: no host gate, purely flag-gated
    if (flags5 & 0x1) ++slot->localRefreshCount;
    return 0;
}

// hud_bar_set_value (Sec8.16, 0x00a4dc20): 3 arguments - arg 1 (numeric,
// mandatory) bar-slot id; arg 2 (number, mandatory) a value; arg 3
// (numeric/flag, optional, default 3). No return. Bit 0x1 writes the
// IMMEDIATE field unconditionally (no dirty-tracking, CONFIRMED); bit 0x2
// writes the ANIMATED-TARGET field, but only (and dirty-flags) when the
// value actually changed (CONFIRMED). HARDENED the same way
// (hudHardeningCounters().barSetValueOobCount).
int stub_hud_bar_set_value(lua_State* L) {
    logCall(L, upLog(L), "hud_bar_set_value", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    double value = lua_tonumber(L, 2); // 1 mandatory number, no nil-gate (CONFIRMED)
    int64_t flags = truncateEa2596(optionalNumberDefault(L, 3, 3.0));
    EngineState* st = upState(L);
    HudBarSlot* slot = st->hudBarSlot(id);
    if (!slot) {
        ++st->hudHardeningCounters().barSetValueOobCount; // CHOSEN (hardened divergence), Sec8.16
        return 0;
    }
    if (flags & 0x1) slot->immediateValue = value; // CONFIRMED: unconditional, no dirty-tracking
    if (flags & 0x2) {
        if (slot->animatedTargetValue != value) ++slot->dirtyCount; // CONFIRMED: change-gated, dirty-flagged
        slot->animatedTargetValue = value;
    }
    return 0;
}

// hud_prompt_clear (Sec8.17, 0x00a4d080): 1 optional string (standard
// nil-gated idiom) - a target name; absent/nil/empty resolves to a null
// target. No return. CONFIRMED: always runs the local clear path (whether
// or not a target name is given, and regardless of resolution -
// Sec8.17's own text does not gate the LOCAL clear on resolution success,
// only the SEPARATE network-replicated path does, and that path only
// fires when the target specifically matches the remote co-op player
// getter (0x009df3d0) - a case this project's single-player-only scope
// never reaches (no remote co-op player entry is ever modeled, same
// reasoning class as player_names_get_all, Sec51.4). The target string
// itself has no further in-scope observable effect, so it is consumed
// without a resolve attempt (avoiding an unwarranted refusal for a value
// nothing downstream depends on).
int stub_hud_prompt_clear(lua_State* L) {
    logCall(L, upLog(L), "hud_prompt_clear", upStateTag(L));
    (void)optionalStringDefault(L, 1, ""); // target name - consumed; the local clear below is CONFIRMED unconditional regardless (see doc comment)
    EngineState* st = upState(L);
    ++st->hudPromptClearCount();
    return 0;
}

// Resolves a gang/track name against hud_set_fake_notoriety's own
// CONFIRMED three parallel 4-entry cross-title alias tables (Sec8.18 -
// read directly from .rdata and empirically verified byte-for-byte,
// reproduced here verbatim): SR3 {"luchadores","deckers","morningstar",
// "police"}, SR2 {"brotherhood","ronin","samedi","police"}, SR1
// {"los_carnales","vice_kings","rollers","police"} - indexed by the SAME
// running counter 0-3; unmatched names fall back to the CONFIRMED
// sentinel slot value 5.
int resolveFakeNotorietySlot(const std::string& name) {
    static const char* kSr3[4] = {"luchadores", "deckers", "morningstar", "police"};
    static const char* kSr2[4] = {"brotherhood", "ronin", "samedi", "police"};
    static const char* kSr1[4] = {"los_carnales", "vice_kings", "rollers", "police"};
    for (int i = 0; i < 4; ++i) {
        if (caseInsensitiveEquals(name, kSr3[i]) || caseInsensitiveEquals(name, kSr2[i]) ||
            caseInsensitiveEquals(name, kSr1[i]))
            return i;
    }
    return 5; // CONFIRMED sentinel for an unmatched name
}

// hud_set_fake_notoriety (Sec8.18, 0x00a4d0e0): 3 arguments - arg 1
// (string, mandatory, unconditional) a gang/track name, resolved through
// the 3-table alias above; arg 2 (boolean, mandatory, unconditional); arg
// 3 (number, optional, default 0.0, clamped to [0,inf) per Sec8.18's own
// text). No return. Writes the boolean+clamped value into the resolved
// slot (0-3, or the CONFIRMED sentinel 5) unconditionally. The host-gated
// opcode-0x40 queue (CONFIRMED false in single player, Sec8.27) never
// fires in this project's single-player-only scope, so only the two array
// writes are test-observable.
int stub_hud_set_fake_notoriety(lua_State* L) {
    logCall(L, upLog(L), "hud_set_fake_notoriety", upStateTag(L));
    std::string name = argString(L, 1);
    bool active = lua_toboolean(L, 2) != 0; // 1 mandatory bool, no nil-gate (CONFIRMED)
    double level = optionalNumberDefault(L, 3, 0.0);
    if (level < 0.0) level = 0.0; // CONFIRMED clamp to [0, inf)
    int slot = resolveFakeNotorietySlot(name);
    EngineState* st = upState(L);
    auto& slots = st->fakeNotorietySlots();
    slots[static_cast<size_t>(slot)].active = active;
    slots[static_cast<size_t>(slot)].level = level;
    return 0;
}

// hud_timer_set (Sec8.19, 0x00a4f700): up to 4 arguments - arg 1
// (numeric, mandatory) a timer-slot id; arg 2 (numeric, mandatory) a
// duration/count value; arg 3 (string, optional, default "") a text key;
// arg 4 (numeric/flag, optional, default 3). No return. Writes the value
// (change-gated/dirty-flagged, same shape as hud_bar_set_value); bit 0x2
// (no host gate) queues an opcode-0x3e command (subtype 1, vs hud_bar_
// on's subtype 2); bit 0x1, when the slot table isn't already full (this
// project's own fixed 4-slot array already enforces that bound
// structurally - every address this project can even reach is therefore
// "not full"), marks dirty and calls the shared local-refresh. arg 3, if
// given, is stored as the slot's own text key (CHOSEN: no real fixed-
// capacity name<->id table is modeled, Sec8.19's own 0x00a1fcc0). After
// the setter: clears any stale prior handle (0x005e4520, shared with
// hud_timer_stop, CONFIRMED unconditional). HARDENED the same way as the
// bar family (hudHardeningCounters().timerSetOobCount).
int stub_hud_timer_set(lua_State* L) {
    logCall(L, upLog(L), "hud_timer_set", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    double value = lua_tonumber(L, 2); // 1 mandatory number, no nil-gate (CONFIRMED)
    std::string textKey = optionalStringDefault(L, 3, "");
    int64_t flags = truncateEa2596(optionalNumberDefault(L, 4, 3.0));
    EngineState* st = upState(L);
    HudTimerSlot* slot = st->hudTimerSlot(id);
    if (!slot) {
        ++st->hudHardeningCounters().timerSetOobCount; // CHOSEN (hardened divergence), Sec8.19
        return 0;
    }
    if (slot->value != value) ++slot->dirtyCount;
    slot->value = value;
    slot->on = true;
    if (flags & 0x2) ++slot->queuedCommandCount; // CONFIRMED: no host gate, purely flag-gated
    if (flags & 0x1) ++slot->localRefreshCount;  // this project's 4-slot array already bounds "isn't already full"
    if (!textKey.empty()) {
        slot->textKey = textKey;
        slot->textKeyResolved = true; // CHOSEN: no real fixed-capacity name<->id table modelled
    }
    ++slot->staleHandleClearCount; // 0x005e4520, unconditional per Sec8.19's own text
    return 0;
}

// hud_timer_stop (Sec8.20, 0x00a4f510): 1 mandatory numeric value (a
// timer-slot id), read unconditionally. No return. CONFIRMED: clears the
// GLOBAL "timer active" byte (0x022b65f5) only when the slot's own
// liveness byte is ALREADY clear; then unconditionally calls the SAME
// shared per-slot state setter hud_bar_off uses, with state code 6 (vs
// hud_bar_off's 7) - turning the slot off. The top-level function also
// clears any stale queue-context handle (0x005e4520, shared with
// hud_timer_set). HARDENED the same way (hudHardeningCounters().
// timerStopOobCount).
int stub_hud_timer_stop(lua_State* L) {
    logCall(L, upLog(L), "hud_timer_stop", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    HudTimerSlot* slot = st->hudTimerSlot(id);
    if (!slot) {
        ++st->hudHardeningCounters().timerStopOobCount; // CHOSEN (hardened divergence), Sec8.20
        return 0;
    }
    if (!slot->on) st->hudTimerGlobalActive() = false; // CONFIRMED: clears the GLOBAL flag only when the slot's own liveness was already clear
    slot->on = false;                                  // CONFIRMED: unconditional state-code-6 ("off") write regardless of the branch above
    ++slot->staleHandleClearCount;
    return 0;
}

// vint_set_mouse_cursor (Sec8.25, 0x008456c0): 3 optional arguments (all
// standard nil-gated) - arg 1 (string, default "") a cursor name; arg 2
// (number, default -1); arg 3 (number, default -1). No return. Real and
// trivially easy, but genuinely zero observable effect in this project's
// scope - this host renders no cursor (per the orchestrator's own prior
// deferral of this exact name, b07's own Section 8 batch header). Kept as
// pure argument-consuming plumbing, for the forward-compatible call shape.
int stub_vint_set_mouse_cursor(lua_State* L) {
    logCall(L, upLog(L), "vint_set_mouse_cursor", upStateTag(L));
    (void)optionalStringDefault(L, 1, "");
    (void)optionalNumberDefault(L, 2, -1.0);
    (void)optionalNumberDefault(L, 3, -1.0);
    return 0;
}

// push_screen (Sec8.26, 0x00842a50): 1 mandatory string (a screen name),
// read unconditionally. Return: 1 boolean - whether the push succeeded.
// CHOSEN (no real registered-screen-type roster is modeled anywhere in
// this project's scope - the same "never-populated producer table" class
// as audio_ambient_emitter_start/stop above): the name->definition lookup
// always misses, so this native always returns false, honestly, rather
// than inventing a populated roster.
int stub_push_screen(lua_State* L) {
    logCall(L, upLog(L), "push_screen", upStateTag(L));
    (void)argString(L, 1);
    lua_pushboolean(L, 0);
    return 1;
}

// =======================================================================
// Sec9 batch: explosion_create (Sec9.3), vehicle_max_speed (Sec9.6),
// vehicle_detonate (Sec9.7), helicopter_set_path_orientation (Sec9.18),
// inv_item_equip (Sec9.19).
// =======================================================================

// explosion_create (Sec9.3, 0x00a49450): 2 mandatory + 4 further optional
// arguments. Arg 1 (string, mandatory) - read but never observably
// consumed again (same "read but functionally inert" pattern as audio_
// play_persona_line's own arg 2, Sec2.5 - OPEN, same caveat). Arg 2
// (string, mandatory) - a location/object-name reference, resolved
// through the SAME shared position-lookup get_dist uses (0x00a455a0) -
// this project's flat objectResolves() stand-in, gating success/failure.
// Arg 3 (string, optional, default absent) - an instigator/owner name;
// consumed, no in-scope observable effect of its own. Arg 4 (boolean,
// optional, default true) - consumed, no in-scope observable effect (the
// real explosion-apply routine's own deep internals, ~1,900 instructions,
// are explicitly OPEN past the entry checks already cited). Arg 5
// (string, optional, default absent) - an explosion "type" reference;
// consumed, not modelled (same OPEN-internals reasoning). Arg 6 (number,
// optional, default 0.0) - a position-offset magnitude; consumed, not
// modelled (the offset vector's own source is OPEN). Return: 1 number -
// the SAME -1.0 failure sentinel (0x012a3038) effect_play uses on arg-2
// resolve failure, else a fresh handle via the SAME shared finalizer
// identity effect_play uses (EngineState::nextVisualEffectHandle()) -
// CONFIRMED the two Lua entry points share one packed-instance-identifier
// convention even though they dispatch to different top-level routines. A
// deliberately thin "resolve + sentinel-or-handle" shell, per this
// batch's own standing instruction NOT to model or even imply the real
// damage/explosion application (0x00594460, ~1,900 untraced
// instructions).
int stub_explosion_create(lua_State* L) {
    logCall(L, upLog(L), "explosion_create", upStateTag(L));
    (void)argString(L, 1); // read but functionally inert (OPEN, same caveat as Sec2.5)
    std::string posName = argString(L, 2);
    (void)optionalStringDefault(L, 3, ""); // instigator/owner name - consumed, no in-scope observable effect
    (void)optionalBoolDefault(L, 4, true);
    (void)optionalStringDefault(L, 5, "");
    (void)optionalNumberDefault(L, 6, 0.0);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(posName)) { // OPEN until set
        lua_pushnumber(L, EngineState::kActionPlaySentinelResolveFail);
        return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(st->nextVisualEffectHandle()));
    return 1;
}

// vehicle_max_speed (Sec9.6, 0x00a65cd0): 2 mandatory arguments - arg 1
// (string) a vehicle reference (two-path resolver chain: the dedicated
// vehicle-instance resolver 0x00a281e0 first, falling back to the
// generic/coop-cache chain on failure - this project's flat
// objectResolves() stand-in collapses both paths, same convention every
// vehicle function in this file already applies); arg 2 (number) a speed
// value, read unconditionally, no presence check. No return. Converts
// mph->m/s via the SAME literal constant vehicle_speed_override (Sec9.5)
// uses and writes it directly (see VehicleState::maxSpeedMps's own doc
// comment for why no clamp/gate/record is modelled here - OPEN, not a
// confirmed absence).
int stub_vehicle_max_speed(lua_State* L) {
    logCall(L, upLog(L), "vehicle_max_speed", upStateTag(L));
    std::string name = argString(L, 1);
    double mph = lua_tonumber(L, 2); // 1 mandatory number, no nil-gate (CONFIRMED)
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    v.maxSpeedMps = mph * 0.44703999161720276; // CONFIRMED literal mph->m/s constant, 0x01119df8 (SAME as vehicle_speed_override, Sec9.5)
    return 0;
}

// vehicle_detonate (Sec9.7, 0x00a64af0): 1 mandatory string - a vehicle
// reference, resolved via the dedicated vehicle-instance resolver
// (0x00a281e0) - this project's flat objectResolves() stand-in, no
// optional arguments, no fallback. No return. CONFIRMED: bails out
// immediately (no effect at all) if the vehicle's own "destroyed" flag
// bit (+0x16c8 bit 0x1, the SAME field vehicle_is_destroyed, Sec4.9,
// reads) is already set, or if either of two further liveness/pause
// gates (not modelled - no in-scope producer) trips. Past that, applies
// position-relative force/damage and effects - its own deep internals
// (~1,150 instructions) are OPEN beyond the entry checks already cited,
// so only "the gate passed" is counted (detonateAppliedCount), never the
// real damage/force effect itself. Per the HARDENING POLICY: this is a
// genuine behavioural gap, not a memory-safety one - nothing to harden.
int stub_vehicle_detonate(lua_State* L) {
    logCall(L, upLog(L), "vehicle_detonate", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.destroyedFlag16c8.get()) return 0; // OPEN until set - CONFIRMED early bail-out when already destroyed
    ++v.detonateAppliedCount;                // gate passed - the real force/damage dispatch's own deep internals are OPEN, not modelled
    return 0;
}

// helicopter_set_path_orientation (Sec9.18, 0x00a4f550): 1 mandatory
// string (helicopter/vehicle reference, resolved via 0x00a281e0) + 1
// mandatory number (orientation/heading, no presence check) + 1 further
// optional string (a waypoint/path-node reference, default absent). No
// return. CROSS-BATCH NOTE (for whoever reconciles this against Batch
// 4's helicopter_shoot_navpoint/helicopter_shoot_human, which share a
// dispatcher per the scoping report): the dedicated setter (0x00b374b0)
// gates on 0x008addb0 (variant 2) - CONFIRMED by Sec9.18's own text to be
// "the SAME gate ... mesh_mover_hide/mesh_mover_show ... consult", and by
// Sec7's own front-matter cross-reference to bottom out on the identical
// real predicate (0x00884760) Sec3.7 already confirmed true in single
// player - so this project always takes the direct-write branch (writing
// VehicleState::pathOrientation directly), never the opcode-0x42 record;
// no new per-vehicle gate field was added since the real predicate's
// single-player answer is already settled project-wide. The spec's own
// "+0x158 field off a further resolved handle" (not confirmed to be the
// vehicle itself) is modelled directly on this VehicleState - this
// project's own flat, single-object simplification, same one vehicle_
// chase's own +0xc68 sub-record already applies. If arg 3 is given and
// resolves (this project's flat objectResolves() stand-in for the real
// type-gated resolver + bit test, Sec9.18/Sec9.21), associates the
// helicopter with the resolved waypoint, recorded honestly as the raw
// name (no real link).
int stub_helicopter_set_path_orientation(lua_State* L) {
    logCall(L, upLog(L), "helicopter_set_path_orientation", upStateTag(L));
    std::string name = argString(L, 1);
    double orientation = lua_tonumber(L, 2); // 1 mandatory number, no presence check (CONFIRMED)
    bool hasWaypoint = lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL;
    std::string waypointName = hasWaypoint ? argString(L, 3) : std::string();
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    // CONFIRMED true in single player (Sec3.7's cross-reference - this is
    // the SAME 0x008addb0-bottomed gate the player-controls family
    // bottoms out on, see doc comment above) - always the direct-write
    // branch, never the opcode-0x42 record.
    v.pathOrientation = orientation;
    if (hasWaypoint && st->objectResolves().get(waypointName)) { // OPEN until set
        v.helicopterWaypointAssociation = waypointName;
    }
    return 0;
}

// inv_item_equip (Sec9.19, 0x00a50700): 2 optional arguments, both
// individually nil-gated - arg 1 (string, optional, default absent) an
// item/weapon name; arg 2 (string, optional, default the literal
// "#PLAYER1#") a target-character reference, resolved through the same
// object-by-name lookup audio_play_persona_line/explosion_create use
// (0x00a281a0) - this project's flat objectResolves() stand-in. No
// return. If the target resolves: records the raw request honestly (no
// real fixed weapon/item-definition catalog exists in this project's
// scope - inventing one would be inventing data, per the HARD
// classification this exact name was given, b07's own Section 9 batch
// header) - "" means the literal-selector-0 ("no item given") path, per
// Sec9.19's own text.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_equip(lua_State* L) {
    logCall(L, upLog(L), "inv_item_equip", upStateTag(L));
    std::string itemName = optionalStringDefault(L, 1, "");
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    std::string resolvedTarget;
    if (!resolveFollowerAwareObject(st, target, resolvedTarget)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedTarget);
    c.equipItemRequest = itemName;
    ++c.equipItemRequestCount;
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB09() {
    static const std::vector<std::string> names = {
        // Sec7 batch - see this file's own doc comment above each stub_*
        // function for the full citation/reasoning.
        "npc_leash_remove",
        "npc_leash_to_nav",
        "set_attack_player_flag",
        "set_animation_state",
        "set_script_animation_state",
        "ai_set_action_delay",
        "human_gravity_enable",
        // Sec8 batch.
        "audio_persona_load_2d",
        "audio_play_persona_line_2d",
        "audio_ambient_emitter_start",
        "audio_ambient_emitter_stop",
        "hud_bar_off",
        "hud_bar_on",
        "hud_bar_set_value",
        "hud_prompt_clear",
        "hud_set_fake_notoriety",
        "hud_timer_set",
        "hud_timer_stop",
        "vint_set_mouse_cursor",
        "push_screen",
        // Sec9 batch.
        "explosion_create",
        "vehicle_max_speed",
        "vehicle_detonate",
        "helicopter_set_path_orientation",
        "inv_item_equip",
    };
    return names;
}

void registerB09(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // Sec7 batch.
    if (wants("npc_leash_remove")) registerOne(L, state, log, stateTag, "npc_leash_remove", openGuard<stub_npc_leash_remove>);
    if (wants("npc_leash_to_nav")) registerOne(L, state, log, stateTag, "npc_leash_to_nav", openGuard<stub_npc_leash_to_nav>);
    if (wants("set_attack_player_flag")) registerOne(L, state, log, stateTag, "set_attack_player_flag", openGuard<stub_set_attack_player_flag>);
    if (wants("set_animation_state")) registerOne(L, state, log, stateTag, "set_animation_state", openGuard<stub_set_animation_state>);
    if (wants("set_script_animation_state")) registerOne(L, state, log, stateTag, "set_script_animation_state", openGuard<stub_set_script_animation_state>);
    if (wants("ai_set_action_delay")) registerOne(L, state, log, stateTag, "ai_set_action_delay", openGuard<stub_ai_set_action_delay>);
    if (wants("human_gravity_enable")) registerOne(L, state, log, stateTag, "human_gravity_enable", openGuard<stub_human_gravity_enable>);
    // Sec8 batch.
    if (wants("audio_persona_load_2d")) registerOne(L, state, log, stateTag, "audio_persona_load_2d", openGuard<stub_audio_persona_load_2d>);
    if (wants("audio_play_persona_line_2d")) registerOne(L, state, log, stateTag, "audio_play_persona_line_2d", openGuard<stub_audio_play_persona_line_2d>);
    if (wants("audio_ambient_emitter_start")) registerOne(L, state, log, stateTag, "audio_ambient_emitter_start", openGuard<stub_audio_ambient_emitter_start>);
    if (wants("audio_ambient_emitter_stop")) registerOne(L, state, log, stateTag, "audio_ambient_emitter_stop", openGuard<stub_audio_ambient_emitter_stop>);
    if (wants("hud_bar_off")) registerOne(L, state, log, stateTag, "hud_bar_off", openGuard<stub_hud_bar_off>);
    if (wants("hud_bar_on")) registerOne(L, state, log, stateTag, "hud_bar_on", openGuard<stub_hud_bar_on>);
    if (wants("hud_bar_set_value")) registerOne(L, state, log, stateTag, "hud_bar_set_value", openGuard<stub_hud_bar_set_value>);
    if (wants("hud_prompt_clear")) registerOne(L, state, log, stateTag, "hud_prompt_clear", openGuard<stub_hud_prompt_clear>);
    if (wants("hud_set_fake_notoriety")) registerOne(L, state, log, stateTag, "hud_set_fake_notoriety", openGuard<stub_hud_set_fake_notoriety>);
    if (wants("hud_timer_set")) registerOne(L, state, log, stateTag, "hud_timer_set", openGuard<stub_hud_timer_set>);
    if (wants("hud_timer_stop")) registerOne(L, state, log, stateTag, "hud_timer_stop", openGuard<stub_hud_timer_stop>);
    if (wants("vint_set_mouse_cursor")) registerOne(L, state, log, stateTag, "vint_set_mouse_cursor", openGuard<stub_vint_set_mouse_cursor>);
    if (wants("push_screen")) registerOne(L, state, log, stateTag, "push_screen", openGuard<stub_push_screen>);
    // Sec9 batch.
    if (wants("explosion_create")) registerOne(L, state, log, stateTag, "explosion_create", openGuard<stub_explosion_create>);
    if (wants("vehicle_max_speed")) registerOne(L, state, log, stateTag, "vehicle_max_speed", openGuard<stub_vehicle_max_speed>);
    if (wants("vehicle_detonate")) registerOne(L, state, log, stateTag, "vehicle_detonate", openGuard<stub_vehicle_detonate>);
    if (wants("helicopter_set_path_orientation")) registerOne(L, state, log, stateTag, "helicopter_set_path_orientation", openGuard<stub_helicopter_set_path_orientation>);
    if (wants("inv_item_equip")) registerOne(L, state, log, stateTag, "inv_item_equip", openGuard<stub_inv_item_equip>);
}

} // namespace stub_sections
} // namespace sr3luahost
