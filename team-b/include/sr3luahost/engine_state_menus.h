// EngineState slice: frontend menus: pause menu/map, main menu, dialogs, coop/online, cellphone.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateMenusData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateMenusData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// --- co-op session (spec-lua-api-behaviour.md Sec26.28, Sec3.1, Sec8.27,
// Sec10.2; batch 2026-10-01) -------------------------------------------
//
// The session singleton 0x024d8534 (accessor 0x0087ba20) and the fields
// of the session object the three Lua queries read. CONFIRMED (Sec26.28):
// the singleton is zero at load and no resolved code installs a session
// ("a host starts with no session"), so `present` starts false
// (applySpecInitialState). Every per-session field below is only read
// when a session exists; nothing in this host installs one, so they stay
// OPEN (tests set them). Whether single player installs a one-member host
// session is OPEN (Sec26.28); its HYPOTHESIS is not implemented.
struct CoopSession {
    OpenValue<bool> present{"co-op session singleton 0x024d8534 non-null (0x0087ba20)",
                            "spec-lua-api-behaviour.md Sec26.28/Sec3.1"};
    // +0x5c == +0x58: the local member is the host member (Sec8.27).
    OpenValue<bool> localIsHost{"session host check (+0x5c == +0x58)", "spec-lua-api-behaviour.md Sec8.27/Sec10.2"};
    // Sec3.1 condition 2, client side: the session is not idle (0x0059fbe0
    // false: +0xf4/+0xf8/+0x224) and byte +0xfd is clear.
    OpenValue<bool> clientGate{"session client gate (not idle per 0x0059fbe0, +0xfd clear)",
                               "spec-lua-api-behaviour.md Sec3.1/Sec26.28"};
    // +0x60 via 0x00681370; coop_is_active needs >= 2 (unsigned compare).
    OpenValue<uint32_t> memberCount{"session member count (+0x60)", "spec-lua-api-behaviour.md Sec3.1"};
    // Sec3.1 condition 4: every member but the local one passes 0x00877a90.
    OpenValue<bool> otherMembersPassSlotCheck{"session member walk slot check (0x00877a90)",
                                              "spec-lua-api-behaviour.md Sec3.1"};
};

// --- Batch 2026-10-01, spec-lua-api-behaviour.md Sec27 (25 UI-cluster
// functions, "ranks 551-650" tranche part D) / Sec28 (25 gameplay-cluster
// functions, part C) - see src/lua_spec_confirmed_stubs.cpp's own per-
// function doc comment for each field's full reasoning/citation; only a
// short pointer is kept here. Same conventions as every batch above:
// OpenValue/OpenValueMap for a real engine slot this project cannot know a
// value for (OPEN until a test sets it; applySpecInitialState sets the few
// CONFIRMED initial values); a plain field (default member initializer)
// for a CONFIRMED initial/file value or a value that is write-only within
// this batch's own scope (CHOSEN stand-in, explicitly labelled below).
// -------------------------------------------------------------------

// Sec27.1 `cat_mouse_results_select` / Sec27.25 `game_get_in_progress_type`
// (both read the same 0x014c2d10 singleton).
struct CatMouseMinigame {
    OpenValue<bool> present{"0x014c2d10 (cat-and-mouse minigame singleton) non-null",
                            "spec-lua-api-behaviour.md Sec27.1/Sec27.25"};
    OpenValue<bool> fieldIc1IsOne{"minigame object +0x1c == 1", "spec-lua-api-behaviour.md Sec27.1"};
    int32_t selection = 0; // the object's own +0xe0 field this batch writes; CHOSEN, write-only in scope
    uint64_t sessionMessageCount = 0; // "message to co-op session" (0x0086f1b0) - no real networking layer, counted only
};

// Sec27.3 `Completion_should_wait_for_coop` / Sec27.4 `Completion_user_is_done_viewing`.
struct CompletionScreen {
    OpenValue<bool> present{"'completion_screen' UI context resolved (0x00878ca0)",
                            "spec-lua-api-behaviour.md Sec27.3/Sec27.4"};
    OpenValue<bool> flagClusterSet{"0x02282282 (completion-screen flag cluster, 3rd member) nonzero",
                                   "spec-lua-api-behaviour.md Sec27.3"};
    OpenValue<bool> derefByteAtLeastOne{"resolved context's own double-pointer-deref +4 byte >= 1",
                                        "spec-lua-api-behaviour.md Sec27.3"};
    OpenValue<bool> everyPlayerAtThreshold1{
        "0x00877ca0(context,1): every player's per-slot state >= 1 (per-player walk, abstracted to one flag)",
        "spec-lua-api-behaviour.md Sec27.3"};
    OpenValue<bool> recordEnabled{"context +4 record's own +2 byte nonzero (gates Sec27.4's write)",
                                  "spec-lua-api-behaviour.md Sec27.4"};
    int32_t lastStateWritten = -1; // Sec27.4's own state byte (always 1 when it writes) - CHOSEN, write-only in scope
    uint64_t messageSentCount = 0; // Sec27.4's own "sends a message" side effect - counted only
};

// Sec27.11 `dialog_box_force_close`.
struct DialogForceClose {
    OpenValueMap<bool> slotMatchesId{
        "dialog slot holds this id (self-consistency gate shared with dialog_box_set_result, Sec23.18)",
        "spec-lua-api-behaviour.md Sec27.11"};
    OpenValueMap<bool> timerArmedNotExpired{"dialog slot +0xc timer armed and not expired (0x00d9e4c0/0x00d9e400)",
                                            "spec-lua-api-behaviour.md Sec27.11"};
    OpenValueMap<bool> alreadyClosing{"dialog slot +0x13b already closing", "spec-lua-api-behaviour.md Sec27.11"};
    OpenValueMap<bool> hasResultCallback{"dialog slot +0x13c result callback present",
                                         "spec-lua-api-behaviour.md Sec27.11"};
    OpenValueMap<bool> callbackNameBlank{"dialog slot +0x140 name blank (0x00da73c0)",
                                         "spec-lua-api-behaviour.md Sec27.11"};
    OpenValueMap<bool> fullyRemoved{"dialog slot +0x138 fully removed", "spec-lua-api-behaviour.md Sec27.11"};
    uint64_t closePendingCount = 0;
    uint64_t resultCallbackFiredCount = 0;
    uint64_t namedCallbackDispatchCount = 0;
    uint64_t slotResetCount = 0;
};

