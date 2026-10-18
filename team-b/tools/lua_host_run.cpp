// IMPLEMENTED PRE-REVIEW, PENDING CLEARANCE (manager rule 2026-09-30): the
// spec-lua-api-behaviour.md / spec-lua-bindings.md sections this file rests on
// are marked "NOT yet cleared for implementation" by the 2026-09-30 desk
// review (115/122 and 57/58 review-status lines). Kept working, behaviour
// unchanged, until Team A clears them; no new behaviour from those sections
// before then (team-b/HANDOFF.md section A, standing rule).
//
// The Lua 5.1 host scaffold's VERDICT TOOL: loads game_lib.lua for real and
// attempts to load/run real shipped mission/UI Lua scripts against the
// stub environment built by sr3luahost (include/sr3luahost/host.h), then
// reports what REALLY happened - never an inferred or invented result.
//
// Archive-walking method: copied and adapted, deliberately, from
// tools/lua_mission_ui_census.cpp's own walk() (same recursion into nested
// `.str2_pc` containers via vpp::Container::openNested()/decompressEntry(),
// same ".lua name suffix" + "Lua bytecode magic" leaf test) rather than
// re-invented - see that file's own top comment for the full population-
// finding rationale this reuses. Not refactored into a shared header this
// pass (the census's walk() is a local, unexported function) - duplicated
// with attribution instead, matching how vpp_dump.cpp/vpp_extract.cpp
// already each carry their own walk rather than sharing one.
//
// What this tool does that the census does NOT: the census only PARSES
// (sr3lua, a clean-room parser that never executes anything). This tool
// EXECUTES: a real luaL_loadbuffer() (a real Lua 5.1 syntax check, from the
// real reference implementation - a genuine cross-check against sr3lua's
// own parse verdict on the same file) and, if that succeeds, a real
// lua_pcall() of the loaded chunk's top-level code against a real,
// persistent lua_State that already has every stub name from the current
// tagged registration list registered (originally 1,430 names, corrected
// 2026-09-29 to 1,435 - spec-lua-bindings.md Sec13.6; see this file's own
// `main()` for which file a given run actually loads - not repeated as a
// fixed number here so this comment cannot go stale the way a hardcoded
// count would) (and, for the gameplay state, game_lib.lua already loaded -
// see below).
//
// IMPORTANT, stated plainly per this task's own instructions: lua_pcall
// only executes a script's TOP-LEVEL statements. A script that consists
// mostly of `function some_hook_name() ... end` definitions (the named-
// hook pattern spec-lua-bindings.md Sec4/Sec8 documents) will load and run
// cleanly without ever calling most of its own stubs - the engine would
// call those hook functions later, by name, at real gameplay trigger
// points this scaffold does not simulate. So a script "running to
// completion" with LOW stub-hit counts is an expected, structurally
// honest result for this kind of file, not a sign the harness is broken -
// see the population summary this tool prints for the real, measured
// breakdown.
//
// HOOK-FIRING PASS (added this task, per spec-lua-bindings.md Sec8.2-8.4/
// Sec12.2-12.9): closes the gap the paragraph above describes. After each
// script's own top-level runChunk has been attempted (regardless of that
// attempt's own pcall result), this tool now also attempts every
// confirmed/pattern-based hook name (sr3luahost::confirmedHooks(),
// include/sr3luahost/hook_registry.h - originally 133 [73 Group-1 general
// hooks + 2 Group-2 unconditional lifecycle hooks + 58 Group-3
// mission-numbered pattern-based hooks], extended to 138 in a same-day
// follow-up pass with 5 more Group-1 names from spec-lua-bindings.md
// Sec14.6 - see hook_registry.h for the live, authoritative count/table,
// not repeated as a fixed number in this comment) against the SAME
// persistent state that script was loaded into (Host::fireConfirmedHooks,
// host.h). Most hooks are still called with ZERO arguments - spec Sec8.3
// confirms the call-in MECHANISM only, not per-hook argument shapes, which
// remain OPEN for most names (see hook_registry.h's own top comment) -
// stated here plainly per this project's "no invented fixes" policy, not
// silently assumed. A small, explicitly-enumerated set of hooks DOES now
// push real, spec-confirmed argument values (spec-lua-bindings.md Sec14) -
// see HookSpec::args's own doc comment (hook_registry.h) for exactly
// which ones and why.
//
// IMPORTANT, stated plainly: because gameplay_/ui_ are PERSISTENT,
// shared states across the WHOLE 804-script population (not one isolated
// state per script), a hook name multiple scripts both define will, by
// the time script N's own hook-firing runs, actually invoke whichever
// script's definition currently happens to be live in that shared global
// table - not necessarily script N's own, since a later script's
// top-level run can overwrite a name an earlier script also defined. This
// is a pre-existing property of this scaffold's persistent-state design
// (HANDOFF.md Sec9.113), not something new to this pass, and is stated
// here rather than silently papered over. Firing per-script (rather than
// once at the very end) still produces a genuinely different, meaningful
// per-row signal: each script's row reflects the cumulative state of
// "every script tried so far, in real walk order" at that point.
//
// This produces a NEW stub-hit ranking that reflects real hook-triggered
// call volume, reported ALONGSIDE (never overwriting) the original
// top-level-only ranking - see verdict_stub_hits.tsv (unchanged,
// top-level-only, historical - reproduces HANDOFF.md Sec9.113's own
// 56-call/5-name baseline exactly) vs verdict_stub_hits_with_hooks.tsv
// (new, all-inclusive). Per-hook fire counts land in
// verdict_hook_fires_by_hook.tsv (aggregate) and
// verdict_hook_fires_detail.tsv (the full per-script-per-hook-per-state
// record this task's own instructions asked to track separately from
// top-level pcall success).
//
// A real, found risk this pass's own hook-firing exposes that the
// top-level-only pass never could (nothing calls into a hook body from
// the top-level pass): a hook body may contain a busy-poll loop against
// an always-nil-returning stub (real game_lib.lua text, grepped directly,
// has the equivalent shape: `repeat thread_yield() until
// teleport_check_done(REMOTE_PLAYER)`, and every registered stub always
// returns nil/falsy). Host::fireConfirmedHooks
// installs a real Lua 5.1 instruction-count watchdog (lua_sethook +
// LUA_MASKCOUNT) around each individual hook call to convert a genuine
// infinite loop into a real, recorded Lua error instead of hanging this
// tool - see host.h's own doc comment on fireConfirmedHooks. This
// watchdog is NEVER applied to runChunk() (the top-level pass above), so
// it cannot perturb the already-independently-verified Sec9.113 numbers.
//
// The sr3luahost::ThreadScheduler-backed thread_new/thread_yield/
// thread_kill/thread_check_done/thread_close registration (host.h/
// thread_scheduler.h) is wired into Host's constructor directly (both
// states) - nothing in this tool needs to touch it beyond letting scripts
// and hook bodies call it; its own hit counts fold into the same HitLog
// ranking as every other stub (see thread_scheduler.h's own top comment
// for the real evidence and the honest scope limits of this minimal
// scheduler).
//
// Usage: lua_host_run <cache_dir> <registered_tagged.txt> <out_dir>
//        [max_scripts_to_run]
// `<registered_tagged.txt>` should be the CURRENT tagged registration list
// (tools/lua_all_registered_1435_tagged.txt as of 2026-09-29, superseding
// the now-stale tools/lua_all_registered_1430_tagged.txt - spec-lua-
// bindings.md Sec13.6); the tool reads whatever count that file actually
// contains (allNames.size()) and reports that real count everywhere below,
// never a hardcoded population number.
// `max_scripts_to_run` (default: run every script found - the real 804-
// script population, subject to whatever this run's cache_dir actually
// contains) caps how many of the FOUND scripts get load/run-attempted,
// after game_lib.lua's own dedicated first attempt; the walk itself always
// covers every archive regardless of this cap, so the population
// denominators this tool prints are always the real, whole-archive counts.

#include "lua_host_run/lua_host_run_session.h"

int main(int argc, char** argv) {
    using namespace luahostrun;
    // Heap-allocated: the session holds what used to be main()'s ~100 shared locals (lua_host_run_session.h).
    auto sessHolder = std::make_unique<LuaHostRunSession>();
    LuaHostRunSession& sess = *sessHolder;

    if (int rc = parseArgs(sess, argc, argv); rc != -1) return rc;
    setupHost(sess);
    reportOpenStateAndHookCensus(sess);
    walkArchives(sess);
    setupVintDocs(sess);
    resolveStateTags(sess);
    runPreloads(sess);
    runScriptPass(sess);
    writeStubHitReports(sess);
    writeSummary(sess);
    setupMissionDrive(sess);
    driveMissions(sess);
    reportMissionDrive(sess);
    return 0;
}
