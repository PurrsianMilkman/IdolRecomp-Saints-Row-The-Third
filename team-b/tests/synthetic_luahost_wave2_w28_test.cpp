// Synthetic tests for wave 2, batch W2-8 (2026-10-08) - spec-lua-api-
// behaviour.md Sec38-60 "gameplay B" (93 names), split across
// src/lua_spec_confirmed_stubs_b19_wave2_char_npc_inv.cpp (56 names,
// "character/NPC/inventory/party") and
// src/lua_spec_confirmed_stubs_b20_wave2_hud_minimap_hooks.cpp (37 names,
// "HUD/minimap/mission-flow/hooks"). One combined test file per the
// dispatch brief's own "or one combined test file if cleaner" option -
// all 93 names are tagged `gameplay` (tools/lua_all_registered_1490_
// tagged.txt), so there is no ui/gameplay split to exercise separately.
// Same hand-written-fixture convention as every other
// synthetic_luahost_*_test.cpp (see tests/synthetic_luahost_b10_batch3_
// test.cpp for the established Host/ok/fails/CHECK pattern this file
// copies).
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
    auto r = host.runChunk(L, code, "wave2_w28_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "wave2_w28_test.lua");
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

// All 93 names this batch registers - every one tagged `gameplay`
// (tools/lua_all_registered_1490_tagged.txt).
std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"group_is_loaded_internal", "gameplay"},
        {"group_show_do", "gameplay"},
        {"group_hide_do", "gameplay"},
        {"players_naked", "gameplay"},
        {"melee_get_move_id_from_name", "gameplay"},
        {"player_parachute_has_backpack", "gameplay"},
        {"set_cower_variant", "gameplay"},
        {"set_unjackable_by_ai_flag", "gameplay"},
        {"set_unrecruitable_flag", "gameplay"},
        {"player_override_set_sprint", "gameplay"},
        {"player_override_set_sprint_recharge", "gameplay"},
        {"player_override_set_sprint_delay", "gameplay"},
        {"player_override_clear_sprint", "gameplay"},
        {"player_override_clear_sprint_recharge", "gameplay"},
        {"player_override_clear_sprint_delay", "gameplay"},
        {"players_in_weird_camera_mode", "gameplay"},
        {"npc_brute_always_throws", "gameplay"},
        {"npc_detection_enable", "gameplay"},
        {"npc_weapon_pickup_override", "gameplay"},
        {"override_npc_run_and_cower_run", "gameplay"},
        {"party_use_melee_weapons_only", "gameplay"},
        {"get_char_skydiving_state", "gameplay"},
        {"human_has_taken_non_melee_damage_from_player", "gameplay"},
        {"inv_item_add_ammo", "gameplay"},
        {"inv_item_add_do", "gameplay"},
        {"inv_item_get_weapon_upgrades", "gameplay"},
        {"character_set_cannot_exit_rc_vehicle", "gameplay"},
        {"ai_force_team_idle", "gameplay"},
        {"notoriety_get", "gameplay"},
        {"set_attack_peds_flag", "gameplay"},
        {"perform_killswitch", "gameplay"},
        {"player_action_is_pressed", "gameplay"},
        {"player_get_custom_voice", "gameplay"},
        {"player_take_human_shield_do", "gameplay"},
        {"npc_aim_at_point", "gameplay"},
        {"npc_brute_set_no_downed_finisher", "gameplay"},
        {"npc_dont_auto_equip_or_unequip_weapons", "gameplay"},
        {"npc_is_in_brute_grab_qte", "gameplay"},
        {"party_dismiss_do", "gameplay"},
        {"human_being_revived", "gameplay"},
        {"human_being_revived_by", "gameplay"},
        {"human_downed_set_mars_kb_interactable", "gameplay"},
        {"inv_item_dual_wield", "gameplay"},
        {"inv_item_get_anim_group", "gameplay"},
        {"inv_item_get_weapon_slot", "gameplay"},
        {"follower_is_unconscious", "gameplay"},
        {"character_check_resource_loaded", "gameplay"},
        {"character_is_combat_ready", "gameplay"},
        {"character_is_on_fire", "gameplay"},
        {"character_parachute_open", "gameplay"},
        {"character_parachute_play_action", "gameplay"},
        {"character_prevent_kneecapping", "gameplay"},
        {"character_set_over_the_shoulder", "gameplay"},
        {"character_take_human_shield_do", "gameplay"},
        {"character_use_synced_finishers", "gameplay"},
        {"action_play_custom_do", "gameplay"},
        {"tutorial_get_case", "gameplay"},
        {"tutorial_visible", "gameplay"},
        {"minimap_icon_add_script_group_do", "gameplay"},
        {"minimap_icon_remove_script_group_do", "gameplay"},
        {"mission_collectible_collected", "gameplay"},
        {"mission_help_clear", "gameplay"},
        {"mission_help_table_do", "gameplay"},
        {"object_indicator_add_script_group_do", "gameplay"},
        {"object_indicator_remove_script_group_do", "gameplay"},
        {"on_dismount", "gameplay"},
        {"on_hit_ped", "gameplay"},
        {"on_m21_player_choice", "gameplay"},
        {"on_mount", "gameplay"},
        {"on_vehicle_debris_flow_recycled", "gameplay"},
        {"open_vint_dialog_check_done", "gameplay"},
        {"hud_get_text_adventure_current_screen_index", "gameplay"},
        {"hud_x_of_y_remove", "gameplay"},
        {"flashpoint_mission_status", "gameplay"},
        {"action_sequence_advance", "gameplay"},
        {"action_sequence_is_playing", "gameplay"},
        {"action_sequence_setup", "gameplay"},
        {"tutorial_stop_current", "gameplay"},
        {"qte_start_m02_skyqte_01", "gameplay"},
        {"message_remove", "gameplay"},
        {"minimap_icon_add_radius_do", "gameplay"},
        {"minimap_icon_remove_radius_do", "gameplay"},
        {"mission_maybe_uncomplete_m22", "gameplay"},
        {"open_vint_dialog_do", "gameplay"},
        {"homie_mission_unlock", "gameplay"},
        {"hud_get_proto_selection", "gameplay"},
        {"hud_pop_screen", "gameplay"},
        {"hud_x_of_y_add", "gameplay"},
        {"hud_x_of_y_update", "gameplay"},
        {"homie_mission_lock", "gameplay"},
        {"flashpoint_start_mission", "gameplay"},
        {"add_object_indicator_to_closest_npc", "gameplay"},
        {"on_mission_item_drop", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 93 names land as real functions in gameplayState,
    // and nowhere in uiState.
    {
        Host host(batchFixtureNames());
        for (const auto& rn : batchFixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            CHECK(!hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // objectResolves() is an OpenValueMap: a name is OPEN (throws
    // OpenStateError) until explicitly .set() one way or the other - a
    // name this test uses as "known unresolved" must be set to false
    // explicitly, same convention every other synthetic_luahost_*_test.cpp
    // already follows.
    es.objectResolves().set("Nobody", false);
    es.objectResolves().set("NoSuchPartner", false);

    // ==================================================================
    // b19: character/NPC/inventory/party.
    // ==================================================================

    // group_is_loaded_internal: false until group_create_do's own registry
    // marks the group created.
    ok(host, gp, "assert(group_is_loaded_internal('G1') == false)");

    // group_show_do/group_hide_do: REVIEW FIX 2026-10-08 - genuinely
    // existence-gated now (the doc comment always claimed this, a prior
    // version of the code didn't actually check it). A never-created
    // group is refused, no member model to crash against (CRASH-flagged
    // in the real engine).
    ok(host, gp, "group_hide_do('G1')");
    CHECK(es.groupHideOnUncreatedGuardCount() == 1);
    ok(host, gp, "group_show_do('G1')");
    CHECK(es.groupShowOnUncreatedGuardCount() == 1);

    es.markGroupCreated("G1");
    ok(host, gp, "assert(group_is_loaded_internal('G1') == true)");
    ok(host, gp, "group_hide_do('G1')");
    CHECK(es.w28GroupVisibility("G1").hidden == true);
    CHECK(es.w28GroupVisibility("G1").hideCount == 1);
    CHECK(es.groupHideOnUncreatedGuardCount() == 1); // unchanged, group now exists
    ok(host, gp, "group_show_do('G1')");
    CHECK(es.w28GroupVisibility("G1").hidden == false);
    CHECK(es.groupShowOnUncreatedGuardCount() == 1); // unchanged, group now exists

    // players_naked: local always, coop only if present.
    ok(host, gp, "players_naked(true)");
    CHECK(es.localPlayerNaked() == true);
    CHECK(es.coopPlayerNaked() == false); // no coop partner known

    // melee_get_move_id_from_name: always the CONFIRMED 65535 sentinel.
    ok(host, gp, "assert(melee_get_move_id_from_name('any_move') == 65535)");

    // player_parachute_has_backpack: reuses the existing CharacterState
    // field directly.
    es.objectResolves().set("#PLAYER1#", true);
    ok(host, gp, "assert(player_parachute_has_backpack() == false)");
    es.getOrCreateCharacter("#PLAYER1#").parachuteBackpackWorn = true;
    ok(host, gp, "assert(player_parachute_has_backpack('#PLAYER1#') == true)");

    // set_cower_variant: clamps to [0,4], double-gated.
    es.objectResolves().set("Bob", true);
    ok(host, gp, "set_cower_variant('Bob', 99)");
    CHECK(es.w28CharacterMisc("Bob").cowerVariant == 4); // clamped

    // set_unjackable_by_ai_flag / set_unrecruitable_flag: double-gate
    // setters on their own real bits.
    es.objectResolves().set("Car1", true);
    ok(host, gp, "set_unjackable_by_ai_flag('Car1', true)");
    CHECK(es.getOrCreateVehicle("Car1").vehicleAiForceFlags.get(0x02) == 0x02u);
    ok(host, gp, "set_unrecruitable_flag('Bob', true)");
    CHECK((es.w28CharacterMisc("Bob").forceFlags1e02 & 0x10) != 0);

    // player_override_* sprint family: GLOBAL, no presence check, clear
    // writes -1.
    ok(host, gp, "player_override_set_sprint(2.5)");
    CHECK(es.sprintUseOverride() == 2.5);
    ok(host, gp, "player_override_clear_sprint()");
    CHECK(es.sprintUseOverride() == -1.0);
    ok(host, gp, "player_override_set_sprint_recharge(1.0); player_override_set_sprint_delay(0.5)");
    CHECK(es.sprintRechargeOverride() == 1.0);
    CHECK(es.sprintDelayOverride() == 0.5);
    ok(host, gp, "player_override_clear_sprint_recharge(); player_override_clear_sprint_delay()");
    CHECK(es.sprintRechargeOverride() == -1.0);
    CHECK(es.sprintDelayOverride() == -1.0);

    // players_in_weird_camera_mode: always false in this project's scope.
    ok(host, gp, "assert(players_in_weird_camera_mode() == false)");

    // npc_brute_always_throws/npc_detection_enable/npc_weapon_pickup_override:
    // double-gated flag setters, raw args stored.
    ok(host, gp, "npc_brute_always_throws('Bob')"); // default true
    CHECK(es.w28CharacterMisc("Bob").bruteAlwaysThrows == true);
    ok(host, gp, "npc_detection_enable('Bob', true)");
    CHECK(es.w28CharacterMisc("Bob").npcDetectionEnabled == true);
    ok(host, gp, "npc_weapon_pickup_override('Bob', true)");
    CHECK(es.w28CharacterMisc("Bob").npcWeaponPickupOverrideArg == true);

    // override_npc_run_and_cower_run: unknown names -> -1, no replication.
    ok(host, gp, "override_npc_run_and_cower_run('Bob', 'run_fast', 'cower_fast')");
    CHECK(es.w28CharacterMisc("Bob").runAnimOverride == -1);
    CHECK(es.w28CharacterMisc("Bob").cowerRunAnimOverride == -1);

    // party_use_melee_weapons_only: local only (no coop partner known).
    ok(host, gp, "party_use_melee_weapons_only(true)");
    CHECK(es.partyMeleeOnlyLocal() == true);
    CHECK(es.partyMeleeOnlyCoop() == false);

    // get_char_skydiving_state: (state,-1) resolved, (-1) unresolved.
    ok(host, gp, "local a,b = get_char_skydiving_state('Bob'); assert(a == 255 and b == -1)");
    ok(host, gp, "local a,b = get_char_skydiving_state('Nobody'); assert(a == -1 and b == nil)");

    // human_has_taken_non_melee_damage_from_player: false, no setter in scope.
    ok(host, gp, "assert(human_has_taken_non_melee_damage_from_player('Bob') == false)");

    // inv_item_add_ammo/inv_item_add_do: accumulate per character/item.
    ok(host, gp, "inv_item_add_ammo('Bob', 'ar', 30)");
    CHECK(es.inventoryAmmo()["Bob"]["ar"] == 30);
    ok(host, gp, "inv_item_add_do('shotgun')"); // defaults: count 1, character #PLAYER1#
    CHECK(es.inventoryItemCounts()["#PLAYER1#"]["shotgun"] == 1);

    // inv_item_get_weapon_upgrades: (-1) alone for an unknown item.
    ok(host, gp, "local lvl, sentinel = inv_item_get_weapon_upgrades('shotgun'); assert(lvl == -1 and sentinel == nil)");

    // character_set_cannot_exit_rc_vehicle: double-gate on flags1c9c bit 9.
    ok(host, gp, "character_set_cannot_exit_rc_vehicle('Bob', true)");
    CHECK(es.getOrCreateCharacter("Bob").flags1c9c.get(0x200) == 0x200u);

    // ai_force_team_idle: HARDENED non-string guard + sentinel-9 scope
    // widening (replicated, not hardened away).
    ok(host, gp, "ai_force_team_idle(nil)");
    CHECK(es.aiForceTeamIdleNullNameGuardCount() == 1);
    ok(host, gp, "ai_force_team_idle('unknown_team')"); // resolves to sentinel 9 -> idles every team
    for (bool b : es.teamIdleFlags()) CHECK(b == true);

    // notoriety_get: zero Lua values for an unknown track; HARDENED
    // non-string guard also takes the zero-values path.
    ok(host, gp, "local v = notoriety_get('unknown_track'); assert(v == nil)");
    ok(host, gp, "local v = notoriety_get(nil); assert(v == nil)");
    CHECK(es.notorietyGetNonStringArgGuardCount() == 1);
    es.registerNotorietyTrackCodeForTesting("cash", 1);
    es.notorietyTrackValues()[1] = 42.0;
    ok(host, gp, "assert(notoriety_get('cash') == 42.0)");

    // set_attack_peds_flag: double-gated plain flag.
    ok(host, gp, "set_attack_peds_flag('Bob', true)");
    CHECK(es.w28CharacterMisc("Bob").attackPedsFlag == true);

    // perform_killswitch: HARDENED guard when "M21_Killbane" doesn't exist.
    es.objectResolves().set("Vic1", true);
    es.objectResolves().set("Kill1", true);
    es.objectResolves().set("M21_Killbane", false);
    ok(host, gp, "perform_killswitch('Vic1', 'Kill1')");
    CHECK(es.performKillswitchMissingKillbaneGuardCount() == 1);
    es.objectResolves().set("M21_Killbane", true);
    ok(host, gp, "perform_killswitch('Vic1', 'Kill1')");
    CHECK(es.performKillswitchMissingKillbaneGuardCount() == 1); // unchanged, now resolves

    // player_action_is_pressed: CHOSEN false default, test-settable.
    ok(host, gp, "assert(player_action_is_pressed('jump') == false)");
    es.actionPressedFlags()["jump"] = true;
    ok(host, gp, "assert(player_action_is_pressed('jump') == true)");

    // player_get_custom_voice: HARDENED -1 for unresolved, counted.
    ok(host, gp, "assert(player_get_custom_voice('Nobody') == -1)");
    CHECK(es.playerGetCustomVoiceUnresolvedGuardCount() == 1);
    ok(host, gp, "assert(player_get_custom_voice('Bob') == -1)"); // resolved, no custom voice set

    // player_take_human_shield_do: optimistic-true local shape.
    es.objectResolves().set("Taker1", true);
    es.objectResolves().set("Victim1", true);
    ok(host, gp, "assert(player_take_human_shield_do('Taker1', 'Victim1') == true)");
    CHECK(es.getOrCreateCharacter("Taker1").humanShieldHostageName == "Victim1");

    // npc_aim_at_point: pure no-op, 0 return values.
    ok(host, gp, "npc_aim_at_point()");

    // npc_brute_set_no_downed_finisher/npc_dont_auto_equip_or_unequip_weapons/
    // npc_is_in_brute_grab_qte.
    ok(host, gp, "npc_brute_set_no_downed_finisher('Bob', true)");
    CHECK(es.w28CharacterMisc("Bob").bruteNoDownedFinisher == true);
    ok(host, gp, "npc_dont_auto_equip_or_unequip_weapons('Bob', true)");
    CHECK(es.w28CharacterMisc("Bob").dontAutoEquipUnequipWeapons == true);
    ok(host, gp, "assert(npc_is_in_brute_grab_qte('Bob') == false)");

    // party_dismiss_do: table required (raises a Lua error otherwise);
    // dismisses each resolved non-player character.
    fails(host, gp, "party_dismiss_do('not_a_table')", "table");
    es.playerClassChainResolves().set("#PLAYER1#", true);
    es.playerClassChainResolves().set("Bob", false); // OPEN until set, same convention as objectResolves()
    ok(host, gp, "party_dismiss_do({n=1, [1]='Bob'})");
    CHECK(es.squadMember("Bob").inRoster == false);
    CHECK(es.squadMember("Bob").dismissCount == 1);

    // human_being_revived/_by: false/nil defaults; SP literal once set.
    ok(host, gp, "assert(human_being_revived('Bob') == false)");
    ok(host, gp, "assert(human_being_revived_by('Bob') == nil)");
    es.revivedByLocalPlayerFlags()["Bob"] = true;
    ok(host, gp, "assert(human_being_revived_by('Bob') == '#NETID#0')");

    // human_downed_set_mars_kb_interactable: no repeat-call guard - leaks.
    ok(host, gp, "human_downed_set_mars_kb_interactable('Bob', 7)");
    CHECK(es.marsKbInteractableLeakCount() == 0);
    ok(host, gp, "human_downed_set_mars_kb_interactable('Bob', 8)");
    CHECK(es.marsKbInteractableLeakCount() == 1); // CONFIRMED: overwrote without clearing first

    // inv_item_dual_wield/_get_anim_group/_get_weapon_slot: sentinels
    // "Default"/255.
    ok(host, gp, "inv_item_dual_wield('Bob', true)");
    CHECK(es.dualWieldFlags()["Bob"] == true);
    ok(host, gp, "assert(inv_item_get_anim_group('unknown_item') == 'Default')");
    ok(host, gp, "assert(inv_item_get_weapon_slot('unknown_item') == 255)");

    // follower_is_unconscious: opposite polarity from human_is_downed.
    ok(host, gp, "assert(follower_is_unconscious('Nobody') == true)"); // unresolved -> true
    ok(host, gp, "assert(follower_is_unconscious('Bob') == false)"); // resolved, not downed (lifeState seeded 0)

    // character_check_resource_loaded/_is_on_fire/_parachute_play_action
    // (AGG): genuinely OPEN, refuse honestly.
    fails(host, gp, "character_check_resource_loaded('Bob')", "Sec57.5");
    fails(host, gp, "character_is_on_fire('Bob')", "Sec57.5");
    fails(host, gp, "character_parachute_play_action('Bob')", "Sec57.5");

    // character_is_combat_ready: nil (not false) for unresolved; CHOSEN
    // false default for resolved.
    ok(host, gp, "assert(character_is_combat_ready('Nobody') == nil)");
    ok(host, gp, "assert(character_is_combat_ready('Bob') == false)");

    // character_parachute_open: resource-leak counter, never reused.
    ok(host, gp, "character_parachute_open('Bob'); character_parachute_open('Bob')");
    CHECK(es.parachuteOpenSpawnCount() == 2);

    // character_prevent_kneecapping: omitted arg clears (default false).
    ok(host, gp, "character_prevent_kneecapping('Bob')");
    CHECK(es.getOrCreateCharacter("Bob").forceFlags1c98.get(0x8) == 0u);
    ok(host, gp, "character_prevent_kneecapping('Bob', true)");
    CHECK(es.getOrCreateCharacter("Bob").forceFlags1c98.get(0x8) == 0x8u);

    // character_set_over_the_shoulder: defaults on.
    ok(host, gp, "character_set_over_the_shoulder('Bob')");
    CHECK(es.getOrCreateCharacter("Bob").forceFlags1c98.get(0x400000) == 0x400000u);

    // character_take_human_shield_do: no range/self/busy test.
    ok(host, gp, "character_take_human_shield_do('Bob', 'Bob')"); // self-target allowed
    CHECK(es.getOrCreateCharacter("Bob").humanShieldHostageName == "Bob");

    // character_use_synced_finishers: double-gate on flags1c9c bit 15.
    ok(host, gp, "character_use_synced_finishers('Bob', true)");
    CHECK(es.getOrCreateCharacter("Bob").flags1c9c.get(0x8000) == 0x8000u);

    // action_play_custom_do: HARDENED unresolved -> 0; dead -> -1; live -> 1.
    ok(host, gp, "assert(action_play_custom_do('Nobody', 'anim') == 0)");
    CHECK(es.actionPlayCustomDoUnresolvedGuardCount() == 1);
    ok(host, gp, "assert(action_play_custom_do('Bob', 'anim') == 1)");
    es.getOrCreateCharacter("Bob").lifeState.set(5); // dead
    ok(host, gp, "assert(action_play_custom_do('Bob', 'anim') == -1)");

    // ==================================================================
    // b20: HUD/minimap/mission-flow/hooks.
    // ==================================================================

    // tutorial_get_case/_visible: OPEN until set, matching the existing
    // tutorial_advance infra.
    ok(host, gp, "assert(tutorial_get_case('not_a_real_tutorial_name_xyz') == false)");
    fails(host, gp, "tutorial_visible()", "Sec38.2");

    // minimap_icon_add_script_group_do/_remove_script_group_do: HARDENED
    // guards (bad group / bad icon name / bad flags).
    ok(host, gp, "minimap_icon_add_script_group_do('NoSuchGroup', 'icon')");
    CHECK(es.minimapIconAddScriptGroupGuardCount() == 1);
    es.markGroupCreated("SG1");
    ok(host, gp, "minimap_icon_add_script_group_do('SG1', 'icon', nil, nil, 4)"); // flags out of range
    CHECK(es.minimapIconAddScriptGroupGuardCount() == 2);
    ok(host, gp, "minimap_icon_add_script_group_do('SG1', 'icon')"); // valid
    CHECK(es.minimapIconAddScriptGroupGuardCount() == 2);
    ok(host, gp, "minimap_icon_remove_script_group_do('NoSuchGroup')");
    CHECK(es.minimapIconRemoveScriptGroupGuardCount() == 1);

    // mission_collectible_collected: always true (no data table in scope).
    ok(host, gp, "assert(mission_collectible_collected() == true)");

    // mission_help_clear/_table_do: no-ops beyond stack shape.
    ok(host, gp, "mission_help_clear()");
    ok(host, gp, "mission_help_table_do('key', true, false, 'a', 'b', 1, 2, 3, true, false)");

    // object_indicator_add_script_group_do: HARDENED guard on a bad group.
    ok(host, gp, "object_indicator_add_script_group_do('NoSuchGroup', 1, 2)");
    CHECK(es.objectIndicatorAddScriptGroupGuardCount() == 1);
    ok(host, gp, "object_indicator_add_script_group_do('SG1', 1, 2)");
    CHECK(es.objectIndicatorAddScriptGroupGuardCount() == 1);
    ok(host, gp, "object_indicator_remove_script_group_do('SG1')");

    // on_dismount/on_hit_ped/on_mount/on_vehicle_debris_flow_recycled/
    // on_m21_player_choice: register-only, empty clears.
    ok(host, gp, "on_dismount('cb1', 'Bob')");
    CHECK(es.onDismountCallbacks()["Bob"] == "cb1");
    ok(host, gp, "on_dismount('', 'Bob')");
    CHECK(es.onDismountCallbacks()["Bob"].empty());
    ok(host, gp, "on_hit_ped('cb2', 'Bob')");
    CHECK(es.onHitPedCharacterCallbacks()["Bob"] == "cb2");
    ok(host, gp, "on_vehicle_debris_flow_recycled('cb3', 'Car1')");
    CHECK(es.onVehicleDebrisFlowRecycledCallbacks()["Car1"] == "cb3");
    ok(host, gp, "on_m21_player_choice('cb4')");
    CHECK(es.onM21PlayerChoiceCallback() == "cb4");
    // on_mount: generic/#PLAYER# match -> silent no-op (CONFIRMED quirk).
    es.playerClassChainResolves().set("#PLAYER#", true);
    ok(host, gp, "on_mount('cb5', '#PLAYER#')");
    CHECK(es.onMountCallbacks().count("#PLAYER#") == 0);
    es.playerClassChainResolves().set("Car1", false); // OPEN until set, same convention as objectResolves()
    ok(host, gp, "on_mount('cb5', 'Car1')");
    CHECK(es.onMountCallbacks()["Car1"] == "cb5");
    CHECK(es.onHookNamePool().ids.count("cb5") == 1);

    // open_vint_dialog_check_done/_do: shared -2/-1 completion mechanism.
    ok(host, gp, "assert(open_vint_dialog_check_done() == -2)");
    ok(host, gp, "open_vint_dialog_do()");
    CHECK(es.openVintDialogDoCount() == 1);
    es.vintDialogResult() = -1;
    ok(host, gp, "assert(open_vint_dialog_check_done() == -1)");

    // hud_get_text_adventure_current_screen_index: consume-once.
    es.hudTextAdventureScreenIndex() = 3;
    ok(host, gp, "assert(hud_get_text_adventure_current_screen_index() == 3)");
    ok(host, gp, "assert(hud_get_text_adventure_current_screen_index() == -1)");

    // hud_x_of_y_add/_update/_remove: 0-3 bound, HARDENED update/remove.
    ok(host, gp, "hud_x_of_y_add(1, 2, 10)");
    CHECK(es.hudXOfYSlots()[1].current == 2.0);
    ok(host, gp, "hud_x_of_y_add(9, 0, 0)"); // out of range
    CHECK(es.hudXOfYAddOobGuardCount() == 1);
    ok(host, gp, "hud_x_of_y_update(1, 5)");
    CHECK(es.hudXOfYSlots()[1].current == 5.0);
    ok(host, gp, "hud_x_of_y_update(99, 5)"); // HARDENED, would be a wild write
    CHECK(es.hudXOfYUpdateOobGuardCount() == 1);
    ok(host, gp, "hud_x_of_y_remove(99)"); // HARDENED, would be a wild indirect call
    CHECK(es.hudXOfYRemoveOobGuardCount() == 1);
    ok(host, gp, "hud_x_of_y_remove(1)");
    CHECK(es.hudXOfYSlots()[1].active == false);

    // flashpoint_mission_status/_start_mission.
    ok(host, gp, "flashpoint_start_mission(5)");
    CHECK(es.flashpointState().activeId == "5");
    ok(host, gp, "assert(flashpoint_mission_status() == 2)"); // started, local==peer in SP

    // action_sequence_setup/_is_playing/_advance.
    ok(host, gp, "action_sequence_setup('Car1')");
    CHECK(es.actionSequenceScript().active == true);
    CHECK(es.actionSequenceScript().controlLockLeakCount == 1);
    es.hasLocalPlayer().forget(); // CLOSED 2026-10-09: now CONFIRMED-true by default (Sec10.3) - force "not known" to exercise this guard
    ok(host, gp, "assert(action_sequence_is_playing() == true)");
    CHECK(es.actionSequenceIsPlayingNullPlayerGuardCount() == 1); // hasLocalPlayer() not known true
    es.hasLocalPlayer().set(true);
    ok(host, gp, "action_sequence_is_playing()");
    CHECK(es.actionSequenceIsPlayingNullPlayerGuardCount() == 1); // unchanged, now has a local player
    ok(host, gp, "action_sequence_advance()");
    CHECK(es.actionSequenceScript().advanceCount == 1);
    ok(host, gp, "action_sequence_setup()"); // no vehicle name -> clears in single player
    CHECK(es.actionSequenceScript().active == false);

    // tutorial_stop_current: call-count only.
    ok(host, gp, "tutorial_stop_current()");
    CHECK(es.tutorialStopCurrentCount() == 1);

    // qte_start_m02_skyqte_01: REVIEW FIX 2026-10-08 - broadened guard.
    // Sec51.1: "a nil, misspelled, despawned, or dead partner argument
    // crashes" - a prior version of this guard only covered "present but
    // fails to resolve" (misspelled/despawned), silently falling through
    // for an omitted/nil partner (the common case, since this argument is
    // optional) and never checking a resolved-but-dead partner at all.
    es.objectResolves().set("Sky1", true);
    ok(host, gp, "qte_start_m02_skyqte_01('Sky1', 'cb', 'NoSuchPartner')"); // present, unresolved
    CHECK(es.qteSkyqte01NullPartnerGuardCount() == 1);
    ok(host, gp, "qte_start_m02_skyqte_01('Sky1', 'cb')"); // omitted partner - NOW ALSO refused
    CHECK(es.qteSkyqte01NullPartnerGuardCount() == 2);
    CHECK(es.qteSkyqte01StartCount() == 0); // neither guarded call started a QTE
    es.objectResolves().set("SkyDeadPartner", true);
    es.getOrCreateCharacter("SkyDeadPartner").lifeState.set(5); // dead
    ok(host, gp, "qte_start_m02_skyqte_01('Sky1', 'cb', 'SkyDeadPartner')"); // resolves, but dead
    CHECK(es.qteSkyqte01DeadPartnerGuardCount() == 1);
    CHECK(es.qteSkyqte01StartCount() == 0);
    es.objectResolves().set("SkyLivePartner", true); // resolves, default-alive (seeded at construction)
    ok(host, gp, "qte_start_m02_skyqte_01('Sky1', 'cb', 'SkyLivePartner')");
    CHECK(es.qteSkyqte01StartCount() == 1);
    CHECK(es.qteSkyqte01NullPartnerGuardCount() == 2); // unchanged by the live-partner call
    CHECK(es.qteSkyqte01DeadPartnerGuardCount() == 1); // unchanged by the live-partner call

    // message_remove: safe call-counter.
    ok(host, gp, "message_remove(1)");
    CHECK(es.messageRemoveCount() == 1);

    // minimap_icon_add_radius_do/_remove_radius_do: HARDENED guard /
    // byte-for-byte reuse of minimap_icon_remove_do's own CharacterState field.
    ok(host, gp, "minimap_icon_add_radius_do('Nobody')");
    CHECK(es.minimapIconAddRadiusGuardCount() == 1);
    ok(host, gp, "minimap_icon_add_radius_do('Bob', 9)"); // selector out of range
    CHECK(es.minimapIconAddRadiusGuardCount() == 2);
    ok(host, gp, "minimap_icon_add_radius_do('Bob')");
    CHECK(es.getOrCreateCharacter("Bob").minimapIcons.size() == 1);
    ok(host, gp, "minimap_icon_remove_radius_do('Bob')");
    CHECK(es.getOrCreateCharacter("Bob").minimapIcons.empty());

    // mission_maybe_uncomplete_m22: call-count only.
    ok(host, gp, "mission_maybe_uncomplete_m22()");
    CHECK(es.missionMaybeUncompleteM22Count() == 1);

    // homie_mission_unlock/_lock: override flags, no restore accessor.
    ok(host, gp, "homie_mission_unlock('hm1')");
    CHECK(es.homieMissionOverride()["hm1"] == 2);
    ok(host, gp, "homie_mission_lock('hm1')");
    CHECK(es.homieMissionOverride()["hm1"] == 1);

    // hud_get_proto_selection: sticky (not consume-once).
    es.hudProtoSelection() = 7;
    ok(host, gp, "assert(hud_get_proto_selection() == 7)");
    ok(host, gp, "assert(hud_get_proto_selection() == 7)"); // still 7, not reset

    // hud_pop_screen: only pops when on top, always broadcasts (no Lua-
    // visible broadcast effect to assert, call-shape only).
    es.hudScreenStack().push_back("ScreenA");
    ok(host, gp, "hud_pop_screen('NotOnTop')");
    CHECK(es.hudScreenStack().size() == 1); // unchanged
    ok(host, gp, "hud_pop_screen('ScreenA')");
    CHECK(es.hudScreenStack().empty());

    // add_object_indicator_to_closest_npc: always "" (no membership roster).
    ok(host, gp, "assert(add_object_indicator_to_closest_npc('SG1', 'Bob') == '')");

    // on_mission_item_drop: NOT reset on empty argument (unlike every
    // on_* sibling above), unconditional broadcast every call.
    ok(host, gp, "on_mission_item_drop('drop_cb')");
    CHECK(es.onMissionItemDropCallback() == "drop_cb");
    ok(host, gp, "on_mission_item_drop()"); // no argument
    CHECK(es.onMissionItemDropCallback() == "drop_cb"); // CONFIRMED: unchanged, not cleared
    CHECK(es.onHookNamePool().ids.count("drop_cb") == 1);

    // ------------------------------------------------------------------
    // REVIEW FIX 2026-10-08: HookNamePool::intern()'s capacity sentinel.
    // Sec22.11 (0x00a1fcc0's own exhaustion branch): "-1 on exhaustion."
    // Sec42.7 (the SAME shared pool, a different finding): a separate
    // string-allocation-failure path stores sentinel 0 instead - a
    // genuinely distinct real failure mode this project doesn't model
    // (nothing here simulates std::string/unordered_map allocation
    // failing). A prior version of intern() merged the two, returning 0
    // at capacity instead of -1. Exercised directly against a standalone
    // pool instance (not through es.onHookNamePool(), to avoid perturbing
    // the shared on_* hook names already registered above).
    // ------------------------------------------------------------------
    {
        EngineState::HookNamePool pool;
        for (size_t i = 0; i < EngineState::HookNamePool::kCapacity; ++i) {
            int32_t id = pool.intern("w28_hookpool_name_" + std::to_string(i));
            CHECK(id == static_cast<int32_t>(i) + 1); // sequential ids starting at 1
        }
        CHECK(pool.ids.size() == EngineState::HookNamePool::kCapacity);
        CHECK(pool.exhaustionCount == 0);
        // Pool is now at capacity: a further NEW name is the exhaustion
        // case and must return -1 (Sec22.11), never 0 (Sec42.7's own
        // DIFFERENT sentinel, for a failure mode this pool never models).
        CHECK(pool.intern("one_name_too_many") == -1);
        CHECK(pool.exhaustionCount == 1);
        CHECK(pool.intern("yet_another_new_name") == -1); // repeat exhaustion keeps returning -1
        CHECK(pool.exhaustionCount == 2);
        // Exhaustion never disturbs already-interned names or their ids.
        CHECK(pool.intern("w28_hookpool_name_0") == 1);
        CHECK(pool.intern("w28_hookpool_name_349") == 350);
        CHECK(pool.ids.size() == EngineState::HookNamePool::kCapacity); // unchanged, no growth past capacity
    }

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_wave2_w28_test: all checks passed\n";
    } else {
        std::cerr << "synthetic_luahost_wave2_w28_test: " << g_failures << " check(s) failed\n";
    }
    return g_failures == 0 ? 0 : 1;
}
