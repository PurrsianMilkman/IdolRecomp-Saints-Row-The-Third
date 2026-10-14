// Synthetic tests for wave 1 Batch 4 of the Sec10-28 lua-natives fan-out
// (2026-10-08): spec-lua-api-behaviour.md Sec13 (28 names) + a 27-name
// subset of Sec14 (ranks ~151-250, "flag-setters/hooks/combat cluster"),
// 55 names total. Same fixture convention as
// tests/synthetic_luahost_sec9_sweep_test.cpp; all 55 are "gameplay" in
// tools/lua_all_registered_1490_tagged.txt (grepped directly, not assumed).
//
// Not every one of the 55 gets its own dedicated check below (several are
// simple global on/off switches or thin record-only stand-ins whose own
// doc comment in the stub file is the primary documentation) - this file
// focuses on: registration of all 55; the double-gate flag-setter shape;
// the player-flags-trio polarity (negated vs. not); the richest function
// (set_max_hit_points); the dead-code gap (inv_item_in_slot); hook-slot
// registration; the ring buffer; and a representative OPEN-state refusal.
//
// FIXED 2026-10-10 (found while verifying an UNRELATED trigger-shape task,
// same "EngineState keeps growing every batch" regression HANDOFF.md's own
// W2-6 entry already documents and fixes for tests/synthetic_luahost_test.cpp):
// this file's own 18 `{ ... }` blocks all declared `Host`/`EngineState` by
// value inside ONE shared stack frame (main()'s own), and crossed the
// default 1 MiB thread stack (STATUS_STACK_OVERFLOW, confirmed via a
// standalone run - PowerShell's own exit code decodes to 0xC00000FD). Fixed
// the same way as that prior entry: each block is now its own
// `static void test_block_N()`, called in sequence from a thin `main()` -
// only the single largest block's own locals are live on the stack at once.
#include <cmath>
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
    auto r = host.runChunk(L, code, "sec13_14_batch4_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "sec13_14_batch4_test.lua");
    if (!r.loadOk || r.pcallOk || r.pcallError.find(needle) == std::string::npos) {
        std::cerr << "expected refusal containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("success") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

std::vector<std::string> kNames = {
    "object_indicator_add_objective_text",
    "notoriety_spawn_group_set_delay",
    "hud_prompt",
    "customization_outfit_wear",
    "character_set_no_explosive_panic",
    "character_ragdoll_clear_last_resort_position",
    "ai_scripted_action_is_processing",
    "vehicle_ignore_repulsors",
    "spawn_region_toggle_filter",
    "refraction_situation_set",
    "npc_revive",
    "hud_timer_hide",
    "hud_critical_timer_set",
    "character_has_specific_human_shield",
    "camera_shake_play",
    "camera_end_look_through",
    "action_nodes_enable",
    "set_team",
    "set_min_hit_points",
    "set_max_hit_points",
    "inv_item_in_slot",
    "hud_achievement_show_progress",
    "helicopter_set_max_bank_angle",
    "crib_disable_interface",
    "character_ragdoll_set_last_valid_position",
    "character_collision_enable",
    "ai_set_personality",
    "vehicle_suppress_flipping",
    "action_play_synced_state",
    "vehicle_disable_chase",
    "set_players_can_be_downed",
    "set_player_can_take_human_shields",
    "player_human_shield_lock",
    "on_mover_enter_water",
    "on_ai_do_action_complete",
    "mission_set_excluded_reward",
    "mesh_mover_kill",
    "helicopter_shoot_navpoint",
    "customization_item_wear",
    "customization_item_revert",
    "vehicle_set_keyframed_physics",
    "vehicle_hide",
    "persona_override_persona_start",
    "helicopter_shoot_human",
    "drug_effect_set_override_values",
    "debris_flow_set_active",
    "camera_restrict_set_limits",
    "audio_conversation_playing",
    "vehicle_speed_cancel",
    "vehicle_disable_flee",
    "set_cant_flee_flag",
    "set_behavior_set",
    "roadblocks_enable",
    "player_script_controlled_clear",
    "on_vehicle_enter_water",
};

std::vector<RegisteredName> fixtureNames() {
    std::vector<RegisteredName> out;
    out.reserve(kNames.size());
    for (const auto& n : kNames) out.push_back({n, "gameplay"});
    return out;
}

} // namespace

    // Registration: all 55, gameplay only.
static void test_block_1() {
        Host host(fixtureNames());
        CHECK(kNames.size() == 55);
        for (const auto& n : kNames) {
            CHECK(hasGlobal(host.gameplayState(), n.c_str()));
            CHECK(!hasGlobal(host.uiState(), n.c_str()));
        }
}


    // object_indicator_add_objective_text (Sec13.1): record on resolve, OPEN refusal otherwise.
static void test_block_2() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "object_indicator_add_objective_text('NoObj','icon','text')", "named-object resolution");
        es.objectResolves().set("Obj1", true);
        ok(host, gp, "object_indicator_add_objective_text('Obj1','iconA','textA')");
        auto& recs = es.getOrCreateCharacter("Obj1").objectiveTextIndicators;
        CHECK(recs.size() == 1);
        CHECK(recs[0].iconKey == "iconA" && recs[0].textKey == "textA");
}


    // notoriety_spawn_group_set_delay (Sec13.2): the 24-entry group-name
    // table (0x008ed350) is pre-seeded for its exact 24 CONFIRMED names by
    // applySpecInitialState (lua_spec_initial_state.cpp item 13, cross-
    // referenced against spec-tables-progression.md Sec6.3's own
    // independently-derived name list for the SAME table - this closes the
    // real mission-drive blocker both dlc2_m01 ('police_tank') and m17
    // ('stag_attack_heli') hit). Both resolve with NO manual set() call;
    // a name outside the 24 stays genuinely OPEN.
