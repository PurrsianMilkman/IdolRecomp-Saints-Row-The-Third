// Placement on the real road/sidewalk graph (include/sr3airuntime/
// road_graph_placement.h): spec-ai-runtime.md Sec20.6.1(c) vehicle ring/cone
// spawn points and Sec20.6.2(c) pedestrian edges. Part 1 is synthetic geometry
// with hand-computed answers (always runs); part 2 asserts the spawn points lie
// on parsed lanes/sidewalks of the real city graph (SKIPPED part when the
// archive is absent, exit stays 0 for part 1).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3airuntime/road_graph_placement.h"
#include "sr3airuntime/traffic_following.h"
#include "sr3airuntime/traffic_lane_follower.h"
#include "sr3airuntime/traffic_yield.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

using namespace sr3airuntime;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)
bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

struct Lcg {
    uint64_t s = 12345;
    size_t operator()(size_t n) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return n ? static_cast<size_t>((s >> 33) % n) : 0;
    }
};

// Distance from p to segment a-b in x-z.
double DistToSeg(double px, double pz, double ax, double az, double bx, double bz) {
    const double ex = bx - ax, ez = bz - az, l2 = ex * ex + ez * ez;
    double t = l2 > 1e-12 ? ((px - ax) * ex + (pz - az) * ez) / l2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(ax + ex * t - px, az + ez * t - pz);
}

sr3zone::RoadGraph SyntheticGraph() {
    sr3zone::RoadGraph g;
    auto node = [&](float x, float z) { g.nodes.push_back({x, 0, z}); return static_cast<uint16_t>(g.nodes.size() - 1); };
    auto seg = [&](uint16_t a, uint16_t b, uint32_t flags) {
        sr3zone::RoadSegment s;
        s.nodeA = a; s.nodeB = b; s.flags = flags;
        s.ctrl1 = g.nodes[a]; s.ctrl2 = g.nodes[b];
        // AABB codec (Sec13.2a(c)): s16 offsets from node A, x 1/16 m.
        const float dx = g.nodes[b].x - g.nodes[a].x, dz = g.nodes[b].z - g.nodes[a].z;
        s.aabbMin = {static_cast<int16_t>(std::min(0.0f, dx) * 16), 0, static_cast<int16_t>(std::min(0.0f, dz) * 16)};
        s.aabbMax = {static_cast<int16_t>(std::max(0.0f, dx) * 16), 0, static_cast<int16_t>(std::max(0.0f, dz) * 16)};
        g.segments.push_back(s);
        return g.segments.size() - 1;
    };
    const uint32_t amb = sr3zone::kSegFlagAmbient;
    seg(node(-300, 0), node(300, 0), amb);             // 0: through road, west -> east
    seg(node(300, 40), node(-300, 40), amb);           // 1: east -> west, 40 m north
    seg(node(0, -300), node(0, 300), amb | 4u);        // 2: bit 2 connector class -> excluded
    seg(node(-300, 80), node(300, 80), amb | sr3zone::kSegFlagNoSpawn8);  // 3: bit 8 -> excluded
    seg(node(-100, -50), node(300, -50), amb);         // 4: starts inside the ring, moves away
    seg(node(-300, 120), node(300, 120), amb);         // 5: used for the capacity check
    seg(node(-300, 200), node(-100, 200), 0);          // 6: no Ambient bit -> not in the include mask
    g.RebuildIndices();
    return g;
}

