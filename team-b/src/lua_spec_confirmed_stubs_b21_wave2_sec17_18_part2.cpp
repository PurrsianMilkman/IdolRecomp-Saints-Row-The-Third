// lua_spec_confirmed_stubs_b21_wave2_sec17_18_part2.cpp
//
// Second half of lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp's stub functions, moved
// here unchanged to keep each file under ~1,500 lines (Team C refactor, 2026-10-17). The
// names/registration pair stays in that file; see its header comment for scope, sources
// and policy.

#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include "sr3luahost/save_game_bridge.h"
#include <cmath>

namespace sr3luahost {
namespace stubs_b21 {

// mission_autosave (Sec17.23, 0x00a52b10, implemented from the Sec60.3
// CORRECTION, not Sec17.23 alone). No arguments. Return none. CORRECTED
// per Sec60.3: a silent-return-on-Steam-check-failure path gates the WHOLE
// function (nothing happens at all on failure - no record, no save
// attempt); the busy predicate's own polarity is inverted from Sec17.23's
// original text (the real save kickoff proceeds only when busy is FALSE);
// the native explicitly skips game_autosave's own extra mission gates (so
// it can save mid-mission - this project has no such gates to skip in the
// first place). The record-open and the no-op stub call are otherwise
// unconditional once past the Steam check.
//
// WIRED 2026-10-08 (spec-save-format.md Sec15.2): a kickoff is this
// native's own real save-entry call (`mission_autosave` 0x00a52b10 ->
// 0x00b94ff0, CONFIRMED by Sec15.2's exhaustive call-site scan) - same
// real file-write branch game_autosave's own kickoff reaches, so a
// successful kickoff now also performs a real write of the live
// EngineState::saveGame()/playerCash() state (save_game_bridge.h's
// writeAutosaveSnapshot(), never throws, failure recorded on
// EngineState::lastAutosaveError() rather than as a Lua error).
int stub_mission_autosave(lua_State* L) {
    logCall(L, upLog(L), "mission_autosave", upStateTag(L));
    EngineState* st = upState(L);
    if (!st->missionAutosaveSteamCheckOk()) return 0; // Sec60.3 correction: silent return, nothing else happens
    EngineState::replicateStateChange("mission_autosave", ""); // CONFIRMED unconditional, opcode 0x43 subtype 0x1c
    ++st->missionAutosaveStubCallCount(); // the always-1, result-unused stub 0x00d21500
    if (!st->missionAutosaveBusy()) { // Sec60.3: proceeds only when busy is FALSE
        if (st->missionAutosaveChainReady()) {
            ++st->missionAutosaveKickoffCount();
            std::string path, err;
            writeAutosaveSnapshot(*st, path, err); // Sec15.2/Sec6.3: real file write
        } else {
            st->missionAutosaveDeferredPending() = true;
        }
    }
    return 0;
}

// light_group_set_intensity (Sec17.24, 0x00a51f50). 1 mandatory string
// (light-group name) + 1 mandatory number (intensity, raw float) + 1
// optional boolean - CONFIRMED read but never used (a genuinely dead/
// vestigial optional argument). Return none. Resolves via a dedicated
// per-kind resolver, collapsed to EngineState::lightGroupResolves() (no
// fixed id table in this project). UNCONDITIONALLY sets bit 0x2 of the
// flags byte at +0x40 and writes the intensity to +0x50, then re-applies
// to every owned member light; a SEPARATE conditional record fires only
// inside a coordinated co-op session.
int stub_light_group_set_intensity(lua_State* L) {
    logCall(L, upLog(L), "light_group_set_intensity", upStateTag(L));
    std::string name = argString(L, 1);
    double intensity = lua_tonumber(L, 2); // CONFIRMED: mandatory raw float
    (void)optionalBoolDefault(L, 3, false); // CONFIRMED dead argument - read, never used
    EngineState* st = upState(L);
    if (!st->lightGroupResolve(name)) return 0; // Sec4.13.0: unresolved group is a no-op
    auto& group = st->lightGroup(name);
    group.flagBit2Set = true;
    group.intensity = intensity;
    ++group.applyCount;
    if (st->coopSession().present.get()) { // CONFIRMED false by default (single player)
        EngineState::replicateStateChange("light_group_set_intensity", name);
        ++group.replicateCount;
    }
    return 0;
}

// inv_item_remove (Sec17.25, 0x00a50b70). 1 mandatory string (item/weapon
// name) + 1 optional string, default "#PLAYER1#" (the SECOND, distinct
// player sentinel - not "#PLAYER#"). Return none. Resolves via
// objectResolves() (0x00a281a0 stand-in). Gate is single
// (forceFlagGatePasses, variant 3, single-gate-0x008ae480-only - the SAME
// shared predicate npc_leash_remove/npc_leash_to_nav already reuse,
// CONFIRMED true in single player). The item definition lookup
// (0x008dcb10/0x005feaa0) collapses to EngineState::
// inventoryItemDefinitionResolves() (no fixed id table in this project).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_inv_item_remove(lua_State* L) {
    logCall(L, upLog(L), "inv_item_remove", upStateTag(L));
    std::string itemName = argString(L, 1);
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    std::string resolvedTarget;
    if (!resolveFollowerAwareObject(st, target, resolvedTarget)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    target = resolvedTarget;
    CharacterState& c = st->getOrCreateCharacter(target);
    if (!c.forceFlagGatePasses.get()) { // CONFIRMED true in single player (variant 3, Sec7's own front matter)
        EngineState::replicateStateChange("inv_item_remove", target);
        ++c.inventoryItemRemoveReplicateCount;
        return 0;
    }
    if (st->inventoryItemDefinitionResolve(itemName)) { // Sec17.25: "none" (not a row) resolves null; other unregistered names OPEN
        ++c.inventoryItemRemoveAppliedCount;
    }
    return 0;
}

// hud_display_set_element (Sec17.26, 0x00a4ed30). 3 mandatory numbers
// (slot id; sub-field index, 0xf meaning "all"; value). Return none. "No
// gate at all, always both" idiom: UNCONDITIONALLY opens a network record,
// AND UNCONDITIONALLY calls the direct-apply dispatcher - which itself
// only writes when slotId < 20 and that slot's own "active" flag is set.
// Extends HudDisplayStatePool (wave-1 Batch 5) with a SEPARATE 20-slot
// active/value table. CHOSEN (hardened divergence, Sec17.26): a negative
// slotId (never explicitly excluded by the spec's own "< 0x14" test) and a
// subIndex outside {0..11, 0xf} (never explicitly bounds-checked beyond
// the 0xf special case) are both refused rather than reproduced as a
// guessed OOB array access - each counted separately.
int stub_hud_display_set_element(lua_State* L) {
    logCall(L, upLog(L), "hud_display_set_element", upStateTag(L));
    int64_t slotIdRaw = truncateEa2596(lua_tonumber(L, 1));
    int64_t subIndexRaw = truncateEa2596(lua_tonumber(L, 2));
    double value = lua_tonumber(L, 3);
    EngineState* st = upState(L);
    auto& pool = st->hudDisplayStatePool();
    EngineState::replicateStateChange("hud_display_set_element", ""); // CONFIRMED unconditional
    ++pool.setElementCount;

    if (slotIdRaw < 0 || slotIdRaw >= HudDisplayStatePool::kElementSlotCount) {
        ++pool.setElementHardenedSlotIdCount; // CHOSEN (hardened divergence, Sec17.26)
        return 0;
    }
    int slotId = static_cast<int>(slotIdRaw);
    if (!pool.elementActive[static_cast<size_t>(slotId)]) return 0; // CONFIRMED: inactive slot, no-op

    if (subIndexRaw == HudDisplayStatePool::kElementSetAllSentinel) {
        for (int i = 0; i < HudDisplayStatePool::kElementSubFieldCount; ++i)
            pool.elementValues[static_cast<size_t>(slotId)][static_cast<size_t>(i)] = value;
        ++pool.setElementAppliedCount;
        return 0;
    }
    if (subIndexRaw < 0 || subIndexRaw >= HudDisplayStatePool::kElementSubFieldCount) {
        ++pool.setElementHardenedSubIndexCount; // CHOSEN (hardened divergence, Sec17.26)
        return 0;
    }
    pool.elementValues[static_cast<size_t>(slotId)][static_cast<size_t>(subIndexRaw)] = value;
    ++pool.setElementAppliedCount;
    return 0;
}

// helicopter_enter_retreat (Sec17.27, 0x00a4cb10). 1 mandatory string
// (vehicle name). Return 1 boolean. Resolves via objectResolves() (0x00a281e0
// stand-in). Reuses EngineState::HelicopterFlyTo::qualifies directly (the
// SAME 0x00ad31a0 "+0x2c in {3,4}" test, CONFIRMED the identical address
// Sec17.4's own loop reads) rather than re-deriving a new gate. Since this
// call site always passes the delegate a literal `true`, the "boolean OR
// fallback-field-clear" short-circuit is structurally always satisfied -
// not separately modeled (it never changes the observable branch taken).
int stub_helicopter_enter_retreat(lua_State* L) {
    logCall(L, upLog(L), "helicopter_enter_retreat", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!st->helicopterFlyTo().qualifies.get(name)) { // OPEN until set - reused directly, Sec28.2
        lua_pushboolean(L, 0);
        return 1;
    }
    VehicleState& veh = st->getOrCreateVehicle(name);
    auto& h = st->helicopter(name);
    if (veh.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        if (h.subObjectModeEnum.get() != 6) { // OPEN until set
            h.field158Cleared = true;
            ++h.retreatTransitionCount; // 0x00b350a0(vehicle, 5, 0)
            if (h.heightCheckTrips.get()) { // OPEN until set
                h.heightTripFlag268 = true;
                h.retreatDeadlineStamp = st->nextMonotonicStamp() + 1500;
            }
        }
    } else {
        EngineState::replicateStateChange("helicopter_enter_retreat", name);
        ++h.retreatReplicateCount;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// door_is_open (Sec17.28, 0x00a48760). 1 mandatory string. Return 1
// boolean. Resolves via objectResolves() (0x006434a0 stand-in). Reuses the
// EXISTING hasInteractionAnchor field (door +0xc8, Sec1.10) directly for
// the outer null-test; the inner state byte (+0x7a bit 0x40) is a new,
// independently OPEN fact. Null pointer -> pushes false.
int stub_door_is_open(lua_State* L) {
    logCall(L, upLog(L), "door_is_open", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    PlacedObjectState& p = st->getOrCreatePlacedObject(name);
    bool isOpen = p.hasInteractionAnchor.get() && p.doorOpenBit40At7a.get(); // OPEN until set
    lua_pushboolean(L, isOpen ? 1 : 0);
    return 1;
}

// dlc_gis_film_grain_unload (Sec17.29, 0x00a479d0). 1 optional boolean,
// default true + 1 optional number (2-bit action mask), default 3. Return
// none. Bit 0 (default set): local DLC film-grain fade toggle, gated on
// the DLC-availability sentinel (CHOSEN false - "not loaded," this project
// models no real DLC content elsewhere either). Bit 1 (default set): opens
// a network record (opcode 0x43, subtype 0x2e). With the default mask,
// BOTH actions always happen.
int stub_dlc_gis_film_grain_unload(lua_State* L) {
    logCall(L, upLog(L), "dlc_gis_film_grain_unload", upStateTag(L));
    bool v = optionalBoolDefault(L, 1, true);
    int mask = static_cast<int>(optionalNumberDefault(L, 2, 3.0));
    EngineState* st = upState(L);
    if (mask & 0x1) {
        if (st->dlcFilmGrainAvailable().get()) { // OPEN until set
            st->filmGrainFadeState() = v;
        } else {
            st->filmGrainFadeState() = false; // CONFIRMED: "DLC not loaded" short-circuits to a reset
        }
        ++st->filmGrainLocalApplyCount();
    }
    if (mask & 0x2) {
        EngineState::replicateStateChange("dlc_gis_film_grain_unload", "");
        ++st->filmGrainReplicateCount();
    }
    return 0;
}

// difficulty_get_level (Sec17.30, 0x00a47300). No arguments. Return 1
// number. Pure global-state query, no resolver/gate/side-effect of any
// kind. CONFIRMED (per lua_spec_confirmed_stubs_b17_wave2_ui_frontend_a.cpp's
// own doc comment) to be the SAME real function get_current_difficulty
// (Sec41.2) under its own alias name - reuses the SAME field directly
// (file-default 1, CONFIRMED there), not a second, weaker-default copy.
int stub_difficulty_get_level(lua_State* L) {
    logCall(L, upLog(L), "difficulty_get_level", upStateTag(L));
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->difficultyLevel()));
    return 1;
}

// =======================================================================
// Section 18 batch (spec-lua-api-behaviour.md Sec18, "ranks 251-350
// tranche, part C", 30 names, wave 2 batch W2-2, 2026-10-08).
// =======================================================================

// cash_add (Sec18.1, 0x00a42ef0). 1 mandatory number (cash amount,
// unconditional) + 1 optional string, default "#PLAYER1#" (the SAME second
// player sentinel inv_item_remove above uses). Return none (0 values).
// Reuses EngineState::playerCash() directly (this project's single
// economy value) - scaled x100 and saturated to +/-2,000,000,000.
int stub_cash_add(lua_State* L) {
    logCall(L, upLog(L), "cash_add", upStateTag(L));
    double amount = lua_tonumber(L, 1); // CONFIRMED: unconditional read
    std::string target = optionalStringDefault(L, 2, "#PLAYER1#");
    EngineState* st = upState(L);
    if (!st->objectResolves().get(target)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(target);
    double scaled = amount * 100.0;
    scaled = std::max<double>(-2000000000.0, std::min<double>(2000000000.0, scaled)); // CONFIRMED saturating clamp
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        st->playerCash() += scaled;
    } else {
        EngineState::replicateStateChange("cash_add", target);
    }
    return 0;
}

// boss_battle_matt_set_matt_flying (Sec18.2, 0x00a408c0). 1 optional
// boolean, default true. Return none. Unconditionally writes to a single
// global - no resolver, no gate, no record.
int stub_boss_battle_matt_set_matt_flying(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_set_matt_flying", upStateTag(L));
    bool v = optionalBoolDefault(L, 1, true);
    upState(L)->bossBattleMattFlying() = v;
    return 0;
}

// ai_do_scripted_take_cover (Sec18.3, 0x00a3e9d0). 1 mandatory string
// (character) + 1 optional string, default absent (anchor). Return 1
// boolean. Resolves arg 1 via the #FOLLOWER#-then-character chain
// 0x00a28150, resolveFollowerAwareObject() - wired 2026-10-08 (follower-
// sentinel-resolver pass). If arg 2 absent: evaluates the "cover take def"
// ability gate 0x00540b90 (spec 18.3 blocker m02/m10: true unless the NPC is in
// a cover stay-down window); if true, dispatches the SAME scripted-action-set
// call and returns true, else returns false with no action. If arg 2 given:
// resolves it, and on success writes the resolved handle into the SAME
// pendingScriptedMoveDestination field ai_do_scripted_move (Sec3.11)
// already models (CONFIRMED the identical physical +0x4b0/+0x4b4 field),
// dispatches, returns true; otherwise false.
int stub_ai_do_scripted_take_cover(lua_State* L) {
    logCall(L, upLog(L), "ai_do_scripted_take_cover", upStateTag(L));
    std::string name = argString(L, 1);
    std::string anchor = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (anchor.empty()) {
        // Spec 18.3 blocker m02/m10 (FINAL): gate 0x00540b90 is true for any NPC not inside a cover stay-down
        // window; true -> commit scripted action 8 and push true, false -> no action and push false.
        const bool gate = !c.coverStayDownActive;
        if (gate) {
            ++c.scriptedTakeCoverDispatchCount; // 0x004f4d00(char, 8, 0)
        }
        lua_pushboolean(L, gate ? 1 : 0);
        return 1;
    }
    if (st->objectResolves().get(anchor)) { // OPEN until set
        c.pendingScriptedMoveDestination = anchor; // CONFIRMED the SAME +0x4b0/+0x4b4 field, Sec3.11
        ++c.scriptedTakeCoverDispatchCount; // 0x004f4d00(char, 8, 0)
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// action_stop_synced_state (Sec18.4, 0x00a3d5a0). 1 mandatory string
// (character) + 1 optional boolean, default false. Return none. Resolves
// via objectResolves() (0x00a281a0 stand-in). The global replication-mode
// flag (0x024d4461) is CHOSEN false by default (single-player-only project
// stance), so this always takes the documented "normal local path" branch,
// clearing the SAME pairing action_play_synced_state (Sec14.1) writes.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_action_stop_synced_state(lua_State* L) {
    logCall(L, upLog(L), "action_stop_synced_state", upStateTag(L));
    std::string name = argString(L, 1);
    bool arg2 = optionalBoolDefault(L, 2, false);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    name = resolvedName;
    CharacterState& c = st->getOrCreateCharacter(name);
    c.lastSyncedStopArg = arg2;
    if (st->globalReplicationModeFlag() && !c.forceFlagGatePasses.get()) { // CHOSEN false by default
        EngineState::replicateStateChange("action_stop_synced_state", name);
        return 0;
    }
    // CONFIRMED normal local path: clears the synced pairing this native shares with action_play_synced_state.
    c.syncedActionPartnerName.clear();
    c.syncedActionPaired = false;
    ++c.syncedStopLocalApplyCount;
    st->endSyncedActionInstance(name); // the controller's synced instance handle goes back to -1 (spec Sec18.5 port rule)
    return 0;
}

// action_play_synced_is_finished (Sec18.5, 0x00a3c0c0). 2 mandatory
// strings (a synced-action pair) + 1 optional number, default a fixed
// constant. Return 1 boolean. Resolves both via objectResolves() (0x00a281a0
// stand-in); either unresolved -> false immediately. For each resolved
// character independently: no active controller state (syncedAnimControllerActive
// false) -> "finished"; else compares elapsed time against the threshold.
// Overall result is the logical AND of both.
// Resolver 0x00a281a0 (both actors) - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here,
// follower-sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_action_play_synced_is_finished(lua_State* L) {
    logCall(L, upLog(L), "action_play_synced_is_finished", upStateTag(L));
    std::string actor1 = argString(L, 1);
    std::string actor2 = argString(L, 2);
    double threshold = optionalNumberDefault(L, 3, 1.0); // CHOSEN placeholder - real constant (0x01117a4c) value unknown here
    EngineState* st = upState(L);
    std::string resolvedActor1, resolvedActor2;
    if (!resolveFollowerAwareObject(st, actor1, resolvedActor1) ||
        !resolveFollowerAwareObject(st, actor2, resolvedActor2)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    actor1 = resolvedActor1;
    actor2 = resolvedActor2;
    // Answered (spec sync bfe0542, Sec18.5 blocker m07, FINAL): finished(char) = no active synced instance
    // (handle -1) OR playback fraction >= threshold; the result is the AND over both characters. See
    // EngineState::syncedActionFinishedFor (the instance is created by action_play_synced_do).
    bool result = st->syncedActionFinishedFor(actor1, threshold) && st->syncedActionFinishedFor(actor2, threshold);
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// spawn_override_change_hood_alternate_spawning (Sec18.6, 0x00a5fab0). 1
// mandatory string (hood/zone name) + 1 optional boolean, default true.
// Return none. Resolves via the shared objectResolves() map directly (no
// separate predicate needed - Sec29's standing convention). UNCONDITIONALLY
// toggles the per-hood bit; a SEPARATE host-only record fires via the
// EXISTING coopLocalIsHost() singleton.
int stub_spawn_override_change_hood_alternate_spawning(lua_State* L) {
    logCall(L, upLog(L), "spawn_override_change_hood_alternate_spawning", upStateTag(L));
    std::string hood = argString(L, 1);
    bool v = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(hood)) return 0; // OPEN until set
    st->hoodAlternateSpawning()[hood] = v; // CONFIRMED unconditional local toggle
    if (st->coopLocalIsHost()) { // reuses the EXISTING 0x0087ba20 host-check singleton, Sec8.27/Sec6.22/Sec10.9
        EngineState::replicateStateChange("spawn_override_change_hood_alternate_spawning", hood);
    }
    return 0;
}

// shop_ignore_clerk (Sec18.7, 0x00a5dab0). 1 mandatory boolean, no nil-
// gate. Return none. CONFIRMED a dead no-op stub in the shipped binary
// (0x00754410, already documented elsewhere in this project as a stripped
// debug/profiling stub) - never touches any real state.
int stub_shop_ignore_clerk(lua_State* L) {
    logCall(L, upLog(L), "shop_ignore_clerk", upStateTag(L));
    (void)lua_toboolean(L, 1);
    return 0;
}

// set_animation_set (Sec18.8, 0x00a5e150). 2 mandatory strings (character;
// animation-set name). Return none. Resolves via objectResolves() (0x00a281a0
// stand-in). The set-name->id lookup (0x004b16c0) collapses to tracking the
// name directly (no fixed id table). If the controller already has an
// active instance (CHOSEN false default - no stated construction value),
// the in-progress blend is immediately re-applied.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_set_animation_set(lua_State* L) {
    logCall(L, upLog(L), "set_animation_set", upStateTag(L));
    std::string name = argString(L, 1);
    std::string setName = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    c.animationSetName = setName;
    if (c.hasActiveAnimationInstance) {
        ++c.animationSetReapplyCount;
    }
    return 0;
}

// player_release_human_shield (Sec18.9, 0x00a598d0). 1 mandatory string
// (character) + 2 optional booleans, default false/false. Return 1
// boolean (the actual release-attempt result). Resolves via objectResolves()
// (0x00a280c0 stand-in); unresolved -> pushes false. A genuinely rich state
// machine, modeled at the observable-branch level only (no real position/
// velocity/timer data in this project). isLocalAuthority CHOSEN true
// (single-player-only project stance).
int stub_player_release_human_shield(lua_State* L) {
    logCall(L, upLog(L), "player_release_human_shield", upStateTag(L));
    std::string name = argString(L, 1);
    bool boolA = optionalBoolDefault(L, 2, false);
    bool boolB = optionalBoolDefault(L, 3, false);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(name);
    if (!c.humanShieldReleaseIsLocalAuthority) {
        EngineState::replicateStateChange("player_release_human_shield", name);
        ++c.humanShieldReplicateCount;
        lua_pushboolean(L, 0);
        return 1;
    }
    bool released = false;
    if (boolA) {
        ++c.humanShieldForcibleEjectCount;
        released = true;
    } else if (boolB) {
        ++c.humanShieldDelayedReleaseCount; // 0x009ad200, own body OPEN (resolved elsewhere, Sec22.26)
        released = true;
    } else if (c.humanShieldNormalReleaseGatesPass.get() && // OPEN until set
               !c.humanShieldHostageName.empty() && c.humanShieldStateByte18d4 == 1) {
        c.humanShieldStateByte18d4 = 4; // CONFIRMED: both parties' state byte -> 4 ("cooldown/just-released")
        ++c.humanShieldNormalReleaseCount;
        released = true;
    }
    lua_pushboolean(L, released ? 1 : 0);
    return 1;
}

// player_is_rc_driver (Sec18.10, 0x00a5a9e0). 1 mandatory string
// (character) + 1 optional string, default absent (vehicle). Return 1
// boolean. Resolves arg 1 via objectResolves() (0x00a280c0 stand-in);
// unresolved -> false. Collapses the full resolve/driver-seat-position-
// compare chain into one OPEN predicate per character name (a pure query).
int stub_player_is_rc_driver(lua_State* L) {
    logCall(L, upLog(L), "player_is_rc_driver", upStateTag(L));
    std::string name = argString(L, 1);
    std::string vehicle = optionalStringDefault(L, 2, "");
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    // Answered (spec sync bfe0542, Sec18.10 blocker m20, FINAL; corrects the Body): "is this character the RC
    // controller of that vehicle" - the vehicle's +0x1df0/+0x1df4 pair compared with the character's own id. Null
    // by default; only the RC start routine writes it, so ordinary driving is NOT an RC driver. With arg 2 the
    // vehicle must resolve; without it the character's active-vehicle pair is used. Unresolved -> false.
    if (!vehicle.empty() && !st->objectResolves().get(vehicle)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool isRc = st->playerIsRcDriver(name, vehicle);
    if (!isRc) st->noteRcDriverPollQuery(name, vehicle); // --auto-objective harness bookkeeping only
    lua_pushboolean(L, isRc ? 1 : 0);
    return 1;
}

// on_incapacitated (Sec18.11, 0x00a58070). 1 mandatory string (callback
// name, opaque) + 1 mandatory string (character name). Return none.
// Resolves arg 2 via objectResolves() (0x005e4dd0 stand-in). Registers into
// the character-kind hook base +0x68/+0x6c, slot 1 - same single-
// dedicated-field-per-hook-kind convention as onDeathCallback above.
int stub_on_incapacitated(lua_State* L) {
    logCall(L, upLog(L), "on_incapacitated", upStateTag(L));
    std::string callbackName = argString(L, 1);
    std::string name = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateCharacter(name).onIncapacitatedCallback = callbackName;
    return 0;
}

// notoriety_force_no_air_spawn (Sec18.12, 0x00a54d40). 1 mandatory
// boolean, no nil-gate. Return none. Writes unconditionally into SIX
// separate global objects' own +0x455 byte via the SAME one-line setter -
// collapsed to one flag since nothing distinguishes the six from each
// other.
int stub_notoriety_force_no_air_spawn(lua_State* L) {
    logCall(L, upLog(L), "notoriety_force_no_air_spawn", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->notorietyForceNoAirSpawn() = v;
    return 0;
}

// helicopters_set_jitter_override (Sec18.13, 0x00a4ce90). 2 optional
// numbers, both defaulting to the SAME fixed float constant. Return none.
// Both values written directly to two adjacent globals via one-line
// setters - no resolver, no gate, no record.
int stub_helicopters_set_jitter_override(lua_State* L) {
    logCall(L, upLog(L), "helicopters_set_jitter_override", upStateTag(L));
    EngineState* st = upState(L);
    st->helicopterJitterMin() = optionalNumberDefault(L, 1, 0.0); // CHOSEN placeholder - real constant (0x01117a4c) value unknown here
    st->helicopterJitterMax() = optionalNumberDefault(L, 2, 0.0);
    return 0;
}

// group_members_alive (Sec18.14, 0x00a4c6d0). 1 mandatory string (group
// name). Return 1 number (live-member count). Resolves via a dedicated
// group-name resolver, collapsed to EngineState::groupResolves() (no fixed
// id table in this project). Walks a test-registerable member list,
// counting non-dead members via the SAME already-established
// CharacterState::isDeadHighConfidence predicate, reused directly.
int stub_group_members_alive(lua_State* L) {
    logCall(L, upLog(L), "group_members_alive", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->groupResolves().get(name)) { // OPEN until set
        lua_pushnumber(L, 0.0);
        return 1;
    }
    int32_t count = 0;
    if (st->groupHasMembersBit().get(name)) { // OPEN until set
        count = st->groupMembersAliveCount(name);
    }
    lua_pushnumber(L, static_cast<lua_Number>(count));
    return 1;
}

// flashpoints_enable (Sec18.15, 0x00a49800). 1 optional boolean, default
// true. Return none. Writes directly to a single global - no resolver, no
// gate, no record.
int stub_flashpoints_enable(lua_State* L) {
    logCall(L, upLog(L), "flashpoints_enable", upStateTag(L));
    bool v = optionalBoolDefault(L, 1, true);
    upState(L)->flashpointsEnabled() = v;
    return 0;
}

// character_set_immune_to_explosion_vomit (Sec18.16, 0x00a42370). 1
// mandatory string + 1 mandatory boolean, no nil-gate. Return none.
// Double-gate (variant 1), identity +8/+0xc: a further bit (0x1000) on the
// EXISTING forceFlags1c9c dword.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_set_immune_to_explosion_vomit(lua_State* L) {
    logCall(L, upLog(L), "character_set_immune_to_explosion_vomit", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c9c.setBits(CharacterState::kImmuneToExplosionVomitBit1c9c,
                                  v ? CharacterState::kImmuneToExplosionVomitBit1c9c : 0u);
    } else {
        EngineState::replicateStateChange("character_set_immune_to_explosion_vomit", resolvedName);
    }
    return 0;
}

// character_set_always_combat_ready (Sec18.17, 0x00a420e0). Same shape as
// Sec18.16. Double-gate (variant 1), identity +8/+0xc: bit 0x1 of the
// +0x1c98 flags field.
int stub_character_set_always_combat_ready(lua_State* L) {
    logCall(L, upLog(L), "character_set_always_combat_ready", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kAlwaysCombatReadyBit1c98,
                                  v ? CharacterState::kAlwaysCombatReadyBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("character_set_always_combat_ready", name);
    }
    return 0;
}

// character_make_priority_target (Sec18.18, 0x00a41bd0). 1 mandatory
// string + 1 mandatory boolean, no nil-gate + 1 optional number, default
// 0.0 (radius). Return none. Local-authority branch: sets bit 0x400 of
// +0x1c98 to the boolean AND unconditionally overwrites +0x1ca4 with the
// radius SQUARED (CONFIRMED, regardless of the boolean's value) - a genuine
// asymmetry against the network branch, which carries the radius
// un-squared (Sec18.18's own flagged, unresolved receiving-side question,
// not modeled further).
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_character_make_priority_target(lua_State* L) {
    logCall(L, upLog(L), "character_make_priority_target", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    double radius = optionalNumberDefault(L, 3, 0.0);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kPriorityTargetBit1c98,
                                  v ? CharacterState::kPriorityTargetBit1c98 : 0u);
        c.priorityTargetRadiusSquared = radius * radius; // CONFIRMED: always overwritten, squared
    } else {
        EngineState::replicateStateChange("character_make_priority_target", resolvedName);
    }
    return 0;
}

// character_is_player (Sec18.19, 0x00a41880). 1 mandatory string. Return 1
// boolean. Resolved through the GENERIC resolver only - this project's flat
// objectResolves() already collapses every resolver variant into one
// shared map (Sec29), so no new state is needed; "does this name resolve
// at all" IS the real test.
int stub_character_is_player(lua_State* L) {
    logCall(L, upLog(L), "character_is_player", upStateTag(L));
    std::string name = argString(L, 1);
    bool resolves = upState(L)->objectResolves().get(name); // OPEN until set
    lua_pushboolean(L, resolves ? 1 : 0);
    return 1;
}

// vehicle_set_team (Sec18.20, 0x00a632b0). 2 mandatory strings (vehicle;
// team name). Return none. Resolves via objectResolves() (0x00a281e0
// stand-in). The team-name hash/lookup (0x0094cc60 -> 0x005961a0)
// collapses to EngineState::lookupVehicleTeamId() - a test-registerable
// map with the one CONFIRMED default fallback (9, "no match"), not an
// invented real-world name mapping. A bare, unconditional local store - no
// gate, no record at all (a real asymmetry against most of this batch's
// setters).
int stub_vehicle_set_team(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_team", upStateTag(L));
    std::string name = argString(L, 1);
    std::string teamName = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    st->getOrCreateVehicle(name).teamId = st->lookupVehicleTeamId(teamName);
    return 0;
}

// vehicle_set_ignore_rail_obstacles (Sec18.21, 0x00a661f0). 1 mandatory
// string + 1 mandatory boolean, no nil-gate. Return none. Resolves via
// objectResolves() (0x0062a190 stand-in). Raw-disassembly-confirmed
// computed-sub-record hidden-`this` (the SAME +0xc68 pattern
// vehicle_set_script_hard_goto already confirms) - a further bit (0x4) on
// the EXISTING vehicleAiForceFlagsByte4 byte.
int stub_vehicle_set_ignore_rail_obstacles(lua_State* L) {
    logCall(L, upLog(L), "vehicle_set_ignore_rail_obstacles", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    VehicleState& veh = st->getOrCreateVehicle(name);
    if (veh.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7/Sec30.5
        veh.vehicleAiForceFlagsByte4.setBits(VehicleState::kIgnoreRailObstaclesBit,
                                              v ? VehicleState::kIgnoreRailObstaclesBit : 0u);
    } else {
        EngineState::replicateStateChange("vehicle_set_ignore_rail_obstacles", name);
    }
    return 0;
}

// set_suppress_synced_attacks_flag (Sec18.22, 0x00a5d710). 1 mandatory
// string + 1 optional boolean, default true. Return none. Double-gate
// (variant 1), identity +8/+0xc: a further bit (0x20000000) on the
// EXISTING +0x1c98 flags field.
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_set_suppress_synced_attacks_flag(lua_State* L) {
    logCall(L, upLog(L), "set_suppress_synced_attacks_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool v = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(resolvedName);
    if (c.forceFlagGatePasses.get()) { // CONFIRMED true in single player, Sec3.7
        c.forceFlags1c98.setBits(CharacterState::kSuppressSyncedAttacksBit1c98,
                                  v ? CharacterState::kSuppressSyncedAttacksBit1c98 : 0u);
    } else {
        EngineState::replicateStateChange("set_suppress_synced_attacks_flag", resolvedName);
    }
    return 0;
}

// reticle_override_clear (Sec18.23, 0x00a5b880). 1 mandatory number
// (flags byte, no nil-gate - not optional). Return none. Bit 0x1 clears
// two reticle-override globals locally; bit 0x2 opens a local-only network
// record.
int stub_reticle_override_clear(lua_State* L) {
    logCall(L, upLog(L), "reticle_override_clear", upStateTag(L));
    int64_t flags = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    if (flags & 0x1) {
        st->reticleOverrideGlobalsCleared() = true;
        ++st->reticleOverrideClearLocalCount();
    }
    if (flags & 0x2) {
        EngineState::replicateStateChange("reticle_override_clear", "");
        ++st->reticleOverrideClearReplicateCount();
    }
    return 0;
}

// radio_global_block (Sec18.24, 0x00a5b540). 1 mandatory boolean, no nil-
// gate. Return none. Writes directly to a single global - distinct from
// radio_block's own per-vehicle scoping (Sec12.20).
int stub_radio_global_block(lua_State* L) {
    logCall(L, upLog(L), "radio_global_block", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->radioGlobalBlocked() = v;
    return 0;
}

// player_grenades_enable (Sec18.25, 0x00a59700). No arguments consumed
// (lua_gettop called but unused - the same "inert argument" shape several
// other natives in this project already document). Return none.
// Unconditionally clears two globals to 0 every call, regardless of any
// script-side argument - genuinely zero-argument-sensitive.
int stub_player_grenades_enable(lua_State* L) {
    logCall(L, upLog(L), "player_grenades_enable", upStateTag(L));
    (void)lua_gettop(L);
    EngineState* st = upState(L);
    st->playerGrenadeGlobalA() = 0.0;
    st->playerGrenadeGlobalB() = 0.0;
    ++st->playerGrenadesEnableCallCount();
    return 0;
}

// player_can_sprint (Sec18.26, 0x00a59600). 1 mandatory boolean, no nil-
// gate. Return none. Always writes the global sprint-capability byte; if
// disabling, additionally halts any currently-active sprint state. No per-
// character "current player" handle in this project (same project-wide
// "singular local player" simplification several other natives apply).
int stub_player_can_sprint(lua_State* L) {
    logCall(L, upLog(L), "player_can_sprint", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    EngineState* st = upState(L);
    st->sprintEnabled() = v;
    if (!v) {
        st->sprintBit2Cleared() = true;
        if (st->sprintModeIsFive()) {
            ++st->sprintDisableCount(); // the extra call under mode/difficulty code 5 (HYPOTHESIS meaning)
        } else {
            ++st->sprintDisableCount();
        }
    }
    return 0;
}

// player_actions_disable (Sec18.27, 0x00a595a0). 1 mandatory string
// (character). Return none. Resolves via objectResolves() (0x00a280c0
// stand-in). Single gate (controlLockGatePasses, the SAME 0x008addb0
// "variant 2" family, CONFIRMED true in single player) - reuses the
// EXISTING, previously write-only actionsEnableBit20_1d99 field (Sec15.21/
// Sec27.x) directly for the local-apply branch.
//
// BIT-SENSE POLARITY (resolved 2026-10-08, prior review flag closed):
// despite the field's own "Enable" name, bit 0x20 at +0x1d99 is a
// *disabled*-sense bit, the SAME family as the sibling bits at this byte
// (CharacterState::controlsDisabled/movementDisabled, 0x08/0x10).
// spec-player-runtime.md Sec2.10 "Question B" names all three bits
// explicitly: "the player's `+0x1d99` bits (`0x08`/`0x10`/`0x20` -
// controls/movement/actions disabled) ... HIGH CONFIDENCE ... all clear
// (0) - enabled" and "treat all three bits ... as clear at spawn -
// controls, movement and actions all enabled" - i.e. clear(0)=enabled,
// set(1)=disabled for bit 0x20 too, exactly like 0x08/0x10. Sec18.27's own
// body independently matches this: the hard-coded literal `1` passed to
// `0x009e30d0(handle, 1)` (never script-controlled - this native has no
// boolean argument, unconditionally "disable") mirrors the sibling direct-
// write setters 0x009e2dd0/0x009e2fa0 (Sec7.22/Sec7.25: "writes the bit
// directly" from their own disable-boolean argument, where a `true`
// "disable" argument becomes bit=1). So "disable" must SET the bit, not
// clear it - the previous `.set(false)` had the polarity backwards.
int stub_player_actions_disable(lua_State* L) {
    logCall(L, upLog(L), "player_actions_disable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.controlLockGatePasses.get()) { // CONFIRMED true in single player, Sec7.7/Sec6.22
        c.actionsEnableBit20_1d99.set(true); // "disable" SETS the disabled-sense bit (spec-player-runtime.md Sec2.10)
    } else {
        EngineState::replicateStateChange("player_actions_disable", name);
        ++c.playerActionsDisableReplicateCount;
    }
    return 0;
}

// party_set_recruitable (Sec18.28, 0x00a59460). 1 mandatory boolean, no
// nil-gate. Return none. Writes directly to a single global - no resolver,
// no gate, no record.
int stub_party_set_recruitable(lua_State* L) {
    logCall(L, upLog(L), "party_set_recruitable", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->partyRecruitable() = v;
    return 0;
}

// parking_spot_disable_all (Sec18.29, 0x00a59320). 1 mandatory boolean, no
// nil-gate. Return none. CONFIRMED (raw-disassembly-verified hidden-
// argument finding): despite its own name, only ONE fixed slot (index 1)
// is ever touched, never "all" - the Lua-facing sense is "enable" (true ->
// clear/enable slot 1) vs. "disable" (false -> set/disable slot 1),
// INVERTED from what the name suggests as a command.
int stub_parking_spot_disable_all(lua_State* L) {
    logCall(L, upLog(L), "parking_spot_disable_all", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->parkingSpotSlot1Enabled() = v; // CONFIRMED: true -> enable (clear), false -> disable (set)
    return 0;
}

// on_purchase (Sec18.30, 0x00a57260). 1 mandatory string (callback name,
// opaque). Return none. Registers into the named-hook-slot-array family's
// first confirmed GLOBAL-anchored instance (base 0x026e9528, slot 4) -
// same empty-string-clears-slot convention as every other hook field in
// this header.
int stub_on_purchase(lua_State* L) {
    logCall(L, upLog(L), "on_purchase", upStateTag(L));
    std::string callbackName = argString(L, 1);
    upState(L)->onPurchaseCallback() = callbackName;
    return 0;
}

} // namespace stubs_b21
} // namespace sr3luahost
