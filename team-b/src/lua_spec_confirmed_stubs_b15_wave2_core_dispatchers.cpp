// lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp
//
// Wave 2, batch W2-1 (2026-10-08): "core dispatchers", 13 natives with one
// dedicated owner - the largest request-submitting entries of spec-lua-api-
// behaviour.md Sec17-26 plus the Sec38.1 "enter" family that shares their
// request machinery:
//
//   vehicle_exit_do              Sec21.11 (+ spec-player-runtime.md Sec6.2-6.4)  COMPLEX
//   vehicle_exit_check_done      Sec24.9  (+ spec-player-runtime.md Sec6.4.3)
//   vehicle_exit_guardian_angel  Sec38.1  (+ spec-player-runtime.md Sec6.3.3)
//   vehicle_enter_do             Sec38.1  (+ spec-player-runtime.md Sec7.4)
//   vehicle_enter_guardian_angel Sec38.1
//   vehicle_enter_group_do       Sec38.1
//   vehicle_enter_group_check_done Sec38.1
//   vehicle_enter_check_done     Sec22.3                                          COMPLEX (size)
//   move_to_do                   Sec21.22                                         COMPLEX
//   vehicle_pathfind_navmesh_do  Sec22.1                                          COMPLEX
//   teleport_to_object           Sec22.4  (+ Sec4.3/Sec35.3/Sec35.4)              COMPLEX
//   character_damage             Sec20.32                                         COMPLEX
//   qte_start                    Sec20.9  (+ Sec15.6)                             COMPLEX
//
// A NEW file (b15 - b10/b13 are reserved/taken by other in-flight work) per
// the refactor's own reviewer guidance: never retrofitted into b01-b14. It
// plugs into lua_spec_confirmed_stubs_registry.cpp's append/register chains
// exactly as b08/b09/b11/b12/b14 do.
//
// REUSED, NOT RE-DERIVED (each checked in engine_state.h before writing):
//   - objectResolves() (Sec29's one flat name map) for every resolver chain
//     below (0x00a281a0/0x00a280c0/0x00a281e0/0x005982e0/0x00734e90/...),
//     playerClassChainResolves() for the class-row +0xa bit 0x2 "player
//     class" test (the SAME stand-in teleport, Sec4.3, already uses for it),
//     and hasCharacter() for the "vehicle resolver is type-gated, so a
//     character's name falls through to its own vehicle" order (the SAME
//     CHOSEN resolution-order idiom b14's vehicle_stop_do/teleport_vehicle_
//     to_object use).
//   - CharacterState::stateEnum (+0x16d4: 0 none / 1 entering / 2 exiting /
//     3 seated, spec-player-runtime.md Sec7.8) for every 0x009b9160 (==3),
//     0x009b9230 (==2) and 0x009b91e0 ({1,2}) predicate; currentVehicleName
//     (+0x16c0/+0x16c4) for every character-fallback vehicle lookup;
//     notDeadForActionPlay() (lifeState +0xcc8 / +0xe3 bit 0 - the exact
//     0x0096f4f0 predicate, Sec3.5/Sec22.3) for every "dead/flagged" test.
//   - CharacterState::independentFollower (+0x1e01 bit 0, Sec7.2) for the
//     Sec38.1 "enter flips the follower flag" side effect, and
//     EngineState::party().membersByLeader (party_add_do, Sec32.4) as the
//     "is following a leader" test (HIGH CONFIDENCE - see
//     isFollowingALeader() below).
//   - CharacterState::actionDelayByIndex (ai_set_action_delay, Sec7.30) for
//     the 3000 ms back-off on CAE actions 37/38/39 ("follow to car", "follow
//     other car", "follow carjack" - EngineState::lookupCaeActionIndex()'s
//     own 70-entry table, Sec30.4): both writers go through the SAME
//     per-action setter 0x004f4ae0(human, action, ms), which arms repeat
//     timer +0x320 + 4*a (spec-ai-runtime.md Sec11.6).
//   - CharacterState::scriptControlledMode/statusByte28ac bit 0x2 for the
//     generic script-mode setter 0x009e3200 (player_script_controlled_clear,
//     Sec14.29, b11 - mode 0 = clear, gated on the same 0x008addb0 single
//     gate, controlLockGatePasses).
//   - EngineState::allocateScriptRequestId()/releaseScriptRequestId() (b14's
//     model of the shared allocator 0x006f62b0) for kind 2 (move_to_do,
//     vehicle_pathfind_navmesh_do) and kind 4 (teleport_to_object). NOT
//     teleport's own teleportScriptedRequestCount()/teleportRequests() -
//     those stay teleport's (no field of theirs is touched here).
//   - The Sec35.2 scripted-request POOL (teleport_check_done/move_to_check_
//     done/vehicle_pathfind_check_done) is deliberately left on Sec35.6's own
//     endorsed "minimum correct model" (populated only by tests), exactly as
//     teleport (Sec4.3) left it: a request this host marks pending in that
//     pool would need a completion this host does not model (arrival,
//     mode-8 exit, ...), so the poll would answer "pending" forever - a
//     fabricated hang, not a faithful model.
//   - QteSlotEntry/qteActiveSlots_ (qte_cleanup, Sec15.6, b12), extended in
//     place for qte_start - see QteSlotEntry's own doc comment.
//   - VehicleState::seat0Occupied (Sec46.4/b14) for vehicle_pathfind_
//     navmesh_do's index-0 occupant gate; VehicleState::forceFlagGatePasses
//     for its vehicle-side authority split (the SAME gate-family stand-in b14
//     uses for a vehicle's own authority).
//   - sr3save::nameHash (CONFIRMED match to 0x00d9e8b0, see b02) for
//     qte_start's QTE-definition hash.
//   - 2026-10-09, spec-player-runtime.md Sec7.4a (the eligibility gate
//     0x00b0ca60, vehicleEnterEligibilityGate() below): CharacterState::
//     stateEnum (+0x16d4, reused a THIRD way - seated/mid-enter-exit),
//     ragdollStateEnumCd2 (+0xcd2), lifeState/killFinalizedFlagE3 (+0xcc8/
//     +0xe3), movementStateByteCcd (+0xccd) and stateCodeByte501 (+0x501,
//     reused a SECOND way alongside Sec22.3's own vehicle_enter_check_done
//     use) are all REUSED, not duplicated; playerClassChainResolves() is
//     reused a SECOND time too, for the class-row's sibling +0xa bit 0x04
//     ("has the standard AI-state block at all") - see that method's own
//     doc comment in engine_state.h. Three genuinely new per-character
//     facts (the busy/pending pair, the generic interaction-lock helper,
//     and the ai_force_flags cant_enter_vehicle bit) and one new reused-by-
//     VALUE-only field (the AI sub-state byte +0x500) are documented at
//     their own declarations in character_state.h, right above
//     CharacterState::vehicleEnterSubmitAccepted.
//
// THE OCCUPANCY STATE MACHINE IS NOT DRIVEN FROM HERE (CHOSEN, stated once
// for all seven vehicle natives; KNOWN DIVERGENCE, not spec-accurate - see
// below) - the enter submitter 0x00af22a0 and the exit request 0x00af3cd0
// hand a request to the 48-record occupancy registry (spec-player-runtime.md
// Sec7), whose phase-boundary writes (+0x16d4 = 1 at enter state 0, 2 at exit
// state 0x14, 3 at the ride state, 0 at release) happen in on-entry handlers.
// This host has no world tick for that registry (sr3player's VehicleOccupancy
// is not wired to the Lua host anywhere), so these natives RECORD the request
// they build (CharacterState::lastVehicleEnterRequest/lastVehicleExitRequest)
// and reproduce every Lua-visible result the spec gives, but do not write
// +0x16d4/+0x16c0 themselves.
//
// CORRECTION (post-merge review): the justification above is WRONG. Per
// spec-player-runtime.md Sec7.4 step 11/Sec7.5/Sec7.8, the FIRST enter
// write (+0x16d4 = 1, +0x16c0/+0x16c4 = vehicle) happens synchronously
// inside submitter 0x00af22a0 itself, not in a later-ticked on-entry
// handler; likewise Sec6.3.1 step 9/Sec6.6.1 step 1 put the first exit
// write (+0x16d4 = 2) synchronously inside 0x00af3cd0 itself. Both are
// reachable with zero world-tick dependency - this is NOT the same
// "pool stays unpopulated, give a definite done answer" shape that
// move_to_do/vehicle_pathfind_navmesh_do/teleport_to_object correctly
// share with teleport's own Sec35.6-endorsed minimal model; it is a
// different, new shortcut. Concretely, not writing it made this host
// diverge from the real game: a second vehicle_exit_do/_guardian_angel
// call returned true again instead of false (or the cancel path), and
// vehicle_enter_check_done never left 1.0 once an enter was accepted.
//
// FIXED (2026-10-08, this pass, after independently re-confirming all four
// cited sections directly against spec-player-runtime.md): both writes are
// now gated exactly as this comment's own prior OPEN follow-up called for -
// on whether the submitter/exit-request actually accepts, not written
// unconditionally on every call.
//   - Enter: stub_vehicle_enter_do's instant and named-seat paths, and
//     stub_vehicle_enter_guardian_angel, write +0x16d4 = 1 and
//     +0x16c0/+0x16c4 = the resolved vehicle only when
//     vehicleEnterSubmitAccepted / vehicleEnterSubmitAcceptedForced reads
//     true (both remain OpenValue<bool> - an unset/OPEN fact still throws
//     via their own .get(), exactly as before, so this cannot fire on every
//     call unconditionally). stub_vehicle_enter_group_do's instant
//     per-member branch does the same, per accepted member. The AI-order
//     path (vehicleEnterAiOrderStarts) deliberately does NOT get this write
//     - Sec38.1 confirms it never reaches the submitter at all (it starts
//     action 0x31 instead), so - like release below - it has no in-scope
//     synchronous trigger.
//   - Exit: stub_vehicle_exit_do's and stub_vehicle_exit_guardian_angel's
//     seated branches write +0x16d4 = 2 unconditionally WITHIN that branch
//     - not newly gated on a fresh "did 0x00af3cd0 accept" fact, because
//     Sec21.11's own already-CONFIRMED finding is that these two natives'
//     Lua-visible effects were never gated on 0x00af3cd0's internal
//     refusal checks (Sec6.3.1 steps 1-7) to begin with ("0x00af3cd0's own
//     bool result is NOT consulted... pushes true regardless"); the only
//     gate that exists, or ever existed, here is each native's own seated
//     test (+0x16d4 == 3), already present before this fix.
// Release (+0x16d4 = 0) was, at this point in the file's history, still
// correctly left unwritten for every path: nothing in scope yet reached it.
// SUPERSEDED 2026-10-10 for stub_vehicle_exit_do's own instant (arg 2 true)
// case only - see that function's own FIXED note, right after its
// `c.stateEnum.set(2);` line, for the full citation (Sec6.6.1/Sec6.7.2/
// Sec6.9/Sec6.11/Sec6.13 point 5) of why the instant path's own release is
// now completed synchronously too, the same way the 2026-10-09 enter-side
// fix below completed ITS OWN instant path past its first synchronous write.
// stub_vehicle_exit_guardian_angel's own request is never instant (no Lua
// argument sets exit-request +0x30 bit 0 for it), so its animated-only path
// is unaffected and still correctly stops at +0x16d4 = 2.
//
// FIXED (2026-10-09, mission-drive m05/m09 deadlock, forward-traced from
// b706fbb's own identical fix to vehicle_enter_group_do's instant path):
// the +0x16d4 = 1 write named above (line 125-128, for stub_vehicle_enter_do's
// instant/named-seat paths and stub_vehicle_enter_guardian_angel) was only
// ever the FIRST of two in-scope synchronous writes, not the whole Sec7.4b
// no-contention cascade - the Lua-side `vehicle_enter_teleport` wrapper
// (game_lib.lua, `vehicle_enter_do(...)` then `repeat thread_yield() until
// vehicle_enter_check_done(...)`) spun for its entire mission-drive tick
// budget on m05/m09 because stub_vehicle_enter_check_done's own seated test
// (Sec22.3, +0x16d4 == 3) could never observe a cleared member - nothing in
// this host ever advanced past state 1 ("entering") for these two natives.
// Per spec-player-runtime.md Sec7.4b (seating commit 0x00afbd90, state 9:
// human+0x16d4 = 3), for the INSTANT, no-contention case the real submitter's
// own on-entry cascade runs synchronously with zero world-tick dependency -
// the same rule b706fbb already applied to vehicle_enter_group_do's shared
// attemptGroupInstantEnter() helper. vehicleEnterSeatPickGate() above (steps
// 6/7) already models the only contention this host can compute (tank-team
// conflict, specific-seat contest) for BOTH the singular and group paths via
// the same computeVehicleEnterSubmitAccepted() - so an accepted result here
// means exactly the same "no contest this host can represent" case the group
// fix already relies on, not a new, weaker guarantee. stub_vehicle_enter_do's
// instant AND named-seat paths, and stub_vehicle_enter_guardian_angel (same
// shared shape, "forced" only changes which refusals are skipped before
// acceptance, not what happens after), now write +0x16d4 = 3 directly on an
// accepted instant placement, completing the write instead of stopping short
// of it - reusing the existing synchronous-write call sites, not a new
// mechanism. Measured delta and updated tests: see this task's own commit/
// handoff.
//
// HARDENING POLICY: none of these 13 entries carries a spec-flagged memory-
// safety defect that this host could reach (checked: the null-human
// dereference in wrapper 0x009b9250 and the missing -1 slot guards in
// 0x00af2460/0x00af24d0 - spec-player-runtime.md Sec6.4 - are not on any path
// these natives take: vehicle_exit_do calls 0x00af3e10 directly, and
// 0x00af3e10 returns before 0x00af24d0 when the slot is missing). One
// HOST-SAFETY choice (not a binary-bug divergence): the two table-taking
// natives read their name tables with raw access (readCharacterNameArrayRaw
// below), so a hostile __index metamethod can never raise a Lua error
// (a longjmp) through this file's C++ frames - see common.h's "NO LUA ERROR
// MAY CROSS A C++ FRAME" note.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include <cfloat>
#include <cmath>
#include <limits>

