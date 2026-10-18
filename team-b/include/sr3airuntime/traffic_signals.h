#pragma once
// sr3airuntime/traffic_signals.h - the traffic-light controller and the per-car stop rules of
// spec-ai-runtime.md Sec20.6.13 (FINAL), on the arrays parsed into sr3zone::RoadGraph (Sec13.2a(i)).
//
//   (b) tick, (c) cursor scheduling, (e) initial state, (h) crossing closed test, (i) hold / release,
//   (f) amber+red stop decision (SignalRuleStops), (g) stop-line rule (StopLineStep).
//
// Time is the engine's millisecond clock handed in by the caller (`nowMs`). A timer is an absolute deadline
// (-1 = disarmed / never). The "first light of the current phase is in a resident tile" gate of (b) is a
// caller predicate on the junction index (the light record carries no position, only a zone id that the 0x2241
// tile loader fills; the viewer passes a CHOSEN distance stand-in).
//
// Not modelled: the 0x2241 per-tile visuals (glare effects), the light refresh (visual only).
#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include "sr3airuntime/traffic_following.h"
#include "sr3zone/road_graph.h"

namespace sr3airuntime {

constexpr int64_t kSignalAmberWindowMs = 3000;  // Sec20.6.13(b): hard-coded last 3000 ms of every phase

struct SignalEvent {
    int64_t timeMs = 0;
    uint32_t junction = 0;
    uint8_t kind = 0;   // 0 = amber onset, 1 = phase switch, 2 = hold, 3 = release
    uint8_t phase = 0;  // the phase that is current after the event
};

class TrafficSignalController {
public:
    // Sec20.6.13(e). Every light 3/2; every junction phase 0 with the timer armed from phase 0's duration (all
    // junctions start in step); per phase: lights 1/1 if it is the junction's phase 0 else 3/2; lanes 1 if phase 0 or
    // the phase has no light, else 3. Lanes not listed keep their on-disk state.
    void Init(const sr3zone::RoadGraph& g, int64_t nowMs) {
        g_ = &g;
        segState_.resize(g.segments.size());
        for (size_t s = 0; s < g.segments.size(); ++s) segState_[s] = g.segments[s].signal;
        veh_.assign(g.lights.size(), 3);
        ped_.assign(g.lights.size(), 2);
        cur_.assign(g.junctions.size(), 0);
        deadline_.assign(g.junctions.size(), -1);
        cursor_ = 0;
        lastTicked_.clear();
        events_.clear();
        for (size_t j = 0; j < g.junctions.size(); ++j) {
            const size_t np = g.PhaseCount(j);
            if (np == 0) continue;
            const size_t p0 = g.junctions[j].firstPhase;
            deadline_[j] = nowMs + g.phases[p0].duration;
            for (size_t k = 0; k < np; ++k) {
                const size_t p = p0 + k;
                const bool lit = g.PhaseLightCount(p) != 0;
                SetLights(p, k == 0 ? 1 : 3, k == 0 ? 1 : 2, /*vehOnly=*/false);
                SetLanes(p, (k == 0 || !lit) ? 1 : 3);
            }
        }
    }

    // Sec20.6.13(c): called once per frame. Ticks junctions from the cursor upward and stops at the first whose tick
    // returns true, leaving the cursor after it; a cursor at the end of the array wraps to 0 on the next call.
    void Update(int64_t nowMs, const std::function<bool(size_t)>& resident) {
        lastTicked_.clear();
        const size_t n = deadline_.size();
        if (cursor_ >= n) cursor_ = 0;
        for (size_t j = cursor_; j < n; ++j) {
            lastTicked_.push_back(j);
            if (Tick(j, nowMs, resident)) {
                cursor_ = j + 1;
                return;
            }
        }
        cursor_ = n;
    }

