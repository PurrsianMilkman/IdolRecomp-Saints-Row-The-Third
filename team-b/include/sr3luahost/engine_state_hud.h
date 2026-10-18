// EngineState slice: HUD, minimap, objective text, tutorial, QTE, reticle.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateHudData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateHudData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {
struct TutorialLookup {
    int index = -1;           // first known name matching, else -1
    bool couldBeIndex176 = false; // -1 only because index 176's string is OPEN
};

// --- tutorial_start (Sec6.19, 0x00a603f0), batch 2026-10-08 ------------
//
// Arg 1 (string, mandatory) resolves through tutorialLookup() above
// (the SAME 210-entry table tutorial_advance/tutorial_stop already
// share - reused directly). Args 2 (number, default 0), 3-5 (booleans,
// each default false) plus an internal hidden literal 1 (never
// Lua-visible) are handed, with the resolved index, to the 12-step
// dispatcher 0x007169e0 (CONFIRMED structure, re-derived 2026-10-01,
// job 20261001T020213-team-a-dksj). Several of its own gates read
// engine globals whose real-world MEANING - and, for these five, even
// their own CONFIRMED initial value - is genuinely OPEN (unlike
// continuous_spawn_start's args 5-7 above, which DO have confirmed
// default values): modelled as OpenValue, not plain fields, per this
// task's own "OPEN means value unknown here, not just meaning
// unknown" distinction. Three of the five share a LITERAL address with
// an already-modelled, unrelated field elsewhere in this header; none
// of those connections is asserted by any spec text cross-referencing
// Sec6.19 with the other section, so each gets its OWN field here,
// flagged with the coincidence in its own comment, not silently
// aliased (this project's standing convention for an unconfirmed
// cross-reference, Sec4.13's own door_open/teleport precedent).
struct TutorialStart {
    // Step 1: "A < 189 and the global byte 0x0151d5a6 is non-zero ->
    // stop (indices 189 and up bypass this byte)." Also read by step
    // 8 ("A>=189 -> set state 1 through 0x00716170, itself gated on
    // 0x0151d5a6 being zero") and step 9. Reads, from its own gating
    // role, as a "base-game tutorial prompts suppressed" toggle - this
    // project's own inference, not an independently confirmed field
    // name. No confirmed initial value anywhere in the spec.
    OpenValue<bool> suppressionByte0151d5a6{
        "tutorial dispatcher global byte 0x0151d5a6 (meaning OPEN; non-zero suppresses base-game [index<189] prompts)",
        "spec-lua-api-behaviour.md Sec6.19"};
    // Step 2: "The global 0x029443bc is 1, 2 or 3 -> stop." **CORRECTED,
    // 2026-10-10, Team A spec sync (dlc2_m03 mission-blocking routing,
    // scratchpad/dlc2_mission_blockers_0x029443bc_vtable68_2026-10-10.md):
    // retract the "tutorial dispatcher global" framing entirely -
    // 0x029443bc is NOT tutorial-owned. It is the machinima recorder's
    // OWN recording-state machine, written exclusively by the
    // 0x00bc4f60-0x00bce150 module (same global cell_machinima_is_
    // recording/game_machinima_is_recording test via MachinimaState::
    // recording above, Sec44.4/Sec59.1 - kept as its own separate
    // OpenValue, not aliased to that bool field, per this struct's own
    // top "no silent cross-link" convention). Full writer map: `0` =
    // idle/not recording (CONFIRMED the zero-fill default - a fresh
    // single-player game reads 0 - this is what applySpecInitialState()
    // now seeds); `1` = actively recording (written by the start-request
    // handler 0x00bce0d0 after 5 other engine-state gates pass); `2`/`3`
    // = stopping/finalizing (an 8-step async drain/flush sub-state
    // machine in companion global 0x029443e0, driven by 0x00bc6d80);
    // `4` = teardown in progress (set by 0x00bc6160, cleared back to 0
    // once 0x00706ab0 reports 2). This dispatcher step is a *consumer*
    // refusing to run during/after a machinima capture, not a
    // tutorial-owned sentinel - tutorial_start itself has no stake in
    // this global's meaning. Field/key name kept as globalMode029443bc
    // (not renamed) since it is this struct's own local step-2 gate
    // value, referenced by address elsewhere in this project; the
    // meaning correction lives in this comment and in the seed below.
    OpenValue<int32_t> globalMode029443bc{
        "machinima-recorder recording-state global 0x029443bc (0 idle [default]/1 recording/2,3 finalizing/4 teardown; step 2 stops the tutorial dispatcher while 1, 2 or 3) - NOT a tutorial-owned concept, corrected 2026-10-10",
        "spec-lua-api-behaviour.md Sec6.19 (correction)"};
    // Step 5: "0x00706ab0 returns 6 -> stop." CONFIRMED (2026-10-10 Team
    // A spec sync, routed mission-blocking dlc2_m03, agent
    // a53c6769fd437715d): 0x00706ab0 is a top-of-mode-stack accessor -
    // `if (0x012f4a80 >= 0) return mode_stack_array[0x012f4a80]; else
    // return 0;` - and the stack-top index 0x012f4a80 is file-backed,
    // initialized to -1, i.e. the mode stack starts EMPTY on a fresh
    // game, so this accessor's CONFIRMED spawn default is 0, not 6.
    // Mode 6 (the only value that stops this step) is the control-
    // scheme/key-bindings configuration sub-screen (spec-frontend-menu-
    // runtime.md Sec1) - step 5 only stops tutorial_start while that
    // options sub-screen is the active mode-stack entry, a state a
    // fresh/default game is never in. See EngineState::EngineState()'s
    // own seed for this field (the same construction-time "pre-seed a
    // newly-CONFIRMED default instead of leaving an OpenValue OPEN"
    // convention as CharacterState::ragdollStateEnumCd2/stateCodeByte501
    // above). NOTE: the exact same address is already modelled as
    // ScreenFade::modeStackTop ("mode stack top... read by the
    // per-frame routine", Sec26.24) - plausibly the same shared
    // per-frame engine-mode query, but not cross-referenced by any
    // spec text, so kept as its own separate field (see struct's own
    // top comment). Remains a plain OpenValue, not a bare int:
    // .set()/.forget() stay available, so a test exercising the
    // key-bindings-screen-open case can still override this explicitly.
    OpenValue<int32_t> gate5Value00706ab0{
        "tutorial dispatcher 0x00706ab0 result (CONFIRMED default 0, mode stack empty on a fresh game; ==6 -> stop, only while the key-bindings config screen is active; NOTE: same address as ScreenFade::modeStackTop, Sec26.24 - not confirmed to be the same field)",
        "spec-lua-api-behaviour.md Sec6.19"};
    // Step 6, first half: "If the byte 0x014f3d34 is non-zero,
    // continue only for A=188 or 196<=A<=201. [OPEN - what the
    // 0x014f3d34 mode is.]"
    OpenValue<bool> mode014f3d34{
        "tutorial dispatcher byte 0x014f3d34 (meaning OPEN)",
        "spec-lua-api-behaviour.md Sec6.19"};
    // Step 6, second half (the "it is zero" branch's own fallback):
    // "...or otherwise only when no mission object is live (0x006d2910
    // returns -1)." NOTE: the exact same address is already modelled
    // as GuardianAngel::modeIsThree ("0x006d2910 mode == 3 via
    // 0x00614d00", Sec28.4) - a DIFFERENT comparison (==3, not ==-1)
    // and not cross-referenced by any spec text to this section, so
    // kept separate (see struct's own top comment).
    OpenValue<bool> noMissionLive006d2910{
        "tutorial dispatcher 0x006d2910 == -1 (\"no mission object is live\"); NOTE: same address as GuardianAngel::modeIsThree, Sec28.4 (a different, ==3 comparison) - not confirmed to be the same field",
        "spec-lua-api-behaviour.md Sec6.19"};
    // Step 7: "A = 76 and the byte 0x0141250d is zero -> A becomes
    // 73. [OPEN - meaning.]" NOTE: the exact same address is already
    // modelled as EffectFinisher::gamepadMode ("gamepad flag
    // 0x0141250d", Sec28.9) - thematically plausible (several of the
    // 210-entry table's own names are explicit PC/gamepad variant
    // pairs, e.g. "combat_grenades"/"combat_grenades_pc"), but not
    // cross-referenced by any spec text, so kept separate (see
    // struct's own top comment). True here means "byte is non-zero"
    // (the remap fires when this is FALSE).
    OpenValue<bool> byteNonZero0141250d{
        "tutorial dispatcher byte 0x0141250d (meaning OPEN; zero -> remap index 76 to 73; NOTE: same address as EffectFinisher::gamepadMode, Sec28.9 - not confirmed to be the same field)",
        "spec-lua-api-behaviour.md Sec6.19"};

