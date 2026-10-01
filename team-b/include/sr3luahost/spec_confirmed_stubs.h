// IMPLEMENTED PRE-REVIEW, PENDING CLEARANCE (manager rule 2026-09-30): the
// spec-lua-api-behaviour.md / spec-lua-bindings.md sections this file rests on
// are marked "NOT yet cleared for implementation" by the 2026-09-30 desk
// review (115/122 and 57/58 review-status lines). Kept working, behaviour
// unchanged, until Team A clears them; no new behaviour from those sections
// before then (team-b/HANDOFF.md section A, standing rule).
//
// The 22 Lua-visible names this task promotes from stub_registry.h's
// generic "log and return nil" behavior to real, spec-confirmed behavior,
// backed by engine_state.h (13 as of the original pass below, +9 more from
// a same-shape follow-up pass, see further down this comment). The first
// 12 of the original 13 were selected by the orchestrator
// as the intersection of (a) the top-22 real runtime stub-hit ranking
// (tools/lua_runtime_stub_ranking.tsv) and (b) already fully adversarially-
// reviewed sections of spec-lua-api-behaviour.md Sec1-9:
//   coop_is_active (Sec3.1), game_get_key_name (Sec2.3),
//   game_UI_audio_play (Sec2.2), game_audio_get_audio_id (Sec8.22),
//   game_get_key_name_for_action (Sec8.23), game_peg_load_with_cb
//   (Sec8.24), ai_add_enemy_target (Sec3.9), on_take_damage (Sec3.13),
//   set_ignore_ai_flag (Sec3.4), get_max_hit_points (Sec7.12),
//   set_current_hit_points (Sec7.31), ai_clear_scripted_action (Sec7.24).
//
// The 13th, vint_object_find (spec-lua-bindings.md Sec13.7/Sec15), was
// folded in mid-task per the orchestrator's relay of a same-day Team A
// finding: this global was simply UNREGISTERED (a nil global, not merely a
// "wrong stub behavior"), accounting for 43% of all hook-fire errors -
// the single highest-impact real gap this project had measured. Verified
// directly from spec-lua-bindings.md Sec13.7/Sec15 (read in full, not
// taken on the relay's word alone) before implementing.
//
// A same-shape follow-up pass (2026-09-30) added 9 more names, all from
// spec-lua-api-behaviour.md Sec10.1-Sec10.9: store_vehicle_get_state
// (Sec10.1), Completion_is_client (Sec10.2), game_hud_update_inventory
// (Sec10.3), tutorial_advance (Sec10.4), minimap_icon_add_do (Sec10.5),
// object_indicator_add_do (Sec10.6), on_qte_animation_trigger (Sec10.7),
// on_revived (Sec10.8), game_get_coop_join_type (Sec10.9) - see
// lua_spec_confirmed_stubs.cpp's own functions 14-22 for each one's full
// citation/reasoning, and engine_state.h for the new fields/structs
// (CharacterState::onRevivedCallback/minimapIcons/objectIndicators;
// EngineState::vehicleStoreActive_/isHost_/hasLocalPlayer_/
// hudInventoryRefreshCount_/tutorialAdvanceCounts_/
// qteAnimationTriggerCallback_/coopJoinType_) backing them.
//
// Cloud phase (2026-09-30): +1, zscene_is_loaded (spec-lua-api-behaviour.md
// Sec14.23), one of the HANDOFF Sec9.143 mission-driving blockers. Its OPEN
// per-record branch is a labelled stub - see its own doc comment. The other
// Sec9.143 blockers (fade_is_fully_faded_out/_in, vint_is_std_res) have no
// behaviour spec yet and stay generic stubs (requested from Team A).
//
// IMPLEMENTED PRE-REVIEW, PENDING EXE RE-CLEARANCE (2026-09-30): the desk
// review now marks spec-lua-api-behaviour.md §1-§5 "NOT yet cleared for
// implementation". Six functions here come from those sections and were
// implemented pre-pause, when they counted as reviewed:
//   game_UI_audio_play (§2.2, NEEDS-EXE), game_get_key_name (§2.3,
//   NEEDS-EXE), coop_is_active (§3.1, NEEDS-EXE), set_ignore_ai_flag
//   (§3.4, NEEDS-EXE: 0x004e2050's `this` and the read bit's mask),
//   ai_add_enemy_target (§3.9, DESK-PASS), on_take_damage (§3.13,
//   DESK-PASS).
// They are kept working (manager decision pending; option (a) = label, no
// behaviour change) and nothing new is implemented from §1-§5 until Team A's
// executable re-derivation clears it.
//
// Deliberately NOT touched (still out of scope):
// thread_check_done (this project's own scaffold mechanism, thread_scheduler.h).
// The 8 vint_object_* lifecycle siblings Sec15 flags as "likely equally
// high-impact if also missing" (create/destroy/first_child/next_sibling/
// parent/set_parent/add_child/get_name_from_handle) are NOT specially
// implemented this pass (not individually behavior-traced beyond the
// shared handle resolver, per Sec15's own text) - they, and the other 46
// names in the same 55-name registrar, are registered as ordinary generic
// logging stubs by simply being present in the current tagged registration
// list (tools/lua_all_registered_1490_tagged.txt) and NOT in this file's
// specConfirmedStubNames() - see host.h/lua_host.cpp's own filtering.
//
// Each function below implements ONLY what its own spec (sub)section
// states at CONFIRMED tier; a HIGH-CONFIDENCE-tier sub-detail is either
// implemented with an explicit "HIGH CONFIDENCE, not CONFIRMED" comment at
// its own call site (engine_state.h documents which per-field) or left as
// a stated, explicit gap (a TODO comment) where implementing it would
// require guessing further - never silently upgraded. See each function's
// own doc comment in lua_spec_confirmed_stubs.cpp for the specific
// per-function reasoning.
//
// Registration shape mirrors thread_scheduler.h's own established
// convention exactly (registerOne(): 3 upvalues - EngineState*, HitLog*,
// stateTag - per real Lua CFunction, each one already a real C function
// implementing its own name, not a shared name-parameterized trampoline
// like stub_registry.h's generic path, since these 12 each have genuinely
// distinct behavior). Every call still logs into the SAME HitLog every
// other stub/hook uses (stub_registry.h/thread_scheduler.h), so these 12
// names keep appearing in the exact same whole-run stub-hit ranking
// tools/lua_host_run.cpp already reports - nothing about the MEASUREMENT
// path changes, only what happens on a real call.
#pragma once

