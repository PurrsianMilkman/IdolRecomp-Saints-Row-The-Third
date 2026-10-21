// sr3airuntime/abandoned_vehicle_schedule.h - the abandoned-car schedule 0x0090eaf0 and its picker 0x00908cf0
// (spec-ai-runtime.md Sec20.6.14(c)/(d), FINAL; conformance finding EC-6). Pure logic, no engine state.
//
// FINAL (spec): step 2 of the maintenance pass, run when its own timer (400-600 ms) expires.
//   1. Collect (<= 20): occupied vehicles reset their entry timer and are skipped (the caller does that); only
//      teams 0,1,2,3,5,6 are kept (the caller filters); the local player's LAST vehicle is skipped unless sunk in
//      a non-watercraft (the caller filters; sunk is not modelled by this host).
//   2. Count gate: n = candidates. n < min(in[0].num_cars, out[0].num_cars) -> global timer reset to -1, stop.
//      Otherwise the global timer starts if not running; E = its age.
//   3. Out of view first (rows {5,10 s},{6,5 s},{7,0 s}): any row with n >= num_cars and E >= time -> picker with
//      out_of_view = 1, delete the pick at once; a failed pick falls through to the in-view rows.
//   4. In view (rows {5,30 s},{6,20 s},{7,10 s},{8,0 s}): the in-view gate timer must have run for at least
//      abandon_despawn_delay (10 s) or be unset; any row with n >= num_cars and E >= time -> picker with
//      out_of_view = 0, the pick FADES over 1000 ms, the gate timer restarts (<= one in-view removal per 10 s).
//   5. Picker: qualifies when its own age >= abandon_despawn_delay, the eligibility predicate 0x00906b00 holds, and
//      (out-of-view picks only) it is not visible; returns the candidate FARTHEST from the players.
//
// CHOSEN: none in the schedule itself. The caller supplies `eligible` (gates 1-7 of Sec20.6.14(c)), `visible` (the
// previous frame's frustum test, the engine's occlusion test is not modelled) and `distance`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace sr3airuntime {

struct AbandonedRow {
    int numCars;
    int64_t timeMs;
};

// Shipped vehicle_despawn.xtbl rows (Sec20.6.14(d)).
inline const std::vector<AbandonedRow>& ShippedOutOfViewAbandonRows() {
    static const std::vector<AbandonedRow> r = {{5, 10000}, {6, 5000}, {7, 0}};
    return r;
}
inline const std::vector<AbandonedRow>& ShippedInViewAbandonRows() {
    static const std::vector<AbandonedRow> r = {{5, 30000}, {6, 20000}, {7, 10000}, {8, 0}};
    return r;
}
inline constexpr int64_t kAbandonDespawnDelayMs = 10000;  // abandon_despawn_delay, shipped 10 s
inline constexpr size_t kAbandonedCollectCap = 20;

struct AbandonedCandidate {
    uint64_t id = 0;
    int64_t ageMs = 0;      // own entry-timer age (the caller starts the timer on first collection)
    bool eligible = true;   // 0x00906b00
    bool visible = false;   // entry +0x14 bit 1
    double distance = 0.0;  // to the nearest player
};

struct AbandonedPick {
    bool found = false;
    uint64_t id = 0;
    bool inView = false;  // true: the pick fades over 1000 ms; false: deleted at once
};

class AbandonedVehicleSchedule {
public:
    AbandonedVehicleSchedule(std::vector<AbandonedRow> inRows = ShippedInViewAbandonRows(),
                             std::vector<AbandonedRow> outRows = ShippedOutOfViewAbandonRows(),
                             int64_t delayMs = kAbandonDespawnDelayMs)
        : in_(std::move(inRows)), out_(std::move(outRows)), delayMs_(delayMs) {}

    // One schedule pass. `cands` already filtered per step 1 (the first kAbandonedCollectCap are used).
    AbandonedPick Run(int64_t nowMs, const std::vector<AbandonedCandidate>& cands) {
        AbandonedPick none;
        const size_t nAll = cands.size() < kAbandonedCollectCap ? cands.size() : kAbandonedCollectCap;
        const int n = static_cast<int>(nAll);
        const int in0 = in_.empty() ? 0 : in_.front().numCars;
        const int out0 = out_.empty() ? 0 : out_.front().numCars;
        const int minThreshold = in0 < out0 ? in0 : out0;
        if (n < minThreshold) {
            globalStartMs_ = kUnset;
            return none;
        }
        if (globalStartMs_ == kUnset) globalStartMs_ = nowMs;
        const int64_t E = nowMs - globalStartMs_;
        auto rowHit = [&](const std::vector<AbandonedRow>& rows) {
            for (const auto& r : rows)
                if (n >= r.numCars && E >= r.timeMs) return true;
            return false;
        };
        if (rowHit(out_)) {
            const AbandonedPick p = Pick(cands, nAll, /*outOfView=*/true);
            if (p.found) return p;
        }
        const bool gateOk = inViewGateStartMs_ == kUnset || nowMs - inViewGateStartMs_ >= delayMs_;
        if (gateOk && rowHit(in_)) {
            AbandonedPick p = Pick(cands, nAll, /*outOfView=*/false);
            if (p.found) {
                p.inView = true;
                inViewGateStartMs_ = nowMs;
                return p;
            }
        }
        return none;
    }

    bool globalTimerRunning() const { return globalStartMs_ != kUnset; }

private:
    static constexpr int64_t kUnset = INT64_MIN;
    AbandonedPick Pick(const std::vector<AbandonedCandidate>& cands, size_t n, bool outOfView) const {
        AbandonedPick best;
        double bestDist = -1.0;
        for (size_t i = 0; i < n; ++i) {
            const auto& c = cands[i];
            if (c.ageMs < delayMs_ || !c.eligible) continue;
            if (outOfView && c.visible) continue;
            if (c.distance > bestDist) {  // strictly greater: the first of equals wins
                bestDist = c.distance;
                best.found = true;
                best.id = c.id;
            }
        }
        return best;
    }

    std::vector<AbandonedRow> in_, out_;
    int64_t delayMs_;
    int64_t globalStartMs_ = kUnset;
    int64_t inViewGateStartMs_ = kUnset;
};

}  // namespace sr3airuntime
