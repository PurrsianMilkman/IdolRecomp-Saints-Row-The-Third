// lua_host_run main() split: step 1, the real mission-driving pass.
// Code moved verbatim out of the former single-function main() (see lua_host_run_session.h).
#include "lua_host_run_session.h"

namespace luahostrun {

void setupMissionDrive(LuaHostRunSession& sess) {
    auto& missionTickBudget = sess.missionTickBudget;
    auto& toolsDir = sess.toolsDir;
    auto& host = *sess.host_;
    auto& missionRows = sess.missionRows;
    auto& missions = sess.missions;
    auto& kMissionTickBudget = sess.kMissionTickBudget;
    auto& missionResults = sess.missionResults;
    auto& preMissionCounts = sess.preMissionCounts;

    // ======================================================================
    // STEP 1 (this task, run only after step 0 above has landed and been
    // measured): drive real missions. Real stem population:
    // tools/mission_package_per_container.tsv (HANDOFF.md Sec9.125),
    // deduped by its own mission_lua_stem column, keeping only rows where
    // stem_covers_a_start_script==1 (a real, confirmed `<stem>_start` entry
    // point exists - spec-lua-bindings.md Sec14.10/HANDOFF Sec9.123,
    // verified e.g. `m01_start(m01_checkpoint, is_restart)` calls
    // `m01_run(...)` internally). HANDOFF cites "49/54 stems" - this pass
    // reports the actual count its own dedup+filter finds, never assumed.
    //
    // For each real mission stem: load that mission's own real, found
    // `.lua` script into the gameplay state (already carrying the real
    // preload chain's own effects from earlier in this same run -
    // system_lib.lua then game_lib.lua, per preloadNamed() above - plus
    // everything the full 804-script population pass above already ran),
    // via the SAME runChunk()/load pattern this file already uses
    // elsewhere, watchdog-wrapped (this task's own NEW call site, never
    // watchdog-protected before - see runChunkWithWatchdog() above's own
    // doc comment for why that's safe here). Then call
    // `<stem>_start("mission start", false)` (CONFIRMED call shape as of
    // 2026-10-02 - spec-lua-api-behaviour.md Sec36.4, spec-lua-bindings.md
    // Sec14.11; see callMissionStart()'s own doc comment for the full
    // citation and for why this harness's remaining tick budget/watchdog
    // numbers below stay CHOSEN), watchdog-wrapped, as a
    // script-thread record through the runner (request 11, Sec26.27: the
    // record is current for the call and the call may yield; a yielded
    // record stays in the thread table and every tick's scheduler passes -
    // 0x00e0cf50, see kSchedulerPassesPerTick below - resume it). Then tick
    // the confirmed-hook-firing pass (the real, already-built mechanism
    // that exercises sr3luahost::ThreadScheduler - thread_new/thread_yield
    // etc. are registered into both states and reachable from any hook
    // body a mission script now defines; see thread_scheduler.h's own
    // documented SCOPE note that thread_check_done() resolves immediately,
    // since nothing in this project ever lua_resume()s a created thread -
    // so a single pcall already runs each hook body to completion; ticking
    // repeats the WHOLE confirmed-hook-firing pass, each individual hook
    // call already watchdog-protected internally by fireConfirmedHooks
    // itself) for a CHOSEN fixed budget of kMissionTickBudget=20 ticks per
    // mission - a round number, not a recovered real per-mission frame
    // count (no such fact exists anywhere in this project's own data):
    // large enough to let a mission's own newly-defined hooks be dispatched
    // several times each (real stateful hook bodies - e.g. a "first_time"
    // init flag - could genuinely behave differently tick-to-tick, so this
    // is a real, not rhetorical, budget) while keeping total added work
    // small (49 missions x 20 ticks x up to hooks.size() hooks is a small
    // fraction of the main pass's own already-measured hook-attempt
    // volume). Stops a given mission's own ticking early, before the full
    // budget, on either of 2 real, CHOSEN (not recovered) conditions,
    // checked and reported per mission below: (a) "error" - a NEW watchdog
    // abort appears on some hook this tick (a genuine runaway-loop signal
    // already individually contained by fireConfirmedHooks' own per-hook
    // watchdog - stopping early here just avoids burning the rest of the
    // budget on a call already known to be hitting the same infinite
    // loop); (b) "completion" - 2 consecutive ticks produce an IDENTICAL
    // (existed/ok/err) signature across the whole hook set, a real,
    // measured (not assumed) idempotence signal - suppressed while a fade is
    // in flight or while the tick's scheduler passes resumed any of this
    // mission's own script threads (both are progress the signature can't
    // see). Otherwise:
    // "budget exhaustion" (all 20 ticks ran).
    //
    // A real, stated caveat, same shape as this file's own top-comment
    // caveat about the main 804-script pass: host.gameplayState() is ONE
    // PERSISTENT state shared across every mission this loop drives, in
    // walk order (not a fresh state per mission) - a later mission's own
    // tick-firing pass can therefore still see an EARLIER mission's own
    // Group-3 mission-numbered hook definitions (e.g. `m02_*` names)
    // still live in the shared global table while driving a later mission
    // (e.g. `m05`), and a later mission's own top-level runChunk()/`_start`
    // call can overwrite a name an earlier mission also defined. Reported
    // here rather than silently papered over; per-mission rows below are
    // each still a real, honest measurement of "what happened when this
    // mission was driven at its own real position in the walk," exactly
    // analogous to how the main pass's own per-script rows already work.
    std::cout << "\n\n=== STEP 1 (this task): real mission-driving pass ===\n";
    fs::path missionTsvPath = toolsDir / "mission_package_per_container.tsv";
    missionRows = loadMissionPackageRows(missionTsvPath.string());
    std::cout << "Loaded " << missionRows.size() << " real rows from " << missionTsvPath.string()
              << " (0 means that file wasn't found from this working directory - step 1 is skipped "
              << "entirely below, not fabricated, if so).\n";

    std::set<std::string> seenStems;
    for (auto& row : missionRows) {
        if (!row.coversStartScript) continue;
        if (seenStems.count(row.missionLuaStem)) continue;
        seenStems.insert(row.missionLuaStem);
        missions.push_back(row);
    }
    std::cout << "Real, deduped-by-mission_lua_stem population with stem_covers_a_start_script==1: "
              << missions.size() << " distinct stems (HANDOFF.md's own citation is \"49/54\" - this is "
              << "this run's own actual, independently-derived count, reported as-is whether or not it "
              << "matches).\n";

    kMissionTickBudget = missionTickBudget; // CHOSEN (default 20, --tick-budget= override) - see this block's own top comment for the reasoning
    // get_frame_time (spec-lua-api-behaviour.md Sec37.2/Sec37.5, CONFIRMED
    // writer: a raw, unscaled, uncapped, unpaused measured wall-clock delta,
    // refreshed every main-loop frame): since this host now resumes each
    // thread's single scheduler pass exactly once per tick (above), a call
    // to get_frame_time() during that resume must report that same tick's
    // own modelled elapsed time to keep the script's own wall-clock reading
    // in step with the host's other per-tick clocks (the fade/cutscene host
    // frame above already use kFadeHostMsPerTick). CHOSEN: reuse that same
    // constant here rather than leave get_frame_time() at the file image's
    // unrelated 1/30 s startup default, which has no relation to this host's
    // own tick length and was the root cause of a stuck-delay mission (e.g.
    // m02) never accumulating enough modelled time to finish - Sec37.5's own
    // "For a host" paragraph names exactly this mismatch shape. Set once,
    // before the mission loop: the value never varies tick-to-tick here.
    host.bareGlobals().setFrameTime(static_cast<float>(kFadeHostMsPerTick) / 1000.0f);

    missionResults.reserve(missions.size());

    // Snapshot host.hitLog() right before step 1 begins - the "before"
    // half of the NEW stub-hit-with-missions ranking's own incremental
    // column below, same snapshot/delta convention as `historical` above.
    for (auto& [name, hit] : host.hitLog().hits()) preMissionCounts[name] = hit.callCount;
}

void driveMissions(LuaHostRunSession& sess) {
    auto& probeAutopilot = sess.probeAutopilot;
    auto& autoAnswerHudPrompts = sess.autoAnswerHudPrompts;
    auto& hudPromptAutoAnswerDelayTicks = sess.hudPromptAutoAnswerDelayTicks;
    auto& host = *sess.host_;
    const auto& hooks = *sess.hooks_;
    auto& st = sess.st;
    auto& missions = sess.missions;
    auto& kMissionTickBudget = sess.kMissionTickBudget;
    auto& missionResults = sess.missionResults;
    auto& schedulerForeignErrorsTotal = sess.schedulerForeignErrorsTotal;
    auto& hudPromptRespondedCount = sess.hudPromptRespondedCount;
    auto& hudPromptFirstSeenGlobalTick = sess.hudPromptFirstSeenGlobalTick;
    auto& globalTick = sess.globalTick;

    for (auto& row : missions) {
        MissionResult mr;
        mr.stem = row.missionLuaStem;
        mr.entryName = row.missionLuaEntryName;
        mr.containerName = row.containerName;

        size_t chosen = SIZE_MAX;
        std::string entryLower = toLower(row.missionLuaEntryName);
        for (size_t i = 0; i < st.found.size(); ++i) {
            if (toLower(st.found[i].entryName) == entryLower) { chosen = i; break; }
        }
        if (chosen == SIZE_MAX) {
            mr.scriptFound = false;
            mr.stopReason = "error: mission script entry not found in this run's own archive walk";
            missionResults.push_back(std::move(mr));
            continue;
        }
        mr.scriptFound = true;
        const auto& script = st.found[chosen];

        std::vector<std::string> errorsInOrder;

        Host::RunResult loadResult = runChunkWithWatchdog(host, host.gameplayState(), script.content, script.entryName);
        mr.loadOk = loadResult.loadOk;
        mr.loadError = loadResult.loadError;
        mr.pcallOk = loadResult.pcallOk;
        mr.pcallError = loadResult.pcallError;
        if (!loadResult.loadOk) errorsInOrder.push_back(loadResult.loadError);
        else if (loadResult.ranPcall && !loadResult.pcallOk) errorsInOrder.push_back(loadResult.pcallError);

        // DOCUMENTED ENGINEERING CHOICE, not a confirmed spec mechanism: the real
        // bound function name is a data-borne string property
        // (`script_mission_start`) read off the mission's own `scripted_mission_start`
        // placement/definition record (spec-lua-api-behaviour.md Sec36.1,
        // spec-lua-bindings.md Sec14.11) - this project does not have that record's
        // data (it lives in the still-parked `.czn_pc` interior, or another unread
        // carrier) to look up the true name. Deriving it as `<stem>_start` from this
        // row's own `mission_lua_stem` column is HYPOTHESIS per Sec36.4/Sec14.11 (a
        // partner team's own census found shipped missions follow this pattern, but
        // the object data itself was never read to confirm it) - kept here as a
        // reasonable stand-in, per Sec36.4's own framing, but never presented as
        // itself a confirmed spec fact.
        std::string funcName = row.missionLuaStem + "_start";
        // Records still live from earlier missions (the gameplay state is
        // shared across the walk - see this block's top caveat). The
        // scheduler resumes them too, as the engine's would; their resumes
        // and errors are counted apart from this mission's own. Measurement
        // only: nothing about the passes depends on this set. A record a
        // leftover thread creates during this mission counts as this
        // mission's (not distinguishable here).
        std::set<uint16_t> foreignIds;
        for (uint16_t id : host.bareGlobals().threads().liveIds()) foreignIds.insert(id);
        // FIX 2026-10-08 (root cause of probe_blockers_ranked_20261008_
        // post_resolver_merge.md item 2, 12/49 missions): spec-lua-api-
        // behaviour.md Sec21.27a confirms phase 8 (the real engine's
        // literal start-up-adjacent "mid-bind" value) auto-resolves to
        // phase 1 well before any script code is reachable - so the
        // instant this driver is about to invoke a mission's own `_start`,
        // the real "mission active, phase != 8" gates are CONFIRMED true,
        // not an unmodelled fact. See EngineState::beginMissionPhaseTracking
        // ()'s own doc comment (include/sr3luahost/engine_state.h) for the
        // full root-cause history and why this is the one correct hook
        // point ("the instant a mission's own `_start` is invoked").
        //
        // UPGRADED this task (2026-10-08, Sec21.27a/Sec21.27b): now also
        // drives the REAL integer phase machine (bindMissionPhaseMachine())
        // through its real 8->(2)->1->(3 or 5) sequence for THIS mission's
        // own name, instead of only flipping the three legacy boolean
        // facades. `type` is the CHOSEN default 0 (no real +0xa0 population
        // table for any of these 49 missions, see that method's own doc
        // comment); `isRestart` is the SAME kMissionStartIsRestart=false
        // this loop already passes to callMissionStart() just below -
        // every mission this harness drives is a fresh start.
        // MISSION-HOST PHASE 3, part (b) (this task, 2026-10-09): m05's own
        // real script-named vehicles, registered as live objects BEFORE this
        // mission's own `_start` runs - closing the real-world-state gap
        // behind the vehicle enter-submitter blocker (`0x00af22a0`, spec-
        // player-runtime.md Sec7.4/Sec38.1) for this one mission. Names are
        // real, read verbatim off the real shipped m05.lua (tools/validation/
        // lua_source_dump.cpp against the real cache, this task's own
        // extraction) - its `M05_groups` table's own `.heli` fields:
        // `m05_heli_host`, `m05_heli_client`, `m05_heli_pierce`,
        // `m05_heli_chase_host` (lines 15/20/25/135 of the real script). NOT
        // fabricated - every one of these 4 is a name m05's own real Lua
        // text passes into a vehicle-enter native (`on_vehicle_enter`,
        // `vehicle_enter_teleport` - a game_lib.lua wrapper over
        // `vehicle_enter_do` - see this task's own commit for the full call-
        // site citation).
        //
        // Two separate writes, because this project's vehicle-enter chain
        // (resolveEnterVehicle(), src/lua_spec_confirmed_stubs_b15_wave2_
        // core_dispatchers.cpp) reads the PRE-EXISTING, hand-seeded
        // objectResolves() map, NOT this task's own Phase 2 liveObjectKind()/
        // isLiveObject() resolver - that resolver has no production
        // consumer anywhere in this codebase yet (verified: grepped every
        // src/ caller). Per spec-zone-data-format.md §11.10 ("resolving a
        // name and finding it live are the SAME event" in the real engine),
        // being live SHOULD be sufficient to resolve - but reproducing that
        // unification is a bigger mechanism change than this task's own
        // scope (closing one mission's blocker), so both facts are written
        // explicitly here instead: objectResolves().set(name, true) (the
        // mechanism vehicle_enter_do's own resolver actually reads today)
        // AND bridgeLiveVehiclePosition() (Phase 2's own mechanism, so a
        // later native that DOES start consuming liveObjectKind() also sees
        // these 4 names as live, and so this is honestly reusing, not
        // duplicating, that Phase 2 mechanism per this task's own ask).
        //
        // Positions are CHOSEN placeholders, NOT spec-confirmed real zone-
        // placement coordinates: this project's own existing `.czn_pc`
        // readers (tools/zone_object_resolver_probe.cpp, hardcoded to look
        // up only "Killbane"; tools/zone_objects_population.cpp, capped at a
        // 12-name population sample) do not support searching for an
        // arbitrary real placed-object name, so the real penthouse-rooftop
        // coordinates these 4 helicopters are actually placed at could not
        // be read without a code change this task did not make (a genuine,
        // reported tooling-coverage gap, not a data-access blocker - see
        // this task's own report). A small fixed offset per vehicle keeps
        // the 4 names distinguishable rather than literally co-located.
        // seat0Occupied=false (CHOSEN): an empty driver seat is the one
        // honestly-reachable state for a vehicle nothing in this minimal
        // host has ever "seated" anything into (same convention this
        // project already uses for every other freshly-created vehicle).
        if (row.missionLuaStem == "m05") {
            static const struct { const char* name; double x, y, z; } kM05RealVehicles[] = {
                {"m05_heli_host", 0.0, 0.0, 0.0},
                {"m05_heli_client", 10.0, 0.0, 0.0},
                {"m05_heli_pierce", 20.0, 0.0, 0.0},
                {"m05_heli_chase_host", 30.0, 0.0, 0.0},
            };
            for (const auto& v : kM05RealVehicles) {
                host.engineState().objectResolves().set(v.name, true);
                host.engineState().bridgeLiveVehiclePosition(v.name, sr3luahost::WorldPosition{v.x, v.y, v.z});
                host.engineState().getOrCreateVehicle(v.name).seat0Occupied.set(false);
            }
        }
        host.engineState().beginMissionPhaseTracking(row.missionLuaStem, /*type=*/0, kMissionStartIsRestart);
        MissionStartResult startResult = callMissionStart(host.gameplayState(), host.bareGlobals().threads(), funcName,
                                                            kMissionStartCheckpoint, kMissionStartIsRestart);
        mr.startExisted = startResult.existedAsFunction;
        mr.startAttempted = startResult.attemptedCall;
        mr.startCallOk = startResult.callOk;
        mr.startSuspended = startResult.suspended;
        mr.startSuspendedAt = startResult.suspendedAt;
        mr.startCallError = startResult.callError;
        if (startResult.attemptedCall && !startResult.callOk) errorsInOrder.push_back(startResult.callError);

        if (!startResult.existedAsFunction) {
            mr.ticksSurvived = 0;
            mr.stopReason = "error: " + funcName + " not defined (real check, spec-lua-bindings.md Sec14.10's "
                             "own confirmed call shape not reachable for this stem in this run)";
        } else {
            struct TickSig {
                uint64_t existed = 0, ok = 0, err = 0;
                bool watchdogAbort = false;
            };
            TickSig prevSig;
            bool havePrevSig = false;
            bool stopped = false;
            bool startTracked = startResult.suspended;
            for (int tick = 1; tick <= kMissionTickBudget; ++tick) {
                host.engineState().screenFadeHostFrame(kFadeHostMsPerTick);
                // The cutscene machine and the zscene lifecycle (Sec26.25 Host
                // summary: promotion then completion, every frame), on the same
                // host clock; after the fade frame (CHOSEN order).
                host.engineState().cutsceneHostFrame();
                // The game clock (Sec48.3, CONFIRMED mechanism): advances by
                // this same tick's own modelled frame time, matching
                // get_frame_time's own per-resume value (kFadeHostMsPerTick/
                // 1000 - see that constant's and setFrameTime()'s own doc
                // comments above). CHOSEN call order: alongside the other
                // per-tick host clocks.
                host.engineState().gameClockAdvanceFrame(static_cast<double>(kFadeHostMsPerTick) / 1000.0);
                host.engineState().tickCharacterDeaths(); // per-human update's death finalizer (engine callback dispatch, spec B)
                // The script-thread scheduler 0x00e0cf50, kSchedulerPassesPerTick
                // times (CHOSEN mapping, see that constant). The watchdog is
                // re-armed on each coroutine right before its resume
                // (lua_sethook resets the instruction count), so it bounds one
                // resume, not a thread's whole life.
                bool ownResumeThisTick = false;
                // PassEvent carries no lua_State* of its own (bare_globals.h)
                // - before/after are called in strict pairs for the same
                // record within one schedulerPass call (src/lua_bare_
                // globals.cpp's own single-threaded while loop, no
                // interleaving), so this one local safely bridges the two.
                lua_State* currentResumeCo = nullptr;
                for (int pass = 0; pass < kSchedulerPassesPerTick; ++pass) {
                    host.runScriptThreadSchedulerPass(
                        [&](const BareThreadTable::Record& rec) {
                            currentResumeCo = rec.co;
                            resetTraceRing(rec.co); // see g_traceRings' own doc comment above - fresh per resume
                            lua_sethook(rec.co, missionWatchdogHook, missionWatchdogHookMask(),
                                        kMissionWatchdogInstructionBudget);
                        },
                        [&](const BareThreadTable::PassEvent& ev) {
                            if (foreignIds.count(ev.id)) {
                                if (ev.resumed) mr.schedulerForeignResumes++;
                                if (!ev.error.empty()) schedulerForeignErrorsTotal++;
                                if (!ev.aliveAfter) foreignIds.erase(ev.id); // its id may be reused later
                                return;
                            }
                            if (ev.resumed) {
                                mr.schedulerResumes++;
                                ownResumeThisTick = true;
                            }
                            if (!ev.error.empty()) {
                                mr.schedulerThreadErrors++;
                                errorsInOrder.push_back(ev.error); // the runner's "<name>: <message>"
                                printTraceRingIfAny(currentResumeCo, ev.name, ev.error);
                            }
                            if (!ev.fetchError.empty())
                                errorsInOrder.push_back("lua_host_run: runner could not fetch " + ev.name + ": " +
                                                        ev.fetchError);
                            if (startTracked && ev.id == startResult.recordId && !ev.aliveAfter) {
                                startTracked = false;
                                if (!ev.error.empty()) {
                                    mr.startAfterTicks = "errored";
                                    mr.startAfterResumeError = ev.error;
                                } else if (ev.killedBefore) {
                                    mr.startAfterTicks = "killed";
                                } else {
                                    mr.startAfterTicks = "finished";
                                }
                            }
                        });
                }
                ++globalTick;
                // ===================== AUTO-ANSWER-HUD-PROMPTS (THIS TASK) =====================
                // See --auto-answer-hud-prompts's own usage text (above) for the
                // full citation. Any hud_prompt request recorded this tick (or
                // any earlier tick not yet answered - hudPromptRespondedCount
                // tracks the boundary) is answered once it has been showing for
                // at least hudPromptAutoAnswerDelayTicks ticks: spawn a
                // coroutine for its own registered callback name via the SAME
                // BareThreadTable mechanism --probe-autopilot's own dispatch
                // block (further above) already uses. Spawn only - the callback
                // body runs on the NEXT tick's own scheduler pass, same
                // decoupled-in-time shape Sec1.14 confirms for the real
                // dispatcher, not a synchronous call here.
                if (autoAnswerHudPrompts) {
                    auto& hudPromptReqs = host.engineState().hudPromptRequests();
                    while (hudPromptFirstSeenGlobalTick.size() < hudPromptReqs.size()) {
                        hudPromptFirstSeenGlobalTick.push_back(globalTick);
                    }
                    while (hudPromptRespondedCount < hudPromptReqs.size()) {
                        const auto& req = hudPromptReqs[hudPromptRespondedCount];
                        const long long firstSeen = hudPromptFirstSeenGlobalTick[hudPromptRespondedCount];
                        if (globalTick - firstSeen < hudPromptAutoAnswerDelayTicks) {
                            // Not due yet - later requests (recorded even more
                            // recently) can't be due either, so stop here and
                            // re-check next tick.
                            break;
                        }
                        ++hudPromptRespondedCount;
                        if (req.arg3.empty()) continue; // no callback name to respond with
                        // CHOSEN stand-in (hud-prompt-direct-callback-standin task, 2026-10-12):
                        // the real dispatch path that resolves hud_prompt's arg3 to a callback is
                        // not yet traced (spec-lua-api-behaviour.md Sec13.3, 2026-10-12 update -
                        // confirmed arg3 genuinely IS the callback name the engine calls, e.g.
                        // "m05_jump_local" in m05.lua, but via a mechanism NOT inside
                        // 0x00a4cff0/0x00a37210/0x00a370c0's own call chain). Measured
                        // (results/verify_m05_hud_prompt_autoanswer_20261012.md): spawning a
                        // coroutine for arg3 via BareThreadTable::allocate() (the prior approach)
                        // does NOT advance m05 past m05.lua:1141, even given 185 further ticks for
                        // the spawned coroutine to run. This directly calls the named global Lua
                        // function by string lookup instead (Host::callGlobalFunctionByName(),
                        // lua_host.cpp) - NOT how the real engine dispatches it, but produces the
                        // same observable effect (the callback body actually running, setting
                        // whatever flag the waiting script polls) for any script that only cares
                        // about the callback having run. Synchronous, not spawn-and-defer: there is
                        // no real-engine decoupled-dispatch evidence for THIS specific call (unlike
                        // on_trigger/on_death, Sec1.14), so no coroutine shape is assumed either way.
                        std::cerr << "[auto-answer-hud-prompts] hud_prompt(\"" << req.target << "\", \""
                                  << req.arg2 << "\", \"" << req.arg3 << "\") seen " << (globalTick - firstSeen)
                                  << " tick(s) ago - CHOSEN stand-in: calling global '" << req.arg3
                                  << "' directly\n";
                        std::string callErr;
                        bool called = host.callGlobalFunctionByName(host.gameplayState(), req.arg3, callErr);
                        if (!called) {
                            std::cerr << "[auto-answer-hud-prompts] callback '" << req.arg3
                                      << "' not called: " << (callErr.empty() ? "no such global function" : callErr)
                                      << "\n";
                        }
                    }
                }
                auto tickHooks = host.fireConfirmedHooks(host.gameplayState(), hooks, script.entryName);
                TickSig sig;
                bool newWatchdogThisTick = false;
                for (auto& hr : tickHooks) {
                    if (hr.existedAsFunction) sig.existed++;
                    if (hr.attemptedCall) {
                        if (hr.callOk) {
                            sig.ok++;
                        } else {
                            sig.err++;
                            errorsInOrder.push_back(hr.callError);
                            if (hr.callError.find("watchdog") != std::string::npos) {
                                sig.watchdogAbort = true;
                                newWatchdogThisTick = true;
                            }
                        }
                    }
                }
                mr.ticksSurvived = tick;
                if (newWatchdogThisTick) {
                    mr.stopReason = "error (a hook call newly hit the instruction watchdog this tick)";
                    stopped = true;
                    break;
                }
                // A fade transition still in flight (state != target) is real,
                // silent progress the hook-firing signature above can't see -
                // screenFadeHostFrame() keeps advancing toward its CONFIRMED
                // (Sec26.24) host-side timeout completion every tick, but
                // nothing about that shows up in sig.{existed,ok,err} while the
                // suspended mission thread that's waiting on it isn't resumed.
                // Without this check the loop declared "completion" after 2
                // ticks (666ms) even when the fade's own request duration was
                // longer, so the host-side substitute never got the chance to
                // fire (found 2026-10-02, purrsian-d3's own next-blocker
                // follow-up). Only suppresses the early-stop; never extends the
                // loop past kMissionTickBudget.
                const auto& fade = host.engineState().screenFade();
                const bool fadePending = fade.state.known() && fade.target.known() &&
                                          fade.state.get() != fade.target.get();
                // Same reasoning for this mission's own script threads: a
                // resumed thread may be making progress the hook signature
                // can't see (a poll loop, a countdown), so a tick that resumed
                // any of them never counts toward "completion".
                if (havePrevSig && sig.existed == prevSig.existed && sig.ok == prevSig.ok && sig.err == prevSig.err &&
                    !fadePending && !ownResumeThisTick) {
                    mr.stopReason = "completion (2 consecutive identical tick signatures - real, measured "
                                     "idempotence, CHOSEN stopping convention, see this block's own top comment)";
                    stopped = true;
                    break;
                }
                prevSig = sig;
                havePrevSig = true;
            }
            if (!stopped) {
                mr.stopReason = "budget exhaustion (" + std::to_string(kMissionTickBudget) + " ticks)";
            }
            if (startTracked) {
                const BareThreadTable::Record* rec = host.bareGlobals().threads().findAlive(startResult.recordId);
                if (rec && rec->name == funcName) {
                    mr.startAfterTicks = "still_suspended";
                    lua_Debug ar;
                    if (lua_getstack(rec->co, 1, &ar) && lua_getinfo(rec->co, "Sln", &ar)) {
                        mr.startStillSuspendedAt = (ar.short_src[0] ? ar.short_src : "?") + std::string(":") +
                                                   std::to_string(ar.currentline) +
                                                   (ar.name ? (std::string(" (in ") + ar.name + ")") : "");
                    }
                    // Per-coroutine ring (2026-10-03): this mission's own
                    // thread was resumed on its own schedulerPass passes
                    // throughout the tick loop above, each resume
                    // overwriting g_traceRings[rec->co] fresh - so reading
                    // it back here by this SAME co is always that thread's
                    // own last resume, never another thread's, answering
                    // "what does this mission's own wait poll" for the
                    // first time (the global-ring version could not).
                    printTraceRingStillSuspended(rec->co, funcName);
                    // (2026-10-07) what this same coroutine called on its
                    // own FIRST resume - its _start call's own setup, before
                    // it ever reached the wait loop printed just above - see
                    // g_firstResumeTraceRings' own doc comment above.
                    printTraceRingFirstResume(rec->co, funcName);

                    // --probe-autopilot (2026-10-07, this task): DIAGNOSTIC
                    // ONLY, this entire block is skipped unless --probe-
                    // autopilot was given - never changes mr.startAfterTicks/
                    // startStillSuspendedAt/any REAL field above, only the
                    // separate probeAutopilot* fields. See that flag's own
                    // usage text for the full citation list of which natives
                    // this synthesizes and which were checked and excluded.
                    if (probeAutopilot) {
                        mr.probeAutopilotAttempted = true;
                        lua_State* missionCo = rec->co; // captured now - `rec` itself is not held past this point
                        auto regIt = g_firstResumeRegistrations.find(missionCo);
                        const CapturedRegistration* chosenReg = nullptr;
                        if (regIt != g_firstResumeRegistrations.end()) {
                            for (const auto& cand : regIt->second) {
                                if (!cand.callbackName.empty()) { chosenReg = &cand; break; }
                            }
                        }
                        if (!chosenReg) {
                            // Real finding, not an omission (this task's own
                            // instruction): the first-resume trace shows no
                            // CONFIRMED callback-registration native with a
                            // recoverable callback-name argument before this
                            // mission's wait loop - matches the already-
                            // established answer for m06/m10/m11/m13/m14/m15
                            // (results/trace_host_calls_firstresume_20261007.md).
                            mr.probeAutopilotNote =
                                "nothing to synthesize - wait is likely satisfied by polled world state, not a "
                                "callback (first-resume trace shows no confirmed callback-registration native "
                                "call with a recoverable callback-name argument before the wait loop)";
                            std::cerr << "[probe-autopilot] " << funcName << " nothing to synthesize: "
                                      << mr.probeAutopilotNote << "\n";
                        } else {
                            mr.probeAutopilotHadCallback = true;
                            mr.probeAutopilotNative = chosenReg->nativeName;
                            mr.probeAutopilotCallbackName = chosenReg->callbackName;
                            mr.probeAutopilotTargetName = chosenReg->targetName;

                            // Dispatch, CORRECTED 2026-10-09 (spec-lua-api-
                            // behaviour.md Sec1.14's own "GAP CLOSED" finding,
                            // 2026-10-09 changelog entry, commit 383f653):
                            // the real dispatcher 0x00a29e40 does NOT call
                            // lua_call/lua_pcall synchronously. It hands off
                            // to the reader 0x00a1fe10, which SPAWNS A NEW
                            // LUA COROUTINE for the resolved callback name
                            // (0x00e0ca80->0x00e0c720, the same mechanism
                            // Sec26.27 already documents for thread_new) -
                            // the body only actually runs LATER, when the
                            // scheduler 0x00e0cba0 resumes it. The previous
                            // version of this block (a bare lua_getglobal +
                            // lua_pcall, matching Host::fireConfirmedHooks'
                            // shape) was this project's own CHOSEN stand-in,
                            // made before Sec1.14's dispatch mechanism was
                            // known - superseded now that it is. This reuses
                            // the EXACT same coroutine mechanism
                            // callMissionStart() above already goes through
                            // (BareThreadTable::allocate()/run(), the host's
                            // model of runner 0x00e0cba0) - no second
                            // coroutine mechanism invented for this. Spawn
                            // only: allocate() never resumes the coroutine by
                            // itself, so the callback body provably has NOT
                            // run yet at this point in the code - only the
                            // follow-up window's own schedulerPass() calls,
                            // below, can resume it (see
                            // BareThreadTable::schedulerPass's own doc
                            // comment, bare_globals.h, for why one call per
                            // tick already matches the real engine's own
                            // steady-state cadence). hasParent=false: nothing
                            // is "current" when an engine-side trigger/hook
                            // fires from outside any running script thread -
                            // the same root-level situation
                            // BareThreadTable::startThread() models for a
                            // mission's own top-level entry. Zero arguments:
                            // unchanged from before (isCallbackRegistrationNative's
                            // own doc comment - no spec section establishes the
                            // FIRED callback's own argument shape, only the
                            // registration call's).
                            lua_State* L = host.gameplayState();
                            std::string targetNote =
                                chosenReg->targetName.empty() ? std::string() : (", \"" + chosenReg->targetName + "\"");
                            std::cerr << "[probe-autopilot] " << funcName << " SYNTHETIC dispatch: first-resume "
                                      << "trace shows " << chosenReg->nativeName << "(\"" << chosenReg->callbackName
                                      << "\"" << targetNote << ") registered before the wait loop - spawning a "
                                      << "coroutine for global '" << chosenReg->callbackName << "', zero args "
                                      << "(Sec1.14: real dispatch spawns a coroutine, never a synchronous call - "
                                      << "it only runs on a later scheduler pass)\n";
                            BareThreadTable::Record* spawnedRec = host.bareGlobals().threads().allocate(
                                L, chosenReg->callbackName, /*nargs=*/0, /*hasParent=*/false);
                            uint16_t spawnedId = BareThreadTable::kNoThread;
                            if (!spawnedRec) {
                                mr.probeAutopilotDispatchResult =
                                    "SYNTHETIC: script-thread table full (" +
                                    std::to_string(BareThreadTable::kCapacity) +
                                    " records, HIGH CONFIDENCE capacity) - coroutine for '" +
                                    chosenReg->callbackName + "' not spawned";
                            } else {
                                spawnedId = spawnedRec->id;
                                mr.probeAutopilotDispatchResult =
                                    "SYNTHETIC: coroutine spawned for '" + chosenReg->callbackName +
                                    "' (record id " + std::to_string(spawnedId) +
                                    ") - not run synchronously; the follow-up window's scheduler pass(es) below "
                                    "resume it";
                            }

                            // Follow-up window: bounded, CHOSEN
                            // (kProbeAutopilotFollowUpTicks) - same per-tick
                            // shape as the main loop above, just shorter.
                            // Never feeds errorsInOrder/mr.firstErrorMessage/
                            // mr.startAfterTicks (those are this mission's
                            // REAL, already-final measurement); collected
                            // separately into probeErrors below.
                            std::vector<std::string> probeErrors;
                            bool ownRecordResumedInFollowup = false;
                            // Tracking for the SPAWNED callback coroutine itself (spawnedId,
                            // above) - separate from ownRecordResumedInFollowup, which is
                            // about whether the MISSION's own wait unblocked. These three
                            // answer "did the real scheduler mechanism actually carry the
                            // spawn through to a real resume", the thing this whole change
                            // is about proving.
                            bool spawnedResumedInFollowup = false;
                            bool spawnedBodyRan = false;
                            std::string spawnedFetchError;
                            std::string spawnedOwnError;
                            for (int ftick = 0; ftick < kProbeAutopilotFollowUpTicks; ++ftick) {
                                host.engineState().screenFadeHostFrame(kFadeHostMsPerTick);
                                host.engineState().cutsceneHostFrame();
                                host.engineState().gameClockAdvanceFrame(static_cast<double>(kFadeHostMsPerTick) /
                                                                          1000.0);
                                host.engineState().tickCharacterDeaths();
                                for (int pass = 0; pass < kSchedulerPassesPerTick; ++pass) {
                                    host.runScriptThreadSchedulerPass(
                                        [&](const BareThreadTable::Record& r2) {
                                            resetTraceRing(r2.co);
                                            lua_sethook(r2.co, missionWatchdogHook, missionWatchdogHookMask(),
                                                        kMissionWatchdogInstructionBudget);
                                        },
                                        [&](const BareThreadTable::PassEvent& ev) {
                                            if (ev.id == spawnedId && spawnedId != BareThreadTable::kNoThread) {
                                                if (ev.resumed) spawnedResumedInFollowup = true;
                                                if (ev.bodyRan) spawnedBodyRan = true;
                                                if (!ev.fetchError.empty()) spawnedFetchError = ev.fetchError;
                                                if (!ev.error.empty()) spawnedOwnError = ev.error;
                                            }
                                            if (foreignIds.count(ev.id)) return; // not this mission's own concern
                                            if (ev.id == startResult.recordId && ev.resumed)
                                                ownRecordResumedInFollowup = true;
                                            if (!ev.error.empty()) probeErrors.push_back(ev.error);
                                            if (!ev.fetchError.empty())
                                                probeErrors.push_back("lua_host_run: runner could not fetch " +
                                                                       ev.name + ": " + ev.fetchError);
                                        });
                                }
                            }
                            // Finalize the spawned coroutine's own outcome now that the
                            // follow-up window's scheduler passes have had a chance to
                            // resume it - this is the part that genuinely only becomes
                            // knowable LATER, matching the real decoupled-in-time mechanism
                            // (Sec1.14): at the point of the spawn itself, above, nothing
                            // could yet be said about whether the callback ran or what it did.
                            if (spawnedId != BareThreadTable::kNoThread) {
                                if (!spawnedResumedInFollowup) {
                                    mr.probeAutopilotDispatchResult +=
                                        " / still not resumed after " + std::to_string(kProbeAutopilotFollowUpTicks) +
                                        " follow-up ticks";
                                } else if (!spawnedFetchError.empty()) {
                                    mr.probeAutopilotDispatchResult =
                                        "SYNTHETIC: callback name '" + chosenReg->callbackName +
                                        "' registered by " + chosenReg->nativeName +
                                        " could not be resolved to a global function when its spawned coroutine "
                                        "was resumed (" +
                                        spawnedFetchError + ") - nothing ran";
                                } else if (!spawnedOwnError.empty()) {
                                    mr.probeAutopilotDispatchResult =
                                        "SYNTHETIC dispatch error (on later resume): " + spawnedOwnError;
                                } else if (spawnedBodyRan) {
                                    mr.probeAutopilotDispatchResult =
                                        "SYNTHETIC dispatch ok (ran on a later scheduler pass, no Lua error)";
                                }
                            }
                            if (!probeErrors.empty()) {
                                mr.probeAutopilotNextBlocker = "SYNTHETIC-probe next error: " + probeErrors.front();
                            } else {
                                const BareThreadTable::Record* rec2 =
                                    host.bareGlobals().threads().findAlive(startResult.recordId);
                                if (rec2 && rec2->name == funcName) {
                                    std::string nowAt;
                                    lua_Debug ar2;
                                    if (lua_getstack(rec2->co, 1, &ar2) && lua_getinfo(rec2->co, "Sln", &ar2)) {
                                        nowAt = (ar2.short_src[0] ? ar2.short_src : "?") + std::string(":") +
                                                std::to_string(ar2.currentline) +
                                                (ar2.name ? (std::string(" (in ") + ar2.name + ")") : "");
                                    }
                                    if (!nowAt.empty() && nowAt != mr.startStillSuspendedAt) {
                                        mr.probeAutopilotNextBlocker =
                                            "SYNTHETIC-probe: advanced to a new wait, no new error - now parked "
                                            "at " +
                                            nowAt;
                                    } else if (ownRecordResumedInFollowup) {
                                        mr.probeAutopilotNextBlocker =
                                            "SYNTHETIC-probe: resumed but parked at the SAME wait location after " +
                                            std::to_string(kProbeAutopilotFollowUpTicks) +
                                            " follow-up ticks - the synthesized event did not change where it "
                                            "waits";
                                    } else {
                                        mr.probeAutopilotNextBlocker =
                                            "SYNTHETIC-probe: thread was never resumed again in " +
                                            std::to_string(kProbeAutopilotFollowUpTicks) +
                                            " follow-up ticks after the synthesized dispatch";
                                    }
                                } else {
                                    mr.probeAutopilotNextBlocker =
                                        "SYNTHETIC-probe: thread no longer in the table after the synthesized "
                                        "dispatch (finished or released), no error recorded";
                                }
                            }
                            std::cerr << "[probe-autopilot] " << funcName << " dispatch_result=["
                                      << mr.probeAutopilotDispatchResult << "] next_blocker=["
                                      << mr.probeAutopilotNextBlocker << "]\n";
                        }
                    }
                } else {
                    mr.startAfterTicks = "released_unobserved"; // left the table outside a scheduler pass
                }
            }
        }

        if (!errorsInOrder.empty()) mr.firstErrorMessage = errorsInOrder.front();
        for (auto& e : errorsInOrder) {
            std::string name = extractNilGlobalName(e);
            if (!name.empty()) { mr.firstUnimplementedStub = name; break; }
        }

        // The headline measurement (see MissionResult::missionCompletionFlagSet's
        // own doc comment above): read back THIS mission's own real +0x88
        // bit 0x4 now that its tick loop has stopped - whatever got it
        // there (mission_set_completed called by the mission's own script,
        // or the organic missionEndDispatch() type-1/2 paths, Sec21.27b).
        mr.missionCompletionFlagSet = host.engineState().missionCompletionFlagBitSet(mr.stem);

        missionResults.push_back(std::move(mr));
        if (missionResults.size() % 10 == 0 || missionResults.size() == missions.size()) {
            std::cout << "  ... " << missionResults.size() << "/" << missions.size() << " missions driven\n";
        }
    }
}

} // namespace luahostrun
