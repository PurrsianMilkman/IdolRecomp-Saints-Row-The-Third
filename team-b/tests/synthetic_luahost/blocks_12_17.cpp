// Synthetic sr3luahost tests: test_block_12 .. test_block_17 (split out of synthetic_luahost_test.cpp, bodies verbatim).
#include "synthetic_luahost/common.h"

    // --- Screen fade state machine (spec-lua-api-behaviour.md Sec26.24;
    // batch 2026-10-01), through Lua: the real completion path (the UI
    // script calls Screen_fade_transition_complete), the two labelled
    // host-substitute fallbacks, and the per-frame routine.
void test_block_12() {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        lua_State* ui = host.uiState();
        EngineState& es = host.engineState();
        auto& f = es.screenFade();
        auto run = [&](lua_State* st, const std::string& chunk) {
            auto r = host.runChunk(st, chunk, "fade.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };
        const auto& c = es.screenFadeCounters();

        // Start-up: state 2 (fully in), target 2; init ran at registration
        // (document found, flag 1); the UI state is the screen_fade state.
        CHECK(f.state.get() == 2 && f.target.get() == 2 && f.flag.get() == 1 && f.documentLoaded.get());
        CHECK(es.screenFadeUiState() == ui);
        run(gp, "assert(fade_is_fully_faded_in() == true and fade_is_fully_faded_out() == false)");
        run(ui, "assert(sfx_faded_in() == true and sfx_faded_out() == false)");
        CHECK(es.screenFadeCompletionPathSummary() == "real:0 fallback_undefined:0 fallback_no_callback:0");

        // 1. screen_fade_do undefined -> fallback_undefined after durationMs.
        run(gp, "fade_out(1.0)");
        CHECK(f.state.get() == 1 && f.target.get() == 3 && f.flag.get() == 0);
        CHECK(c.screenFadeDoCalls == 0);
        es.screenFadeHostFrame(999);
        CHECK(f.state.get() == 1 && c.fallbackUndefined == 0);
        es.screenFadeHostFrame(1); // clock 1000 = request + 1000 ms
        CHECK(f.state.get() == 3 && c.fallbackUndefined == 1);
        CHECK(f.logoAt.get() == 2000 && f.holdLogoUntil.get() == -1 && f.imagesAt.get() == -1);
        run(gp, "assert(fade_is_fully_faded_out() == true and fade_is_fully_faded_in() == false)");
        run(ui, "assert(sfx_faded_out() == true)");
        // The per-frame routine stops at the OPEN mode-stack top.
        CHECK(c.framesBlockedOnOpen == 2 && c.lastFrameBlocker.find("mode stack") != std::string::npos);
        // Already fully out: a fade-out request changes nothing.
        run(gp, "fade_out(0.5)");
        CHECK(f.state.get() == 3 && f.target.get() == 3);
        es.screenFadeHostFrame(1000);
        CHECK(c.fallbackUndefined == 1);

        // 2. The real path: screen_fade_do(flag, alpha, ms) runs in the UI
        // state; the script's later call of Screen_fade_transition_complete
        // completes the fade.
        run(ui, "fade_log = {}\n"
                "function screen_fade_do(flag, alpha, ms) fade_log[#fade_log + 1] = {flag, alpha, ms} end");
        run(gp, "fade_in(0.25)");
        CHECK(f.state.get() == 0 && f.target.get() == 2 && c.screenFadeDoCalls == 1);
        CHECK(f.logoAt.get() == -1 && f.imagesAt.get() == -1);  // reset by the fade-in request
        run(ui, "assert(#fade_log == 1 and fade_log[1][1] == 0 and fade_log[1][2] == 0.0 and fade_log[1][3] == 250)");
        run(gp, "assert(fade_is_fully_faded_in() == false)");
        run(ui, "assert(select('#', Screen_fade_transition_complete()) == 0)");
        CHECK(f.state.get() == 2 && c.realCompletions == 1);
        run(gp, "assert(fade_is_fully_faded_in() == true)");
        es.screenFadeHostFrame(1000);                        // the fallback was disarmed
        CHECK(c.fallbackNoCallback == 0 && c.fallbackUndefined == 1);
        run(gp, "fade_out(2, {0, 0, 0}, 1)");
        run(ui, "assert(fade_log[2][1] == 0 and fade_log[2][2] == 1.0 and fade_log[2][3] == 2000)");
        CHECK(f.state.get() == 1);
        run(ui, "Screen_fade_transition_complete()");
        CHECK(f.state.get() == 3 && c.realCompletions == 2);
        // Called again with nothing running: a settled state is left alone.
        run(ui, "Screen_fade_transition_complete()");
        CHECK(f.state.get() == 3 && c.realCompletions == 2);
        run(gp, "assert(Screen_fade_transition_complete == nil)"); // UI state only

        // 3. A synchronous call from inside screen_fade_do runs before the
        // request sets the direction, so it completes nothing; the fade is
        // then completed by fallback_no_callback.
        run(ui, "function screen_fade_do(flag, alpha, ms) Screen_fade_transition_complete() end");
        run(gp, "fade_in(0.5)");
        CHECK(f.state.get() == 0 && c.realCompletions == 2);
        es.screenFadeHostFrame(500);
        CHECK(f.state.get() == 2 && c.fallbackNoCallback == 1);

        // 4. An error inside screen_fade_do stays in the UI state.
        run(ui, "function screen_fade_do() error('fade script boom') end");
        run(gp, "fade_out(0.1)");
        CHECK(c.screenFadeDoErrors == 1 && c.lastScreenFadeDoError.find("fade script boom") != std::string::npos);
        CHECK(f.state.get() == 1);
        es.screenFadeHostFrame(100);
        CHECK(f.state.get() == 3 && c.fallbackNoCallback == 2);
        // A screen_fade_do that never returns is stopped by the UI-call watchdog.
        run(ui, "function screen_fade_do() while true do end end");
        run(gp, "fade_in(0.1)");
        CHECK(c.screenFadeDoErrors == 2 && c.lastScreenFadeDoError.find("watchdog") != std::string::npos);
        es.screenFadeHostFrame(100);
        CHECK(f.state.get() == 2 && c.fallbackNoCallback == 3);
        run(ui, "screen_fade_do = nil");

        // 5. A request parked during the opposite transition is replayed by
        // the per-frame routine with 250 ms, once the state has settled and
        // only when the routine is not stopped by OPEN state.
        run(gp, "fade_out(1)");                               // state 1, target 3
        run(gp, "fade_in(3)");                                // fade-out running: parked
        CHECK(f.state.get() == 1 && f.target.get() == 2);
        CHECK(es.screenFadeRequests().back().durationMs == 3000.0);
        es.screenFadeHostFrame(1000);                         // fade-out completes (fallback)
        CHECK(f.state.get() == 3 && f.target.get() == 2);
        es.screenFadeHostFrame(333);                          // routine blocked: mode stack OPEN
        CHECK(f.state.get() == 3);
        f.modeStackTop.set(0);
        f.cutsceneState.set(0);
        es.screenFadeHostFrame(333);                          // logo stamp not reached: replay
        CHECK(f.state.get() == 0 && f.target.get() == 2 && f.logoAt.get() == -1);
        const int64_t replayAt = es.screenFadeClockMs();
        es.screenFadeHostFrame(249);
        CHECK(f.state.get() == 0);
        es.screenFadeHostFrame(1);                            // 250 ms, not the 3000 asked for
        CHECK(f.state.get() == 2 && es.screenFadeClockMs() == replayAt + 250);

        // 6. Mode 4 resets the four stamps and stops the routine.
        run(gp, "fade_out(0)");
        es.screenFadeHostFrame(1);                            // completes at once (0 ms)
        CHECK(f.state.get() == 3 && f.logoAt.get() == es.screenFadeClockMs() + 1000);
        f.modeStackTop.set(4);
        es.screenFadeHostFrame(1);
        CHECK(f.logoAt.get() == -1 && f.holdLogoUntil.get() == -1);
        f.modeStackTop.set(0);

        // 7. Loading logo, holds and load images, with the script hooks
        // defined in the UI state.
        run(ui, "shown = {}\n"
                "function screen_fade_logo_show() shown[#shown + 1] = 'logo' end\n"
                "function screen_fade_images_show() shown[#shown + 1] = 'images' end");
        f.useLoadImages.set(true);
        run(gp, "fade_in(0)");                                 // 3 -> fading in
        es.screenFadeHostFrame(1);
        CHECK(f.state.get() == 2);
        run(gp, "fade_out(0)");
        es.screenFadeHostFrame(3);
        const int64_t outDone = es.screenFadeClockMs();
        CHECK(f.state.get() == 3 && f.logoAt.get() == outDone + 1000);
        es.screenFadeHostFrame(1001);                          // logo stamp passed
        const int64_t logoShown = es.screenFadeClockMs();
        run(ui, "assert(#shown == 1 and shown[1] == 'logo')");
        CHECK(f.logoAt.get() == -1 && f.holdLogoUntil.get() == logoShown + 1000);
        CHECK(f.imagesAt.get() == logoShown + 6000 && f.holdImagesUntil.get() == -1);
        CHECK(c.uiScriptCalls == 1);
        // A fade-in now is parked by the logo hold (state left at 3).
        run(gp, "fade_in(1)");
        CHECK(f.state.get() == 3 && f.target.get() == 2);
        es.screenFadeHostFrame(999);                           // hold not passed
        CHECK(f.state.get() == 3);
        es.screenFadeHostFrame(3);                             // hold passed, images not due: replay
        CHECK(f.state.get() == 0 && f.imagesAt.get() == -1);   // the fade-in resets the images stamp
        es.screenFadeHostFrame(250);
        CHECK(f.state.get() == 2);
        // Images path: set the stamps directly (values only the routine writes).
        f.imagesAt.set(es.screenFadeClockMs() - 1);
        es.screenFadeHostFrame(1);
        run(ui, "assert(#shown == 2 and shown[2] == 'images')");
        CHECK(f.imagesAt.get() == -1 && f.holdImagesUntil.get() == es.screenFadeClockMs() + 1500);
        run(gp, "fade_out(0)");                                // fade-out is not held
        CHECK(f.state.get() == 1);
        es.screenFadeHostFrame(1);
        CHECK(f.state.get() == 3);
        CHECK(f.holdImagesUntil.get() == -1);                  // the fade-out completion cleared the holds
        f.holdImagesUntil.set(es.screenFadeClockMs() + 500);
        run(gp, "fade_in(0)");                                 // held by the images stamp
        CHECK(f.state.get() == 3 && f.target.get() == 2);
        // A stamp met by the clock exactly has no specified result: OPEN.
        f.holdImagesUntil.set(es.screenFadeClockMs());
        {
            auto r = host.runChunk(gp, "fade_in(0)", "eq.lua");
            CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("exact equality") != std::string::npos);
        }
        f.holdImagesUntil.set(-1);
        f.logoAt.set(-1);

        // 8. Durations: trunc(seconds x 1000); non-finite refused before any write.
        const auto before = es.screenFadeRequests().size();
        {
            auto r = host.runChunk(gp, "fade_in(1e300)", "dur.lua");
            CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("fade duration") != std::string::npos);
        }
        CHECK(es.screenFadeRequests().size() == before);
        run(gp, "fade_in(0/0, 2)");                            // bit 0x1 clear: no conversion
        run(gp, "assert(select('#', fade_in(0.0019, 1)) == 0)");
        CHECK(es.screenFadeRequests().back().durationMs == 1.0 && es.screenFadeRequests().back().targetAlpha == 0.0f);
        // fade_in's broadcast: host-gated like fade_out's.
        CHECK(es.screenFadeOpcode53Count() == 0);
        es.coopSession().present.set(true);
        es.coopSession().localIsHost.set(true);
        run(gp, "fade_in(1, 2)");
        CHECK(es.screenFadeOpcode53Count() == 1 && !f.lastBroadcastWasOut.get());
        es.coopSession().present.set(false);

        CHECK(es.screenFadeCompletionPathSummary() ==
              "real:" + std::to_string(c.realCompletions) + " fallback_undefined:" +
                  std::to_string(c.fallbackUndefined) + " fallback_no_callback:" + std::to_string(c.fallbackNoCallback));
        CHECK(c.realCompletions == 2);
    }

    // --- Screen fade callbacks (C++ level, Sec26.24): the completion body
    // calls the in-flight callback with the TARGET, handles the deferred slot,
    // and a settled request calls back at once. No UI state attached here,
    // so screen_fade_do counts as undefined.
