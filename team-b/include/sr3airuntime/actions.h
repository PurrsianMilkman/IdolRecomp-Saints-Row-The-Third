#pragma once

// sr3airuntime/actions.h - AI runtime unit A3: the 70 action routines
// (action-table column +0x08).
//
// Source: spec-ai-runtime.md Sec11, specifically Sec11.1 (the two
// null-routine actions against shipped data), Sec11.2 (the calling
// contract and the block-fields-written table), Sec11.3 (the routine
// families, A-H), Sec11.4 (which goals reach which family), Sec11.5
// (behaviour-record readers), Sec11.6 (helpers settled on the way) and
// Sec11.7 (port notes). Every implemented routine below is CONFIRMED in
// that text; every stub cites the exact Sec11.3 family letter and the
// unread/undecoded helper(s) that block a real implementation.
//
// SCOPE AND THE GEOMETRY SEAM. Several routines' ACCEPT/REFUSE gate is
// fully CONFIRMED while their OUTPUT depends on a spatial query this
// project does not implement (navmesh snapping, path existence, a vehicle
// clearance probe, a spatial "find nearest prop" search - all explicitly
// out of scope per this task's own standing rules: "real pathfinding/
// navmesh queries beyond what the spec text actually gives as a confirmed
// formula" is not built here). Rather than stub the whole routine, this
// file injects the RESULT of such a query as an ActionWorldFacts field
// (e.g. a pre-resolved candidate handle, a pre-snapped point, a precomputed
// clearance bool) and implements the CONFIRMED gating/side-effect logic in
// full around it. This is the same seam idea as sr3player's injectable
// MoveInputSource/SprintStamina - a documented boundary, not a guess at the
// query's answer. Routines whose CONFIRMED text gives no gating structure
// at all beyond "calls an unread N-thousand-instruction function" would be
// left as OpenStateError stubs instead, since there would be no CONFIRMED
// control flow to wrap - this was the case for Sec11.3 H's two trajectory
// solvers (actions 4 `throw grenade`, 5 `throw weapon`) until unit A5
// (Sec13.2) read both solvers' own gating/obstacle-screening structure and
// Unicorn-emulated their shared ballistic-angle core against 11 real
// input/output vectors; both are now implemented for real (see actions.h's
// own "H: thrown weapons" ActionWorldFacts fields and
// SolveThrowTrajectory).
//
// THE 8 NON-PURE ROUTINES (Sec11.2: "Not pure"). Actions 26, 45, 46, 51
// back off on refusal; 50 backs itself off by 1000ms BEFORE testing
// anything; 61 backs off a DIFFERENT action (58); 42 and 43 write a
// navmesh-snapped position back into the perception entry they inspect and
// clear its flag 0x10 when the snap moved it. This file's ActionRegistry
// calls every routine uniformly (the chooser and the re-assert path, per
// Sec9.4) - it is the CALL SITES in decision.h's Chooser() that are
// disciplined (only those two places ever invoke a routine), not a special
// code path inside this file. This unit's own tests exercise exactly that:
// the side effects fire when Chooser() calls these routines, and nowhere
// else a routine might otherwise be probed from.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sr3airuntime/common.h"
#include "sr3airuntime/decision.h"
#include "sr3luahost/open_state.h"

namespace sr3airuntime {

using sr3luahost::OpenStateError;

// --- the geometry/helper seam (see this header's own SCOPE note) -----------------
struct ActionWorldFacts {
    // D: shooting (0,1,2,3). "Can fire" (0x0050c890/0x0050c930) depends on
    // weapon/animation/seat state this project's AI unit does not own.
    bool canFireNow = false;          // 0x0050c890(human, false)
    bool canFireWithGrace750 = false; // 0x0050c930(human, 750) (action 0)
    bool canFireWithGrace1000 = false;// 0x0050c930(human, 1000) (action 13)
    bool fireSweepCase1 = false;      // 0x005e6ce0 && in-range chain (action 1)
    bool fireSweepBruteCase = false;  // brute + clip>=0.9 + >=3 hostiles in arc (action 1)

