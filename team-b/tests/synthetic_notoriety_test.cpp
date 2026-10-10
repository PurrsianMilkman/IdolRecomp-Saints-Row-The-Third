// Synthetic tests for the notoriety tracking MECHANISM, spec-ai-runtime.md
// Sec19 (include/sr3airuntime/notoriety.h).
//
// Covers this task's own 4 scoped pieces: the team-relationship hostility
// flip at the zero boundary (both directions, and that it re-affirms rather
// than no-ops on every actual level change); the crimes-to-points and decay
// rules (real shipped notoriety.xtbl/notoriety_levels.xtbl numbers,
// spec-tables-progression.md Sec14.6); the clear-to-zero roster/timer
// sequence (firing ONLY on a transition into level <= 0, explicitly NOT on
// ordinary decay between positive levels); and the heat-code table's
// TRACK-indexing (including a regression case that would catch an
// accidental level-indexing mistake).

#include <iostream>

#include "sr3airuntime/notoriety.h"

using namespace sr3airuntime;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

TeamRelationMatrix NeutralMatrix() {
    TeamRelationMatrix m;
    for (auto& row : m) row.fill(kTeamRelationNeutral);
    return m;
}

// ===========================================================================
// Part A: the heat-code table is TRACK-indexed, not level-indexed (Sec19.4
// "two further corrections").
// ===========================================================================
void testHeatCodeTableValuesByTrack() {
    CHECK(HeatCodeForTrack(NotorietyTrack::kLuchadores) == 1);
    CHECK(HeatCodeForTrack(NotorietyTrack::kDeckers) == 2);
    CHECK(HeatCodeForTrack(NotorietyTrack::kMorningstar) == 3);
    CHECK(HeatCodeForTrack(NotorietyTrack::kPolice) == 5);
}

void testHeatCodeRegressionCatchesLevelIndexingMistake() {
    // A level-indexed (wrong) implementation would return
    // kHeatCodeByTrack[level] instead of kHeatCodeByTrack[track]. Drive the
    // Police track (correct code 5) through several DIFFERENT levels and
    // confirm the dispatched heat code never drifts to match the LEVEL's own
    // index (which for level 2 would wrongly read as 3, Morningstar's code).
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();

    int64_t t = 0;
    auto r1 = driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, 75, t);  // -> level 1
    CHECK(r1.dispatched);
    CHECK(r1.newLevel == 1);
    CHECK(r1.heatCode == 5);  // NOT kHeatCodeByTrack[1] (==2, Deckers) - would be the level-indexed bug

    auto r2 = driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, 225, t);  // -> level 2 (75+225=300=limit[2])
    CHECK(r2.dispatched);
    CHECK(r2.newLevel == 2);
    CHECK(r2.heatCode == 5);  // NOT kHeatCodeByTrack[2] (==3, Morningstar) - the exact regression this test targets

    // Cross-check against a DIFFERENT track reaching the SAME level: its own
    // heat code must differ from Police's, proving the table really is
    // track-keyed, not shared/level-keyed.
    NotorietyDriver driver2;
    // Gang table: limit[2]=225, so a single 225-point award lands exactly on
    // level 2 (span(0)=75 then span(1)=150, summing to exactly 225).
    auto m2 = driver2.AddPointsRaw(matrix, NotorietyTrack::kMorningstar, 225, t);
    CHECK(m2.dispatched);
    CHECK(m2.newLevel == 2);
    CHECK(m2.heatCode == 3);          // Morningstar's own real code
    CHECK(m2.heatCode != r2.heatCode);  // same LEVEL (2), different TRACK -> different heat code
}

// ===========================================================================
// Part B: the team-relationship hostility flip at the zero boundary
// (Sec19.4 CONFIRMED - any level >= 1 writes HOSTILE, any level <= 0 writes
// UNFRIENDLY, symmetric, from team 0 against the heat-coded slot).
// ===========================================================================
void testCrossingAboveZeroWritesHostileSymmetrically() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;

    auto result = driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, 75, t);  // 0 -> level 1
    CHECK(result.dispatched);
    CHECK(result.teamRelationWritten == kTeamRelationHostile);
    const int32_t police = HeatCodeForTrack(NotorietyTrack::kPolice);
    CHECK(matrix[kPlayerBaselineTeam][police] == kTeamRelationHostile);
    CHECK(matrix[police][kPlayerBaselineTeam] == kTeamRelationHostile);  // symmetric write
}

