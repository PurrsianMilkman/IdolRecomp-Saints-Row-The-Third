#include "sr3airuntime/actions.h"

#include <algorithm>
#include <cmath>

namespace sr3airuntime {

namespace {
bool AlwaysAccept(HumanAiState&, ActionWorldFacts&, ActionBlock&, int64_t) { return true; }
}  // namespace

// Sec13.2.1 (unit A5): the shared ballistic-angle core 0x0054c630. See
// actions.h's own doc comment on ThrowTrajectoryResult/SolveThrowTrajectory
// for what is CONFIRMED-by-listing (the overall structure: delta,
// degenerate guard, angle solve, basis rotate, two output buffers) versus
// reconstructed from the oracle (the exact angle formula, since the real
// trig pair 0x00ea2470/0x00ea3a70 was not pinned down statically).
//
// Reconstruction. Let d2 = delta.x^2 + delta.z^2 (horizontal distance
// squared, the exact quantity 0x00da0730 reads per Sec13.2.1 step2),
// d = sqrt(d2), h = delta.y, a = arc*d2/(2*speed^2) (the standard
// "0.5*g*(d/v)^2" drop-over-distance term). The two angles this core
// solves for are the two roots of
//     a*u^2 + d*u + (a - h) = 0,   u = tan(theta)
// - the ordinary flat/lobbed ballistic-range pair, with the human's own
// gravity-drop term carrying a SIGN such that h=0 gives a small NEGATIVE
// outA.y (confirmed by every h=0 oracle row below) rather than the
// small-positive "aim up to counter drop" a Y-up convention would predict;
// i.e. this core's own local Y axis for THIS vector reads as the opposite
// sense from world "up" (Sec13.2.1's own OPEN item5 flags exactly this
// ambiguity - this reconstruction settles the GEOMETRIC EFFECT precisely,
// per that section's own stated approach, without resolving which axis
// convention produces it). Each root u maps to a unit vector via
// cos(theta) = 1/sqrt(1+u^2) (taken positive - every oracle row's
// outA.x/outB.x is positive), sin(theta) = u*cos(theta):
//     result = (cos(theta)*dirX, sin(theta), cos(theta)*dirZ)
// where (dirX, dirZ) = (delta.x, delta.z)/d. outA uses the root taken with
// the quadratic formula's "-d + sqrt(disc)" numerator (the smaller-
// magnitude, flatter root); outB uses "-d - sqrt(disc)" (the complementary,
// lobbed root) - matching Sec13.2.1's own confirmed numeric relationship
// ("outB's (X,Z) pair is outA's (Z,X) pair, sign-adjusted... outB.y =
// -(outA's own horizontal magnitude)").
//
// Validated to within float precision against all 11 of Sec13.2.1's own
// Unicorn-oracle vectors (tests/synthetic_ai_runtime_test.cpp's own
// oracle-vector checks reproduce the match for the record).
ThrowTrajectoryResult SolveThrowTrajectory(const Vec3& origin, const Vec3& target, double arc, double speed) {
    ThrowTrajectoryResult result;
    Vec3 delta = target - origin;

    // Step 2: degenerate-input guard ("is approximately zero", checked via
    // 0x00dad830 - the exact helper+threshold (|v| < 1e-4) this project's
    // own src/player_movement.cpp already cites for this identical
    // address).
    double d2 = delta.x * delta.x + delta.z * delta.z;
    if (d2 < 1e-4) {
        // "Overwrites delta with a fixed default (const, 0, 0) direction."
        // The exact literal "const" is not given by the spec text; (1,0,0)
        // is a JUDGMENT CALL placeholder for this degenerate (origin and
        // target coincide horizontally) case, which has no meaningful throw
        // direction regardless of the exact constant used.
        delta = Vec3{1.0, 0.0, 0.0};
        d2 = 1.0;
    }

    const double d = std::sqrt(d2);
    const double dirX = delta.x / d;
    const double dirZ = delta.z / d;
    const double h = delta.y;

    auto rootToVec = [&](double u) {
        const double cosT = 1.0 / std::sqrt(1.0 + u * u);
        const double sinT = u * cosT;
        return Vec3{cosT * dirX, sinT, cosT * dirZ};
    };

    if (speed == 0.0) {
        return result;  // no meaningful ballistic solve at zero speed; solved = false
    }

    const double a = arc * d2 / (2.0 * speed * speed);
    if (std::fabs(a) < 1e-12) {
        // arc ~ 0: the quadratic collapses to a single linear solution
        // (h = d*u), confirmed by the oracle's own "arc = 0.0" row
        // (outA == outB == the flat direction, to full float precision).
        const double u = h / d;
        result.outA = rootToVec(u);
        result.outB = result.outA;
        result.solved = true;
        return result;
    }

    const double disc = d * d - 4.0 * a * (a - h);
    if (disc < 0.0) {
        return result;  // AL=0: no real ballistic solution at this speed/arc/geometry
    }
    const double sqrtDisc = std::sqrt(disc);
    const double u1 = (-d + sqrtDisc) / (2.0 * a);
    const double u2 = (-d - sqrtDisc) / (2.0 * a);

    result.outA = rootToVec(u1);
    result.outB = rootToVec(u2);
    result.solved = true;
    return result;
}

ActionRegistry::ActionRegistry() {
    // ---- Family A: shared stubs (Sec11.3 A) --------------------------------------
    reg(action::CoverTakeOff, AlwaysAccept);
    reg(action::HoldPosition, AlwaysAccept);
    reg(action::AvatarStomp, [](HumanAiState&, ActionWorldFacts&, ActionBlock&, int64_t) {
        return false;  // the shared return-false stub 0x006948c0 - never chosen by a goal (Sec11.3 A)
    });

    // ---- Family B: movement to a computed destination (Sec11.3 B) ---------------
    auto moveChase = [](HumanAiState& h, ActionWorldFacts&, ActionBlock& block, int64_t nowMs) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        const PerceptionEntry* e18 = h.perception.find(plist::CoverMemory18, t.handle);
        if (e18 && (nowMs - e18->noticedAtMs) < 1500) return false;
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        if (!e0) return false;
        block.destination = e0->position;  // P, approximated by the list-0 entry's own position
        return true;
    };
    reg(action::MoveChase, moveChase);
    reg(action::AvatarChase, moveChase);  // 20 jumps to 19's routine (Sec11.3 A)

