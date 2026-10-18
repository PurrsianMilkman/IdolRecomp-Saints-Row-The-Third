// lua_host_run main() split: verdict_summary.txt (the population summary).
// Code moved verbatim out of the former single-function main() (see lua_host_run_session.h).
#include "lua_host_run_session.h"

namespace luahostrun {

void writeSummary(LuaHostRunSession& sess) {
    auto& preloadStatesSpec164 = sess.preloadStatesSpec164;
    auto& hostRng = sess.hostRng;
    auto& hostRngSeed = sess.hostRngSeed;
    auto& probeUnresolvedNames = sess.probeUnresolvedNames;
    auto& vintDocs = sess.vintDocs;
    auto& probeAutopilot = sess.probeAutopilot;
    auto& autoAnswerHudPrompts = sess.autoAnswerHudPrompts;
    auto& hudPromptAutoAnswerDelayTicks = sess.hudPromptAutoAnswerDelayTicks;
    auto& probeAutopilotAutoRaisedRingSize = sess.probeAutopilotAutoRaisedRingSize;
    auto& outDir = sess.outDir;
    auto& t0 = sess.t0;
    auto& allNames = sess.allNames;
    auto& host = *sess.host_;
    auto& displayOption = sess.displayOption;
    auto& sceneTableSummary = sess.sceneTableSummary;
    auto& continuousExplosionTableSummary = sess.continuousExplosionTableSummary;
    auto& spawnRankHitPointsSummary = sess.spawnRankHitPointsSummary;
    auto& weaponDefinitionTableSummary = sess.weaponDefinitionTableSummary;
    auto& vehicleTypeClassTableSummary = sess.vehicleTypeClassTableSummary;
    auto& openStateSummary = sess.openStateSummary;
    const auto& hooks = *sess.hooks_;
    auto& hookNamesAlsoRegisteredAsStubs = sess.hookNamesAlsoRegisteredAsStubs;
    auto& archives = sess.archives;
    auto& st = sess.st;
    auto& grandBytes = sess.grandBytes;
    auto& parsedDocs = sess.parsedDocs;
    auto& docByScript = sess.docByScript;
    auto& docsParseFailed = sess.docsParseFailed;
    auto& docsLoaded = sess.docsLoaded;
    auto& docObjectsLoaded = sess.docObjectsLoaded;
    auto& docPropertiesLoaded = sess.docPropertiesLoaded;
    auto& docContextHits = sess.docContextHits;
    auto& docContextMisses = sess.docContextMisses;
    auto& tagUi = sess.tagUi;
    auto& tagGameplay = sess.tagGameplay;
    auto& tagOpen = sess.tagOpen;
    auto& tagConflict = sess.tagConflict;
    auto& pre_system_ui = sess.pre_system_ui;
    auto& pre_vint_lib = sess.pre_vint_lib;
    auto& pre_game_ui_globals = sess.pre_game_ui_globals;
    auto& pre_vdo_base = sess.pre_vdo_base;
    auto& pre_vdo_anim = sess.pre_vdo_anim;
    auto& pre_vdo_input = sess.pre_vdo_input;
    auto& pre_cell_foreground = sess.pre_cell_foreground;
    auto& pre_system_gp = sess.pre_system_gp;
    auto& gameLibIndices = sess.gameLibIndices;
    auto& gameLibLoadedOk = sess.gameLibLoadedOk;
    auto& gameLibRanOk = sess.gameLibRanOk;
    auto& gameLibLoadError = sess.gameLibLoadError;
    auto& gameLibRunError = sess.gameLibRunError;
    auto& staticRank = sess.staticRank;
    auto& toRun = sess.toRun;
    auto& loadOkCount = sess.loadOkCount;
    auto& pcallOkCount_gp = sess.pcallOkCount_gp;
    auto& pcallOkCount_ui = sess.pcallOkCount_ui;
    auto& agreeCount = sess.agreeCount;
    auto& disagreeCount = sess.disagreeCount;
    auto& historical = sess.historical;
    auto& historicalTotal = sess.historicalTotal;
    auto& hookAgg = sess.hookAgg;
    auto& watchdogAbortCount = sess.watchdogAbortCount;
    auto& skippedGpFireCount = sess.skippedGpFireCount;
    auto& skippedUiFireCount = sess.skippedUiFireCount;
    auto& skippedGpRunChunkCount = sess.skippedGpRunChunkCount;
    auto& skippedUiRunChunkCount = sess.skippedUiRunChunkCount;
    auto& spec164Overrides = sess.spec164Overrides;
    auto& hitRows = sess.hitRows;
    auto& hitRowsWithHooks = sess.hitRowsWithHooks;
    auto& hooksEverDefinedCount = sess.hooksEverDefinedCount;
    auto& hooksEverCalledOkCount = sess.hooksEverCalledOkCount;
    auto& hooksEverCalledErrCount = sess.hooksEverCalledErrCount;
    auto& errorMessageRows = sess.errorMessageRows;

    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();

    // --- Population summary, printed AND written -------------------------
    std::ofstream summary(outDir / "verdict_summary.txt");
    auto line = [&](const std::string& s) { summary << s << "\n"; std::cout << s << "\n"; };
    line("\n=== POPULATION SUMMARY (lua_host_run) ===");
    line("archives_scanned=" + std::to_string(archives.size()));
    line("open_state_slots_with_values_at_start=" + openStateSummary);
    line("display_option=" + displayOption);
    line("zscene_table=" + sceneTableSummary);
    line("continuous_explosion_table=" + continuousExplosionTableSummary);
    line("spawn_rank_hit_points=" + spawnRankHitPointsSummary);
    line("weapon_definition_table=" + weaponDefinitionTableSummary);
    line("vehicle_type_class_table=" + vehicleTypeClassTableSummary);
    line("vint_docs=" + std::string(vintDocs ? "on" : "off (--no-vint-docs)") + " found=" +
         std::to_string(st.docs.size()) + " parsed=" + std::to_string(parsedDocs.size()) +
         " parse_failed=" + std::to_string(docsParseFailed) + " scripts_indexed=" + std::to_string(docByScript.size()) +
         " loaded_so_far=" + std::to_string(docsLoaded) + " objects=" + std::to_string(docObjectsLoaded) +
         " properties=" + std::to_string(docPropertiesLoaded) + " context_hits=" + std::to_string(docContextHits) +
         " context_misses=" + std::to_string(docContextMisses) +
         " (real .vint_doc element trees in the Vint object registry; per-call current document = the "
         "document naming the defining chunk as lua_script_file - CHOSEN; see --no-vint-docs)");
    line("auto_answer_hud_prompts=" +
         std::string(autoAnswerHudPrompts ? ("on delay_ticks=" + std::to_string(hudPromptAutoAnswerDelayTicks))
                                           : "off"));
    {
        // Typed property path counters (EngineState::setVintProperty/
        // getVintProperty doc comments), cumulative at this point of the run.
        const auto& vp = host.engineState().vintPropertyStats();
        line("vint_properties=set_bad_handle:" + std::to_string(vp.setBadHandle) +
             " set_typed:" + std::to_string(vp.setTyped) +
             " set_tween_borrow_typed:" + std::to_string(vp.setTweenBorrowTyped) +
             " set_raw_open_code:" + std::to_string(vp.setRawOpenCode) +
             " set_raw_untyped_object:" + std::to_string(vp.setRawUntypedObject) +
             " set_raw_unknown_type:" + std::to_string(vp.setRawUnknownType) +
             " set_raw_enum_unresolved:" + std::to_string(vp.setRawEnumUnresolved) +
             " set_raw_tween_borrow_unresolved:" + std::to_string(vp.setRawTweenBorrowUnresolved) +
             " set_name_miss:" + std::to_string(vp.setNameMiss) +
             " set_read_only_noop:" + std::to_string(vp.setReadOnlyNoop) +
             " set_shape_refusals:" + std::to_string(vp.setShapeRefusals) +
             " set_callback_names_stored:" + std::to_string(vp.setCallbackNamesStored) +
             " screen_size_sets:" + std::to_string(vp.screenSizeSets) +
             " screen_size_div_zero_guards:" + std::to_string(vp.screenSizeDivZeroGuards) +
             " get_bad_handle:" + std::to_string(vp.getBadHandle) + " get_typed:" + std::to_string(vp.getTyped) + " get_raw_echo:" + std::to_string(vp.getRawEcho) +
             " get_computed:" + std::to_string(vp.getComputed) + " get_name_miss:" + std::to_string(vp.getNameMiss) +
             " get_open_refusals:" + std::to_string(vp.getOpenRefusals) +
             " doc_records_typed:" + std::to_string(vp.docRecordsTyped) +
             " doc_records_raw:" + std::to_string(vp.docRecordsRaw) +
             " doc_records_skipped_no_descriptor:" + std::to_string(vp.docRecordsSkippedNoDescriptor) +
             " doc_records_skipped_read_only:" + std::to_string(vp.docRecordsSkippedReadOnly) +
             " doc_objects_seeded:" + std::to_string(vp.docObjectsSeeded) +
             " (typed path = spec-lua-bindings.md Sec9/Sec20 descriptor tables + spec-vint-render.md Sec7/Sec8; "
             "raw = CHOSEN pass-through where clean spec text gives no code)");
    }
    line("total_bytes_read=" + std::to_string(grandBytes));
    line("total_directory_entries=" + std::to_string(st.stats.entries));
    line("real_lua_scripts_found(name ends '.lua')=" + std::to_string(st.found.size()));
    // --- Real preload results, both states (spec-lua-bindings.md Sec16.4) ---
    line("\n=== PRELOAD SUMMARY (spec-lua-bindings.md Sec16.4, real confirmed order) ===");
    for (const PreloadResult* pr : {&pre_system_ui, &pre_vint_lib, &pre_game_ui_globals, &pre_vdo_base,
                                     &pre_vdo_anim, &pre_vdo_input, &pre_cell_foreground, &pre_system_gp}) {
        std::string tag = "preload_" + pr->stateLabel + "_" + pr->name;
        line(tag + "_instances_found=" + std::to_string(pr->instancesFound));
        line(tag + "_load_ok=" + std::string(pr->loadOk ? "true" : "false"));
        line(tag + "_pcall_ok=" + std::string(pr->pcallOk ? "true" : "false"));
        if (!pr->loadOk && pr->instancesFound > 0) line(tag + "_load_error=" + sanitizeTsv(pr->loadError));
        else if (pr->loadOk && !pr->pcallOk) line(tag + "_pcall_error=" + sanitizeTsv(pr->pcallError));
    }
    line("game_lib_lua_instances_found=" + std::to_string(gameLibIndices.size()));
    line("game_lib_lua_first_instance_load_ok=" + std::string(gameLibLoadedOk ? "true" : "false"));
    line("game_lib_lua_first_instance_pcall_ok=" + std::string(gameLibRanOk ? "true" : "false"));
    if (!gameLibLoadedOk) line("game_lib_lua_load_error=" + sanitizeTsv(gameLibLoadError));
    else if (!gameLibRanOk) line("game_lib_lua_pcall_error=" + sanitizeTsv(gameLibRunError));
    line("scripts_attempted_this_run=" + std::to_string(toRun) + " (of " + std::to_string(st.found.size()) + " found)");
    line("real_luaL_loadbuffer_ok=" + std::to_string(loadOkCount) + "/" + std::to_string(toRun) +
         " (load is a pure syntax check, read from whichever state's runChunk() actually ran this script - "
         "see STATE-TAG RESTRICTION SUMMARY below; state-independent, so this denominator is unaffected by "
         "this task's own step-0 runChunk restriction)");
    line("real_lua_pcall_ok_gameplay_state=" + std::to_string(pcallOkCount_gp) + "/" +
         std::to_string(toRun - skippedGpRunChunkCount) + " of those whose gameplay-state runChunk() was "
         "actually attempted this run (this task, step 0: NOT loadOkCount any more - a ui-tagged script's "
         "gameplay-state runChunk() is now skipped by design, not attempted-and-failed; see "
         "scripts_with_gameplay_runChunk_skipped below)");
    line("real_lua_pcall_ok_ui_state=" + std::to_string(pcallOkCount_ui) + "/" +
         std::to_string(toRun - skippedUiRunChunkCount) + " of those whose ui-state runChunk() was actually "
         "attempted this run (same denominator fix as the gameplay line above)");
    line("sr3lua_parser_agrees_with_real_lua_load_result=" + std::to_string(agreeCount) + "/" + std::to_string(toRun));
    line("sr3lua_vs_real_lua_disagreements=" + std::to_string(disagreeCount));
    // NOTE on the two lines below: this project's own pre-existing metric
    // name ("total_stub_calls_recorded(both states, whole run)") is kept,
    // but its value now reflects host.hitLog() as it stands at the very
    // END of this run - i.e. ALL-INCLUSIVE (top-level + hook-triggered) -
    // since hook firing (new this task) shares the SAME HitLog. Relabeled
    // explicitly, rather than left to silently mean something different
    // than it did before this task, per this task's own "don't overwrite
    // the historical one silently" instruction. The TOP-LEVEL-ONLY
    // (historical, pre-this-task) figure is reported right after it,
    // under its own distinctly-named metric, computed from the separate
    // `historical` tracker above (never from host.hitLog() directly).
    line("total_stub_calls_recorded(ALL-INCLUSIVE: top-level + hook-triggered, both states, whole run)=" +
         std::to_string(host.hitLog().totalCalls()));
    line("total_stub_calls_recorded(TOP-LEVEL-ONLY, historical, matches Sec9.113's original baseline, "
         "both states, whole run)=" + std::to_string(historicalTotal));
    line("distinct_stub_names_that_fired_at_least_once(TOP-LEVEL-ONLY, historical, matches Sec9.113's "
         "original baseline)=" + std::to_string(historical.size()) + " (of " + std::to_string(allNames.size() + kHostExtraGlobals) +
         " registered - the +" + std::to_string(kHostExtraGlobals) + " are the 24 bare globals and thread_close, not in the " +
         std::to_string(allNames.size()) + "-name registration list file)");
    line("elapsed_seconds=" + std::to_string(seconds));

    line("\n=== HOOK-FIRING PASS SUMMARY (new this task; spec-lua-bindings.md Sec8.2-8.4/Sec12.2-12.9) ===");
    line("confirmed_hooks_fired_per_script=" + std::to_string(hooks.size()) +
         " (73 Group-1 general [existence-gated, CONFIRMED] + 2 Group-2 lifecycle [UNCONDITIONAL real "
         "call, CONFIRMED] + 58 Group-3 mission-numbered [existence-gated, HIGH CONFIDENCE / pattern-"
         "based, NOT individually dispatcher-confirmed - only 2 of the underlying 60 were, and those 2 "
         "are already counted in Group 1])");
    line("hook_argument_count_used=0 for every call (spec Sec8.3 confirms the call-in MECHANISM only, "
         "not per-hook argument shapes, which remain OPEN - see include/sr3luahost/hook_registry.h). Do "
         "NOT read a hook 'called ok' below as proof the real engine would also see it succeed with its "
         "own real (unconfirmed) arguments.");
    line("hooks_out_of_scope_by_design=Group 4 (7 sprintf-templated per-widget-instance families, spec "
         "Sec8.4) + Group 5 (2 data-driven runtime-populated tables, spec Sec8.5) - both explicitly "
         "unbounded/runtime-only per the spec itself, no static name list exists to fire, NOT counted "
         "anywhere in this run's numbers.");
    line("hook_fire_attempts_theoretical_max_pre_statetag_restriction(scripts x hooks x 2 states, what "
         "this run WOULD have attempted with no per-script state restriction, matching every prior "
         "baseline's own formula)=" + std::to_string(toRun * hooks.size() * 2));
    line("\n=== STATE-TAG RESTRICTION SUMMARY (this task; spec-lua-bindings.md Sec14.5) ===");
    line(std::string("host_rng_option=") +
         (hostRng ? "on seed=" + std::to_string(hostRngSeed) +
                        " (HYPOTHESIS / HOST SUBSTITUTE: deterministic host generator fills the 8192-entry ring once; "
                        "values are not the game's, spec-lua-api-behaviour.md Sec26.27)"
                  : std::string("off (default: CONFIRMED file-image ring - every rand_int/rand_float draw returns lo; "
                                "whether the engine fills the ring before scripts draw is OPEN)")) +
         " draws=" + std::to_string(host.bareGlobals().random().draws()));
    // probe_unresolved_names_option (DIAGNOSTIC ONLY, see usage text above
    // and OpenValueMap::enableProbeFallback's own doc comment, open_state.h):
    // reported here so any run's own verdict_summary.txt makes plain whether
    // its numbers used the real named-object resolution gate or the
    // synthetic probe stand-in - never ambiguous after the fact.
    line(std::string("probe_unresolved_names_option=") +
         (probeUnresolvedNames
              ? "on (DIAGNOSTIC: every miss on the named-object resolution map returns a synthetic "
                "\"resolves\" placeholder instead of refusing - every number below past such a miss is "
                "a synthetic stand-in, not a real measurement)"
              : "off (default: the real, genuinely-OPEN named-object resolution map refuses as normal)"));
    // probe_autopilot_option (DIAGNOSTIC ONLY, see --probe-autopilot's own
    // usage text above): same transparency convention as
    // probe_unresolved_names_option just above - whether this run's
    // still_suspended missions got a synthesized next-blocker probe, and
    // whether the trace ring size was auto-raised to get it (never silent).
    line(std::string("probe_autopilot_option=") +
         (probeAutopilot
              ? "on (DIAGNOSTIC: still_suspended missions get a synthesized callback dispatch, tagged "
                "SYNTHETIC, to find the next blocker past their wait - see probe_autopilot_* lines below "
                "and verdict_mission_drive.tsv's probe_autopilot_* columns)" +
                    std::string(probeAutopilotAutoRaisedRingSize
                                    ? " trace_ring_size auto-raised to " +
                                          std::to_string(kProbeAutopilotAutoRingSize) +
                                          " (--trace-host-calls-before-error was not explicitly given)"
                                    : " trace_ring_size=" + std::to_string(g_traceRingSize) +
                                          " (explicitly given via --trace-host-calls-before-error)")
              : "off (default: no synthesized events, no change to any mission's drive)"));
    {
        size_t resolved = 0, ran = 0;
        for (const auto& il : host.bareGlobals().includeQueue().log()) {
            resolved += il.resolved ? 1 : 0;
            ran += il.runOk ? 1 : 0;
        }
        line("include_loads=" + std::to_string(host.bareGlobals().includeQueue().log().size()) +
             " resolved=" + std::to_string(resolved) + " ran_ok=" + std::to_string(ran) +
             " (deferred include queue, spec-lua-bindings.md Sec16.4)");
        line("script_thread_errors=" + std::to_string(host.bareGlobals().threads().errors().size()) +
             " live_script_threads_at_end=" + std::to_string(host.bareGlobals().threads().liveCount()));
    }
    line(std::string("preload_states_option=") +
         (preloadStatesSpec164 ? "spec16.4 (default; Sec16.1/Sec16.4 CONFIRMED, cleared 2026-10-01)"
                               : "tag (opt-in old behaviour: preload files follow the per-script state tag)") +
         " scripts_rerouted=" + std::to_string(spec164Overrides));
    line("real_population_split(of " + std::to_string(st.found.size()) + " found scripts)=ui:" +
         std::to_string(tagUi) + " gameplay:" + std::to_string(tagGameplay) + " OPEN:" + std::to_string(tagOpen) +
         " conflict:" + std::to_string(tagConflict));
    line("engineering_choice_for_OPEN/conflict_scripts=RUN/FIRE AGAINST BOTH STATES (unchanged, original "
         "\"maximum observation\" behavior, now applying to BOTH the top-level runChunk() call site [this "
         "task, step 0] AND the hook-firing call site [Sec9.140]) - stated choice, not the only defensible "
         "one; see this file's own source comment near the top of the main loop for the full reasoning.");
    line("\n--- Step 0 (this task): top-level runChunk() restriction, the NEW call site this task closes ---");
    line("scripts_with_gameplay_runChunk_skipped(tagged ui - own top-level code no longer runs in the "
         "gameplay state at all, closing Sec9.140's own traced definition-leak cause)=" +
         std::to_string(skippedGpRunChunkCount));
    line("scripts_with_ui_runChunk_skipped(tagged gameplay - own top-level code no longer runs in the ui "
         "state at all)=" + std::to_string(skippedUiRunChunkCount));
    line("\n--- Hook-firing call site (Sec9.140, unchanged this task) ---");
    line("scripts_with_gameplay_hook_firing_skipped(tagged ui, hooks.size()=" + std::to_string(hooks.size()) +
         " real calls not attempted per skipped script)=" + std::to_string(skippedGpFireCount));
    line("scripts_with_ui_hook_firing_skipped(tagged gameplay, hooks.size()=" + std::to_string(hooks.size()) +
         " real calls not attempted per skipped script)=" + std::to_string(skippedUiFireCount));
    line("real_hook_fire_attempts_not_made_this_run(sum of the two skip counts above x " + std::to_string(hooks.size()) +
         " hooks each - the real gap between the theoretical-max line above and the real ok+erred total "
         "below)=" + std::to_string((skippedGpFireCount + skippedUiFireCount) * hooks.size()));
    line("KNOWN, STATED COVERAGE TRADEOFF: existedAsFunction is NOT measured for a skipped state (this "
         "task restricts fireConfirmedHooks()'s own CALL SITE, not just whether it acts on the result - "
         "adding a separate cheap existence-only check to Host was out of this task's own stated scope). "
         "gameplay_defined_in_N_scripts/ui_defined_in_N_scripts in verdict_hook_fires_by_hook.tsv can "
         "therefore read LOWER than a prior baseline for a hook whose OTHER-state definition used to be "
         "observed incidentally on a wrong-state script - a real, honest measurement-coverage change, "
         "separate from the ok/err tally change below.");
    line("hooks_defined_in_at_least_one_script(of " + std::to_string(hooks.size()) + " confirmed/pattern-based)=" +
         std::to_string(hooksEverDefinedCount));
    line("hook_calls_attempted_and_ok(both states, whole run)=" + std::to_string(hooksEverCalledOkCount));
    line("hook_calls_attempted_and_erred(both states, whole run)=" + std::to_string(hooksEverCalledErrCount));
    line("hook_calls_aborted_by_instruction_watchdog(subset of the erred count above; real, found risk - "
         "see host.h's own fireConfirmedHooks doc comment)=" + std::to_string(watchdogAbortCount));
    line("NOTE: hook firing runs against the SAME persistent gameplay_/ui_ states every script's own "
         "top-level run already shares - a hook name multiple scripts define will, by the time script N's "
         "own firing runs, invoke whichever script's definition is currently live in that shared global "
         "table, not necessarily script N's own. Pre-existing property of this scaffold's persistent-"
         "state design (Sec9.113), not new to this pass - stated here rather than silently papered over.");
    line("hook_names_also_present_in_the_registered_stub_list=" + std::to_string(hookNamesAlsoRegisteredAsStubs.size()) +
         " (of " + std::to_string(hooks.size()) + " hooks, against the " + std::to_string(allNames.size()) +
         "-name registered stub list) - a real, checked overlap, not assumed 0: for any name in this list, its own "
         "existedAsFunction/callOk numbers above are a genuine mixture of \"hit the always-succeeding "
         "generic logging stub already sitting in that global from Host construction\" and \"hit a real, "
         "possibly-erroring script-defined override that later replaced it\" - see this tool's own source "
         "comment (tools/lua_host_run.cpp) for the full reasoning.");
    if (!hookNamesAlsoRegisteredAsStubs.empty()) {
        std::string joined;
        for (size_t i = 0; i < hookNamesAlsoRegisteredAsStubs.size(); ++i) {
            if (i) joined += ", ";
            joined += hookNamesAlsoRegisteredAsStubs[i];
        }
        line("  overlapping name(s): " + joined);
    }

    line("\n=== NEW STUB-HIT RANKING INCLUDING HOOK-TRIGGERED CALLS (replaces the old ranking as the "
         "'real' one - the OLD, top-level-only ranking is kept, unmodified, in verdict_stub_hits.tsv; "
         "its own total/distinct-name figures are printed above, under their own TOP-LEVEL-ONLY-labeled "
         "lines, not repeated here) ===");
    line("distinct_stub_names_that_fired_at_least_once(ALL-INCLUSIVE)=" + std::to_string(host.hitLog().hits().size()) +
         " (of " + std::to_string(allNames.size() + kHostExtraGlobals) + " registered, +" + std::to_string(kHostExtraGlobals) +
         " being the 24 bare globals and thread_close)");

    line("\n=== TOP 25 STUB NAMES BY REAL RUNTIME HIT COUNT - TOP-LEVEL ONLY (historical, matches "
         "Sec9.113's original baseline exactly) ===");
    for (size_t i = 0; i < hitRows.size() && i < 25; ++i) {
        auto& [name, hit] = hitRows[i];
        auto it = staticRank.find(name);
        std::ostringstream row;
        row << "  " << (i + 1) << ". " << name << " runtime_calls=" << hit.callCount;
        if (it != staticRank.end()) {
            row << "  (static census: " << it->second.totalCallSites << " call sites / "
                << it->second.distinctScripts << " scripts)";
        } else {
            row << "  (not present in the static reconciliation file)";
        }
        line(row.str());
    }

    line("\n=== TOP 25 STUB NAMES BY REAL RUNTIME HIT COUNT - INCLUDING HOOK-TRIGGERED CALLS (NEW, this "
         "task's own 'MUCH more representative' ranking) ===");
    for (size_t i = 0; i < hitRowsWithHooks.size() && i < 25; ++i) {
        auto& [name, hit] = hitRowsWithHooks[i];
        auto histIt = historical.find(name);
        uint64_t histCount = (histIt != historical.end()) ? histIt->second.callCount : 0;
        std::ostringstream row;
        row << "  " << (i + 1) << ". " << name << " runtime_calls=" << hit.callCount
            << " (top-level-only was " << histCount << ", +" << (hit.callCount - histCount) << " from hook firing)";
        line(row.str());
    }

    line("\n=== TOP 15 CONFIRMED HOOKS BY TOTAL REAL CALLS ATTEMPTED (both states) ===");
    {
        std::vector<size_t> order(hooks.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            uint64_t ta = hookAgg[a].gpCalledOk + hookAgg[a].gpCalledErr + hookAgg[a].uiCalledOk + hookAgg[a].uiCalledErr;
            uint64_t tb = hookAgg[b].gpCalledOk + hookAgg[b].gpCalledErr + hookAgg[b].uiCalledOk + hookAgg[b].uiCalledErr;
            if (ta != tb) return ta > tb;
            return hooks[a].name < hooks[b].name;
        });
        for (size_t rank = 0; rank < order.size() && rank < 15; ++rank) {
            size_t hi = order[rank];
            const auto& agg = hookAgg[hi];
            uint64_t total = agg.gpCalledOk + agg.gpCalledErr + agg.uiCalledOk + agg.uiCalledErr;
            std::ostringstream row;
            row << "  " << (rank + 1) << ". " << hooks[hi].name << " [" << hookGroupLabel(hooks[hi].group)
                << "] total_calls_attempted=" << total << " (gameplay: defined_in=" << agg.gpDefined
                << " ok=" << agg.gpCalledOk << " err=" << agg.gpCalledErr << "; ui: defined_in="
                << agg.uiDefined << " ok=" << agg.uiCalledOk << " err=" << agg.uiCalledErr << ")";
            line(row.str());
        }
    }