    // Sec20.6.13(b). True when the junction switched or entered/stayed in its amber window.
    bool Tick(size_t j, int64_t nowMs, const std::function<bool(size_t)>& resident) {
        const size_t np = g_->PhaseCount(j);
        if (np == 0) return false;
        const size_t p0 = g_->junctions[j].firstPhase;
        const size_t cur = p0 + cur_[j];
        if (g_->PhaseLightCount(cur) == 0) return false;  // lightless junction: frozen
        if (resident && !resident(j)) return false;      // not streamed in
        if (deadline_[j] >= 0 && nowMs >= deadline_[j]) {
            SetLights(cur, 3, 2, false);
            SetLanes(cur, 3);
            cur_[j] = static_cast<uint8_t>(cur_[j] + 1 >= np ? 0 : cur_[j] + 1);
            const size_t nxt = p0 + cur_[j];
            deadline_[j] = nowMs + g_->phases[nxt].duration;  // timed from the actual switch
            SetLights(nxt, 1, 1, false);
            SetLanes(nxt, 1);
            if (recordEvents_) events_.push_back({nowMs, static_cast<uint32_t>(j), 1, cur_[j]});
            return true;
        }
        if (deadline_[j] >= 0 && deadline_[j] - nowMs < kSignalAmberWindowMs) {
            const bool first = g_->PhaseLaneCount(cur) == 0 || LaneState(FirstLane(cur)) != 2;
            SetLights(cur, 2, 0, /*vehOnly=*/true);  // pedestrian byte untouched
            SetLanes(cur, 2);
            if (recordEvents_ && first) events_.push_back({nowMs, static_cast<uint32_t>(j), 0, cur_[j]});
            return true;
        }
        return false;
    }

    // Sec20.6.13(i): all-red hold (timer disarmed) and release (arm 0 and tick at once).
    void Hold(size_t j, int64_t nowMs) {
        const size_t cur = g_->junctions[j].firstPhase + cur_[j];
        SetLights(cur, 3, 2, false);
        SetLanes(cur, 3);
        deadline_[j] = -1;
        if (recordEvents_) events_.push_back({nowMs, static_cast<uint32_t>(j), 2, cur_[j]});
    }
    void Release(size_t j, int64_t nowMs, const std::function<bool(size_t)>& resident) {
        deadline_[j] = nowMs;
        if (recordEvents_) events_.push_back({nowMs, static_cast<uint32_t>(j), 3, cur_[j]});
        Tick(j, nowMs, resident);  // a lightless junction is not ticked (latent bug of the original, FINAL)
    }

    // ---- queries ----
    int LaneState(size_t seg) const { return segState_[seg] & 0xf; }  // 1 proceed, 2 amber, 3 stop
    int VehicleLight(size_t l) const { return veh_[l]; }
    int PedestrianLight(size_t l) const { return ped_[l]; }
    size_t CurrentPhase(size_t j) const { return cur_[j]; }
    int64_t Deadline(size_t j) const { return deadline_[j]; }
    // Sec20.6.13(h): closed when the light's vehicle state is 2, 3 or 5, or its pedestrian state is 2.
    bool CrossingClosed(size_t crossing) const {
        const int32_t l = g_->crossingLight[crossing];
        if (l < 0) return false;
        const int v = veh_[l];
        return v == 2 || v == 3 || v == 5 || ped_[l] == 2;
    }
    // Junctions ticked by the last Update (diagnostic / the vector-10 test).
    const std::vector<size_t>& LastTicked() const { return lastTicked_; }
    size_t JunctionCount() const { return deadline_.size(); }

