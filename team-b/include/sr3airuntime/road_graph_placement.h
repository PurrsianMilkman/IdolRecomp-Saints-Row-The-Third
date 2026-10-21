#pragma once
// sr3airuntime/road_graph_placement.h - free-roam spawn placement on the REAL
// road/sidewalk graph (sr3zone::RoadGraph, spec-zone-data-format.md Sec13.2a):
//   * vehicles: spec-ai-runtime.md Sec20.6.1(c) candidate generation - road
//     segments from a spatial query around the camera (max 128), filtered by
//     the include/exclude flag masks, skipped when the segment is full
//     (3 live, 5 on a highway segment), spawn point = where the segment's
//     CHORD (node A -> node B) crosses the 130 m x scale ring, kept only
//     inside the speed-dependent moving cone and passing the closing test;
//     65 m inner-ring fallback (hidden points only) when no point exists at R.
//   * peds: Sec20.6.2(c) candidate edges - up to 100 sidewalk edges within
//     70 m x scale of the camera.
// Pure functions over the graph; the viewer owns the RNG, occupancy and the
// camera. NOT wired here (stay OPEN): zone-loaded test (needs a streamed-zone
// predicate), obstruction points (identity OPEN in the spec), co-op partner test.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3airuntime/common.h"
#include "sr3airuntime/freeroam_population.h"
#include "sr3airuntime/near_field_ped_population.h"
#include "sr3zone/road_graph.h"

