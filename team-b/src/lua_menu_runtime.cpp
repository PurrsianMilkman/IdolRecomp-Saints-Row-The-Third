// Implementation for sr3luahost/menu_runtime.h (spec-frontend-menu-runtime.md)
// and the two EngineState methods menu_runtime.h's own FadeCompletionTeardown
// needs (declared in engine_state.h, implemented here since they are the
// only callers that need bare_globals.h's full BareThreadTable definition -
// see engine_state.h's own forward-declaration comment).
#include "sr3luahost/menu_runtime.h"

#include <stdexcept>

#include "sr3luahost/bare_globals.h"
#include "sr3luahost/save_game_bridge.h"

namespace sr3luahost {

// ===========================================================================
// EngineState::pushGamePauseReason / popGamePauseReason
// ===========================================================================
void EngineState::pushGamePauseReason(int32_t reason) {
    if (gamePauseDepth_.depth == 0 && gamePauseThreads_ && gamePauseGameplayL_) {
        gamePauseThreads_->exemptState(gamePauseGameplayL_);
    }
    ++gamePauseDepth_.depth;
    gamePauseDepth_.lastReason = reason;
}

void EngineState::popGamePauseReason(int32_t reason) {
    // Sec37.3: bookkeeping-only - a pop never checks whether this reason was
    // the one pushed, so an over-eager pop (depth already 0) is simply
    // ignored rather than going negative.
    if (gamePauseDepth_.depth <= 0) return;
    --gamePauseDepth_.depth;
    gamePauseDepth_.lastReason = reason;
    if (gamePauseDepth_.depth == 0 && gamePauseThreads_ && gamePauseGameplayL_) {
        gamePauseThreads_->unexemptState(gamePauseGameplayL_);
    }
}

namespace {

// Same convention as src/lua_screen_fade.cpp's own uiCallWatchdog / tools/
// lua_host_run.cpp's own missionWatchdogHook: a real Lua 5.1 instruction-
// count hook (lua_sethook + LUA_MASKCOUNT), not invented language behavior -
// a tool-safety measure only, so a misbehaving fixture function cannot hang
// a ctest run.
constexpr int kCoroutineLaunchInstructionBudget = 5'000'000;

void coroutineLaunchWatchdog(lua_State* L, lua_Debug*) {
    luaL_error(L, "sr3luahost watchdog: menu-runtime coroutine launch exceeded the %d-instruction budget",
               kCoroutineLaunchInstructionBudget);
}

}  // namespace

// ===========================================================================
// LaunchLuaCoroutine / LaunchMissionStartCoroutine
// ===========================================================================
LuaCoroutineLaunchResult LaunchLuaCoroutine(lua_State* L, BareThreadTable& threads, const std::string& funcName,
                                             int nargs, const std::function<void(lua_State*)>& pushArgs) {
    LuaCoroutineLaunchResult r;
    lua_getglobal(L, funcName.c_str());
    r.existedAsFunction = (lua_type(L, -1) == LUA_TFUNCTION);
    lua_pop(L, 1);
    if (!r.existedAsFunction) return r;

    r.attemptedCall = true;
    if (pushArgs) pushArgs(L);
    // BareThreadTable::allocate() fetches the global itself (the runner's
    // own step 4, matching the real engine's "look it up again inside the
    // allocate/run pair" shape - see tools/lua_host_run.cpp's own
    // callMissionStart() doc comment for the identical convention) - only
    // the arguments go on L's stack here.
    BareThreadTable::Record* rec = threads.allocate(L, funcName, nargs, /*hasParent=*/false);
    if (!rec) {
        lua_settop(L, 0);  // allocate() leaves the arguments when the table is full
        r.callOk = false;
        r.callError = "menu_runtime: script-thread table full (" + std::to_string(BareThreadTable::kCapacity) +
                      " records) - " + funcName + " not started";
        return r;
    }
    lua_State* co = rec->co;
    // MISSION-HOST PHASE 3 (this task): captured before run() can release
    // `rec` - the same "read the id before the record may become dangling"
    // convention tools/lua_host_run.cpp's own callMissionStart() already
    // uses, so a caller still has this coroutine's real id if it suspends.
    const uint16_t recordId = rec->id;
    // Pin `co` in L's registry for the duration of this call: `rec` (and
    // its own anchoring ref) is dangling once run() releases it below, but
    // this function still needs to read `co`'s status/error afterward -
    // same safety pattern tools/lua_host_run.cpp's own callMissionStart()
    // uses, for the identical reason.
    lua_rawgeti(L, LUA_REGISTRYINDEX, rec->ref);
    const int coRef = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_sethook(co, coroutineLaunchWatchdog, LUA_MASKCOUNT, kCoroutineLaunchInstructionBudget);
    std::string fetchError;
    const bool alive = threads.run(L, rec, fetchError);  // rec is dangling once this returns false
    lua_sethook(co, nullptr, 0, 0);
    if (!fetchError.empty()) {
        r.callOk = false;
        r.callError = "menu_runtime: runner could not fetch " + funcName + ": " + fetchError;
    } else if (alive) {
        r.callOk = true;
        r.suspended = true;
        r.recordId = recordId;
    } else {
        const int st = lua_status(co);
        if (st != 0 && st != LUA_YIELD) {
            const char* msg = lua_tostring(co, -1);
            r.callOk = false;
            r.callError = msg ? msg : "(no error message)";
        } else {
            r.callOk = true;
        }
    }
    luaL_unref(L, LUA_REGISTRYINDEX, coRef);
    return r;
}

LuaCoroutineLaunchResult LaunchMissionStartCoroutine(lua_State* L, BareThreadTable& threads,
                                                      const std::string& missionStem, const std::string& checkpoint,
                                                      bool isRestart) {
    // Sec36.2/36.4: "the caller then pushes exactly two arguments onto that
    // record - the checkpoint name as a string... and the restart flag as a
    // boolean" - never a number (Sec36.4's own explicit warning, the real
    // dlc2_m02 bug shape tests/lua_host_run_fixture.cpp's own top comment
    // documents).
    return LaunchLuaCoroutine(L, threads, missionStem + "_start", 2, [&](lua_State* l) {
        lua_pushstring(l, checkpoint.c_str());
        lua_pushboolean(l, isRestart ? 1 : 0);
    });
}

// ===========================================================================
// InputLockDispatcher (Sec6, `0x007ae0a0`)
// ===========================================================================
LuaCoroutineLaunchResult InputLockDispatcher::dispatch(lua_State* L, BareThreadTable& threads,
                                                        int32_t currentUiScreenCode, bool arg, bool screenVisible) {
    // Step 1: screen-visibility query (`0x007b3b70` -> `0x007ca730`) -
    // purpose not traced (OPEN); counted only.
    if (screenVisible) ++screenVisibleQueryCount_;
    // Step 2: screen code 5 (and not 0x11, which collapses to "only == 5
    // fires" since nothing else in this dispatcher reacts to 0x11).
    if (currentUiScreenCode == 5) {
        ++newGameBlockInputFiredCount_;
        return LaunchLuaCoroutine(L, threads, "main_menu_new_game_block_input", 0, nullptr);
    }
    // Step 3: screen code 0xb (11) - pushes the caller's own boolean arg.
    if (currentUiScreenCode == 0xb) {
        ++saveLoadLockInputFiredCount_;
        return LaunchLuaCoroutine(L, threads, "save_load_lock_input", 1,
                                   [arg](lua_State* l) { lua_pushboolean(l, arg ? 1 : 0); });
    }
    return LuaCoroutineLaunchResult{};  // neither hook applies for this screen code
}

// ===========================================================================
// FadeCompletionTeardown (Sec4, `0x007b4cb0` -> `0x007b3e80`/`0x007b3de0`)
// ===========================================================================
void FadeCompletionTeardown(EngineState& es, uint32_t /*target*/) {
    const EngineState::FadeTeardownDialogState& dlg = es.fadeTeardownDialogState();
    // 0x007b3e80: push/pop reason 0xd, edge-triggered on the class-bit-0x8
    // predicate (the spec's own "differs from cached byte" / "going the
    // other way" wording). This project's closed world has exactly ONE
    // real pusher of this counter (this function), so `lastReason == 0xd`
    // doubles safely as that cached edge - see engine_state.h's own
    // GamePauseDepthState doc comment for why a mixed-reason world would
    // need a dedicated cache field instead.
    const auto& depth = es.gamePauseDepth();
    if (dlg.anyOpenClassBit0x8Dialog) {
        if (depth.depth == 0 || depth.lastReason != 0xd) es.pushGamePauseReason(0xd);
    } else {
        if (depth.depth > 0 && depth.lastReason == 0xd) es.popGamePauseReason(0xd);
    }
    // 0x007b3de0: structural mirror, bit 0x40, OPEN side effect (0x007f1070) -
    // counted only, not modelled.
    if (dlg.anyOpenClassBit0x40Dialog) es.recordFadeTeardownSecondEffect();
}

// ===========================================================================
// GameModeTable (Sec1)
// ===========================================================================
GameModeTable::GameModeTable() {
    registerMode(game_mode::kGameplay, "gameplay", 0);
    registerMode(game_mode::kControlScheme, "control-scheme/options UI", 0);
    registerMode(game_mode::kBinkMovie, "Bink movie", 0);
    registerMode(game_mode::kAsyncPlayerTeleport, "async player-teleport", 0);
    registerMode(game_mode::kShutdown, "shutdown", 0);
    registerMode(game_mode::kLoadingScreenHypothesis, "loading-screen (HYPOTHESIS)", 0);
    registerMode(game_mode::kNewGameTransitionRequestedWeakHypothesis,
                 "new-game/level-transition requested (WEAK HYPOTHESIS)", 0);
    // Slots 0/1/2/9 stay unregistered - Sec1: "OPEN, not resolved."
}

void GameModeTable::registerMode(int32_t index, std::string name, uint8_t flagByte) {
    if (index < 0 || index >= game_mode::kCount) {
        throw std::out_of_range("GameModeTable::registerMode: index out of the real table's own 0x0b bound");
    }
    slots_[static_cast<size_t>(index)] = Slot{true, std::move(name), flagByte};
}

bool GameModeTable::isRegistered(int32_t index) const {
    if (index < 0 || index >= game_mode::kCount) return false;
    return slots_[static_cast<size_t>(index)].registered;
}

const std::string& GameModeTable::modeName(int32_t index) const {
    static const std::string kEmpty;
    if (index < 0 || index >= game_mode::kCount) return kEmpty;
    return slots_[static_cast<size_t>(index)].name;
}

void GameModeTable::pushMode(int32_t index) {
    // "a mode already on the stack is not pushed twice" (Sec1's own reading
    // of 0x00706fe0(1, mode)'s call shape).
    if (isOnStack(index)) return;
    stack_.push_back(index);
}

void GameModeTable::popMode() {
    if (!stack_.empty()) stack_.pop_back();
}

int32_t GameModeTable::currentMode() const {
    return stack_.empty() ? game_mode::kNone : stack_.back();
}

bool GameModeTable::isOnStack(int32_t index) const {
    for (int32_t m : stack_) {
        if (m == index) return true;
    }
    return false;
}

// ===========================================================================
// MissionPhaseMachine (lua-api Sec21.27a + Sec36.2/36.4)
// ===========================================================================
void MissionPhaseMachine::bind(const std::string& missionStem, bool isRestart, bool hasName) {
    boundMissionStem_ = missionStem;
    phase_ = MissionPhase::Binding;    // phase 8, `0x006d6920`
    phase_ = MissionPhase::Beginning;  // auto-advance to 1 (Sec21.27a's phase-8 row)
    marker_ = MissionStartMarker{};
    if (!isRestart && hasName) {
        // `0x006D9310` -> `0x006d5720`: arm the marker, advance to phase 2.
        phase_ = MissionPhase::WaitingZoneStreaming;
        marker_.armed = true;
        marker_.pendingMissionStem = missionStem;
        marker_.checkpointName = "mission start";
        marker_.isRestart = false;
    }
    // else: `0x006D9310` skips phase 2 entirely - the caller must call
    // relaunchDirect() itself.
}

LuaCoroutineLaunchResult MissionPhaseMachine::triggerMarker() {
    if (!marker_.armed) {
        throw std::logic_error(
            "MissionPhaseMachine::triggerMarker: the mission-start marker is not armed (no fresh, named, "
            "non-restart bind is pending) - nothing for the player to have organically reached");
    }
    ++marker_.triggerCount;
    marker_.armed = false;
    LuaCoroutineLaunchResult r = launchStartFunction("mission start", false);
    phase_ = MissionPhase::MarkerArmedCheckpointIcon;  // phase 5, `0x006d86f0(5)`
    return r;
}

LuaCoroutineLaunchResult MissionPhaseMachine::relaunchDirect(const std::string& checkpointName, bool isRestart) {
    return launchStartFunction(checkpointName, isRestart);
}

LuaCoroutineLaunchResult MissionPhaseMachine::launchStartFunction(const std::string& checkpointName,
                                                                    bool isRestart) {
    LuaCoroutineLaunchResult r = LaunchMissionStartCoroutine(gameplayL_, threads_, boundMissionStem_, checkpointName,
                                                              isRestart);
    if (r.attemptedCall) {
        // Build on the already-landed mission-phase-completion fix
        // (engine_state.h's own FIX 2026-10-08 comment,
        // EngineState::beginMissionPhaseTracking()) rather than duplicating
        // its phase-machine work: the SAME real hook this host's mission
        // driver (tools/lua_host_run.cpp) uses "immediately before its one
        // callMissionStart() call" - here, immediately before the mission's
        // own `_start` runs via the marker/relaunch path instead.
        es_.beginMissionPhaseTracking();
    }
    return r;
}

// ===========================================================================
// MainMenuFlow (spec-frontend-menu-runtime.md Sec2/Sec3/Sec5)
// ===========================================================================
bool MainMenuFlow::levelTransitionGate(int32_t arg1, int32_t arg2) {
    // `0x0088ba10(arg1, arg2)` (Sec2 item 6). Busy-gated on `0x024e4824` -
    // this host models no concurrent "another transition already in
    // flight" state, so the gate always opens (HOST SUBSTITUTE, labelled -
    // the spec gives no default for that busy byte either). The HYPOTHESIS
    // mode-4 sentinel write and the OPEN session-construction chain (item
    // 6(c)/(d), handed to the session/coop team by the spec itself) are
    // both left unmodelled; only the caller's own two literal args are
    // recorded, matching the one part of item 6 both CONFIRMED and actually
    // consumable here.
    ++levelTransitionGateCallCount_;
    lastLevelTransitionGateArgs_ = {arg1, arg2};
    return true;
}

MainMenuFlow::NewGameResult MainMenuFlow::runSharedNewGameRoutine(bool networkModuleUp, int32_t currentUiScreenCode,
                                                                    const std::string& zoneName) {
    NewGameResult out;
    // 1. Network module teardown (`0x00867740`) if up - no network module
    //    modelled here; recorded as an observable flag only.
    if (networkModuleUp) out.networkTornDown = true;
    // 2-4: the 20-byte template copy / patched local copy / `0x006fe180`
    //    consumer are OPEN (not independently traced) - only the CONFIRMED
    //    float-zero sub-effect (`0x006fe1d0(0)`) has anything to model, and
    //    it writes a global this project does not otherwise read, so it is
    //    not given a field either (nothing downstream would ever observe
    //    it) - a labelled no-op, not a silently dropped step.
    // 5. The two generic one-shot "transition requested" flags.
    transitionRequestedFlagA_ = true;  // `0x022425dc`
    transitionRequestedFlagB_ = true;  // `0x022425dd`
    // 6. The shared level-transition gate.
    out.levelTransitionGateOpened = levelTransitionGate(4, 2);
    // 7. Hardcoded destination world name into the shared pending-zone buffer.
    pendingZoneName_ = zoneName;
    out.pendingZoneName = pendingZoneName_;
    // 8. Shared input-lock dispatcher.
    out.inputLockResult = inputLock_.dispatch(gameplayL_, threads_, currentUiScreenCode, true);
    // 9. Fade-out request, completion callback = the shared teardown (Sec4).
    es_.screenFadeRequest(true, -1, &FadeCompletionTeardown, 1);
    out.fadeOutRequested = true;
    // 10. The 904-dword inert save-format block (Sec11.3 entry 29) - see
    //     this method's own header doc comment for why there is nothing to
    //     actually zero in this project's own model.
    ++newGameInertBlockZeroCount_;
    // 11. Dead/linker-folded stub - no-op.
    return out;
}

MainMenuFlow::NewGameResult MainMenuFlow::newGame(bool networkModuleUp, int32_t currentUiScreenCode) {
    return runSharedNewGameRoutine(networkModuleUp, currentUiScreenCode, "sr3_city");
}

MainMenuFlow::NewGameResult MainMenuFlow::hordeStart(bool networkModuleUp, int32_t currentUiScreenCode) {
    // `0x006e6630`: DAT_014f3d34 := 1, DAT_014f3d2c := 0, then tail-calls the
    // IDENTICAL routine new game uses.
    hordeFlag_ = true;
    return runSharedNewGameRoutine(networkModuleUp, currentUiScreenCode, "sr3_city");
}

MainMenuFlow::NewGameResult MainMenuFlow::jumpToZone(const std::string& zoneName, int32_t currentUiScreenCode) {
    // `0x007aebb0` (Sec5): only 4 steps - the zone-name write, the SAME
    // level-transition gate call (literal 4, 2), the input-lock dispatcher,
    // and the fade+teardown request. No network-teardown check, no
    // template copy, no one-shot flags, no inert-block memset.
    NewGameResult out;
    pendingZoneName_ = zoneName;
    out.pendingZoneName = pendingZoneName_;
    out.levelTransitionGateOpened = levelTransitionGate(4, 2);
    out.inputLockResult = inputLock_.dispatch(gameplayL_, threads_, currentUiScreenCode, true);
    es_.screenFadeRequest(true, -1, &FadeCompletionTeardown, 1);
    out.fadeOutRequested = true;
    return out;
}

MainMenuFlow::ContinueResult MainMenuFlow::continueGame(const std::vector<SaveSlotCandidate>& candidates,
                                                          int32_t currentUiScreenCode) {
    ContinueResult out;
    // Sec3 step 3: skip any slot flagged `loadError` (the real `+0x5c`/
    // `+0x5d` byte), pick the highest `lastPlayed`.
    const SaveSlotCandidate* best = nullptr;
    for (const auto& c : candidates) {
        if (c.loadError) continue;
        if (!best || c.lastPlayed > best->lastPlayed) best = &c;
    }
    if (!best) {
        // Sec3 step 4: fire `main_menu_controller_selected`, show the
        // MAIN_MENU_CONTINUE_UNAVAILABLE dialog (UI out of scope - the hook
        // fire is the one real, observable effect modelled).
        LaunchLuaCoroutine(gameplayL_, threads_, "main_menu_controller_selected", 0, nullptr);
        ++continueNoSlotAvailableCount_;
        return out;
    }
    out.slotAvailable = true;
    out.selectedPath = best->path;
    // Sec3 steps 5+7: FUN_00b95210 ("step 1. request") / FUN_00b947a0
    // ("step 3a. apply") - save_game_bridge.h's own already-real
    // loadSaveSnapshotFile() performs both synchronously, so there is no
    // separate per-frame-pending phase to model here (step 7's own "per-
    // frame continuation" collapses to "happens inline, once").
    out.loadOk = loadSaveSnapshotFile(best->path, es_, out.loadError);
    if (out.loadOk) {
        // Sec3 step 6: the SAME input-lock dispatcher + fade-out new game's
        // own step 8/9 uses (the spec's own "net comparison" calls these
        // explicitly shared).
        inputLock_.dispatch(gameplayL_, threads_, currentUiScreenCode, true);
        es_.screenFadeRequest(true, -1, &FadeCompletionTeardown, 1);
        out.fadeOutRequested = true;
    } else {
        // Sec3 step 8 (the else arm, `0x007ae63c`): clears the SAME generic
        // flag new game sets, and pushes mode 3 (loading-screen, HYPOTHESIS)
        // unless already there.
        transitionRequestedFlagB_ = false;  // `0x022425dd`
        modes_.pushMode(game_mode::kLoadingScreenHypothesis);
        out.pushedLoadingScreenMode = true;
    }
    return out;
}

}  // namespace sr3luahost
