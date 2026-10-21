#pragma once

// Weather-stage scheduler: the runtime state machine behind weather.xtbl
// (Chance / Average_Duration / Variance / Next_Stage_List) that the playtest
// previously replaced by a fixed stage ("Light Rain" lighting, "Clear Skies"
// ped density). Pure logic, no D3D, no tables I/O: BuildWeatherStageInfo()
// is the only function that touches sr3tables_environment::WeatherStage.
//
// SOURCES (spec-*.md only; nothing here is derived from the executable):
//   spec-tables-environment.md Sec2   : Chance is a probability weight normalised over all stages;
//                                       Next_Stage names resolve case-insensitively, unmatched -> NULL.
//   spec-tables-environment.md Sec18  : the weather schedule is a weighted-Chance stage picker plus a
//                                       schedule-entry array plus a per-tick duration jitter "avg +/-
//                                       variance*avg", "structurally parallel" to the wind state machine.
//   spec-tables-environment.md Sec4.1 (wind transition mechanism, HIGH CONFIDENCE) - the weather machine
//                                       is stated to reuse the identical primitives, so the wind rules
//                                       are applied here BY ANALOGY (labelled below).
//   spec-ai-runtime.md Sec20.6.0(d)   : ped density = current stage's PedDensity.
//
// SPEC-STATED (implemented as stated):
//   * Chance normalised over all stages; the next TARGET stage is a Chance-weighted pick.
//   * Duration of a stage entry = random in [avg - var*avg, avg + var*avg] seconds
//     (Average_Duration, Variance as a FRACTION of the average; "seconds" is the wind unit, by analogy).
//   * The schedule is a list of (stage, duration) entries; the clock advances through them; when the
//     elapsed time passes the current entry's duration the schedule advances and the clock is rewound
//     by that duration. At game start the first picked stage fills entries 0 and 1, current = 1, and the
//     elapsed time is primed to +0.48 * r * duration (WS-3; the engine arms the timer with the negative of that,
//     so the first stage is shorter).
//   * A new target is reached through a shortest-path search over Next_Stage_List edges with edge cost
//     |Chance(u) - Chance(v)| * duration(v); every stage first gets a random duration; the path's stages
//     (excluding the start) are appended. Bounded by the stage count.
//   * Blend: elapsed/duration shifted by +/-0.5 and clamped to [0,1], then smoothed. A stage is therefore
//     "pure" at the middle of its entry, and each half blends toward the previous / next entry's stage,
//     reaching a 0.5 weight exactly at the boundary (continuous across the advance).
//   * A forced stage replaces the schedule with that stage twice (this is --weather=<stage>).
//
// CHOSEN (NOT spec-stated; each is also reported as a Team A question):
//   * The random generator (a fixed-seed splitmix32; default seed kDefaultWeatherSeed) - the spec names
//     the picker but not its RNG, so the game's own random stream cannot be matched.
//   * (WS-5 resolved: the smoothing primitive is the engine ease S(t) = 2t^2 / 1-2(1-t)^2, HIGH, no longer CHOSEN.)
//   * Time base: the scheduler clock is REAL (unscaled, unpaused) sim seconds, as the wind clock is. The
//     spec does not say whether the weather clock follows game time (40x) or real time.
//   * Picking the same stage as the current one is re-rolled (up to 8 times) so the schedule always
//     moves; if a target is unreachable over the Next_Stage graph it is appended directly.
//   * "Current stage" for the consumers (ped density) = the stage of the current schedule entry.
//   * A lighting stand-in for stages with no weather_time_of_day cells: see ResolveLightingStage().

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <string>
#include <vector>

#include "sr3tables_environment/tables.h"
#include "sr3tables_environment/weather_time_of_day.h"

namespace sr3render {

constexpr uint32_t kDefaultWeatherSeed = 0x5EEDu;  // CHOSEN

struct WeatherStageInfo {
    std::string name;
    float chance = 0.0f;  // NORMALISED probability weight (sums to 1 over the table)
    float averageDuration = 60.0f;
    float variance = 0.0f;
    float pedDensity = 1.0f;
    std::vector<int> next;  // Next_Stage_List resolved to stage indices; unmatched names dropped
};

inline bool WeatherNameEqualsCi(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Sec2 load procedure: normalise Chance over the table, resolve Next_Stage by case-insensitive name.
// A zero Chance sum is left as raw zeros (the spec notes there is no guard; the picker then falls back
// to a uniform pick, see PickWeightedStage).
inline std::vector<WeatherStageInfo> BuildWeatherStageInfo(const std::vector<sr3tables_environment::WeatherStage>& rows) {
    std::vector<WeatherStageInfo> out(rows.size());
    float sum = 0.0f;
    for (const auto& r : rows) sum += r.chance.value;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        out[i].name = r.name ? *r.name : std::string();
        out[i].chance = sum > 0.0f ? r.chance.value / sum : r.chance.value;
        out[i].averageDuration = r.averageDuration.value;
        out[i].variance = r.variance.value;
        out[i].pedDensity = r.pedDensity.value;
    }
    for (size_t i = 0; i < rows.size(); ++i)
        for (const std::string& nm : rows[i].nextStageNames)
            for (size_t j = 0; j < out.size(); ++j)
                if (WeatherNameEqualsCi(out[j].name, nm)) {
                    out[i].next.push_back(static_cast<int>(j));
                    break;
                }
    return out;
}

// Engine ease 0x00DAD960(t, 1.0, 0.5) (WS-5, HIGH): S(t) = 2t^2 for t <= 0.5, 1 - 2(1-t)^2 above.
// b in [0, 0.5] -> weight of the neighbouring stage.
inline float WeatherSmooth(float b) {
    b = std::min(1.0f, std::max(0.0f, b));
    return b <= 0.5f ? 2.0f * b * b : 1.0f - 2.0f * (1.0f - b) * (1.0f - b);
}

class WeatherScheduler {
public:
    struct State {
        int currentStage = -1;  // stage of the current schedule entry (consumers: ped density)
        int stageA = -1;        // blend corner A (== currentStage)
        int stageB = -1;        // blend corner B (neighbouring entry's stage; == A when pure)
        float blend = 0.0f;     // weight of B in [0, 0.5]
        double elapsed = 0.0;   // seconds into the current entry (negative while primed)
        double duration = 0.0;  // current entry duration (seconds)
        bool pinned = false;
    };

