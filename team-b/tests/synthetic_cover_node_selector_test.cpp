// Synthetic tests for the cover_node selection unit, spec-ai-runtime.md
// Sec15.7 (include/sr3airuntime/cover_node_selector.h).
//
// Covers: angle dequantization/wrap, the arc-test formula (primary arc,
// secondary/pulled-back arc, the 30-degree open-cover override cone, the
// aerial-vehicle bypass), the hard player-shield gate, the AABB search
// volume (base/brute radius, follower/group-leader widening, post-area
// clipping), the brute-force gather+filter pipeline (unclaimed/list-20/
// open-usable/arc exclusions), the scoring formula's clamp/quantization,
// the claim compare-and-set, the list-20 blacklist + random back-off, and
// the step-6 reachability switch's CHOSEN stand-in (distance+collision)
// together with the flip to the injected real-mechanism seam - all fully
// synthetic, no real .czn_pc data read anywhere in this file.
//
// NOTE ON THE SWITCH'S DEFAULT. This file's own task brief originally
// called for testing that the reachability switch's default means "always
// no cover." That premise (a statically-dead navmesh registry) was REFUTED
// mid-task by a live capture of the real game (see cover_node_selector.h's
// own header comment) - the switch's role and default changed accordingly.
// This file tests the CORRECTED default instead: kCoverPathQueryUseStandIn
// defaults true, meaning step6 uses the labelled distance+collision
// stand-in (neither a hard "always fails" nor a hard "always succeeds"),
// and flipping it to false defers to an injected real-mechanism callable.
#include <iostream>
#include <vector>

#include "sr3airuntime/cover_node_selector.h"

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

constexpr double kPiLocal = 3.14159265358979323846;

CoverNode MakeCover(uint64_t handle, Vec3 pos, double yaw = 0.0) {
    CoverNode c;
    c.handle = handle;
    c.position = pos;
    c.yaw = yaw;
    return c;
}

// ===================================================================
// Part A: angle dequantization and wrap (Sec15.7's dequantize formula,
// CONFIRMED exactly: "(byte/255)*2*PI - PI").
// ===================================================================

void testDequantizeAngleByteEndpoints() {
    CHECK(std::fabs(DequantizeAngleByte(0) - (-kPiLocal)) < 1e-9);
    CHECK(std::fabs(DequantizeAngleByte(255) - kPiLocal) < 1e-9);
    // Midpoint (~127.5) dequantizes close to 0, never exactly 0 (255 is odd).
    CHECK(std::fabs(DequantizeAngleByte(127)) < 0.02);
    CHECK(std::fabs(DequantizeAngleByte(128)) < 0.02);
}

void testWrapAngleNormalizesLargeValues() {
    CHECK(std::fabs(WrapAngle(0.0)) < 1e-9);
    CHECK(std::fabs(WrapAngle(kPiLocal * 2.0)) < 1e-9);      // a full turn wraps to ~0
    CHECK(std::fabs(WrapAngle(kPiLocal * 4.0)) < 1e-9);      // two full turns likewise
    CHECK(WrapAngle(kPiLocal / 2.0) > 0 && WrapAngle(kPiLocal / 2.0) < kPiLocal);
}

// ===================================================================
// Part B: the arc test, 0x008cd1d0 (Sec15.7, read in full).
// ===================================================================

void testPrimaryArcExposedWhenBearingWithinArc() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, /*yaw=*/0.0);
    cover.coverAngleLeft = 0.3;
    cover.coverAngleRight = -0.3;  // arc = [-0.3, 0.3] around "facing +X"
    const Vec3 threatInFront{10, 0, 0};  // bearing 0 - dead centre of the arc
    CHECK(CoverArcTestExposed(cover, threatInFront, /*isAerial=*/false));
}