static void test_block_3() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "notoriety_spawn_group_set_delay('police_tank', 5000)");
        {
            auto& g = es.getOrCreateNotorietySpawnGroup("police_tank");
            CHECK(g.clearFlagCount == 1);
            CHECK(g.lastDelayRequested == 5000);
            CHECK(g.delayRequestCount == 1);
        }
        ok(host, gp, "notoriety_spawn_group_set_delay('stag_attack_heli', 7500)");
        {
            auto& g = es.getOrCreateNotorietySpawnGroup("stag_attack_heli");
            CHECK(g.clearFlagCount == 1);
            CHECK(g.lastDelayRequested == 7500);
            CHECK(g.delayRequestCount == 1);
        }
        // every other one of the 24 CONFIRMED names resolves too (spot-check
        // the first/last of the table, not just the two mission-blocking ones).
        ok(host, gp, "notoriety_spawn_group_set_delay('police', 1000)");
        CHECK(es.getOrCreateNotorietySpawnGroup("police").delayRequestCount == 1);
        ok(host, gp, "notoriety_spawn_group_set_delay('survival_bums', 2000)");
        CHECK(es.getOrCreateNotorietySpawnGroup("survival_bums").delayRequestCount == 1);
        // a name genuinely outside the 24 stays OPEN - not guessed.
        refuses(host, gp, "notoriety_spawn_group_set_delay('not_a_real_spawn_group', 1000)",
                "notoriety spawn-group resolution");
        // regression guard (same convention as objectResolves()'s own closed-set
        // guard, synthetic_luahost_test.cpp test_block_17): exactly 24 names are
        // pre-seeded - never silently widen this set.
        CHECK(es.notorietySpawnGroupResolves().knownCount() == 24);
}


    // character_set_no_explosive_panic (Sec13.5) / vehicle_ignore_repulsors
    // (Sec13.8) / vehicle_suppress_flipping (Sec13.28): double-gate bit
    // setters, direct-apply (forceFlagGatePasses CONFIRMED true in single
    // player).
static void test_block_4() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Char1", true);
        ok(host, gp, "character_set_no_explosive_panic('Char1')"); // default true
        CharacterState& c = es.getOrCreateCharacter("Char1");
        CHECK((c.forceFlags1c9c.get(CharacterState::kNoExplosivePanicBit1c9c) != 0));
        ok(host, gp, "character_set_no_explosive_panic('Char1', false)");
        CHECK((c.forceFlags1c9c.get(CharacterState::kNoExplosivePanicBit1c9c) == 0));

        es.objectResolves().set("Veh1", true);
        ok(host, gp, "vehicle_ignore_repulsors('Veh1', true)");
        VehicleState& v = es.getOrCreateVehicle("Veh1");
        CHECK((v.vehicleAiForceFlagsByte4.get(VehicleState::kIgnoreRepulsorsBit) != 0));
        ok(host, gp, "vehicle_suppress_flipping('Veh1')"); // default true
        CHECK((v.forceFlags.get(VehicleState::kSuppressFlippingBit1d7d) != 0));
        ok(host, gp, "vehicle_suppress_flipping('Veh1', false)");
        CHECK((v.forceFlags.get(VehicleState::kSuppressFlippingBit1d7d) == 0));
}


    // Player-flags trio (Sec14.3-Sec14.5): negated/negated/not-negated
    // polarity, cross-checked against literal debug-tag census (Sec14.31).