    // Per-entry descriptor data (Sec6.19's own "descriptor record"):
    // compile-time-baked constants this project has no transcription
    // of (unlike the 210-name table itself, fully transcribed) - OPEN
    // per index until a test supplies the specific entry it is
    // exercising. flagsByte24: bit 0x04 "use the rich (animated/
    // video) widget", bit 0x08 "allowed while a mission is active",
    // bit 0x10 "host-only" (CONFIRMED bit meanings; OPEN per-index
    // value). defaultParam20: the "+0x20 default parameter" copied
    // into the per-entry record's own +0x18 field when arg 5 is
    // false (CONFIRMED mechanism; OPEN per-index value).
    OpenValueMap<uint32_t> flagsByte24{
        "tutorial descriptor flags byte +0x24, keyed by table index (0x04 rich widget/0x08 mission-active-ok/0x10 host-only)",
        "spec-lua-api-behaviour.md Sec6.19"};
    OpenValueMap<int32_t> defaultParam20{
        "tutorial descriptor default parameter +0x20, keyed by table index",
        "spec-lua-api-behaviour.md Sec6.19"};

    // Runtime per-entry record (Sec6.19's own entry-layout table,
    // fields +0x14/+0x18/+0x1c - the +0x00/+0x04 linked-list links and
    // +0x08/+0x0c descriptor-pointer/state fields are already modelled
    // above via tutorialState()/applySpecInitialState(); +0x10 is
    // "not touched by the dumped code" per that table, so it is not
    // modelled at all). Written ONLY by step 11's simple-prompt
    // (non-rich-widget) branch, per that step's own text ("fill
    // +0x14/+0x18/+0x1c as in the table above") - the rich-widget
    // branch never touches these.
    struct EntryRecord {
        bool queuedForSimplePrompt = false; // step 11's own "if the entry is not already queued" gate
        int64_t argument2Value = 0;         // +0x14: arg 2 (N), truncateEa2596
        int32_t argument5GatedValue = 0;    // +0x18: 0 if arg 5 (B5) true, else defaultParam20[index]
        int32_t hiddenLiteralP4 = 1;        // +0x1c: the hidden literal 1 (CONFIRMED, never Lua-visible)
    };
    std::unordered_map<int32_t, EntryRecord> entries;

