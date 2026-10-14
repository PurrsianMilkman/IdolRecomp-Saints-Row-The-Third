// Synthetic tests for the Section 8 batch (2026-10-07, full-document spec
// sweep): audio_conversation_end (Sec8.2), audio_conversation_load_direct
// (Sec8.1), audio_persona_remove_2d (Sec8.6), audio_stop/audio_stop_
// emitting_id (Sec8.9), audio_is_playing (Sec8.3), audio_persona_line_
// playing (Sec8.4), audio_remove_mission_cellphone (Sec8.8). Same fixture
// convention as tests/synthetic_luahost_native_batch1007_test.cpp; all 7
// base names are "gameplay" in tools/lua_all_registered_1490_tagged.txt
// (grepped directly, not assumed); audio_stop is additionally dual-
// registered into the UI state (src/lua_host.cpp's own specUiNames fix).
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

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

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "sec8_sweep_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "sec8_sweep_test.lua");
    if (!r.loadOk || r.pcallOk || r.pcallError.find(needle) == std::string::npos) {
        std::cerr << "expected refusal containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("success") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

std::vector<RegisteredName> fixtureNames() {
    return {
        {"audio_conversation_end", "gameplay"},
        {"audio_conversation_load_direct", "gameplay"},
        {"audio_persona_remove_2d", "gameplay"},
        {"audio_stop", "gameplay"},
        {"audio_stop_emitting_id", "gameplay"},
        {"audio_is_playing", "gameplay"},
        {"audio_persona_line_playing", "gameplay"},
        {"audio_remove_mission_cellphone", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: gameplay for all 8; audio_stop is ALSO dual-registered
    // into the UI state (src/lua_host.cpp's own specUiNames fix), the rest
    // are not.
    {
        Host host(fixtureNames());
        for (const auto& rn : fixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            bool expectUi = (rn.name == "audio_stop");
            CHECK(hasGlobal(host.uiState(), rn.name.c_str()) == expectUi);
        }
    }

    // audio_conversation_end / audio_persona_remove_2d: host-only queue,
    // CONFIRMED false in single player - pure argument consumption.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "audio_conversation_end(5)");
        ok(host, gp, "audio_conversation_end(-1)"); // sentinel, still a no-op either way
        ok(host, gp, "audio_persona_remove_2d(0)");
        ok(host, gp, "audio_persona_remove_2d(42)");
    }

    // audio_conversation_load_direct: a fresh, increasing handle (masked
    // to a byte per the CONFIRMED "truncated to a single byte" contract).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "local h1 = audio_conversation_load_direct('conv1')\n"
                     "local h2 = audio_conversation_load_direct('conv2')\n"
                     "assert(h2 == h1 + 1)");
    }

    // audio_stop / audio_stop_emitting_id: both the SAME underlying
    // function - clears the (always-false, never-populated) slot table.
    // audio_is_playing: always false, this project builds no registry
    // that could ever mark an id "live".
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        lua_State* ui = host.uiState();
        ok(host, gp, "assert(audio_is_playing(1) == false)");
        ok(host, gp, "audio_stop(1)");
        ok(host, gp, "audio_stop_emitting_id(1)");
        ok(host, gp, "assert(audio_is_playing(1) == false)");
        ok(host, ui, "audio_stop(2)"); // the real UI-state registration also works
    }

    // audio_persona_line_playing: unresolved -> refuses; known-not-
    // resolved -> false (by convention); resolved -> OPEN predicate.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "audio_persona_line_playing('Nobody')", "named-object resolution");
        es.objectResolves().set("KnownMissingPersona", false);
        ok(host, gp, "assert(audio_persona_line_playing('KnownMissingPersona') == false)");
        es.objectResolves().set("Persona1", true);
        ok(host, gp, "assert(audio_persona_line_playing('Persona1') == false)"); // ce4556f: no voice instance attached
        es.personaLinePlaying().set("Persona1", true);
        ok(host, gp, "assert(audio_persona_line_playing('Persona1') == true)");
    }

    // audio_remove_mission_cellphone: pure argument consumption, no
    // durable state (this project has no mission-cellphone concept).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "audio_remove_mission_cellphone('Dex')");
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_sec8_sweep_test: all checks passed\n";
    return 0;
}