// Sec27.13/Sec27.14/Sec27.21/Sec27.22: all bounds-check against the SAME 0x02289b94 slot count.
struct PlayerSlots {
    OpenValue<uint32_t> count{"0x02289b94 (player slot count)",
                              "spec-lua-api-behaviour.md Sec27.13/Sec27.14/Sec27.21/Sec27.22"};
    OpenValueMap<bool> sendInviteOk{"0x0088e690(record,0) == 0", "spec-lua-api-behaviour.md Sec27.13"};
    OpenValueMap<bool> canSendInvite{"0x0088dfd0 result", "spec-lua-api-behaviour.md Sec27.14"};
    OpenValueMap<bool> joinFriendInProgressOk{"0x0088e040 result", "spec-lua-api-behaviour.md Sec27.22"};
    uint64_t gamercardShownCount = 0; // Sec27.21: 0x00870810 internals OPEN, counted only
};

// Sec27.20 `game_sign_into_network`: this project has no real platform SDK - only the call's own raw
// arguments are recorded, honestly, as "a request" (same convention as MinimapIconRecord/ObjectIndicatorRecord
// above), since the real async-callback registration is a confirmed no-op stub on this platform anyway.
struct SignInRequest {
    bool wantSignIn = false;
    std::string callbackName;
};

// waiting_for_player_dialog (Sec33.5): CONFIRMED reference-counted
// show/hide, including the real, confirmed hazard this project
// reproduces deliberately: neither this function's own show nor hide
// path ever clears "currently shown" (only an untraced third writer,
// out of scope, does) - so once a real display happens, currentlyShown
// stays true for the rest of the run, same as the real engine. The
// "suppressing game-state check" gating whether a show actually
// displays is NOT modeled (this project has no such check anywhere) -
// every show that isn't already "shown" is treated as actually
// displaying, a stated simplification.
struct WaitingForPlayerDialog {
    int refCount = 0;
    bool pending = false;
    bool currentlyShown = false;
    int showBroadcastCount = 0;
    int hideBroadcastCount = 0;
};

// pause_menu_update_option (Sec16.12) / pause_menu_restore_defaults
// (Sec16.13): the real dispatcher maps ~25 individual (category,
// option) pairs to individual engine setters (audio-bus volumes,
// mouse-sensitivity curves, gamma, ...) - Sec16.12's own text gives
// only 4 representative examples, not the full enumeration ("full case
// enumeration not reproduced here, matching house style"), and none of
// those ~25 real target subsystems exist anywhere else in this
// project's scope. Inventing the other ~20 mappings would be
// fabricating data this spec section does not supply - this project
// instead records the raw (category, option, flag, value) request,
// the SAME "record the raw request, not the unreadable real internal
// effect" convention this header already uses throughout (e.g.
// MinimapIconRecord). restore_defaults is the sibling "reset" request,
// keyed only by category (no option/value).
struct PauseMenuOptionRequest {
    int32_t optionId = 0;
    bool flag = false;
    double value = 0.0;
    uint64_t count = 0;
};

// pause_menu_accept_options (Sec11.11, 0x007e0430): CONFIRMED a 3-way
// AND gate (one thunked predicate, OPEN target, ANDed with two flag
// globals) selects whether pending options changes commit. Every link
// of that AND is individually OPEN with nothing this project can
// default honestly - collapsed to ONE OpenValue<bool> representing the
// gate's own combined outcome (same "collapse a multi-condition AND
// chain whose sub-conditions are all individually OPEN into one gate
// field" convention as VcustCamera::targetValid above), refusing until
// a test sets it rather than guessing any individual link.
struct PauseMenuOptions {
    OpenValue<bool> acceptGatePasses{"pause-menu options accept 3-way gate (0x007060e0)",
                                     "spec-lua-api-behaviour.md Sec11.11"};
    bool committed = false; // CONFIRMED effect once the gate passes
    uint64_t acceptCalls = 0;
};

// =======================================================================
// Wave 2 batch W2-6 (2026-10-08), spec-lua-api-behaviour.md UI-frontend
// hardening batch - 143 names split across lua_spec_confirmed_stubs_b17_
// wave2_ui_frontend_a.cpp (66 names: Sec19/Sec23/Sec26 + lobby/coop/
// session/steam `game_*`) and _b18_wave2_ui_frontend_b.cpp (77 names:
// menus/pause/dialog/cellphone/cell/save/machinima/completion). State
// grouped by theme below; see each stub's own doc comment in those two
// files for the per-name CONFIRMED/OPEN/HYPOTHESIS/CHOSEN citation.
// Hardening counters (`...GuardCount`) are test-observable only, not
// real engine fields - they count how often this host's own deliberate
// "fail closed instead of reproducing the real engine's crash" branch
// fired, per the orchestrator's standing hardening policy.
// =======================================================================

// --- Sec19.7/Sec23.14 game_interface_effect_begin/_end -----------------
struct InterfaceEffectState {
    std::string activeName;
    double param1 = 0.0;
    double param2 = 0.0;
    bool forced = false;
    uint64_t beginCount = 0;
    uint64_t endCount = 0;
};

// --- Sec19.9/Sec44.3/Sec26.21 game_coop_get_starting_coop/_syslink,
// game_is_syslink: all three CONFIRMED to read the SAME two bytes
// game_coop_start_new_live/_syslink already write (coopLiveSessionActive/
// coopSyslinkSessionActive, above) - no new state needed.

// --- Sec26.22 cellphone_choose_vehicle ----------------------------------
struct CellphoneVehicleDelivery {
    int32_t lastSelectedIndex = -1;
    uint64_t selectCount = 0;
    uint64_t announceCount = 0;
};

// --- Sec26.4/Sec26.5 game_lobby_get_remote_player_name /
// Completion_get_other_player_name: CONFIRMED same +0xc2 name field,
// reached via two different context objects this project models as one.
struct RemotePlayerInfo {
    bool present = false;
    std::string name;
};

// --- Sec26.20 game_vkeyboard_input ---------------------------------------
struct VirtualKeyboardState {
    bool open = false;
    uint64_t requestCount = 0;
    std::string lastTitle;
};

// --- Sec23.11/Sec41.2 game_stop_find_syslink_servers/_start_... --------
struct SyslinkSearch {
    bool active = false;
    uint64_t startCount = 0;
    uint64_t stopCount = 0;
    uint64_t guardedNoopCount = 0;
};

// --- Sec41.2/Sec56.3 Steam overlay/url openers (no validation, per spec) -
struct SteamOverlayCounters {
    uint64_t friendsOverlayCount = 0;
    std::string lastOverlayArg;
    uint64_t urlOverlayCount = 0;
    std::string lastUrl;
    uint64_t dlcOverlayCount = 0;
};

// --- Sec41.2 main_menu_new_game/_horde_start/_set_coop_menu_type -------
struct MainMenuNewGame {
    std::string worldName = "sr3_city"; // CONFIRMED hardcoded literal
    bool hordeMode = false;
    uint64_t newGameCount = 0;
    uint64_t hordeStartCount = 0;
};