    // Step 11's rich-widget branch: "build the rich widget... keep its
    // handle in 0x0151d5b4, set state 4." This project has no real
    // UI-widget object (no coordinate/UI-document system in scope) -
    // counted per index only, same "dispatch happened" convention as
    // every other widget-handle field project-wide.
    std::unordered_map<int32_t, uint64_t> richWidgetDispatchCount;
    // Step 12's own "a dword from 0x0086be20" and the opcode-0x54
    // record itself: CONFIRMED to fire (gated on session+host only,
    // since P4=1 is always true from Lua - Sec6.19's own "a host
    // therefore always replicates tutorial_start" closing line), but
    // this project does not simulate real network replication (same
    // project-wide convention as every other record-and-replicate
    // idiom, Sec4.13's cross-function note) - counted only.
    uint64_t replicateCallCount = 0;
};

// Sec32.3 `qte_human_is_used`: two fixed QTE slot records. This
// project's own name-keyed simplification of "the slot's owning player
// is the local or remote co-op player depending on a session-indexed
// byte" (that real local/remote selection mechanism is itself
// HYPOTHESIS/OPEN per spec) - a test sets whichever name currently owns
// a slot directly (by whatever sentinel it chooses, e.g. "#PLAYER1#"),
// rather than this project re-deriving local-vs-remote identity from
// playerRig() here.
struct QteSlot {
    bool active = false;
    std::string owningPlayerName;
    std::vector<std::string> participantNames; // the slot's up to five id-pair-identified participants, modeled by name
};

// hud_prompt (Sec13.3): success path (arg 1 resolves) records a
// request; failure path resets the global default/HUD-prompt state
// (0x00a36e60(0), independently decompiled, 3 real callers project-
// wide) - counted, not modelled further (its own internal fields are
// OPEN).
struct HudPromptRequest {
    std::string target, arg2, arg3;
};

// set_team (Sec13.18): reuses lookupNotorietyTrackCode/
// registerNotorietyTrackCodeForTesting/kNotorietyTrackCodeNotFound
// above directly - Sec13.18's own real, CONFIRMED finding that a
// character's team is drawn from the IDENTICAL gang/faction enumeration
// as the notoriety-track system (0x0094cc60, the SAME resolver
// notoriety_force_no_spawn's own arg 1 already uses, Sec3.6).

// hud_timer_hide (Sec13.12): a fixed pool of (at most) 4 HUD-timer
// slots - CHOSEN all-false ("not hidden") at construction, since no
// spec gives a real start-up value for either byte array.
struct HudTimerSlots {
    std::array<bool, 4> hidden{};          // 0x022b6665, stride 0x40 (per-slot visibility byte)
    std::array<bool, 4> layoutCompanion{}; // 0x022b6664 (companion byte)
    bool anyVisibleSummary = false;        // 0x022b65f5
    int32_t layoutMode = 0;                // 0x022b6630 (CHOSEN 0; escalates 2 -> 4 per Sec13.12's own text)
};

// hud_critical_timer_set (Sec13.13): "only actually runs once per
// timer" is CONFIRMED gated on a single GLOBAL (0x014c835c == 0, not
// per-slot) - this project's own direct reproduction: the FIRST call
// across the whole run configures; every later call is a no-op for
// that part (CHOSEN false = "not yet configured").
struct HudCriticalTimerState {
    bool configured = false;
    int64_t timerId = 0;
    std::string labelText;
    std::string secondaryKey;
    bool secondaryKeyResolved = false;
};

// reticle_override_set (Sec20.8): the two fixed 16-byte buffers
// (0x022b7030/0x022b7020) and the "override active"/duration globals
// are GLOBAL singletons, not per-object state (CONFIRMED - no object
// resolve of any kind happens in this native's own body at all).
// CONFIRMED truncation to a 16-byte (15 char + NUL) buffer via the
// real "fixed-size copy helper" 0x00da78d0 - a safe, bounded copy is
// the real CONFIRMED behavior here, not a hardening divergence.
struct ReticleOverride {
    bool active = false;
    std::string str1;
    std::string str2;
    double duration = 0.0;
};

// --- Sec50.2 object_indicator_init_lua ----------------------------------
struct ObjectIndicatorInitLua {
    bool oneTimeSetupDone = false;
    uint64_t elementCacheCount = 0;
    OpenValue<bool> callbackPoolAvailable{"object_indicator_init_lua callback pool availability",
                                           "spec-lua-api-behaviour.md Sec50.2"};
    uint64_t poolExhaustedGuardCount = 0;
};
// --- hud_pop_screen (§54.4): CONFIRMED only pops when the named
// screen is on top of the stack, but broadcasts the pop regardless.
// Reuses the SAME hudScreenStack()/hudScreenStack_ hud_push_screen
// (Sec21.6, above) already models as its one plain-vector stand-in for
// the real HUD-screen stack - not a second, duplicate field (push side
// is out of this batch's own 93 names, so only pop is exercised here;
// a test can still seed it via the shared accessor).

// --- hud_x_of_y_add/_update(§54.3 CRASH)/_remove(§41.4 CRASH). A new
// fixed 4-slot array (CONFIRMED real bound, from hud_x_of_y_add's own
// "correctly rejects indices outside 0-3"). HARDENED (CHOSEN
// divergence) for update/remove: the real engine applies ZERO bound
// checking to update's slot index (a worse, write-shaped sibling of
// remove's own unbounded-index-into-a-wild-indirect-call hazard) -
// both are bound-checked here to the SAME 0-3 range add already
// enforces, refused+counted outside it.
struct HudXOfYSlot {
    bool active = false;
    double current = 0.0;
    double total = 0.0;
};

