// EngineState slice: mission context, triggers, script callbacks/hooks, boss battles, strongholds.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateMissionsData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateMissionsData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// ===================================================================
// Engine -> Lua callback firing (spec-lua-api-behaviour.md "Engine
// callback dispatch: death and destruction" / "...vehicle entry/exit,
// phone calls, triggers, group vehicles", 2026-10-14; implementation in
// src/lua_engine_callbacks.cpp). Every fire creates a deferred script
// thread through the spawner (Host installs it); nothing here calls Lua
// synchronously and no fire path clears a registration.
// ===================================================================
using CallbackSpawner = std::function<bool(const PendingCallback&)>;
struct EngineCallbackCounters {
    uint64_t fired = 0;      // fireEngineCallback calls with a non-empty name
    uint64_t accepted = 0;
    uint64_t lost = 0;       // missing global / full thread table (silent per spec)
};

// --- generic on_trigger "stalled wait" tracker (feature/auto-objective-
// trigger-vehicle-seat, 2026-10-12) ---------------------------------
// Mirrors GetDistStallInfo/recordGetDistCall() above, but for the
// SEPARATE wait-shape m09/m13 actually stall on: a mission script that
// waits on an on_trigger()/on_trigger_exit() callback to fire (e.g.
// m13.lua's own m13_goto_a_trigger, per results/verify_combined_with_
// seatedpose_20261010's own stall-point dump) never calls get_dist at
// all, so the get_dist tracker above cannot see it stall. runPlaytest()'s
// own per-frame in-world-trigger block (tools/sr3_viewer.cpp) calls
// recordTriggerWaitFrame() once per frame, naming the EARLIEST-registered
// trigger (triggerRegistrationOrder() below) that has geometry but has
// never produced an ENTER transition yet (triggerEverEntered() below) -
// the trigger-volume analogue of "the same get_dist pair, N frames
// running." A name CHANGE (a different trigger becomes the earliest
// still-pending one, e.g. because the previous one finally fired) resets
// the counter to 1, exactly like recordGetDistCall()'s own unordered-pair
// reset rule.
struct TriggerWaitStallInfo {
    std::string triggerName;
    uint64_t framesWaiting = 0;
};

// Sec27.25 `game_get_in_progress_type`'s other two branches (the minigame branch reuses catMouseMinigame()).
struct InProgressType {
    OpenValue<bool> activeMissionPresent{"0x014c8460 non-null and phase 0x014c7a14 != 8",
                                         "spec-lua-api-behaviour.md Sec27.25"};
    OpenValue<int32_t> activeMissionType{"active mission's own +0xa0 type field", "spec-lua-api-behaviour.md Sec27.25"};
    OpenValue<bool> activityPresent{"phase 6/7 with 0x014c8328 non-null", "spec-lua-api-behaviour.md Sec27.25"};
    OpenValue<int32_t> activityType{"that object's own +0xa0 field", "spec-lua-api-behaviour.md Sec27.25"};
};

// Sec28.4 `guardian_angel_enable_indicators`.
struct GuardianAngel {
    OpenValue<bool> modeIsThree{"0x006d2910 mode == 3 (via 0x00614d00, OPEN what the mode means)",
                                "spec-lua-api-behaviour.md Sec28.4"};
    OpenValue<bool> objectPresent{"0x00614cb0() non-null", "spec-lua-api-behaviour.md Sec28.4"};
    bool indicatorsEnabled = false; // the object's own +0x57d byte this batch writes; CHOSEN, write-only in scope

    // --- guardian_angel_start_zone_pinning / _clear_zone_pinning /
    // _release_attack_groups (Sec53.3, wave-2 batch W2-7, 2026-10-08) ---
    //
    // CONFIRMED (Sec53.3): the release runs UNCONDITIONALLY before the
    // name lookup - "an unknown or misspelled zone-set name releases
    // whatever is currently pinned and then pins nothing new" - so
    // `startZonePinning` below always clears currentZonePinSetName
    // first, THEN attempts the new pin. CONFIRMED: "a zone set with
    // more than 8 zones pins nothing at all, yet the function still
    // records that set as 'the current pin' internally" - a real
    // state/reality mismatch this project reproduces via the separate
    // zonePinSucceeded flag (the name is recorded either way; whether
    // it actually took effect is tracked honestly, not conflated).
    // Real zone-SET membership/size data lives in `.czn_pc`-adjacent
    // zone tables this project has no by-name resolver for (same
    // "no real zone data" gap as elsewhere) - zone-set SIZE is test-
    // settable (OpenValueMap<int32_t>, OPEN until set) rather than
    // invented; a real call against an unregistered name throws
    // OpenStateError (honest refusal), matching this project's
    // established convention for "needs real zone data this project
    // doesn't have."
    std::string currentZonePinSetName;    // "" = none pinned (also the post-release transient state)
    bool zonePinSucceeded = false;        // false when size > 8 (recorded as current anyway) or never pinned
    OpenValueMap<int32_t> zoneSetSize{"guardian-angel zone-set size (zone-table data this project has no resolver for)",
                                      "spec-lua-api-behaviour.md Sec53.3"};
    uint64_t releaseAttackGroupsCount = 0; // guardian_angel_release_attack_groups: resolved in full per spec,
                                            // no further internal detail given - call count only, same "record
                                            // that a call happened" convention used throughout this header.
};

// Sec28.15 `crib_unlock_strongold`.
struct Stronghold {
    OpenValueMap<bool> stillLocked{"stronghold byte +0x7a bit 0x2 set ('still locked')",
                                   "spec-lua-api-behaviour.md Sec28.15"};
    uint64_t unlockedCount = 0;             // host-branch, bit was set
    uint64_t clientOrSoloRequestCount = 0;  // the "without a session or as client" branch - internals OPEN, counted only
};

// Sec28.24 `boss_battle_matt_get_cheat` / Sec28.25 `boss_battle_matt_cheats_start`, the Sec24.1
// `boss_battle_matt_cheats_stop` cluster (0x012ec71c.. 0x012ec73c).
struct BossBattleMatt {
    // 0x012ec730: the cheat-slot sentinel Sec28.24 reads. Its only writer in any spec read so far is
    // Sec24.1's `boss_battle_matt_cheats_stop`, out of this batch's own scope - stays OPEN here.
    OpenValue<int32_t> cheatSlot{"0x012ec730 (cheat-slot sentinel; writer is Sec24.1's cheats_stop, out of this batch's scope)",
                                 "spec-lua-api-behaviour.md Sec28.24"};
    // 0x012ec734/0x012ec738/0x012ec73c, CHOSEN, write-only in scope. n2/n3 are stored as the raw lua_tonumber
    // doubles forwarded to 0x005e59f0 - Sec28.25's own text does not state they are rounded.
    int32_t lastId = 0;
    double lastN2 = 0.0, lastN3 = 0.0;
    bool active = false;                        // 0x012ec724
    OpenValue<int32_t> retryCounter{"0x012ec720", "spec-lua-api-behaviour.md Sec28.25"};
    static constexpr int32_t kRetryLimit = 4;     // 0x012ec71c, CONFIRMED static constant
    int64_t deadlineMs = 0;                       // 0x012ec728, CHOSEN, write-only in scope

    // --- boss_battle_matt_set_player_as_avatar / boss_battle_matt_
    // hide_wings (Sec30.2, "ranking tranche 03"), batch 2026-10-08
    // (Batch 7, dedicated agent) -----------------------------------

    // Global 0x0268d2d0 bit 0: written UNCONDITIONALLY by set_player_
    // as_avatar per its own input flag (CONFIRMED), regardless of the
    // worker's own outcome.
    bool avatarModeBit = false;
    // The two further gated lookups the worker performs beyond
    // hasLocalPlayer()/playerRig().coopPlayerPresent (both reused from
    // Sec10.3/Sec28.12 above, not duplicated): "npc_bahamut" resolving
    // (become-avatar path) and the model itself resolving after the
    // customization item is applied (a textually SEPARATE failure
    // point per Sec30.2's own 2 sentences: "not found -> false" /
    // "...failure -> false"); "StyleTest_PC" resolving (restore path).
    // This project has no real customization-item/model-resolution
    // table (same "no real data table" gap already established
    // elsewhere, e.g. vehicle_set_sirenlights's per-class allow flag) -
    // OPEN until set.
    OpenValue<bool> npcBahamutResolves{"\"npc_bahamut\" customization item lookup resolves",
                                       "spec-lua-api-behaviour.md Sec30.2"};
    OpenValue<bool> avatarModelResolves{"avatar model resolve (post customization-item apply)",
                                        "spec-lua-api-behaviour.md Sec30.2"};
    OpenValue<bool> styleTestPcResolves{"\"StyleTest_PC\" customization item lookup resolves",
                                        "spec-lua-api-behaviour.md Sec30.2"};
    // Global byte 0x012ec740, host-side only (CONFIRMED gated on
    // coopLocalIsHost()): 1 on a successful become-avatar, 0 on a
    // successful restore. Never written on any other path.
    bool hostAvatarActiveByte = false;
    uint64_t becomeAvatarSuccessCount = 0; // test-observable
    uint64_t restoreSuccessCount = 0;      // test-observable