static void test_block_5() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("#PLAYER#", true);
        CharacterState& c = es.getOrCreateCharacter("#PLAYER#");

        ok(host, gp, "set_players_can_be_downed('#PLAYER#', true)"); // "can be downed" -> stored bit CLEAR (negated)
        CHECK((c.statusByte28ad.get(CharacterState::kCantBeDownedBit28ad) == 0));
        ok(host, gp, "set_players_can_be_downed('#PLAYER#', false)"); // "can NOT be downed" -> stored bit SET
        CHECK((c.statusByte28ad.get(CharacterState::kCantBeDownedBit28ad) != 0));

        ok(host, gp, "set_player_can_take_human_shields('#PLAYER#')"); // default true -> stored bit CLEAR (negated)
        CHECK((c.statusByte28ac.get(CharacterState::kCantTakeHumanShieldsBit28ac) == 0));
        ok(host, gp, "set_player_can_take_human_shields('#PLAYER#', false)");
        CHECK((c.statusByte28ac.get(CharacterState::kCantTakeHumanShieldsBit28ac) != 0));

        ok(host, gp, "player_human_shield_lock('#PLAYER#', true)"); // CONFIRMED: no negation
        CHECK((c.statusByte28ad.get(CharacterState::kHumanShieldLockedBit28ad) != 0));
        ok(host, gp, "player_human_shield_lock('#PLAYER#', false)");
        CHECK((c.statusByte28ad.get(CharacterState::kHumanShieldLockedBit28ad) == 0));
}


    // player_script_controlled_clear (Sec14.29): reuses controlLockGatePasses
    // (CONFIRMED true in single player) and clears bit 0x2 of the SAME
    // +0x28ac byte set_player_can_take_human_shields targets bit 0x80 of.
static void test_block_6() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("#PLAYER#", true);
        CharacterState& c = es.getOrCreateCharacter("#PLAYER#");
        c.scriptControlledMode = 7;
        c.statusByte28ac.setBits(CharacterState::kScriptControlledBit28ac, CharacterState::kScriptControlledBit28ac);
        ok(host, gp, "player_script_controlled_clear('#PLAYER#')");
        CHECK(c.scriptControlledMode == 0);
        CHECK((c.statusByte28ac.get(CharacterState::kScriptControlledBit28ac) == 0));
}


    // set_min_hit_points / set_max_hit_points (Sec13.19/Sec13.20): vehicle
    // path, ratio preservation and the min<=max cross-field guarantee.
static void test_block_7() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Car1", true);
        VehicleState& v = es.getOrCreateVehicle("Car1");
        v.maxHitPointsV = 100;
        v.currentHitPointsV = 50; // 50% health
        ok(host, gp, "set_max_hit_points('Car1', 200)"); // default false -> preserve ratio
        CHECK(v.maxHitPointsV == 200);
        CHECK(v.currentHitPointsV == 100); // 50% of 200

        ok(host, gp, "set_max_hit_points('Car1', 10, true)"); // reset current to new max
        CHECK(v.maxHitPointsV == 10);
        CHECK(v.currentHitPointsV == 10);

        ok(host, gp, "set_min_hit_points('Car1', 500)"); // exceeds current max -> raises max to match
        CHECK(v.minHitPointsV == 500);
        CHECK(v.maxHitPointsV == 500);
}


    // set_max_hit_points (Sec13.20): character path, ratio preservation.
static void test_block_8() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Npc1", true);
        CharacterState& c = es.getOrCreateCharacter("Npc1");
        c.maxHitPoints.set(100);
        c.currentHitPoints.set(25); // 25% health
        ok(host, gp, "set_max_hit_points('Npc1', 400)"); // preserve ratio
        CHECK(c.maxHitPoints.get() == 400);
        CHECK(c.currentHitPoints.get() == 100); // 25% of 400
}


    // inv_item_in_slot (Sec13.21): the now-fully-recovered 11-name table
    // (0x008dc6b0), its 2 newly-confirmed dead codes (grenade/single_use),
    // the pre-existing 0x10/0x11 dead-code gap, the "best"/"best-firearm"/
    // "best-melee" test-hook fallback, and a genuine slot-name miss now
    // answering "none" directly instead of refusing (CLOSED 2026-10-09 -
    // see this native's own stub doc comment for the full citation).
