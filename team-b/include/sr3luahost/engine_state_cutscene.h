// EngineState slice: cutscene machine, zscene, screen fade, game pause, camera, screen effects.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateCutsceneData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateCutsceneData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// --- zscene (spec-lua-api-behaviour.md Sec26.25, Sec14.23, Sec8.21;
// batch 2026-10-01, lifecycle driver + scene table 2026-10-01 nnlt text;
// implementation in src/lua_cutscene.cpp) -------------------------------
//
// Scene identity: the engine keys its scene table (0x0153b294, entries
// 0xf8 bytes) by the CRC-32 of the lower-cased name (0x00d9e8b0, seed 0)
// and compares entry POINTERS for "current"/"pending". This host names an
// entry by its lower-cased name (zsceneTableKey). With the real table
// installed (installZsceneTable) a name resolves through the CRC, first
// match, like 0x00721be0, so two names with one CRC reach one entry.
//
// Scene table (REAL, Sec26.25 item 5): built from cutscene.xtbl (names)
// and one <name>.cte_xtbl per scene (fields) by sr3tables_cutscene;
// lua_host_run installs it from cutscene_tables.vpp_pc. A name missing
// from an installed table is "missing" (REAL). Without an installed table
// the per-name map zsceneLoadable[key] is used, OPEN until a test sets it.
//
// Start-up values: 0x0153b556 the skip_all_cutscenes byte (false),
// 0x0153b530 the current entry ("" = null, Sec26.25 Globals, RESOLVED
// 2026-10-02), 0x0153b542 the re-queue-on-reset byte (false - Sec26.25
// Globals' own 2026-10-02 correction: "permanently zero in practice...
// all seven of its call sites in the binary pass 0", not merely a
// start-up guess) and the nearest scene-bearing world object (list
// 0x03171a64; "" = none found - this host has no world-object manager
// anywhere, so the real list is unconditionally empty here, not merely
// unpopulated - zscene_prep blocker fix, 2026-10-09) are set by
// applySpecInitialState (lua_spec_initial_state.cpp).
// CONFIRMED globals with no specced start-up value - all OPEN at start:
//  - 0x0153b538 pending entry.
//  - 0x0153b51c load state: 0 idle, 1 loading, 2 loaded (all CONFIRMED).
//  - 0x0153b541 (label HIGH CONFIDENCE).
//  - the soundtrack stream 0x0153b71c, its status and its timer 0x0153b724.
// OPEN because the host has no streaming or audio subsystem (each a
// labelled, test-settable OpenValue; an explicit .set()/.forget() always
// wins):
//  - the class 0x00dafb60 gives an entry's selected request-group handle
//    (1 not live, 3 resident, 5 failed, anything else: wait). Per entry.
//    The selection between +0xc and +0x10 (the female variant) is folded
//    into this one "selected handle" class - a stated simplification.
//    zscenePromotionStep() (lua_cutscene.cpp) now writes a REAL default
//    of 1 ("not live"/null handle) for a name's FIRST-EVER promotion,
//    narrowly, only when no test has already given it a class: Sec2.7's
//    own classifier names "null handle" as its first-checked, catch-all
//    case, and Sec26.25's Host summary spells out the exact, CONFIRMED
//    consequence in full for a host like this one with no real
//    streaming backend ("a host whose streaming model never marks the
//    group resident leaves the scene at load state 1 forever"/"a refused
//    handle start silently drops the scene ... the scene is silently
//    forgotten"). This is a write at the one real event (the simulated
//    0x00dafea0 handle-start call) that would produce it, not a blanket
//    read-time default - it never touches a name a test has set, and it
//    never resolves the SEPARATE, still-genuinely-OPEN "what 0x00dafad0
//    leaves the flag word as" question (Sec26.25 review status) a LIVE
//    teardown's own .forget() raises - that case keeps refusing exactly
//    as before (see testFailedLoad, synthetic_luahost_cutscene_test.cpp).
//  - whether the soundtrack stream has ended (status 0x66).
struct ZsceneTableRow {
    std::string name;               // +0x0 as written in cutscene.xtbl
    uint32_t crc = 0;               // +0x4: NameHash(lower-cased name, 0)
    std::optional<int> kind;        // +0x8 (nullopt: OPEN, see sr3tables_cutscene)
    // +0x20 resources (names for the host's 0x00722f10 load log);
    // nullopt: OPEN (the spec describes the list only for kinds 1 and 2).
    std::optional<std::vector<std::string>> resources;
    // sr3tables_cutscene::SceneFields::cameraScriptText (added 2026-10-08,
    // the cutscene_play_do camera-script wiring task) - the CameraScript
    // element's raw text, unresolved. nullopt: OPEN (absent in the real
    // .cte_xtbl, or a row built without this field, e.g. an existing
    // test's own hand-built rows - see that header's own doc comment).
    std::optional<std::string> cameraScriptName;
};

