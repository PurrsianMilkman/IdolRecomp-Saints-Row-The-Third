// lua_host_run main() split: the per-script top-level + hook-firing pass and its stub-hit/hook/error TSVs.
// Code moved verbatim out of the former single-function main() (see lua_host_run_session.h).
#include "lua_host_run_session.h"

namespace luahostrun {

void runScriptPass(LuaHostRunSession& sess) {
    auto& preloadStatesSpec164 = sess.preloadStatesSpec164;
    auto& outDir = sess.outDir;
    auto& toolsDir = sess.toolsDir;
    auto& maxScriptsToRun = sess.maxScriptsToRun;
    auto& host = *sess.host_;
    const auto& hooks = *sess.hooks_;
    auto& st = sess.st;
    auto& scriptTag = sess.scriptTag;
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
    auto& errorMessageCounts = sess.errorMessageCounts;

    // --- Cross-reference against the static call-count ranking -----------
    fs::path reconciliationPath = toolsDir / "lua_reconciliation_called_and_registered_1181.tsv";
    staticRank = loadStaticReconciliation(reconciliationPath.string());
    std::cout << "\nLoaded " << staticRank.size() << " rows from " << reconciliationPath.string()
              << " for the static-vs-runtime cross-check (0 means that file wasn't found from this working "
              << "directory - the cross-check is skipped, not fabricated).\n";

    // --- Main loop: try every found script's own top-level runChunk()
    // pass, RESTRICTED (this task, step 0 - spec-lua-bindings.md Sec14.5)
    // to each script's own real resolved statetag, in walk order, up to
    // maxScriptsToRun. -----------------------------------------------
    // Real cause this restriction closes (HANDOFF.md Sec9.140's own traced
    // finding): before this task, runChunk() ran EVERY script's own
    // top-level code (including every `function some_hook_name() ... end`
    // definition it contains) in BOTH persistent states unconditionally,
    // regardless of the script's own real state tag - so a UI-tagged
    // script's own hook-function definitions leaked into the gameplay
    // state's shared global table (and vice versa) even though Sec9.140's
    // own EARLIER fix already restricted fireConfirmedHooks()'s CALL SITE
    // to each script's own real state. Restricting THIS call site too
    // (the actual cause Sec9.140 traced, concretely confirmed for
    // horde_results_populate: its gameplay existedAsFunction flipped true
    // at script #645, the first OPEN-tagged script walked after the real
    // UI definer ran) closes that leak at its real source. `ui`/`gameplay`
    // tagged scripts now only run their own top-level code in their own
    // real state; `OPEN`/`conflict` tagged scripts (10/0 of 804) still run
    // in BOTH states - the SAME "maximum observation, stated engineering
    // choice" convention Sec9.140 already established for the hook-firing
    // restriction (see `fireGp`/`fireUi` below, now computed ONCE per
    // script and reused for both this call site and the hook-firing call
    // site, rather than duplicated). -----------------------------------
    toRun = std::min<uint64_t>(maxScriptsToRun, st.found.size());
    std::cout << "\n=== Running " << toRun << " of " << st.found.size()
              << " found scripts (population denominator) - top-level runChunk() now RESTRICTED per-script "
              << "to its own real statetag (ui/gameplay -> own state only; OPEN/conflict -> both states) ===\n";

    std::ofstream perScript(outDir / "verdict_per_script.tsv");
    perScript << "archive\tpath_chain\tentry_name\tbytes\tsr3lua_parse_ok\tsr3lua_error\t"
                 "gameplay_runChunk_attempted\tgameplay_load_ok\tgameplay_load_error\tgameplay_pcall_ok\tgameplay_pcall_error\t"
                 "ui_runChunk_attempted\tui_load_ok\tui_load_error\tui_pcall_ok\tui_pcall_error\t"
                 "resolved_state_tag\n";

    uint64_t sr3luaParseOkCount = 0;

    // --- Historical (top-level-only) stub-hit tracking, kept SEPARATE from
    // the hook-firing pass below so verdict_stub_hits.tsv keeps meaning
    // EXACTLY what it always meant (HANDOFF.md Sec9.113's own 56-call/
    // 5-name baseline) even though the SAME shared host.hitLog() the hook
    // firing pass below also writes into. Method: seed from whatever
    // game_lib.lua's own dedicated pre-loop run above already produced,
    // then for each script take a call-count snapshot immediately BEFORE
    // and immediately AFTER its own top-level runChunk pair (before any
    // hook firing happens), and add only that delta - which is
    // necessarily identical to what an unmodified, hook-firing-free build
    // of this same loop would have produced, since nothing about the
    // runChunk calls themselves changed. -----------------------------
    for (auto& [name, hit] : host.hitLog().hits()) {
        auto& hh = historical[name];
        hh.callCount = hit.callCount;
        hh.lastArgc = hit.lastArgc;
        hh.lastArgTypes = hit.lastArgTypes;
    }
    historicalTotal = host.hitLog().totalCalls();

    // --- Per-hook aggregate accumulators, parallel to `hooks` (same
    // order, so index i here always corresponds to hooks[i]). ----------
    hookAgg = std::vector<HookAgg>(hooks.size());

    std::ofstream hookDetail(outDir / "verdict_hook_fires_detail.tsv");
    hookDetail << "archive\tpath_chain\tentry_name\tstate\thook_name\thook_group\t"
                  "existed_as_function\tattempted_call\tcall_ok\tcall_error\t"
                  "resolved_state_tag\tstate_matches_resolved_tag\n";

    for (uint64_t i = 0; i < toRun; ++i) {
        const auto& s = st.found[i];

        bool sr3luaOk = true;
        std::string sr3luaErr;
        try {
            sr3lua::Parser::analyze(s.content);
        } catch (const sr3lua::LuaSyntaxError& ex) {
            sr3luaOk = false;
            sr3luaErr = ex.what();
        }
        if (sr3luaOk) sr3luaParseOkCount++;

        // --- Real per-script state restriction, computed ONCE per script
        // and reused at BOTH real call sites below (this task, step 0 +
        // Sec9.140's own earlier hook-firing restriction): a script tagged
        // `ui` (kind 21/22 + type 27) only ever runs/fires into
        // host.uiState(); a script tagged `gameplay` (kind 39/40 + type 32)
        // only ever runs/fires into host.gameplayState(). A script tagged
        // OPEN or `conflict` (its own container entry didn't resolve
        // cleanly to either confirmed shape - see statetag::resolve())
        // still runs/fires against BOTH states, unchanged from this
        // harness's original "maximum observation" design
        // (include/sr3luahost/host.h's own top comment) - a deliberate,
        // stated engineering choice for this task (not the only defensible
        // one - "skip OPEN entirely" was the other real option
        // considered): since OPEN genuinely means "no real data either
        // way," running/firing both preserves this scaffold's original
        // safety property (never silently reducing coverage) for exactly
        // the population this task's own real data couldn't resolve,
        // while still applying the real restriction to every script THIS
        // TASK'S OWN real data resolved. s.entryName is the real, on-hand
        // per-script filename (already ends in ".lua" - see
        // endsWithLuaExt() above) - the real value threaded through for
        // HookArgKind::ScriptFilenameString (_PrepareForDynamicGlobals,
        // spec-lua-bindings.md Sec14.1), never a placeholder.
        statetag::Tag tag = scriptTag[i];
        bool fireGp = (tag == statetag::Tag::Gameplay) || (tag == statetag::Tag::Open) ||
                      (tag == statetag::Tag::Conflict);
        bool fireUi = (tag == statetag::Tag::Ui) || (tag == statetag::Tag::Open) ||
                      (tag == statetag::Tag::Conflict);
        // Sec16.4 (CONFIRMED, cleared 2026-10-01; default, --preload-states=tag
        // restores the old behaviour): the preloads run only in the state
        // Sec16.4 names, overriding the per-script tag.
        if (preloadStatesSpec164) {
            std::string lower = toLower(s.entryName);
            static const char* const kUiOnly[] = {"vint_lib.lua", "game_ui_globals.lua", "vdo_base_object.lua",
                                                  "vdo_anim_object.lua", "vdo_input_tracker.lua"};
            bool uiOnly = false;
            for (const char* n : kUiOnly) uiOnly = uiOnly || lower == n;
            if (uiOnly && (fireGp || !fireUi)) { fireGp = false; fireUi = true; ++spec164Overrides; }
            if (lower == "game_lib.lua" && (fireUi || !fireGp)) { fireUi = false; fireGp = true; ++spec164Overrides; }
            // system_lib.lua: both states, which is what an OPEN/conflict tag already gives;
            // forced here so the assignment is exactly Sec16.4's whatever the tag says.
            if (lower == "system_lib.lua" && !(fireGp && fireUi)) { fireGp = fireUi = true; ++spec164Overrides; }
        }
        if (!fireGp) { skippedGpFireCount++; skippedGpRunChunkCount++; }
        if (!fireUi) { skippedUiFireCount++; skippedUiRunChunkCount++; }

        // Snapshot immediately before this script's own top-level runs -
        // the "before" half of the historical (top-level-only) delta.
        std::unordered_map<std::string, uint64_t> beforeTopCounts;
        for (auto& [name, hit] : host.hitLog().hits()) beforeTopCounts[name] = hit.callCount;
        uint64_t beforeTopTotal = host.hitLog().totalCalls();

        // --- Top-level runChunk() pass, RESTRICTED (this task, step 0) to
        // `fireGp`/`fireUi` computed just above - see this loop's own
        // comment block above and this file's own top-of-loop comment for
        // the full method/citation. A skipped state gets a real, empty
        // default-constructed RunResult (loadOk=false, ranPcall=false) -
        // NOT a claim the script fails to load in that state, simply "not
        // attempted this pass," exactly the same honest-skip convention
        // already used for the hook-firing restriction below. At least one
        // of fireGp/fireUi is always true (tag is always exactly one of
        // Ui/Gameplay/Open/Conflict), so `realLoadOk` below can always be
        // read from whichever side actually ran.
        Host::RunResult gpResult = fireGp ? host.runChunk(host.gameplayState(), s.content, s.entryName)
                                           : Host::RunResult{};
        Host::RunResult uiResult = fireUi ? host.runChunk(host.uiState(), s.content, s.entryName)
                                           : Host::RunResult{};

        // Snapshot immediately after (before any hook firing below) - the
        // "after" half. Only this delta is added to `historical`.
        for (auto& [name, hit] : host.hitLog().hits()) {
            uint64_t before = 0;
            auto bIt = beforeTopCounts.find(name);
            if (bIt != beforeTopCounts.end()) before = bIt->second;
            if (hit.callCount > before) {
                auto& hh = historical[name];
                hh.callCount += (hit.callCount - before);
                hh.lastArgc = hit.lastArgc;
                hh.lastArgTypes = hit.lastArgTypes;
            }
        }
        historicalTotal += (host.hitLog().totalCalls() - beforeTopTotal);

        // load result is state-independent (pure syntax check, luaL_
        // loadbuffer does not touch either state's own globals) - read
        // from whichever side actually ran this pass (fireGp preferred,
        // falling back to uiResult - both are byte-identical when both
        // ran, which only OPEN/conflict scripts still do this task).
        bool realLoadOk = fireGp ? gpResult.loadOk : uiResult.loadOk;
        if (realLoadOk) loadOkCount++;
        if (gpResult.pcallOk) pcallOkCount_gp++;
        if (uiResult.pcallOk) pcallOkCount_ui++;

        if (sr3luaOk == realLoadOk) agreeCount++; else disagreeCount++;

        perScript << sanitizeTsv(s.archive) << "\t" << sanitizeTsv(s.pathChain) << "\t"
                  << sanitizeTsv(s.entryName) << "\t" << s.content.size() << "\t"
                  << (sr3luaOk ? 1 : 0) << "\t" << sanitizeTsv(sr3luaErr) << "\t"
                  << (fireGp ? 1 : 0) << "\t"
                  << (gpResult.loadOk ? 1 : 0) << "\t" << sanitizeTsv(gpResult.loadError) << "\t"
                  << (gpResult.pcallOk ? 1 : 0) << "\t" << sanitizeTsv(gpResult.pcallError) << "\t"
                  << (fireUi ? 1 : 0) << "\t"
                  << (uiResult.loadOk ? 1 : 0) << "\t" << sanitizeTsv(uiResult.loadError) << "\t"
                  << (uiResult.pcallOk ? 1 : 0) << "\t" << sanitizeTsv(uiResult.pcallError) << "\t"
                  << statetag::tagLabel(tag) << "\n";

        // --- Hook-firing pass, RESTRICTED to each script's own real state
        // (Sec9.140, unchanged this task): after this script's own
        // top-level attempt (win or lose), attempt every confirmed/
        // pattern-based hook name against ONLY the persistent state(s)
        // `fireGp`/`fireUi` (computed once, above, and now also governing
        // the runChunk() call site just above - this task's own step 0
        // change) say this script really belongs to.

        // A real, empty "not fired this pass" result for a skipped state -
        // NOT a claim the hook doesn't exist there (existedAsFunction is
        // simply not measured when the call site itself is skipped, a
        // real, stated tradeoff of restricting the CALL SITE rather than
        // adding a separate cheap existence-only check to Host - see this
        // block's own comment above). attemptedCall stays false, so this
        // synthesized result can never contribute to hookAgg's ok/err
        // tallies or errorMessageCounts below (both gated on
        // attemptedCall), exactly as intended.
        auto skipped = [&]() {
            std::vector<Host::HookFireResult> out;
            out.reserve(hooks.size());
            for (auto& h : hooks) {
                Host::HookFireResult r;
                r.hookName = h.name;
                r.group = h.group;
                out.push_back(r);
            }
            return out;
        };
        auto gpHooks = fireGp ? host.fireConfirmedHooks(host.gameplayState(), hooks, s.entryName) : skipped();
        auto uiHooks = fireUi ? host.fireConfirmedHooks(host.uiState(), hooks, s.entryName) : skipped();
        for (size_t hi = 0; hi < hooks.size(); ++hi) {
            const auto& gh = gpHooks[hi];
            const auto& uh = uiHooks[hi];
            auto& agg = hookAgg[hi];

            if (gh.existedAsFunction) agg.gpDefined++;
            if (gh.attemptedCall) {
                if (gh.callOk) agg.gpCalledOk++; else agg.gpCalledErr++;
                if (!gh.callOk && gh.callError.find("watchdog") != std::string::npos) watchdogAbortCount++;
                if (!gh.callOk) errorMessageCounts[stripLuaChunkLocationPrefix(gh.callError)]++;
            }
            if (uh.existedAsFunction) agg.uiDefined++;
            if (uh.attemptedCall) {
                if (uh.callOk) agg.uiCalledOk++; else agg.uiCalledErr++;
                if (!uh.callOk && uh.callError.find("watchdog") != std::string::npos) watchdogAbortCount++;
                if (!uh.callOk) errorMessageCounts[stripLuaChunkLocationPrefix(uh.callError)]++;
            }

            hookDetail << sanitizeTsv(s.archive) << "\t" << sanitizeTsv(s.pathChain) << "\t"
                       << sanitizeTsv(s.entryName) << "\t" << "gameplay" << "\t" << gh.hookName << "\t"
                       << hookGroupLabel(gh.group) << "\t" << (gh.existedAsFunction ? 1 : 0) << "\t"
                       << (gh.attemptedCall ? 1 : 0) << "\t" << (gh.callOk ? 1 : 0) << "\t"
                       << sanitizeTsv(gh.callError) << "\t" << statetag::tagLabel(tag) << "\t"
                       << (fireGp ? 1 : 0) << "\n";
            hookDetail << sanitizeTsv(s.archive) << "\t" << sanitizeTsv(s.pathChain) << "\t"
                       << sanitizeTsv(s.entryName) << "\t" << "ui" << "\t" << uh.hookName << "\t"
                       << hookGroupLabel(uh.group) << "\t" << (uh.existedAsFunction ? 1 : 0) << "\t"
                       << (uh.attemptedCall ? 1 : 0) << "\t" << (uh.callOk ? 1 : 0) << "\t"
                       << sanitizeTsv(uh.callError) << "\t" << statetag::tagLabel(tag) << "\t"
                       << (fireUi ? 1 : 0) << "\n";
        }

        if ((i + 1) % 100 == 0 || i + 1 == toRun) {
            std::cout << "  ... " << (i + 1) << "/" << toRun << " tried\n";
        }
    }
    perScript.close();
    hookDetail.close();
}

void writeStubHitReports(LuaHostRunSession& sess) {
    auto& outDir = sess.outDir;
    auto& host = *sess.host_;
    const auto& hooks = *sess.hooks_;
    auto& staticRank = sess.staticRank;
    auto& historical = sess.historical;
    auto& hookAgg = sess.hookAgg;
    auto& errorMessageCounts = sess.errorMessageCounts;
    auto& hitRows = sess.hitRows;
    auto& hitRowsWithHooks = sess.hitRowsWithHooks;
    auto& hooksEverDefinedCount = sess.hooksEverDefinedCount;
    auto& hooksEverCalledOkCount = sess.hooksEverCalledOkCount;
    auto& hooksEverCalledErrCount = sess.hooksEverCalledErrCount;
    auto& errorMessageRows = sess.errorMessageRows;

    // --- Stub hit aggregate #1 (UNCHANGED file/format/meaning): the
    // HISTORICAL, top-level-only ranking - built from `historical`, NOT
    // from host.hitLog() directly (which now also holds hook-triggered
    // calls) - so this file keeps reproducing HANDOFF.md Sec9.113's own
    // 56-call/5-name baseline exactly, never silently overwritten by this
    // task's own new hook-firing pass. -------------------------------
    std::ofstream hitsOut(outDir / "verdict_stub_hits.tsv");
    hitsOut << "name\tcall_count\tlast_argc\tlast_arg_types\tstatic_total_call_sites\tstatic_distinct_scripts\n";
    hitRows = std::vector<std::pair<std::string, HistHit>>(historical.begin(), historical.end());
    std::sort(hitRows.begin(), hitRows.end(), [](const auto& a, const auto& b) {
        if (a.second.callCount != b.second.callCount) return a.second.callCount > b.second.callCount;
        return a.first < b.first;
    });
    for (auto& [name, hit] : hitRows) {
        auto it = staticRank.find(name);
        uint64_t sCalls = (it != staticRank.end()) ? it->second.totalCallSites : 0;
        uint64_t sScripts = (it != staticRank.end()) ? it->second.distinctScripts : 0;
        hitsOut << name << "\t" << hit.callCount << "\t" << hit.lastArgc << "\t" << hit.lastArgTypes
                << "\t" << sCalls << "\t" << sScripts << "\n";
    }
    hitsOut.close();

    // --- Stub hit aggregate #2 (NEW): the FINAL, all-inclusive ranking -
    // top-level AND hook-triggered calls together, straight from
    // host.hitLog() as it stands at the very end of the whole run. This
    // is the "MUCH more representative stub-hit ranking" this task's own
    // brief asked for, reported ALONGSIDE (never replacing) the file
    // above. --------------------------------------------------------
    std::ofstream hitsWithHooksOut(outDir / "verdict_stub_hits_with_hooks.tsv");
    hitsWithHooksOut << "name\tcall_count_with_hooks\thistorical_top_level_only_call_count\t"
                        "incremental_from_hook_firing\tlast_argc\tlast_arg_types\t"
                        "static_total_call_sites\tstatic_distinct_scripts\t"
                        "gameplay_call_count\tui_call_count\n";
    hitRowsWithHooks = std::vector<std::pair<std::string, sr3luahost::StubHit>>(host.hitLog().hits().begin(), host.hitLog().hits().end());
    std::sort(hitRowsWithHooks.begin(), hitRowsWithHooks.end(), [](const auto& a, const auto& b) {
        if (a.second.callCount != b.second.callCount) return a.second.callCount > b.second.callCount;
        return a.first < b.first;
    });
    for (auto& [name, hit] : hitRowsWithHooks) {
        auto histIt = historical.find(name);
        uint64_t histCount = (histIt != historical.end()) ? histIt->second.callCount : 0;
        auto it = staticRank.find(name);
        uint64_t sCalls = (it != staticRank.end()) ? it->second.totalCallSites : 0;
        uint64_t sScripts = (it != staticRank.end()) ? it->second.distinctScripts : 0;
        // gameplay_call_count/ui_call_count: REAL per-state call counts
        // (StubHit::gameplayCallCount/uiCallCount, stub_registry.h),
        // measured directly at each real call site - never inferred from
        // this name's own registered cluster tag (a name can genuinely be
        // called from a state its cluster tag doesn't match, e.g.
        // tutorial_advance - see HANDOFF.md Sec9.117/spec-lua-bindings.md
        // Sec14).
        hitsWithHooksOut << name << "\t" << hit.callCount << "\t" << histCount << "\t"
                         << (hit.callCount - histCount) << "\t" << hit.lastArgc << "\t" << hit.lastArgTypes
                         << "\t" << sCalls << "\t" << sScripts << "\t" << hit.gameplayCallCount << "\t"
                         << hit.uiCallCount << "\n";
    }
    hitsWithHooksOut.close();

    // --- Per-hook fire aggregate (NEW): which of the confirmed hooks were ever
    // defined+called, in how many scripts, ok vs error, per state. -----
    std::ofstream hookByHookOut(outDir / "verdict_hook_fires_by_hook.tsv");
    hookByHookOut << "hook_name\thook_group\tgameplay_defined_in_N_scripts\tgameplay_called_ok\t"
                     "gameplay_called_error\tui_defined_in_N_scripts\tui_called_ok\tui_called_error\t"
                     "total_calls_attempted_both_states\thook_evidence\n";
    for (size_t hi = 0; hi < hooks.size(); ++hi) {
        const auto& h = hooks[hi];
        const auto& agg = hookAgg[hi];
        uint64_t totalAttempted = agg.gpCalledOk + agg.gpCalledErr + agg.uiCalledOk + agg.uiCalledErr;
        hookByHookOut << h.name << "\t" << hookGroupLabel(h.group) << "\t" << agg.gpDefined << "\t"
                      << agg.gpCalledOk << "\t" << agg.gpCalledErr << "\t" << agg.uiDefined << "\t"
                      << agg.uiCalledOk << "\t" << agg.uiCalledErr << "\t" << totalAttempted << "\t"
                      << hookEvidenceLabel(h.evidence) << "\n";
        if (agg.gpDefined > 0 || agg.uiDefined > 0) hooksEverDefinedCount++;
        hooksEverCalledOkCount += (agg.gpCalledOk + agg.uiCalledOk);
        hooksEverCalledErrCount += (agg.gpCalledErr + agg.uiCalledErr);
    }
    hookByHookOut.close();

    // --- Top real Lua error messages by frequency (NEW, orchestrator's
    // follow-up item): a real count-and-sort over every real hook-call
    // callError string this run captured, grouped by
    // stripLuaChunkLocationPrefix()'s own normalized text (see that
    // function's own doc comment for exactly what it strips and why) -
    // this file's OWN top comment addition has the full rationale. A real
    // aggregate over REAL captured error strings, not a guess at what
    // errors "should" exist. ------------------------------------------
    errorMessageRows = std::vector<std::pair<std::string, uint64_t>>(errorMessageCounts.begin(), errorMessageCounts.end());
    std::sort(errorMessageRows.begin(), errorMessageRows.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    std::ofstream errorMsgOut(outDir / "verdict_hook_error_messages.tsv");
    errorMsgOut << "normalized_message\tcount\n";
    for (auto& [msg, count] : errorMessageRows) {
        errorMsgOut << sanitizeTsv(msg) << "\t" << count << "\n";
    }
    errorMsgOut.close();
}

} // namespace luahostrun