void testPrimaryArcNotExposedWhenBearingOutsideArc() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.1;
    cover.coverAngleRight = -0.1;
    cover.coverDefAngleLeft = 0.1;
    cover.coverDefAngleRight = -0.1;  // keep the secondary arc narrow too
    const Vec3 threatBehind{-10, 0, 0};  // bearing = PI, well outside +-0.1
    CHECK(!CoverArcTestExposed(cover, threatBehind, false));
}

void testSecondaryArcExposesWhenPrimaryIsSafe() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.1;
    cover.coverAngleRight = -0.1;        // narrow primary arc - this threat misses it
    cover.coverDefAngleLeft = 2.0;
    cover.coverDefAngleRight = 1.0;      // wide secondary arc (relative to the pulled-back point)
    const Vec3 threatToTheSide{0, 0, 10};  // bearing from cover ~ PI/2 (~1.5708), outside primary
    CHECK(!AngleWithinArc(WrapAngle(BearingTo(cover.position, threatToTheSide) - cover.yaw), cover.coverAngleLeft,
                          cover.coverAngleRight));  // sanity: primary itself really is safe here
    CHECK(CoverArcTestExposed(cover, threatToTheSide, false));  // but the secondary arc still catches it
}

void testOpenCoverOverrideForcesExposedWithin30DegCone() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.1;
    cover.coverAngleRight = -0.1;
    cover.coverDefAngleLeft = 0.05;
    cover.coverDefAngleRight = -0.05;  // secondary arc narrow too - threat below misses both on its own
    const Vec3 threat{0, 0, 10};       // primary bearing ~ PI/2
    const double bearing = BearingTo(cover.position, threat);

    cover.openCoverOverride = true;
    cover.openCoverDirection = bearing;  // dead-centre of the 30-degree cone
    CHECK(CoverArcTestExposed(cover, threat, false));
}

void testOpenCoverOverrideDoesNotForceWhenFlagUnset() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.1;
    cover.coverAngleRight = -0.1;
    cover.coverDefAngleLeft = 0.05;
    cover.coverDefAngleRight = -0.05;
    const Vec3 threat{0, 0, 10};
    cover.openCoverOverride = false;  // flag unset - no override, regardless of direction
    cover.openCoverDirection = BearingTo(cover.position, threat);
    CHECK(!CoverArcTestExposed(cover, threat, false));
}

void testOpenCoverOverrideDoesNotForceWhenOutsideCone() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.1;
    cover.coverAngleRight = -0.1;
    cover.coverDefAngleLeft = 0.05;
    cover.coverDefAngleRight = -0.05;
    const Vec3 threat{0, 0, 10};  // primary bearing ~ PI/2
    cover.openCoverOverride = true;
    cover.openCoverDirection = 0.0;  // ~90 degrees off the threat's own bearing - outside the 30-degree cone
    CHECK(!CoverArcTestExposed(cover, threat, false));
}

void testAerialVehicleBypassesArcTestEntirely() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.3;
    cover.coverAngleRight = -0.3;
    const Vec3 threatInFront{10, 0, 0};  // would normally expose via the primary arc
    CHECK(CoverArcTestExposed(cover, threatInFront, /*isAerial=*/false));  // sanity: non-aerial DOES expose
    CHECK(!CoverArcTestExposed(cover, threatInFront, /*isAerial=*/true)); // aerial bypasses entirely
}

// ===================================================================
// Part C: the hard player-shield gate (Sec15.7: "inside 0x00509e50, if any
// threat...resolves to the local player fails a shield/visibility test,
// the whole candidate cover is instantly disqualified" - independent of
// the arc test's own outcome for that candidate).
// ===================================================================

void testPlayerShieldGateDisqualifiesCandidateIndependentOfArc() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.1;
    cover.coverAngleRight = -0.1;
    cover.coverDefAngleLeft = 0.1;
    cover.coverDefAngleRight = -0.1;  // narrow both arcs - this threat is safely outside both

    PerceivedThreat player;
    player.position = Vec3{-10, 0, 0};  // behind the cover - arc-safe on its own
    player.isLocalPlayer = true;
    player.playerShieldOk = false;  // but fails the shield/visibility test
    std::vector<PerceivedThreat> threats = {player};

    CHECK(!CoverArcTestExposed(cover, player.position, false));  // sanity: arc alone says safe
    CHECK(!EvaluateCoverCandidate(cover, threats));              // the hard gate disqualifies it anyway
}

