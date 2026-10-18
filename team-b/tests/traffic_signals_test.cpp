// Traffic-signal controller (include/sr3airuntime/traffic_signals.h) against spec-ai-runtime.md
// Sec20.6.13: tick rules (b), cursor scheduling (c), phase classes (d), init (e), stop rules (f)/(g),
// crossings (h), test vectors (k). Part 1 is synthetic (always runs); part 2 uses the real city graph
// (skipped when the archive is absent).
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3airuntime/traffic_following.h"
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
bool Near(double a, double b, double tol = 1e-2) { return std::fabs(a - b) <= tol; }
const auto kAlways = [](size_t) { return true; };

struct PhaseSpec {
    uint16_t duration;
    int lights;
    std::vector<uint16_t> lanes;
};
void AddJunction(sr3zone::RoadGraph& g, const std::vector<PhaseSpec>& phases) {
    sr3zone::SignalJunction j;
    j.firstPhase = static_cast<uint16_t>(g.phases.size());
    g.junctions.push_back(j);
    for (const auto& p : phases) {
        sr3zone::SignalPhase sp;
        sp.firstLight = static_cast<uint16_t>(g.phaseLights.size());
        sp.firstLane = static_cast<uint16_t>(g.phaseLanes.size());
        sp.duration = p.duration;
        for (int k = 0; k < p.lights; ++k) {
            g.phaseLights.push_back(static_cast<uint16_t>(g.lights.size()));
            g.lights.push_back({});
        }
        for (uint16_t l : p.lanes) g.phaseLanes.push_back(l);
        g.phases.push_back(sp);
    }
}
sr3zone::RoadGraph Base(size_t nSeg) {
    sr3zone::RoadGraph g;
    g.nodes.push_back({0, 0, 0});
    for (size_t i = 0; i < nSeg; ++i) {
        sr3zone::RoadSegment s;
        s.signal = 3;
        g.segments.push_back(s);
    }
    return g;
}