    reg(action::KillbaneChase, [](HumanAiState& h, ActionWorldFacts&, ActionBlock&, int64_t nowMs) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        const PerceptionEntry* e18 = h.perception.find(plist::CoverMemory18, t.handle);
        if (e18 && (nowMs - e18->noticedAtMs) < 500) return false;
        return true;  // "writes nothing"
    });

    reg(action::MoveAdvance, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        if (f.isPlayerLedGroupMember) return false;
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        if (!e0) return false;
        const bool E = h.perception.engageable(t.handle, h.behavior.killEnemySeenInLastMs, nowMs);
        const bool A = WeaponBandTest(h.world.weaponRangeA, h.world.weaponRangeB, Distance(h.position, e0->position)) <= 0;
        const bool B = h.world.stateCcd == 9;
        bool refuse;
        if (h.behavior.combatAggression == 0) refuse = E || A || B;
        else if (h.behavior.combatAggression == 2) refuse = E && A;
        else refuse = E && (A || B);
        if (refuse) return false;
        block.destination = e0->position;
        return true;
    });

    reg(action::MoveRush, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        if (!e0) return false;
        const PerceptionEntry* e18 = h.perception.find(plist::CoverMemory18, t.handle);
        if (e18 && (nowMs - e18->noticedAtMs) < 500) return false;
        if (f.targetFlagE8Bit100) return false;
        if (!f.pathfindSucceeded) return false;
        if (h.mode.state.currentAction != action::MoveRush) {
            const double limit = std::max(h.world.weaponRangeB, 10.0);
            if (Distance(h.position, e0->position) < limit) return false;
        }
        block.destination = e0->position;
        return true;
    });

    reg(action::MoveCloseIn, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        if (h.world.isFollower) return false;
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        const bool engageable = h.perception.engageable(t.handle, h.behavior.killEnemySeenInLastMs, nowMs);
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        if (!e0) return false;
        const bool insideBand =
            WeaponBandTest(h.world.weaponRangeA, h.world.weaponRangeB, Distance(h.position, e0->position)) == 0;
        if (!(engageable && insideBand)) return false;
        // The riot-shield formation sub-case (Sec11.3 B) is not reproduced -
        // this always uses the base "point toward T" result.
        if (IsUnsetPosition(f.precomputedPoint)) return false;
        block.destination = f.precomputedPoint;
        return true;
    });

    reg(action::MoveSurround, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t) {
        if (!LiveTarget(h).has) return false;
        if (IsUnsetPosition(f.precomputedPoint)) return false;
        block.destination = f.precomputedPoint;
        return true;
    });

    reg(action::MoveRetreat, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        if (f.postAreaRadius >= 0 && f.postAreaRadius < 6.0) return false;
        const bool onlyBrutes = h.behavior.goalEnabled[goal::AvoidBrute] != 0;
        uint64_t bestHandle = 0;
        double bestRadius = -1;
        for (const auto& e : h.perception.entries(plist::KnownHostiles)) {
            if (!(e.flags & pflag::ActivelyKnown)) continue;
            if ((nowMs - e.noticedAtMs) > 3000) continue;
            // "Skipping entities on another level" (0x004de500) is not
            // modelled per-entry here (that helper is only wired to the
            // human's own live target elsewhere in this port) - a
            // documented simplification.
            const bool brute = h.world.isBruteFn && h.world.isBruteFn(e.handle);
            if (onlyBrutes && !brute) continue;
            const double radius = brute ? (h.world.isBruteWeaponInfo12 ? 15.0 : 10.0) : h.world.weaponRangeB;
            if (Distance(h.position, e.position) < radius && radius > bestRadius) {
                bestRadius = radius;
                bestHandle = e.handle;
            }
        }
        if (bestHandle == 0) return false;
        if (IsUnsetPosition(f.precomputedPoint)) return false;
        block.objectHandle = bestHandle;
        block.destination = f.precomputedPoint;
        return true;
    });

    // NON-PURE (Sec11.2): backs off on refusal, at two different durations.
    reg(action::MoveSidestep, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        if (h.world.inVehicle) return false;
        if (f.flag985cd0) return false;
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        if (!h.perception.targetLineOfSightValidOrNone(true, t.handle)) return false;
        if (f.atOwnCoverPoint) return false;
        if (!f.targetWithinFacingCone) return false;
        const bool engageable = h.perception.engageable(t.handle, h.behavior.killEnemySeenInLastMs, nowMs);
        if (engageable) {
            // "a percentage roll against dodge_chance must succeed, and
            // THEN either T is aiming at the human or list 3 is non-empty.
            // Otherwise back-off 1000ms" (Sec11.3 B) - the back-off covers
            // BOTH failure modes (a failed roll, or a successful roll with
            // neither aimed-at nor shot-at), not just the second.
            const bool aimedOrShotAt = h.world.targetAimingAtHuman || !h.perception.entries(plist::ShotsAtMe).empty();
            if (!f.dodgeRollSucceeded || !aimedOrShotAt) {
                h.actionRepeatTimers[action::MoveSidestep].arm(nowMs, 1000);
                return false;
            }
        }
        if (!f.sidestepCandidateFound) {
            h.actionRepeatTimers[action::MoveSidestep].arm(nowMs, 5000);
            return false;
        }
        block.destination = f.precomputedPoint;
        block.value40 = static_cast<uint32_t>(f.sidestepVariant);
        return true;
    });

    reg(action::MoveToPostCombat, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        if (f.isPlayerLedGroupMember) return false;
        const bool ok = f.flag315Bit6 || !Agree0x004e50e0(h);
        if (!ok || !f.flag315Bit5 || !GoalAcquireTarget(h, nowMs)) return false;
        if (!(f.postAreaRadius >= 0 && f.postAreaPositionSet)) return false;
        const double radius = std::max(1.0, f.postAreaRadius);
        if (Distance(h.position, f.postAreaPosition) < radius) return false;
        block.destination = f.postAreaPosition;
        return true;
    });
    reg(action::MoveToPostIdle, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t) {
        if (f.isPlayerLedGroupMember) return false;
        const bool ok = f.flag315Bit6 || !Agree0x004e50e0(h);
        if (!ok) return false;
        if (!(f.flag315Bit4 || f.flag315Bit5 || f.flag315Bit6)) return false;
        if (h.forceFlags.inScriptedCover) return false;
        if (!(f.postAreaRadius >= 0 && f.postAreaPositionSet)) return false;
        block.destination = f.postAreaPosition;
        return true;
    });

    reg(action::MoveToNavmesh, [](HumanAiState&, ActionWorldFacts& f, ActionBlock&, int64_t) { return f.offNavmesh; });

    reg(action::MoveRegroup, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (f.leashRadiusSet && !f.leashAnchorIsLeader) return false;
        double limit = h.world.flag317Bit0 ? h.behavior.seperatedDistanceInterior : h.behavior.seperatedDistance;
        if (f.targetIsUnarmedBrute) limit += 10.0;
        if (f.leashRadiusSet && f.leashRadius < limit) limit = f.leashRadius;
        if (Distance(h.position, f.groupOrFollowLeaderPosition) > limit) return true;
        return f.groupLeaderOnAnotherLevel;
    });

    reg(action::KillbaneStrafe, [](HumanAiState& h, ActionWorldFacts&, ActionBlock& block, int64_t) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        if (!e0 || DistanceSq(h.position, e0->position) < 6.25) return false;
        block.timer14Ms = 0;
        block.smallIndex = 0;
        block.smallIndex2 = 0;
        return true;
    });

    // ---- Family C: approach a hostile for a strike (Sec11.3 C) ------------------
    reg(action::Melee, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t) {
        if (!f.canMeleeGate) return false;
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        if (!e0) return false;
        if (WeaponBandTest(f.meleeRangeA, f.meleeRangeB, Distance(h.position, e0->position)) != 0) return false;
        if (h.world.inVehicle || f.targetRagdoll || f.flag0095d740ForMelee || f.flag0098a3f0) return false;
        // Brute cases and the riot-shield rule (Sec11.3 C) are checked
        // separately in the original and are not reproduced here.
        block.secondaryPoint = e0->position;  // the strike point, approximated by T's own position
        return true;
    });

    reg(action::FollowAvoidLOF, [](HumanAiState& h, ActionWorldFacts&, ActionBlock& block, int64_t) {
        if (!HasQueuedEventKind(h.mode.state, 5)) return false;
        block.objectHandle = h.mode.state.secondaryHandle458;
        return true;
    });

    // ---- Family D: shooting (Sec11.3 D) ------------------------------------------
    reg(action::Fire, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t nowMs) {
        if (!f.canFireWithGrace750) return false;
        const LiveTargetResult t = LiveTarget(h);
        return t.has && h.perception.engageable(t.handle, h.behavior.killEnemySeenInLastMs, nowMs);
    });
    reg(action::FireSweep, [](HumanAiState&, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (!f.canFireNow) return false;
        return f.fireSweepCase1 || f.fireSweepBruteCase;
    });
    reg(action::FireSuppress, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t nowMs) {
        if (!f.canFireNow) return false;
        if (!f.atOwnCoverPoint) return false;
        if (h.perception.hasAllyLink) return false;
        const LiveTargetResult t = LiveTarget(h);
        return t.has && h.perception.engageable(t.handle, h.behavior.killEnemySeenInLastMs, nowMs);
    });
    reg(action::FireChaos, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (!f.canFireNow) return false;
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        return !h.perception.visibleNow(t.handle, h.world.hasCoverObject);
    });

    // ---- Family E: cover (Sec11.3 E) ---------------------------------------------
    reg(action::CoverTakeDef, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t nowMs) {
        if (h.coverStayDownTimer.isArmed() && h.coverStayDownTimer.elapsedMs(nowMs) < h.behavior.popUpTimeMs) {
            return false;
        }
        if (h.coverFindNewTimer.expired(nowMs) || !h.world.hasCoverObject) return true;
        return !f.atOwnCoverPoint;
    });
    reg(action::CoverAdvance, [](HumanAiState& h, ActionWorldFacts&, ActionBlock&, int64_t nowMs) {
        return h.coverTimeInCoverTimer.elapsedMs(nowMs) >= h.behavior.advanceCoverTimeMs;
    });
    reg(action::CoverStayDown, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t) {
        if (!h.behavior.abilities[action::CoverStayDown].enabled) return false;  // redundant recheck (Sec11.3 E)
        if (f.flagE3c > 1.0) return false;
        const double radius = (h.world.stateCce == 16) ? f.coverApproachRadius : f.coverFireRadius;
        if (!h.world.hasCoverObject || Distance(h.position, h.world.coverObjectPosition) > radius) return false;
        if (!h.forceFlags.inScriptedCover && !f.coverEvaluatorApproves) return false;
        if (!f.coverAnim1d92Playing) {
            block.secondaryPoint = f.coverFireGeomPoint;
            block.tertiaryVector = f.coverFireGeomTertiary;
            block.value40 = f.coverFireGeomValue40;
        }
        block.objectHandle = f.coverObjectHandle;
        return true;
    });
    reg(action::CoverFire, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t nowMs) {
        const LiveTargetResult t = LiveTarget(h);
        if (!h.perception.targetLineOfSightValidOrNone(t.has, t.handle)) return false;
        if (!t.has) return false;
        if (!h.perception.visibleNow(t.handle, h.world.hasCoverObject)) return false;
        if (!h.perception.engageable(t.handle, h.behavior.killEnemySeenInLastMs, nowMs)) return false;
        if (!f.atOwnCoverPoint) return false;
        if (f.coverAnim1d92Playing || f.coverAnim1d93Playing) return false;
        return !f.coverShieldsFromTarget;
    });
    reg(action::CoverPopout, [this](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        const bool forced = (block.flags56 & ActionBlock::kFlag56_ForcedRecheck) != 0;
        if (!forced && GoalCover(h, nowMs)) return false;
        if (!f.atOwnCoverPoint) return false;
        if (!f.canFireWithGrace1000) return false;
        if (!forced) {
            ActionBlock probe = block;
            if (routines_[action::CoverFire] && routines_[action::CoverFire](h, f, probe, nowMs)) return false;
        }
        if (f.flag76fBit5) return false;
        if (!(f.coverObjectFlag8cBit1 || f.flag985cd0)) return false;
        return true;
    });

    // ---- Family F: picking an object to act on (Sec11.3 F) ----------------------
    // NON-PURE (Sec11.2): back off on refusal.
    reg(action::PickupWeapon, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        if (h.forceFlags.cantPickupWeapons) return false;
        if (!f.pickupCandidateFound) {
            h.actionRepeatTimers[action::PickupWeapon].arm(nowMs, 700);
            return false;
        }
        block.objectHandle = f.pickupCandidateHandle;
        return true;
    });
    reg(action::HumanShieldGrab, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        if (!f.canGrabVictim) return false;
        if (h.world.isFollower) {
            const double limit = h.world.flag317Bit0 ? h.behavior.seperatedDistanceInterior : h.behavior.seperatedDistance;
            if (Distance(h.position, f.groupLeaderPosition) > limit) return false;
        }
        if (!f.humanShieldCandidateFound) {
            h.actionRepeatTimers[action::HumanShieldGrab].arm(nowMs, 400);  // random 300-500ms; deterministic midpoint
            return false;
        }
        block.objectHandle = f.humanShieldCandidateHandle;
        return true;
    });
    reg(action::BruteThrowProp, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has || f.targetCapturedOrLocked) return false;
        if (f.targetHeldBySomeone && f.targetHolderAttitude >= 3) return false;
        if (!f.propCandidateFound) {
            h.actionRepeatTimers[action::BruteThrowProp].arm(nowMs, 1000);
            return false;
        }
        block.objectHandle = f.propCandidateHandle;
        return true;
    });
    // NON-PURE (Sec11.2): backs ITSELF off 1000ms BEFORE testing anything.
    reg(action::VehicleExtract, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        h.actionRepeatTimers[action::VehicleExtract].arm(nowMs, 1000);
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        if (!f.vehicleExtractLevelChancePassed) return false;
        if (!f.vehicleExtractVehicleOk) return false;
        if (!f.vehicleExtractDoorClear) return false;
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        if (!e0) return false;
        const double dx = e0->position.x - h.position.x;
        const double dz = e0->position.z - h.position.z;
        const double dy = e0->position.y - h.position.y;
        if (std::sqrt(dx * dx + dz * dz) > 400.0 || std::abs(dy) > 3.0) return false;
        if (f.postAreaRadius >= 0 && Distance(h.position, f.postAreaPosition) > f.postAreaRadius) return false;
        if (!f.noGroupMemberDoingSameAction) return false;
        block.objectHandle = t.handle;
        return true;
    });
    reg(action::FollowAcquire, [](HumanAiState&, ActionWorldFacts& f, ActionBlock& block, int64_t) {
        if (f.leashRadiusSet) return false;  // "no leash radius set (negative +0x2f0)"
        if (!f.followAcquireFound) return false;
        block.objectHandle = f.followAcquireAllyHandle;
        return true;
    });
    reg(action::InvestigateMove, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        auto rows = PerceptionState::InvestigationCandidateRows(h.behavior.investigateSeenInLastMs, h.world.isFollower);
        const PerceptionEntry* cand = h.perception.query(rows, nowMs);
        if (!cand || (cand->flags & pflag::WrittenOff)) return false;
        if (h.world.isFollower && !f.followerPlayerHasTarget) return false;
        block.destination = cand->position;
        block.objectHandle = cand->handle;
        block.smallIndex = static_cast<uint8_t>(cand->listNumber);
        return true;
    });
    // NON-PURE (Sec11.2): snaps the candidate's position back into
    // perception and clears its PositionVerified flag if the snap moved it
    // beyond threshold - REGARDLESS of whether the routine goes on to accept.
    reg(action::InvestigateLook, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        auto rows = PerceptionState::InvestigationCandidateRows(h.behavior.investigateSeenInLastMs, h.world.isFollower);
        const PerceptionEntry* cand = h.perception.query(rows, nowMs);
        if (!cand) return false;
        const uint64_t handle = cand->handle;
        const int listNum = cand->listNumber;
        h.perception.updatePosition(listNum, handle, f.snappedCandidatePosition);
        if (f.snappedPositionDiffersBeyondThreshold) {
            h.perception.clearFlags(listNum, handle, pflag::PositionVerified);
        }
        const PerceptionEntry* after = h.perception.find(listNum, handle);
        if (!after || !(after->flags & pflag::PositionVerified)) return false;
        if (!f.flag4a5Bit4) {
            block.objectHandle = handle;
            block.smallIndex = static_cast<uint8_t>(listNum);
            block.destination = f.snappedCandidatePosition;
        }
        return true;
    });
    // NON-PURE (Sec11.2): same snap mechanism as 42.
    reg(action::InvestigatePeek, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t nowMs) {
        if (h.mode.state.currentAction != action::InvestigateMove && h.mode.state.currentAction != action::InvestigateLook) {
            return false;
        }
        if (!f.flag0098da50) return false;
        auto rows = PerceptionState::InvestigationCandidateRows(h.behavior.investigateSeenInLastMs, h.world.isFollower);
        const PerceptionEntry* cand = h.perception.query(rows, nowMs);
        if (!cand) return false;
        const uint64_t handle = cand->handle;
        const int listNum = cand->listNumber;
        h.perception.updatePosition(listNum, handle, f.snappedCandidatePosition);
        if (f.snappedPositionDiffersBeyondThreshold) {
            h.perception.clearFlags(listNum, handle, pflag::PositionVerified);
        }
        block.secondaryHandle = f.peekCoverObjectHandle;
        if (f.peekSide) block.flags56 |= ActionBlock::kFlag56_PeekSide;
        else block.flags56 &= ~ActionBlock::kFlag56_PeekSide;
        block.objectHandle = handle;
        block.smallIndex = static_cast<uint8_t>(listNum);
        return true;
    });
    reg(action::Reload, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (f.flag0098da50 && f.atOwnCoverPoint) return false;
        return h.world.canReload;
    });
    reg(action::Taunt, [](HumanAiState& h, ActionWorldFacts&, ActionBlock&, int64_t) {
        return !h.perception.entries(plist::Hearsay).empty() || !h.perception.entries(plist::KnownHostiles).empty();
    });
    reg(action::AvatarTaunt, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        return LiveTarget(h).has && f.targetStateCce != 0 && f.targetStateCce != 6;
    });
    reg(action::RollerBladerChange, [](HumanAiState&, ActionWorldFacts& f, ActionBlock&, int64_t) {
        return f.rollerBladerDescriptorBit;
    });
    reg(action::ZombieExplode, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (!f.flag1c98Bit17) return false;
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has || f.selfCapturedOrLocked || f.targetCapturedOrLocked) return false;
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        return e0 != nullptr && Distance(h.position, e0->position) <= f.zombieExplodeRadius;
    });
    reg(action::ZombieEat, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t) {
        if (h.maxHealth <= 0 || (h.health / h.maxHealth) > 0.9) return false;
        if (!f.zombieEatGateOk) return false;
        if (f.flag1c98Bit17) return false;
        if (!f.corpseCandidateFound) return false;
        block.objectHandle = f.corpseCandidateHandle;
        return true;
    });
    reg(action::QuickKill, [](HumanAiState&, ActionWorldFacts& f, ActionBlock& block, int64_t) {
        if (!f.quickKillCandidateFound) return false;
        block.objectHandle = f.quickKillCandidateHandle;
        return true;
    });

    // ---- Family G: pure gates (Sec11.3 G) ----------------------------------------
    reg(action::Idle, [](HumanAiState& h, ActionWorldFacts&, ActionBlock&, int64_t) {
        if (!AcceptanceTest(h.mode.state, mode::Idle, h.mode.acceptance)) return false;
        if (h.world.isFollower) return false;
        if (h.world.healthState == 14) return false;
        if (h.world.healthState == 18 && h.world.stateCce == 35) return false;
        return true;
    });
    reg(action::FollowFormation, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (h.world.isHumanTyped && h.world.flag1e01Bit0) return false;
        if (f.leashRadiusSet && !f.leashAnchorIsLeader) return false;
        if (!f.groupLeaderOutOfVehicle) return false;
        if (f.flag76fBit5ForFollow) return false;
        if (Distance(h.position, f.groupOrFollowLeaderPosition) > 2.5) return true;
        return f.followedPlayerInVehicleState;
    });
    reg(action::FollowPlayer, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (h.world.isHumanTyped && h.world.flag1e01Bit0) return false;
        if (f.leashRadiusSet && !f.leashAnchorIsLeader) return false;
        if (Distance(h.position, f.groupOrFollowLeaderPosition) > f.followPlayerRadius) return true;
        return f.followedPlayerInVehicleState;
    });
    reg(action::FollowTeleport, [](HumanAiState&, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (!f.followedPlayerExists) return false;
        if (f.leashRadiusSet) return false;
        if (f.humanHeldInGrabForTeleport || f.flag0095d740 || f.flag0091f310_2) return false;
        if (!f.resolves0x004d7e00WithoutFlag) return false;
        const double farThreshold = f.playerInVehicleForTeleport ? 150.0 : 100.0;
        if (f.followedPlayerHorizDist > farThreshold && f.followedPlayerHeightDiff > 4.0) return true;
        if (f.followedPlayerHorizDist <= 100.0 && f.followedPlayerHeightDiff < 3.0) return false;
        return f.flag009b5690Self && !f.flag009b5690Player;
    });
    reg(action::FollowMakeWay, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (!f.followedPlayerInVehicleState) return false;
        if (f.flag009a1dc0ForFollowMakeWay) return false;
        return Distance(h.position, f.groupOrFollowLeaderPosition) <= f.followMakeWayRadius;
    });
    reg(action::FollowLemming, [](HumanAiState&, ActionWorldFacts& f, ActionBlock&, int64_t) {
        if (!f.followLemmingGlobalNonzero) return false;
        if (!(f.flag317Bit1 || f.offNavmesh)) return false;
        return !f.flag009ba8b0;
    });
    reg(action::FollowBrowse, [](HumanAiState& h, ActionWorldFacts&, ActionBlock&, int64_t) {
        if (LiveTarget(h).has) return false;
        return HasQueuedEventKind(h.mode.state, 97);
    });
    reg(action::VehiclePassenger, [](HumanAiState& h, ActionWorldFacts&, ActionBlock&, int64_t) { return h.world.inVehicle; });

    reg(action::AvatarBullrush, [](HumanAiState& h, ActionWorldFacts&, ActionBlock&, int64_t) {
        const LiveTargetResult t = LiveTarget(h);
        return t.has && h.perception.visibleNow(t.handle, h.world.hasCoverObject);
    });
    auto shockwaveFireballs = [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        if (!e0 || !h.perception.visibleNow(t.handle, h.world.hasCoverObject)) return false;
        const double dist = Distance(h.position, e0->position);
        if (f.globalShockwaveMinApplies) return dist > 15.0;
        return true;
    };
    reg(action::AvatarShockwave, shockwaveFireballs);
    reg(action::AvatarFireballs, shockwaveFireballs);
    // NON-PURE (Sec11.2): backs off a DIFFERENT action (58) on one refusal path.
    reg(action::AvatarTeleportStomp, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t nowMs) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        if (f.targetRagdoll) {
            h.actionRepeatTimers[action::AvatarStomp].arm(nowMs, 7000);
            return false;
        }
        const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, t.handle);
        if (!e0 || !h.perception.visibleNow(t.handle, h.world.hasCoverObject)) return false;
        return Distance(h.position, e0->position) > 30.0;
    });
    reg(action::BruteGunFinisher, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has || !f.targetIsPlayerClassForBrute) return false;
        if (!h.perception.visibleNow(t.handle, h.world.hasCoverObject)) return false;
        if (!f.flag004e66b0) return false;
        return f.targetRagdoll;
    });
    reg(action::BruteAttackBrute, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has || !(h.world.isBruteFn && h.world.isBruteFn(t.handle))) return false;
        if (!h.perception.visibleNow(t.handle, h.world.hasCoverObject)) return false;
        if (!(f.vectorTest1cec || f.flag005031b0)) return false;
        if (!f.flag00963d40) return false;
        return f.targetDb0Check;
    });
    reg(action::BruteQtePlayer, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has || !f.targetIsPlayerClassForBrute) return false;
        if (!h.perception.visibleNow(t.handle, h.world.hasCoverObject)) return false;
        if (!f.flag005031b0) return false;
        if (f.targetCapturedOrLocked) return false;
        if (!f.flag00963d40) return false;
        if (!f.globalBit022cce86Clear) return false;
        if (f.is0x009422dClassForQte) {
            if (f.targetIs0x00942220) return false;
            if (!f.selfNot0097cdd0) return false;
            if (!(f.globalTimer012e27f0Passed || f.list5HumansFacingTargetCount >= f.list5HumansFacingTargetThreshold)) {
                return false;
            }
        }
        return true;
    });
    reg(action::BruteBullRush, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock&, int64_t nowMs) {
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has || f.targetCapturedOrLocked) return false;
        const PerceptionEntry* e18 = h.perception.find(plist::CoverMemory18, t.handle);
        if (e18 && (nowMs - e18->noticedAtMs) < 500) return false;
        if (!h.perception.visibleNow(t.handle, h.world.hasCoverObject)) return false;
        if (!f.targetVehicleOk) return false;
        if (h.world.targetLevelDifference != 0) return false;
        if (!f.targetWithinFortyFiveDegrees) return false;
        if (!f.clearanceProbesOk) return false;
        // Holder/riot-shield rules (as for 51) and the `frustrated`
        // tie-break (Sec11.3 G) decide WHICH candidate wins among several
        // and are not reproduced by this single-candidate gate.
        return f.navmeshPathExistsBetweenHumans;
    });

    // ---- Family H: thrown weapons (Sec11.3 H, gating/obstacle-screening
    // resolved by unit A5's Sec13.2) -----------------------------------------------
    reg(action::ThrowWeapon, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t) {
        if (!f.heldItemThrowableNow) return false;
        const LiveTargetResult t = LiveTarget(h);
        if (!t.has) return false;
        if (h.perception.visibleNow(t.handle, h.world.hasCoverObject)) return false;  // "T exists and is NOT visible"
        if (f.targetInVehicleForThrow) return false;
        if (!f.clearanceProbesOk) return false;
        if (IsUnsetPosition(f.precomputedPoint)) return false;  // P
        const ThrowTrajectoryResult solve = SolveThrowTrajectory(h.position, f.precomputedPoint, f.throwArc, f.throwSpeed);
        if (!solve.solved) return false;
        block.secondaryPoint = f.precomputedPoint;  // +0x24 = P
        block.tertiaryVector = solve.outA;           // +0x30...+0x38 = the launch data (only outA is ever read back - Sec13.2.1)
        return true;
    });

    reg(action::ThrowGrenade, [](HumanAiState& h, ActionWorldFacts& f, ActionBlock& block, int64_t) {
        if (!f.grenadeInHand) return false;
        if (!f.canThrowGrenadeNow) return false;
        if (!f.grenadeHasTarget) return false;
        if (!f.grenadePrecondition0x0054d180) return false;
        // "Tries aim points in turn: P, T's cover object, nearby objects.
        // The first that 0x0054d930 can solve fills the block" - per
        // candidate, the SIMD proximity-reject against the avoid list runs
        // first (Sec13.2.2 step2), then the shared-core solve (step3).
        const double avoidRadiusSq = f.grenadeAvoidRadius * f.grenadeAvoidRadius;
        for (const Vec3& candidate : f.grenadeCandidatePoints) {
            bool blocked = false;
            for (const Vec3& avoid : f.grenadeAvoidPositions) {
                if (DistanceSq(candidate, avoid) < avoidRadiusSq) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) continue;
            const ThrowTrajectoryResult solve = SolveThrowTrajectory(h.position, candidate, f.throwArc, f.throwSpeed);
            if (!solve.solved) continue;
            block.secondaryPoint = candidate;   // +0x24/+0x2c = P (the winning candidate)
            block.tertiaryVector = solve.outA;  // +0x30/+0x38 = the launch data
            return true;
        }
        return false;
    });

    // ---- The two null routines (Sec9.5/Sec11.1): deliberately NOT registered. --
    // call() below refuses them explicitly before consulting routines_ at
    // all, so there is no entry here to accidentally wire up.
}

