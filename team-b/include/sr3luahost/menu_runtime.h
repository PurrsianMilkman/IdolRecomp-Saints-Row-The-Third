// Front-end/menu runtime (spec-frontend-menu-runtime.md) - the 11-slot
// top-level game-mode table (Sec1), the real new-game/horde-start/continue
// flows (Sec2/Sec3/Sec5), and the shared input-lock dispatcher (Sec6) that
// both route through. The fade-completion teardown (Sec4) and the game-
// pause depth counter it drives (Sec37.3) live on EngineState itself
// (engine_state.h's own GamePauseDepthState/FadeTeardownDialogState,
// FadeCompletionTeardown's free-function declaration below) since that
// callback's signature (EngineState::FadeCallback, a plain function
// pointer) cannot carry extra captured context.
//
// CRITICAL FACT this file deliberately does NOT violate (spec-frontend-
// menu-runtime.md Sec2's own highest-value negative result): new game's
// real EXE code never calls the mission-bind function (`0x006d6920`) and
// never touches the mission-phase machine at all - MainMenuFlow::newGame()
// below stops exactly where the real `0x007ae950` routine stops. Binding
// the FIRST mission is a separate, still-OPEN zone/level-start link (Sec2's
// own closing paragraph; spec-lua-api-behaviour.md Sec36.1's own "which
// carrier holds a scripted_mission_start object" OPEN item) - modelled
// here as MissionPhaseMachine::bind(), a method nothing in MainMenuFlow
// ever calls. A caller that wants the full "new game -> first mission"
// chain must call bind() itself, from OUTSIDE MainMenuFlow, exactly as the
// real engine's own (untraced) zone/level-start code would - never by
// hardcoding a call to any one mission's own `_start` function from inside
// newGame() itself.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/lua_c_api.h"

namespace sr3luahost {

class BareThreadTable;

// --- Sec1: the 11-slot top-level game-mode table --------------------------
namespace game_mode {
inline constexpr int32_t kCount = 11;         // `0x01503bc0`, bound `< 0xb`
inline constexpr int32_t kNone = -1;          // nothing on the stack
inline constexpr int32_t kGameplay = 5;       // `0x007041b0` - CONFIRMED
inline constexpr int32_t kControlScheme = 6;  // `0x007e1d10` - CONFIRMED (resolved 2026-10-08)
inline constexpr int32_t kBinkMovie = 7;      // `0x00bdc120` - CONFIRMED
inline constexpr int32_t kAsyncPlayerTeleport = 8; // `0x007a8530` - CONFIRMED
inline constexpr int32_t kShutdown = 10;      // 0xa - CONFIRMED (spec-lua-api-behaviour.md Sec26.25)
// HYPOTHESIS (Sec1): the loading-screen mode - call-shape/context evidence
// from main_menu_continue's own body, callee bodies not independently read.
inline constexpr int32_t kLoadingScreenHypothesis = 3;
// WEAK HYPOTHESIS (Sec1/Sec2): an object-field sentinel-value coincidence
// only (0xb/11 matching this table's own 11-slot bound) - no traced
// consumer. Named here only so a caller can cite it; nothing in this file
// ever pushes it.
inline constexpr int32_t kNewGameTransitionRequestedWeakHypothesis = 4;
}  // namespace game_mode

// `0x01503bc0` (registration) + `0x01503b50`/`0x012f4a80` (the stack) +
// `0x00706be0`/`0x00706a10`/`0x00706a40`/`0x00706ab0`/`0x00706b90`/
// `0x00706fe0` (the family Sec1 names). Per-mode CONTENT (enter/leave/
// per-frame behaviour) is explicitly out of scope (Sec1 only asks for "the
// real table/slot shape so mode transitions are tracked correctly") - this
// class only tracks WHICH slots are registered and WHICH are currently on
// the stack.
class GameModeTable {
public:
    // Pre-registers the identities Sec1 actually names (kGameplay,
    // kControlScheme, kBinkMovie, kAsyncPlayerTeleport, kShutdown, and the
    // two HYPOTHESIS slots above with a label noting their confidence);
    // slots 0/1/2/9 stay unregistered (Sec1: "OPEN, not resolved").
    GameModeTable();

