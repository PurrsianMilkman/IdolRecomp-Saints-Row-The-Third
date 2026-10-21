#pragma once
// sr3airuntime/gps_route.h - GPS waypoints, route planning and replanning, spec-ai-runtime.md Sec23
// (FINAL, 2026-10-19), over the road graph of spec-zone-data-format.md Sec13.2a (sr3zone::RoadGraph).
// Engine side only: the route line is DRAWN by the HUD (spec-hud-runtime.md H.6); this header only exposes the
// route as a lane list + polyline (GpsPlanner::route(), RemainingPolyline()).
//
// Ported (Sec numbers are spec-ai-runtime.md): the per-player record kinds/saved mission goal/timers (23.1), retarget
// / clear / pin-versus-mission rules (23.2), the traffic A* in GPS mode with its costs and lane filter (23.4),
// monitoring, arrival, off-route replanning (23.5), aircraft direct mode (23.6).
//
// CHOSEN (each also listed in the task report as a question to Team A):
//  C1. The planner state machine's states 1-10 are collapsed into ONE synchronous plan() call (the spec's
//      1000 us per-frame budget exists to spread the work over frames; observable behaviour is the same route).
//  C2. Candidate lanes (ring-radius law 0x007D1090 is OPEN): the <= kMaxCandidates nearest filter-passing lanes
//      within a radius that widens 40 -> 80 -> 160 -> 250 m (the spec's 250 m give-up); the trimming rules of
//      state 9 are not ported; route cost = A* g.
//  C3. Island ids: the district query (hood_island_type) is supplied by the caller (IslandFn); the default puts
//      every non-water lane on island 1 and water lanes (bit 18) on island 0. Connected sets are flooded through
//      successor + predecessor links (spec: "connected lane set").
//  C4. Shortcut lanes (flag bit 15) are all-locked until setShortcutUnlocked() (spec 23.10 step 1).
//  C5. No route found after the retries: the plan commits in direct mode (straight line to the goal, planFailed
//      set) so the player still has a target; the 3 s direct-mode retry then re-tries the road plan.
//  C6. The "seated in a vehicle that is neither a Watercraft nor 0x00AD3C50" exception of the far-off branch is
//      not ported (identity OPEN): a car far off route falls through to the 3 s / 25 m branch.
//  C7. The goal is moved onto a lane within 10 m only as bookkeeping (goalOffRoad = no lane within 10 m); the goal
//      position itself is kept (spec 23.10 step 3 allows this).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <queue>
#include <string>
#include <vector>

#include "sr3airuntime/common.h"
#include "sr3zone/road_graph.h"