void TestSynthetic() {
    sr3zone::RoadGraph g = SyntheticGraph();
    Lcg rng;
    RoadVehicleSpawnQuery q;
    q.centre = {0, 0, 0};
    q.pickUniform = [&](size_t n) { return rng(n); };
    auto pts = GatherRoadVehicleSpawnPoints(g, q);
    // Stationary camera: seg 0 at x=-130 (heading toward the camera), seg 1 at x=+130 (z=40 -> x=+-123.7).
    bool saw0 = false, saw1 = false;
    for (const auto& p : pts) {
        CHECK(Near(std::hypot(p.pos.x - q.centre.x, p.pos.z - q.centre.z), 130.0, 1e-6));
        CHECK(p.segment != 2 && p.segment != 3 && p.segment != 4 && p.segment != 6);
        const auto& A = g.SegA(p.segment);
        const auto& B = g.SegB(p.segment);
        CHECK(Near(DistToSeg(p.pos.x, p.pos.z, A.x, A.z, B.x, B.z), 0.0, 1e-6));
        if (p.segment == 0) { saw0 = true; CHECK(Near(p.pos.x, -130.0) && Near(p.tangent.x, 1.0)); }
        if (p.segment == 1) { saw1 = true; CHECK(Near(p.pos.x, std::sqrt(130.0 * 130.0 - 40.0 * 40.0))); }
    }
    CHECK(saw0 && saw1);

    // Moving +x at 13.4112 m/s: minDot 0.5, only the +x side (seg 1) survives.
    q.cameraVelocity = {13.4112, 0, 0};
    pts = GatherRoadVehicleSpawnPoints(g, q);
    CHECK(!pts.empty());
    for (const auto& p : pts) CHECK((p.pos.x - q.centre.x) / 130.0 >= 0.5 - 1e-9);  // inside the moving cone
    // Sec20.6.1(c) keeps BOTH chord/ring crossings: seg 0 (heading +x at 10 m/s, camera 13.4 m/s: not closing)
    // contributes its +130 crossing, never the -130 one (outside the cone).
    for (const auto& p : pts) if (p.segment == 0) CHECK(Near(p.pos.x, 130.0));
    CHECK(std::any_of(pts.begin(), pts.end(), [](const RoadSpawnPoint& p) { return p.segment == 0; }));
    q.cameraVelocity = {0, 0, 0};

    // Capacity: segment 0 holding 3 live vehicles is skipped.
    q.occupancy = [](uint32_t s) { return s == 0 ? 3 : 0; };
    pts = GatherRoadVehicleSpawnPoints(g, q);
    CHECK(std::none_of(pts.begin(), pts.end(), [](const RoadSpawnPoint& p) { return p.segment == 0; }));
    q.occupancy = nullptr;

    // Inner ring: a graph with only a short segment that never reaches 130 m.
    sr3zone::RoadGraph g2;
    g2.nodes = {{-100, 0, 0}, {0, 0, 0}};
    sr3zone::RoadSegment s;
    s.nodeA = 0; s.nodeB = 1; s.flags = sr3zone::kSegFlagAmbient; s.ctrl1 = g2.nodes[0]; s.ctrl2 = g2.nodes[1];
    s.aabbMax = {1600, 0, 0};
    g2.segments.push_back(s);
    g2.RebuildIndices();
    pts = GatherRoadVehicleSpawnPoints(g2, q);
    CHECK(pts.size() == 1);
    if (pts.size() == 1) {
        CHECK(pts[0].innerRing);
        CHECK(Near(pts[0].pos.x, -65.0));
    }
    q.isVisible = [](const Vec3&) { return true; };  // visible inner points are rejected
    pts = GatherRoadVehicleSpawnPoints(g2, q);
    CHECK(pts.empty());

    // Peds: edges within 70 m, nearest first, width carried.
    sr3zone::RoadGraph g3;
    g3.sidewalkNodes.resize(6);
    g3.sidewalkNodes[0].pos = {20, 0, 0};  g3.sidewalkNodes[1].pos = {20, 0, 30};
    g3.sidewalkNodes[2].pos = {60, 0, 0};  g3.sidewalkNodes[3].pos = {60, 0, 30};
    g3.sidewalkNodes[4].pos = {200, 0, 0}; g3.sidewalkNodes[5].pos = {200, 0, 30};
    for (uint16_t i = 0; i < 6; i += 2) {
        sr3zone::SidewalkEdge e;
        e.nodeA = i; e.nodeB = i + 1; e.width256 = static_cast<uint16_t>(i == 2 ? 4 * 256 : 3 * 256);
        g3.sidewalkEdges.push_back(e);
    }
    g3.RebuildIndices();
    auto edges = GatherRoadPedCandidateEdges(g3, {0, 0, 0});
    CHECK(edges.size() == 2);
    if (edges.size() == 2) {
        CHECK(edges[0].id == 1 && Near(edges[0].width, 3.0));
        CHECK(edges[1].id == 2 && Near(edges[1].width, 4.0));
        CHECK(Near(edges[0].start.x, 20) && Near(edges[0].end.z, 30));
    }
    edges = GatherRoadPedCandidateEdges(g3, {0, 0, 0}, 70.0, 1);
    CHECK(edges.size() == 1 && edges[0].id == 1);
}

