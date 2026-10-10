#include "sr3airuntime/decision.h"

#include <cmath>

namespace sr3airuntime {

// 0x004e50e0 (Sec9.6 `group`; reused by A3's actions 27/28, Sec11.3 B).
bool Agree0x004e50e0(const HumanAiState& h) {
    if (h.world.isHumanTyped && h.world.flag1e01Bit0) return false;
    switch (h.mode.state.currentAction) {
        case action::MoveScripted:
        case action::MoveSidestep:
        case action::PickupWeapon:
        case action::BruteBullRush:
        case action::QuickKill:
            return false;
        default:
            return true;
    }
}

// 0x004f6310(kind, ...) walking the live and staged event lists (Sec9.6
// `suppress`; reused by A3's action 40 `follow browse`, Sec11.3 G).
bool HasQueuedEventKind(const ModeState& st, int32_t kind) {
    for (const auto& e : st.events) {
        if (e.kind == kind) return true;
    }
    for (const auto& e : st.staging) {
        if (e.kind == kind) return true;
    }
    return false;
}

namespace {

// 0x004e68e0 == 1 (Sec11.6, used by `advance` step 6). P's existence is
// already guaranteed by `advance` step 2 by the time this is called.
bool Advance0x004e68e0Is1(const HumanAiState& h, Vec3 p) {
    if (!h.world.isHumanTyped) return true;
    if (h.world.isDownOrRagdoll) {
        const bool within2 = std::abs(h.position.x - p.x) <= 2.0 && std::abs(h.position.y - p.y) <= 2.0 &&
                              std::abs(h.position.z - p.z) <= 2.0;
        if (!within2) return true;
    }
    return WeaponBandTest(h.world.weaponRangeA, h.world.weaponRangeB, Distance(h.position, p)) == 1;
}

}  // namespace

// --- Sec9.4 step 2: the state mask / precondition test ----------------------------
uint32_t BuildStateMask(const HumanAiState& h) {
    uint32_t mask = 0;
    const bool inVehicle = h.world.inVehicle;
    const int32_t hs = h.world.healthState;
    if (!inVehicle && (hs == 0 || hs == 7)) mask |= 0x01;  // on foot
    if (inVehicle) mask |= 0x02;                            // in vehicle
    if (hs == 3) mask |= 0x04;                              // in water
    if (LiveTarget(h).has) mask |= 0x08;                    // must have target
    if (h.forceFlags.inScriptedCover) mask |= 0x10;         // can do in cover
    return mask;
}

bool PreconditionTest(uint32_t preFlags, uint32_t stateMask) {
    if ((preFlags & stateMask & 0x07u) == 0) return false;          // need a common medium
    if ((preFlags & 0x08u) && !(stateMask & 0x08u)) return false;   // needs a target
    if ((stateMask & 0x10u) && !(preFlags & 0x10u)) return false;   // in scripted cover: can-do-in-cover only
    return true;
}

LiveTargetResult LiveTarget(const HumanAiState& h) {
    if (h.forceFlags.cantAttack) return {false, 0};
    if (!h.world.targetHandleSet) return {false, 0};
    if (!h.world.targetResolvesToUsableBaseTypeObject) return {false, 0};
    if (h.world.targetIsDownOrDead) return {false, 0};
    if (h.world.targetFlag3bBit0) return {false, 0};
    if (!h.world.targetActiveFlag33Bit3) return {false, 0};
    return {true, h.world.targetHandle};
}

// --- Sec9.3: the decision gate ----------------------------------------------------
bool Gate(HumanAiState& h, int64_t nowMs) {
    ModeState& st = h.mode.state;
    if (st.currentMode == mode::Stunned && st.subState == 9) return false;
    if (h.forceFlags.alwaysCower) return false;
    if (st.switchPending && (st.requestedMode == mode::Stunned || st.requestedMode == mode::Panic)) return false;
    if (h.world.healthState == 5 || h.world.healthState == 6 || h.world.isDeadFlagE3Bit0) return false;
    if (h.world.heldInGrab) return false;
    if (h.world.isPlayerClass) return false;
    if (h.world.healthState == 14 && st.currentMode != mode::Skydiving) {
        h.mode.request(mode::Skydiving, nowMs);
        return false;
    }
    return (st.actionFlags & (ModeState::kActionFinished | ModeState::kReEvaluateRequested)) != 0;
}

// --- Sec9.6: the 17 goal tests -----------------------------------------------------
bool GoalChangeMode(const HumanAiState&, int64_t) { return true; }

bool GoalAvoidBrute(const HumanAiState& h, int64_t nowMs) {
    for (const auto& e : h.perception.entries(plist::KnownHostiles)) {
        if (!(e.flags & pflag::ActivelyKnown)) continue;
        if ((nowMs - e.noticedAtMs) > 3000) continue;
        if (h.world.isBruteFn && h.world.isBruteFn(e.handle)) return true;
    }
    return false;
}

bool GoalKillNearPlayer(const HumanAiState& h, int64_t nowMs) {
    const double r = h.killNearPlayerRadius.get();  // OPEN until seeded (Sec9.6 goal 2 / Sec8 item 6)
    const double r2 = r * r;
    for (const auto& e : h.perception.entries(plist::LastSightings)) {
        if (e.kind != 33) continue;
        if ((nowMs - e.noticedAtMs) > 3000) continue;
        if (DistanceSq(e.position, h.position) < r2) return true;
    }
    return false;
}

bool GoalSurvive(const HumanAiState& h, int64_t) {
    const double frac = (h.maxHealth > 0) ? (h.health / h.maxHealth) : 0.0;
    return frac < h.behavior.lowHealthPct;
}

bool GoalRetreat(const HumanAiState& h, int64_t nowMs) {
    for (const auto& e : h.perception.entries(plist::KnownHostiles)) {
        if (!(e.flags & pflag::ActivelyKnown)) continue;
        if ((nowMs - e.noticedAtMs) > 5000) continue;
        if (DistanceSq(e.position, h.position) < h.world.weaponRangeA * h.world.weaponRangeA) return true;
    }
    return false;
}

bool GoalGroup(const HumanAiState& h, int64_t) {
    if (!h.world.hasGroupLeader) return false;
    if (!(h.world.groupLeaderIsPlayer || h.world.groupLeaderHasSquadLeaderFlag)) return false;
    return Agree0x004e50e0(h);
}

bool GoalSuppress(const HumanAiState& h, int64_t) {
    if (!HasQueuedEventKind(h.mode.state, 108)) return false;
    if (h.world.stateCcd == 9 && (h.world.stateCce == 17 || h.world.stateCce == 19)) return true;
    return h.world.coverSuppressHelper;
}

bool GoalCover(const HumanAiState& h, int64_t nowMs) {
    if (h.forceFlags.inScriptedCover && !h.world.coverSuppressHelper) return true;

    const LiveTargetResult t = LiveTarget(h);
    if (!t.has) return false;
    const PerceptionEntry* tEntry = h.perception.find(plist::KnownHostiles, t.handle);
    if (!tEntry || (nowMs - tEntry->noticedAtMs) > h.behavior.flushOutSeenInLastMs) return false;
    // Step 3 (a byte at 0x01115d10, 0 in the file image) is a compiled-out
    // switch: never true, no rule here (Sec9.6).

    if (h.world.hasCoverObject) {
        const bool skipRangeCheck = h.world.stateCcd == 9 &&
                                     (h.world.stateCce == 15 || h.world.stateCce == 16 || h.world.stateCce == 18) &&
                                     h.coverTimeInCoverTimer.elapsedMs(nowMs) < h.behavior.advanceCoverTimeMs;
        if (!skipRangeCheck) {
            const double f = h.world.combatAggressionIsTwo ? 2.0 : 1.0;
            const double limit = f * h.world.weaponRangeB;
            if (DistanceSq(h.world.coverObjectPosition, tEntry->position) > limit * limit) return false;
        }
    }

    if (h.world.flag76fBit3 && !h.perception.entries(plist::ShotsAtMe).empty()) return true;

    if (!h.coverStayDownTimer.expired(nowMs)) return true;  // "not armed, or not yet reached"
    {
        const int64_t threshold = h.world.flag76fBit3 ? 1000 : h.behavior.popUpTimeMs;
        if (h.coverStayDownTimer.elapsedMs(nowMs) < threshold) return false;
    }

    if (!h.world.hasCoverObject && h.world.targetHoldsRangedWeapon) {
        if (h.perception.timeSinceLastSeen(t.handle, nowMs) < h.behavior.flushOutSeenInLastMs) return true;
    }

    if (h.behavior.takeCoverWhenDamaged && h.perception.damageAccumulator(nowMs) > 0.1) return true;
    if (h.behavior.takeCoverWhenShotAt && !h.perception.entries(plist::ShotsAtMe).empty()) return true;
    if (h.behavior.takeCoverWhenAimedAt && h.world.targetAimingAtHuman) return true;
    return false;
}

bool GoalTakingDamage(const HumanAiState& h, int64_t nowMs) {
    if (h.world.hasCoverObject) return false;
    if (h.perception.damageAccumulator(nowMs) <= 0.05) return false;
    return !h.perception.entries(plist::ShotsAtMe).empty();
}

bool GoalReload(const HumanAiState& h, int64_t) {
    return h.world.canReload && h.world.clipFraction <= h.behavior.clipPct;
}

bool GoalAdvance(const HumanAiState& h, int64_t nowMs) {
    if (GoalReload(h, nowMs)) return false;  // step 1: the reload test must FAIL

    const LiveTargetResult t = LiveTarget(h);
    if (!t.has) return false;
    const PerceptionEntry* tEntry = h.perception.find(plist::KnownHostiles, t.handle);
    if (!tEntry || !(tEntry->flags & pflag::ActivelyKnown)) return false;  // step 2

    if (h.world.heldItemIsRanged && !h.world.isBruteWeaponInfo12 && !h.world.isAggressivePersonality) {  // step 3
        if (h.world.targetLevelDifference > 0) return false;
    }

    auto rows = PerceptionState::InvestigationCandidateRows(h.behavior.acquireTargetSeenInLastMs, h.world.isFollower);
    if (!h.perception.query(rows, nowMs)) return false;  // step 4

    if (h.world.hasCoverObject && h.world.stateCcd == 9 &&  // step 5
        (h.world.stateCce == 15 || h.world.stateCce == 16 || h.world.stateCce == 18) &&
        h.coverTimeInCoverTimer.elapsedMs(nowMs) < h.behavior.advanceCoverTimeMs && !h.world.isAggressivePersonality) {
        const double limit = 2.0 * h.world.weaponRangeB;
        if (DistanceSq(tEntry->position, h.world.coverObjectPosition) <= limit * limit) return false;
    }

    if (!Advance0x004e68e0Is1(h, tEntry->position)) return false;  // step 6

    if (h.world.is0x009422dClass && h.world.targetInVehicleState3) {  // step 7
        if (!h.world.vehicleGeometryClearTail) return false;
    }
    return true;
}

bool GoalKillEnemy(const HumanAiState& h, int64_t nowMs) {
    if (h.forceFlags.cantAttack || h.forceFlags.combatDisabled) return false;
    const LiveTargetResult t = LiveTarget(h);
    bool seenRecently = false;
    if (t.has) seenRecently = h.perception.timeSinceLastSeen(t.handle, nowMs) <= h.behavior.killEnemySeenInLastMs;
    const bool postureHolds =
        h.world.stateCcd == 9 && (h.world.stateCce == 15 || h.world.stateCce == 16 || h.world.stateCce == 18);
    return seenRecently || postureHolds;
}

bool GoalFlushOut(const HumanAiState& h, int64_t nowMs) {
    const LiveTargetResult t = LiveTarget(h);
    if (!t.has) return false;
    if (h.perception.timeSinceLastSeen(t.handle, nowMs) > h.behavior.flushOutSeenInLastMs) return false;
    if (h.forceFlags.combatDisabled) return false;
    if (h.world.allyLinkResolvesToLiveObject) return false;
    const PerceptionEntry* e2 = h.perception.find(plist::LineOfFire, t.handle);
    if (e2 && (e2->flags & pflag::CloseInnerRadius)) return false;
    const bool rangedDamaged = h.world.heldItemIsRanged && h.perception.damageAccumulator(nowMs) > 0.1 &&
                                !h.perception.engageable(t.handle, h.behavior.killEnemySeenInLastMs, nowMs);
    if (rangedDamaged) return false;
    return true;
}

bool GoalFrustrated(const HumanAiState& h, int64_t nowMs) {
    if (!h.mode.state.commitTimer.isArmed() || !h.mode.state.idleTimer.isArmed()) return false;
    const int64_t diff = h.mode.state.idleTimer.elapsedMs(nowMs) - h.mode.state.commitTimer.elapsedMs(nowMs);
    if (diff < 8000) return false;
    const PerceptionEntry* head = h.perception.head(plist::KnownHostiles);
    return head != nullptr && (head->flags & pflag::VisuallyConfirmedOnce);
}

bool GoalAcquireTarget(const HumanAiState& h, int64_t nowMs) {
    if (h.forceFlags.combatDisabled) return false;
    const LiveTargetResult t = LiveTarget(h);
    if (!h.perception.targetLineOfSightValidOrNone(t.has, t.handle)) return false;

    const PerceptionEntry* head = h.perception.head(plist::KnownHostiles);
    if (!head || !(head->flags & pflag::ActivelyKnown)) return false;
    if ((nowMs - head->noticedAtMs) > h.behavior.acquireTargetSeenInLastMs) return false;

    if (t.has && h.world.heldItemIsRanged && h.perception.damageAccumulator(nowMs) > 0.1 &&
        !h.perception.engageable(t.handle, h.behavior.killEnemySeenInLastMs, nowMs)) {
        return true;
    }
    if ((head->flags & pflag::PositionVerified) && (head->flags & pflag::WrittenOff)) return false;

    if (t.has) {
        const PerceptionEntry* e2 = h.perception.find(plist::LineOfFire, t.handle);
        if (e2 && (e2->flags & pflag::CloseInnerRadius)) return true;
    }
    if (h.behavior.goalsCanProcessEarly) return true;

    int64_t tPrime;
    const PerceptionEntry* e2ForTPrime = t.has ? h.perception.find(plist::LineOfFire, t.handle) : nullptr;
    if (e2ForTPrime) {
        tPrime = h.perception.clearLineOfFireTimer.elapsedOrNeverMs(nowMs);
    } else {
        tPrime = t.has ? h.perception.timeSinceLastSeen(t.handle, nowMs) : PerceptionState::kNeverSeenMs;
    }
    return tPrime >= h.behavior.flushOutSeenInLastMs;
}

bool GoalInvestigate(const HumanAiState& h, int64_t nowMs) {
    const LiveTargetResult t = LiveTarget(h);
    if (!h.behavior.goalsCanProcessEarly) {
        if (t.has && h.perception.timeSinceLastSeen(t.handle, nowMs) <= h.behavior.flushOutSeenInLastMs) return false;
    }
    auto rows = PerceptionState::InvestigationCandidateRows(h.behavior.investigateSeenInLastMs, h.world.isFollower);
    if (!h.perception.query(rows, nowMs)) return false;
    if (h.forceFlags.combatDisabled) return false;
    for (const auto& e : h.perception.entries(plist::KnownHostiles)) {
        if ((e.flags & pflag::InView) && e.kind != 0xff) return false;  // "nothing currently in view"
    }
    return true;
}

bool GoalIdle(const HumanAiState& h, int64_t nowMs) {
    const auto& list0 = h.perception.entries(plist::KnownHostiles);
    if (list0.empty()) return true;
    if (h.behavior.goalsCanProcessEarly) return true;
    const PerceptionEntry* head = h.perception.head(plist::KnownHostiles);
    return head != nullptr && (nowMs - head->noticedAtMs) >= h.behavior.investigateSeenInLastMs;
}

const std::array<GoalTestFn, goal::kCount> kGoalTests = {
    &GoalChangeMode,   &GoalAvoidBrute, &GoalKillNearPlayer, &GoalSurvive,  &GoalRetreat,
    &GoalGroup,        &GoalSuppress,   &GoalCover,          &GoalTakingDamage, &GoalAdvance,
    &GoalReload,       &GoalKillEnemy,  &GoalFlushOut,       &GoalFrustrated,  &GoalAcquireTarget,
    &GoalInvestigate,  &GoalIdle,
};

// --- Sec9.4: the chooser -----------------------------------------------------------
uint8_t Chooser(HumanAiState& h, uint8_t& reasonOut, ActionBlock& blockOut, int64_t nowMs) {
    std::array<bool, action::kCount> tried{};
    const uint32_t s = BuildStateMask(h);
    ModeState& st = h.mode.state;

    if ((st.actionFlags & ModeState::kOrdered) && st.orderedRecheckTimer.expired(nowMs)) {
        blockOut = st.stagedBlock;
        blockOut.flags56 |= ActionBlock::kFlag56_Ordered;
        bool refused = false;
        if (st.actionFlags & ModeState::kOrderedForceRecheck) {
            blockOut.flags56 |= ActionBlock::kFlag56_ForcedRecheck;
            if (!HasNullRoutine(st.stagedBlock.actionId) && h.actionRoutine) {
                if (!h.actionRoutine(st.stagedBlock.actionId, blockOut)) refused = true;
            }
        }
        if (refused) {
            blockOut = ActionBlock{};
            st.orderedRecheckTimer.arm(nowMs, 250);
            // falls through to the goal loop below
        } else {
            return st.stagedBlock.actionId;  // reasonOut is NOT written on this path (Sec9.4 step 3)
        }
    }

    if (h.forceFlags.ignoreAi) {
        return action::HoldPosition;  // reasonOut is NOT written on this path (Sec9.4 step 4)
    }

    if (!(st.actionFlags & ModeState::kActionFinished) && st.currentAction != ActionBlock::kNoAction &&
        kActionTable[st.currentAction].keepCurrentExemption == 0) {
        tried[st.currentAction] = true;
    }

    for (int g = 0; g < goal::kCount; ++g) {
        reasonOut = static_cast<uint8_t>(g);
        if (h.behavior.goalEnabled[g] == 0) continue;
        if (!kGoalTests[g](h, nowMs)) continue;

        for (uint8_t a : h.goalActionLists[g]) {
            if (a == ActionBlock::kNoAction) break;
            bool accepted = false;
            if (!tried[a] && h.behavior.abilities[a].enabled) {
                if (PreconditionTest(h.preFlags[a], s) && h.actionRepeatTimers[a].expired(nowMs)) {
                    if (!HasNullRoutine(a) && h.actionRoutine && h.actionRoutine(a, blockOut)) {
                        accepted = true;
                    }
                }
            }
            if (accepted) return a;
            tried[a] = true;
            if (!(st.actionFlags & ModeState::kActionFinished) && st.committedReason == g && st.currentAction == a) {
                return ActionBlock::kNoAction;
            }
        }
    }

    reasonOut = 17;
    return h.world.inVehicle ? static_cast<uint8_t>(action::VehiclePassenger)
                              : static_cast<uint8_t>(action::HoldPosition);
}

// --- Sec9.2: the decision step ------------------------------------------------------
void DecisionStep(HumanAiState& h, int64_t nowMs) {
    ModeState& st = h.mode.state;
    const uint8_t a0 = st.currentAction;

    if (!Gate(h, nowMs)) {
        st.actionFlags &= ~ModeState::kActionFinished;
        return;
    }

    if (!h.world.hasHeldItem && !GoalIdle(h, nowMs) && h.weaponUpkeepHook) {
        h.weaponUpkeepHook();  // Sec9.2 step 3 (HYPOTHESIS; 0x00600720 not read)
    }

    const bool bit0 = (st.actionFlags & ModeState::kActionFinished) != 0;
    if (a0 < action::kCount) {
        if (!bit0 && st.commitTimer.elapsedMs(nowMs) < h.noInterruptMs[a0]) {
            return;  // re-decision wait
        }
    } else {
        st.idleTimer.restart(nowMs, 0);
    }

    ActionBlock block;  // Sec9.2 step 5: already defaults to NaN positions, kNoAction (common.h's ActionBlock)
    uint8_t reason = 0;
    const uint8_t r = Chooser(h, reason, block, nowMs);
    st.actionFlags &= ~ModeState::kReEvaluateRequested;

    if (r != ActionBlock::kNoAction && (r != a0 || bit0)) {
        st.actionFlags &= ~ModeState::kActionFinished;
        block.actionId = r;
        // 0x004f4eb0 (Sec9.10 item 3): no observable effect; omitted.
        const int32_t categoryMode = kActionTable[r].categoryMode;
        h.mode.request(categoryMode, nowMs);
        if (st.switchPending && st.requestedMode == categoryMode) {
            st.committedReason = reason;
            st.currentAction = r;
            st.commitTimer.restart(nowMs, 0);
            st.committedBlock = block;
        }
    }
}

// --- Sec4.1 wrapper: finish + the cool-down roll -----------------------------------
bool FinishAction(HumanAiState& h, int64_t nowMs, const std::function<int64_t(int64_t, int64_t)>& rollFn) {
    const uint8_t a = h.mode.state.currentAction;
    if (a < action::kCount) {
        const AbilityEntry& ability = h.behavior.abilities[a];
        const int64_t lo = ability.repeatMinMs;
        const int64_t hi = ability.repeatMaxMs;
        const int64_t roll = rollFn ? rollFn(lo, hi) : (lo + hi) / 2;
        EngineTimer& timer = h.actionRepeatTimers[a];
        const int64_t currentRemaining = timer.remainingMs(nowMs);
        if (roll > currentRemaining) {
            timer.arm(nowMs, roll);  // "can only be lengthened, never shortened" (Sec4.1 step 1)
        }
    }

    ModeMachine::FinishInputs in;
    in.stagedActionCategoryMode =
        (h.mode.state.stagedBlock.actionId < action::kCount) ? kActionTable[h.mode.state.stagedBlock.actionId].categoryMode
                                                              : mode::kNone;
    return h.mode.finishAction(in, nowMs);
}

}  // namespace sr3airuntime