#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/lua_c_api.h"
#include "sr3luahost/stub_registry.h"

namespace sr3luahost {

// Registers exactly the names in `namesToRegister` (a subset of
// specConfirmedStubNames() - see that function's own doc comment) into
// `L`, backed by `state` and logging into `log` under `stateTag`
// ("gameplay"/"ui" - see host.h's own top comment). A name not present in
// `namesToRegister` is left untouched by this call. Host (host.h) calls
// this once per state, passing each state's own real subset (these 22
// names do NOT all share one cluster tag - see lua_host.cpp for how the
// split is computed from the actual tagged registration list, never
// hardcoded here), AFTER filtering all 22 names out of the generic
// registerStubs() call for both states (so a plain-global lua_setglobal
// from either path can never race/overwrite the other for the same name).
void registerSpecConfirmedStubs(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                                 const std::vector<std::string>& namesToRegister);

// The literal set of 22 names this file registers - exposed so Host (or a
// test) can filter them out of the generic tagged-registration-list split
// without duplicating the literal list a second time.
const std::vector<std::string>& specConfirmedStubNames();

// Bare globals registered outside the tagged registration list
// (spec-lua-bindings.md Sec13.2/Sec16.3/Sec16.4: the 24 names registered by
// 0x00e0f900). Answered 2026-10-01 (jobs 20261001T020218-team-a-bgcx,
// 20261001T114555-team-a-lgdz): all 24, into BOTH states, before any preload.
// Each row is also in specConfirmedStubNames(); Host registers the rows
// through registerBareGlobals() (bare_globals.h) into the states they name,
// before every other registration.
struct SpecBareGlobal {
    std::string name;
    bool gameplay = false;
    bool ui = false;
    std::string spec; // section that confirms the state(s)
};
const std::vector<SpecBareGlobal>& specBareGlobals();

} // namespace sr3luahost