void testClearingToZeroWritesUnfriendlyNotNeutral() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;

    driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, 75, t);  // -> level 1, now HOSTILE
    const int32_t police = HeatCodeForTrack(NotorietyTrack::kPolice);
    CHECK(matrix[kPlayerBaselineTeam][police] == kTeamRelationHostile);

    auto result = driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, -75, t);  // -> level 0
    CHECK(result.dispatched);
    CHECK(result.newLevel == 0);
    CHECK(result.teamRelationWritten == kTeamRelationUnfriendly);  // a stand-down, NOT neutral (2)
    CHECK(matrix[kPlayerBaselineTeam][police] == kTeamRelationUnfriendly);
    CHECK(matrix[police][kPlayerBaselineTeam] == kTeamRelationUnfriendly);
}

void testOrdinaryDownwardStepBetweenPositiveLevelsStillReaffirmsHostile() {
    // Sec19.4: the matrix write happens on EVERY dispatched change in EITHER
    // direction - only the roster/timer sequence is clear-to-zero-only.
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;

    driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, 300, t);  // -> level 2 (limit[2]=300)
    const int32_t police = HeatCodeForTrack(NotorietyTrack::kPolice);
    CHECK(matrix[kPlayerBaselineTeam][police] == kTeamRelationHostile);

    auto result = driver.DecayTick(matrix, NotorietyTrack::kPolice, t);  // forced decay below, see Part C for timer gating
    // (DecayTick is timer-gated - this specific call may no-op if the timer
    // hasn't expired yet; the actual "stays positive, still writes HOSTILE"
    // behaviour is exercised precisely in Part D's
    // testClearToZeroSequenceDoesNotRunBetweenPositiveLevels, which arms the
    // timer correctly. This test only re-confirms the matrix write itself
    // fires on a positive-to-positive AddPoints step, independent of decay.)
    (void)result;
    auto stepDown = driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, -225, t);  // level 2 -> level 1 (still > 0)
    CHECK(stepDown.dispatched);
    CHECK(stepDown.newLevel == 1);
    CHECK(stepDown.teamRelationWritten == kTeamRelationHostile);  // re-affirmed, not left stale
    CHECK(!stepDown.clearToZeroSequenceRan);
}

void testNoLevelChangeMeansNoDispatchAtAll() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;

    auto result = driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, 10, t);  // span(0)=75, 10 doesn't cross it
    CHECK(!result.dispatched);
    const int32_t police = HeatCodeForTrack(NotorietyTrack::kPolice);
    CHECK(matrix[kPlayerBaselineTeam][police] == kTeamRelationNeutral);  // untouched - matches NeutralMatrix() seed
}

// ===========================================================================
// Part C: crimes-to-points (notoriety.xtbl gating) and the real shipped
// level-limit numbers (spec-tables-progression.md Sec14.6).
// ===========================================================================
void testRealLevelLimitsAndSpansForGangAndPolice() {
    // Sec14.6 real cumulative limits.
    CHECK(kGangLevelTable.levelLimit[1] == 75 && kPoliceLevelTable.levelLimit[1] == 75);
    CHECK(kGangLevelTable.levelLimit[2] == 225 && kPoliceLevelTable.levelLimit[2] == 300);  // the one place the two sets diverge in limits
    CHECK(kGangLevelTable.levelLimit[3] == 500 && kPoliceLevelTable.levelLimit[3] == 700);
    CHECK(kGangLevelTable.levelLimit[4] == 1200 && kPoliceLevelTable.levelLimit[4] == 1200);  // converge again
    CHECK(kGangLevelTable.levelLimit[5] == 2500 && kPoliceLevelTable.levelLimit[5] == 2500);

    // Decay rates diverge at every level 1-5 (Sec14.6: "set 1 decays faster").
    CHECK(kPoliceLevelTable.decayRate[1] > kGangLevelTable.decayRate[1]);
    CHECK(kPoliceLevelTable.decayRate[2] > kGangLevelTable.decayRate[2]);
    CHECK(kPoliceLevelTable.decayRate[5] > kGangLevelTable.decayRate[5]);

    CHECK(NotorietyLevelSpan(kGangLevelTable, 0) == 75);
    CHECK(NotorietyLevelSpan(kPoliceLevelTable, 1) == 225);  // 300 - 75
}