bool ActionRegistry::call(int actionId, HumanAiState& h, ActionWorldFacts& facts, ActionBlock& block, int64_t nowMs) {
    if (actionId < 0 || actionId >= action::kCount) {
        throw OpenStateError("action id out of range (" + std::to_string(actionId) + ")", "spec-ai-runtime.md Sec9.5");
    }
    if (HasNullRoutine(actionId)) {
        // Defense in depth: Chooser() (decision.h) already refuses to reach
        // here via the goal loop or the re-assert path (Sec9.5/Sec11.1's
        // null-routine guard). A direct call bypassing the chooser is
        // treated as a programming error, not "call address 0".
        throw OpenStateError("action " + std::to_string(actionId) + " has a null routine in the original executable",
                              "spec-ai-runtime.md Sec9.5/Sec11.1 (never callable by a port)");
    }
    if (!routines_[static_cast<size_t>(actionId)]) {
        throw OpenStateError(std::string("action routine '") + kActionTable[actionId].name + "' (" +
                                 std::to_string(actionId) + ")",
                              "spec-ai-runtime.md Sec11.3 (not implemented in this port; "
                              "see actions.h's own family-by-family coverage note)");
    }
    return routines_[static_cast<size_t>(actionId)](h, facts, block, nowMs);
}

}  // namespace sr3airuntime
