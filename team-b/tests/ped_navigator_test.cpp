// Ambient ped navigator (include/sr3airuntime/ped_navigator.h) against spec-ai-runtime.md Sec20.6.9.
// Synthetic: shortest path on a small graph, unreachable goal, arrival radius 0.425 m + event 8 delay, a route over a
// signalled crossing waits while closed and crosses when open. Real: a path between two real sidewalk nodes is connected
// and uses real edges (skipped when the archive is absent).
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3airuntime/ped_navigator.h"
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

struct E { uint16_t a, b; uint16_t crossing; };

sr3zone::RoadGraph Build(const std::vector<sr3zone::RoadVec3>& pts, const std::vector<E>& es, int32_t lightForCrossing0 = -1) {
    sr3zone::RoadGraph g;
    std::vector<std::vector<uint16_t>> adj(pts.size());
    for (size_t i = 0; i < es.size(); ++i) {
        sr3zone::SidewalkEdge e;
        e.nodeA = es[i].a; e.nodeB = es[i].b; e.width256 = 4 * 256; e.crossing = es[i].crossing;
        g.sidewalkEdges.push_back(e);
        adj[es[i].a].push_back(static_cast<uint16_t>(i));
        adj[es[i].b].push_back(static_cast<uint16_t>(i));
    }
    for (size_t n = 0; n < pts.size(); ++n) {
        sr3zone::SidewalkNode sn;
        sn.pos = pts[n];
        sn.adjFirst = static_cast<uint16_t>(g.sidewalkAdj.size());
        for (uint16_t e : adj[n]) g.sidewalkAdj.push_back(e);
        g.sidewalkNodes.push_back(sn);
    }
    g.crossingLight.push_back(lightForCrossing0);
    g.RebuildIndices();
    return g;
}

