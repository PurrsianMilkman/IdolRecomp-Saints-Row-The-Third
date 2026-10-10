// Synthetic test for spec-player-runtime.md Sec7.4a: the vehicle-enter
// eligibility gate 0x00b0ca60(character, forced, eligibility_bit), traced
// in full by Team A 2026-10-09 as a pure character-state check with no
// vehicle/physics argument at all, and implemented here as
// vehicleEnterEligibilityGate() in
// src/lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp.
//
// This file exercises each of the 9 refusal paths and the success tail
// INDIRECTLY, through the two Lua natives that actually call the gate
// (vehicle_enter_do's instant/named-seat paths - "forced"/"not forced"
// respectively - and vehicle_enter_guardian_angel, always "forced") - the
// gate itself is a plain C++ helper, not a separate Lua-registered native,
// so there is no direct way to call it from a Lua chunk; this matches the
// project's own "test through the registered native" convention for every
// other internal gate helper (e.g. notDeadForActionPlay(), forceFlagGatePasses).
//
// Each case uses a FRESH character name so getOrCreateCharacter()'s own
// construction-time seeding (stateEnum=0, lifeState=0, killFinalizedFlagE3=
// false, stateCodeByte501=0/Idle as of 2026-10-10's spec-lua-api-behaviour.md
// Sec22.3 sync) is the only thing already set unless this test sets more -
// exactly reproducing a freshly-spawned, healthy, non-busy character before this
// test narrows in on the ONE field each case means to exercise.
#include <iostream>
#include <string>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"

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
    auto r = host.runChunk(L, code, "vehicle_enter_eligibility_gate_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "vehicle_enter_eligibility_gate_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixture() {
    return {{"vehicle_enter_do", "gameplay"}, {"vehicle_enter_guardian_angel", "gameplay"}};
}

// A healthy, resolvable, freshly-spawned character and its vehicle, both
// present in objectResolves() - the minimum needed to clear vehicle_enter_do/
// vehicle_enter_guardian_angel's own resolve step before the gate ever runs.
CharacterState& freshChar(EngineState& es, const std::string& charName, const std::string& vehicleName) {
    es.objectResolves().set(charName, true);
    es.objectResolves().set(vehicleName, true);
    return es.getOrCreateCharacter(charName);
}

} // namespace