namespace sr3luahost {
namespace {

// =======================================================================
// Shared helpers (this file only)
// =======================================================================

} // namespace
namespace stubs_b15 {
// 0x0083dff0's table shape (Sec30.5/Sec38.1 - "t.n if numeric, else counted
// via lua_next"), read exactly like common.h's readCharacterNameArray but
// with lua_rawget/lua_rawgeti: CHOSEN host safety (see this file's top
// comment) - identical result for every plain table a real script passes.
std::vector<std::string> readCharacterNameArrayRaw(lua_State* L, int idx) {
    std::vector<std::string> out;
    if (idx > lua_gettop(L) || lua_type(L, idx) != LUA_TTABLE) return out;
    lua_pushstring(L, "n");
    lua_rawget(L, idx);
    bool hasN = lua_type(L, -1) == LUA_TNUMBER;
    double n = hasN ? lua_tonumber(L, -1) : 0.0;
    lua_pop(L, 1);
    int count = (hasN && n >= 0.0 && n < 1000000.0) ? static_cast<int>(n) : -1;
    for (int i = 1; count < 0 || i <= count; ++i) {
        lua_rawgeti(L, idx, i);
        if (lua_type(L, -1) == LUA_TNIL) { lua_pop(L, 1); break; }
        out.push_back(argString(L, -1));
        lua_pop(L, 1);
        if (i > 100000) break; // safety bound, not a spec fact (same as readCharacterNameArray)
    }
    return out;
}
} // namespace stubs_b15
namespace {
using stubs_b15::readCharacterNameArrayRaw;

} // namespace
namespace stubs_b15 {
// The enter family's vehicle resolution (Sec38.1): "Resolves the vehicle
// directly, or - if that fails - as a character's own current vehicle." The
// vehicle resolver 0x00a281e0 is type-gated, so in this host's one flat name
// map a name already known as a CHARACTER takes the fallback (its own
// +0x16c0/+0x16c4 link, currentVehicleName) and every other resolved name is
// the vehicle itself - the SAME CHOSEN partition b14's vehicle_stop_do uses.
bool resolveEnterVehicle(EngineState* st, const std::string& name, std::string& vehicleOut) {
    if (st->hasCharacter(name)) {
        if (!st->objectResolves().get(name)) return false; // OPEN until set
        std::string v = st->getOrCreateCharacter(name).currentVehicleName.get(); // OPEN until set
        if (v.empty()) return false; // no current vehicle -> the fallback fails too
        vehicleOut = v;
        return true;
    }
    if (!st->objectResolves().get(name)) return false; // OPEN until set
    vehicleOut = name;
    return true;
}
} // namespace stubs_b15
namespace {
using stubs_b15::resolveEnterVehicle;

// Sec38.1: "entering a vehicle while following a leader silently flips the
// character's own independent-follower flag (bit 0 of +0x1e01) and never
// clears it again." Sec38.1 does not name the field the "following a leader"
// test reads. HIGH CONFIDENCE stand-in: the leader/follower link this host
// already models, party_add_do's membersByLeader (Sec32.4) - the only
// Lua-reachable "X follows leader Y" relation in scope. A character on no
// leader's list is "not following" (no flip). "Flips ... never clears it
// again" is read as a set to true (independent) - HIGH CONFIDENCE, the only
// reading in which "never clears it again" is meaningful.
bool isFollowingALeader(EngineState* st, const std::string& character) {
    for (const auto& kv : st->party().membersByLeader) {
        for (const auto& m : kv.second) {
            if (m == character) return true;
        }
    }
    return false;
}

} // namespace
namespace stubs_b15 {
void flipIndependentFollowerIfFollowing(EngineState* st, const std::string& character) {
    if (isFollowingALeader(st, character)) {
        // Direct write: Sec38.1 names no gate for this hidden side effect
        // (unlike follower_make_independent's own double-gate setter, Sec7.2).
        st->getOrCreateCharacter(character).independentFollower.set(true);
    }
}
} // namespace stubs_b15
namespace {
using stubs_b15::flipIndependentFollowerIfFollowing;

// The 3000 ms back-off on CAE actions 0x25/0x26/0x27 (37/38/39) that
// vehicle_exit_do (Sec21.11: "three fixed 3000ms timed events, codes 0x25/
// 0x26/0x27, via 0x004f4ae0") and vehicle_exit_guardian_angel (Sec38.1: "a
// 3-second cooldown to three AI actions (ids 37-39 of the existing 70-entry
// CAE action table)") both post. CONFIRMED (Sec21.11) each code individually
// gates through the single gate 0x008addb0 (variant 2) - controlLockGatePasses
// - before arming the repeat timer; this host stores the delay in ms in
// actionDelayByIndex, the SAME field ai_set_action_delay's own 0x004f4ae0 call
// writes. Without authority the timer is not armed locally; the request goes
// to the owner (0x004f4830, spec-ai-runtime.md Sec11.6).
void applyVehicleExitFollowBackoff(CharacterState& c, const std::string& name) {
    for (int32_t code : {0x25, 0x26, 0x27}) {
        if (c.controlLockGatePasses.get()) {
            c.actionDelayByIndex[code] = 3000.0;
        } else {
            EngineState::replicateStateChange("vehicle_exit_action_backoff", name);
        }
    }
}

// =======================================================================
// Exit family
// =======================================================================

// ---------------------------------------------------------------------
// vehicle_exit_do (Sec21.11, 0x00a67800; exit request record and cancel
// family per spec-player-runtime.md Sec6.2/Sec6.3/Sec6.4). Arguments: arg 1
// mandatory string (occupant, generic/#PLAYER# chain 0x00a281a0); arg 2
// boolean, NO nil-gate (default false); arg 3/arg 4 optional booleans
// (nil-gated, default false); arg 5 optional string (nil-gated, default
// absent) - an animation-state name, through the shared table 0x004bf810
// (EngineState::lookupAnimationStateId(), -1 when absent/unregistered).
//
// Return - CONFIRMED per-branch arity asymmetry (raw disassembly, Sec21.11):
//   - resolved AND seated (+0x16d4 == 3, 0x009b9160): builds the standard exit
//     request (constructor 0x004dccd0; +0x30 bit 0 = arg 2 "instant", +0x30
//     bit 4 = arg 4, +0x2c = arg 5's id - CONFIRMED arg 3 is parsed but never
//     written, spec-player-runtime.md Sec6.2), hands it to 0x00af3cd0 (its
//     own bool result is NOT consulted), forwards the occupant's identity
//     through 0x009b9130/0x004dcf00/0x00a7bdc0 (no Lua-visible effect, not
//     modelled), posts the three 3000 ms back-offs (applyVehicleExitFollow
//     Backoff), and pushes TRUE - 1 value;
//   - not seated, arg 2 set, and entering-or-exiting (+0x16d4 in {1,2},
//     0x009b91e0): the cancel 0x00af3e10 - and returns ZERO Lua values (no
//     lua_push* at all, CONFIRMED);
//   - otherwise (unresolved, or neither): pushes FALSE - 1 value.
//
// OPEN (two spec texts disagree - recorded, not resolved): Sec21.11 gives the
// cancel call as 0x00af3e10(occupant, arg3, arg4); spec-player-runtime.md
// Sec6.4.1 lists vehicle_exit_do's call site as (human, 1, 0), and Sec6.2's
// "arg 3 ... has no effect" agrees with the literal reading. 0x00af3e10's
// arguments are (replicate, keep_in_place); neither has a Lua-visible effect
// in this host (the phase dispatch it performs belongs to the occupancy
// machine, not driven here - see this file's top comment), so only the
// call itself is counted (CharacterState::vehicleExitCancelCount) and
// neither argument reading is baked in.
// ---------------------------------------------------------------------
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_vehicle_exit_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_exit_do", upStateTag(L));
    std::string name = argString(L, 1);
    bool arg2 = lua_toboolean(L, 2) != 0;          // CONFIRMED: no nil-gate - "instant" AND the cancel selector
    (void)optionalBoolDefault(L, 3, false);        // CONFIRMED parsed, never written (spec-player-runtime.md Sec6.2)
    bool arg4 = optionalBoolDefault(L, 4, false);  // -> exit request +0x30 bit 4
    bool hasAnim = lua_gettop(L) >= 5 && lua_type(L, 5) != LUA_TNIL;
    std::string animName = hasAnim ? argString(L, 5) : std::string();

    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushboolean(L, 0);
        return 1;
    }
    name = resolvedName;
    CharacterState& c = st->getOrCreateCharacter(name);
    int32_t phase = c.stateEnum.get(); // OPEN until set (+0x16d4)