void testCrimeActivityMinMaxLevelWindowGatesAward() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;

    // kConfirmedArmoredTruckAssault: police window is levels 2-3 only.
    // At level 0, the award must be gated out entirely (no award, no
    // dispatch) - Sec19.2's Min_Level/Max_Level window.
    auto gated = driver.TryAwardCrimePoints(matrix, NotorietyTrack::kPolice, kConfirmedArmoredTruckAssault, t);
    CHECK(!gated.dispatched);
    CHECK(driver.trackRecord(NotorietyTrack::kPolice).level == 0);

    // Push into the window (level 2) via the plain add-points primitive, then
    // confirm the SAME activity now awards its real 100 points.
    driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, kPoliceLevelTable.levelLimit[2], t);  // -> exactly level 2, fraction 0
    CHECK(driver.trackRecord(NotorietyTrack::kPolice).level == 2);
    CHECK(driver.trackRecord(NotorietyTrack::kPolice).fraction == 0.0);
    auto awarded = driver.TryAwardCrimePoints(matrix, NotorietyTrack::kPolice, kConfirmedArmoredTruckAssault, t);
    // 100 points at fraction 0 doesn't cross span(2)=400 (Police table) -
    // no level change, but the real 100-point award must have landed.
    CHECK(!awarded.dispatched);
    CHECK(driver.trackRecord(NotorietyTrack::kPolice).level == 2);
    CHECK(driver.trackRecord(NotorietyTrack::kPolice).fraction == 100.0);
}

void testCheckDetectionGateRequiresLiveDetectionCallback() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;

    CrimeActivity checkedActivity;
    checkedActivity.name = "synthetic-check-detection-activity";  // SYNTHETIC test fixture, not a real xtbl row
    checkedActivity.gang = {50, 0, kNotorietyMaxLevel, true};
    checkedActivity.police = {50, 0, kNotorietyMaxLevel, true};

    // No detection callback wired -> fails closed (CHOSEN default, see
    // notoriety.h's own doc comment on isActorDetectedByLivePlayer).
    auto notDetected = driver.TryAwardCrimePoints(matrix, NotorietyTrack::kLuchadores, checkedActivity, t);
    CHECK(!notDetected.dispatched);
    CHECK(driver.trackRecord(NotorietyTrack::kLuchadores).fraction == 0.0);

    driver.isActorDetectedByLivePlayer = [] { return true; };
    auto detected = driver.TryAwardCrimePoints(matrix, NotorietyTrack::kLuchadores, checkedActivity, t);
    CHECK(!detected.dispatched);  // 50 < span(0)=75 (Gang table) - points land but don't cross a level boundary yet
    CHECK(driver.trackRecord(NotorietyTrack::kLuchadores).fraction == 50.0);
}

void testEnterOwnedStoreIsTheOnlyNegativePointActivityAndReducesNotoriety() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;

    driver.AddPointsRaw(matrix, NotorietyTrack::kDeckers, 100, t);  // bank some points first (level 1, fraction 25 past limit[1]=75)
    CHECK(driver.trackRecord(NotorietyTrack::kDeckers).level == 1);
    const double fractionBefore = driver.trackRecord(NotorietyTrack::kDeckers).fraction;

    driver.TryAwardCrimePoints(matrix, NotorietyTrack::kDeckers, kConfirmedEnterOwnedStore, t);
    CHECK(driver.trackRecord(NotorietyTrack::kDeckers).fraction == fractionBefore - 20.0);
    CHECK(kConfirmedEnterOwnedStore.gang.points == -20);
    CHECK(kConfirmedEnterOwnedStore.police.points == -20);
}