// The lane graph the playtest follows is built from the parsed graph: polylines are
// the decoded segment points and adjacency is the on-disk successor lists
// (spec-ai-runtime.md Sec20.6.6: "use the on-disk links", not geometric coincidence).
void TestLaneGraphFromRoad() {
    sr3zone::RoadGraph g;
    g.nodes = {{0, 0, 0}, {50, 0, 0}, {100, 0, 0}, {100, 0, 40}};
    auto mk = [&](uint16_t a, uint16_t b, uint16_t succRef, uint32_t flags) {
        sr3zone::RoadSegment s;
        s.nodeA = a; s.nodeB = b; s.flags = flags; s.succRef = succRef;
        s.ctrl1 = g.nodes[a]; s.ctrl2 = g.nodes[b];
        g.segments.push_back(s);
    };
    // N = 3 segments; seg0 -> pooled list [1,2] (ref = N + 0 = 3); seg1, seg2 have no successors.
    mk(0, 1, 3, sr3zone::kSegFlagAmbient);
    mk(1, 2, 0xffff, sr3zone::kSegFlagAmbient);
    mk(1, 3, 0xffff, sr3zone::kSegFlagAmbient);
    g.succPool = {2, 1, 2};
    g.shapePool = {};
    g.RebuildIndices();
    TrafficLaneGraph lg(g);
    CHECK(lg.segmentCount() == 3);
    CHECK(lg.segment(0).polyline.size() == 2);
    CHECK(lg.segment(0).rawKindField == sr3zone::kSegFlagAmbient);
    CHECK((lg.NextSegments(0) == std::vector<size_t>{1, 2}));
    CHECK(lg.NextSegments(1).empty());
}

