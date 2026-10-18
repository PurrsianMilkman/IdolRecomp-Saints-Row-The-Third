// Pedestrian crossing gate (include/sr3airuntime/ped_crossing.h) against spec-ai-runtime.md Sec20.6.9(c) and
// Sec20.6.13(h). Part 1 synthetic (always runs): closed crossing -> wait at the kerb, open -> cross, a started crossing
// is finished, an unsignalled crossing is never closed. Part 2 real city crossing 10 / light 954 driven by the real
// TrafficSignalController (skipped when the archive is absent).
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3airuntime/ped_crossing.h"
#include "sr3airuntime/traffic_signals.h"
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

// Walk a ped from `p` toward `dest` at 1.4 m/s in 0.1 s ticks for `seconds`, the gate applied each tick.
Vec3 Walk(Vec3 p, const Vec3& dest, double seconds, uint64_t edgeId, const Vec3& a, const Vec3& b, double width,
          const std::function<bool()>& closed, PedCrossingState& st) {
    for (int i = 0; i < static_cast<int>(seconds * 10); ++i) {
        const Vec3 want = StepTowardDestination(p, dest, 0.14);
        p = PedCrossingGate(p, want, edgeId, a, b, width, closed(), st);
    }
    return p;
}

void TestSynthetic() {
    const Vec3 a{0, 0, 0}, b{0, 0, 10};  // crossing edge along +z, 4 m wide
    const Vec3 dest{0, 0, 20};
    bool closedNow = true;
    auto closed = [&] { return closedNow; };
    PedCrossingState st;
    Vec3 p{0, 0, -5};
    p = Walk(p, dest, 10.0, 7, a, b, 4.0, closed, st);
    CHECK(st.waiting && !st.committed);
    CHECK(p.z < 0.0 && p.z > -0.2);  // stopped at the near kerb line
    p = Walk(p, dest, 5.0, 7, a, b, 4.0, closed, st);
    CHECK(p.z < 0.0);  // still waiting while closed
    closedNow = false;
    p = Walk(p, dest, 1.0, 7, a, b, 4.0, closed, st);
    CHECK(!st.waiting && st.committed && p.z > 0.0);
    closedNow = true;  // light turns closed mid-crossing: finish the started crossing
    p = Walk(p, dest, 15.0, 7, a, b, 4.0, closed, st);
    CHECK(p.z >= 19.9);

    // far-to-near direction
    PedCrossingState st2;
    closedNow = true;
    Vec3 q{0, 0, 15};
    q = Walk(q, {0, 0, -10}, 10.0, 7, a, b, 4.0, closed, st2);
    CHECK(st2.waiting && q.z > 10.0 && q.z < 10.2);

    // unsignalled crossing: never closed (caller passes closed = false) -> straight through
    PedCrossingState st3;
    Vec3 r{0, 0, -5};
    r = Walk(r, dest, 25.0 / 1.4, 8, a, b, 4.0, [] { return false; }, st3);
    CHECK(!st3.waiting && r.z >= 19.9);

    // a ped walking parallel beside the crossing (outside its width) is not held
    PedCrossingState st4;
    Vec3 s{6, 0, -5};
    s = Walk(s, {6, 0, 20}, 25.0 / 1.4, 7, a, b, 4.0, closed, st4);
    CHECK(!st4.waiting && s.z >= 19.9);
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
    // find the sidewalk edge carrying crossing 10
    int64_t edge = -1;
    for (size_t i = 0; i < g.sidewalkEdges.size(); ++i)
        if (g.sidewalkEdges[i].crossing == 10) { edge = static_cast<int64_t>(i); break; }
    CHECK(edge >= 0);
    if (edge < 0) return;
    const auto& e = g.sidewalkEdges[static_cast<size_t>(edge)];
    const auto& na = g.sidewalkNodes[e.nodeA].pos;
    const auto& nb = g.sidewalkNodes[e.nodeB].pos;
    const Vec3 a{na.x, na.y, na.z}, b{nb.x, nb.y, nb.z};
    const double L = std::hypot(b.x - a.x, b.z - a.z);
    CHECK(L > 1.0);
    const Vec3 dir{(b.x - a.x) / L, 0, (b.z - a.z) / L};
    const Vec3 start{a.x - dir.x * 6, a.y, a.z - dir.z * 6};
    const Vec3 dest{b.x + dir.x * 6, b.y, b.z + dir.z * 6};

    TrafficSignalController c;
    c.Init(g, 0);
    c.Update(0, [](size_t) { return true; });
    CHECK(c.CrossingClosed(10));
    const auto closed = [&] { return c.CrossingClosed(10); };
    PedCrossingState st;
    Vec3 p = start;
    // t = 0: light 954 closed. The ped waits at the kerb for several seconds.
    p = Walk(p, dest, 8.0, static_cast<uint64_t>(edge) + 1, a, b, e.width(), closed, st);
    CHECK(st.waiting);
    CHECK(((p.x - a.x) * dir.x + (p.z - a.z) * dir.z) < 0.0);
    // The junction switches at t = 10000 (vector 3): the crossing opens and the ped crosses.
    for (int i = 0; i < 400; ++i) c.Update(10000, [](size_t) { return true; });
    CHECK(!c.CrossingClosed(10));
    p = Walk(p, dest, 25.0, static_cast<uint64_t>(edge) + 1, a, b, e.width(), closed, st);
    CHECK(!st.waiting);
    CHECK(((p.x - a.x) * dir.x + (p.z - a.z) * dir.z) > L);
}
}  // namespace

int main() {
    TestSynthetic();
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
    if (g_failures == 0) std::cout << "ped_crossing_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
