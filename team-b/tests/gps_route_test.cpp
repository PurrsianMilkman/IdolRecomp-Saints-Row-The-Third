// GPS route planner (include/sr3airuntime/gps_route.h) against spec-ai-runtime.md Sec23.8 vectors TV1-TV11.
// Synthetic graphs only.
#include <cmath>
#include <iostream>

#include "sr3airuntime/gps_route.h"

using namespace sr3airuntime;
using sr3zone::RoadGraph;

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

// Adds a lane A->B (chord set from the geometry); returns its index.
uint16_t AddLane(RoadGraph& g, sr3zone::RoadVec3 a, sr3zone::RoadVec3 b, uint32_t flags) {
    const uint16_t na = static_cast<uint16_t>(g.nodes.size());
    g.nodes.push_back(a);
    g.nodes.push_back(b);
    sr3zone::RoadSegment s;
    s.nodeA = na;
    s.nodeB = na + 1;
    s.flags = flags;
    const double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    s.chord256 = static_cast<uint16_t>(std::sqrt(dx * dx + dy * dy + dz * dz) * 256.0);
    g.segments.push_back(s);
    return static_cast<uint16_t>(g.segments.size() - 1);
}
constexpr uint32_t kRoad = 1u << 10;
constexpr uint32_t kHwy = 1u << 4;

// TV1-TV3 graph: S=0, A=1 (highway), B=2, L=3 (left neighbour of S).
struct Fork {
    RoadGraph g;
    Fork() {
        AddLane(g, {-40, 0, 0}, {0, 0, 0}, kRoad);             // 0 S
        AddLane(g, {0, 0, 0}, {100, 0, 0}, kRoad | kHwy);      // 1 A (end 100 m from the goal at the origin)
        AddLane(g, {0, 0, 0}, {0, 0, 90}, kRoad);              // 2 B (end 90 m)
        AddLane(g, {-40, 0, 5}, {10, 0, 0}, kRoad);            // 3 L (end 10 m)
        g.segments[1].chord256 = 40 * 256;
        g.segments[2].chord256 = 40 * 256;
        g.succPool = {2, 1, 2};
        g.segments[0].succRef = 4;  // n = 4 -> pool index 0
        g.segments[0].left = 3;
        g.RebuildIndices();
    }
};

// Straight road along +x from x=-100 to 1000, 100 m lanes.
struct Chain {
    RoadGraph g;
    Chain() {
        for (int i = -1; i < 10; ++i) AddLane(g, {float(i * 100), 0, 0}, {float(i * 100 + 100), 0, 0}, kRoad);
        for (size_t i = 0; i + 1 < g.segments.size(); ++i) g.segments[i].succRef = static_cast<uint16_t>(i + 1);
        g.RebuildIndices();
    }
};
gps::PlayerSample At(double x, double z, gps::Mover m = gps::Mover::Ground) {
    gps::PlayerSample p;
    p.charPos = p.startPos = {x, 0, z};
    p.mover = m;
    return p;
}
}  // namespace

