// Synthetic sr3luahost tests: test_block_18 .. test_block_22 (split out of synthetic_luahost_test.cpp, bodies verbatim).
#include "synthetic_luahost/common.h"

    // --- Player gender initial state (2026-10-02, spec-lua-api-behaviour.md
    // Sec16.8/Sec16.8a, spec-save-format.md Sec10.4, spec-tables-
    // customization.md Sec16.2): the raw, un-initialised engine never
    // observes the engine's own fresh-object sentinel (+0xa41 := 3) -
    // characterGender() stays fully OPEN for every name, "#PLAYER1#"
    // included, until applySpecInitialState() runs. Once it does, the
    // fresh-game default-preset path (this host never loads a real save -
    // tools/lua_host_run.cpp has no save-file reference anywhere) resolves
    // "#PLAYER1#" to the shipped player_presets.xtbl Default row
    // ("male_white", Gender=0) - the mechanism that resolves mission m21's
    // "character +0xa41 gender byte (1=female, else male)['#PLAYER1#'] is
    // OPEN" blocker.
void test_block_18() {
        sr3luahost::EngineState raw; // no applySpecInitialState: the sentinel is never directly observable
        CHECK(!raw.characterGender().known("#PLAYER1#"));
        bool threw = false;
        try {
            (void)raw.characterGender().get("#PLAYER1#");
        } catch (const sr3luahost::OpenStateError& e) {
            threw = true;
            CHECK(std::string(e.what()).find("gender byte (1=female, else male)['#PLAYER1#']") != std::string::npos);
        }
        CHECK(threw);

        sr3luahost::EngineState es;
        sr3luahost::applySpecInitialState(es);
        CHECK(es.characterGender().known("#PLAYER1#"));
        CHECK(es.characterGender().get("#PLAYER1#") == 0); // male, Default preset row
        // "#PLAYER2#" resolves by name (Sec29.4) but no co-op session is
        // active at start (item 1), so no second player object exists to
        // even hold a sentinel pair - its gender correctly stays OPEN.
        CHECK(!es.characterGender().known("#PLAYER2#"));
        // Race (+0xa40) has no modelled reader anywhere in this codebase
        // yet, so it is intentionally not set here - nothing to query means
        // nothing to leave correctly-OPEN or wrongly-guessed.

        Host host({});
        CHECK(host.engineState().characterGender().get("#PLAYER1#") == 0);
    }

    // --- Game clock initial state + per-frame advance (2026-10-03,
    // spec-lua-api-behaviour.md Sec48, corrects Sec32.1): the raw engine
    // never observes the clock's hour/minute bytes - OPEN until set, same
    // as every other OpenValue. applySpecInitialState() now sets them to
    // 10:00:00 (the CONFIRMED single-player new-game value, Sec48.2) -
    // this is the actual mechanism that closes the dlc3_m01/m19/etc.
    // blocker, since set_time_of_day's own forward-delta arithmetic reads
    // the current hour/minute before anything else.
void test_block_19() {
        sr3luahost::EngineState raw;
        CHECK(!raw.gameClock().hour.known() && !raw.gameClock().minute.known());

        sr3luahost::EngineState es;
        sr3luahost::applySpecInitialState(es);
        CHECK(es.gameClock().hour.get() == 10 && es.gameClock().minute.get() == 0);

        Host host({});
        CHECK(host.engineState().gameClock().hour.get() == 10);
        CHECK(host.engineState().gameClock().minute.get() == 0);

        // gameClockAdvanceFrame (Sec48.3, CONFIRMED mechanism): 40x real
        // time. 60 real seconds -> 2400 game seconds = 40 game minutes:
        // 10:00:00 -> 10:40:00.
        es.gameClockAdvanceFrame(60.0);
        CHECK(es.gameClock().hour.get() == 10 && es.gameClock().minute.get() == 40);
        // Accumulates across multiple calls (this host's own per-tick
        // cadence, tools/lua_host_run.cpp): 3 more real minutes (180s) ->
        // 7200 game seconds = 2 game hours: 10:40 -> 12:40.
        for (int i = 0; i < 3; ++i) es.gameClockAdvanceFrame(60.0);
        CHECK(es.gameClock().hour.get() == 12 && es.gameClock().minute.get() == 40);
        // Day wrap (STATED SIMPLIFICATION, GameClock's own doc comment):
        // 12:40 + 1200 real seconds (48000 game seconds = 13h20m) wraps
        // past 24:00 within the same tracked day: 12:40 + 13:20 = 26:00 ->
        // 02:00 the "same" modelled day (date fields not advanced).
        es.gameClockAdvanceFrame(1200.0);
        CHECK(es.gameClock().hour.get() == 2 && es.gameClock().minute.get() == 0);

        // set_time_of_day (Sec48.4): forgets hour/minute rather than
        // fabricating the post-snap value - advancing after that is
        // correctly a no-op (no known base to add a delta to). Called
        // directly (the Lua stub itself is exercised end-to-end in
        // synthetic_luahost_batch31_32_test.cpp) since this host wasn't
        // constructed with set_time_of_day in its registration list.
        host.engineState().setTimeOfDay(5, 0);
        CHECK(!host.engineState().gameClock().hour.known());
        double before = host.engineState().gameClock().frameAdvanceSecondsAccumulated;
        host.engineState().gameClockAdvanceFrame(60.0);
        CHECK(!host.engineState().gameClock().hour.known()); // still unknown, not fabricated
        CHECK(host.engineState().gameClock().frameAdvanceSecondsAccumulated == before + 60.0); // still tracked
    }

    // --- Batch 2026-10-02: spec-lua-bindings.md Sec18-Sec21 ("Vint UI
    // API"), 8 names - vint_object_first_child/vint_object_clone (Sec18),
    // vint_get_time_index/vint_dataitem_get (Sec19), vint_set_property/
    // vint_get_property (Sec20), vint_dataresponder_finished/
    // vint_internal_dataresponder_request (Sec21). Same "exercised through
    // the real Lua-visible global, a genuine round-trip through the real
    // Lua 5.1 C API" convention as every other spec-confirmed test above.