void TestTiming() {
    // 259-class: two phases 10 s / 10 s.
    sr3zone::RoadGraph g = Base(4);
    AddJunction(g, {{10000, 2, {0, 1}}, {10000, 1, {2, 3}}});
    {
        // phase 0 approach: 7 s green, 3 s amber, 10 s red over one 20 s cycle, 100 ms samples
        TrafficSignalController c;
        c.Init(g, 0);
        int gr = 0, am = 0, rd = 0;
        for (int64_t t = 0; t < 20000; t += 100) {
            c.Update(t, kAlways);
            const int s = c.LaneState(0);
            (s == 1 ? gr : s == 2 ? am : rd) += 1;
        }
        CHECK(gr == 71);   // t = 0 .. 7000 (amber only when remaining < 3000)
        CHECK(am == 29);   // 7100 .. 9900
        CHECK(rd == 100);  // 10000 .. 19900
    }
    {
        TrafficSignalController c;
        c.Init(g, 0);
        c.Update(0, kAlways);
        CHECK(c.LaneState(0) == 1 && c.LaneState(1) == 1 && c.LaneState(2) == 3 && c.LaneState(3) == 3);
        CHECK(c.VehicleLight(0) == 1 && c.PedestrianLight(0) == 1 && c.VehicleLight(2) == 3 && c.PedestrianLight(2) == 2);
        c.Update(6999, kAlways);
        CHECK(c.LaneState(0) == 1);
        c.Update(7001, kAlways);  // remaining 2999 < 3000: amber; pedestrian byte untouched
        CHECK(c.LaneState(0) == 2 && c.LaneState(2) == 3 && c.VehicleLight(0) == 2 && c.PedestrianLight(0) == 1);
        c.Update(9999, kAlways);
        CHECK(c.LaneState(0) == 2);
        // switch: old red and new green in the same call (no all-red interval)
        c.Update(10000, kAlways);
        CHECK(c.LaneState(0) == 3 && c.LaneState(2) == 1 && c.LaneState(3) == 1);
        CHECK(c.VehicleLight(0) == 3 && c.PedestrianLight(0) == 2 && c.VehicleLight(2) == 1 && c.PedestrianLight(2) == 1);
        CHECK(c.CurrentPhase(0) == 1);
        c.Update(17001, kAlways);
        CHECK(c.LaneState(2) == 2);
        c.Update(20000, kAlways);  // wraps to phase 0; 20 s cycle
        CHECK(c.CurrentPhase(0) == 0 && c.LaneState(0) == 1 && c.LaneState(2) == 3);
    }
    {
        // no all-red at any 10 ms sample over three cycles
        TrafficSignalController c;
        c.Init(g, 0);
        for (int64_t t = 0; t <= 60000; t += 10) {
            c.Update(t, kAlways);
            bool any = false;
            for (size_t l = 0; l < 4; ++l) any = any || c.LaneState(l) != 3;
            CHECK(any);
        }
    }
    {
        // 15000 / 10000 (junction 187 class): phase 0 12 s green / 3 amber / 10 red; phase 1 7 / 3 / 15; cycle 25 s.
        sr3zone::RoadGraph h = Base(2);
        AddJunction(h, {{15000, 1, {0}}, {10000, 1, {1}}});
        TrafficSignalController c;
        c.Init(h, 0);
        for (int64_t t = 0; t < 25000; t += 100) {
            c.Update(t, kAlways);
            const int s0 = c.LaneState(0), s1 = c.LaneState(1);
            if (t <= 12000) CHECK(s0 == 1 && s1 == 3);
            else if (t < 15000) CHECK(s0 == 2 && s1 == 3);
            else if (t <= 22000) CHECK(s0 == 3 && s1 == 1);
            else CHECK(s0 == 3 && s1 == 2);
        }
        c.Update(25000, kAlways);
        CHECK(c.LaneState(0) == 1 && c.LaneState(1) == 3);
    }
    {
        // one phase (junction 26 class): 7 s green, 3 s amber, the switch writes 3 then 1 in one call: never red.
        sr3zone::RoadGraph h = Base(1);
        AddJunction(h, {{10000, 1, {0}}});
        TrafficSignalController c;
        c.Init(h, 0);
        bool sawAmber = false;
        for (int64_t t = 0; t <= 50000; t += 50) {
            c.Update(t, kAlways);
            CHECK(c.LaneState(0) != 3);
            sawAmber = sawAmber || c.LaneState(0) == 2;
        }
        CHECK(sawAmber);
    }
    {
        // 10000 / 0 (junction 162 class): phase 1 is left again on the next visit, never amber.
        sr3zone::RoadGraph h = Base(2);
        AddJunction(h, {{10000, 1, {0}}, {0, 1, {1}}});
        TrafficSignalController c;
        c.Init(h, 0);
        c.Update(10000, kAlways);
        CHECK(c.CurrentPhase(0) == 1 && c.LaneState(1) == 1 && c.LaneState(0) == 3);
        c.Update(10001, kAlways);
        CHECK(c.CurrentPhase(0) == 0 && c.LaneState(0) == 1 && c.LaneState(1) == 3);
    }
}

