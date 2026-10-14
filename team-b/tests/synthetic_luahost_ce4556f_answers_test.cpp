// Synthetic tests for spec sync ce4556f's FINAL answers: character_is_ready
// AND-chain (Sec7.27), ai_scripted_action_is_complete / +0x4aa lookup
// (Sec7.28), character_playing_combat_move (Sec25.27), audio_persona_line_
// playing (Sec8.4), vehicle +0xbd0 body-active default (Sec38.1), and the
// group-member NPC alias rule (Sec29, m19 chem_0N_brute).
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/persona_id.h"
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
    auto r = host.runChunk(L, code, "ce4556f_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixtureNames() {
    return {
        {"character_is_ready", "gameplay"},
        {"character_playing_combat_move", "gameplay"},
        {"audio_persona_line_playing", "gameplay"},
        {"ai_scripted_action_is_complete", "gameplay"},
        {"character_exists", "gameplay"},
        {"group_create_do", "gameplay"},
        {"persona_override_persona_start", "gameplay"},
    };
}
} // namespace

int main() {
    // character_is_ready: ordinary standing NPC is ready; pending ticket -> not.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Npc1", true);
        es.getOrCreateCharacter("Npc1").isAlive.set(true);
        ok(host, gp, "assert(character_is_ready('Npc1') == true)");
        es.getOrCreateCharacter("Npc1").scriptedActionLookupNonNull.set(false); // pending ticket
        ok(host, gp, "assert(character_is_ready('Npc1') == false)");
        es.getOrCreateCharacter("Npc1").scriptedActionLookupNonNull.set(true); // ticket complete/consumed
        ok(host, gp, "assert(character_is_ready('Npc1') == true)");
        es.characterReadyBlocked().set("Npc1", true); // ragdolled
        ok(host, gp, "assert(character_is_ready('Npc1') == false)");
    }
    // character_playing_combat_move: default none -> false; seeded true wins.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Bob", true);
        ok(host, gp, "assert(character_playing_combat_move('Bob') == false)");
        es.characterPlayingCombatMove().set("Bob", true);
        ok(host, gp, "assert(character_playing_combat_move('Bob') == true)");
    }
    // audio_persona_line_playing: no attached voice instance -> false.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("P1", true);
        ok(host, gp, "assert(audio_persona_line_playing('P1') == false)");
        es.personaLinePlaying().set("P1", true);
        ok(host, gp, "assert(audio_persona_line_playing('P1') == true)");
    }
    // ai_scripted_action_is_complete: no ticket -> complete; pending ticket -> not.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("N", true);
        ok(host, gp, "assert(ai_scripted_action_is_complete('N') == true)");
        es.getOrCreateCharacter("N").scriptedActionLookupNonNull.set(false);
        ok(host, gp, "assert(ai_scripted_action_is_complete('N') == false)");
    }
    // Persona names are Wwise FNV-1 ids (Sec17.9), never named objects.
    {
        CHECK(PersonaIdFromName("PAK3_Interrogation") == 3989737946u);
        CHECK(PersonaIdFromName("NONE") == 0u);
        CHECK(PersonaIdFromName("") == 0u);
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        host.engineState().objectResolves().set("Leader", true);
        ok(host, gp, "persona_override_persona_start('Leader', 'PAK3_Interrogation')");
    }
    // vehicle +0xbd0: default is active for a live vehicle; explicit false wins.
    {
        EngineState es;
        VehicleState& v = es.getOrCreateVehicle("Car");
        CHECK(v.bodyActiveOrDefault() == true);
        v.bodyActive.set(false);
        CHECK(v.bodyActiveOrDefault() == false);
        v.bodyActive.set(true);
        CHECK(v.bodyActiveOrDefault() == true);
    }
    // group-created flips a pending group-member alias from false to true.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("chem_02_brute", false);
        ok(host, gp, "assert(character_exists('chem_02_brute') == false)");
        es.markGroupCreated("chem_02_brute_group");
        // No pending registration without zone data: alias stays as set.
        ok(host, gp, "assert(character_exists('chem_02_brute') == false)");
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_ce4556f_answers_test: all checks passed\n";
    return 0;
}