    // boss_battle_matt_hide_wings: per-entry hide/show of every
    // attached object whose own +0xe0 record hashes to "Avatar_wings"
    // (Sec30.2) - this project has no real attached-object list
    // (same "no generic entity shape invented" boundary), so only the
    // last flag value is tracked, test-observable. Word global
    // 0x012ec742 (Matt's own 16-bit object id, hide + host only, never
    // cleared on show) has no real object-id scheme in this project -
    // only the "was it stored" fact is tracked (same "the write is
    // real, the sub-object is not separately modelled" convention as
    // VehicleState::unjackableSecondarySubObjectFlag).
    bool wingsHidden = false;
    bool wingsHideHostMarkerSet = false;

    // --- boss_battle_matt_qte_begin / boss_battle_matt_set_phase
    // (Sec59.5, wave-2 batch W2-7, 2026-10-08) -----------------------
    //
    // CONFIRMED (Sec59.5): set_phase "stores ANY value with no bound
    // against the table's own 4-entry size and never resets it even
    // when it rejects the value as out-of-range" - so `phase` below is
    // a bare, unclamped int32_t (CHOSEN initial value 1, "phase one" -
    // no construction-time default is stated). HARDENED
    // (host-safety, this project's own choice, not a spec fact): the
    // real engine's own phases 5-8 read whatever garbage sits past the
    // 4-entry name table and phases >= 9 hand a non-pointer number to a
    // string-compare routine and fault - this project has no such
    // adjacent memory to read, so qte_begin below refuses (does not
    // start a QTE) for any phase outside [1,4], counting the refusal
    // via qteBeginOutOfRangePhaseGuardCount rather than fabricating
    // what the garbage name would have been.
    int32_t phase = 1;
    static constexpr int32_t kPhaseTableSize = 4;
    uint64_t qteBeginOutOfRangePhaseGuardCount = 0;
    // HARDENED: real engine reads straight through a null "Matt" object
    // when the resolver's liveness-rejecting result is mishandled
    // (Sec59.5 path 1) - this host instead refuses to start the QTE
    // when "Matt" does not resolve as a live object (objectResolves()),
    // counted here.
    uint64_t qteBeginMattNotLiveGuardCount = 0;
    // HARDENED: real engine writes a completion hook through the
    // literal fixed address 0x1f00 when the player argument doesn't
    // resolve to a proper character (Sec59.5 path 3) - this host
    // refuses instead, counted here.
    uint64_t qteBeginUnresolvedPlayerGuardCount = 0;
    // Matt's OWN QTE-in-progress flag - NOT the same flag the
    // "Killbane QTE-active flag" boss_battle_kb_enable/boss_battle_
    // mars_killbane_active (Sec45.5) share (a DIFFERENT boss fight,
    // Killbane, tracked separately as killbaneQteActive_ below) -
    // kept apart deliberately so these two bosses' own state never
    // gets conflated. CHOSEN false initial (no construction value
    // stated).
    bool qteActive = false;
};

// --- Mission context shared by mission_end_failure (Sec1.5) and
// mission_set_checkpoint (Sec1.6); the mission-end-transition timer
// (0x012f2630) is ALSO read by character_is_dead's own override
// (Sec3.5) - all three cite the identical real globals, cross-checked
// against each other by this same batch, not independently guessed per
// function. ---------------------------------------------------------
struct MissionContext {
    // 0x014c8460 non-null AND phase 0x014c7a14 != 8 (CONFIRMED - both
    // Sec1.5 and Sec1.6 test the exact same two globals the same way,
    // collapsed into one combined predicate, the same convention this
    // project's own InProgressType::activeMissionPresent, Sec27.25,
    // already established for the identical pair - a SEPARATELY SCOPED
    // instance, not reused, since that struct is out of this batch's
    // own 12-section scope to touch). OPEN until a test sets it, no
    // spec gives a start-up value.
    OpenValue<bool> activeAndRunning{"mission context active, phase != 8 (0x014c8460/0x014c7a14)",
                                     "spec-lua-api-behaviour.md Sec1.5/Sec1.6"};
    // mission_end_failure's own identity-gate condition (Sec1.5, shared
    // verbatim by mission_end_success/Sec6.12 and mission_end_to_
    // activity/Sec6.20 - all three read this same field): arg 1 must
    // name the SAME mission this global currently holds (this project's
    // "key by name string" simplification standing in for real pointer
    // identity, per this header's own top note).
    //
    // FIX 2026-10-08 (mission-context-name task, second-order blocker
    // behind probe_blockers_ranked_20261008_post_resolver_merge.md's own
    // "mission context phase != 8" item, 9/49 missions - dlc1_mm_01..06/
    // mm_p_02/04/05): this field was left permanently OPEN with NO
    // production writer anywhere - EngineState::bindMissionPhaseMachine()
    // (0x006d6920, "bind mission") sets missionContext_.boundMissionName
    // (the phase machine's own identity stand-in) but never touched this
    // SEPARATE field, so every real mission script that reached
    // mission_end_failure/mission_end_success/mission_end_to_activity
    // threw OpenStateError reading an OPEN value instead of getting a
    // real (possibly gate-failing, never-throwing) answer. Sec21.27a
    // CONFIRMS 0x014c8460's first dword (the SAME global this field
    // stands in for) is written by exactly one function, 0x006d6920, at
    // the exact instant phase goes to 8 - the one real bind-time write
    // bindMissionPhaseMachine() already models. Now set THERE (see that
    // method, below), at the same real call site, not a second
    // independent write path - no longer OPEN once any mission has
    // bound.
    OpenValue<std::string> currentMissionName{
        "current mission name (pointer-identity stand-in, 0x014c8460's own target)",
        "spec-lua-api-behaviour.md Sec1.5"};
    // mission_end_failure's OTHER identity-gate condition (Sec1.5, same
    // sharing as currentMissionName above): the CURRENT mission's own
    // liveness check must come back DEAD (its +0x33 bit 0x4 set, or
    // +0x34 == 0xff - "already inside its own ending/destroy
    // transition") - "a mission 'found and still fully alive' does NOT
    // pass" (Sec1.5's own text).
    //
    // FIX 2026-10-08 (same task/root-cause as currentMissionName above):
    // also now set by bindMissionPhaseMachine(), to false ("alive, not
    // yet tearing down") - the one honestly-reachable real value at the
    // exact instant a mission is freshly bound (a just-created handle's
    // own liveness bits cannot simultaneously read "being destroyed" or
    // "freed"), the SAME "OPEN facade -> CHOSEN real reachable value"
    // fix already applied to activeAndRunning (EngineState::EngineState()
    // 's own doc comment). This makes the gate fail CLOSED (silent
    // no-op, matching Sec1.5's own "still fully alive does NOT pass"
    // text) rather than throw, for every mission that has bound but not
    // yet begun tearing down - which is every real mission this
    // project's probe drives today, since no spec passage anywhere in
    // Sec1.5/Sec21.27a/Sec21.27b ties this liveness bit to the real
    // phase counter (6/7) or to any other event this project can trace -
    // genuinely OPEN past this point, left test-settable (.set(true))
    // for a test that wants to exercise the gate's full-pass path,
    // rather than invented.
    OpenValue<bool> currentMissionEnding{"current mission liveness bit (+0x33/+0x34, 'already ending')",
                                         "spec-lua-api-behaviour.md Sec1.5"};
    // The debounce pair mission_set_checkpoint's own dispatch
    // (0x006d5060) reads (Sec1.6: "two adjacent cooldown/timer objects,
    // 0x012f2630/0x012f2634 ... both NOT still counting down"); timer 1
    // is ALSO the exact mission-end-transition timer mission_end_failure
    // arms (via its own final dispatch, 0x006da400 - independently
    // identified elsewhere in this project as "the mission-end routine"
    // that arms this same global for 2,000-4,000ms, Sec3.5's own
    // citation) and character_is_dead's override reads (Sec3.5). This
    // project drives no real countdown clock (nothing in scope
    // decrements either timer once armed) - a plain armed/idle bool
    // stand-in for each, CHOSEN false ("idle") at construction: timer 2's
    // own real writer is never identified anywhere in Sec1.6's own text
    // (OPEN, out of scope), so nothing in this closed world ever arms
    // it; timer 1 starts false for the same "nothing has happened yet"
    // reason, becoming true only via armMissionEndTransitionTimer()
    // below, this project's own single in-scope writer.
    bool missionEndTimer1Armed = false; // 0x012f2630 - failure's own timer, armed by armMissionEndTransitionTimer() below
    // FIX (Sec21.27c, 2026-10-09): no longer "OPEN who arms this" - full
    // disassembly of the shared presentation dispatch 0x006da400 shows
    // mission_end_success arms THIS timer (armMissionEndSuccessTimer()
    // below) - a SEPARATE timer from failure's own timer 1, correcting
    // this file's prior (batch b29) assumption that success shared
    // timer 1 purely because both callers call the same dispatch
    // function. See dispatchMissionEndPresentation() below.
    bool missionEndTimer2Armed = false; // 0x012f2634 - success's own timer
    // Test-observable count of successful mission_end_failure dispatches
    // (gate passed, 0x006da400's FAILURE branch actually taken - i.e.
    // NOT a type-2-coerced call, see
    // missionEndFailureCoercedToSuccessCount below) - not a spec fact,
    // just a plain call counter for test convenience, same convention
    // as partyDismissAllCount above.
    uint64_t missionEndFailureDispatchCount = 0;
    // mission_end_success (Sec6.12, batch b29, 2026-10-08): CONFIRMED
    // to share mission_end_failure's own gate (above); its own final
    // dispatch (0x006da400) is the SAME shared function failure calls,
    // but FIX (Sec21.27c, 2026-10-09, full disassembly of that shared
    // function): it arms its OWN separate timer (missionEndTimer2Armed
    // above), not failure's timer 1 as previously assumed - kept as a
    // SEPARATE counter so a test can tell the two callers apart
    // regardless.
    uint64_t missionEndSuccessDispatchCount = 0;
    // mission_end_success's own arg 3 (polymorphic: a table's first two
    // indexed elements, or a plain string's one slot, or absent):
    // CONFIRMED each resolved name's position-pair would anchor the
    // success UI, but this project tracks no real transform/position
    // data (same "only WHICH branch ran" convention mission_set_
    // checkpoint, Sec1.6, already established) - only whether each slot
    // resolved is recorded.
    bool lastMissionEndSuccessSlot1Resolved = false;
    bool lastMissionEndSuccessSlot2Resolved = false;
    // mission_end_to_activity (Sec6.20, batch b29): CONFIRMED to share
    // the SAME gate, then (gate passing) attempts arg 2's own "activity"
    // object-kind resolve; 0x006da960 copies position-pair into two
    // globals ONLY when (resolved AND host) - this project has no real
    // transform data, so only whether that capture happened is counted;
    // 0x006da7b0 itself runs unconditionally once the gate passes
    // (CONFIRMED, Sec6.20's own text), tracked as its own counter since
    // its own internal behaviour is OPEN (Sec6.20).
    uint64_t missionEndToActivityPositionCaptureCount = 0;
    uint64_t missionEndToActivityDispatchCount = 0;