void TestLightlessAndResidency() {
    // lightless junction: no light in phase 0 -> every listed lane released by init, never ticks
    sr3zone::RoadGraph g = Base(3);
    AddJunction(g, {{10000, 0, {0, 1}}, {0, 0, {2}}});
    TrafficSignalController c;
    c.Init(g, 0);
    CHECK(c.LaneState(0) == 1 && c.LaneState(1) == 1 && c.LaneState(2) == 1);
    for (int64_t t = 0; t < 100000; t += 500) c.Update(t, kAlways);
    CHECK(c.LaneState(0) == 1 && c.LaneState(1) == 1 && c.LaneState(2) == 1 && c.CurrentPhase(0) == 0);

    // not resident: frozen; the deadline stays absolute; first visit after expiry switches once, re-arms from then
    sr3zone::RoadGraph h = Base(2);
    AddJunction(h, {{10000, 1, {0}}, {10000, 1, {1}}});
    TrafficSignalController d;
    d.Init(h, 0);
    for (int64_t t = 0; t < 33000; t += 100) d.Update(t, [](size_t) { return false; });
    CHECK(d.CurrentPhase(0) == 0 && d.LaneState(0) == 1);
    d.Update(33000, kAlways);
    CHECK(d.CurrentPhase(0) == 1 && d.LaneState(1) == 1);
    d.Update(42999, kAlways);
    CHECK(d.CurrentPhase(0) == 1 && d.LaneState(1) == 2);
    d.Update(43000, kAlways);
    CHECK(d.CurrentPhase(0) == 0);
}

void TestCursor() {
    // vector 10: five junctions A-E, B and D in amber
    sr3zone::RoadGraph g = Base(10);
    for (int i = 0; i < 5; ++i) {
        const uint16_t dur = (i == 1 || i == 3) ? 10000 : 60000;
        AddJunction(g, {{dur, 1, {static_cast<uint16_t>(2 * i)}}, {dur, 1, {static_cast<uint16_t>(2 * i + 1)}}});
    }
    TrafficSignalController c;
    c.Init(g, 0);
    const std::vector<std::vector<size_t>> expect = {{0, 1}, {2, 3}, {4}, {0, 1}, {2, 3}, {4}};
    for (const auto& e : expect) {
        c.Update(8000, kAlways);
        CHECK(c.LastTicked() == e);
    }
}

void TestStopRule() {
    const double aB = 12.06;
    CHECK(!SignalRuleStops(3, 10.0, 4.0, aB));  // vector 4: 12.5 > 12.06 -> drive through
    CHECK(SignalRuleStops(3, 10.0, 4.2, aB));   // vector 5: 11.90 <= 12.06 -> stop
    CHECK(!SignalRuleStops(2, 10.0, 4.0, aB));  // amber and red share the rule
    CHECK(SignalRuleStops(2, 10.0, 4.2, aB));
    CHECK(!SignalRuleStops(1, 10.0, 0.5, aB));  // green never stops
    CHECK(!SignalRuleStops(0, 10.0, 0.5, aB));
    CHECK(!SignalRuleStops(3, 15.0, 9.0, aB) && SignalRuleStops(3, 15.0, 10.0, aB));  // vector 7
    {
        const auto r = FollowingLaw(aB, 2, 4.2, 0.0, 10.0, false);  // full braking
        CHECK(Near(r.brake, aB) && Near(r.cap, 0.0));
        const auto s = FollowingLaw(aB, 2, 11.0, 0.0, 10.0, false);  // vector 6: dEff 6, demand 8.33, cap 9.49
        CHECK(Near(s.brake / aB, 0.691, 1e-3) && Near(s.cap, 9.49, 5e-3));
    }
    CHECK(SignalRuleStops(3, 0.0, 0.0, aB));  // d floored at 0.001: a stopped car stays stopped
}

void TestStopLine() {
    // vector 8: bit-7 lane, state 1 for ever; stop 3 m before the end, hold 1200 ms, then drive on
    const double aB = 12.06;
    StopLineMemory m;
    auto r = StopLineStep(m, 730, 30.0, 8.0, 16, aB, false);
    CHECK(r.active && r.brake == 0.0 && r.cap > 8.0);  // far: the kind-4 law only
    r = StopLineStep(m, 730, 3.5, 1.5, 16, aB, false);  // < 4.0 m and < 2.0 m/s: served, hold starts
    CHECK(r.active && Near(r.brake, 100.0) && Near(r.cap, 0.0) && m.servedSegment == 730);
    r = StopLineStep(m, 730, 3.0, 0.0, 600, aB, false);
    CHECK(r.active && Near(r.brake, 100.0));
    r = StopLineStep(m, 730, 3.0, 0.0, 599, aB, false);  // 1199 ms
    CHECK(r.active);
    r = StopLineStep(m, 730, 3.0, 0.0, 2, aB, false);  // 1201 ms: released, the junction yield decides
    CHECK(!r.active);
    r = StopLineStep(m, 730, 2.0, 0.0, 16, aB, false);  // served entries are skipped
    CHECK(!r.active);
    StopLineMemory m2;  // fast approach inside 4 m does not arm
    r = StopLineStep(m2, 5, 3.5, 5.0, 16, aB, false);
    CHECK(r.active && r.brake > 0.0 && m2.servedSegment == -1);
}

