// Synthetic tests for the zscene lifecycle driver, the cutscene machine and
// the UI resolution state (src/lua_cutscene.cpp, src/lua_vint_resolution.cpp),
// built from spec-lua-api-behaviour.md Sec26.25 / Sec26.26 (2026-10-01 text
// through job nnlt). Expected values are the spec's statements and worked
// examples; the engine-state values a test sets are the OPEN inputs the host
// has no source for (handle classes, the soundtrack's end, the cutscene state).
//
// Mutation checks (applied by hand to the implementation, each made at least
// one CHECK below fail; reverted afterwards):
//   M1 promote synchronously inside zscene_prep          (testBarePrepPromotesNextFrame)
//   M2 completion without the soundtrack condition       (testBarePrepPromotesNextFrame, testFiveSeconds)
//   M3 5 s compared with >= instead of refusing equality  (testFiveSeconds)
//   M4 class 5 ignored instead of tearing down            (testFailedLoad)
//   M5 reset-check not re-queueing with 0x0153b542        (testRequeue)
//   M6 state 1 -> 4 instead of 1 -> 3                     (testMachineStates)
//   M7 5 -> 6 for a loaded zscene                         (testMachineStates)
//   M8 decimal 0.075 / 0.925 as the safe-frame constants  (testSafeFrame)
//   M9 ladder threshold >= 2.48 instead of > 2.48          (testVintInit)
//   M10 0x10..0x13 implemented instead of refused          (testMachineStates)
//   M11 reset-check skipped when nothing is pending        (testResetCheckWithNothingPending,
//       testIdleDriver) - the host's behaviour before the 2026-10-01 continuation

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"

using namespace sr3luahost;

namespace {

int g_checks = 0;
int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        ++g_checks;                                                                           \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

std::vector<RegisteredName> names() {
    return {
        {"zscene_prep", "gameplay"},          {"zscene_is_loaded", "gameplay"},
        {"vint_is_std_res", "ui"},            {"vint_get_safe_frame", "ui"},
        {"Screen_fade_transition_complete", "ui"},
    };
}

struct Fixture {
    Host host{names()};
    EngineState& es = host.engineState();
    lua_State* gp = host.gameplayState();
    lua_State* ui = host.uiState();

