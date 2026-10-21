// lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp
//
// Wave 2, Batch W2-3 (2026-10-08): "Sec20+Sec21 gameplay residue", 57 names
// from spec-lua-api-behaviour.md Sec20 ("Ranks 351-450 tranche, part C - 32
// gameplay-cluster functions") and Sec21 ("Ranks 351-450 tranche, part A -
// 30 gameplay-cluster functions"). This is a NEW file, following the exact
// same pattern as every other bNN section (own anonymous-namespace stub_*
// functions, own namesB16()/registerB16(wants) pair declared in
// lua_spec_confirmed_stubs_sections.h, called from
// lua_spec_confirmed_stubs_registry.cpp) - see that file's own top comment
// for the full split rationale. New EngineState fields this batch needs
// live in include/sr3luahost/engine_state.h, each cited individually at its
// own declaration (CharacterState/VehicleState/PlacedObjectState/EngineState
// "Wave 2, Batch W2-3" blocks).
//
// The 57 names: dlc_achievement_counter_get (Sec20.25), player_grenades_
// disable (Sec21.17), ingame_effect_add_trigger (Sec21.5), team_make_allies
// (Sec20.6), spawning_allow_cop_ambient_spawns (Sec21.14), mission_is_
// complete (Sec20.14), vehicle_car_alarm_enable (Sec20.3), character_
// disable_revival (Sec21.9), character_set_only_player_grabs (Sec21.8),
// continuous_explosion_avoid_object_add (Sec21.7), hud_push_screen
// (Sec21.6), inv_item_remove_in_slot (Sec21.4), notoriety_set_override_timer
// (Sec21.3), npc_use_closest_action_node_of_type (Sec21.2), on_collision
// (Sec21.1), action_stop_custom (Sec21.28), change_mission_enemy_team
// (Sec21.27), character_is_in_a_driver_seat (Sec21.26), drug_effect_clear_
// override_values (Sec21.25), get_char_vehicle_type (Sec21.24), inv_item_
// equip_slot (Sec21.23), npc_hearing_enable (Sec21.21), npc_refire_rate_
// override (Sec21.20), on_door_opened (Sec21.19), on_weapon_equip (Sec21.18),
// set_force_combat_ready_team (Sec21.16), set_script_animation_group
// (Sec21.15), trigger_get_radius (Sec21.13), vehicle_anim_finished
// (Sec21.12), vehicle_set_allow_ram_ped (Sec21.10), vehicle_prevent_vehicle_
// collision_damage (Sec21.30), vehicle_skydiving_move_to_do (Sec21.29),
// character_disable_human_downed_interactions (Sec20.31), character_has_
// human_shield (Sec20.30), character_ignite (Sec20.29), character_is_
// jumping (Sec20.28), character_set_counter_on_grabbed_by_player (Sec20.27),
// character_set_only_scripted_grabs (Sec20.26), get_object_pos (Sec20.24),
// get_team (Sec20.23), hud_timer_get_remainder (Sec20.22), human_enter_
// downed (Sec20.21), human_skydive_create_npc_backpack (Sec20.20), interior_
// disable_exterior (Sec20.19), inv_has_item (Sec20.18), inv_item_get_
// equipped_item (Sec20.17), inv_item_is_equipped (Sec20.16), light_group_
// use_intensity (Sec20.15), mission_set_enemy_team (Sec20.13), on_pickup
// (Sec20.12), persona_suppress_ambient_lines (Sec20.11), player_warp_to_
// shore_enable (Sec20.10), reticle_override_set (Sec20.8), skydive_set_
// catching_shaundi (Sec20.7), tutorial_active (Sec20.5), tutorial_vehicle_
// controls_enabled (Sec20.4), vehicle_repair (Sec20.2).
//
// TRANSPOSITION HAZARD, explicitly flagged by the orchestrator's own
// dispatch and re-verified here: character_set_counter_on_grabbed_by_player
// (Sec20.27, 0x00a42200, THIS batch) sits at an address only 0x60 away from
// the ALREADY-REGISTERED `character_set_counter_on_grabbed` (Sec28.19,
// 0x00a421a0, lua_spec_confirmed_stubs_b02_sec27_28.cpp) - a near-identical
// name but a COMPLETELY DIFFERENT real mechanism (that sibling writes
// EngineState::counterOnGrabbed().value[name] via its own dedicated double-
// gate; THIS native writes bit 0x4000000 of CharacterState::forceFlags1c98,
// the SAME shared word character_set_only_scripted_grabs/character_set_
// only_player_grabs also use). See tests/synthetic_luahost_wave2_w23_test.cpp's
// own dedicated cross-check test (asserts setting one never touches the
// other's own storage).
//
// mission_is_complete (Sec20.14) is CONFIRMED (Sec20.14's own 2026-10-01
// resolution, cross-referenced at Sec27.2) to be the SAME function
// (0x00a525a0) as the ALREADY-REGISTERED `cell_is_mission_complete`
// (Sec27.2, lua_spec_confirmed_stubs_b02_sec27_28.cpp), registered under two
// Lua names. Per the task's own explicit instruction, this file does NOT
// re-derive or duplicate that body: stub_mission_is_complete below calls the
// EXACT SAME public EngineState primitives (objectResolves()/
// missionComplete()) stub_cell_is_mission_complete already calls - the
// shared behavior lives in those two shared accessors, not in either stub's
// own few lines, so wiring a second name to them is reuse, not
// re-derivation (consistent with how team_make_allies below reuses
// setTeamRelationIfInBounds()/recordTeamRelationOobGuard() rather than
// re-deriving team_make_hostile's/team_make_unfriendly's own hardening).
//
// team_make_allies (Sec20.6): writes relation value 4 symmetrically (team 5
// mirrors team 6) via the SAME EngineState::setTeamRelationIfInBounds()/
// recordTeamRelationOobGuard() infrastructure team_make_hostile (Sec38.2,
// value 0)/team_make_unfriendly (Sec30.6, value 1) already use and already
// HARDENED (an unknown/out-of-bounds team name is refused - no write, no
// replication - rather than an out-of-bounds matrix write, matching that
// EXACT existing hardening pattern verbatim, not a new one invented here).
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include "sr3airuntime/combat_ai.h"

#include <algorithm>
#include <cmath>