    // E: cover (8,10,11,12,13).
    double coverApproachRadius = 0;          // 0x024fb1c0 (tunable)
    double coverFireRadius = 0;              // 0x024fb1d0 (tunable)
    bool atOwnCoverPoint = false;            // 0x00540a70(human,...) (Sec11.6; also used by 26, 44)
    bool coverEvaluatorApproves = false;     // 0x00509e50 (action 11)
    bool coverAnim1d92Playing = false;       // action 11/12
    bool coverAnim1d93Playing = false;       // action 12
    bool coverShieldsFromTarget = true;      // 0x008cd1d0(cover,T,T pos)==0 (action 12)
    bool flag76fBit5 = false;                // action 13
    bool coverObjectFlag8cBit1 = false;      // action 13
    bool flag985cd0 = false;                 // 0x00985cd0 (actions 13, 26, 68)
    Vec3 coverFireGeomPoint;                 // action 11's 0x005422a0 fill (+0x24)
    Vec3 coverFireGeomTertiary;              // action 11's 0x005422a0 fill (+0x30)
    uint32_t coverFireGeomValue40 = 0;       // action 11's 0x005422a0 fill (+0x40)
    uint64_t coverObjectHandle = 0;          // actions 11, 13
    double flagE3c = 0;                      // human +0xe3c (action 11; must be <= 1.0)

    // B: movement to a computed destination (16,18,19/20,21,22,23,25,27,28,29,41,66,67).
    bool isPlayerLedGroupMember = false;     // 0x0097e7b0 + player class (actions 18, 27, 28)
    bool targetFlagE8Bit100 = false;         // action 22
    bool pathfindSucceeded = false;          // 0x004e91c0 (actions 22, 23)
    bool targetVehicleOk = false;            // T's vehicle +0x5c>=0.1 etc. (action 52/53; also gates 23's riot path, unused here)
    Vec3 precomputedPoint = UnsetPosition(); // the host-resolved navmesh/geometry point for whichever routine needs one
    double postAreaRadius = -1;              // AI sub-object +0x40 (actions 16, 27, 28, 50, 51); <0 = "not set"
    Vec3 postAreaPosition = UnsetPosition();
    bool postAreaPositionSet = false;
    bool flag315Bit4 = false, flag315Bit5 = false, flag315Bit6 = false;  // actions 27, 28

    // 26 move sidestep (NON-PURE):
    bool targetWithinFacingCone = false;     // 45 degree cone test (not an in-scope orientation model)
    bool dodgeRollSucceeded = false;         // the dodge_chance percentage roll (0x004dd950)
    bool sidestepCandidateFound = false;
    int32_t sidestepVariant = 0;             // 1, 2 or 3

    // F: object-picking (33,37,38,39,42,43,45,46,50,51,52,63,64).
    bool followerPlayerHasTarget = false;    // action 41
    bool candidateFlagPositionVerified = false;  // actions 41(not required there)/42/43 - PositionVerified gate
    Vec3 snappedCandidatePosition = UnsetPosition();  // actions 42, 43 (the navmesh-snapped result)
    bool snappedPositionDiffersBeyondThreshold = false;  // actions 42, 43 (>0.25 on any axis -> clear flag 0x10)
    bool flag4a5Bit4 = false;                // action 42
    uint64_t peekCoverObjectHandle = 0;      // action 43 (0x008d1d80)
    bool peekSide = false;                   // action 43 (+0x55 bit 1)
    bool candidateFoundForInvestigate = false;  // used with PerceptionState's own query result normally; kept for clarity

    bool canGrabVictim = false;              // 0x009aa520 (action 46)
    Vec3 groupLeaderPosition;                // actions 46 (seperated_distance check), 17/32/34 reuse WorldFacts leash fields instead
    bool threatSeenInLast10s = false;        // action 46 (perception.timeSinceLastSeen used instead when a handle is known)
    uint64_t humanShieldCandidateHandle = 0;
    bool humanShieldCandidateFound = false;