// --- Sec44.2/Sec56.6 coop kick/gamercard + the Sec44.2 project-wide
// wide-sprintf-wrapper latent-overflow hardening note (this project
// formats via std::string/snprintf, never a fixed 256-wide-char stack
// buffer, so the real overflow cannot reproduce here by construction;
// the counter below is test-observable evidence the bounded path ran).
struct CoopKickState {
    uint64_t confirmDialogShownCount = 0;
    uint64_t kickedCount = 0;
    uint64_t callbackFiredCount = 0;
    bool lastCallbackArg = false;
    uint64_t gamercardOverlayCount = 0;
    uint64_t wideSprintfBoundGuardCount = 0;
};

// --- Sec44.2 game_is_joinable: CONFIRMED real logic quirk (not a crash) -
// free-slot count tested "!= 0" rather than "> 0"; reproduced exactly,
// not fixed, since it is not memory-unsafe.
struct JoinableState {
    OpenValue<int32_t> capacity{"session capacity (joinable slot test)", "spec-lua-api-behaviour.md Sec44.2"};
    int32_t currentPlayerCount = 0;
    int32_t reservedSlots = 2; // CONFIRMED "two reserved-slot checks"
};

// --- Sec44.3 lobby map/char selection, horde data, ready state ---------
struct HordeLobbyState {
    int32_t selectedMapIndex = -1;
    int32_t committedMapIndex = -1;
    bool continueFromSavedWave = false;
    std::unordered_map<int32_t, int32_t> savedProgressByMap; // OPEN data; test-populated
    uint64_t spuriousRestartWarningCount = 0;
    int32_t selectedCharIndex = -1;
    int32_t stagedCharIndexHost = -1;
    uint64_t readyStateCountdownStartedCount = 0;
    uint64_t readyStateCancelCount = 0;
    uint64_t finishedLoadingCount = 0;
};

// --- Sec56.3 game_pause_map_filter --------------------------------------
struct PauseMapFilterState {
    std::string currentFilterId = "PFT_FILTER_ALL"; // CHOSEN default name (one of 8 public ids)
    bool locked = false; // CONFIRMED: a mission-imposed lock (real id >= 0x80) the setter ignores
    uint64_t setCount = 0;
    uint64_t getCount = 0;
};

// --- Sec54.4/Sec56.4 game_proto_select: CRASH-shaped in the real engine
// (an invalid argument corrupts the caller's own saved-register stack
// spill). This host never touches raw registers, so the hardened
// divergence is simply to refuse the invalid-argument case instead of
// selecting anything, counted, while reproducing "lowest of 0/1/2 not
// named by either argument" for valid 1-3 arguments.
struct ProtoSelectState {
    int32_t selected = -1;
    uint64_t invalidArgGuardCount = 0;
};

// --- Sec55.1 game_is_going_to_main_menu: a simplified game-state stack -
struct GameStateStack {
    int32_t currentTop = 2;      // CHOSEN default (OPEN what 2/5/6 really name)
    int32_t projectedTop = -1;   // -1 = no projection queued
    std::vector<int32_t> queuedOps;
    uint64_t overflowGuardCount = 0; // hardening: the real 16-dword buffer is unbounded; this host bounds it
};

// --- Sec55.1 game_get_game_play_mode/_pending_game_play_mode -----------
struct GamePlayMode {
    std::string current = "Normal"; // CONFIRMED file-default
    std::string pending;            // empty => "None"
};

// --- Sec57.1 dialog_box_create_internal / dialog_box_set_result /
// dialog_open_pause_display / dialog_close_finished /
// dialog_option_accept_kbd_input/_end_kbd_input / dialog_pause_unpause -
// dialog_box_create_internal is one of this batch's two OWN named
// memory-safety findings (callback-name bound to 63 chars; nil title/
// body/callback handled) - see the stub's own doc comment for the fix.
struct DialogUiState {
    bool pauseDisplayOpen = false;
    uint64_t pauseDisplayOpenCount = 0;
    int32_t currentOptionIndex = 0;
    bool closeLatchSet = false;
    uint64_t closeFinishedCount = 0;
    bool kbdEditing = false;
    int32_t kbdCursorPos = 0;
    std::string kbdCommittedText;
    uint64_t kbdAcceptCount = 0;
    uint64_t kbdEndCount = 0;
    std::array<int32_t, 4> slotShowingId{{-1, -1, -1, -1}}; // test-only fixture
    OpenValue<bool> optionApplyResult{"dialog_box_set_result option-apply outcome (0x007c1c80)",
                                       "spec-lua-api-behaviour.md Sec23.18"};
    uint64_t setResultSelfConsistencyFailCount = 0;
    uint64_t pauseUnpauseCount = 0;
    uint64_t createCount = 0;
    uint64_t createNilArgGuardCount = 0;
    uint64_t createCallbackNameTruncatedCount = 0;
    std::string lastCreateCallbackName;
    std::string lastCreateTitle;
};

// --- Sec59.3/59.1 cell_* cheat menu/UI queries/music toggle -------------
struct CellUiState {
    bool allowCamera = true; // CHOSEN default (AGG - collectively described only)
    int32_t pendingCheatIndex = -1;
    bool cheatNoConfirmNeeded = false;
    OpenValue<uint32_t> unlockedCheatCount{"cell cheat-menu unlocked-cheat count", "spec-lua-api-behaviour.md Sec59.3"};
    uint64_t activateNullRecordGuardCount = 0;
    uint64_t unlockCount = 0;
    bool phoneUiDocType1Open = false;
    bool phoneUiDocType2Open = false;
    bool mapDisabledExtraGate = false;
    std::unordered_map<int32_t, bool> stationDisabled; // 1-based station -> hidden/disabled
    int32_t listeningStation = -1;
    uint64_t toggleStationNoopCount = 0;
};

// --- Sec51.2 sb_saintsbook_is_available/_select_chop_shop_vehicle/
// _select_hitman_target -------------------------------------------------
struct SaintsbookState {
    int32_t activeHitmanTarget = -1;
    int32_t activeChopShopVehicle = -1;
    uint64_t hitmanReselectNoopCount = 0;
    uint64_t chopShopDeselectCount = 0;
    OpenValue<bool> eligibilityTerm1{"sb_saintsbook_is_available eligibility term 1", "spec-lua-api-behaviour.md Sec51.2"};
    OpenValue<bool> eligibilityTerm2{"sb_saintsbook_is_available eligibility term 2", "spec-lua-api-behaviour.md Sec51.2"};
};

