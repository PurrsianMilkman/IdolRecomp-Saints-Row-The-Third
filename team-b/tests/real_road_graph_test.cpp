// Real-data regression for sr3zone::RoadGraph (include/sr3zone/road_graph.h),
// spec-zone-data-format.md Sec13.2a. Reads sr3_city~fawld.czn_pc from
// CRREISH_GAME_CACHE_DIR/sr3_city_0.vpp_pc; exit 77 (SKIPPED) when absent.
// Every expected value below is a Sec13.2a(m)/(n) test vector (global totals,
// tile 1215 "Downtown", tile 1517 "Suburban-type").
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3zone/road_graph.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)
bool Near(double a, double b, double tol = 1e-3) { return std::fabs(a - b) <= tol; }

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
bool find(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            if (find(c.openNested(i), name, out)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}
}  // namespace

int main() {
#ifndef CRREISH_GAME_CACHE_DIR
    std::cout << "CRREISH_GAME_CACHE_DIR not configured - SKIPPED\n";
    return 77;
#else
    const std::string path = std::string(CRREISH_GAME_CACHE_DIR) + "/sr3_city_0.vpp_pc";
    if (!std::ifstream(path, std::ios::binary).good()) {
        std::cout << "archive not found (" << path << ") - SKIPPED\n";
        return 77;
    }
    std::vector<uint8_t> czn;
    try {
        auto opened = vppcache::openArchive(path);
        if (!find(opened->container, "sr3_city~fawld.czn_pc", czn)) {
            std::cout << "sr3_city~fawld.czn_pc not found\n";
            return 1;
        }
    } catch (const std::exception& ex) {
        std::cout << "open failed: " << ex.what() << "\n";
        return 77;
    }

    sr3zone::RoadGraph g;
    try {
        g = sr3zone::RoadGraph::Parse(vpp::ByteView(czn.data(), czn.size()));
    } catch (const std::exception& ex) {
        std::cerr << "Parse threw: " << ex.what() << "\n";
        return 1;
    }

    // Sec13.2a(m) global totals.
    CHECK(g.segments.size() == 11455);
    CHECK(g.nodes.size() == 10141);
    CHECK(g.shapePool.size() == 6216 * 3);
    CHECK(g.sidewalkNodes.size() == 7659);
    CHECK(g.sidewalkEdges.size() == 8703);
    if (g.segments.size() != 11455 || g.sidewalkEdges.size() != 8703) return 1;

    // Sec13.2a(n) tile 1215 (Downtown): 161 segments, 140 nodes, 120 sidewalk nodes, 149 edges.
    const sr3zone::RoadTile dt{12, 15};
    CHECK(g.SegmentsInTile(dt).size() == 161);
    CHECK(g.SidewalkEdgesInTile(dt).size() == 149);
    {
        size_t nn = 0, sn = 0;
        for (const auto& n : g.nodes) if (sr3zone::RoadTileOf(n.x, n.z) == dt) ++nn;
        for (const auto& n : g.sidewalkNodes) if (sr3zone::RoadTileOf(n.pos.x, n.pos.z) == dt) ++sn;
        CHECK(nn == 140);
        CHECK(sn == 120);
        int sig = 0, bit10 = 0;
        for (uint32_t s : g.SegmentsInTile(dt)) {
            if (g.segments[s].signal == 3) ++sig;
            if (g.segments[s].flags & sr3zone::kSegFlagAmbient) ++bit10;
        }
        CHECK(sig == 41);
        CHECK(bit10 == 142);
    }
    // tile 1517: 45 segments, 20 sidewalk edges.
    const sr3zone::RoadTile sub{15, 17};
    CHECK(g.SegmentsInTile(sub).size() == 45);
    CHECK(g.SidewalkEdgesInTile(sub).size() == 20);

    // seg 33: node 36 -> 37, ctrl, flags, lengths, links, shape.
    {
        const auto& s = g.segments[33];
        CHECK(s.nodeA == 36 && s.nodeB == 37);
        CHECK(Near(g.nodes[36].x, -203.5) && Near(g.nodes[36].y, 16.33, 0.01) && Near(g.nodes[36].z, -511.5));
        CHECK(Near(s.ctrl1.x, -215.9, 0.01) && Near(s.ctrl2.z, -508.5, 0.01));
        CHECK(s.flags == 0x02000404u);
        CHECK(Near(s.chord256 / 256.0, 21.5703, 0.005));
        CHECK(Near(s.halfArc256 / 256.0, 13.0508, 0.005));
        CHECK(s.shapeFirst == 35);
        CHECK(g.InteriorPointCount(33) == 2);
        const auto succ = g.Successors(33);
        const auto pred = g.Predecessors(33);
        CHECK(succ.size() == 1 && succ[0] == 43);
        CHECK(pred.size() == 1 && pred[0] == 54);
        CHECK(g.Conflicts(33).empty());
        const auto poly = g.SegmentPolyline(33);
        CHECK(poly.size() == 4);
        if (poly.size() == 4) {
            CHECK(Near(poly[1].x, -211.250, 0.01) && Near(poly[1].z, -510.812, 0.01));
            CHECK(Near(poly[2].x, -216.313, 0.01) && Near(poly[2].z, -508.062, 0.01));
        }
        sr3zone::RoadVec3 mn, mx;
        g.SegmentAabb(33, mn, mx);
        CHECK(Near(mn.x, -219.0, 0.01) && Near(mx.x, -203.5, 0.01) && Near(mn.z, -511.5, 0.01) &&
              Near(mx.z, -496.5, 0.01));
    }
    // Pooled successor list: seg 6902 -> [6903, 11395]; predecessor [6901].
    {
        const auto succ = g.Successors(6902);
        CHECK(succ.size() == 2 && succ[0] == 6903 && succ[1] == 11395);
        const auto pred = g.Predecessors(6902);
        CHECK(pred.size() == 1 && pred[0] == 6901);
        CHECK(g.segments[6900].left == 6920);  // oncoming lane neighbour
        CHECK(g.segments[6900].flags & sr3zone::kSegFlagOncomingLeft);
    }
    // Junction: node 189 receives segments [212, 328], both leading to [183, 184].
    for (size_t seg : {212u, 328u}) {
        CHECK(g.segments[seg].nodeB == 189);
        const auto s = g.Successors(seg);
        CHECK(s.size() == 2 && s[0] == 183 && s[1] == 184);
    }
    // Sidewalk edge 260: nodes 271-282, 27.174 m, width 3, crossing 10. Node 271.
    {
        const auto& e = g.sidewalkEdges[260];
        CHECK(e.nodeA == 271 && e.nodeB == 282);
        CHECK(Near(e.width(), 3.0));
        CHECK(e.crossing == 10);
        const auto& a = g.sidewalkNodes[e.nodeA];
        const auto& b = g.sidewalkNodes[e.nodeB];
        CHECK(Near(a.pos.x, -203.5) && Near(a.pos.y, 16.15, 0.01) && Near(a.pos.z, -502.425));
        CHECK(Near(std::hypot(b.pos.x - a.pos.x, std::hypot(b.pos.y - a.pos.y, b.pos.z - a.pos.z)), 27.174, 0.01));
        CHECK(g.sidewalkEdges[259].nodeA == 271 && g.sidewalkEdges[259].nodeB == 272);
    }
    // Invariants: successor continuity within 0.04 m; back-reference; lane neighbour symmetry.
    {
        size_t bad = 0, links = 0, badSym = 0;
        for (size_t i = 0; i < g.segments.size(); ++i) {
            for (uint16_t j : g.Successors(i)) {
                ++links;
                const auto& b = g.SegB(i);
                const auto& a = g.SegA(j);
                if (std::hypot(std::hypot(a.x - b.x, a.y - b.y), a.z - b.z) > 0.04) ++bad;
                const auto pj = g.Predecessors(j);
                if (std::find(pj.begin(), pj.end(), i) == pj.end()) ++bad;
            }
            const auto& s = g.segments[i];
            // Oncoming left neighbour (flag bit 0) points back through ITS left; same-direction through its right.
            if (s.left != 0xffff) {
                const auto& l = g.segments[s.left];
                const uint16_t back = (s.flags & sr3zone::kSegFlagOncomingLeft) ? l.left : l.right;
                if (back != i) ++badSym;
            }
        }
        CHECK(links == 14325);
        CHECK(bad == 0);
        CHECK(badSym == 0);
    }
    // Sec13.2a(i) + Sec20.6.13(d): signal lights, junctions, phases, list pools, crossings.
    {
        CHECK(g.lights.size() == 1207);
        CHECK(g.junctions.size() == 346);
        CHECK(g.phases.size() == 678);
        CHECK(g.phaseLights.size() == 1233);
        CHECK(g.phaseLanes.size() == 1544);
        CHECK(g.crossingLight.size() == 1195);
        size_t litCrossings = 0;
        for (int32_t l : g.crossingLight) litCrossings += l >= 0;
        CHECK(litCrossings == 876);
        CHECK(g.crossingLight[0] == -1);
        CHECK(g.crossingLight[10] == 954);
        CHECK(g.lights[954].vehicleState == 3 && g.lights[954].pedState == 2);
        CHECK(g.lights[954].zoneId == 0xffff && g.lights[954].objectIndex == -1);
        // junction 2: position, phases, lists (vector 1)
        CHECK(Near(g.junctions[2].pos.x, -222.0) && Near(g.junctions[2].pos.y, 26.0) && Near(g.junctions[2].pos.z, -516.0));
        CHECK(g.junctions[2].byte0f == 1 && g.junctions[2].currentPhase == 0);
        CHECK(g.PhaseCount(2) == 2);
        // every light is referenced by some phase; no lane is listed twice
        std::vector<int> lightRef(g.lights.size(), 0), laneRef(g.segments.size(), 0);
        for (uint16_t l : g.phaseLights) ++lightRef[l];
        for (uint16_t s : g.phaseLanes) ++laneRef[s];
        CHECK(std::count(lightRef.begin(), lightRef.end(), 0) == 0);
        CHECK(*std::max_element(laneRef.begin(), laneRef.end()) == 1);
        CHECK(std::count(laneRef.begin(), laneRef.end(), 1) == 1544);
        // phase classes (Sec20.6.13(d)): 259 two-phase 10/10, 2 at 15/10 or 10/15, junction 162, junction 26, 83 lightless
        size_t cls1010 = 0, cls1510 = 0, cls10_0 = 0, cls1phase = 0, lightless = 0, other = 0;
        bool j162 = false, j26 = false;
        for (size_t j = 0; j < g.junctions.size(); ++j) {
            const size_t np = g.PhaseCount(j);
            const size_t p0 = g.junctions[j].firstPhase;
            if (g.PhaseLightCount(p0) == 0) { ++lightless; continue; }
            if (np == 2) {
                const unsigned a = g.phases[p0].duration, b = g.phases[p0 + 1].duration;
                if (a == 10000 && b == 10000) ++cls1010;
                else if ((a == 15000 && b == 10000) || (a == 10000 && b == 15000)) ++cls1510;
                else if (a == 10000 && b == 0) { ++cls10_0; j162 = j == 162; }
                else ++other;
            } else if (np == 1 && g.phases[p0].duration == 10000) {
                ++cls1phase;
                j26 = j == 26;
            } else {
                ++other;
            }
        }
        CHECK(cls1010 == 259);
        CHECK(cls1510 == 2);
        CHECK(cls10_0 == 1 && j162);
        CHECK(cls1phase == 1 && j26);
        CHECK(lightless == 83);
        CHECK(other == 0);
        // junctions 187 / 231 are the 15/10 pair
        for (size_t j : {187u, 231u}) {
            const size_t p0 = g.junctions[j].firstPhase;
            const unsigned a = g.phases[p0].duration, b = g.phases[p0 + 1].duration;
            CHECK((a == 15000 && b == 10000) || (a == 10000 && b == 15000));
        }
        // Sec20.6.13(g): 195 stop-line (bit 7) lanes, all in phase lists
        size_t bit7 = 0;
        for (size_t s = 0; s < g.segments.size(); ++s)
            if (g.segments[s].flags & sr3zone::kSegFlagStopLine) {
                ++bit7;
                CHECK(laneRef[s] == 1);
            }
        CHECK(bit7 == 195);
    }
    if (g_failures == 0) std::cout << "real_road_graph_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
#endif
}