    line("\n=== TOP 25 REAL LUA ERROR MESSAGES BY FREQUENCY (NEW - real count-and-sort over every real "
         "hook-call callError string this run captured, grouped by normalized text; see "
         "verdict_hook_error_messages.tsv for the full list and this file's own top comment for exactly "
         "what normalization strips) ===");
    line("distinct_normalized_error_messages=" + std::to_string(errorMessageRows.size()) +
         " (total error occurrences=" + std::to_string(hooksEverCalledErrCount) + ", matches "
         "hook_calls_attempted_and_erred above)");
    for (size_t i = 0; i < errorMessageRows.size() && i < 25; ++i) {
        auto& [msg, count] = errorMessageRows[i];
        std::ostringstream row;
        row << "  " << (i + 1) << ". count=" << count << "  " << msg;
        line(row.str());
    }
    summary.close();

    std::cout << "\nWrote " << (outDir / "verdict_per_script.tsv").string() << ", "
              << (outDir / "verdict_stub_hits.tsv").string() << " (historical, unchanged), "
              << (outDir / "verdict_stub_hits_with_hooks.tsv").string() << " (new), "
              << (outDir / "verdict_hook_fires_by_hook.tsv").string() << " (new), "
              << (outDir / "verdict_hook_fires_detail.tsv").string() << " (new), "
              << (outDir / "verdict_hook_error_messages.tsv").string() << " (new), "
              << (outDir / "verdict_summary.txt").string() << "\n";
}

} // namespace luahostrun