    // Optional event log (amber onset / switch / hold / release), drained by the caller.
    void SetRecordEvents(bool on) { recordEvents_ = on; }
    std::vector<SignalEvent> DrainEvents() {
        std::vector<SignalEvent> out;
        out.swap(events_);
        return out;
    }

private:
    size_t FirstLane(size_t phase) const { return g_->phaseLanes[g_->phases[phase].firstLane]; }
    void SetLights(size_t phase, int veh, int ped, bool vehOnly) {
        const size_t n = g_->PhaseLightCount(phase);
        for (size_t k = 0; k < n; ++k) {
            const size_t l = g_->phaseLights[g_->phases[phase].firstLight + k];
            veh_[l] = static_cast<uint8_t>(veh);
            if (!vehOnly) ped_[l] = static_cast<uint8_t>(ped);
        }
    }
    void SetLanes(size_t phase, int state) {  // low nibble only; the high nibble is kept
        const size_t n = g_->PhaseLaneCount(phase);
        for (size_t k = 0; k < n; ++k) {
            uint8_t& b = segState_[g_->phaseLanes[g_->phases[phase].firstLane + k]];
            b = static_cast<uint8_t>((b & 0xf0) | state);
        }
    }

    const sr3zone::RoadGraph* g_ = nullptr;
    std::vector<uint8_t> segState_, veh_, ped_, cur_;
    std::vector<int64_t> deadline_;
    size_t cursor_ = 0;
    std::vector<size_t> lastTicked_;
    std::vector<SignalEvent> events_;
    bool recordEvents_ = false;
};

// Sec20.6.13(f): amber (2) and red (3) share one rule. d = distance to the lane end, floored at 0.001;
// stop when v^2 / (2 d) <= aB (vehicle +0xc58), else drive through. Green and the unwritten states never stop.
inline bool SignalRuleStops(int laneState, double v, double d, double aB) {
    if (laneState != 2 && laneState != 3) return false;
    d = std::max(d, 0.001);
    return v * v / (2.0 * d) <= aB;
}

// Sec20.6.13(g): segment flag bit 7 stop line, law kind 4 (3 m stand-off). Per-car memory of the served entry.
struct StopLineMemory {
    int32_t servedSegment = -1;  // route entry marked served (+8 bit 0x10)
    bool holding = false;        // vehicle +0xee1 bit 0x10
    double heldMs = 0.0;         // timer +0xfbc
};
struct StopLineResult {
    bool active = false;     // this source is feeding the law
    double brake = 0.0;      // deceleration demand, m/s^2
    double cap = 0.0;        // speed cap, m/s
    bool remember = false;   // law asked for the 300 ms memory
};
constexpr double kStopLineArmDistance = 4.0;  // m, 0x012a2f6c
constexpr double kStopLineArmSpeed = 2.0;     // m/s, 0x01187ebc
constexpr double kStopLineHoldMs = 1200.0;    // 0x00b52792
constexpr double kStopLineHoldBrake = 100.0;  // 0x0111645c

// Only for mode 8 with the obey bit (the caller gates). `d` = distance to the lane end of `segment`.
inline StopLineResult StopLineStep(StopLineMemory& m, int32_t segment, double d, double v, double dtMs, double aB,
                                   bool recentlyBraked) {
    StopLineResult r;
    if (m.holding && m.servedSegment != segment) m.holding = false;
    if (m.holding) {
        m.heldMs += dtMs;
        if (m.heldMs < kStopLineHoldMs) {
            r.active = true;
            r.brake = kStopLineHoldBrake;
            r.cap = 0.0;
            return r;
        }
        m.holding = false;  // drive on; the junction yield decides
        return r;
    }
    if (m.servedSegment == segment) return r;
    if (d < kStopLineArmDistance && v < kStopLineArmSpeed) {
        m.servedSegment = segment;
        m.holding = true;
        m.heldMs = 0.0;
        r.active = true;
        r.brake = kStopLineHoldBrake;
        r.cap = 0.0;
        return r;
    }
    const FollowingResult f = FollowingLaw(aB, 4, d, 0.0, v, recentlyBraked);
    r.active = true;
    r.brake = f.brake;
    r.cap = f.cap;
    r.remember = f.remember;
    return r;
}

}  // namespace sr3airuntime