static void test_block_9() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Hero", true);
        CharacterState& c = es.getOrCreateCharacter("Hero");
        c.inventoryBestSlotItemForTesting[0xe] = "pistol";   // "best-firearm"
        c.inventoryBestSlotItemForTesting[0x2] = "sawed_off"; // "pistol" slot (table code 2), LIVE

        lua_getglobal(gp, "inv_item_in_slot");
        lua_pushstring(gp, "Hero");
        lua_pushstring(gp, "best-firearm");
        lua_call(gp, 2, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "pistol");
        lua_pop(gp, 1);

        // "pistol" is a table-recovered LIVE slot name (code 0x2), wired
        // into the SAME inventoryBestSlotItemForTesting map.
        lua_getglobal(gp, "inv_item_in_slot");
        lua_pushstring(gp, "Hero");
        lua_pushstring(gp, "pistol");
        lua_call(gp, 2, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "sawed_off");
        lua_pop(gp, 1);

        // "unarmed" (table code 0) is LIVE but never registered -> honest "none".
        lua_getglobal(gp, "inv_item_in_slot");
        lua_pushstring(gp, "Hero");
        lua_pushstring(gp, "unarmed");
        lua_call(gp, 2, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "none");
        lua_pop(gp, 1);

        // "grenade" (table code 9) is CONFIRMED DEAD even with a registration present.
        c.inventoryBestSlotItemForTesting[0x9] = "frag"; // must still be ignored
        lua_getglobal(gp, "inv_item_in_slot");
        lua_pushstring(gp, "Hero");
        lua_pushstring(gp, "grenade");
        lua_call(gp, 2, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "none");
        lua_pop(gp, 1);

        // "single_use" (table code 0xa) is CONFIRMED DEAD for a different
        // reason (never forwarded by this native's own caller-side switch),
        // also ignoring any registration.
        c.inventoryBestSlotItemForTesting[0xa] = "cardboard_tube"; // must still be ignored
        lua_getglobal(gp, "inv_item_in_slot");
        lua_pushstring(gp, "Hero");
        lua_pushstring(gp, "single_use");
        lua_call(gp, 2, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "none");
        lua_pop(gp, 1);

        lua_getglobal(gp, "inv_item_in_slot");
        lua_pushstring(gp, "Hero");
        lua_pushstring(gp, "best-non-lethal"); // CONFIRMED dead-code gap -> always "none"
        lua_call(gp, 2, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "none");
        lua_pop(gp, 1);

        // a character known NOT to resolve -> "none" regardless of slot name
        es.objectResolves().set("Nobody", false);
        lua_getglobal(gp, "inv_item_in_slot");
        lua_pushstring(gp, "Nobody");
        lua_pushstring(gp, "best-firearm");
        lua_call(gp, 2, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "none");
        lua_pop(gp, 1);

        // an unrecognized slot name on a RESOLVED character is now a
        // genuine, CONFIRMED miss (both the 11-name table and the 5
        // "best-*" literals are closed) -> answers "none" directly, no
        // longer refuses (CLOSED 2026-10-09, was OPEN table 0x008dc6b0).
        lua_getglobal(gp, "inv_item_in_slot");
        lua_pushstring(gp, "Hero");
        lua_pushstring(gp, "3");
        lua_call(gp, 2, 1);
        CHECK(std::string(lua_tostring(gp, -1)) == "none");
        lua_pop(gp, 1);
}


    // character_ragdoll_set_last_valid_position (Sec13.25): ring-buffer
    // append vs. full-reset, and character_ragdoll_clear_last_resort_position
    // (Sec13.6) stays a pure global counter with no per-character field.
static void test_block_10() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Char2", true);
        es.objectResolves().set("Dest1", true);
        ok(host, gp, "character_ragdoll_set_last_valid_position('Char2','Dest1', false)");
        CharacterState& c = es.getOrCreateCharacter("Char2");
        CHECK(c.ragdollValidPositionHistory[1] == "Dest1"); // index advances from 0 to 1 (wraps 10)

        es.objectResolves().set("Dest2", true);
        ok(host, gp, "character_ragdoll_set_last_valid_position('Char2','Dest2', true)"); // reset ALL
        for (const auto& slot : c.ragdollValidPositionHistory) CHECK(slot == "Dest2");

        uint64_t before = es.ragdollLastResortPositionClearCount();
        ok(host, gp, "character_ragdoll_clear_last_resort_position()");
        CHECK(es.ragdollLastResortPositionClearCount() == before + 1);
        CHECK(c.ragdollValidPositionHistory[0] == "Dest2"); // unaffected by the (different) clear native
}


    // on_mover_enter_water (Sec14.6) / on_vehicle_enter_water (Sec14.30):
    // hook-slot registration, empty clears.