// --- Sec50.1 main_menu_restart_video/_video_has_stopped: CONFIRMED a
// stale-flag-then-null-call crash (the "playing" flag is set even when
// video creation failed) - hardened to only set it on success.
struct MainMenuVideo {
    bool playingFlag = false;
    uint64_t restartCount = 0;
    uint64_t nullVideoGuardCount = 0;
    uint64_t stopHookCount = 0;
    uint64_t startHookCount = 0;
};

// --- Sec50.4 options_display_pc_set_options -----------------------------
struct PcOptionsState {
    int32_t resolutionWidth = 0, resolutionHeight = 0;
    bool vsync = false, fullscreen = false;
    int32_t msaa = 0, anisotropic = 0, lightingDetail = 0, shadowDetail = 0, sceneDetail = 0;
    int32_t reflections = -1, postProcessing = -1, ambientOcclusion = -1; // CONFIRMED: out-of-range -> -1, no range check
    uint64_t applyCount = 0;
};

// --- Sec54.1 main_menu_matchmaking/_continue: CONFIRMED null-dialog-pool
// crash (reuses dialogPoolSlotsInUse() above), hardened.
struct MainMenuMatchmaking {
    uint64_t startCount = 0;
    uint64_t nullDialogGuardCount = 0;
};

// --- Sec51.5 pause_menu_horde_mode_new/_retry/southpaw queries ---------
struct PauseMenuHorde {
    uint64_t newCount = 0;
    uint64_t retryCount = 0;
};

// --- Sec42.1 pause_map_zoom/_add_bookmark/_drag_map/
// pause_menu_change_control_scheme --------------------------------------
struct PauseMapZoomState {
    double zoomFactor = 1.0;
    double zoomMin = 0.1;
    double zoomMax = 4.0;
    double panX = 0.0, panY = 0.0;
    uint64_t zoomCount = 0;
};
struct PauseMapBookmarks {
    int32_t selectedBookmark = -1; // -1 = none selected
    std::vector<double> bookmarkPositions; // up to 5
    uint64_t addCount = 0;
    uint64_t removeCount = 0;
    OpenValue<bool> markerCreateSucceeds{"pause_map_add_bookmark marker-object creation", "spec-lua-api-behaviour.md Sec42.1"};
    uint64_t createFailureGuardCount = 0;
};
struct PauseMenuControlSchemeSets {
    OpenValue<int32_t> countA{"pause-menu control-scheme set A count", "spec-lua-api-behaviour.md Sec42.1"};
    OpenValue<int32_t> countB{"pause-menu control-scheme set B count", "spec-lua-api-behaviour.md Sec42.1"};
    int32_t appliedIndex = -1;
    uint64_t emptySetGuardCount = 0;
};

// --- Sec45.4 cellphone_dial ----------------------------------------------
struct CellphoneDialState {
    OpenValue<uint32_t> phoneBookCount{"cellphone phone-book entry count", "spec-lua-api-behaviour.md Sec45.4"};
    uint64_t outOfRangeGuardCount = 0;
    uint64_t dialCount = 0;
};

// --- Sec45.4/Sec56.1 autil_hud_mayhem_init/autil_mashing_minigame_
// faded_out: the former has a documented shape (one-time native-callback
// creation + per-call element lookup, null-read on an exhausted pool on
// the first call); the latter has NO spec text anywhere in this project
// (checked spec-lua-api-behaviour.md's full Sec59/Sec45 text and
// spec-lua-bindings.md - genuinely absent), so it is refused OPEN rather
// than guessed from its name.
struct AutilHudMayhemInit {
    bool oneTimeSetupDone = false;
    OpenValue<bool> nativeCallbackPoolAvailable{"autil_hud_mayhem_init native-callback pool availability",
                                                 "spec-lua-api-behaviour.md Sec45.6"};
    uint64_t poolExhaustedGuardCount = 0;
    uint64_t subscribeCount = 0;
};

struct EngineStateMenusData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using CoopSession = sr3luahost::CoopSession;
    using CatMouseMinigame = sr3luahost::CatMouseMinigame;
    using CompletionScreen = sr3luahost::CompletionScreen;
    using DialogForceClose = sr3luahost::DialogForceClose;
    using PlayerSlots = sr3luahost::PlayerSlots;
    using SignInRequest = sr3luahost::SignInRequest;
    using WaitingForPlayerDialog = sr3luahost::WaitingForPlayerDialog;
    using PauseMenuOptionRequest = sr3luahost::PauseMenuOptionRequest;
    using PauseMenuOptions = sr3luahost::PauseMenuOptions;
    using InterfaceEffectState = sr3luahost::InterfaceEffectState;
    using CellphoneVehicleDelivery = sr3luahost::CellphoneVehicleDelivery;
    using RemotePlayerInfo = sr3luahost::RemotePlayerInfo;
    using VirtualKeyboardState = sr3luahost::VirtualKeyboardState;
    using SyslinkSearch = sr3luahost::SyslinkSearch;
    using SteamOverlayCounters = sr3luahost::SteamOverlayCounters;
    using MainMenuNewGame = sr3luahost::MainMenuNewGame;
    using CoopKickState = sr3luahost::CoopKickState;
    using JoinableState = sr3luahost::JoinableState;
    using HordeLobbyState = sr3luahost::HordeLobbyState;
    using PauseMapFilterState = sr3luahost::PauseMapFilterState;
    using ProtoSelectState = sr3luahost::ProtoSelectState;
    using GameStateStack = sr3luahost::GameStateStack;
    using GamePlayMode = sr3luahost::GamePlayMode;
    using DialogUiState = sr3luahost::DialogUiState;
    using CellUiState = sr3luahost::CellUiState;
    using SaintsbookState = sr3luahost::SaintsbookState;
    using MainMenuVideo = sr3luahost::MainMenuVideo;
    using PcOptionsState = sr3luahost::PcOptionsState;
    using MainMenuMatchmaking = sr3luahost::MainMenuMatchmaking;
    using PauseMenuHorde = sr3luahost::PauseMenuHorde;
    using PauseMapZoomState = sr3luahost::PauseMapZoomState;
    using PauseMapBookmarks = sr3luahost::PauseMapBookmarks;
    using PauseMenuControlSchemeSets = sr3luahost::PauseMenuControlSchemeSets;
    using CellphoneDialState = sr3luahost::CellphoneDialState;
    using AutilHudMayhemInit = sr3luahost::AutilHudMayhemInit;

