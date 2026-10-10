// Synthetic end-to-end test for spec-frontend-menu-runtime.md: the 11-slot
// game-mode table (Sec1), the real new-game/horde-start/continue flows
// (Sec2/Sec3), the shared input-lock dispatcher (Sec6) and fade-completion
// teardown (Sec4), and the mission-start marker/phase machine (lua-api
// Sec21.27a, Sec36.2/36.4).
//
// The critical fact this file is built to PROVE, not merely assert (Sec2's
// own highest-value negative result): MainMenuFlow::newGame() never calls
// the mission-bind function and never touches the mission-phase machine -
// section 1 below calls newGame() alone and checks that the synthetic
// mission's own `_start` function was NOT invoked. Section 2 then drives
// the SEPARATE, still-OPEN zone/level-start link (spec-lua-api-behaviour.md
// Sec36.1's own "which carrier holds a scripted_mission_start object" OPEN
// item) explicitly, from OUTSIDE MainMenuFlow, through the REAL marker/
// phase mechanism (bind -> arm -> organic trigger), and shows the mission
// really starts - the SAME observable "mission started" signal this
// project's own mission-phase-completion fix
// (EngineState::beginMissionPhaseTracking(), engine_state.h) already uses.
//
// "m01" is this project's own real, independently-extracted mission-stem
// roster fact (tools/mission_package_per_container.tsv's own
// mission_lua_stem column - real game Lua script data, not a disassembly
// finding and not invented for this test), used here only as the TEST's
// own stand-in for the zone's real (still-OPEN) mission-binding carrier -
// never as a literal inside newGame()/MainMenuFlow itself. `m01_start`'s
// OWN BODY below is a synthetic fixture (same established convention as
// tests/lua_host_run_fixture.cpp's dlc1_mm_0N bodies), not real game script
// text.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/menu_runtime.h"
#include "sr3luahost/save_game_bridge.h"

using namespace sr3luahost;
namespace fs = std::filesystem;

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
    auto r = host.runChunk(L, code, "menu_runtime_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool globalIsNil(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool nilVal = lua_isnil(L, -1);
    lua_pop(L, 1);
    return nilVal;
}

std::string globalAsString(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    std::string s = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return s;
}

bool globalAsBool(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool b = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return b;
}

int globalAsInt(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    int v = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return v;
}

fs::path makeSandboxDir(const char* tag) {
    fs::path dir = fs::temp_directory_path() /
                   (std::string("sr3_menu_runtime_test_") + tag + "_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir;
}

}  // namespace