static void test_block_11() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Mover1", true);
        ok(host, gp, "on_mover_enter_water('cb_mover', 'Mover1')");
        CHECK(es.getOrCreatePlacedObject("Mover1").onMoverEnterWaterCallback == "cb_mover");
        ok(host, gp, "on_mover_enter_water('', 'Mover1')"); // empty clears
        CHECK(es.getOrCreatePlacedObject("Mover1").onMoverEnterWaterCallback.empty());

        es.objectResolves().set("Boat1", true);
        ok(host, gp, "on_vehicle_enter_water('cb_veh', 'Boat1')");
        CHECK(es.getOrCreateVehicle("Boat1").onVehicleEnterWaterCallback == "cb_veh");
}


    // on_ai_do_action_complete (Sec14.7): generic/#PLAYER# match -> do
    // nothing; plain-character fallback -> registers slot.
static void test_block_12() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.playerClassChainResolves().set("#PLAYER#", true);
        ok(host, gp, "on_ai_do_action_complete('cb', '#PLAYER#')");
        CHECK(es.getOrCreateCharacter("#PLAYER#").onAiDoActionCompleteCallback.empty()); // CONFIRMED: does nothing

        es.playerClassChainResolves().set("Npc2", false);
        es.objectResolves().set("Npc2", true);
        ok(host, gp, "on_ai_do_action_complete('cb2', 'Npc2')");
        CHECK(es.getOrCreateCharacter("Npc2").onAiDoActionCompleteCallback == "cb2");
}


    // ai_scripted_action_is_processing (Sec13.7): unresolved -> false;
    // gate-false reuses the SAME +0x4aa lookup ai_scripted_action_is_complete
    // models; gate-true refuses (OPEN table).
static void test_block_13() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        es.objectResolves().set("Nobody", false);
        lua_getglobal(gp, "ai_scripted_action_is_processing");
        lua_pushstring(gp, "Nobody");
        lua_call(gp, 1, 1);
        CHECK(lua_toboolean(gp, -1) == 0);
        lua_pop(gp, 1);

        es.objectResolves().set("Npc3", true);
        CharacterState& c = es.getOrCreateCharacter("Npc3");
        c.scriptedActionDispatchGatePasses.set(false);
        c.scriptedActionLookupNonNull.set(true); // lookup succeeded -> "processing" is the negation -> false
        lua_getglobal(gp, "ai_scripted_action_is_processing");
        lua_pushstring(gp, "Npc3");
        lua_call(gp, 1, 1);
        CHECK(lua_toboolean(gp, -1) == 0);
        lua_pop(gp, 1);

        c.scriptedActionDispatchGatePasses.set(true);
        // Sec13.7 gate polarity (2026-10-12): gate true -> no scripted-action tags modelled -> false, no error.
        ok(host, gp, "assert(ai_scripted_action_is_processing('Npc3') == false)");
}


    // hud_achievement_show_progress (Sec13.22): test-registered roster
    // match, completion counter on current==total.
static void test_block_14() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.knownAchievementsForTesting().insert("ach_demolition");
        ok(host, gp, "hud_achievement_show_progress('ach_demolition', 3, 3)");
        CHECK(es.achievementCompletionCount() == 1);
        CHECK(es.achievementToastCount() == 1);
        CHECK(es.achievementReplicateCount() == 1);
        ok(host, gp, "hud_achievement_show_progress('unknown_ach', 1, 3)"); // no match -> no effect
        CHECK(es.achievementCompletionCount() == 1);
}


    // customization_outfit_wear (Sec13.4) / customization_item_wear
    // (Sec14.12): dual local-player representation, reusing
    // hasLocalPlayer()/playerRig().coopPlayerPresent directly.
