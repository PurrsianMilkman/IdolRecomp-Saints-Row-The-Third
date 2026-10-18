#include "sr3airuntime/ped_navigator.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace sr3airuntime {

namespace {
double Dist2D(const sr3zone::RoadVec3& a, const sr3zone::RoadVec3& b) { return std::hypot(double(a.x) - b.x, double(a.z) - b.z); }
double Dist2D(const Vec3& a, const sr3zone::RoadVec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

// Edge ids of node n (CSR, spec-zone-data-format.md Sec13.2a: the list ends at the next node's value or the pool count).
void NodeEdgeRange(const sr3zone::RoadGraph& g, uint32_t n, size_t& b, size_t& e) {
    b = g.sidewalkNodes[n].adjFirst;
    e = n + 1 < g.sidewalkNodes.size() ? g.sidewalkNodes[n + 1].adjFirst : g.sidewalkAdj.size();
    if (e > g.sidewalkAdj.size()) e = g.sidewalkAdj.size();
}
}  // namespace

uint32_t NearestSidewalkEdge(const sr3zone::RoadGraph& g, const Vec3& pos, double radius) {
    uint32_t best = kPedNoIndex;
    double bestD = radius;
    for (uint32_t ei : g.SidewalkEdgesNear(pos.x, pos.z, radius)) {
        const auto& e = g.sidewalkEdges[ei];
        const auto& a = g.sidewalkNodes[e.nodeA].pos;
        const auto& b = g.sidewalkNodes[e.nodeB].pos;
        const double ex = double(b.x) - a.x, ez = double(b.z) - a.z;
        const double L2 = ex * ex + ez * ez;
        double t = L2 > 1e-12 ? ((pos.x - a.x) * ex + (pos.z - a.z) * ez) / L2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double d = std::hypot(pos.x - (a.x + ex * t), pos.z - (a.z + ez * t));
        if (d <= bestD) {
            bestD = d;
            best = ei;
        }
    }
    return best;
}

std::optional<PedRoute> FindSidewalkRoute(const sr3zone::RoadGraph& g, const Vec3& pos, uint32_t startEdge,
                                          uint32_t goalNode, size_t* expansionsOut) {
    if (expansionsOut) *expansionsOut = 0;
    if (startEdge >= g.sidewalkEdges.size() || goalNode >= g.sidewalkNodes.size()) return std::nullopt;
    const auto& se = g.sidewalkEdges[startEdge];
    const sr3zone::RoadVec3& goalPos = g.sidewalkNodes[goalNode].pos;

    struct Rec { double g = 1e300; uint32_t parent = kPedNoIndex, viaEdge = kPedNoIndex; bool closed = false; };
    std::unordered_map<uint32_t, Rec> rec;
    struct Item { double f; uint32_t node; bool operator<(const Item& o) const { return f > o.f; } };
    std::priority_queue<Item> open;
    const uint32_t seeds[2] = {se.nodeA, se.nodeB};
    for (uint32_t s : seeds) {  // both ends of the start edge, seeded with their straight-line distance
        if (s >= g.sidewalkNodes.size()) continue;
        const double d = Dist2D(pos, g.sidewalkNodes[s].pos);
        Rec& r = rec[s];
        if (d < r.g) {
            r.g = d;
            open.push({d + Dist2D(g.sidewalkNodes[s].pos, goalPos), s});
        }
    }
    size_t expansions = 0;
    uint32_t reached = kPedNoIndex;
    while (!open.empty()) {
        const Item it = open.top();
        open.pop();
        Rec& cur = rec[it.node];
        if (cur.closed) continue;
        cur.closed = true;
        if (it.node == goalNode) {
            reached = it.node;
            break;
        }
        if (++expansions > kPedPathExpansionCapChosen) break;
        size_t b, e;
        NodeEdgeRange(g, it.node, b, e);
        const double gcur = cur.g;
        for (size_t k = b; k < e; ++k) {
            const uint32_t ei = g.sidewalkAdj[k];
            if (ei >= g.sidewalkEdges.size()) continue;
            const auto& ed = g.sidewalkEdges[ei];
            const uint32_t m = ed.nodeA == it.node ? ed.nodeB : ed.nodeA;
            if (m >= g.sidewalkNodes.size()) continue;
            const double ng = gcur + Dist2D(g.sidewalkNodes[it.node].pos, g.sidewalkNodes[m].pos);
            if (ng > kPedPathCostCapM) continue;
            Rec& mr = rec[m];
            if (mr.closed || ng >= mr.g) continue;
            mr.g = ng;
            mr.parent = it.node;
            mr.viaEdge = ei;
            open.push({ng + Dist2D(g.sidewalkNodes[m].pos, goalPos), m});
        }
    }
    if (expansionsOut) *expansionsOut = expansions;
    if (reached == kPedNoIndex) return std::nullopt;

    PedRoute route;
    route.startEdge = startEdge;
    std::vector<uint32_t> nodes{reached}, edges;
    uint32_t n = reached;
    while (rec[n].parent != kPedNoIndex) {
        edges.push_back(rec[n].viaEdge);
        n = rec[n].parent;
        nodes.push_back(n);
    }
    std::reverse(nodes.begin(), nodes.end());
    std::reverse(edges.begin(), edges.end());
    route.nodes = std::move(nodes);
    route.edges = std::move(edges);
    route.length = rec[reached].g - rec[route.nodes.front()].g;
    return route;
}

uint32_t ChoosePedGoalNode(const sr3zone::RoadGraph& g, const Vec3& pos, const std::function<double()>& rand01) {
    const std::vector<uint32_t> near = g.SidewalkEdgesNear(pos.x, pos.z, kPedGoalRadiusChosenM);
    if (near.empty()) return kPedNoIndex;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const size_t i = std::min(near.size() - 1, static_cast<size_t>(rand01() * near.size()));
        const auto& e = g.sidewalkEdges[near[i]];
        const uint32_t n = rand01() < 0.5 ? e.nodeA : e.nodeB;
        if (n >= g.sidewalkNodes.size()) continue;
        const double d = Dist2D(pos, g.sidewalkNodes[n].pos);
        if (d >= kPedGoalMinDistChosenM && d <= kPedGoalRadiusChosenM) return n;
    }
    return kPedNoIndex;
}