// ---- conformance-review fixes RG-3..RG-7 (spec-conformance-review.md Sec A) ----
void TestReviewFixes() {
    Lcg rng;

    // RG-3, Sec20.6.1(c): include mask = 0x400 | Spline_Type bits of streamed slots | 0x10000.
    // Table 0x01308ad4 = {0x10, 0x40000, 0x10000, 0x400000, 0x20000, 0x200000} for
    // {Highway Only, Boat, Offroad, Indoor, Baggage, Taxi}; "All Roads"/"Surface Roads" store -1 (no bit).
    CHECK(BuildRoadSpawnIncludeMask({}) == 0x10400u);  // nothing streamed: fall back to 0x10400
    CHECK(BuildRoadSpawnIncludeMask({"All Roads", "Surface Roads"}) == 0x10400u);
    CHECK(BuildRoadSpawnIncludeMask({"Highway Only"}) == 0x10410u);
    CHECK(BuildRoadSpawnIncludeMask({"Highway Only", "Taxi", "Baggage"}) == (0x10400u | 0x10u | 0x200000u | 0x20000u));
    CHECK(BuildRoadSpawnIncludeMask({"Boat", "Offroad", "Indoor"}) == (0x10400u | 0x40000u | 0x400000u));
    {
        // A segment carrying only the Highway bit (0x10, no Ambient) is eligible only with that bit streamed.
        sr3zone::RoadGraph h;
        h.nodes = {{-300, 0, 0}, {300, 0, 0}};
        sr3zone::RoadSegment s;
        s.nodeA = 0; s.nodeB = 1; s.flags = sr3zone::kSegFlagHighway; s.ctrl1 = h.nodes[0]; s.ctrl2 = h.nodes[1];
        s.aabbMin = {0, 0, 0}; s.aabbMax = {9600, 0, 0};
        h.segments.push_back(s);
        h.RebuildIndices();
        RoadVehicleSpawnQuery q;
        q.centre = {0, 0, 0};
        q.pickUniform = [&](size_t n) { return rng(n); };
        CHECK(GatherRoadVehicleSpawnPoints(h, q).empty());
        q.includeMask = BuildRoadSpawnIncludeMask({"Highway Only"});
        CHECK(!GatherRoadVehicleSpawnPoints(h, q).empty());
    }

    // RG-4, Sec20.6.1(c) "Segment full": 3 live vehicles, 5 on a bit-4 segment.
    {
        SegmentOccupancy occ;
        occ.Add(7); occ.Add(7); occ.Add(9);
        CHECK(occ.Count(7) == 2 && occ.Count(9) == 1 && occ.Count(11) == 0);
        occ.Remove(7);
        CHECK(occ.Count(7) == 1);
        occ.Clear();
        CHECK(occ.Count(9) == 0);

        sr3zone::RoadGraph h;
        h.nodes = {{-300, 0, 0}, {300, 0, 0}};
        sr3zone::RoadSegment s;
        s.nodeA = 0; s.nodeB = 1; s.flags = sr3zone::kSegFlagAmbient | sr3zone::kSegFlagHighway;
        s.ctrl1 = h.nodes[0]; s.ctrl2 = h.nodes[1];
        s.aabbMin = {0, 0, 0}; s.aabbMax = {9600, 0, 0};
        h.segments.push_back(s);
        h.RebuildIndices();
        RoadVehicleSpawnQuery q;
        q.centre = {0, 0, 0};
        q.pickUniform = [&](size_t n) { return rng(n); };
        for (int live : {3, 4}) {  // a bit-4 segment still has room at 3 and 4
            q.occupancy = [live](uint32_t) { return live; };
            CHECK(!GatherRoadVehicleSpawnPoints(h, q).empty());
        }
        q.occupancy = [](uint32_t) { return 5; };
        CHECK(GatherRoadVehicleSpawnPoints(h, q).empty());
    }

    // RG-5, Sec20.6.1(c) "Inner ring": the 65 m retry happens only when NO crossing exists at R.
    {
        sr3zone::RoadGraph h;
        h.nodes = {{0, 0, 0}, {300, 0, 0}, {-100, 0, 0}, {0, 0, 0}};
        auto mk = [&](uint16_t a, uint16_t b, int16_t maxX) {
            sr3zone::RoadSegment s;
            s.nodeA = a; s.nodeB = b; s.flags = sr3zone::kSegFlagAmbient;
            s.ctrl1 = h.nodes[a]; s.ctrl2 = h.nodes[b];
            s.aabbMin = {0, 0, 0}; s.aabbMax = {maxX, 0, 0};
            h.segments.push_back(s);
        };
        mk(0, 1, 4800);   // crosses R at x=+130 heading away: closing test rejects it
        mk(2, 3, 1600);   // reaches only 65 m (x=-65), heading toward the camera: would pass at 65 m
        h.RebuildIndices();
        RoadVehicleSpawnQuery q;
        q.centre = {0, 0, 0};
        q.pickUniform = [&](size_t n) { return rng(n); };
        CHECK(GatherRoadVehicleSpawnPoints(h, q).empty());  // a crossing exists at R, so no 65 m retry
        sr3zone::RoadGraph h2;
        h2.nodes = {h.nodes[2], h.nodes[3]};
        sr3zone::RoadSegment s = h.segments[1];
        s.nodeA = 0; s.nodeB = 1; s.ctrl1 = h2.nodes[0]; s.ctrl2 = h2.nodes[1];
        h2.segments.push_back(s);
        h2.RebuildIndices();
        CHECK(GatherRoadVehicleSpawnPoints(h2, q).size() == 1);  // no crossing at R: the 65 m point is kept
    }
    {
        // Frustum test for the +-1.5 m box (Sec20.6.1(c)): camera at origin looking +z, 90 deg, aspect 1.
        const float f = 1.0f, nearZ = 0.5f, farZ = 1000.0f, qq = farZ / (farZ - nearZ);
        const float vp[16] = {f, 0, 0, 0, 0, f, 0, 0, 0, 0, qq, 1, 0, 0, -nearZ * qq, 0};
        CHECK(BoxInFrustum(vp, {0, 0, 50}, 1.5));
        CHECK(!BoxInFrustum(vp, {0, 0, -50}, 1.5));
        CHECK(!BoxInFrustum(vp, {200, 0, 50}, 1.5));
        CHECK(BoxInFrustum(vp, {51, 0, 50}, 1.5));   // box edge reaches inside the right plane (x < z)
        CHECK(!BoxInFrustum(vp, {54, 0, 50}, 1.5));
    }

    // RG-6, Sec20.6.1(b)/(d) 2026-10-15 FINAL vectors: displacement x 30 (fixed, not real dt); moving gate
    // dot(camFwd, delta) x 30 > 2.2352; behind = dot(vehPos - camPos, delta) < 0.
    {
        const Vec3 fwd{0, 1, 0};
        Vec3 d = CameraVelocityFromDisplacement({0, 0.1, 0});
        CHECK(Near(d.y, 3.0, 1e-9));
        CHECK(Near(DespawnForwardSpeed(fwd, {0, 0.1, 0}), 3.0, 1e-9));  // camPos (0,0,0), prev (0,-0.1,0)
        CHECK(DespawnCameraMoving(fwd, {0, 0.1, 0}));
        CHECK(VehicleBehindCameraMotion({0, -50, 0}, {0, 0.1, 0}));    // dot -5 -> 65 m
        CHECK(!VehicleBehindCameraMotion({0, 50, 0}, {0, 0.1, 0}));    // dot +5 -> 140 m
        CHECK(Near(DespawnForwardSpeed(fwd, {0, 0.05, 0}), 1.5, 1e-9));
        CHECK(!DespawnCameraMoving(fwd, {0, 0.05, 0}));                // below the 2.2352 gate: still camera
        CHECK(!DespawnCameraMoving(fwd, {0.2, 0, 0}));                 // strafing: forward component 0 -> still
        CHECK(!DespawnCameraMoving(fwd, {0, -0.1, 0}));                // reversing -> still
    }

    // RG-7, Sec20.6.1(c): both chord/ring crossings are kept; highway rule.
    {
        Vec3 pts[2];
        CHECK(ChordCircleCrossings({60, 0, -300}, {60, 0, 300}, {0, 0, 0}, 130.0, pts) == 2);
        const double zc = std::sqrt(130.0 * 130.0 - 60.0 * 60.0);
        CHECK(Near(pts[0].z, -zc) && Near(pts[1].z, zc));
        CHECK(ChordCircleCrossings({60, 0, 0}, {60, 0, 300}, {0, 0, 0}, 130.0, pts) == 1);  // one end inside
        sr3zone::RoadGraph h;
        h.nodes = {{70, 0, -300}, {70, 0, 300}};
        sr3zone::RoadSegment s;
        s.nodeA = 0; s.nodeB = 1; s.flags = sr3zone::kSegFlagAmbient; s.ctrl1 = h.nodes[0]; s.ctrl2 = h.nodes[1];
        s.aabbMin = {0, 0, 0}; s.aabbMax = {0, 0, 9600};
        h.segments.push_back(s);
        h.RebuildIndices();
        RoadVehicleSpawnQuery q;
        q.centre = {0, 0, 0};
        q.cameraVelocity = {30, 0, 0};  // cone minDot 0.5; both crossings are inside it and not closing
        q.pickUniform = [&](size_t n) { return rng(n); };
        CHECK(GatherRoadVehicleSpawnPoints(h, q).size() == 2);
        // Highway rule: active only while the player's segment is bit 4 and fewer than trunc(0.75 x target)
        // near-field vehicles are on bit-4 segments.
        CHECK(HighwayRuleActive(true, 5, 12));    // 5 < trunc(9)
        CHECK(!HighwayRuleActive(true, 9, 12));   // 9 !< 9
        CHECK(!HighwayRuleActive(false, 0, 12));  // player not on a bit-4 segment
        CHECK(!HighwayRuleActive(true, 0, 1));    // trunc(0.75) = 0
    }
}