double Dist(const sr3zone::RoadVec3& a, const sr3zone::RoadVec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

void TestSynthetic() {
    // 0 --10-- 1 --10-- 2          0-1-2-3 is the long way round (z=0 then up 10), 0-4-3 the shortcut? build:
    // nodes: 0(0,0) 1(10,0) 2(20,0) 3(20,10) 4(0,10) 5(100,100 isolated)
    // edges: 0-1, 1-2, 2-3, 0-4, 4-3 (4-3 is 20 m, so 0-4-3 = 30 m; 0-1-2-3 = 30 m too; make 1-2 longer) -> node 2 at (25,0)
    const std::vector<sr3zone::RoadVec3> pts = {{0, 0, 0}, {10, 0, 0}, {25, 0, 0}, {20, 0, 10}, {0, 0, 10}, {100, 0, 100}};
    const sr3zone::RoadGraph g = Build(pts, {{0, 1, 0xffff}, {1, 2, 0xffff}, {2, 3, 0xffff}, {0, 4, 0xffff}, {4, 3, 0xffff}});
    size_t exp = 0;
    const auto r = FindSidewalkRoute(g, Vec3{1, 0, 0}, 0, 3, &exp);
    CHECK(r.has_value());
    if (r) {
        // the cheaper route is 0-4-3 (10 + 20 = 30 m) vs 0-1-2-3 (10 + 15 + 11.2 = 36.2 m), or 1 ... 3 from the other seed
        CHECK(r->nodes.back() == 3);
        CHECK(r->edges.size() + 1 == r->nodes.size());
        CHECK(r->nodes.size() == 3);  // seed 0, then 4, then 3
        CHECK(r->nodes[0] == 0 && r->nodes[1] == 4);
        CHECK(std::fabs(r->length - 30.0) < 1e-6);
        CHECK(exp >= 1);
    }
    CHECK(!FindSidewalkRoute(g, Vec3{1, 0, 0}, 0, 5).has_value());  // isolated goal
    CHECK(!FindSidewalkRoute(g, Vec3{1, 0, 0}, 0, 99).has_value());  // bad goal index
    CHECK(NearestSidewalkEdge(g, Vec3{5, 0, 1}, 5.0) == 0);
    CHECK(NearestSidewalkEdge(g, Vec3{500, 0, 500}, 5.0) == kPedNoIndex);
}

void TestArrival() {
    CHECK(PedArrived({0, 0, 0}, {0.42, 0, 0}));
    CHECK(!PedArrived({0, 0, 0}, {0.43, 0, 0}));
    const std::vector<sr3zone::RoadVec3> pts = {{0, 0, 0}, {10, 0, 0}};
    const sr3zone::RoadGraph g = Build(pts, {{0, 1, 0xffff}});
    PedNavigator nav;
    Vec3 pos{0, 0, 0};
    CHECK(PedNavigatorBegin(nav, g, pos, 1));
    const auto never = [](size_t) { return false; };
    int64_t now = 0;
    PedNavResult res = PedNavResult::Walking;
    for (int i = 0; i < 400 && res != PedNavResult::Arrived; ++i) {
        now += 100;
        res = PedNavigatorStep(nav, g, pos, 0.1, 1.4, now, never);
    }
    CHECK(res == PedNavResult::Arrived);
    CHECK(PedArrived(pos, {10, 0, 0}));
    CHECK(pos.x < 10.0 - 0.425 + 1.4 * 0.1 + 1e-9);  // stops at the radius, not at the node
    CHECK(nav.event8DueAtMs == now + 1000);
    CHECK(!nav.active);
}

void TestCrossingWait() {
    // A(0,0) -- B(0,10) = crossing edge (index 1) -- C(0,20)...: nodes 0..3 along +z at z=0,10,24,34; edge 1-2 is the crossing
    const std::vector<sr3zone::RoadVec3> pts = {{0, 0, 0}, {0, 0, 10}, {0, 0, 24}, {0, 0, 34}};
    const sr3zone::RoadGraph g = Build(pts, {{0, 1, 0xffff}, {1, 2, 0}, {2, 3, 0xffff}}, 0);
    PedNavigator nav;
    Vec3 pos{0, 0, 0};
    CHECK(PedNavigatorBegin(nav, g, pos, 3));
    bool closed = true;
    const auto cl = [&](size_t c) { return c == 0 && closed; };
    int64_t now = 0;
    PedNavResult res = PedNavResult::Walking;
    for (int i = 0; i < 300; ++i) {  // 30 s closed
        now += 100;
        res = PedNavigatorStep(nav, g, pos, 0.1, 1.4, now, cl);
    }
    CHECK(res == PedNavResult::Waiting);
    CHECK(pos.z < 10.0 && pos.z > 9.5);  // stopped at the kerb before the near edge
    CHECK(nav.waits == 1);
    closed = false;
    for (int i = 0; i < 400 && res != PedNavResult::Arrived; ++i) {
        now += 100;
        res = PedNavigatorStep(nav, g, pos, 0.1, 1.4, now, cl);
    }
    CHECK(res == PedNavResult::Arrived);
    CHECK(pos.z > 33.0);
    CHECK(nav.crosses == 1);
}

bool EntryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    const bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                    r.status == vpp::DecodeStatus::ContentValidated || r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                    r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}
bool FindEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return EntryBytes(c, i, out);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            if (FindEntry(c.openNested(i), name, out)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

void TestReal(const sr3zone::RoadGraph& g) {
    CHECK(g.sidewalkNodes.size() > 7000);
    // Two known nodes: take the node set reachable from node 0's first edge by BFS and pick the hop-farthest one within
    // 3 km of straight-line, so the route is long enough to be a real test.
    const uint32_t startEdge = g.sidewalkAdj[g.sidewalkNodes[0].adjFirst];
    const auto& se = g.sidewalkEdges[startEdge];
    std::vector<int> hops(g.sidewalkNodes.size(), -1);
    std::vector<uint32_t> q{se.nodeA};
    hops[se.nodeA] = 0;
    uint32_t far = se.nodeA;
    for (size_t h = 0; h < q.size(); ++h) {
        const uint32_t n = q[h];
        const size_t b = g.sidewalkNodes[n].adjFirst;
        const size_t e = n + 1 < g.sidewalkNodes.size() ? g.sidewalkNodes[n + 1].adjFirst : g.sidewalkAdj.size();
        for (size_t k = b; k < e; ++k) {
            const auto& ed = g.sidewalkEdges[g.sidewalkAdj[k]];
            const uint32_t m = ed.nodeA == n ? ed.nodeB : ed.nodeA;
            if (hops[m] >= 0) continue;
            hops[m] = hops[n] + 1;
            q.push_back(m);
            if (hops[m] <= 60 && hops[m] > hops[far]) far = m;
        }
    }
    CHECK(hops[far] >= 10);
    const Vec3 pos{g.sidewalkNodes[se.nodeA].pos.x, g.sidewalkNodes[se.nodeA].pos.y, g.sidewalkNodes[se.nodeA].pos.z};
    size_t exp = 0;
    const auto r = FindSidewalkRoute(g, pos, startEdge, far, &exp);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->nodes.back() == far);
    CHECK(r->edges.size() + 1 == r->nodes.size());
    CHECK(exp <= kPedPathExpansionCapChosen);
    double len = 0;
    for (size_t i = 0; i < r->edges.size(); ++i) {
        const auto& ed = g.sidewalkEdges[r->edges[i]];
        const uint32_t a = r->nodes[i], b = r->nodes[i + 1];
        CHECK((ed.nodeA == a && ed.nodeB == b) || (ed.nodeA == b && ed.nodeB == a));  // a real edge joining the pair
        len += Dist(g.sidewalkNodes[a].pos, g.sidewalkNodes[b].pos);
    }
    CHECK(std::fabs(len - r->length) < 0.01);
    // the first route node is an end of the start edge
    CHECK(r->nodes[0] == se.nodeA || r->nodes[0] == se.nodeB);
    // walking it with no closed crossings reaches the goal
    PedNavigator nav;
    Vec3 p = pos;
    CHECK(PedNavigatorBegin(nav, g, p, far));
    int64_t now = 0;
    PedNavResult res = PedNavResult::Walking;
    for (int i = 0; i < 200000 && res != PedNavResult::Arrived; ++i) {
        now += 100;
        res = PedNavigatorStep(nav, g, p, 0.1, 1.4, now, [](size_t) { return false; });
    }
    CHECK(res == PedNavResult::Arrived);
    CHECK(nav.edgesWalked >= r->edges.size());
    std::cout << "real route: " << r->nodes.size() << " nodes, " << r->length << " m, " << exp << " expansions\n";
}
}  // namespace

