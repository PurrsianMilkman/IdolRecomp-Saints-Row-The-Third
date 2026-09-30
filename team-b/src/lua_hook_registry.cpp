#include "sr3luahost/hook_registry.h"

namespace sr3luahost {

const char* hookGroupLabel(HookGroup g) {
    switch (g) {
        case HookGroup::Group1_GeneralNamed: return "Group1_general_confirmed";
        case HookGroup::Group2_LifecycleUnconditional: return "Group2_lifecycle_unconditional_confirmed";
        case HookGroup::Group3_MissionNumberedPatternBased: return "Group3_mission_numbered_pattern_based";
    }
    return "unknown";
}

const char* hookGroupNote(HookGroup g) {
    switch (g) {
        case HookGroup::Group1_GeneralNamed:
            return "Group 1: general named hook, spec-lua-bindings.md Sec8.3/Sec12.3, CONFIRMED literal "
                   "call-site name, existence-gated (lua_getglobal + lua_isfunction, call only if true).";
        case HookGroup::Group2_LifecycleUnconditional:
            return "Group 2: system/lifecycle hook, spec-lua-bindings.md Sec12.4, CONFIRMED but called "
                   "UNCONDITIONALLY by the real engine (no existence check first, per the decompiled "
                   "FUN_00e0de80). This project still checks existence first for honest measurement, but "
                   "does NOT gate the call on it - a script that doesn't define this hook gets a real Lua "
                   "'attempt to call a nil value' error recorded, matching the spec's own 'presumably "
                   "errors at the real call site' statement.";
        case HookGroup::Group3_MissionNumberedPatternBased:
            return "Group 3: mission-numbered hook, spec-lua-bindings.md Sec12.6, HIGH CONFIDENCE by "
                   "naming-convention string presence ONLY - only 2 of the underlying 60 mission-numbered "
                   "strings were individually traced to a real dispatcher call site (m24_killbane_on_death/"
                   "_on_undowned, already counted in Group 1). The other 58 (fired here) are NOT "
                   "individually dispatcher-confirmed - pattern-based evidence, not the same confirmation "
                   "strength as Group 1/2.";
    }
    return "";
}

namespace {

std::vector<HookSpec> buildConfirmedHooks() {
    std::vector<HookSpec> h;
    h.reserve(138);

    // `args` defaults to empty - matches EVERY hook except the small set
    // given a real, spec-confirmed arg contract below (spec-lua-bindings.md
    // Sec14) via an explicit non-empty 3rd argument at that hook's own
    // add(...) call site - see hook_registry.h's own top-of-struct comment.
    auto add = [&](const char* name, HookGroup g, std::vector<HookArg> args = {}) {
        HookSpec s;
        s.name = name;
        s.group = g;
        s.args = std::move(args);
        h.push_back(std::move(s));
    };

    // ---- Group 1 - 73 entries, spec-lua-bindings.md Sec12.3, #1-73 -------

    // 1-11: Mission/activity completion screens.
    // cmp_mission_success/cmp_activity_success/cmp_fail_populate: explicit
    // EXCEPTION, per this task's own instruction - Team A flagged these
    // (spec-lua-bindings.md Sec14.3) as possibly NOT real Lua hooks at all
    // (native-C-callback-wired instead), pending their own adversarial
    // re-check, still unresolved. Left at 0 args, matching their CURRENT
    // (already-correct, unchanged since Sec9.117) behavior - not given a
    // real arg contract even though the two OTHER completion-screen names
    // right below (cmp_common_screen_start/completion_coop_disconnected)
    // were, precisely because Sec14.3's own finding is that these three
    // (plus garage_populate/garage_performance_stats further below) are a
    // DIFFERENT, non-Lua mechanism (FUN_00e1e7e0/FUN_00e25040 registries,
    // not the confirmed Lua dispatcher) - still fired here exactly as
    // every other Group 1 name, per this project's own prior instruction
    // not to special-case them pending Team A's re-check.
    add("cmp_mission_success", HookGroup::Group1_GeneralNamed);
    add("cmp_activity_success", HookGroup::Group1_GeneralNamed);
    add("cmp_fail_populate", HookGroup::Group1_GeneralNamed);
    add("cmp_rewards_success", HookGroup::Group1_GeneralNamed);
    add("cmp_stronghold_success", HookGroup::Group1_GeneralNamed);
    add("credits_grab_credits", HookGroup::Group1_GeneralNamed);
    add("horde_results_populate", HookGroup::Group1_GeneralNamed);
    add("cat_mouse_results_populate", HookGroup::Group1_GeneralNamed);
    add("coop_diversion_success_responder", HookGroup::Group1_GeneralNamed);
    // cmp_common_screen_start: spec Sec14.3 CONFIRMED 1 arg, a DOUBLE
    // (value = global DAT_02282958, real-world meaning OPEN/untraced). No
    // specific real magnitude is confirmed by the spec - only the
    // arity/type - so 0.0 is used: a stated, honest placeholder (the
    // LEAST-invented choice), never a claim about the real engine's own
    // runtime value.
    add("cmp_common_screen_start", HookGroup::Group1_GeneralNamed, {HookArg::number(0.0)});
    // completion_coop_disconnected: spec Sec14.3 CONFIRMED 0 arguments -
    // args left empty (no functional change from before this pass; stated
    // explicitly here rather than silently relying on the default).
    add("completion_coop_disconnected", HookGroup::Group1_GeneralNamed);

    // 12-21: Main menu / pause / options.
    add("main_menu_internet_disconnected", HookGroup::Group1_GeneralNamed);
    add("main_menu_ethernet_disconnected", HookGroup::Group1_GeneralNamed);
    add("Main_menu_gameboot_complete", HookGroup::Group1_GeneralNamed);
    add("pause_map_interface_event", HookGroup::Group1_GeneralNamed);
    add("pause_map_stag_completion", HookGroup::Group1_GeneralNamed);
    add("options_display_populate_display_modes", HookGroup::Group1_GeneralNamed);
    add("options_display_populate_msaa", HookGroup::Group1_GeneralNamed);
    add("vint_remap_update_display", HookGroup::Group1_GeneralNamed);
    add("vint_remap_get_action_binding", HookGroup::Group1_GeneralNamed);
    add("pause_map_is_taxi_mode", HookGroup::Group1_GeneralNamed);

    // 22-35: HUD / inventory.
    // hud_running_man_event_update: spec Sec14.4 CONFIRMED call-site-
    // dependent arity - 1 float at most real sites, a 2nd float pushed
    // ONLY at one specific site (FUN_0063c300) when a caller-supplied
    // event-type code == 0x16. This project's own firing loop calls each
    // hook exactly once per script per state, with no real "event type"
    // context to branch on - so, per this task's own explicit instruction,
    // a single concrete choice is made here and stated plainly rather than
    // silently picked: the 1-float form (the normal/default case across
    // most real call sites), NOT the 2-float branch. No fabricated
    // "event type" input is synthesized to reach the 2-arg branch. The
    // pushed float's own value is not spec-confirmed (arity/type only), so
    // 0.0 is used - the same honest-placeholder convention as
    // cmp_common_screen_start above.
    add("hud_running_man_event_update", HookGroup::Group1_GeneralNamed, {HookArg::number(0.0)});
    add("hud_running_man_load_complete", HookGroup::Group1_GeneralNamed);
    add("hud_touch_combo", HookGroup::Group1_GeneralNamed);
    add("hud_qte", HookGroup::Group1_GeneralNamed);
    add("hud_diversion", HookGroup::Group1_GeneralNamed);
    add("hud_zombie", HookGroup::Group1_GeneralNamed);
    add("hud_btnmash", HookGroup::Group1_GeneralNamed);
    add("hud_mayhem_world_cash_update", HookGroup::Group1_GeneralNamed);
    add("object_indicator_update", HookGroup::Group1_GeneralNamed);
    add("object_indicator_remove", HookGroup::Group1_GeneralNamed);
    add("hud_inventory_hide", HookGroup::Group1_GeneralNamed);
    add("hud_inventory_show", HookGroup::Group1_GeneralNamed);
    add("hud_hit_clear_all", HookGroup::Group1_GeneralNamed);
    add("hud_msg_hide_region", HookGroup::Group1_GeneralNamed);

    // 36-43: Garage / vehicle.
    add("garage_vehicle_load_begin", HookGroup::Group1_GeneralNamed);
    add("garage_vehicle_load_completed", HookGroup::Group1_GeneralNamed);
    // garage_populate/garage_performance_stats: same explicit EXCEPTION as
    // cmp_mission_success/cmp_activity_success/cmp_fail_populate above -
    // left at 0 args, unchanged, pending Team A's adversarial re-check.
    add("garage_populate", HookGroup::Group1_GeneralNamed);
    add("garage_performance_stats", HookGroup::Group1_GeneralNamed);
    add("store_vehicle_switch_mode", HookGroup::Group1_GeneralNamed);
    add("store_vehicle_signal_swap_complete", HookGroup::Group1_GeneralNamed);
    add("store_vehicle_exit_begin_bg_clear", HookGroup::Group1_GeneralNamed);
    add("clones_m3_pow", HookGroup::Group1_GeneralNamed);

    // 44-49: Vehicle customization.
    add("vcust_populate_menu", HookGroup::Group1_GeneralNamed);
    add("vcust_populate_palette_menu", HookGroup::Group1_GeneralNamed);
    add("vcust_populate_underglow_color", HookGroup::Group1_GeneralNamed);
    add("vcust_populate_color_grid", HookGroup::Group1_GeneralNamed);
    add("vcust_populate_wheel_menu", HookGroup::Group1_GeneralNamed);
    add("vcust_populate_wheel_grid_menu", HookGroup::Group1_GeneralNamed);

    // 50-51: Tutorials.
    add("tutorial_advance", HookGroup::Group1_GeneralNamed);
    add("tutorial_responder", HookGroup::Group1_GeneralNamed);

    // 52-56: Minigames.
    add("button_mashing_minigame", HookGroup::Group1_GeneralNamed);
    add("sr2_balance_meter", HookGroup::Group1_GeneralNamed);
    add("mayhem_local_player_world_cash", HookGroup::Group1_GeneralNamed);
    add("whored_countdown_timer_update", HookGroup::Group1_GeneralNamed);
    add("cell_missions_button_b", HookGroup::Group1_GeneralNamed);

    // 57-68: Screen transitions / misc UI / system.
    add("screen_fade_do", HookGroup::Group1_GeneralNamed);
    add("screen_fade_auto_save_show", HookGroup::Group1_GeneralNamed);
    add("screen_fade_auto_save_hide", HookGroup::Group1_GeneralNamed);
    add("countdown_display", HookGroup::Group1_GeneralNamed);
    add("countdown_unpause", HookGroup::Group1_GeneralNamed);
    add("newsticker_populate", HookGroup::Group1_GeneralNamed);
    add("store_gallery_init_complete", HookGroup::Group1_GeneralNamed);
    add("map_filter", HookGroup::Group1_GeneralNamed);
    add("map_district_names", HookGroup::Group1_GeneralNamed);
    add("gis_grain_fade_out", HookGroup::Group1_GeneralNamed);
    add("vint_lib_init_constants", HookGroup::Group1_GeneralNamed);
    add("city_load_hide_images", HookGroup::Group1_GeneralNamed);

    // 69-71: Store / controls.
    add("store_lock_controls", HookGroup::Group1_GeneralNamed);
    add("store_unlock_controls", HookGroup::Group1_GeneralNamed);
    add("store_weapon_cover_weapon", HookGroup::Group1_GeneralNamed);

    // 72-73: Mission-specific, general dispatcher.
    // Both spec Sec14.3 CONFIRMED 0 arguments each - args left empty (no
    // functional change from before this pass; stated explicitly).
    add("m24_killbane_on_death", HookGroup::Group1_GeneralNamed);
    add("m24_killbane_on_undowned", HookGroup::Group1_GeneralNamed);

    // ---- Group 2 - 2 entries, spec-lua-bindings.md Sec12.4, #74-75 -------
    // _PrepareForDynamicGlobals: spec Sec14.1 CONFIRMED 1 arg, a STRING -
    // "the canonicalized script filename" (FUN_00e0cff0 force-appends a
    // .lua suffix if missing). Real value used: the actual script's own
    // entryName this project already has on hand while firing (every real
    // found script's name already ends in ".lua" - see
    // tools/lua_host_run.cpp's endsWithLuaExt() gate - so the "append
    // .lua if missing" step is a no-op for 100% of this project's real
    // population; the value pushed is genuinely the real per-script
    // filename, not a placeholder). Threaded through via
    // Host::fireConfirmedHooks's new `scriptFilename` parameter - see
    // host.h/lua_host.cpp and tools/lua_host_run.cpp's own call sites.
    add("_PrepareForDynamicGlobals", HookGroup::Group2_LifecycleUnconditional, {HookArg::scriptFilename()});
    // _DynamicGlobalsLoadComplete: spec Sec14.1 CONFIRMED 0 arguments -
    // args left empty (no functional change from before this pass; the
    // spec's own "fires only on a non-error load result" gating nuance is
    // NOT modeled here - out of THIS task's scope, which is argument
    // contracts only, not call-gating logic - so this remains Group 2's
    // pre-existing UNCONDITIONAL call, unchanged).
    add("_DynamicGlobalsLoadComplete", HookGroup::Group2_LifecycleUnconditional);

    // ---- Group 3 - 58 entries, spec-lua-bindings.md Sec12.6, #76-133 -----

    // 76-78: Cutscene/film-trigger.
    add("m02_clapboards_get", HookGroup::Group3_MissionNumberedPatternBased);
    add("m02_clapboards_reset", HookGroup::Group3_MissionNumberedPatternBased);
    add("m02_clapboards_set", HookGroup::Group3_MissionNumberedPatternBased);

    // 79-81: Costume/customization.
    add("m02_ear", HookGroup::Group3_MissionNumberedPatternBased);
    add("m02_gloves", HookGroup::Group3_MissionNumberedPatternBased);
    add("m02_suit_f_bod", HookGroup::Group3_MissionNumberedPatternBased);

    // 82-84: Vehicle/traversal.
    add("m02_plane", HookGroup::Group3_MissionNumberedPatternBased);
    add("m02_pull_chute_1", HookGroup::Group3_MissionNumberedPatternBased);
    add("m02_pull_chute_2", HookGroup::Group3_MissionNumberedPatternBased);

    // 85-87: Cutscene trigger.
    add("m02_shaundi_pushed_away", HookGroup::Group3_MissionNumberedPatternBased);
    add("m02_shoot_plane_window", HookGroup::Group3_MissionNumberedPatternBased);
    add("m10_strip", HookGroup::Group3_MissionNumberedPatternBased);

    // 88: Minigame/QTE.
    add("m02_skyqte_01", HookGroup::Group3_MissionNumberedPatternBased);

    // 89-92: Combat.
    add("m03_fake_tag_punch_hit", HookGroup::Group3_MissionNumberedPatternBased);
    add("m03_punch_load", HookGroup::Group3_MissionNumberedPatternBased);
    add("m03_punch_unload", HookGroup::Group3_MissionNumberedPatternBased);
    add("m03_set_sprint_waning", HookGroup::Group3_MissionNumberedPatternBased);

    // 93: Combat/ability.
    add("m03_super_powers", HookGroup::Group3_MissionNumberedPatternBased);

    // 94: Vehicle/combat.
    add("m05_heli_pierce", HookGroup::Group3_MissionNumberedPatternBased);

    // 95-112: Camera - "josh" over-the-shoulder (9).
    add("m12_josh_overshoulder_run_back", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_josh_overshoulder_run_forward", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_josh_overshoulder_run_left", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_josh_overshoulder_run_right", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_josh_overshoulder_stand", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_josh_overshoulder_walk_back", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_josh_overshoulder_walk_forward", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_josh_overshoulder_walk_left", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_josh_overshoulder_walk_right", HookGroup::Group3_MissionNumberedPatternBased);
    // 95-112: Camera - "plym" over-the-shoulder (9).
    add("m12_plym_overshoulder_run_back", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_plym_overshoulder_run_forward", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_plym_overshoulder_run_left", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_plym_overshoulder_run_right", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_plym_overshoulder_stand", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_plym_overshoulder_walk_back", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_plym_overshoulder_walk_forward", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_plym_overshoulder_walk_left", HookGroup::Group3_MissionNumberedPatternBased);
    add("m12_plym_overshoulder_walk_right", HookGroup::Group3_MissionNumberedPatternBased);

    // 113-128: Gameplay-modifier/minigame-effect (STAG).
    add("m16_boss_invert", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_boss_invert_stop", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_boss_slow", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_boss_slow_stop", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_cheat_slow", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_cheat_slow_stop", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_glitch", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_hotdog_mute_cannon", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_hotdog_unmute_cannon", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_invert", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_invert_stop", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_qte_complete", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_reset_all", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_shockwave_impact", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_shrink", HookGroup::Group3_MissionNumberedPatternBased);
    add("m16_shrink_stop", HookGroup::Group3_MissionNumberedPatternBased);

    // 129-132: Combat/boss-fight.
    add("m21_crowd_cheer_mid", HookGroup::Group3_MissionNumberedPatternBased);
    add("m21_killbane_damaged", HookGroup::Group3_MissionNumberedPatternBased);
    add("m21_killswitch_qte_complete_cb", HookGroup::Group3_MissionNumberedPatternBased);
    add("m21_wieldable_prop_created_cb", HookGroup::Group3_MissionNumberedPatternBased);

    // 133: Narrative choice.
    add("m21_player_choice", HookGroup::Group3_MissionNumberedPatternBased);

    // ---- 5 more Group 1 entries, spec-lua-bindings.md Sec14.6, added in a
    // same-day follow-up pass (2026-09-29) - found as a side effect of
    // Sec14.6's own exhaustive negative search for a mission-lifecycle
    // sprintf-hook family, NOT part of the original numbered #1-133
    // census above. Each is CONFIRMED via the same real dispatcher
    // call-site mechanism every other Group 1 name uses (Sec14.6's own
    // text gives no reason to sort any of these 5 into Group 2/3), so
    // they are ordinary existence-gated Group 1 entries - not specially
    // treated. 0 args (no argument contract confirmed for any of these 5
    // by Sec14.6 - it settles NAMES/mechanism, not argument shapes).
    // 134: pairs with the already-known store_weapon_cover_weapon (#71).
    add("store_weapon_uncover_weapon", HookGroup::Group1_GeneralNamed);
    // 135-136: store gallery upload/download lifecycle.
    add("store_gallery_upload_complete", HookGroup::Group1_GeneralNamed);
    add("store_gallery_download_list_complete", HookGroup::Group1_GeneralNamed);
    // 137: screen-capture UI.
    add("screen_capture_open_preview_dialog", HookGroup::Group1_GeneralNamed);
    // 138: fired from the modal-dialog/popup pool's own cleanup path
    // (Sec14.6: a small pooled, 4-slot modal-dialog/popup manager,
    // DAT_02282d40) - generic UI-dialog plumbing, NOT mission/activity
    // lifecycle related (an initial hypothesis tying it there was
    // checked and refuted per Sec14.6's own text).
    add("dialog_build", HookGroup::Group1_GeneralNamed);

    return h;
}

} // namespace

const std::vector<HookSpec>& confirmedHooks() {
    static const std::vector<HookSpec> table = buildConfirmedHooks();
    return table;
}

} // namespace sr3luahost