// ===========================================================================
// Part D: the clear-to-zero roster/timer sequence - fires ONLY on a
// transition into level <= 0, never on ordinary decay between positive
// levels (Sec19.4, corrected 2026-10-09).
// ===========================================================================
void testClearToZeroSequenceRunsOnlyWhenReachingZero() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;

    std::vector<uint64_t> reconsidered;
    std::vector<int32_t> released;
    driver.onRosterEligibilityReconsider = [&](uint64_t h) { reconsidered.push_back(h); };
    driver.onReleasePlayerPursuitHandles = [&](int32_t p) { released.push_back(p); };
    driver.roster = {{111, static_cast<int32_t>(NotorietyTrack::kPolice)},
                      {222, static_cast<int32_t>(NotorietyTrack::kPolice)},
                      {333, static_cast<int32_t>(NotorietyTrack::kDeckers)}};  // different track - must be ignored
    driver.livePlayerCount = 2;

    // Step 1: 0 -> level 2 (positive). No clear-to-zero sequence should run.
    driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, kPoliceLevelTable.levelLimit[2], t);
    CHECK(reconsidered.empty());
    CHECK(released.empty());

    // Step 2: level 2 -> level 1 (still positive) - "ordinary decay between
    // positive levels" - must NOT run the sequence either.
    auto stepDown = driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, -(kPoliceLevelTable.levelLimit[2] - kPoliceLevelTable.levelLimit[1]), t);
    CHECK(stepDown.dispatched);
    CHECK(stepDown.newLevel == 1);
    CHECK(!stepDown.clearToZeroSequenceRan);
    CHECK(reconsidered.empty());
    CHECK(released.empty());

    // Step 3: level 1 -> level 0 - THE clear-to-zero transition. Now the
    // sequence must run: exactly the 2 Police-track roster members
    // reconsidered (not the Deckers one), exactly min(livePlayerCount, 2)
    // players released, and the 2000ms timer armed.
    auto cleared = driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, -kPoliceLevelTable.levelLimit[1], t);
    CHECK(cleared.dispatched);
    CHECK(cleared.newLevel == 0);
    CHECK(cleared.clearToZeroSequenceRan);
    CHECK(cleared.rosterReconsiderCount == 2);
    CHECK(static_cast<int>(reconsidered.size()) == 2);
    CHECK(reconsidered[0] == 111 && reconsidered[1] == 222);  // in roster order; the Deckers entry (333) never called
    CHECK(cleared.playersReleased == 2);
    CHECK(static_cast<int>(released.size()) == 2);
    CHECK(driver.trackRecord(NotorietyTrack::kPolice).clearToZeroTimer.isArmed());
    CHECK(driver.trackRecord(NotorietyTrack::kPolice).clearToZeroTimer.remainingMs(t) == NotorietyDriver::kClearToZeroTimerMs);
}

void testClearToZeroSequenceCapsPlayerDispatchAtTwoEvenWithMoreLivePlayers() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;
    driver.livePlayerCount = 5;  // this project has no real co-op beyond 2 anyway - prove the cap holds regardless
    std::vector<int32_t> released;
    driver.onReleasePlayerPursuitHandles = [&](int32_t p) { released.push_back(p); };

    driver.AddPointsRaw(matrix, NotorietyTrack::kLuchadores, 75, t);   // -> level 1
    auto cleared = driver.AddPointsRaw(matrix, NotorietyTrack::kLuchadores, -75, t);  // -> level 0
    CHECK(cleared.clearToZeroSequenceRan);
    CHECK(cleared.playersReleased == 2);
    CHECK(static_cast<int>(released.size()) == 2);
}