    WeatherScheduler(std::vector<WeatherStageInfo> stages, uint32_t seed = kDefaultWeatherSeed)
        : stages_(std::move(stages)), rng_(seed) {
        StartSchedule(-1);
    }

    // Forced stage (--weather=<stage>): the schedule becomes that stage twice and never advances.
    bool Pin(const std::string& name) {
        for (size_t i = 0; i < stages_.size(); ++i)
            if (WeatherNameEqualsCi(stages_[i].name, name)) {
                pinned_ = true;
                entries_.clear();
                const double d = RandomDuration(static_cast<int>(i));
                entries_.push_back({static_cast<int>(i), d});
                entries_.push_back({static_cast<int>(i), d});
                cur_ = 1;
                elapsed_ = 0.0;
                return true;
            }
        return false;
    }

    // Advances by dt seconds; returns the number of stage changes (current entry's stage differing from
    // the previous entry's). `onChange(fromStage, toStage)` is invoked per change when provided.
    template <class F>
    int Tick(double dt, F&& onChange) {
        if (stages_.empty() || pinned_ || dt <= 0.0) return 0;
        elapsed_ += dt;
        int changes = 0;
        for (int guard = 0; guard < 1024; ++guard) {
            if (elapsed_ < entries_[cur_].duration) break;
            elapsed_ -= entries_[cur_].duration;
            const int from = entries_[cur_].stage;
            ++cur_;
            if (cur_ + 1 >= static_cast<int>(entries_.size())) PlanNext();
            if (entries_[cur_].stage != from) {
                ++changes;
                onChange(from, entries_[cur_].stage);
            }
            // keep one entry of history for the blend; drop older ones
            while (cur_ > 1) {
                entries_.pop_front();
                --cur_;
            }
        }
        return changes;
    }
    int Tick(double dt) {
        return Tick(dt, [](int, int) {});
    }

    State Current() const {
        State s;
        if (stages_.empty() || entries_.empty()) return s;
        s.pinned = pinned_;
        s.currentStage = s.stageA = entries_[cur_].stage;
        s.stageB = s.stageA;
        s.elapsed = elapsed_;
        s.duration = entries_[cur_].duration;
        if (pinned_) return s;
        const double f = std::min(1.0, std::max(0.0, elapsed_ / entries_[cur_].duration));
        float b = 0.0f;
        int other = cur_;
        if (f < 0.5) {
            other = cur_ - 1;
            b = static_cast<float>(0.5 - f);
        } else {
            other = cur_ + 1;
            b = static_cast<float>(f - 0.5);
        }
        if (other >= 0 && other < static_cast<int>(entries_.size())) {
            s.stageB = entries_[other].stage;
            s.blend = (s.stageB == s.stageA) ? 0.0f : WeatherSmooth(b);
        }
        return s;
    }

    const std::vector<WeatherStageInfo>& Stages() const { return stages_; }
    const std::string& StageName(int i) const {
        static const std::string kNone = "(none)";
        return (i >= 0 && i < static_cast<int>(stages_.size())) ? stages_[i].name : kNone;
    }
    float PedDensity(int i) const {
        return (i >= 0 && i < static_cast<int>(stages_.size())) ? stages_[i].pedDensity : 1.0f;
    }
    size_t EntryCount() const { return entries_.size(); }

    // Exposed for tests.
    double RandomDurationForTest(int stage) { return RandomDuration(stage); }
    int PickWeightedStageForTest() { return PickWeightedStage(); }

private:
    struct Entry {
        int stage;
        double duration;
    };

    float Rand01() {
        rng_ += 0x9E3779B9u;
        uint32_t z = rng_;
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        z ^= z >> 16;
        return static_cast<float>(z >> 8) / 16777216.0f;  // [0,1)
    }

