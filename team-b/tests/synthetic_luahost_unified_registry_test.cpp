// End-to-end guard for the lua-natives registration unification task
// (2026-10-08, see results/lua_natives_backlog_recount_20261008.md):
// checks that
// specConfirmedStubNames() (spec_confirmed_stubs.h) - the single source of
// truth this task made for "every native name this project's Lua host
// actually registers through a real, non-generic-stub implementation" -
// really does match what a real Host, built from the real tagged
// registration list, puts into its two real Lua global tables. Unlike
// synthetic_luahost_roster_test.cpp (which spot-checks a curated name
// list) or synthetic_luahost_bare_globals_test.cpp (behavioural checks on
// the 24 bare globals' own bodies), this test's whole point is to catch
// the EXACT shape of blind spot that motivated this task: a real
// registration call (registerThreadClose(), via thread_scheduler.h) that
// bypassed specConfirmedStubNames() entirely, so nothing that walked this
// function's own return value ever saw thread_close even though it is a
// real, actively-registered global in both states. A future registration
// track that makes the same mistake again should fail THIS test, not slip
// through unnoticed the way thread_close did.
//
// Method - real introspection of the real Lua state, not a second,
// hand-maintained name list compared only against a third hand-maintained
// list:
//   1. Build the real Host from the real tagged registration list (same
//      file synthetic_luahost_roster_test.cpp uses).
//   2. Walk each state's REAL global table (lua_next over LUA_GLOBALSINDEX)
//      and keep every key whose value is a real Lua C function.
//   3. Tell a generic "log and return nil" stub (registerStubs(),
//      stub_registry.cpp) apart from a real, per-name implementation
//      WITHOUT re-deriving Host::Host()'s own gameplay/ui split logic a
//      second time: every generic stub shares the exact same compiled C
//      function pointer (genericStubTrampoline) regardless of name or
//      state, so grabbing that pointer from any one known-generic name and
//      comparing lua_tocfunction() results tells the two categories apart
//      by what the real Lua state actually holds.
//   4. Subtract the small, fixed, exactly-enumerated set of top-level
//      function names stock Lua 5.1's own luaopen_base() installs (read
//      directly from the real, unmodified, vendored source,
//      third_party/lua51/src/lbaselib.c - the ONLY stock library this
//      project's Host opens, per lua_host.cpp's own openEngineStockLibs()
//      and synthetic_luahost_roster_test.cpp's own existing assertions
//      that math/string/table/io/os/debug/package are all nil).
//   5. What is left, per state, must be EXACTLY the subset of
//      specConfirmedStubNames() that is really a function in that state -
//      in both directions: no real registration specConfirmedStubNames()
//      doesn't know about, and no name specConfirmedStubNames() claims
//      that isn't really there.
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "sr3luahost/bare_globals.h"
#include "sr3luahost/host.h"
#include "sr3luahost/spec_confirmed_stubs.h"
#include "sr3luahost/thread_scheduler.h"

#include "../src/lua_spec_confirmed_stubs_sections.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

// The complete, exact set of top-level FUNCTION names stock Lua 5.1's own
// luaopen_base (third_party/lua51/src/lbaselib.c) installs into a fresh
// state: base_funcs[] (registered straight into _G by luaL_register),
// plus ipairs/pairs (auxopen) and newproxy (base_open()'s own three extra
// lua_setglobal calls). Read directly from that real, unmodified vendored
// source - not guessed, not copied from any other comment in this
// project. _G (a table) and _VERSION (a string) are also set by
// base_open() but are never function-valued, so a function-only walk never
// needs to exclude them; `coroutine`'s own 6 members
// (create/resume/running/status/wrap/yield) live INSIDE the `coroutine`
// table (luaL_register(L, LUA_COLIBNAME, co_funcs) creates/fills that
// table, not the globals table), so they never appear in a top-level walk
// either.
const std::set<std::string>& stockBaseLibraryFunctionNames() {
    static const std::set<std::string> names = {
        "assert",  "collectgarbage", "dofile",    "error",      "gcinfo",     "getfenv", "getmetatable",
        "loadfile", "load",          "loadstring", "next",       "pcall",      "print",   "rawequal",
        "rawget",  "rawset",         "select",    "setfenv",    "setmetatable", "tonumber", "tostring",
        "type",    "unpack",         "xpcall",    "ipairs",     "pairs",      "newproxy",
    };
    return names;
}

// Every top-level global name in `L` whose value is a real Lua function
// (C or otherwise - a sanity net, since nothing here is expected to define
// a pure-Lua global before any script runs). Real lua_next()/
// LUA_GLOBALSINDEX introspection of the actual state, not a second list.
std::set<std::string> functionGlobalNames(lua_State* L) {
    std::set<std::string> out;
    lua_pushnil(L);
    while (lua_next(L, LUA_GLOBALSINDEX) != 0) {
        // stack: ... key value
        if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TFUNCTION) {
            out.insert(lua_tostring(L, -2));
        }
        lua_pop(L, 1); // pop value; key stays on top for lua_next's next call
    }
    return out;
}

} // namespace