    // --- Sec21.27c upgrade (new task, 2026-10-09): "0x006da400 opened
    // in full" - see dispatchMissionEndPresentation() further below for
    // the one real model of that shared function, and each field's own
    // doc comment just below for citations. ----------------------------
    std::string lastFailReasonText;          // 0x014c7a08 - CONFIRMED write; renderer OPEN (see below)
    std::string lastSuccessAnnouncementText;  // 0x014c846c - CONFIRMED write; renderer OPEN (see below)
    // Sec21.27c's own text flags two items as explicitly OPEN (not
    // guessed): who creates a NEW fail-dialog instance (0x006da400 only
    // ever closes a STALE one, via the existing 4-slot modal-dialog
    // pool 0x02282d40/0x007c1dd0/0x007c31b0 already tied to the
    // "dialog_build" Lua hook - no creation call found inside this
    // function itself); and what, on a later frame, actually RENDERS
    // either text global above (most plausibly the separately-
    // documented cmp_mission_success native UI entry, Sec14.3, not
    // itself Lua-dispatched - not traced end-to-end). This project has
    // no in-scope dialog-pool/renderer simulation to hook either OPEN
    // item into, so neither gets an invented field - only the two
    // writes Sec21.27c itself confirms happen are modelled, above.

    // CONFIRMED mechanism (Sec21.27c): failure's HUD-screen-teardown/
    // fade effect is gated on a flags-byte bit (0x014c848c & 0x8) - no
    // spec-given start-up value for that byte, so this is OPEN until a
    // test sets it (same "CONFIRMED mechanism, OPEN value" discipline
    // as every other not-given flags byte in this file, e.g.
    // MissionCheckpoint::usePlayerVehicleTransform above), NOT guessed
    // true or false by default.
    OpenValue<bool> hudTeardownGateBit{"mission-end flags byte 0x014c848c bit 0x8 (HUD-teardown gate)",
                                       "spec-lua-api-behaviour.md Sec21.27c"};
    // CHOSEN representation (this task, explicitly invited by its own
    // brief): stands in for the real 0x007b4990(-1) "tear down EVERY
    // currently active HUD screen" call on the generic HUD-screen
    // registry 0x012fced8 - this project has no real HUD-screen
    // registry to simulate (explicitly out of scope per the brief), so
    // only whether/how-often the teardown-ALL primitive fired is
    // tracked - same "count the call, not the subsystem" convention as
    // waypointRemoveCount/notorietyUiMeterResetCount above.
    uint64_t hudTeardownCount = 0;
    // Test-observable summary of the MOST RECENT
    // dispatchMissionEndPresentation() call: true only when that call
    // fired the teardown-all primitive. CONFIRMED asymmetry (Sec21.27c):
    // success NEVER sets this true, regardless of the gate bit above -
    // only the (non-coerced) failure path can.
    bool lastDispatchTornDownHud = false;

    // NEW finding, CONFIRMED by raw push-order/branch structure
    // (Sec21.27c, 2026-10-09): for mission TYPE 2 specifically, a call
    // to mission_end_failure is silently coerced into
    // 0x006d9f70(mission, 1) - hardcoded SUCCESS via the SAME
    // end-of-mission super-dispatcher missionEndDispatch() below
    // already models - skipping ALL of dispatchMissionEndPresentation()'s
    // own fail-text/debounce/HUD-clear logic entirely (that function is
    // never even entered on this path). Test-observable count of how
    // many times this coercion actually fired.
    uint64_t missionEndFailureCoercedToSuccessCount = 0;

    // CONFIRMED (Sec21.27c): timer 1 (0x12f2630) arms for 2000ms
    // normally, or 4000ms "if a second mission-end fires while the
    // first is cooling down" - modelled as "while timer 1 is already
    // armed from an earlier, not-yet-cleared dispatch." This project
    // drives no real countdown clock (same "armed/idle bool stand-in"
    // limitation this struct's own top note already states) - this
    // field records which of the two durations the most recent arm
    // actually used, for test observability only.
    int32_t missionEndTimer1LastArmedDurationMs = 0; // 0 = never armed; else 2000 or 4000

    // --- Sec21.27a/21.27b upgrade (this task, 2026-10-08): the REAL
    // 0x014c7a14 phase counter (int, 0-8) and the data the REAL end-of-
    // mission dispatcher (0x006d9f70, Sec21.27b) needs - additive to
    // everything above. `activeAndRunning` stays EXACTLY as it was (a
    // couple dozen existing tests call its own .set() directly; its own
    // meaning, "phase != 8," is unchanged) rather than being silently
    // re-derived from `phase`, which would change behaviour for a test
    // that sets ONLY activeAndRunning and never touches the real phase
    // machine at all. EngineState::bindMissionPhaseMachine() (below) is
    // the one production path that keeps both the real phase AND every
    // legacy facade consistent; a test exercising the legacy facades
    // alone is unaffected either way.
    //
    // Mission TYPE (the bound object's own +0xa0 field, 0-3, Sec21.27a/
    // Sec21.27b) has no real per-mission population table anywhere in
    // this project (no mission-object loader reads a real +0xa0 value
    // in scope) - EngineState::bindMissionPhaseMachine()'s own `type`
    // parameter defaults to a CHOSEN 0 (Sec21.27b's own type-0/3
    // "shared handler" branch - the most common/generic case, per this
    // task's own explicit instruction), overridable per-call for a test
    // that wants to exercise type 1/2's own distinct branches.
    int32_t phase = 0;                    // 0x014c7a14 - CONFIRMED real start-up value (0x006d6010)
    bool missionBound = false;            // 0x014c8460 non-null
    std::string boundMissionName;         // pointer-identity stand-in, same convention as currentMissionName above
    int32_t boundMissionType = 0;         // the bound object's own +0xa0 field (0-3) - CHOSEN default 0, see above
    bool missionStartMarkerArmed = false; // Sec21.27a phase 2/5 - no real HUD marker/checkpoint-icon object in scope; test-observable only
    uint64_t phaseTransitionCount = 0;    // test-observable count of setMissionPhase() writes, including its own internal self-loop
    // missionEndDispatch() (0x006d9f70, Sec21.27b) bookkeeping - see
    // that method's own doc comment for the full per-branch citation.
    uint64_t missionEndDispatchCount = 0;
    bool lastMissionEndDispatchSuccessFlag = false;
    uint64_t missionEndType1UnlockAndCompleteCount = 0;   // type 1: fires the completion writer AND mission_unlock's own setter (Sec21.27b)
    uint64_t missionEndType2CompleteAndOpenStubCount = 0; // type 2: fires the completion writer; 0x006d9470 itself stays OPEN, counted only
    uint64_t missionEndType0Or3ScriptHookAttemptCount = 0; // types 0/3: a +0x1f8 script-hook ATTEMPT would be made here - no real +0x1f8 name table in scope (same gap as the already-documented +0x1f0/script_mission_start real-name lookup, Sec36.4), counted only
    uint64_t missionEndSilentAutoSuccessCount = 0; // 0x006db7fa fired via missionSilentClass() below
};

