// lua_spec_confirmed_stubs_b15_wave2_core_dispatchers_part2.cpp
//
// Second half of lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp's stub functions, moved
// here unchanged to keep each file under ~1,500 lines (Team C refactor, 2026-10-17). The
// names/registration pair stays in that file; see its header comment for scope, sources
// and policy.

#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include <cfloat>
#include <cmath>
#include <limits>

namespace sr3luahost {
namespace stubs_b15 {

// Defined in lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp.
std::vector<std::string> readCharacterNameArrayRaw(lua_State* L, int idx);

// Defined in lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp.
bool resolveEnterVehicle(EngineState* st, const std::string& name, std::string& vehicleOut);

// Defined in lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp.
void flipIndependentFollowerIfFollowing(EngineState* st, const std::string& character);

// Defined in lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp.
bool vehicleEnterEligibilityGate(EngineState* st, const std::string& characterName, CharacterState& c,
                                  bool forced, bool eligibilityBit);

// Defined in lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp.
bool attemptGroupInstantEnter(EngineState* st, const std::string& member, const std::string& vehicle,
                               int64_t seatIndex);

// character_damage's "plain lua_tonumber cast directly to a single-precision
// float" (Sec20.32). IEEE-754 binary32 conversion with round-to-nearest,
// written out so an out-of-range double (e.g. a script's 1e300) gives the
// IEEE result (+/-inf, or +/-FLT_MAX just below the overflow threshold)
// instead of relying on a C++ cast whose out-of-range behavior is not
// portable. Standard IEEE arithmetic, not an engine-derived constant.
float toSinglePrecisionIeee(double v) {
    if (std::isnan(v)) return std::numeric_limits<float>::quiet_NaN();
    const double kOverflow = std::ldexp(1.0, 128) - std::ldexp(1.0, 103); // ties round to even = 2^128 -> inf
    if (v >= kOverflow) return std::numeric_limits<float>::infinity();
    if (v <= -kOverflow) return -std::numeric_limits<float>::infinity();
    // FLT_MAX, not numeric_limits<float>::max(): common.h pulls in <windows.h>
    // without NOMINMAX, so the `max` function-like macro is live in this TU.
    const double kMax = static_cast<double>(FLT_MAX);
    if (v > kMax) return FLT_MAX;
    if (v < -kMax) return -FLT_MAX;
    return static_cast<float>(v);
}

// 0x009e3200 with mode 0 and no position payload (Sec22.3) - the SAME apply
// player_script_controlled_clear (Sec14.29, b11) already models: gated on the
// single gate 0x008addb0 (controlLockGatePasses); local path clears the mode
// field +0x2888 and bit 0x2 of +0x28ac; otherwise an opcode-0x46
// "player"/"script_mode_params" record.
void clearScriptMode(CharacterState& c, const std::string& name) {
    if (c.controlLockGatePasses.get()) {
        c.scriptControlledMode = 0;
        c.statusByte28ac.setBits(CharacterState::kScriptControlledBit28ac, 0u);
    } else {
        EngineState::replicateStateChange("script_mode_params", name);
    }
}

int stub_vehicle_enter_group_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_enter_group_do", upStateTag(L));
    std::string vehicleArg = argString(L, 1);
    bool instant = lua_toboolean(L, 2) != 0;
    std::vector<std::string> names = readCharacterNameArrayRaw(L, 3);