    void ok(lua_State* L, const std::string& code) {
        auto r = host.runChunk(L, code, "cutscene_test.lua");
        CHECK(r.loadOk && r.pcallOk);
        if (!(r.loadOk && r.pcallOk)) std::cerr << "  chunk: " << code << "\n  error: " << r.pcallError << "\n";
    }
    void refuses(lua_State* L, const std::string& code, const std::string& needle) {
        auto r = host.runChunk(L, code, "cutscene_ref.lua");
        const bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
        CHECK(good);
        if (!good) std::cerr << "  chunk: " << code << "\n  want '" << needle << "', got: " << r.pcallError << "\n";
    }
    // One host tick: the fade frame advances the clock, then the cutscene frame.
    void tick(int64_t ms) {
        es.screenFadeHostFrame(ms);
        es.cutsceneHostFrame();
    }
    // A table with one zscene, one story cutscene and one entry of OPEN kind.
    void installTable() {
        std::vector<EngineState::ZsceneTableRow> rows(3);
        rows[0].name = "Z_A";
        rows[0].crc = sr3save::nameHash("Z_A");
        rows[0].kind = 1;
        rows[0].resources = std::vector<std::string>{};
        rows[1].name = "story_b";
        rows[1].crc = sr3save::nameHash("story_b");
        rows[1].kind = 2;
        rows[1].resources = std::vector<std::string>{"npc_b", "veh_variant_b"};
        rows[2].name = "open_c";
        rows[2].crc = sr3save::nameHash("open_c");
        es.installZsceneTable(std::move(rows));
    }
    // The idle start a test chooses (none of these has a specced start-up value).
    void idleZscene() {
        es.zsceneSkipAllCutscenes().set(false);
        es.zsceneCurrent().set("");
        es.zscenePending().set("");
        es.zsceneStateCode().set(0);
        es.zsceneAutoSelectNearest().set(false);
        es.zsceneRequeueOnReset().set(false);
        es.zsceneSoundtrackActive().set(false);
        es.cutsceneState().set(0);
    }
    const std::string& blocker() const { return es.cutsceneCounters().lastFrameBlocker; }
    bool blocked() const { return es.cutsceneCounters().lastFrameBlocked; }
};

void testTableResolution() {
    Fixture f;
    f.installTable();
    CHECK(f.es.zsceneTableInstalled() && f.es.zsceneTableSize() == 3);
    // Missing from the installed table -> nothing to load: true, without
    // reading the skip byte.
    f.ok(f.gp, "assert(zscene_is_loaded('not_in_table') == true)");
    f.ok(f.gp, "assert(zscene_is_loaded('STORY_B') == true)"); // kind 2: not loadable
    // kind 1: the skip byte next - false at start (Sec26.25 Globals,
    // 2026-10-01, CONFIRMED) - then the current entry, null at start
    // (Sec26.25 Globals, RESOLVED 2026-10-02, CONFIRMED): not current -> false.
    CHECK(f.es.zsceneSkipAllCutscenes().known() && !f.es.zsceneSkipAllCutscenes().get());
    CHECK(f.es.zsceneCurrent().known() && f.es.zsceneCurrent().get().empty());
    f.ok(f.gp, "assert(zscene_is_loaded('z_a') == false)");
    f.refuses(f.gp, "zscene_is_loaded('open_c')", "kind (+0x8)");
    // zscene_prep on a missing or kind-2 name: the gate returns, nothing read.
    f.ok(f.gp, "zscene_prep('story_b'); zscene_prep('nope')");
    CHECK(!f.es.zscenePending().known());
    // The CRC is the lower-cased one (resolution through 0x00721be0).
    f.es.zsceneSkipAllCutscenes().set(true);
    f.ok(f.gp, "assert(zscene_is_loaded('z_A') == true)");
}

void testBarePrepPromotesNextFrame() {
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.zsceneHandleClass().set("z_a", 0); // loading: neither resident nor failed
    f.ok(f.gp, "zscene_prep('Z_A')");
    // Not synchronous: the prep only parks the entry (Sec26.25 lifecycle 1).
    CHECK(f.es.zscenePending().get() == "z_a" && f.es.zsceneCurrent().get().empty() && f.es.zsceneStateCode().get() == 0);
    f.ok(f.gp, "assert(zscene_is_loaded('z_a') == false)");
    // Next frame: 0x007258a0 (cutscene state 0) runs 0x00720320 (no current ->
    // true, 0x0153b541 := 1) and promotes: current, pending cleared, state 1,
    // the soundtrack stream started at the frame's clock.
    f.tick(100);
    CHECK(!f.blocked());
    CHECK(f.es.zsceneCurrent().get() == "z_a" && f.es.zscenePending().get().empty() && f.es.zsceneStateCode().get() == 1);
    CHECK(f.es.zsceneAutoSelectNearest().get() == true);
    CHECK(f.es.zsceneSoundtrackActive().get() && f.es.zsceneSoundtrackStartMs().get() == 100);
    CHECK(f.es.cutsceneCounters().zscenePromotions == 1 && f.es.cutsceneCounters().zsceneCompletions == 0);
    f.ok(f.gp, "assert(zscene_is_loaded('z_a') == false and zscene_is_loaded() == false)");
    // Resident, but the soundtrack's status is OPEN (no audio) and 5 s have
    // not passed: the completion refuses on it.
    f.es.zsceneHandleClass().set("z_a", 3);
    f.tick(100);
    CHECK(f.blocked() && f.blocker().find("0x66") != std::string::npos);
    CHECK(f.es.zsceneStateCode().get() == 1);
    // The soundtrack has ended: state 2.
    f.es.zsceneSoundtrackEnded().set(true);
    f.tick(100);
    CHECK(!f.blocked() && f.es.zsceneStateCode().get() == 2);
    CHECK(f.es.cutsceneCounters().zsceneCompletions == 1);
    f.ok(f.gp, "assert(zscene_is_loaded('Z_A') == true and zscene_is_loaded() == true)");
    // Load state 2: nothing more happens.
    f.tick(100);
    CHECK(!f.blocked() && f.es.zsceneStateCode().get() == 2 && f.es.cutsceneCounters().zsceneCompletions == 1);
    // Still waiting (class other than 3/5) keeps state 1.
    Fixture g;
    g.installTable();
    g.idleZscene();
    g.es.zsceneHandleClass().set("z_a", 2);
    g.ok(g.gp, "zscene_prep('z_a')");
    g.tick(10);
    g.tick(10);
    CHECK(!g.blocked() && g.es.zsceneStateCode().get() == 1);
}

void testFiveSeconds() {
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.zsceneHandleClass().set("z_a", 3);
    f.ok(f.gp, "zscene_prep('z_a')");
    f.tick(100); // promoted at t = 100; the completion refuses on the status
    CHECK(f.es.zsceneStateCode().get() == 1 && f.blocked());
    f.tick(4999); // t = 5099: 4999 ms elapsed, status still needed
    CHECK(f.blocked() && f.es.zsceneStateCode().get() == 1);
    f.tick(1); // exactly 5 s: "or 5 s pass" does not fix the equality case
    CHECK(f.blocked() && f.blocker().find("exact equality") != std::string::npos);
    f.tick(1); // 5001 ms: passed, the status is not needed
    CHECK(!f.blocked() && f.es.zsceneStateCode().get() == 2);
    CHECK(!f.es.zsceneSoundtrackEnded().known());
}

void testFailedLoad() {
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.zsceneHandleClass().set("z_a", 5);
    f.ok(f.gp, "zscene_prep('z_a')");
    f.tick(10); // promoted; then class 5 -> teardown 0x00721c20(1, 0, 0), live path
    CHECK(!f.blocked());
    CHECK(f.es.cutsceneCounters().zsceneFailedLoads == 1);
    CHECK(f.es.zsceneStateCode().get() == 0 && f.es.zsceneCurrent().get() == "z_a"); // current left as it was
    CHECK(f.es.zsceneAutoSelectNearest().get() == false && f.es.zsceneRequeueOnReset().get() == false);
    CHECK(!f.es.zsceneHandleClass().known("z_a"));   // released handle: class OPEN
    CHECK(!f.es.zsceneSoundtrackActive().known());   // 0x007317a0(1): stream state OPEN
    // Next frame: load state 0, 0x0153b541 clear -> the reset 0x00720320 reads
    // the released handle's class: OPEN (the spec's own open item).
    f.tick(10);
    CHECK(f.blocked() && f.blocker().find("handle class") != std::string::npos);
}

void testPromotionWaitsForPreviousStream() {
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.zsceneHandleClass().set("z_a", 0);
    f.es.zsceneSoundtrackActive().set(true);   // a previous stream still running
    f.es.zsceneSoundtrackStartMs().set(0);
    f.es.zsceneSoundtrackEnded().set(false);
    // The reset-check sets 0x0153b541, so the idle driver of the same frame
    // asks for the nearest world object: none.
    f.es.zsceneNearestWorldObjectScene().set("");
    f.ok(f.gp, "zscene_prep('z_a')");
    f.tick(1000);
    CHECK(!f.blocked() && f.es.zscenePending().get() == "z_a" && f.es.zsceneStateCode().get() == 0);
    CHECK(f.es.zsceneAutoSelectNearest().get() == true); // the reset-check itself ran
    CHECK(f.es.cutsceneCounters().zscenePromotions == 0);
    f.es.zsceneSoundtrackEnded().set(true);
    f.tick(10);
    CHECK(f.es.zsceneCurrent().get() == "z_a" && f.es.zsceneStateCode().get() == 1);
    CHECK(f.es.cutsceneCounters().zscenePromotions == 1);
    // OPEN previous stream (no specced start-up value): refused, nothing written.
    Fixture g;
    g.installTable();
    g.idleZscene();
    g.es.zsceneSoundtrackActive().forget();
    g.es.zsceneHandleClass().set("z_a", 0);
    g.ok(g.gp, "zscene_prep('z_a')");
    g.tick(10);
    CHECK(g.blocked() && g.blocker().find("0x0153b71c") != std::string::npos);
    CHECK(g.es.zscenePending().get() == "z_a" && g.es.zsceneAutoSelectNearest().get() == false);
}

void testRequeue() {
    // Reset-check with a current entry whose handle went not-live (class 1):
    // state := 0, current := 0 and, with 0x0153b542 set, the current entry is
    // re-queued into the pending slot - and that is what gets promoted.
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.zsceneCurrent().set("story_b");
    f.es.zsceneStateCode().set(2);
    f.es.zsceneHandleClass().set("story_b", 1);
    f.es.zscenePending().set("z_a");
    f.es.zsceneRequeueOnReset().set(true);
    f.tick(10);
    CHECK(!f.blocked());
    CHECK(f.es.zsceneCurrent().get() == "story_b" && f.es.zsceneStateCode().get() == 1);
    CHECK(f.es.zscenePending().get().empty());
    // Without 0x0153b542 the Lua-pending entry is promoted.
    Fixture g;
    g.installTable();
    g.idleZscene();
    g.es.zsceneCurrent().set("story_b");
    g.es.zsceneStateCode().set(2);
    g.es.zsceneHandleClass().set("story_b", 1);
    g.es.zsceneHandleClass().set("z_a", 0);
    g.es.zscenePending().set("z_a");
    g.tick(10);
    CHECK(g.es.zsceneCurrent().get() == "z_a" && g.es.zsceneStateCode().get() == 1);
    // Live current handle and 0x0153b541 clear: the reset-check is false, no promotion.
    Fixture h;
    h.installTable();
    h.idleZscene();
    h.es.zsceneCurrent().set("story_b");
    h.es.zsceneStateCode().set(2);
    h.es.zsceneHandleClass().set("story_b", 3);
    h.es.zscenePending().set("z_a");
    h.tick(10);
    CHECK(!h.blocked() && h.es.zscenePending().get() == "z_a" && h.es.zsceneCurrent().get() == "story_b");
}

void testIdleDriver() {
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.zsceneAutoSelectNearest().set(true);
    // Load state 0 with 0x0153b541 set: the nearest scene-bearing world object
    // (no world objects in this host): OPEN.
    f.tick(10);
    CHECK(f.blocked() && f.blocker().find("0x03171a64") != std::string::npos);
    f.es.zsceneNearestWorldObjectScene().set("");
    f.tick(10);
    CHECK(!f.blocked() && f.es.zscenePending().get().empty());
    // An object whose scene is z_a: prepped through the gate.
    f.es.zsceneNearestWorldObjectScene().set("z_a");
    f.tick(10);
    CHECK(!f.blocked() && f.es.zscenePending().get() == "z_a" && f.es.cutsceneCounters().zsceneIdleAutoPreps == 1);
    // 0x0153b541 clear: the idle driver's own reset 0x00720320 runs (no
    // current -> 541 := 1). Cutscene state 3 (checks failing) so that
    // 0x007258a0's reset-check (states 0 and 2 only) does not set it first.
    Fixture g;
    g.installTable();
    g.idleZscene();
    g.es.cutsceneState().set(3);
    g.es.cutscenePlayerChecksPass().set(false);
    g.es.cutsceneManager().present.set(false); // 0x007258a0's in-progress byte reads it in state >= 2
    g.tick(10);
    CHECK(!g.blocked() && g.es.zsceneAutoSelectNearest().get() == true && g.es.cutsceneState().get() == 3);
    // Cutscene state 0, nothing pending: 0x007258a0's reset-check still runs
    // (no current -> 541 := 1), so the idle driver of the same frame already
    // takes the nearest-world-object branch - OPEN in this host.
    Fixture h;
    h.installTable();
    h.idleZscene();
    h.tick(10);
    CHECK(h.blocked() && h.blocker().find("0x03171a64") != std::string::npos);
    CHECK(h.es.zsceneAutoSelectNearest().get() == true && h.es.cutsceneCounters().zscenePromotions == 0);
}

void testResetCheckWithNothingPending() {
    // 0x007258a0 calls the reset-check 0x00720320 in cutscene states 0 and 2
    // whether or not an entry is pending (Sec26.25 lifecycle 3). A scene whose
    // selected handle goes not-live (class 1) while it loads, with nothing
    // pending: state := 0, current := 0, and with 0x0153b542 set the entry is
    // re-queued - and promoted again in the same call, since the reset-check
    // returned true.
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.zsceneCurrent().set("z_a");
    f.es.zsceneStateCode().set(1);
    f.es.zsceneHandleClass().set("z_a", 1);
    f.es.zsceneRequeueOnReset().set(true);
    f.tick(10);
    CHECK(!f.blocked());
    CHECK(f.es.zsceneCurrent().get() == "z_a" && f.es.zscenePending().get().empty() && f.es.zsceneStateCode().get() == 1);
    CHECK(f.es.cutsceneCounters().zscenePromotions == 1);
    CHECK(f.es.zsceneSoundtrackActive().get() && f.es.zsceneSoundtrackStartMs().get() == 10);
    // Without 0x0153b542: reset, nothing re-queued, nothing promoted; the
    // idle driver of the same frame (load state 0, 0x0153b541 clear) runs
    // its own reset: no current -> 541 := 1.
    Fixture g;
    g.installTable();
    g.idleZscene();
    g.es.zsceneCurrent().set("z_a");
    g.es.zsceneStateCode().set(1);
    g.es.zsceneHandleClass().set("z_a", 1);
    g.tick(10);
    CHECK(!g.blocked());
    CHECK(g.es.zsceneCurrent().get().empty() && g.es.zscenePending().get().empty() && g.es.zsceneStateCode().get() == 0);
    CHECK(g.es.cutsceneCounters().zscenePromotions == 0 && g.es.zsceneAutoSelectNearest().get() == true);
    // Outside states 0 / 2 the reset-check is not run by 0x007258a0: the
    // loading entry with a not-live handle just waits in 0x007285c0.
    Fixture h;
    h.installTable();
    h.idleZscene();
    h.es.cutsceneState().set(3);
    h.es.cutscenePlayerChecksPass().set(false);
    h.es.cutsceneManager().present.set(false);
    h.es.zsceneCurrent().set("z_a");
    h.es.zsceneStateCode().set(1);
    h.es.zsceneHandleClass().set("z_a", 1);
    h.tick(10);
    CHECK(!h.blocked() && h.es.zsceneCurrent().get() == "z_a" && h.es.zsceneStateCode().get() == 1);
    // A re-queue whose previous stream is still running: the reset is
    // applied, the promotion waits (pending holds the re-queued entry).
    Fixture k;
    k.installTable();
    k.idleZscene();
    k.es.zsceneCurrent().set("z_a");
    k.es.zsceneStateCode().set(1);
    k.es.zsceneHandleClass().set("z_a", 1);
    k.es.zsceneRequeueOnReset().set(true);
    k.es.zsceneSoundtrackActive().set(true);
    k.es.zsceneSoundtrackStartMs().set(0);
    k.es.zsceneSoundtrackEnded().set(false);
    k.es.zsceneNearestWorldObjectScene().set(""); // idle driver of the same frame (541 set next frame)
    k.tick(10);
    CHECK(!k.blocked() && k.es.zscenePending().get() == "z_a" && k.es.zsceneCurrent().get().empty());
    CHECK(k.es.zsceneStateCode().get() == 0 && k.es.cutsceneCounters().zscenePromotions == 0);
}

void testCutsceneStateTwo() {
    // A cutscene_play-style path: the machine sits in state 2 ("waits for the
    // zscene to load, then 0x00725df0 continues") while the per-frame
    // promotion and completion load the scene; then the continuation, whose
    // body is not in the spec, refuses.
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.cutsceneState().set(2);
    f.es.cutsceneManager().present.set(true);
    f.es.cutsceneManager().sceneKey.set("z_a");
    f.es.zsceneHandleClass().set("z_a", 3);
    f.ok(f.gp, "zscene_prep('z_a')");
    f.es.zsceneSoundtrackEnded().set(true); // forgotten again by the promotion
    f.tick(10);
    CHECK(f.es.zsceneStateCode().get() == 1 && f.blocked()); // promoted; status of the new stream OPEN
    CHECK(f.es.cutscenePlayingByte().get() == false && f.es.cutsceneInProgressByte().get() == true);
    f.es.zsceneSoundtrackEnded().set(true);
    f.tick(10);
    CHECK(!f.blocked() && f.es.zsceneStateCode().get() == 2 && f.es.cutsceneState().get() == 2);
    f.tick(10);
    CHECK(f.blocked() && f.blocker().find("0x00725df0") != std::string::npos);
    CHECK(f.es.cutsceneState().get() == 2);
    // The scene itself is loaded (current, state 2), whatever the machine does next.
    f.ok(f.gp, "assert(zscene_is_loaded('z_a') == true)");
    // Idle (state 0) with no manager: in-progress byte false, no manager read.
    // (No world objects: the idle driver's nearest-object query answers none.)
    Fixture g;
    g.installTable();
    g.idleZscene();
    g.es.zsceneNearestWorldObjectScene().set("");
    g.tick(10);
    CHECK(!g.blocked() && g.es.cutsceneInProgressByte().get() == false && !g.es.cutsceneManager().present.known());
}

void testMachineStates() {
    // 1 -> 3; 3 -> 4 once the player-side checks pass (OPEN predicate).
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.cutsceneManager().present.set(true);
    f.es.cutsceneManager().sceneKey.set("story_b");
    f.es.zsceneNearestWorldObjectScene().set(""); // the idle driver's world query: no objects
    f.es.cutsceneState().set(1);
    f.tick(10);
    CHECK(!f.blocked() && f.es.cutsceneState().get() == 3);
    f.tick(10);
    CHECK(f.blocked() && f.blocker().find("player-side checks") != std::string::npos && f.es.cutsceneState().get() == 3);
    f.es.cutscenePlayerChecksPass().set(false);
    f.tick(10);
    CHECK(!f.blocked() && f.es.cutsceneState().get() == 3);
    f.es.cutscenePlayerChecksPass().set(true);
    // 3 -> 4, and 0x007258a0's case 4 in the same frame: the two preload packfiles, 0x0153b543.
    f.tick(10);
    CHECK(!f.blocked() && f.es.cutsceneState().get() == 4);
    CHECK(f.es.cutscenePreloadMounted().get() == true);
    CHECK(f.es.cutsceneMountLog() == (std::vector<std::string>{"preload_items.vpp", "preload_effects.vpp"}));
    // 4 -> 5, then case 5: not a loaded zscene -> 0x00722f10 loads -> 6.
    const int64_t before = f.es.screenFadeClockMs();
    f.tick(10);
    CHECK(f.es.cutsceneState().get() == 6);
    CHECK(f.es.cutsceneLoadLog() ==
          (std::vector<std::string>{"npc_basehead", "cutscene manager handle", "npc_b", "veh_variant_b", "+0xf4 soundtrack"}));
    CHECK(f.es.cutsceneLoadStamp().get() == before + 10 + 120000);
    // 0x00722f10 writes 0x0153b530 / 0x0153b51c / 0x0153b541 / 0x0153b542
    // with values the spec does not give: OPEN, and the completion step of
    // the same frame stops on the load state.
    CHECK(!f.es.zsceneCurrent().known() && !f.es.zsceneStateCode().known());
    CHECK(f.blocked() && f.blocker().find("0x0153b51c") != std::string::npos);
    // 6: the loading helper's body is OPEN.
    f.tick(10);
    CHECK(f.blocked() && f.blocker().find("0x0072c790") != std::string::npos && f.es.cutsceneState().get() == 6);

    // 5 -> 7 for a loaded zscene.
    Fixture g;
    g.installTable();
    g.idleZscene();
    g.es.cutsceneManager().present.set(true);
    g.es.cutsceneManager().sceneKey.set("z_a");
    g.es.zsceneCurrent().set("z_a");
    g.es.zsceneStateCode().set(2);
    g.es.cutsceneState().set(5);
    g.tick(10);
    CHECK(!g.blocked() && g.es.cutsceneState().get() == 7 && g.es.cutsceneLoadLog().empty());

    // Every state whose body is OPEN refuses, naming it; 0x10..0x13 are
    // HIGH CONFIDENCE only; 20+ is outside the jump table.
    const std::vector<std::pair<int, std::string>> refusals = {
        {6, "0x0072c790"}, {7, "0x0072bff0"}, {8, "0x0072c260"},  {9, "0x00729150"},  {10, "0x0072c980"},
        {11, "0x0072c980"}, {12, "0x0072c980"}, {14, "0x00722e00"}, {16, "HIGH CONFIDENCE"}, {17, "HIGH CONFIDENCE"},
        {18, "HIGH CONFIDENCE"}, {19, "HIGH CONFIDENCE"}, {20, "jump table"}, {-1, "jump table"}};
    for (const auto& [state, needle] : refusals) {
        Fixture h;
        h.idleZscene();
        h.es.cutsceneState().set(state);
        h.tick(10);
        CHECK(h.blocked() && h.blocker().find(needle) != std::string::npos && h.es.cutsceneState().get() == state);
    }
    // 13: the chain target copy happens, then the transition refuses.
    {
        Fixture h;
        h.idleZscene();
        h.es.cutsceneManager().present.set(true);
        h.es.cutsceneManager().chainTarget.set("next_scene");
        h.es.cutsceneState().set(13);
        h.tick(10);
        CHECK(h.blocked() && h.blocker().find("writer of state 14") != std::string::npos);
        CHECK(h.es.cutsceneChainTarget().get() == "next_scene" && h.es.cutsceneState().get() == 13);
    }
    // 15: no chain target -> teardown 0x10; a chain target -> 5, manager rebuilt (fields OPEN).
    {
        Fixture h;
        h.idleZscene();
        h.es.cutsceneManager().present.set(true);
        h.es.cutsceneChainTarget().set("");
        h.es.cutsceneState().set(15);
        h.tick(10);
        CHECK(h.es.cutsceneState().get() == 0x10);
        Fixture k;
        k.idleZscene();
        k.es.cutsceneManager().present.set(true);
        k.es.cutsceneManager().sceneKey.set("z_a");
        k.es.cutsceneChainTarget().set("story_b");
        k.es.cutsceneState().set(15);
        k.tick(10);
        CHECK(k.es.cutsceneState().get() == 5 && !k.es.cutsceneManager().sceneKey.known());
    }
    // The cutscene state itself has no specced start-up value.
    Fixture z;
    z.tick(10);
    CHECK(z.blocked() && z.blocker().find("0x0153b520") != std::string::npos);
    CHECK(z.es.cutsceneCounters().framesRun == 1 && z.es.cutsceneCounters().framesBlockedOnOpen == 1);
}

void testTeardownGuard() {
    // The guard is an AND: a known cutscene state outside 7..13 decides it
    // without reading the manager.
    Fixture f;
    f.installTable();
    f.idleZscene();
    f.es.zsceneCurrent().set("story_b");
    f.es.zsceneHandleClass().set("story_b", 3);
    f.ok(f.gp, "zscene_prep('z_a')"); // cutscene state 0, manager OPEN: teardown runs
    CHECK(f.es.zscenePending().get() == "z_a" && f.es.zsceneStateCode().get() == 0);
    // A manager known absent decides it too, whatever the state.
    Fixture g;
    g.installTable();
    g.idleZscene();
    g.es.cutsceneState().forget();
    g.es.cutsceneManager().present.set(false);
    g.es.zsceneCurrent().set("story_b");
    g.es.zsceneStateCode().set(2);
    g.es.zsceneHandleClass().set("story_b", 3);
    g.ok(g.gp, "zscene_prep('z_a')");
    CHECK(g.es.zsceneStateCode().get() == 0);
    // In 7..13 with a manager whose +8 is OPEN: refused, nothing written.
    Fixture h;
    h.installTable();
    h.idleZscene();
    h.es.cutsceneState().set(9);
    h.es.cutsceneManager().present.set(true);
    h.es.zsceneCurrent().set("story_b");
    h.es.zsceneStateCode().set(2);
    h.refuses(h.gp, "zscene_prep('z_a')", "cutscene manager +8");
    CHECK(h.es.zscenePending().get().empty() && h.es.zsceneStateCode().get() == 2);
    h.es.cutsceneManager().field8.set(0);
    h.es.zsceneHandleClass().set("story_b", 3);
    h.ok(h.gp, "zscene_prep('z_a')");
    CHECK(h.es.zscenePending().get() == "z_a" && h.es.zsceneStateCode().get() == 0);
}

void testVintInit() {
    Fixture f;
    f.refuses(f.ui, "vint_is_std_res()", "UI init 0x00e23910 not run");
    // 1280 x 720 (a single display, ratio 1.78 <= 2.48): mode -1 from the
    // CONFIRMED single threshold alone; wide.
    f.es.vintUiSubsystemInit(1280, 720);
    CHECK(f.es.vintGlobalWidth().get() == 1280 && f.es.vintGlobalHeight().get() == 720);
    CHECK(f.es.vintRecordFirst().get() == 1280 && f.es.vintRecordSecond().get() == 720);
    CHECK(f.es.vintDisplayMode().get() == -1 && f.es.vintLayoutIndex().get() == 0);
    f.ok(f.ui, "assert(vint_is_std_res() == false)");
    f.es.vintUiSubsystemInit(1024, 768);
    CHECK(f.es.vintDisplayMode().get() == -1 && f.es.vintLayoutIndex().get() == 1);
    f.ok(f.ui, "assert(vint_is_std_res() == true)");
    f.es.vintUiSubsystemInit(1280, 1024);
    f.ok(f.ui, "assert(vint_is_std_res() == true)");
    f.es.vintUiSubsystemInit(1920, 1200);
    f.ok(f.ui, "assert(vint_is_std_res() == false)");
    // The ladder: strict > 2.48 in single precision.
    CHECK(EngineState::vintDisplayModeLadder(2480, 1000) == -1);
    CHECK(EngineState::vintDisplayModeLadder(1920, 1080) == -1);
    CHECK(EngineState::vintDisplayModeLadder(0, 0) == -1);   // NaN fails the compare
    CHECK(EngineState::vintDisplayModeLadder(-5, 1) == -1);
    bool refused = false;
    try {
        EngineState::vintDisplayModeLadder(2481, 1000);      // above 2.48: HIGH CONFIDENCE doubles decide
    } catch (const OpenStateError& e) {
        refused = std::string(e.what()).find("HIGH CONFIDENCE") != std::string::npos;
    }
    CHECK(refused);
    // A three-panel span: the mode is OPEN, and vint_is_std_res needs it.
    f.es.vintUiSubsystemInit(3 * 1280, 1024);
    CHECK(!f.es.vintDisplayMode().known() && !f.es.vintLayoutIndex().known());
    f.refuses(f.ui, "vint_is_std_res()", "0x0132bd80");
    // 0x00e2ad30: < 1.5 or mode 2.
    CHECK(EngineState::vintLayoutIndexFor(1499, 1000, -1) == 1);
    CHECK(EngineState::vintLayoutIndexFor(1500, 1000, -1) == 0);
    CHECK(EngineState::vintLayoutIndexFor(1920, 1080, 2) == 1);
    CHECK(EngineState::vintLayoutIndexFor(1920, 1080, 3) == 0);
}

void testVintResolutionChange() {
    Fixture f;
    bool refused = false;
    try {
        f.es.vintResolutionChange(800, 600); // globals OPEN before the init
    } catch (const OpenStateError&) {
        refused = true;
    }
    CHECK(refused && !f.es.vintRecordFirst().known());
    f.es.vintUiSubsystemInit(1280, 720);
    f.ok(f.ui, "function vint_lib_init_constants() VLIC = (VLIC or 0) + 1 end");
    CHECK(f.es.vintResolutionChange(1280, 720) == false); // unchanged: nothing
    CHECK(f.es.vintLibInitConstantsCalls() == 0);
    CHECK(f.es.vintResolutionChange(1920, 1080) == true); // wide -> wide: no class flip
    CHECK(f.es.vintRecordFirst().get() == 1920 && f.es.vintLibInitConstantsCalls() == 1);
    CHECK(f.es.vintDocumentResetsNotModelled() == 0);
    CHECK(f.es.vintResolutionChange(1024, 768) == true);  // wide -> standard: <doc>_reset() for each document
    CHECK(f.es.vintLayoutIndex().get() == 1 && f.es.vintDocumentResetsNotModelled() == 1);
    f.ok(f.ui, "assert(VLIC == 2)");
}

void testSafeFrame() {
    Fixture f;
    // CONFIRMED constants, set at start: the widened singles.
    CHECK(f.es.vintSafeFrameScale1().get() == EngineState::doubleFromBits(0x3FB3333340000000ull));
    CHECK(f.es.vintSafeFrameScale1().get() == static_cast<double>(0.075f));
    CHECK(f.es.vintSafeFrameScale2().get() == static_cast<double>(0.925f));
    CHECK(f.es.vintSafeFrameScale1().get() != 0.075 && f.es.vintSafeFrameScale2().get() != 0.925);
    f.refuses(f.ui, "vint_get_safe_frame()", "+0x8"); // a, b: no specced writer
    auto frame = [&](int a, int b, const std::string& expect) {
        f.es.vintSafeFrameA().set(a);
        f.es.vintSafeFrameB().set(b);
        f.ok(f.ui, "local a, b, c, d = vint_get_safe_frame()\nlocal s = a .. ',' .. b .. ',' .. c .. ',' .. d\n"
                   "assert(s == '" + expect + "', s)");
    };
    frame(1280, 720, "96,54,1184,666");     // Sec26.26 worked value
    frame(1920, 1080, "144,81,1776,999");   // Sec26.26 worked value
    frame(1440, 900, "108,68,1332,833");    // Sec26.26: the engine's answer where the decimals tie
    frame(0, 0, "0,0,0,0");
    frame(-1280, 720, "-96,54,-1184,666");
    // No tie for any dimension below 2^23 with the widened constants (the
    // spec's statement), checked over a range that covers every display size.
    int ties = 0;
    for (int v = 0; v <= 70000; ++v) {
        for (double c : {f.es.vintSafeFrameScale1().get(), f.es.vintSafeFrameScale2().get()}) {
            const double p = c * v;
            if (p - std::floor(p) == 0.5) ++ties;
        }
    }
    CHECK(ties == 0);
    // The decimals tie at 20 (mod 40): 0.925 x 900 = 832.5 exactly - refused
    // here (mode not stated); half-to-even would give 832, not the engine's 833.
    f.es.vintSafeFrameScale1().set(0.075);
    f.es.vintSafeFrameScale2().set(0.925);
    f.es.vintSafeFrameA().set(1440);
    f.es.vintSafeFrameB().set(900);
    f.refuses(f.ui, "vint_get_safe_frame()", "tie");
    // Out of int32: refused.
    f.es.vintSafeFrameScale1().set(1e10);
    f.refuses(f.ui, "vint_get_safe_frame()", "int32");
}

} // namespace

int main() {
    testTableResolution();
    testBarePrepPromotesNextFrame();
    testFiveSeconds();
    testFailedLoad();
    testPromotionWaitsForPreviousStream();
    testRequeue();
    testIdleDriver();
    testResetCheckWithNothingPending();
    testCutsceneStateTwo();
    testMachineStates();
    testTeardownGuard();
    testVintInit();
    testVintResolutionChange();
    testSafeFrame();
    std::cout << "sr3luahost cutscene/zscene/vint: " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
