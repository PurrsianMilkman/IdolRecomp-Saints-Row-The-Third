// Synthetic tests for wave 1, Batch 5 of the 7-agent parallel fan-out
// (2026-10-08): spec-lua-api-behaviour.md Sec15 (25 names) + Sec16 (16
// names), 41 natives total - see
// src/lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp and
// include/sr3luahost/engine_state.h for the full per-function reasoning.
// Same convention as tests/synthetic_luahost_batch2728_test.cpp: a small,
// hand-written fixture, real cluster tags grepped directly from
// tools/lua_all_registered_1490_tagged.txt (all 25 Sec15 names are
// `gameplay`, all 16 Sec16 names are `ui`).
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
        }                                                                                     \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "batch5_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "batch5_test.lua");
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

std::vector<RegisteredName> batchFixtureNames() {
    return {
        // Sec15 - all `gameplay` (grepped, tools/lua_all_registered_1490_tagged.txt).
        {"character_set_damage_multiplier", "gameplay"},
        {"vehicle_anim_start", "gameplay"},
        {"set_always_cower_flag", "gameplay"},
        {"refraction_situation_clear", "gameplay"},
        {"qte_cleanup", "gameplay"},
        {"persona_clear_global_situation_override", "gameplay"},
        {"on_weapon_pickup", "gameplay"},
        {"mission_unlock", "gameplay"},
        {"mission_set_completed", "gameplay"},
        {"message", "gameplay"},
        {"hud_display_remove_state", "gameplay"},
        {"follower_remain_in_car", "gameplay"},
        {"character_is_ragdolled", "gameplay"},
        {"vehicle_skydiving_start", "gameplay"},
        {"vehicle_is_tank", "gameplay"},
        {"skydive_set_player_dive_fall_speed", "gameplay"},
        {"set_unenterable_for_player", "gameplay"},
        {"set_player_undercover_team", "gameplay"},
        {"player_actions_enable", "gameplay"},
        {"npc_leash_to_object", "gameplay"},
        {"hud_display_create_state", "gameplay"},
        {"hud_display_commit_state", "gameplay"},
        {"game_choice_set_state", "gameplay"},
        {"dlc_achievement_counter_set", "gameplay"},
        {"character_parachute_detach", "gameplay"},
        // Sec16 - all `ui`.
        {"game_get_language", "ui"},
        {"game_award_respect", "ui"},
        {"tutorial_set_hud_help_closing", "ui"},
        {"completion_load_lut", "ui"},
        {"completion_cutscene_was_active", "ui"},
        {"pcr_change_hair", "ui"},
        {"pcr_change_composite", "ui"},
        {"pcr_get_player_preset", "ui"},
        {"pcr_set_identity", "ui"},
        {"dialog_box_disconnect", "ui"},
        {"main_menu_supress_profile_change", "ui"},
        {"pause_menu_control_scheme_init", "ui"},
        {"pause_menu_update_option", "ui"},
        {"pause_menu_restore_defaults", "ui"},
        {"store_common_bg_anim_complete", "ui"},
        {"store_weapon_hide_weapon", "ui"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 41 names land in their own real cluster's state;
    // hud_display_create_state/_commit_state/_remove_state are a REAL,
    // pre-confirmed exception (src/lua_host.cpp's own cross-check: "all 7
    // are genuinely dual-registered... identical function pointer each")
    // - promoted by this batch from generic stubs to real behavior, now
    // also explicitly dual-registered via specUiNames.push_back(), the
    // same migration audio_stop/game_is_active_input_gamepad/
    // coop_is_active already went through.
    {
        Host host(batchFixtureNames());
        for (const auto& rn : batchFixtureNames()) {
            lua_State* want = rn.cluster == "ui" ? host.uiState() : host.gameplayState();
            lua_State* other = rn.cluster == "ui" ? host.gameplayState() : host.uiState();
            bool dualRegistered = rn.name == "hud_display_create_state" || rn.name == "hud_display_commit_state" ||
                                   rn.name == "hud_display_remove_state";
            CHECK(hasGlobal(want, rn.name.c_str()));
            CHECK(hasGlobal(other, rn.name.c_str()) == dualRegistered);
        }
    }

    Host host(batchFixtureNames());
    lua_State* ui = host.uiState();
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // Sec15.1 character_set_damage_multiplier: OPEN resolve gate; CHOSEN
    // default 1.0; no in-scope reader beyond the C++ field.
    es.objectResolves().set("npc1", true);
    ok(host, gp, "character_set_damage_multiplier('npc1', 2.5)");
    CHECK(es.getOrCreateCharacter("npc1").damageMultiplier == 2.5);

    // ------------------------------------------------------------------
    // Sec15.2 vehicle_anim_start: resolve-gated, records the raw request,
    // unconditional replicate (no-op, not independently observable).
    es.objectResolves().set("v1", true);
    ok(host, gp, "vehicle_anim_start('v1', 'track_a', 'track_b', 0.5)");
    CHECK(es.getOrCreateVehicle("v1").animStartTrackArg2 == "track_a");
    CHECK(es.getOrCreateVehicle("v1").animStartTrackArg3 == "track_b");
    CHECK(es.getOrCreateVehicle("v1").animStartBlend == 0.5);
    CHECK(es.getOrCreateVehicle("v1").animStartCount == 1);

    // ------------------------------------------------------------------
    // Sec15.4 set_always_cower_flag: default true; AI-event-26 queue only
    // counts on the true-setting edge (Sec44.6 correction).
    ok(host, gp, "set_always_cower_flag('npc1')");
    CHECK(es.getOrCreateCharacter("npc1").alwaysCowerFlag == true);
    CHECK(es.getOrCreateCharacter("npc1").aiEvent26QueuedCount == 1);
    ok(host, gp, "set_always_cower_flag('npc1', false)");
    CHECK(es.getOrCreateCharacter("npc1").alwaysCowerFlag == false);
    CHECK(es.getOrCreateCharacter("npc1").aiEvent26QueuedCount == 1); // unchanged on the false edge

    // ------------------------------------------------------------------
    // Sec15.5 refraction_situation_clear: default 3 sets both bits.
    ok(host, gp, "refraction_situation_clear()");
    CHECK(es.refractionGlobalOverrideCleared() == true);
    CHECK(es.refractionClearLocalCount() == 1);
    CHECK(es.refractionClearReplicateCount() == 1);
    ok(host, gp, "refraction_situation_clear(1)"); // bit 0x1 only
    CHECK(es.refractionClearLocalCount() == 2);
    CHECK(es.refractionClearReplicateCount() == 1); // unchanged

    // ------------------------------------------------------------------
    // Sec15.24/15.12/15.25/15.6: the shared HUD display-state slot pool,
    // and qte_cleanup's own cross-link into it.
    ok(host, gp, "local a,b,c,d,e = hud_display_create_state(), hud_display_create_state(), "
                 "hud_display_create_state(), hud_display_create_state(), hud_display_create_state() "
                 "assert(a==15 and b==16 and c==17 and d==18 and e==19)");
    ok(host, gp, "assert(hud_display_create_state() == -1)"); // CONFIRMED exhaustion sentinel
    CHECK(es.hudDisplayStatePool().createCount == 5);
    ok(host, gp, "hud_display_commit_state(15)");
    CHECK(es.hudDisplayStatePool().pendingQueue.size() == 1);
    ok(host, gp, "hud_display_commit_state(15)"); // already queued -> rejected
    CHECK(es.hudDisplayStatePool().commitRejectedCount == 1);
    ok(host, gp, "hud_display_remove_state(15, true)");
    CHECK(es.hudDisplayStatePool().removed[0] == true);
    CHECK(es.hudDisplayStatePool().pendingQueue.empty()); // advanced/removed from the queue
    ok(host, gp, "hud_display_remove_state(15, false)"); // already removed -> invalid, still counted
    CHECK(es.hudDisplayStatePool().removeInvalidCount == 1);
    ok(host, gp, "hud_display_remove_state(999, false)"); // out of range -> invalid
    CHECK(es.hudDisplayStatePool().removeInvalidCount == 2);
    // qte_cleanup: a test-registered QTE entry linking slot 16 releases it too.
    es.registerActiveQteSlotForTesting(16);
    CHECK(es.hudDisplayStatePool().used[1] == true && es.hudDisplayStatePool().removed[1] == false);
    ok(host, gp, "qte_cleanup()");
    CHECK(es.hudDisplayStatePool().removed[1] == true);
    CHECK(es.qteActiveSlotCount() == 0);
    CHECK(es.qteCleanupCount() == 1);

    // ------------------------------------------------------------------
    // Sec15.7 persona_clear_global_situation_override: unconditional
    // reset, host-gated replicate (CONFIRMED false in single player ->
    // never fires, no observable difference either way since it's a
    // no-op - only the reset itself and the counter are checked).
    ok(host, gp, "persona_clear_global_situation_override()");
    CHECK(es.personaGlobalSituationOverride() == 0);
    CHECK(es.personaGlobalSituationOverrideResetCount() == 1);

    // ------------------------------------------------------------------
    // Sec15.8 on_weapon_pickup: resolve-gated hook-slot registration.
    ok(host, gp, "on_weapon_pickup('cb_pickup', 'npc1')");
    CHECK(es.getOrCreateCharacter("npc1").onWeaponPickupCallback == "cb_pickup");

    // ------------------------------------------------------------------
    // Sec15.9/15.10 mission_unlock / mission_set_completed: idempotent
    // completion; unconditional replicate call count regardless of
    // resolve/idempotency.
    es.objectResolves().set("m_unknown", false); // explicitly confirmed non-existent -> no-op, no OPEN read
    ok(host, gp, "mission_unlock('m_unknown')");
    CHECK(es.missionUnlockedValue("m_unknown") == false);
    es.objectResolves().set("m01", true);
    ok(host, gp, "mission_unlock('m01')");
    CHECK(es.missionUnlockedValue("m01") == true);
    ok(host, gp, "mission_set_completed('m01')");
    CHECK(es.missionCompletedValue("m01") == true);
    CHECK(es.missionSetCompletedCallCount() == 1);
    ok(host, gp, "mission_set_completed('m01')"); // idempotent re-mark, replicate still fires
    CHECK(es.missionSetCompletedCallCount() == 2);
    es.objectResolves().set("m_never_resolved", false); // explicitly confirmed non-existent
    ok(host, gp, "mission_set_completed('m_never_resolved')"); // unresolved: still counts, no mark
    CHECK(es.missionSetCompletedCallCount() == 3);
    CHECK(es.missionCompletedValue("m_never_resolved") == false);

    // ------------------------------------------------------------------
    // Sec15.11 message: always returns an incrementing handle.
    ok(host, gp, "local a = message('hello') local b = message('world') assert(b == a + 1)");

    // ------------------------------------------------------------------
    // Sec15.13 follower_remain_in_car: forceFlagGatePasses CONFIRMED true
    // in single player -> direct bit write.
    ok(host, gp, "follower_remain_in_car('npc1', true)");
    CHECK(es.getOrCreateCharacter("npc1").flags1c9c.get(CharacterState::kCannotExitVehiclesBit1c9c) != 0);
    ok(host, gp, "follower_remain_in_car('npc1', false)");
    CHECK(es.getOrCreateCharacter("npc1").flags1c9c.get(CharacterState::kCannotExitVehiclesBit1c9c) == 0);

    // ------------------------------------------------------------------
    // Sec15.14 character_is_ragdolled: unresolved -> false, no OPEN read;
    // resolved but state unset -> OPEN; then exclusion test.
    es.objectResolves().set("nobody", false);
    ok(host, gp, "assert(character_is_ragdolled('nobody') == false)");
    fails(host, gp, "character_is_ragdolled('npc1')", "+0xcd2");
    es.getOrCreateCharacter("npc1").ragdollStateEnumCd2.set(0);
    ok(host, gp, "assert(character_is_ragdolled('npc1') == false)");
    es.getOrCreateCharacter("npc1").ragdollStateEnumCd2.set(3);
    ok(host, gp, "assert(character_is_ragdolled('npc1') == true)");
    es.getOrCreateCharacter("npc1").ragdollStateEnumCd2.set(6);
    ok(host, gp, "assert(character_is_ragdolled('npc1') == false)");

    // ------------------------------------------------------------------
    // Sec15.15 vehicle_skydiving_start: first call (bit clear) ->
    // replicate path; second call (bit pre-set by a test) -> local apply.
    ok(host, gp, "vehicle_skydiving_start('v1')");
    CHECK(es.getOrCreateVehicle("v1").skydiveStartReplicateCount == 1);
    CHECK(es.getOrCreateVehicle("v1").skydiveActiveFlag1_16c4 == false);
    es.getOrCreateVehicle("v1").skydiveActiveFlag1_16c4 = true;
    es.objectResolves().set("anchor1", true);
    ok(host, gp, "vehicle_skydiving_start('v1', 'anchor1')");
    CHECK(es.getOrCreateVehicle("v1").skydiveStartLocalApplyCount == 1);
    CHECK(es.getOrCreateVehicle("v1").skydiveAnchorSourceName == "anchor1");

    // ------------------------------------------------------------------
    // Sec15.16 vehicle_is_tank: unresolved -> false; resolved -> OPEN
    // until the predicate is set.
    ok(host, gp, "assert(vehicle_is_tank('nobody') == false)");
    fails(host, gp, "vehicle_is_tank('v1')", "Sec15.16");
    es.getOrCreateVehicle("v1").isTankClassPredicate.set(true);
    ok(host, gp, "assert(vehicle_is_tank('v1') == true)");

    // ------------------------------------------------------------------
    // Sec15.18 skydive_set_player_dive_fall_speed: direct overwrite.
    ok(host, gp, "skydive_set_player_dive_fall_speed(42.0)");
    CHECK(es.playerSkydiveFallSpeed() == 42.0);
    ok(host, gp, "skydive_set_player_dive_fall_speed()"); // bare stack-depth check, CHOSEN placeholder
    CHECK(es.playerSkydiveFallSpeed() == 0.0);

    // ------------------------------------------------------------------
    // Sec15.19 set_unenterable_for_player: gated primary bit 0x80 at
    // +0x1d7b + unconditional secondary mirror.
    ok(host, gp, "set_unenterable_for_player('v1')");
    CHECK(es.getOrCreateVehicle("v1").forceFlags1d7b.get(VehicleState::kUnenterableForPlayerBit1d7b) != 0);
    CHECK(es.getOrCreateVehicle("v1").unenterableForPlayerSecondarySubObjectFlag == true);
    ok(host, gp, "set_unenterable_for_player('v1', false)");
    CHECK(es.getOrCreateVehicle("v1").forceFlags1d7b.get(VehicleState::kUnenterableForPlayerBit1d7b) == 0);
    CHECK(es.getOrCreateVehicle("v1").unenterableForPlayerSecondarySubObjectFlag == false);

    // ------------------------------------------------------------------
    // Sec15.20 set_player_undercover_team: reuses lookupNotorietyTrackCode
    // (Sec3.6's own helper) - unregistered name -> fallback 9.
    ok(host, gp, "set_player_undercover_team('npc1', 'blue_team')");
    CHECK(es.getOrCreateCharacter("npc1").undercoverTeamCode == 9); // CONFIRMED not-found fallback
    es.registerNotorietyTrackCodeForTesting("blue_team", 2);
    ok(host, gp, "set_player_undercover_team('npc1', 'blue_team')");
    CHECK(es.getOrCreateCharacter("npc1").undercoverTeamCode == 2);

    // ------------------------------------------------------------------
    // Sec15.21 player_actions_enable: resolve-gated, CHOSEN default
    // branch (no toggle argument).
    ok(host, gp, "player_actions_enable('npc1')");
    CHECK(es.getOrCreateCharacter("npc1").playerActionsEnableCount == 1);

    // ------------------------------------------------------------------
    // Sec15.22 npc_leash_to_object: negative radius clears; non-negative
    // + resolved anchor sets.
    ok(host, gp, "npc_leash_to_object('npc1', 'anchor1', 5.0, true)");
    CHECK(es.getOrCreateCharacter("npc1").npcLeashAnchorName == "anchor1");
    CHECK(es.getOrCreateCharacter("npc1").npcLeashRadius == 5.0);
    CHECK(es.getOrCreateCharacter("npc1").npcLeashArg4 == true);
    ok(host, gp, "npc_leash_to_object('npc1', 'anchor1', -1.0)");
    CHECK(es.getOrCreateCharacter("npc1").npcLeashAnchorName == "");
    CHECK(es.getOrCreateCharacter("npc1").npcLeashClearCount == 1);

    // ------------------------------------------------------------------
    // Sec15.26 game_choice_set_state: persistent bitmask, HOST-SAFETY
    // guard on index >= 32.
    ok(host, gp, "game_choice_set_state(0, true)");
    CHECK(es.gameChoiceBitValue(0) == true);
    ok(host, gp, "game_choice_set_state(0, false)");
    CHECK(es.gameChoiceBitValue(0) == false);
    ok(host, gp, "game_choice_set_state(250, true)"); // byte range but >= 32 -> HOST-SAFETY guard, no UB
    CHECK(es.gameChoiceSetCount() == 3);

    // ------------------------------------------------------------------
    // Sec15.27 dlc_achievement_counter_set: HARDENING POLICY - in-range
    // writes exactly; negative index refused (CHOSEN hardened
    // divergence); out-of-range-high is the real CONFIRMED no-op.
    ok(host, gp, "dlc_achievement_counter_set(0, 7)");
    CHECK(es.dlcAchievementCounterValue(0) == 7);
    ok(host, gp, "dlc_achievement_counter_set(2, 99)");
    CHECK(es.dlcAchievementCounterValue(2) == 99);
    ok(host, gp, "dlc_achievement_counter_set(-1, 1234)"); // CHOSEN hardened divergence - refused, not written
    CHECK(es.dlcAchievementCounterNegativeIndexGuardCount() == 1);
    ok(host, gp, "dlc_achievement_counter_set(5, 1234)"); // CONFIRMED real no-op (the `< 3` check)
    CHECK(es.dlcAchievementCounterNegativeIndexGuardCount() == 1); // unchanged - not the hardened path

    // ------------------------------------------------------------------
    // Sec15.28 character_parachute_detach: resolve-gated counter.
    ok(host, gp, "character_parachute_detach('npc1')");
    CHECK(es.getOrCreateCharacter("npc1").parachuteDetachCount == 1);

    // ==================================================================
    // Sec16 (UI cluster)
    // ==================================================================

    // Sec16.1 game_get_language: CHOSEN default -1 -> ""; in-range index;
    // past-the-floor index -> OPEN.
    ok(host, ui, "assert(game_get_language() == '')");
    es.setLanguageIndexForTesting(3);
    ok(host, ui, "assert(game_get_language() == 'JP')");
    es.setLanguageIndexForTesting(12);
    fails(host, ui, "game_get_language()", "Sec16.1");

    // Sec16.2 game_award_respect: OPEN multiplier gates the return value.
    fails(host, ui, "game_award_respect(10)", "Sec16.2");
    es.respectMultiplier().set(2.0);
    ok(host, ui, "assert(game_award_respect(10) == 20)");
    CHECK(es.respectGrantCount() == 1);

    // Sec16.3 tutorial_set_hud_help_closing: trivial.
    ok(host, ui, "tutorial_set_hud_help_closing(true)");
    CHECK(es.tutorialHudHelpClosing() == true);

    // Sec16.4 completion_load_lut: "none"-prefix clears; a real name gets
    // a fresh incrementing id (no real table in scope).
    ok(host, ui, "completion_load_lut('none_of_the_above')"); // 4-char PREFIX match, not exact
    CHECK(es.colorGradingLutSlot(2).lutId == -1);
    CHECK(es.colorGradingLutSlot(2).intensity == 0.0);
    ok(host, ui, "completion_load_lut('my_cool_lut')");
    CHECK(es.colorGradingLutSlot(2).lutId >= 1);
    CHECK(es.colorGradingLutSlot(2).intensity == 1.0);

    // Sec16.5 completion_cutscene_was_active: pure read, test-settable.
    ok(host, ui, "assert(completion_cutscene_was_active() == false)");
    es.completionCutsceneWasActive() = true;
    ok(host, ui, "assert(completion_cutscene_was_active() == true)");

    // Sec16.6 pcr_change_hair: arg1 bounds-check; arg2 clear vs apply.
    ok(host, ui, "pcr_change_hair(0, -1, 0, 0)"); // categoryId 1, clear
    CHECK(es.pcrCustomization().items[1].categoryId == 1);
    CHECK(es.pcrCustomization().items[1].styleId == -1);
    ok(host, ui, "pcr_change_hair(1, 5, 2, 3)"); // categoryId 2, apply
    CHECK(es.pcrCustomization().items[2].categoryId == 2);
    CHECK(es.pcrCustomization().items[2].styleId == 5);
    ok(host, ui, "pcr_change_hair(5, 0, 0, 0)"); // out of range arg1 -> real no-op
    CHECK(es.pcrCustomization().items[0].categoryId == -1); // untouched

    // Sec16.7 pcr_change_composite: category lookup, special-cased
    // "fingernails" mirror.
    ok(host, ui, "pcr_change_composite('eyes', 0.1, 0.2)");
    CHECK(es.pcrCustomization().items[1].categoryId == 1); // "eyes" at index 1
    ok(host, ui, "pcr_change_composite('fingernails', 0.3, 0.4, 0.5)");
    CHECK(es.pcrCustomization().items[0x16].categoryId == 0x16);
    CHECK(es.pcrCustomization().fingernailsRMirrorSet == true);
    CHECK(es.pcrCustomization().fingernailsRMirror.rawArg2 == 0.3);
    ok(host, ui, "pcr_change_composite('no_such_category', 1, 2)");
    CHECK(es.pcrCustomization().compositeLookupFailedCount == 1);

    // Sec16.8/16.8a pcr_get_player_preset / pcr_set_identity: sentinel ->
    // Default preset fallback; set_identity writes gender only.
    ok(host, ui, "local g,e,b = pcr_get_player_preset() assert(g=='male' and e=='white' and b=='average')");
    ok(host, ui, "pcr_set_identity('Female')"); // case-insensitive "male" test -> non-male -> 1
    ok(host, ui, "local g,e,b = pcr_get_player_preset() assert(g=='female' and e=='white')");
    CHECK(es.pcrCustomization().setIdentityBodyRefreshCount == 1);
    ok(host, ui, "pcr_set_identity('male')");
    ok(host, ui, "local g = pcr_get_player_preset() assert(g=='male')");

    // Sec16.9/16.10/16.14/16.15: trivial counters/globals.
    ok(host, ui, "dialog_box_disconnect()");
    CHECK(es.dialogBoxDisconnectCount() == 1);
    ok(host, ui, "main_menu_supress_profile_change(true)");
    CHECK(es.mainMenuProfileChangeSuppressed() == true);
    ok(host, ui, "store_common_bg_anim_complete()");
    CHECK(es.storeCommonBgAnimComplete() == true);
    ok(host, ui, "store_weapon_hide_weapon()");
    CHECK(es.storeWeaponPreviewSlot() == -1);

    // Sec16.11 pause_menu_control_scheme_init: variable return count.
    ok(host, ui, "local r = {pause_menu_control_scheme_init(false)} assert(#r == 0)");
    ok(host, ui, "local a,b = pause_menu_control_scheme_init(true, 5) assert(a==0.0 and b==25.0)"); // scheme 5: no 2nd push
    es.pauseMenuPresetScheme0() = 7;
    ok(host, ui, "local a,b,c = pause_menu_control_scheme_init(true, 0) assert(a==0.0 and b==7 and c==25.0)");

    // Sec16.12/16.13 pause_menu_update_option / restore_defaults: raw
    // request recorded, not the unenumerated real dispatch.
    ok(host, ui, "pause_menu_update_option(2, 0, true, 0.75)");
    {
        auto* req = es.pauseMenuOptionRequest(0, 2);
        CHECK(req != nullptr && req->flag == true && req->value == 0.75 && req->count == 1);
    }
    ok(host, ui, "pause_menu_restore_defaults(0)");
    CHECK(es.pauseMenuRestoreDefaultsCount(0) == 1);

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_batch5_hud_customization_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_batch5_hud_customization_test: " << g_failures << " check(s) failed\n";
    return 1;
}