    double RandomDuration(int stage) {
        const WeatherStageInfo& s = stages_[stage];
        const float lo = s.averageDuration - s.variance * s.averageDuration;
        const float hi = s.averageDuration + s.variance * s.averageDuration;
        const double d = lo + (hi - lo) * static_cast<double>(Rand01());
        return std::max(0.001, d);
    }

    int PickWeightedStage() {
        float total = 0.0f;
        for (const auto& s : stages_) total += std::max(0.0f, s.chance);
        if (total <= 0.0f) return static_cast<int>(Rand01() * static_cast<float>(stages_.size())) % static_cast<int>(stages_.size());
        float r = Rand01() * total;
        for (size_t i = 0; i < stages_.size(); ++i) {
            r -= std::max(0.0f, stages_[i].chance);
            if (r < 0.0f) return static_cast<int>(i);
        }
        return static_cast<int>(stages_.size()) - 1;
    }

    void StartSchedule(int forced) {
        entries_.clear();
        cur_ = 1;
        elapsed_ = 0.0;
        if (stages_.empty()) return;
        const int s = forced >= 0 ? forced : PickWeightedStage();
        const double d = RandomDuration(s);
        entries_.push_back({s, d});
        entries_.push_back({s, d});
        // WS-3: the engine arms the entry timer with -(r*d*0.48*1000) ms (0.48 = double at 0x0111EAF0), so the ELAPSED
        // time starts at +0.48*r*d and the first stage is SHORTER (spec Sec4.1 "negative" is the arm argument).
        elapsed_ = 0.48 * static_cast<double>(Rand01()) * d;
        PlanNext();
    }

    // Picks a target and appends the shortest path from the last entry's stage to it.
    void PlanNext() {
        const int n = static_cast<int>(stages_.size());
        const int start = entries_.back().stage;
        int target = PickWeightedStage();
        for (int tries = 0; tries < 8 && target == start && n > 1; ++tries) target = PickWeightedStage();
        std::vector<double> dur(n);
        for (int i = 0; i < n; ++i) dur[i] = RandomDuration(i);
        std::vector<double> cost(n, std::numeric_limits<double>::infinity());
        std::vector<int> prev(n, -1);
        std::vector<bool> done(n, false);
        cost[start] = 0.0;
        for (int iter = 0; iter < n; ++iter) {
            int u = -1;
            for (int i = 0; i < n; ++i)
                if (!done[i] && (u < 0 || cost[i] < cost[u])) u = i;
            if (u < 0 || cost[u] == std::numeric_limits<double>::infinity()) break;
            done[u] = true;
            for (int v : stages_[u].next) {
                const double c = cost[u] + std::fabs(static_cast<double>(stages_[u].chance) - stages_[v].chance) * dur[v];
                if (c < cost[v]) {
                    cost[v] = c;
                    prev[v] = u;
                }
            }
        }
        std::vector<int> path;
        if (target != start && prev[target] >= 0) {
            for (int v = target; v != start && v >= 0 && static_cast<int>(path.size()) <= n; v = prev[v]) path.push_back(v);
            std::reverse(path.begin(), path.end());
        } else {
            path.push_back(target);  // CHOSEN: unreachable (or same-stage) -> append the target directly
        }
        for (int v : path) entries_.push_back({v, dur[v]});
    }

    std::vector<WeatherStageInfo> stages_;
    std::deque<Entry> entries_;
    int cur_ = 1;
    double elapsed_ = 0.0;
    bool pinned_ = false;
    uint32_t rng_;
};

// ---------------------------------------------------------------------------
// Lighting stage for a weather stage that has no weather_time_of_day cells.
//
// SPEC: in the shipped data "Clear Skies" never appears as a Stage_Name in any segment, so its cell is
// entirely absent (all presence bits clear = "inherit", Sec3.3/Sec17). The spec does NOT say what it
// inherits from (the cell is a sparse override layer; the base layer is not identified).
// CHOSEN stand-in: use the cells of the first Next_Stage (following the chain, bounded) that has cells
// in every segment - Clear Skies -> Overcast, its only successor in the shipped graph. If none is found
// the stage name is returned unchanged (the lighting code then falls back to its old fixed defaults).
// ---------------------------------------------------------------------------
inline bool StageHasCellsInAllSegments(const std::vector<sr3tables_environment::WeatherTimeSegment>& segs,
                                       const std::string& stageName) {
    if (segs.empty()) return false;
    for (const auto& seg : segs) {
        bool found = false;
        for (const auto& c : seg.stages)
            if (c.stageName && WeatherNameEqualsCi(*c.stageName, stageName)) found = true;
        if (!found) return false;
    }
    return true;
}

inline std::string ResolveLightingStage(const std::vector<WeatherStageInfo>& stages, int stage,
                                        const std::vector<sr3tables_environment::WeatherTimeSegment>& segs) {
    if (stage < 0 || stage >= static_cast<int>(stages.size())) return std::string();
    int s = stage;
    for (size_t hop = 0; hop <= stages.size(); ++hop) {
        if (StageHasCellsInAllSegments(segs, stages[s].name)) return stages[s].name;
        if (stages[s].next.empty()) break;
        s = stages[s].next.front();
    }
    return stages[stage].name;
}

}  // namespace sr3render