    // `0x00706a40(index, callbackBlock, byteFlag)`. CONFIRMED shape; the
    // callback block itself is mode CONTENT, not modelled. Throws
    // std::out_of_range for index >= kCount (the real setter's own bound).
    void registerMode(int32_t index, std::string name, uint8_t flagByte);
    bool isRegistered(int32_t index) const;
    const std::string& modeName(int32_t index) const;

    // `0x00706fe0(1, index)`'s own observed call shape (Sec1: "a mode value
    // compared against 3... called when it is not already 3" at
    // main_menu_continue's own call site) generalized to "a mode already on
    // the stack is not pushed twice" - the registrar's own documented rule.
    // HIGH CONFIDENCE, not CONFIRMED from `0x00706fe0`'s own bytes.
    void pushMode(int32_t index);
    // `0x00706be0`'s own pop half (implied by the stack's existence; not
    // independently named in Sec1 beyond the pump itself).
    void popMode();
    // `0x00706ab0` (read current/top mode, pre-existing).
    int32_t currentMode() const;
    bool isOnStack(int32_t index) const;
    const std::vector<int32_t>& stack() const { return stack_; }

private:
    struct Slot {
        bool registered = false;
        std::string name;
        uint8_t flagByte = 0;
    };
    std::array<Slot, static_cast<size_t>(game_mode::kCount)> slots_{};
    std::vector<int32_t> stack_;
};

// --- the shared `0x00e0ca80` coroutine-launch primitive -------------------
// (spec-lua-api-behaviour.md Sec36.2: "the SAME named-global-lookup/
// coroutine-creation primitive" used for mission-start AND, per spec-
// frontend-menu-runtime.md Sec2 step 8/Sec6, for the two input-lock Lua
// hooks). A smaller, test-friendly sibling of tools/lua_host_run.cpp's own
// callMissionStart() - deliberately NOT a call into that function, which is
// entangled with that tool's own probe-only instrumentation (a watchdog
// instruction budget, per-coroutine trace rings, several file-local
// globals private to that translation unit); this is the same CONFIRMED
// mechanism (lua_getglobal -> require a function -> BareThreadTable::
// allocate (coroutine + not-yet-started record) -> BareThreadTable::run),
// with none of that tool's own probe bookkeeping.
struct LuaCoroutineLaunchResult {
    bool existedAsFunction = false;
    bool attemptedCall = false;
    bool callOk = false;
    bool suspended = false;
    std::string callError;
    // MISSION-HOST PHASE 3 (this task): the BareThreadTable::Record id the
    // suspended coroutine was allocated under (set only when `suspended` is
    // true; otherwise left at this sentinel) - additive, so a caller that
    // needs to keep driving this SAME coroutine from its own per-frame
    // scheduler pass (tools/sr3_viewer.cpp's own runPlaytest(), the real
    // production replacement for its prior direct BareThreadTable::allocate/
    // run call) can tell this record apart from every other one that pass
    // visits. 65535 duplicates BareThreadTable::kNoThread's own sentinel
    // value (bare_globals.h) rather than importing it, since this header
    // only forward-declares BareThreadTable (see its own forward-decl
    // comment, above) and deliberately does not pull in bare_globals.h.
    uint16_t recordId = 65535;
};
LuaCoroutineLaunchResult LaunchLuaCoroutine(lua_State* L, BareThreadTable& threads, const std::string& funcName,
                                             int nargs, const std::function<void(lua_State*)>& pushArgs);

// Sec36.2/36.4's own two-argument shape (checkpoint STRING, is_restart
// BOOLEAN) - the mission-start-specific wrapper over LaunchLuaCoroutine().
LuaCoroutineLaunchResult LaunchMissionStartCoroutine(lua_State* L, BareThreadTable& threads,
                                                      const std::string& missionStem, const std::string& checkpoint,
                                                      bool isRestart);

// --- Sec6: the shared input-lock dispatcher, `0x007ae0a0` -----------------
// Current UI screen code identities are OPEN (Sec6/Sec9 item 3) - the
// caller supplies `currentUiScreenCode` directly; this project does not
// invent a UI-screen-stack model to derive it. Step 1's screen-visibility
// query (`0x007b3b70` -> `0x007ca730`, purpose not traced - OPEN) is
// represented only as an observability counter.
class InputLockDispatcher {
public:
    LuaCoroutineLaunchResult dispatch(lua_State* L, BareThreadTable& threads, int32_t currentUiScreenCode, bool arg,
                                       bool screenVisible = false);

