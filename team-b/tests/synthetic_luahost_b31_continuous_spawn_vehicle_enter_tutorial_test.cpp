// Synthetic tests for batch b31
// (src/lua_spec_confirmed_stubs_b31_continuous_spawn_vehicle_enter_tutorial.cpp):
// continuous_spawn_start (Sec4.8), on_vehicle_enter (Sec4.12),
// tutorial_start (Sec6.19). Same hand-written-fixture convention as every
// other synthetic_luahost_*_test.
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
        }                                                                                      \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "b31_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "b31_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"continuous_spawn_start", "gameplay"},
        {"continuous_spawn_stop", "gameplay"}, // b08 - pulled in for the stop/restart cross-check below
        {"on_vehicle_enter", "gameplay"},
        {"tutorial_start", "gameplay"},
    };
}

} // namespace

int main() {
    // ----------------------------------------------------------------
    // continuous_spawn_start (Sec4.8): unresolved name -> OPEN refusal;
    // explicitly-false resolve -> silent no-op; a fresh registration
    // writes every argument (with CONFIRMED defaults for the ones
    // omitted) and activates once; a second call for the SAME region
    // takes the fast re-start path (only the active flag moves); after a
    // real continuous_spawn_stop (whole-slot reset), a third call for the
    // same name claims a genuinely fresh record again.
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        fails(host, gp, "continuous_spawn_start('NoSuchRegion')", "named-object resolution");

        es.objectResolves().set("GhostRegion", false);
        ok(host, gp, "continuous_spawn_start('GhostRegion')");
        CHECK(es.continuousSpawnRegions().count("GhostRegion") == 0);

        es.objectResolves().set("Zone1", true);
        // arg4 omitted -> CONFIRMED default "" (spec-lua-api-behaviour.md
        // Sec4.8); no OpenValue refusal anymore.
        CHECK(es.continuousSpawnDefaultTypeFilter() == "");
        ok(host, gp, "continuous_spawn_start('Zone1')"); // every optional arg omitted
        {
            auto& rec = es.continuousSpawnRegions().at("Zone1");
            CHECK(rec.count == 0);
            CHECK(rec.respawnIntervalMs == 0);
            CHECK(rec.typeNameFilter == "");
            CHECK(rec.numericArg5 == 1);
            CHECK(rec.numericArg6 == 14);
            CHECK(rec.numericArg7 == 19);
            CHECK(!rec.hasArg8 && rec.arg8String.empty());
            CHECK(!rec.hasArg9 && rec.arg9String.empty());
            CHECK(!rec.hasArg10 && rec.arg10RawPointerString.empty());
            CHECK(rec.started == true);
            CHECK(rec.activationCount == 1);
        }
        CHECK(es.continuousSpawnActive().at("Zone1") == true);

        // Fast re-start path: a second call with DIFFERENT argument values
        // for an EXISTING region must NOT rewrite anything but the active
        // flag (CONFIRMED).
        es.continuousSpawnActive()["Zone1"] = false; // simulate having been stopped-without-reset isn't real; just flip the flag directly
        ok(host, gp, "continuous_spawn_start('Zone1', 99, 5.0, 'other_filter', 2, 3, 4, 'a', 'b', 'c')");
        {
            auto& rec = es.continuousSpawnRegions().at("Zone1");
            CHECK(rec.count == 0);                          // UNCHANGED - fast re-start path never rewrites
            CHECK(rec.typeNameFilter == ""); // UNCHANGED
            CHECK(rec.activationCount == 1);                  // UNCHANGED - no second activation
        }
        CHECK(es.continuousSpawnActive().at("Zone1") == true); // flipped back to true by the fast re-start path

        // A real continuous_spawn_stop fully resets the slot (erases the
        // detailed record) - a subsequent start claims a genuinely fresh
        // one.
        ok(host, gp, "continuous_spawn_stop('Zone1')");
        CHECK(es.continuousSpawnActive().at("Zone1") == false);
        CHECK(es.continuousSpawnRegions().count("Zone1") == 0);
        ok(host, gp, "continuous_spawn_start('Zone1', 7, 2.0, 'other_filter', 2, 3, 4, 'a', 'b', 'c')");
        {
            auto& rec = es.continuousSpawnRegions().at("Zone1");
            CHECK(rec.count == 7);                    // genuinely fresh this time
            CHECK(rec.respawnIntervalMs == 2000);      // 2.0 * 1000.0, truncated
            CHECK(rec.typeNameFilter == "other_filter"); // arg 4 given explicitly this time
            CHECK(rec.numericArg5 == 2 && rec.numericArg6 == 3 && rec.numericArg7 == 4);
            CHECK(rec.hasArg8 && rec.arg8String == "a");
            CHECK(rec.hasArg9 && rec.arg9String == "b");
            CHECK(rec.hasArg10 && rec.arg10RawPointerString == "c");
            CHECK(rec.activationCount == 1);
        }
    }

    // ----------------------------------------------------------------
    // on_vehicle_enter (Sec4.12): 3-way branch. Branch 1 (generic/
    // #PLAYER#) -> character hook slot 0x16 (+0x1f00). Branch 2 (type-
    // gated bit 0x4@+0xb) -> character hook slot 0x17 (+0xf8). Branch 3
    // (type-gated bit 0x8@+0xb) -> vehicle hook slot 7 (+0xb0). Neither
    // resolver matching -> silent no-op; a genuinely unregistered name
    // refuses (OPEN).
    // ----------------------------------------------------------------
    {
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        // playerClassChainResolves(name)'s own CHOSEN default (engine_state.h)
        // now resolves a non-vehicle-shaped, never-registered name to TRUE
        // automatically, so it lands on branch 1 with no .set() call at all -
        // demonstrated directly, replacing this spot's former "OPEN until
        // set" refusal for an arbitrary unregistered name.
        ok(host, gp, "on_vehicle_enter('default_cb', 'NoSuchCharacter')");
        CHECK(es.getOrCreateCharacter("NoSuchCharacter").onVehicleEnterGenericSlotCallback == "default_cb");

        // A genuinely vehicle-shaped name still defaults to FALSE on branch 1
        // (same CHOSEN heuristic) and, with neither objectResolves() nor
        // onVehicleEnterResolverKind() registered for it either, still
        // refuses OPEN exactly as before - the default only changes WHICH
        // resolver throws first for a totally unregistered name, never the
        // eventual all-unknown outcome.
        fails(host, gp, "on_vehicle_enter('cb', 'NoSuchVehicle 001')", "is OPEN");

        // Branch 1: generic/#PLAYER# chain.
        es.playerClassChainResolves().set("#PLAYER#", true);
        ok(host, gp, "on_vehicle_enter('on_enter_cb', '#PLAYER#')");
        CHECK(es.getOrCreateCharacter("#PLAYER#").onVehicleEnterGenericSlotCallback == "on_enter_cb");
        CHECK(es.getOrCreateCharacter("#PLAYER#").onVehicleEnterCharacterSlotCallback.empty());

        // Branch 2: type-gated bit 0x4@+0xb ("character" tier).
        es.playerClassChainResolves().set("Shaundi", false);
        es.objectResolves().set("Shaundi", true);
        es.onVehicleEnterResolverKind().set("Shaundi", 1);
        ok(host, gp, "on_vehicle_enter('seat_cb', 'Shaundi')");
        CHECK(es.getOrCreateCharacter("Shaundi").onVehicleEnterCharacterSlotCallback == "seat_cb");
        CHECK(es.getOrCreateCharacter("Shaundi").onVehicleEnterGenericSlotCallback.empty());

        // Branch 3: type-gated bit 0x8@+0xb ("vehicle" tier).
        es.playerClassChainResolves().set("Hollywood", false);
        es.objectResolves().set("Hollywood", true);
        es.onVehicleEnterResolverKind().set("Hollywood", 2);
        ok(host, gp, "on_vehicle_enter('vehicle_cb', 'Hollywood')");
        CHECK(es.getOrCreateVehicle("Hollywood").onVehicleEnterCallback == "vehicle_cb");

        // Empty-string callback clears the slot (same convention every
        // other 0x005e4660-backed hook field uses).
        ok(host, gp, "on_vehicle_enter('', 'Hollywood')");
        CHECK(es.getOrCreateVehicle("Hollywood").onVehicleEnterCallback.empty());

        // Neither resolver matches (explicitly false) -> silent no-op.
        es.playerClassChainResolves().set("Parked1", false);
        es.objectResolves().set("Parked1", false);
        ok(host, gp, "on_vehicle_enter('cb', 'Parked1')");
        CHECK(!es.getOrCreateCharacter("Parked1").onVehicleEnterGenericSlotCallback.size());
        CHECK(!es.getOrCreateCharacter("Parked1").onVehicleEnterCharacterSlotCallback.size());
    }

    // ----------------------------------------------------------------
    // tutorial_start (Sec6.19): the 210-entry name table, transcribed
    // directly from spec-lua-api-behaviour.md's own lines 3324-3393 (NOT
    // re-derived from lua_tutorial_names.cpp/EngineState::tutorialName -
    // an independent check that the two actually agree), plus the 12-step
    // dispatcher.
    // ----------------------------------------------------------------
    {
        CHECK(EngineState::kTutorialEntryCount == 210);
        CHECK(std::string(EngineState::tutorialName(0)) == "save");
        CHECK(std::string(EngineState::tutorialName(1)) == "autosave");
        CHECK(std::string(EngineState::tutorialName(12)) == "combat");
        CHECK(std::string(EngineState::tutorialName(36)) == "escort_minigame");
        CHECK(std::string(EngineState::tutorialName(94)) == "mission_checkpoint");
        CHECK(std::string(EngineState::tutorialName(119)) == "territory_");
        CHECK(std::string(EngineState::tutorialName(122)) == "vehicle entry"); // literal space, not underscore
        CHECK(std::string(EngineState::tutorialName(129)) == "escort_minigame"); // genuine shipped duplicate of 36
        CHECK(std::string(EngineState::tutorialName(150)) == "game_complete");
        CHECK(std::string(EngineState::tutorialName(176)) == "dlc");
        CHECK(std::string(EngineState::tutorialName(185)) == "TUT_SIXAXIS_BOAT");
        CHECK(std::string(EngineState::tutorialName(200)) == "horde_mode_unlimited_ammo");
        CHECK(std::string(EngineState::tutorialName(209)) == "dlc3_win");
        int named = 0;
        for (int i = 0; i < EngineState::kTutorialEntryCount; ++i) named += EngineState::tutorialName(i) ? 1 : 0;
        CHECK(named == 210); // zero nulls, per Sec6.19's own "verified: 210/210 entries, zero nulls"
    }
    {
        // An unresolved name -> silent no-op, no dispatcher reached at
        // all (so none of the dispatcher's own OPEN globals need to be
        // set for this one call).
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "tutorial_start('not_a_real_tutorial_name')");
    }
    {
        // Dispatcher OPEN-state discipline: a resolved name still refuses
        // until the dispatcher's own OPEN globals are supplied.
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        ok(host, gp, "tutorial_start('save')"); // flags/gate known at construction (Team A sync 9c70c61): no OPEN refusal remains
    }
    {
        // Full success path - simple-prompt branch (steps 1-11 "else"),
        // then the "already queued" no-rewrite sub-branch, then a host
        // replicate (step 12).
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        EngineState::TutorialStart& ts = es.tutorialStart();
        ts.suppressionByte0151d5a6.set(false);
        // Both of tutorial_start's own dispatcher-step globals are now
        // construction-time pre-seeded defaults, not OPEN values a test must
        // supply: step 2's 0x029443bc (the machinima recorder's recording-
        // state machine, "idle"=0) via applySpecInitialState() - corrected
        // 2026-10-10, Team A spec sync - and step 5's 0x00706ab0 (mode-stack-
        // top, empty-stack default=0) via EngineState::EngineState()'s own
        // seed, same correction pass. No .set() needed here any more for
        // either (both were previously supplied per-test while still OPEN).
        ts.mode014f3d34.set(false);
        ts.noMissionLive006d2910.set(true);
        ts.flagsByte24.set(EngineState::tutorialStateKey(0), 0u); // no rich-widget/mission-active/host-only bits

        CHECK(es.tutorialState().get("0") == 0); // CONFIRMED initial state for index < 189
        ok(host, gp, "tutorial_start('save', 5, true, false, true)"); // F=true drives state 0 -> 1 (step 9); B5=true -> argument5GatedValue=0, no defaultParam20 needed
        CHECK(es.tutorialState().get("0") == 2); // simple-prompt queued (step 11)
        {
            auto& entry = ts.entries.at(0);
            CHECK(entry.queuedForSimplePrompt == true);
            CHECK(entry.argument2Value == 5);
            CHECK(entry.argument5GatedValue == 0);
            CHECK(entry.hiddenLiteralP4 == 1);
        }
        CHECK(ts.replicateCallCount == 0); // no co-op session -> step 12 never fires

        // Calling again while state==2 stops at step 10 (not 1, not (B4
        // and 4), not >=189) - entirely before step 11, so nothing in the
        // entry record changes even with different arguments.
        ok(host, gp, "tutorial_start('save', 999, false, false, false)");
        CHECK(ts.entries.at(0).argument2Value == 5); // UNCHANGED

        // Force state back to 1 (test-direct, exercising step 11's OWN
        // "already queued -> do not rewrite" sub-branch, reachable only
        // this way in a single-process model): queuedForSimplePrompt is
        // still true from before, so a fresh dispatch must still skip the
        // ENTIRE step-11 body, including the state write - per the spec's
        // own text, "set state 2" is itself inside the "if not already
        // queued" gate, not a separate unconditional action.
        es.tutorialState().set("0", 1);
        ok(host, gp, "tutorial_start('save', 777, false, false, false)");
        CHECK(es.tutorialState().get("0") == 1); // UNCHANGED by this call - step 11's whole body was skipped
        CHECK(ts.entries.at(0).argument2Value == 5); // STILL unchanged - "already queued" guard held
    }
    {
        // B5 = false drives the descriptor's own +0x20 default parameter
        // through (OPEN per-index until a test supplies it).
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        EngineState::TutorialStart& ts = es.tutorialStart();
        ts.suppressionByte0151d5a6.set(false);
        // Both dispatcher-step globals pre-seeded (see first test block above).
        ts.mode014f3d34.set(false);
        ts.noMissionLive006d2910.set(true);
        ts.flagsByte24.set(EngineState::tutorialStateKey(1), 0u);

        ok(host, gp, "tutorial_start('autosave', 0, true, false, false)"); // e6691f1: table default 2 (index 1)
        CHECK(ts.entries.at(1).argument5GatedValue == 2);
        ts.entries.clear(); es.tutorialState().set(EngineState::tutorialStateKey(1), 0);
        ts.defaultParam20.set(EngineState::tutorialStateKey(1), 42);
        ok(host, gp, "tutorial_start('autosave', 0, true, false, false)");
        CHECK(ts.entries.at(1).argument5GatedValue == 42);
    }
    {
        // Rich-widget branch (step 11's "if"), and step 10's own second
        // disjunct (B4 true and state == 4) letting a repeat dispatch
        // through.
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        EngineState::TutorialStart& ts = es.tutorialStart();
        ts.suppressionByte0151d5a6.set(false);
        // Both dispatcher-step globals pre-seeded (see first test block above).
        ts.mode014f3d34.set(false);
        ts.noMissionLive006d2910.set(true);
        ts.flagsByte24.set(EngineState::tutorialStateKey(2), 0x04u); // rich-widget bit

        ok(host, gp, "tutorial_start('notoriety_gang', 0, true, false, false)");
        CHECK(es.tutorialState().get("2") == 4);
        CHECK(ts.richWidgetDispatchCount.at(2) == 1);

        ok(host, gp, "tutorial_start('notoriety_gang', 0, false, true, false)"); // B4=true lets step 10 through again at state 4
        CHECK(ts.richWidgetDispatchCount.at(2) == 2);
    }
    {
        // Index >= 189 bypasses the F/state-0 gate entirely (step 8/10) -
        // these entries start in state 1 already (applySpecInitialState).
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        EngineState::TutorialStart& ts = es.tutorialStart();
        ts.suppressionByte0151d5a6.set(false);
        // Both dispatcher-step globals pre-seeded (see first test block above).
        ts.mode014f3d34.set(false);
        ts.noMissionLive006d2910.set(true);
        ts.flagsByte24.set(EngineState::tutorialStateKey(189), 0u);

        CHECK(es.tutorialState().get("189") == 1); // CONFIRMED initial state for index >= 189
        ok(host, gp, "tutorial_start('dlc1_act_genki_escort_into', 3, false, false, true)"); // F=false - irrelevant for A>=189
        CHECK(es.tutorialState().get("189") == 2);
        CHECK(ts.entries.at(189).argument2Value == 3);
    }
    {
        // Step 4: a co-op CLIENT (session present, not host) is stopped
        // outright when the descriptor's own host-only bit (0x10) is set,
        // for an index < 189.
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        EngineState::TutorialStart& ts = es.tutorialStart();
        ts.suppressionByte0151d5a6.set(false);
        // Both dispatcher-step globals pre-seeded (see first test block above).
        ts.mode014f3d34.set(false);
        ts.noMissionLive006d2910.set(true);
        ts.flagsByte24.set(EngineState::tutorialStateKey(3), 0x10u); // host-only bit
        es.coopSession().present.set(true);
        es.coopSession().localIsHost.set(false); // a client

        ok(host, gp, "tutorial_start('notoriety_police', 0, true, false, true)");
        CHECK(es.tutorialState().get("3") == 0); // unchanged - stopped at step 4, never reached 9/10/11
        CHECK(ts.entries.count(3) == 0);
    }
    {
        // Step 12: a co-op HOST always replicates (P4 == 1 is always true
        // from Lua, so the descriptor's own bit 0x10 does not matter
        // here).
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        EngineState::TutorialStart& ts = es.tutorialStart();
        ts.suppressionByte0151d5a6.set(false);
        // Both dispatcher-step globals pre-seeded (see first test block above).
        ts.mode014f3d34.set(false);
        ts.noMissionLive006d2910.set(true);
        ts.flagsByte24.set(EngineState::tutorialStateKey(4), 0u);
        es.coopSession().present.set(true);
        es.coopSession().localIsHost.set(true); // the host

        ok(host, gp, "tutorial_start('notoriety_forgive', 0, true, false, true)");
        CHECK(ts.replicateCallCount == 1);
    }
    {
        // Step 5 (Sec6.19, 2026-10-10 Team A spec sync): 0x00706ab0 is a
        // top-of-mode-stack accessor with a CONFIRMED spawn default of 0
        // (mode stack empty on a fresh game) - EngineState::EngineState()
        // now pre-seeds gate5Value00706ab0 to 0, so a fresh host reaches
        // past step 5 with NO explicit .set() call here, unlike every
        // other still-OPEN dispatcher global this file sets by hand above.
        Host host(batchFixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        EngineState::TutorialStart& ts = es.tutorialStart();
        ts.suppressionByte0151d5a6.set(false);
        ts.globalMode029443bc.set(0);
        ts.mode014f3d34.set(false);
        ts.noMissionLive006d2910.set(true);
        ts.flagsByte24.set(EngineState::tutorialStateKey(5), 0u);

        CHECK(ts.gate5Value00706ab0.known()); // pre-seeded, not OPEN
        CHECK(ts.gate5Value00706ab0.get() == 0);
        ok(host, gp, "tutorial_start('crib_receive', 0, true, false, true)"); // reaches past step 5 with no explicit set()
        CHECK(es.tutorialState().get("5") == 2); // queued - step 5 did not stop it

        // The override path (mode 6 = key-bindings config screen active)
        // still stops step 5 exactly as before this pre-seed, and
        // .forget() reopens the OPEN path.
        ts.gate5Value00706ab0.set(6);
        ok(host, gp, "tutorial_start('crib_customize', 0, true, false, true)");
        CHECK(es.tutorialState().get("6") == 0); // unchanged - stopped at step 5

        ts.gate5Value00706ab0.forget();
        fails(host, gp, "tutorial_start('crib_garage', 0, true, false, true)", "0x00706ab0");
    }

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_b31_continuous_spawn_vehicle_enter_tutorial_test: all checks passed\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_b31_continuous_spawn_vehicle_enter_tutorial_test: " << g_failures << " check(s) failed\n";
    return 1;
}