namespace sr3luahost {
namespace {

// =======================================================================
// Sec20 group (29 names)
// =======================================================================

// vehicle_repair (Sec20.2, 0x00a64370). 1 mandatory string (vehicle name).
// 1 optional number (repair fraction), standard nil-gated idiom, default
// 1.0 (the byte-confirmed value of the shared `.rdata` constant 0x01117a4c,
// Sec24.9's own "byte-confirmed" resolution - the SAME default
// helicopters_set_jitter_override/Sec18.13 already cites by role). 1
// further optional number, rounded, default sentinel -1 (CONFIRMED, no
// further in-scope effect beyond being recorded - Sec20.2's own text never
// describes a second local consumer of this argument). Return: none.
// CONFIRMED: only proceeds if the repair fraction is positive. Reuses
// VehicleState::minHitPointsV/maxHitPointsV/currentHitPointsV (Sec13.19/
// Sec13.20) for the actual heal step: repair amount = maxHitPointsV *
// fraction, applied as healing (added to currentHitPointsV, clamped to
// [0, maxHitPointsV]) - CONFIRMED shape ("the actual 'heal' step -
// computes a repair amount scaled by the vehicle's own +0x155c field times
// the repair fraction and applies it as a NEGATED value"). CHOSEN (project-
// wide single-player simplification, same as every other single-gate
// authority split in this header): always takes the authoritative local-
// apply branch (the non-authoritative "zero current HP first" branch is
// dead code with no co-op session installed). The sub-component-reset/
// physics-recompute gate (+0x1e00 sub-object live AND +0xbd0 flag) is
// abstracted to VehicleState::repairSubObjectActive, OPEN until a test sets
// it - gates ONLY that inner reset/recompute step (not modeled further, no
// in-scope reader), not the HP-repair step itself (CONFIRMED unconditional
// once the fraction is positive and the vehicle resolves).
int stub_vehicle_repair(lua_State* L) {
    logCall(L, upLog(L), "vehicle_repair", upStateTag(L));
    std::string name = argString(L, 1);
    double fraction = optionalNumberDefault(L, 2, 1.0);
    double roundedArg = truncateEa2596(optionalNumberDefault(L, 3, -1.0));
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    if (!(fraction > 0.0)) return 0; // CONFIRMED: only proceeds if positive
    VehicleState& v = st->getOrCreateVehicle(name);
    v.lastRepairRoundedArg = roundedArg;
    int32_t repairAmount = static_cast<int32_t>(std::lround(v.maxHitPointsV * fraction));
    v.currentHitPointsV = std::clamp(v.currentHitPointsV + repairAmount, 0, v.maxHitPointsV);
    ++v.repairAppliedCount;
    if (v.repairSubObjectActive.known() && v.repairSubObjectActive.get()) {
        // the sub-component reset/physics-recompute step - internals OPEN, not modeled beyond this gate
    }
    EngineState::replicateStateChange("vehicle_repair", name);
    return 0;
}

// vehicle_car_alarm_enable (Sec20.3, 0x00a62100). 1 mandatory string
// (vehicle name). 1 mandatory boolean, no nil-gate. Return: none. CONFIRMED:
// resolves via objectResolves(); if the sub-object capability gate (own
// +0x2c field) is zero ("not disabled by design"), sets/clears bit 0x8 of
// +0x163d to the boolean. No resolver-of-a-second-object, no record, no
// replication.
int stub_vehicle_car_alarm_enable(lua_State* L) {
    logCall(L, upLog(L), "vehicle_car_alarm_enable", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& v = st->getOrCreateVehicle(name);
    if (v.alarmDisabledByDesign.known() && !v.alarmDisabledByDesign.get()) { // OPEN until set
        v.statusByte163d.setBits(VehicleState::kCarAlarmEnabledBit163d, flag ? VehicleState::kCarAlarmEnabledBit163d : 0u);
    }
    return 0;
}

// tutorial_vehicle_controls_enabled (Sec20.4, 0x00a60310). 1 mandatory
// boolean, no nil-gate. Return: none. CONFIRMED: writes the boolean
// straight to a single global (0x02845bcc). No resolver, no gate, no
// record.
int stub_tutorial_vehicle_controls_enabled(lua_State* L) {
    logCall(L, upLog(L), "tutorial_vehicle_controls_enabled", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0;
    upState(L)->tutorialVehicleControlsEnabled() = flag;
    return 0;
}

// tutorial_active (Sec20.5, 0x00a60060). 1 mandatory string (tutorial
// name). Return: 1 boolean, always exactly 1 value. CONFIRMED: linear-
// scanned (case-insensitive) against a fixed 210-entry table; on a valid
// index, tests the per-tutorial-entry state (0x007162a0) for exactly 3
// ("active" per this function's own registered name, CONFIRMED); no match
// -> false. Reuses EngineState::tutorialState() (Sec6.19/Sec10.4/Sec26.28)
// directly - a name `.known()` to that map stands in for "found in the
// 210-entry table" (this project's own name-keyed simplification, same
// convention as every other fixed-table lookup in this header).
int stub_tutorial_active(lua_State* L) {
    logCall(L, upLog(L), "tutorial_active", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool active = false;
    if (!name.empty() && st->tutorialState().known(name)) {
        active = st->tutorialState().get(name) == 3; // CONFIRMED "active" state value
    }
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

// team_make_allies (Sec20.6, 0x00a60600). 2 mandatory strings (team
// names). No boolean, no optional argument. Return: none. CONFIRMED: both
// names convert via resolveTeamId() (0x0094cc60); writes relation value 4
// symmetrically (team 5 mirrors team 6) via setTeamRelationIfInBounds() -
// the SAME shared infrastructure team_make_hostile (Sec38.2)/team_make_
// unfriendly (Sec30.6) already use and already HARDENED (an out-of-bounds
// team id is refused via recordTeamRelationOobGuard(), no write, no
// replication - matching that EXACT existing pattern, not a new one). Then
// a LOCAL-only dispatch record (opcode 0x43, sub-tag 0x30, literal relation
// value 4) - committed via 0x0086f1b0, tracked here via the standard
// replicateStateChange() no-op call site marker. Per spec-lua-api-
// behaviour.md Sec30.6 ("the two 'auxiliary arrays' that note names are the
// SAME relation matrix at rows/column 6"), the "team 5" mirroring onto team
// 6 is not a separate, unmodeled mechanism - it IS setTeamRelationIfInBounds()'s
// own existing team-5-mirrors-team-6 refinement, already exercised here
// through the shared call (post-merge correction - a fresh reviewer found
// the original "NOT modeled, separate mechanism" claim here was false).
int stub_team_make_allies(lua_State* L) {
    logCall(L, upLog(L), "team_make_allies", upStateTag(L));
    std::string team1 = argString(L, 1);
    std::string team2 = argString(L, 2);
    EngineState* st = upState(L);
    int id1 = st->resolveTeamId(team1);
    int id2 = st->resolveTeamId(team2);
    if (!st->setTeamRelationIfInBounds(id1, id2, 4)) { // value 4 = allies (CONFIRMED)
        st->recordTeamRelationOobGuard(); // HOST-SAFETY: shared Sec30.6/Sec38.2 infra, see comment above
        logCall(L, upLog(L), "team_make_allies:OOB_TEAM_INDEX_GUARD", upStateTag(L));
        return 0;
    }
    EngineState::replicateStateChange("team_make_allies", team1 + "/" + team2); // always, no authority gate (CONFIRMED)
    return 0;
}

// skydive_set_catching_shaundi (Sec20.7, 0x00a5dae0). 2 mandatory
// booleans, no nil-gate on either (read unconditionally). Return: none. No
// Lua name argument at all - fixed-name entities. CONFIRMED: delegates to
// 0x009b0050(boolA, boolB, hardcoded false) - the hardcoded-false 3rd
// argument means the opcode-0x41 network record ALWAYS opens, every call
// (tracked as a plain counter, this project's standard no-op replication
// stand-in). Regardless, the REAL local setup/teardown always runs:
// boolA=false tears down (releases 4 handles if currently active, stops a
// cached conversation if one is tracked); boolA=true sets up (acquires 4
// handles, marks active, stores boolB as the "catching" sub-mode).
int stub_skydive_set_catching_shaundi(lua_State* L) {
    logCall(L, upLog(L), "skydive_set_catching_shaundi", upStateTag(L));
    bool boolA = lua_toboolean(L, 1) != 0;
    bool boolB = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    auto& s = st->skydiveCatchingShaundi();
    ++s.networkRecordCount; // unconditional, hardcoded-false 3rd argument (CONFIRMED)
    if (!boolA) {
        if (s.activeSentinel != -1) {
            ++s.releaseCount; // 4 handles released (Shaundi/cargo-plane/current-player/network-roster)
            s.activeSentinel = -1;
        }
        if (s.cachedConversationId != -1) {
            ++s.conversationStopCount;
            s.cachedConversationId = -1;
        }
    } else {
        ++s.acquireCount; // 4 handles acquired
        s.activeSentinel = 0;
        s.catchingSubMode = boolB;
    }
    return 0;
}

// reticle_override_set (Sec20.8, 0x00a5b820). 2 mandatory strings + 1
// mandatory number (rounded) - no optional-argument handling, all 3 read
// unconditionally. Return: none. CONFIRMED: delegates to 0x007f5750(str1,
// str2, flagsByte). Bit 0x1: writes str1/str2 into two fixed 16-byte
// buffers (CONFIRMED truncating copy via the real "fixed-size copy helper"
// 0x00da78d0 - a safe bounded copy, not a hardening divergence), sets
// "override active" and a duration field to 1.0 (the byte-confirmed
// 0x01117a4c constant). Bit 0x2: opens a LOCAL-only dispatch record
// (opcode 0x40, sub-tag 0x25/37) - tracked via replicateStateChange.
int stub_reticle_override_set(lua_State* L) {
    logCall(L, upLog(L), "reticle_override_set", upStateTag(L));
    std::string str1 = argString(L, 1);
    std::string str2 = argString(L, 2);
    int64_t flagsByte = truncateEa2596(lua_tonumber(L, 3));
    EngineState* st = upState(L);
    auto& r = st->reticleOverride();
    if (flagsByte & 0x1) {
        r.str1 = str1.substr(0, EngineState::kReticleOverrideBufferCap); // CONFIRMED safe truncating copy
        r.str2 = str2.substr(0, EngineState::kReticleOverrideBufferCap);
        r.active = true;
        r.duration = 1.0; // byte-confirmed 0x01117a4c
    }
    if (flagsByte & 0x2) {
        EngineState::replicateStateChange("reticle_override_set", str1 + "/" + str2);
    }
    return 0;
}

// player_warp_to_shore_enable (Sec20.10, 0x00a59ac0). 1 mandatory string
// (character name). No boolean argument exists at all. Return: none.
// CONFIRMED: resolves via objectResolves(); delegates to 0x009e0840
// (character, hardcoded false). Same double-gate (variant 1) template as
// Sec20.1/Sec20.26/Sec20.27/Sec20.31: gate passes -> clears bit 0x2 of the
// EXISTING CharacterState::statusByte28ad field (Sec32.3's own "cant_be_
// downed 0x1 / human_shield_locked 0x8" byte, a CONFIRMED distinct bit on
// that same shared byte); gate fails -> replicate only. Because the
// boolean is permanently hardcoded false, this entry point can only ever
// CLEAR the bit, i.e. it always ENABLES warp-to-shore (CONFIRMED, not a
// toggle).
int stub_player_warp_to_shore_enable(lua_State* L) {
    logCall(L, upLog(L), "player_warp_to_shore_enable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.statusByte28ad.setBits(0x2u, 0u); // always clears (hardcoded false), CONFIRMED
    } else {
        EngineState::replicateStateChange("player_warp_to_shore_enable", name);
    }
    return 0;
}

// persona_suppress_ambient_lines (Sec20.11, 0x00a5ae80). 1 mandatory
// boolean, no nil-gate. Return: none. CONFIRMED: a reference-counted
// counter (floor-at-zero decrement, NOT a plain toggle) - true increments,
// false decrements only if currently non-zero.
int stub_persona_suppress_ambient_lines(lua_State* L) {
    logCall(L, upLog(L), "persona_suppress_ambient_lines", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0;
    upState(L)->personaSuppressAmbientLinesAdjust(flag);
    return 0;
}

// on_pickup (Sec20.12, 0x00a58300). ARGUMENT ORDER CORRECTED from real
// script usage (m23: on_pickup("m23_minigun_cb", gun); m08/m16: on_pickup("",
// item)): arg 1 is the opaque callback name, arg 2 the item name - the same
// (callback, target) order as on_collision/on_weapon_pickup. Sec20.12's text
// lists them item-first; flagged to Team A as a spec-sync question. Return: none. CONFIRMED:
// resolves via objectResolves() (this project's flat stand-in for the
// item/pickup-kind resolver 0x00729080); registers hook slot 0 on the
// item's own +0xc0 sub-record (raw-disassembly-confirmed hidden-`this`).
int stub_on_pickup(lua_State* L) {
    logCall(L, upLog(L), "on_pickup", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string itemName = argString(L, 2);
    EngineState* st = upState(L);
    if (st->objectResolves().get(itemName)) { // OPEN until set
        st->getOrCreatePlacedObject(itemName).onPickupCallback = callbackName;
    }
    return 0;
}

// mission_set_enemy_team (Sec20.13, 0x00a526b0). 1 mandatory string (team
// name). Return: none. CONFIRMED: converts the name via resolveTeamId()
// (0x0094cc60); writes the global missionEnemyTeamId only if
// missionContext().activeAndRunning is true (0x014c8460/0x014c7a14,
// CONFIRMED).
int stub_mission_set_enemy_team(lua_State* L) {
    logCall(L, upLog(L), "mission_set_enemy_team", upStateTag(L));
    std::string teamName = argString(L, 1);
    EngineState* st = upState(L);
    int teamId = st->resolveTeamId(teamName);
    if (st->missionContext().activeAndRunning.get()) { // OPEN until set
        st->missionEnemyTeamId = teamId;
    }
    return 0;
}

// mission_is_complete (Sec20.14, 0x00a525a0) - CONFIRMED the SAME function
// as the ALREADY-REGISTERED `cell_is_mission_complete` (Sec27.2,
// lua_spec_confirmed_stubs_b02_sec27_28.cpp's own stub_cell_is_mission_
// complete), registered under two Lua names (Sec20.14's own 2026-10-01
// resolution). Wired to the EXISTING shared state (EngineState::
// objectResolves()/missionComplete()) that sibling stub already reads -
// not re-derived, not duplicated (see this file's own top comment). 1
// mandatory string (mission name). Return: 1 boolean.
int stub_mission_is_complete(lua_State* L) {
    logCall(L, upLog(L), "mission_is_complete", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool complete = false;
    if (!name.empty()) {
        if (st->objectResolves().get(name)) { // OPEN until set - SAME shared map stub_cell_is_mission_complete reads
            complete = st->missionIsComplete(name); // Sec20.14 port pointer: false unless host state says complete
        }
    }
    lua_pushboolean(L, complete ? 1 : 0);
    return 1;
}

// light_group_use_intensity (Sec20.15, 0x00a51fe0). 1 mandatory string
// (light-group name). 1 mandatory boolean, no nil-gate. Return: none.
// CONFIRMED: resolves via objectResolves(); if the host-check gate passes
// (coopLocalIsHost(), the SAME 0x0087ba20 own +0x5c==+0x58 test), opens a
// LOCAL-only dispatch record (opcode 0x43, sub-tag 0x26/38) - tracked via
// replicateStateChange. UNCONDITIONALLY (regardless of that gate):
// sets/clears +0x40 bit 0x2 ("use custom intensity"). Child-list
// propagation is not modeled (no child-registry data anywhere in this
// project).
int stub_light_group_use_intensity(lua_State* L) {
    logCall(L, upLog(L), "light_group_use_intensity", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    if (st->coopLocalIsHost()) {
        EngineState::replicateStateChange("light_group_use_intensity", name);
    }
    st->getOrCreatePlacedObject(name).useCustomIntensity = flag; // unconditional (CONFIRMED)
    return 0;
}

// inv_item_is_equipped (Sec20.16, 0x00a50a20). 1 optional string (specific
// item name), standard nil-gated idiom, default absent. 1 optional string
// (character name), standard nil-gated idiom, default "#PLAYER1#". Return:
// 1 boolean. Reuses CharacterState::equippedItemName (the SAME accessor
// inv_item_get_equipped_item/set_script_animation_group/inv_item_equip_
// slot all read, per Sec21.15's own cross-reference). CONFIRMED
// counterintuitive polarity: with arg1 omitted, true means NOTHING is
// equipped; with arg1 given, true only on a case-insensitive name match.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_is_equipped(lua_State* L) {
    logCall(L, upLog(L), "inv_item_is_equipped", upStateTag(L));
    bool hasFilter = lua_gettop(L) >= 1 && lua_type(L, 1) != LUA_TNIL;
    std::string filter = hasFilter ? argString(L, 1) : "";
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    bool result = false;
    std::string resolvedTarget;
    if (resolveFollowerAwareObject(st, target, resolvedTarget)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        const std::string& equipped = st->getOrCreateCharacter(resolvedTarget).equippedItemName;
        if (!hasFilter) {
            result = equipped.empty(); // CONFIRMED inverted polarity
        } else {
            result = !equipped.empty() && caseInsensitiveEquals(equipped, filter);
        }
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// inv_item_get_equipped_item (Sec20.17, 0x00a508c0). 1 mandatory string
// (character name). Return: 1 value - the equipped item's name, or nil.
// Reuses CharacterState::equippedItemName (SAME shared accessor, see
// Sec20.16 above).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_get_equipped_item(lua_State* L) {
    logCall(L, upLog(L), "inv_item_get_equipped_item", upStateTag(L));
    std::string target = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedTarget;
    if (resolveFollowerAwareObject(st, target, resolvedTarget)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        const std::string& equipped = st->getOrCreateCharacter(resolvedTarget).equippedItemName;
        if (!equipped.empty()) {
            lua_pushstring(L, equipped.c_str());
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// inv_has_item (Sec20.18, 0x00a50660). 1 mandatory string (item name). 1
// optional string (character name), standard nil-gated idiom, default
// "#PLAYER1#". Return: 1 boolean. CHOSEN: this project has no real
// inventory-contents model - CharacterState::inventoryItemPresence is a
// plain, zero-default map (no item unless a test sets one), not an
// OpenValueMap/OPEN refusal (see that field's own doc comment).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_has_item(lua_State* L) {
    logCall(L, upLog(L), "inv_has_item", upStateTag(L));
    std::string itemName = argString(L, 1);
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    bool has = false;
    std::string resolvedTarget;
    if (resolveFollowerAwareObject(st, target, resolvedTarget)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        auto& presence = st->getOrCreateCharacter(resolvedTarget).inventoryItemPresence;
        auto it = presence.find(itemName);
        has = it != presence.end() && it->second;
    }
    lua_pushboolean(L, has ? 1 : 0);
    return 1;
}

// interior_disable_exterior (Sec20.19, 0x00a51880). 1 mandatory string
// (interior name). 1 mandatory boolean, no nil-gate. Return: none.
// CONFIRMED: resolves via objectResolves(); if EngineState::
// replicationModeFlag0x024d4461 (the SAME global action_stop_synced_state,
// Sec18.4, out of scope, already establishes) is set, opens a LOCAL-only
// dispatch record (opcode 0x43, sub-tag 0x2c/44) - tracked via
// replicateStateChange. UNCONDITIONALLY: sets/clears +0x7b bit 0x10
// ("exterior disabled"), additionally setting the +0x7d bit 0x2 "dirty"
// flag only when the value actually changed (CONFIRMED).
int stub_interior_disable_exterior(lua_State* L) {
    logCall(L, upLog(L), "interior_disable_exterior", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    if (st->replicationModeFlag0x024d4461.get()) { // OPEN until set
        EngineState::replicateStateChange("interior_disable_exterior", name);
    }
    PlacedObjectState& p = st->getOrCreatePlacedObject(name);
    if (p.exteriorDisabled != flag) {
        p.exteriorDisabled = flag;
        p.exteriorDisabledDirtyBit = true; // CONFIRMED: only on a real change
    }
    return 0;
}

// human_skydive_create_npc_backpack (Sec20.20, 0x00a4d380). 1 mandatory
// string (character name), resolved via the #FOLLOWER#-then-character
// chain 0x00a28150, resolveFollowerAwareObject() - wired 2026-10-08
// (follower-sentinel-resolver pass). Return: none. CONFIRMED:
// only acts if the capability gate (CharacterState::canCarryBackpack,
// class-descriptor +0xa bit 0x2) is set. Creates a cosmetic backpack
// (single global tracker - releases any prior tracked backpack first) plus
// a secondary type-0x1e visual object.
int stub_human_skydive_create_npc_backpack(lua_State* L) {
    logCall(L, upLog(L), "human_skydive_create_npc_backpack", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (!(c.canCarryBackpack.known() && c.canCarryBackpack.get())) return 0; // OPEN until set
    if (!st->currentTrackedBackpackCharacterName.empty()) {
        ++st->backpackPriorReleasedCount;
    }
    st->currentTrackedBackpackCharacterName = resolvedName;
    ++st->backpackCreatedCount;
    ++st->backpackSecondaryVisualCreatedCount; // the type-0x1e object, same position, OPEN role
    return 0;
}

// human_enter_downed (Sec20.21, 0x00a4ea10). 1 mandatory string (character
// name). 1 optional number (duration), rounded, default sentinel -1.
// Return: none. CONFIRMED: resolves via objectResolves(); skips entirely if
// already in state 6 (reuses CharacterState::lifeState, Sec7.13/Sec32.3).
// CHOSEN (single-player simplification, same as every other single-gate
// authority split): always takes the authoritative branch - checks
// enterDownedApplyGate AND duration > 0 before applying.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_human_enter_downed(lua_State* L) {
    logCall(L, upLog(L), "human_enter_downed", upStateTag(L));
    std::string name = argString(L, 1);
    double duration = truncateEa2596(optionalNumberDefault(L, 2, -1.0));
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    name = resolvedName;
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.lifeState.get() == 6) return 0; // CONFIRMED "already downed" skip, reuses existing field
    c.lastEnterDownedDuration = duration;
    if (c.enterDownedApplyGate.known() && c.enterDownedApplyGate.get()) { // OPEN until set
        ++c.enterDownedAppliedCount;
        // Team A 2026-10-20 (Sec21.10.1 note, FINAL): the number is truncated to an integer and is MILLISECONDS; an absent or
        // non-positive duration keeps the type's default (type 0 default used here: 30000 ms x the difficulty scale).
        if (duration <= 0.0)
            duration = static_cast<double>(sr3airuntime::combat::DownedDeadlineMs(0, st->downedTimerScale(), false));
        c.lifeState.set(6);
        c.downedDeadlineSeconds = st->syncedActionNowSeconds() + duration / 1000.0;
        // Type 1 enter (0x009728c0): buffer = int(+0x1588) + threshold; HP is already 0 here, so the buffer is the threshold.
        c.downedBufferKnown = c.bleedOutHitPoints.has_value();
        c.downedBuffer = c.bleedOutHitPoints.value_or(0);
    }
    return 0;
}

// hud_timer_get_remainder (Sec20.22, 0x00a4e7a0). 1 mandatory number
// (timer id), rounded. Return: 1 number. CONFIRMED: indexes the SAME fixed
// 4-slot timer array hud_timer_set/hud_timer_stop (Sec8.19/Sec8.20) already
// model, with NO bounds check on the index at all (neither upper nor
// lower) - a genuine OOB-read memory-safety bug, HARDENED here (see
// HudHardeningCounters::timerGetRemainderOobCount's own doc comment): an
// in-range id [0,4) reads the record's own remainingTicks field exactly
// (adding 4294967296.0 - the byte-confirmed value of 0x0113e6c8 - when
// negative, the CONFIRMED unsigned-wraparound correction); any other id is
// refused (0.0 returned, counted, no OOB read performed).
int stub_hud_timer_get_remainder(lua_State* L) {
    logCall(L, upLog(L), "hud_timer_get_remainder", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    HudTimerSlot* slot = st->hudTimerSlot(id);
    if (!slot) {
        ++st->hudHardeningCounters().timerGetRemainderOobCount; // CHOSEN (hardened divergence), Sec20.22
        logCall(L, upLog(L), "hud_timer_get_remainder:OOB_TIMER_INDEX_GUARD", upStateTag(L));
        lua_pushnumber(L, 0.0);
        return 1;
    }
    int32_t raw = slot->remainingTicks;
    double value = raw < 0 ? (static_cast<double>(raw) + 4294967296.0) : static_cast<double>(raw);
    lua_pushnumber(L, value);
    return 1;
}

// get_team (Sec20.23, 0x00a4c2f0). 1 mandatory string (object name).
// Return: 1 value - a team-name string, or nil. CHOSEN: the real 4-stage
// resolver cascade collapses to objectResolves() (Sec29 convention);
// nothing in this project's own scope WRITES a per-object team id
// (vehicle_set_team, Sec18.20, is not itself implemented here) - a pure
// query surface, OPEN until a test sets a team name via
// EngineState::teamNameByObject().
int stub_get_team(lua_State* L) {
    logCall(L, upLog(L), "get_team", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        // Spec 20.23 blocker m09 (FINAL): the player (and Saints homies) carries team id 0 -> "Playas". A test or
        // a future team model can still set a per-object name; every other object stays OPEN.
        if (st->teamNameByObject().known(name)) {
            lua_pushstring(L, st->teamNameByObject().get(name).c_str());
        } else if (name.rfind("#PLAYER", 0) == 0) {
            lua_pushstring(L, EngineState::teamNameForId(0));
        } else if (st->hasCharacter(name) && !st->getOrCreateCharacter(name).teamName.empty()) {
            // Spec 20.23 (2026-10-15): a zone script_npc with an explicit script_npc_team reads that team back
            // through the id table (case-insensitive name -> id, unknown -> 9 "None").
            lua_pushstring(L, EngineState::canonicalTeamName(st->getOrCreateCharacter(name).teamName));
        } else {
            lua_pushstring(L, st->teamNameByObject().get(name).c_str()); // OPEN until set (throws)
        }
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

// get_object_pos (Sec20.24, 0x00a4c200). 1 mandatory string (object name).
// Return: 6 numbers on success (x,y,z + 3 trailing placeholder values), 3
// numbers on failure (the 3 trailing placeholder values only). CONFIRMED:
// the trailing 3 values are, in EVERY case, the exact same fixed `.rdata`
// constant (0x0112a1e0) repeated 3 times - genuinely OPEN (never
// independently byte-confirmed anywhere in this spec, unlike 0x01117a4c),
// refused honestly via EngineState::objectPosTrailingConstant rather than
// guessed. Position reuses CharacterState::worldPosition (the SAME "treat
// every resolved name as character-shaped" simplification get_dist/
// teleport_vehicle already establish).
int stub_get_object_pos(lua_State* L) {
    logCall(L, upLog(L), "get_object_pos", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    double trailing = st->objectPosTrailingConstant.get(); // OPEN until set - needed on BOTH paths
    if (st->objectResolves().get(name)) { // OPEN until set
        // REAL fallback (2026-10-10, m07 live-playtest follow-up) - see
        // EngineState::tryResolveWorldPositionFromZoneData()'s own doc
        // comment: try the real zone-data position before requiring a
        // live per-frame bridge write (only ever done for "#PLAYER1#").
        CharacterState& c = st->getOrCreateCharacter(name);
        st->tryResolveWorldPositionFromZoneData(name, c.worldPosition);
        WorldPosition pos = c.worldPosition.get(); // OPEN until set
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        lua_pushnumber(L, pos.z);
        lua_pushnumber(L, trailing);
        lua_pushnumber(L, trailing);
        lua_pushnumber(L, trailing);
        return 6;
    }
    lua_pushnumber(L, trailing);
    lua_pushnumber(L, trailing);
    lua_pushnumber(L, trailing);
    return 3;
}

// dlc_achievement_counter_get (Sec20.25, 0x00a47570). 1 mandatory number
// (counter index), rounded. Return: 1 number if index < 3 (including a
// NEGATIVE index on the real binary - an OOB read, HARDENED here, see
// EngineState::dlcAchievementCounterGet's own doc comment); otherwise
// (index >= 3) pushes NOTHING - a genuine, CONFIRMED "no return value"
// case, not a divergence. Reuses the SAME 3-slot array dlc_achievement_
// counter_set (Sec15.27) already writes.
int stub_dlc_achievement_counter_get(lua_State* L) {
    logCall(L, upLog(L), "dlc_achievement_counter_get", upStateTag(L));
    int64_t index = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    double value = 0.0;
    if (st->dlcAchievementCounterGet(index, value)) {
        lua_pushnumber(L, value);
        return 1;
    }
    if (index < 0) logCall(L, upLog(L), "dlc_achievement_counter_get:NEGATIVE_INDEX_GUARD", upStateTag(L));
    return 0; // CONFIRMED "push nothing" for index >= 3, or HARDENED refusal for index < 0
}

// character_set_only_scripted_grabs (Sec20.26, 0x00a42660). 1 mandatory
// string (character name). 1 optional boolean, standard nil-gated idiom,
// default TRUE. Return: none. CONFIRMED: resolves via the #FOLLOWER#-then-
// character chain 0x00a28150, resolveFollowerAwareObject() - wired
// 2026-10-08 (follower-sentinel-resolver pass); double-gate (variant 1):
// gate passes -> bit 0x2000000 of CharacterState::forceFlags1c98; gate
// fails -> replicate only.
int stub_character_set_only_scripted_grabs(lua_State* L) {
    logCall(L, upLog(L), "character_set_only_scripted_grabs", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kOnlyScriptedGrabsBit1c98, flag ? CharacterState::kOnlyScriptedGrabsBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_set_only_scripted_grabs", resolvedName);
    }
    return 0;
}

// character_set_counter_on_grabbed_by_player (Sec20.27, 0x00a42200). 1
// mandatory string + 1 optional boolean, default TRUE - IDENTICAL shape to
// Sec20.26 above, resolves via 0x00a28150, resolveFollowerAwareObject() -
// wired 2026-10-08 (follower-sentinel-resolver pass). CONFIRMED: delegates
// to 0x00947e50 - the SAME double-gate (variant 1) template, bit 0x4000000
// of the SAME +0x1c98 word Sec20.26 uses (an adjacent bit, confirming
// these are sibling flags) - NOT the unrelated CounterOnGrabbed mechanism
// the similarly-named ALREADY-REGISTERED `character_set_counter_on_grabbed`
// (Sec28.19) uses. See this file's own top comment for the full
// transposition-hazard writeup.
int stub_character_set_counter_on_grabbed_by_player(lua_State* L) {
    logCall(L, upLog(L), "character_set_counter_on_grabbed_by_player", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kCounterOnGrabbedByPlayerBit1c98,
                                  flag ? CharacterState::kCounterOnGrabbedByPlayerBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_set_counter_on_grabbed_by_player", resolvedName);
    }
    return 0;
}

// Defined in lua_spec_confirmed_stubs_b16_wave2_sec20_21_part2.cpp (split out of this file, no behaviour change).
} // namespace
namespace stubs_b16 {
int stub_character_is_jumping(lua_State* L);
int stub_character_ignite(lua_State* L);
int stub_character_has_human_shield(lua_State* L);
int stub_character_disable_human_downed_interactions(lua_State* L);
int stub_on_collision(lua_State* L);
int stub_npc_use_closest_action_node_of_type(lua_State* L);
int stub_notoriety_set_override_timer(lua_State* L);
int stub_inv_item_remove_in_slot(lua_State* L);
int stub_ingame_effect_add_trigger(lua_State* L);
int stub_hud_push_screen(lua_State* L);
int stub_continuous_explosion_avoid_object_add(lua_State* L);
int stub_character_set_only_player_grabs(lua_State* L);
int stub_character_disable_revival(lua_State* L);
int stub_vehicle_set_allow_ram_ped(lua_State* L);
int stub_vehicle_anim_finished(lua_State* L);
int stub_trigger_get_radius(lua_State* L);
int stub_spawning_allow_cop_ambient_spawns(lua_State* L);
int stub_set_script_animation_group(lua_State* L);
int stub_set_force_combat_ready_team(lua_State* L);
int stub_player_grenades_disable(lua_State* L);
int stub_on_weapon_equip(lua_State* L);
int stub_on_door_opened(lua_State* L);
int stub_npc_refire_rate_override(lua_State* L);
int stub_npc_hearing_enable(lua_State* L);
int stub_inv_item_equip_slot(lua_State* L);
int stub_get_char_vehicle_type(lua_State* L);
int stub_drug_effect_clear_override_values(lua_State* L);
int stub_character_is_in_a_driver_seat(lua_State* L);
int stub_change_mission_enemy_team(lua_State* L);
int stub_action_stop_custom(lua_State* L);
int stub_vehicle_skydiving_move_to_do(lua_State* L);
int stub_vehicle_prevent_vehicle_collision_damage(lua_State* L);
} // namespace stubs_b16
namespace {
using stubs_b16::stub_character_is_jumping;
using stubs_b16::stub_character_ignite;
using stubs_b16::stub_character_has_human_shield;
using stubs_b16::stub_character_disable_human_downed_interactions;
using stubs_b16::stub_on_collision;
using stubs_b16::stub_npc_use_closest_action_node_of_type;
using stubs_b16::stub_notoriety_set_override_timer;
using stubs_b16::stub_inv_item_remove_in_slot;
using stubs_b16::stub_ingame_effect_add_trigger;
using stubs_b16::stub_hud_push_screen;
using stubs_b16::stub_continuous_explosion_avoid_object_add;
using stubs_b16::stub_character_set_only_player_grabs;
using stubs_b16::stub_character_disable_revival;
using stubs_b16::stub_vehicle_set_allow_ram_ped;
using stubs_b16::stub_vehicle_anim_finished;
using stubs_b16::stub_trigger_get_radius;
using stubs_b16::stub_spawning_allow_cop_ambient_spawns;
using stubs_b16::stub_set_script_animation_group;
using stubs_b16::stub_set_force_combat_ready_team;
using stubs_b16::stub_player_grenades_disable;
using stubs_b16::stub_on_weapon_equip;
using stubs_b16::stub_on_door_opened;
using stubs_b16::stub_npc_refire_rate_override;
using stubs_b16::stub_npc_hearing_enable;
using stubs_b16::stub_inv_item_equip_slot;
using stubs_b16::stub_get_char_vehicle_type;
using stubs_b16::stub_drug_effect_clear_override_values;
using stubs_b16::stub_character_is_in_a_driver_seat;
using stubs_b16::stub_change_mission_enemy_team;
using stubs_b16::stub_action_stop_custom;
using stubs_b16::stub_vehicle_skydiving_move_to_do;
using stubs_b16::stub_vehicle_prevent_vehicle_collision_damage;

std::vector<std::string> namesB16Impl() {
    return {
        // Sec20 group (29 names)
        "vehicle_repair",
        "vehicle_car_alarm_enable",
        "tutorial_vehicle_controls_enabled",
        "tutorial_active",
        "team_make_allies",
        "skydive_set_catching_shaundi",
        "reticle_override_set",
        "player_warp_to_shore_enable",
        "persona_suppress_ambient_lines",
        "on_pickup",
        "mission_set_enemy_team",
        "mission_is_complete",
        "light_group_use_intensity",
        "inv_item_is_equipped",
        "inv_item_get_equipped_item",
        "inv_has_item",
        "interior_disable_exterior",
        "human_skydive_create_npc_backpack",
        "human_enter_downed",
        "hud_timer_get_remainder",
        "get_team",
        "get_object_pos",
        "dlc_achievement_counter_get",
        "character_set_only_scripted_grabs",
        "character_set_counter_on_grabbed_by_player",
        "character_is_jumping",
        "character_ignite",
        "character_has_human_shield",
        "character_disable_human_downed_interactions",
        // Sec21 group (28 names)
        "on_collision",
        "npc_use_closest_action_node_of_type",
        "notoriety_set_override_timer",
        "inv_item_remove_in_slot",
        "ingame_effect_add_trigger",
        "hud_push_screen",
        "continuous_explosion_avoid_object_add",
        "character_set_only_player_grabs",
        "character_disable_revival",
        "vehicle_set_allow_ram_ped",
        "vehicle_anim_finished",
        "trigger_get_radius",
        "spawning_allow_cop_ambient_spawns",
        "set_script_animation_group",
        "set_force_combat_ready_team",
        "player_grenades_disable",
        "on_weapon_equip",
        "on_door_opened",
        "npc_refire_rate_override",
        "npc_hearing_enable",
        "inv_item_equip_slot",
        "get_char_vehicle_type",
        "drug_effect_clear_override_values",
        "character_is_in_a_driver_seat",
        "change_mission_enemy_team",
        "action_stop_custom",
        "vehicle_skydiving_move_to_do",
        "vehicle_prevent_vehicle_collision_damage",
    };
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB16() {
    static const std::vector<std::string> names = namesB16Impl();
    return names;
}

void registerB16(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("vehicle_repair")) registerOne(L, state, log, stateTag, "vehicle_repair", openGuard<stub_vehicle_repair>);
    if (wants("vehicle_car_alarm_enable")) registerOne(L, state, log, stateTag, "vehicle_car_alarm_enable", openGuard<stub_vehicle_car_alarm_enable>);
    if (wants("tutorial_vehicle_controls_enabled")) registerOne(L, state, log, stateTag, "tutorial_vehicle_controls_enabled", openGuard<stub_tutorial_vehicle_controls_enabled>);
    if (wants("tutorial_active")) registerOne(L, state, log, stateTag, "tutorial_active", openGuard<stub_tutorial_active>);
    if (wants("team_make_allies")) registerOne(L, state, log, stateTag, "team_make_allies", openGuard<stub_team_make_allies>);
    if (wants("skydive_set_catching_shaundi")) registerOne(L, state, log, stateTag, "skydive_set_catching_shaundi", openGuard<stub_skydive_set_catching_shaundi>);
    if (wants("reticle_override_set")) registerOne(L, state, log, stateTag, "reticle_override_set", openGuard<stub_reticle_override_set>);
    if (wants("player_warp_to_shore_enable")) registerOne(L, state, log, stateTag, "player_warp_to_shore_enable", openGuard<stub_player_warp_to_shore_enable>);
    if (wants("persona_suppress_ambient_lines")) registerOne(L, state, log, stateTag, "persona_suppress_ambient_lines", openGuard<stub_persona_suppress_ambient_lines>);
    if (wants("on_pickup")) registerOne(L, state, log, stateTag, "on_pickup", openGuard<stub_on_pickup>);
    if (wants("mission_set_enemy_team")) registerOne(L, state, log, stateTag, "mission_set_enemy_team", openGuard<stub_mission_set_enemy_team>);
    if (wants("mission_is_complete")) registerOne(L, state, log, stateTag, "mission_is_complete", openGuard<stub_mission_is_complete>);
    if (wants("light_group_use_intensity")) registerOne(L, state, log, stateTag, "light_group_use_intensity", openGuard<stub_light_group_use_intensity>);
    if (wants("inv_item_is_equipped")) registerOne(L, state, log, stateTag, "inv_item_is_equipped", openGuard<stub_inv_item_is_equipped>);
    if (wants("inv_item_get_equipped_item")) registerOne(L, state, log, stateTag, "inv_item_get_equipped_item", openGuard<stub_inv_item_get_equipped_item>);
    if (wants("inv_has_item")) registerOne(L, state, log, stateTag, "inv_has_item", openGuard<stub_inv_has_item>);
    if (wants("interior_disable_exterior")) registerOne(L, state, log, stateTag, "interior_disable_exterior", openGuard<stub_interior_disable_exterior>);
    if (wants("human_skydive_create_npc_backpack")) registerOne(L, state, log, stateTag, "human_skydive_create_npc_backpack", openGuard<stub_human_skydive_create_npc_backpack>);
    if (wants("human_enter_downed")) registerOne(L, state, log, stateTag, "human_enter_downed", openGuard<stub_human_enter_downed>);
    if (wants("hud_timer_get_remainder")) registerOne(L, state, log, stateTag, "hud_timer_get_remainder", openGuard<stub_hud_timer_get_remainder>);
    if (wants("get_team")) registerOne(L, state, log, stateTag, "get_team", openGuard<stub_get_team>);
    if (wants("get_object_pos")) registerOne(L, state, log, stateTag, "get_object_pos", openGuard<stub_get_object_pos>);
    if (wants("dlc_achievement_counter_get")) registerOne(L, state, log, stateTag, "dlc_achievement_counter_get", openGuard<stub_dlc_achievement_counter_get>);
    if (wants("character_set_only_scripted_grabs")) registerOne(L, state, log, stateTag, "character_set_only_scripted_grabs", openGuard<stub_character_set_only_scripted_grabs>);
    if (wants("character_set_counter_on_grabbed_by_player")) registerOne(L, state, log, stateTag, "character_set_counter_on_grabbed_by_player", openGuard<stub_character_set_counter_on_grabbed_by_player>);
    if (wants("character_is_jumping")) registerOne(L, state, log, stateTag, "character_is_jumping", openGuard<stub_character_is_jumping>);
    if (wants("character_ignite")) registerOne(L, state, log, stateTag, "character_ignite", openGuard<stub_character_ignite>);
    if (wants("character_has_human_shield")) registerOne(L, state, log, stateTag, "character_has_human_shield", openGuard<stub_character_has_human_shield>);
    if (wants("character_disable_human_downed_interactions")) registerOne(L, state, log, stateTag, "character_disable_human_downed_interactions", openGuard<stub_character_disable_human_downed_interactions>);

    if (wants("on_collision")) registerOne(L, state, log, stateTag, "on_collision", openGuard<stub_on_collision>);
    if (wants("npc_use_closest_action_node_of_type")) registerOne(L, state, log, stateTag, "npc_use_closest_action_node_of_type", openGuard<stub_npc_use_closest_action_node_of_type>);
    if (wants("notoriety_set_override_timer")) registerOne(L, state, log, stateTag, "notoriety_set_override_timer", openGuard<stub_notoriety_set_override_timer>);
    if (wants("inv_item_remove_in_slot")) registerOne(L, state, log, stateTag, "inv_item_remove_in_slot", openGuard<stub_inv_item_remove_in_slot>);
    if (wants("ingame_effect_add_trigger")) registerOne(L, state, log, stateTag, "ingame_effect_add_trigger", openGuard<stub_ingame_effect_add_trigger>);
    if (wants("hud_push_screen")) registerOne(L, state, log, stateTag, "hud_push_screen", openGuard<stub_hud_push_screen>);
    if (wants("continuous_explosion_avoid_object_add")) registerOne(L, state, log, stateTag, "continuous_explosion_avoid_object_add", openGuard<stub_continuous_explosion_avoid_object_add>);
    if (wants("character_set_only_player_grabs")) registerOne(L, state, log, stateTag, "character_set_only_player_grabs", openGuard<stub_character_set_only_player_grabs>);
    if (wants("character_disable_revival")) registerOne(L, state, log, stateTag, "character_disable_revival", openGuard<stub_character_disable_revival>);
    if (wants("vehicle_set_allow_ram_ped")) registerOne(L, state, log, stateTag, "vehicle_set_allow_ram_ped", openGuard<stub_vehicle_set_allow_ram_ped>);
    if (wants("vehicle_anim_finished")) registerOne(L, state, log, stateTag, "vehicle_anim_finished", openGuard<stub_vehicle_anim_finished>);
    if (wants("trigger_get_radius")) registerOne(L, state, log, stateTag, "trigger_get_radius", openGuard<stub_trigger_get_radius>);
    if (wants("spawning_allow_cop_ambient_spawns")) registerOne(L, state, log, stateTag, "spawning_allow_cop_ambient_spawns", openGuard<stub_spawning_allow_cop_ambient_spawns>);
    if (wants("set_script_animation_group")) registerOne(L, state, log, stateTag, "set_script_animation_group", openGuard<stub_set_script_animation_group>);
    if (wants("set_force_combat_ready_team")) registerOne(L, state, log, stateTag, "set_force_combat_ready_team", openGuard<stub_set_force_combat_ready_team>);
    if (wants("player_grenades_disable")) registerOne(L, state, log, stateTag, "player_grenades_disable", openGuard<stub_player_grenades_disable>);
    if (wants("on_weapon_equip")) registerOne(L, state, log, stateTag, "on_weapon_equip", openGuard<stub_on_weapon_equip>);
    if (wants("on_door_opened")) registerOne(L, state, log, stateTag, "on_door_opened", openGuard<stub_on_door_opened>);
    if (wants("npc_refire_rate_override")) registerOne(L, state, log, stateTag, "npc_refire_rate_override", openGuard<stub_npc_refire_rate_override>);
    if (wants("npc_hearing_enable")) registerOne(L, state, log, stateTag, "npc_hearing_enable", openGuard<stub_npc_hearing_enable>);
    if (wants("inv_item_equip_slot")) registerOne(L, state, log, stateTag, "inv_item_equip_slot", openGuard<stub_inv_item_equip_slot>);
    if (wants("get_char_vehicle_type")) registerOne(L, state, log, stateTag, "get_char_vehicle_type", openGuard<stub_get_char_vehicle_type>);
    if (wants("drug_effect_clear_override_values")) registerOne(L, state, log, stateTag, "drug_effect_clear_override_values", openGuard<stub_drug_effect_clear_override_values>);
    if (wants("character_is_in_a_driver_seat")) registerOne(L, state, log, stateTag, "character_is_in_a_driver_seat", openGuard<stub_character_is_in_a_driver_seat>);
    if (wants("change_mission_enemy_team")) registerOne(L, state, log, stateTag, "change_mission_enemy_team", openGuard<stub_change_mission_enemy_team>);
    if (wants("action_stop_custom")) registerOne(L, state, log, stateTag, "action_stop_custom", openGuard<stub_action_stop_custom>);
    if (wants("vehicle_skydiving_move_to_do")) registerOne(L, state, log, stateTag, "vehicle_skydiving_move_to_do", openGuard<stub_vehicle_skydiving_move_to_do>);
    if (wants("vehicle_prevent_vehicle_collision_damage")) registerOne(L, state, log, stateTag, "vehicle_prevent_vehicle_collision_damage", openGuard<stub_vehicle_prevent_vehicle_collision_damage>);
}

} // namespace stub_sections
} // namespace sr3luahost