    bool targetHeldBySomeone = false;        // action 51 (0x009aa360 on T)
    int32_t targetHolderAttitude = 4;        // action 51 ("hostile to the holder" needs attitude<3)
    uint64_t propCandidateHandle = 0;        // action 51
    bool propCandidateFound = false;

    bool pickupCandidateFound = false;       // action 45
    uint64_t pickupCandidateHandle = 0;

    uint64_t quickKillCandidateHandle = 0;   // action 63 (0x004fe730/0x00963d40/riot-shield/velocity-prediction chain)
    bool quickKillCandidateFound = false;

    bool corpseCandidateFound = false;       // action 64
    uint64_t corpseCandidateHandle = 0;
    bool zombieEatGateOk = false;            // 0x0097cdd0/+0x1c9a (action 64)
    bool flag1c98Bit17 = false;              // actions 64 (negated), 65

    bool selfCapturedOrLocked = false;       // 0x0060b530(human) (actions 51, 65, 54, 53)
    bool targetCapturedOrLocked = false;     // 0x0060b530(T)
    double zombieExplodeRadius = 0;          // 0x013bacc4 -> +0x28 scaled by 0.75 already applied by host

    // G: pure gates (17,25,30,32,33,34,35,36,40,44,47,48,53-62,65,68,69).
    bool offNavmesh = false;                 // +0xc38 bit 7 (action 25)
    bool leashAnchorIsLeader = false;        // actions 17, 32, 34 ("the same leash test as 17")
    bool leashRadiusSet = false;
    double leashRadius = 0;
    Vec3 groupOrFollowLeaderPosition;        // actions 17, 32, 34, 30, 36
    bool groupLeaderOnAnotherLevel = false;  // action 17 (0x004de500 != 0)
    bool groupLeaderOutOfVehicle = false;    // action 32
    bool flag76fBit5ForFollow = false;       // action 32 (distinct instance name from cover's own flag76fBit5 above)
    double followTeleportRadius = 0;         // action 34 (0x013bae40)
    bool followedPlayerExists = false;       // actions 34, 35, 40 (reuses LiveTarget-style gating is wrong here; explicit)
    bool humanHeldInGrabForTeleport = false; // action 35 (0x009aa360)
    bool flag0095d740 = false;               // action 35
    bool flag0091f310_2 = false;             // action 35
    bool resolves0x004d7e00WithoutFlag = false;  // action 35
    bool playerInVehicleForTeleport = false; // action 35
    double followedPlayerHeightDiff = 0;     // action 35
    double followedPlayerHorizDist = 0;      // action 35
    bool flag009b5690Self = false;           // action 35
    bool flag009b5690Player = false;         // action 35
    bool followedPlayerInVehicleState = false;  // action 30
    bool flag009a1dc0ForFollowMakeWay = false;  // action 30
    double followMakeWayRadius = 0;          // action 30
    bool followLemmingGlobalNonzero = false; // action 36
    bool flag317Bit1 = false;                // action 36
    bool flag009ba8b0 = false;               // action 36

    bool vehicleExtractLevelChancePassed = false;  // action 50 (notoriety ramp roll)
    bool vehicleExtractVehicleOk = false;
    bool vehicleExtractDoorClear = false;
    bool noGroupMemberDoingSameAction = false;  // actions 23 (riot), 50
    uint64_t followAcquireAllyHandle = 0;    // action 33
    bool followAcquireFound = false;

    bool flag0098da50 = false;               // actions 43 (gate), 44 (refusal)

    int32_t targetStateCce = 0;              // action 59 (T's +0xcce)
    bool rollerBladerDescriptorBit = false;  // action 68