void testPlayerShieldGateOkWhenShieldHolds() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.1;
    cover.coverAngleRight = -0.1;
    cover.coverDefAngleLeft = 0.1;
    cover.coverDefAngleRight = -0.1;

    PerceivedThreat player;
    player.position = Vec3{-10, 0, 0};
    player.isLocalPlayer = true;
    player.playerShieldOk = true;
    std::vector<PerceivedThreat> threats = {player};
    CHECK(EvaluateCoverCandidate(cover, threats));
}

void testNonPlayerThreatIgnoredByShieldGateField() {
    CoverNode cover = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.1;
    cover.coverAngleRight = -0.1;
    cover.coverDefAngleLeft = 0.1;
    cover.coverDefAngleRight = -0.1;

    PerceivedThreat notPlayer;
    notPlayer.position = Vec3{-10, 0, 0};  // arc-safe
    notPlayer.isLocalPlayer = false;
    notPlayer.playerShieldOk = false;  // irrelevant - not the player
    std::vector<PerceivedThreat> threats = {notPlayer};
    CHECK(EvaluateCoverCandidate(cover, threats));  // not disqualified
}

// ===================================================================
// Part D: the AABB search volume (Sec15.7 step1).
// ===================================================================

void testBuildCoverSearchAabbBaseRadiusNonBrute() {
    CoverSearchHuman human;
    human.position = Vec3{100, 5, 200};
    human.isBrute = false;
    AABB box = BuildCoverSearchAabb(human);
    CHECK(box.min.x == 86 && box.max.x == 114);
    CHECK(box.min.y == 1 && box.max.y == 9);
    CHECK(box.min.z == 186 && box.max.z == 214);
}

void testBuildCoverSearchAabbBruteRadius() {
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};
    human.isBrute = true;
    AABB box = BuildCoverSearchAabb(human);
    CHECK(box.min.x == -15 && box.max.x == 15);
    CHECK(box.min.z == -15 && box.max.z == 15);
}

void testBuildCoverSearchAabbWidenedForFollowerGroupLeader() {
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};
    human.isFollower = true;
    human.hasGroupLeader = true;
    human.groupLeaderPosition = Vec3{50, 0, 0};  // well beyond the base 14.0 radius
    AABB box = BuildCoverSearchAabb(human);
    CHECK(box.max.x == 50);   // widened to include the leader
    CHECK(box.min.x == -14);  // the near side is untouched
}

void testBuildCoverSearchAabbNotWidenedForNonFollower() {
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};
    human.isFollower = false;  // not a follower - leader position must not widen the box
    human.hasGroupLeader = true;
    human.groupLeaderPosition = Vec3{50, 0, 0};
    AABB box = BuildCoverSearchAabb(human);
    CHECK(box.max.x == 14);
}

void testBuildCoverSearchAabbClippedToPostArea() {
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};
    human.hasPostArea = true;
    human.postAreaPosition = Vec3{0, 0, 0};
    human.postAreaRadius = 5;  // narrower than the base 14.0 search radius
    AABB box = BuildCoverSearchAabb(human);
    CHECK(box.min.x == -5 && box.max.x == 5);
    CHECK(box.min.z == -5 && box.max.z == 5);
    CHECK(box.min.y == -4 && box.max.y == 4);  // vertical bounds untouched by the post area
}

// ===================================================================
// Part E: gather (step2) + filter (step3).
// ===================================================================