// mission_set_cancel_warp_location (Sec6.21, batch b29, 2026-10-08).
// CONFIRMED: both args optional strings (standard nil-gated idiom,
// default absent), each resolved via objectResolves() (0x005982e0, the
// SAME third-tier resolver door_open/teleport's own destination use,
// Sec1.10/Sec4.13) gated by the SAME "mission active, phase != 8"
// predicate MissionContext::activeAndRunning above already models (the
// identical two globals, 0x014c8460/0x014c7a14, per Sec6.21's own
// cross-reference). Both absent/unresolved -> clears the override
// (0x006cf050); BOTH resolved -> sets it (0x006d1960); EXACTLY ONE
// resolved -> CONFIRMED no-op, neither path runs. This project has no
// real `+0x4c` field/transform data to read off either resolved object
// (the same OPEN item Sec6.21's own text leaves for teleport, Sec4.3,
// too) - only which of the three outcomes occurred is recorded.
struct MissionCancelWarpLocation {
    bool set = false; // true after a successful two-name set, false after a clear or at construction
    std::string fromName;
    std::string toName;
    uint64_t setCount = 0;
    uint64_t clearCount = 0;
    uint64_t partialNoOpCount = 0; // test-observable only - exactly one of the two names resolved
};

// mission_set_checkpoint (Sec1.6)'s own record-keeping. Kept separate
// from MissionContext above since it is scoped to this one function's
// own arguments/effects, not shared with mission_end_failure/
// character_is_dead.
struct MissionCheckpoint {
    // Stashed via the mission-context bookkeeping call (0xbd9c10,
    // Sec1.6) - test-observable only, the real bookkeeping effect
    // itself is not modeled further (no in-scope reader).
    std::string lastName;
    uint64_t setCount = 0;
    // arg 2 == false's own further gate (Sec1.6): the fixed, hardcoded
    // checkpoint-vehicle object (literal hash 0x31d152c, singleton
    // 0x24433a8) resolves AND the current default character is
    // confirmed inside it via the ALREADY-CONFIRMED occupancy predicate
    // 0x009b9290 (Sec3.10) - collapsed into one combined, derived
    // predicate since the resolved object's own real-world name is
    // explicitly NOT pinned to a human-readable string this project
    // could key a normal objectResolves()/getOrCreateCharacter() lookup
    // by (Sec1.6's own text: "HIGH CONFIDENCE... not independently
    // pinned to a human-readable name"). OPEN until a test sets it;
    // only read when arg 2 is false - the spec's own short-circuit ("if
    // arg 2 is true... the function always snapshots from the generic
    // default-character accessor" without consulting this at all), so
    // this project's stub mirrors that short-circuit exactly and never
    // forces a test to set this when arg 2 is true.
    OpenValue<bool> usePlayerVehicleTransform{
        "fixed checkpoint-vehicle resolves and local player occupies it "
        "(0x31d152c/0x24433a8/0xad3340/0xad3350/0x009b9290)",
        "spec-lua-api-behaviour.md Sec1.6"};
    bool lastUsedVehicleTransform = false; // which branch the last successful call actually took - test-observable
    bool lastLoadoutSlotsZeroed = false;   // arg 3's own branch (Sec1.6) - test-observable; no real template data loaded (see the stub's own doc comment)
};

// --- Trigger-volume geometry + the real per-frame overlap test
// (spec-lua-api-behaviour.md Sec1.14, 2026-10-09 batch) -------------
//
// Sec1.14 CONFIRMS the RUNTIME trigger handle's own field layout (shape
// code `+0x8c`: 0=oriented box, 1=sphere, 2=capped cylinder; position
// `+0x40`; box half-extents/cylinder axis endpoint `+0x58`/`+0x5c`/
// `+0x60`; radius `+0x90`; length `+0x94`) and the real per-frame poll
// mechanism's own SHAPE (a tracked "currently inside" set - enter/exit
// is a set-membership TRANSITION, never a steady per-frame refire).
// This struct/test models exactly that CONFIRMED shape.
//
// RESOLVED 2026-10-10 (spec-lua-api-behaviour.md Sec1.14's own "On-disk
// gaps - RESOLVED 2026-10-10" paragraph, cross-referenced from spec-zone-
// data-format.md Sec11.7's own "A third class resolved" paragraph):
// `Trigger_Volume`'s class-specific tail-property parser (vtable `+0x04`,
// `0x0093e520`) is now read in full. The on-disk encoding: `trigger_shape_
// type` is an authored STRING resolved through a name-to-enum lookup into
// the same runtime shape code `+0x8c` (0/1/2); `trigger_shape_param1`/
// `trigger_shape_param2` are then read depending on that code - box
// (0): BOTH as 3-component float vectors, the box's own local-space
// min/max corner (`+0x90`-`+0x98`/`+0x9c`-`+0xa4`), independently
// cross-confirmed against the object's own bounding-box virtual method;
// sphere (1): param1 only, a scalar f32 radius (`+0x90`), param2 not
// read; cylinder (2): both scalars, param1=radius (`+0x90`), param2=
// length (`+0x94`). This project reads this real data via sr3zone's own
// TriggerShapeType()/TriggerShapeParam1()/TriggerShapeParam2()/
// DecodePropertyAsFloat()/DecodePropertyAsVec3() (include/sr3zone/
// zone_objects.h) plus TriggerShapeCodeForTypeString()'s own empirical
// (real-population-scan-grounded, NOT guessed - see that function's own
// doc comment) string-to-enum mapping - Sec1.14 itself does not give the
// on-disk string's exact spellings ("literal shape-name strings not
// traced to full depth"), so this project determined them the only
// clean-room-safe way available to it: by reading real `.czn_pc` data
// (tools/zone_object_resolver_probe.cpp), never by disassembling the
// name-to-enum lookup itself. **Still genuinely OPEN, per Sec1.14's own
// text, and therefore NOT modelled here either:** the cylinder's second
// axis-endpoint field (`+0x58`-`+0x60`, used by the precise cylinder
// containment test but written by neither the tail parser nor the
// constructor) - `ex`/`ey`/`ez` below stay a CHOSEN caller-supplied value
// for a cylinder trigger specifically, exactly as before this task;
// only the box/sphere/cylinder radius+length fields moved from CHOSEN to
// REAL. registerTriggerGeometry() still takes a caller-supplied
// geometry - the REAL loader (tools/sr3_viewer.cpp's own runPlaytest())
// now populates it from real `.czn_pc` tail properties instead of a
// flat CHOSEN sphere stand-in, but a geometry is still constructed and
// handed in by a caller, not read directly by this struct.
struct TriggerVolumeGeometry {
    double px = 0, py = 0, pz = 0;     // +0x40 - real, from sr3zone::ObjectPosition() when known
    int shapeCode = 1;                 // +0x8c - CONFIRMED values 0/1/2 only; CHOSEN default 1 (sphere)
    // Box only (shapeCode==0): REAL on-disk local-space min/max corner
    // (trigger_shape_param1/param2), relative to (px,py,pz) - NOT a
    // symmetric half-extent around the position (an authored box's own
    // min/max corner need not be centred on its placed-object origin).
    double boxMinX = 0, boxMinY = 0, boxMinZ = 0;
    double boxMaxX = 0, boxMaxY = 0, boxMaxZ = 0;
    double ex = 0, ey = 0, ez = 0;      // +0x58/+0x5c/+0x60 - cylinder's second axis endpoint
                                        // ONLY (box no longer uses this - see boxMin/boxMax
                                        // above); still CHOSEN/caller-supplied - Sec1.14's own
                                        // "true source not identified" OPEN item, unresolved by
                                        // this task.
    double radius = 0;                 // sphere: param1/+0x90; cylinder: param2/+0x94 (f752324)
    double length = 0;                 // cylinder axis length: param1/+0x90 (f752324), not
                                        // separately consumed by the containment test below
                                        // (which uses the two axis endpoints directly) - Sec1.14
                                        // does not spell out its exact relationship to the
                                        // endpoint pair, so nothing here guesses at one.
    bool positionKnown = false;        // true once a real zone-data position was applied
    bool shapeKnown = false;           // true once real on-disk trigger_shape_type/param1/param2
                                        // were decoded for this trigger (vs. a CHOSEN fallback)
};