    EngineState* st = upState(L);
    std::string vehicle;
    if (!resolveEnterVehicle(st, vehicleArg, vehicle)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool anyRefused = false;
    bool anyReached = false;
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string& member = names[i];
        if (!st->objectResolves().get(member)) continue; // OPEN until set
        if (instant) {
            // Same "instant IS forced" reasoning as vehicle_enter_do's own
            // instant path above (Sec7.4 step 3) - this per-member branch
            // goes through the exact same submitter 0x00af22a0, now via the
            // shared attemptGroupInstantEnter() helper (reused, see its own
            // doc comment, also by vehicle_enter_group_check_done below).
            bool ok = attemptGroupInstantEnter(st, member, vehicle, static_cast<int64_t>(i)); // CONFIRMED: seat = table position
            if (ok) {
                anyReached = true;
            } else {
                anyRefused = true;
            }
        } else {
            CharacterState& c = st->getOrCreateCharacter(member);
            CharacterState::VehicleEnterRequestRecord rec;
            rec.vehicleName = vehicle;
            rec.path = CharacterState::kVehicleEnterPathAiOrder;
            rec.accepted = true; // CONFIRMED: the order's own result is never checked - counts as reached
            anyReached = true;
            flipIndependentFollowerIfFollowing(st, member);
            c.lastVehicleEnterRequest = rec;
            ++c.vehicleEnterRequestCount;
        }
    }
    lua_pushboolean(L, (!anyRefused && anyReached) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_enter_group_check_done (Sec38.1, 0x00a66920). 3 arguments: vehicle
// (NO character fallback - CONFIRMED, unlike the three entries above), a "do
// not re-order" boolean, a table of character names. Return: 1 boolean.
// CONFIRMED: an unresolved vehicle answers "done" (true) - including a name
// this host knows as a character, since the type-gated vehicle resolver
// fails for it and no fallback is tried. Sec38.1's own text (read directly
// from this worktree's spec-lua-api-behaviour.md, verbatim) says only that
// the poll is not pure - with the flag false it re-issues the enter order to
// every "straggling member" on each call - WITHOUT itself giving the
// per-member "done" predicate or what makes a member a straggler.
//
// FIXED (2026-10-09, per Team A direct spec-sync - not found verbatim in
// Sec38.1's own text above, flagged as a sync gap, implemented per Team A's
// explicit instruction since it is precise and keyed entirely to fields
// this project already models): no completion threshold - EVERY member must
// clear. A member clears if dead (lifeState in {5,6} or killFinalizedFlagE3
// - the SAME membership test vehicleEnterEligibilityGate()'s own step 5
// reuses), OR the +0x501 state exemption (CharacterState::stateCodeByte501
// == 0x26 - "not a timeout", i.e. this project does not model any timeout
// path separately, only the exempt value itself), OR seated in THIS
// vehicle (+0x16d4 == 3, CharacterState::stateEnum, "plus a matching
// handle" - CharacterState::currentVehicleName == this vehicle). A member
// whose name never resolves cannot be acted on at all (same "can't act on
// it" convention this function's own unresolved-vehicle branch above
// already uses) - CHOSEN to treat it as cleared (skipped, does not block
// "done"), since nothing in this project's data can tell a genuinely absent
// name apart from one this host just hasn't seen yet.
//
// Stragglers (members that don't clear) are re-ordered only when the "do
// not re-order" argument is false, via the SAME shared instant-submit path
// vehicle_enter_group_do's own instant branch uses (attemptGroupInstantEnter()
// above, reused not duplicated) - re-seated at the straggler's own table
// position, exactly as vehicle_enter_group_do's own CONFIRMED "seat = table
// position" rule already establishes. Per Team A: "vehicle_enter_group_
// teleport hard-codes [do_not_reorder] true, so a straggler can spin
// forever" - that native is out of this task's own scope (not a registered
// stub in this file), so it is not touched here; this function's own
// behavior is unconditionally correct for whichever caller passes which
// value, including a caller that always passes true.
// ---------------------------------------------------------------------
int stub_vehicle_enter_group_check_done(lua_State* L) {
    logCall(L, upLog(L), "vehicle_enter_group_check_done", upStateTag(L));
    std::string vehicleArg = argString(L, 1);
    bool doNotReorder = lua_toboolean(L, 2) != 0;
    std::vector<std::string> names = readCharacterNameArrayRaw(L, 3);
    EngineState* st = upState(L);
    if (st->hasCharacter(vehicleArg) || !st->objectResolves().get(vehicleArg)) { // OPEN until set
        lua_pushboolean(L, 1); // CONFIRMED: unresolved vehicle -> "done"
        return 1;
    }
    const std::string& vehicle = vehicleArg;
    bool allDone = true;
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string& member = names[i];
        if (!st->objectResolves().get(member)) continue; // CHOSEN: unresolved -> cleared (see doc comment)
        CharacterState& c = st->getOrCreateCharacter(member);
        bool dead = (c.lifeState.known() && (c.lifeState.get() == 5 || c.lifeState.get() == 6)) ||
                    c.killFinalizedFlagE3.get();
        bool stateExempt = c.stateCodeByte501.known() && c.stateCodeByte501.get() == 0x26;
        bool seatedHere = c.stateEnum.known() && c.stateEnum.get() == 3 &&
                          c.currentVehicleName.known() && c.currentVehicleName.get() == vehicle;
        bool cleared = dead || stateExempt || seatedHere;
        if (cleared) continue;
        allDone = false;
        if (!doNotReorder) {
            attemptGroupInstantEnter(st, member, vehicle, static_cast<int64_t>(i));
        }
    }
    lua_pushboolean(L, allDone ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_enter_check_done (Sec22.3, 0x00a66e60). Arguments: arg 1 mandatory
// string (character, 0x00a281a0); arg 2 optional boolean (nil-gated, default
// false); arg 3 optional string (nil-gated, default "" - CONFIRMED a genuine
// zero-length string, which never resolves). Return: 1 NUMBER, one of three
// byte-confirmed constants, exactly as Sec22.3 orders them:
//   - character unresolved -> 2.0 immediately (nothing else runs);
//   - dead/flagged (0x0096f4f0), OR seated (+0x16d4 == 3, 0x009b9160), OR
//     (arg 2 only) the helper 0x009b95c0 -> 1.0;
//   - else the state-code byte pair +0x501/+0x504 == '&'/0x12, OR +0x16d4 in
//     {1,2} (0x009b91e0) -> 0.0;
//   - else 2.0.
// Then, if the result != 0.0 AND the character is player-class (class-row
// +0xa bit 0x2), the script-mode setter 0x009e3200 runs with mode 0
// (clearScriptMode - the SAME apply player_script_controlled_clear models).
//
// Two evaluation-order choices, both behaviour-preserving (every operand is a
// side-effect-free read): within the 0.0 group the already-read +0x16d4 test
// runs before the OPEN byte pair, and the arg-3 vehicle resolve (with its
// +0x16c0/+0x16c4 fallback through 0x004dcf00) is not performed, because its
// only consumer is 0x009b95c0, which this host models as one OPEN per-
// character result (CharacterState::enterCheckHelper9b95c0) - resolving a
// name whose result nothing reads would only add refusals.
//
// NOTE on labels: Sec22.3 calls 0.0 "done" and 1.0 "still pending/remote",
// written before the +0x16d4 relabelling (3 = seated, 1/2 = entering/
// exiting). Under Sec35.2's own pool convention (0 pending / 1 done / 2 no
// request, the 2.0 sentinel this function is CONFIRMED to share with
// vehicle_pathfind_check_done) the same numbers read "entering = pending,
// seated = done". Only the numbers are reproduced; neither label is relied on.
// ---------------------------------------------------------------------
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
int stub_vehicle_enter_check_done(lua_State* L) {
    logCall(L, upLog(L), "vehicle_enter_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    bool arg2 = optionalBoolDefault(L, 2, false);
    (void)optionalStringDefault(L, 3, std::string()); // see the evaluation-order note above
    EngineState* st = upState(L);
    std::string resolvedName;
    if (!resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushnumber(L, 2.0); // CONFIRMED 0x012a2e28
        return 1;
    }
    name = resolvedName;
    CharacterState& c = st->getOrCreateCharacter(name);
    double result = 2.0; // CONFIRMED default
    if (!c.notDeadForActionPlay()) {
        result = 1.0; // 0x0096f4f0
    } else {
        int32_t phase = c.stateEnum.get(); // OPEN until set
        if (phase == 3) {
            result = 1.0; // 0x009b9160 - seated
        } else if (arg2 && c.enterCheckHelper9b95c0.get()) { // OPEN until set
            result = 1.0;
        } else if (phase == 1 || phase == 2) {
            result = 0.0; // 0x009b91e0
        } else if (c.stateCodeByte501.get() == '&' && c.stateCodeByte504.get() == CharacterState::kAiModeHijack) { // 0x12 = Hijack, spec-ai-runtime.md Sec3.2 mode 18 (spec-lua-api-behaviour.md Sec22.3, 2026-10-10 sync); stateCodeByte504 ("requested mode" byte) still OPEN per-character
            result = 0.0;
        }
    }
    if (result != 0.0 && st->playerClassChainResolves(name)) { // CHOSEN default now applies when no test .set() it
        clearScriptMode(c, name); // 0x009e3200(mode 0, no position) - hidden this = the character
    }
    lua_pushnumber(L, result);
    return 1;
}

// =======================================================================
// Movement / pathfind / teleport dispatchers
// =======================================================================

// ---------------------------------------------------------------------
// move_to_do (Sec21.22, 0x00a549e0). Arguments, all positional: arg 1
// character (0x00a281a0); arg 2 destination name; arg 3 number (truncated) -
// move mode; args 4/5/6 booleans, no nil-gate; arg 7 number (truncated, read
// unconditionally - CONFIRMED, so a NaN/out-of-range arg 7 refuses even when
// arg 6 is false, per this project's standard 0x00ea2596 policy). Return: 1
// number - 0.0 on any failure, else a positive per-call "move action id".
//
// Body, in Sec21.22's order (the destination resolve is side-effect-free, so
// it is evaluated after arg 1 - an unresolved arg 1 returns 0.0 whatever the
// destination, and this avoids refusing on a destination nothing consumes):
//   1. arg 1 unresolved -> 0.0 (no character, so no +0xc30 write).
//   2. Destination: arg 6 false -> arg 2 through 0x00643a80 (5th resolver +
//      liveness; objectResolves()), its +0x40 position the anchor. arg 6 true
//      -> REFUSED (labelled OPEN): the anchor comes from a global "current"
//      object fetched by 0x00a53f60 (Sec21.22: "not independently
//      decompiled, OPEN"), gated on arg 7 < that object's +0x33c count, then
//      0x00930e90(arg 7). (Note for whoever implements that path later: the
//      spec states only that upper-bound comparison, so a negative arg 7 is
//      worth guarding under the hardening policy.)
//   3. 0x0095acb0(target, 1, 0.2, 0, 0) - unconditional (counted).
//   4. No anchor -> failure. Else 0x00a3bae0(target, arg 5) (OPEN per value
//      of arg 5) must hold, else failure.
//   5. State code from arg 3: mode 3 keeps code 3 when either animation-tag
//      probe (codes 0x32/0x34 via 0x004b46c0) resolves, else falls back to
//      mode 1's code (Sec21.22 - note Sec22.15's check_done sibling falls back
//      to 2 instead; each is reproduced as written for its own function);
//      HIGH CONFIDENCE modes 1/2 map to codes 1/2 ("a final state code
//      (1/2/3)"); any other mode -> REFUSED (its code is not given).
//   6. 0x006f62b0(target, 2, 0) - a kind-2 request id (allocateScriptRequest
//      Id); then 0x00993630(target, &state) (OPEN) must succeed, else failure.
//   7. Success: 0x00d9e180(2000, 4000) arms the +0xc30 timer (this host:
//      a nextMonotonicStamp() marker); the 0x00971cf0/0x0098f250 position
//      notify has no Lua-visible effect (not modelled); a new id from the
//      global counter 0x0130cb94 (EngineState::allocateMoveToActionId() -
//      OPEN initial value) is stored into +0xc34 and returned as a number.
// Any failure after step 1: +0xc30 = -1 (CONFIRMED sentinel 0xffffffff),
// returns 0.0.
// ---------------------------------------------------------------------
// Target (arg 1) resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct
// disassembly of its own body): a two-stage #PLAYER#-then-#FOLLOWER#
// wrapper, not PLAYER-only as an earlier reading had it - wired here via
// resolveFollowerAwareObject(). The destination's resolver (arg 2,
// 0x00643a80) is unaffected - distinct ("5th") resolver.
int stub_move_to_do(lua_State* L) {
    logCall(L, upLog(L), "move_to_do", upStateTag(L));
    std::string target = argString(L, 1);
    std::string destination = argString(L, 2);
    int64_t mode = truncateEa2596(lua_tonumber(L, 3));
    bool arg4 = lua_toboolean(L, 4) != 0;
    bool arg5 = lua_toboolean(L, 5) != 0;
    bool arg6 = lua_toboolean(L, 6) != 0;
    int64_t arg7 = truncateEa2596(lua_tonumber(L, 7)); // CONFIRMED: read unconditionally
    (void)arg7; // consumed only by the refused arg-6 path

    EngineState* st = upState(L);
    std::string resolvedTarget;
    if (!resolveFollowerAwareObject(st, target, resolvedTarget)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        lua_pushnumber(L, 0.0);
        return 1;
    }
    target = resolvedTarget;
    if (arg6) {
        throw OpenStateError("move_to_do arg-6 destination mode: global 'current' object 0x00a53f60 and its +0x33c bound",
                             "spec-lua-api-behaviour.md Sec21.22");
    }
    bool anchorResolved = st->objectResolves().get(destination); // OPEN until set - 0x00643a80
    CharacterState& c = st->getOrCreateCharacter(target);
    ++c.moveToPreMoveCallCount; // 0x0095acb0(target, 1, 0.2, 0, 0)

    auto fail = [&]() {
        c.moveToTimerC30 = -1; // CONFIRMED sentinel 0xffffffff
        lua_pushnumber(L, 0.0);
        return 1;
    };
    if (!anchorResolved) return fail();
    // arg5 false: 0x00a3bae0(target, 0) is character_is_ready's own predicate (spec 7.27), computed live unless a
    // test/override seeded it. arg5 true: not specified, OPEN until set.
    bool predicate = arg5 ? c.moveToPredicateA3baeArgTrue.get()
                          : (c.moveToPredicateA3baeArgFalse.known() ? c.moveToPredicateA3baeArgFalse.get()
                                                                    : characterReadyPredicate(st, target));
    if (!predicate) return fail();

    int32_t stateCode;
    if (mode == 1) {
        stateCode = 1;
    } else if (mode == 2) {
        stateCode = 2;
    } else if (mode == 3) {
        stateCode = c.moveToMode3AnimTagResolves.get() ? 3 : 1; // OPEN until set; falls back to mode 1's code
    } else {
        throw OpenStateError("move_to_do state code for a move mode outside 1/2/3", "spec-lua-api-behaviour.md Sec21.22");
    }

    (void)st->allocateScriptRequestId(target, EngineState::kScriptedRequestKindMoveOrPathfind); // 0x006f62b0(target, 2, 0)
    // 0x00993630 (Sec21.22, 78fb40a): a test-set value wins; otherwise the spec's port value - true when the human
    // is on foot, not already within 0.3 m (horizontal) of the destination, and a route exists. Not modelled (read
    // as passing): the post-area leash, route existence (no navmesh here), record flag +0x58 bit 0x20.
    bool dispatchOk;
    if (c.moveToDispatchSucceeds.known()) {
        dispatchOk = c.moveToDispatchSucceeds.get();
    } else if (c.currentVehicleName.known() && !c.currentVehicleName.get().empty()) {
        dispatchOk = false; // human in a vehicle
    } else {
        dispatchOk = true;
        if (stateCode == 1) { // arrival test 0x00992640 (record +0x5c == 1)
            CharacterState& dest = st->getOrCreateCharacter(destination);
            bool havePos = st->tryResolveWorldPositionFromZoneData(target, c.worldPosition) &&
                           st->tryResolveWorldPositionFromZoneData(destination, dest.worldPosition);
            if (havePos) {
                WorldPosition a = c.worldPosition.get(), b = dest.worldPosition.get();
                double dx = a.x - b.x, dz = a.z - b.z;
                if (dx * dx + dz * dz < 0.09) dispatchOk = false; // "already there"
            }
        }
    }
    if (!dispatchOk) return fail();

    int32_t id = st->allocateMoveToActionId(); // OPEN until the counter's initial value is set
    c.moveToTimerC30 = st->nextMonotonicStamp(); // 0x00d9e180(2000, 4000) - CHOSEN marker, randomized deadline not simulated
    c.moveToRequestTagC34 = id;
    CharacterState::MoveToRequestRecord rec;
    rec.destinationName = destination;
    rec.moveMode = mode;
    rec.stateCode = stateCode;
    rec.arg4 = arg4;
    rec.arg5 = arg5;
    rec.returnedId = id;
    c.lastMoveToRequest = rec;
    lua_pushnumber(L, static_cast<lua_Number>(id));
    return 1;
}

// ---------------------------------------------------------------------
// vehicle_pathfind_navmesh_do (Sec22.1, 0x00a67e40). 6 arguments, all
// positional with NO nil-gate (CONFIRMED asymmetry): arg 1 vehicle (0x00a281e0),
// arg 2 a secondary name, args 3-5 booleans, arg 6 number (truncated).
// Return: 1 number - the raw result of the dispatcher 0x00b24fa0.
//
// Body: arg 1 resolves; the occupant/part accessor 0x00ab5070 at index 0
// (VehicleState::seat0Occupied - the same "driver" reading Sec9.10 gives it)
// and a non-empty arg 2 are both required, otherwise the call "falls
// straight to a 'clear the pending request' tail". REFUSED (labelled OPEN):
// Sec22.1 does not give what that tail pushes, nor what is pushed when arg 1
// itself fails to resolve - no fabricated number. Otherwise arg 2 resolves
// (group resolver 0x0062a1f0, else the 5th resolver 0x005982e0 - one flat
// objectResolves() lookup here; the group-avoidance call 0x00a3b860 vs a
// plain destination is not distinguished, since neither choice reaches a
// Lua-visible result except through the OPEN dispatcher return) or, if
// neither resolves, the vehicle's own position/orientation is the
// destination (0x00da38e0); a kind-2 request id is allocated (0x006f62b0(
// vehicle, 2, 0) - the SAME kind as move_to_do, CONFIRMED shared namespace);
// 0x00b24fa0 then either records-and-replicates (opcode 0x41) or, with
// authority, delegates to the +0xc68 AI-directive sub-record. Its raw result
// is EngineState::vehicleNavmeshDispatchResult() (OPEN). The occupant-slot
// cleanup toggle 0x006f6360 has no Lua-visible effect (not modelled).
// ---------------------------------------------------------------------
int stub_vehicle_pathfind_navmesh_do(lua_State* L) {
    logCall(L, upLog(L), "vehicle_pathfind_navmesh_do", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    std::string secondary = argString(L, 2);
    bool b3 = lua_toboolean(L, 3) != 0;
    bool b4 = lua_toboolean(L, 4) != 0;
    bool b5 = lua_toboolean(L, 5) != 0;
    int64_t n6 = truncateEa2596(lua_tonumber(L, 6));

    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicleName)) { // OPEN until set
        throw OpenStateError("vehicle_pathfind_navmesh_do return value when arg 1 does not resolve",
                             "spec-lua-api-behaviour.md Sec22.1");
    }
    VehicleState& v = st->getOrCreateVehicle(vehicleName);
    if (!v.seat0OccupantPresent() || secondary.empty()) { // seat-0 occupant default false (spec 46.4 m06 note)
        throw OpenStateError("vehicle_pathfind_navmesh_do 'clear the pending request' tail's return value",
                             "spec-lua-api-behaviour.md Sec22.1");
    }
    bool destResolved = st->objectResolves().get(secondary); // OPEN until set
    // Spec a166141 Sec22.1: 1.0 = installed on the local authority, 2.0 = non-authority (network
    // record queued). 0.0 (>25 points / unresolved target) is not modelled; a test may force any value.
    double result = st->vehicleNavmeshDispatchResult().known()
                        ? st->vehicleNavmeshDispatchResult().get()
                        : (v.forceFlagGatePasses.get() ? 1.0 : 2.0);

    VehicleState::NavmeshPathfindRequest rec;
    rec.destinationName = destResolved ? secondary : std::string(); // "" = the vehicle's own position (0x00da38e0)
    rec.flagArg3 = b3;
    rec.flagArg4 = b4;
    rec.flagArg5 = b5;
    rec.numberArg6 = n6;
    rec.requestId = st->allocateScriptRequestId(vehicleName, EngineState::kScriptedRequestKindMoveOrPathfind);
    if (v.forceFlagGatePasses.get()) {
        rec.appliedLocally = true; // +0xc68 delegate 0x00b4f150 (internals OPEN)
    } else {
        EngineState::replicateStateChange("vehicle_pathfind_navmesh_do", vehicleName); // opcode 0x41
    }
    v.lastNavmeshPathfindRequest = rec;
    ++v.navmeshPathfindCount;
    lua_pushnumber(L, result);
    return 1;
}

// ---------------------------------------------------------------------
// teleport_to_object (Sec22.4, 0x00a612a0; paths per Sec4.3/Sec35.3/
// Sec35.4). Arguments: arg 1 character (0x00a281a0); arg 2 target object
// (0x00734e90 on the 0x02442750 registry - objectResolves()), no nil-gate,
// but CONFIRMED the whole body is skipped on a null/empty raw pointer
// (argString maps lua_tolstring's NULL to ""); arg 3 boolean default false;
// arg 4 boolean default TRUE (CONFIRMED asymmetry); args 5-7 numbers default
// 0.0 (local x/y/z offset); arg 8 number default 0.0 - a heading in RADIANS,
// converted with the byte-read constant 57.29583 (0x0111a3f8) to degrees.
// Return: none.
//
// Shares teleport's (Sec4.3) arg 1-7 shape and its general applier
// 0x00996b50, but has its OWN record (CharacterState::lastTeleportTo
// ObjectRequest) - teleport's teleportRequests()/teleportScriptedRequest
// Count() are not touched (arg 8 alone differs in type: a boolean there).
//
// Body: target missing/unresolved -> nothing; arg 1 unresolved -> nothing.
// Both resolved: a kind-4 request id (0x006f62b0(character, 4, 0) - the same
// kind teleport allocates, CONFIRMED by Sec22.4's correction), then:
//   - player class (class-row +0xa bit 0x2, playerClassChainResolves() -
//     the same stand-in teleport uses) -> 0x009e2220, the asynchronous
//     player-teleport (Sec35.3: queued into a 2-slot ring, game mode 8,
//     completed at mode exit). This host has no mode-8 driver, so the
//     request is left allocated and only the path is recorded.
//   - otherwise -> zone/cell stream-in (0x00861aa0/0x00861ec0), remote-state
//     notify (0x004e4130), default orientation (0x00da38e0/0x004e39b0) - none
//     Lua-visible - and the general applier 0x00996b50, single gate 0x008addb0
//     (controlLockGatePasses): without authority an opcode-0x41 record; with
//     authority the local apply, which (Sec35.4) completes the kind-4 request
//     before returning (releaseScriptRequestId) and forcibly ejects an active
//     human-shield partner (CharacterState::humanShieldHostageName, Sec45.1 -
//     HIGH CONFIDENCE that "ejects" leaves the taker holding nothing). Its
//     vehicle exit, state/physics resets and the camera snap (6400.0 threshold,
//     current-player +0x1e7c-+0x1e9c) are occupancy/world effects this host
//     does not model.
// ---------------------------------------------------------------------
// Character (arg 1) resolver 0x00a281a0 - RE-CORRECTED (2026-10-08,
// direct disassembly of its own body): a two-stage #PLAYER#-then-
// #FOLLOWER# wrapper, not PLAYER-only as an earlier reading had it -
// wired here via resolveFollowerAwareObject(). targetObject's resolver
// (arg 2, 0x00734e90) is unaffected - distinct resolver.
int stub_teleport_to_object(lua_State* L) {
    logCall(L, upLog(L), "teleport_to_object", upStateTag(L));
    std::string character = argString(L, 1);
    std::string targetObject = argString(L, 2);
    bool arg3 = optionalBoolDefault(L, 3, false);
    bool arg4 = optionalBoolDefault(L, 4, true); // CONFIRMED default true
    double ox = optionalNumberDefault(L, 5, 0.0);
    double oy = optionalNumberDefault(L, 6, 0.0);
    double oz = optionalNumberDefault(L, 7, 0.0);
    double headingRad = optionalNumberDefault(L, 8, 0.0);

    if (targetObject.empty()) return 0; // CONFIRMED: null/empty raw pointer -> whole body skipped
    EngineState* st = upState(L);
    if (!st->objectResolves().get(targetObject)) return 0; // OPEN until set - 0x00734e90 + liveness 0x00853b10
    std::string resolvedCharacter;
    if (!resolveFollowerAwareObject(st, character, resolvedCharacter)) return 0; // OPEN until set - 0x00a281a0; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    character = resolvedCharacter;
    bool isPlayer = st->playerClassChainResolves(character); // CHOSEN default now applies when no test .set() it - see engine_state.h
    CharacterState& c = st->getOrCreateCharacter(character);
    bool authority = isPlayer ? false : c.controlLockGatePasses.get();

    CharacterState::TeleportToObjectRequest rec;
    rec.targetObjectName = targetObject;
    rec.arg3 = arg3;
    rec.arg4 = arg4;
    rec.offsetX = ox;
    rec.offsetY = oy;
    rec.offsetZ = oz;
    rec.headingRadians = headingRad;
    rec.headingDegrees = headingRad * 57.29583; // CONFIRMED constant (0x0111a3f8)
    rec.tookPlayerPath = isPlayer;

    (void)st->allocateScriptRequestId(character, EngineState::kScriptedRequestKindTeleport); // 0x006f62b0(character, 4, 0)
    if (!isPlayer) {
        if (authority) {
            rec.appliedLocally = true;
            if (!c.humanShieldHostageName.empty()) {
                ++c.teleportToObjectShieldEjectCount; // vtable +0x44 ejection (character_release_human_shield's mechanism)
                c.humanShieldHostageName.clear();
            }
            st->releaseScriptRequestId(character, EngineState::kScriptedRequestKindTeleport); // completed before return (Sec35.4)
        } else {
            EngineState::replicateStateChange("teleport_to_object", character); // opcode 0x41
        }
    }
    c.lastTeleportToObjectRequest = rec;
    ++c.teleportToObjectCount;
    return 0;
}

// =======================================================================
// Damage and QTE
// =======================================================================

// ---------------------------------------------------------------------
// character_damage (Sec20.32, 0x00a41180). Arguments: arg 1 target (0x00a281a0);
// arg 2 damage amount - a plain lua_tonumber cast to single precision, NOT
// rounded (CONFIRMED distinct sub-idiom; toSinglePrecisionIeee); arg 3
// optional boolean, nil-gated, default TRUE; arg 4 optional source name,
// nil-gated, default absent. Return: none.
//
// Body: flags word = 0x100 (always) | 0x200 (arg 3). Target unresolved ->
// nothing. A given source resolves (0x00a281a0) or is null - the call still
// proceeds. Then the central damage engine 0x0096fa20(target, source,
// amount, 0x0117f8b8, 0x0117f8bc, 0, &flags): liveness (objectResolves);
// early exit on the target's descriptor byte +0x33 bit 0x2 (CharacterState::
// damageImmuneDescriptorBit33, OPEN); bit 0x800 of the flags is never set by
// this entry point (0x100|0x200 only), so its fixed-amount override cannot
// fire from here; single gate 0x008addb0 (controlLockGatePasses):
//   - not authoritative -> the opcode-0x41 record (both network ids, the
//     amount via the float encoder 0x0091dbc0, the default position pair, 11
//     flag bits) - recorded (CharacterState::lastReplicatedDamage);
//   - authoritative -> RESOLVED 2026-10-09 (wave 6 spec sync, spec-player-
//     runtime.md Sec15.3a, "generic damage application, death, and ragdoll
//     hand-off"), closing this function's own former OpenStateError
//     ("0x0096fa20 authoritative branch: final damage amount after its
//     modifier chain"). Sec15.3a does NOT give every one of the ~30
//     modifier gates Sec20.32's own earlier pass counted (those stay OPEN -
//     friendly-fire scaling, the HP-bracket falloff curve, weapon/armor
//     multipliers, the incendiary auto-ignite cross-call, several
//     suppression gates) - it newly pins only the two it names explicitly,
//     plus the health-mutation primitives and the full death/ragdoll chain
//     past the zero-HP threshold:
//       1. resistance scale: an integer percentage read off the
//          INSTIGATOR's own +0x1cac (CONFIRMED mechanism; CharacterState::
//          instigatorDamageScalePercent, CHOSEN neutral 100 default - see
//          its own doc comment for why this project does not reuse
//          maxHitPoints directly here);
//       2. difficulty scalar, applied ONLY when the instigator is
//          player-class: EngineState::damageDealtDifficultyMultiplier(),
//          the REAL, empirically-decoded Damage_Dealt_Mult values from
//          difficulty_levels.xtbl Set A (spec-tables-progression.md
//          Sec14.7), indexed by EngineState::difficultyLevel();
//       3. health -= scaled amount, clamped to [0, maxHitPoints]
//          (0x0094cfc0/0x0094d520, CONFIRMED) - the real engine's own
//          separate force-snap-to-0.0 pre-write on "a clearly lethal hit"
//          is a float-precision-only safety measure that collapses into
//          this same clamped integer result once health is modelled as
//          int32 (Sec7.12's own already-CONFIRMED storage type for
//          +0x1cb8 - kept over Sec15.3a's own "(float)" shorthand for that
//          same field; see CharacterState's own "Wave 6" doc comment for
//          the full reconciliation note);
//       4. +0x1584 last-damage-magnitude scratch updated
//          (CharacterState::lastDamageMagnitude);
//       5. at the CONFIRMED zero threshold (cross-referenced to Sec20.32's
//          own "fixed threshold constant (0x0125e270, plausibly 0.0)" -
//          superseding that section's own unresolved "0x00963530 vs
//          0x0096f8c0" footnote, which Sec15.3a's fresh disassembly does
//          not revisit): the death transition 0x00970e70 (gated on a
//          CHOSEN pass-through kill-eligibility stand-in, CharacterState::
//          lethalHitKillEligible - the real predicate 0x0096f990 is OPEN)
//          and the ragdoll/finalize hand-off (0x0098ce40's real branch
//          point, driven here via CharacterState::ragdollActivationEligible
//          - see that field's own doc comment for the exact two named
//          branches this project models, and the one combination Sec15.3a
//          leaves genuinely unspecified).
// ---------------------------------------------------------------------
// Resolver 0x00a281a0 (both target arg 1 and source arg 4) - RE-CORRECTED
// (2026-10-08, direct disassembly of its own body): a two-stage
// #PLAYER#-then-#FOLLOWER# wrapper, not PLAYER-only as an earlier reading
// had it - wired here, follower-sentinel-resolver pass, via
// resolveFollowerAwareObject().
int stub_character_damage(lua_State* L) {
    logCall(L, upLog(L), "character_damage", upStateTag(L));
    std::string target = argString(L, 1);
    float amount = toSinglePrecisionIeee(lua_tonumber(L, 2)); // CONFIRMED: no rounding
    bool arg3 = optionalBoolDefault(L, 3, true);              // CONFIRMED default TRUE
    bool hasSource = lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL;
    std::string sourceArg = hasSource ? argString(L, 4) : std::string();
    uint32_t flags = 0x100u | (arg3 ? 0x200u : 0u);

    EngineState* st = upState(L);
    std::string resolvedTarget;
    if (!resolveFollowerAwareObject(st, target, resolvedTarget)) return 0; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    target = resolvedTarget;
    std::string source;
    std::string resolvedSource;
    if (hasSource && resolveFollowerAwareObject(st, sourceArg, resolvedSource)) source = resolvedSource; // OPEN until set; unresolved -> null source; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    CharacterState& c = st->getOrCreateCharacter(target);
    if (c.damageImmuneDescriptorBit33.get()) return 0; // OPEN until set - 0x0096fa20's early exit
    if (!c.controlLockGatePasses.get()) {
        CharacterState::CharacterDamageRecord rec;
        rec.amount = amount;
        rec.flags = flags;
        rec.sourceName = source;
        c.lastReplicatedDamage = rec;
        ++c.characterDamageReplicateCount;
        EngineState::replicateStateChange("character_damage", target); // opcode 0x41
        return 0;
    }

    // AUTHORITATIVE branch - RESOLVED 2026-10-09 (wave 6, spec-player-
    // runtime.md Sec15.3a). See this function's own top doc comment for
    // the full per-step citation. The actual math/health-mutation/death-
    // ragdoll chain is EngineState::applyCharacterDamage() (lua_engine_
    // state.cpp) - factored out so a future non-Lua caller (e.g. an
    // IWeaponDamageSink adapter, include/sr3luahost/character_damage_sink.h)
    // shares this exact implementation rather than a second copy.
    st->applyCharacterDamage(target, source, amount, flags);
    return 0;
}

// ---------------------------------------------------------------------
// qte_start (Sec20.9, 0x00a5b270). Arguments: arg 1 character (generic
// resolver 0x00a280c0); arg 2 a QTE-definition name - hashed, not resolved
// (0x00d9e8b0 = sr3save::nameHash, CONFIRMED match, see b02); arg 3 a hook
// name; arg 4 optional anchor name (nil-gated; #FOLLOWER#-then-character
// chain 0x00a28150, resolveFollowerAwareObject() - wired 2026-10-08,
// follower-sentinel-resolver pass), absent/unresolved -> the global default pair
// 0x01180028/0x0118002c; arg 5 optional boolean, default false; arg 6
// optional second anchor (nil-gated; 0x005982e0 + liveness), same default.
// Return: none.
//
// Body: arg 1 unresolved -> nothing. Resolved: 0x005e4660(arg 3, &slot) with
// the CONFIRMED hidden this = character +0x1f00 - the named-hook-slot-array
// registration, preset slot 0x18 (CharacterState::qteResultHookCallback;
// empty string clears, same setter as on_death). Then 0x0060dd60's single
// gate 0x008addb0 (controlLockGatePasses): not authoritative -> the opcode-
// 0x59 record (counted); authoritative -> the shared QTE-context slot is
// populated (EngineState::qteStartActivate(): one QteSlotEntry per character,
// the SAME list qte_cleanup, Sec15.6, tears down - see QteSlotEntry's doc
// comment). Not modelled (no Lua-visible result, or OPEN): the up-to-4
// participant resolution (their names come from the QTE definition, not from
// Lua), the hook/animation-state calls on them, the local-player HUD-slot
// routing (HIGH CONFIDENCE-inferred role only), and the 0x014b01xx field
// layout (OPEN, Sec15.30). Distinct from qte_start_m02_skyqte_01 (a separate
// native, another batch): nothing here is keyed or named for it.
// ---------------------------------------------------------------------
int stub_qte_start(lua_State* L) {
    logCall(L, upLog(L), "qte_start", upStateTag(L));
    std::string character = argString(L, 1);
    std::string definition = argString(L, 2);
    std::string hookName = argString(L, 3);
    bool hasAnchor1 = lua_gettop(L) >= 4 && lua_type(L, 4) != LUA_TNIL;
    std::string anchor1 = hasAnchor1 ? argString(L, 4) : std::string();
    bool flag = optionalBoolDefault(L, 5, false);
    bool hasAnchor2 = lua_gettop(L) >= 6 && lua_type(L, 6) != LUA_TNIL;
    std::string anchor2 = hasAnchor2 ? argString(L, 6) : std::string();

    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) return 0; // OPEN until set
    QteSlotEntry entry;
    entry.characterName = character;
    entry.definitionHash = sr3save::nameHash(definition);
    entry.definitionName = definition;
    entry.flag = flag;
    // arg4/anchor1 resolves via the #FOLLOWER#-then-character chain
    // 0x00a28150 (Sec20.9's own text) - wired 2026-10-08 (follower-
    // sentinel-resolver pass). arg6/anchor2 resolves via the DIFFERENT
    // third-tier resolver 0x005982e0 (no #FOLLOWER# acceptance) - unchanged.
    std::string resolvedAnchor1;
    if (hasAnchor1 && resolveFollowerAwareObject(st, anchor1, resolvedAnchor1)) entry.anchor1Name = resolvedAnchor1; // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
    if (hasAnchor2 && st->objectResolves().get(anchor2)) entry.anchor2Name = anchor2; // OPEN until set

    CharacterState& c = st->getOrCreateCharacter(character);
    c.qteResultHookCallback = hookName; // slot 0x18 at base +0x1f00; "" clears
    if (c.controlLockGatePasses.get()) {
        st->qteStartActivate(entry);
    } else {
        ++c.qteStartReplicateCount;
        EngineState::replicateStateChange("qte_start", character); // opcode 0x59
    }
    return 0;
}

} // namespace stubs_b15
} // namespace sr3luahost