void test_block_20() {
        Host host(specConfirmedFixtureNames());
        lua_State* ui = host.uiState();
        lua_State* gp = host.gameplayState();
        sr3luahost::EngineState& es = host.engineState();
        auto refusesOpen = [&](lua_State* st, const std::string& chunk, const char* needle) {
            auto r = host.runChunk(st, chunk, "vint1821_open.lua");
            CHECK(r.loadOk && !r.pcallOk);
            CHECK(r.pcallError.find("is OPEN") != std::string::npos);
            CHECK(r.pcallError.find(needle) != std::string::npos);
        };
        auto ok = [&](lua_State* st, const std::string& chunk) {
            auto r = host.runChunk(st, chunk, "vint1821.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };
        auto globalNumber = [&](lua_State* st, const char* name) -> double {
            lua_getglobal(st, name);
            double v = lua_tonumber(st, -1);
            lua_pop(st, 1);
            return v;
        };

        // All 8 are `ui`-tagged (CONFIRMED, tools/lua_all_registered_1490_
        // tagged.txt) - nil in gameplay.
        ok(gp,
           "assert(vint_object_first_child == nil and vint_object_clone == nil and "
           "vint_get_time_index == nil and vint_dataitem_get == nil and "
           "vint_set_property == nil and vint_get_property == nil and "
           "vint_dataresponder_finished == nil and vint_internal_dataresponder_request == nil)");

        // 1. vint_object_first_child (Sec18.1): CONFIRMED - zero Lua values
        // (not even 0.0) on either a bad handle or a resolved object with
        // no first child; exactly one number (the child's handle) when a
        // real first child exists.
        {
            ok(ui, "assert(select('#', vint_object_first_child(999999)) == 0)"); // bad handle
            uint32_t root = es.registerVdoObjectForTesting("root", 0, 0);
            ok(ui, "assert(select('#', vint_object_first_child(" + std::to_string(root) + ")) == 0)"); // no child yet
            uint32_t child = es.registerVdoObjectForTesting("child", root, 0);
            es.setVdoObjectFirstChildForTesting(root, child);
            ok(ui, "assert(vint_object_first_child(" + std::to_string(root) + ") == " + std::to_string(child) + ")");
        }

        // 2. vint_object_clone (Sec18.2): CONFIRMED - always exactly 1
        // number, 0.0 on every failure path (no current document - OPEN;
        // an unresolvable orig_handle); the new clone's real handle on
        // success, scoped to the resolved parent (explicit arg, or the
        // original's own parent as fallback - including a given-but-
        // unresolvable parent handle) and the resolved CURRENT document
        // (not the original's own document).
        {
            refusesOpen(ui, "vint_object_clone(1)", "current default vint document"); // OPEN: no current document yet
            es.currentDefaultDocHandle().set(42);
            ok(ui, "assert(vint_object_clone(999999) == 0.0)"); // bad orig_handle -> 0.0, logged (not Lua-visible), not 0 Lua values

            uint32_t parentA = es.registerVdoObjectForTesting("parentA", 0, 7);
            uint32_t origWithParent = es.registerVdoObjectForTesting("orig_with_parent", parentA, 7);

            // No parent arg -> falls back to the ORIGINAL's own parent
            // (parentA); the clone is scoped to the CURRENT document (42),
            // not the original's own document (7).
            ok(ui, "CLONE1 = vint_object_clone(" + std::to_string(origWithParent) + ")");
            uint32_t clone1 = static_cast<uint32_t>(globalNumber(ui, "CLONE1"));
            CHECK(clone1 != 0 && clone1 != origWithParent);
            const auto* c1obj = es.vdoObjectForTesting(clone1);
            CHECK(c1obj != nullptr);
            if (c1obj) CHECK(c1obj->parentHandle == parentA && c1obj->docHandle == 42);

            // An explicit, resolvable parent arg overrides the fallback.
            uint32_t parentB = es.registerVdoObjectForTesting("parentB", 0, 7);
            ok(ui, "CLONE2 = vint_object_clone(" + std::to_string(origWithParent) + ", " + std::to_string(parentB) + ")");
            uint32_t clone2 = static_cast<uint32_t>(globalNumber(ui, "CLONE2"));
            CHECK(clone2 != 0 && clone2 != clone1);
            const auto* c2obj = es.vdoObjectForTesting(clone2);
            CHECK(c2obj != nullptr);
            if (c2obj) CHECK(c2obj->parentHandle == parentB && c2obj->docHandle == 42);

            // A given-but-unresolvable parent handle ALSO falls back to the
            // original's own parent (CONFIRMED: "falls back ... if that
            // resolution yields nothing").
            ok(ui, "CLONE3 = vint_object_clone(" + std::to_string(origWithParent) + ", 888888)");
            uint32_t clone3 = static_cast<uint32_t>(globalNumber(ui, "CLONE3"));
            const auto* c3obj = es.vdoObjectForTesting(clone3);
            CHECK(c3obj != nullptr);
            if (c3obj) CHECK(c3obj->parentHandle == parentA);
        }

        // 3. vint_get_time_index (Sec19.1): CONFIRMED - absent, nil, OR the
        // literal number 0 all take the SAME current-document fallback
        // path (a genuine difference from every other optional numeric-
        // handle argument elsewhere in this document). This host cannot
        // separately reach the real "resolution failed" zero-push path
        // (see EngineState::vintGetTimeIndex's own doc comment), so every
        // call either returns a real, test-set value or refuses as OPEN.
        {
            refusesOpen(ui, "vint_get_time_index(5)", "vint document time index"); // explicit nonzero doc, never set
            es.vintTimeIndexByDoc().set("5", 12.5);
            ok(ui, "assert(vint_get_time_index(5) == 12.5)");

            // currentDefaultDocHandle_ is 42 (set above, in test 2) - the
            // fallback path, not yet set in vintTimeIndexByDoc_.
            refusesOpen(ui, "vint_get_time_index()", "vint document time index");
            refusesOpen(ui, "vint_get_time_index(0)", "vint document time index"); // explicit 0 -> SAME fallback, not "explicit"
            es.vintTimeIndexByDoc().set("42", 99.0);
            ok(ui, "assert(vint_get_time_index() == 99.0)");
            ok(ui, "assert(vint_get_time_index(0) == 99.0)");
            ok(ui, "assert(vint_get_time_index(5) == 12.5)"); // explicit nonzero stays independent of the fallback
        }

        // 4. vint_dataitem_get (Sec19.2): CONFIRMED - zero Lua values on a
        // bad handle; every populated field of the resolved data item, in
        // order, otherwise; bounded at the real 32-slot cap.
        {
            ok(ui, "assert(select('#', vint_dataitem_get(999999)) == 0)"); // bad handle

            VintTaggedValue n1;
            n1.kind = VintTaggedValue::Kind::Number;
            n1.number = 3.5;
            VintTaggedValue b1;
            b1.kind = VintTaggedValue::Kind::Boolean;
            b1.boolean = true;
            VintTaggedValue s1;
            s1.kind = VintTaggedValue::Kind::String;
            s1.text = "hello";
            uint32_t di = es.registerVintDataItemForTesting({n1, b1, s1});
            ok(ui,
               "local a, b, c = vint_dataitem_get(" + std::to_string(di) + ")\n"
               "assert(a == 3.5 and b == true and c == 'hello')");

            std::vector<VintTaggedValue> many;
            for (int i = 0; i < 40; ++i) {
                VintTaggedValue v;
                v.kind = VintTaggedValue::Kind::Number;
                v.number = i;
                many.push_back(v);
            }
            uint32_t di2 = es.registerVintDataItemForTesting(many); // CONFIRMED cap: truncated to 32
            ok(ui, "assert(select('#', vint_dataitem_get(" + std::to_string(di2) + ")) == 32)");
        }

        // 5./6. vint_set_property / vint_get_property (Sec20): CONFIRMED -
        // vint_set_property always returns zero Lua values, every path
        // (including a missing/non-string property_name, and an
        // unresolvable handle - both silent no-ops); vint_get_property
        // returns zero Lua values (not even nil) on a bad handle or a
        // property-name miss, else echoes back exactly what was last set
        // (the real per-type dispatch/tween-redirect/callback-claim
        // mechanisms are NOT modeled - see EngineState::setVintProperty's
        // own doc comment).
        {
            uint32_t obj = es.registerVdoObjectForTesting("widget", 0, 0);
            ok(ui, "assert(select('#', vint_get_property(999999, 'scale')) == 0)"); // bad handle
            ok(ui, "assert(select('#', vint_get_property(" + std::to_string(obj) + ", 'scale')) == 0)"); // never set

            ok(ui, "assert(select('#', vint_set_property(" + std::to_string(obj) + ", 'is_paused', false)) == 0)");
            ok(ui, "local a = vint_get_property(" + std::to_string(obj) + ", 'is_paused'); assert(a == false)");

            // Multi-value (vec2-shaped) round-trip.
            ok(ui, "vint_set_property(" + std::to_string(obj) + ", 'scale', 1.5, 2.5)");
            ok(ui, "local x, y = vint_get_property(" + std::to_string(obj) + ", 'scale'); assert(x == 1.5 and y == 2.5)");

            // CONFIRMED: missing/non-string property_name -> silent no-op.
            ok(ui, "assert(select('#', vint_set_property(" + std::to_string(obj) + ")) == 0)"); // no property_name at all
            ok(ui, "vint_set_property(" + std::to_string(obj) + ", 123, 'x')"); // non-string name -> no-op
            ok(ui, "assert(select('#', vint_get_property(" + std::to_string(obj) + ", 123)) == 0)"); // confirms nothing stored

            // An unresolvable handle silently no-ops (CONFIRMED: "an
            // unresolvable handle simply fails to resolve later").
            ok(ui, "assert(select('#', vint_set_property(999999, 'scale', 1, 2)) == 0)");
            ok(ui, "assert(select('#', vint_get_property(999999, 'scale')) == 0)");

            // Case-insensitive property-name hash match (CONFIRMED, Sec9.2:
            // the engine's own lower-cased string hash).
            ok(ui, "vint_set_property(" + std::to_string(obj) + ", 'Visible', true)");
            ok(ui, "assert(vint_get_property(" + std::to_string(obj) + ", 'VISIBLE') == true)");
        }

        // 7./8. vint_dataresponder_finished / vint_internal_dataresponder_
        // request (Sec21): CONFIRMED - no record at all -> "finished"
        // (true), the real, honestly-flagged quirk; the request native
        // never creates a record and always returns zero Lua values, even
        // for a name with no record (a complete, silent no-op) or wrong
        // argument types.
        {
            ok(ui, "assert(vint_dataresponder_finished('never_registered') == true)");
            es.registerDataResponderForTesting("my_responder", false);
            ok(ui, "assert(vint_dataresponder_finished('my_responder') == false)");

            ok(ui, "assert(select('#', vint_internal_dataresponder_request('never_registered', 'cb', 10)) == 0)");
            ok(ui, "assert(vint_dataresponder_finished('never_registered') == true)"); // still no record - unaffected
            CHECK(es.dataResponderDispatchAttempts("never_registered") == 0);

            ok(ui, "vint_internal_dataresponder_request('my_responder', 'cb', 10)");
            CHECK(es.dataResponderDispatchAttempts("my_responder") == 1);
            ok(ui, "assert(vint_dataresponder_finished('my_responder') == false)"); // this native never flips it (CONFIRMED)

            // CONFIRMED: wrong/missing arg type -> silent no-op, no dispatch attempt.
            ok(ui, "vint_internal_dataresponder_request('my_responder', 123, 10)");    // callback_name not a string
            CHECK(es.dataResponderDispatchAttempts("my_responder") == 1);
            ok(ui, "vint_internal_dataresponder_request('my_responder', 'cb', 'ten')"); // max_records not a number
            CHECK(es.dataResponderDispatchAttempts("my_responder") == 1);
            ok(ui, "vint_internal_dataresponder_request('my_responder')");              // missing both
            CHECK(es.dataResponderDispatchAttempts("my_responder") == 1);

            es.registerDataResponderForTesting("my_responder", true);
            ok(ui, "assert(vint_dataresponder_finished('my_responder') == true)");
        }

        // Every one of the 8 calls above must fold into the SAME HitLog
        // ranking every other stub/hook in this project uses.
        for (const char* name : {"vint_object_first_child", "vint_object_clone", "vint_get_time_index",
                                  "vint_dataitem_get", "vint_set_property", "vint_get_property",
                                  "vint_dataresponder_finished", "vint_internal_dataresponder_request"}) {
            CHECK(host.hitLog().hits().count(name) == 1);
            CHECK(host.hitLog().hits().at(name).callCount >= 1);
        }
    }

    // --- Batch 2026-10-02: spec-lua-api-behaviour.md Sec33 ("ranking
    // tranche 05", 25 names) + the Sec14.1 0x0095da50 correction. Same
    // "exercised through the real Lua-visible global" convention as every
    // other spec-confirmed test above.
void test_block_21() {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        sr3luahost::EngineState& es = host.engineState();
        auto refusesOpen = [&](lua_State* st, const std::string& chunk, const char* needle) {
            auto r = host.runChunk(st, chunk, "sec33_open.lua");
            CHECK(r.loadOk && !r.pcallOk);
            CHECK(r.pcallError.find("is OPEN") != std::string::npos);
            CHECK(r.pcallError.find(needle) != std::string::npos);
        };
        auto ok = [&](lua_State* st, const std::string& chunk) {
            auto r = host.runChunk(st, chunk, "sec33.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };

        // All 25 are `gameplay`-tagged - nil in ui.
        {
            auto r = host.runChunk(host.uiState(),
                "assert(vehicle_is_helicopter == nil and ai_clear_priority_target == nil and "
                "action_play_synced_do == nil and waiting_for_player_dialog == nil)",
                "sec33_ui_nil.lua");
            CHECK(r.loadOk && r.pcallOk);
        }

        // vehicle_is_helicopter (Sec33.1): class 3 -> true, 4 (VTOL) -> false, unresolved -> false.
        //
        // CONFIRMED CASE-BY-CASE (vehicle-class xtbl wiring task, 2026-10-10):
        // "heli1" here is a pure Lua-host-only test fixture with NO real
        // zone-spawn backing (no sr3zone::ObjectsSection is ever ingested in
        // this fixture) - and this one test body needs flyingType to visit
        // THREE distinct states in sequence (OPEN, class 3, class 4), which a
        // single real xtbl-sourced class value could never produce (the real,
        // general, data-driven path - EngineState::loadZoneObjectsSection(),
        // proven end-to-end by tests/synthetic_luahost_batch2728_test.cpp's
        // own m05_heli_pierce fixture - only ever assigns ONE fixed class per
        // vehicle instance). Manual .set() stays, legitimately, per this
        // task's own "confirm case-by-case" instruction.
        {
            refusesOpen(gp, "vehicle_is_helicopter('heli1')", "named-object resolution");
            es.objectResolves().set("heli1", true);
            refusesOpen(gp, "vehicle_is_helicopter('heli1')", "flying-type enum");
            es.getOrCreateVehicle("heli1").flyingType.set(3);
            ok(gp, "assert(vehicle_is_helicopter('heli1') == true)");
            es.getOrCreateVehicle("heli1").flyingType.set(4); // VTOL answers false here
            ok(gp, "assert(vehicle_is_helicopter('heli1') == false)");
            es.objectResolves().set("not_a_vehicle", false);
            ok(gp, "assert(vehicle_is_helicopter('not_a_vehicle') == false)");
        }

        // vehicle_is_vtol_hover / vehicle_is_vtol_jet (Sec33.1): the
        // CONFIRMED stack-discipline defect - a true result pushes 2
        // values (true, false); every other path pushes exactly 1 (false).
        {
            es.objectResolves().set("vtol1", true);
            auto& v = es.getOrCreateVehicle("vtol1");
            v.flyingType.set(4);
            v.vtolState.set(0); // hover
            ok(gp, "local a, b = vehicle_is_vtol_hover('vtol1'); assert(a == true and b == false)");
            ok(gp, "assert(select('#', vehicle_is_vtol_hover('vtol1')) == 2)");
            ok(gp, "local a, b = vehicle_is_vtol_jet('vtol1'); assert(a == false and b == nil and select('#', vehicle_is_vtol_jet('vtol1')) == 1)");
            v.vtolState.set(2); // jet
            ok(gp, "local a, b = vehicle_is_vtol_jet('vtol1'); assert(a == true and b == false)");
            v.flyingType.set(3); // not a VTOL at all -> single false either way
            ok(gp, "assert(select('#', vehicle_is_vtol_hover('vtol1')) == 1 and vehicle_is_vtol_hover('vtol1') == false)");
        }

        // vehicle_is_ready (Sec33.1): all three gates.
        {
            es.objectResolves().set("car1", true);
            auto& v = es.getOrCreateVehicle("car1");
            refusesOpen(gp, "vehicle_is_ready('car1')", "not-ready flag (+0x3b");
            v.notReadyBit3b.set(false);
            v.notReadyBit3a.set(false);
            v.fullySetUp.set(true);
            ok(gp, "assert(vehicle_is_ready('car1') == true)");
            v.notReadyBit3b.set(true);
            ok(gp, "assert(vehicle_is_ready('car1') == false)");
            v.notReadyBit3b.set(false);
            v.fullySetUp.set(false);
            ok(gp, "assert(vehicle_is_ready('car1') == false)");
        }

        // vehicle_never_flatten_tires / vehicle_set_weapons_disarmed /
        // vehicle_set_no_chase (Sec33.1): double-gate bit setters, always
        // applied locally (this project's own convention).
        {
            ok(gp, "vehicle_never_flatten_tires('tank1')"); // default true
            CHECK(es.getOrCreateVehicle("tank1").forceFlags1d7b.get(0x1) == 0x1);
            ok(gp, "vehicle_never_flatten_tires('tank1', false)");
            CHECK(es.getOrCreateVehicle("tank1").forceFlags1d7b.get(0x1) == 0x0);

            ok(gp, "vehicle_set_weapons_disarmed('tank1', true)");
            CHECK(es.getOrCreateVehicle("tank1").forceFlags1d7e.get(0x4) == 0x4);
            ok(gp, "vehicle_set_weapons_disarmed('tank1')"); // absent -> false
            CHECK(es.getOrCreateVehicle("tank1").forceFlags1d7e.get(0x4) == 0x0);

            ok(gp, "vehicle_set_no_chase('tank1', true)");
            CHECK(es.getOrCreateVehicle("tank1").vehicleAiForceFlags.get(0x1) == 0x1);
        }

        // vehicle_set_kneecappers (Sec33.1): deferred-vs-enabled per the
        // "fully set up" gate.
        {
            auto& v = es.getOrCreateVehicle("kneecar1");
            v.fullySetUp.set(false);
            ok(gp, "vehicle_set_kneecappers('kneecar1')"); // default true, not set up -> deferred only
            CHECK(v.kneecappersDeferred == true && v.kneecappersEnabled == false);
            v.fullySetUp.set(true);
            ok(gp, "vehicle_set_kneecappers('kneecar1')");
            CHECK(v.kneecappersEnabled == true);
            ok(gp, "vehicle_set_kneecappers('kneecar1', false)");
            CHECK(v.kneecappersEnabled == false && v.kneecappersDeferred == false);
        }

        // vehicle_set_sirenlights (Sec33.1): class 3 -> both bits; class 12
        // -> siren only; an unrecognized class -> OPEN (no per-class table).
        {
            es.objectResolves().set("cruiser1", true);
            auto& v = es.getOrCreateVehicle("cruiser1");
            // ce4556f Sec38.1: +0xbd0 defaults to 1 for a live vehicle, so the
            // first OPEN reached is the class enum.
            CHECK(v.bodyActiveOrDefault());
            v.bodyActive.set(true);
            refusesOpen(gp, "vehicle_set_sirenlights('cruiser1', true)", "class enum");
            v.vehicleClass.set(3);
            ok(gp, "vehicle_set_sirenlights('cruiser1', true)");
            CHECK(v.forceFlags1d7c.get(0x18) == 0x18);
            ok(gp, "vehicle_set_sirenlights('cruiser1', false)");
            CHECK(v.forceFlags1d7c.get(0x18) == 0x0);
            v.vehicleClass.set(12);
            ok(gp, "vehicle_set_sirenlights('cruiser1', true)");
            CHECK(v.forceFlags1d7c.get(0x8) == 0x8);
            v.vehicleClass.set(99);
            refusesOpen(gp, "vehicle_set_sirenlights('cruiser1', true)", "light-bar option"); // retuned 2026-10-14 (spec sync bfe0542, Sec33.1 blocker m10): the class enum is answered, the per-instance customisation record stays OPEN
        }

        // vehicle_set_ambient (Sec33.1): mode/sub-mode and the unconverted speed cap.
        {
            ok(gp, "vehicle_set_ambient('ambient1')"); // default -1.0 -> no speed cap
            auto& v = es.getOrCreateVehicle("ambient1");
            CHECK(v.aiMode == 6 && v.aiSubMode == 0x11 && v.ambientFlags.get(0x1) == 0x1);
            CHECK(v.hasSpeedCap == false);
            ok(gp, "vehicle_set_ambient('ambient1', 2500.0)");
            CHECK(v.hasSpeedCap == true && v.speedCapRaw == 1000.0); // clamped, UNCONVERTED (no mph factor)
            ok(gp, "vehicle_set_ambient('ambient1', 30.0)");
            CHECK(v.speedCapRaw == 30.0);
        }

        // vehicle_spotlight_is_target_spotted (Sec33.1): side effect
        // happens, actual geometry test is OPEN.
        {
            es.objectResolves().set("spotcar1", true);
            es.objectResolves().set("spottarget1", true);
            refusesOpen(gp, "vehicle_spotlight_is_target_spotted('spotcar1', 'spottarget1')", "raycast test");
            CHECK(es.getOrCreateVehicle("spotcar1").spotlightTargetName == "spottarget1");
            es.objectResolves().set("spottarget2", false);
            ok(gp, "assert(vehicle_spotlight_is_target_spotted('spotcar1', 'spottarget2') == false)");
        }

        // ai_clear_priority_target (Sec33.2): single-gate null-clear.
        {
            es.objectResolves().set("npc_rush", true);
            es.getOrCreateCharacter("npc_rush").forcedTargetHandle = "some_prior_target";
            ok(gp, "ai_clear_priority_target('npc_rush')");
            CHECK(es.getOrCreateCharacter("npc_rush").forcedTargetHandle.empty());
        }

        // ai_set_in_scripted_cover (Sec33.2): false only if unresolved; disabling queues action 13.
        {
            es.objectResolves().set("cover_unresolved", false);
            ok(gp, "assert(ai_set_in_scripted_cover('cover_unresolved', true) == false)");
            es.objectResolves().set("npc_cover", true);
            ok(gp, "assert(ai_set_in_scripted_cover('npc_cover', true) == true)");
            CHECK(es.getOrCreateCharacter("npc_cover").inScriptedCover.get() == true);
            ok(gp, "assert(ai_set_in_scripted_cover('npc_cover', false) == true)");
            CHECK(es.getOrCreateCharacter("npc_cover").inScriptedCover.get() == false);
            CHECK(es.getOrCreateCharacter("npc_cover").scriptedAction.get() == 13);
        }

        // ai_pay_attention_to_position (Sec33.2): mode 7/3 mapping.
        {
            es.objectResolves().set("npc_attend", true);
            es.objectResolves().set("attend_obj", true);
            ok(gp, "ai_pay_attention_to_position('npc_attend', 'attend_obj')"); // default true -> mode 7
            auto& c = es.getOrCreateCharacter("npc_attend");
            CHECK(c.attentionMode == 7 && c.attentionSourceObjectName == "attend_obj");
            ok(gp, "ai_pay_attention_to_position('npc_attend', 'attend_obj', false)");
            CHECK(c.attentionMode == 3);
        }

        // ai_do_scripted_rush (Sec33.2): with-target always succeeds;
        // without a target, OPEN unless already rushing (action 22).
        {
            es.objectResolves().set("npc_rush2", true);
            es.objectResolves().set("rush_target", true);
            ok(gp, "assert(ai_do_scripted_rush('npc_rush2', 'rush_target') == true)");
            CHECK(es.getOrCreateCharacter("npc_rush2").scriptedAction.get() == 22);
            CHECK(es.getOrCreateCharacter("npc_rush2").scriptedRushTargetName == "rush_target");

            es.objectResolves().set("npc_rush3", true);
            refusesOpen(gp, "ai_do_scripted_rush('npc_rush3')", "reachability/distance/melee checks");
            es.getOrCreateCharacter("npc_rush3").scriptedAction.set(22); // already rushing -> accepted immediately
            ok(gp, "assert(ai_do_scripted_rush('npc_rush3') == true)");
        }

        // action_play_synced_do (Sec33.3) + the Sec14.1 0x0095da50
        // correction: a dead actor 1 -> -1; else 1, regardless of whether
        // the synced-action name resolves to a known index (this project's
        // never-populated table honestly answers "not found" either way).
        {
            es.objectResolves().set("performer1", true);
            es.objectResolves().set("partner1", true);
            ok(gp, "assert(action_play_synced_do('performer1', 'partner1', 'wave_action') == 1)");
            CHECK(es.lookupSyncedActionIndex("wave_action") == EngineState::kSyncedActionNotFound);
            es.registerSyncedActionForTesting("wave_action", 7);
            CHECK(es.lookupSyncedActionIndex("wave_action") == 7); // found path also plays successfully (not cross-referenced further, see doc comment)
            ok(gp, "assert(action_play_synced_do('performer1', 'partner1', 'wave_action') == 1)");
            es.getOrCreateCharacter("performer1").isDeadHighConfidence = true;
            ok(gp, "assert(action_play_synced_do('performer1', 'partner1', 'wave_action') == -1)");
        }

        // action_play_directional_stumble_do (Sec33.3): push-count
        // semantics - clean success, dead character (still plays), and the
        // "would crash" unresolved paths treated as safe failures instead.
        {
            es.objectResolves().set("stumbler1", true);
            es.objectResolves().set("stumble_ref1", true);
            ok(gp, "assert(select('#', action_play_directional_stumble_do('stumbler1', 'stumble_ref1')) == 1)");
            ok(gp, "assert(action_play_directional_stumble_do('stumbler1', 'stumble_ref1') == 1.0)");

            es.getOrCreateCharacter("stumbler1").isDeadHighConfidence = true;
            ok(gp, "local a, b = action_play_directional_stumble_do('stumbler1', 'stumble_ref1'); "
                   "assert(a == -1.0 and b == 1.0)");

            // Fresh, non-dead character name here - stumbler1 above was set
            // dead and stays dead, which would add its own extra -1.0 push
            // and change the expected values below.
            es.objectResolves().set("stumbler_fresh", true);
            es.objectResolves().set("stumble_unresolved", false);
            ok(gp, "local a, b = action_play_directional_stumble_do('stumbler_fresh', 'stumble_unresolved'); "
                   "assert(a == -1.0 and b == 1.0)"); // object unresolved -> one -1.0, then the unconditional final 1.0

            es.objectResolves().set("stumbler_unresolved", false);
            ok(gp, "assert(select('#', action_play_directional_stumble_do('stumbler_unresolved', 'stumble_ref1')) == 2)");
        }

        // action_sequence_end (Sec33.3): global teardown, host-only broadcast.
        {
            auto& seq = es.actionSequence();
            seq.active = true;
            seq.localScriptedCameraTarget = "cam1";
            int before = seq.hostBroadcastCount;
            ok(gp, "action_sequence_end()");
            CHECK(seq.active == false && seq.localScriptedCameraTarget.empty());
            CHECK(seq.trackedHandleReleaseCount == 2);
            CHECK(seq.hostBroadcastCount == before); // single player: coopLocalIsHost() == false (no session)
        }

        // audio_set_listener_override / audio_clear_listener_override (Sec33.4).
        {
            es.objectResolves().set("listener_obj", true);
            ok(gp, "audio_set_listener_override('listener_obj')");
            CHECK(es.audioListenerOverrideTarget() == "listener_obj");
            ok(gp, "audio_clear_listener_override()");
            CHECK(es.audioListenerOverrideTarget().empty());
        }

        // audio_play_for_navpoint (Sec33.4): id on resolve, 0 on failure, arg 3 inert.
        {
            es.objectResolves().set("navpoint_obj", true);
            ok(gp, "NAV_ID = audio_play_for_navpoint('some_sound', 'navpoint_obj', 'ignored_arg')");
            double navId = 0.0;
            { lua_getglobal(gp, "NAV_ID"); navId = lua_tonumber(gp, -1); lua_pop(gp, 1); }
            CHECK(navId > 0.0);
            es.objectResolves().set("navpoint_unresolved", false);
            ok(gp, "assert(audio_play_for_navpoint('some_sound', 'navpoint_unresolved') == 0)");
        }

        // audio_any_conversation_playing (Sec33.4): settles the "mission_conv" identity question.
        {
            ok(gp, "assert(audio_any_conversation_playing() == false)"); // e6691f1: slot starts 0xff
            es.conversationChannelActive().set("mission_conv", true);
            ok(gp, "assert(audio_any_conversation_playing() == true)");
            es.conversationChannelActive().set("mission_conv", false);
            ok(gp, "assert(audio_any_conversation_playing() == false)");
        }

        // boss_battle_matt_begin (Sec33.5): full reset + deadline arm +
        // the "Matt" character bit, gated by resolve-before-write.
        {
            auto& matt = es.bossBattleMatt();
            matt.cheatSlot.set(42);
            matt.lastId = 99;
            matt.active = false;
            refusesOpen(gp, "boss_battle_matt_begin()", "named-object resolution");
            CHECK(matt.cheatSlot.get() == 42); // refused BEFORE any write - no partial update
            es.objectResolves().set("Matt", true);
            ok(gp, "boss_battle_matt_begin()"); // default true -> 5000ms deadline
            CHECK(matt.cheatSlot.get() == -1 && matt.lastId == -1 && matt.active == true);
            CHECK(matt.deadlineMs == 5000);
            CHECK(es.getOrCreateCharacter("Matt").flagsEc.get(0x4000) == 0x4000);
            ok(gp, "boss_battle_matt_begin(false)");
            CHECK(matt.deadlineMs == 0);
        }

        // auto_pickup_disable (Sec33.5): plain session-wide off switch.
        {
            CHECK(es.allowWeaponAutoPickup() == true);
            ok(gp, "auto_pickup_disable()");
            CHECK(es.allowWeaponAutoPickup() == false);
        }

        // waiting_for_player_dialog (Sec33.5): reference-counted show/hide,
        // and the deliberately-reproduced "currently shown never clears" hazard.
        {
            auto& d = es.waitingForPlayerDialog();
            ok(gp, "waiting_for_player_dialog(false)"); // hide at 0 -> no-op
            CHECK(d.refCount == 0);
            ok(gp, "waiting_for_player_dialog(true)");
            CHECK(d.refCount == 1 && d.currentlyShown == true);
            ok(gp, "waiting_for_player_dialog(true)"); // nested show
            CHECK(d.refCount == 2);
            ok(gp, "waiting_for_player_dialog(false)");
            CHECK(d.refCount == 1 && d.currentlyShown == true); // not the final hide yet
            ok(gp, "waiting_for_player_dialog(false)"); // final hide
            CHECK(d.refCount == 0);
            CHECK(d.currentlyShown == true); // CONFIRMED hazard: never cleared by this path
            // A second show cycle: display branch is silently skipped (currentlyShown already true).
            int showsBefore = d.showBroadcastCount;
            ok(gp, "waiting_for_player_dialog(true)");
            CHECK(d.showBroadcastCount == showsBefore); // no new display attempt
            ok(gp, "waiting_for_player_dialog(false)");
        }

        // Every one of the 25 calls above must fold into the SAME HitLog
        // ranking every other stub/hook in this project uses.
        for (const char* name :
             {"vehicle_is_helicopter", "vehicle_is_vtol_hover", "vehicle_is_vtol_jet", "vehicle_is_ready",
              "vehicle_never_flatten_tires", "vehicle_set_weapons_disarmed", "vehicle_set_no_chase",
              "vehicle_set_kneecappers", "vehicle_set_sirenlights", "vehicle_set_ambient",
              "vehicle_spotlight_is_target_spotted", "ai_clear_priority_target", "ai_set_in_scripted_cover",
              "ai_pay_attention_to_position", "ai_do_scripted_rush", "action_play_synced_do",
              "action_play_directional_stumble_do", "action_sequence_end", "audio_set_listener_override",
              "audio_clear_listener_override", "audio_play_for_navpoint", "audio_any_conversation_playing",
              "boss_battle_matt_begin", "auto_pickup_disable", "waiting_for_player_dialog"}) {
            CHECK(host.hitLog().hits().count(name) == 1);
            CHECK(host.hitLog().hits().at(name).callCount >= 1);
        }
    }

    // --- Batch 2026-10-02: spec-lua-api-behaviour.md Sec35 (the
    // `teleport_coop` investigation) - teleport_check_done/
    // turn_to_check_done/move_to_check_done/vehicle_pathfind_check_done.
    // A REGRESSION GUARD: Sec15.17's original desk reading had this exact
    // done/pending polarity BACKWARDS (an earlier draft read "true
    // whenever any teleport is tracked"; the corrected, now-CONFIRMED
    // convention, re-derived from the raw instruction stream, is the
    // opposite emphasis - true for DONE or NO REQUEST, false only while
    // GENUINELY PENDING) - this block pins the corrected polarity
    // directly so that mistake can never silently return.
void test_block_22() {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        sr3luahost::EngineState& es = host.engineState();
        auto ok = [&](lua_State* st, const std::string& chunk) {
            auto r = host.runChunk(st, chunk, "teleport_coop.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };

        // All 4 are `gameplay`-tagged (CONFIRMED, tools/lua_all_registered_
        // 1490_tagged.txt) - nil in the UI state.
        ok(host.uiState(),
           "assert(teleport_check_done == nil and turn_to_check_done == nil and "
           "move_to_check_done == nil and vehicle_pathfind_check_done == nil)");

        // 1./2. teleport_check_done (Sec15.17) / turn_to_check_done: no
        // request tracked for this name at all -> true (CONFIRMED, Sec35.2:
        // "done OR no request" -> true) - this is also the REAL state
        // every genuine call reaches in this host (no in-scope native
        // allocates a request), and is exactly the fix that unsticks
        // `teleport_coop`'s own `repeat thread_yield() until
        // teleport_check_done(LOCAL_PLAYER)` poll (Sec35's own "why" note).
        ok(gp, "assert(teleport_check_done('#PLAYER1#') == true)");
        ok(gp, "assert(turn_to_check_done('#PLAYER1#') == true)");

        // A request registered PENDING -> false (the corrected polarity's
        // other half - must NOT be true while genuinely pending).
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindTeleport, false);
        ok(gp, "assert(teleport_check_done('#PLAYER1#') == false)");
        // A DIFFERENT name is unaffected (the name argument is genuinely
        // used as a per-request lookup key, not ignored).
        ok(gp, "assert(teleport_check_done('#PLAYER2#') == true)");
        // Once marked done, a read both answers true AND releases the
        // record (CONFIRMED, Sec35.2: a "done" read consumes it) - a
        // second read with no new request sees "no request" -> true, same
        // answer, for a different confirmed reason.
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindTeleport, true);
        ok(gp, "assert(teleport_check_done('#PLAYER1#') == true)");
        CHECK(es.scriptedRequestStatusCode("#PLAYER1#", EngineState::kScriptedRequestKindTeleport) == 2); // released already

        // turn_to_check_done consults a SEPARATE kind (0, not 4) - a
        // teleport-kind pending record for the same name must not leak
        // into the turn-to query.
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindTeleport, false);
        ok(gp, "assert(turn_to_check_done('#PLAYER1#') == true)"); // kind 0, no record -> true
        ok(gp, "assert(teleport_check_done('#PLAYER1#') == false)"); // kind 4, still pending

        // 3. move_to_check_done (Sec22.15): arg 2 (not arg 1) is the
        // character name this host looks up; same corrected polarity,
        // kind 2 (move-to/pathfind).
        ok(gp, "assert(move_to_check_done(1, '#PLAYER1#', 'anchor', 1, false, false, false, 0) == true)");
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindMoveOrPathfind, false);
        ok(gp, "assert(move_to_check_done(1, '#PLAYER1#', 'anchor', 1, false, false, false, 0) == false)");
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindMoveOrPathfind, true);
        ok(gp, "assert(move_to_check_done(1, '#PLAYER1#', 'anchor', 1, false, false, false, 0) == true)");

        // 4. vehicle_pathfind_check_done (Sec9.10): pushes a NUMBER, not a
        // boolean - 2 (not a boolean "true") when this host's own vehicle-
        // resolution gate is closed, CONFIRMED to be the SAME literal
        // fallback the real engine uses when the vehicle fails to resolve.
        ok(gp, "assert(vehicle_pathfind_check_done('some_vehicle') == 2)");
        // Opening the gate exposes the SAME shared-pool 0/1/2 polarity
        // teleport_check_done's own boolean is built on, kind 2 (shared
        // with move_to_check_done, Sec22.15's own "a THIRD confirmed use
        // of kind 2" text) - a DIFFERENT name from move_to_check_done's own
        // kind-2 record above, so the two cannot collide.
        es.setVehiclePathfindResolvableForTesting("some_vehicle", true);
        ok(gp, "assert(vehicle_pathfind_check_done('some_vehicle') == 2)"); // gate open, still no record -> 2 (no request)
        es.registerScriptedRequestForTesting("some_vehicle", EngineState::kScriptedRequestKindMoveOrPathfind, false);
        ok(gp, "assert(vehicle_pathfind_check_done('some_vehicle') == 0)"); // pending
        es.registerScriptedRequestForTesting("some_vehicle", EngineState::kScriptedRequestKindMoveOrPathfind, true);
        ok(gp, "assert(vehicle_pathfind_check_done('some_vehicle') == 1)"); // done

        for (const char* name :
             {"teleport_check_done", "turn_to_check_done", "move_to_check_done", "vehicle_pathfind_check_done"}) {
            CHECK(host.hitLog().hits().count(name) == 1);
            CHECK(host.hitLog().hits().at(name).callCount >= 1);
        }
    }