void TestHoldRelease() {
    sr3zone::RoadGraph g = Base(2);
    AddJunction(g, {{10000, 1, {0}}, {10000, 1, {1}}});
    TrafficSignalController c;
    c.Init(g, 0);
    c.Hold(0, 1000);  // all-red until released, timer disarmed
    CHECK(c.LaneState(0) == 3 && c.LaneState(1) == 3 && c.Deadline(0) == -1);
    c.Update(500000, kAlways);
    CHECK(c.LaneState(0) == 3 && c.LaneState(1) == 3);
    c.Release(0, 600000, kAlways);  // switch to the next phase at once
    CHECK(c.CurrentPhase(0) == 1 && c.LaneState(1) == 1 && c.LaneState(0) == 3);
}

// Kinematic approach with the same composition the viewer uses: throttle law, then the kind-2 source when
// SignalRuleStops says stop. A car 60 m from the lane end at the 10 m/s limit meets a red lane: it must stop short
// of the node (more than 0 m, at most the 5 m stand-off plus the overshoot of the law), then drive off on green.
void TestApproachSim() {
    sr3zone::RoadGraph g = Base(2);
    AddJunction(g, {{10000, 1, {0}}, {10000, 1, {1}}});
    TrafficSignalController c;
    c.Init(g, 0);
    const double aB = 8.0, dt = 1.0 / 30.0;
    double dEnd = 60.0, v = 10.0;
    int64_t now = 11000;  // phase 1 green for lane 1; lane 0 is red until 20000
    c.Update(now, kAlways);
    for (int i = 0; i < 400; ++i) c.Update(now, kAlways);  // switch to phase 1 happened at 10000 (deadline 0 + 10000)
    CHECK(c.LaneState(0) == 3);
    bool stopped = false;
    int64_t stopT = 0;
    for (; now < 20500; now += 33) {
        c.Update(now, kAlways);
        v += 3.0 * AmbientThrottleLaw(10.0, v, false) * dt;
        v = std::min(v, 10.0);
        if (SignalRuleStops(c.LaneState(0), v, dEnd, aB)) {
            const auto fr = FollowingLaw(aB, 2, dEnd, 0.0, v, false);
            if (fr.brake > 0.0) v = std::min(v, std::max(0.0, v - fr.brake * dt));
            v = std::min(v, fr.cap);
        }
        dEnd -= v * dt;
        if (v < 0.01 && !stopped) {
            stopped = true;
            stopT = now;
        }
        CHECK(dEnd > 0.0);
    }
    CHECK(stopped);
    CHECK(dEnd > 0.0 && dEnd < 6.0);  // waiting short of the end node, near the 5 m line
    CHECK(v < 0.01);
    // green for lane 0 from 21000 (phase 1 was entered at 11000): the car releases.
    for (int i = 0; i < 400; ++i) c.Update(21000, kAlways);
    CHECK(c.LaneState(0) == 1);
    for (int step = 0; step < 90; ++step) {
        v += 3.0 * AmbientThrottleLaw(10.0, v, false) * dt;
        if (SignalRuleStops(c.LaneState(0), v, dEnd, aB)) v = 0.0;
        dEnd -= v * dt;
    }
    CHECK(v > 5.0 && dEnd < 0.0);
}