struct EngineStateHudData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using TutorialLookup = sr3luahost::TutorialLookup;
    using TutorialStart = sr3luahost::TutorialStart;
    using QteSlot = sr3luahost::QteSlot;
    using HudPromptRequest = sr3luahost::HudPromptRequest;
    using HudTimerSlots = sr3luahost::HudTimerSlots;
    using HudCriticalTimerState = sr3luahost::HudCriticalTimerState;
    using ReticleOverride = sr3luahost::ReticleOverride;
    using ObjectIndicatorInitLua = sr3luahost::ObjectIndicatorInitLua;
    using HudXOfYSlot = sr3luahost::HudXOfYSlot;

public:
    int hudInventoryRefreshCount() const { return hudInventoryRefreshCount_; }
    void recordHudInventoryRefresh() { ++hudInventoryRefreshCount_; }

    // --- tutorial_advance (Sec10.4) -----------------------------------------

    // Batch 2026-10-01 (spec-lua-api-behaviour.md Sec10.4, Sec6.19, Sec26.28).
    // The name resolver 0x00717780 searches the static, file-backed table of
    // 210 name pointers at 0x012f5930 case-insensitively and returns the
    // first matching index or -1 (CONFIRMED). The names are the functional
    // identifiers Sec26.28 lists (owner's content ruling 2026-10-01; kept in
    // src/lua_tutorial_names.cpp). Index 176's string is OPEN: the spec only
    // says it is shorter than 4 characters or non-ASCII.
    static constexpr int kTutorialEntryCount = 210;
    // Per-entry run-time STATE (entry +0x0c of the table at 0x0151d600, 0-4,
    // not a kind - Sec6.19). Keyed by the decimal index. CONFIRMED initial
    // values (applySpecInitialState): entries 0-188 state 0, 189-209 state 1
    // after the registration fill 0x007178c0 (Sec26.28).
    OpenValueMap<int>& tutorialState() { return tutorialState_; }
    static std::string tutorialStateKey(int index) { return std::to_string(index); }
    TutorialStart& tutorialStart() { return tutorialStart_; }

    // --- on_qte_animation_trigger (Sec10.7) ---------------------------------

    // Real body (CONFIRMED, disassembly): registers (non-empty callback
    // name) or clears (empty callback name) a SINGLE GLOBAL QTE-animation-
    // trigger hook slot, rooted at a fixed global base rather than inside
    // a per-object slot array (Sec10.7) - a genuinely different shape
    // from the on_death/on_take_damage/on_revived per-CHARACTER hook-slot
    // family (CharacterState::onTakeDamageCallback/onRevivedCallback
    // above), so this project's own matching stand-in lives directly on
    // EngineState itself, not on any CharacterState. Default empty ("no
    // callback registered").
    const std::string& qteAnimationTriggerCallback() const { return qteAnimationTriggerCallback_; }
    void setQteAnimationTriggerCallback(const std::string& callbackName) { qteAnimationTriggerCallback_ = callbackName; }

    // Sec32.5 `object_indicator_remove_do`: counts the opcode 0x40 sub-tag 4
    // record (bit 0x2 of the arg-2 bitmask) - this project builds no
    // networking layer (see replicateStateChange's own doc comment above).
    // Bit 0x1's own "clear every indicator" effect reuses
    // CharacterState::objectIndicators directly (the stub clears that
    // vector) rather than a separate field here.
    uint64_t& indicatorRemoveRecordCount() { return indicatorRemoveRecordCount_; }

    // objective_text (Sec1.3): see ObjectiveTextSlot's own doc comment
    // above for the full reasoning (map-keyed-by-slot, not a fixed array;
    // localization is OPEN and not modelled; the arg3/arg4 combining rule
    // is reproduced). A slot's PRESENCE in this map (count() != 0) is this
    // project's own "is this slot currently showing text" signal - the HUD
    // objective-text overlay (tools/sr3_viewer.cpp, spec-hud-runtime.md
    // §6.5) iterates slots 0..3 and only draws a slot present here.
    // objective_text_clear (Sec1.4, recordObjectiveTextClear() above) erases
    // the entry on clear, so a cleared slot is never left stale in this map.
    std::unordered_map<int64_t, ObjectiveTextSlot>& objectiveTextSlots() { return objectiveTextSlots_; }
    // 0x022b65f5 - a GLOBAL "a timer is active" byte, distinct from any
    // one slot's own liveness (hud_timer_stop's own text: "clears a
    // global 'timer active' flag ... when the slot's own liveness byte is
    // already clear"). CHOSEN false initial state - no spec gives this
    // byte's own real construction value.
    bool& hudTimerGlobalActive() { return hudTimerGlobalActive_; }

    HudHardeningCounters& hudHardeningCounters() { return hudHardeningCounters_; }

    // hud_prompt_clear (Sec8.17): the local, always-taken clear path. The
    // genuinely network-replicated path (opcode 0x4a, gated on the target
    // matching the remote co-op player getter 0x009df3d0) can never fire
    // in this project's single-player-only scope (no remote co-op player
    // entry is ever modeled - the same class of reasoning
    // player_names_get_all, Sec51.4, already applies), so only this one
    // counter is test-observable.
    uint64_t& hudPromptClearCount() { return hudPromptClearCount_; }

    // hud_set_fake_notoriety (Sec8.18): the 6-slot (0-3 real tracks + the
    // CONFIRMED sentinel slot 5; index 4 unused) fixed array - bounded by
    // construction (the stub's own resolveFakeNotorietySlot() always
    // returns 0-3 or 5), no OOB risk, no hardening needed.
    std::array<FakeNotorietySlot, 6>& fakeNotorietySlots() { return fakeNotorietySlots_; }

    // hud_display_create_state/remove_state/commit_state (Sec15.24/15.12/
    // 15.25) - see HudDisplayStatePool's own doc comment above for the full
    // shared-infra reasoning.
    HudDisplayStatePool& hudDisplayStatePool() { return hudDisplayStatePool_; }

    // qte_cleanup (Sec15.6) - see QteSlotEntry's own doc comment above for
    // the "mechanism confirmed, no in-scope producer" reasoning.
    void registerActiveQteSlotForTesting(int linkedHudSlotId) { qteActiveSlots_.push_back({linkedHudSlotId}); }
    size_t qteActiveSlotCount() const { return qteActiveSlots_.size(); }
    uint64_t qteCleanupCount() const { return qteCleanupCount_; }
    const std::vector<QteSlotEntry>& qteActiveSlots() const { return qteActiveSlots_; }

    // message (Sec15.11): CONFIRMED a message/popup-instance id is always
    // returned regardless of which of the 2 dispatch paths runs (Sec15.11's
    // own raw-disassembly-confirmed single return value) - no real id
    // scheme is given, so this project reuses its own established
    // monotonically-increasing-handle convention (nextAudioVoiceHandle()/
    // nextVisualEffectHandle()'s own precedent), as its own dedicated
    // counter (Sec15.11's own text: "a SEPARATE message-slot mechanism
    // from the hud_display_* trio's own slot-queue - no shared state was
    // found between them").
    int64_t nextMessageHandle() { return nextMessageHandle_++; }

    // tutorial_set_hud_help_closing (Sec16.3): global 0x0151d5a4, direct
    // unconditional write, no gate.
    bool& tutorialHudHelpClosing() { return tutorialHudHelpClosing_; }
    std::vector<HudPromptRequest>& hudPromptRequests() { return hudPromptRequests_; }
    // Sec13.3 (e8dc04d): hud_prompt's arg-3 callback fires on an interact press
    // (handler-table 0x0111f2d0 entry 13, 0x009da810) while 0x02703cac != 0 (and
    // no lower-bit handler consumed the press): the named global is called with
    // NO args, then the prompt is cleared (0x00a36e60(0)). Returns the callback
    // name to invoke ("" when no prompt is active - the press is not consumed)
    // and clears the prompt. The caller (the interact-press dispatcher) does the
    // by-name call.
    bool hudPromptActive() const { return hudPromptActive_; }
    uint64_t hudPromptDefaultResetCount() const { return hudPromptDefaultResetCount_; }
    HudTimerSlots& hudTimerSlots() { return hudTimerSlotsPool13_; }
    HudCriticalTimerState& hudCriticalTimer() { return hudCriticalTimer_; }
    uint64_t& hudCriticalTimerLogCount() { return hudCriticalTimerLogCount_; }
    uint64_t& hudCriticalTimerNotifyCount() { return hudCriticalTimerNotifyCount_; }

    // hud_critical_timer_stop (Sec12.1, 0x00a4ed00): CONFIRMED a genuine
    // zero-argument command, single fixed widget - the internal helper
    // (0x006d0dc0) and the stale-handle-clear tag (0xc) are both OPEN past
    // their own call structure (this project builds no HUD timer/bar slot-
    // array infrastructure yet, same deferred-infra note as this batch's
    // own task brief). Call-count only.
    uint64_t hudCriticalTimerStopCount() const { return hudCriticalTimerStopCount_; }
    void recordHudCriticalTimerStop() { ++hudCriticalTimerStopCount_; }

    // tutorial_vehicle_controls_enabled (Sec20.4): a bare global boolean, no
    // resolver, no gate, no record - CHOSEN false default (no spec default
    // given; matches this project's usual "never touched" starting point).
    bool& tutorialVehicleControlsEnabled() { return tutorialVehicleControlsEnabled_; }

    // hud_push_screen (Sec21.6): a DIFFERENT, dedicated HUD-screen registry
    // (0x012fced8) from the 0x02442750 multi-kind singleton objectResolves()
    // stands in for elsewhere - kept as its OWN map, not folded into
    // objectResolves(), matching this native's own explicit "a DIFFERENT
    // singleton" text. hudScreenTypeField is the screen's own +0x30 field
    // (role: "Planet_Saints" special-case trigger value 0x29, AND the
    // mask-bit-1 broadcast payload) - OPEN until a test sets it (no spec
    // default per screen).
    OpenValueMap<bool>& hudScreenResolves() { return hudScreenResolves_; }
    OpenValueMap<int32_t>& hudScreenTypeField() { return hudScreenTypeField_; }
    std::vector<std::string>& hudScreenStack() { return hudScreenStack_; }
    uint64_t hudPushScreenPlanetSaintsCount = 0;
    uint64_t hudPushScreenBroadcastCount = 0;
    ReticleOverride& reticleOverride() { return reticleOverride_; }
    static constexpr size_t kReticleOverrideBufferCap = 15; // 16-byte buffer, 15 chars + NUL
    void recordTutorialUnlock(int index) { ++tutorialUnlockCounts_[index]; }

    // message_remove_all (Sec25.5): genuinely zero-real-argument (Sec25.5's
    // own text) - no real message queue modelled anywhere in this project,
    // so only a call count stands in for "cleared every pending message."
    uint64_t& messageRemoveAllCount() { return messageRemoveAllCount_; }

    // hud_inventory_disable (Sec25.12): CONFIRMED "always both" category -
    // unconditionally local-applies AND fires a record, with the host-check
    // result forwarded as data (not a skip gate). Default false (no
    // CONFIRMED construction value).
    bool& hudInventoryDisabled() { return hudInventoryDisabled_; }
    uint64_t& hudInventoryDisableReplicateCount() { return hudInventoryDisableReplicateCount_; }
    uint64_t& hudInventoryDisableSecondaryCallCount() { return hudInventoryDisableSecondaryCallCount_; } // 0x007f01b0, role OPEN

    // hud_critical_timer_get_remainder (Sec25.13): a DIFFERENT global
    // ("critical" HUD countdown timer, 0x006cf080) from HudCriticalTimerState
    // above (hud_critical_timer_set's own, unrelated timer record) - CHOSEN
    // default 0.0 ("no time remaining"); 0x006cf080's own internal state
    // source is OPEN per Sec25.13's own text.
    double& hudCriticalTimerRemainder() { return hudCriticalTimerRemainder_; }

    // on_hud_screen_exit (Sec24.24): the dedicated HUD-screen-name registry
    // singleton (0x012fced8) is a never-populated object space in this
    // project's scope - same "test-registrable" OpenValueMap<bool>
    // precedent as notorietySpawnGroupResolves/shops above (an unregistered
    // screen name is a real, CONFIRMED no-op per the shared hook-slot-array
    // family's own "on a match..." branch structure).
    OpenValueMap<bool>& hudScreenNameResolves() { return hudScreenNameResolves_; }

    // --- Sec44.5 game_hud_hide: CONFIRMED reference-counted (clamped at 0);
    // calling with no argument counts as a show, not a no-op.
    int32_t& hudHideRefCount() { return hudHideRefCount_; }
    ObjectIndicatorInitLua& objectIndicatorInitLua() { return objectIndicatorInitLua_; }

    // --- tutorial_get_case/_visible/_stop_current (§38.2/§52.3). Reuses
    // the EXISTING tutorialLookup()/210-entry table directly for name
    // resolution. tutorial_get_case's own "+0x10 field" (a per-entry case
    // number) has no spec-given data - OpenValueMap so a resolved-but-
    // unset entry refuses honestly (OpenStateError) rather than fabricate
    // a number, the SAME convention tutorialState() above already uses.
    // tutorial_visible's own boolean depends on "~11 further conditions,
    // not individually traced" (§38.2's own words, genuinely OPEN) - also
    // an OpenValue, refusing until a test sets it. tutorial_stop_current
    // has no tutorial-PROMPT-QUEUE model in this project (a different,
    // runtime-only mechanism from the 210-entry table) - call-count only,
    // same "deferred infra" convention as hudCriticalTimerStopCount_ above.
    OpenValueMap<double>& tutorialCaseNumber() { return tutorialCaseNumber_; }
    OpenValue<bool>& tutorialVisible() { return tutorialVisible_; }
    uint64_t tutorialStopCurrentCount() const { return tutorialStopCurrentCount_; }
    void recordTutorialStopCurrent() { ++tutorialStopCurrentCount_; }

    // --- character_take_human_shield_do / player_take_human_shield_do
    // (§57.5/§51.4). Reuses the EXISTING CharacterState::
    // humanShieldHostageName field directly (already modelled for an
    // earlier character_has_specific_human_shield-family native) - no new
    // per-character state needed.

    // --- qte_start_m02_skyqte_01 (§51.1 CRASH). CONFIRMED the underlying
    // worker checks its optional partner argument on the remote
    // (forwarded) branch but NOT on the local branch - reachable only for
    // a locally-owned character, which this project's whole single-player
    // scope always is. §51.1's own text is explicit that "a nil,
    // misspelled, despawned, or dead partner argument crashes" - CORRECTED
    // 2026-10-08 (review of this batch): a prior version of this guard
    // only covered "present but fails to resolve" (misspelled/despawned),
    // silently falling through for an omitted/nil partner (the most
    // common real case - this native's own partner argument is optional)
    // and never checking a resolved-but-dead partner at all, missing 2 of
    // the 4 named crash shapes. HARDENED (CHOSEN divergence): both an
    // omitted/nil partner AND a partner that fails to resolve are refused
    // (no-op) by the SAME counter below - the real crash is the identical
    // null dereference either way, so this project doesn't split "nil" and
    // "unresolved name" into separate counters (matching this project's
    // own established convention of ONLY splitting counters when the
    // underlying code path is genuinely distinct, e.g. action_play_do's
    // separate resolve-failure vs. dead-not-forced sentinels/counters,
    // Sec22.29). A resolved-but-dead partner is a genuinely distinct code
    // path (reads CharacterState rather than just objectResolves()), so it
    // gets its OWN counter below, reusing CharacterState::
    // notDeadForActionPlay() - this project's existing general "is this
    // character dead" predicate (the SAME lifeState==5/killFinalizedFlagE3
    // pair character_is_dead's own stub reads directly, Sec3.5) - rather
    // than inventing a new liveness check or reusing the narrower,
    // separately-scoped isDeadHighConfidence field (tied specifically to
    // set_current_hit_points' own internal logic, Sec7.31 - see that
    // field's own doc comment above for why it is NOT a general-purpose
    // dead check).
    uint64_t qteSkyqte01NullPartnerGuardCount() const { return qteSkyqte01NullPartnerGuardCount_; }
    void recordQteSkyqte01NullPartnerGuard() { ++qteSkyqte01NullPartnerGuardCount_; }
    uint64_t qteSkyqte01DeadPartnerGuardCount() const { return qteSkyqte01DeadPartnerGuardCount_; }
    void recordQteSkyqte01DeadPartnerGuard() { ++qteSkyqte01DeadPartnerGuardCount_; }
    uint64_t qteSkyqte01StartCount() const { return qteSkyqte01StartCount_; }
    void recordQteSkyqte01Start() { ++qteSkyqte01StartCount_; }

    // --- message_remove (§50.2). CONFIRMED "fully bounds- and generation-
    // checked, no crash shape" - the already-registered message() (Sec15.11,
    // lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp) models its
    // own returned id as a bare monotonic handle with no backing slot
    // array, so there is nothing for this native to actually mutate;
    // implemented as a safe call counter, consistent with that existing
    // simplification (not a cross-batch state change to stub_message).
    uint64_t messageRemoveCount() const { return messageRemoveCount_; }
    void recordMessageRemove() { ++messageRemoveCount_; }

    // --- minimap_icon_add_radius_do (§50.2 CRASH) / _remove_radius_do
    // (§50 intro, a pure alias of the ALREADY-REGISTERED
    // minimap_icon_remove_do, lua_spec_confirmed_stubs_b04_r04_r06_08.cpp -
    // reproduced byte-for-byte in the new file rather than cross-TU-
    // aliased, since that existing stub has internal linkage; see the new
    // stub's own doc comment). Both reuse CharacterState::minimapIcons
    // directly - no new per-object state. HARDENED (CHOSEN divergence,
    // add_radius_do only): the real engine reads a handle through a null
    // object pointer when the name is unresolved, and indexes a 4-entry
    // function-pointer table with no range check on the selector argument
    // (only 1-3 safe) - both refused (no-op) here instead, one shared
    // counter (either guard can fire independently per call).
    uint64_t minimapIconAddRadiusGuardCount() const { return minimapIconAddRadiusGuardCount_; }
    void recordMinimapIconAddRadiusGuard() { ++minimapIconAddRadiusGuardCount_; }

    // --- hud_get_text_adventure_current_screen_index (§41.4): CONFIRMED
    // consume-once (reads a global then immediately resets it to -1).
    int32_t& hudTextAdventureScreenIndex() { return hudTextAdventureScreenIndex_; }
    // --- hud_get_proto_selection (§54.4): "a sticky poll result" (does
    // NOT consume-once, unlike the field above) - CHOSEN default -1.
    int32_t& hudProtoSelection() { return hudProtoSelection_; }
    static constexpr size_t kHudXOfYSlotCount = 4;
    std::array<HudXOfYSlot, kHudXOfYSlotCount>& hudXOfYSlots() { return hudXOfYSlots_; }
    uint64_t hudXOfYAddOobGuardCount() const { return hudXOfYAddOobGuardCount_; }
    void recordHudXOfYAddOobGuard() { ++hudXOfYAddOobGuardCount_; }
    uint64_t hudXOfYUpdateOobGuardCount() const { return hudXOfYUpdateOobGuardCount_; }
    void recordHudXOfYUpdateOobGuard() { ++hudXOfYUpdateOobGuardCount_; }
    uint64_t hudXOfYRemoveOobGuardCount() const { return hudXOfYRemoveOobGuardCount_; }
    void recordHudXOfYRemoveOobGuard() { ++hudXOfYRemoveOobGuardCount_; }

    // --- Script-group HUD markers (§42.4): minimap_icon_add_script_group_do
    // CRASH / minimap_icon_remove_script_group_do CRASH (both share the
    // CONFIRMED unbounded 4-entry function-pointer-table-by-flags hazard;
    // add ALSO has a null icon-name dereference) / object_indicator_add_
    // script_group_do CRASH (CONFIRMED null-group dereference on a bad
    // group name) - this project has no group-membership roster to walk
    // (see w28GroupVisibility's own doc comment), so each is implemented
    // as a safe existence/argument-validated no-op; one named counter per
    // site, per this project's hardening policy.
    uint64_t minimapIconAddScriptGroupGuardCount() const { return minimapIconAddScriptGroupGuardCount_; }
    void recordMinimapIconAddScriptGroupGuard() { ++minimapIconAddScriptGroupGuardCount_; }
    uint64_t minimapIconRemoveScriptGroupGuardCount() const { return minimapIconRemoveScriptGroupGuardCount_; }
    void recordMinimapIconRemoveScriptGroupGuard() { ++minimapIconRemoveScriptGroupGuardCount_; }
    uint64_t objectIndicatorAddScriptGroupGuardCount() const { return objectIndicatorAddScriptGroupGuardCount_; }
    void recordObjectIndicatorAddScriptGroupGuard() { ++objectIndicatorAddScriptGroupGuardCount_; }

    // reticle_override_clear (Sec18.23): the two reticle-override globals
    // cleared on bit 0x1 of the caller's flags byte.
    bool& reticleOverrideGlobalsCleared() { return reticleOverrideGlobalsCleared_; }
    uint64_t& reticleOverrideClearLocalCount() { return reticleOverrideClearLocalCount_; }
    uint64_t& reticleOverrideClearReplicateCount() { return reticleOverrideClearReplicateCount_; }

