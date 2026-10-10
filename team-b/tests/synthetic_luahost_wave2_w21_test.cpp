// Synthetic tests for wave 2 batch W2-1 ("core dispatchers", 2026-10-08) -
// src/lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp: the 13
// natives vehicle_exit_do (Sec21.11), vehicle_exit_check_done (Sec24.9),
// vehicle_exit_guardian_angel / vehicle_enter_do / vehicle_enter_guardian_
// angel / vehicle_enter_group_do / vehicle_enter_group_check_done (Sec38.1),
// vehicle_enter_check_done (Sec22.3), move_to_do (Sec21.22),
// vehicle_pathfind_navmesh_do (Sec22.1), teleport_to_object (Sec22.4),
// character_damage (Sec20.32), qte_start (Sec20.9). Same convention as the
// other per-batch luahost tests: a hand-written fixture of (name, cluster)
// rows, no real tagged-registration file or archive read.
//
// Specific focus: every CONFIRMED branch and return arity (including
// vehicle_exit_do's 0-value cancel path), every labelled OPEN refusal, the
// reuse of existing fields (stateEnum/currentVehicleName/actionDelayByIndex/
// independentFollower/scriptControlledMode/qteActiveSlots_), and that
// teleport_to_object never touches teleport's own (Sec4.3) records.
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/character_damage_sink.h"
#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"
#include "sr3save/save_crc.h"

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
    auto r = host.runChunk(L, code, "wave2_w21_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "wave2_w21_test.lua");
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

std::vector<std::string> batchNames() {
    return {
        "vehicle_exit_do",          "vehicle_exit_check_done",       "vehicle_exit_guardian_angel",
        "vehicle_enter_do",         "vehicle_enter_guardian_angel",  "vehicle_enter_group_do",
        "vehicle_enter_group_check_done", "vehicle_enter_check_done", "move_to_do",
        "vehicle_pathfind_navmesh_do", "teleport_to_object",         "character_damage",
        "qte_start",
    };
}

std::vector<RegisteredName> fixture() {
    std::vector<RegisteredName> out;
    for (const auto& n : batchNames()) out.push_back({n, "gameplay"});
    out.push_back({"qte_cleanup", "gameplay"}); // b12 - to check qte_start's entries are torn down by it
    return out;
}

// A resolved, alive character with a known +0x16d4 phase.
CharacterState& seatChar(EngineState& es, const std::string& name, int32_t phase) {
    es.objectResolves().set(name, true);
    CharacterState& c = es.getOrCreateCharacter(name);
    c.stateEnum.set(phase);
    return c;
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 13 land as real functions in gameplay, nowhere else,
    // and they are part of the public spec-confirmed name list.
    {
        Host host(fixture());
        for (const auto& n : batchNames()) {
            CHECK(hasGlobal(host.gameplayState(), n.c_str()));
            CHECK(!hasGlobal(host.uiState(), n.c_str()));
            bool listed = false;
            for (const auto& s : specConfirmedStubNames()) listed = listed || s == n;
            CHECK(listed);
        }
    }

    Host host(fixture());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ==================================================================
    // Sec24.9 vehicle_exit_check_done
    // ==================================================================
    es.objectResolves().set("xc_unres", false);
    ok(host, gp, "assert(vehicle_exit_check_done('xc_unres') == true)");
    CHECK(!es.hasCharacter("xc_unres"));

    es.objectResolves().set("xc_open", true);
    ok(host, gp, "assert(vehicle_exit_check_done('xc_open') == true)"); // state 0 (default): not seated/exiting

    seatChar(es, "xc_dead", 3).lifeState.set(5); // 0x0096f4f0: dead even while seated
    ok(host, gp, "assert(vehicle_exit_check_done('xc_dead') == true)");
    seatChar(es, "xc_e3", 2).killFinalizedFlagE3.set(true);
    ok(host, gp, "assert(vehicle_exit_check_done('xc_e3') == true)");

    seatChar(es, "xc_seated", 3);
    ok(host, gp, "assert(vehicle_exit_check_done('xc_seated') == false)");
    seatChar(es, "xc_exiting", 2); // 0x009b9230: +0x16d4 == 2
    ok(host, gp, "assert(vehicle_exit_check_done('xc_exiting') == false)");
    seatChar(es, "xc_entering", 1);
    ok(host, gp, "assert(vehicle_exit_check_done('xc_entering') == true)");
    seatChar(es, "xc_none", 0);
    ok(host, gp, "assert(vehicle_exit_check_done('xc_none') == true)");

    // ==================================================================
    // Sec21.11 vehicle_exit_do
    // ==================================================================
    es.objectResolves().set("xd_unres", false);
    ok(host, gp, "assert(select('#', vehicle_exit_do('xd_unres', true)) == 1)");
    ok(host, gp, "assert(vehicle_exit_do('xd_unres', true) == false)");

    es.objectResolves().set("xd_open", true);
    ok(host, gp, "assert(vehicle_exit_do('xd_open') == false)"); // state 0 (default): neither seated nor cancel-eligible

    // Seated: the exit request + 3 back-offs, pushes true.
    es.registerAnimationStateIdForTesting("exit_dive", 77);
    CharacterState& xd = seatChar(es, "xd_seat", 3);
    ok(host, gp, "assert(vehicle_exit_do('xd_seat', true, true, true, 'exit_dive') == true)");
    CHECK(xd.vehicleExitRequestCount == 1);
    CHECK(xd.lastVehicleExitRequest.has_value());
    CHECK(xd.lastVehicleExitRequest->instant == true);   // +0x30 bit 0 = arg 2
    CHECK(xd.lastVehicleExitRequest->flag30Bit4 == true); // +0x30 bit 4 = arg 4
    CHECK(xd.lastVehicleExitRequest->animationOverrideId == 77);
    CHECK(xd.lastVehicleExitRequest->flag31Bit4 == false);
    CHECK(xd.lastVehicleExitRequest->replicated == true); // constructor default
    CHECK(xd.actionDelayByIndex.count(37) && xd.actionDelayByIndex[37] == 3000.0);
    CHECK(xd.actionDelayByIndex.count(38) && xd.actionDelayByIndex[38] == 3000.0);
    CHECK(xd.actionDelayByIndex.count(39) && xd.actionDelayByIndex[39] == 3000.0);
    CHECK(xd.actionDelayByIndex.size() == 3);
    // FIXED (2026-10-08, post-merge review): the seated branch now writes
    // +0x16d4 = 2 synchronously (spec-player-runtime.md Sec6.3.1 step 9/
    // Sec6.6.1 step 1 - Set-exit's own dispatch reaches state 0x14's
    // on-entry handler inside 0x00af3cd0's own call, zero world-tick
    // dependency) - the occupancy machine IS now advanced from here, unlike
    // this comment's own pre-fix text.
    CHECK(xd.stateEnum.get() == 2);
    CHECK(xd.vehicleExitCancelCount == 0);
    // CAE table sanity: ids 37-39 are the three "follow ... car" actions.
    CHECK(es.lookupCaeActionIndex("follow to car") == 37);
    CHECK(es.lookupCaeActionIndex("follow other car") == 38);
    CHECK(es.lookupCaeActionIndex("follow carjack") == 39);

    // Symptom check (the post-merge reviewer's own named defect): a SECOND
    // vehicle_exit_do call on this now-exiting (phase 2, not seated)
    // character must no longer return true again. Per the native's own
    // branch structure: not seated, arg 2 absent -> the final "otherwise"
    // branch -> false; arg 2 = true with phase in {1,2} -> the cancel path
    // (0 Lua values), not the seated-true path either.
    ok(host, gp, "assert(vehicle_exit_do('xd_seat') == false)");
    CHECK(xd.vehicleExitRequestCount == 1); // unchanged: the seated branch was NOT re-entered
    ok(host, gp, "assert(select('#', vehicle_exit_do('xd_seat', true)) == 0)"); // cancel path, not "true" again
    CHECK(xd.vehicleExitCancelCount == 1);
    CHECK(xd.stateEnum.get() == 2); // the cancel path does not itself touch +0x16d4 (not modelled; U6.4.1's own on-entry handlers would)

    // Re-seat (this host does not model a full ride -> exit -> re-enter
    // cycle) to keep testing the seated branch's own argument parsing in
    // isolation, now that a real exit call changes phase instead of leaving
    // it frozen at 3.
    // Defaults: arg 2 absent -> false (no nil-gate), arg 3 has no effect, no anim -> -1.
    xd.stateEnum.set(3);
    ok(host, gp, "assert(vehicle_exit_do('xd_seat') == true)");
    CHECK(xd.lastVehicleExitRequest->instant == false);
    CHECK(xd.lastVehicleExitRequest->flag30Bit4 == false);
    CHECK(xd.lastVehicleExitRequest->animationOverrideId == -1);
    CHECK(xd.stateEnum.get() == 2);
    xd.stateEnum.set(3);
    ok(host, gp, "assert(vehicle_exit_do('xd_seat', false, true) == true)"); // arg 3 alone: same record
    CHECK(xd.lastVehicleExitRequest->instant == false && xd.lastVehicleExitRequest->flag30Bit4 == false);
    xd.stateEnum.set(3);
    ok(host, gp, "vehicle_exit_do('xd_seat', false, nil, nil, 'unregistered_anim')");
    CHECK(xd.lastVehicleExitRequest->animationOverrideId == -1);
    CHECK(xd.vehicleExitRequestCount == 4);

    // Back-off gate false: the timers are not armed locally.
    CharacterState& xdg = seatChar(es, "xd_gate", 3);
    xdg.controlLockGatePasses.set(false);
    ok(host, gp, "assert(vehicle_exit_do('xd_gate') == true)");
    CHECK(xdg.actionDelayByIndex.empty());
    CHECK(xdg.vehicleExitRequestCount == 1);
    CHECK(xdg.stateEnum.get() == 2); // written regardless of the back-off gate - a separate, unrelated gate

    // Cancel path: entering/exiting + arg 2 -> ZERO return values.
    CharacterState& xc1 = seatChar(es, "xd_entering", 1);
    ok(host, gp, "assert(select('#', vehicle_exit_do('xd_entering', true)) == 0)");
    CHECK(xc1.vehicleExitCancelCount == 1);
    CHECK(xc1.vehicleExitRequestCount == 0);
    CHECK(xc1.actionDelayByIndex.empty());
    CharacterState& xc2 = seatChar(es, "xd_exiting", 2);
    ok(host, gp, "assert(select('#', vehicle_exit_do('xd_exiting', true)) == 0)");
    CHECK(xc2.vehicleExitCancelCount == 1);
    // Entering without arg 2 -> false (1 value); state 0 with arg 2 -> false.
    ok(host, gp, "assert(select('#', vehicle_exit_do('xd_entering', false)) == 1)");
    ok(host, gp, "assert(vehicle_exit_do('xd_entering', false) == false)");
    CHECK(xc1.vehicleExitCancelCount == 1);
    seatChar(es, "xd_none", 0);
    ok(host, gp, "assert(vehicle_exit_do('xd_none', true) == false)");

    // ==================================================================
    // Sec38.1 vehicle_exit_guardian_angel
    // ==================================================================
    es.objectResolves().set("xg_unres", false);
    ok(host, gp, "assert(vehicle_exit_guardian_angel('xg_unres') == false)");
    CharacterState& xg0 = seatChar(es, "xg_entering", 1);
    ok(host, gp, "assert(vehicle_exit_guardian_angel('xg_entering') == false)");
    CHECK(xg0.vehicleExitRequestCount == 0 && xg0.actionDelayByIndex.empty());
    CharacterState& xg = seatChar(es, "xg_seat", 3);
    ok(host, gp, "assert(vehicle_exit_guardian_angel('xg_seat') == true)");
    CHECK(xg.vehicleExitRequestCount == 1);
    CHECK(xg.lastVehicleExitRequest->flag31Bit4 == true);
    CHECK(xg.lastVehicleExitRequest->instant == false);
    CHECK(xg.actionDelayByIndex.size() == 3 && xg.actionDelayByIndex[38] == 3000.0);
    // FIXED (2026-10-08): same synchronous +0x16d4 = 2 write as vehicle_
    // exit_do's seated branch.
    CHECK(xg.stateEnum.get() == 2);
    // Symptom check: a second call on the now-exiting (not seated)
    // character must no longer report "was seated" (true) again.
    ok(host, gp, "assert(vehicle_exit_guardian_angel('xg_seat') == false)");
    CHECK(xg.vehicleExitRequestCount == 1); // unchanged - the seated branch was not re-entered

    // ==================================================================
    // Sec38.1 vehicle_enter_do
    // ==================================================================
    es.objectResolves().set("ed_unres", false);
    es.objectResolves().set("ed_car", true);
    ok(host, gp, "assert(vehicle_enter_do('ed_unres', 'ed_car', true) == false)");
    CHECK(!es.hasCharacter("ed_unres"));

    es.objectResolves().set("ed_c1", true);
    es.objectResolves().set("ed_nocar", false);
    ok(host, gp, "assert(vehicle_enter_do('ed_c1', 'ed_nocar', true) == false)");

    // Instant path: 2026-10-09, spec-player-runtime.md Sec7.4a - the
    // eligibility gate 0x00b0ca60 is now REALLY computed instead of being a
    // pure OPEN placeholder (see CharacterState::vehicleEnterSubmitAccepted's
    // own updated doc comment and vehicleEnterEligibilityGate() in the
    // dispatcher .cpp). For a fresh character, steps 1-3 (seated/mid-enter-
    // exit/busy-pending) all pass cleanly (seeded/CHOSEN defaults); step 4's
    // ragdoll state (+0xcd2) is itself CONFIRMED-defaulted to 0, "character
    // proxy" (fdcabd5, a separate prerequisite fix), not OPEN; step 5 passes
    // on the seeded lifeState/killFinalizedFlagE3 defaults; forced (instant)
    // skips steps 6-8 entirely - so the gate is cleanly ELIGIBLE with no
    // field set at all. 2026-10-09 follow-up (this task): an eligible result
    // with no test override now resolves via a CHOSEN default (`true`, Sec7.4
    // step 4's own seat pick, U6.2, still unmodelled - see that doc comment)
    // instead of throwing on the submitter's own OPEN fact - so this first
    // call now succeeds directly. This supersedes the test's former
    // "fails(..., '+0xcd2')" expectation (two independently-committed fixes
    // landing on top of each other, not a regression - see the dedicated
    // synthetic_luahost_vehicle_enter_eligibility_gate_test.cpp for full
    // coverage of every refusal path and the success tail).
    ok(host, gp, "assert(vehicle_enter_do('ed_c1', 'ed_car', true) == true)");
    CharacterState& ed = es.getOrCreateCharacter("ed_c1");
    CHECK(ed.lastVehicleEnterRequest.has_value());
    CHECK(ed.lastVehicleEnterRequest->accepted == true);
    CHECK(!ed.vehicleEnterSubmitAccepted.known()); // CHOSEN default resolved it - no override needed
    // FIXED (2026-10-09): Sec7.4b's confirmed synchronous no-contention
    // instant commit writes stateEnum = 3 ("seated"), not 1 ("entering") -
    // see the source file's own FIXED note for the full citation.
    CHECK(ed.stateEnum.known() && ed.stateEnum.get() == 3);
    CHECK(ed.currentVehicleName.get() == "ed_car");

    // A test's own override still wins outright (full back-compat with every
    // pre-existing caller that drives the overall result directly).
    ed.vehicleEnterSubmitAccepted.set(true);
    ok(host, gp, "assert(vehicle_enter_do('ed_c1', 'ed_car', true, 2, true, false) == true)");
    CHECK(ed.lastVehicleEnterRequest.has_value());
    CHECK(ed.lastVehicleEnterRequest->vehicleName == "ed_car");
    CHECK(ed.lastVehicleEnterRequest->path == CharacterState::kVehicleEnterPathInstant);
    CHECK(ed.lastVehicleEnterRequest->seatIndex == 2);
    CHECK(ed.lastVehicleEnterRequest->arg5 == true && ed.lastVehicleEnterRequest->arg6 == false);
    CHECK(ed.lastVehicleEnterRequest->accepted == true);
    // FIXED (2026-10-08, post-merge review): CONFIRMED spec-player-runtime.md
    // Sec7.4 step 11/Sec7.8 - the submitter's own accepted path writes
    // +0x16d4 and +0x16c0/+0x16c4 = the vehicle SYNCHRONOUSLY, inside
    // its own call - +0x16d4 IS now advanced from here (supersedes this
    // comment's own prior "is not advanced" text).
    // FIXED (2026-10-09): completed to the real Sec7.4b value, 3 ("seated"),
    // not 1 ("entering") - see the source file's own FIXED note.
    CHECK(ed.stateEnum.known() && ed.stateEnum.get() == 3);
    CHECK(ed.currentVehicleName.get() == "ed_car");
    ed.vehicleEnterSubmitAccepted.set(false);
    ok(host, gp, "assert(vehicle_enter_do('ed_c1', 'ed_car', true) == false)");
    CHECK(ed.lastVehicleEnterRequest->seatIndex == -1); // CHOSEN default
    CHECK(ed.lastVehicleEnterRequest->accepted == false);
    CHECK(ed.stateEnum.get() == 3); // a refusal leaves prior state untouched - not reset

    // Named-seat path (arg 7 given, not instant).
    ed.vehicleEnterSubmitAccepted.set(true);
    ok(host, gp, "assert(vehicle_enter_do('ed_c1', 'ed_car', false, nil, nil, nil, 'driver') == true)");
    CHECK(ed.lastVehicleEnterRequest->path == CharacterState::kVehicleEnterPathNamedSeat);
    CHECK(ed.lastVehicleEnterRequest->seatName == "driver");
    CHECK(ed.stateEnum.get() == 3); // named-seat path gets the same (now Sec7.4b-complete) write too

    // AI-order path: OPEN start result; player class -> script mode 2. Does
    // NOT go through the submitter 0x00af22a0 at all (Sec38.1: it starts
    // action 0x31 instead), so it must not touch +0x16d4/+0x16c0 - confirmed
    // below: stateEnum/currentVehicleName stay exactly as the instant/
    // named-seat paths above already left them, not re-written by this path.
    fails(host, gp, "vehicle_enter_do('ed_c1', 'ed_car', false)", "action 0x31");
    ed.vehicleEnterAiOrderStarts.set(true);
    // playerClassChainResolves(name)'s own CHOSEN default (engine_state.h):
    // "ed_c1" is not vehicle-shaped, so it now resolves player-class TRUE
    // with NO .set() call at all - replaces this test's former "fails(...,
    // '0x00a27e20')" refusal at this exact point, the only spot in this
    // project that exercised that no-longer-real throw for this native.
    ok(host, gp, "assert(vehicle_enter_do('ed_c1', 'ed_car', false) == true)");
    CHECK(ed.lastVehicleEnterRequest->path == CharacterState::kVehicleEnterPathAiOrder);
    CHECK(ed.scriptControlledMode == 2);
    CHECK(ed.stateEnum.get() == 3); // AI-order path: unchanged, not newly written
    es.objectResolves().set("ed_npc", true);
    es.playerClassChainResolves().set("ed_npc", false);
    CharacterState& npc = es.getOrCreateCharacter("ed_npc");
    npc.vehicleEnterAiOrderStarts.set(true);
    ok(host, gp, "assert(vehicle_enter_do('ed_npc', 'ed_car', false) == true)");
    CHECK(npc.scriptControlledMode == 0); // non-player: mode untouched
    CHECK(npc.stateEnum.get() == 0); // AI-order path never reaches the submitter - no write (stays at the seeded default)
    npc.vehicleEnterAiOrderStarts.set(false);
    es.playerClassChainResolves().forget("ed_npc");
    ok(host, gp, "assert(vehicle_enter_do('ed_npc', 'ed_car', false) == false)"); // failure: class not even read

    // Character fallback: arg 2 names a character -> that character's own vehicle.
    es.objectResolves().set("ed_driver", true);
    es.getOrCreateCharacter("ed_driver").currentVehicleName.set("ed_truck");
    ok(host, gp, "assert(vehicle_enter_do('ed_c1', 'ed_driver', true) == true)");
    CHECK(ed.lastVehicleEnterRequest->vehicleName == "ed_truck");
    CHECK(ed.currentVehicleName.get() == "ed_truck"); // the write follows the RESOLVED vehicle, not the raw arg
    es.objectResolves().set("ed_walker", true);
    es.getOrCreateCharacter("ed_walker").currentVehicleName.set("");
    ok(host, gp, "assert(vehicle_enter_do('ed_c1', 'ed_walker', true) == false)");
    CHECK(ed.currentVehicleName.get() == "ed_truck"); // unchanged: fallback failure returns before any write

    // Follower-flag side effect: only a character on some leader's party list.
    CHECK(!ed.independentFollower.known());
    es.objectResolves().set("ed_homie", true);
    es.party().membersByLeader["#PLAYER1#"].push_back("ed_homie");
    CharacterState& homie = es.getOrCreateCharacter("ed_homie");
    homie.vehicleEnterSubmitAccepted.set(false);
    ok(host, gp, "assert(vehicle_enter_do('ed_homie', 'ed_car', true) == false)");
    CHECK(homie.independentFollower.known() && homie.independentFollower.get() == true);
    CHECK(!ed.independentFollower.known()); // not following anyone - untouched
    CHECK(homie.stateEnum.get() == 0); // refused instant placement - no write (stays at the seeded default)

    // ==================================================================
    // Sec38.1 vehicle_enter_guardian_angel - forced submit, separate OPEN fact.
    // ==================================================================
    es.objectResolves().set("eg_c", true);
    CharacterState& eg = es.getOrCreateCharacter("eg_c");
    // 2026-10-09, Sec7.4a: the real gate now runs before the submitter's own
    // CHOSEN-default fallback (this task's 2026-10-09 follow-up), even for a
    // forced request (steps 1-5 still apply unconditionally) - every one of
    // those fields is already satisfied for a fresh character (step 4's
    // ragdoll state is itself CONFIRMED-defaulted to 0, fdcabd5; the rest are
    // seeded), so the gate clears with NOTHING explicitly set, and the
    // eligible result resolves via vehicleEnterSubmitAcceptedForced's own
    // CHOSEN default (true) rather than throwing. Proven to be the SEPARATE
    // forced fact, not the ORDINARY one (vehicleEnterSubmitAccepted, set to
    // the OPPOSITE value below): if the native misread the ordinary field
    // instead, this call would return false, not true.
    eg.vehicleEnterSubmitAccepted.set(false); // the ORDINARY result must not be what's read
    ok(host, gp, "assert(vehicle_enter_guardian_angel('eg_c', 'ed_car', 1) == true)");
    CHECK(!eg.vehicleEnterSubmitAcceptedForced.known()); // CHOSEN default resolved it - no override needed
    CHECK(eg.lastVehicleEnterRequest->path == CharacterState::kVehicleEnterPathGuardianAngel);
    CHECK(eg.lastVehicleEnterRequest->seatIndex == 1);
    // FIXED (2026-10-08): same synchronous Sec7.4 step 11/Sec7.8 write as
    // vehicle_enter_do's instant/named-seat paths - "forced" eligibility
    // only changes which refusals are skipped before acceptance.
    // FIXED (2026-10-09): completed to the real Sec7.4b value, 3 ("seated"),
    // not 1 ("entering") - see the source file's own FIXED note.
    CHECK(eg.stateEnum.known() && eg.stateEnum.get() == 3);
    CHECK(eg.currentVehicleName.get() == "ed_car");
    ok(host, gp, "assert(vehicle_enter_guardian_angel('eg_c', 'ed_nocar') == false)");
    ok(host, gp, "assert(vehicle_enter_guardian_angel('ed_unres', 'ed_car') == false)");

    // ==================================================================
    // Sec38.1 vehicle_enter_group_do
    // ==================================================================
    ok(host, gp, "assert(vehicle_enter_group_do('ed_nocar', true, {'eg_c'}) == false)");
    es.objectResolves().set("gm_a", true);
    es.objectResolves().set("gm_b", true);
    es.objectResolves().set("gm_x", false); // unresolved member - still occupies table position 2
    es.objectResolves().set("gm_c", true);
    CharacterState& ga = es.getOrCreateCharacter("gm_a");
    CharacterState& gb = es.getOrCreateCharacter("gm_b");
    CharacterState& gc = es.getOrCreateCharacter("gm_c");
    ga.vehicleEnterSubmitAccepted.set(true);
    gb.vehicleEnterSubmitAccepted.set(true);
    gc.vehicleEnterSubmitAccepted.set(true);
    ok(host, gp, "assert(vehicle_enter_group_do('ed_car', true, {'gm_a', 'gm_b', 'gm_x', 'gm_c'}) == true)");
    CHECK(ga.lastVehicleEnterRequest->seatIndex == 0); // first table entry = seat 0 = driver
    CHECK(gb.lastVehicleEnterRequest->seatIndex == 1);
    CHECK(gc.lastVehicleEnterRequest->seatIndex == 3); // strictly by table position
    CHECK(!es.hasCharacter("gm_x"));
    // FIXED (2026-10-09, mission-drive m07 deadlock - see attemptGroupInstant
    // Enter()'s own doc comment): each accepted instant member reaches the
    // real Sec7.4b no-contention synchronous commit - stateEnum = 3
    // ("seated"), not 1 ("entering") as a prior pass had it. This is what
    // lets vehicle_enter_group_check_done's own seated-in-this-vehicle clear
    // test (below) ever observe a cleared member at all.
    CHECK(ga.stateEnum.known() && ga.stateEnum.get() == 3);
    CHECK(gb.stateEnum.known() && gb.stateEnum.get() == 3);
    CHECK(gc.stateEnum.known() && gc.stateEnum.get() == 3);
    CHECK(ga.currentVehicleName.get() == "ed_car");
    gb.vehicleEnterSubmitAccepted.set(false); // any refused instant placement -> false
    ok(host, gp, "assert(vehicle_enter_group_do('ed_car', true, {'gm_a', 'gm_b', 'gm_c'}) == false)");
    CHECK(gc.vehicleEnterRequestCount == 2); // CHOSEN: the walk continues past a refusal
    CHECK(gb.stateEnum.get() == 3); // refused THIS call - prior seated state from the first call untouched, not reset
    // Non-instant: orders, results never read - no OPEN refusal even when unset.
    es.objectResolves().set("gm_d", true);
    CharacterState& gd = es.getOrCreateCharacter("gm_d");
    ok(host, gp, "assert(vehicle_enter_group_do('ed_car', false, {'gm_d'}) == true)");
    CHECK(gd.lastVehicleEnterRequest->path == CharacterState::kVehicleEnterPathAiOrder);
    CHECK(!gd.vehicleEnterAiOrderStarts.known());
    CHECK(gd.stateEnum.get() == 0); // the order branch never reaches the submitter - no write (stays at the seeded default)
    // Nobody reached -> false; missing/empty table -> false.
    ok(host, gp, "assert(vehicle_enter_group_do('ed_car', false, {'gm_x'}) == false)");
    ok(host, gp, "assert(vehicle_enter_group_do('ed_car', false, {}) == false)");
    ok(host, gp, "assert(vehicle_enter_group_do('ed_car', false) == false)");
    // t.n honoured; a hostile __index metamethod is never invoked (raw reads).
    ok(host, gp, "assert(vehicle_enter_group_do('ed_car', false, {n = 0, 'gm_d'}) == false)");
    ok(host, gp,
       "local t = setmetatable({}, {__index = function() error('idx') end})\n"
       "assert(vehicle_enter_group_do('ed_car', false, t) == false)");
    // Character fallback for the group vehicle; follower flip per member.
    es.party().membersByLeader["#PLAYER1#"].push_back("gm_d");
    ok(host, gp, "assert(vehicle_enter_group_do('ed_driver', false, {'gm_d'}) == true)");
    CHECK(gd.lastVehicleEnterRequest->vehicleName == "ed_truck");
    CHECK(gd.independentFollower.known() && gd.independentFollower.get() == true);

    // ==================================================================
    // Sec38.1 vehicle_enter_group_check_done
    // ==================================================================
    // FIXED (2026-10-09, per Team A spec-sync): real per-member completion
    // rule, no completion threshold - see stub_vehicle_enter_group_check_done's
    // own doc comment in lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp.
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_nocar', false, {'gm_a'}) == true)");
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_driver', false, {'gm_a'}) == true)"); // no character fallback
    // gm_a is already seated (stateEnum==3) in ed_car from the group_do call
    // above (now that attemptGroupInstantEnter() writes the real Sec7.4b
    // commit) - it clears on the very first poll, no reorder needed.
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', true, {'gm_a'}) == true)");
    // A fresh member whose instant placement is REFUSED this call is a real
    // straggler: not done, and (do_not_reorder=false) re-issued through the
    // shared attemptGroupInstantEnter() helper each poll, incrementing its
    // own request count, but still refused -> still not done.
    es.objectResolves().set("gm_e", true);
    CharacterState& ge = es.getOrCreateCharacter("gm_e");
    ge.vehicleEnterSubmitAccepted.set(false);
    uint64_t geCountBefore = ge.vehicleEnterRequestCount;
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', false, {'gm_a', 'gm_e'}) == false)");
    CHECK(ge.vehicleEnterRequestCount == geCountBefore + 1); // straggler re-ordered
    CHECK(!ge.stateEnum.known() || ge.stateEnum.get() != 3); // still refused -> never reached seated
    // do_not_reorder=true: still a straggler (not done), but NOT re-issued -
    // the exact hard-coded shape Team A attributes to vehicle_enter_group_teleport
    // (that native's own [do_not_reorder]=true argument is Lua-side, out of
    // this file's scope - see stub_vehicle_enter_group_check_done's own doc
    // comment).
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', true, {'gm_a', 'gm_e'}) == false)");
    CHECK(ge.vehicleEnterRequestCount == geCountBefore + 1); // unchanged - no re-order this time
    // THE FIX ITSELF: once the straggler's own instant placement is actually
    // ACCEPTED, the very next re-order (do_not_reorder=false) seats it for
    // real (stateEnum=3, the Sec7.4b no-contention synchronous commit) -
    // no manual stateEnum override needed, unlike before this fix - and the
    // member clears on its OWN very next poll (the poll that issued the
    // reorder still answers false, matching Sec38.1's "re-issues... each
    // time it's called" - the result is decided before the reorder runs).
    ge.vehicleEnterSubmitAccepted.set(true);
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', false, {'gm_a', 'gm_e'}) == false)");
    CHECK(ge.stateEnum.known() && ge.stateEnum.get() == 3);
    CHECK(ge.currentVehicleName.get() == "ed_car");
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', true, {'gm_a', 'gm_e'}) == true)");
    // No completion threshold: a second, unresolved member is skipped
    // (CHOSEN - see doc comment), still "done" with gm_a alone clear.
    es.objectResolves().set("gm_unresolved_straggler", false);
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', true, {'gm_a', 'gm_unresolved_straggler'}) == true)");
    // A second REAL, non-clearing member blocks "done" even though gm_a
    // itself already cleared - no threshold, every member must clear.
    es.objectResolves().set("gm_straggler2", true);
    CharacterState& gs2 = es.getOrCreateCharacter("gm_straggler2");
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', true, {'gm_a', 'gm_straggler2'}) == false)");
    // The +0x501 state-exemption clears a member too, independent of stateEnum.
    gs2.stateCodeByte501.set(0x26);
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', true, {'gm_a', 'gm_straggler2'}) == true)");
    // Dead also clears, independent of either of the above.
    CharacterState& gs3 = es.getOrCreateCharacter("gm_straggler3");
    es.objectResolves().set("gm_straggler3", true);
    gs3.lifeState.set(6);
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', true, {'gm_a', 'gm_straggler3'}) == true)");
    // "Matching handle": seated (==3) in a DIFFERENT vehicle does not clear.
    CharacterState& gs4 = es.getOrCreateCharacter("gm_straggler4");
    es.objectResolves().set("gm_straggler4", true);
    es.objectResolves().set("ed_othercar", true);
    gs4.stateEnum.set(3);
    gs4.currentVehicleName.set("ed_othercar");
    ok(host, gp, "assert(vehicle_enter_group_check_done('ed_car', true, {'gm_a', 'gm_straggler4'}) == false)");

    // ==================================================================
    // Sec22.3 vehicle_enter_check_done
    // ==================================================================
    es.objectResolves().set("ec_unres", false);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_unres') == 2)");

    CharacterState& ecd = seatChar(es, "ec_dead", 1);
    ecd.lifeState.set(5);
    es.playerClassChainResolves().set("ec_dead", false);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_dead') == 1)");

    seatChar(es, "ec_seated", 3);
    es.playerClassChainResolves().set("ec_seated", false);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_seated') == 1)");

    seatChar(es, "ec_entering", 1);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_entering') == 0)"); // 0.0: player class not even read
    seatChar(es, "ec_exiting", 2);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_exiting') == 0)");

    CharacterState& ec0 = seatChar(es, "ec_none", 0);
    es.playerClassChainResolves().set("ec_none", false);
    // CONFIRMED (2026-10-10, spec-lua-api-behaviour.md Sec22.3 sync):
    // stateCodeByte501 is now pre-seeded 0 (Idle) by getOrCreateCharacter, so
    // a character that never touches it no longer throws here - it reads as
    // "not '&'", short-circuits past the still-OPEN stateCodeByte504 entirely
    // (never read), and returns the default 2.0.
    ok(host, gp, "assert(vehicle_enter_check_done('ec_none') == 2)");
    ec0.stateCodeByte501.set('&');
    // stateCodeByte504 (the requested-mode byte) remains genuinely OPEN
    // per-character - setting 501 to '&' alone is no longer short-circuited,
    // so this still throws until a test supplies 504 too.
    fails(host, gp, "vehicle_enter_check_done('ec_none')", "+0x504");
    ec0.stateCodeByte504.set(CharacterState::kAiModeHijack); // 0x12 = Hijack (spec-ai-runtime.md Sec3.2 mode 18)
    ok(host, gp, "assert(vehicle_enter_check_done('ec_none') == 0)");
    ec0.stateCodeByte504.set(0x13);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_none') == 2)");
    // arg 2: the 0x009b95c0 helper (OPEN), read only then.
    fails(host, gp, "vehicle_enter_check_done('ec_none', true)", "0x009b95c0");
    ec0.enterCheckHelper9b95c0.set(true);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_none', true, 'ed_car') == 1)");
    ec0.enterCheckHelper9b95c0.set(false);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_none', true, 'never_registered_vehicle') == 2)");

    // Player class + result != 0.0 -> 0x009e3200 mode 0 (script mode cleared).
    CharacterState& ecp = seatChar(es, "ec_player", 3);
    es.playerClassChainResolves().set("ec_player", true);
    ecp.scriptControlledMode = 5;
    ecp.statusByte28ac.setBits(CharacterState::kScriptControlledBit28ac, CharacterState::kScriptControlledBit28ac);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_player') == 1)");
    CHECK(ecp.scriptControlledMode == 0);
    CHECK(ecp.statusByte28ac.get(CharacterState::kScriptControlledBit28ac) == 0);
    // ... but not when the result is 0.0.
    ecp.stateEnum.set(1);
    ecp.scriptControlledMode = 5;
    ok(host, gp, "assert(vehicle_enter_check_done('ec_player') == 0)");
    CHECK(ecp.scriptControlledMode == 5);
    // Gate false: replicated instead of written.
    ecp.stateEnum.set(3);
    ecp.controlLockGatePasses.set(false);
    ok(host, gp, "assert(vehicle_enter_check_done('ec_player') == 1)");
    CHECK(ecp.scriptControlledMode == 5);

    // ==================================================================
    // Post-merge review fix (2026-10-08): end-to-end checks of the two
    // concretely-wrong symptoms the reviewer named, using the real native
    // call chain (not a test-seeded stateEnum) - spec-player-runtime.md
    // Sec7.4 step 11/Sec7.8 (enter) and Sec6.3.1 step 9/Sec6.6.1 step 1
    // (exit) confirm both writes happen synchronously, inside the
    // submitter's/exit-request's own call.
    // ==================================================================

    // Symptom 1 (exit): a second vehicle_exit_do call on an already-exiting
    // character must no longer return true again. (The per-feature exit
    // blocks above already check this against the SAME character the first
    // call used; this is an independent, fresh-character confirmation.)
    es.objectResolves().set("sym_x", true);
    CharacterState& symx = seatChar(es, "sym_x", 3);
    ok(host, gp, "assert(vehicle_exit_do('sym_x') == true)");
    CHECK(symx.stateEnum.get() == 2); // +0x16d4 advanced synchronously, not left at 3
    ok(host, gp, "assert(vehicle_exit_do('sym_x') == false)"); // no longer true again

    // Symptom 2 (enter): vehicle_enter_check_done must no longer get stuck.
    // BEFORE the 2026-10-08 fix, vehicle_enter_do never wrote stateEnum at
    // all, so polling vehicle_enter_check_done right after an accepted enter
    // threw OpenStateError ("+0x16d4" unset) on every call - a mission-driver
    // poll loop could never progress past its first iteration.
    // FIXED (2026-10-09): the 2026-10-08 fix only got as far as stateEnum = 1
    // ("entering"), which made vehicle_enter_check_done resolve deterministically
    // but PERMANENTLY to 0.0 (Sec22.3's "entering or exiting" bucket) - this
    // is the real m05/m09 mission-drive deadlock (vehicle_enter_teleport's own
    // `repeat thread_yield() until vehicle_enter_check_done(...)` poll loop
    // could never observe a cleared member). Per Sec7.4b's confirmed
    // synchronous no-contention instant commit, the accepted enter now leaves
    // stateEnum known as 3 ("seated"), so the poll resolves to 1.0 (done) on
    // its very first check - see this native's own phase==3 branch above.
    es.objectResolves().set("sym_e", true);
    es.objectResolves().set("sym_e_car", true);
    CharacterState& syme = es.getOrCreateCharacter("sym_e");
    syme.vehicleEnterSubmitAccepted.set(true);
    ok(host, gp, "assert(vehicle_enter_do('sym_e', 'sym_e_car', true) == true)");
    CHECK(syme.stateEnum.known());
    CHECK(syme.stateEnum.get() == 3);
    CHECK(syme.currentVehicleName.get() == "sym_e_car");
    ok(host, gp, "assert(vehicle_enter_check_done('sym_e') == 1)"); // was: OpenStateError forever, then stuck at 0.0 - now really done

    // ==================================================================
    // Sec21.22 move_to_do
    // ==================================================================
    es.objectResolves().set("mt_unres", false);
    ok(host, gp, "assert(move_to_do('mt_unres', 'mt_dest', 1, false, false, false, 0) == 0)");
    CHECK(!es.hasCharacter("mt_unres"));
    fails(host, gp, "move_to_do('mt_unres', 'mt_dest', 0/0, false, false, false, 0)", "0x00ea2596"); // NaN mode

    es.objectResolves().set("mt_c", true);
    fails(host, gp, "move_to_do('mt_c', 'mt_dest', 1, false, false, true, 3)", "0x00a53f60"); // arg-6 path OPEN

    es.objectResolves().set("mt_nodest", false);
    ok(host, gp, "assert(move_to_do('mt_c', 'mt_nodest', 1, false, false, false, 0) == 0)");
    CharacterState& mt = es.getOrCreateCharacter("mt_c");
    CHECK(mt.moveToPreMoveCallCount == 1); // 0x0095acb0 is unconditional once arg 1 resolves
    CHECK(mt.moveToTimerC30.has_value() && *mt.moveToTimerC30 == -1);
    CHECK(!mt.moveToRequestTagC34.has_value());

    es.objectResolves().set("mt_dest", true);
    fails(host, gp, "move_to_do('mt_c', 'mt_dest', 1, false, false, false, 0)", "0x00a3bae0(character, false)");
    fails(host, gp, "move_to_do('mt_c', 'mt_dest', 1, false, true, false, 0)", "0x00a3bae0(character, true)");
    mt.moveToPredicateA3baeArgFalse.set(false);
    mt.moveToPredicateA3baeArgTrue.set(true);
    *mt.moveToTimerC30 = 99;
    ok(host, gp, "assert(move_to_do('mt_c', 'mt_dest', 1, false, false, false, 0) == 0)");
    CHECK(*mt.moveToTimerC30 == -1);

    fails(host, gp, "move_to_do('mt_c', 'mt_dest', 4, false, true, false, 0)", "outside 1/2/3");
    fails(host, gp, "move_to_do('mt_c', 'mt_dest', 1, false, true, false, 0)", "0x00993630");
    CHECK(es.hasActiveScriptRequest("mt_c", EngineState::kScriptedRequestKindMoveOrPathfind)); // allocated before 0x00993630
    mt.moveToDispatchSucceeds.set(false);
    ok(host, gp, "assert(move_to_do('mt_c', 'mt_dest', 2, false, true, false, 0) == 0)");
    CHECK(*mt.moveToTimerC30 == -1);

    mt.moveToDispatchSucceeds.set(true);
    fails(host, gp, "move_to_do('mt_c', 'mt_dest', 2, true, true, false, 0)", "0x0130cb94"); // counter OPEN
    CHECK(*mt.moveToTimerC30 == -1); // the refusal wrote nothing on the success path
    es.moveToActionIdCounter().set(0);
    ok(host, gp, "assert(move_to_do('mt_c', 'mt_dest', 2, true, true, false, 0) == 1)");
    CHECK(mt.moveToRequestTagC34.has_value() && *mt.moveToRequestTagC34 == 1);
    CHECK(*mt.moveToTimerC30 > 0);
    CHECK(mt.lastMoveToRequest->stateCode == 2 && mt.lastMoveToRequest->arg4 == true &&
          mt.lastMoveToRequest->destinationName == "mt_dest" && mt.lastMoveToRequest->returnedId == 1);
    ok(host, gp, "assert(move_to_do('mt_c', 'mt_dest', 1, false, true, false, 0) == 2)"); // sequential
    // Mode 3: keeps code 3 if a probe resolves, else mode 1's code.
    fails(host, gp, "move_to_do('mt_c', 'mt_dest', 3, false, true, false, 0)", "0x004b46c0");
    mt.moveToMode3AnimTagResolves.set(false);
    ok(host, gp, "assert(move_to_do('mt_c', 'mt_dest', 3, false, true, false, 0) == 3)");
    CHECK(mt.lastMoveToRequest->stateCode == 1);
    mt.moveToMode3AnimTagResolves.set(true);
    ok(host, gp, "move_to_do('mt_c', 'mt_dest', 3.9, false, true, false, 0)"); // truncation: 3.9 -> mode 3
    CHECK(mt.lastMoveToRequest->stateCode == 3 && mt.lastMoveToRequest->moveMode == 3);
    // 16-bit wrap, skipping 0.
    es.moveToActionIdCounter().set(0xFFFF);
    ok(host, gp, "assert(move_to_do('mt_c', 'mt_dest', 1, false, true, false, 0) == 1)");

    // ==================================================================
    // Sec22.1 vehicle_pathfind_navmesh_do
    // ==================================================================
    es.objectResolves().set("pn_unres", false);
    fails(host, gp, "vehicle_pathfind_navmesh_do('pn_unres', 'pn_dest', false, false, false, 0)", "does not resolve");
    es.objectResolves().set("pn_v", true);
    fails(host, gp, "vehicle_pathfind_navmesh_do('pn_v', 'pn_dest', false, false, false, 0)", "seat-0");
    VehicleState& pv = es.getOrCreateVehicle("pn_v");
    pv.seat0Occupied.set(false);
    fails(host, gp, "vehicle_pathfind_navmesh_do('pn_v', 'pn_dest', false, false, false, 0)", "clear the pending request");
    pv.seat0Occupied.set(true);
    fails(host, gp, "vehicle_pathfind_navmesh_do('pn_v', '', false, false, false, 0)", "clear the pending request");
    fails(host, gp, "vehicle_pathfind_navmesh_do('pn_v', {}, false, false, false, 0)", "clear the pending request");
    es.objectResolves().set("pn_dest", true);
    fails(host, gp, "vehicle_pathfind_navmesh_do('pn_v', 'pn_dest', false, false, false, 0)", "0x00b24fa0");
    CHECK(!es.hasActiveScriptRequest("pn_v", EngineState::kScriptedRequestKindMoveOrPathfind)); // nothing before the refusal
    es.vehicleNavmeshDispatchResult().set(7.0);
    ok(host, gp, "assert(vehicle_pathfind_navmesh_do('pn_v', 'pn_dest', true, false, true, 4.7) == 7)");
    CHECK(pv.navmeshPathfindCount == 1);
    CHECK(pv.lastNavmeshPathfindRequest->destinationName == "pn_dest");
    CHECK(pv.lastNavmeshPathfindRequest->flagArg3 && !pv.lastNavmeshPathfindRequest->flagArg4 &&
          pv.lastNavmeshPathfindRequest->flagArg5);
    CHECK(pv.lastNavmeshPathfindRequest->numberArg6 == 4);
    CHECK(pv.lastNavmeshPathfindRequest->appliedLocally);
    CHECK(es.activeScriptRequestIdOf("pn_v", EngineState::kScriptedRequestKindMoveOrPathfind) ==
          pv.lastNavmeshPathfindRequest->requestId);
    CHECK(pv.lastNavmeshPathfindRequest->requestId != 0);
    es.objectResolves().set("pn_nowhere", false);
    pv.forceFlagGatePasses.set(false);
    ok(host, gp, "assert(vehicle_pathfind_navmesh_do('pn_v', 'pn_nowhere', false, false, false, 0) == 7)");
    CHECK(pv.lastNavmeshPathfindRequest->destinationName.empty()); // the vehicle's own position
    CHECK(!pv.lastNavmeshPathfindRequest->appliedLocally);         // replicated (opcode 0x41)

    // ==================================================================
    // Sec22.4 teleport_to_object
    // ==================================================================
    const size_t teleportRecordsBefore = es.teleportRequests().size();
    const uint64_t teleportCountBefore = es.teleportScriptedRequestCount();
    es.objectResolves().set("tp_c", true);
    ok(host, gp, "teleport_to_object('tp_c', '')");  // empty raw pointer -> body skipped
    ok(host, gp, "teleport_to_object('tp_c', {})");  // non-string -> NULL -> body skipped
    ok(host, gp, "teleport_to_object('tp_c')");
    CHECK(!es.hasCharacter("tp_c"));
    es.objectResolves().set("tp_gone", false);
    ok(host, gp, "teleport_to_object('tp_c', 'tp_gone')");
    CHECK(!es.hasCharacter("tp_c"));
    es.objectResolves().set("tp_target", true);
    es.objectResolves().set("tp_unres", false);
    ok(host, gp, "teleport_to_object('tp_unres', 'tp_target')");
    CHECK(!es.hasCharacter("tp_unres"));
    // (Former "fails(..., '0x00a27e20')" check removed here: playerClassChainResolves(name)'s
    // new CHOSEN default, engine_state.h, now resolves an untouched, non-vehicle-shaped name like
    // "tp_c" to TRUE automatically - that specific OPEN-until-set throw no longer occurs. See
    // tests/synthetic_luahost_trigger_mission_cluster_test.cpp for dedicated default-behavior
    // coverage of this gate. The next line immediately overrides "tp_c" to false, before any other
    // call touches it, to exercise the non-player path this test actually targets below.)

    // Non-player, local authority: applied at once, request completed, shield ejected.
    es.playerClassChainResolves().set("tp_c", false);
    CharacterState& tp = es.getOrCreateCharacter("tp_c");
    tp.humanShieldHostageName = "tp_hostage";
    ok(host, gp, "teleport_to_object('tp_c', 'tp_target', true, nil, 1, 2, 3, 3.14159265)");
    CHECK(tp.teleportToObjectCount == 1);
    CHECK(tp.lastTeleportToObjectRequest->targetObjectName == "tp_target");
    CHECK(tp.lastTeleportToObjectRequest->arg3 == true);
    CHECK(tp.lastTeleportToObjectRequest->arg4 == true); // CONFIRMED default true
    CHECK(tp.lastTeleportToObjectRequest->offsetX == 1 && tp.lastTeleportToObjectRequest->offsetY == 2 &&
          tp.lastTeleportToObjectRequest->offsetZ == 3);
    CHECK(std::fabs(tp.lastTeleportToObjectRequest->headingDegrees - 180.0) < 1e-3);
    CHECK(!tp.lastTeleportToObjectRequest->tookPlayerPath);
    CHECK(tp.lastTeleportToObjectRequest->appliedLocally);
    CHECK(tp.teleportToObjectShieldEjectCount == 1 && tp.humanShieldHostageName.empty());
    CHECK(!es.hasActiveScriptRequest("tp_c", EngineState::kScriptedRequestKindTeleport));
    ok(host, gp, "teleport_to_object('tp_c', 'tp_target')");
    CHECK(tp.teleportToObjectShieldEjectCount == 1); // no partner -> nothing to eject
    CHECK(tp.lastTeleportToObjectRequest->arg3 == false && tp.lastTeleportToObjectRequest->headingRadians == 0.0);
    // Without authority: opcode 0x41 only, the request stays outstanding.
    tp.controlLockGatePasses.set(false);
    ok(host, gp, "teleport_to_object('tp_c', 'tp_target')");
    CHECK(!tp.lastTeleportToObjectRequest->appliedLocally);
    CHECK(es.hasActiveScriptRequest("tp_c", EngineState::kScriptedRequestKindTeleport));
    // Player path: the asynchronous 0x009e2220 path - recorded, request left allocated.
    es.objectResolves().set("tp_player", true);
    es.playerClassChainResolves().set("tp_player", true);
    ok(host, gp, "teleport_to_object('tp_player', 'tp_target', false, false)");
    CharacterState& tpp = es.getOrCreateCharacter("tp_player");
    CHECK(tpp.lastTeleportToObjectRequest->tookPlayerPath);
    CHECK(!tpp.lastTeleportToObjectRequest->appliedLocally);
    CHECK(tpp.lastTeleportToObjectRequest->arg4 == false);
    CHECK(es.hasActiveScriptRequest("tp_player", EngineState::kScriptedRequestKindTeleport));
    // teleport's own (Sec4.3) records are never touched.
    CHECK(es.teleportRequests().size() == teleportRecordsBefore);
    CHECK(es.teleportScriptedRequestCount() == teleportCountBefore);

    // ==================================================================
    // Sec20.32 character_damage
    // ==================================================================
    es.objectResolves().set("cd_unres", false);
    ok(host, gp, "character_damage('cd_unres', 10)");
    CHECK(!es.hasCharacter("cd_unres"));
    es.objectResolves().set("cd_t", true);
    fails(host, gp, "character_damage('cd_t', 10)", "+0x33 bit 0x2");
    CharacterState& cd = es.getOrCreateCharacter("cd_t");
    cd.damageImmuneDescriptorBit33.set(true);
    ok(host, gp, "character_damage('cd_t', 10)"); // early exit
    CHECK(cd.characterDamageReplicateCount == 0);
    cd.damageImmuneDescriptorBit33.set(false);

    // ------------------------------------------------------------------
    // wave 6 (2026-10-09, spec-player-runtime.md Sec15.3a): the real
    // authoritative-branch mechanism - health reduction, resistance/
    // difficulty scaling, the death threshold, and the ragdoll/finalize
    // hand-off. Each sub-case uses its own fresh character so the
    // assertions below don't depend on call order.
    // ------------------------------------------------------------------

    // (a) Plain reduction, no source: 100 HP, 30 damage -> 70 left.
    CharacterState& dmg1 = es.getOrCreateCharacter("cd_dmg1");
    es.objectResolves().set("cd_dmg1", true);
    dmg1.damageImmuneDescriptorBit33.set(false);
    dmg1.maxHitPoints.set(100);
    dmg1.currentHitPoints.set(100);
    ok(host, gp, "character_damage('cd_dmg1', 30)");
    CHECK(dmg1.currentHitPoints.get() == 70);
    CHECK(dmg1.lastDamageMagnitude == 30.0);
    CHECK(!dmg1.isDeadHighConfidence);

    // (a2) REGRESSION CHECK (review pass, 2026-10-09): maxHitPoints must
    // resolve through EngineState::drawOrGetMaxHitPoints() - the SAME
    // Sec60.4 lazy-draw convention get_max_hit_points/set_current_hit_points
    // already use for this exact field (engine_state.h's own doc comment:
    // "this cap is resolved the same way regardless of which of the two
    // natives a script calls first") - not a direct, un-drawn
    // maxHitPoints.get(). A direct .get() would reintroduce the identical
    // OpenStateError-for-an-ordinary-character class of bug Sec60.4 already
    // fixed (probe_blockers_ranked_20261007.md item 2, 26/49 missions) for
    // any character damaged before its own cap was ever explicitly queried
    // or set - character_damage is a third such native.
    CharacterState& lazyCap = es.getOrCreateCharacter("cd_dmg_lazycap");
    es.objectResolves().set("cd_dmg_lazycap", true);
    lazyCap.damageImmuneDescriptorBit33.set(false);
    lazyCap.currentHitPoints.set(40); // known; maxHitPoints deliberately left OPEN
    CHECK(!lazyCap.maxHitPoints.known());
    ok(host, gp, "character_damage('cd_dmg_lazycap', 10)"); // must NOT throw OpenStateError
    CHECK(lazyCap.maxHitPoints.known());
    CHECK(lazyCap.maxHitPoints.get() == 100); // drawOrGetMaxHitPoints()'s own CHOSEN diagnostic fallback - installSpawnRankHitPoints() is never called in this test file
    CHECK(lazyCap.currentHitPoints.get() == 30);

    // (b) Resistance scaling: the source's own instigatorDamageScalePercent
    // (CHOSEN stand-in - see that field's own doc comment) halves the
    // amount before it lands.
    es.objectResolves().set("cd_resist_src", true);
    es.getOrCreateCharacter("cd_resist_src").instigatorDamageScalePercent = 50;
    CharacterState& resistT = es.getOrCreateCharacter("cd_resist_t");
    es.objectResolves().set("cd_resist_t", true);
    resistT.damageImmuneDescriptorBit33.set(false);
    resistT.maxHitPoints.set(100);
    resistT.currentHitPoints.set(100);
    ok(host, gp, "character_damage('cd_resist_t', 40, nil, 'cd_resist_src')");
    CHECK(resistT.currentHitPoints.get() == 80); // 40 * 50% = 20 taken

    // (c) Difficulty scalar: applied ONLY when the instigator is
    // player-class, using the REAL Damage_Dealt_Mult values (Set A,
    // spec-tables-progression.md Sec14.7): L0/L1/L2 = 1.1/1.0/0.75.
    es.objectResolves().set("cd_diff_src", true);
    es.playerClassChainResolves().set("cd_diff_src", true);
    int32_t savedDifficulty = es.difficultyLevel();
    es.difficultyLevel() = 0;
    CharacterState& diff0 = es.getOrCreateCharacter("cd_diff0");
    es.objectResolves().set("cd_diff0", true);
    diff0.damageImmuneDescriptorBit33.set(false);
    diff0.maxHitPoints.set(100);
    diff0.currentHitPoints.set(100);
    ok(host, gp, "character_damage('cd_diff0', 10, nil, 'cd_diff_src')");
    CHECK(diff0.currentHitPoints.get() == 89); // 10 * 1.1 = 11

    es.difficultyLevel() = 1;
    CharacterState& diff1 = es.getOrCreateCharacter("cd_diff1");
    es.objectResolves().set("cd_diff1", true);
    diff1.damageImmuneDescriptorBit33.set(false);
    diff1.maxHitPoints.set(100);
    diff1.currentHitPoints.set(100);
    ok(host, gp, "character_damage('cd_diff1', 10, nil, 'cd_diff_src')");
    CHECK(diff1.currentHitPoints.get() == 90); // 10 * 1.0 = 10

    es.difficultyLevel() = 2;
    CharacterState& diff2 = es.getOrCreateCharacter("cd_diff2");
    es.objectResolves().set("cd_diff2", true);
    diff2.damageImmuneDescriptorBit33.set(false);
    diff2.maxHitPoints.set(100);
    diff2.currentHitPoints.set(100);
    ok(host, gp, "character_damage('cd_diff2', 10, nil, 'cd_diff_src')");
    // 10 * 0.75 = 7.5 taken -> 100 - 7.5 = 92.5, and the clamp rounds the
    // FINAL health value (not the delta) half-away-from-zero -> 93.
    CHECK(diff2.currentHitPoints.get() == 93);

    // A non-player-class instigator never gets the difficulty scalar, even
    // at the SAME hard difficulty level.
    es.objectResolves().set("cd_diff_src_npc", true);
    es.playerClassChainResolves().set("cd_diff_src_npc", false);
    CharacterState& diffNpc = es.getOrCreateCharacter("cd_diff_npc");
    es.objectResolves().set("cd_diff_npc", true);
    diffNpc.damageImmuneDescriptorBit33.set(false);
    diffNpc.maxHitPoints.set(100);
    diffNpc.currentHitPoints.set(100);
    ok(host, gp, "character_damage('cd_diff_npc', 10, nil, 'cd_diff_src_npc')");
    CHECK(diffNpc.currentHitPoints.get() == 90); // 10 * 1.0 (resistance only, no difficulty scalar) = 10
    es.difficultyLevel() = savedDifficulty;

    // (d) Death threshold + the default ragdoll-activation branch: an
    // overwhelming hit clamps health to exactly 0 (never negative), and
    // the real engine's own zero-HP death trigger fires.
    CharacterState& lethal = es.getOrCreateCharacter("cd_dmg_lethal");
    es.objectResolves().set("cd_dmg_lethal", true);
    lethal.damageImmuneDescriptorBit33.set(false);
    lethal.maxHitPoints.set(50);
    lethal.currentHitPoints.set(50);
    ok(host, gp, "character_damage('cd_dmg_lethal', 1000)");
    CHECK(lethal.currentHitPoints.get() == 0);
    CHECK(lethal.lastDamageMagnitude == 50.0); // capped at the character's own old HP, not the raw 1000
    CHECK(lethal.isDeadHighConfidence);
    CHECK(lethal.deathTransitionCount == 1);
    CHECK(lethal.ragdollStateEnumCd2.get() == 1);  // CHOSEN sentinel - satisfies character_is_ragdolled's != 0 && != 6 test
    CHECK(lethal.deathRagdollActivateCount == 1);
    CHECK(lethal.lifeState.get() == 0);      // the ragdoll branch does NOT run the finalizer
    CHECK(!lethal.killFinalizedFlagE3.get());

    // (e) The non-ragdoll fallback (locally-owned player-class victim,
    // readiness check failed): no ragdoll call at all - the SAME finalizer
    // character_kill's own arg4=false branch uses.
    CharacterState& finalize = es.getOrCreateCharacter("cd_dmg_finalize");
    es.objectResolves().set("cd_dmg_finalize", true);
    finalize.damageImmuneDescriptorBit33.set(false);
    finalize.maxHitPoints.set(10);
    finalize.currentHitPoints.set(10);
    finalize.ragdollActivationEligible = false;
    es.playerClassChainResolves().set("cd_dmg_finalize", true);
    ok(host, gp, "character_damage('cd_dmg_finalize', 999)");
    CHECK(finalize.currentHitPoints.get() == 0);
    CHECK(finalize.deathTransitionCount == 1);
    CHECK(finalize.deathRagdollActivateCount == 0); // ragdoll never activated on this branch
    // CONFIRMED spawn default (fdcabd5): known immediately at construction,
    // 0 - this branch never touches it, so it stays at that default rather
    // than remaining OPEN.
    CHECK(finalize.ragdollStateEnumCd2.get() == 0);
    CHECK(finalize.lifeState.get() == 5);
    CHECK(finalize.killFinalizedFlagE3.get());
    CHECK(finalize.characterKillFinalizerCount == 1);

    // (f) The kill-eligibility gate (0x0096f990, OPEN real condition):
    // false blocks the death transition entirely - health is still forced
    // to 0, but nothing past that runs.
    CharacterState& gated = es.getOrCreateCharacter("cd_dmg_gated");
    es.objectResolves().set("cd_dmg_gated", true);
    gated.damageImmuneDescriptorBit33.set(false);
    gated.maxHitPoints.set(10);
    gated.currentHitPoints.set(10);
    gated.lethalHitKillEligible = false;
    ok(host, gp, "character_damage('cd_dmg_gated', 999)");
    CHECK(gated.currentHitPoints.get() == 0);
    CHECK(gated.isDeadHighConfidence);
    CHECK(gated.deathTransitionCount == 0);
    // CONFIRMED spawn default (fdcabd5) - never touched by this branch.
    CHECK(gated.ragdollStateEnumCd2.get() == 0);
    CHECK(gated.lifeState.get() == 0);
    CHECK(!gated.killFinalizedFlagE3.get());

    // (g) The one combination Sec15.3a leaves genuinely unspecified
    // (ragdoll-ineligible, NOT player-class): left unmodelled rather than
    // guessed - only the generic "HP reached zero"/death-transition
    // bookkeeping fires, neither named branch does.
    CharacterState& openGap = es.getOrCreateCharacter("cd_dmg_openish");
    es.objectResolves().set("cd_dmg_openish", true);
    openGap.damageImmuneDescriptorBit33.set(false);
    openGap.maxHitPoints.set(10);
    openGap.currentHitPoints.set(10);
    openGap.ragdollActivationEligible = false;
    es.playerClassChainResolves().set("cd_dmg_openish", false);
    ok(host, gp, "character_damage('cd_dmg_openish', 999)");
    CHECK(openGap.currentHitPoints.get() == 0);
    CHECK(openGap.deathTransitionCount == 1);
    CHECK(openGap.deathRagdollActivateCount == 0);
    // CONFIRMED spawn default (fdcabd5) - never touched by this branch.
    CHECK(openGap.ragdollStateEnumCd2.get() == 0);
    CHECK(openGap.lifeState.get() == 0);
    CHECK(!openGap.killFinalizedFlagE3.get());

    // (h) include/sr3luahost/character_damage_sink.h's EngineStateLethalDamageSink -
    // the concrete adapter standing in for sr3weapons::IWeaponDamageSink
    // until feature/hitscan-fire-dispatch merges (see that header's own
    // top doc comment). Exercised directly against EngineState, not
    // through Lua.
    EngineStateLethalDamageSink sink(es);
    CharacterState& sinkT = es.getOrCreateCharacter("cd_sink_t");
    es.objectResolves().set("cd_sink_t", true);
    sinkT.maxHitPoints.set(30);
    sinkT.currentHitPoints.set(30);
    sink.applyLethalHit("cd_sink_t");
    CHECK(sink.callCount == 1 && sink.lastTarget == "cd_sink_t");
    CHECK(sinkT.currentHitPoints.get() == 0);
    CHECK(sinkT.ragdollStateEnumCd2.get() == 1);
    CHECK(sinkT.deathRagdollActivateCount == 1);
    // A target with no known health at all: applyGuaranteedLethalHit() must
    // not throw (unlike applyCharacterDamage(), it does not require
    // maxHitPoints/currentHitPoints to be known - see its own doc comment).
    CharacterState& sinkNoHp = es.getOrCreateCharacter("cd_sink_nohp");
    sink.applyLethalHit("cd_sink_nohp");
    CHECK(sink.callCount == 2 && sink.lastTarget == "cd_sink_nohp");
    CHECK(!sinkNoHp.maxHitPoints.known() && !sinkNoHp.currentHitPoints.known());
    CHECK(sinkNoHp.ragdollStateEnumCd2.get() == 1);
    CHECK(sinkNoHp.deathRagdollActivateCount == 1);

    // Non-authoritative: the opcode-0x41 record.
    cd.controlLockGatePasses.set(false);
    es.objectResolves().set("cd_src", true);
    es.objectResolves().set("cd_nosrc", false);
    ok(host, gp, "character_damage('cd_t', 12.75, nil, 'cd_src')");
    CHECK(cd.characterDamageReplicateCount == 1);
    CHECK(cd.lastReplicatedDamage->amount == 12.75f);
    CHECK(cd.lastReplicatedDamage->flags == 0x300u); // 0x100 | 0x200 (arg 3 default TRUE)
    CHECK(cd.lastReplicatedDamage->sourceName == "cd_src");
    ok(host, gp, "character_damage('cd_t', 0.1, false, 'cd_nosrc')");
    CHECK(cd.lastReplicatedDamage->flags == 0x100u);
    CHECK(cd.lastReplicatedDamage->sourceName.empty()); // unresolved source -> null, call proceeds
    CHECK(cd.lastReplicatedDamage->amount == 0.1f);     // single precision, not rounded
    ok(host, gp, "character_damage('cd_t', 1e300)");
    CHECK(std::isinf(cd.lastReplicatedDamage->amount) && cd.lastReplicatedDamage->amount > 0);
    ok(host, gp, "character_damage('cd_t', -1e300)");
    CHECK(std::isinf(cd.lastReplicatedDamage->amount) && cd.lastReplicatedDamage->amount < 0);
    CHECK(cd.characterDamageReplicateCount == 4);

    // ==================================================================
    // Sec20.9 qte_start (+ qte_cleanup, Sec15.6)
    // ==================================================================
    es.objectResolves().set("qs_unres", false);
    ok(host, gp, "qte_start('qs_unres', 'qte_def', 'cb')");
    CHECK(!es.hasCharacter("qs_unres") && es.qteActiveSlotCount() == 0);

    es.objectResolves().set("qs_c", true);
    es.objectResolves().set("qs_anchor", true);
    es.objectResolves().set("qs_noanchor", false);
    ok(host, gp, "qte_start('qs_c', 'm07_qte', 'qte_done_cb', 'qs_anchor', true, 'qs_noanchor')");
    CharacterState& qs = es.getOrCreateCharacter("qs_c");
    CHECK(qs.qteResultHookCallback == "qte_done_cb");
    CHECK(es.qteActiveSlotCount() == 1);
    const QteSlotEntry& e0 = es.qteActiveSlots()[0];
    CHECK(e0.characterName == "qs_c");
    CHECK(e0.definitionHash == sr3save::nameHash("m07_qte"));
    CHECK(e0.definitionName == "m07_qte");
    CHECK(e0.anchor1Name == "qs_anchor");
    CHECK(e0.anchor2Name.empty()); // unresolved -> the global default pair
    CHECK(e0.flag == true);
    CHECK(e0.linkedHudSlotId == -1);
    // Same character again: its one slot is overwritten, not duplicated.
    ok(host, gp, "qte_start('qs_c', 'other_qte', '')");
    CHECK(es.qteActiveSlotCount() == 1);
    CHECK(es.qteActiveSlots()[0].definitionHash == sr3save::nameHash("other_qte"));
    CHECK(es.qteActiveSlots()[0].anchor1Name.empty() && es.qteActiveSlots()[0].flag == false);
    CHECK(qs.qteResultHookCallback.empty()); // "" clears the slot-0x18 hook
    // An unknown anchor name is OPEN (resolver map), not silently defaulted.
    fails(host, gp, "qte_start('qs_c', 'other_qte', 'cb', 'never_registered_anchor')", "never_registered_anchor");
    // A second character takes its own slot; qte_cleanup tears both down
    // together with any test-registered entry.
    es.objectResolves().set("qs_d", true);
    ok(host, gp, "qte_start('qs_d', 'm07_qte', 'cb2')");
    es.registerActiveQteSlotForTesting(-1);
    CHECK(es.qteActiveSlotCount() == 3);
    ok(host, gp, "qte_cleanup()");
    CHECK(es.qteActiveSlotCount() == 0);
    // Non-authoritative: opcode-0x59 record only, no local slot.
    qs.controlLockGatePasses.set(false);
    ok(host, gp, "qte_start('qs_c', 'm07_qte', 'cb3')");
    CHECK(qs.qteStartReplicateCount == 1);
    CHECK(es.qteActiveSlotCount() == 0);
    CHECK(qs.qteResultHookCallback == "cb3"); // the hook registration precedes the gate

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_wave2_w21_test: all checks passed\n";
    return 0;
}