void testGatherExcludesClaimedAndOutOfBoxCandidates() {
    AABB box;
    box.min = Vec3{-14, -4, -14};
    box.max = Vec3{14, 4, 14};

    std::vector<CoverNode> pool;
    pool.push_back(MakeCover(1, Vec3{5, 0, 5}));           // inside, unclaimed - survives
    CoverNode claimed = MakeCover(2, Vec3{5, 0, 5});
    claimed.claimedBy = 99;
    pool.push_back(claimed);                               // inside, but claimed - excluded
    pool.push_back(MakeCover(3, Vec3{100, 0, 100}));       // outside the box - excluded

    std::vector<CoverNode*> gathered = GatherCoverCandidates(box, pool);
    CHECK(gathered.size() == 1);
    CHECK(gathered[0]->handle == 1);
}

void testFilterExcludesRecentlyFailedList20() {
    CoverNode node = MakeCover(42, Vec3{5, 0, 5});
    std::vector<CoverNode*> gathered = {&node};
    PerceptionState perception;
    std::vector<PerceivedThreat> noThreats;

    CHECK(FilterCoverCandidates(gathered, perception, noThreats).size() == 1);  // not yet blacklisted

    perception.postStimulus(plist::CoverMemory20, node.handle, node.position, 0.0, 0, -1, 0, /*nowMs=*/1000);
    CHECK(FilterCoverCandidates(gathered, perception, noThreats).empty());  // now excluded
}

void testFilterExcludesNotOpenUsable() {
    CoverNode node = MakeCover(1, Vec3{5, 0, 5});
    node.isOpenUsable = false;
    std::vector<CoverNode*> gathered = {&node};
    PerceptionState perception;
    std::vector<PerceivedThreat> noThreats;
    CHECK(FilterCoverCandidates(gathered, perception, noThreats).empty());
}

void testFilterExcludesCandidateExposedToAThreat() {
    CoverNode node = MakeCover(1, Vec3{0, 0, 0}, 0.0);
    node.coverAngleLeft = 0.3;
    node.coverAngleRight = -0.3;
    std::vector<CoverNode*> gathered = {&node};
    PerceptionState perception;
    std::vector<PerceivedThreat> threats = {PerceivedThreat{/*handle=*/7, Vec3{10, 0, 0}}};  // bearing 0 - exposed
    CHECK(FilterCoverCandidates(gathered, perception, threats).empty());
}

// ===================================================================
// Part F: scoring (step4) - clamp/quantization.
// ===================================================================

void testScoreClampExactlyOneHundredWithExtremeWeights() {
    CoverNode cover = MakeCover(1, Vec3{3, 0, 0}, 0.0);
    cover.coverAngleLeft = 0.1;
    cover.coverAngleRight = -0.1;

    // A threat bearing EXACTLY at the arc's own edge -> margin == 0 ->
    // full exposure penalty. (A real candidate this exposed would already
    // be excluded by step3/EvaluateCoverCandidate - this test calls the
    // pure scoring function directly, which has no knowledge of step3's
    // own gating, to exercise the clamp math in isolation.)
    const double edgeBearing = 0.1;
    PerceivedThreat threatAtEdge;
    threatAtEdge.position = cover.position + Vec3{std::cos(edgeBearing), 0, std::sin(edgeBearing)} * 5.0;
    std::vector<PerceivedThreat> threats = {threatAtEdge};

    CoverSearchHuman human;
    human.position = cover.position + Vec3{50, 0, 0};  // far beyond the 10-unit radius used below
    human.isFollower = true;
    human.hasGroupLeader = true;
    human.groupLeaderPosition = cover.position + Vec3{0, 0, 2000};  // absurdly far from the cover

    CoverScoreWeights weights;
    weights.exposureWeight = 100;
    weights.distanceWeight = 100;
    weights.groupCohesionWeight = 100;  // sum 300 - comfortably forces the outer clamp

    double score = ScoreCoverCandidate(human, cover, threats, /*searchRadiusUsed=*/10.0, weights);
    CHECK(score == 100.0);
}