// mission_set_excluded_reward (Sec14.8): SEPARATELY SCOPED from
// EngineState::MissionContext::activeAndRunning above (same real pair
// of globals, 0x014c8460/0x014c7a14, but this project's own established
// convention - see that field's own doc comment - is to NOT merge
// across call sites unless a spec explicitly confirms shared scope).
struct MissionExcludedRewardsState {
    OpenValue<bool> activeAndRunning{
        "mission active, phase != 8 (0x014c8460/0x014c7a14) - mission_set_excluded_reward's own gate",
        "spec-lua-api-behaviour.md Sec14.8"};
    OpenValue<bool> descriptorBit8At0xc{"per-object-type descriptor bit 0x8 at +0xc (0x02cc9900)",
                                        "spec-lua-api-behaviour.md Sec14.8"};
    std::vector<uint32_t> excludedRewardHashes; // de-duped, CRC-32 (sr3save::nameHash) of the reward name
    uint64_t dedupSkipCount = 0;
};

// boss_battle_kb_enable_crowd_props (Sec22.27): CHOSEN minimal stand-in
// for the real 6-named-marker/8-global-struct "crowd prop spawn point"
// table (not previously catalogued, Sec22.27's own text) - only the
// CONFIRMED top-level facts (enabled state, density argument) are
// tracked, since no in-scope reader needs the marker table itself.
struct BossBattleKbCrowdProps {
    bool enabled = false;
    double density = 1.0; // Sec22.27's own nil-gated-argument default
    uint64_t enableCount = 0;
    uint64_t disableCount = 0;
};

// on_random_obj_killed_do (Sec24.22): CHOSEN minimal record (resolved
// object-category name, callback name, the 4-way category selector) -
// the 4 category-specific dispatch handlers' own internals are OPEN.
struct RandomObjKilledHook {
    std::string objectCategoryName;
    std::string callbackName;
    int32_t category = 0;
};

// --- Sec55.2 game_cancel_mission: 3 distinct crash paths in the real
// engine (null mission pointer; dialog-pool exhaustion; unhandled type) -
// all 3 hardened to a safe no-op here, each counted.
struct MissionCancelState {
    OpenValue<int32_t> inProgressType{"active mission in-progress type", "spec-lua-api-behaviour.md Sec55.2"};
    bool missionPointerValid = false; // CHOSEN: whether a real mission object exists for non-diversion types
    uint64_t nullMissionGuardCount = 0;
    uint64_t dialogPoolExhaustedGuardCount = 0;
    uint64_t unhandledTypeGuardCount = 0;
    uint64_t diversionCancelCount = 0;
    uint64_t confirmDialogOpenCount = 0;
};

// =====================================================================
// Batch W2-8 (2026-10-08), wave-2 fan-out: spec-lua-api-behaviour.md
// §38-60 "gameplay B" (character/NPC/inventory/party/HUD/minimap/
// mission-flow/hooks), 93 names split across
// lua_spec_confirmed_stubs_b19_wave2_char_npc_inv.cpp (55 names) and
// lua_spec_confirmed_stubs_b20_wave2_hud_minimap_hooks.cpp (38 names).
// Both files are new translation units in the existing bNN split
// (lua_spec_confirmed_stubs_registry.cpp's own top comment); their
// shared EngineState additions live in this ONE block since
// engine_state.h is a single header both files include. Every new
// per-object field below is a NEW, STANDALONE map/struct (never an
// insertion into CharacterState/VehicleState's own bodies) - this
// project's own established convention for a later feature needing new
// per-object state (cf. missionCellphoneAudio_/spawnRegionFilterBits_/
// aiPersonalityProfileCodes_ above), chosen here specifically to avoid
// a high-conflict-risk edit to either of those two very large structs.
// =====================================================================

// --- Shared on_* hook-name pool (0x00a1fcc0, spec-lua-api-behaviour.md
// §42.3/§42.7/§47) - a ~350-slot fixed name->id registry with
// insertion-on-miss, used by on_dismount/on_hit_ped/on_mount/
// on_vehicle_debris_flow_recycled/on_m21_player_choice/
// on_mission_item_drop (all 6 registered by THIS batch - this is the
// first implementation of the pool, not a reuse of a prior one; the
// spec's own "already documented" phrasing at §42.7/§47 means
// documented IN THE SPEC TEXT, not already coded). Modelled the SAME
// way this project's existing internHudSecondaryKey() (Sec12.1,
// 0x00a1fcc0's OTHER already-coded call site, a narrower per-feature
// version of the identical real mechanism) already chose: sequential
// id assignment starting at 1 on first use, CHOSEN (the real pool's
// own starting id/base is not given) - NOT a literal hash-collision
// simulation (§42.7's "matches purely by name hash, two different
// names with an equal hash could share a slot" quirk is a documented
// but UNMODELED real-engine hazard, the same simplification already
// made for internHudSecondaryKey; reproducing the real hash would
// need the real 0x00a1fcc0 algorithm, which is OPEN). CORRECTED
// 2026-10-08 (review of this batch): the real pool has TWO distinct,
// separately-sentinelled failure modes, not one - §22.11's own
// raw-disassembly re-confirmation of 0x00a1fcc0 is explicit that table
// EXHAUSTION (no free slot left once kCapacity, 350, CONFIRMED real
// pool size, §42.7/§47, names are already interned) "returns -1 on
// exhaustion", while §42.7 separately documents that a STRING-
// ALLOCATION FAILURE during registration (a transient allocator
// failure while copying the name, unrelated to how many names are
// already interned) "stores the sentinel id 0 rather than the
// 'failed' sentinel -1." These are two genuinely different real
// conditions the spec text itself distinguishes by sentinel value, not
// one merged failure mode - a prior version of this comment/code
// conflated them (claiming -1-shaped capacity exhaustion returns 0,
// "not a CHOSEN hardening divergence"), which was simply wrong, not a
// documented simplification. Modelled here: reaching kCapacity on a
// new name is the EXHAUSTION case and returns -1 (matching §22.11's
// own sentinel exactly - a real, testable capacity limit, not a
// hardening divergence). The separate string-allocation-failure path
// (sentinel 0) has no structural equivalent in this project - nothing
// here ever models std::string/unordered_map allocation failing, and
// no other OpenValue-style field in this codebase fabricates an
// injectable allocation-failure hook - so it is simply not reachable,
// not silently dropped (same "no invented fixes" discipline as every
// other OPEN gap in this header).
struct HookNamePool {
    static constexpr size_t kCapacity = 350;
    int32_t intern(const std::string& name) {
        auto it = ids.find(name);
        if (it != ids.end()) return it->second;
        if (ids.size() >= kCapacity) { ++exhaustionCount; return -1; } // §22.11: -1 on table exhaustion
        int32_t id = nextId++;
        ids[name] = id;
        return id;
    }
    std::unordered_map<std::string, int32_t> ids;
    int32_t nextId = 1;
    uint64_t exhaustionCount = 0; // §22.11 table-exhaustion sentinel (-1) - NOT §42.7's distinct
                                   // string-allocation-failure sentinel (0), which this project
                                   // doesn't model (see doc comment above)
};

// --- flashpoint_start_mission/_mission_status (§45.2/§55.5). No
// world-flashpoint-list model exists in this project (OPEN) - the id
// is stored raw rather than resolved against a real list (CHOSEN
// simplification). flashpoint_mission_status's own "combine rule
// favoring in-progress when the two sides disagree" (§45.2's own
// words, the fullest reading this spec text supports) is implemented
// literally: equal -> that value, disagreement -> 2 (started/in
// progress); single player never has a peer (§8.27/§26.28), so
// peerStatus is simply kept equal to localStatus here, CHOSEN.
struct FlashpointState {
    int32_t localStatus = 1;  // 1=idle/reset (CONFIRMED file default), 2=started, 3=HYPOTHESIS failed/aborted, 4=complete
    int32_t peerStatus = 1;
    std::string activeId;
};

struct EngineStateMissionsData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using CallbackSpawner = sr3luahost::CallbackSpawner;
    using EngineCallbackCounters = sr3luahost::EngineCallbackCounters;
    using TriggerWaitStallInfo = sr3luahost::TriggerWaitStallInfo;
    using InProgressType = sr3luahost::InProgressType;
    using GuardianAngel = sr3luahost::GuardianAngel;
    using Stronghold = sr3luahost::Stronghold;
    using BossBattleMatt = sr3luahost::BossBattleMatt;
    using MissionContext = sr3luahost::MissionContext;
    using MissionCancelWarpLocation = sr3luahost::MissionCancelWarpLocation;
    using MissionCheckpoint = sr3luahost::MissionCheckpoint;
    using TriggerVolumeGeometry = sr3luahost::TriggerVolumeGeometry;
    using MissionExcludedRewardsState = sr3luahost::MissionExcludedRewardsState;
    using BossBattleKbCrowdProps = sr3luahost::BossBattleKbCrowdProps;
    using RandomObjKilledHook = sr3luahost::RandomObjKilledHook;
    using MissionCancelState = sr3luahost::MissionCancelState;
    using HookNamePool = sr3luahost::HookNamePool;
    using FlashpointState = sr3luahost::FlashpointState;

