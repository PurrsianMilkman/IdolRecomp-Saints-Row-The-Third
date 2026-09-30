#include "sr3luahost/stub_registry.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace sr3luahost {

std::vector<RegisteredName> loadTaggedRegistrationList(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        throw std::runtime_error("could not open registration list: " + path);
    }
    std::vector<RegisteredName> out;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); // tolerate CRLF
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue; // malformed line - skip, don't crash the whole load
        RegisteredName rn;
        rn.name = line.substr(0, tab);
        rn.cluster = line.substr(tab + 1);
        if (rn.name.empty()) continue;
        out.push_back(std::move(rn));
    }
    return out;
}

void HitLog::record(const std::string& name, int argc, const std::string& argTypes, const std::string& state) {
    auto& h = hits_[name];
    h.callCount++;
    // Real, per-call state tag (see stub_registry.h's own doc comment) -
    // any tag other than the two real states this project's Host ever
    // registers ("gameplay"/"ui") is not expected, but neither bucket is
    // incremented rather than guessing, so gameplayCallCount+uiCallCount
    // can only ever be <= callCount, never silently wrong.
    if (state == "gameplay") h.gameplayCallCount++;
    else if (state == "ui") h.uiCallCount++;
    h.lastArgc = argc;
    h.lastArgTypes = argTypes;
    totalCalls_++;
}

namespace {

// Console logging is real per-call evidence, but a hot stub called across
// hundreds of scripts could in principle produce an unbounded amount of
// stdout - capped here so one run can't blow up the console/log file.
// HitLog::record() (the actual measurement this task asks for: name, argc,
// arg types, aggregated by call count) is NEVER skipped by this cap - only
// the human-readable per-call print line is.
constexpr uint64_t kMaxConsoleLines = 20000;
uint64_t g_consoleLinesPrinted = 0;

// The one shared C closure every registered stub actually is. Upvalue 1:
// the stub's own Lua-visible name (a Lua string). Upvalue 2: a light-
// userdata pointer back to the HitLog this call's registerStubs()
// invocation was given. Upvalue 3 (added same-day follow-up pass): the
// real state tag ("gameplay"/"ui") this specific registerStubs() call was
// made for - a Lua string, passed straight through to HitLog::record() so
// per-state call counts are real per-call evidence, never inferred from
// this name's own registered cluster tag after the fact (a name can
// genuinely be called from a state its own cluster tag doesn't match -
// see stub_registry.h's own doc comment on StubHit). Real
// lua_gettop()/lua_type()/lua_typename() calls only - the logged
// argc/types are real, observed values from the actual call, never
// invented. Always returns exactly one value, nil, per this task's own
// specified stub contract.
int genericStubTrampoline(lua_State* L) {
    const char* name = lua_tostring(L, lua_upvalueindex(1));
    auto* log = static_cast<HitLog*>(lua_touserdata(L, lua_upvalueindex(2)));
    const char* state = lua_tostring(L, lua_upvalueindex(3));
    int argc = lua_gettop(L);
    std::string types;
    types.reserve(static_cast<size_t>(argc) * 8);
    for (int i = 1; i <= argc; ++i) {
        if (i > 1) types += ",";
        types += lua_typename(L, lua_type(L, i));
    }
    if (g_consoleLinesPrinted < kMaxConsoleLines) {
        std::fprintf(stdout, "[stub] %s argc=%d types=[%s]\n", name ? name : "?", argc, types.c_str());
        ++g_consoleLinesPrinted;
        if (g_consoleLinesPrinted == kMaxConsoleLines) {
            std::fprintf(stdout, "[stub] ... console log cap (%llu lines) reached; further calls still counted in HitLog, just not printed ...\n",
                         static_cast<unsigned long long>(kMaxConsoleLines));
        }
    }
    if (log && name) log->record(name, argc, types, state ? state : "");
    lua_pushnil(L);
    return 1;
}

} // namespace

void registerStubs(lua_State* L, const std::vector<RegisteredName>& names, HitLog& log, const std::string& stateTag) {
    for (const auto& rn : names) {
        lua_pushstring(L, rn.name.c_str());
        lua_pushlightuserdata(L, &log);
        lua_pushstring(L, stateTag.c_str());
        lua_pushcclosure(L, genericStubTrampoline, 3);
        lua_setglobal(L, rn.name.c_str());
    }
}

} // namespace sr3luahost
