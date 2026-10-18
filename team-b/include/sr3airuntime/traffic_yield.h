#pragma once
// sr3airuntime/traffic_yield.h - the cross-traffic junction yield of spec-ai-runtime.md Sec20.6.7(c)
// (0x00b5ed50 -> 0x00b5e6c0), on the conflict lists (Sec13.2a(e)) of sr3zone::RoadGraph.
//
// Gates (FINAL): look-ahead D = v^2/(2 f a_lim) + 5; the route ahead is walked, adding chord lengths, to the first
// segment with a non-empty conflict list within D; no yield when the car could not stop before it anyway; for every
// car on each conflicting segment and on that segment's single-predecessor chain the occupancy windows of both cars
// are predicted at a constant 3 m/s^2 up to the target speed and the car yields when they are not separated by more
// than 2 s. The yield is law kind 3 of Sec20.6.7(b) at the start of the conflict segment (the caller applies it).
//
// CHOSEN stand-ins (see the report questions): (1) the viewer has no route object (vehicle +0x11a4), so at a fork
// the successor that best matches the car's heading is walked; (2) the exact window terms are HIGH only (car length
// not added); (3) anti-deadlock tie-break: on overlapping windows only the car that would enter the conflict later
// (equal: the higher id) yields. The spec gives no tie-break; two cars stopped 5 m before crossing segments would
// otherwise yield to each other for ever; (4) the predecessor chain is followed at most 3 segments.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "sr3airuntime/traffic_following.h"
#include "sr3zone/road_graph.h"

namespace sr3airuntime {

struct YieldOther {
    uint64_t id = 0;
    double distToSegEnd = 0.0;  // along the lane the car is on
    double speed = 0.0;
};
struct YieldOwn {
    uint64_t id = 0;
    size_t seg = 0;             // segment the car is on
    double distToSegEnd = 0.0;  // to the end of `seg`
    double speed = 0.0;
    double headingX = 1.0, headingZ = 0.0;  // used only to choose a successor at a fork
    double aB = 8.0;
    double targetSpeed = 10.0;
    bool recentlyBraked = false;
};
struct YieldDecision {
    bool found = false;           // a conflict segment lies within the look-ahead
    bool yield = false;           // brake for it (law kind 3 at distToConflict)
    double distToConflict = 0.0;  // to the start of the conflict segment
    size_t conflictSeg = 0;
};

inline double RoadSegmentLength(const sr3zone::RoadGraph& g, size_t seg) {
    const auto& s = g.segments[seg];
    if (s.chord256 != 0) return s.chord256 / 256.0;
    const auto& a = g.SegA(seg);
    const auto& b = g.SegB(seg);
    return std::hypot(static_cast<double>(b.x) - a.x, static_cast<double>(b.z) - a.z);
}

inline YieldDecision EvaluateJunctionYield(const sr3zone::RoadGraph& g, const YieldOwn& own,
                                           const std::function<std::vector<YieldOther>(size_t)>& carsOn) {
    YieldDecision out;
    const double reach = YieldLookahead(own.speed, own.aB, own.recentlyBraked);
    size_t seg = own.seg;
    double dist = own.distToSegEnd;
    for (int hop = 0; hop < 6 && dist <= reach; ++hop) {
        const std::vector<uint16_t> succ = g.Successors(seg);
        if (succ.empty()) break;
        size_t s = succ[0];
        if (succ.size() > 1) {
            double best = -2.0;
            for (uint16_t c : succ) {
                const double dx = static_cast<double>(g.SegB(c).x) - g.SegA(c).x;
                const double dz = static_cast<double>(g.SegB(c).z) - g.SegA(c).z;
                const double l = std::hypot(dx, dz);
                const double dot = l > 1e-9 ? (dx * own.headingX + dz * own.headingZ) / l : -1.0;
                if (dot > best) {
                    best = dot;
                    s = c;
                }
            }
        }
        const std::vector<uint16_t> conflicts = g.Conflicts(s);
        if (!conflicts.empty()) {
            out.found = true;
            out.distToConflict = dist;
            out.conflictSeg = s;
            if (!YieldCouldStop(own.speed, own.aB, dist)) return out;  // too late to stop: drive through
            const YieldWindow mine = PredictOccupancyWindow(dist, RoadSegmentLength(g, s), own.speed, own.targetSpeed);
            auto conflictsWith = [&](const YieldOther& o, double distToStart, double len) {
                if (o.id == own.id) return false;
                const YieldWindow w = PredictOccupancyWindow(distToStart, len, o.speed, own.targetSpeed);
                if (!YieldWindowsConflict(mine, w)) return false;
                return w.enter < mine.enter || (w.enter == mine.enter && o.id < own.id);
            };
            for (uint16_t c : conflicts) {
                const double lenC = RoadSegmentLength(g, c);
                for (const YieldOther& o : carsOn(c))
                    if (conflictsWith(o, o.distToSegEnd - lenC, lenC)) {
                        out.yield = true;
                        return out;
                    }
                size_t p = c;
                double acc = 0.0;
                for (int h = 0; h < 3; ++h) {
                    const std::vector<uint16_t> pr = g.Predecessors(p);
                    if (pr.size() != 1) break;
                    p = pr[0];
                    for (const YieldOther& o : carsOn(p))
                        if (conflictsWith(o, o.distToSegEnd + acc, lenC)) {
                            out.yield = true;
                            return out;
                        }
                    acc += RoadSegmentLength(g, p);
                }
            }
            return out;
        }
        dist += RoadSegmentLength(g, s);
        seg = s;
    }
    return out;
}

}  // namespace sr3airuntime