// --- cutscene machine (Sec26.25 item 6, "The cutscene machine proper";
// src/lua_cutscene.cpp) ---------------------------------------------------
//
// The engine runs, once per frame, the state machine 0x0072d660 (jump
// table of 20 states) and then its second step 0x007258a0 (five cases:
// 0, 2, 4, 5, 15). The cutscene state is 0x0153b520 (ScreenFade::
// cutsceneState, shared with the fade routine's logo test). States 0..0xf
// are CONFIRMED; 0x10..0x13 are HIGH CONFIDENCE (by elimination) and are
// refused, as are the states whose helper bodies are OPEN (6..14) and
// the continuation of 0x00725df0 after state 2's load wait.
struct CutsceneManager { // *0x0153b528
    OpenValue<bool> present{"*0x0153b528 non-null (cutscene manager)", "spec-lua-api-behaviour.md Sec26.25"};
    // Host identity of the scene the manager plays (the engine holds it in
    // the manager object; which field is not specced).
    OpenValue<std::string> sceneKey{"cutscene manager's scene entry", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<int32_t> field8{"cutscene manager +8", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<std::string> chainTarget{"cutscene manager +0x35cc (next cutscene)", "spec-lua-api-behaviour.md Sec26.25"};
};
struct CutsceneCounters {
    uint64_t framesRun = 0;
    uint64_t framesBlockedOnOpen = 0;
    bool lastFrameBlocked = false;
    std::string lastFrameBlocker;
    uint64_t stateTransitions = 0;
    uint64_t zscenePromotions = 0;   // 0x00720410 runs
    uint64_t zsceneCompletions = 0;  // 0x007285c0: state := 2
    uint64_t zsceneFailedLoads = 0;  // 0x007285c0: class 5 -> teardown
    uint64_t zsceneIdleAutoPreps = 0; // 0x00728440 -> 0x007232e0
};

// --- screen fade (spec-lua-api-behaviour.md Sec26.24, Sec26.9, Sec2.9,
// Sec8.13; batch 2026-10-01; implementation in src/lua_screen_fade.cpp) --
//
// CONFIRMED (Sec26.24): one state dword 0x012e6aa4 (0 fading in, 1 fading
// out, 2 fully in - the start-up state, 3 fully out) and a target
// 0x012e6aa8 (3 out, 2 in). The request helpers 0x0059f8c0 (out) and
// 0x0059fc40 (in) only set the direction and call the UI-state Lua global
// screen_fade_do(flag, alpha, durationMs); a fade completes when the UI
// script calls the UI-state native Screen_fade_transition_complete
// (0x005a0110), which this host registers in the UI state. A request
// parked while the opposite transition runs is replayed by the
// per-frame routine 0x0059fe70 with 250 ms.
//
// Every global below is an OpenValue: applySpecInitialState sets the
// file-backed initial values Sec26.24 lists (and, 2026-10-07, the
// cutscene state's own zero-fill default, Sec60.4); the mode-stack top
// the per-frame routine reads is still OPEN.
using FadeCallback = void (*)(EngineState& es, uint32_t target);
struct ScreenFade {
    OpenValue<uint32_t> state{"0x012e6aa4 (fade state)", "spec-lua-api-behaviour.md Sec26.24"};
    OpenValue<uint32_t> target{"0x012e6aa8 (fade target)", "spec-lua-api-behaviour.md Sec26.24"};
    OpenValue<uint32_t> flag{"0x013effc8 (fade flag)", "spec-lua-api-behaviour.md Sec26.24"};
    // 0x012e6aa0 != -1: the "screen_fade" UI document record is loaded
    // (its id is HIGH CONFIDENCE as a label). -1 in the file.
    OpenValue<bool> documentLoaded{"0x012e6aa0 != -1 (screen_fade document id)", "spec-lua-api-behaviour.md Sec26.24"};
    // Millisecond stamps, -1 = unset (Sec26.24 table).
    OpenValue<int64_t> logoAt{"0x012e6aac (show loading logo at)", "spec-lua-api-behaviour.md Sec26.24"};
    OpenValue<int64_t> holdLogoUntil{"0x012e6ab0 (hold fade-in until, logo)", "spec-lua-api-behaviour.md Sec26.24"};
    OpenValue<int64_t> imagesAt{"0x012e6ab4 (show load images at)", "spec-lua-api-behaviour.md Sec26.24"};
    OpenValue<int64_t> holdImagesUntil{"0x012e6ab8 (hold fade-in until, images)", "spec-lua-api-behaviour.md Sec26.24"};
    OpenValue<int64_t> autoSaveStamp{"0x012e6abc (auto-save indicator stamp)", "spec-lua-api-behaviour.md Sec26.24"};
    OpenValue<int32_t> autoSaveCounter{"0x013effd4 (auto-save counter)", "spec-lua-api-behaviour.md Sec26.24"};
    OpenValue<bool> useLoadImages{"0x0149365c (sfx_use_load_images byte)", "spec-lua-api-behaviour.md Sec26.24"};
    OpenValue<bool> lastBroadcastWasOut{"0x013effc5 (last 0x53 record was a fade-out)", "spec-lua-api-behaviour.md Sec26.24"};
    // Read by the per-frame routine; OPEN (no spec value).
    OpenValue<int32_t> modeStackTop{"mode stack top (0x00706ab0: 0x01503b50[0x012f4a80])", "spec-lua-api-behaviour.md Sec26.24"};
    // CONFIRMED known at construction (Sec60.4, 2026-10-07 closure,
    // applySpecInitialState() sets it to 0/"idle") - zero-fill .data with
    // no static initializer, same mechanism as 0x0153b530/0x0153b556. See
    // applySpecInitialState's own item 4 comment (lua_spec_initial_state.cpp).
    OpenValue<int32_t> cutsceneState{"0x0153b520 (cutscene state)", "spec-lua-api-behaviour.md Sec26.24/Sec26.25"};
    // 0x013effcc in-flight and 0x013effd0 deferred completion callbacks:
    // 0 in the file (CONFIRMED); the Lua wrappers always pass 0.
    FadeCallback inFlight = nullptr;
    FadeCallback deferred = nullptr;
};

struct ScreenFadeCounters {
    uint64_t realCompletions = 0;         // the UI script's own call flipped the state
    uint64_t fallbackUndefined = 0;       // substitute: screen_fade_do undefined / no document
    uint64_t fallbackNoCallback = 0;      // substitute: screen_fade_do called, no completion
    uint64_t screenFadeDoCalls = 0;
    uint64_t screenFadeDoErrors = 0;      // Lua errors inside screen_fade_do (swallowed)
    uint64_t uiScriptCalls = 0;           // screen_fade_logo_show/images_show/auto_save_*
    uint64_t framesRun = 0;
    uint64_t framesBlockedOnOpen = 0;
    std::string lastFrameBlocker;         // the OPEN global that last stopped a frame
    std::string lastScreenFadeDoError;
};

// --- fade_out (spec-lua-api-behaviour.md Sec2.9) ------------------------

// CONFIRMED: the three colour components (0.0 when absent) plus a fixed
// alpha of 255 go to the overlay colour setter, which scales each by
// 1/255. Stored here already scaled. hasScreenFadeColour() is false
// until the first fade_out: the real initial overlay colour is not in
// any spec.
struct ScreenFadeColour {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
};

// Every request a Lua wrapper (fade_out / fade_in bit 0x1) handed to the
// request helper, in call order: trunc(seconds x 1000) and the alpha the
// helper passes to screen_fade_do (1.0 out, 0.0 in) - a log for tests.
struct ScreenFadeRequest {
    double durationMs = 0.0;
    float targetAlpha = 0.0f;
};

// --- Sec37.3's shared game-pause depth counter: a concrete instance ----
// (spec-frontend-menu-runtime.md Sec4, the fade-completion teardown's own
// push/pop of reason 0xd). The real engine: ~60 push/pop sites, 11
// reason codes, ONE shared counter, "a pop decrements the depth without
// checking its own reason was the one pushed" (Sec37.3's own text -
// bookkeeping only, which is why popGamePauseReason() below never
// verifies `reason` against what was pushed). The one CONFIRMED real
// CONSEQUENCE this project can actually model is bare_globals.h's own
// documented finding: every real pusher/popper (0x007074a0/0x00708a80
// push, 0x00707540/0x00708330 pop) passes the GAMEPLAY Lua state to the
// scheduler's refcounted exempt list - so a 0->1 push attaches the
// gameplay state to BareThreadTable's exempt list (if one was attached
// via attachGamePauseScheduler()) and a 1->0 pop detaches it, exactly
// matching that file's own "the gameplay state is exempted for exactly
// as long as any game-pause level is held" reading. BareThreadTable is
// only forward-declared here (no cross-include with bare_globals.h) -
// push/pop are implemented out-of-line in src/lua_menu_runtime.cpp,
// which includes it.
struct GamePauseDepthState {
    int32_t depth = 0;
    int32_t lastReason = -1;
};

// --- the fade-completion teardown's own dialog-pool class-bit scan -----
// (spec-frontend-menu-runtime.md Sec4, `0x007b4cb0` -> `0x007b3e80`/
// `0x007b3de0`). This project models no UI dialog pool (`0x02247b80`,
// the real engine's own tracked-entry array with per-entry class-flag
// bytes) - these two bools are the NET predicates that pool-walk would
// resolve to for the one real consumer this project can wire (the pause
// push/pop below), transcribed directly from the spec's own described
// inputs rather than inventing the pool itself. The gate `0x00867830`
// (push additionally requires it return false - OPEN, not traced) and
// the pop-side query `0x00707490` (OPEN) are both left unmodelled: NOT
// gating here is the deliberate, labelled choice (every real fade this
// project requests - including new game's and continue's own - routes
// through this exact callback, so throwing on an OPEN read here would
// make EVERY fade un-completable, not a safe default).
struct FadeTeardownDialogState {
    bool anyOpenClassBit0x8Dialog = false;  // §4's own pause-push/pop predicate input
    bool anyOpenClassBit0x40Dialog = false; // §4's own second, non-pause side effect's input (0x007f1070, OPEN what it does) - counted only
};

// screen_capture_preview_should_upload (Sec49.4, `ui`). 1 boolean, 0
// Lua return values either way (CONFIRMED). CONFIRMED: an explicit
// `false` argument takes a "cancel" path; a denied platform privilege
// silently takes the SAME cancel path (so the calling script cannot
// tell the two apart from the return value alone - there is none).
// This project has no real platform-privilege layer anywhere in scope
// to query (CHOSEN, explicitly labelled, NOT a confirmed value): the
// "platform denied" fork is modeled as ALWAYS "not denied" here, so
// only the explicit-false path is ever actually reachable from this
// host - `explicitFalseCancelCount` is the one CONFIRMED path;
// `uploadProceedCount` is this host's own stand-in answer for every
// `true` call (never "denied").
struct ScreenCapturePreview {
    uint64_t explicitFalseCancelCount = 0; // CONFIRMED: explicit false -> the cancel path
    uint64_t uploadProceedCount = 0;       // CHOSEN "always not denied" stand-in for a true call, see comment above
};

// --- cutscene_play_do's real camera-script pipeline (spec-lua-api-
// behaviour.md Sec57.2a / spec-cutscene-camera-format.md Sec10.6 item 7,
// 2026-10-08 wiring task) ------------------------------------------------
//
// REAL, Sec57.2a: cutscene_play_do's engine entry 0x00725df0 starts the
// 20-state cutscene machine (0x0072d660); one state handler
// (0x0072bff0) is the sole caller of the .csc_pc load driver
// (0x00737290), which is gated by the kind test 0x00721bc0 before
// calling the parser 0x00569290 (this project's sr3cutscene::
// camera_script, already implemented and tested from spec-cutscene-
// camera-format.md Sec10 directly). REAL, spec-cutscene-camera-
// format.md Sec10.6 item 7: the kind test "tests ... whether that
// entry's kind field ... equals 0 (Designer)" - ONLY Designer-kind
// cutscenes skip the camera-script load; Zscene (kind 1) and Story
// (kind 2) both require and load one, and "a missing or unparsable
// script aborts the cutscene load" (a REAL, defined abort, not a crash
// and not an OPEN unknown).
//
// This host has no manager-allocation / state-machine timing for this
// step (cutsceneManager_.present is never allocated here - same "this
// host never allocates X" scope limit cutscenePlayCheckDone() already
// documents above), so it is modeled as its own directly-callable step
// (cutsceneCameraScriptStep(), private, below), run synchronously from
// cutscenePlayDo() on every actual start (never on a refused one, same
// as the existing zscene-gate call it sits next to) - NOT folded into
// cutsceneMachineStep()'s case 7 above, because exactly how 0x0072bff0's
// own success/failure feeds the NEXT state transition is still OPEN
// (case 8's own helpers, 0x00720200/0x0072c260) and this task's own
// scope is the load pipeline itself, not a guess at that transition.
//
// The actual byte source for a named .csc_pc is NOT built into this
// class - same convention as loadVintDocument/loadZoneObjectsSection
// (both take an ALREADY-PARSED object), except that cutscene_play_do's
// Lua wrapper has no per-call opportunity to hand one in (Sec57.2: it
// only takes a name, the read-and-discarded 2nd argument, and a
// destination table), so the opener is installed ONCE, like
// installZsceneTable, by whoever wires this host to a real cache
// (tools/lua_host_run.cpp) or a test - mirrors sr3tables_cutscene's own
// SceneFileOpener shape exactly (include/sr3tables_cutscene/
// scene_table.h), one level further down the same real pipeline.
using CameraScriptOpener = std::function<std::optional<std::vector<uint8_t>>(const std::string& csFileName)>;

enum class CutsceneCameraScriptOutcome {
    NotAttempted,        // cutscenePlayDo has never actually started a cutscene yet
    SkippedDesignerKind, // REAL: kind 0, the 0x00721bc0 gate - the load is never attempted
    SkippedKindUnknown,  // CHOSEN (conservative default): kind not determinable (no scene
                         // table installed, or the name resolves to no entry) - never guess
                         // whether a script is needed when this host cannot tell
    Aborted,             // REAL: kind 1/2 but no bytes could be opened/parsed for this name
                         // ("a missing or unparsable script aborts the cutscene load")
    Loaded,              // REAL: kind 1/2, a real .csc_pc was opened and parsed
};

// completion_load_lut (Sec16.4): the "none"-prefix clear path is fully
// CONFIRMED and safe (slot 2 := {-1, 0.0}); the resolve-a-named-LUT
// path has no real name->id table in this project's scope (same class
// of gap as vehicle_anim_start above) - reuses this project's own
// established "no real table, a fresh monotonically-increasing handle
// stands in" convention (audio_conversation_load_direct's own
// precedent, Sec8.1) rather than refusing, since nothing in scope
// reads the resolved id back either way.
struct ColorGradingLutSlot { int32_t lutId = -1; double intensity = 0.0; };

// refraction_situation_set (Sec13.10): arg 1 is a data-table key hashed
// via sr3save::nameHash (same CRC-32 as customization_outfit_wear
// above). Bit 0x1 (default on): local apply (the real destination slot
// index this project has no source for, Sec13.10's own OPEN item, is
// not modelled - only that an apply happened). Bit 0x2 (default on):
// replicate.
struct RefractionSituationRequest {
    uint32_t hashedName = 0;
    int64_t flags = 3;
    bool appliedLocally = false;
};

// camera_restrict_set_limits (Sec14.21): one fixed camera-restriction
// record (this project's own stand-in for a singular, un-named real
// record Sec14.21's own text does not further identify) plus the
// local-player-name comparison its own 3-way dispatch needs (a
// SEPARATELY SCOPED stand-in - no prior field in this header carries
// the local player's own NAME as a string, only presence/gender
// booleans, hasLocalPlayer()/playerRig() above).
struct CameraRestrictLimits {
    double minYaw = 0, maxYaw = 0, minPitch = 0, maxPitch = 0;
    bool arg6 = false;
    bool written = false;
};

// camera_shake_play (Sec13.15): last-request stand-in, always
// replicated (CONFIRMED - no double-gate skip at this native).
struct CameraShakeState {
    std::string lastProfile;
    double lastMagnitude = 1.0;
    uint64_t playCount = 0;

    // camera_shake_stop (Sec17.14) / camera_shake_play_looping
    // (Sec17.15), wave 2 batch W2-2 (2026-10-08): the SAME shared
    // global camera-shake record family (CONFIRMED - both share the
    // recovered opcode-0x43 subtype 0xf) - a DISTINCT "looping" slot
    // from the one-shot lastProfile/lastMagnitude above (camera_shake_
    // play, Sec13.15, is a genuinely different, simpler dispatch per
    // this batch's own transposition-risk note - never conflated).
    std::string loopingProfile;
    double loopingMagnitude = 1.0;
    bool loopingActive = false;
    uint64_t loopingPlayCount = 0;
    uint64_t stopCount = 0;
};

// vehicle_set_invulnerable (Sec12.21, 0x00a62f70): RESOLVED, no new
// state needed - CONFIRMED (Sec46.1) to reach the exact same setter
// (0x00a7a830) turn_invulnerable's own vehicle branch already uses,
// merging the boolean into VehicleState::forceFlags1d7a's existing
// kInvulnerableBit1d7a bit directly.

// camera_restrict_target_object (Sec12.23, 0x00a45860): CONFIRMED a
// genuinely subtle two-part gate (arg 3, "except for this viewer,"
// selects which of {local-apply, network-replicate} happens) plus a
// two-resolver target lookup whose own vtable-method internals are
// OPEN. This project represents "the local player" the SAME way every
// other function in scope already does - the reserved "#PLAYER#"
// sentinel string (this document's own Sec3 front matter) - rather than
// inventing a separate identity check; see stub_camera_restrict_
// target_object's own doc comment for the full CHOSEN gate reasoning.
// The deep position/radius dispatch selection (0x0057a950/0x0057ab80/
// 0x0057aca0) is OPEN and not reproduced (no camera/world-position
// system in scope).
struct CameraRestrictTarget {
    bool hasTarget = false;
    std::string targetName;
    double radius = 0.0;
    bool localApplied = false;
    bool replicated = false;
    uint64_t callCount = 0;
};

// --- camera_look_through_do / camera_script_is_finished (Sec59.4) -----
// CONFIRMED: "the HUD-hide reference is taken BEFORE the target object
// is resolved" - hudHideRefCount below is incremented unconditionally
// by the stub, regardless of resolve outcome, matching that text
// exactly (a real "effect taken before its precondition is checked"
// logic defect, not a crash - reproduced faithfully, not hardened
// away). camera_script_is_finished's own "divide-by-zero-shaped but
// harmless NaN comparison for a zero duration" (CONFIRMED structure,
// HYPOTHESIS exact comparison outcome) is modelled as CHOSEN
// "immediately finished" for durationMs <= 0 - the simplest reading
// consistent with "harmless" that does not fabricate a specific NaN
// comparison result spec text does not give.
struct CameraLookThrough {
    int hudHideRefCount = 0;
    bool active = false;
    double durationMs = 0.0;
    OpenValue<double> elapsedMs{"camera look-through elapsed time (no real per-frame clock tied to this call)",
                                "spec-lua-api-behaviour.md Sec59.4"};
    uint64_t broadcastCount = 0;
};

// --- Sec44.4/Sec56.5 machinima family (game_machinima_*, cinema_editor_
// create_camera_zone/_save_clip, cell_machinima_is_recording): shared
// path-builder defect - the real builder leaves its output buffer
// unwritten on 3 failure exits and NO caller checks; this project's
// hardened divergence is to fail closed instead (every caller checks
// `ok` before using `path`). OPEN whether the environment (process-
// token/folder lookup/creation) succeeds; CHOSEN default true so normal
// operation proceeds, with a test-only toggle forcing the failure path.
struct MachinimaPathResult {
    bool ok = false;
    std::string path;
};
struct MachinimaState {
    bool recording = false;
    bool playbackActive = false;
    uint64_t playbackEnterCount = 0;
    uint64_t recordCount = 0;
    uint64_t pathBuilderFailureGuardCount = 0;
    std::unordered_map<std::string, bool> clips; // name -> exists (in-memory stand-in for the clips folder)
};

// --- Sec45.2 cinema_editor_create_camera_zone/_save_clip ----------------
struct CinemaEditorState {
    uint32_t nextZoneHandle = 1;
    std::unordered_map<uint32_t, uint32_t> zoneGeneration; // handle -> generation
    uint64_t oobTemplateGuardCount = 0;
    uint64_t createCount = 0;
    uint64_t saveClipPathGuardCount = 0;
};

struct EngineStateCutsceneData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using ZsceneTableRow = sr3luahost::ZsceneTableRow;
    using CutsceneManager = sr3luahost::CutsceneManager;
    using CutsceneCounters = sr3luahost::CutsceneCounters;
    using FadeCallback = sr3luahost::FadeCallback;
    using ScreenFade = sr3luahost::ScreenFade;
    using ScreenFadeCounters = sr3luahost::ScreenFadeCounters;
    using ScreenFadeColour = sr3luahost::ScreenFadeColour;
    using ScreenFadeRequest = sr3luahost::ScreenFadeRequest;
    using GamePauseDepthState = sr3luahost::GamePauseDepthState;
    using FadeTeardownDialogState = sr3luahost::FadeTeardownDialogState;
    using ScreenCapturePreview = sr3luahost::ScreenCapturePreview;
    using CameraScriptOpener = sr3luahost::CameraScriptOpener;
    using CutsceneCameraScriptOutcome = sr3luahost::CutsceneCameraScriptOutcome;
    using ColorGradingLutSlot = sr3luahost::ColorGradingLutSlot;
    using RefractionSituationRequest = sr3luahost::RefractionSituationRequest;
    using CameraRestrictLimits = sr3luahost::CameraRestrictLimits;
    using CameraShakeState = sr3luahost::CameraShakeState;
    using CameraRestrictTarget = sr3luahost::CameraRestrictTarget;
    using CameraLookThrough = sr3luahost::CameraLookThrough;
    using MachinimaPathResult = sr3luahost::MachinimaPathResult;
    using MachinimaState = sr3luahost::MachinimaState;
    using CinemaEditorState = sr3luahost::CinemaEditorState;

public:
    bool zsceneTableInstalled() const { return zsceneTableInstalled_; }
    size_t zsceneTableSize() const { return zsceneTable_.size(); }

    OpenValueMap<bool>& zsceneLoadable() { return zsceneLoadable_; }
    OpenValue<bool>& zsceneSkipAllCutscenes() { return zsceneSkipAllCutscenes_; }
    OpenValue<std::string>& zsceneCurrent() { return zsceneCurrent_; }
    OpenValue<std::string>& zscenePending() { return zscenePending_; }
    OpenValue<int>& zsceneStateCode() { return zsceneStateCode_; }
    OpenValue<bool>& zsceneAutoSelectNearest() { return zsceneAutoSelectNearest_; }
    OpenValue<bool>& zsceneRequeueOnReset() { return zsceneRequeueOnReset_; }
    OpenValueMap<int>& zsceneHandleClass() { return zsceneHandleClass_; }
    OpenValue<bool>& zsceneSoundtrackActive() { return zsceneSoundtrackActive_; }
    OpenValue<int64_t>& zsceneSoundtrackStartMs() { return zsceneSoundtrackStartMs_; }
    OpenValue<bool>& zsceneSoundtrackEnded() { return zsceneSoundtrackEnded_; }
    OpenValue<std::string>& zsceneNearestWorldObjectScene() { return zsceneNearestWorldObjectScene_; }
    int zscenePendingBlockedRefusals() const { return zscenePendingBlockedRefusals_; }
    CutsceneManager& cutsceneManager() { return cutsceneManager_; }
    OpenValue<int32_t>& cutsceneState() { return screenFade_.cutsceneState; }
    OpenValue<bool>& cutscenePreloadMounted() { return cutscenePreloadMounted_; }   // 0x0153b543
    OpenValue<std::string>& cutsceneChainTarget() { return cutsceneChainTarget_; }  // 0x0153b52c ("" = none)
    OpenValue<bool>& cutscenePlayerChecksPass() { return cutscenePlayerChecksPass_; }
    OpenValue<int64_t>& cutsceneLoadStamp() { return cutsceneLoadStamp_; }          // 0x012f5d30
    OpenValue<bool>& cutscenePlayingByte() { return cutscenePlayingByte_; }         // 0x0153b525
    OpenValue<bool>& cutsceneInProgressByte() { return cutsceneInProgressByte_; }   // 0x0153b526
    // Host logs of what states 4 and 5 started (no packfile mounting or
    // resource streaming in this host).
    const std::vector<std::string>& cutsceneMountLog() const { return cutsceneMountLog_; }
    const std::vector<std::string>& cutsceneLoadLog() const { return cutsceneLoadLog_; }
    const CutsceneCounters& cutsceneCounters() const { return cutsceneCounters_; }
    ScreenFade& screenFade() { return screenFade_; }
    const ScreenFade& screenFade() const { return screenFade_; }

    // The UI Lua state that stands in for the "screen_fade" document: where
    // screen_fade_do and the other screen_fade_* script globals are looked
    // up. Set when Screen_fade_transition_complete is registered there.
    void attachScreenFadeUiState(lua_State* ui) { screenFadeUi_ = ui; }
    lua_State* screenFadeUiState() const { return screenFadeUi_; }
    int64_t screenFadeClockMs() const { return screenFadeClockMs_; }
    const ScreenFadeCounters& screenFadeCounters() const { return screenFadeCounters_; }
    bool hasScreenFadeColour() const { return hasScreenFadeColour_; }
    const ScreenFadeColour& screenFadeColour() const { return screenFadeColour_; }
    void setScreenFadeColour(const ScreenFadeColour& c) { screenFadeColour_ = c; hasScreenFadeColour_ = true; }
    const std::vector<ScreenFadeRequest>& screenFadeRequests() const { return screenFadeRequests_; }
    void recordScreenFadeRequest(const ScreenFadeRequest& r) { screenFadeRequests_.push_back(r); }
    int screenFadeOpcode53Count() const { return screenFadeOpcode53Count_; }
    const GamePauseDepthState& gamePauseDepth() const { return gamePauseDepth_; }
    FadeTeardownDialogState& fadeTeardownDialogState() { return fadeTeardownDialogState_; }
    uint64_t fadeTeardownSecondEffectCount() const { return fadeTeardownSecondEffectCount_; }
    void recordFadeTeardownSecondEffect() { ++fadeTeardownSecondEffectCount_; }

    // Sec28.3 `hdr_bloom_set_multiplier`: 0x012ec280, CONFIRMED initial 1.0.
    float& hdrBloomMultiplier() { return hdrBloomMultiplier_; }
    ScreenCapturePreview& screenCapturePreview() { return screenCapturePreview_; }
    const std::string& cutscenePlayDestination1() const { return cutscenePlayDestination1_; }
    const std::string& cutscenePlayDestination2() const { return cutscenePlayDestination2_; }
    bool cutscenePlayInProgress() const { return cutscenePlayInProgress_; }
    void installCameraScriptOpener(CameraScriptOpener opener) { cameraScriptOpener_ = std::move(opener); }
    CutsceneCameraScriptOutcome cutsceneCameraScriptOutcome() const { return cutsceneCameraScriptOutcome_; }
    // Counts actual INVOCATIONS of the installed opener - 0 for
    // SkippedDesignerKind/SkippedKindUnknown proves the load was genuinely
    // never attempted, not merely that its result was discarded (the
    // assertion instruction #4/#5 of this task's own brief asks for
    // exactly this: "confirm no .csc_pc read was attempted").
    uint64_t cutsceneCameraScriptOpenerCalls() const { return cutsceneCameraScriptOpenerCalls_; }

    // --- vehicle_set_malfunctioning (Sec30.5): the local player's own
    // screen-distortion float(s) at +0x1650..+0x165c (tracked as one
    // scalar stand-in, no per-axis model - this project has no screen-
    // effect system beyond this one gated write), the global byte
    // 0x02625852, and the "cyber_m16_glitch" screen-effect preset slot
    // (tracked as a bool - this project has no real effect-preset table).
    double& localPlayerScreenDistortion1650() { return localPlayerScreenDistortion1650_; }
    bool& cyberM16GlitchEffectActive() { return cyberM16GlitchEffectActive_; }
    bool refractionGlobalOverrideCleared() const { return refractionGlobalOverrideCleared_; }
    uint64_t refractionClearLocalCount() const { return refractionClearLocalCount_; }
    uint64_t refractionClearReplicateCount() const { return refractionClearReplicateCount_; }
    const ColorGradingLutSlot& colorGradingLutSlot(int i) const { return colorGradingLutSlots_[i]; }

    // completion_cutscene_was_active (Sec16.5): pure read of global
    // 0x02282283 - this project's own real writer is out of scope (Sec16.5's
    // own text), so a test sets this directly.
    bool& completionCutsceneWasActive() { return completionCutsceneWasActive_; }
    std::vector<RefractionSituationRequest>& refractionSituationRequests() { return refractionSituationRequests_; }
    CameraRestrictLimits& cameraRestrictLimits() { return cameraRestrictLimits_; }

    // camera_restrict_set_active (Sec8.12, 0x00a445e0, batch b30,
    // 2026-10-08). CONFIRMED: a SEPARATE flag from CameraRestrictLimits
    // above - Sec8.12's own text never cross-cites Sec14.21's record, and
    // the two are different real addresses, so this project does not
    // assume an undocumented shared instance (same caution as the
    // forceNoSpawn/tierItemFlags split above). CHOSEN starting value
    // (false, "not restricted") - no spec states this flag's construction
    // value either way.
    bool cameraRestrictionActive() const { return cameraRestrictionActive_; }
    void setCameraRestrictionActive(bool active) { cameraRestrictionActive_ = active; }

    // drug_effect_set_override_values (Sec14.19): a fixed channel-0 entry of
    // a structurally multi-channel pair of parallel arrays (0x02625848/
    // 0x02625840) this project only ever reaches channel 0 of (HIGH
    // CONFIDENCE other channels exist and are reachable elsewhere, per
    // Sec14.19's own text) - CHOSEN 0.0 defaults.
    double& drugEffectOverrideValueA() { return drugEffectOverrideA_; }
    double& drugEffectOverrideValueB() { return drugEffectOverrideB_; }
    CameraShakeState& cameraShake() { return cameraShake_; }

    // camera_end_look_through (Sec13.16): the fixed 3-call teardown
    // sequence's own internals (0x0056cc30/0x00564910/0x007f1070) are OPEN -
    // counted only, same "record that a call happened" convention used
    // throughout this batch.
    uint64_t& cameraEndLookThroughCount() { return cameraEndLookThroughCount_; }
    uint64_t& cameraEndLookThroughSmoothCount() { return cameraEndLookThroughSmoothCount_; } // the boolean-gated 4th call
    CameraRestrictTarget& cameraRestrictTarget() { return cameraRestrictTarget_; }

    // dlc_gis_film_grain_load (Sec22.20): the SAME shared "active" latch
    // global (0x02317598) dlc_gis_film_grain_unload (Sec17.29, a DIFFERENT,
    // not-yet-implemented batch) also targets - modelled here since THIS
    // native is the one in scope; a future implementation of the `_unload`
    // sibling should reuse this SAME field, not a second one.
    bool& dlcGisFilmGrainActive() { return dlcGisFilmGrainActive_; }
    uint64_t& dlcGisFilmGrainLoadLatchSetCount() { return dlcGisFilmGrainLoadLatchSetCount_; }
    uint64_t& dlcGisFilmGrainLoadNotifyCount() { return dlcGisFilmGrainLoadNotifyCount_; }

    // --- camera_script_enable / camera_script_disable (Sec45.2) -----------
    // "write byte 0x013c9382 ('script camera owns the view,' HYPOTHESIS)
    // and... UNCONDITIONALLY broadcast... with no host check - either
    // machine can send it." CHOSEN false initial.
    bool& scriptCameraOwnsView() { return scriptCameraOwnsView_; }
    uint64_t& cameraScriptBroadcastCount() { return cameraScriptBroadcastCount_; }

    // --- cutscene_was_skipped (Sec45.2) ------------------------------------
    // "reads bit 0x40 of the cutscene flag byte; stays true after the
    // skipped cutscene has ended, clearing only when the next cutscene
    // starts." CHOSEN false initial (no cutscene has been skipped yet in
    // a fresh host - a real fact independent of the real bit's own
    // start-up value, since nothing sets it true without an actual skip
    // action, out of this batch's own scope). cutscene_check_exiting
    // reuses the EXISTING cutsceneState()/screenFade().cutsceneState
    // accessor directly (Sec26.24/26.25) - no new field needed for it.
    bool& cutsceneWasSkippedFlag() { return cutsceneWasSkippedFlag_; }
    CameraLookThrough& cameraLookThrough() { return cameraLookThrough_; }
    OpenValue<bool>& machinimaPathBuilderSucceeds() { return machinimaPathBuilderSucceeds_; }
    MachinimaState& machinima() { return machinima_; }
    CinemaEditorState& cinemaEditor() { return cinemaEditor_; }

    // --- Sec23.9/Sec23.20/Sec23.21/Sec23.22 completion/main-menu small flags -
    uint64_t& completionCameraShakeCount() { return completionCameraShakeCount_; }
    bool& completionCutsceneWasSkipped() { return completionCutsceneWasSkipped_; }

    // dlc_gis_film_grain_unload (Sec17.29): the DLC-availability sentinel
    // (0x013010d8 == -1 means "not loaded") - CHOSEN false ("not loaded,"
    // this project models no real DLC content anywhere else either).
    OpenValue<bool>& dlcFilmGrainAvailable() { return dlcFilmGrainAvailable_; }
    bool& filmGrainFadeState() { return filmGrainFadeState_; }
    uint64_t& filmGrainLocalApplyCount() { return filmGrainLocalApplyCount_; }
    uint64_t& filmGrainReplicateCount() { return filmGrainReplicateCount_; }

protected: // private in EngineState before the split

    // --- zscene (Sec26.25/Sec14.23/Sec8.21), see the accessor doc comment above ---
    OpenValueMap<bool> zsceneLoadable_{"zscene table entry with kind 1 (0x00721be0 lookup, 0x00723d20 test)",
                                       "spec-lua-api-behaviour.md Sec26.25/Sec14.23"};
    bool zsceneTableInstalled_ = false;
    std::vector<ZsceneTableRow> zsceneTable_;
    OpenValue<bool> zsceneSkipAllCutscenes_{"0x0153b556 (skip_all_cutscenes)", "spec-lua-api-behaviour.md Sec26.25/Sec8.21"};
    OpenValue<std::string> zsceneCurrent_{"0x0153b530 (current scene entry)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<std::string> zscenePending_{"0x0153b538 (pending scene entry)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<int> zsceneStateCode_{"0x0153b51c (zscene load state)", "spec-lua-api-behaviour.md Sec26.25/Sec14.23"};
    OpenValue<bool> zsceneAutoSelectNearest_{"0x0153b541 (auto-select the nearest scene when idle)",
                                             "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> zsceneRequeueOnReset_{"0x0153b542 (re-queue the current entry on reset)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValueMap<int> zsceneHandleClass_{"selected request-group handle class (0x00dafb60: 1 not live, 3 resident, 5 failed) of scene entry",
                                         "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> zsceneSoundtrackActive_{"0x0153b71c (soundtrack stream started, not released)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<int64_t> zsceneSoundtrackStartMs_{"0x0153b724 (soundtrack timer start, host ms)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> zsceneSoundtrackEnded_{"soundtrack stream status 0x66 (ended; no audio in this host)",
                                           "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<std::string> zsceneNearestWorldObjectScene_{"nearest scene-bearing world object (list 0x03171a64; no world objects in this host)",
                                                          "spec-lua-api-behaviour.md Sec26.25"};
    int zscenePendingBlockedRefusals_ = 0;
    // The cutsceneCounters_.framesRun value as of the most recent time a NEW
    // (non-empty) key was written into zscenePending_ - 2026-10-02 correction
    // (the `mm_p_01` zscene-promotion-driver investigation): EngineState is
    // shared across an entire mission-drive run, so cutsceneCounters_.
    // lastFrameBlocked can be true only because of some earlier, unrelated
    // mission's own frames (nothing pending there either), long before this
    // name was ever prepped. zsceneIsLoaded's pending-blocked refusal (below)
    // must not fire on that stale signal; it needs at least one frame to have
    // actually run for THIS pending entry first.
    uint64_t zscenePendingSetAtFrame_ = 0;

    // --- cutscene machine (Sec26.25 item 6) ---
    CutsceneManager cutsceneManager_;
    OpenValue<bool> cutscenePreloadMounted_{"0x0153b543 (preload packfiles mounted)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<std::string> cutsceneChainTarget_{"0x0153b52c (next cutscene to chain into)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> cutscenePlayerChecksPass_{"cutscene state 3 player-side checks", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<int64_t> cutsceneLoadStamp_{"0x012f5d30 (120 s resource-load stamp)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> cutscenePlayingByte_{"0x0153b525 (a cutscene is playing)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> cutsceneInProgressByte_{"0x0153b526 (a cutscene is in progress)", "spec-lua-api-behaviour.md Sec26.25"};
    std::vector<std::string> cutsceneMountLog_;
    std::vector<std::string> cutsceneLoadLog_;
    CutsceneCounters cutsceneCounters_;

    // src/lua_cutscene.cpp internals: each "plan" only reads (and may throw
    // OpenStateError); each "apply" only writes.
    struct ZsceneResolved {
        bool found = false;        // a table entry (or, without a table, the per-name map) exists
        bool loadable = false;     // found and kind == 1
        std::string key;           // zsceneTableKey of the entry
        // Added for the cutscene_play_do camera-script wiring task
        // (2026-10-08): known exactly when found && zsceneTableInstalled_ -
        // zsceneResolve() itself already throws (refuse()) for a found
        // entry whose kind is OPEN, so by the time this struct is returned
        // without throwing, a found/table-installed entry's kind is never
        // nullopt. nullopt in every other case (not found; no table
        // installed at all - the OPEN per-name bool map has no kind concept).
        std::optional<int> kind;
        // ZsceneTableRow::cameraScriptName of the found entry; nullopt under
        // the same conditions as kind above, OR when the row itself has no
        // CameraScript text recorded.
        std::optional<std::string> cameraScriptName;
    };
    enum class TeardownKind { Nothing, NotLive, Live };
    struct ResetPlan { // 0x00720320
        bool result = false;
        enum class Write { None, SetAutoSelect, ResetNotLive } write = Write::None;
        std::string current;
        bool requeue = false;
    };
    struct GatePlan {
        bool proceeds = false;     // false: the gate returns (missing / kind != 1 / skip / already current)
        std::string key;
        TeardownKind teardown = TeardownKind::Nothing;
    };
    CameraScriptOpener cameraScriptOpener_;
    CutsceneCameraScriptOutcome cutsceneCameraScriptOutcome_ = CutsceneCameraScriptOutcome::NotAttempted;
    uint64_t cutsceneCameraScriptOpenerCalls_ = 0;
    std::optional<sr3cutscene::CameraScript> cutsceneCameraScript_;

    // --- screen fade (Sec26.24), see the accessor doc comment above ---
    ScreenFade screenFade_;
    lua_State* screenFadeUi_ = nullptr;
    int64_t screenFadeClockMs_ = 0; // host clock, see screenFadeHostFrame
    enum class FadeFallbackReason { Undefined, NoCallback };
    struct FadeFallbackTimer {
        bool armed = false;
        int64_t dueMs = 0;
        FadeFallbackReason reason = FadeFallbackReason::Undefined;
    } screenFadeFallback_;
    ScreenFadeCounters screenFadeCounters_;

    // --- game-pause depth / fade-teardown dialog predicates (Sec37.3; see
    // the accessor doc comments above for the full citation) ---
    GamePauseDepthState gamePauseDepth_;
    BareThreadTable* gamePauseThreads_ = nullptr;
    lua_State* gamePauseGameplayL_ = nullptr;
    FadeTeardownDialogState fadeTeardownDialogState_;
    uint64_t fadeTeardownSecondEffectCount_ = 0;

    // --- fade_out (Sec2.9) / mission_end_silently (Sec15.23) ---
    bool hasScreenFadeColour_ = false;
    ScreenFadeColour screenFadeColour_;
    std::vector<ScreenFadeRequest> screenFadeRequests_;
    int screenFadeOpcode53Count_ = 0;
    float hdrBloomMultiplier_ = 1.0f; // Sec28.3, CONFIRMED initial 1.0
    ScreenCapturePreview screenCapturePreview_;
    std::string cutscenePlayDestination1_;             // Sec57.2, "" = none/dropped
    std::string cutscenePlayDestination2_;
    bool cutscenePlayInProgress_ = false;              // host bookkeeping stand-in, not a direct spec field
    // CHOSEN (Sec57.2a's 2026-10-11 "new finding" note, see
    // cutscenePlayApplyEndPlacement()'s own doc comment above): true once
    // the current cutscene_play start's own destinations have actually been
    // applied to the player(s) - reset to false on every fresh destination
    // write (cutscenePlayDo(), even a refused one - same "written even when
    // refused" quirk the destination fields themselves already follow) so a
    // later cutscenePlayCheckDone() poll or an immediate skip-branch commit
    // each get exactly one placement per destination write, never a repeat.
    bool cutscenePlayPlacementApplied_ = false;
    double localPlayerScreenDistortion1650_ = 0.0;                // Sec30.5
    bool cyberM16GlitchEffectActive_ = false;                     // Sec30.5

    // --- Batch 5 (ranks 251-350, spec-lua-api-behaviour.md Sec15/Sec16),
    // 2026-10-08 - see each public accessor's own doc comment above for
    // citation/reasoning. ---
    bool refractionGlobalOverrideCleared_ = false;       // Sec15.5
    uint64_t refractionClearLocalCount_ = 0;
    uint64_t refractionClearReplicateCount_ = 0;
    std::array<ColorGradingLutSlot, 4> colorGradingLutSlots_{}; // Sec16.4
    int64_t nextLutId_ = 1;
    bool completionCutsceneWasActive_ = false;            // Sec16.5
    std::vector<RefractionSituationRequest> refractionSituationRequests_;
    CameraRestrictLimits cameraRestrictLimits_;
    bool cameraRestrictionActive_ = false; // Sec8.12, batch b30
    double drugEffectOverrideA_ = 0.0;
    double drugEffectOverrideB_ = 0.0;
    CameraShakeState cameraShake_;
    uint64_t cameraEndLookThroughCount_ = 0;
    uint64_t cameraEndLookThroughSmoothCount_ = 0;
    CameraRestrictTarget cameraRestrictTarget_;
    bool dlcGisFilmGrainActive_ = false;
    uint64_t dlcGisFilmGrainLoadLatchSetCount_ = 0;
    uint64_t dlcGisFilmGrainLoadNotifyCount_ = 0;
    bool scriptCameraOwnsView_ = false;
    uint64_t cameraScriptBroadcastCount_ = 0;
    bool cutsceneWasSkippedFlag_ = false;
    CameraLookThrough cameraLookThrough_;
    OpenValue<bool> machinimaPathBuilderSucceeds_{"machinima clip path-builder success (process-token/folder lookup/creation)",
                                                   "spec-lua-api-behaviour.md Sec44.4/Sec56.5"};
    MachinimaState machinima_;
    CinemaEditorState cinemaEditor_;
    uint64_t completionCameraShakeCount_ = 0;
    bool completionCutsceneWasSkipped_ = false;
    OpenValue<bool> dlcFilmGrainAvailable_{"DLC-availability sentinel for dlc_gis_film_grain_unload (0x013010d8 != -1)",
                                           "spec-lua-api-behaviour.md Sec17.29"};
    bool filmGrainFadeState_ = false;
    uint64_t filmGrainLocalApplyCount_ = 0;
    uint64_t filmGrainReplicateCount_ = 0;
};

} // namespace sr3luahost