public:
    CoopSession& coopSession() { return coopSession_; }

    // --- game_get_coop_join_type (Sec10.9) ----------------------------------

    // Real body: reads a single global (`0x012f44fc`) holding a code `0`,
    // `1`, or `2` and pushes it back as a Lua number (CONFIRMED,
    // disassembly, full body read). The global's sole writer is real,
    // host-authoritative, and network-replicated (CONFIRMED, Sec10.9) -
    // this project builds no real networking layer (explicit project
    // convention, same as coopSession_/replicateStateChange above), so
    // `coopJoinType_` is this project's own minimal `int` stand-in,
    // default 0. Pure query among this task's 9 in-scope names (the real
    // writer is a different, out-of-scope function) - test-only setter.
    OpenValue<int>& coopJoinType() { return coopJoinType_; }
    CatMouseMinigame& catMouseMinigame() { return catMouseMinigame_; }
    CompletionScreen& completionScreen() { return completionScreen_; }

    // Sec27.6 `pause_menu_has_seen_display_cal_screen`: 0x02297c80, CONFIRMED zero at start; no in-scope setter.
    bool& pauseMenuSeenDisplayCalScreen() { return pauseMenuSeenDisplayCalScreen_; }
    // Sec27.7 `msn_text_adventure_set_screen`: 0x01301124, CONFIRMED -1 initial.
    int32_t& textAdventureScreenIndex() { return textAdventureScreenIndex_; }
    // Sec27.8 `horde_results_set_end_action`: 0x012f3b48, CONFIRMED initial 2.
    int32_t& hordeResultsEndAction() { return hordeResultsEndAction_; }

    // Sec27.10 `game_lobby_coop_finished`: 0x0224244c, CONFIRMED zero-filled; reuses the existing
    // screenFadeRequest()/Sec26.24 mechanism for its own fade-out(-1, null, 1) call.
    bool& coopLobbyFinished() { return coopLobbyFinished_; }
    DialogForceClose& dialogForceClose() { return dialogForceClose_; }
    PlayerSlots& playerSlots() { return playerSlots_; }

    // Sec27.15 `game_set_coop_friendly_fire` / Sec27.16 `game_get_coop_friendly_fire`:
    // 0x012f4500, CONFIRMED file value 1. The setter's own host/solo write gate reuses coopSession() above.
    OpenValue<int32_t>& coopFriendlyFireRaw() { return coopFriendlyFireRaw_; }

    // Sec27.19 `game_is_signed_in`: 0x0086fdd0 -> 0x008703b0, Steam user interface pointer (0x0101c54c) non-null.
    OpenValue<bool>& steamInterfaceAvailable() { return steamInterfaceAvailable_; }
    std::vector<SignInRequest>& signInRequests() { return signInRequests_; }
    uint64_t& signInErrorDialogCount() { return signInErrorDialogCount_; } // CONFIRMED unconditional every call

    // Sec27.23 `game_coop_start_new_live` / Sec27.24 `game_coop_start_new_syslink`:
    // 0x014ff6c1/0x014ff6c2, CONFIRMED zero-filled, mutually exclusive.
    bool& coopLiveSessionActive() { return coopLiveSessionActive_; }
    bool& coopSyslinkSessionActive() { return coopSyslinkSessionActive_; }

    // Sec28.23 `cellphone_animate_start_do`: reuses the existing hasLocalPlayer() above for "player 1 exists".
    OpenValue<bool>& cellphoneAnimSuppressed() { return cellphoneAnimSuppressed_; } // 0x00943a20(player) result
    uint64_t& cellphoneAnimPlayedCount() { return cellphoneAnimPlayedCount_; }
    WaitingForPlayerDialog& waitingForPlayerDialog() { return waitingForPlayerDialog_; }

    // 0x0229a318 - the pause-map tutorial-mode flag `pause_map_is_tutorial_mode`
    // (Sec31.1 table item 5) reads and `pause_map_tutorial_mode` (Sec32.1)
    // writes. A SEPARATE global from the stag-mode flag above (Sec31.5's own
    // text: the pause-map close handler "clears the tutorial-mode flag
    // 0x0229a318... alongside the stag-mode flag" - two distinct bytes
    // cleared together by one handler, not one byte read two ways).
    OpenValue<bool>& pauseMapTutorialMode() { return pauseMapTutorialMode_; }

    // 0x0229a2ac - the pause-map's own "currently selected zone" global
    // (Sec31.2/Sec31.3). Default 0 ("no zone selected") - not independently
    // confirmed via an explicit static-initializer/exhaustive-write-census
    // check this pass (unlike 0x0153b530/0x0153b556's own dedicated such
    // checks elsewhere in this file), but HIGH CONFIDENCE: Sec31.2's own
    // CONFIRMED function body treats a 0 read as the normal, meaningful
    // "nothing selected" case, not as missing information, and Sec31.3's own
    // exhaustive 7-use census finds only two writers - pause_map_set_gps
    // (sets a real zone) and a teardown helper (explicitly resets to 0) - so
    // 0 is a real, reachable, well-understood state of this global, not a
    // guess at unknown content.
    uint32_t pauseMapSelectedZone() const { return pauseMapSelectedZone_; }
    void setPauseMapSelectedZoneForTesting(uint32_t zoneHandle) { pauseMapSelectedZone_ = zoneHandle; }

    uint64_t pauseMapStagCompletionHookFiredCount() const { return pauseMapStagCompletionHookFiredCount_; }
    uint64_t pauseMapTakeoverClaimedCount() const { return pauseMapTakeoverClaimedCount_; }
    uint64_t pauseMapTakeoverAutosaveRequestCount() const { return pauseMapTakeoverAutosaveRequestCount_; }

    // cell_camera_enable / cell_camera_is_enabled (Sec45.4): CONFIRMED "a
    // global that has exactly one other reference anywhere in the binary -
    // its own getter ... confirmed Lua-side-only state with no engine
    // consumer." A single bare global, not per-name.
    OpenValue<bool>& cellCameraEnabled() { return cellCameraEnabled_; }

    // game_is_active_input_gamepad (Sec2.6): 0x0141250d (control context
    // 0x01411b18+0x9f5), CONFIRMED zero-filled .data (initial value 0/
    // "false", "not gamepad" - a real stated construction value, unlike
    // most OpenValue fields in this header). The real per-frame writer
    // chain (0x00707440->0x005be880->0x005bcde0->0x005bc9d0) is not run by
    // this host (no per-frame update loop drives player-runtime input yet,
    // per inputSystem()'s own doc comment above) - a plain bool stand-in,
    // test-settable, starting at its real CONFIRMED construction value
    // rather than an OpenValue (there is nothing to be OPEN about: the
    // start-up value itself is CONFIRMED, only the per-frame updates are
    // unmodeled).
    bool& activeInputIsGamepad() { return activeInputIsGamepad_; }

    // pop_screen (Sec2.8): no in-scope push in this batch (push_screen,
    // Sec8.26, is out of scope) to populate a real starting depth - this
    // project's own starting depth is CHOSEN 0 (a fresh/empty screen
    // stack), not a spec-stated value. A test can increment this directly
    // to exercise a real pop.
    int32_t& uiScreenStackDepth() { return uiScreenStackDepth_; }
    uint64_t& popScreenCount() { return popScreenCount_; }

    // game_get_language (Sec16.1): 12 CONFIRMED real table entries (floor,
    // not necessarily the full list - Sec16.1's own "table not walked past
    // entry 12" caveat). No in-scope setter exists (no "set_language" in
    // this batch) - a test-only hook populates the index; CHOSEN default
    // -1 ("no language selected" -> the CONFIRMED "" fallback). An index
    // past the 12 CONFIRMED entries is genuinely unknown (not fabricated -
    // this IS the function's whole observable return value, the one case
    // in this batch where "no in-scope reader" does not apply, since the
    // calling Lua script itself is the reader) - refused as OPEN.
    void setLanguageIndexForTesting(int32_t idx) { currentLanguageIndex_ = idx; }

    // dialog_box_disconnect (Sec16.9): every real internal consequence
    // (session/pause-UI teardown) is out of this project's modeled scope
    // (no session/pause-UI state anywhere in this project) - a pure call
    // counter is the only test-observable fact.
    uint64_t dialogBoxDisconnectCount() const { return dialogBoxDisconnectCount_; }
    void recordDialogBoxDisconnect() { ++dialogBoxDisconnectCount_; }

    // main_menu_supress_profile_change (Sec16.10): global 0x022425f6,
    // direct unconditional write, no gate.
    bool& mainMenuProfileChangeSuppressed() { return mainMenuProfileChangeSuppressed_; }

    // pause_menu_control_scheme_init (Sec16.11): global 0x0229a558 (the
    // scheme id); the 2 stored-preset globals (0x01411b08/0x01411b10) have
    // no in-scope WRITER (this batch names no "pause_menu_save_control_
    // scheme" - a test sets them directly). Return shape (0/2/3 pushes) is
    // reproduced in the stub itself, not here.
    int32_t& pauseMenuControlScheme() { return pauseMenuControlScheme_; }
    int32_t& pauseMenuPresetScheme0() { return pauseMenuPresetScheme0_; }
    int32_t& pauseMenuPresetScheme1Or2() { return pauseMenuPresetScheme1Or2_; }
    void recordPauseMenuRestoreDefaults(int32_t category) { ++pauseMenuRestoreDefaultsCounts_[category]; }

    // horde_mode_is_active (Sec11.6, 0x00842ad0): CONFIRMED a single global
    // byte, pushed directly - writer not traced (Sec11.6's own text), OPEN
    // until a test sets it.
    OpenValue<bool>& hordeModeActive() { return hordeModeActive_; }
    PauseMenuOptions& pauseMenuOptions() { return pauseMenuOptions_; }

    // interior_disable_exterior (Sec20.19): the SAME global replication-mode
    // flag action_stop_synced_state (Sec18.4, out of this batch's scope)
    // already establishes (0x024d4461) - OPEN until a test sets it (no
    // in-scope writer here either).
    OpenValue<bool> replicationModeFlag0x024d4461{"global replication-mode flag (0x024d4461)",
                                                  "spec-lua-api-behaviour.md Sec18.4/Sec20.19"};
    InterfaceEffectState& interfaceEffect() { return interfaceEffect_; }

    // --- Sec19.6 online_validator_set_checking ------------------------------
    bool& onlineValidatorChecking() { return onlineValidatorChecking_; }
    CellphoneVehicleDelivery& cellphoneVehicleDelivery() { return cellphoneVehicleDelivery_; }
    RemotePlayerInfo& remotePlayer() { return remotePlayer_; }

    // --- Sec26.6 completion_screen_finished ---------------------------------
    bool& completionScreenFinishedFlag() { return completionScreenFinishedFlag_; }

    // --- Sec26.18 game_use_radial_menu_item: slot 12 and any out-of-range
    // slot are CONFIRMED to always report 1.0; slots 0-7 dispatch through an
    // OPEN internal (0x00600720's own success/failure) - OpenValue until a
    // test sets it.
    OpenValue<bool>& radialMenuQuickItemSucceeds() { return radialMenuQuickItemSucceeds_; }
    uint64_t& radialMenuSlot12Count() { return radialMenuSlot12Count_; }
    VirtualKeyboardState& virtualKeyboard() { return virtualKeyboard_; }
    OpenValue<bool>& virtualKeyboardCloseResult() { return virtualKeyboardCloseResult_; }

    // --- Sec23.13/Sec44.2 game_is_connected_to_service/_internet: CONFIRMED
    // the SAME handler ("logged on to Steam"), distinct from the existing,
    // weaker game_is_signed_in ("interface exists").
    OpenValue<bool>& steamLoggedOn() { return steamLoggedOn_; }
    SyslinkSearch& syslinkSearch() { return syslinkSearch_; }
    SteamOverlayCounters& steamOverlay() { return steamOverlay_; }

    // --- Sec41.6 get_get_key_names_for_axis_action: CONFIRMED the real
    // engine returns uninitialized stack memory for an unknown axis-action
    // name (this host has no 34-entry table to match against, so every call
    // takes that path) - CHOSEN (hardened divergence): two empty strings
    // instead of leftover stack content, counted.
    uint64_t& axisActionUnknownNameGuardCount() { return axisActionUnknownNameGuardCount_; }
    MainMenuNewGame& mainMenuNewGame() { return mainMenuNewGame_; }
    int32_t& coopMenuType() { return coopMenuType_; } // raw byte store; 2 further UI dispatch codes OPEN
    CoopKickState& coopKick() { return coopKick_; }
    JoinableState& joinable() { return joinable_; }

    // --- Sec44.2 game_join_syslink_game: CONFIRMED 8-bit index compare -
    // index 257 silently matches the same record as index 1.
    uint64_t& syslinkJoinCount() { return syslinkJoinCount_; }
    int32_t& syslinkJoinLastIndex() { return syslinkJoinLastIndex_; }
    HordeLobbyState& hordeLobby() { return hordeLobby_; }
    PauseMapFilterState& pauseMapFilter() { return pauseMapFilter_; }
    ProtoSelectState& protoSelect() { return protoSelect_; }

    // --- Sec56.2 game_record_mode_can_encode/_enable/_show_hud/
    // game_record_set_quality_level: CONFIRMED the family of 4 is 3 no-op +
    // 1 constant-false, but NOT which of the 4 names is which - an
    // irreducible ambiguity this project refuses rather than guesses (see
    // each stub's own OPEN refusal). Counter is test-observable only.
    uint64_t& gameRecordAmbiguousRefusalCount() { return gameRecordAmbiguousRefusalCount_; }

    // --- Sec56.3 game_request_game_terminate: CONFIRMED once-only exit latch -
    bool& gameTerminateRequested() { return gameTerminateRequested_; }

    // --- Sec56.3 game_use_imperial_units: CONFIRMED to follow the real
    // Windows user locale (GetUserDefaultLCID/GetLocaleInfoW) with no in-
    // game option. Implemented via the real Win32 API on _WIN32 builds
    // (same real-header convention common.h already uses for
    // GetKeyNameTextW - a standard, publicly-documented OS function, not
    // reverse-engineered content); a test-only override takes precedence
    // when set, so tests do not depend on the CI machine's own locale.
    OpenValue<bool>& imperialUnitsOverrideForTesting() { return imperialUnitsOverrideForTesting_; }
    GameStateStack& gameStateStack() { return gameStateStack_; }
    GamePlayMode& gamePlayMode() { return gamePlayMode_; }

    // --- Sec55.1 game_get_coop_ping -----------------------------------------
    OpenValue<uint32_t>& coopPing() { return coopPing_; }
    // Shared 4-slot dialog pool (Sec27.11 cross-reference) - also consumed by
    // main_menu_matchmaking and cell_cheats_activate_cheat below.
    int32_t& dialogPoolSlotsInUse() { return dialogPoolSlotsInUse_; }
    DialogUiState& dialogUi() { return dialogUi_; }
    CellUiState& cellUi() { return cellUi_; }
    SaintsbookState& saintsbook() { return saintsbook_; }
    // CONFIRMED: the real 3rd eligibility term reads a global nothing in the
    // binary ever writes - permanently dead. Not an OpenValue (there is
    // nothing to ever set): always false.
    bool saintsbookThirdTermDeadGlobal() const { return false; }
    MainMenuVideo& mainMenuVideo() { return mainMenuVideo_; }
    OpenValue<bool>& mainMenuVideoCreateSucceeds() { return mainMenuVideoCreateSucceeds_; }
    PcOptionsState& pcOptions() { return pcOptions_; }

    // --- Sec50.1 pause_menu_cant_retry_because_host_in_creation/
    // _cat_mouse_active/_common_top_finished_loading -------------------------
    bool& pauseMenuHostInCharacterCreation() { return pauseMenuHostInCharacterCreation_; }
    bool& catMouseMinigameActive() { return catMouseMinigameActive_; }
    uint64_t& pauseMenuCommonTopFinishedLoadingCount() { return pauseMenuCommonTopFinishedLoadingCount_; }
    MainMenuMatchmaking& mainMenuMatchmaking() { return mainMenuMatchmaking_; }
    uint64_t& mainMenuContinueCount() { return mainMenuContinueCount_; }
    OpenValueMap<std::string>& localizedStringForTag() { return localizedStringForTag_; }
    PauseMenuHorde& pauseMenuHorde() { return pauseMenuHorde_; }
    bool& southpawControlScheme() { return southpawControlScheme_; }
    bool& vehicleSouthpawControlScheme() { return vehicleSouthpawControlScheme_; }
    PauseMapZoomState& pauseMapZoomState() { return pauseMapZoomState_; }
    PauseMapBookmarks& pauseMapBookmarks() { return pauseMapBookmarks_; }
    uint64_t& pauseMapDragMapCount() { return pauseMapDragMapCount_; }
    PauseMenuControlSchemeSets& pauseMenuControlSchemeSets() { return pauseMenuControlSchemeSets_; }

    // --- Sec57.3 crib-adjacent / Sec57.4 world/UI front-end -----------------
    bool& cityLoadImgComplete() { return cityLoadImgComplete_; }
    uint64_t& communityLoginOpenCount() { return communityLoginOpenCount_; }
    int32_t& completionCallbackCaseSelected() { return completionCallbackCaseSelected_; }

    // --- Sec26.10 pause_menu_quit_game_internal -----------------------------
    bool& pauseMenuQuitRequested() { return pauseMenuQuitRequested_; }
    bool& completionCreditsWereActive() { return completionCreditsWereActive_; }
    uint64_t& mainMenuRedeemCodeOpenCount() { return mainMenuRedeemCodeOpenCount_; }
    CellphoneDialState& cellphoneDial() { return cellphoneDial_; }
    AutilHudMayhemInit& autilHudMayhemInit() { return autilHudMayhemInit_; }

    // character_is_player (Sec18.19): reuses objectResolves() directly -
    // the GENERIC resolver only (0x00a280c0, not the fuller #PLAYER#-then-
    // #FOLLOWER# chain), but this project's flat objectResolves() already
    // collapses every resolver variant into one shared map (Sec29's
    // standing convention) - no new state needed.

    // action_stop_synced_state (Sec18.4): the global replication-mode flag
    // (0x024d4461) - CHOSEN false (single-player-only project stance, so
    // the native always takes its documented "normal local path" branch).
    bool& globalReplicationModeFlag() { return globalReplicationModeFlag_; }