    uint64_t screenVisibleQueryCount() const { return screenVisibleQueryCount_; }
    uint64_t newGameBlockInputFiredCount() const { return newGameBlockInputFiredCount_; }
    uint64_t saveLoadLockInputFiredCount() const { return saveLoadLockInputFiredCount_; }

private:
    uint64_t screenVisibleQueryCount_ = 0;
    uint64_t newGameBlockInputFiredCount_ = 0;
    uint64_t saveLoadLockInputFiredCount_ = 0;
};

// --- Sec4: the shared fade-completion teardown, `0x007b4cb0` --------------
// Registered as the ScreenFade completion callback (EngineState::
// FadeCallback - see this file's own top comment for why it is a free
// function, not a method). Drives EngineState::pushGamePauseReason(0xd)/
// popGamePauseReason(0xd) from EngineState::fadeTeardownDialogState()'s own
// net predicate (see that struct's own doc comment, engine_state.h, for
// what is and is not modelled).
void FadeCompletionTeardown(EngineState& es, uint32_t target);

// --- Sec21.27a (lua-api) + Sec36.2/36.4: the mission-start marker/phase ---
// machine, the REAL (non-hardcoded) mechanism new-game's own zone load
// must eventually reach. See this file's own top comment: nothing in
// MainMenuFlow below ever calls bind() - it exists for whichever OPEN
// zone/level-start code path is the real caller.
enum class MissionPhase : int32_t {
    Idle = 0,
    Beginning = 1,
    WaitingZoneStreaming = 2,  // marker armed here on a fresh, named bind
    WaitingVisibilitySync = 3,
    SilentBranch = 4,
    MarkerArmedCheckpointIcon = 5,
    EndingLocal = 6,
    FailedRetryPending = 7,
    Binding = 8,  // first phase of a newly-bound mission; auto-advances
};

struct MissionStartMarker {
    bool armed = false;
    std::string pendingMissionStem;
    std::string checkpointName = "mission start";  // `0x006d5460`'s own literal (Sec36.2's table)
    bool isRestart = false;
    uint64_t triggerCount = 0;
};

class MissionPhaseMachine {
public:
    MissionPhaseMachine(EngineState& es, lua_State* gameplayL, BareThreadTable& threads)
        : es_(es), gameplayL_(gameplayL), threads_(threads) {}

    MissionPhase phase() const { return phase_; }
    const MissionStartMarker& marker() const { return marker_; }
    const std::string& boundMissionStem() const { return boundMissionStem_; }

    // `0x006d6920` ("bind mission"). CONFIRMED: sets phase 8 the instant the
    // mission-context pointer goes non-null, "held only while [a camera/
    // cutscene transition-stage query] is still settling, then auto-advances
    // to 1" (Sec21.27a's own phase-8 row) - modelled as an immediate
    // auto-advance, since this host has no camera/cutscene-transition-stage
    // concept to hold on (HOST SUBSTITUTE, labelled). Then (Sec21.27a's
    // phase-2 row, `0x006D9310`): if NOT a restart and the mission has a
    // name, arms the marker (tail-calls `0x006d5720`) and advances to phase
    // 2; otherwise (`hasName=false` or `isRestart=true`) phase 2 is skipped
    // entirely - the caller must then invoke relaunchDirect() itself,
    // exactly as `0x006D9310` "relaunches script_mission_start directly."
    //
    // real-world `is_restart` derivation (Sec36.3: pending-in-progress-
    // mission handle == this mission) is NOT reproduced here - this host
    // has no "pending in-progress mission" concept (no resume/retry path,
    // the same scoping choice tools/lua_host_run.cpp's own callMissionStart()
    // doc comment already makes) - `isRestart` is the caller's own direct
    // input instead of a derived one.
    void bind(const std::string& missionStem, bool isRestart, bool hasName);