void testScoreIsQuantizedToWholeNumber() {
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};
    CoverNode cover = MakeCover(1, Vec3{3, 0, 0});  // distance 3
    std::vector<PerceivedThreat> noThreats;
    // default weights: distanceWeight=40; (3/7)*40 = 17.142857... -> rounds to 17.
    double score = ScoreCoverCandidate(human, cover, noThreats, /*searchRadiusUsed=*/7.0);
    CHECK(score == 17.0);
}

void testScoreIncreasesWithDistance() {
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};
    CoverNode near = MakeCover(1, Vec3{2, 0, 0});
    CoverNode far = MakeCover(2, Vec3{12, 0, 0});
    std::vector<PerceivedThreat> noThreats;
    double scoreNear = ScoreCoverCandidate(human, near, noThreats, 14.0);
    double scoreFar = ScoreCoverCandidate(human, far, noThreats, 14.0);
    CHECK(scoreFar > scoreNear);
}

void testGroupCohesionPenaltyOnlyAppliesToFollowers() {
    CoverNode cover = MakeCover(1, Vec3{2, 0, 0});
    std::vector<PerceivedThreat> noThreats;

    CoverSearchHuman nonFollower;
    nonFollower.position = Vec3{0, 0, 0};
    nonFollower.isFollower = false;
    nonFollower.hasGroupLeader = true;
    nonFollower.groupLeaderPosition = Vec3{0, 0, 1000};  // would be a huge penalty if it applied
    double scoreNonFollower = ScoreCoverCandidate(nonFollower, cover, noThreats, 14.0);

    CoverSearchHuman follower = nonFollower;
    follower.isFollower = true;
    double scoreFollower = ScoreCoverCandidate(follower, cover, noThreats, 14.0);

    CHECK(scoreFollower > scoreNonFollower);
}

// ===================================================================
// Part G: TryClaimCoverNode (step7's compare-and-set) and the list-20
// blacklist + random back-off.
// ===================================================================

void testTryClaimCoverNodeCompareAndSet() {
    CoverNode node = MakeCover(1, Vec3{0, 0, 0});
    CHECK(TryClaimCoverNode(node, /*claimant=*/10));  // unclaimed -> succeeds
    CHECK(node.claimedBy == 10);
    CHECK(!TryClaimCoverNode(node, /*claimant=*/20));  // claimed by someone else -> fails (contention)
    CHECK(node.claimedBy == 10);                       // unchanged
    CHECK(TryClaimCoverNode(node, /*claimant=*/10));    // same claimant re-claiming -> still succeeds
}

void testBlacklistWritesIntoPerceptionList20() {
    CoverNode a = MakeCover(1, Vec3{0, 0, 0});
    CoverNode b = MakeCover(2, Vec3{1, 0, 0});
    std::vector<CoverNode*> survivors = {&a, &b};
    PerceptionState perception;
    CHECK(!perception.hasEntry(plist::CoverMemory20, 1));
    CHECK(!perception.hasEntry(plist::CoverMemory20, 2));
    BlacklistSurvivorsIntoCoverMemory(perception, survivors, /*nowMs=*/5000);
    CHECK(perception.hasEntry(plist::CoverMemory20, 1));
    CHECK(perception.hasEntry(plist::CoverMemory20, 2));
}

void testRandomCoverSearchBackoffMsRangeAndNullMidpoint() {
    CHECK(RandomCoverSearchBackoffMs(nullptr) == 7000);
    DeterministicRng rng(123);
    for (int i = 0; i < 200; ++i) {
        int64_t ms = RandomCoverSearchBackoffMs(&rng);
        CHECK(ms >= 6000 && ms <= 8000);
    }
}

// ===================================================================
// Part H: the step-6 reachability switch. CORRECTED DEFAULT (see this
// file's own header comment): kCoverPathQueryUseStandIn defaults true,
// selecting the labelled distance+collision stand-in, NOT a hard
// "always unreachable"/"always reachable" extreme.
// ===================================================================

