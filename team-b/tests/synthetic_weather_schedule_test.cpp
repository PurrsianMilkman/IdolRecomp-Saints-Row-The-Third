// Unit tests for sr3render::WeatherScheduler / BuildWeatherStageInfo / ResolveLightingStage
// (include/sr3render/weather_schedule.h; spec-tables-environment.md Sec2/Sec18). Synthetic stage graph only.
#include <cmath>
#include <iostream>
#include <set>

#include "sr3render/tod_lighting.h"
#include "sr3render/weather_schedule.h"

namespace {
using namespace sr3render;
int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

sr3tables_environment::WeatherStage Row(const char* name, float chance, float avg, float var, float ped,
                                        std::vector<std::string> next) {
    sr3tables_environment::WeatherStage r;
    r.name = name;
    r.chance.value = chance;
    r.averageDuration.value = avg;
    r.variance.value = var;
    r.pedDensity.value = ped;
    r.nextStageNames = std::move(next);
    return r;
}

std::vector<WeatherStageInfo> Graph() {
    // Shape of the shipped graph (names/edges from the spec); weights and durations are synthetic.
    return BuildWeatherStageInfo({Row("Clear Skies", 60, 100, 0.2f, 1.0f, {"Overcast"}),
                                  Row("Overcast", 20, 50, 0.2f, 1.0f, {"light rain", "CLEAR SKIES", "Heavy Rain"}),
                                  Row("Light Rain", 10, 40, 0.5f, 0.5f, {"Heavy Rain", "Post-Storm"}),
                                  Row("Heavy Rain", 5, 30, 0.1f, 0.1f, {"Post-Storm", "Nowhere"}),
                                  Row("Post-Storm", 5, 30, 0.1f, 0.75f, {"Clear Skies"})});
}

void TestBuild() {
    auto g = Graph();
    float sum = 0;
    for (auto& s : g) sum += s.chance;
    CHECK(std::fabs(sum - 1.0f) < 1e-6f);
    CHECK(std::fabs(g[0].chance - 0.6f) < 1e-6f);
    CHECK(g[1].next.size() == 3 && g[1].next[0] == 2 && g[1].next[1] == 0 && g[1].next[2] == 3);  // case-insensitive
    CHECK(g[3].next.size() == 1 && g[3].next[0] == 4);                                             // unmatched dropped
}

void TestDeterminismAndPin() {
    WeatherScheduler a(Graph(), 7), b(Graph(), 7), c(Graph(), 8);
    bool same = true, diff = false;
    for (int i = 0; i < 5000; ++i) {
        a.Tick(1.0);
        b.Tick(1.0);
        c.Tick(1.0);
        same = same && a.Current().currentStage == b.Current().currentStage && a.Current().elapsed == b.Current().elapsed;
        diff = diff || a.Current().currentStage != c.Current().currentStage;
    }
    CHECK(same);
    CHECK(diff);
    WeatherScheduler q(Graph(), 7);
    CHECK(!q.Pin("No Such Stage"));
    CHECK(q.Pin("heavy rain"));
    int changes = q.Tick(100000.0);
    CHECK(changes == 0);
    CHECK(q.Current().currentStage == 3 && q.Current().pinned && q.Current().blend == 0.0f);
    CHECK(q.PedDensity(q.Current().currentStage) == 0.1f);
}

void TestStageChangesFollowEdges() {
    auto g = Graph();
    WeatherScheduler s(g, 1234);
    std::set<int> seen{s.Current().currentStage};
    int changes = 0;
    for (int i = 0; i < 400000; ++i) {
        s.Tick(0.5, [&](int from, int to) {
            bool edge = false;  // every change must be along a Next_Stage edge
            for (int n : g[from].next) edge = edge || n == to;
            CHECK(edge);
            CHECK(from != to);
            ++changes;
        });
        const auto st = s.Current();
        seen.insert(st.currentStage);
        CHECK(st.blend >= 0.0f && st.blend <= 0.5f + 1e-6f);
        CHECK(st.elapsed < st.duration + 1e-9);
    }
    CHECK(changes > 20);
    CHECK(seen.size() >= 4);
}

void TestDurationRange() {
    WeatherScheduler s(Graph(), 99);
    for (int stage = 0; stage < 5; ++stage) {
        const auto& si = s.Stages()[stage];
        for (int i = 0; i < 200; ++i) {
            const double d = s.RandomDurationForTest(stage);
            CHECK(d >= si.averageDuration - si.variance * si.averageDuration - 1e-3);
            CHECK(d <= si.averageDuration + si.variance * si.averageDuration + 1e-3);
        }
    }
}

void TestWeightedPick() {
    WeatherScheduler s(Graph(), 5);
    int counts[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 20000; ++i) ++counts[s.PickWeightedStageForTest()];
    CHECK(counts[0] > counts[1] && counts[1] > counts[2] && counts[2] > counts[3]);
    CHECK(counts[0] > 11000 && counts[0] < 13000);  // ~60%
}

void TestBlendContinuity() {
    // The blend weight toward the neighbour is 0.5 at both boundaries of an entry -> continuous across the advance.
    WeatherScheduler s(Graph(), 31);
    int checked = 0;
    for (int i = 0; i < 2000000 && checked < 5; ++i) {
        const auto before = s.Current();
        if (s.Tick(0.01) > 0) {
            const auto after = s.Current();
            CHECK(before.stageB == after.stageA);
            CHECK(after.stageB == before.stageA);
            CHECK(std::fabs(before.blend - 0.5f) < 0.02f);
            CHECK(std::fabs(after.blend - 0.5f) < 0.02f);
            ++checked;
        }
    }
    CHECK(checked >= 3);
    CHECK(std::fabs(WeatherSmooth(0.5f) - 0.5f) < 1e-6f && WeatherSmooth(0.0f) == 0.0f && WeatherSmooth(1.0f) == 1.0f);
    CHECK(std::fabs(WeatherSmooth(0.25f) - 0.125f) < 1e-6f);  // 2t^2 (WS-5)
    CHECK(std::fabs(WeatherSmooth(0.75f) - 0.875f) < 1e-6f);  // 1 - 2(1-t)^2
}

sr3tables_environment::WeatherTimeSegment Seg(int start, int ramp, std::vector<std::pair<const char*, float>> cells) {
    sr3tables_environment::WeatherTimeSegment s;
    s.startTimeHHMM.value = start;
    s.rampOutTimeMinutes.value = ramp;
    for (auto& c : cells) {
        sr3tables_environment::WeatherTimeOfDayCell cell;
        cell.stageName = c.first;
        cell.lighting.ambientColor.present = true;
        cell.lighting.ambientColor.rgb.r = {c.second, true};
        cell.lighting.ambientColor.rgb.g = {c.second, true};
        cell.lighting.ambientColor.rgb.b = {c.second, true};
        s.stages.push_back(cell);
    }
    return s;
}

void TestLightingResolveAndBlend() {
    auto g = Graph();
    std::vector<sr3tables_environment::WeatherTimeSegment> segs{
        Seg(600, 60, {{"Overcast", 0.2f}, {"Light Rain", 0.4f}, {"Heavy Rain", 0.1f}, {"Post-Storm", 0.3f}}),
        Seg(1800, 60, {{"Overcast", 0.2f}, {"Light Rain", 0.4f}, {"Heavy Rain", 0.1f}, {"Post-Storm", 0.3f}})};
    CHECK(StageHasCellsInAllSegments(segs, "overcast"));
    CHECK(!StageHasCellsInAllSegments(segs, "Clear Skies"));
    CHECK(ResolveLightingStage(g, 0, segs) == "Overcast");  // CHOSEN: Clear Skies -> its Next_Stage that has cells
    CHECK(ResolveLightingStage(g, 2, segs) == "Light Rain");
    CHECK(ResolveLightingStage(g, 9, segs).empty());
    auto lo = ComputeTodLightingBlended(segs, 0.5f, "Overcast", "Light Rain", 0.0f);
    auto hi = ComputeTodLightingBlended(segs, 0.5f, "Overcast", "Light Rain", 1.0f);
    auto mid = ComputeTodLightingBlended(segs, 0.5f, "Overcast", "Light Rain", 0.5f);
    CHECK(std::fabs(lo.ambientColor[0] - 0.2f) < 1e-4f);
    CHECK(std::fabs(hi.ambientColor[0] - 0.4f) < 1e-4f);
    CHECK(std::fabs(mid.ambientColor[0] - 0.3f) < 1e-4f);
    auto def = ComputeTodLightingBlended(segs, 0.5f, "Clear Skies", "Clear Skies", 0.0f);  // missing -> old defaults
    CHECK(def.ambientColor[0] == 0.35f);
}
}  // namespace

int main() {
    TestBuild();
    TestDeterminismAndPin();
    TestStageChangesFollowEdges();
    TestDurationRange();
    TestWeightedPick();
    TestBlendContinuity();
    TestLightingResolveAndBlend();
    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