    // The marker's own trigger body, `0x006d5460` (CONFIRMED call shape):
    // fires the SAME coroutine-launch primitive mission-start always uses,
    // checkpoint ALWAYS the literal "mission start", isRestart ALWAYS
    // false (only ever reached on a fresh, non-restart arm - Sec36.2's own
    // table row). Also sets phase 5 ("`0x006d86f0(5)`, called... from
    // `0x006d5460` itself" - Sec21.27a). Throws std::logic_error if the
    // marker is not armed (nothing to trigger) - this project's own
    // convention for a caller-ordering mistake, not a modelled engine fault.
    LuaCoroutineLaunchResult triggerMarker();

    // The `0x006D9310` "skip phase 2, relaunch directly" branch (restart or
    // no name): fires the start primitive immediately with the caller-
    // supplied checkpoint name (Sec36.3's current-checkpoint-name global,
    // not modelled here - no resume path, see bind()'s own doc comment).
    LuaCoroutineLaunchResult relaunchDirect(const std::string& checkpointName, bool isRestart);

private:
    LuaCoroutineLaunchResult launchStartFunction(const std::string& checkpointName, bool isRestart);

    EngineState& es_;
    lua_State* gameplayL_;
    BareThreadTable& threads_;
    MissionPhase phase_ = MissionPhase::Idle;
    MissionStartMarker marker_;
    std::string boundMissionStem_;
};

// --- Sec2/Sec3/Sec5: new game / horde start / continue / the zone-jump ---
// sibling, all sharing the level-transition gate, the pending zone-name
// buffer, the input-lock dispatcher and the fade-out+teardown request.
class MainMenuFlow {
public:
    MainMenuFlow(EngineState& es, lua_State* gameplayL, BareThreadTable& threads, GameModeTable& modes)
        : es_(es), gameplayL_(gameplayL), threads_(threads), modes_(modes) {}

    struct NewGameResult {
        bool networkTornDown = false;
        bool levelTransitionGateOpened = false;
        std::string pendingZoneName;
        LuaCoroutineLaunchResult inputLockResult;
        bool fadeOutRequested = false;
    };

    // `0x007ae950`'s full body (Sec2, items 1-11). Item 3 (`0x006fe180`,
    // the patched-local-copy consumer) and the level-transition gate's own
    // inner session-construction chain (item 6(c)/(d)) are OPEN/HYPOTHESIS
    // per the spec and NOT independently traced - left unmodelled rather
    // than guessed (levelTransitionGate() below records only the two
    // literal args, the one part of item 6 that is both CONFIRMED and
    // actually consumed by a caller). Item 10's 904-dword inert-save-block
    // memset needs no new field: this project's own SaveSnapshotFields has
    // no backing storage for that block at all (nothing to zero - already
    // the vacuous truth spec-save-format.md Sec11.3's own "reads all-zero
    // in fresh/new-game saves" empirical note describes), so it is a
    // counted no-op (newGameInertBlockZeroCount()) rather than a fabricated
    // write. Does NOT call MissionPhaseMachine::bind() - see this file's
    // own top comment for why that would violate Sec2's own critical
    // negative result.
    //
    // Checked 2026-10-08 against spec-frontend-menu-runtime.md Sec2 item
    // 7/Sec36.1's data-side advance (mission 1's own dedicated zone,
    // `sr3_city~fm01.czn_pc`, carries a real `Start_player_nav` marker -
    // spec-zone-data-format.md Sec11's own correction paragraph): the
    // literal "sr3_city" below is still correct and UNCHANGED - it is
    // `0x007ae950`'s own disassembly-confirmed hardcoded write to the
    // GENERIC pending-zone-name buffer (also written by main_menu_continue
    // and jumpToZone()'s own `0x007aebb0`), not a claim about which zone
    // carries mission 1's own start marker/population. That is a separate,
    // still-OPEN question for whichever zone/level-start code eventually
    // calls MissionPhaseMachine::bind() - not this routine's concern.
    NewGameResult newGame(bool networkModuleUp, int32_t currentUiScreenCode);

