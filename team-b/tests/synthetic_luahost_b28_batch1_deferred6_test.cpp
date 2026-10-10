// Synthetic tests for the 6-native clearance fast-follow onto wave 1 batch
// 1 (src/lua_spec_confirmed_stubs_b28_batch1_deferred6.cpp): on_dismiss
// (Sec1.11), character_prevent_flinching (Sec3.14), character_allow_ragdoll
// (Sec3.16), npc_combat_enable (Sec3.17), inv_weapon_add_temporary
// (Sec4.10), inv_weapon_remove_temporary (Sec4.11). Same hand-written-
// fixture convention as every other synthetic_luahost_*_test. All 6 are
// `gameplay` per tools/lua_all_registered_1490_tagged.txt (grepped
// directly).
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"
#include "sr3luahost/zone_object_resolver.h" // AsciiLowerFold - matching the stub's own case-fold

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "b28_batch1_deferred6_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "b28_batch1_deferred6_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"on_dismiss", "gameplay"},
        {"character_prevent_flinching", "gameplay"},
        {"character_allow_ragdoll", "gameplay"},
        {"npc_combat_enable", "gameplay"},
        {"inv_weapon_add_temporary", "gameplay"},
        {"inv_weapon_remove_temporary", "gameplay"},
    };
}

} // namespace

