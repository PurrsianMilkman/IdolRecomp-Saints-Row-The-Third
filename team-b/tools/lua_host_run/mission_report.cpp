// lua_host_run main() split: step 1's reports (verdict_mission_drive.tsv, the with-missions stub hits, the summary).
// Code moved verbatim out of the former single-function main() (see lua_host_run_session.h).
#include "lua_host_run_session.h"

namespace luahostrun {

void reportMissionDrive(LuaHostRunSession& sess) {
    auto& probeUnresolvedNames = sess.probeUnresolvedNames;
    auto& probeAutopilot = sess.probeAutopilot;
    auto& outDir = sess.outDir;
    auto& host = *sess.host_;
    auto& staticRank = sess.staticRank;
    auto& missionRows = sess.missionRows;
    auto& missions = sess.missions;
    auto& kMissionTickBudget = sess.kMissionTickBudget;
    auto& missionResults = sess.missionResults;
    auto& schedulerForeignErrorsTotal = sess.schedulerForeignErrorsTotal;
    auto& preMissionCounts = sess.preMissionCounts;

    std::ofstream missionOut(outDir / "verdict_mission_drive.tsv");
    missionOut << "stem\tentry_name\tcontainer_name\tscript_found\tload_ok\tload_error\tpcall_ok\tpcall_error\t"
                  "start_func_name\tstart_existed\tstart_attempted\tstart_call_ok\tstart_call_error\t"
                  "ticks_survived\tstop_reason\tfirst_error_message\tfirst_unimplemented_stub\tstart_suspended\t"
                  "start_suspended_at\tstart_after_ticks\tstart_after_resume_error\tstart_still_suspended_at\t"
                  "scheduler_resumes\tscheduler_thread_errors\tscheduler_foreign_resumes\t"
                  "probe_autopilot_attempted\tprobe_autopilot_native\tprobe_autopilot_callback_name\t"
                  "probe_autopilot_target_name\tprobe_autopilot_dispatch_result\tprobe_autopilot_next_blocker\t"
                  "probe_autopilot_note\tmission_completion_flag_0x4_set\n";
    uint64_t missionsFoundCount = 0, missionsStartExistedCount = 0, missionsStartOkCount = 0;
    uint64_t missionsStartSuspendedCount = 0;
    uint64_t missionsStartStillSuspended = 0, missionsStartFinishedAfterResume = 0;
    uint64_t missionsStartErroredAfterResume = 0, missionsStartKilledAfterResume = 0;
    uint64_t schedulerResumesTotal = 0, schedulerOwnErrorsTotal = 0, schedulerForeignResumesTotal = 0;
    uint64_t stopBudget = 0, stopError = 0, stopCompletion = 0;
    uint64_t ticksSum = 0;
    // THE headline count this task (2026-10-08) was asked for: how many of
    // the 49 missions' own mission handles actually reached the real
    // completion flag (+0x88 bit 0x4, Sec15.10/Sec21.27b) - see
    // MissionResult::missionCompletionFlagSet's own doc comment above for
    // why this is deliberately separate from stopReason's own "completion"
    // bucket (a different, pre-existing, Lua-coroutine-idle measurement).
    uint64_t missionsCompletionFlagSetCount = 0;
    for (auto& mr : missionResults) {
        if (mr.scriptFound) missionsFoundCount++;
        if (mr.startExisted) missionsStartExistedCount++;
        if (mr.startCallOk) missionsStartOkCount++;
        if (mr.startSuspended) missionsStartSuspendedCount++;
        if (mr.missionCompletionFlagSet) missionsCompletionFlagSetCount++;
        ticksSum += (uint64_t)mr.ticksSurvived;
        if (mr.stopReason.rfind("budget exhaustion", 0) == 0) stopBudget++;
        else if (mr.stopReason.rfind("error", 0) == 0) stopError++;
        else if (mr.stopReason.rfind("completion", 0) == 0) stopCompletion++;
        missionOut << sanitizeTsv(mr.stem) << "\t" << sanitizeTsv(mr.entryName) << "\t"
                   << sanitizeTsv(mr.containerName) << "\t" << (mr.scriptFound ? 1 : 0) << "\t"
                   << (mr.loadOk ? 1 : 0) << "\t" << sanitizeTsv(mr.loadError) << "\t" << (mr.pcallOk ? 1 : 0)
                   << "\t" << sanitizeTsv(mr.pcallError) << "\t" << sanitizeTsv(mr.stem + "_start") << "\t"
                   << (mr.startExisted ? 1 : 0) << "\t" << (mr.startAttempted ? 1 : 0) << "\t"
                   << (mr.startCallOk ? 1 : 0) << "\t" << sanitizeTsv(mr.startCallError) << "\t"
                   << mr.ticksSurvived << "\t" << sanitizeTsv(mr.stopReason) << "\t"
                   << sanitizeTsv(mr.firstErrorMessage) << "\t" << sanitizeTsv(mr.firstUnimplementedStub) << "\t"
                   << (mr.startSuspended ? 1 : 0) << "\t" << sanitizeTsv(mr.startSuspendedAt) << "\t"
                   << sanitizeTsv(mr.startAfterTicks) << "\t" << sanitizeTsv(mr.startAfterResumeError) << "\t"
                   << sanitizeTsv(mr.startStillSuspendedAt) << "\t" << mr.schedulerResumes << "\t"
                   << mr.schedulerThreadErrors << "\t" << mr.schedulerForeignResumes << "\t"
                   << (mr.probeAutopilotAttempted ? 1 : 0) << "\t" << sanitizeTsv(mr.probeAutopilotNative) << "\t"
                   << sanitizeTsv(mr.probeAutopilotCallbackName) << "\t"
                   << sanitizeTsv(mr.probeAutopilotTargetName) << "\t"
                   << sanitizeTsv(mr.probeAutopilotDispatchResult) << "\t"
                   << sanitizeTsv(mr.probeAutopilotNextBlocker) << "\t" << sanitizeTsv(mr.probeAutopilotNote)
                   << "\t" << (mr.missionCompletionFlagSet ? 1 : 0) << "\n";
        if (mr.startAfterTicks == "still_suspended") missionsStartStillSuspended++;
        else if (mr.startAfterTicks == "finished") missionsStartFinishedAfterResume++;
        else if (mr.startAfterTicks == "errored") missionsStartErroredAfterResume++;
        else if (mr.startAfterTicks == "killed") missionsStartKilledAfterResume++;
        schedulerResumesTotal += mr.schedulerResumes;
        schedulerOwnErrorsTotal += mr.schedulerThreadErrors;
        schedulerForeignResumesTotal += mr.schedulerForeignResumes;
    }
    missionOut.close();

    // --- NEW runtime stub-hit ranking folding in the mission-driven calls
    // (this task's own explicit deliverable "meant to directly guide Team
    // A's next spec tranches") - SAME shape as verdict_stub_hits_with_
    // hooks.tsv above, plus one new incremental_from_missions column (the
    // SAME before/after HitLog-snapshot-delta convention already used
    // throughout this file). Reads host.hitLog() at the very end of this
    // whole run - i.e. genuinely ALL-INCLUSIVE: top-level (step 0,
    // restricted) + hook-firing (Sec9.140, restricted) + this step's own
    // mission-driven runChunk/`_start`/tick calls, all folded together,
    // never silently overwriting either pre-existing ranking file. -------
    std::ofstream missionHitsOut(outDir / "verdict_stub_hits_with_missions.tsv");
    missionHitsOut << "name\tcall_count_all_inclusive\tcall_count_before_missions(top_level+hooks)\t"
                      "incremental_from_missions\tlast_argc\tlast_arg_types\tstatic_total_call_sites\t"
                      "static_distinct_scripts\tgameplay_call_count\tui_call_count\n";
    std::vector<std::pair<std::string, sr3luahost::StubHit>> hitRowsWithMissions(host.hitLog().hits().begin(),
                                                                                  host.hitLog().hits().end());
    std::sort(hitRowsWithMissions.begin(), hitRowsWithMissions.end(), [](const auto& a, const auto& b) {
        if (a.second.callCount != b.second.callCount) return a.second.callCount > b.second.callCount;
        return a.first < b.first;
    });
    uint64_t missionIncrementalTotal = 0;
    for (auto& [name, hit] : hitRowsWithMissions) {
        auto beforeIt = preMissionCounts.find(name);
        uint64_t before = (beforeIt != preMissionCounts.end()) ? beforeIt->second : 0;
        uint64_t incremental = hit.callCount - before; // never negative: HitLog only ever accumulates
        missionIncrementalTotal += incremental;
        auto it = staticRank.find(name);
        uint64_t sCalls = (it != staticRank.end()) ? it->second.totalCallSites : 0;
        uint64_t sScripts = (it != staticRank.end()) ? it->second.distinctScripts : 0;
        missionHitsOut << name << "\t" << hit.callCount << "\t" << before << "\t" << incremental << "\t"
                       << hit.lastArgc << "\t" << hit.lastArgTypes << "\t" << sCalls << "\t" << sScripts << "\t"
                       << hit.gameplayCallCount << "\t" << hit.uiCallCount << "\n";
    }
    missionHitsOut.close();

    std::ofstream missionSummary(outDir / "verdict_mission_drive_summary.txt");
    auto mline = [&](const std::string& s) { missionSummary << s << "\n"; std::cout << s << "\n"; };
    mline("\n=== STEP 1 SUMMARY (real mission-driving pass) ===");
    mline("real_rows_loaded_from_mission_package_per_container_tsv=" + std::to_string(missionRows.size()));
    mline("distinct_stems_after_dedup_and_stem_covers_a_start_script_filter=" + std::to_string(missions.size()) +
          " (HANDOFF.md's own citation: \"49/54\" - reported here as this run's own actual count, not assumed)");
    // start_checkpoint_arg/start_is_restart_arg are CONFIRMED call-shape values
    // (spec-lua-api-behaviour.md Sec36.4, spec-lua-bindings.md Sec14.11, synced
    // 2026-10-02) - no longer CHOSEN, so reported on their own line, separate
    // from the genuinely CHOSEN values below (not recovered data).
    mline("CONFIRMED_values(spec-lua-api-behaviour.md Sec36.4/spec-lua-bindings.md Sec14.11 call shape, "
          "fresh start only - this harness has no resume/retry call site): start_checkpoint_arg=\"" +
          kMissionStartCheckpoint + "\", start_is_restart_arg=" +
          std::string(kMissionStartIsRestart ? "true" : "false"));
    // mission_simulated_seconds_budget: the tick budget re-expressed in this
    // host's own simulated time, so a caller doesn't have to multiply ticks
    // by fade_host_ms_per_tick by hand to judge whether "still suspended"
    // missions had enough modelled time to finish a long but legitimate wait
    // (e.g. a scripted delay/fade) before assuming they're stuck on a real
    // blocker - see --tick-budget='s own usage text.
    mline("mission_simulated_seconds_budget=" +
          std::to_string(static_cast<double>(kMissionTickBudget) * kFadeHostMsPerTick / 1000.0) +
          " (CHOSEN: mission_tick_budget * fade_host_ms_per_tick / 1000 - this host's own per-tick frame "
          "time, not a real-engine frame rate; raise via --tick-budget= to tell a long legitimate wait "
          "apart from a missing value, then read start_still_suspended_at per mission below)");
    mline("CHOSEN_values(not recovered data, stated per this project's own discipline): mission_tick_budget=" +
          std::to_string(kMissionTickBudget) +
          ", watchdog_instruction_budget=" + std::to_string(kMissionWatchdogInstructionBudget) +
          ", fade_host_ms_per_tick=" + std::to_string(kFadeHostMsPerTick) +
          ", scheduler_passes_per_tick=" + std::to_string(kSchedulerPassesPerTick) +
          " (CHOSEN: 1 tick == 1 rendered/game frame == 1 scheduler pass, matching the corrected spec's "
          "once-per-frame driver 0x00e0dfc0 - the old 11x figure approximated a ~30 ms independent pump "
          "the spec now confirms was boot-only, see this block's own top comment)" +
          ", get_frame_time_per_resume_s=" + std::to_string(static_cast<float>(kFadeHostMsPerTick) / 1000.0f) +
          " (CHOSEN: tied to fade_host_ms_per_tick so one resume's wall-clock delta matches one tick, see "
          "setFrameTime() call above)" +
          " (watchdog: same constant as host.h's own kHookWatchdogInstructionBudget, not independently chosen)");
    mline("missions_with_script_found=" + std::to_string(missionsFoundCount) + "/" + std::to_string(missions.size()));
    mline("missions_with_start_existed=" + std::to_string(missionsStartExistedCount) + "/" + std::to_string(missions.size()));
    mline("missions_with_start_call_ok=" + std::to_string(missionsStartOkCount) + "/" + std::to_string(missions.size()));
    // _start runs as a script-thread record (Sec26.27 runner, request 11): a
    // call that yielded without error counts as ok above and is listed here
    // (measured right after the call, before any tick). The tick loop's
    // scheduler passes (0x00e0cf50) then resume it; what became of it is the
    // missions_start_after_ticks line.
    mline("missions_with_start_suspended=" + std::to_string(missionsStartSuspendedCount) + "/" +
          std::to_string(missions.size()) + " (subset of start_call_ok: yielded on the first run, before any tick)");
    mline("missions_start_after_ticks=still_suspended:" + std::to_string(missionsStartStillSuspended) +
          " finished:" + std::to_string(missionsStartFinishedAfterResume) +
          " errored:" + std::to_string(missionsStartErroredAfterResume) +
          " killed:" + std::to_string(missionsStartKilledAfterResume) +
          " (of the missions_with_start_suspended records, after the tick loop's scheduler passes)");
    // probed_unresolved_names (DIAGNOSTIC ONLY, see --probe-unresolved-names'
    // own usage text above): every distinct name the named-object resolution
    // gate synthetically resolved during this run, so the probe's own effect
    // on the numbers above is never silently invisible. Empty/omitted when
    // the flag is off - this line only appears to avoid implying a probe
    // happened when it didn't.
    if (probeUnresolvedNames) {
        const auto& probed = host.engineState().probedNames();
        std::string namesJoined;
        for (const auto& n : probed) {
            if (!namesJoined.empty()) namesJoined += ", ";
            namesJoined += n;
        }
        mline("probed_unresolved_names=" + std::to_string(probed.size()) +
              " distinct name(s) synthetically resolved (DIAGNOSTIC, not real data): [" + namesJoined + "]");
        mline("probed_field_reads=" + std::to_string(host.engineState().probedFieldReadCount()) +
              " (DIAGNOSTIC: per-character/per-vehicle OPEN field reads that fell back to a labelled "
              "placeholder because their owner was itself a probed name - see probedFieldOr()'s own doc "
              "comment, engine_state.h)");
        mline("probed_world_position_reads=" + std::to_string(host.engineState().worldPositionProbeReadCount()) +
              " (DIAGNOSTIC: get_dist's own worldPosition OPEN gate, widened regardless of whether the "
              "owner name was itself probed - a fixed synthetic (0,0,0) position, never real data - see "
              "worldPositionOr()'s own doc comment, engine_state.h)");
        const auto& csm = host.engineState().controlStateMachine();
        mline("control_state_stack_after_missions=depth:" + std::to_string(csm.stack().size()) +
              " handles_issued:" + std::to_string(csm.handleCounter()) +
              " (spec-player-runtime.md Sec2.5 request stack, capacity 5; pushes are player_controls_disable's only)");
    }
    // probe_autopilot summary (DIAGNOSTIC ONLY, see --probe-autopilot's own
    // usage text above): aggregate outcome across every still_suspended
    // mission this run attempted it on - same "never silent, never implies
    // a probe happened when it didn't" convention as probed_unresolved_names
    // just above.
    if (probeAutopilot) {
        uint64_t attempted = 0, hadCallback = 0, nothingToSynthesize = 0;
        std::string missionsWithCallback, missionsWithNothing;
        for (const auto& mr : missionResults) {
            if (!mr.probeAutopilotAttempted) continue;
            attempted++;
            if (mr.probeAutopilotHadCallback) {
                hadCallback++;
                if (!missionsWithCallback.empty()) missionsWithCallback += ", ";
                missionsWithCallback += mr.stem + "(" + mr.probeAutopilotNative + ")";
            } else {
                nothingToSynthesize++;
                if (!missionsWithNothing.empty()) missionsWithNothing += ", ";
                missionsWithNothing += mr.stem;
            }
        }
        mline("probe_autopilot_missions_attempted=" + std::to_string(attempted) +
              " (of this run's own still_suspended missions)");
        mline("probe_autopilot_synthesized_events=" + std::to_string(hadCallback) +
              " distinct mission(s) with a SYNTHETIC callback dispatch (DIAGNOSTIC, not real data): [" +
              missionsWithCallback + "]");
        mline("probe_autopilot_nothing_to_synthesize=" + std::to_string(nothingToSynthesize) +
              " mission(s) whose first-resume trace showed no confirmed callback-registration native - a real "
              "finding (the wait is most likely satisfied by polled world state, not a callback), not an "
              "omission: [" + missionsWithNothing + "]");
    }
    mline("scheduler_passes=own_resumes:" + std::to_string(schedulerResumesTotal) +
          " own_thread_errors:" + std::to_string(schedulerOwnErrorsTotal) +
          " leftover_resumes:" + std::to_string(schedulerForeignResumesTotal) +
          " leftover_thread_errors:" + std::to_string(schedulerForeignErrorsTotal) +
          " (leftover = records still live from an earlier mission in the shared gameplay state)");
    mline("live_script_threads_after_missions=" + std::to_string(host.bareGlobals().threads().liveCount()) +
          " (capacity " + std::to_string(BareThreadTable::kCapacity) + ")");
    mline("stop_reason_histogram: budget_exhaustion=" + std::to_string(stopBudget) + " error=" +
          std::to_string(stopError) + " completion=" + std::to_string(stopCompletion) + " (sum should equal "
          "missions_with_script_found, since a not-found script gets its own separate \"script not found\" "
          "stop reason, counted under neither bucket above)");
    // THE headline number this task (2026-10-08, Sec21.27a/Sec21.27b real
    // phase machine + completion-flag writer) was asked to report:
    // genuinely distinct from stop_reason_histogram's own "completion"
    // bucket just above (that one only means "2 consecutive identical tick
    // signatures" - a Lua-idle measurement, not the mission's own
    // completion state). This counts missions whose own mission handle
    // actually reached +0x88 bit 0x4 (EngineState::missionCompletionFlagBitSet()).
    mline("missions_with_real_completion_flag_0x4_set=" + std::to_string(missionsCompletionFlagSetCount) + "/" +
          std::to_string(missions.size()) +
          " (the mission's own handle actually marked complete - mission_set_completed called by its own "
          "script, or an organic missionEndDispatch() type-1/2 path, Sec21.27b - NOT the same thing as "
          "stop_reason=completion above, which only means the tick loop went idle)");
    mline("total_ticks_survived_across_all_missions=" + std::to_string(ticksSum));
    mline("total_stub_calls_this_step_contributed(ALL-INCLUSIVE minus pre-step-1 snapshot, real HitLog delta)=" +
          std::to_string(missionIncrementalTotal));
    {
        // Sec26.25: the per-frame cutscene machine + zscene promotion/completion.
        // Also appended to verdict_summary.txt.
        const std::string csLine = "cutscene_frame=" + host.engineState().cutsceneFrameSummary();
        mline(csLine);
        std::ofstream summaryAppend(outDir / "verdict_summary.txt", std::ios::app);
        summaryAppend << csLine << "\n";
    }
    {
        // Screen fade (Sec26.24): which path completed each transition. real =
        // the UI script called Screen_fade_transition_complete; fallback_* =
        // the labelled HOST-SIDE SUBSTITUTE timer (screen_fade_do undefined /
        // called but never completing). Also appended to verdict_summary.txt.
        const auto& fc = host.engineState().screenFadeCounters();
        std::string fadeLine = "fade_completion_path=" + host.engineState().screenFadeCompletionPathSummary();
        std::string fadeDetail = "fade_detail=screen_fade_do_calls:" + std::to_string(fc.screenFadeDoCalls) +
                                 " screen_fade_do_errors:" + std::to_string(fc.screenFadeDoErrors) +
                                 " host_frames:" + std::to_string(fc.framesRun) +
                                 " frames_blocked_on_open:" + std::to_string(fc.framesBlockedOnOpen) +
                                 (fc.lastFrameBlocker.empty() ? std::string() : " last_blocker=[" + fc.lastFrameBlocker + "]");
        mline(fadeLine);
        mline(fadeDetail);
        std::ofstream summaryAppend(outDir / "verdict_summary.txt", std::ios::app);
        summaryAppend << fadeLine << "\n" << fadeDetail << "\n";
    }
    mline("\n--- Per-mission detail (also in verdict_mission_drive.tsv) ---");
    for (auto& mr : missionResults) {
        std::ostringstream row;
        row << "  " << mr.stem << ": script_found=" << (mr.scriptFound ? "yes" : "NO");
        if (mr.scriptFound) {
            row << " load_ok=" << (mr.loadOk ? 1 : 0) << " pcall_ok=" << (mr.pcallOk ? 1 : 0)
                << " start_existed=" << (mr.startExisted ? 1 : 0) << " start_call_ok=" << (mr.startCallOk ? 1 : 0)
                << (mr.startSuspended ? " start_suspended=1" : "") << " ticks_survived=" << mr.ticksSurvived << " stop_reason=[" << mr.stopReason << "]";
            if (mr.startAfterTicks == "still_suspended")
                row << " start_after_ticks=still_suspended waiting_at=["
                    << (mr.startStillSuspendedAt.empty() ? "?" : mr.startStillSuspendedAt) << "]";
            if (!mr.firstErrorMessage.empty()) row << " first_error=[" << mr.firstErrorMessage << "]";
            if (!mr.firstUnimplementedStub.empty()) row << " first_unimplemented_stub=" << mr.firstUnimplementedStub;
            if (mr.probeAutopilotAttempted) {
                if (mr.probeAutopilotHadCallback)
                    row << " probe_autopilot=[" << mr.probeAutopilotNative << "(\"" << mr.probeAutopilotCallbackName
                        << "\")" << " dispatch=[" << mr.probeAutopilotDispatchResult << "]"
                        << " next_blocker=[" << mr.probeAutopilotNextBlocker << "]]";
                else
                    row << " probe_autopilot=[" << mr.probeAutopilotNote << "]";
            }
        }
        mline(row.str());
    }
    missionSummary.close();

    std::cout << "\nWrote " << (outDir / "verdict_mission_drive.tsv").string() << " (new), "
              << (outDir / "verdict_stub_hits_with_missions.tsv").string() << " (new), "
              << (outDir / "verdict_mission_drive_summary.txt").string() << " (new)\n";
}

} // namespace luahostrun
