// Synthetic tests for the Section 7 batch (2026-10-07, full-document spec
// sweep): character_exists (Sec7.9), human_is_downed (Sec7.13),
// character_show (Sec7.15), character_ragdoll_set_last_resort_position
// (Sec7.18), on_damage (Sec7.4), on_mover_destroyed (Sec7.16), on_detection
// (Sec7.23), follower_make_independent (Sec7.2), follower_set_can_abandon
// (Sec7.6), set_dont_attack_me_on_sight_flag (Sec7.14), npc_suppress_persona
// (Sec7.19), clear_animation_state (Sec7.3), character_prevent_explosion_
// fling (Sec7.20), character_prevent_bumping (Sec7.26), character_set_
// never_catch_fire (Sec7.29), ai_scripted_action_is_complete (Sec7.28),
// player_movement_enable (Sec7.8). Same fixture convention as
// tests/synthetic_luahost_native_batch1007_test.cpp; all 17 names are
// "gameplay" in tools/lua_all_registered_1490_tagged.txt (grepped directly,
// not assumed).
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
    auto r = host.runChunk(L, code, "sec7_sweep_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "sec7_sweep_test.lua");
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
        {"character_exists", "gameplay"},
        {"human_is_downed", "gameplay"},
        {"character_show", "gameplay"},
        {"character_ragdoll_set_last_resort_position", "gameplay"},
        {"on_damage", "gameplay"},
        {"on_mover_destroyed", "gameplay"},
        {"on_detection", "gameplay"},
        {"follower_make_independent", "gameplay"},
        {"follower_set_can_abandon", "gameplay"},
        {"set_dont_attack_me_on_sight_flag", "gameplay"},
        {"npc_suppress_persona", "gameplay"},
        {"clear_animation_state", "gameplay"},
        {"character_prevent_explosion_fling", "gameplay"},
        {"character_prevent_bumping", "gameplay"},
        {"character_set_never_catch_fire", "gameplay"},
        {"ai_scripted_action_is_complete", "gameplay"},
        {"player_movement_enable", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: gameplay only, all 17.
    {
        Host host(fixtureNames());
        for (const auto& rn : fixtureNames()) {
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()));
            CHECK(!hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    // character_exists: resolve-only query.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "assert(character_exists('Nobody') == false)"); // Sec7.9 (78fb40a): unresolved -> false
        es.objectResolves().set("KnownMissing", false);
        ok(host, gp, "assert(character_exists('KnownMissing') == false)");
        es.objectResolves().set("Alice", true);
        ok(host, gp, "assert(character_exists('Alice') == true)");
    }

    // human_is_downed: reuses CharacterState::lifeState, value 6 = downed.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "human_is_downed('Nobody')", "named-object resolution");
        es.objectResolves().set("KnownMissing2", false);
        ok(host, gp, "assert(human_is_downed('KnownMissing2') == false)");
        es.objectResolves().set("Bob", true);
        es.getOrCreateCharacter("Bob").lifeState.set(6);
        ok(host, gp, "assert(human_is_downed('Bob') == true)");
        es.getOrCreateCharacter("Bob").lifeState.set(5);
        ok(host, gp, "assert(human_is_downed('Bob') == false)");
    }

    // character_show / character_ragdoll_set_last_resort_position:
    // resolve-only, no state mutation, no return.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "character_show('Nobody')", "named-object resolution");
        es.objectResolves().set("Carl", true);
        ok(host, gp, "character_show('Carl')");
        refuses(host, gp, "character_ragdoll_set_last_resort_position('Nobody')", "named-object resolution");
        ok(host, gp, "character_ragdoll_set_last_resort_position('Carl')");
    }

    // on_damage / on_mover_destroyed / on_detection: unconditional hook
    // registration, no resolve gate.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "on_damage('cb1', 'Dave')");
        CHECK(es.getOrCreateCharacter("Dave").onDamageCallback == "cb1");
        ok(host, gp, "on_mover_destroyed('cb2', 'Door1')");
        CHECK(es.getOrCreatePlacedObject("Door1").onMoverDestroyedCallback == "cb2");
        ok(host, gp, "on_detection('cb3', 'Eve')");
        CHECK(es.getOrCreateCharacter("Eve").onDetectionCallback == "cb3");
    }

    // follower_make_independent / follower_set_can_abandon /
    // set_dont_attack_me_on_sight_flag / npc_suppress_persona: resolve-gated
    // double-gate bit setters, direct-apply branch (forceFlagGatePasses
    // CONFIRMED true in single player by construction).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "follower_make_independent('Nobody')", "named-object resolution");
        es.objectResolves().set("Frank", true);
        ok(host, gp, "follower_make_independent('Frank', true)");
        CHECK(es.getOrCreateCharacter("Frank").independentFollower.get() == true);
        ok(host, gp, "follower_make_independent('Frank')"); // default false
        CHECK(es.getOrCreateCharacter("Frank").independentFollower.get() == false);

        ok(host, gp, "follower_set_can_abandon('Frank', true)");
        CHECK(es.getOrCreateCharacter("Frank").followerCanAbandon.get() == true);

        ok(host, gp, "set_dont_attack_me_on_sight_flag('Frank', true)");
        CHECK(es.getOrCreateCharacter("Frank").dontAttackMeOnSight.get() == true);

        ok(host, gp, "npc_suppress_persona('Frank', true)");
        CHECK(es.getOrCreateCharacter("Frank").npcSuppressPersona.get() == true);
    }

    // clear_animation_state: writes sentinel -1 via the shared double-gate setter.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "clear_animation_state('Nobody')", "named-object resolution");
        es.objectResolves().set("Gina", true);
        es.getOrCreateCharacter("Gina").animationStateOverride.set(42);
        ok(host, gp, "clear_animation_state('Gina')");
        CHECK(es.getOrCreateCharacter("Gina").animationStateOverride.get() == -1);
    }

    // character_prevent_explosion_fling / character_prevent_bumping /
    // character_set_never_catch_fire: 3 new bits on the shared +0x1c98 field.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        es.objectResolves().set("Hank", true);
        ok(host, gp, "character_prevent_explosion_fling('Hank', true)");
        CHECK((es.getOrCreateCharacter("Hank").forceFlags1c98.get(CharacterState::kPreventExplosionFlingBit1c98) != 0));
        ok(host, gp, "character_prevent_bumping('Hank', true)");
        CHECK((es.getOrCreateCharacter("Hank").forceFlags1c98.get(CharacterState::kPreventBumpingBit1c98) != 0));
        ok(host, gp, "character_set_never_catch_fire('Hank', true)");
        CHECK((es.getOrCreateCharacter("Hank").forceFlags1c98.get(CharacterState::kNeverCatchFireBit1c98) != 0));
        ok(host, gp, "character_prevent_explosion_fling('Hank', false)");
        CHECK((es.getOrCreateCharacter("Hank").forceFlags1c98.get(CharacterState::kPreventExplosionFlingBit1c98) == 0));
    }

    // ai_scripted_action_is_complete: CONFIRMED true on failed resolve
    // (the only predicate in this batch defaulting true, not false). An
    // UNSEEN name still refuses (OPEN), same discipline every other native
    // uses - "failed resolve" means a name known NOT to resolve, not a
    // name never seen at all.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "ai_scripted_action_is_complete('Nobody')", "named-object resolution");
        es.objectResolves().set("NotThere", false);
        ok(host, gp, "assert(ai_scripted_action_is_complete('NotThere') == true)");
        es.objectResolves().set("Known", true);
        ok(host, gp, "assert(ai_scripted_action_is_complete('Known') == true)"); // ce4556f: no ticket -> non-null
        es.getOrCreateCharacter("Known").scriptedActionLookupNonNull.set(true);
        ok(host, gp, "assert(ai_scripted_action_is_complete('Known') == true)");
        es.getOrCreateCharacter("Known").scriptedActionLookupNonNull.set(false);
        ok(host, gp, "assert(ai_scripted_action_is_complete('Known') == false)");
    }

    // player_movement_enable: conditional clear-if-set, no unconditional flip.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "player_movement_enable('Nobody')", "named-object resolution");
        es.objectResolves().set("Ivy", true);
        es.getOrCreateCharacter("Ivy").movementDisabled.set(true);
        ok(host, gp, "player_movement_enable('Ivy')");
        CHECK(es.getOrCreateCharacter("Ivy").movementDisabled.get() == false);
        ok(host, gp, "player_movement_enable('Ivy')"); // already enabled: no-op, no crash
        CHECK(es.getOrCreateCharacter("Ivy").movementDisabled.get() == false);
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_sec7_sweep_test: all checks passed\n";
    return 0;
}