    if (phase == 3) { // 0x009b9160: seated
        CharacterState::VehicleExitRequestRecord req; // 0x004dccd0 defaults
        req.instant = arg2;
        req.flag30Bit4 = arg4;
        req.animationOverrideId = hasAnim ? st->lookupAnimationStateId(animName) : -1;
        c.lastVehicleExitRequest = req;
        ++c.vehicleExitRequestCount; // 0x00af3cd0(occupant, &req) - result not consulted (CONFIRMED: pushes true regardless)
        // FIXED (2026-10-08): CONFIRMED spec-player-runtime.md Sec6.3.1 step
        // 9 ("Set-exit(record, slot, 0x14, send=1, from_network=0) -> true")
        // + Sec6.6.1 step 1 ("Human +0x16d4 = 2") - Set-exit's own dispatch
        // (Sec7.5: "jump through 0x00b05ac4") reaches state 0x14's on-entry
        // handler SYNCHRONOUSLY, inside 0x00af3cd0's own call, with zero
        // world-tick dependency. Unconditional within this branch (not a
        // new accept-gated fact): Sec21.11 already confirms this native's
        // Lua-visible effects were never gated on 0x00af3cd0's own internal
        // refusal checks (Sec6.3.1 steps 1-7) - "result not consulted...
        // pushes true regardless" - so the only gate is this branch's own
        // seated test above.
        c.stateEnum.set(2);
        // FIXED (2026-10-10, mission-drive m05 deadlock, forward-traced from
        // a10a9ba/b706fbb's own identical completion of the ENTER side's
        // synchronous-commit cascade): the above +0x16d4 = 2 write alone
        // only reaches state 0x14 ("requested") - the structural mirror of
        // the pre-fix enter bug, where stopping at the FIRST synchronous
        // write left vehicle_enter_check_done's own seated test unable to
        // ever observe completion. Read directly against spec-player-
        // runtime.md Sec6 (U6.6) for THIS side, arg 2 ("instant") true:
        //   - Sec6.6.1 step 5's "skip the wait" condition needs exit-request
        //     +0x32 bit 2 set, which nothing in this native's own argument
        //     list (or Sec6.2's field table) ever sets - so even an instant
        //     request genuinely needs the owning tick's per-frame handler
        //     (0x00b05b00, Sec6.6.2: "a local request normally moves to
        //     0x15 on the next tick") to leave state 0x14 in the real game.
        //   - Once state 0x15 IS reached, though, the instant route cascades
        //     synchronously within that SAME tick's call chain with no
        //     further tick dependency: 0x15's own on-entry handler dispatches
        //     straight to 0x17 or 0x19 (Sec6.7.2 "Route... instant path"),
        //     and 0x17's on-entry calls set-exit(...,0x19,1,0) "at once"
        //     (Sec6.9 step 6) - the finaliser 0x19 then releases the slot,
        //     writing +0x16d4 = 0 (Sec6.11 step 15/Sec7.8).
        //   - Sec6.13 point 5 names exactly this as a port's own sanctioned
        //     simplification: "A port that stubs U6.7 can send every exit
        //     through the instant path... and still reproduce the observable
        //     end state" - this host has no per-tick occupancy dispatcher at
        //     all (this file's own top comment), so - precisely as the
        //     enter-side fix already established for ITS own no-contention
        //     instant case ("the no-contention case is the only case it can
        //     ever represent" per b706fbb) - the one-tick wait at 0x14 is not
        //     separately representable here either, and completing straight
        //     to the real terminal/observable state on an instant request is
        //     the faithful choice, not a weaker one.
        // So: only for arg 2 (instant) true, complete past the 0x14 write
        // above directly to the real release values - +0x16d4 = 0 (Sec6.11
        // step 15/Sec7.8's own `0x00b0880b`) and the vehicle link +0x16c0/
        // +0x16c4 = null (Sec7.8's `0x00b0881c`; this host's own stand-in,
        // CharacterState::currentVehicleName, already treats an EMPTY string
        // as "no current vehicle" - see resolveEnterVehicle()'s own check).
        // The non-instant (animated) path is explicitly NOT completed here:
        // Sec6.7.2's animated route goes to 0x1d/0x1f and plays a real clip
        // over many real frames before ever reaching the finaliser, the same
        // "cannot be synchronously represented" shape as the enter side's own
        // AI-order path (never completed either) - leaving +0x16d4 = 2 is the
        // honest, not-invented result for that branch.
        // UPDATED 2026-10-12 (spec e8dc04d, Sec24.9a, m05 stall): Lua's
        // vehicle_exit wrapper always passes arg 2 = false and then loops
        // `while not vehicle_exit_check_done` - true only once +0x16d4 is 0
        // or 1. With no per-tick occupancy dispatcher in this host, leaving
        // +0x16d4 = 2 on the animated path wedged the thread forever
        // (m05_jump_local). Sec24.9a's "Minimum" port recipe: treat the
        // request as instant (3->2->0, occupancy cleared) within the call,
        // for both instant and animated requests.
        // Spec V.1 exit commit (state 0x19 "out"): the hooks fire BEFORE the slot release below zeroes +0x16d4.
        if (c.currentVehicleName.known() && !c.currentVehicleName.get().empty())
            st->commitVehicleExit(name, c.currentVehicleName.get());
        c.stateEnum.set(0);
        c.currentVehicleName.set(std::string());
        applyVehicleExitFollowBackoff(c, name);
        lua_pushboolean(L, 1);
        return 1;
    }
    if (arg2 && (phase == 1 || phase == 2)) { // 0x009b91e0: entering or exiting
        ++c.vehicleExitCancelCount; // 0x00af3e10 - see the OPEN note above on its two arguments
        return 0;                   // CONFIRMED: no lua_push* on this path - 0 Lua values
    }
    lua_pushboolean(L, 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_exit_check_done (Sec24.9, 0x00a668a0). Arguments: 1 mandatory
// string (character, 0x00a281a0). Return: exactly 1 boolean. TRUE ("done")
// if the character fails to resolve, OR is dead/flagged (0x0096f4f0), OR is
// alive and neither predicate holds; FALSE only while alive and EITHER
// 0x009b9160 OR 0x009b9230 holds. Sec24.9 left both predicates' roles OPEN;
// spec-player-runtime.md now settles them from clean spec text: 0x009b9160 is
// +0x16d4 == 3 (seated, Sec7.8) and 0x009b9230 is +0x16d4 == 2 (exiting,
// Sec6.4.3: "each first tests 0x009b9230, i.e. +0x16d4 = 2 (exiting)"). So
// this is "still seated or still getting out" - the same shape vehicle_exit_
// group_check_done (Sec30.5, b03) already reads per member.
// ---------------------------------------------------------------------
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_vehicle_exit_check_done(lua_State* L) {
    logCall(L, upLog(L), "vehicle_exit_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool done = true;
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        CharacterState& c = st->getOrCreateCharacter(resolvedName);
        if (c.notDeadForActionPlay()) { // 0x0096f4f0 false
            int32_t phase = c.stateEnum.get(); // OPEN until set
            if (phase == 3 || phase == 2) done = false;
        }
    }
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_exit_guardian_angel (Sec38.1, 0x00a66c70). Arguments: 1 string
// (character). Return: 1 boolean - CONFIRMED "true means 'was seated,' not
// 'exit succeeded'": the exit request can be silently refused (e.g. the
// occupancy slot is mid-transition) while this still reports true. Body:
// unresolved -> false. Seated (0x009b9160, +0x16d4 == 3 - HIGH CONFIDENCE
// that "was seated" is this established predicate, Sec38.1 names no other) ->
// builds an exit request with +0x31 bit 4 set (spec-player-runtime.md
// Sec6.3.3: "+0x31 bit 4 (0x00a66c70)" - "0x15 leaves +0xec bit 0x100000 clear,
// and the finaliser does not restore the saved weapon", Sec6.2), submits it
// through 0x00af3cd0 (result not consulted), applies the 3-second back-off
// on CAE actions 37-39, returns true. HIGH CONFIDENCE that the back-off sits
// on the seated path only (Sec38.1's sentence follows the seated-return
// description; vehicle_exit_do's identical three-code post is inside its own
// seated branch, Sec21.11). Not seated -> false, nothing posted.
// ---------------------------------------------------------------------
int stub_vehicle_exit_guardian_angel(lua_State* L) {
    logCall(L, upLog(L), "vehicle_exit_guardian_angel", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(name);
    if (c.stateEnum.get() != 3) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState::VehicleExitRequestRecord req; // 0x004dccd0 defaults
    req.flag31Bit4 = true;
    c.lastVehicleExitRequest = req;
    ++c.vehicleExitRequestCount; // 0x00af3cd0 - may be refused; CONFIRMED still reports true
    // FIXED (2026-10-08): same synchronous Sec6.3.1 step 9/Sec6.6.1 step 1
    // +0x16d4 = 2 write as vehicle_exit_do's seated branch above (see its
    // own comment for the full citation) - this entry's own seated check
    // just above already guarantees phase == 3 at this point.
    c.stateEnum.set(2);
    applyVehicleExitFollowBackoff(c, name);
    lua_pushboolean(L, 1);
    return 1;
}

// =======================================================================
// Enter family (Sec38.1 + spec-player-runtime.md Sec7.4)
// =======================================================================

} // namespace
namespace stubs_b15 {
// ---------------------------------------------------------------------
// The eligibility gate 0x00b0ca60(character, forced, eligibility_bit)
// (spec-player-runtime.md Sec7.4a, traced in full 2026-10-09 - closes the
// long-standing mission-drive "enter submitter" blocker's eligibility half).
// Team A's own framing, confirmed exhaustively by full disassembly of the
// function's own body and all 8 direct callees: a PURE character-state
// check with NO vehicle or physics argument at all - every vehicle/seat/
// physics check belongs to the sibling functions Sec7.4 step 4 and
// Sec13.4-Sec13.6 already cover (U6.2, this host's unmodelled vehicle/seat
// pick), run only AFTER this gate passes. Plain boolean result, no
// secondary reason code - every refusal path and the one success path
// collapse to the same 0/1.
//
// `eligibilityBit` (request +0x24 bit 0, "passed to the eligibility
// predicate," Sec7.2) is accepted for signature fidelity only: Sec7.4a's
// own exhaustive "what it reads" list (every instruction in the body and
// all 8 direct callees, cross-checked against a whole-image byte scan) does
// not name this stack argument among the fields the gate's 9+1 branches
// actually consult, and no in-scope caller in this project's own request-
// building code ever sets enter-request +0x24 bit 0 (Sec38.1 names no Lua
// argument for it on vehicle_enter_do/vehicle_enter_guardian_angel/
// vehicle_enter_group_do). Honestly modelled as accepted-but-unread rather
// than silently dropped from the signature or guessed at.
//
// The 9 refusal paths, in the real function's own order (CONFIRMED by
// Sec7.4a's listing), each with where this project gets the fact it tests:
//   1. Seated (+0x16d4 == 3)                    - CharacterState::stateEnum (reused)
//   2. Mid enter/exit (+0x16d4 in {1,2})        - CharacterState::stateEnum (reused)
//   3. Busy/pending pair (+0x16d8/+0x16dc != 0) - vehicleEnterEligibilityBusyPendingNonZero (new, CHOSEN false)
//   4. Ragdolling (+0xcd2 not in {0,6})         - CharacterState::ragdollStateEnumCd2 (reused)
//   5. Down/dead (+0xcc8 in {5,6} or +0xe3&1)   - CharacterState::lifeState/killFinalizedFlagE3 (reused)
//   6a. NOT forced: interaction-lock helper     - genericInteractionLockGatePasses (new, genuinely OPEN)
//   6b. NOT forced: jump/dive (+0xccd == 4)     - CharacterState::movementStateByteCcd (reused)
//   6c. NOT forced: climb (+0xccd == 6)         - CharacterState::movementStateByteCcd (reused)
//   8a. Has-AI-state-block bit set, cant_enter_vehicle (+0x2ba bit 0x10) - cantEnterVehicleFlag2ba (new, CHOSEN false)
//   8b. Has-AI-state-block bit set, mode == Idle, sub-state in {0x10,0x11} - CharacterState::stateCodeByte501/aiSubStateByte500 (reused/new)
// Success: forced (unconditional); OR step 7's bit clear; OR step 8's chain
// falls through (not cant_enter_vehicle, mode != Idle, or mode == Idle with
// a sub-state outside {0x10,0x11}).
//
// Reads are ordered to match the real function's own short-circuiting
// exactly (e.g. aiSubStateByte500 is only ever touched when mode == Idle,
// never otherwise) - an OPEN field this specific run never needed to
// consult never throws, same discipline as every other computed predicate
// in this project.
bool vehicleEnterEligibilityGate(EngineState* st, const std::string& characterName, CharacterState& c,
                                  bool forced, bool eligibilityBit) {
    (void)eligibilityBit; // accepted for signature fidelity only - see this function's own doc comment

    // Steps 1/2: +0x16d4 (stateEnum) - seated, or already mid enter/exit of
    // some (other) vehicle. Unconditional, even when forced.
    int32_t phase = c.stateEnum.get();
    if (phase == 3) return false;                 // step 1: seated
    if (phase == 1 || phase == 2) return false;    // step 2: mid enter/exit

    // Step 3: +0x16d8/+0x16dc busy/pending pair. Unconditional, even when forced.
    if (c.vehicleEnterEligibilityBusyPendingNonZero) return false;

    // Step 4: ragdolling (+0xcd2 not in {0,6}). Unconditional, even when forced.
    int32_t ragdoll = c.ragdollStateEnumCd2.get();
    if (ragdoll != 0 && ragdoll != 6) return false;

    // Step 5: down or dead (+0xcc8 in {5,6}, or +0xe3 bit 0x01). Unconditional,
    // even when forced. NOTE: deliberately NOT CharacterState::notDeadForActionPlay()
    // (that predicate tests lifeState != 5 only, not {5,6}) - Sec7.4a's own
    // text is {5,6}, a different membership test on the SAME two real fields.
    int32_t life = c.lifeState.get();
    if (life == 5 || life == 6) return false;
    if (c.killFinalizedFlagE3.get()) return false;

    if (!forced) {
        // Step 6: only when NOT forced - the generic interaction-lock helper,
        // then jump/dive and climb pose.
        if (!c.genericInteractionLockGatePasses.get()) return false;
        int32_t move = c.movementStateByteCcd.get();
        if (move == 4) return false; // jump/dive
        if (move == 6) return false; // climb
    }

    // Step 8 tail, forced half: success unconditionally, skipping step 7
    // and the rest of step 8 entirely (Sec7.4a: "forced -> success
    // unconditionally" - step 7's own bit is never consulted for a forced
    // request, so playerClassChainResolves() is not even called here).
    if (forced) return true;

    // Step 7: class-capability-row filter (0x02cc9900 +0xa bit 0x04, "has
    // the standard AI-state block at all") - reused stand-in, see
    // EngineState::playerClassChainResolves()'s own doc comment for the
    // full citation of this reuse (the SAME pattern spec-lua-api-
    // behaviour.md's teleport, Sec4.3, already applies to the sibling bit
    // 0x02 "is-player" test on the same row).
    bool hasAiStateBlock = st->playerClassChainResolves(characterName);
    if (!hasAiStateBlock) return true; // step 8: bit clear -> success anyway

    // Step 8 tail, bit set: cant_enter_vehicle, then current AI mode/sub-state.
    if (c.cantEnterVehicleFlag2ba) return false;
    int32_t mode = c.stateCodeByte501.get();
    if (mode != CharacterState::kAiModeIdle) return true; // not Idle -> allow
    int32_t subState = c.aiSubStateByte500.get();         // only read when mode == Idle
    return subState != 0x10 && subState != 0x11;          // "observe vehicle world/vehicular accident"
}
} // namespace stubs_b15
namespace {
using stubs_b15::vehicleEnterEligibilityGate;

// ---------------------------------------------------------------------
// Sec7.4 step 4's real seat-pick validator, `0x00b0fdf0` (spec-player-
// runtime.md Sec13.4, CONFIRMED by full listing, ~616 insns) - replacing
// the CHOSEN-default-true fallback this function used to fall through to
// unconditionally (see the FIXED note below). This implements ONLY steps
// 6-7 of that function's 9-step gate order, per Team A's explicit 2026-10-09
// spec-sync instruction: those two steps are the ones m07's own retry-on-
// refusal loop (`m07_setup_pony_carts`) needs a real `false` for; the other
// seven steps (1-5, 8, 9's own obstruction/speed/angle machinery) stay
// CHOSEN-default-pass-through (never refuse) because this host has none of
// the data they read (vehicle descriptor flag bytes, physics-body state,
// seat-scoring helper 0x00b0eb90's own per-seat Seat_Info table, the
// obstruction gates' geometry queries) - each is a genuine, labelled gap,
// not a guess papered over as "done":
//   - step 1 (vehicle descriptor flags +0x1d7b, request +0x22 bit 0x80,
//     seat count >= 1): OPEN - no vehicle descriptor-flag/seat-count model;
//   - step 2 (seat-scoring gate 0x00b0eb90): OPEN - no Seat_Info table;
//   - step 3 (dead-driver/occupant-state exclusions, vehicle +0x16c0):
//     OPEN - no per-vehicle driver-occupant-state field;
//   - step 4 (vehicle category/speed gate on the driver seat): OPEN - no
//     vehicle speed/category data;
//   - step 5/9 (tank-type bias-tail, obstruction gates 0x00b0da10/
//     0x00b0dc20, entry-speed threshold): OPEN - no physics/geometry data.
// Verbatim source read directly from this worktree's own spec-player-
// runtime.md Sec13.4 (not the task's paraphrase) for steps 6/7 below.
//
// Step 6 (vehicle-wide team conflict, spec text verbatim): "(when step 5 is
// true) An 8-seat scan for a vehicle-wide team conflict. For each seat 0-7:
// find a claimed (0x00ab5070) or pending (0x00ab5150) occupant; if found
// and not dead (0x0096f4f0), compare that occupant's condition-class byte
// (+0x1ca8) against the requesting character's own via 0x004fa040. Any
// conflicting pair fails the whole function." Step 5 itself is "0x00ad3140
// (vehicle) ... (vehicle+0xbf4)+0x4c4 == 0xe - i.e. 'is this vehicle tagged
// Special_Vehicle_Type: Tank'" - so step 6 ONLY runs for tank-class
// vehicles; this project's own VehicleState::vehicleClass (the SAME real
// field) gates it the same way below (VehicleState::kVehicleClassTank).
// This project has no per-seat occupancy registry (stated project-wide gap,
// this file's own top comment: "THE OCCUPANCY STATE MACHINE IS NOT DRIVEN
// FROM HERE") and no team-relation matrix (team_make_hostile's own relation
// byte matrix, spec-lua-api-behaviour.md Sec38.2, is not tracked anywhere
// in this host) - 0x004fa040's real test is a relation-matrix lookup, not a
// raw team-id compare. CHOSEN stand-in, reusing only already-modelled
// fields (no new state): walk every OTHER known character whose
// CharacterState::currentVehicleName matches this vehicle and whose
// CharacterState::stateEnum is 1 (pending) or 3 (claimed/seated) and who is
// not dead (lifeState in {5,6} or killFinalizedFlagE3), and treat a
// DIFFERENT, BOTH-RESOLVED (!= -1 sentinel) CharacterState::currentTeamCode
// as a conflict. This is deliberately narrower than the real relation-
// matrix test (two distinct team ids are not necessarily hostile) but
// requires no invented matrix data; OPEN per Team A: whether a resolved,
// distinct-team pair that ISN'T actually hostile should pass here is a real
// question this project cannot answer without the matrix.
//
// Step 7 (specific-seat contest, spec text verbatim): "A specific-seat
// contest check, only when request +0x00 (seat) is not -1. ... When request
// +0x23 bit 0 (the fast/sprint flag written in Sec13.2) is clear, and the
// direct-claim flag is set, it additionally checks the distance from the
// vehicle to whichever character currently claims that seat ... and
// refuses if that claimant is still close enough to matter - a proximity-
// based eviction of stale claims, not a hard 'seat taken' refusal. When
// request +0x23 bit 0 is set, or the seat isn't contested, this check is
// skipped entirely." This host has no position/physics data wired to the
// Lua host (the same stated gap spec-player-runtime.md Sec13.6's own
// obstruction gates are left unmodelled for here). CHOSEN: treat every live
// claimant on the exact requested seat as "still close enough" (the
// conservative direction - a real stale/far claim would actually be
// evicted and succeed, so this can only produce an extra REFUSAL relative
// to the real game in that one sub-case, never an extra acceptance).
// Reuses CharacterState::lastVehicleEnterRequest (vehicleName + seatIndex +
// accepted), already kept per-character by every caller below, as the only
// per-seat occupancy record this host has - not a new field. `sprintFlag`
// (request +0x23 bit 0, "the triggering button press" per Sec13.7) is
// CHOSEN false at every call site below: Sec38.1's own text builds these
// Lua natives' request records independently of the real player
// trigger-button path that field is tied to, so none of vehicle_enter_do's
// Lua arguments plausibly maps to it (Sec38.1 leaves arg5/arg6's roles
// explicitly OPEN rather than naming either as this bit) - false is the
// correct default for a script/AI-driven submission, not an arbitrary
// guess.
//
// TEAM A SPEC-SYNC NOTE: this worktree's own spec-lua-api-behaviour.md
// Sec38.1 text (read directly, not paraphrased) does NOT itself state
// steps 6/7 above - that detail lives in spec-player-runtime.md Sec13.4
// only. Flagged for completeness, not a discrepancy with Sec38.1's own
// (narrower) claims.
bool vehicleEnterSeatPickGate(EngineState* st, const std::string& characterName, const std::string& vehicleName,
                               int64_t seatIndex, bool sprintFlag) {
    // Step 6 (tank-gated vehicle-wide team conflict).
    VehicleState& v = st->getOrCreateVehicle(vehicleName);
    bool isTank = v.vehicleClass.known() && v.vehicleClass.get() == VehicleState::kVehicleClassTank;
    if (isTank) {
        CharacterState& requester = st->getOrCreateCharacter(characterName);
        int32_t requesterTeam = requester.currentTeamCode;
        for (auto& entry : st->characters()) {
            if (entry.first == characterName) continue;
            CharacterState& other = entry.second;
            if (!other.currentVehicleName.known() || other.currentVehicleName.get() != vehicleName) continue;
            if (!other.stateEnum.known()) continue;
            int32_t otherPhase = other.stateEnum.get();
            if (otherPhase != 1 && otherPhase != 3) continue; // not claimed/pending
            bool otherDead = (other.lifeState.known() && (other.lifeState.get() == 5 || other.lifeState.get() == 6)) ||
                              other.killFinalizedFlagE3.get();
            if (otherDead) continue;
            if (requesterTeam != -1 && other.currentTeamCode != -1 && other.currentTeamCode != requesterTeam) {
                return false; // step 6: conflicting pair
            }
        }
    }

    // Step 7 (specific-seat contest).
    if (seatIndex != -1 && !sprintFlag) {
        for (auto& entry : st->characters()) {
            if (entry.first == characterName) continue;
            CharacterState& other = entry.second;
            if (!other.lastVehicleEnterRequest.has_value()) continue;
            const auto& req = *other.lastVehicleEnterRequest;
            if (!req.accepted || req.vehicleName != vehicleName || req.seatIndex != seatIndex) continue;
            if (!other.stateEnum.known()) continue;
            int32_t otherPhase = other.stateEnum.get();
            if (otherPhase != 1 && otherPhase != 3) continue; // claim since released/left
            return false; // step 7: seat still contested, treated as "close enough"
        }
    }

    return true;
}

// The submitter's own eligibility-then-seat-pick split (Sec7.4 step 3 cites
// Sec7.4a's full trace; step 4 is U6.2, Sec13.4): a test's own .set() on
// `openValue`, when present, wins outright (full backward compatibility
// with every existing test that drives the overall accepted/refused result
// directly, without exercising the gate's own fields at all). Otherwise the
// real eligibility gate runs first: a refusal there is a CONFIRMED, computed
// false - closing the mission-drive blocker for every refused case without
// needing a test to configure anything.
//
// 2026-10-09 follow-up (measured, not assumed): an ELIGIBLE result used to
// fall straight through to `openValue.get()`, which throws OpenStateError
// whenever nothing has .set() it - a real mission-drive run never does, so
// every eligible case still blocked exactly as before the gate existed.
// Measured directly on m05/m07/m09 (the eligibility gate itself passes:
// start_after_resume_error in the mission-drive probe named THIS OpenValue,
// not any of the gate's own per-step OpenValues, confirming the gate
// returned true and the old code then threw on the unset fallback).
//
// FIXED (2026-10-09, per Team A spec-sync): an eligible result no longer
// resolves straight to `true` ("seat pick assumed to succeed") - it now
// runs vehicleEnterSeatPickGate() above (Sec13.4 steps 6/7, the only two
// steps this host can compute real data for) and only falls back to the
// CHOSEN-default `true` once that gate also passes. This closes the exact
// m07 occupant-conflict case Team A flagged; steps 1-5/8/9 remain the same
// kind of labelled, data-starved gap the previous CHOSEN-default-true
// comment already described - not newly invented here, just narrowed.
bool computeVehicleEnterSubmitAccepted(EngineState* st, const std::string& characterName, CharacterState& c,
                                        OpenValue<bool>& openValue, bool forced, bool eligibilityBit,
                                        const std::string& vehicleName, int64_t seatIndex, bool sprintFlag) {
    if (openValue.known()) return openValue.get();
    if (!vehicleEnterEligibilityGate(st, characterName, c, forced, eligibilityBit)) return false;
    if (!vehicleEnterSeatPickGate(st, characterName, vehicleName, seatIndex, sprintFlag)) return false;
    return true; // CHOSEN default - Sec13.4 steps 1-5/8/9, unmodelled; see vehicleEnterSeatPickGate()'s own doc comment above
}

// ---------------------------------------------------------------------
// vehicle_enter_do (Sec38.1, 0x00a67000). 7 arguments: arg 1 character
// (0x00a281a0), arg 2 vehicle (resolveEnterVehicle - direct, else that
// character's own current vehicle), arg 3 "instant" boolean (lua_toboolean),
// then 4 optional, nil-gated: arg 4 seat index, args 5/6 booleans, arg 7 seat
// name. Sec38.1 gives no default values for the four optionals - CHOSEN:
// seat -1 (the request record's own "requested seat (-1 none)" reset value,
// spec-player-runtime.md Sec7.2), booleans false, seat name absent; none of
// them reaches a Lua-visible result here (they shape the submitted request
// only). Seat index converted with the project's standard 0x00ea2596
// truncation (HIGH CONFIDENCE: the same conversion every other rounded number
// in this cluster uses; Sec38.1 does not name it).
//
// Return: 1 boolean. CONFIRMED false when the character or the vehicle
// (including the fallback) fails to resolve (Sec38.6: "the enter-do family
// -> false"). Otherwise one of three CONFIRMED dispatch paths, in order:
// instant placement (arg 3); a named-seat placement (arg 7 given); or, with
// neither, an AI order that stages a default action template and starts
// action 0x31 ("vehicle enter scpt", CAE index 49, Sec30.4), switching a
// player-class character's script-mode field to 2 on success. HIGH
// CONFIDENCE: the pushed boolean is the dispatched path's own result (the
// submitter 0x00af22a0's for the two placements - OPEN per character,
// CharacterState::vehicleEnterSubmitAccepted; the order's start -
// vehicleEnterAiOrderStarts) - Sec38.1 does not spell it out for this
// entry, but its group sibling's text does ("any instant placement was
// refused" -> false).
//
// Mode-2 write: Sec38.1 names the field ("script-mode field") but not the
// setter. HIGH CONFIDENCE it is the generic script-mode setter 0x009e3200
// (the only documented writer of +0x2888, Sec14.29/Sec22.3/Sec22.15), so it
// goes through that setter's 0x008addb0 gate; only the mode field is written
// - what 0x009e3200 does to +0x28ac bit 0x2 for a non-zero mode is not given
// (Sec14.29 documents mode 0 only), so that bit is left untouched.
//
// Sec38.1's hidden side effect: flipIndependentFollowerIfFollowing().
// All OPEN reads happen before any write, so a refusal leaves no partial
// state behind.
//
// FIXED (2026-10-08): CONFIRMED spec-player-runtime.md Sec7.4 step 11 ("Copy
// the request ... into slot +0x5c and call set-enter(record, slot, 0, 1).
// Return true.") + Sec7.8 (+0x16d4 = 1 and +0x16c0/+0x16c4 = the vehicle
// handle, both at state 0's on-entry handler) - set-enter's own dispatch
// (Sec7.5: "jump through 0x00afe924") reaches that on-entry handler
// SYNCHRONOUSLY, inside the submitter 0x00af22a0's own call, with zero
// world-tick dependency. Only the instant and named-seat paths actually
// reach the submitter (vehicleEnterSubmitAccepted); the AI-order path
// starts action 0x31 instead and never calls it, so it is excluded.
// ---------------------------------------------------------------------
int stub_vehicle_enter_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_enter_do", upStateTag(L));
    std::string character = argString(L, 1);
    std::string vehicleArg = argString(L, 2);
    bool instant = lua_toboolean(L, 3) != 0;
    int64_t seatIndex = (lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL) ? truncateEa2596(lua_tonumber(L, 4)) : -1;
    bool arg5 = optionalBoolDefault(L, 5, false);
    bool arg6 = optionalBoolDefault(L, 6, false);
    bool hasSeatName = lua_gettop(L) >= 7 && lua_type(L, 7) != LUA_TNIL;
    std::string seatName = hasSeatName ? argString(L, 7) : std::string();

    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    std::string vehicle;
    if (!resolveEnterVehicle(st, vehicleArg, vehicle)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(character);

    int path;
    bool result;
    bool setScriptMode2 = false;
    if (instant) {
        path = CharacterState::kVehicleEnterPathInstant;
        // forced = instant OR request +0x23 bit 3 (Sec7.4 step 3); this
        // path's own "instant" IS the forced condition - request +0x23 bit 3
        // is never separately set by this native (no Lua argument for it).
        // seatIndex/sprintFlag=false: see vehicleEnterSeatPickGate()'s own
        // doc comment for why neither arg5/arg6 maps to the real sprint bit.
        result = computeVehicleEnterSubmitAccepted(st, character, c, c.vehicleEnterSubmitAccepted,
                                                     /*forced=*/true, /*eligibilityBit=*/false,
                                                     vehicle, seatIndex, /*sprintFlag=*/false);
    } else if (hasSeatName) {
        path = CharacterState::kVehicleEnterPathNamedSeat; // seat name -> the animation-state-style name table (id not observable here)
        // Not instant, and request +0x23 bit 3 is never set on this path
        // either -> forced = false (Sec7.4 step 3). Seat index is OPEN on
        // this path (name->index resolution not observable here) - passed
        // as -1 so vehicleEnterSeatPickGate()'s step 7 (specific-seat
        // contest) simply never fires for a named-seat request, a known
        // limitation rather than a guessed index.
        result = computeVehicleEnterSubmitAccepted(st, character, c, c.vehicleEnterSubmitAccepted,
                                                     /*forced=*/false, /*eligibilityBit=*/false,
                                                     vehicle, /*seatIndex=*/-1, /*sprintFlag=*/false);
    } else {
        path = CharacterState::kVehicleEnterPathAiOrder;
        result = c.vehicleEnterAiOrderStarts.get(); // OPEN until set
        if (result) setScriptMode2 = st->playerClassChainResolves(character); // CHOSEN default now applies when no test .set() it - class-row +0xa bit 0x2
    }

    flipIndependentFollowerIfFollowing(st, character);
    // See this function's own doc comment's FIXED note: only the two paths
    // that actually reach the submitter 0x00af22a0 get its synchronous
    // state-0 on-entry write; the AI-order path does not.
    // FIXED (2026-10-09): Sec7.4b's confirmed synchronous no-contention
    // instant commit - stateEnum = 3 ("seated"), not 1 ("entering"), is the
    // real, final write for this model's only representable case (see the
    // top-of-file FIXED note added alongside this change, and
    // attemptGroupInstantEnter()'s own identical write below for the
    // group-path precedent this follows).
    if (result && (path == CharacterState::kVehicleEnterPathInstant ||
                   path == CharacterState::kVehicleEnterPathNamedSeat)) {
        c.stateEnum.set(3);
        c.currentVehicleName.set(vehicle);
        // Spec V.1 seating commit: vehicle hook, then occupant hook (deferred script threads).
        st->commitVehicleSeat(character, vehicle, static_cast<int>(seatIndex));
    }
    CharacterState::VehicleEnterRequestRecord rec;
    rec.vehicleName = vehicle;
    rec.path = path;
    rec.seatIndex = seatIndex;
    rec.seatName = seatName;
    rec.arg5 = arg5;
    rec.arg6 = arg6;
    rec.accepted = result;
    c.lastVehicleEnterRequest = rec;
    ++c.vehicleEnterRequestCount;
    if (setScriptMode2) {
        if (c.controlLockGatePasses.get()) {
            c.scriptControlledMode = 2;
        } else {
            EngineState::replicateStateChange("script_mode_params", character);
        }
    }
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_enter_guardian_angel (Sec38.1, 0x00a67670). 3 arguments: character,
// vehicle, optional seat index (nil-gated; CHOSEN default -1, same as
// vehicle_enter_do). Return: 1 boolean. "Same resolver/follower-flag shape as
// the previous entry, but submits a record with 'forced' eligibility" -
// request +0x23 bit 3 (spec-player-runtime.md Sec7.2/Sec7.4 step 3), which
// skips several of the submitter's own refusal checks - so its result is a
// SEPARATE OPEN fact, CharacterState::vehicleEnterSubmitAcceptedForced.
// Unresolved character/vehicle -> false (Sec38.6).
//
// FIXED (2026-10-08): same synchronous Sec7.4 step 11/Sec7.8 state-0
// on-entry write as vehicle_enter_do's instant/named-seat paths (see that
// function's own comment for the full citation) - "forced" eligibility
// (Sec7.4 step 3) only changes which refusals are skipped before
// acceptance, not what the submitter's own unconditional step 11 does once
// accepted.
// ---------------------------------------------------------------------
int stub_vehicle_enter_guardian_angel(lua_State* L) {
    logCall(L, upLog(L), "vehicle_enter_guardian_angel", upStateTag(L));
    std::string character = argString(L, 1);
    std::string vehicleArg = argString(L, 2);
    int64_t seatIndex = (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) ? truncateEa2596(lua_tonumber(L, 3)) : -1;

    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    std::string vehicle;
    if (!resolveEnterVehicle(st, vehicleArg, vehicle)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterState& c = st->getOrCreateCharacter(character);
    // This native's own request is "forced" unconditionally (+0x23 bit 3,
    // Sec38.1/Sec7.4 step 3) - the whole point of the guardian-angel entry.
    bool result = computeVehicleEnterSubmitAccepted(st, character, c, c.vehicleEnterSubmitAcceptedForced,
                                                      /*forced=*/true, /*eligibilityBit=*/false,
                                                      vehicle, seatIndex, /*sprintFlag=*/false);

    flipIndependentFollowerIfFollowing(st, character);
    // FIXED (2026-10-09): same Sec7.4b synchronous no-contention commit as
    // vehicle_enter_do's instant/named-seat paths above - stateEnum = 3, not 1.
    if (result) {
        c.stateEnum.set(3);
        c.currentVehicleName.set(vehicle);
        st->commitVehicleSeat(character, vehicle, static_cast<int>(seatIndex)); // spec V.1 seating commit
    }
    CharacterState::VehicleEnterRequestRecord rec;
    rec.vehicleName = vehicle;
    rec.path = CharacterState::kVehicleEnterPathGuardianAngel;
    rec.seatIndex = seatIndex;
    rec.accepted = result;
    c.lastVehicleEnterRequest = rec;
    ++c.vehicleEnterRequestCount;
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

} // namespace
namespace stubs_b15 {
// ---------------------------------------------------------------------
// vehicle_enter_group_do (Sec38.1, 0x00a67330). 3 arguments: vehicle (WITH
// the character fallback - Sec38.1 says only the check_done sibling lacks
// it), "instant" boolean (lua_toboolean), a table of character names (the
// 0x0083dff0 shape, readCharacterNameArrayRaw). Return: 1 boolean - CONFIRMED
// false if the vehicle fails to resolve or any instant placement was refused,
// else true if at least one member reached its order/placement. CONFIRMED:
// instant mode assigns seats strictly by table position (the first table
// entry is seat 0 = driver - position counts every entry, so a table that
// doesn't list the intended driver first puts the wrong member in the
// driver's seat); an order's own result is never checked, so every member
// that reaches the order branch counts as success. A member whose name does
// not resolve reaches neither branch (HIGH CONFIDENCE: "reached its order/
// placement" presupposes a resolved member). CHOSEN: a refused instant
// placement does not stop the walk (Sec38.1 does not say it returns early;
// either choice gives the same Lua result, only the later members' records
// differ). Per member: the Sec38.1 follower-flag side effect.
//
// FIXED (2026-10-08): each accepted instant-placement member gets the same
// synchronous Sec7.4 step 11/Sec7.8 state-0 on-entry write as vehicle_
// enter_do's own instant path (see that function's own comment) - the
// order branch does not, for the same reason vehicle_enter_do's AI-order
// path does not (never reaches the submitter 0x00af22a0 in scope here).
// ---------------------------------------------------------------------
// Shared by vehicle_enter_group_do's own instant per-member branch below and
// vehicle_enter_group_check_done's own straggler re-order step (Sec38.1,
// "re-issues the enter order to every straggling member each time it's
// called") - a single member's instant-placement attempt through the real
// submitter (forced=true, the same "instant IS forced" reasoning every
// other instant path in this file uses), with the exact same synchronous
// Sec7.4 step 11/Sec7.8 state-0 on-entry write and lastVehicleEnterRequest/
// vehicleEnterRequestCount bookkeeping both callers already performed
// inline before this refactor (added 2026-10-09, no behavior change for
// vehicle_enter_group_do's own existing callers - reused, not duplicated,
// per this task's own instruction).
//
// FIXED (2026-10-09, mission-drive m07 deadlock): vehicle_enter_group_teleport
// (game_lib.lua:3089, `vehicle_enter_group_do(vehicle, true, npcs)` followed by
// `repeat thread_yield() until vehicle_enter_group_check_done(vehicle, true,
// npcs)`) spun for its entire tick budget because NOTHING in this host ever
// wrote stateEnum (+0x16d4) to 3 - check_done's own "seated in THIS vehicle"
// clear test (Sec38.1) can only ever see 1 ("entering"), never 3 ("seated"),
// so every member is a permanent straggler and, since vehicle_enter_group_
// teleport hard-codes do_not_reorder=true (out of this file's scope, Lua-side),
// stragglers are never even re-issued. Per spec-player-runtime.md Sec7.4b
// (forward-traced from Sec7.4a, 2026-10-09 entry): for the INSTANT, no-
// contention case specifically, the real submitter's own on-entry cascade
// (hub -> 0x00afabd0 "if head of its entry-point queue: set-enter 9, the
// commit" -> 0x00afbd90 seating commit, human+0x16d4 = 3) runs with ZERO
// world-tick dependency - it is a synchronous chain of on-entry handlers, not
// a per-frame wait, UNLESS a competing occupant already holds the seat
// (0x00afeeb0's own per-frame retry, which only fires on a contested claim).
// This project's own already-built, already-reviewed sr3player::
// VehicleInteractionRegistry (player_vehicle_occupancy.cpp, NOT wired to this
// Lua host anywhere - confirmed by a whole-src grep, its only referrers are
// itself, its header, and its own standalone test) independently reproduces
// exactly this: OnEnterHub -> OnEnterInstantTakeSeat -> OnEnterCommit ->
// SetRide(kRideActive) -> OnRideActive (stateEnums_[occ] = 3) all fire
// synchronously inside one SubmitEnterRequest call when headOfQueue is true
// (no competing claimant) - see that file's own OnEnterCommit/OnRideActive.
// This project's group-enter model has no per-seat occupant-conflict tracking
// at all (CHOSEN, Sec38.1's own "no de-duplication... a second request... can
// claim a second slot" plus this file's own "seat = table position" rule
// mean every resolved member is, in this model, always the sole claimant of
// its own distinct seat) - so the no-contention case is the ONLY case this
// model can ever represent, and per Sec7.4b that case commits synchronously.
// Writing stateEnum = 3 (not 1) here therefore completes the ALREADY-PARTIALLY
// -modeled synchronous instant-path write (the submission-accept write this
// file's own top-of-file banner comment already establishes for state 1),
// not a new invented mechanism - reusing the exact same computeVehicleEnter
// SubmitAccepted() + direct CharacterState field writes every other instant
// path in this file already uses, just carrying the write one step further
// for the no-contention case Sec7.4b confirms completes in the same call.
// SCOPED NARROWLY to this shared group helper (attemptGroupInstantEnter) -
// vehicle_enter_do's own singular instant path and vehicle_enter_guardian_
// angel (both above, unchanged) still write only stateEnum = 1, per this
// file's own top-of-file banner note on why phase 3 is not written from
// here; that inconsistency is intentional and flagged, not silently fixed,
// since those two natives are out of this task's own scope and already have
// passing tests pinned to the stateEnum==1 result - see this task's own
// final report for the open follow-up this leaves.
bool attemptGroupInstantEnter(EngineState* st, const std::string& member, const std::string& vehicle,
                               int64_t seatIndex) {
    CharacterState& c = st->getOrCreateCharacter(member);
    bool ok = computeVehicleEnterSubmitAccepted(st, member, c, c.vehicleEnterSubmitAccepted,
                                                  /*forced=*/true, /*eligibilityBit=*/false,
                                                  vehicle, seatIndex, /*sprintFlag=*/false);
    if (ok) {
        // FIXED (2026-10-09): Sec7.4b's confirmed synchronous no-contention
        // instant commit - stateEnum = 3 ("seated"), not 1 ("entering"), is
        // the real, final write for this model's only representable case
        // (no seat-conflict tracking - see this function's own doc comment).
        c.stateEnum.set(3);
        c.currentVehicleName.set(vehicle);
        st->commitVehicleSeat(member, vehicle, static_cast<int>(seatIndex)); // spec V.1 seating commit
    }
    flipIndependentFollowerIfFollowing(st, member);
    CharacterState::VehicleEnterRequestRecord rec;
    rec.vehicleName = vehicle;
    rec.path = CharacterState::kVehicleEnterPathInstant;
    rec.seatIndex = seatIndex;
    rec.accepted = ok;
    c.lastVehicleEnterRequest = rec;
    ++c.vehicleEnterRequestCount;
    return ok;
}
} // namespace stubs_b15
namespace {
using stubs_b15::attemptGroupInstantEnter;

// Defined in lua_spec_confirmed_stubs_b15_wave2_core_dispatchers_part2.cpp (split out of this file, no behaviour change).
} // namespace
namespace stubs_b15 {
int stub_vehicle_enter_group_do(lua_State* L);
int stub_vehicle_enter_group_check_done(lua_State* L);
int stub_vehicle_enter_check_done(lua_State* L);
int stub_move_to_do(lua_State* L);
int stub_vehicle_pathfind_navmesh_do(lua_State* L);
int stub_teleport_to_object(lua_State* L);
int stub_character_damage(lua_State* L);
int stub_qte_start(lua_State* L);
} // namespace stubs_b15
namespace {
using stubs_b15::stub_vehicle_enter_group_do;
using stubs_b15::stub_vehicle_enter_group_check_done;
using stubs_b15::stub_vehicle_enter_check_done;
using stubs_b15::stub_move_to_do;
using stubs_b15::stub_vehicle_pathfind_navmesh_do;
using stubs_b15::stub_teleport_to_object;
using stubs_b15::stub_character_damage;
using stubs_b15::stub_qte_start;

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB15() {
    static const std::vector<std::string> names = {
        // Wave 2 batch W2-1 (2026-10-08, dedicated owner) - all 13 are
        // `gameplay` in tools/lua_all_registered_1490_tagged.txt (grepped
        // directly for this batch).
        "vehicle_exit_do",
        "vehicle_exit_check_done",
        "vehicle_exit_guardian_angel",
        "vehicle_enter_do",
        "vehicle_enter_guardian_angel",
        "vehicle_enter_group_do",
        "vehicle_enter_group_check_done",
        "vehicle_enter_check_done",
        "move_to_do",
        "vehicle_pathfind_navmesh_do",
        "teleport_to_object",
        "character_damage",
        "qte_start",
    };
    return names;
}

void registerB15(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("vehicle_exit_do")) registerOne(L, state, log, stateTag, "vehicle_exit_do", openGuard<stub_vehicle_exit_do>);
    if (wants("vehicle_exit_check_done")) registerOne(L, state, log, stateTag, "vehicle_exit_check_done", openGuard<stub_vehicle_exit_check_done>);
    if (wants("vehicle_exit_guardian_angel")) registerOne(L, state, log, stateTag, "vehicle_exit_guardian_angel", openGuard<stub_vehicle_exit_guardian_angel>);
    if (wants("vehicle_enter_do")) registerOne(L, state, log, stateTag, "vehicle_enter_do", openGuard<stub_vehicle_enter_do>);
    if (wants("vehicle_enter_guardian_angel")) registerOne(L, state, log, stateTag, "vehicle_enter_guardian_angel", openGuard<stub_vehicle_enter_guardian_angel>);
    if (wants("vehicle_enter_group_do")) registerOne(L, state, log, stateTag, "vehicle_enter_group_do", openGuard<stub_vehicle_enter_group_do>);
    if (wants("vehicle_enter_group_check_done")) registerOne(L, state, log, stateTag, "vehicle_enter_group_check_done", openGuard<stub_vehicle_enter_group_check_done>);
    if (wants("vehicle_enter_check_done")) registerOne(L, state, log, stateTag, "vehicle_enter_check_done", openGuard<stub_vehicle_enter_check_done>);
    if (wants("move_to_do")) registerOne(L, state, log, stateTag, "move_to_do", openGuard<stub_move_to_do>);
    if (wants("vehicle_pathfind_navmesh_do")) registerOne(L, state, log, stateTag, "vehicle_pathfind_navmesh_do", openGuard<stub_vehicle_pathfind_navmesh_do>);
    if (wants("teleport_to_object")) registerOne(L, state, log, stateTag, "teleport_to_object", openGuard<stub_teleport_to_object>);
    if (wants("character_damage")) registerOne(L, state, log, stateTag, "character_damage", openGuard<stub_character_damage>);
    if (wants("qte_start")) registerOne(L, state, log, stateTag, "qte_start", openGuard<stub_qte_start>);
}

} // namespace stub_sections
} // namespace sr3luahost
