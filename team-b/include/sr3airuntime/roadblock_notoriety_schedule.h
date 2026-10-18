#pragma once

// sr3airuntime/roadblock_notoriety_schedule.h - the notoriety-driven roadblock
// SCHEDULE of spec-ai-runtime.md Sec20.6.10 (a) and (c), FINAL: when a
// roadblock placement is due, as a pure object. WHERE/WHAT to place stays the
// caller's job (police_roadblock_spawn.h's slot table, a CHOSEN stand-in:
// the spec's layout rows `roadblock_layouts.xtbl` are not wired).
//
// Implemented (FINAL): the gates (roadblocks enabled, live roadblock count
// < 3, the target player is driving), the per-slot level gate (1 <= L <= 5),
// "level 1 never spawns" (shipped rows exist for levels 2-5 only), the
// Max_active_roadblocks cap, the per-(slot, level) timer (not armed -> arm
// with a uniform Min_time..Max_time and stop; armed and expired -> place),
// the success re-arm and the 2000 ms retry after a failed placement. Only the
// POLICE track's table (slot 3) is encoded; the gang rows differ (see spec
// table) and no gang notoriety source exists in the viewer.
//
// Not modelled: "on a road segment without the water flag" (the viewer has no
// per-frame player road-segment query; callers pass `playerDriving` only),
// layout cooldowns, the suppression object, co-op target selection.

#include <array>
#include <cstdint>
#include <functional>

#include "sr3airuntime/common.h"

namespace sr3airuntime {

struct RoadblockLevelRow {
    int32_t maxActive;  // Max_active_roadblocks
    int32_t minSeconds;
    int32_t maxSeconds;
};

// Sec20.6.10(c) shipped schedule, Police: level 2: 1 / 15-40, 3: 2 / 10-30,
// 4: 2 / 5-20, 5: 3 / 0-10. Levels 0 and 1 have no row.
inline constexpr std::array<RoadblockLevelRow, 6> kPoliceRoadblockRows = {{
    {0, 0, 0},
    {0, 0, 0},
    {1, 15, 40},
    {2, 10, 30},
    {2, 5, 20},
    {3, 0, 10},
}};

class NotorietyRoadblockScheduler {
public:
    static constexpr int32_t kMaxLiveRoadblocks = 3;   // 0x012ed17c
    static constexpr int64_t kRetryAfterFailureMs = 2000;

    bool enabled = true;  // roadblock cvar (0x0092b2b0)

    // Uniform integer in [lo, hi]; replaceable for deterministic tests.
    std::function<int64_t(int64_t lo, int64_t hi)> uniform;

    struct Decision {
        bool place = false;  // a placement attempt is due this frame
        int32_t level = 0;
    };

    static bool HasRow(int32_t level) {
        return level >= 1 && level <= 5 && kPoliceRoadblockRows[static_cast<size_t>(level)].maxActive > 0;
    }

    Decision Update(int64_t nowMs, int32_t level, bool playerDriving, int32_t liveRoadblocks) {
        Decision d;
        if (!enabled || !playerDriving || liveRoadblocks >= kMaxLiveRoadblocks) return d;
        if (!HasRow(level)) return d;
        const RoadblockLevelRow& row = kPoliceRoadblockRows[static_cast<size_t>(level)];
        if (liveRoadblocks >= row.maxActive) return d;
        EngineTimer& t = timers_[static_cast<size_t>(level)];
        if (!t.isArmed()) {
            t.arm(nowMs, Interval(row));
            return d;
        }
        if (t.expired(nowMs)) {
            d.place = true;
            d.level = level;
        }
        return d;
    }

    // Report the outcome of a placement attempt returned by Update().
    void ReportPlacement(int32_t level, bool success, int64_t nowMs) {
        if (level < 1 || level > 5) return;
        const RoadblockLevelRow& row = kPoliceRoadblockRows[static_cast<size_t>(level)];
        timers_[static_cast<size_t>(level)].arm(nowMs, success ? Interval(row) : kRetryAfterFailureMs);
    }

    bool timerArmed(int32_t level) const { return timers_[static_cast<size_t>(level)].isArmed(); }
    int64_t timerRemainingMs(int32_t level, int64_t nowMs) const {
        return timers_[static_cast<size_t>(level)].remainingMs(nowMs);
    }

private:
    std::array<EngineTimer, 6> timers_;
    uint64_t rng_ = 0x9E3779B97F4A7C15ull;

    int64_t Interval(const RoadblockLevelRow& row) {
        const int64_t lo = static_cast<int64_t>(row.minSeconds) * 1000;
        const int64_t hi = static_cast<int64_t>(row.maxSeconds) * 1000;
        if (uniform) return uniform(lo, hi);
        rng_ = rng_ * 6364136223846793005ull + 1442695040888963407ull;  // CHOSEN default generator
        return hi <= lo ? lo : lo + static_cast<int64_t>((rng_ >> 33) % static_cast<uint64_t>(hi - lo + 1));
    }
};

}  // namespace sr3airuntime