// RG-12 / RG-13, Sec20.6.3(b) throttle law and Sec20.6.7(b) following law (hand-computed vectors).
// Sec20.6.7(c) junction yield on a hand-built crossing: approach 0 -> connector 1 (west to east, conflicts with 2);
// crossing connector 2 (south to north) fed by predecessor 3.
void TestJunctionYield() {
    sr3zone::RoadGraph g;
    auto node = [&](float x, float z) { g.nodes.push_back({x, 0, z}); return static_cast<uint16_t>(g.nodes.size() - 1); };
    auto seg = [&](uint16_t a, uint16_t b) {
        sr3zone::RoadSegment s;
        s.nodeA = a; s.nodeB = b; s.flags = sr3zone::kSegFlagAmbient;
        s.ctrl1 = g.nodes[a]; s.ctrl2 = g.nodes[b];
        g.segments.push_back(s);
        return g.segments.size() - 1;
    };
    seg(node(-100, 0), node(-10, 0));  // 0 approach
    seg(node(-10, 0), node(10, 0));    // 1 connector (conflicts with 2)
    seg(node(0, -10), node(0, 10));    // 2 cross connector
    seg(node(0, -100), node(0, -10));  // 3 cross approach (feeds 2)
    g.segments[0].succRef = 1;
    g.segments[1].conflictRef = 2;
    g.segments[2].predRef = 3;
    g.segments[3].succRef = 2;
    g.RebuildIndices();
    CHECK(RoadSegmentLength(g, 1) == 20.0);

    YieldOwn own;
    own.id = 2; own.seg = 0; own.distToSegEnd = 5.0; own.speed = 5.0; own.aB = 12.06;
    auto none = [](size_t) { return std::vector<YieldOther>{}; };
    // Conflict segment 5 m ahead, nobody else around: found, no yield.
    YieldDecision d = EvaluateJunctionYield(g, own, none);
    CHECK(d.found && !d.yield && d.conflictSeg == 1 && Near(d.distToConflict, 5.0));
    // Beyond the look-ahead D = 25/(2*0.75*10) + 5 = 6.67 m: not found.
    own.distToSegEnd = 8.0;
    CHECK(!EvaluateJunctionYield(g, own, none).found);
    own.distToSegEnd = 5.0;
    // Another car 5 m before the cross connector, same speed, lower id: overlapping windows -> yield.
    auto cross = [](uint64_t id, double dEnd, double v) {
        return [=](size_t s) { return s == 3 ? std::vector<YieldOther>{{id, dEnd, v}} : std::vector<YieldOther>{}; };
    };
    CHECK(EvaluateJunctionYield(g, own, cross(1, 5.0, 5.0)).yield);
    // Anti-deadlock tie-break (CHOSEN): the same car with a higher id goes first, we do not yield.
    CHECK(!EvaluateJunctionYield(g, own, cross(3, 5.0, 5.0)).yield);
    // A car 90 m away (windows separated by more than 2 s): no yield.
    CHECK(!EvaluateJunctionYield(g, own, cross(1, 90.0, 10.0)).yield);
    // A car already inside the conflicting connector enters first: yield.
    auto inside = [](size_t s) { return s == 2 ? std::vector<YieldOther>{{1, 20.0, 5.0}} : std::vector<YieldOther>{}; };
    CHECK(EvaluateJunctionYield(g, own, inside).yield);
    // Too fast to stop within the distance: v^2/(2 a_b) = 100/24.12 = 4.15 > 4 -> drive through.
    own.speed = 10.0; own.distToSegEnd = 4.0;
    d = EvaluateJunctionYield(g, own, inside);
    CHECK(d.found && !d.yield);
}