bool PedNavigatorBegin(PedNavigator& nav, const sr3zone::RoadGraph& g, const Vec3& pos, uint32_t goalNode) {
    nav.active = false;
    nav.crossing = PedCrossingState{};
    nav.waiting = false;
    const uint32_t startEdge = NearestSidewalkEdge(g, pos, 15.0);
    if (startEdge == kPedNoIndex) return false;
    auto r = FindSidewalkRoute(g, pos, startEdge, goalNode);
    if (!r) return false;
    nav.route = std::move(*r);
    nav.next = 0;
    const auto& gp = g.sidewalkNodes[goalNode].pos;
    nav.goal = Vec3{gp.x, gp.y, gp.z};
    nav.event8DueAtMs = -1;
    nav.active = true;
    return true;
}

double PickPedWalkSpeed(bool female, const std::function<double()>& rand01) {
    const double* pool = female ? kPedWalkPoolFemale : kPedWalkPoolMale;
    const size_t n = female ? kPedWalkPoolFemaleSize : kPedWalkPoolMaleSize;
    size_t i = static_cast<size_t>(rand01() * static_cast<double>(n));
    if (i >= n) i = n - 1;
    return pool[i];
}

PedNavResult PedNavigatorStep(PedNavigator& nav, const sr3zone::RoadGraph& g, Vec3& pos, double dt, double speed,
                              int64_t nowMs, const std::function<bool(size_t)>& crossingClosed) {
    if (!nav.active) return PedNavResult::Arrived;
    // Advances past every route node the ped is within the arrival radius of. true = the goal was reached.
    auto advance = [&]() {
        while (true) {
            const auto& np = g.sidewalkNodes[nav.route.nodes[nav.next]].pos;
            if (!PedArrived(pos, Vec3{np.x, np.y, np.z})) return false;
            ++nav.edgesWalked;
            if (nav.next + 1 >= nav.route.nodes.size()) {
                nav.active = false;
                nav.event8DueAtMs = nowMs + kPedArrivalEventDelayMs;
                nav.crossing = PedCrossingState{};
                return true;
            }
            ++nav.next;
        }
    };
    if (advance()) return PedNavResult::Arrived;

    const auto& tp = g.sidewalkNodes[nav.route.nodes[nav.next]].pos;
    const Vec3 from = pos;
    Vec3 to = StepTowardDestination(from, Vec3{tp.x, tp.y, tp.z}, speed * dt);
    const uint32_t curEdge = nav.next == 0 ? nav.route.startEdge : nav.route.edges[nav.next - 1];
    const auto& ed = g.sidewalkEdges[curEdge];
    const PedCrossingState before = nav.crossing;
    if (ed.crossing != 0xffff) {
        const bool closed = crossingClosed ? crossingClosed(ed.crossing) : false;
        const auto& na = g.sidewalkNodes[ed.nodeA].pos;
        const auto& nb = g.sidewalkNodes[ed.nodeB].pos;
        to = PedCrossingGate(from, to, uint64_t(curEdge) + 1, Vec3{na.x, na.y, na.z}, Vec3{nb.x, nb.y, nb.z}, ed.width(),
                             closed, nav.crossing);
    } else if (nav.crossing.edgeId != 0) {
        nav.crossing = PedCrossingState{};  // left the crossing edge
    }
    pos = to;
    if (nav.crossing.waiting && !before.waiting) ++nav.waits;
    if (nav.crossing.committed && !before.committed) ++nav.crosses;
    nav.waiting = nav.crossing.waiting;
    if (nav.waiting) return PedNavResult::Waiting;
    if (advance()) return PedNavResult::Arrived;
    return PedNavResult::Walking;
}

}  // namespace sr3airuntime
