// Synthetic tests for spec-lua-api-behaviour.md Sec49 ("ranking tranche
// 16") - 2026-10-03 - the 9 of its 25 CONFIRMED names this pass
// implements (the 8 explicitly mandated by the orchestrator, regardless
// of call count, PLUS `spawning_boats` - the one name this batch's own
// fresh mission-drive run found a REAL measured hit for; every other
// name was zero real hits, the same outcome Sec38/Sec39/Sec40's own
// batch already found for their combined 75). See include/sr3luahost/
// engine_state.h and src/lua_spec_confirmed_stubs.cpp (that file's own
// Sec49 batch header comment) for the full per-function reasoning and
// for why the other 16 names stay generic logging stubs this pass. Same
// convention as tests/synthetic_luahost_tranche060708_test.cpp: a small,
// hand-written fixture. Every name below is registered with its own real
// cluster tag, grepped directly from tools/lua_all_registered_1490_
// tagged.txt.
//
// Specific focus (per this task's own explicit instruction): every
// MANDATED crash guard (shop_purchase_purchase_shop's null-shop,
// spawn_override_set_override_category_for_hood's null-category,
// skydive_move_to_check_done's pointer-arithmetic-survives-null-test,
// set_char_in_string's unbounded index) is exercised to confirm this
// host guards it gracefully rather than crashing or throwing uncaught;
// every MANDATED faithful quirk (shop_purchase_purchase_shop's own
// double-charge, store_stronghold_game_purchase_upgrade's charge-then-
// no-op, squad_enable's asymmetric enable/disable, sfx_use_load_images'
// hardcoded true, screen_capture_preview_should_upload's CHOSEN "never
// denied" stand-in) is exercised to confirm it reproduces EXACTLY, not
// "fixed".
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

// Runs `code` and expects it to succeed (asserts inside the chunk do the work).
void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "tranche16_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code` and expects a Lua error whose message contains `needle`.
void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "tranche16_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