int main() {
    auto names = loadTaggedRegistrationList(CRREISH_TOOLS_DIR "/lua_all_registered_1490_tagged.txt");
    CHECK(names.size() == 1491); // same real file/count synthetic_luahost_roster_test.cpp pins
    Host host(names);
    lua_State* gp = host.gameplayState();
    lua_State* ui = host.uiState();

    // ---- Part 1: the unified list itself, no duplicates. The expected
    // total is re-derived independently here (every individual bNN
    // section's own namesBxx(), plus bareGlobalRoster()/threadCloseNames())
    // rather than hardcoded, so this check can't itself go stale the way
    // a literal count did after wave-1's own batches grew
    // specConfirmedStubNames() past its original 327-name baseline
    // (results/lua_natives_backlog_recount_20261008.md: 302 b01-b07 + 24
    // bare globals + 1 thread_close) without this test being updated to
    // match - exactly the kind of drift this whole test exists to catch,
    // just applied to its own expected-size literal instead of a missing
    // registration. ----
    // MAINTENANCE: this is a manually-enumerated mirror of every
    // stub_sections::namesBxx() accessor lua_spec_confirmed_stubs_registry.
    // cpp's own specConfirmedStubNames()/registerSpecConfirmedStubs() sum
    // over (see lua_spec_confirmed_stubs_sections.h for the full,
    // canonical list of section accessors) - it is NOT auto-discovered,
    // so the two lists CAN drift again exactly like this: 2026-10-08,
    // wave-1 Batch 3 (commit 8b0264e) added stub_sections::namesB10() (40
    // names) to both registry.cpp assembly functions but this sum was
    // never updated to match, so this test failed on unmodified main
    // until this fix added the missing `namesB10().size()` term below. If
    // this CHECK (or the allRealSpecialNames one further down) ever fails
    // again after a wave-of-natives merge, diff this sum against registry.
    // cpp's own append()/registerB0N() call lists first - a missing or
    // extra term here is the single most likely cause.
    const std::vector<std::string>& unified = specConfirmedStubNames();
    std::set<std::string> unifiedSet(unified.begin(), unified.end());
    // NOTE (2026-10-08, found while landing wave 2 Batch W2-3/b16): this sum
    // was ALREADY missing stub_sections::namesB10().size() before this
    // batch touched the file at all (b10 - "wave 1, Batch 3... Sec11+Sec12,
    // 40 names" - merged to main just ahead of this batch's own worktree
    // sync, per its own registry.cpp append() call - but this test's own
    // expectedTotal sum was never updated to match, so it was already red
    // on main's tip independent of this batch's own 57 names). Fixed here
    // alongside adding this batch's own namesB16() - both are mechanical,
    // additive corrections to this test's own tracking list, not a new
    // behavior; see this batch's own report for the full note. (At least 8
    // wave-2 agents in total have now independently converged on this same
    // pre-existing root cause, wave 2 batches W2-2 and W2-8 among them.)
    size_t expectedTotal = stub_sections::namesB01().size() + stub_sections::namesB02().size() +
                            stub_sections::namesB03().size() + stub_sections::namesB04().size() +
                            stub_sections::namesB05().size() + stub_sections::namesB06().size() +
                            stub_sections::namesB07().size() + stub_sections::namesB08().size() +
                            stub_sections::namesB09().size() + stub_sections::namesB10().size() +
                            stub_sections::namesB11().size() + stub_sections::namesB12().size() +
                            stub_sections::namesB14().size() +
                            stub_sections::namesB15().size() + // wave 2 batch W2-1 (core dispatchers)
                            stub_sections::namesB16().size() + // wave 2 batch W2-3 fan-out
                            stub_sections::namesB22().size() + stub_sections::namesB23().size() + // wave 2 batch W2-4
                            // b26/b27 (2026-10-08, wave 2 Batch W2-7): spec-lua-api-behaviour.md
                            // Sec38-60, 113 names - see lua_spec_confirmed_stubs_b26_wave2_
                            // vehicles_helicopter.cpp / lua_spec_confirmed_stubs_b27_wave2_
                            // spawn_world_audio.cpp's own top comments.
                            stub_sections::namesB26().size() + stub_sections::namesB27().size() +
                            // b24/b25 (2026-10-08, wave 2 Batch W2-5): "Customization frontends."
                            stub_sections::namesB24().size() + stub_sections::namesB25().size() +
                            stub_sections::namesB17().size() + // wave 2 batch W2-6 (split A, 66 names)
                            stub_sections::namesB18().size() + // wave 2 batch W2-6 (split B, 77 names)
                            stub_sections::namesB19().size() + stub_sections::namesB20().size() + // wave 2 batch W2-8
                            stub_sections::namesB21().size() + // wave 2 batch W2-2
                            stub_sections::namesB28().size() + // 6-native clearance fast-follow onto wave 1 batch 1
                            stub_sections::namesB29().size() + // Sec6 backlog close-out
                            stub_sections::namesB30().size() + // tranche-F close-out + camera_restrict_set_active
                            stub_sections::namesB31().size() + // continuous_spawn_start/on_vehicle_enter/tutorial_start
                            bareGlobalRoster().size() + threadCloseNames().size();
    CHECK(unified.size() == expectedTotal);
    CHECK(unifiedSet.size() == unified.size()); // every entry unique - no track overlaps another
    CHECK(unifiedSet.count("thread_close") == 1); // the exact name this task routed through the union
    for (const auto& e : bareGlobalRoster()) CHECK(unifiedSet.count(e.name) == 1);

    // ---- Part 2: identify the generic stub trampoline's own real,
    // compiled C function pointer from a real generic name (any tagged-
    // list name NOT in the unified list, found in whichever real state
    // actually holds it as a function) - every generic stub shares this
    // exact pointer, regardless of name or state. ----
    lua_CFunction genericPtr = nullptr;
    for (const auto& rn : names) {
        if (unifiedSet.count(rn.name)) continue;
        for (lua_State* L : {gp, ui}) {
            lua_getglobal(L, rn.name.c_str());
            if (lua_type(L, -1) == LUA_TFUNCTION) genericPtr = lua_tocfunction(L, -1);
            lua_pop(L, 1);
            if (genericPtr) break;
        }
        if (genericPtr) break;
    }
    CHECK(genericPtr != nullptr);

    // ---- Part 3: the real end-to-end check, per state. ----
    std::set<std::string> allRealSpecialNames; // union across both states
    for (lua_State* L : {gp, ui}) {
        const char* stateName = (L == gp) ? "gameplay" : "ui";

        // What the real global table actually holds, minus stock Lua 5.1's
        // own base-library functions, minus anything backed by the shared
        // generic-stub pointer.
        std::set<std::string> actualSpecial;
        for (const auto& n : functionGlobalNames(L)) {
            if (stockBaseLibraryFunctionNames().count(n)) continue;
            lua_getglobal(L, n.c_str());
            lua_CFunction ptr = lua_tocfunction(L, -1);
            lua_pop(L, 1);
            if (ptr == genericPtr) continue; // a generic logging stub
            actualSpecial.insert(n);
        }

        // What specConfirmedStubNames() claims is really a function in
        // THIS state (the unified list is flat/state-agnostic; a given
        // name may resolve in one state, both, or - if the list is ever
        // wrong - neither).
        std::set<std::string> expectedSpecial;
        for (const auto& n : unified) {
            lua_getglobal(L, n.c_str());
            bool isFn = lua_type(L, -1) == LUA_TFUNCTION;
            lua_pop(L, 1);
            if (isFn) expectedSpecial.insert(n);
        }

        if (actualSpecial != expectedSpecial) {
            for (const auto& n : actualSpecial) {
                if (!expectedSpecial.count(n)) {
                    std::cerr << "[" << stateName << "] REAL registration '" << n
                              << "' is a distinct, non-generic function but is NOT in "
                                 "specConfirmedStubNames() - exactly the blind spot this test guards\n";
                }
            }
            for (const auto& n : expectedSpecial) {
                if (!actualSpecial.count(n)) {
                    std::cerr << "[" << stateName << "] specConfirmedStubNames() claims '" << n
                              << "' but it is not really a distinct, non-generic function in this state\n";
                }
            }
            ++g_failures;
        }
        allRealSpecialNames.insert(actualSpecial.begin(), actualSpecial.end());
    }

    // Every one of the unified names must be real (a distinct,
    // non-generic function) in AT LEAST one of the two states - catches a
    // stale/dead entry the per-state checks above could otherwise miss if
    // a name were simply absent from both states' own expectedSpecial sets
    // (which would make the per-state comparisons trivially agree on
    // excluding it).
    if (allRealSpecialNames != unifiedSet) {
        for (const auto& n : unifiedSet) {
            if (!allRealSpecialNames.count(n)) {
                std::cerr << "specConfirmedStubNames() claims '" << n
                          << "' but it is not a distinct, non-generic function in EITHER real state\n";
            }
        }
        ++g_failures;
    }
    CHECK(allRealSpecialNames.size() == expectedTotal);

    // ---- A tiny behavioural spot-check on the one name this task
    // specifically threaded through the union: thread_close must still
    // behave exactly as before (a real no-op, never an error) in both
    // states - a structural refactor, not a behaviour change. ----
    for (lua_State* L : {gp, ui}) {
        auto r = host.runChunk(L, "thread_close(123)\nthread_close()", "unified_registry_test.lua");
        if (!(r.loadOk && r.pcallOk)) {
            std::cerr << "thread_close behaviour changed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n";
            ++g_failures;
        }
    }

    if (g_failures == 0) {
        std::cout << "ALL sr3luahost UNIFIED REGISTRY TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(S) FAILED\n";
    return 1;
}