int main() {
    Host host(fixture());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ==================================================================
    // Refusal 1: already seated (+0x16d4 == 3). Named-seat path (not
    // instant -> forced = false): unconditional, so this alone refuses.
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg1_seated", "eg1_car");
        c.stateEnum.set(3);
        ok(host, gp, "assert(vehicle_enter_do('eg1_seated', 'eg1_car', false, nil, nil, nil, 'driver') == false)");
        CHECK(c.lastVehicleEnterRequest.has_value() && c.lastVehicleEnterRequest->accepted == false);
    }

    // ==================================================================
    // Refusal 2: already mid enter/exit of some vehicle (+0x16d4 in {1,2}).
    // Unconditional - exercised on the FORCED (instant) path too, to show
    // "forced" does not bypass this one.
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg2_entering", "eg2_car");
        c.stateEnum.set(1);
        ok(host, gp, "assert(vehicle_enter_do('eg2_entering', 'eg2_car', true) == false)"); // instant -> forced
        CharacterState& c2 = freshChar(es, "eg2_exiting", "eg2_car2");
        c2.stateEnum.set(2);
        ok(host, gp, "assert(vehicle_enter_do('eg2_exiting', 'eg2_car2', true) == false)");
    }

    // ==================================================================
    // Refusal 3: +0x16d8/+0x16dc busy/pending pair non-zero. Unconditional -
    // exercised on the FORCED (guardian_angel) path to show "forced" does
    // not bypass this one either.
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg3_busy", "eg3_car");
        c.vehicleEnterEligibilityBusyPendingNonZero = true;
        ok(host, gp, "assert(vehicle_enter_guardian_angel('eg3_busy', 'eg3_car') == false)");
    }

    // ==================================================================
    // Refusal 4: ragdolling (+0xcd2 not in {0,6}).
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg4_ragdoll", "eg4_car");
        c.ragdollStateEnumCd2.set(3); // any value outside {0,6}
        ok(host, gp, "assert(vehicle_enter_do('eg4_ragdoll', 'eg4_car', true) == false)");
    }

    // ==================================================================
    // Refusal 5: down or dead (+0xcc8 in {5,6}, or +0xe3 bit 0x01). Two
    // sub-cases, same refusal path, mirroring the sibling gate tests'
    // "both readers of the same two fields" convention (seatChar's own
    // lifeState/killFinalizedFlagE3 pair in the w21 test file).
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg5_down", "eg5_car");
        c.ragdollStateEnumCd2.set(0); // clears step 4 so step 5 is what's tested
        c.lifeState.set(6);           // "downed"
        ok(host, gp, "assert(vehicle_enter_do('eg5_down', 'eg5_car', true) == false)");

        CharacterState& c2 = freshChar(es, "eg5_killfinal", "eg5_car2");
        c2.ragdollStateEnumCd2.set(0);
        c2.killFinalizedFlagE3.set(true);
        ok(host, gp, "assert(vehicle_enter_do('eg5_killfinal', 'eg5_car2', true) == false)");
    }

    // ==================================================================
    // Refusal 6a: the generic interaction-lock helper (0x009562d0) refuses -
    // NOT forced only. Also shows FORCED bypasses this exact check: the
    // same unset OpenValue would otherwise throw, but the forced path never
    // reads it at all.
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg6a_locked", "eg6a_car");
        c.ragdollStateEnumCd2.set(0);
        c.genericInteractionLockGatePasses.set(false);
        ok(host, gp, "assert(vehicle_enter_do('eg6a_locked', 'eg6a_car', false, nil, nil, nil, 'driver') == false)");

        // Forced (instant) path: the SAME unset-by-default interaction lock
        // on a fresh character never throws, because forced skips step 6
        // entirely (and step 7/8 too - "forced -> success unconditionally"
        // never even calls playerClassChainResolves()) - would throw
        // OpenStateError on an unconfigured genericInteractionLockGatePasses
        // if step 6 ran unconditionally. The gate's own eligibility is only
        // HALF the submitter's overall result (Sec7.4 step 4's vehicle/seat
        // pick is still OPEN) - computeVehicleEnterSubmitAccepted() now
        // resolves an eligible-and-unset case to the CHOSEN default `true`
        // (2026-10-09 follow-up, see CharacterState::vehicleEnterSubmitAccepted's
        // own doc comment: leaving this as an unconditional throw meant a
        // real mission-drive run never got past it, measured directly on
        // m05/m07/m09) rather than throwing - so this now resolves straight
        // to true without a test .set() at all.
        CharacterState& c2 = freshChar(es, "eg6a_forced_skips", "eg6a_car2");
        c2.ragdollStateEnumCd2.set(0);
        ok(host, gp, "assert(vehicle_enter_do('eg6a_forced_skips', 'eg6a_car2', true) == true)");
        CHECK(!c2.vehicleEnterSubmitAccepted.known()); // CHOSEN default resolved it - no test override needed
    }

    // ==================================================================
    // Refusal 6b/6c: jump/dive (+0xccd == 4) and climb (+0xccd == 6) pose -
    // NOT forced only.
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg6b_jump", "eg6b_car");
        c.ragdollStateEnumCd2.set(0);
        c.genericInteractionLockGatePasses.set(true);
        c.movementStateByteCcd.set(4);
        ok(host, gp, "assert(vehicle_enter_do('eg6b_jump', 'eg6b_car', false, nil, nil, nil, 'driver') == false)");

        CharacterState& c2 = freshChar(es, "eg6c_climb", "eg6c_car2");
        c2.ragdollStateEnumCd2.set(0);
        c2.genericInteractionLockGatePasses.set(true);
        c2.movementStateByteCcd.set(6);
        ok(host, gp, "assert(vehicle_enter_do('eg6c_climb', 'eg6c_car2', false, nil, nil, nil, 'driver') == false)");
    }

    // ==================================================================
    // Step 7 bit clear -> success anyway, regardless of cant_enter_vehicle/
    // mode/sub-state (none of which are even touched - no throw even though
    // cantEnterVehicleFlag2ba/stateCodeByte501/aiSubStateByte500 stay OPEN/
    // default).
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg7_noblock", "eg7_car");
        c.ragdollStateEnumCd2.set(0);
        c.genericInteractionLockGatePasses.set(true);
        c.movementStateByteCcd.set(0);
        es.playerClassChainResolves().set("eg7_noblock", false); // class-row bit 0x04 clear
        // Gate-eligible (bit clear -> success anyway) - resolved directly by
        // the CHOSEN default now, see eg6a's own note.
        ok(host, gp, "assert(vehicle_enter_do('eg7_noblock', 'eg7_car', false, nil, nil, nil, 'driver') == true)");
        CHECK(!c.vehicleEnterSubmitAccepted.known());
    }

    // ==================================================================
    // Refusal 8a: bit set, cant_enter_vehicle (+0x2ba bit 0x10) set.
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg8a_cantenter", "eg8a_car");
        c.ragdollStateEnumCd2.set(0);
        c.genericInteractionLockGatePasses.set(true);
        c.movementStateByteCcd.set(0);
        // playerClassChainResolves() left at its own CHOSEN default (true -
        // a plain character name is not vehicle-shaped) - bit 0x04 set.
        c.cantEnterVehicleFlag2ba = true;
        ok(host, gp, "assert(vehicle_enter_do('eg8a_cantenter', 'eg8a_car', false, nil, nil, nil, 'driver') == false)");
    }

    // ==================================================================
    // Refusal 8b: bit set, not cant_enter_vehicle, mode == Idle (0), AI
    // sub-state in {0x10, 0x11} ("observe vehicle world/vehicular accident").
    // Two sub-cases for both boundary values.
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg8b_accident1", "eg8b_car");
        c.ragdollStateEnumCd2.set(0);
        c.genericInteractionLockGatePasses.set(true);
        c.movementStateByteCcd.set(0);
        c.cantEnterVehicleFlag2ba = false;
        c.stateCodeByte501.set(CharacterState::kAiModeIdle);
        c.aiSubStateByte500.set(0x10);
        ok(host, gp, "assert(vehicle_enter_do('eg8b_accident1', 'eg8b_car', false, nil, nil, nil, 'driver') == false)");

        CharacterState& c2 = freshChar(es, "eg8b_accident2", "eg8b_car2");
        c2.ragdollStateEnumCd2.set(0);
        c2.genericInteractionLockGatePasses.set(true);
        c2.movementStateByteCcd.set(0);
        c2.cantEnterVehicleFlag2ba = false;
        c2.stateCodeByte501.set(CharacterState::kAiModeIdle);
        c2.aiSubStateByte500.set(0x11);
        ok(host, gp, "assert(vehicle_enter_do('eg8b_accident2', 'eg8b_car2', false, nil, nil, nil, 'driver') == false)");
    }

    // ==================================================================
    // Success tail, three independent ways to reach it (NOT forced):
    //   (a) bit set, not cant_enter_vehicle, mode != Idle (sub-state never
    //       even read - no throw despite aiSubStateByte500 staying OPEN).
    //   (b) bit set, not cant_enter_vehicle, mode == Idle, sub-state outside
    //       {0x10, 0x11}.
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg9a_notidle", "eg9a_car");
        c.ragdollStateEnumCd2.set(0);
        c.genericInteractionLockGatePasses.set(true);
        c.movementStateByteCcd.set(0);
        c.cantEnterVehicleFlag2ba = false;
        c.stateCodeByte501.set(43); // sr3airuntime::mode::Player - anything != Idle
        // Gate-eligible - resolved directly by the CHOSEN default now (see
        // eg6a's own note), BEFORE touching aiSubStateByte500 at all.
        ok(host, gp, "assert(vehicle_enter_do('eg9a_notidle', 'eg9a_car', false, nil, nil, nil, 'driver') == true)");
        // aiSubStateByte500 was never set and never read - confirms the
        // lazy, short-circuited read order matches the real function's own.
        CHECK(!c.aiSubStateByte500.known());
        CHECK(!c.vehicleEnterSubmitAccepted.known());

        CharacterState& c2 = freshChar(es, "eg9b_idlesafe", "eg9b_car2");
        c2.ragdollStateEnumCd2.set(0);
        c2.genericInteractionLockGatePasses.set(true);
        c2.movementStateByteCcd.set(0);
        c2.cantEnterVehicleFlag2ba = false;
        c2.stateCodeByte501.set(CharacterState::kAiModeIdle);
        c2.aiSubStateByte500.set(0); // anything outside {0x10, 0x11}
        // Gate-eligible - resolved directly by the CHOSEN default now (see
        // eg6a's own note).
        ok(host, gp, "assert(vehicle_enter_do('eg9b_idlesafe', 'eg9b_car2', false, nil, nil, nil, 'driver') == true)");
        CHECK(!c2.vehicleEnterSubmitAccepted.known());
    }

    // ==================================================================
    // Success tail, forced path (guardian_angel - always forced): steps 1-5
    // still apply, step 6 and step 7/8 are skipped entirely - no need to
    // touch genericInteractionLockGatePasses/movementStateByteCcd/
    // cantEnterVehicleFlag2ba/aiSubStateByte500 AT ALL, and none of them
    // throws (confirms "forced -> success unconditionally" short-circuits
    // before step 7 even calls playerClassChainResolves()). stateCodeByte501
    // is NOT a usable "untouched" witness any more: as of 2026-10-10
    // (spec-lua-api-behaviour.md Sec22.3 sync) it is pre-seeded 0/Idle at
    // construction (the same situation ragdollStateEnumCd2 was already in,
    // per fdcabd5) - checked below as a value instead.
    //
    // Gate-eligible - resolved directly by the CHOSEN default now (see
    // eg6a's own note): computeVehicleEnterSubmitAccepted() no longer throws
    // on an OPEN vehicleEnterSubmitAcceptedForced (8b82232 replaced that with
    // a CHOSEN-default fallback through the real gate + seat-pick), so this
    // is a direct set-then-ok call like eg9a/eg9b, not the older "fails then
    // set then ok" two-step some earlier revisions of this test used before
    // that fallback existed.
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg10_forced_ok", "eg10_car");
        c.ragdollStateEnumCd2.set(0); // only steps 1-5's fields need anything
        c.vehicleEnterSubmitAcceptedForced.set(true); // gate-eligible result resolves concretely (see eg6a's own note)
        ok(host, gp, "assert(vehicle_enter_guardian_angel('eg10_forced_ok', 'eg10_car') == true)");
        CHECK(!c.genericInteractionLockGatePasses.known());
        CHECK(!c.movementStateByteCcd.known());
        CHECK(c.stateCodeByte501.known());
        CHECK(c.stateCodeByte501.get() == CharacterState::kAiModeIdle); // construction-time pre-seed, never touched by this gate path
        CHECK(!c.aiSubStateByte500.known());
        // FIXED (2026-10-09): completed to the real Sec7.4b value, 3
        // ("seated"), not 1 ("entering") - see the source file's own FIXED note.
        CHECK(c.stateEnum.get() == 3); // Sec7.4 step 11/Sec7.8 synchronous write on acceptance
        CHECK(c.currentVehicleName.get() == "eg10_car");
    }

    // ==================================================================
    // Backward-compatible override: a test's own .set() on
    // vehicleEnterSubmitAccepted/Forced still wins outright, without ever
    // touching the gate's own fields (full compatibility with every
    // pre-existing test in synthetic_luahost_wave2_w21_test.cpp).
    // ==================================================================
    {
        CharacterState& c = freshChar(es, "eg11_override", "eg11_car");
        c.vehicleEnterSubmitAccepted.set(true); // the gate's own fields are left entirely OPEN/default
        ok(host, gp, "assert(vehicle_enter_do('eg11_override', 'eg11_car', true) == true)");
        // The gate itself was never even invoked - the override short-
        // circuited before vehicleEnterEligibilityGate() ran at all. Checked
        // via a field the gate reads but the constructor does NOT pre-seed
        // (unlike ragdollStateEnumCd2, and - as of 2026-10-10's
        // spec-lua-api-behaviour.md Sec22.3 sync - stateCodeByte501, both of
        // which now get a CONFIRMED default at construction time and so are
        // already "known" on every fresh character regardless of whether the
        // gate ever runs, making neither a usable witness for this specific
        // check any more).
        CHECK(!c.aiSubStateByte500.known());
    }

    if (g_failures == 0) {
        std::cout << "synthetic_luahost_vehicle_enter_eligibility_gate_test: OK\n";
        return 0;
    }
    std::cerr << "synthetic_luahost_vehicle_enter_eligibility_gate_test: " << g_failures << " failure(s)\n";
    return 1;
}