    // `0x007c9d60` -> `0x006e6630`: sets the horde flag, then tails into the
    // IDENTICAL routine newGame() runs (Sec2's own transcription). `0x014f3d34`
    // := 1, `0x014f3d2c` := 0 - read by "~20 functions", none traced here.
    NewGameResult hordeStart(bool networkModuleUp, int32_t currentUiScreenCode);
    bool hordeFlag() const { return hordeFlag_; }

    // `0x007aebb0` (Sec5): new game's exact sequence generalized to a
    // caller-supplied zone name, WITHOUT the network-teardown check, the
    // 20-byte template copy, the two one-shot flags, or the inert-block
    // memset (Sec5's own transcription has only 4 steps). HYPOTHESIS, not
    // confirmed: a developer/machinima "load any named zone as if starting
    // a new game there" helper (Sec5's own confidence note) - exposed here
    // only as the body itself, not as a claim about who can reach it.
    NewGameResult jumpToZone(const std::string& zoneName, int32_t currentUiScreenCode);

    struct SaveSlotCandidate {
        std::string path;
        int64_t lastPlayed = 0;
        bool loadError = false;  // the real `+0x5c`/`+0x5d` flag (Sec3 step 3)
    };
    struct ContinueResult {
        bool slotAvailable = false;
        std::string selectedPath;
        bool loadOk = false;
        std::string loadError;
        bool fadeOutRequested = false;
        bool pushedLoadingScreenMode = false;  // the else-arm's own mode-3 push (Sec3 step 8)
    };

    // `main_menu_continue` (`0x007ae420`, Sec3's full per-frame body,
    // collapsed to its own synchronous net effect - see this method's own
    // body comment for why the real "per-frame continuation" (step 7) needs
    // no separate pending phase here). Reuses save_game_bridge.h's own
    // already-real loadSaveSnapshotFile() for the "step 1. request" /
    // "step 3a. apply" pair (`0x00B95210`/`0x00B947A0`) rather than
    // duplicating it.
    ContinueResult continueGame(const std::vector<SaveSlotCandidate>& candidates, int32_t currentUiScreenCode);

    uint64_t newGameInertBlockZeroCount() const { return newGameInertBlockZeroCount_; }
    uint64_t levelTransitionGateCallCount() const { return levelTransitionGateCallCount_; }
    std::pair<int32_t, int32_t> lastLevelTransitionGateArgs() const { return lastLevelTransitionGateArgs_; }
    bool pendingTransitionFlagA() const { return transitionRequestedFlagA_; }  // `0x022425dc`
    bool pendingTransitionFlagB() const { return transitionRequestedFlagB_; } // `0x022425dd`
    const std::string& pendingZoneName() const { return pendingZoneName_; }

private:
    NewGameResult runSharedNewGameRoutine(bool networkModuleUp, int32_t currentUiScreenCode,
                                           const std::string& zoneName);
    bool levelTransitionGate(int32_t arg1, int32_t arg2);

    EngineState& es_;
    lua_State* gameplayL_;
    BareThreadTable& threads_;
    GameModeTable& modes_;
    InputLockDispatcher inputLock_;

    bool hordeFlag_ = false;
    std::string pendingZoneName_;
    bool transitionRequestedFlagA_ = false;
    bool transitionRequestedFlagB_ = false;
    uint64_t newGameInertBlockZeroCount_ = 0;
    uint64_t levelTransitionGateCallCount_ = 0;
    std::pair<int32_t, int32_t> lastLevelTransitionGateArgs_{0, 0};
    uint64_t continueNoSlotAvailableCount_ = 0;
};

}  // namespace sr3luahost
