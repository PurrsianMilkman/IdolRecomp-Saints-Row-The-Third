// Synthetic tests for wave 2, Batch W2-2 (2026-10-08) -
// spec-lua-api-behaviour.md Sec17 (30 gameplay-cluster names, "ranks
// 251-350 tranche, part A") + Sec18 (30 gameplay-cluster names, "ranks
// 251-350 tranche, part C"), 60 names total. Same hand-written-fixture
// convention as every other synthetic_luahost_*_test. All 60 names are
// `gameplay` cluster per the spec's own section headers.
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "wave2_w22_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "wave2_w22_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

// The 60 (name, cluster) rows this batch registers - all `gameplay`.
std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"vehicle_set_script_hard_goto", "gameplay"},
        {"vehicle_set_crazy", "gameplay"},
        {"set_perfect_aim", "gameplay"},
        {"set_civilians_flee", "gameplay"},
        {"set_attack_enemies_flag", "gameplay"},
        {"player_force_vehicle_seat", "gameplay"},
        {"on_human_killed", "gameplay"},
        {"mesh_mover_destroyed", "gameplay"},
        {"character_set_persona", "gameplay"},
        {"character_set_never_fall", "gameplay"},
        {"character_set_attack_multiplier", "gameplay"},
        {"character_ragdoll", "gameplay"},
        {"character_add_prop", "gameplay"},
        {"camera_shake_stop", "gameplay"},
        {"camera_shake_play_looping", "gameplay"},
        {"ai_attack_region", "gameplay"},
        {"vehicle_prevent_transition_to_ambient", "gameplay"},
        {"tutorial_stop", "gameplay"},
        {"set_stag_active", "gameplay"},
        {"set_cant_cower_flag", "gameplay"},
        {"persona_set_global_situation_override", "gameplay"},
        {"persona_set_dlc_bundle", "gameplay"},
        {"mission_autosave", "gameplay"},
        {"light_group_set_intensity", "gameplay"},
        {"inv_item_remove", "gameplay"},
        {"hud_display_set_element", "gameplay"},
        {"helicopter_enter_retreat", "gameplay"},
        {"door_is_open", "gameplay"},
        {"dlc_gis_film_grain_unload", "gameplay"},
        {"difficulty_get_level", "gameplay"},
        {"cash_add", "gameplay"},
        {"boss_battle_matt_set_matt_flying", "gameplay"},
        {"ai_do_scripted_take_cover", "gameplay"},
        {"action_stop_synced_state", "gameplay"},
        {"action_play_synced_is_finished", "gameplay"},
        {"spawn_override_change_hood_alternate_spawning", "gameplay"},
        {"shop_ignore_clerk", "gameplay"},
        {"set_animation_set", "gameplay"},
        {"player_release_human_shield", "gameplay"},
        {"player_is_rc_driver", "gameplay"},
        {"on_incapacitated", "gameplay"},
        {"notoriety_force_no_air_spawn", "gameplay"},
        {"helicopters_set_jitter_override", "gameplay"},
        {"group_members_alive", "gameplay"},
        {"flashpoints_enable", "gameplay"},
        {"character_set_immune_to_explosion_vomit", "gameplay"},
        {"character_set_always_combat_ready", "gameplay"},
        {"character_make_priority_target", "gameplay"},
        {"character_is_player", "gameplay"},
        {"vehicle_set_team", "gameplay"},
        {"vehicle_set_ignore_rail_obstacles", "gameplay"},
        {"set_suppress_synced_attacks_flag", "gameplay"},
        {"reticle_override_clear", "gameplay"},
        {"radio_global_block", "gameplay"},
        {"player_grenades_enable", "gameplay"},
        {"player_can_sprint", "gameplay"},
        {"player_actions_disable", "gameplay"},
        {"party_set_recruitable", "gameplay"},
        {"parking_spot_disable_all", "gameplay"},
        {"on_purchase", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 60 names land as real functions in gameplay state,
    // and nowhere else.
    {
        Host host(batchFixtureNames());
        CHECK(hasGlobal(host.gameplayState(), "vehicle_set_script_hard_goto"));
        CHECK(!hasGlobal(host.uiState(), "vehicle_set_script_hard_goto"));
        CHECK(hasGlobal(host.gameplayState(), "on_purchase"));
        for (const auto& rn : batchFixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ==================================================================
    // Section 17.
    // ==================================================================

    // Sec17.1 vehicle_set_script_hard_goto: resolve gate, double-gate bit.
    fails(host, gp, "vehicle_set_script_hard_goto('v1', true)", "'v1'");
    es.objectResolves().set("v1", true);
    ok(host, gp, "vehicle_set_script_hard_goto('v1', true)");
    CHECK((es.getOrCreateVehicle("v1").vehicleAiForceFlags.get(VehicleState::kScriptHardGotoBit) &
           VehicleState::kScriptHardGotoBit) != 0);
    ok(host, gp, "vehicle_set_script_hard_goto('v1', false)");
    CHECK((es.getOrCreateVehicle("v1").vehicleAiForceFlags.get(VehicleState::kScriptHardGotoBit) &
           VehicleState::kScriptHardGotoBit) == 0);
    es.getOrCreateVehicle("v1").forceFlagGatePasses.set(false);
    ok(host, gp, "vehicle_set_script_hard_goto('v1', true)");
    CHECK((es.getOrCreateVehicle("v1").vehicleAiForceFlags.get(VehicleState::kScriptHardGotoBit) &
           VehicleState::kScriptHardGotoBit) == 0); // gate false -> replicate only, no local bit flip
    es.getOrCreateVehicle("v1").forceFlagGatePasses.set(true);

    // Sec17.2 vehicle_set_crazy: simple direct write, no gate.
    es.objectResolves().set("v2", true);
    ok(host, gp, "vehicle_set_crazy('v2', true)");
    CHECK(es.getOrCreateVehicle("v2").crazyEnabled == true);
    ok(host, gp, "vehicle_set_crazy('v2', false)");
    CHECK(es.getOrCreateVehicle("v2").crazyEnabled == false);

    // Sec17.3 set_perfect_aim: default-true optional bool, double-gate bit.
    es.objectResolves().set("c1", true);
    ok(host, gp, "set_perfect_aim('c1')");
    CHECK((es.getOrCreateCharacter("c1").forceFlags1c98.get(CharacterState::kPerfectAimBit1c98) &
           CharacterState::kPerfectAimBit1c98) != 0);

    // Sec17.4 set_civilians_flee: empty registry -> safe no-op by default;
    // a registered civilian with the AI-mode-skip gate cleared gets the
    // directive applied; the skip gate true -> civilian untouched.
    ok(host, gp, "set_civilians_flee(true)");
    es.registerCivilianForTesting("civ1");
    es.objectResolves().set("civ1", true);
    es.civilianFleeAiModeSkip().set("civ1", false);
    es.objectResolves().set("anchor1", false);
    ok(host, gp, "set_civilians_flee(true, true, 'anchor1')"); // anchor doesn't resolve -> self-position variant
    CHECK(es.getOrCreateCharacter("civ1").fleeActive == true);
    CHECK(es.getOrCreateCharacter("civ1").fleeAnchorName == "");
    es.objectResolves().set("anchor1", true);
    ok(host, gp, "set_civilians_flee(true, true, 'anchor1')");
    CHECK(es.getOrCreateCharacter("civ1").fleeActive == true);
    CHECK(es.getOrCreateCharacter("civ1").fleeAnchorName == "anchor1");
    ok(host, gp, "set_civilians_flee(false)");
    CHECK(es.getOrCreateCharacter("civ1").fleeActive == false);
    es.civilianFleeAiModeSkip().set("civ1", true);
    ok(host, gp, "set_civilians_flee(true)"); // skip gate true -> no change
    CHECK(es.getOrCreateCharacter("civ1").fleeActive == false);

    // Sec17.5 set_attack_enemies_flag: unconditional +0xac bit, then the
    // gated secondary ai_data bit only when the vtable predicate is true.
    es.objectResolves().set("c2", true);
    es.getOrCreateCharacter("c2").attackEnemiesSecondaryGatePredicate.set(false);
    ok(host, gp, "set_attack_enemies_flag('c2', true)");
    CHECK(es.getOrCreateCharacter("c2").attackEnemiesBitAc == true);
    CHECK((es.getOrCreateCharacter("c2").aiDataFlags9.knownMask() & CharacterState::kAttackEnemiesOnSightBit9) == 0);
    es.getOrCreateCharacter("c2").attackEnemiesSecondaryGatePredicate.set(true);
    ok(host, gp, "set_attack_enemies_flag('c2', true)");
    CHECK((es.getOrCreateCharacter("c2").aiDataFlags9.get(CharacterState::kAttackEnemiesOnSightBit9) &
           CharacterState::kAttackEnemiesOnSightBit9) != 0);

    // Sec17.6 player_force_vehicle_seat: two independent global writes.
    es.objectResolves().set("c3", true);
    ok(host, gp, "player_force_vehicle_seat('c3', 2)");
    CHECK(es.getOrCreateCharacter("c3").forcedVehicleSeatIndex == 2);
    CHECK(es.forcedSeatVehicleName() == "");
    es.objectResolves().set("v3", true);
    ok(host, gp, "player_force_vehicle_seat('c3', 3, 'v3')");
    CHECK(es.getOrCreateCharacter("c3").forcedVehicleSeatIndex == 3);
    CHECK(es.forcedSeatVehicleName() == "v3");

    // Sec17.7 on_human_killed: global hook, slot 0xb, optional filter.
    ok(host, gp, "on_human_killed()");
    CHECK(es.onHumanKilledCallback() == "");
    // Retuned 2026-10-14 (spec sync bfe0542 section C): the string is the CALLBACK NAME, not a filter - it is
    // stored as-is with no resolve step, the last call wins, and ""/nil clears.
    ok(host, gp, "on_human_killed('zombie')");
    CHECK(es.onHumanKilledCallback() == "zombie");
    ok(host, gp, "on_human_killed('other_cb')");
    CHECK(es.onHumanKilledCallback() == "other_cb");
    ok(host, gp, "on_human_killed('')");
    CHECK(es.onHumanKilledCallback() == "");
    ok(host, gp, "on_human_killed('zombie')");
    ok(host, gp, "on_human_killed(nil)");
    CHECK(es.onHumanKilledCallback() == "");

    // Sec17.8 mesh_mover_destroyed: reuses the +0xc0/+0xdc sub-object facts.
    es.objectResolves().set("mover1", true);
    ok(host, gp, "assert(mesh_mover_destroyed('mover1') == true)"); // neither sub-object present -> destroyed
    es.getOrCreatePlacedObject("mover1").hasPlainKillSubObjectDc = true;
    ok(host, gp, "assert(mesh_mover_destroyed('mover1') == false)");
    es.getOrCreatePlacedObject("mover1").hasScriptedKillSubObjectC0 = true;
    es.getOrCreatePlacedObject("mover1").scriptedSubObjectField138NonNull.set(true);
    ok(host, gp, "assert(mesh_mover_destroyed('mover1') == false)");
    es.getOrCreatePlacedObject("mover1").scriptedSubObjectField138NonNull.set(false);
    ok(host, gp, "assert(mesh_mover_destroyed('mover1') == true)");

    // Sec17.9 character_set_persona: direct-apply path (gate true by default).
    es.objectResolves().set("c4", true);
    ok(host, gp, "character_set_persona('c4', 'gangster')");
    CHECK(es.getOrCreateCharacter("c4").currentPersonaName == "gangster");
    CHECK(es.getOrCreateCharacter("c4").personaApplyCount == 1);

    // Sec17.10 character_set_never_fall: default-true optional bool.
    es.objectResolves().set("c5", true);
    ok(host, gp, "character_set_never_fall('c5')");
    CHECK((es.getOrCreateCharacter("c5").forceFlags1c9c.get(CharacterState::kNeverFallBit1c9c) &
           CharacterState::kNeverFallBit1c9c) != 0);

    // Sec17.11 character_set_attack_multiplier: raw value write.
    es.objectResolves().set("c6", true);
    ok(host, gp, "character_set_attack_multiplier('c6', 2.5)");
    CHECK(es.getOrCreateCharacter("c6").attackMultiplier == 2.5);

    // Sec17.12 character_ragdoll: duration 0 clears; nonzero starts.
    es.objectResolves().set("c7", true);
    ok(host, gp, "character_ragdoll('c7', 0)");
    CHECK(es.getOrCreateCharacter("c7").ragdollClearCount == 1);
    ok(host, gp, "character_ragdoll('c7')"); // default duration 3,600,000
    CHECK(es.getOrCreateCharacter("c7").ragdollStartCount == 1);
    CHECK(es.getOrCreateCharacter("c7").ragdollDurationMs == 3600000);

    // Sec17.13 character_add_prop: gated on both character and prop resolve.
    es.objectResolves().set("c8", true);
    es.propDefinitionResolves().set("hat1", false);
    ok(host, gp, "character_add_prop('c8', 'hat1')");
    CHECK(es.getOrCreateCharacter("c8").attachedProps.empty());
    es.propDefinitionResolves().set("hat1", true);
    ok(host, gp, "character_add_prop('c8', 'hat1')");
    CHECK(es.getOrCreateCharacter("c8").attachedProps.size() == 1);
    // f752324: items_3d catalogue installed -> complete table; unknown name = silent no-op, case-folded.
    es.installItems3dNames({"GasMask"});
    es.objectResolves().set("c9", true);
    ok(host, gp, "character_add_prop('c9', 'gasmask')");
    CHECK(es.getOrCreateCharacter("c9").attachedProps.size() == 1);
    ok(host, gp, "character_add_prop('c9', 'nosuchprop')");
    CHECK(es.getOrCreateCharacter("c9").attachedProps.size() == 1);

    // Sec17.14/17.15 camera_shake_stop/camera_shake_play_looping: kept
    // separate from the already-registered camera_shake_play.
    ok(host, gp, "camera_shake_play_looping('rumble')");
    CHECK(es.cameraShake().loopingActive == true);
    CHECK(es.cameraShake().loopingProfile == "rumble");
    ok(host, gp, "camera_shake_stop()");
    CHECK(es.cameraShake().loopingActive == false);
    CHECK(es.cameraShake().stopCount == 1);

    // Sec17.16 ai_attack_region: negative radius clears; non-negative with
    // a resolved region applies the leash-to-position wrapper.
    es.objectResolves().set("c9", true);
    ok(host, gp, "ai_attack_region('c9', 'region1', -1)");
    es.objectResolves().set("region1", false);
    ok(host, gp, "ai_attack_region('c9', 'region1', 5)");
    CHECK(es.getOrCreateCharacter("c9").npcLeashSetCount == 0); // region not yet resolved
    es.objectResolves().set("region1", true);
    ok(host, gp, "ai_attack_region('c9', 'region1', 5)");
    CHECK(es.getOrCreateCharacter("c9").npcLeashAnchorName == "region1");
    CHECK(es.getOrCreateCharacter("c9").npcLeashRadius == 5);

    // Sec17.17 vehicle_prevent_transition_to_ambient: a second +0xc68 bit
    // offset (+7), distinct from +5/+4.
    es.objectResolves().set("v4", true);
    ok(host, gp, "vehicle_prevent_transition_to_ambient('v4', true)");
    CHECK((es.getOrCreateVehicle("v4").vehicleAiForceFlagsByte7.get(VehicleState::kNeverGoAmbientBit) &
           VehicleState::kNeverGoAmbientBit) != 0);

    // Sec17.18 tutorial_stop: unknown name -> no-op; known name -> state 4.
    ok(host, gp, "tutorial_stop('not_a_real_tutorial_name_xyz')");
    std::string knownTutorialName;
    for (int i = 0; i < EngineState::kTutorialEntryCount; ++i) {
        const char* n = EngineState::tutorialName(i);
        if (n) { knownTutorialName = n; break; }
    }
    CHECK(!knownTutorialName.empty());
    ok(host, gp, "tutorial_stop('" + knownTutorialName + "')");
    EngineState::TutorialLookup lookup = EngineState::tutorialLookup(knownTutorialName);
    CHECK(lookup.index >= 0);
    CHECK(es.tutorialState().get(EngineState::tutorialStateKey(lookup.index)) == 4);

    // Sec17.19 set_stag_active: bare global write.
    ok(host, gp, "set_stag_active(true)");
    CHECK(es.stagActive() == true);
    ok(host, gp, "set_stag_active(false)");
    CHECK(es.stagActive() == false);

    // Sec17.20 set_cant_cower_flag: a third independent bit on object +0xa.
    es.objectResolves().set("c10", true);
    ok(host, gp, "set_cant_cower_flag('c10')"); // default true
    CHECK(es.getOrCreateCharacter("c10").cantCowerFlag == true);

    // Sec17.21 persona_set_global_situation_override: range-checked [1,9].
    ok(host, gp, "persona_set_global_situation_override(5)");
    CHECK(es.personaGlobalSituationOverride() == 5);
    ok(host, gp, "persona_set_global_situation_override(0)"); // out of range, ignored
    CHECK(es.personaGlobalSituationOverride() == 5);
    ok(host, gp, "persona_set_global_situation_override(10)"); // out of range, ignored
    CHECK(es.personaGlobalSituationOverride() == 5);

    // Sec17.22 persona_set_dlc_bundle: change-detection.
    ok(host, gp, "persona_set_dlc_bundle(3)");
    CHECK(es.personaDlcBundle() == 3);

    // Sec17.23 mission_autosave (corrected per Sec60.3): default non-
    // blocking path reaches a real kickoff; Steam-check failure silently
    // returns with no record/stub-call at all; busy=true skips the whole
    // inner chain (neither a kickoff NOR a deferred-pending mark - CONFIRMED
    // per Sec60.3, the busy gate wraps the deferred-mark branch too, not
    // just the kickoff); busy=false with the chain not ready defers instead.
    ok(host, gp, "mission_autosave()");
    CHECK(es.missionAutosaveStubCallCount() == 1);
    CHECK(es.missionAutosaveKickoffCount() == 1);
    es.missionAutosaveSteamCheckOk() = false;
    ok(host, gp, "mission_autosave()");
    CHECK(es.missionAutosaveStubCallCount() == 1); // unchanged - silent return
    es.missionAutosaveSteamCheckOk() = true;
    es.missionAutosaveBusy() = true;
    ok(host, gp, "mission_autosave()");
    CHECK(es.missionAutosaveStubCallCount() == 2); // record/stub still fire
    CHECK(es.missionAutosaveKickoffCount() == 1);  // but no new kickoff
    CHECK(es.missionAutosaveDeferredPending() == false); // busy -> neither branch runs
    es.missionAutosaveBusy() = false;
    es.missionAutosaveChainReady() = false;
    ok(host, gp, "mission_autosave()");
    CHECK(es.missionAutosaveKickoffCount() == 1);        // still no new kickoff
    CHECK(es.missionAutosaveDeferredPending() == true);  // not busy, chain not ready -> deferred

    // Sec17.24 light_group_set_intensity: unresolved -> no-op; resolved ->
    // unconditional local apply; co-op session present -> also replicates.
    es.lightGroupResolves().set("lg1", false);
    ok(host, gp, "light_group_set_intensity('lg1', 2.0, true)");
    CHECK(es.lightGroup("lg1").applyCount == 0);
    es.lightGroupResolves().set("lg1", true);
    ok(host, gp, "light_group_set_intensity('lg1', 2.0)");
    CHECK(es.lightGroup("lg1").intensity == 2.0);
    CHECK(es.lightGroup("lg1").applyCount == 1);
    CHECK(es.lightGroup("lg1").replicateCount == 0); // no co-op session by default

    // Sec17.25 inv_item_remove: default "#PLAYER1#" sentinel target; gate
    // true -> applies only if the item resolves; gate false -> replicates.
    es.objectResolves().set("#PLAYER1#", true);
    es.inventoryItemDefinitionResolves().set("pistol", false);
    ok(host, gp, "inv_item_remove('pistol')");
    CHECK(es.getOrCreateCharacter("#PLAYER1#").inventoryItemRemoveAppliedCount == 0);
    es.inventoryItemDefinitionResolves().set("pistol", true);
    ok(host, gp, "inv_item_remove('pistol')");
    CHECK(es.getOrCreateCharacter("#PLAYER1#").inventoryItemRemoveAppliedCount == 1);
    // installInventoryItemNames: case-insensitive, complete table -> unknown resolves false (no OPEN).
    CHECK(!es.inventoryItemTableComplete());
    es.installInventoryItemNames({"Rifle-Gang", "DLC_fireball"});
    CHECK(es.inventoryItemTableComplete());
    CHECK(es.inventoryItemDefinitionResolve("dlc_FIREBALL"));
    CHECK(es.inventoryItemDefinitionResolve("RIFLE-gang"));
    CHECK(!es.inventoryItemDefinitionResolve("no-such-item"));
    CHECK(!es.inventoryItemDefinitionResolve("none"));
    ok(host, gp, "inv_item_remove('DLC_fireball')");
    CHECK(es.getOrCreateCharacter("#PLAYER1#").inventoryItemRemoveAppliedCount == 2);
    ok(host, gp, "inv_item_remove('no-such-item')");
    CHECK(es.getOrCreateCharacter("#PLAYER1#").inventoryItemRemoveAppliedCount == 2);
    es.getOrCreateCharacter("#PLAYER1#").forceFlagGatePasses.set(false);
    ok(host, gp, "inv_item_remove('pistol')");
    CHECK(es.getOrCreateCharacter("#PLAYER1#").inventoryItemRemoveReplicateCount == 1);
    es.getOrCreateCharacter("#PLAYER1#").forceFlagGatePasses.set(true);

    // Sec17.26 hud_display_set_element: inactive slot -> no local write;
    // active slot -> single sub-field or "all" (0xf); out-of-range slot/
    // subIndex -> hardened refusal, each counted separately.
    ok(host, gp, "hud_display_set_element(0, 0, 5.0)");
    CHECK(es.hudDisplayStatePool().setElementCount == 1);
    CHECK(es.hudDisplayStatePool().setElementAppliedCount == 0); // slot 0 not active
    es.hudDisplayStatePool().elementActive[0] = true;
    ok(host, gp, "hud_display_set_element(0, 3, 7.5)");
    CHECK(es.hudDisplayStatePool().elementValues[0][3] == 7.5);
    CHECK(es.hudDisplayStatePool().setElementAppliedCount == 1);
    ok(host, gp, "hud_display_set_element(0, 15, 9.0)"); // 0xf = all
    for (int i = 0; i < HudDisplayStatePool::kElementSubFieldCount; ++i)
        CHECK(es.hudDisplayStatePool().elementValues[0][static_cast<size_t>(i)] == 9.0);
    ok(host, gp, "hud_display_set_element(99, 0, 1.0)"); // CHOSEN hardened: slot out of range
    CHECK(es.hudDisplayStatePool().setElementHardenedSlotIdCount == 1);
    ok(host, gp, "hud_display_set_element(0, 12, 1.0)"); // CHOSEN hardened: subIndex out of range (not 0xf)
    CHECK(es.hudDisplayStatePool().setElementHardenedSubIndexCount == 1);

    // Sec17.27 helicopter_enter_retreat: mode-gate false -> pushes false,
    // no state change; mode-gate true + mode != 6 -> transitions and
    // (when the height check trips) schedules a deadline.
    //
    // CONFIRMED CASE-BY-CASE (vehicle-class xtbl wiring task, 2026-10-10):
    // "heli1" here is a pure Lua-host-only test fixture with no real
    // zone-spawn backing, and needs qualifies to visit both false and true
    // within one test body - legitimately staying manual per this task's own
    // "confirm case-by-case" instruction (see
    // tests/synthetic_luahost_batch2728_test.cpp's own m05_heli_pierce
    // fixture for the real, general, data-driven path this project now has).
    es.objectResolves().set("heli1", true);
    es.helicopterFlyTo().qualifies.set("heli1", false);
    ok(host, gp, "assert(helicopter_enter_retreat('heli1') == false)");
    CHECK(es.helicopter("heli1").retreatTransitionCount == 0);
    es.helicopterFlyTo().qualifies.set("heli1", true);
    es.helicopter("heli1").subObjectModeEnum.set(3);
    es.helicopter("heli1").heightCheckTrips.set(false);
    ok(host, gp, "assert(helicopter_enter_retreat('heli1') == true)");
    CHECK(es.helicopter("heli1").retreatTransitionCount == 1);
    CHECK(es.helicopter("heli1").retreatDeadlineStamp == 0); // height check did not trip
    es.helicopter("heli1").subObjectModeEnum.set(3);
    es.helicopter("heli1").heightCheckTrips.set(true);
    ok(host, gp, "assert(helicopter_enter_retreat('heli1') == true)");
    CHECK(es.helicopter("heli1").retreatTransitionCount == 2);
    CHECK(es.helicopter("heli1").retreatDeadlineStamp > 0);
    es.helicopter("heli1").subObjectModeEnum.set(6);
    ok(host, gp, "assert(helicopter_enter_retreat('heli1') == true)"); // already retreating -> no further transition
    CHECK(es.helicopter("heli1").retreatTransitionCount == 2);

    // Sec17.28 door_is_open: reuses hasInteractionAnchor (Sec1.10).
    es.objectResolves().set("door1", true);
    es.getOrCreatePlacedObject("door1").hasInteractionAnchor.set(false);
    ok(host, gp, "assert(door_is_open('door1') == false)");
    es.getOrCreatePlacedObject("door1").hasInteractionAnchor.set(true);
    es.getOrCreatePlacedObject("door1").doorOpenBit40At7a.set(true);
    ok(host, gp, "assert(door_is_open('door1') == true)");

    // Sec17.29 dlc_gis_film_grain_unload: default mask 3 runs both halves;
    // DLC unavailable short-circuits to a reset (OPEN until set).
    es.dlcFilmGrainAvailable().set(false);
    ok(host, gp, "dlc_gis_film_grain_unload()");
    CHECK(es.filmGrainLocalApplyCount() == 1);
    CHECK(es.filmGrainReplicateCount() == 1);
    CHECK(es.filmGrainFadeState() == false); // DLC unavailable by default -> reset
    es.dlcFilmGrainAvailable().set(true);
    ok(host, gp, "dlc_gis_film_grain_unload(true, 1)"); // bit 0 only
    CHECK(es.filmGrainFadeState() == true);
    CHECK(es.filmGrainLocalApplyCount() == 2);
    CHECK(es.filmGrainReplicateCount() == 1); // unchanged - bit 1 not set

    // Sec17.30 difficulty_get_level: CONFIRMED the SAME real function as
    // get_current_difficulty (Sec41.2, 0x00a47300, already covered by its
    // own test) under its own alias name - shares the SAME field, file-
    // default 1, not a second, independently-OPEN copy.
    ok(host, gp, "assert(difficulty_get_level() == 1)"); // CONFIRMED file-default 1
    es.difficultyLevel() = 2;
    ok(host, gp, "assert(difficulty_get_level() == 2)");

    // ==================================================================
    // Section 18.
    // ==================================================================

    // Sec18.1 cash_add: reuses the shared playerCash() economy value.
    es.objectResolves().set("#PLAYER1#", true);
    double before = es.playerCash();
    ok(host, gp, "cash_add(10)");
    CHECK(es.playerCash() == before + 1000.0); // x100 scale
    ok(host, gp, "cash_add(30000000000)"); // saturates at 2,000,000,000
    CHECK(es.playerCash() == before + 1000.0 + 2000000000.0);

    // Sec18.2 boss_battle_matt_set_matt_flying: default true.
    ok(host, gp, "boss_battle_matt_set_matt_flying()");
    CHECK(es.bossBattleMattFlying() == true);
    ok(host, gp, "boss_battle_matt_set_matt_flying(false)");
    CHECK(es.bossBattleMattFlying() == false);

    // Sec18.3 ai_do_scripted_take_cover (blocker m02/m10, FINAL 2026-10-15): the 0x00540b90 gate is true for a
    // fresh NPC -> action committed and true; false only inside a cover stay-down window (no action, false);
    // anchor given + resolved -> true, writes the shared destination field.
    es.objectResolves().set("c11", true);
    es.getOrCreateCharacter("c11").coverStayDownActive = true;
    ok(host, gp, "assert(ai_do_scripted_take_cover('c11') == false)");
    CHECK(es.getOrCreateCharacter("c11").scriptedTakeCoverDispatchCount == 0);
    es.getOrCreateCharacter("c11").coverStayDownActive = false;
    ok(host, gp, "assert(ai_do_scripted_take_cover('c11') == true)");
    CHECK(es.getOrCreateCharacter("c11").scriptedTakeCoverDispatchCount == 1);
    es.objectResolves().set("anchor2", true);
    ok(host, gp, "assert(ai_do_scripted_take_cover('c11', 'anchor2') == true)");
    CHECK(es.getOrCreateCharacter("c11").pendingScriptedMoveDestination == "anchor2");
    CHECK(es.getOrCreateCharacter("c11").scriptedTakeCoverDispatchCount == 2);

    // Sec18.4 action_stop_synced_state: clears the SAME pairing
    // action_play_synced_state writes.
    es.objectResolves().set("c12", true);
    es.getOrCreateCharacter("c12").syncedActionPartnerName = "c13";
    es.getOrCreateCharacter("c12").syncedActionPaired = true;
    ok(host, gp, "action_stop_synced_state('c12')");
    CHECK(es.getOrCreateCharacter("c12").syncedActionPaired == false);
    CHECK(es.getOrCreateCharacter("c12").syncedActionPartnerName.empty());

    // Sec18.5 action_play_synced_is_finished: AND of both actors' states.
    es.objectResolves().set("c14", true);
    es.objectResolves().set("c15", true);
    es.getOrCreateCharacter("c14").syncedAnimControllerActive.set(false);
    es.getOrCreateCharacter("c15").syncedAnimControllerActive.set(false);
    ok(host, gp, "assert(action_play_synced_is_finished('c14', 'c15') == true)");
    es.getOrCreateCharacter("c14").syncedAnimControllerActive.set(true);
    es.getOrCreateCharacter("c14").syncedAnimElapsedTime.set(0.5);
    ok(host, gp, "assert(action_play_synced_is_finished('c14', 'c15', 1.0) == false)");
    es.getOrCreateCharacter("c14").syncedAnimElapsedTime.set(1.5);
    ok(host, gp, "assert(action_play_synced_is_finished('c14', 'c15', 1.0) == true)");

    // Sec18.6 spawn_override_change_hood_alternate_spawning: unconditional
    // toggle; host-only extra notify (CHOSEN false by default).
    es.objectResolves().set("hood1", true);
    ok(host, gp, "spawn_override_change_hood_alternate_spawning('hood1', true)");
    CHECK(es.hoodAlternateSpawning()["hood1"] == true);
    ok(host, gp, "spawn_override_change_hood_alternate_spawning('hood1', false)");
    CHECK(es.hoodAlternateSpawning()["hood1"] == false);

    // Sec18.7 shop_ignore_clerk: CONFIRMED no-op.
    ok(host, gp, "shop_ignore_clerk(true)");

    // Sec18.8 set_animation_set: name tracked directly.
    es.objectResolves().set("c16", true);
    ok(host, gp, "set_animation_set('c16', 'walk_set')");
    CHECK(es.getOrCreateCharacter("c16").animationSetName == "walk_set");

    // Sec18.9 player_release_human_shield: local authority by default;
    // boolA forces an ejection; normal release needs a held partner + state 1.
    es.objectResolves().set("c17", true);
    ok(host, gp, "assert(player_release_human_shield('c17', true) == true)");
    CHECK(es.getOrCreateCharacter("c17").humanShieldForcibleEjectCount == 1);
    es.getOrCreateCharacter("c17").humanShieldHostageName = "hostage1";
    es.getOrCreateCharacter("c17").humanShieldStateByte18d4 = 1;
    es.getOrCreateCharacter("c17").humanShieldNormalReleaseGatesPass.set(true);
    ok(host, gp, "assert(player_release_human_shield('c17') == true)");
    CHECK(es.getOrCreateCharacter("c17").humanShieldStateByte18d4 == 4);

    // Sec18.10 player_is_rc_driver: collapsed OPEN predicate per character.
    es.objectResolves().set("c18", true);
    es.isRcDriverPredicate().set("c18", true);
    ok(host, gp, "assert(player_is_rc_driver('c18') == true)");

    // Sec18.11 on_incapacitated: per-character hook slot 1.
    es.objectResolves().set("c19", true);
    ok(host, gp, "on_incapacitated('onInc', 'c19')");
    CHECK(es.getOrCreateCharacter("c19").onIncapacitatedCallback == "onInc");

    // Sec18.12 notoriety_force_no_air_spawn: bare global.
    ok(host, gp, "notoriety_force_no_air_spawn(true)");
    CHECK(es.notorietyForceNoAirSpawn() == true);

    // Sec18.13 helicopters_set_jitter_override: two adjacent globals.
    ok(host, gp, "helicopters_set_jitter_override(1.0, 2.0)");
    CHECK(es.helicopterJitterMin() == 1.0);
    CHECK(es.helicopterJitterMax() == 2.0);

    // Sec18.14 group_members_alive: resolves + has-members gate + alive count.
    es.groupResolves().set("grp1", false);
    ok(host, gp, "assert(group_members_alive('grp1') == 0)"); // unresolved
    es.groupResolves().set("grp1", true);
    es.groupHasMembersBit().set("grp1", false);
    ok(host, gp, "assert(group_members_alive('grp1') == 0)"); // no members bit
    es.groupHasMembersBit().set("grp1", true);
    es.registerGroupMemberForTesting("grp1", "m1");
    es.registerGroupMemberForTesting("grp1", "m2");
    es.getOrCreateCharacter("m2").isDeadHighConfidence = true;
    ok(host, gp, "assert(group_members_alive('grp1') == 1)");

    // Sec18.15 flashpoints_enable: default true.
    ok(host, gp, "flashpoints_enable()");
    CHECK(es.flashpointsEnabled() == true);
    ok(host, gp, "flashpoints_enable(false)");
    CHECK(es.flashpointsEnabled() == false);

    // Sec18.16 character_set_immune_to_explosion_vomit: a further +0x1c9c bit.
    es.objectResolves().set("c20", true);
    ok(host, gp, "character_set_immune_to_explosion_vomit('c20', true)");
    CHECK((es.getOrCreateCharacter("c20").forceFlags1c9c.get(CharacterState::kImmuneToExplosionVomitBit1c9c) &
           CharacterState::kImmuneToExplosionVomitBit1c9c) != 0);

    // Sec18.17 character_set_always_combat_ready: a further +0x1c98 bit.
    es.objectResolves().set("c21", true);
    ok(host, gp, "character_set_always_combat_ready('c21', true)");
    CHECK((es.getOrCreateCharacter("c21").forceFlags1c98.get(CharacterState::kAlwaysCombatReadyBit1c98) &
           CharacterState::kAlwaysCombatReadyBit1c98) != 0);

    // Sec18.18 character_make_priority_target: radius always squared on
    // local apply, regardless of the boolean.
    es.objectResolves().set("c22", true);
    ok(host, gp, "character_make_priority_target('c22', true, 4.0)");
    CHECK(es.getOrCreateCharacter("c22").priorityTargetRadiusSquared == 16.0);
    ok(host, gp, "character_make_priority_target('c22', false, 3.0)");
    CHECK(es.getOrCreateCharacter("c22").priorityTargetRadiusSquared == 9.0); // still overwritten
    CHECK((es.getOrCreateCharacter("c22").forceFlags1c98.get(CharacterState::kPriorityTargetBit1c98) &
           CharacterState::kPriorityTargetBit1c98) == 0);

    // Sec18.19 character_is_player: reuses objectResolves() directly.
    ok(host, gp, "assert(character_is_player('c22') == true)");
    es.objectResolves().set("nobody", false);
    ok(host, gp, "assert(character_is_player('nobody') == false)");

    // Sec18.20 vehicle_set_team: default fallback 9 on no match; a test-
    // registered mapping overrides it.
    es.objectResolves().set("v5", true);
    ok(host, gp, "vehicle_set_team('v5', 'unknown_team_xyz')");
    CHECK(es.getOrCreateVehicle("v5").teamId == 9);
    es.registerVehicleTeamIdForTesting("cops", 2);
    ok(host, gp, "vehicle_set_team('v5', 'cops')");
    CHECK(es.getOrCreateVehicle("v5").teamId == 2);

    // Sec18.21 vehicle_set_ignore_rail_obstacles: a further +0xc68+4 bit.
    es.objectResolves().set("v6", true);
    ok(host, gp, "vehicle_set_ignore_rail_obstacles('v6', true)");
    CHECK((es.getOrCreateVehicle("v6").vehicleAiForceFlagsByte4.get(VehicleState::kIgnoreRailObstaclesBit) &
           VehicleState::kIgnoreRailObstaclesBit) != 0);

    // Sec18.22 set_suppress_synced_attacks_flag: a further +0x1c98 bit.
    es.objectResolves().set("c23", true);
    ok(host, gp, "set_suppress_synced_attacks_flag('c23')"); // default true
    CHECK((es.getOrCreateCharacter("c23").forceFlags1c98.get(CharacterState::kSuppressSyncedAttacksBit1c98) &
           CharacterState::kSuppressSyncedAttacksBit1c98) != 0);

    // Sec18.23 reticle_override_clear: bitmask selects local/replicate halves.
    ok(host, gp, "reticle_override_clear(1)");
    CHECK(es.reticleOverrideGlobalsCleared() == true);
    CHECK(es.reticleOverrideClearLocalCount() == 1);
    CHECK(es.reticleOverrideClearReplicateCount() == 0);
    ok(host, gp, "reticle_override_clear(2)");
    CHECK(es.reticleOverrideClearReplicateCount() == 1);

    // Sec18.24 radio_global_block: bare global.
    ok(host, gp, "radio_global_block(true)");
    CHECK(es.radioGlobalBlocked() == true);

    // Sec18.25 player_grenades_enable: zero-argument-sensitive reset.
    ok(host, gp, "player_grenades_enable()");
    CHECK(es.playerGrenadesEnableCallCount() == 1);
    CHECK(es.playerGrenadeGlobalA() == 0.0);

    // Sec18.26 player_can_sprint: enable is a bare write; disable cascades.
    ok(host, gp, "player_can_sprint(true)");
    CHECK(es.sprintEnabled() == true);
    CHECK(es.sprintBit2Cleared() == false);
    ok(host, gp, "player_can_sprint(false)");
    CHECK(es.sprintEnabled() == false);
    CHECK(es.sprintBit2Cleared() == true);

    // Sec18.27 player_actions_disable: reuses the previously write-only
    // actionsEnableBit20_1d99 field. Bit-sense polarity resolved 2026-10-08:
    // despite the field's own "Enable" name, it is a *disabled*-sense bit
    // (spec-player-runtime.md Sec2.10 "Question B": "the player's `+0x1d99`
    // bits (`0x08`/`0x10`/`0x20` - controls/movement/actions disabled) ...
    // all clear (0) - enabled" at spawn) - so "disable" must SET it to true.
    es.objectResolves().set("c24", true);
    CHECK(es.getOrCreateCharacter("c24").actionsEnableBit20_1d99.get() == false); // clear=enabled at spawn
    ok(host, gp, "player_actions_disable('c24')");
    CHECK(es.getOrCreateCharacter("c24").actionsEnableBit20_1d99.get() == true); // set=disabled

    // Sec18.28 party_set_recruitable: bare global.
    ok(host, gp, "party_set_recruitable(true)");
    CHECK(es.partyRecruitable() == true);

    // Sec18.29 parking_spot_disable_all: CONFIRMED inverted sense - true
    // means enable (clear) slot 1, not "disable all".
    ok(host, gp, "parking_spot_disable_all(true)");
    CHECK(es.parkingSpotSlot1Enabled() == true);
    ok(host, gp, "parking_spot_disable_all(false)");
    CHECK(es.parkingSpotSlot1Enabled() == false);

    // Sec18.30 on_purchase: first GLOBAL-anchored hook-slot-array instance.
    ok(host, gp, "on_purchase('onBuy')");
    CHECK(es.onPurchaseCallback() == "onBuy");

    if (g_failures == 0) {
        std::cout << "All wave2_w22 checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