void testStandInDefaultIsTrueAndGatesByDistance() {
    CHECK(kCoverPathQueryUseStandIn);  // the documented default
    const Vec3 from{0, 0, 0};
    const Vec3 nearPoint{10, 0, 0};                         // well inside kCoverPathSearchBudgetA (128)
    const Vec3 farPoint{kCoverPathSearchBudgetA + 10, 0, 0}; // beyond it
    std::vector<CoverCollisionTriangle> noGeometry;
    CHECK(CoverCandidateIsPathReachable(from, nearPoint, nullptr, noGeometry));
    CHECK(!CoverCandidateIsPathReachable(from, farPoint, nullptr, noGeometry));
}

void testStandInBlockedByCollisionTriangle() {
    const Vec3 from{0, 0, 0};
    const Vec3 to{10, 0, 0};  // well within range
    // A triangle straddling the straight segment's own path, in the XZ plane.
    CoverCollisionTriangle blocker;
    blocker.ax = 5, blocker.az = -2;
    blocker.bx = 5, blocker.bz = 2;
    blocker.cx = 6, blocker.cz = 0;
    std::vector<CoverCollisionTriangle> geometry = {blocker};
    CHECK(!CoverCandidateIsPathReachable(from, to, nullptr, geometry));
}

void testStandInClearWhenCollisionGeometryIsFarAway() {
    const Vec3 from{0, 0, 0};
    const Vec3 to{10, 0, 0};
    CoverCollisionTriangle farAway;
    farAway.ax = 500, farAway.az = 500;
    farAway.bx = 501, farAway.bz = 500;
    farAway.cx = 500, farAway.cz = 501;
    std::vector<CoverCollisionTriangle> geometry = {farAway};
    CHECK(CoverCandidateIsPathReachable(from, to, nullptr, geometry));
}

void testSwitchFlippedDefersToRealPathQueryAndIgnoresStandIn() {
    const Vec3 from{0, 0, 0};
    const Vec3 to{kCoverPathSearchBudgetA + 500, 0, 0};  // WAY beyond the stand-in's own range
    std::vector<CoverCollisionTriangle> noGeometry;

    kCoverPathQueryUseStandIn = false;  // flip the switch
    CoverPathQueryFn alwaysTrue = [](const Vec3&, const Vec3&) { return true; };
    CHECK(CoverCandidateIsPathReachable(from, to, alwaysTrue, noGeometry));  // the stand-in's range is ignored now

    CoverPathQueryFn alwaysFalse = [](const Vec3&, const Vec3&) { return false; };
    CHECK(!CoverCandidateIsPathReachable(from, to, alwaysFalse, noGeometry));

    CHECK(!CoverCandidateIsPathReachable(from, to, nullptr, noGeometry));  // no mechanism -> unreachable, not true

    kCoverPathQueryUseStandIn = true;  // restore - this is a shared process-wide switch
}

// ===================================================================
// Part I: SelectCoverNode end to end (Sec15.7 steps 1-7 together).
// ===================================================================

void testSelectCoverNodeFindsAndClaimsReachableCoverByDefault() {
    kCoverPathQueryUseStandIn = true;  // make sure we're on the documented default
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};

    std::vector<CoverNode> pool;
    CoverNode good = MakeCover(1, Vec3{5, 0, 0}, 0.0);
    good.coverAngleLeft = 0.1;
    good.coverAngleRight = -0.1;  // narrow arc, no threats below so it can't be exposed anyway
    pool.push_back(good);

    std::vector<PerceivedThreat> noThreats;
    PerceptionState perception;
    std::vector<CoverCollisionTriangle> noGeometry;

    CoverSelectionResult result = SelectCoverNode(human, /*humanHandle=*/999, pool, noThreats, perception,
                                                    /*nowMs=*/1000, nullptr, noGeometry);
    CHECK(result.found);
    CHECK(result.coverHandle == 1);
    CHECK(pool[0].claimedBy == 999);  // claimed in place
}