    bool targetIsPlayerClassForBrute = false;  // actions 54, 56
    bool globalShockwaveMinApplies = false;    // actions 57, 60 (global int 0x012ec720 > 1)
    bool targetRagdoll = false;                // T's 0x009a1dc0 state (actions 56, 59(negated-ish), 61, 30)
    bool flag004e66b0 = false;                 // action 56
    bool vectorTest1cec = false;                // action 55 (the −12.0 vector test)
    bool flag005031b0 = false;                  // actions 54, 55
    bool flag00963d40 = false;                  // actions 54, 55
    bool targetDb0Check = false;                // action 55 (T +0xdb0 != 0x00456940(...))
    bool globalBit022cce86Clear = true;         // action 54
    bool is0x009422dClassForQte = false;        // action 54
    bool targetIs0x00942220 = false;             // action 54
    bool selfNot0097cdd0 = false;                // action 54
    bool globalTimer012e27f0Passed = false;      // action 54
    int32_t list5HumansFacingTargetCount = 0;    // action 54
    int32_t list5HumansFacingTargetThreshold = 0;// action 54 (0x013ba814)

    bool noListEighteenEntryYoungerThan500 = true;  // action 53 (shared with 51's prop/list18-style gating names)
    bool targetWithinFortyFiveDegrees = false;       // action 53
    bool clearanceProbesOk = true;                   // action 53
    bool navmeshPathExistsBetweenHumans = false;      // action 53

    // 17 move regroup / 32 follow formation / 34 follow player (leash family):
    bool targetIsUnarmedBrute = false;     // 0x0052a240(T) (action 17, +10.0 to the limit)
    double followPlayerRadius = 0;         // 0x013bae40 (action 34's own run-time radius)

    // 6 melee / 7 melee vehicle / 15 melee stand back / 23 move rush to melee:
    bool canMeleeGate = false;             // 0x00551d90 ("can melee")
    double meleeRangeA = 0, meleeRangeB = 0;  // the melee-range band (distinct from weaponRangeA/B)
    bool flag0095d740ForMelee = false;     // 0x0095d740(T)
    bool flag0098a3f0 = false;             // 0x0098a3f0(T)

    // H: thrown weapons - 5 `throw weapon` (0x005531a0) and 4 `throw
    // grenade` (0x0054dce0), unit A5's Sec11.3 H / Sec13.2. Both ultimately
    // call the now-oracle-tested shared core SolveThrowTrajectory below
    // (Sec13.2.1's 0x0054c630). arc/speed are held-item tunables this
    // project has no weapon/inventory runtime to resolve
    // ([weapon_info+0x35c] for arc at both call sites; speed is
    // class-picked for the weapon throw - 0x0054c770's own "+0x350 vs
    // +0x34c" by 0x00853b30(human,0x22), a DIFFERENT class bit than the
    // "is player" 0x21 used elsewhere, Sec13.2's own OPEN flag - and fixed
    // at [this+0x1fc] for the grenade, no player/non-player branch there),
    // so both are host-resolved per the project's established weapon-range
    // seam (WorldFacts::weaponRangeA/B's own "already resolved by the host"
    // convention).
    double throwArc = 0;    // [weapon_info+0x35c] (both actions 4 and 5)
    double throwSpeed = 0;  // action 5: class-picked by the host; action 4: the grenade's own fixed [this+0x1fc] speed

    // 5 `throw weapon` (0x005531a0, Sec11.3 H):
    bool heldItemThrowableNow = false;   // 0x00b88610 (throwable) AND usable from the current seat, combined
    bool targetInVehicleForThrow = false;  // "T not in a vehicle" - true here means IN a vehicle, i.e. refuse