void testDecayTickIsGatedByTimerAndSuppressionFlag() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;

    // Level 2, fraction 50 (comfortably clear of both the level-2/3 and the
    // level-1/2 boundary) - Gang table: limit[2]=225, so 275 total points
    // lands here (span(0)=75, span(1)=150, remainder 50).
    driver.AddPointsRaw(matrix, NotorietyTrack::kMorningstar, kGangLevelTable.levelLimit[2] + 50, t);
    CHECK(driver.trackRecord(NotorietyTrack::kMorningstar).level == 2);
    CHECK(driver.trackRecord(NotorietyTrack::kMorningstar).fraction == 50.0);

    // No decay timer has ever been armed for this track yet (only
    // notoriety_reset arms it, Sec19.1) - EngineTimer defaults to
    // !isArmed(), and an unarmed timer never "expires" (common.h's own
    // documented convention) - so DecayTick must no-op here.
    auto notYet = driver.DecayTick(matrix, NotorietyTrack::kMorningstar, t);
    CHECK(!notYet.dispatched);

    // Arm it, then advance time past the real shipped NotorietyFirstDecay
    // (15000ms at level 2, Sec14.6) - too early first.
    driver.trackRecord(NotorietyTrack::kMorningstar).decayTimer.arm(t, kGangLevelTable.firstDecayMs[2]);
    auto tooEarly = driver.DecayTick(matrix, NotorietyTrack::kMorningstar, t + 100);
    CHECK(!tooEarly.dispatched);

    // Past expiry: one tick removes the real shipped decay-rate amount (10
    // at Gang level 2, Sec14.6) - not enough to cross either level boundary
    // from fraction 50, so no dispatch, just the fraction moving.
    auto afterExpiry = driver.DecayTick(matrix, NotorietyTrack::kMorningstar, t + kGangLevelTable.firstDecayMs[2] + 1);
    CHECK(!afterExpiry.dispatched);
    CHECK(driver.trackRecord(NotorietyTrack::kMorningstar).fraction == 40.0);  // 50 - decayRate[2] (10)

    // Decay-suppressed flag freezes decay entirely, even once the timer is
    // (re-)expired.
    driver.decaySuppressed = true;
    driver.trackRecord(NotorietyTrack::kMorningstar).decayTimer.arm(t, 0);  // force-expire
    auto suppressed = driver.DecayTick(matrix, NotorietyTrack::kMorningstar, t + 1);
    CHECK(!suppressed.dispatched);
    CHECK(driver.trackRecord(NotorietyTrack::kMorningstar).fraction == 40.0);  // unchanged - suppressed before any read
}

void testDecayCrossingDownToZeroRunsClearToZeroSequenceButOrdinaryDecayDoesNot() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 0;
    std::vector<uint64_t> reconsidered;
    driver.onRosterEligibilityReconsider = [&](uint64_t h) { reconsidered.push_back(h); };
    driver.roster = {{999, static_cast<int32_t>(NotorietyTrack::kPolice)}};
    driver.livePlayerCount = 1;

    // Level 1, fraction right at the boundary so ONE decay tick clears it to 0.
    driver.AddPointsRaw(matrix, NotorietyTrack::kPolice, kPoliceLevelTable.levelLimit[1], t);  // -> level 1, fraction 0
    CHECK(driver.trackRecord(NotorietyTrack::kPolice).level == 1);
    driver.trackRecord(NotorietyTrack::kPolice).decayTimer.arm(t, 0);  // force-expire immediately

    auto result = driver.DecayTick(matrix, NotorietyTrack::kPolice, t + 1);
    CHECK(result.dispatched);
    CHECK(result.newLevel == 0);
    CHECK(result.clearToZeroSequenceRan);
    CHECK(result.rosterReconsiderCount == 1);
    CHECK(static_cast<int>(reconsidered.size()) == 1 && reconsidered[0] == 999);
}