public:
    void setCallbackSpawner(CallbackSpawner s) { callbackSpawner_ = std::move(s); }
    std::vector<PendingCallback>& pendingEngineCallbacks() { return pendingEngineCallbacks_; }
    const std::vector<FiredCallbackRecord>& engineCallbackLog() const { return engineCallbackLog_; }
    void clearEngineCallbackLog() { engineCallbackLog_.clear(); }
    const EngineCallbackCounters& engineCallbackCounters() const { return engineCallbackCounters_; }
    // OPEN (Sec21.10.1 type 2): the condition "the global mission object's two counters are equal" is not modelled; the
    // Killbane fight hook fires at a type-2 expiry only when a test/host sets this true.
    OpenValue<bool>& downedType2MissionCountersEqual() { return downedType2MissionCountersEqual_; }
    // Overlap-pass bookkeeping for the player: true once the trigger's overlap set was baselined (the first
    // evaluation of an already-enabled trigger consumes "already inside", spec rule (a)).
    bool& triggerOverlapBaselined(const std::string& triggerName) { return triggerOverlapBaselined_[triggerName]; }
    const TriggerWaitStallInfo& triggerWaitStallInfo() const { return triggerWaitStall_; }
    const std::vector<std::string>& triggerRegistrationOrder() const { return triggerRegistrationOrder_; }

    // True once a trigger name's own ENTER transition (triggerCurrentlyInside()
    // going false->true) has fired at least once this session - separate from
    // triggerCurrentlyInside() itself, which can go back to false on EXIT and
    // would otherwise look identical to "never entered" to a stall tracker.
    bool& triggerEverEntered(const std::string& triggerName) { return triggerEverEntered_[triggerName]; }
    // fix/auto-objective-driving (2026-10-13): triggers the running script POLLED for the live player via
    // object_is_in_trigger (the polling analogue of an on_trigger wait), in query order, drained once per
    // frame by the --auto-objective harness. Harness bookkeeping only; no engine semantics.
    void noteTriggerPollQuery(const std::string& triggerName) { triggerPollQueries_.push_back(triggerName); }
    std::vector<std::string> drainTriggerPollQueries() { std::vector<std::string> out; out.swap(triggerPollQueries_); return out; }

    // --- mission_end_silently (Sec15.23) ------------------------------------

    // The global mission-flags word 0x014c848c. CONFIRMED: mission_end_silently
    // always sets bit 0x4 and sets/clears bit 0x10 to mirror its argument
    // (meaning of both OPEN). Every other bit, and the initial value, is OPEN:
    // only the bits a confirmed writer has written are known (open_state.h).
    OpenBits32& missionFlagsWord() { return missionFlagsWord_; }

    // Sec27.2 `cell_is_mission_complete` (0x00a525a0, same function as Sec20.14's `mission_is_complete`):
    // keyed by mission name - the resolved object's own +0x88 bit 0x4 (resolve itself reuses objectResolves()).
    OpenValueMap<bool>& missionComplete() { return missionComplete_; }
    InProgressType& inProgressType() { return inProgressType_; }
    GuardianAngel& guardianAngel() { return guardianAngel_; }

    // Sec28.7 `get_num_humans_in_trigger`.
    OpenValueMap<int32_t>& humansInTriggerCount() { return humansInTriggerCount_; }
    Stronghold& stronghold() { return stronghold_; }

    // Sec28.17 `clear_callbacks_for_obj`: the real 6-wrapper/6-sub-record-base dispatch tree (Sec28.17's own
    // seven hidden-`this` findings) is NOT modelled - only that a call was gated on objectResolves() and
    // cleared SOMETHING is counted, honestly, per name (same "record the request" convention as
    // minimap_icon_add_do/object_indicator_add_do above).
    std::unordered_map<std::string, uint64_t>& callbacksClearedCount() { return callbacksClearedCount_; }
    BossBattleMatt& bossBattleMatt() { return bossBattleMatt_; }
    bool guardianAngelGroupKnown(const std::string& name) const { return guardianAngelGroups_.count(name) != 0; }
    MissionContext& missionContext() { return missionContext_; }

    // `0x006db7fa` (Sec21.27b's own organic-trigger table): the per-tick
    // driver's own case-4 "silent"-class branch - keyed per mission name,
    // OPEN/test-settable, the SAME "no spec-given real population, flat
    // test-settable resolver" convention as weaponDefinitionResolves()/
    // lookupNotorietyTrackCode() elsewhere in this file (no table of which
    // of the real 49 missions carry class-descriptor bit 0x20 was read by
    // this project). Read by bindMissionPhaseMachine() above via `.known()`
    // only (never a blind `.get()` that could throw) - for every real
    // mission in today's probe population this stays unset, so the trigger
    // is a documented no-op there, not a disguised always-true default.
    OpenValueMap<bool>& missionSilentClass() { return missionSilentClass_; }

    // mission_end_success's own dispatch (0x006da400, the SAME shared
    // function failure uses) arming effect - the REAL separate timer this
    // branch arms, 0x12f2634 (Sec21.27c) - the only in-scope writer of
    // timer 2. CORRECTS this file's own prior (batch b29, 2026-10-08)
    // assumption that success armed the SAME timer 1 as failure, purely
    // because both callers share one dispatch function - see
    // MissionContext::missionEndTimer2Armed's own doc comment above for the
    // full citation. No duration is given for this timer anywhere in
    // Sec21.27c's own text (unlike timer 1's confirmed 2000/4000ms pair) -
    // armed/idle only, not guessed.
    void armMissionEndSuccessTimer() { missionContext_.missionEndTimer2Armed = true; }
    MissionCancelWarpLocation& missionCancelWarpLocation() { return missionCancelWarpLocation_; }
    MissionCheckpoint& missionCheckpoint() { return missionCheckpoint_; }
    bool triggerEnableKnown(const std::string& name) const { return triggerEnable_.count(name) != 0; }
    bool onTriggerCallbackKnown(const std::string& triggerName) const { return onTriggerCallbacks_.count(triggerName) != 0; }
    // For runPlaytest()'s own per-frame trigger-geometry auto-register (see
    // EngineState::TriggerVolumeGeometry's own doc comment): every trigger
    // name an on_trigger() call has registered a callback for so far.
    const std::unordered_map<std::string, std::string>& onTriggerCallbacksMap() const { return onTriggerCallbacks_; }
    bool onTriggerExitCallbackKnown(const std::string& triggerName) const { return onTriggerExitCallbacks_.count(triggerName) != 0; }
    const std::unordered_map<std::string, std::string>& onTriggerExitCallbacksMap() const { return onTriggerExitCallbacks_; }
    // For runPlaytest()'s own per-frame walk: every trigger this session has
    // loaded geometry for, by name.
    const std::unordered_map<std::string, TriggerVolumeGeometry>& triggerGeometryMap() const { return triggerGeometry_; }

    // The real per-frame poll's own CONFIRMED edge-detector shape (Sec1.14:
    // "a trigger is only re-tested/fired while tracked in this set, and
    // set-membership transitions are the enter/exit events") - one bool per
    // trigger name, defaulting to "not inside" (a session always starts
    // with the player outside every trigger, never mid-volume).
    bool& triggerCurrentlyInside(const std::string& triggerName) { return triggerInsideState_[triggerName]; }

    // object_is_in_trigger (Sec1.12): CONFIRMED (2026-10-01) arg 1 =
    // trigger (objectResolves()), arg 2 = object (ALSO objectResolves() -
    // this project's flat stand-in for the real position-lookup resolver
    // 0x00a455a0, same convention VehicleState::worldPosition's own doc
    // comment already applies) - arg 2 is parsed UNCONDITIONALLY even if
    // arg 1 failed to resolve (CONFIRMED), reproduced by reading both
    // before branching. The real geometric containment test (0x0093bfc0,
    // box/sphere/cylinder against the trigger's own position+extents) has
    // no in-scope geometry/extents data anywhere in this project (no
    // coordinate system modelled, the same stated project-wide gap
    // VehicleState::worldPosition's own doc comment notes) - the test's
    // own boolean OUTCOME is tracked as its own OPEN fact, keyed by the
    // (trigger, object) pair, rather than invented from position data
    // this project does not have.
    OpenValueMap<bool>& triggerContainsObject() { return triggerContainsObject_; }
    static constexpr uint8_t kMissionFlagClearedByWriter = 0x8;     // cleared (not set) by 0x006d6870 - real meaning OPEN
    static constexpr uint8_t kMissionFlagCompleted = 0x4;           // Sec15.10/Sec21.27b
    static constexpr uint8_t kMissionFlagCompletedSecondary = 0x40; // set alongside kMissionFlagCompleted by the SAME writer - real meaning OPEN
    uint64_t missionSetCompletedCallCount() const { return missionSetCompletedCallCount_; }
    MissionExcludedRewardsState& missionExcludedRewards() { return missionExcludedRewards_; }

    // guardian_angel_increment_node_progress (Sec12.17, 0x00a4abb0):
    // CONFIRMED a genuine zero-argument command, single unconditional call
    // (0x00626b10, OPEN - "which specific 'current' guardian-angel context
    // it advances" is not traced). Call-count only.
    uint64_t guardianAngelNodeProgressCount() const { return guardianAngelNodeProgressCount_; }
    void recordGuardianAngelNodeProgress() { ++guardianAngelNodeProgressCount_; }

    // mission_set_enemy_team (Sec20.13): a bare global, written only while
    // missionContext().activeAndRunning is true (the SAME shared gate
    // change_mission_enemy_team, Sec21.27, also reads). CHOSEN sentinel -1
    // ("never set").
    int32_t missionEnemyTeamId = -1;

    // change_mission_enemy_team (Sec21.27): a DIFFERENT field from
    // mission_set_enemy_team's own missionEnemyTeamId above (CONFIRMED
    // distinct offsets, +0xa0 one-shot gate vs. +0xa5 team-code byte,
    // Sec21.27's own text: "the exact relationship between the two fields
    // was not resolved this pass, flagged OPEN"). CONFIRMED polarity: the
    // write proceeds only when the real +0xa0 field reads 0 - modeled here
    // as `false` ("not yet blocked/fired"); `true` means the real field
    // read non-zero, blocking the write. The one-shot gate itself is OPEN
    // until a test sets it.
    OpenValue<bool> missionOneShotGateA0{"mission context +0xa0 one-shot gate (change_mission_enemy_team)",
                                         "spec-lua-api-behaviour.md Sec21.27"};
    int32_t missionEnemyTeamCodeA5 = -1; // CHOSEN sentinel "never set"
    uint64_t changeMissionEnemyTeamNotifyCount = 0;
    BossBattleKbCrowdProps& bossBattleKbCrowdProps() { return bossBattleKbCrowdProps_; }

    // boss_battle_matt_cheats_stop (Sec24.1): CHOSEN minimal stand-in for the
    // real multi-global "boss battle Matt" cheat/state-tracking cluster -
    // only whether the cluster was active (the real sentinel-not-already-
    // -1 gate) and a call count are tracked.
    bool& bossBattleMattCheatsActive() { return bossBattleMattCheatsActive_; }
    uint64_t& bossBattleMattCheatsStopCount() { return bossBattleMattCheatsStopCount_; }

    // trigger_type_enable (Sec24.13): CHOSEN simplification - a trigger TYPE
    // name is used directly as the map key rather than this project
    // reproducing the real hash+64-bit-bitmask-by-type-id mechanism (no
    // generic hash table modelled anywhere in this project). Missing/absent
    // = enabled (bit clear), matching the real all-zero-at-start bitmask.
    // Both directions ALSO fire the SAME local-only notification record
    // (identical opcode+sub-tag either way, Sec24.13's own confirmed
    // "identical record for both directions" idiom) - tracked as one shared
    // counter, since the record payload itself never distinguishes direction.
    std::unordered_map<std::string, bool>& triggerTypeDisabled() { return triggerTypeDisabled_; }
    uint64_t& triggerTypeEnableNotifyCount() { return triggerTypeEnableNotifyCount_; }

    // trigger_set_hold_to_repeat (Sec24.14): resolved trigger's own +0x7b bit
    // 0x10 - CHOSEN per-name map (missing = false, matching a zero-filled
    // byte's own default bit state).
    std::unordered_map<std::string, bool>& triggerHoldToRepeat() { return triggerHoldToRepeat_; }

    // mission_set_next_mission (Sec25.4) - CORRECTED by Sec60.3: in single
    // player this native takes its own no-session path and changes nothing
    // at all; the previously-cited gate constant (0x011239d0) is a true
    // compile-time constant, not a live "mission system active" flag. This
    // project therefore implements it as a genuine, confirmed no-op - no
    // EngineState value is ever written - with only a call count for test
    // observability (same "genuine no-op, call count only" convention as
    // bink_play, Sec24.2).
    uint64_t& missionSetNextMissionNoopCount() { return missionSetNextMissionNoopCount_; }
    std::vector<RandomObjKilledHook>& randomObjKilledHooks() { return randomObjKilledHooks_; }

    // --- boss_battle_kb_enable / boss_battle_mars_killbane_active
    // (Sec45.5) - a real logic defect, ONE shared flag ---------------------
    // CONFIRMED: "boss_battle_mars_killbane_active(false) clears the
    // Killbane QTE-active flag WITHOUT running the QTE-abort cleanup that
    // boss_battle_kb_enable(false) runs." ONE shared bool - both setters
    // write it, only kb_enable's own false-transition also runs cleanup.
    // "Both default to enabling when called with no argument" (the Lua
    // ARGUMENT default is true; the field's own CHOSEN initial value is
    // false - "no QTE in progress" is the natural boot state).
    bool& killbaneQteActive() { return killbaneQteActive_; }
    bool killbaneCleanupPerformed() const { return killbaneCleanupPerformed_; }

    // --- trigger_allow_dead_guys (Sec52/T15) -------------------------------
    // No individual spec text beyond "resolved in full, no further crash
    // shapes" (§52.3's own one-line mention). CHOSEN minimal: trigger name
    // + optional boolean (default true, matching this document's dominant
    // nil-gated-optional convention elsewhere), local field write only -
    // no replication is claimed since none is documented.
    std::unordered_map<std::string, bool>& triggerAllowDeadGuys() { return triggerAllowDeadGuys_; }
    MissionCancelState& missionCancel() { return missionCancel_; }

    // --- Sec50.4 msn_killbane_selected_option -------------------------------
    int32_t& killbaneSelectedOption() { return killbaneSelectedOption_; }
    uint64_t& killbaneCallbackFiredCount() { return killbaneCallbackFiredCount_; }

    // --- Sec59.2 cell_music_menu_toggle_station / Sec59.1 cell_is_map_disabled:
    // a hard-coded six-mission list this host tracks via a test-settable
    // "current mission stem" (no real mission-stack string anywhere else in
    // this project's public API) plus its own extra gating byte (cellUi_
    // above). Also: cell_cheats_unlock_cheat's content-id gate (a cheat
    // whose master record carries a non-default content id is never
    // unlocked through this path, CONFIRMED) - per-cheat content id, 0 =
    // default = always unlockable.
    std::string& currentMissionStemForTesting() { return currentMissionStem_; }
    HookNamePool& onHookNamePool() { return onHookNamePool_; }

    // --- perform_killswitch (§51.4 CRASH). CONFIRMED the victim argument
    // is used only for a type check; the real QTE always targets whatever
    // live object is registered under the LITERAL script name
    // "M21_Killbane" - reuses objectResolves() with that literal name, no
    // new per-object state. HARDENED (CHOSEN divergence): the real engine
    // zeroes the lookup result and makes a virtual call through it when no
    // such object exists; this project refuses (no-op) instead, counted.
    uint64_t performKillswitchMissingKillbaneGuardCount() const { return performKillswitchMissingKillbaneGuardCount_; }
    void recordPerformKillswitchMissingKillbaneGuard() { ++performKillswitchMissingKillbaneGuardCount_; }

    // --- mission_maybe_uncomplete_m22 (§50.6). CONFIRMED gated on an XOR
    // over two specific follow-on missions' own progress, which this
    // project has no mission-progression model for (OPEN) - call-count
    // only, same "deferred infra" convention as several fields above; the
    // CONFIRMED "broadcasts from every machine regardless of host status"
    // half is still reproduced via EngineState::replicateStateChange.
    uint64_t missionMaybeUncompleteM22Count() const { return missionMaybeUncompleteM22Count_; }
    void recordMissionMaybeUncompleteM22() { ++missionMaybeUncompleteM22Count_; }

    // --- homie_mission_lock/_unlock (§53/AGG, §54.4). The ONLY text
    // available for either (homie_mission_unlock's own aggregate mention,
    // §54.4): "an activity-record override... paired with its lock
    // sibling, has no way to restore the record's original default state
    // through either binding" - modelled as a simple per-id override flag
    // with deliberately NO reset/clear accessor (matching "no way to
    // restore" exactly): 0 = no override, 1 = locked, 2 = unlocked.
    std::unordered_map<std::string, int32_t>& homieMissionOverride() { return homieMissionOverride_; }
    FlashpointState& flashpointState() { return flashpointState_; }

    // --- on_dismount/on_hit_ped/on_mount/on_vehicle_debris_flow_recycled/
    // on_m21_player_choice/on_mission_item_drop (§42.3/§47) - all 6
    // register a callback NAME (interned via onHookNamePool() above, see
    // its own doc comment) into a hook slot; this project stores the raw
    // callback STRING directly per object (the SAME "single dedicated
    // field per hook kind" simplification CharacterState::onRevivedCallback/
    // onDeathCallback above already establish - not a generic slot-id
    // array, since this project only ever tracks the specific hook kinds
    // in scope). Empty/nil clears (CONFIRMED family-wide convention, same
    // as onRevivedCallback), EXCEPT on_mission_item_drop (see its own
    // field's doc comment below - CONFIRMED NOT reset on empty/nil).
    std::unordered_map<std::string, std::string>& onDismountCallbacks() { return onDismountCallbacks_; }
    std::unordered_map<std::string, std::string>& onHitPedCharacterCallbacks() { return onHitPedCharacterCallbacks_; }
    // on_mount (§42.3): CONFIRMED real inverted-probe quirk - checks the
    // GENERIC/"#PLAYER#" chain FIRST and registers the hook ONLY IF THAT
    // CHECK FAILS. CHOSEN (HIGH CONFIDENCE by direct structural analogy):
    // this project's own already-implemented on_ai_do_action_complete
    // (Sec14.7, lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp) is
    // CONFIRMED to share the IDENTICAL two-tier shape (playerClassChainResolves()
    // match -> no-op; else plain objectResolves() fallback -> register on
    // the character) - reused here rather than guessed fresh; the exact
    // real hook-slot NUMBER for on_mount is OPEN (not given anywhere in
    // this spec), so only the callback NAME is tracked, same "single
    // dedicated field" convention as every sibling above.
    std::unordered_map<std::string, std::string>& onMountCallbacks() { return onMountCallbacks_; }
    // on_m21_player_choice (§42.3): CONFIRMED a hand-inlined registration
    // against a GLOBAL hook record (not an object) - HIGH CONFIDENCE a
    // general player-level/UI hook slot despite the mission-21-sounding name.
    std::string& onM21PlayerChoiceCallback() { return onM21PlayerChoiceCallback_; }
    // on_mission_item_drop (§47): CONFIRMED a GLOBAL, non-object callback-
    // name hook, interned via the SAME onHookNamePool() the 5 on_* hooks
    // above share - CONFIRMED this persistent global is NOT reset when the
    // argument is nil/empty (a no-argument call simply reuses whatever
    // name the previous real call registered) - the stub must NOT clear
    // this field on an empty argument, unlike every sibling above.
    std::string& onMissionItemDropCallback() { return onMissionItemDropCallback_; }

    // flashpoints_enable (Sec18.15): a single global byte. CHOSEN true
    // ("enabled" - matches optionalBoolDefault's own default=true reading,
    // same convention as roadblocksEnabled() above).
    bool& flashpointsEnabled() { return flashpointsEnabled_; }

    // boss_battle_matt_set_matt_flying (Sec18.2): a bare global "Matt is
    // flying" state flag - no per-object target, no replication.
    bool& bossBattleMattFlying() { return bossBattleMattFlying_; }

    // on_human_killed (Sec17.7): a GLOBAL (not per-object) hook, slot 0xb
    // (11) of the SAME hook-slot array family. The optional filter string
    // resolves to a numeric type id via a fixed 350-entry name-interning
    // table (0x00a1fcc0) this project does not reproduce (same "no fixed
    // id table" class of gap as several other name-lookup natives) -
    // collapsed to one flat resolves-or-not predicate, test-registerable.
    std::string& onHumanKilledCallback() { return onHumanKilledCallback_; }
    std::string& onHumanKilledFilterName() { return onHumanKilledFilterName_; }