int main() {
    const Vec3 origin{0, 0, 0};
    // ---- TV1 / TV2 / TV3 + lane filter ----
    {
        Fork f;
        gps::GpsGraph G(f.g);
        auto s = gps::Expand(G, 0, false, true);
        const gps::Succ *A = nullptr, *B = nullptr;
        for (const auto& x : s) { if (x.lane == 1) A = &x; if (x.lane == 2) B = &x; }
        CHECK(A && B);
        if (A && B) {
            CHECK(Near(A->stepCost, 20.0));
            CHECK(Near(B->stepCost, 40.0));
            CHECK(Near(A->stepCost + gps::Heuristic(G, 1, origin, true), 70.0));   // TV1
            CHECK(Near(B->stepCost + gps::Heuristic(G, 2, origin, true), 85.0));
        }
        // TV2: S oncoming-left flag -> L expanded in GPS mode only
        f.g.segments[0].flags |= 1u;
        s = gps::Expand(G, 0, false, true);
        const gps::Succ* L = nullptr;
        for (const auto& x : s) if (x.lane == 3) L = &x;
        CHECK(L != nullptr);
        if (L) {
            CHECK(Near(L->stepCost, 6.0));
            CHECK(Near(L->stepCost + gps::Heuristic(G, 3, origin, true), 11.0));
        }
        bool hasL = false;
        for (const auto& x : gps::Expand(G, 0, false, false)) hasL |= x.lane == 3;
        CHECK(!hasL);
        // TV3: reached by a lane change -> successors only
        s = gps::Expand(G, 0, true, true);
        CHECK(s.size() == 2);
        for (const auto& x : s) CHECK(!x.laneChange);
        // highway lane-change target costs 3.0
        f.g.segments[3].flags |= kHwy;
        for (const auto& x : gps::Expand(G, 0, false, true)) if (x.lane == 3) CHECK(Near(x.stepCost, 3.0));
        // lane filter: shortcut locked until bit 27 is set; bit 24 avoided unless allowed; offroad unusable
        f.g.segments[1].flags |= (1u << 15);
        CHECK(!gps::LanePasses(G, 1, true, false));
        G.setShortcutUnlocked(1, true);
        CHECK(gps::LanePasses(G, 1, true, false));
        f.g.segments[2].flags |= (1u << 24);
        CHECK(!gps::LanePasses(G, 2, true, false));
        CHECK(gps::LanePasses(G, 2, true, true));
        f.g.segments[2].flags = (1u << 16);
        CHECK(!gps::LanePasses(G, 2, true, true));
        // search: A* picks the highway branch toward a goal near A's end
        f.g.segments[2].flags = kRoad;
        f.g.segments[1].flags = kRoad | kHwy;
        auto r = gps::Search(G, 0, {1}, Vec3{100, 0, 0}, true, 1);
        CHECK(r.found && r.lanes.size() == 2 && r.lanes[1] == 1 && Near(r.cost, 20.0));
    }
    // ---- planning + monitor (TV4-TV11) ----
    {
        Chain c;
        gps::GpsGraph G(c.g);
        // plan
        gps::GpsPlanner pl(G);
        pl.SetPin(At(10, 5), Vec3{900, 0, 5}, 0);
        CHECK(pl.isPlaced());
        CHECK(!pl.route().direct);
        CHECK(pl.route().lanes.size() >= 9);
        CHECK(pl.route().lengthMeters > 990.0 && pl.route().lengthMeters < 1010.0);  // whole lanes 0..1000 + 5 m off-road leg
        CHECK(pl.route().currentLane == 0);
        // progress consumes the route as the player advances
        pl.Update(At(450, 0), 500);
        CHECK(pl.route().currentLane >= 4);
        CHECK(pl.RemainingPolyline().size() < pl.route().polyline.size());
    }
    {   // TV4 / TV5: arrival radius sqrt(15)
        Chain c; gps::GpsGraph G(c.g);
        gps::GpsPlanner pl(G);
        pl.SetPin(At(100, 0), origin, 0);
        CHECK(pl.isPlaced());
        CHECK(pl.Update(At(3.9, 0), 100) == gps::Event::None);   // 15.21 >= 15 kept
        CHECK(pl.isPlaced());
        CHECK(pl.Update(At(3.5, 0), 200) == gps::Event::Arrived);  // 12.25 < 15
        CHECK(!pl.isPlaced());
        // mission waypoint (kind 1) at (0.5, 0): player on it, kept
        gps::GpsPlanner pm(G);
        pm.SetMission(At(100, 0), Vec3{0.5, 0, 0}, 0);
        CHECK(pm.Update(At(0.6, 0), 100) == gps::Event::None);
        CHECK(pm.isPlaced());
    }
    {   // TV6: 25 m rule
        Chain c; gps::GpsGraph G(c.g);
        gps::GpsPlanner pl(G);
        pl.SetPin(At(50, 0), Vec3{900, 0, 0}, 0);
        CHECK(pl.Update(At(500, 20), 4000) == gps::Event::None);  // 400 < 625 never
        CHECK(pl.Update(At(500, 20), 60000) == gps::Event::None);
        CHECK(pl.Update(At(500, 30), 2999) == gps::Event::None);  // timer not expired
        CHECK(pl.Update(At(500, 30), 4000) == gps::Event::Replanned);  // 900 > 625
        CHECK(pl.stats().replans == 1);
    }
    {   // TV7: tracked goal moved
        Chain c; gps::GpsGraph G(c.g);
        gps::GpsPlanner pl(G);
        Vec3 tgt{800, 0, 0};
        pl.SetMission(At(50, 0), tgt, 0, [&](Vec3& o) { o = tgt; return true; });
        tgt.x = 816;
        CHECK(pl.Update(At(50, 0), 3500) == gps::Event::Replanned);  // 256 > 225
        gps::GpsPlanner p2(G);
        Vec3 t2{800, 0, 0};
        p2.SetMission(At(50, 0), t2, 0, [&](Vec3& o) { o = t2; return true; });
        t2.x = 814;
        CHECK(p2.Update(At(50, 0), 3500) == gps::Event::None);  // 196
    }
    {   // TV8: 120 m off route, far branch on the 2.5 s timer
        Chain c; gps::GpsGraph G(c.g);
        gps::GpsPlanner pl(G);
        pl.SetPin(At(50, 0), Vec3{900, 0, 0}, 0);
        CHECK(pl.Update(At(500, 120, gps::Mover::OnFoot), 2000) == gps::Event::None);
        CHECK(pl.Update(At(500, 120, gps::Mover::OnFoot), 2600) == gps::Event::Replanned);
    }
    {   // TV9: mission waypoint, pause-map pin replaces, press again restores the mission route
        Chain c; gps::GpsGraph G(c.g);
        gps::GpsPlanner pl(G);
        pl.SetMission(At(50, 0), Vec3{800, 0, 0}, 0);
        CHECK(pl.route().kind == gps::Kind::Mission);
        pl.TogglePin(At(50, 0), Vec3{300, 0, 0}, 100);
        CHECK(pl.route().kind == gps::Kind::Pin);
        CHECK(Near(pl.route().goal.x, 300));
        pl.TogglePin(At(50, 0), Vec3{300, 0, 0}, 200);
        CHECK(pl.isPlaced() && pl.route().kind == gps::Kind::Mission);
        CHECK(Near(pl.route().goal.x, 800));
        // a mission clear never removes a pin and a pin clear never removes a mission route
        pl.SetPin(At(50, 0), Vec3{300, 0, 0}, 300);
        pl.Clear(gps::Kind::Mission, 400);
        CHECK(pl.isPlaced() && pl.route().kind == gps::Kind::Pin);
        gps::GpsPlanner pm(G);
        pm.SetMission(At(50, 0), Vec3{800, 0, 0}, 0);
        pm.Clear(gps::Kind::Pin, 100);
        CHECK(pm.isPlaced());
        pm.Clear(gps::Kind::Mission, 200);
        CHECK(!pm.isPlaced());
    }
    {   // TV10: duplicate request within 1 m ignored
        Chain c; gps::GpsGraph G(c.g);
        gps::GpsPlanner pl(G);
        pl.SetPin(At(50, 0), Vec3{800, 0, 0}, 0);
        const auto plans = pl.stats().plans;
        pl.SetPin(At(50, 0), Vec3{800.6, 0, 0}, 10);
        CHECK(pl.stats().plans == plans && pl.stats().ignoredDup == 1);
        pl.SetPin(At(50, 0), Vec3{802, 0, 0}, 20);
        CHECK(pl.stats().plans == plans + 1);
    }
    {   // TV11: helicopter -> direct mode, retried after landing
        Chain c; gps::GpsGraph G(c.g);
        gps::GpsPlanner pl(G);
        pl.SetPin(At(50, 0, gps::Mover::Helicopter), Vec3{800, 0, 0}, 0);
        CHECK(pl.isPlaced() && pl.route().direct && pl.route().lanes.empty());
        CHECK(pl.Update(At(60, 0, gps::Mover::Helicopter), 3500) == gps::Event::None);  // still airborne
        CHECK(pl.route().direct);
        CHECK(pl.Update(At(60, 0, gps::Mover::OnFoot), 7000) == gps::Event::DirectRetry);
        CHECK(!pl.route().direct && !pl.route().lanes.empty());
    }
    {   // unreachable goal commits direct with planFailed (CHOSEN C5)
        Chain c; gps::GpsGraph G(c.g);
        gps::GpsPlanner pl(G);
        pl.SetPin(At(50, 0), Vec3{5000, 0, 5000}, 0);
        CHECK(pl.isPlaced() && pl.route().direct && pl.route().planFailed);
    }
    {   // GPS-6: mover kind from the vehicle class (Sec23.6)
        CHECK(gps::MoverForVehicleClass(false, 2) == gps::Mover::OnFoot);
        // R4F-5 Sec23.2 step 1: current or last vehicle
        CHECK(gps::MoverForPlayer(false, 0, 2) == gps::Mover::Airplane);
        CHECK(gps::MoverForPlayer(false, 0, 3) == gps::Mover::Helicopter);
        CHECK(gps::MoverForPlayer(false, 0, 4) == gps::Mover::Helicopter);
        CHECK(gps::MoverForPlayer(false, 0, 0) == gps::Mover::OnFoot);
        CHECK(gps::MoverForPlayer(false, 0, 5) == gps::Mover::OnFoot);
        CHECK(gps::MoverForPlayer(false, 0, -1) == gps::Mover::OnFoot);
        CHECK(gps::MoverForPlayer(true, 5, 2) == gps::Mover::Boat);
        CHECK(gps::MoverForVehicleClass(true, 0) == gps::Mover::Ground);
        CHECK(gps::MoverForVehicleClass(true, 1) == gps::Mover::Ground);
        CHECK(gps::MoverForVehicleClass(true, 2) == gps::Mover::Airplane);
        CHECK(gps::MoverForVehicleClass(true, 3) == gps::Mover::Helicopter);
        CHECK(gps::MoverForVehicleClass(true, 4) == gps::Mover::Helicopter);
        CHECK(gps::MoverForVehicleClass(true, 5) == gps::Mover::Boat);
    }
    if (g_failures == 0) std::cout << "gps_route_test: all checks passed\n";
    return g_failures;
}