int main() {
    // ========================================================================
    // 1. GameModeTable (Sec1): registration shape + the "not pushed twice"
    //    rule + mode 6's real identity.
    // ========================================================================
    {
        GameModeTable modes;
        CHECK(modes.isRegistered(game_mode::kGameplay));
        CHECK(modes.isRegistered(game_mode::kControlScheme));
        CHECK(modes.modeName(game_mode::kControlScheme).find("control-scheme") != std::string::npos);
        CHECK(modes.isRegistered(game_mode::kShutdown));
        // Slots Sec1 leaves OPEN stay unregistered.
        CHECK(!modes.isRegistered(0));
        CHECK(!modes.isRegistered(1));
        CHECK(!modes.isRegistered(2));
        CHECK(!modes.isRegistered(9));

        CHECK(modes.currentMode() == game_mode::kNone);
        modes.pushMode(game_mode::kLoadingScreenHypothesis);
        CHECK(modes.currentMode() == game_mode::kLoadingScreenHypothesis);
        CHECK(modes.isOnStack(game_mode::kLoadingScreenHypothesis));
        // Pushing the SAME mode again must not duplicate it on the stack
        // (the registrar's own "not pushed twice" rule).
        modes.pushMode(game_mode::kLoadingScreenHypothesis);
        CHECK(modes.stack().size() == 1);
        modes.pushMode(game_mode::kGameplay);
        CHECK(modes.stack().size() == 2);
        CHECK(modes.currentMode() == game_mode::kGameplay);
        modes.popMode();
        CHECK(modes.currentMode() == game_mode::kLoadingScreenHypothesis);
    }

    // ========================================================================
    // 2. New game: prove the critical negative result (Sec2) - newGame()
    //    alone never reaches any mission's `_start`, even though a
    //    synthetic one exists and would be trivially callable by name.
    // ========================================================================
    {
        Host host({});
        lua_State* L = host.gameplayState();
        EngineState& es = host.engineState();
        BareThreadTable& threads = host.bareGlobals().threads();
        es.attachGamePauseScheduler(&threads, L);
        GameModeTable modes;
        MainMenuFlow flow(es, L, threads, modes);

        // A synthetic stand-in for the real `m01_start` entry point (NOT
        // real game script text - see this file's own top comment). Records
        // its own checkpoint/is_restart arguments and flips a "started" flag
        // other synthetic mission's own check in section 3 below reads.
        ok(host, L,
           "function m01_start(cp, restart)\n"
           "  m01_started = true\n"
           "  m01_started_cp = cp\n"
           "  m01_started_restart = restart\n"
           "end\n");
        // The two Lua hook names the input-lock dispatcher (Sec6) fires
        // through the SAME `0x00e0ca80` primitive mission-start uses.
        ok(host, L, "function main_menu_new_game_block_input() new_game_block_input_fired = "
                    "(new_game_block_input_fired or 0) + 1 end");

        MainMenuFlow::NewGameResult r = flow.newGame(/*networkModuleUp=*/false, /*currentUiScreenCode=*/5);

        // Sec2's own confirmed, observable effects.
        CHECK(r.pendingZoneName == "sr3_city");
        CHECK(flow.pendingZoneName() == "sr3_city");
        CHECK(r.levelTransitionGateOpened);
        CHECK(flow.levelTransitionGateCallCount() == 1);
        {
            auto gateArgs = flow.lastLevelTransitionGateArgs();
            CHECK(gateArgs.first == 4 && gateArgs.second == 2);
        }
        CHECK(flow.pendingTransitionFlagA());
        CHECK(flow.pendingTransitionFlagB());
        CHECK(r.fadeOutRequested);
        // Screen code 5 -> main_menu_new_game_block_input fires for real.
        CHECK(r.inputLockResult.existedAsFunction);
        CHECK(r.inputLockResult.attemptedCall);
        CHECK(r.inputLockResult.callOk);
        CHECK(globalAsInt(L, "new_game_block_input_fired") == 1);

        // THE CRITICAL CHECK: new game, by itself, must not have bound or
        // started ANY mission - even though m01_start exists and is a
        // trivially callable global.
        CHECK(globalIsNil(L, "m01_started"));
        CHECK(globalIsNil(L, "m01_started_cp"));
        CHECK(es.gamePauseDepth().depth == 0);  // no dialog open -> fade teardown pushed nothing

        // The fade-out request itself completes via this project's own
        // existing host-side fallback (no UI screen_fade_do defined here) -
        // proving the fade primitive, and therefore FadeCompletionTeardown
        // (Sec4), really ran.
        es.screenFadeHostFrame(1);
        CHECK(es.screenFadeCounters().fallbackUndefined == 1 || es.screenFadeCounters().realCompletions == 1);
    }

    // ========================================================================
    // 3. The REAL (non-hardcoded) data-driven path from a bound mission to
    //    its own `_start`: bind() -> arm -> organic marker trigger. Proves
    //    the marker-arm mechanism spec-lua-api-behaviour.md Sec21.27a/
    //    Sec36.2/36.4 documents, NOT a shortcut call to m01_start. This is a
    //    GENERIC mechanism demonstration (the branch an ordinary story
    //    mission with a preceding free-roam period takes) - "m01" is used
    //    only as a real, independently-extracted stem name (this file's own
    //    top-comment citation), NOT a claim that the real mission 1 (bank
    //    heist) itself takes this branch. Corrected 2026-10-08: it most
    //    likely does NOT - spec-zone-data-format.md Sec11's own correction
    //    paragraph found mission 1's real per-mission zone
    //    (`sr3_city~fm01.czn_pc`, inside `sr3_city_0.vpp_pc/m01.str2_pc/`)
    //    carries neither a `scripted_mission_start`- nor a
    //    `silent_mission_start`-classed object, nor any mission-start-shaped
    //    `Trigger_Volume`, among its 415 objects (hash-checked population
    //    census) - HIGH CONFIDENCE (spec-lua-api-behaviour.md Sec36.1's own
    //    data-side-advance paragraph, Sec21.27a/b) mission 1 instead reaches
    //    `0x006D9310` through its OWN direct-relaunch bypass branch, the one
    //    `restartPhases` below demonstrates, even on a genuine fresh start -
    //    not only on an actual restart as `restartPhases`'s own
    //    `isRestart=true` argument demonstrates it for here. Which of
    //    `isRestart=true` or `hasName=false` is the real reason mission 1
    //    takes that branch on a fresh start is OPEN, not narrowed by this
    //    finding - not asserted either way.
    // ========================================================================
    {
        Host host({});
        lua_State* L = host.gameplayState();
        EngineState& es = host.engineState();
        BareThreadTable& threads = host.bareGlobals().threads();

        ok(host, L,
           "function m01_start(cp, restart)\n"
           "  m01_started = true\n"
           "  m01_started_cp = cp\n"
           "  m01_started_restart = restart\n"
           "end\n");

        MissionPhaseMachine phases(es, L, threads);
        CHECK(phases.phase() == MissionPhase::Idle);
        CHECK(es.missionContext().activeAndRunning.known() == false ||
              !es.missionContext().activeAndRunning.get());

        // Exercises the GENERIC marker-arm branch of the still-OPEN
        // zone/level-start link - a stand-in for whichever ordinary story
        // mission a future zone/level-start binder reaches this way, using
        // "m01" per this file's own top-comment citation. NOT a claim that
        // the real mission 1 itself is bound this way on a fresh start -
        // see this block's own updated header comment above for why that is
        // now HIGH CONFIDENCE wrong for mission 1 specifically. Fresh start:
        // isRestart=false, hasName=true.
        phases.bind("m01", /*isRestart=*/false, /*hasName=*/true);
        CHECK(phases.phase() == MissionPhase::WaitingZoneStreaming);  // phase 2
        CHECK(phases.marker().armed);
        CHECK(phases.marker().pendingMissionStem == "m01");
        CHECK(phases.marker().checkpointName == "mission start");
        CHECK(phases.marker().triggerCount == 0);

        // Armed, but not yet reached: the marker's own trigger body has not
        // run, so the mission's `_start` must still be untouched.
        CHECK(globalIsNil(L, "m01_started"));

        // The organic "player reaches the marker" event - the SAME call
        // shape `0x006d5460` itself has (Sec36.2's own table row).
        LuaCoroutineLaunchResult start = phases.triggerMarker();
        CHECK(start.existedAsFunction);
        CHECK(start.attemptedCall);
        CHECK(start.callOk);
        CHECK(!start.suspended);  // the fixture body never yields

        CHECK(phases.phase() == MissionPhase::MarkerArmedCheckpointIcon);  // phase 5
        CHECK(!phases.marker().armed);                                     // consumed
        CHECK(phases.marker().triggerCount == 1);

        // The REAL, measured call-shape evidence (Sec36.2/36.4): checkpoint
        // is the literal "mission start", is_restart is false - never the
        // number 0 the real dlc2_m02 bug shape used.
        CHECK(globalAsBool(L, "m01_started"));
        CHECK(globalAsString(L, "m01_started_cp") == "mission start");
        CHECK(!globalAsBool(L, "m01_started_restart"));

        // The already-landed mission-phase-completion facade
        // (EngineState::beginMissionPhaseTracking()) was reached through the
        // REAL marker path, not bypassed - this project's own "a mission
        // has begun" signal is now true.
        CHECK(es.missionContext().activeAndRunning.get());
        CHECK(es.inProgressType().activeMissionPresent.get());

        // triggerMarker() on an un-armed marker is a caller-ordering error,
        // not a silently-accepted no-op.
        bool threw = false;
        try {
            phases.triggerMarker();
        } catch (const std::logic_error&) {
            threw = true;
        }
        CHECK(threw);

        // The `0x006D9310` "restart or no name" branch: skips phase 2
        // entirely, relaunches directly with the caller-supplied checkpoint.
        // Tested here via an actual restart (isRestart=true), but per this
        // block's own updated header comment above, this is also HIGH
        // CONFIDENCE the branch the real mission 1 takes even on its
        // genuine fresh start (OPEN: via this same isRestart path, or via
        // hasName=false instead - not narrowed).
        MissionPhaseMachine restartPhases(es, L, threads);
        restartPhases.bind("m01", /*isRestart=*/true, /*hasName=*/true);
        CHECK(!restartPhases.marker().armed);
        CHECK(restartPhases.phase() == MissionPhase::Beginning);  // phase 2 skipped
        LuaCoroutineLaunchResult resumed = restartPhases.relaunchDirect("2", /*isRestart=*/true);
        CHECK(resumed.callOk);
        CHECK(globalAsString(L, "m01_started_cp") == "2");
        CHECK(globalAsBool(L, "m01_started_restart"));
    }

    // ========================================================================
    // 4. Horde start: sets the shared flag, tails into the SAME routine.
    // ========================================================================
    {
        Host host({});
        lua_State* L = host.gameplayState();
        EngineState& es = host.engineState();
        BareThreadTable& threads = host.bareGlobals().threads();
        GameModeTable modes;
        MainMenuFlow flow(es, L, threads, modes);

        CHECK(!flow.hordeFlag());
        MainMenuFlow::NewGameResult r = flow.hordeStart(/*networkModuleUp=*/false, /*currentUiScreenCode=*/0);
        CHECK(flow.hordeFlag());
        CHECK(r.pendingZoneName == "sr3_city");  // the IDENTICAL shared routine
        CHECK(r.levelTransitionGateOpened);
    }

    // ========================================================================
    // 5. Continue/Load (Sec3): no slot, a real successful load (reusing
    //    save_game_bridge.h's own real writer to build a genuine file), and
    //    a load failure falling through to the else-arm's mode-3 push.
    // ========================================================================
    {
        Host host({});
        lua_State* L = host.gameplayState();
        EngineState& es = host.engineState();
        BareThreadTable& threads = host.bareGlobals().threads();
        GameModeTable modes;
        MainMenuFlow flow(es, L, threads, modes);

        // No candidates at all: the unavailable-dialog branch (Sec3 step 4).
        MainMenuFlow::ContinueResult none = flow.continueGame({}, /*currentUiScreenCode=*/0xb);
        CHECK(!none.slotAvailable);
        CHECK(!none.loadOk);

        // Every candidate load-error-flagged: same branch, by the real
        // `+0x5c`/`+0x5d` skip rule (Sec3 step 3), not just an empty list.
        MainMenuFlow::ContinueResult allErrored =
            flow.continueGame({{"/nonexistent/path.sr3s_pc", 100, /*loadError=*/true}}, 0xb);
        CHECK(!allErrored.slotAvailable);

        // A real save file via save_game_bridge.h's own already-real writer
        // (reused, not duplicated) - picked as the highest-lastPlayed,
        // non-error slot among several candidates.
        fs::path sandbox = makeSandboxDir("continue");
        es.saveGameDirectory() = sandbox.string();
        es.saveGame().respectTotal = 12345;
        es.saveGame().playerLevel = 7;
        es.playerCash() = 42.0;
        std::string outPath, writeErr;
        CHECK(writeAutosaveSnapshot(es, outPath, writeErr));
        CHECK(writeErr.empty());

        // Mutate live state so the load-back is a real, observable change.
        es.saveGame().respectTotal = 0;
        es.saveGame().playerLevel = 0;
        es.playerCash() = 0.0;

        ok(host, L, "function save_load_lock_input(locked) save_load_lock_input_arg = locked end");

        std::vector<MainMenuFlow::SaveSlotCandidate> candidates = {
            {"/nonexistent/older.sr3s_pc", 10, /*loadError=*/false},
            {outPath, 999, /*loadError=*/false},  // highest lastPlayed, no error -> picked
            {"/nonexistent/errored.sr3s_pc", 5000, /*loadError=*/true},  // highest raw number, but errored -> skipped
        };
        MainMenuFlow::ContinueResult loaded = flow.continueGame(candidates, /*currentUiScreenCode=*/0xb);
        CHECK(loaded.slotAvailable);
        CHECK(loaded.selectedPath == outPath);
        CHECK(loaded.loadOk);
        CHECK(loaded.loadError.empty());
        CHECK(loaded.fadeOutRequested);
        CHECK(es.saveGame().respectTotal == 12345);
        CHECK(es.saveGame().playerLevel == 7);
        CHECK(globalAsBool(L, "save_load_lock_input_arg"));  // screen 0xb -> save_load_lock_input(true)

        // A selected slot whose file genuinely fails to parse: the else-arm
        // (Sec3 step 8) - clears the shared flag, pushes mode 3.
        fs::path garbage = sandbox / "garbage.sr3s_pc";
        {
            std::ofstream g(garbage, std::ios::binary);
            g << "not a real save file";
        }
        flow.newGame(false, 0);  // sets transitionRequestedFlagB_ true first, so clearing it is observable
        CHECK(flow.pendingTransitionFlagB());
        MainMenuFlow::ContinueResult failed =
            flow.continueGame({{garbage.string(), 1, /*loadError=*/false}}, /*currentUiScreenCode=*/0xb);
        CHECK(failed.slotAvailable);
        CHECK(!failed.loadOk);
        CHECK(failed.pushedLoadingScreenMode);
        CHECK(!flow.pendingTransitionFlagB());
        CHECK(modes.isOnStack(game_mode::kLoadingScreenHypothesis));
    }

    // ========================================================================
    // 6. Fade-completion teardown pause-depth wiring (Sec4/Sec37.3): an
    //    open class-bit-0x8 dialog pushes pause reason 0xd (and exempts the
    //    gameplay state from the scheduler); closing it pops/unexempts.
    // ========================================================================
    {
        Host host({});
        lua_State* L = host.gameplayState();
        EngineState& es = host.engineState();
        BareThreadTable& threads = host.bareGlobals().threads();
        es.attachGamePauseScheduler(&threads, L);

        CHECK(!threads.isExempt(L));
        es.fadeTeardownDialogState().anyOpenClassBit0x8Dialog = true;
        es.screenFadeRequest(true, -1, &FadeCompletionTeardown, 1);
        es.screenFadeHostFrame(1);  // completes via the host-side fallback
        CHECK(es.gamePauseDepth().depth == 1);
        CHECK(es.gamePauseDepth().lastReason == 0xd);
        CHECK(threads.isExempt(L));

        es.fadeTeardownDialogState().anyOpenClassBit0x8Dialog = false;
        es.screenFadeRequest(false, -1, &FadeCompletionTeardown, 1);
        es.screenFadeHostFrame(1);
        CHECK(es.gamePauseDepth().depth == 0);
        CHECK(!threads.isExempt(L));
    }

    if (g_failures == 0) {
        std::cout << "synthetic_menu_runtime_test: all checks passed.\n";
        return 0;
    }
    std::cerr << "synthetic_menu_runtime_test: " << g_failures << " check(s) failed.\n";
    return 1;
}