void testSelectCoverNodeNoCoverWhenBlockedByCollisionAndBlacklists() {
    kCoverPathQueryUseStandIn = true;
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};

    std::vector<CoverNode> pool;
    CoverNode good = MakeCover(1, Vec3{5, 0, 0}, 0.0);
    good.coverAngleLeft = 0.1;
    good.coverAngleRight = -0.1;
    pool.push_back(good);

    std::vector<PerceivedThreat> noThreats;
    PerceptionState perception;

    // A triangle sitting directly between the human (0,0,0) and the cover (5,0,0).
    CoverCollisionTriangle blocker;
    blocker.ax = 2.5, blocker.az = -1;
    blocker.bx = 2.5, blocker.bz = 1;
    blocker.cx = 3.0, blocker.cz = 0;
    std::vector<CoverCollisionTriangle> geometry = {blocker};

    DeterministicRng backoffRng(5);
    CoverSelectionResult result =
        SelectCoverNode(human, 999, pool, noThreats, perception, /*nowMs=*/2000, nullptr, geometry, &backoffRng);
    CHECK(!result.found);
    CHECK(result.backoffMs >= 6000 && result.backoffMs <= 8000);
    CHECK(pool[0].claimedBy == 0);                               // never claimed
    CHECK(perception.hasEntry(plist::CoverMemory20, 1));         // blacklisted
}

void testSelectCoverNodeUsesRealQueryWhenSwitchFlipped() {
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};

    std::vector<CoverNode> pool;
    CoverNode good = MakeCover(1, Vec3{5, 0, 0}, 0.0);
    good.coverAngleLeft = 0.1;
    good.coverAngleRight = -0.1;
    pool.push_back(good);

    std::vector<PerceivedThreat> noThreats;
    PerceptionState perception;

    // A triangle that WOULD block the stand-in, but the switch bypasses it.
    CoverCollisionTriangle blocker;
    blocker.ax = 2.5, blocker.az = -1;
    blocker.bx = 2.5, blocker.bz = 1;
    blocker.cx = 3.0, blocker.cz = 0;
    std::vector<CoverCollisionTriangle> geometry = {blocker};

    kCoverPathQueryUseStandIn = false;
    CoverPathQueryFn alwaysReachable = [](const Vec3&, const Vec3&) { return true; };
    CoverSelectionResult result =
        SelectCoverNode(human, 999, pool, noThreats, perception, 3000, alwaysReachable, geometry);
    kCoverPathQueryUseStandIn = true;  // restore immediately

    CHECK(result.found);
    CHECK(pool[0].claimedBy == 999);
}

void testSelectCoverNodeNoCoverWhenNoCandidatesGathered() {
    kCoverPathQueryUseStandIn = true;
    CoverSearchHuman human;
    human.position = Vec3{0, 0, 0};
    std::vector<CoverNode> pool;  // empty pool
    std::vector<PerceivedThreat> noThreats;
    PerceptionState perception;
    CoverSelectionResult result = SelectCoverNode(human, 1, pool, noThreats, perception, 0);
    CHECK(!result.found);
    CHECK(result.survivors.empty());
}

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

}  // namespace