namespace sr3airuntime {

// Sec20.6.1(c) include mask = 0x400 (Ambient class) | the Spline_Type flag bits of every streamed vehicle slot
// | 0x10000. Table 0x01308ad4 maps the six Spline_Type names (spawn_info_groups.xtbl, spec-tables-progression.md
// Sec10.6) to flag bits {Highway Only 0x10, Boat 0x40000, Offroad 0x10000, Indoor 0x400000, Baggage 0x20000,
// Taxi 0x200000}; any other text ("All Roads", "Surface Roads") stores -1 and adds no bit.
// With nothing streamed (the viewer feeds the streamed slots' Spline_Types via viewer_playtest_support.cpp, RG-3;
// the Sec20.6.1(e) re-roll cadence itself stays OPEN) the mask is the 0x10400 fallback.
inline constexpr uint32_t kRoadSpawnIncludeMask = 0x400u | 0x10000u;
inline uint32_t SplineTypeFlagBit(const std::string& name) {
    static const struct { const char* n; uint32_t bit; } kTable[6] = {
        {"Highway Only", 0x10u}, {"Boat", 0x40000u}, {"Offroad", 0x10000u},
        {"Indoor", 0x400000u},   {"Baggage", 0x20000u}, {"Taxi", 0x200000u}};
    for (const auto& e : kTable)
        if (name == e.n) return e.bit;
    return 0;
}
inline uint32_t BuildRoadSpawnIncludeMask(const std::vector<std::string>& streamedSplineTypes) {
    uint32_t m = kRoadSpawnIncludeMask;
    for (const auto& n : streamedSplineTypes) m |= SplineTypeFlagBit(n);
    return m;
}

// Sec13.2a(j)/Sec20.6.1(c) "Segment full": live near-field vehicles per road segment.
class SegmentOccupancy {
public:
    void Add(uint32_t seg) { ++counts_[seg]; }
    void Remove(uint32_t seg) {
        auto it = counts_.find(seg);
        if (it != counts_.end() && --it->second <= 0) counts_.erase(it);
    }
    int Count(uint32_t seg) const {
        auto it = counts_.find(seg);
        return it == counts_.end() ? 0 : it->second;
    }
    void Clear() { counts_.clear(); }
    // Vehicles on segments for which pred(segment) is true (e.g. bit-4 highway segments).
    template <class Pred>
    int CountWhere(Pred pred) const {
        int n = 0;
        for (const auto& kv : counts_)
            if (pred(kv.first)) n += kv.second;
        return n;
    }

private:
    std::unordered_map<uint32_t, int> counts_;
};
// Sec20.6.3(a): lane speed. Flag bit 19 (1000) never occurs on disk; class =
// segment +0x30 >> 4 = 0 for every shipped segment, "Default" row = 10 m/s.
inline constexpr double kAmbientLaneSpeedMs = 10.0;
inline constexpr int kSegmentCapacity = 3;         // Sec20.6.1(c)
inline constexpr int kHighwaySegmentCapacity = 5;  // Sec20.6.1(c)

struct RoadSpawnPoint {
    Vec3 pos;      // chord crossing of the ring (spec-faithful, Sec20.6.1(c))
    uint32_t segment = 0;
    Vec3 tangent;  // horizontal unit vector A -> B
    bool innerRing = false;
    // CHOSEN refinement: the nearest point on the lane's decoded polyline
    // (the chord can sit several metres off a curved lane), and the index of
    // the first polyline point AHEAD of it (a lane-follow cursor).
    Vec3 lanePos;
    size_t laneNextPoint = 1;
};

// Nearest point (x-z) of a segment's polyline to (x,z); y interpolated.
inline void ProjectOntoLane(const sr3zone::RoadGraph& graph, uint32_t seg, double x, double z, Vec3& out,
                            size_t& nextPoint) {
    const auto poly = graph.SegmentPolyline(seg);
    double best = 1e300;
    out = {poly[0].x, poly[0].y, poly[0].z};
    nextPoint = 1;
    for (size_t i = 0; i + 1 < poly.size(); ++i) {
        const double ex = static_cast<double>(poly[i + 1].x) - poly[i].x, ez = static_cast<double>(poly[i + 1].z) - poly[i].z;
        const double l2 = ex * ex + ez * ez;
        double t = l2 > 1e-12 ? ((x - poly[i].x) * ex + (z - poly[i].z) * ez) / l2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double px = poly[i].x + ex * t, pz = poly[i].z + ez * t;
        const double d = (px - x) * (px - x) + (pz - z) * (pz - z);
        if (d < best) {
            best = d;
            out = {px, poly[i].y + (static_cast<double>(poly[i + 1].y) - poly[i].y) * t, pz};
            nextPoint = i + 1;
        }
    }
}

struct RoadVehicleSpawnQuery {
    Vec3 centre;
    Vec3 cameraVelocity;
    double ringScale = 1.0;
    uint32_t includeMask = kRoadSpawnIncludeMask;
    uint32_t excludeMask = sr3zone::kSpawnExcludeMask;
    bool highwayOnly = false;
    size_t maxSegments = 128;
    size_t maxCandidates = 32;
    std::function<size_t(size_t)> pickUniform;               // required (random subset when > maxSegments)
    std::function<bool(const Vec3&)> isVisible;              // inner-ring hidden test; default: never visible
    std::function<int(uint32_t)> occupancy;                  // live vehicles on a segment; default 0
};

inline std::vector<RoadSpawnPoint> GatherRoadVehicleSpawnPoints(const sr3zone::RoadGraph& graph,
                                                                 const RoadVehicleSpawnQuery& q) {
    std::vector<RoadSpawnPoint> out;
    const double R = 130.0 * q.ringScale;
    const double speed = std::sqrt(q.cameraVelocity.x * q.cameraVelocity.x + q.cameraVelocity.z * q.cameraVelocity.z);
    const bool moving = speed > kCameraMovingSpeed;
    const double minDot = MovingConeMinDot(speed);
    const Vec3 vdir = moving ? Vec3{q.cameraVelocity.x / speed, 0.0, q.cameraVelocity.z / speed} : Vec3{0, 0, 0};

    // Spatial query (max 128 segments). The spec does not give the tree's
    // result order, so when more than the cap match a uniform random subset is
    // kept (CHOSEN).
    std::vector<uint32_t> nearSegs = graph.SegmentsNear(q.centre.x, q.centre.z, R);
    if (nearSegs.size() > q.maxSegments && q.pickUniform) {
        for (size_t i = 0; i < q.maxSegments; ++i) {
            const size_t j = i + q.pickUniform(nearSegs.size() - i);
            std::swap(nearSegs[i], nearSegs[std::min(j, nearSegs.size() - 1)]);
        }
        nearSegs.resize(q.maxSegments);
    }
    // Flag-mask filter, highway rule, segment-full test.
    std::vector<uint32_t> usable;
    for (uint32_t s : nearSegs) {
        const uint32_t f = graph.segments[s].flags;
        if ((f & q.includeMask) == 0 || (f & q.excludeMask) != 0) continue;
        const bool hwy = (f & sr3zone::kSegFlagHighway) != 0;
        if (q.highwayOnly && !hwy) continue;
        const int cap = hwy ? kHighwaySegmentCapacity : kSegmentCapacity;
        if (q.occupancy && q.occupancy(s) >= cap) continue;
        usable.push_back(s);
    }

    // Returns true when ANY usable segment's chord crosses the circle (before the cone/closing/hidden filters):
    // Sec20.6.1(c) retries at R/2 only when "no crossing exists at R". Both crossings of a chord are candidates.
    auto pass = [&](double radius, bool inner) {
        bool anyCrossing = false;
        for (uint32_t s : usable) {
            const auto& A = graph.SegA(s);
            const auto& B = graph.SegB(s);
            Vec3 crossings[2];
            const int nc = ChordCircleCrossings({A.x, A.y, A.z}, {B.x, B.y, B.z}, q.centre, radius, crossings);
            if (nc > 0) anyCrossing = true;
            for (int ci = 0; ci < nc; ++ci) {
                if (out.size() >= q.maxCandidates) return anyCrossing;
                const Vec3 p = crossings[ci];
                double dx = p.x - q.centre.x, dz = p.z - q.centre.z;
                const double dl = std::sqrt(dx * dx + dz * dz);
                if (dl < 1e-9) continue;
                dx /= dl;
                dz /= dl;
                if (moving && dx * vdir.x + dz * vdir.z < minDot) continue;  // moving cone
                double tx = static_cast<double>(B.x) - A.x, tz = static_cast<double>(B.z) - A.z;
                const double tl = std::sqrt(tx * tx + tz * tz);
                if (tl < 1e-9) continue;
                tx /= tl;
                tz /= tl;
                // Closing test: reject when (tangent x lane speed - camera velocity) . dir > 0.
                const double vx = tx * kAmbientLaneSpeedMs - q.cameraVelocity.x;
                const double vz = tz * kAmbientLaneSpeedMs - q.cameraVelocity.z;
                if (vx * dx + vz * dz > 0.0) continue;
                if (inner && q.isVisible && q.isVisible(p)) continue;  // inner-ring points must be hidden
                RoadSpawnPoint rp;
                rp.pos = p;
                rp.segment = s;
                rp.tangent = {tx, 0.0, tz};
                rp.innerRing = inner;
                ProjectOntoLane(graph, s, p.x, p.z, rp.lanePos, rp.laneNextPoint);
                out.push_back(rp);
            }
        }
        return anyCrossing;
    };
    const bool crossingAtR = pass(R, false);
    if (!crossingAtR) pass(R * 0.5, true);  // Sec20.6.1(c): no crossing at R -> retry at R/2 = 65 m, hidden points only
    return out;
}

// Sec20.6.2(c): up to maxEdges sidewalk edges within radius of the centre,
// nearest first (CHOSEN ordering: the spec gives the cap, not the order).
// SidewalkSegment::id = edge index + 1 (0 is "no segment" in the callers).
inline std::vector<SidewalkSegment> GatherRoadPedCandidateEdges(const sr3zone::RoadGraph& graph, const Vec3& centre,
                                                                 double radius = 70.0, size_t maxEdges = 100) {
    std::vector<std::pair<double, uint32_t>> byDist;
    for (uint32_t i : graph.SidewalkEdgesNear(centre.x, centre.z, radius)) {
        const auto& e = graph.sidewalkEdges[i];
        const auto& a = graph.sidewalkNodes[e.nodeA].pos;
        const auto& b = graph.sidewalkNodes[e.nodeB].pos;
        const double ex = static_cast<double>(b.x) - a.x, ez = static_cast<double>(b.z) - a.z;
        const double l2 = ex * ex + ez * ez;
        double t = l2 > 1e-12 ? ((centre.x - a.x) * ex + (centre.z - a.z) * ez) / l2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        byDist.push_back({std::hypot(a.x + ex * t - centre.x, a.z + ez * t - centre.z), i});
    }
    std::sort(byDist.begin(), byDist.end());
    if (byDist.size() > maxEdges) byDist.resize(maxEdges);
    std::vector<SidewalkSegment> out;
    out.reserve(byDist.size());
    for (const auto& d : byDist) {
        const auto& e = graph.sidewalkEdges[d.second];
        const auto& a = graph.sidewalkNodes[e.nodeA].pos;
        const auto& b = graph.sidewalkNodes[e.nodeB].pos;
        SidewalkSegment s;
        s.id = static_cast<uint64_t>(d.second) + 1;
        s.start = {a.x, a.y, a.z};
        s.end = {b.x, b.y, b.z};
        s.width = e.width();
        out.push_back(s);
    }
    return out;
}

}  // namespace sr3airuntime