protected: // private in EngineState before the split
    CallbackSpawner callbackSpawner_;
    std::vector<PendingCallback> pendingEngineCallbacks_;
    std::vector<FiredCallbackRecord> engineCallbackLog_;
    EngineCallbackCounters engineCallbackCounters_;
    OpenValue<bool> downedType2MissionCountersEqual_{"downed type 2 expiry: the mission object's two counters are equal (0x009730d0)",
                                                     "spec-ai-runtime.md Sec21.10.1"};
    std::unordered_map<std::string, bool> triggerOverlapBaselined_;
    TriggerWaitStallInfo triggerWaitStall_;             // feature/auto-objective-trigger-vehicle-seat, 2026-10-12
    std::vector<std::string> triggerRegistrationOrder_; // ditto
    std::unordered_map<std::string, bool> triggerEverEntered_; // ditto
    std::vector<std::string> triggerPollQueries_;       // ditto (fix/auto-objective-driving)
    OpenBits32 missionFlagsWord_{"0x014c848c", "spec-lua-api-behaviour.md Sec15.23"};
    OpenValueMap<bool> missionComplete_{"character/mission object +0x88 bit 0x4 ('is mission complete')",
                                        "spec-lua-api-behaviour.md Sec27.2/Sec20.14"};
    InProgressType inProgressType_;
    GuardianAngel guardianAngel_;
    // groupFirstNpcName()/groupNextNpcName() above (Sec28.5/Sec28.6) are
    // derived directly from groupMemberNames_ (declared alongside
    // groupResolves_/groupHasMembersBit_ below, Sec18.14) - no separate
    // backing field needed here.
    OpenValueMap<int32_t> humansInTriggerCount_{"trigger occupancy count (0x0075d4b0/category 47)",
                                                "spec-lua-api-behaviour.md Sec28.7"};
    Stronghold stronghold_;
    std::unordered_map<std::string, uint64_t> callbacksClearedCount_;
    BossBattleMatt bossBattleMatt_;
    std::unordered_map<std::string, bool> guardianAngelGroups_; // Sec6.6/Sec6.8
    MissionContext missionContext_;
    MissionCheckpoint missionCheckpoint_;
    MissionCancelWarpLocation missionCancelWarpLocation_;
    struct TriggerEnableRecord {
        bool enabled = true;
        int64_t flags = 3;
    };
    std::unordered_map<std::string, TriggerEnableRecord> triggerEnable_;
    std::unordered_map<std::string, std::string> onTriggerCallbacks_;

    // --- Batch 2026-10-08, wave 1 batch 1: spec-lua-api-behaviour.md §1-4
    // - see each public accessor's own doc comment above for citation. ---
    std::unordered_map<std::string, std::string> onTriggerExitCallbacks_;
    // Sec1.14 trigger-volume geometry/overlap state - see registerTriggerGeometry()/
    // triggerCurrentlyInside()'s own doc comments above.
    std::unordered_map<std::string, TriggerVolumeGeometry> triggerGeometry_;
    std::unordered_map<std::string, bool> triggerInsideState_;
    OpenValueMap<bool> triggerContainsObject_{"trigger/object containment test result (0x0093bfc0)",
                                               "spec-lua-api-behaviour.md Sec1.12"};
    std::unordered_map<std::string, bool> missionCompletedMap_;   // Sec15.10 - legacy facade, see writeMissionCompletionFlagBits()
    uint64_t missionSetCompletedCallCount_ = 0;
    // The REAL mission handle's own +0x88 packed multi-bit flags byte
    // (Sec21.27b) - see writeMissionCompletionFlagBits()/
    // writeMissionUnlockedFlagBit()/missionCompletionFlagBitSet() (this
    // header's Sec15.9/Sec15.10 section) for its only writers/reader.
    std::unordered_map<std::string, uint8_t> missionFlagBits_;
    // `0x006db7fa`'s own per-mission "silent"-class gate (Sec21.27b) - see
    // missionSilentClass()'s own doc comment (MissionContext's own section,
    // above) for why this is OPEN/test-settable with no real population.
    OpenValueMap<bool> missionSilentClass_{"mission class-descriptor bit 0x20 ('silent')",
                                            "spec-lua-api-behaviour.md Sec21.27b"};
    MissionExcludedRewardsState missionExcludedRewards_;
    uint64_t guardianAngelNodeProgressCount_ = 0;
    BossBattleKbCrowdProps bossBattleKbCrowdProps_;
    bool bossBattleMattCheatsActive_ = false;
    uint64_t bossBattleMattCheatsStopCount_ = 0;
    std::unordered_map<std::string, bool> triggerTypeDisabled_;
    uint64_t triggerTypeEnableNotifyCount_ = 0;
    std::unordered_map<std::string, bool> triggerHoldToRepeat_;
    uint64_t missionSetNextMissionNoopCount_ = 0;
    std::unordered_map<std::string, std::string> namedHookCallbacks_;
    std::vector<RandomObjKilledHook> randomObjKilledHooks_;
    bool killbaneQteActive_ = false;
    bool killbaneCleanupPerformed_ = false;
    std::unordered_map<std::string, bool> triggerAllowDeadGuys_;
    MissionCancelState missionCancel_;
    int32_t killbaneSelectedOption_ = -1;
    uint64_t killbaneCallbackFiredCount_ = 0;
    std::string currentMissionStem_;

    // --- Batch W2-8 (2026-10-08), wave-2 §38-60 gameplay B - see each
    // public accessor's own doc comment above for citation/reasoning. ---
    HookNamePool onHookNamePool_;
    uint64_t performKillswitchMissingKillbaneGuardCount_ = 0;
    uint64_t missionMaybeUncompleteM22Count_ = 0;
    std::unordered_map<std::string, int32_t> homieMissionOverride_;
    FlashpointState flashpointState_;
    std::unordered_map<std::string, std::string> onDismountCallbacks_;
    std::unordered_map<std::string, std::string> onHitPedCharacterCallbacks_;
    std::unordered_map<std::string, std::string> onMountCallbacks_;
    std::string onM21PlayerChoiceCallback_;
    std::string onMissionItemDropCallback_;
    bool flashpointsEnabled_ = true;
    bool bossBattleMattFlying_ = false;
    std::string onHumanKilledCallback_;
    std::string onHumanKilledFilterName_;
};

} // namespace sr3luahost