int main() {
    kCoverPathQueryUseStandIn = true;  // defensive reset before the suite runs

    run("testDequantizeAngleByteEndpoints", testDequantizeAngleByteEndpoints);
    run("testWrapAngleNormalizesLargeValues", testWrapAngleNormalizesLargeValues);

    run("testPrimaryArcExposedWhenBearingWithinArc", testPrimaryArcExposedWhenBearingWithinArc);
    run("testPrimaryArcNotExposedWhenBearingOutsideArc", testPrimaryArcNotExposedWhenBearingOutsideArc);
    run("testSecondaryArcExposesWhenPrimaryIsSafe", testSecondaryArcExposesWhenPrimaryIsSafe);
    run("testOpenCoverOverrideForcesExposedWithin30DegCone", testOpenCoverOverrideForcesExposedWithin30DegCone);
    run("testOpenCoverOverrideDoesNotForceWhenFlagUnset", testOpenCoverOverrideDoesNotForceWhenFlagUnset);
    run("testOpenCoverOverrideDoesNotForceWhenOutsideCone", testOpenCoverOverrideDoesNotForceWhenOutsideCone);
    run("testAerialVehicleBypassesArcTestEntirely", testAerialVehicleBypassesArcTestEntirely);

    run("testPlayerShieldGateDisqualifiesCandidateIndependentOfArc",
        testPlayerShieldGateDisqualifiesCandidateIndependentOfArc);
    run("testPlayerShieldGateOkWhenShieldHolds", testPlayerShieldGateOkWhenShieldHolds);
    run("testNonPlayerThreatIgnoredByShieldGateField", testNonPlayerThreatIgnoredByShieldGateField);

    run("testBuildCoverSearchAabbBaseRadiusNonBrute", testBuildCoverSearchAabbBaseRadiusNonBrute);
    run("testBuildCoverSearchAabbBruteRadius", testBuildCoverSearchAabbBruteRadius);
    run("testBuildCoverSearchAabbWidenedForFollowerGroupLeader",
        testBuildCoverSearchAabbWidenedForFollowerGroupLeader);
    run("testBuildCoverSearchAabbNotWidenedForNonFollower", testBuildCoverSearchAabbNotWidenedForNonFollower);
    run("testBuildCoverSearchAabbClippedToPostArea", testBuildCoverSearchAabbClippedToPostArea);

    run("testGatherExcludesClaimedAndOutOfBoxCandidates", testGatherExcludesClaimedAndOutOfBoxCandidates);
    run("testFilterExcludesRecentlyFailedList20", testFilterExcludesRecentlyFailedList20);
    run("testFilterExcludesNotOpenUsable", testFilterExcludesNotOpenUsable);
    run("testFilterExcludesCandidateExposedToAThreat", testFilterExcludesCandidateExposedToAThreat);

    run("testScoreClampExactlyOneHundredWithExtremeWeights", testScoreClampExactlyOneHundredWithExtremeWeights);
    run("testScoreIsQuantizedToWholeNumber", testScoreIsQuantizedToWholeNumber);
    run("testScoreIncreasesWithDistance", testScoreIncreasesWithDistance);
    run("testGroupCohesionPenaltyOnlyAppliesToFollowers", testGroupCohesionPenaltyOnlyAppliesToFollowers);

    run("testTryClaimCoverNodeCompareAndSet", testTryClaimCoverNodeCompareAndSet);
    run("testBlacklistWritesIntoPerceptionList20", testBlacklistWritesIntoPerceptionList20);
    run("testRandomCoverSearchBackoffMsRangeAndNullMidpoint", testRandomCoverSearchBackoffMsRangeAndNullMidpoint);

    run("testStandInDefaultIsTrueAndGatesByDistance", testStandInDefaultIsTrueAndGatesByDistance);
    run("testStandInBlockedByCollisionTriangle", testStandInBlockedByCollisionTriangle);
    run("testStandInClearWhenCollisionGeometryIsFarAway", testStandInClearWhenCollisionGeometryIsFarAway);
    run("testSwitchFlippedDefersToRealPathQueryAndIgnoresStandIn",
        testSwitchFlippedDefersToRealPathQueryAndIgnoresStandIn);

    run("testSelectCoverNodeFindsAndClaimsReachableCoverByDefault",
        testSelectCoverNodeFindsAndClaimsReachableCoverByDefault);
    run("testSelectCoverNodeNoCoverWhenBlockedByCollisionAndBlacklists",
        testSelectCoverNodeNoCoverWhenBlockedByCollisionAndBlacklists);
    run("testSelectCoverNodeUsesRealQueryWhenSwitchFlipped", testSelectCoverNodeUsesRealQueryWhenSwitchFlipped);
    run("testSelectCoverNodeNoCoverWhenNoCandidatesGathered", testSelectCoverNodeNoCoverWhenNoCandidatesGathered);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic cover_node-selector tests passed.\n";
    return 0;
}