void test_block_13() {
        EngineState es;
        sr3luahost::applySpecInitialState(es);
        auto& f = es.screenFade();
        f.modeStackTop.set(0);
        f.cutsceneState.set(0);
        CHECK(!f.documentLoaded.get());
        g_fadeCalls.clear();
        es.screenFadeRequest(false, 0, fadeCbC, 7);             // already fully in
        CHECK(g_fadeCalls.size() == 1 && g_fadeCalls[0] == std::make_pair('C', 2u));
        CHECK(f.flag.get() == 0);                               // stopped before step 3
        g_fadeCalls.clear();
        es.screenFadeRequest(true, 100, fadeCbA, 5);            // out, in flight A
        CHECK(f.state.get() == 1 && f.inFlight == fadeCbA && f.flag.get() == 5);
        es.screenFadeRequest(false, 100, fadeCbB, 6);           // parked: deferred B
        CHECK(f.state.get() == 1 && f.target.get() == 2 && f.deferred == fadeCbB && f.inFlight == fadeCbA);
        CHECK(es.screenFadeCompletionBody());                   // 1 -> 3; A(target 2)
        CHECK(f.state.get() == 3 && g_fadeCalls.size() == 1 && g_fadeCalls[0] == std::make_pair('A', 2u));
        CHECK(f.inFlight == nullptr && f.deferred == fadeCbB);  // 3 != target 2: deferred kept
        es.screenFadeHostFrame(1);                              // replay with B, 250 ms, flag 6
        CHECK(f.state.get() == 0 && f.inFlight == fadeCbB && f.flag.get() == 6);
        CHECK(es.screenFadeCompletionBody());                   // 0 -> 2; B(2); deferred == in-flight: cleared
        CHECK(g_fadeCalls.size() == 2 && g_fadeCalls[1] == std::make_pair('B', 2u));
        CHECK(f.inFlight == nullptr && f.deferred == nullptr);
        CHECK(!es.screenFadeCompletionBody());                  // settled: no flip, nothing called
        CHECK(g_fadeCalls.size() == 2);
        // Deferred different from in-flight, settled at the target: both called.
        g_fadeCalls.clear();
        es.screenFadeRequest(true, 100, fadeCbA, 0);            // out, A
        es.screenFadeRequest(false, 100, fadeCbB, 0);           // parked B, target 2
        es.screenFadeRequest(true, 100, fadeCbC, 0);            // target back to 3; state 1: restarts, C in flight
        CHECK(f.state.get() == 1 && f.target.get() == 3 && f.inFlight == fadeCbC && f.deferred == fadeCbB);
        CHECK(es.screenFadeCompletionBody());
        CHECK(g_fadeCalls.size() == 2 && g_fadeCalls[0] == std::make_pair('C', 3u) &&
              g_fadeCalls[1] == std::make_pair('B', 3u));
        CHECK(f.inFlight == nullptr && f.deferred == nullptr && f.state.get() == 3);
        // The fallback timer only completes the transition it was armed for.
        // Every flip above came from the body directly: no timer fired.
        CHECK(es.screenFadeCounters().fallbackUndefined == 0 && es.screenFadeCounters().realCompletions == 0);
    }

    // --- UI resolution queries (Sec26.26) through Lua.