    // 4 `throw grenade` (0x0054dce0, 1822 instructions, read for structure
    // only - Sec13.2.2):
    bool grenadeInHand = false;        // weapon info +0x348 ("a grenade in either hand")
    bool canThrowGrenadeNow = false;   // 0x0050c930(human, 750) (this action's own instance of the can-fire-with-grace test)
    bool grenadeHasTarget = false;     // T (LiveTarget) or the threat resolver 0x004e6bb0 fallback, combined
    bool grenadePrecondition0x0054d180 = true;  // 0x0054d930's own first call, "a precondition this unit did not read further into"; default pass, host may force false
    double grenadeAvoidRadius = 0;              // [this+0x1d0]: the per-call SIMD proximity-reject radius
    std::vector<Vec3> grenadeAvoidPositions;    // "allies to avoid" (list 5 + the manager's human lists, 0x0054ca40) - positions only, host-resolved
    std::vector<Vec3> grenadeCandidatePoints;   // aim points tried in turn: P, T's cover object, nearby objects (host-resolved; real geometry out of scope)
};

// --- Sec13.2.1: the shared ballistic-angle core 0x0054c630(outA, outB,
// originPoint, targetPoint, arc, speed) - cdecl, 6 args --------------------
// CONFIRMED structure (delta/degenerate-guard/angle-solve/basis-rotate,
// Sec13.2.1 steps 1-4); the exact transcendental pair (0x00ea2470/
// 0x00ea3a70) was not pinned down statically, so the closed-form angle
// formula below is reconstructed from, and validated to full float
// precision against, Sec13.2.1's own 11-vector Unicorn emulation oracle
// (real function bytes, zero stubs needed for the core itself) rather than
// quoted from a decompile - exactly the "oracle settles the geometric
// effect even without the exact function names" approach that section's
// own text describes. See SolveThrowTrajectory's own .cpp comment for the
// derivation and the per-vector match. `arc`/`speed` are plain values (not
// pointers), matching the real cdecl signature.
struct ThrowTrajectoryResult {
    bool solved = false;  // AL's own contract: propagated from the first sub-call (0x0054c1d0); modelled here as "a real solution exists" (the discriminant below is non-negative)
    Vec3 outA;  // the "low"/flatter root - the ONLY buffer either action routine's own caller reads (Sec13.2.1's own "+0x50..0x58" copy note)
    Vec3 outB;  // the "high"/lobbed complementary root - computed and returned for fidelity; OPEN item4: whether any OTHER, unexamined caller of 0x0054c630 consumes it
};
ThrowTrajectoryResult SolveThrowTrajectory(const Vec3& origin, const Vec3& target, double arc, double speed);

// --- the registry -------------------------------------------------------------------
class ActionRegistry {
public:
    ActionRegistry();

    // Calls the routine for actionId, or throws OpenStateError if it is a
    // null-routine action (defensive: the chooser already guards this,
    // Sec9.5/Sec11.1 - this is belt-and-braces, never "calling address 0")
    // or an unimplemented one (citing the Sec11.3 family and the blocking
    // helper(s)). `facts` is the geometry/helper seam above - a separate
    // parameter (not a HumanAiState member) so decision.h (A1) does not
    // need to know this A3-only type.
    bool call(int actionId, HumanAiState& h, ActionWorldFacts& facts, ActionBlock& block, int64_t nowMs);
    bool isImplemented(int actionId) const { return static_cast<bool>(routines_[actionId]); }

    // Wires h.actionRoutine to this registry for one decision call (nowMs
    // and facts fixed for that call, per Sec5's "no time-step argument" -
    // the routines themselves read timers through h, not through an
    // argument). `facts` must outlive the wiring (the host's per-call
    // local, refreshed before each DecisionStep/Chooser call).
    void wire(HumanAiState& h, ActionWorldFacts& facts, int64_t nowMs) {
        h.actionRoutine = [this, &h, &facts, nowMs](int actionId, ActionBlock& block) {
            return this->call(actionId, h, facts, block, nowMs);
        };
    }

private:
    using Routine = std::function<bool(HumanAiState&, ActionWorldFacts&, ActionBlock&, int64_t)>;
    std::array<Routine, action::kCount> routines_;
    void reg(int actionId, Routine r) { routines_[actionId] = std::move(r); }
};

}  // namespace sr3airuntime