static void test_block_15() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.playerRig().coopPlayerPresent.set(false);
        ok(host, gp, "customization_outfit_wear('outfit_biz_casual')");
        CHECK(es.customizationOutfitWearRequests().size() == 1);
        CHECK(es.customizationOutfitWearRequests()[0].secondRepresentationApplied == false);

        es.hasLocalPlayer().set(true);
        es.playerRig().coopPlayerPresent.set(true);
        ok(host, gp, "customization_item_wear('hat', 'red', 'matte')");
        CHECK(es.customizationItemWearRequests().size() == 1);
        CHECK(es.customizationItemWearRequests()[0].appliedPrimary == true);
        CHECK(es.customizationItemWearRequests()[0].appliedSecondRepresentation == true);
}


    // helicopter_shoot_navpoint (Sec14.11) / helicopter_shoot_human
    // (Sec14.18): shared dispatcher outcome, drive-mode-gate asymmetry.
static void test_block_16() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Heli1", true);
        es.objectResolves().set("Nav1", true);
        es.objectResolves().set("Target1", true);
        VehicleState& v = es.getOrCreateVehicle("Heli1");

        // Not in chase mode -> helicopter_shoot_navpoint returns false without even reading the dispatcher.
        lua_getglobal(gp, "helicopter_shoot_navpoint");
        lua_pushstring(gp, "Heli1");
        lua_pushstring(gp, "Nav1");
        lua_call(gp, 2, 1);
        CHECK(lua_toboolean(gp, -1) == 0);
        lua_pop(gp, 1);

        v.aiDriveModeIsChasing = true;
        v.helicopterFireSucceeds.set(true);
        lua_getglobal(gp, "helicopter_shoot_navpoint");
        lua_pushstring(gp, "Heli1");
        lua_pushstring(gp, "Nav1");
        lua_call(gp, 2, 1);
        CHECK(lua_toboolean(gp, -1) == 1);
        lua_pop(gp, 1);

        // helicopter_shoot_human does NOT check the drive-mode gate itself.
        v.aiDriveModeIsChasing = false;
        lua_getglobal(gp, "helicopter_shoot_human");
        lua_pushstring(gp, "Heli1");
        lua_pushstring(gp, "Target1");
        lua_call(gp, 2, 1);
        CHECK(lua_toboolean(gp, -1) == 1); // same dispatcher outcome, gate not checked
        lua_pop(gp, 1);
}


    // mesh_mover_kill (Sec14.10): the +0xdc path's "amount=0 only finalizes
    // an already-<=0 health value" finding.
static void test_block_17() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Mover2", true);
        auto& mover = es.getOrCreatePlacedObject("Mover2");
        mover.hasPlainKillSubObjectDc = true;
        mover.replicateGate027038c0Zero.set(false); // authoritative/local path
        mover.killSubObjectHealth = 50;              // still positive -> no finalize
        ok(host, gp, "mesh_mover_kill('Mover2')");
        CHECK(mover.plainKillFinalizeCount == 0);
        CHECK(mover.killSubObjectHealth == 50); // unchanged - amount is always 0

        mover.killSubObjectHealth = 0; // already at/below zero -> finalizes
        ok(host, gp, "mesh_mover_kill('Mover2')");
        CHECK(mover.plainKillFinalizeCount == 1);
}


    // set_behavior_set (Sec14.27) / ai_set_personality (Sec13.27): test-
    // registration hooks with the CONFIRMED real fallback sentinels.
static void test_block_18() {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Npc4", true);
        ok(host, gp, "set_behavior_set('Npc4', 'unregistered_set')");
        CHECK(es.getOrCreateCharacter("Npc4").behaviorSetIndex == EngineState::kBehaviorSetNotFound);
        es.registerBehaviorSetForTesting("aggressive", 7);
        ok(host, gp, "set_behavior_set('Npc4', 'aggressive')");
        CHECK(es.getOrCreateCharacter("Npc4").behaviorSetIndex == 7);

        ok(host, gp, "ai_set_personality('Npc4', 'unregistered_profile')");
        CHECK(es.getOrCreateCharacter("Npc4").aiPersonalityProfileCode == EngineState::kAiPersonalityProfileFallback);
}

int main() {
    test_block_1();
    test_block_2();
    test_block_3();
    test_block_4();
    test_block_5();
    test_block_6();
    test_block_7();
    test_block_8();
    test_block_9();
    test_block_10();
    test_block_11();
    test_block_12();
    test_block_13();
    test_block_14();
    test_block_15();
    test_block_16();
    test_block_17();
    test_block_18();

    if (g_failures == 0) {
        std::cout << "All sec13_14_batch4 checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