void test_block_14() {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        lua_State* ui = host.uiState();
        EngineState& es = host.engineState();
        auto run = [&](const char* chunk) {
            auto r = host.runChunk(ui, chunk, "vint.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };
        auto refuses = [&](const char* chunk, const char* needle) {
            auto r = host.runChunk(ui, chunk, "vint_ref.lua");
            CHECK(r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos);
            if (r.pcallError.find(needle) == std::string::npos) std::cerr << chunk << ": " << r.pcallError << "\n";
        };
        {
            auto r = host.runChunk(gp, "assert(vint_is_std_res == nil and vint_get_safe_frame == nil)", "v0.lua");
            CHECK(r.loadOk && r.pcallOk);
        }
        refuses("vint_is_std_res()", "0x00e236f0");
        es.vintRecordFirst().set(1024);
        es.vintRecordSecond().set(768);                       // 1.333 < 1.5: mode not read
        run("assert(vint_is_std_res() == true and select('#', vint_is_std_res(1, 2)) == 1)");
        es.vintRecordFirst().set(1280);
        es.vintRecordSecond().set(1024);                      // 5:4
        run("assert(vint_is_std_res() == true)");
        es.vintRecordFirst().set(1920);
        es.vintRecordSecond().set(1080);
        refuses("vint_is_std_res()", "0x0132bd80");
        es.vintDisplayMode().set(-1);
        run("assert(vint_is_std_res() == false)");
        es.vintDisplayMode().set(2);
        run("assert(vint_is_std_res() == true)");
        for (int mode : {0, 1, 3, 4}) {
            es.vintDisplayMode().set(mode);
            run("assert(vint_is_std_res() == false)");
        }
        es.vintRecordFirst().set(1500);
        es.vintRecordSecond().set(1000);                      // exactly 1.5: not below
        run("assert(vint_is_std_res() == false)");
        es.vintRecordFirst().set(0);
        es.vintRecordSecond().set(0);                         // 0/0 = NaN: not below
        run("assert(vint_is_std_res() == false)");
        es.vintRecordFirst().set(-5);
        es.vintRecordSecond().set(0);                         // -inf: below
        run("assert(vint_is_std_res() == true)");

        refuses("vint_get_safe_frame()", "+0x8");
        es.vintSafeFrameA().set(1280);
        es.vintSafeFrameB().set(720);
        // The constants are CONFIRMED (job nnlt) and set at start; Sec26.26's
        // worked value for 1280 x 720. Fuller coverage:
        // tests/synthetic_luahost_cutscene_test.cpp.
        run("local a, b, c, d = vint_get_safe_frame()\n"
            "assert(a == 96 and b == 54 and c == 1184 and d == 666)\n"
            "assert(select('#', vint_get_safe_frame()) == 4)");
        es.vintSafeFrameScale1().set(0.0625);
        es.vintSafeFrameScale2().set(0.9375);
        run("local a, b, c, d = vint_get_safe_frame()\n"
            "assert(a == 80 and b == 45 and c == 1200 and d == 675)");
        es.vintSafeFrameScale2().set(1.0 / 3.0);              // 426.67 / 240: round to nearest
        run("local a, b, c, d = vint_get_safe_frame()\nassert(c == 427 and d == 240)");
        es.vintSafeFrameScale1().set(0.5);
        es.vintSafeFrameA().set(3);                            // 1.5: an exact tie, mode not stated
        refuses("vint_get_safe_frame()", "tie");
    }

    // --- Hook evidence labels (spec-lua-bindings.md Â§4 desk review,
    // 2026-09-30): nothing removed, every Group 1 name labelled.
void test_block_15() {
        int sec8 = 0, pending = 0, refuted = 0, groupLevel = 0;
        for (const auto& h : sr3luahost::confirmedHooks()) {
            switch (h.evidence) {
                case sr3luahost::HookEvidence::Sec8Confirmed: ++sec8; break;
                case sr3luahost::HookEvidence::Sec4ScanPendingRecheck: ++pending; break;
                case sr3luahost::HookEvidence::RefutedSec14_3: ++refuted; break;
                case sr3luahost::HookEvidence::GroupLevel: ++groupLevel; break;
            }
            if (h.group != sr3luahost::HookGroup::Group1_GeneralNamed) CHECK(h.evidence == sr3luahost::HookEvidence::GroupLevel);
        }
        CHECK(refuted == 5);
        CHECK(pending == 42);
        CHECK(sec8 + pending + refuted == 78); // every Group 1 name labelled
        CHECK(groupLevel == static_cast<int>(sr3luahost::confirmedHooks().size()) - 78);
        for (const auto& h : sr3luahost::confirmedHooks()) {
            if (h.name == "screen_fade_do" || h.name == "hud_running_man_event_update")
                CHECK(h.evidence == sr3luahost::HookEvidence::Sec8Confirmed);
            if (h.name == "garage_populate") CHECK(h.evidence == sr3luahost::HookEvidence::RefutedSec14_3);
            if (h.name == "newsticker_populate") CHECK(h.evidence == sr3luahost::HookEvidence::Sec4ScanPendingRecheck);
        }
    }

    // --- Engine-state slot inventory and applySpecInitialState (batch
    // 2026-10-01): the CONFIRMED start-up values are set, everything else
    // stays OPEN.
void test_block_16() {
        sr3luahost::EngineState es;
        sr3luahost::applySpecInitialState(es);
        const auto inv = es.openSlotInventory();
        std::unordered_map<std::string, int> perArea, knownPerArea;
        for (const auto& r : inv) {
            ++perArea[r.area];
            if (r.known || r.knownKeys > 0) ++knownPerArea[r.area];
            CHECK(!r.global.empty() && r.spec.rfind("spec-", 0) == 0);
        }
        // 60 = the 40 of the first batch + 6 zscene (Sec26.25 lifecycle
        // driver and scene table, nnlt) + 10 cutscene machine + 3 vint (width,
        // height, layout index) + 1 vint (vintTimeIndexByDoc_, Sec19.1 batch
        // 2026-10-02). The Sec27/Sec28 batch's own area ("batch2728"), the
        // Sec31/Sec32 batch's own area ("batch3132", 2026-10-02), the
        // Sec38/Sec39/Sec40 batch's own area ("ranking0608", 2026-10-02 -
        // saveSystemUi_.slotCount, pcuCategoryTable_.kindByIndex,
        // pcuCatalogOutfits_.flagsByIndex; see lua_spec_confirmed_stubs.cpp's
        // own Sec38/Sec39/Sec40 batch header - most of that batch's own new
        // fields are CHOSEN plain bools/per-entity maps, not global
        // OpenValue/OpenValueMap singletons, so only these 3 land here), and
        // the resumed-session Sec44/Sec45/Sec46 batch's own area
        // ("batch444546", 2026-10-02 - 5 rows: ambientGangSpawnEnabled_/
        // cellCameraEnabled_/ambientCopSpawnEnabled_/actionNodesShouldntFlee_/
        // actionNodesRestrictSpawning_), and the Sec49 batch's own area
        // ("ranking16", 2026-10-03 - spawnOverride_.categoryNameResolves;
        // see lua_spec_confirmed_stubs.cpp's own Sec49 batch header - the
        // rest of that batch's own new fields are likewise CHOSEN plain
        // bools/per-entity maps, so only this 1 lands here), and the Batch 7
        // fan-out area ("tranche03batch7", 2026-10-08 - pathfindResult_,
        // rcSignalRange_, bossBattleMatt_.npcBahamutResolves/
        // avatarModelResolves/styleTestPcResolves; see
        // lua_spec_confirmed_stubs_b14_batch7_tranche03.cpp's own Sec30
        // batch header - the rest of that batch's own new fields are
        // CHOSEN plain fields/per-character-or-vehicle OpenValue fields,
        // not centrally inventoried here, same existing precedent), and
        // Batch 3's own area (2026-10-08, Sec11+Sec12, results/
        // lua_natives_sec10_28_fanout_scoping_20261008.md - "batch3_pcr_
        // vehicle", 9 rows: pcrPlasticSurgeonMode_, pcrMorph_.
        // sessionResolves/slotResolves, pcuStreaming_.rowCounterPending/
        // listMembershipPending, hordeModeActive_, storeCameraPreset_.
        // presetResolves, pauseMenuOptions_.acceptGatePasses,
        // vintRemapStaging_.dialogAlreadyOpen; the rest of that batch's
        // own new fields are likewise CHOSEN plain bools/per-entity maps
        // or per-character/per-vehicle fields, not centrally inventoried
        // here either) are each counted separately (dynamically, via
        // perArea) so every batch's own check stays independent of the
        // others' exact row counts.
        CHECK(inv.size() == 60u + static_cast<size_t>(perArea["batch2728"]) +
                             static_cast<size_t>(perArea["batch3132"]) +
                             static_cast<size_t>(perArea["ranking0608"]) +
                             static_cast<size_t>(perArea["batch444546"]) +
                             static_cast<size_t>(perArea["ranking16"]) +
                             static_cast<size_t>(perArea["player"]) +
                             static_cast<size_t>(perArea["tranche03batch7"]) +
                             static_cast<size_t>(perArea["batch3_pcr_vehicle"]) +
                             static_cast<size_t>(perArea["wave2w21"])); // wave 2 batch W2-1 (b15), 2 rows
        CHECK(perArea["batch3_pcr_vehicle"] == 9 && knownPerArea["batch3_pcr_vehicle"] == 0); // all start OPEN/empty
        // Player runtime batch (2026-10-03): the lock-handle global 0x0263ae3c,
        // CONFIRMED null at session/mission start (spec-player-runtime.md
        // Sec2.10, self-balancing - see applySpecInitialState()'s own item 11).
        CHECK(perArea["player"] == 1 && knownPerArea["player"] == 1);
        // Wave 2 batch W2-1 (b15): move_to_do's action-id counter 0x0130cb94
        // and 0x00b24fa0's raw result - both genuinely OPEN at start-up.
        CHECK(perArea["wave2w21"] == 2 && knownPerArea["wave2w21"] == 0);
        CHECK(perArea["co-op"] == 6 && perArea["tutorial"] == 1 && perArea["vehicle-store"] == 1);
        CHECK(perArea["zscene"] == 13 && perArea["cutscene"] == 10 && perArea["fade"] == 14 &&
              perArea["vint"] == 11 && perArea["other"] == 4);
        CHECK(perArea["ranking0608"] == 3 && knownPerArea["ranking0608"] == 0); // all 3 start OPEN/empty
        CHECK(perArea["ranking16"] == 1 && knownPerArea["ranking16"] == 0);     // starts OPEN/empty
        CHECK(perArea["tranche03batch7"] == 5 && knownPerArea["tranche03batch7"] == 0); // all 5 start OPEN
        CHECK(knownPerArea["co-op"] == 1 && knownPerArea["tutorial"] == 1 && knownPerArea["vehicle-store"] == 1);
        // vint: the two safe-frame constants (CONFIRMED, nnlt) are set at start;
        // vintTimeIndexByDoc_ (a per-name map) starts empty (OPEN, Sec19.1), so
        // it doesn't add to the known count.
        // zscene: the skip byte 0x0153b556 (false, Sec26.25 Globals, 2026-10-01),
        // the current entry 0x0153b530 (null, Sec26.25 Globals, 2026-10-02),
        // the re-queue-on-reset byte 0x0153b542 (false, CONFIRMED permanently
        // zero in practice) and the nearest scene-bearing world object list
        // 0x03171a64 (""/none found, CONFIRMED - no world-object manager
        // exists in this host) - 4, not 2, since 2026-10-09 (the zscene_prep
        // / mm_p_06 + mm_p_01 blocker fix). The per-entry handle class
        // 0x00dafb60 stays unknown here (0 keys): its own real default is
        // written narrowly at promotion time (zscenePromotionStep), not at
        // construction - see engine_state.h's own doc comment.
        // other: objectResolves_ now carries 5 known entries (Sec29's closed
        // literal set - "homies"/"shopkeepers"/"-- Cutscene Script Group --"/
        // "#PLAYER1#"/"#PLAYER2#"), so its one inventory record counts as known.
        // fade: 13, not 12, since 2026-10-07 (Sec60.4's closure) -
        // applySpecInitialState now also sets the cutscene state 0x0153b520
        // to 0 ("idle"); only modeStackTop stays OPEN in this area now.
        // other: 2, not 1, since 2026-10-09 (Sec10.3's closure, mission-
        // census item) - applySpecInitialState's own item 16 (renumbered on
        // merge - see lua_spec_initial_state.cpp) now pre-seeds
        // hasLocalPlayer_ to a CONFIRMED default of true; only
        // currentDefaultDocHandle_ and missionFlagsWord_ stay OPEN in this
        // area now (objectResolves_ was already known). zscene stays 4
        // (unrelated, already-merged zscene_prep/mm_p_06+mm_p_01 fix above).
        CHECK(knownPerArea["zscene"] == 4 && knownPerArea["cutscene"] == 0 && knownPerArea["fade"] == 13 &&
              knownPerArea["vint"] == 2 && knownPerArea["other"] == 2);
        for (const auto& r : inv) {
            if (r.global.find("0x024d8534") != std::string::npos) CHECK(r.known);
            if (r.global.find("0x0151d600") != std::string::npos) CHECK(r.knownKeys == 210);
            if (r.global.find("0x022cdf08") != std::string::npos) CHECK(r.known);
            if (r.global.find("mode stack") != std::string::npos) CHECK(!r.known);
            if (r.global.find("0x0153b520") != std::string::npos) CHECK(r.known); // 2026-10-07, Sec60.4
            if (r.global.find("0x012f44fc") != std::string::npos) CHECK(!r.known);
            if (r.global.find("0x009df3d0") != std::string::npos) CHECK(r.known); // 2026-10-09, applySpecInitialState item 13 (Sec28.12)
        }
        // Values (spec text): co-op none; store flag 0; fade globals at their
        // file values; tutorial states after the fill.
        CHECK(es.coopSession().present.get() == false);
        CHECK(!es.coopIsActive() && !es.coopLocalIsHost() && !es.coopLocalIsClient());
        // 0x009df3d0 (second/co-op player non-null, Sec28.12): no second
        // client ever joins this host's roster - applySpecInitialState item
        // 13, 2026-10-09.
        CHECK(es.playerRig().coopPlayerPresent.get() == false);
        CHECK(es.vehicleStoreActive().get() == false);
        const auto& f = es.screenFade();
        CHECK(f.state.get() == 2 && f.target.get() == 2 && f.flag.get() == 0 && !f.documentLoaded.get());
        CHECK(f.logoAt.get() == -1 && f.holdLogoUntil.get() == -1 && f.imagesAt.get() == -1 &&
              f.holdImagesUntil.get() == -1 && f.autoSaveStamp.get() == -1);
        // 0x0149365c: set to 1 by the start-up 0x005d1a30 (Sec26.24, nnlt).
        CHECK(f.autoSaveCounter.get() == 0 && f.useLoadImages.get() && !f.lastBroadcastWasOut.get());
        CHECK(f.inFlight == nullptr && f.deferred == nullptr);
        // 0x0153b520: idle (0) at start - Sec60.4, 2026-10-07 closure of the mm_p_06 blocker.
        CHECK(f.cutsceneState.known() && f.cutsceneState.get() == 0);
        for (int i = 0; i < EngineState::kTutorialEntryCount; ++i)
            CHECK(es.tutorialState().get(EngineState::tutorialStateKey(i)) == (i <= 188 ? 0 : 1));
        CHECK(es.zsceneSkipAllCutscenes().known() && es.zsceneSkipAllCutscenes().get() == false);
        CHECK(es.zsceneCurrent().known() && es.zsceneCurrent().get().empty()); // 0x0153b530 null at start
        // 0x0153b542 CONFIRMED permanently false; the nearest-world-object
        // query CONFIRMED "" (no world-object manager in this host) - both
        // 2026-10-09, zscene_prep blocker fix.
        CHECK(es.zsceneRequeueOnReset().known() && es.zsceneRequeueOnReset().get() == false);
        CHECK(es.zsceneNearestWorldObjectScene().known() && es.zsceneNearestWorldObjectScene().get().empty());
        CHECK(!es.zsceneStateCode().known() && !es.zscenePending().known());
        CHECK(!es.zsceneAutoSelectNearest().known()); // no spec gives this one's start-up value
        CHECK(!es.vintDisplayMode().known() && !es.vintRecordFirst().known());
        CHECK(es.vintSafeFrameScale1().get() == static_cast<double>(0.075f) &&
              es.vintSafeFrameScale2().get() == static_cast<double>(0.925f));
        // The inventory follows writes: a value, a per-name key, and bits.
        es.zsceneStateCode().set(2);
        es.zsceneLoadable().set("scene_a", true);
        es.missionFlagsWord().setBits(0x14, 0x4);
        for (const auto& r : es.openSlotInventory()) {
            if (r.global.find("0x0153b51c") != std::string::npos) CHECK(r.known && r.knownKeys == 1);
            if (r.global.find("0x00723d20") != std::string::npos) CHECK(!r.known && r.knownKeys == 1);
            if (r.global == "0x014c848c") CHECK(!r.known && r.knownKeys == 2);
        }
        // Bare-global table (Sec13.2/Sec16.4, answered 2026-10-01): all 24
        // names, each into BOTH states, each also a spec-confirmed name.
        for (const auto& bg : sr3luahost::specBareGlobals()) {
            CHECK(bg.gameplay || bg.ui);
            const auto& n = sr3luahost::specConfirmedStubNames();
            CHECK(std::find(n.begin(), n.end(), bg.name) != n.end());
            CHECK(bg.spec.rfind("spec-", 0) == 0);
        }
        CHECK(sr3luahost::specBareGlobals().size() == 24);
        for (const auto& bg : sr3luahost::specBareGlobals()) CHECK(bg.gameplay && bg.ui);
        // Host applies the same initial state. With no UI state holding
        // Screen_fade_transition_complete, the fade init does not run.
        sr3luahost::Host h({});
        CHECK(h.engineState().vehicleStoreActive().get() == false);
        CHECK(h.engineState().coopSession().present.get() == false);
        CHECK(!h.engineState().screenFade().documentLoaded.get());
        CHECK(h.engineState().screenFadeUiState() == nullptr);
    }

    // --- Named-object resolution's closed literal set (Sec29, 2026-10-02):
    // applySpecInitialState() pre-populates exactly the 5 names Sec29.4
    // confirms engine code registers unconditionally - everything else,
    // including a mission character's own fixed name, must stay OPEN. This
    // is a regression guard against ever accidentally widening the set.
void test_block_17() {
        sr3luahost::EngineState es;
        sr3luahost::applySpecInitialState(es);
        auto& resolves = es.objectResolves();
        for (const std::string& name :
             {"homies", "shopkeepers", "-- Cutscene Script Group --", "#PLAYER1#", "#PLAYER2#"}) {
            CHECK(resolves.known(name));
            CHECK(resolves.get(name) == true);
        }
        CHECK(resolves.knownCount() == 6 /* +"#CLOSEST_PLAYER#" = false, Sec4.13.0 */);
        // An arbitrary name NOT in the closed set - including the actual
        // mission-blocker name - stays OPEN: producer (3) (173 in-memory
        // call sites, never individually traced) could register essentially
        // any name, so this project does not know and must not guess.
        // "Homies" is NOT in this list any more: the by-name registry lookup (0x004588f0) is case-insensitive, so
        // it is the same object as "homies" (post-6799e45 sweep, m21 '#player1#' / m24 'Saint_NPC 007').
        CHECK(resolves.known("Homies") && resolves.get("Homies") == true);
        CHECK(resolves.known("#player1#"));
        for (const std::string& name : {"Killbane", "homie", "#PLAYER3#", "some_other_npc"}) {
            CHECK(!resolves.known(name));
            bool threw = false;
            try {
                (void)resolves.get(name);
            } catch (const sr3luahost::OpenStateError& e) {
                threw = true;
                CHECK(std::string(e.what()).find("named-object resolution['" + name + "']") != std::string::npos);
            }
            CHECK(threw);
        }
    }

