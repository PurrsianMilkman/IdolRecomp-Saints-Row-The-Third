// Refusal stress test for the spec stubs (cloud phase, 2026-09-30; manager
// follow-up to bridge job 20260930T225845-team-b-jklk).
//
// jklk crashed on MSVC with 0xC0000374 (heap corruption): stubs raised Lua
// errors (longjmp) from inside openGuard's C++ frame, and MSVC's unwinding
// longjmp destroyed a live std::string twice. glibc's longjmp does not
// unwind, so this test is mainly an MSVC regression check (the windows-latest
// CI job). On Linux it still checks the refusal messages.
//
// Every spec stub is called in every state that has it, with a spread of
// argument shapes (long heap-allocated strings included), many times over,
// both through pcall and as an unprotected top-level error that the host's
// own lua_pcall catches. Engine state is left OPEN throughout, so the stubs
// that read it refuse on every call.
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/host.h"
#include "sr3luahost/spec_confirmed_stubs.h"

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

bool hasGlobal(lua_State* L, const std::string& name) {
    lua_getglobal(L, name.c_str());
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}
} // namespace

int main(int argc, char** argv) {
    int rounds = argc > 1 ? std::stoi(argv[1]) : 300;
    auto names = loadTaggedRegistrationList(CRREISH_TOOLS_DIR "/lua_all_registered_1490_tagged.txt");
    Host host(names);
    const std::string longName(200, 'n'); // well past any small-string buffer
    const std::vector<std::string> argShapes = {
        "", "nil", "'x'", "'" + longName + "'", "{}", "{1,2,3}", "1,2,3", "'a',{},nil,5", "-1", "1e300",
        "true", "function() end", "'" + longName + "','" + longName + "'", "0/0", "5", "setmetatable({}, {__index = function() error('idx') end})",
    };

    int statesRun = 0;
    for (lua_State* L : {host.gameplayState(), host.uiState()}) {
        std::string body;
        int stubsHere = 0;
        for (const std::string& n : specConfirmedStubNames()) {
            if (!hasGlobal(L, n)) continue;
            ++stubsHere;
            for (const std::string& a : argShapes) body += "  r(" + n + (a.empty() ? "" : ", " + a) + ")\n";
        }
        CHECK(stubsHere > 0);
        std::string chunk =
            "local calls, refused = 0, 0\n"
            "local function r(f, ...) calls = calls + 1; if not pcall(f, ...) then refused = refused + 1 end end\n"
            "for i = 1, " + std::to_string(rounds) + " do\n" + body + "end\n"
            "return calls, refused\n";
        // Run through the host's own runChunk (its lua_pcall), as lua_host_run does.
        auto r = host.runChunk(L, chunk, "refusal_stress.lua");
        CHECK(r.loadOk && r.pcallOk);
        if (!r.pcallOk) std::cerr << r.pcallError << "\n";

        // Unprotected refusals: each raises straight out to runChunk's lua_pcall.
        for (int i = 0; i < rounds; ++i) {
            auto e = host.runChunk(L, "zscene_is_loaded('" + longName + "')", "z.lua");
            if (hasGlobal(L, "zscene_is_loaded")) {
                CHECK(e.loadOk && !e.pcallOk);
                CHECK(e.pcallError.rfind("zscene_is_loaded: engine state ", 0) == 0);
            }
            auto f = host.runChunk(L, "fade_out(1, 5)", "f.lua");
            if (hasGlobal(L, "fade_out")) CHECK(f.loadOk && !f.pcallOk);
            // The fade state has a CONFIRMED start-up value since batch
            // 2026-10-01 (Sec26.24): sfx_faded_out answers; zscene_prep still
            // refuses on the OPEN scene table.
            auto s = host.runChunk(L, "assert(type(sfx_faded_out()) == 'boolean')", "s.lua");
            if (hasGlobal(L, "sfx_faded_out")) CHECK(s.loadOk && s.pcallOk);
            auto p = host.runChunk(L, "zscene_prep('" + longName + "')", "p.lua");
            if (hasGlobal(L, "zscene_prep")) {
                CHECK(p.loadOk && !p.pcallOk);
                CHECK(p.pcallError.rfind("zscene_prep: engine state ", 0) == 0);
            }
            if (g_failures) break;
        }
        ++statesRun;
    }
    CHECK(statesRun == 2);
    if (g_failures == 0) std::cout << "refusal stress: all checks passed (" << rounds << " rounds per state)\n";
    return g_failures == 0 ? 0 : 1;
}
