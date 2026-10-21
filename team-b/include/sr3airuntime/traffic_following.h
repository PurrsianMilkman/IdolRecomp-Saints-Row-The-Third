#pragma once
// sr3airuntime/traffic_following.h - ambient-traffic speed laws from spec-ai-runtime.md:
//   * Sec20.6.3(b) throttle law 0x00b207d0 (AmbientThrottleLaw);
//   * Sec20.6.7(b) car-following law 0x00b51950 (FollowingLaw): 5 m standstill gap, ~11 m steady gap, 0.75/0.5
//     brake hysteresis. The caller owns the obstacle probe (Sec20.6.7(a)), the 300 ms memory and the combiner.
// Signal controller (Sec20.6.13) and the junction yield (Sec20.6.7(c)) live in their own headers (traffic_signals.h,
// traffic_yield.h) and are wired by the viewer; this file holds only the two speed laws. Stop-line geometry stays OPEN.
#include <algorithm>
#include <cmath>
#include <optional>

namespace sr3airuntime {

// Sec20.6.3(b): vt < 1 -> 0.001; else k = max(2, (vt/5)^2) with vt clamped to [1,100];
// throttle = k (1 - v^2/vt^2) clamped to [0.001, cap]; cap = 1.0 when `flag`, else 0.7 + 0.3 clamp((v-5)/5, 0, 1).
inline double AmbientThrottleLaw(double vt, double v, bool flag) {
    if (vt < 1.0) return 0.001;
    vt = std::clamp(vt, 1.0, 100.0);
    const double k = std::max(2.0, (vt / 5.0) * (vt / 5.0));
    const double cap = flag ? 1.0 : 0.7 + 0.3 * std::clamp((v - 5.0) / 5.0, 0.0, 1.0);
    return std::clamp(k * (1.0 - v * v / (vt * vt)), 0.001, cap);
}

struct FollowingResult {
    double brake = 0.0;     // deceleration demand (m/s^2); 0 = none
    double cap = 0.0;       // speed cap (m/s)
    bool remember = false;  // this target braked us: remember it for 300 ms (hysteresis 0.5 instead of 0.75)
};

// Sec20.6.7(b). aB = vehicle +0xc58 (AI_Max_Braking_Decel x condition scale), kind 0 obstacle / 1 destination
// stop / 2 signal / 3 junction yield / 4 mode-8 stop point, d = gap, u = obstacle speed along own heading,
// v = own speed, recentlyBraked = this target braked us within the last 300 ms.
inline FollowingResult FollowingLaw(double aB, int kind, double d, double u, double v, bool recentlyBraked) {
    const double aLim = std::min(aB, 10.0);
    const double aC = 0.75 * aLim;
    double dEff = d - (kind == 4 ? 3.0 : 5.0);
    if (dEff < 0.5 && u < 2.0) dEff = 0.0;
    double vs, req;
    if (dEff >= 0.01) {
        vs = u + std::sqrt(2.0 * aC * dEff);
        req = (u >= v) ? 0.0 : (u - v) * (u - v) / (2.0 * dEff);
    } else if (kind == 1 && u > v) {
        req = 0.0;
        vs = u;
    } else {
        req = aB;
        vs = 0.0;
    }
    const double f = recentlyBraked ? 0.5 : 0.75;
    FollowingResult r;
    if (req > f * aLim) {
        r.brake = req;
        r.cap = vs;
        r.remember = true;
    } else if (dEff < 6.0) {
        r.cap = std::max(u - 0.5, 2.0);
    } else if (d - 4.0 > 0.01 && u + std::sqrt(aB * (d - 4.0)) > v) {
        r.cap = u + std::sqrt(aB * (d - 4.0));
    } else {
        r.cap = std::max(v, 2.0);
    }
    return r;
}

// ---- Sec20.6.7(a) probe re-arm timer (RF-3) ----
// vehicle +0x1118 is re-armed with trunc(max(0, (15 - v)/15 * 140)) + 60 ms (15.0 at 0x01117a60, 140.0 at
// 0x01187b08): 200 ms when stopped, 106 ms at 10 m/s, 60 ms from 15 m/s. Between runs the result is reused.
inline int ObstacleProbeRearmMs(double v) {
    return static_cast<int>(std::max(0.0, (15.0 - v) / 15.0 * 140.0)) + 60;
}

// ---- Sec20.6.3(b) braking capability (RF-3) ----
// vehicle +0xc58 = AI_Max_Braking_Decel x {1.0, 0.8, 0.7, 0.6, 0.5}[vehicle condition level 0..4] (0x0130ebe0).
inline double ConditionScaledBrakingDecel(double aiMaxBrakingDecel, int conditionLevel) {
    static const double kScale[5] = {1.0, 0.8, 0.7, 0.6, 0.5};
    return aiMaxBrakingDecel * kScale[std::clamp(conditionLevel, 0, 4)];
}

// Per-car a_b: the car's own vehicle-info AI_Max_Braking_Decel (+0x770) when it has a usable row value (present, finite
// and > 0), else `fallbackAiMax` (CHOSEN by the caller); then the Sec20.6.3(b) condition scale. `usedRow` (optional) is
// set to whether the row value was used.
inline double PerCarBrakingDecel(std::optional<double> rowAiMax, double fallbackAiMax, int conditionLevel,
                                 bool* usedRow = nullptr) {
    const bool use = rowAiMax.has_value() && std::isfinite(*rowAiMax) && *rowAiMax > 0.0;
    if (usedRow) *usedRow = use;
    return ConditionScaledBrakingDecel(use ? *rowAiMax : fallbackAiMax, conditionLevel);
}

// ---- Sec20.6.13(f)/(g) mode gate (RF-2) ----
// The light test lives in the combiner (mode 8, and HIGH mode 14) and needs the obey bit (+0xc6c bit 0); the
// stop-line rule (law kind 4) needs the obey bit and mode byte == 8 exactly. Chase 4, flee 2, rejoin 6 and the other
// modes run neither.
inline constexpr int kVehicleModeAmbient = 8;
inline constexpr int kVehicleModeHeld = 14;
inline bool RunsSignalRule(int mode, bool obeyTrafficLights) {
    return obeyTrafficLights && (mode == kVehicleModeAmbient || mode == kVehicleModeHeld);
}
inline bool RunsStopLineRule(int mode, bool obeyTrafficLights) {
    return obeyTrafficLights && mode == kVehicleModeAmbient;
}

// ---- Sec20.6.7(c) junction yield (gates FINAL, time windows HIGH) ----
// Look-ahead D = v^2 / (2 f a_lim) + 5 m with f as in (b) (0.5 while the target is remembered, else 0.75) and
// a_lim = min(a_b, 10).
inline double YieldLookahead(double v, double aB, bool recentlyBraked) {
    const double aLim = std::min(aB, 10.0);
    return v * v / (2.0 * (recentlyBraked ? 0.5 : 0.75) * aLim) + 5.0;
}
// No yield when the vehicle could not stop before the conflict segment anyway: v^2/(2 a_b) > distance.
inline bool YieldCouldStop(double v, double aB, double distToConflict) { return v * v / (2.0 * aB) <= distToConflict; }

constexpr double kYieldAccelMs2 = 3.0;           // 0x01115250 / 0x012a2e18: constant prediction acceleration
constexpr double kYieldWindowSeparationS = 2.0;  // 0x01116458 / 0x01187ebc

// Time to cover `dist` from speed v accelerating at 3 m/s^2 up to vt, then at constant speed.
inline double YieldTimeToCover(double dist, double v, double vt) {
    if (dist <= 0.0) return 0.0;
    v = std::max(v, 0.0);
    if (v >= vt) return dist / std::max(v, 1e-3);
    const double tAcc = (vt - v) / kYieldAccelMs2;
    const double dAcc = v * tAcc + 0.5 * kYieldAccelMs2 * tAcc * tAcc;
    if (dist <= dAcc) return (-v + std::sqrt(v * v + 2.0 * kYieldAccelMs2 * dist)) / kYieldAccelMs2;
    return tAcc + (dist - dAcc) / vt;
}
struct YieldWindow {
    double enter = 0.0, exit = 0.0;  // seconds from now the car occupies the conflict segment
};
// distToStart = distance to the segment start (<= 0: already inside), segLen = its chord length (+0x20).
inline YieldWindow PredictOccupancyWindow(double distToStart, double segLen, double v, double vt) {
    return {YieldTimeToCover(distToStart, v, vt), YieldTimeToCover(distToStart + segLen, v, vt)};
}
// The car yields when the two occupancy windows are NOT separated by more than 2 s.
inline bool YieldWindowsConflict(const YieldWindow& a, const YieldWindow& b) {
    return !(b.enter > a.exit + kYieldWindowSeparationS || a.enter > b.exit + kYieldWindowSeparationS);
}

}  // namespace sr3airuntime