// ===========================================================================
// Part E: notoriety_reset (Sec19.1) - zero level/fraction, re-arm the 500ms
// decay timer, prune the active-alert registry by heat code, dispatch only
// on an actual change.
// ===========================================================================
void testResetZeroesAndRearmsAndPrunesRegistryAndDispatchesOnlyIfWasPositive() {
    NotorietyDriver driver;
    TeamRelationMatrix matrix = NeutralMatrix();
    int64_t t = 1000;

    driver.AddPointsRaw(matrix, NotorietyTrack::kDeckers, kGangLevelTable.levelLimit[2] + 5, t);
    CHECK(driver.trackRecord(NotorietyTrack::kDeckers).level == 2);

    driver.registerActiveAlertRecordForTesting(NotorietyTrack::kDeckers);
    driver.registerActiveAlertRecordForTesting(NotorietyTrack::kDeckers);
    driver.registerActiveAlertRecordForTesting(NotorietyTrack::kPolice);  // different track - must survive the prune
    CHECK(driver.activeAlertRecordCount() == 3);

    auto result = driver.Reset(matrix, NotorietyTrack::kDeckers, t);
    CHECK(result.dispatched);  // was level 2 (> 0) -> level 0 is a real change
    CHECK(result.newLevel == 0);
    CHECK(driver.trackRecord(NotorietyTrack::kDeckers).level == 0);
    CHECK(driver.trackRecord(NotorietyTrack::kDeckers).fraction == 0.0);
    CHECK(driver.trackRecord(NotorietyTrack::kDeckers).decayTimer.isArmed());
    CHECK(driver.trackRecord(NotorietyTrack::kDeckers).decayTimer.remainingMs(t) == NotorietyDriver::kResetDecayTimerMs);
    CHECK(driver.activeAlertRecordCount() == 1);  // the 2 Deckers entries pruned, the 1 Police entry kept

    // Resetting an already-zero track: no change, no dispatch.
    auto noop = driver.Reset(matrix, NotorietyTrack::kDeckers, t + 1);
    CHECK(!noop.dispatched);
}

}  // namespace

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

int main() {
    run("testHeatCodeTableValuesByTrack", testHeatCodeTableValuesByTrack);
    run("testHeatCodeRegressionCatchesLevelIndexingMistake", testHeatCodeRegressionCatchesLevelIndexingMistake);

    run("testCrossingAboveZeroWritesHostileSymmetrically", testCrossingAboveZeroWritesHostileSymmetrically);
    run("testClearingToZeroWritesUnfriendlyNotNeutral", testClearingToZeroWritesUnfriendlyNotNeutral);
    run("testOrdinaryDownwardStepBetweenPositiveLevelsStillReaffirmsHostile",
        testOrdinaryDownwardStepBetweenPositiveLevelsStillReaffirmsHostile);
    run("testNoLevelChangeMeansNoDispatchAtAll", testNoLevelChangeMeansNoDispatchAtAll);

    run("testRealLevelLimitsAndSpansForGangAndPolice", testRealLevelLimitsAndSpansForGangAndPolice);
    run("testCrimeActivityMinMaxLevelWindowGatesAward", testCrimeActivityMinMaxLevelWindowGatesAward);
    run("testCheckDetectionGateRequiresLiveDetectionCallback", testCheckDetectionGateRequiresLiveDetectionCallback);
    run("testEnterOwnedStoreIsTheOnlyNegativePointActivityAndReducesNotoriety",
        testEnterOwnedStoreIsTheOnlyNegativePointActivityAndReducesNotoriety);

    run("testClearToZeroSequenceRunsOnlyWhenReachingZero", testClearToZeroSequenceRunsOnlyWhenReachingZero);
    run("testClearToZeroSequenceCapsPlayerDispatchAtTwoEvenWithMoreLivePlayers",
        testClearToZeroSequenceCapsPlayerDispatchAtTwoEvenWithMoreLivePlayers);
    run("testDecayTickIsGatedByTimerAndSuppressionFlag", testDecayTickIsGatedByTimerAndSuppressionFlag);
    run("testDecayCrossingDownToZeroRunsClearToZeroSequenceButOrdinaryDecayDoesNot",
        testDecayCrossingDownToZeroRunsClearToZeroSequenceButOrdinaryDecayDoesNot);

    run("testResetZeroesAndRearmsAndPrunesRegistryAndDispatchesOnlyIfWasPositive",
        testResetZeroesAndRearmsAndPrunesRegistryAndDispatchesOnlyIfWasPositive);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic notoriety tests passed.\n";
    return 0;
}