void TestWalkSpeedPick() {
    // Sec20.6.9(a): base walk male 1.386 / female 1.368 is the first pool entry; every pick lies in 0.69..2.27 m/s.
    CHECK(PickPedWalkSpeed(false, [] { return 0.0; }) == 1.386);
    CHECK(PickPedWalkSpeed(true, [] { return 0.0; }) == 1.368);
    CHECK(PickPedWalkSpeed(false, [] { return 0.9999999; }) == kPedWalkPoolMale[kPedWalkPoolMaleSize - 1]);
    CHECK(PickPedWalkSpeed(true, [] { return 1.0; }) == kPedWalkPoolFemale[kPedWalkPoolFemaleSize - 1]);
    double lo = 1e9, hi = 0;
    for (int k = 0; k < 1000; ++k) {
        const double v = PickPedWalkSpeed(k & 1, [&] { return k / 1000.0; });
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    CHECK(lo >= 0.69 && hi <= 2.27 && lo < 0.7 && hi > 2.2);  // spans the variant range
}

int main() {
    TestWalkSpeedPick();
    TestSynthetic();
    TestArrival();
    TestCrossingWait();
#ifdef CRREISH_GAME_CACHE_DIR
    const std::string path = std::string(CRREISH_GAME_CACHE_DIR) + "/sr3_city_0.vpp_pc";
    if (std::ifstream(path, std::ios::binary).good()) {
        try {
            auto opened = vppcache::openArchive(path);
            std::vector<uint8_t> czn;
            if (FindEntry(opened->container, "sr3_city~fawld.czn_pc", czn)) {
                TestReal(sr3zone::RoadGraph::Parse(vpp::ByteView(czn.data(), czn.size())));
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
    if (g_failures == 0) std::cout << "ped_navigator_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