void TestCrossing() {
    sr3zone::RoadGraph g = Base(2);
    AddJunction(g, {{10000, 1, {0}}, {10000, 1, {1}}});
    g.crossingLight = {-1, 0, 1};
    TrafficSignalController c;
    c.Init(g, 0);
    c.Update(0, kAlways);
    CHECK(!c.CrossingClosed(0));  // no light: always open
    CHECK(!c.CrossingClosed(1));  // light 0: vehicle 1, pedestrian 1 -> open
    CHECK(c.CrossingClosed(2));   // light 1: vehicle 3, pedestrian 2 -> closed
    c.Update(7001, kAlways);      // amber: vehicle byte 2 closes it though the pedestrian byte stays 1
    CHECK(c.CrossingClosed(1));
    c.Update(10000, kAlways);
    CHECK(!c.CrossingClosed(2) && c.CrossingClosed(1));
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
    CHECK(g.junctions.size() == 346);
    {
        TrafficSignalController c;  // vector 1: t = 0
        c.Init(g, 0);
        c.Update(0, kAlways);
        CHECK(c.LaneState(218) == 1 && c.LaneState(54) == 1 && c.LaneState(192) == 3 && c.LaneState(191) == 3);
        for (size_t l = 936; l <= 939; ++l) CHECK(c.VehicleLight(l) == 1 && c.PedestrianLight(l) == 1);
        CHECK(c.VehicleLight(954) == 3 && c.PedestrianLight(954) == 2);
        CHECK(g.crossingLight.size() > 10 && g.crossingLight[10] == 954);
        CHECK(c.CrossingClosed(10));
        // lightless junctions are released: only lanes of later, lit phases of lit junctions stay 3
        size_t red = 0;
        for (size_t s = 0; s < g.segments.size(); ++s) red += c.LaneState(s) == 3;
        size_t expectRed = 0;
        for (size_t j = 0; j < g.junctions.size(); ++j) {
            const size_t np = g.PhaseCount(j);
            for (size_t p = 1; p < np; ++p) {
                const size_t gp = g.junctions[j].firstPhase + p;
                if (g.PhaseLightCount(gp) == 0) continue;
                expectRed += g.PhaseLaneCount(gp);
            }
        }
        CHECK(red == expectRed);
        for (int i = 0; i < 400; ++i) c.Update(7500, kAlways);  // vector 2: every junction visited
        CHECK(c.LaneState(218) == 2 && c.LaneState(54) == 2 && c.LaneState(192) == 3);
        for (size_t l = 936; l <= 939; ++l) CHECK(c.VehicleLight(l) == 2 && c.PedestrianLight(l) == 1);
        CHECK(c.CrossingClosed(10));
    }
    {
        TrafficSignalController c;  // vector 3: first visit at t >= 10000
        c.Init(g, 0);
        for (int i = 0; i < 400; ++i) c.Update(10000, kAlways);
        CHECK(c.LaneState(218) == 3 && c.LaneState(54) == 3 && c.LaneState(192) == 1 && c.LaneState(191) == 1);
        CHECK(c.VehicleLight(954) == 1 && c.PedestrianLight(954) == 1);
        CHECK(!c.CrossingClosed(10));
        for (int i = 0; i < 400; ++i) c.Update(17001, kAlways);
        CHECK(c.CrossingClosed(10));  // amber of the phase at 17,000
        for (int i = 0; i < 400; ++i) c.Update(20000, kAlways);
        CHECK(c.LaneState(218) == 1 && c.LaneState(192) == 3);
    }
}

}  // namespace

int main() {
    TestTiming();
    TestLightlessAndResidency();
    TestCursor();
    TestStopRule();
    TestStopLine();
    TestCrossing();
    TestApproachSim();
    TestHoldRelease();
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
    if (g_failures == 0) std::cout << "traffic_signals_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