protected: // private in EngineState before the split
    CoopSession coopSession_;
    OpenValue<int> coopJoinType_{"0x012f44fc (co-op join type)", "spec-lua-api-behaviour.md Sec10.9"};

    // --- Batch 2026-10-01, spec-lua-api-behaviour.md Sec27/Sec28 - see each
    // public accessor's own doc comment above for citation/reasoning. ---
    CatMouseMinigame catMouseMinigame_;
    CompletionScreen completionScreen_;
    bool pauseMenuSeenDisplayCalScreen_ = false; // Sec27.6, CONFIRMED zero at start
    int32_t textAdventureScreenIndex_ = -1;      // Sec27.7, CONFIRMED -1 initial
    int32_t hordeResultsEndAction_ = 2;          // Sec27.8, CONFIRMED initial 2
    bool coopLobbyFinished_ = false; // Sec27.10, CONFIRMED zero-filled
    DialogForceClose dialogForceClose_;
    PlayerSlots playerSlots_;
    OpenValue<int32_t> coopFriendlyFireRaw_{"0x012f4500 (coop friendly-fire internal mode)",
                                            "spec-lua-api-behaviour.md Sec27.15/Sec27.16"};
    OpenValue<bool> steamInterfaceAvailable_{"Steam user interface pointer (0x0101c54c import) non-null",
                                             "spec-lua-api-behaviour.md Sec27.19"};
    std::vector<SignInRequest> signInRequests_;
    uint64_t signInErrorDialogCount_ = 0;
    bool coopLiveSessionActive_ = false;    // 0x014ff6c1, Sec27.23, CONFIRMED zero-filled
    bool coopSyslinkSessionActive_ = false; // 0x014ff6c2, Sec27.24, CONFIRMED zero-filled
    OpenValue<bool> cellphoneAnimSuppressed_{"0x00943a20(player) result", "spec-lua-api-behaviour.md Sec28.23"};
    uint64_t cellphoneAnimPlayedCount_ = 0;
    WaitingForPlayerDialog waitingForPlayerDialog_;
    // --- pause-map / Sec31, Sec32.1 pause_map_tutorial_mode - batch
    // 2026-10-02 - see each public accessor's own doc comment above for
    // citation/reasoning. ---
    OpenValue<bool> pauseMapTutorialMode_{"0x0229a318 (pause-map tutorial-mode flag)",
                                          "spec-lua-api-behaviour.md Sec31.5/Sec32.1"};
    uint32_t pauseMapSelectedZone_ = 0; // 0x0229a2ac, see pauseMapSelectedZone()'s own doc comment
    PauseMapHoveredZone pauseMapHoveredZone_;
    uint64_t pauseMapStagCompletionHookFiredCount_ = 0;
    uint64_t pauseMapTakeoverClaimedCount_ = 0;
    uint64_t pauseMapTakeoverAutosaveRequestCount_ = 0;
    OpenValue<bool> cellCameraEnabled_{"cell_camera_enable global", "spec-lua-api-behaviour.md Sec45.4"};
    bool activeInputIsGamepad_ = false;
    int32_t uiScreenStackDepth_ = 0;
    uint64_t popScreenCount_ = 0;
    int32_t currentLanguageIndex_ = -1;                   // Sec16.1
    uint64_t dialogBoxDisconnectCount_ = 0;               // Sec16.9
    bool mainMenuProfileChangeSuppressed_ = false;        // Sec16.10
    int32_t pauseMenuControlScheme_ = -1;                 // Sec16.11, CHOSEN "none" default
    int32_t pauseMenuPresetScheme0_ = 0;
    int32_t pauseMenuPresetScheme1Or2_ = 0;
    std::map<std::pair<int32_t, int32_t>, PauseMenuOptionRequest> pauseMenuOptionRequests_; // Sec16.12
    std::unordered_map<int32_t, uint64_t> pauseMenuRestoreDefaultsCounts_;                  // Sec16.13
    OpenValue<bool> hordeModeActive_{"horde_mode_is_active global byte (0x006e44f0)", "spec-lua-api-behaviour.md Sec11.6"};
    PauseMenuOptions pauseMenuOptions_;

    // --- Wave 2 batch W2-6 (2026-10-08) - see each public accessor's own
    // doc comment above for citation/reasoning. ---
    InterfaceEffectState interfaceEffect_;
    bool onlineValidatorChecking_ = false;
    CellphoneVehicleDelivery cellphoneVehicleDelivery_;
    RemotePlayerInfo remotePlayer_;
    bool completionScreenFinishedFlag_ = false;
    OpenValue<bool> radialMenuQuickItemSucceeds_{"radial-menu quick-item use outcome (0x00600720)",
                                                  "spec-lua-api-behaviour.md Sec26.18"};
    uint64_t radialMenuSlot12Count_ = 0;
    VirtualKeyboardState virtualKeyboard_;
    OpenValue<bool> virtualKeyboardCloseResult_{"game_vkeyboard_input synchronous close result (0x0059f8a0)",
                                                 "spec-lua-api-behaviour.md Sec26.20"};
    OpenValue<bool> steamLoggedOn_{"Steam logged-on state (ISteamUser::BLoggedOn)", "spec-lua-api-behaviour.md Sec23.13/Sec44.2"};
    SyslinkSearch syslinkSearch_;
    SteamOverlayCounters steamOverlay_;
    uint64_t axisActionUnknownNameGuardCount_ = 0;
    MainMenuNewGame mainMenuNewGame_;
    int32_t coopMenuType_ = 0;
    CoopKickState coopKick_;
    JoinableState joinable_;
    uint64_t syslinkJoinCount_ = 0;
    int32_t syslinkJoinLastIndex_ = -1;
    HordeLobbyState hordeLobby_;
    PauseMapFilterState pauseMapFilter_;
    ProtoSelectState protoSelect_;
    uint64_t gameRecordAmbiguousRefusalCount_ = 0;
    bool gameTerminateRequested_ = false;
    OpenValue<bool> imperialUnitsOverrideForTesting_{"game_use_imperial_units test override", "spec-lua-api-behaviour.md Sec56.3"};
    GameStateStack gameStateStack_;
    GamePlayMode gamePlayMode_;
    OpenValue<uint32_t> coopPing_{"co-op ping (first non-local session member +0xb200)", "spec-lua-api-behaviour.md Sec55.1"};
    int32_t dialogPoolSlotsInUse_ = 0;
    DialogUiState dialogUi_;
    CellUiState cellUi_;
    SaintsbookState saintsbook_;
    MainMenuVideo mainMenuVideo_;
    OpenValue<bool> mainMenuVideoCreateSucceeds_{"main_menu_restart_video video-object creation", "spec-lua-api-behaviour.md Sec50.1"};
    PcOptionsState pcOptions_;
    bool pauseMenuHostInCharacterCreation_ = false;
    bool catMouseMinigameActive_ = false;
    uint64_t pauseMenuCommonTopFinishedLoadingCount_ = 0;
    MainMenuMatchmaking mainMenuMatchmaking_;
    uint64_t mainMenuContinueCount_ = 0;
    OpenValueMap<std::string> localizedStringForTag_{"localized string for tag", "spec-lua-api-behaviour.md Sec53.5"};
    PauseMenuHorde pauseMenuHorde_;
    bool southpawControlScheme_ = false;
    bool vehicleSouthpawControlScheme_ = false;
    PauseMapZoomState pauseMapZoomState_;
    PauseMapBookmarks pauseMapBookmarks_;
    uint64_t pauseMapDragMapCount_ = 0;
    PauseMenuControlSchemeSets pauseMenuControlSchemeSets_;
    bool cityLoadImgComplete_ = false;
    uint64_t communityLoginOpenCount_ = 0;
    int32_t completionCallbackCaseSelected_ = -1;
    AutilHudMayhemInit autilHudMayhemInit_;
    bool pauseMenuQuitRequested_ = false;
    bool completionCreditsWereActive_ = false;
    uint64_t mainMenuRedeemCodeOpenCount_ = 0;
    CellphoneDialState cellphoneDial_;
    bool globalReplicationModeFlag_ = false;
};

} // namespace sr3luahost