protected: // private in EngineState before the split
    int hudInventoryRefreshCount_ = 0;                         // Sec10.3
    std::unordered_map<int, int> tutorialAdvanceCounts_;       // Sec10.4, by table index
    std::string qteAnimationTriggerCallback_;                  // Sec10.7
    OpenValueMap<int> tutorialState_{"tutorial entry state (0x0151d600[i] +0x0c)", "spec-lua-api-behaviour.md Sec6.19/Sec10.4/Sec26.28"};
    TutorialStart tutorialStart_; // Sec6.19, batch 2026-10-08
    QteSlot qteSlots_[2];
    uint64_t indicatorRemoveRecordCount_ = 0;

    // Ranking tranches 15/17/20/22/23 real-hit batch (2026-10-03) - see the
    // public accessors above for each field's own citation.
    std::unordered_map<int, int> tutorialLockCounts_;  // Sec52.3, by table index
    std::unordered_map<int64_t, uint64_t> objectiveTextClearCounts_;
    std::unordered_map<int64_t, ObjectiveTextSlot> objectiveTextSlots_;
    std::array<HudBarSlot, kHudBarSlotCount> hudBarSlots_{};
    std::array<HudTimerSlot, kHudTimerSlotCount> hudTimerSlots_{};
    bool hudTimerGlobalActive_ = false;
    HudHardeningCounters hudHardeningCounters_;
    uint64_t hudPromptClearCount_ = 0;
    std::array<FakeNotorietySlot, 6> fakeNotorietySlots_{};
    HudDisplayStatePool hudDisplayStatePool_;             // Sec15.6/15.12/15.24/15.25
    std::vector<QteSlotEntry> qteActiveSlots_;
    uint64_t qteCleanupCount_ = 0;
    int64_t nextMessageHandle_ = 1;                       // Sec15.11

    bool tutorialHudHelpClosing_ = false;                 // Sec16.3
    std::vector<HudPromptRequest> hudPromptRequests_;
    uint64_t hudPromptDefaultResetCount_ = 0;
    bool hudPromptActive_ = false;            // 0x02703cac != 0, Sec13.3
    std::string hudPromptCallbackName_;       // slot 9 callback name, Sec13.3
    // Renamed from hudTimerSlots_ during merge to resolve a duplicate-member
    // collision with Batch 2's own differently-typed hudTimerSlots_ field
    // above (std::array<HudTimerSlot, kHudTimerSlotCount>, Sec8.19/8.20) -
    // same name, unrelated real mechanisms. Public accessor name unchanged.
    HudTimerSlots hudTimerSlotsPool13_;
    HudCriticalTimerState hudCriticalTimer_;
    uint64_t hudCriticalTimerLogCount_ = 0;
    uint64_t hudCriticalTimerNotifyCount_ = 0;
    std::unordered_map<std::string, int32_t> hudCriticalTimerSecondaryKeyIds_;
    int32_t nextHudCriticalTimerSecondaryKeyId_ = 1;
    uint64_t hudCriticalTimerStopCount_ = 0;
    bool tutorialVehicleControlsEnabled_ = false;
    OpenValueMap<bool> hudScreenResolves_{"HUD screen registry (0x012fced8) name resolution",
                                          "spec-lua-api-behaviour.md Sec21.6"};
    OpenValueMap<int32_t> hudScreenTypeField_{"HUD screen's own +0x30 field", "spec-lua-api-behaviour.md Sec21.6"};
    std::vector<std::string> hudScreenStack_;
    ReticleOverride reticleOverride_;
    std::unordered_map<int, int> tutorialUnlockCounts_;
    uint64_t messageRemoveAllCount_ = 0;
    bool hudInventoryDisabled_ = false;
    uint64_t hudInventoryDisableReplicateCount_ = 0;
    uint64_t hudInventoryDisableSecondaryCallCount_ = 0;
    double hudCriticalTimerRemainder_ = 0.0;
    OpenValueMap<bool> hudScreenNameResolves_{"HUD-screen-name registry (0x012fced8)",
                                              "spec-lua-api-behaviour.md Sec24.24"};
    int32_t hudHideRefCount_ = 0;
    ObjectIndicatorInitLua objectIndicatorInitLua_;
    OpenValueMap<double> tutorialCaseNumber_{"tutorial entry +0x10 field (tutorial_get_case's own case-number)",
                                              "spec-lua-api-behaviour.md Sec38.2"};
    OpenValue<bool> tutorialVisible_{"tutorial_visible's own ~11-condition visibility predicate",
                                     "spec-lua-api-behaviour.md Sec38.2"};
    uint64_t tutorialStopCurrentCount_ = 0;
    uint64_t qteSkyqte01NullPartnerGuardCount_ = 0;
    uint64_t qteSkyqte01DeadPartnerGuardCount_ = 0;
    uint64_t qteSkyqte01StartCount_ = 0;
    uint64_t messageRemoveCount_ = 0;
    uint64_t minimapIconAddRadiusGuardCount_ = 0;
    int32_t hudTextAdventureScreenIndex_ = -1;
    int32_t hudProtoSelection_ = -1;
    std::array<HudXOfYSlot, kHudXOfYSlotCount> hudXOfYSlots_{};
    uint64_t hudXOfYAddOobGuardCount_ = 0;
    uint64_t hudXOfYUpdateOobGuardCount_ = 0;
    uint64_t hudXOfYRemoveOobGuardCount_ = 0;
    uint64_t minimapIconAddScriptGroupGuardCount_ = 0;
    uint64_t minimapIconRemoveScriptGroupGuardCount_ = 0;
    uint64_t objectIndicatorAddScriptGroupGuardCount_ = 0;
    bool reticleOverrideGlobalsCleared_ = false;
    uint64_t reticleOverrideClearLocalCount_ = 0;
    uint64_t reticleOverrideClearReplicateCount_ = 0;
};

} // namespace sr3luahost