void TestFollowingAndThrottle() {
    // Throttle law 0x00b207d0: vt<1 -> 0.001; k = max(2,(vt/5)^2); thr = k(1 - v^2/vt^2) clamped to
    // [0.001, cap]; cap = 1.0 with the flag, else 0.7 + 0.3 clamp((v-5)/5, 0, 1).
    CHECK(Near(AmbientThrottleLaw(0.5, 3.0, false), 0.001));
    CHECK(Near(AmbientThrottleLaw(10.0, 0.0, false), 0.7));    // k=4, 4 -> cap 0.7 at rest
    CHECK(Near(AmbientThrottleLaw(10.0, 8.0, false), 0.88));   // 4*0.36=1.44 -> cap 0.7+0.3*0.6
    CHECK(Near(AmbientThrottleLaw(10.0, 10.0, false), 0.001)); // at the target
    CHECK(Near(AmbientThrottleLaw(10.0, 0.0, true), 1.0));     // flag: cap 1.0
    CHECK(Near(AmbientThrottleLaw(3.5, 0.0, true), 1.0));      // k=2 -> 2 -> cap 1.0
    CHECK(Near(AmbientThrottleLaw(3.5, 3.0, true), 2.0 * (1.0 - 9.0 / 12.25)));  // 0.5306
    CHECK(Near(kAmbientLaneSpeedMs, 10.0));

    // Following law 0x00b51950, a_b = 8 -> a_lim = 8, a_c = 6.
    auto F = [](double d, double u, double v, bool braked, int kind = 0) {
        return FollowingLaw(8.0, kind, d, u, v, braked);
    };
    FollowingResult r = F(25.0, 0.0, 10.0, false);  // dEff 20, req 2.5 <= 6; cap = u + sqrt(8*21)
    CHECK(Near(r.brake, 0.0) && Near(r.cap, std::sqrt(8.0 * 21.0)));
    r = F(13.0, 0.0, 10.0, false);  // dEff 8, req 100/16 = 6.25 > 6: brake, cap vs = sqrt(2*6*8)
    CHECK(Near(r.brake, 6.25) && Near(r.cap, std::sqrt(96.0)) && r.remember);
    r = F(11.0, 10.0, 10.0, false);  // steady gap dEff = 6, lead at own speed: cap = u + sqrt(8*7)
    CHECK(Near(r.brake, 0.0) && Near(r.cap, 10.0 + std::sqrt(56.0)));
    r = F(10.0, 10.0, 10.0, false);  // dEff 5 < 6: cap = max(u-0.5, 2)
    CHECK(Near(r.brake, 0.0) && Near(r.cap, 9.5));
    r = F(5.2, 0.0, 10.0, false);    // dEff 0.2 < 0.5 and u < 2 -> 0 -> full braking
    CHECK(Near(r.brake, 8.0) && Near(r.cap, 0.0));
    r = F(15.0, 0.0, 10.0, false);   // req 5 <= 0.75*8: no brake, hold: cap = max(v,2)
    CHECK(Near(r.brake, 0.0) && Near(r.cap, 10.0) && !r.remember);
    r = F(15.0, 0.0, 10.0, true);    // remembered within 300 ms: hysteresis 0.5*8 = 4 < 5 -> brake
    CHECK(Near(r.brake, 5.0) && Near(r.cap, std::sqrt(120.0)) && r.remember);
    r = F(8.0, 0.0, 10.0, false, 4); // kind 4: dEff = d - 3 = 5
    CHECK(Near(r.brake, 10.0) && Near(r.cap, std::sqrt(2.0 * 6.0 * 5.0)));

    // RF-3, Sec20.6.7(a): the obstacle probe re-arms after trunc(max(0, (15 - v)/15 * 140)) + 60 ms.
    CHECK(ObstacleProbeRearmMs(0.0) == 200);   // stopped
    CHECK(ObstacleProbeRearmMs(10.0) == 106);  // trunc(46.67) + 60
    CHECK(ObstacleProbeRearmMs(15.0) == 60);
    CHECK(ObstacleProbeRearmMs(30.0) == 60);   // clamped at 0 above 15 m/s
    // RF-3, Sec20.6.3(b): a_b = AI_Max_Braking_Decel x {1.0, 0.8, 0.7, 0.6, 0.5}[condition level].
    CHECK(Near(ConditionScaledBrakingDecel(12.06, 0), 12.06));
    CHECK(Near(ConditionScaledBrakingDecel(12.06, 1), 12.06 * 0.8));
    CHECK(Near(ConditionScaledBrakingDecel(12.06, 4), 12.06 * 0.5));
    CHECK(Near(ConditionScaledBrakingDecel(12.06, 9), 12.06 * 0.5));  // clamped to the last scale

    // RF-2, Sec20.6.13(f)/(g): the light test runs for mode 8 (and HIGH mode 14) with the obey bit; the stop line
    // only for mode 8 with the obey bit; chase 4, flee 2, rejoin 6 and every other mode skip both.
    CHECK(RunsSignalRule(8, true) && RunsSignalRule(14, true));
    CHECK(!RunsSignalRule(8, false) && !RunsSignalRule(14, false));
    for (int mode : {0, 2, 4, 6, 7, 15}) CHECK(!RunsSignalRule(mode, true));
    CHECK(RunsStopLineRule(8, true) && !RunsStopLineRule(14, true) && !RunsStopLineRule(8, false));
    CHECK(!RunsStopLineRule(4, true));

    // Sec20.6.7(c) junction yield (windows HIGH): look-ahead D = v^2/(2 f a_lim) + 5; no yield when the car could
    // not stop anyway; occupancy windows predicted at a constant 3 m/s^2 up to the target speed.
    CHECK(Near(YieldLookahead(10.0, 12.06, false), 100.0 / (2.0 * 0.75 * 10.0) + 5.0));
    CHECK(Near(YieldLookahead(10.0, 12.06, true), 100.0 / (2.0 * 0.5 * 10.0) + 5.0));
    CHECK(YieldCouldStop(10.0, 12.06, 5.0));  // 100/24.12 = 4.1 <= 5
    CHECK(!YieldCouldStop(10.0, 12.06, 4.0));
    CHECK(Near(YieldTimeToCover(0.0, 5.0, 10.0), 0.0));
    CHECK(Near(YieldTimeToCover(10.0, 10.0, 10.0), 1.0));                // already at the target speed
    CHECK(Near(YieldTimeToCover(6.0, 0.0, 10.0), 2.0));                  // still accelerating: 1.5 t^2 = 6
    CHECK(Near(YieldTimeToCover(36.67, 0.0, 10.0), 10.0 / 3.0 + 2.0, 1e-2));  // 16.67 m to speed, then cruise
    {
        const YieldWindow own = PredictOccupancyWindow(10.0, 8.0, 5.0, 10.0);
        const YieldWindow far = PredictOccupancyWindow(200.0, 8.0, 10.0, 10.0);  // ~20 s away
        const YieldWindow near_ = PredictOccupancyWindow(12.0, 8.0, 5.0, 10.0);
        CHECK(own.exit > own.enter);
        CHECK(!YieldWindowsConflict(own, far));   // separated by more than 2 s
        CHECK(YieldWindowsConflict(own, near_));  // overlapping windows
        CHECK(YieldWindowsConflict(own, own));
    }
}

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}
bool findEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            if (findEntry(c.openNested(i), name, out)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

// Min x-z distance from a point to the segment's decoded polyline.
double DistToPolyline(const sr3zone::RoadGraph& g, uint32_t seg, double x, double z) {
    const auto poly = g.SegmentPolyline(seg);
    double best = 1e30;
    for (size_t i = 0; i + 1 < poly.size(); ++i)
        best = std::min(best, DistToSeg(x, z, poly[i].x, poly[i].z, poly[i + 1].x, poly[i + 1].z));
    return best;
}

void TestReal(const sr3zone::RoadGraph& g) {
    Lcg rng;
    RoadVehicleSpawnQuery q;
    q.pickUniform = [&](size_t n) { return rng(n); };
    const Vec3 centres[] = {{-320, 16, -560}, {-203.5, 16.3, -511.5}, {745.3, 3, -980}, {0, 0, 0}, {300, 10, -300}};
    size_t total = 0;
    double worstChordVsCurve = 0;
    for (const Vec3& c : centres) {
        q.centre = c;
        for (int moving = 0; moving < 2; ++moving) {
            q.cameraVelocity = moving ? Vec3{8.0, 0, 3.0} : Vec3{0, 0, 0};
            const auto pts = GatherRoadVehicleSpawnPoints(g, q);
            CHECK(pts.size() <= q.maxCandidates);
            for (const auto& p : pts) {
                ++total;
                const double d = std::hypot(p.pos.x - c.x, p.pos.z - c.z);
                CHECK(Near(d, p.innerRing ? 65.0 : 130.0, 1e-4));
                const auto& s = g.segments[p.segment];
                CHECK((s.flags & kRoadSpawnIncludeMask) != 0);
                CHECK((s.flags & sr3zone::kSpawnExcludeMask) == 0);
                const auto& A = g.SegA(p.segment);
                const auto& B = g.SegB(p.segment);
                CHECK(DistToSeg(p.pos.x, p.pos.z, A.x, A.z, B.x, B.z) < 1e-3);        // exactly on the lane chord
                const double dc = DistToPolyline(g, p.segment, p.pos.x, p.pos.z);      // and near the curve
                worstChordVsCurve = std::max(worstChordVsCurve, dc);
                CHECK(dc < 12.0);  // chord vs curve is informational (max printed below); the lane projection is the on-lane guarantee
                CHECK(DistToPolyline(g, p.segment, p.lanePos.x, p.lanePos.z) < 1e-3);
                CHECK(p.laneNextPoint >= 1 && p.laneNextPoint < g.SegmentPolyline(p.segment).size());
            }
        }
    }
    std::printf("[placement-real] vehicle spawn points checked: %zu, worst chord-vs-polyline %.3f m\n", total, worstChordVsCurve);
    CHECK(total > 0);

    // Peds: every candidate edge is a real edge within 70 m, and a spawn lies within width/2 of it.
    size_t edgesSeen = 0, spawns = 0;
    for (const Vec3& c : centres) {
        auto edges = GatherRoadPedCandidateEdges(g, c);
        CHECK(edges.size() <= 100);
        for (const auto& e : edges) {
            ++edgesSeen;
            CHECK(e.id >= 1 && e.id <= g.sidewalkEdges.size());
            const auto& re = g.sidewalkEdges[e.id - 1];
            const auto& na = g.sidewalkNodes[re.nodeA].pos;
            const auto& nb = g.sidewalkNodes[re.nodeB].pos;
            CHECK(Near(e.start.x, na.x, 1e-4) && Near(e.end.z, nb.z, 1e-4));
            CHECK(Near(e.width, re.width256 / 256.0));
            CHECK(DistToSeg(c.x, c.z, na.x, na.z, nb.x, nb.z) <= 70.0 + 1e-6);
        }
        NearFieldPedSpawnGates gates;
        auto pick = [&](size_t n) { return rng(n); };
        for (int i = 0; i < 20 && !edges.empty(); ++i) {
            auto r = SelectNearFieldPedSpawn(edges, gates, c, pick);
            if (!r.found) continue;
            ++spawns;
            const auto& re = g.sidewalkEdges[r.segmentId - 1];
            const auto& na = g.sidewalkNodes[re.nodeA].pos;
            const auto& nb = g.sidewalkNodes[re.nodeB].pos;
            CHECK(DistToSeg(r.position.x, r.position.z, na.x, na.z, nb.x, nb.z) <= re.width256 / 512.0 + 1e-6);
        }
    }
    std::printf("[placement-real] ped edges checked: %zu, spawns checked: %zu\n", edgesSeen, spawns);
    CHECK(edgesSeen > 0);
    CHECK(spawns > 0);
}
}  // namespace

int main() {
    TestSynthetic();
    TestLaneGraphFromRoad();
    TestReviewFixes();
    TestFollowingAndThrottle();
    TestJunctionYield();
#ifdef CRREISH_GAME_CACHE_DIR
    const std::string path = std::string(CRREISH_GAME_CACHE_DIR) + "/sr3_city_0.vpp_pc";
    if (std::ifstream(path, std::ios::binary).good()) {
        try {
            std::vector<uint8_t> czn;
            auto opened = vppcache::openArchive(path);
            if (findEntry(opened->container, "sr3_city~fawld.czn_pc", czn)) {
                const auto g = sr3zone::RoadGraph::Parse(vpp::ByteView(czn.data(), czn.size()));
                TestReal(g);
            } else {
                std::cout << "fawld.czn_pc not found - real part skipped\n";
            }
        } catch (const std::exception& ex) {
            std::cerr << "real part threw: " << ex.what() << "\n";
            ++g_failures;
        }
    } else {
        std::cout << "archive absent - real part skipped\n";
    }
#endif
    if (g_failures == 0) std::cout << "road_graph_placement_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