int main() {
    Host host(batchFixtureNames());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // on_dismiss (Sec1.11): plain character resolver - ALWAYS lazily
    // resolves (no objectResolves() gate, same convention as on_revived),
    // registers hook slot 10 via onDismissCallback, empty string clears.
    // ------------------------------------------------------------------
    CHECK(es.getOrCreateCharacter("Bob").onDismissCallback.empty());
    ok(host, gp, "on_dismiss('cbName', 'Bob')");
    CHECK(es.getOrCreateCharacter("Bob").onDismissCallback == "cbName");
    ok(host, gp, "on_dismiss('', 'Bob')"); // CONFIRMED: empty string clears the slot
    CHECK(es.getOrCreateCharacter("Bob").onDismissCallback.empty());
    // No objectResolves() gate at all - a never-before-seen name still
    // lazily resolves via getOrCreateCharacter(), matching on_revived.
    ok(host, gp, "on_dismiss('cb2', 'BrandNewName')");
    CHECK(es.getOrCreateCharacter("BrandNewName").onDismissCallback == "cb2");

    // ------------------------------------------------------------------
    // character_prevent_flinching (Sec3.14): OPEN until objectResolves()
    // is set; direct-sense bit 0x800 on forceFlags1c98.
    // ------------------------------------------------------------------
    fails(host, gp, "character_prevent_flinching('Nobody', true)", "named-object resolution");
    es.objectResolves().set("Nobody", false);
    ok(host, gp, "character_prevent_flinching('Nobody', true)"); // resolved-but-false -> no-op, no crash
    es.objectResolves().set("Bob", true);
    ok(host, gp, "character_prevent_flinching('Bob', true)");
    CHECK(es.getOrCreateCharacter("Bob").forceFlags1c98.get(CharacterState::kPreventFlinchingBit1c98) ==
          CharacterState::kPreventFlinchingBit1c98);
    ok(host, gp, "character_prevent_flinching('Bob')"); // no nil-gate - omitted arg2 -> Lua nil -> false
    CHECK(es.getOrCreateCharacter("Bob").forceFlags1c98.get(CharacterState::kPreventFlinchingBit1c98) == 0u);

    // ------------------------------------------------------------------
    // character_allow_ragdoll (Sec3.16): negated-sense bit 0x80
    // (kNeverRagdollBit1c98) - allow=true CLEARS it, allow=false SETS it.
    // ------------------------------------------------------------------
    ok(host, gp, "character_allow_ragdoll('Bob', true)");
    CHECK(es.getOrCreateCharacter("Bob").forceFlags1c98.get(CharacterState::kNeverRagdollBit1c98) == 0u);
    ok(host, gp, "character_allow_ragdoll('Bob', false)");
    CHECK(es.getOrCreateCharacter("Bob").forceFlags1c98.get(CharacterState::kNeverRagdollBit1c98) ==
          CharacterState::kNeverRagdollBit1c98);
    // Independence from the prevent_flinching bit on the SAME shared byte.
    ok(host, gp, "character_prevent_flinching('Bob', true)");
    CHECK(es.getOrCreateCharacter("Bob").forceFlags1c98.get(CharacterState::kPreventFlinchingBit1c98) ==
          CharacterState::kPreventFlinchingBit1c98);
    CHECK(es.getOrCreateCharacter("Bob").forceFlags1c98.get(CharacterState::kNeverRagdollBit1c98) ==
          CharacterState::kNeverRagdollBit1c98); // unchanged by the sibling bit's own write

    // ------------------------------------------------------------------
    // npc_combat_enable (Sec3.17): default true, negated-sense bit 0x10 on
    // the AI-data sub-object's OWN +8 byte (humanAiDataForceFlagsByte8) -
    // a DIFFERENT field from humanAiDataForceFlagsByte9 (npc_hearing_enable,
    // Sec21.21's own +9 byte), verified independent below.
    // ------------------------------------------------------------------
    ok(host, gp, "npc_combat_enable('Bob')"); // default true -> enable combat -> bit clear
    CHECK(es.getOrCreateCharacter("Bob").humanAiDataForceFlagsByte8.get(CharacterState::kCombatDisabledBit) == 0u);
    ok(host, gp, "npc_combat_enable('Bob', false)");
    CHECK(es.getOrCreateCharacter("Bob").humanAiDataForceFlagsByte8.get(CharacterState::kCombatDisabledBit) ==
          CharacterState::kCombatDisabledBit);
    // Distinct from the +9 byte (npc_hearing_enable's own field) - setting
    // one never marks the other known.
    CHECK(!es.getOrCreateCharacter("Bob").humanAiDataForceFlagsByte9.known(CharacterState::kDeafBit));

    // ------------------------------------------------------------------
    // inv_weapon_add_temporary (Sec4.10) / inv_weapon_remove_temporary
    // (Sec4.11): target + weapon both OPEN until resolved; add writes the
    // full slot; remove clears the marker/ammo/flags but leaves the slot
    // present (CONFIRMED: no claim the slot itself is erased, only its
    // fields); case-insensitive weapon-name lookup (AsciiLowerFold, the
    // SAME _stricmp-equivalent fold this project's zone-object resolver
    // already established).
    // ------------------------------------------------------------------
    fails(host, gp, "inv_weapon_add_temporary('Bob', 'GDHC')", "weapon-definition name resolution");
    es.weaponDefinitionResolves().set(AsciiLowerFold("GDHC"), true);
    ok(host, gp, "inv_weapon_add_temporary('Bob', 'gdhc', 30, true, false, true, false, 7)");
    {
        auto& slot = es.weaponTemporaryInventory()["Bob"][AsciiLowerFold("GDHC")];
        CHECK(slot.temporaryGrant == true);
        CHECK(slot.ammoCount == 30);
        CHECK(slot.rawArg4 == true);
        CHECK(slot.rawArg5 == false);
        CHECK(slot.rawArg6 == true);
        CHECK(slot.rawArg7 == false);
        CHECK(slot.rawArg8 == 7);
        CHECK(slot.addCount == 1);
    }
    // Defaults: arg3->1, args4-7->false, arg8->0.
    es.weaponDefinitionResolves().set(AsciiLowerFold("Pistol"), true);
    ok(host, gp, "inv_weapon_add_temporary('Bob', 'Pistol')");
    {
        auto& slot = es.weaponTemporaryInventory()["Bob"][AsciiLowerFold("pistol")];
        CHECK(slot.ammoCount == 1);
        CHECK(slot.rawArg4 == false && slot.rawArg8 == 0);
    }

    // remove: unresolved weapon name throws; resolved-but-never-added is a
    // silent no-op; resolved-and-added clears the fields.
    fails(host, gp, "inv_weapon_remove_temporary('Bob', 'Unknown')", "weapon-definition name resolution");
    es.weaponDefinitionResolves().set(AsciiLowerFold("NeverAdded"), true);
    ok(host, gp, "inv_weapon_remove_temporary('Bob', 'NeverAdded')"); // no existing slot - no-op, no crash
    ok(host, gp, "inv_weapon_remove_temporary('Bob', 'GDHC')"); // case-insensitive vs. the 'gdhc' add above
    {
        auto& slot = es.weaponTemporaryInventory()["Bob"][AsciiLowerFold("gdhc")];
        CHECK(slot.temporaryGrant == false);
        CHECK(slot.ammoCount == 0);
        CHECK(slot.removeCount == 1);
    }

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_b28_batch1_deferred6_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_b28_batch1_deferred6_test: " << g_failures << " check(s) failed\n";
    return 1;
}
