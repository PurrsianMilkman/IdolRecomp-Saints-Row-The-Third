// Generic, self-logging Lua stub registration - the scaffold's core piece
// (see the task this was built for: a Lua 5.1 host scaffold registering
// every real engine-provided name from the current tagged registration
// list (originally 1,430 names, `tools/lua_all_registered_1430_tagged.txt`;
// superseded 2026-09-29 by the corrected 1,435-name
// `tools/lua_all_registered_1435_tagged.txt` - spec-lua-bindings.md
// Sec13.6 - see host.h/lua_host_run.cpp for which file a given run
// actually loads; deliberately not repeating a specific count here so this
// comment cannot go stale the way a hardcoded number would) as stub C
// functions.
//
// Every registered name becomes a real C closure, bound into a real
// lua_State's global table via the real public `lua_pushcclosure` +
// `lua_setfield(L, LUA_GLOBALSINDEX, name)` idiom - i.e. THIS project's own
// use of exactly the mechanism spec-lua-bindings.md Sec13 found the real
// engine using for its own registration (a deliberate, honest parallel: we
// are not guessing at a different binding style, we are using the one the
// spec already confirmed the real engine uses for this exact purpose).
// What each stub DOES on call is this task's own explicit, chosen
// contract, not anything inferred from the spec: log its own name,
// argument count, every argument's real `lua_type()`, and (added in the
// same-day follow-up pass that added StubHit's gameplayCallCount/
// uiCallCount below) which real state it was called against, then return
// nil.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3luahost/lua_c_api.h"

namespace sr3luahost {

// One line of the current tagged registration list (see this file's own
// top comment): a real engine global name plus its "gameplay"/"ui" cluster
// tag (spec-lua-bindings.md Sec13.2's two-registrar/two-Lua-state split,
// Sec13.6's fourth-registrar correction), tab-separated, already verified
// to be plain names with zero exe addresses.
struct RegisteredName {
    std::string name;
    std::string cluster; // "gameplay" or "ui", verbatim from the file
};

// Parses the tagged registration list. Throws std::runtime_error if the
// file cannot be opened. Malformed lines (no tab) are skipped rather than
// throwing - none are expected (the file is pre-verified plain-text), but a
// skip-and-report is safer than crashing the whole scaffold over one stray
// line this reader didn't anticipate.
std::vector<RegisteredName> loadTaggedRegistrationList(const std::string& path);

// One aggregated record for a single stub name, accumulated across every
// call, in every script, against every Lua state, for the lifetime of one
// HitLog instance.
struct StubHit {
    uint64_t callCount = 0;    // total, BOTH states combined - unchanged meaning/value from before the
                               // per-state fields below were added (gameplayCallCount + uiCallCount ==
                               // callCount always, by construction of record() below).
    // Real per-state breakdown (added for the orchestrator's runtime-ranking
    // deliverable, 2026-09-29 follow-up pass): which persistent lua_State a
    // call actually happened against, tagged by the caller of record() at
    // the real call site (registerStubs()/registerThreadScheduler() below),
    // never inferred from a name's registered-list cluster tag - a name's
    // real calls can land in a state its own cluster tag doesn't match
    // (confirmed for tutorial_advance, HANDOFF.md Sec9.117/Sec14), so this
    // is measured directly, per call, not looked up after the fact.
    uint64_t gameplayCallCount = 0;
    uint64_t uiCallCount = 0;
    int lastArgc = 0;         // argc of the most recent call (a spot-check sample, not a history)
    std::string lastArgTypes; // comma-joined lua_typename() results of the most recent call
};

// Shared, cross-script, cross-state call counter. One instance is meant to
// span an entire run (both the gameplay and UI lua_State, every script
// tried) so "which stub names fired, ranked by hit count" is a real,
// whole-run aggregate - exactly what the verdict tool reports.
class HitLog {
public:
    // `state` is a real, caller-supplied tag naming which persistent
    // lua_State this specific call happened against ("gameplay" or "ui" -
    // see registerStubs()/registerThreadScheduler() below, the only real
    // callers) - stored per-call, not derived from any registration-list
    // lookup, so a name called from a state other than its own registered
    // cluster is recorded honestly rather than mis-attributed.
    void record(const std::string& name, int argc, const std::string& argTypes, const std::string& state);
    const std::unordered_map<std::string, StubHit>& hits() const { return hits_; }
    uint64_t totalCalls() const { return totalCalls_; }

private:
    std::unordered_map<std::string, StubHit> hits_;
    uint64_t totalCalls_ = 0;
};

// Registers every entry of `names` as a generic logging stub into `L`'s
// globals. Each stub is `genericStubTrampoline` (stub_registry.cpp),
// closed over three upvalues: its own name (a Lua string), a light-
// userdata pointer back to `log`, and `stateTag` (a Lua string - the real
// state this particular registration call is for, e.g. "gameplay"/"ui",
// passed straight through to every HitLog::record() call this stub makes
// so per-state call counts are real per-call evidence, never inferred
// after the fact). Safe to call twice with disjoint `names` against two
// different states sharing one `log`, which is exactly how Host (host.h)
// uses it for the real gameplay/UI state split.
void registerStubs(lua_State* L, const std::vector<RegisteredName>& names, HitLog& log, const std::string& stateTag);

} // namespace sr3luahost