// The 9 (name, cluster) rows this batch registers - real tags grepped
// directly from tools/lua_all_registered_1490_tagged.txt.
std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"shop_purchase_purchase_shop", "ui"},
        {"spawn_override_set_override_category_for_hood", "gameplay"},
        {"spawning_boats", "gameplay"},
        {"skydive_move_to_check_done", "gameplay"},
        {"set_char_in_string", "ui"},
        {"store_stronghold_game_purchase_upgrade", "ui"},
        {"squad_enable", "gameplay"},
        {"sfx_use_load_images", "ui"},
        {"screen_capture_preview_should_upload", "ui"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: each of the 8 names lands in its own real cluster
    // tag's state, and nowhere else.
    {
        Host host(batchFixtureNames());
        for (const auto& rn : batchFixtureNames()) {
            lua_State* home = rn.cluster == "ui" ? host.uiState() : host.gameplayState();
            lua_State* away = rn.cluster == "ui" ? host.gameplayState() : host.uiState();
            CHECK(hasGlobal(home, rn.name.c_str()));
            CHECK(!hasGlobal(away, rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* gp = host.gameplayState();
    lua_State* ui = host.uiState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // Sec49.1 shop_purchase_purchase_shop - MANDATED crash guard #1: CHOSEN
    // default (no ambient trigger-handle lookup) -> guarded, no crash.
    CHECK(es.shopPurchase().nullShopGuardCount == 0);
    ok(host, ui, "shop_purchase_purchase_shop()");
    CHECK(es.shopPurchase().nullShopGuardCount == 1);
    ok(host, ui, "shop_purchase_purchase_shop()");
    CHECK(es.shopPurchase().nullShopGuardCount == 2); // every call guarded by this host's own CHOSEN default

    // MANDATED faithful double-charge quirk: once resolved, EVERY call
    // repeats the payment/trigger-disable/broadcast/fly-by side effects;
    // only `owned` is guarded to a single call.
    es.shopPurchase().triggerResolves = true;
    es.shopPurchase().price = 10.0;
    es.playerCash() = 100.0;
    ok(host, ui, "shop_purchase_purchase_shop()");
    CHECK(es.shopPurchase().nullShopGuardCount == 2); // unchanged - resolved now
    CHECK(es.playerCash() == 90.0);
    CHECK(es.shopPurchase().paymentRepeatCount == 1);
    CHECK(es.shopPurchase().owned == true);
    ok(host, ui, "shop_purchase_purchase_shop()"); // CONFIRMED: repeats, no "already purchased" guard
    CHECK(es.playerCash() == 80.0);                // charged AGAIN
    CHECK(es.shopPurchase().paymentRepeatCount == 2);
    CHECK(es.shopPurchase().triggerDisableRepeatCount == 2);
    CHECK(es.shopPurchase().ownedBroadcastRepeatCount == 2);
    CHECK(es.shopPurchase().flyByRepeatCount == 2);
    CHECK(es.shopPurchase().owned == true); // still true - the ONE guarded bit never toggles back

    // ------------------------------------------------------------------
    // Sec49.2 spawn_override_set_override_category_for_hood - MANDATED
    // crash guard #2. CHOSEN default (category never known to resolve,
    // no session) -> null stored plainly, NOT guarded (the real defect's
    // own non-crashing branch); host+session -> guarded (the real
    // defect's own crashing branch).
    CHECK(es.spawnOverride().hoodCategoryImmediateNullDerefGuardCount == 0);
    es.coopSession().present.set(false);
    ok(host, gp, "spawn_override_set_override_category_for_hood('Chinatown', 'never_registered_category')");
    CHECK(es.spawnOverride().hoodCategoryImmediateNullDerefGuardCount == 0); // no session: plain store, not guarded
    CHECK(es.spawnOverride().perHoodCategoryNullStored.count("Chinatown") == 1);

    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    ok(host, gp, "spawn_override_set_override_category_for_hood('Downtown', 'never_registered_category')");
    CHECK(es.spawnOverride().hoodCategoryImmediateNullDerefGuardCount == 1); // host+session: guarded

    es.spawnOverride().categoryNameResolves.set("gang_territory", true);
    ok(host, gp, "spawn_override_set_override_category_for_hood('Downtown', 'gang_territory')");
    CHECK(es.spawnOverride().hoodCategoryImmediateNullDerefGuardCount == 1); // unchanged - resolved now
    CHECK(es.spawnOverride().perHoodCategory["Downtown"] == "gang_territory");
    CHECK(es.spawnOverride().perHoodCategoryNullStored.count("Downtown") == 0); // cleared on a later resolved call

    // ------------------------------------------------------------------
    // Sec49.2 spawning_boats - added after this batch's own fresh
    // mission-drive run found a REAL measured hit (not mandated, but
    // implemented per the task's own call-count-evidence rule). CONFIRMED:
    // writes a host-authoritative, session-replicated byte with NO host
    // gate on the write itself - unlike the gated sibling tranche 13
    // describes.
    CHECK(es.spawningBoats().value == false);
    CHECK(es.spawningBoats().writeCount == 0);
    ok(host, gp, "spawning_boats(true)");
    CHECK(es.spawningBoats().value == true); // CONFIRMED: no host gate on the write itself
    CHECK(es.spawningBoats().writeCount == 1);
    ok(host, gp, "spawning_boats(false)");
    CHECK(es.spawningBoats().value == false);
    CHECK(es.spawningBoats().writeCount == 2);

    // ------------------------------------------------------------------
    // Sec49.3 skydive_move_to_check_done - MANDATED crash guard #3 (new
    // pointer-arithmetic-survives-null-test shape). An unresolved target
    // is guarded and answers "done" (true), matching the sibling
    // list-mode branch's own already-correct answer for the identical
    // failure - regardless of which mode was requested.
    CHECK(es.skydiveMoveToCheckDone().nullTargetGuardCount == 0);
    ok(host, gp, "assert(skydive_move_to_check_done('never_registered_target', false) == true)");
    CHECK(es.skydiveMoveToCheckDone().nullTargetGuardCount == 1);
    es.objectResolves().set("skyBad", false); // explicitly known NOT to resolve
    ok(host, gp, "assert(skydive_move_to_check_done('skyBad', true) == true)");
    CHECK(es.skydiveMoveToCheckDone().nullTargetGuardCount == 2);
    // A resolved target's own done/pending determination is OPEN in
    // either mode - refuses rather than invents an answer.
    es.objectResolves().set("skyGood", true);
    fails(host, gp, "skydive_move_to_check_done('skyGood', false)", "done/pending determination");
    CHECK(es.skydiveMoveToCheckDone().nullTargetGuardCount == 2); // unchanged - not the guarded path

    // ------------------------------------------------------------------
    // Sec49.4 set_char_in_string - MANDATED bounds check (CHOSEN cap
    // 1024). An out-of-range index is guarded and the original string
    // returned unmodified; an in-bounds index builds the CHOSEN
    // space-padded result.
    CHECK(es.setCharInString().indexOutOfBoundsGuardCount == 0);
    ok(host, ui, "assert(set_char_in_string('hello', 99999, 'X') == 'hello')"); // guarded: unmodified
    CHECK(es.setCharInString().indexOutOfBoundsGuardCount == 1);
    ok(host, ui, "assert(set_char_in_string('hello', -1, 'X') == 'hello')"); // negative: guarded too
    CHECK(es.setCharInString().indexOutOfBoundsGuardCount == 2);
    ok(host, ui, "assert(set_char_in_string('hello', 1, 'X') == 'hXllo')"); // in-bounds: overwrite in place
    CHECK(es.setCharInString().indexOutOfBoundsGuardCount == 2);           // unchanged - not guarded
    ok(host, ui, "assert(set_char_in_string('hi', 4, 'X') == 'hi  X')");    // in-bounds, past the end: CHOSEN space-pad
    ok(host, ui, "assert(set_char_in_string('', 0, 'X') == 'X')");

    // ------------------------------------------------------------------
    // Sec49.1 store_stronghold_game_purchase_upgrade - MANDATED faithful
    // quirk (charge-then-no-op) PLUS a MANDATED, separate crash guard (no
    // local-player null check). CHOSEN default (no ambient stronghold
    // context) -> no-op entirely.
    ok(host, ui, "store_stronghold_game_purchase_upgrade()");
    CHECK(es.strongholdPurchaseUpgrade().chargeCount == 0);

    es.strongholdPurchaseUpgrade().strongholdResolves = true;
    es.strongholdPurchaseUpgrade().price = 15.0;
    CHECK(es.strongholdPurchaseUpgrade().localPlayerNullGuardCount == 0);
    fails(host, ui, "store_stronghold_game_purchase_upgrade()", "0x009da4e0"); // local player still OPEN
    es.hasLocalPlayer().set(false);
    ok(host, ui, "store_stronghold_game_purchase_upgrade()"); // local player explicitly absent -> guarded
    CHECK(es.strongholdPurchaseUpgrade().localPlayerNullGuardCount == 1);
    CHECK(es.strongholdPurchaseUpgrade().chargeCount == 0); // guarded before the charge

    es.hasLocalPlayer().set(true);
    es.playerCash() = 50.0;
    es.coopSession().present.set(false); // CONFIRMED single-player: no session -> level-up gate always fails
    ok(host, ui, "store_stronghold_game_purchase_upgrade()");
    CHECK(es.playerCash() == 35.0);                                     // CONFIRMED: charged unconditionally
    CHECK(es.strongholdPurchaseUpgrade().chargeCount == 1);
    CHECK(es.strongholdPurchaseUpgrade().levelUpGateFailedNoopCount == 1); // CONFIRMED: level-up silently no-ops
    ok(host, ui, "store_stronghold_game_purchase_upgrade()"); // repeats every call - no "already upgraded" guard either
    CHECK(es.playerCash() == 20.0);
    CHECK(es.strongholdPurchaseUpgrade().chargeCount == 2);
    CHECK(es.strongholdPurchaseUpgrade().levelUpGateFailedNoopCount == 2);

    // ------------------------------------------------------------------
    // Sec49.3 squad_enable - MANDATED faithful quirk: asymmetric
    // enable/disable, NOT real inverses.
    auto& crewA = es.squadMember("Pierce");
    crewA.inRoster = true; // simulate an already-recruited crew member
    ok(host, gp, "squad_enable('Pierce', false)");
    CHECK(crewA.inRoster == false); // CONFIRMED: actually dismissed
    CHECK(crewA.dismissCount == 1);

    CHECK(crewA.flagBitA == false);
    CHECK(crewA.flagBitB == false);
    ok(host, gp, "squad_enable('Pierce', true)");
    CHECK(crewA.flagBitA == true); // CONFIRMED: flips two unrelated flag bits
    CHECK(crewA.flagBitB == true);
    CHECK(crewA.enableTrueCount == 1);
    CHECK(crewA.inRoster == false); // CONFIRMED real defect: does NOT re-add to the crew - NOT a real inverse of false

    // Default argument (CONFIRMED false when omitted).
    auto& crewB = es.squadMember("Shaundi");
    crewB.inRoster = true;
    ok(host, gp, "squad_enable('Shaundi')");
    CHECK(crewB.inRoster == false);
    CHECK(crewB.dismissCount == 1);

    // ------------------------------------------------------------------
    // Sec49.4 sfx_use_load_images - MANDATED trivial constant true.
    ok(host, ui, "assert(sfx_use_load_images() == true)");

    // ------------------------------------------------------------------
    // Sec49.4 screen_capture_preview_should_upload - MANDATED faithful
    // quirk PLUS the MANDATED CHOSEN "never denied" platform-privilege
    // stand-in. 0 Lua return values either way (CONFIRMED).
    CHECK(es.screenCapturePreview().explicitFalseCancelCount == 0);
    ok(host, ui, "assert(select('#', screen_capture_preview_should_upload(false)) == 0)");
    CHECK(es.screenCapturePreview().explicitFalseCancelCount == 1);
    CHECK(es.screenCapturePreview().uploadProceedCount == 0);
    ok(host, ui, "assert(select('#', screen_capture_preview_should_upload(true)) == 0)");
    CHECK(es.screenCapturePreview().uploadProceedCount == 1); // CHOSEN stand-in: never "denied" from this host
    CHECK(es.screenCapturePreview().explicitFalseCancelCount == 1); // unchanged

    if (g_failures == 0)
        std::cout << "batch 2026-10-03 (Sec49 ranking tranche 16, 9 functions): all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