namespace sr3airuntime::gps {

// ---- constants (Sec23.3-23.5) ----
constexpr uint32_t kIncludeMask = 0x08062D00u;   // bits 8,10,11,13,17,18,27
constexpr uint32_t kExcludeMask = 0x01000000u;   // bit 24
constexpr uint32_t kShortcutBit = 1u << 15;      // "GPS shortcut" class
constexpr uint32_t kUnlockBit = 1u << 27;        // run-time unlock bit
constexpr uint32_t kBit24 = 1u << 24;
constexpr uint32_t kZeroChordBit = 1u << 31;     // chord counts as 0
constexpr uint32_t kHighwayBit = sr3zone::kSegFlagHighway;
constexpr uint32_t kBoatBit = sr3zone::kSegFlagBoat;
constexpr double kHeuristicScaleGps = 0.5;       // 0x0126D2CC
constexpr double kHighwayFactorGps = 0.5;        // 0x00B1C8DF
constexpr double kLaneChangeCost = 6.0;          // 0x012A2F7C
constexpr double kLaneChangeCostHighway = 3.0;   // 0x01116444
constexpr double kPinArrivalSq = 15.0;           // 0x01117A60, squared metres
constexpr double kOffRouteSq = 625.0;            // 25 m
constexpr double kFarOffRouteSq = 10000.0;       // 100 m
constexpr double kGoalMovedSq = 225.0;           // 15 m, 0x01115320
constexpr double kSameGoalSq = 1.0;              // retarget dedupe
constexpr int64_t kReplanCheckMs = 3000;         // +0xB1C
constexpr int64_t kFarOffRouteMs = 2500;         // +0xB20
constexpr double kGoalSnapM = 10.0;              // 0x012FDFC0
constexpr size_t kMaxCandidates = 8;             // CHOSEN C2
constexpr int kMaxWiden = 4;

enum class Kind : uint8_t { Pin = 0, Mission = 1 };
enum class Mover : uint8_t { OnFoot, Ground, Boat, Airplane, Helicopter };

// Sec23.6: the mover kind from what the player is in. Vehicle_Type class: 0 Automobile, 1 Motorcycle, 2 Airplane,
// 3 Helicopter, 4 VTOL, 5 Watercraft. Car or bike -> road graph; boat -> road graph (start from the vehicle); airplane,
// helicopter and VTOL -> direct mode (VTOL is mapped to Helicopter: both are aircraft, only "airborne" matters here).
// Not seated -> on foot. An unknown class is treated as a ground vehicle (CHOSEN).
constexpr Mover MoverForVehicleClass(bool seated, int vehicleClass) {
    if (!seated) return Mover::OnFoot;
    switch (vehicleClass) {
        case 2: return Mover::Airplane;
        case 3:
        case 4: return Mover::Helicopter;
        case 5: return Mover::Boat;
        default: return Mover::Ground;
    }
}

// Sec23.2 step 1: the retarget primitive goes to direct mode if the player's CURRENT OR LAST vehicle is an airplane,
// helicopter or VTOL (class 2/3/4), so an on-foot player who left an aircraft still plans in direct mode. Seated: the
// current vehicle's class (Sec23.6). Not seated: the last vehicle's class decides only for the aircraft classes; any
// other last vehicle (or none, lastClass < 0) is on foot.
constexpr Mover MoverForPlayer(bool seated, int currentClass, int lastClass) {
    if (seated) return MoverForVehicleClass(true, currentClass);
    if (lastClass >= 2 && lastClass <= 4) return MoverForVehicleClass(true, lastClass);
    return Mover::OnFoot;
}

using Vec3 = sr3airuntime::Vec3;

// ---------------------------------------------------------------------------------------------------------
// Graph wrapper: island ids + shortcut unlock bits (Sec23.4 / 23.10 step 1).
// ---------------------------------------------------------------------------------------------------------
class GpsGraph {
public:
    using IslandFn = std::function<int(const sr3zone::RoadVec3&)>;
    explicit GpsGraph(const sr3zone::RoadGraph& g, IslandFn islandOfPoint = nullptr) : g_(&g) {
        const size_t n = g.segments.size();
        island_.assign(n, -1);
        unlocked_.assign(n, 0);
        // flood connected lane sets (successor + predecessor links) with the island of the first lane's node A
        std::vector<std::vector<uint32_t>> adj(n);
        for (size_t s = 0; s < n; ++s)
            for (uint16_t t : g.Successors(s))
                if (t < n) {
                    adj[s].push_back(t);
                    adj[t].push_back(static_cast<uint32_t>(s));
                }
        for (size_t s = 0; s < n; ++s) {
            if (island_[s] != -1) continue;
            if (g.segments[s].flags & kBoatBit) { island_[s] = 0; continue; }  // water lanes: island 0
            const int id = islandOfPoint ? islandOfPoint(g.SegA(s)) : 1;
            std::vector<uint32_t> stack{static_cast<uint32_t>(s)};
            island_[s] = id;
            while (!stack.empty()) {
                const uint32_t c = stack.back();
                stack.pop_back();
                for (uint32_t t : adj[c])
                    if (island_[t] == -1 && !(g.segments[t].flags & kBoatBit)) {
                        island_[t] = id;
                        stack.push_back(t);
                    }
            }
        }
        for (size_t s = 0; s < n; ++s)
            if (island_[s] == -1) island_[s] = 0;
    }
    const sr3zone::RoadGraph& graph() const { return *g_; }
    int island(size_t seg) const { return island_[seg]; }
    // k-th bit-15 lane in file order unlocks with bit k of the save array; here a direct lane-level setter.
    void setShortcutUnlocked(size_t seg, bool v) { unlocked_[seg] = v ? 1 : 0; }
    bool shortcutUnlocked(size_t seg) const { return unlocked_[seg] != 0; }
    // Run-time flags: stored flags with bit 27 set for unlocked shortcuts.
    uint32_t flags(size_t seg) const { return g_->segments[seg].flags | (unlocked_[seg] ? kUnlockBit : 0u); }

private:
    const sr3zone::RoadGraph* g_;
    std::vector<int> island_;
    std::vector<uint8_t> unlocked_;
};

// ---- A* pieces (exposed for the Sec23.8 vectors) ----
inline double Dist3(const sr3zone::RoadVec3& a, const Vec3& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
// h = straight-line 3D distance from the lane's end node B to the goal, x0.5 in GPS mode.
inline double Heuristic(const GpsGraph& G, size_t lane, const Vec3& goal, bool gps) {
    return Dist3(G.graph().SegB(lane), goal) * (gps ? kHeuristicScaleGps : 1.0);
}
// Lane filter (every expanded lane): shares a bit with the include mask, none with the exclude mask unless
// allowBit24; in GPS mode a bit-15 lane also needs bit 27.
inline bool LanePasses(const GpsGraph& G, size_t lane, bool gps, bool allowBit24) {
    const uint32_t f = G.flags(lane);
    if (!(f & kIncludeMask)) return false;
    if (!allowBit24 && (f & kExcludeMask)) return false;
    if (gps && (f & kShortcutBit) && !(f & kUnlockBit)) return false;
    return true;
}
inline double Chord(const GpsGraph& G, size_t lane) {
    const auto& s = G.graph().segments[lane];
    return (s.flags & kZeroChordBit) ? 0.0 : s.chord256 / 256.0;
}
struct Succ {
    uint32_t lane = 0;
    double stepCost = 0.0;
    bool laneChange = false;
};
// Successors of `lane` with the cost added to g. `viaLaneChange` = the lane was itself reached by a lane change.
inline std::vector<Succ> Expand(const GpsGraph& G, size_t lane, bool viaLaneChange, bool gps) {
    std::vector<Succ> out;
    const auto& g = G.graph();
    const size_t n = g.segments.size();
    for (uint16_t t : g.Successors(lane)) {
        if (t >= n) continue;
        double c = Chord(G, t);
        if (gps && (G.flags(t) & kHighwayBit)) c *= kHighwayFactorGps;
        out.push_back({t, c, false});
    }
    if (!viaLaneChange) {
        const auto& s = g.segments[lane];
        const bool oncoming = (s.flags & sr3zone::kSegFlagOncomingLeft) != 0;
        const uint16_t nb[2] = {s.left, s.right};
        for (int k = 0; k < 2; ++k) {
            if (nb[k] == 0xffff || nb[k] >= n) continue;
            if (k == 0 && oncoming && !gps) continue;  // oncoming left neighbour: GPS mode only
            const double c = (G.flags(nb[k]) & kHighwayBit) ? kLaneChangeCostHighway : kLaneChangeCost;
            out.push_back({nb[k], c, true});
        }
    }
    return out;
}

struct SearchResult {
    bool found = false;
    std::vector<uint32_t> lanes;  // start lane .. goal lane
    double cost = std::numeric_limits<double>::infinity();
    uint32_t expanded = 0;
};

// One A* from `start` to any lane of `goals`. costBound prunes (+0x1C: best cost so far).
inline SearchResult Search(const GpsGraph& G, uint32_t start, const std::vector<uint32_t>& goals, const Vec3& goalPos,
                           bool gps, int goalIsland, double costBound = std::numeric_limits<double>::infinity()) {
    SearchResult res;
    const size_t n = G.graph().segments.size();
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> gs(n, inf);
    std::vector<int32_t> parent(n, -1);
    std::vector<uint8_t> viaChange(n, 0), closed(n, 0), isGoal(n, 0);
    bool allow24 = (G.flags(start) & kBit24) != 0;
    for (uint32_t gl : goals) {
        isGoal[gl] = 1;
        if (G.flags(gl) & kBit24) allow24 = true;
    }
    using QE = std::pair<double, uint32_t>;  // f, lane
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    gs[start] = 0.0;
    open.push({Heuristic(G, start, goalPos, gps), start});
    while (!open.empty()) {
        const auto [f, cur] = open.top();
        open.pop();
        if (closed[cur]) continue;  // stale record (decrease-key by re-push)
        if (f >= costBound) break;
        closed[cur] = 1;
        ++res.expanded;
        if (isGoal[cur]) {
            res.found = true;
            res.cost = gs[cur];
            for (int32_t c = static_cast<int32_t>(cur); c != -1; c = parent[c]) res.lanes.push_back(static_cast<uint32_t>(c));
            std::reverse(res.lanes.begin(), res.lanes.end());
            return res;
        }
        for (const Succ& s : Expand(G, cur, viaChange[cur] != 0, gps)) {
            if (closed[s.lane]) continue;
            if (!LanePasses(G, s.lane, gps, allow24)) continue;
            // island rule: do not step onto another island's lane unless it is the goal's
            const int ci = G.island(cur), ti = G.island(s.lane);
            if (ci != 0 && ti != ci && ti != goalIsland) continue;
            const double ng = gs[cur] + s.stepCost;
            if (ng < gs[s.lane]) {
                gs[s.lane] = ng;
                parent[s.lane] = static_cast<int32_t>(cur);
                viaChange[s.lane] = s.laneChange ? 1 : 0;
                open.push({ng + Heuristic(G, s.lane, goalPos, gps), s.lane});
            }
        }
    }
    return res;
}

// ---------------------------------------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------------------------------------
inline double PointSegDistSq(const Vec3& p, const sr3zone::RoadVec3& a, const sr3zone::RoadVec3& b) {
    const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    const double apx = p.x - a.x, apy = p.y - a.y, apz = p.z - a.z;
    const double len2 = abx * abx + aby * aby + abz * abz;
    double t = len2 > 0.0 ? (apx * abx + apy * aby + apz * abz) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double dx = apx - t * abx, dy = apy - t * aby, dz = apz - t * abz;
    return dx * dx + dy * dy + dz * dz;
}
inline double PolylineLength(const std::vector<sr3zone::RoadVec3>& pl) {
    double len = 0;
    for (size_t i = 1; i < pl.size(); ++i) {
        const double dx = pl[i].x - pl[i - 1].x, dy = pl[i].y - pl[i - 1].y, dz = pl[i].z - pl[i - 1].z;
        len += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return len;
}

// ---------------------------------------------------------------------------------------------------------
// Route view: what the HUD (H.6) reads.
// ---------------------------------------------------------------------------------------------------------
struct GpsRoute {
    bool active = false;             // Sec23.1 state 11: a route list or direct mode exists (waypoint_is_placed)
    bool direct = false;             // +0xA91: straight line to the goal
    bool goalOffRoad = false;        // +0xA90: the HUD adds a straight last leg (already appended to `polyline`)
    bool planFailed = false;         // CHOSEN C5
    Kind kind = Kind::Pin;           // +0xA92
    Vec3 goal;                       // +0xA84
    std::vector<uint32_t> lanes;     // committed lane (segment) list in travel order
    std::vector<sr3zone::RoadVec3> polyline;  // whole route, start..goal (goal leg included when off-road)
    std::vector<uint32_t> laneFirstPoint;     // index into `polyline` where each lane starts
    size_t currentLane = 0;          // +0xA74: index into `lanes` of the route node nearest the player
    double lengthMeters = 0.0;       // whole polyline length
    double cost = 0.0;               // A* g of the winner
};

struct PlayerSample {
    bool present = true;
    Vec3 charPos;                    // +0x40 (the character, not the vehicle)
    Vec3 startPos;                   // vehicle position if seated, else the character
    Mover mover = Mover::OnFoot;
};

enum class Event : uint8_t { None, Arrived, Replanned, Cleared, GoalGone, DirectRetry };

struct Stats {
    uint32_t plans = 0, replans = 0, arrivals = 0, clears = 0, failedPlans = 0, directCommits = 0, ignoredDup = 0;
    uint32_t lastExpanded = 0;
    double lastLength = 0.0, lastCost = 0.0;
};

// ---------------------------------------------------------------------------------------------------------
// The per-player GPS record + planner (Sec23.1-23.5).
// ---------------------------------------------------------------------------------------------------------
class GpsPlanner {
public:
    using TraceFn = std::function<void(const std::string&)>;
    using TrackFn = std::function<bool(Vec3&)>;  // current position of the tracked object; false = gone

    explicit GpsPlanner(const GpsGraph& g) : G_(&g) {}
    void setTrace(TraceFn f) { trace_ = std::move(f); }

    // waypoint_is_placed (slot 0): a route list or direct mode exists, either kind.
    bool isPlaced() const { return route_.active; }
    const GpsRoute& route() const { return route_; }
    const Stats& stats() const { return stats_; }
    bool hasSavedMission() const { return savedValid_; }

    // The remaining route from the current node to the goal (what the draw consumes), H.6.
    std::vector<sr3zone::RoadVec3> RemainingPolyline() const {
        if (!route_.active) return {};
        if (route_.lanes.empty() || route_.currentLane >= route_.laneFirstPoint.size()) return route_.polyline;
        return std::vector<sr3zone::RoadVec3>(route_.polyline.begin() + route_.laneFirstPoint[route_.currentLane],
                                              route_.polyline.end());
    }

    // ---- Sec23.2 retarget primitive 0x007D6970 ----
    void Retarget(const PlayerSample& p, const Vec3& goal, Kind kind, bool replan, int64_t nowMs) {
        lastPlayer_ = p;
        if (p.mover == Mover::Airplane || p.mover == Mover::Helicopter) {  // direct mode, no road search
            Free();
            route_.direct = true;
            CommitCommon(goal, kind, nowMs, /*direct=*/true);
            ++stats_.directCommits;
            Trace("direct", goal, kind);
            return;
        }
        if (!replan) {
            const double dx = goal.x - goalReq_.x, dz = goal.z - goalReq_.z;
            if (kind == requestedKind_ && route_.active && dx * dx + dz * dz < kSameGoalSq) {
                ++stats_.ignoredDup;
                return;
            }
        }
        requestedKind_ = kind;
        goalReq_ = goal;
        ++(replan ? stats_.replans : stats_.plans);
        Plan(p, goal, kind, nowMs, replan);
    }

    void SetPin(const PlayerSample& p, const Vec3& goal, int64_t nowMs) {
        Retarget(p, goal, Kind::Pin, false, nowMs);
    }
    void SetMission(const PlayerSample& p, const Vec3& goal, int64_t nowMs, TrackFn tracked = nullptr) {
        trackedMission_ = std::move(tracked);
        Retarget(p, goal, Kind::Mission, false, nowMs);
    }
    // Pause-map "set GPS" button: toggles an own pin off; otherwise sets (replacing a mission route).
    void TogglePin(const PlayerSample& p, const Vec3& goal, int64_t nowMs) {
        if (route_.active && route_.kind != Kind::Mission) Clear(Kind::Pin, nowMs);
        else SetPin(p, goal, nowMs);
    }

    // ---- Sec23.2 clear 0x007D6DC0: only when the active kind matches ----
    void Clear(Kind kind, int64_t nowMs) {
        if (kind == Kind::Mission) {  // forgets the saved mission waypoint + tracked handle regardless
            savedValid_ = false;
            trackedMission_ = nullptr;
        }
        if (route_.active && route_.kind == kind) {
            Free();
            route_ = GpsRoute{};
            ++stats_.clears;
            Trace("clear", route_.goal, kind);
        }
        if (kind == Kind::Pin && savedValid_ && !route_.active) {  // restore the saved mission waypoint
            Retarget(lastPlayer_, saved_, Kind::Mission, false, nowMs);
        }
    }

    // ---- Sec23.5 monitor (state 11), once per frame ----
    Event Update(const PlayerSample& p, int64_t nowMs) {
        lastPlayer_ = p;
        if (!route_.active) return Event::None;
        if (!p.present) { Clear(requestedKind_, nowMs); return Event::Cleared; }
        Vec3 G = route_.goal;
        const bool tracking = route_.kind == Kind::Mission && static_cast<bool>(trackedMission_);
        if (tracking) {
            if (!trackedMission_(G)) { Clear(requestedKind_, nowMs); return Event::GoalGone; }
        }
        // arrival: player pin only (kind 0), XZ squared < 15
        const double dx = p.startPos.x - G.x, dz = p.startPos.z - G.z;
        if (route_.kind == Kind::Pin && dx * dx + dz * dz < kPinArrivalSq) {
            ++stats_.arrivals;
            Trace("arrive", G, Kind::Pin);
            Clear(Kind::Pin, nowMs);
            return Event::Arrived;
        }
        // progress: nearest route node, D^2 = min squared 3D distance to the route
        double d2 = std::numeric_limits<double>::max();  // direct mode: FLT_MAX-like
        if (!route_.direct) d2 = Progress(p.charPos);
        bool replan = false;
        if (!route_.direct && d2 > kFarOffRouteSq && nowMs >= farTimerAt_) {
            farTimerAt_ = nowMs + kFarOffRouteMs;
            replan = true;
        } else if (nowMs >= checkTimerAt_) {
            if (route_.direct) {
                const bool airborne = p.mover == Mover::Airplane || p.mover == Mover::Helicopter;
                if (!airborne || route_.planFailed) replan = true;  // direct retry after landing
            } else if (d2 > kOffRouteSq) {
                replan = true;
            } else if (tracking) {
                const double gx = G.x - route_.goal.x, gy = G.y - route_.goal.y, gz = G.z - route_.goal.z;
                if (gx * gx + gy * gy + gz * gz > kGoalMovedSq) replan = true;
            }
        }
        if (!replan) return Event::None;
        const bool wasDirect = route_.direct;
        Retarget(p, G, requestedKind_, /*replan=*/true, nowMs);
        return wasDirect ? Event::DirectRetry : Event::Replanned;
    }

private:
    void Trace(const char* what, const Vec3& goal, Kind kind) {
        if (!trace_) return;
        char b[256];
        std::snprintf(b, sizeof b, "%s kind=%d goal=(%.1f,%.1f) lanes=%zu len=%.1f cost=%.1f direct=%d offroad=%d failed=%d", what,
                      static_cast<int>(kind), goal.x, goal.z, route_.lanes.size(), route_.lengthMeters, route_.cost,
                      route_.direct ? 1 : 0, route_.goalOffRoad ? 1 : 0, route_.planFailed ? 1 : 0);
        trace_(b);
    }
    void Free() {
        route_.lanes.clear();
        route_.polyline.clear();
        route_.laneFirstPoint.clear();
        route_.direct = false;
        route_.planFailed = false;
        route_.goalOffRoad = false;
    }
    void CommitCommon(const Vec3& goal, Kind kind, int64_t nowMs, bool direct) {
        route_.active = true;
        route_.goal = goal;
        route_.kind = kind;
        route_.direct = direct;
        requestedKind_ = kind;
        goalReq_ = goal;
        checkTimerAt_ = nowMs + kReplanCheckMs;
        farTimerAt_ = nowMs + kFarOffRouteMs;
        route_.currentLane = 0;
        if (kind == Kind::Mission) { saved_ = goal; savedValid_ = true; }  // a kind-1 commit saves its goal
        if (direct) {
            route_.polyline = {{static_cast<float>(lastPlayer_.startPos.x), static_cast<float>(lastPlayer_.startPos.y),
                                static_cast<float>(lastPlayer_.startPos.z)},
                               {static_cast<float>(goal.x), static_cast<float>(goal.y), static_cast<float>(goal.z)}};
            route_.lengthMeters = PolylineLength(route_.polyline);
        }
    }

    // Nearest filter-passing lanes to `pos` (C2): sorted ascending by exact polyline distance.
    std::vector<std::pair<double, uint32_t>> Candidates(const Vec3& pos, double radius, bool gps) const {
        const auto& g = G_->graph();
        std::vector<std::pair<double, uint32_t>> v;
        for (uint32_t s : g.SegmentsNear(pos.x, pos.z, radius)) {
            if (!LanePasses(*G_, s, gps, /*allowBit24=*/true)) continue;
            const auto pl = g.SegmentPolyline(s);
            double best = std::numeric_limits<double>::max();
            for (size_t i = 1; i < pl.size(); ++i) best = std::min(best, PointSegDistSq(pos, pl[i - 1], pl[i]));
            if (pl.size() == 1) best = PointSegDistSq(pos, pl[0], pl[0]);
            if (best <= radius * radius) v.push_back({std::sqrt(best), s});
        }
        std::sort(v.begin(), v.end());
        if (v.size() > kMaxCandidates) v.resize(kMaxCandidates);
        return v;
    }

    void Plan(const PlayerSample& p, const Vec3& goal, Kind kind, int64_t nowMs, bool /*replan*/) {
        Free();
        const bool gps = true;
        SearchResult best;
        double bestTotal = std::numeric_limits<double>::infinity();
        double goalNearest = -1.0;
        static const double kRadii[kMaxWiden] = {40.0, 80.0, 160.0, 250.0};
        for (int r = 0; r < kMaxWiden && !best.found; ++r) {
            const auto starts = Candidates(p.startPos, kRadii[r], gps);
            const auto goalC = Candidates(goal, kRadii[r], gps);
            if (starts.empty() || goalC.empty()) continue;
            goalNearest = goalC[0].first;
            std::vector<uint32_t> goals;
            for (const auto& gc : goalC) goals.push_back(gc.second);
            const int goalIsland = G_->island(goals[0]);
            for (const auto& sc : starts) {  // route cost = start-to-lane distance + A* g
                SearchResult sr = Search(*G_, sc.second, goals, goal, gps, goalIsland, bestTotal - sc.first);
                stats_.lastExpanded = sr.expanded;
                if (sr.found && sr.cost + sc.first < bestTotal) {
                    bestTotal = sr.cost + sc.first;
                    best = std::move(sr);
                }
            }
        }
        if (!best.found) {  // C5: commit direct, flagged
            ++stats_.failedPlans;
            Free();
            CommitCommon(goal, kind, nowMs, /*direct=*/true);
            route_.planFailed = true;
            Trace("plan-failed", goal, kind);
            return;
        }
        const auto& g = G_->graph();
        route_.lanes = best.lanes;
        route_.cost = best.cost;
        route_.polyline.clear();
        route_.laneFirstPoint.clear();
        for (uint32_t l : route_.lanes) {
            auto pl = g.SegmentPolyline(l);
            size_t from = 0;
            if (!route_.polyline.empty()) {
                const auto& last = route_.polyline.back();
                if (std::fabs(last.x - pl[0].x) < 0.01f && std::fabs(last.y - pl[0].y) < 0.01f &&
                    std::fabs(last.z - pl[0].z) < 0.01f) from = 1;
            }
            // a lane starts at its node A, which is the previous lane's last point when they join
            route_.laneFirstPoint.push_back(static_cast<uint32_t>(route_.polyline.size() - from));
            route_.polyline.insert(route_.polyline.end(), pl.begin() + from, pl.end());
        }
        CommitCommon(goal, kind, nowMs, /*direct=*/false);
        route_.goalOffRoad = goalNearest < 0 || goalNearest > kGoalSnapM;
        if (route_.goalOffRoad)
            route_.polyline.push_back({static_cast<float>(goal.x), static_cast<float>(goal.y), static_cast<float>(goal.z)});
        route_.lengthMeters = PolylineLength(route_.polyline);
        stats_.lastLength = route_.lengthMeters;
        stats_.lastCost = route_.cost;
        Trace("plan", goal, kind);
    }

    // Sec23.5 step 4: min squared 3D distance to the route; sets the current node.
    double Progress(const Vec3& pos) {
        double best = std::numeric_limits<double>::max();
        size_t bi = route_.currentLane;
        const auto& pl = route_.polyline;
        for (size_t li = 0; li < route_.lanes.size(); ++li) {
            const size_t a = route_.laneFirstPoint[li];
            const size_t b = li + 1 < route_.lanes.size() ? route_.laneFirstPoint[li + 1] + 1 : pl.size();
            for (size_t i = a + 1; i < std::min(b, pl.size()); ++i) {
                const double d = PointSegDistSq(pos, pl[i - 1], pl[i]);
                if (d < best) { best = d; bi = li; }
            }
            if (b - a == 1 && pl.size() > a) {  // single-point lane
                const double d = PointSegDistSq(pos, pl[a], pl[a]);
                if (d < best) { best = d; bi = li; }
            }
        }
        route_.currentLane = bi;
        return best;
    }

    const GpsGraph* G_;
    GpsRoute route_;
    Stats stats_;
    TraceFn trace_;
    TrackFn trackedMission_;  // +0xB10: goal follows this object (mission kind only)
    PlayerSample lastPlayer_;
    Kind requestedKind_ = Kind::Pin;  // +0xA5E
    Vec3 goalReq_;                    // +0x10
    Vec3 saved_;                      // +0xAF0
    bool savedValid_ = false;         // +0xAEE
    int64_t checkTimerAt_ = 0;        // +0xB1C expiry
    int64_t farTimerAt_ = 0;          // +0xB20 expiry
};

}  // namespace sr3airuntime::gps
