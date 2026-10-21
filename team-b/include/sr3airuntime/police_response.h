#pragma once

// sr3airuntime/police_response.h - police response to notoriety: dispatch rules, the chase speed law and the
// disengage rule, as pure functions/objects (no host, no rendering).
//
// WHAT THE SPEC SAYS (spec-ai-runtime.md; the only source of engine facts here):
//   * Sec19.4 (CONFIRMED): the response to a notoriety level change is a BINARY team-relation flip, not graduated:
//     level >= 1 on a track writes HOSTILE (0) into relation(player team 0, heat-coded team); level <= 0 writes
//     UNFRIENDLY (1) and runs the roster release (a stand-down, not a creation pass). EngineState::tickNpcAi already
//     applies the flip for mission NPCs; this file's `PoliceHostile(level)` is the same rule for viewer-owned units.
//   * Sec19.7/Sec20.6.10 (FINAL): the ONLY notoriety-gated unit PRODUCTION is the roadblock scheduler
//     (roadblock_notoriety_schedule.h, levels 2-5, player driving). No level-driven ambient "chase cop" dispatch was
//     found (Sec19.8 "Level -> response": hostility, roadblocks, and two table families with no found reader).
//   * Sec19.8 (FINAL): an AI of the police team that perceives the player re-arms that track's first-decay timer
//     (sighting stays "fresh" 15000 ms) and an AI engaging the player holds the track at level >= 1: the viewer
//     applies both through Notoriety::RearmFirstDecay / SetHoldFlag (`kSeenFreshMs`).
//   * Sec22.2 (FINAL): the mode-4 chase speed law `0x00b48e50` (ChaseCommandedSpeed below), the 10 m hand-off to the
//     occupants (squared < 100.0), the 4.0 m stand-off, "there is no distance give-up; the chase ends when the target
//     dies or is gone", and the siren on for teams above 4.
//
// CHOSEN stand-ins (the spec leaves them unspecified; each is a Team A question in the task report):
//   * HOW MANY chasing units per level and the spawn pacing (kChosenDispatchRows): no table or code path gives a
//     count for ambient pursuit; only Sec20.6.10's roadblock Max_active_roadblocks / Min..Max intervals exist.
//   * WHERE they appear: the roadblock spawn distance 135 m (Sec20.6.10(b), `Notoriety_roadblock_spawn_dist`) is
//     reused for cars; on-foot cops use a 75-90 m ring (outside the 70 m "visible" reject of Sec20.6.2 and inside the
//     90 m far limit);
//   * the car's occupants: at the hand-off the car stops and its crew leaves it as on-foot cops (Sec22.2 says only
//     "the car stops steering; occupants fight");
//   * sub-state 0x0d "direct steer, no route" (Sec22.2) is the steering used: the road-graph route planner
//     (0x00b4c710) is not ported here;
//   * a stand-down (level 0) removes units that are beyond kStandDownRemoveM, or all after kStandDownMaxMs. The spec
//     only says the chaser "returns to ambient" (mode 6); there is no ambient police pool here to return to.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "sr3airuntime/common.h"

namespace sr3airuntime {

// ---- Sec19.4 (CONFIRMED) -------------------------------------------------------------------------------------
inline bool PoliceHostile(int32_t policeLevel) { return policeLevel >= 1; }

// Sec19.8: a sighting stays fresh this long (the literal 0x3A98).
inline constexpr int64_t kPoliceSeenFreshMs = 15000;

// ---- CHOSEN dispatch ------------------------------------------------------------------------------------------
struct PoliceDispatchRow {
    int32_t footTarget;      // live on-foot cops wanted at this level
    int32_t carTarget;       // live chase cars wanted at this level
    int64_t spawnIntervalMs; // pacing: at most one unit per kind per interval
};
// level 0 -> no units. CHOSEN (see the file header); monotone in level, capped by the roadblock row maxima's scale.
inline constexpr std::array<PoliceDispatchRow, 6> kChosenDispatchRows = {{
    {0, 0, 0},
    {2, 0, 4000},
    {3, 1, 3500},
    {4, 2, 3000},
    {5, 3, 2500},
    {6, 4, 2000},
}};

inline constexpr double kChosenFootSpawnMinM = 75.0;   // outside the 70 m visible reject (Sec20.6.2)
inline constexpr double kChosenFootSpawnMaxM = 90.0;   // inside the 90 m far limit (Sec20.6.2)
inline constexpr double kCarSpawnDistM = 135.0;        // Sec20.6.10(b) Notoriety_roadblock_spawn_dist, reused (CHOSEN)
inline constexpr double kChosenCarChaseKeepM = 400.0;  // a chaser further than this is re-placed (CHOSEN, a lost car)
inline constexpr int64_t kChosenStandDownMaxMs = 20000;
inline constexpr double kChosenStandDownRemoveM = 90.0;
inline constexpr int32_t kChosenCrewSize = 2;          // occupants that leave a handed-off car (CHOSEN)
inline constexpr int64_t kChosenCrewExitDelayMs = 1500;

struct PoliceDispatchDecision {
    bool spawnFoot = false;
    bool spawnCar = false;
    bool disengage = false;  // level 0 (or the player is gone): stand down
};

class PoliceDispatcher {
public:
    // One call per frame. `liveFoot`/`liveCar` count units that are still engaged (not standing down).
    PoliceDispatchDecision Update(int64_t nowMs, int32_t level, int32_t liveFoot, int32_t liveCar) {
        PoliceDispatchDecision d;
        level = std::max<int32_t>(0, std::min<int32_t>(5, level));
        if (!PoliceHostile(level)) {
            d.disengage = true;
            footReadyMs_ = carReadyMs_ = -1;
            return d;
        }
        const PoliceDispatchRow& row = kChosenDispatchRows[static_cast<size_t>(level)];
        if (liveFoot < row.footTarget) {
            if (footReadyMs_ < 0) footReadyMs_ = nowMs;  // first unit of an engagement appears at once
            if (nowMs >= footReadyMs_) {
                d.spawnFoot = true;
                footReadyMs_ = nowMs + row.spawnIntervalMs;
            }
        }
        if (liveCar < row.carTarget) {
            if (carReadyMs_ < 0) carReadyMs_ = nowMs;
            if (nowMs >= carReadyMs_) {
                d.spawnCar = true;
                carReadyMs_ = nowMs + row.spawnIntervalMs;
            }
        }
        return d;
    }

private:
    int64_t footReadyMs_ = -1, carReadyMs_ = -1;
};

// A spawn point on the ring [minM, maxM] around `center` at angle `angleRad` (r01 in [0,1] picks the radius).
inline Vec3 PoliceRingPoint(const Vec3& center, double angleRad, double r01, double minM, double maxM) {
    const double r = minM + (maxM - minM) * std::max(0.0, std::min(1.0, r01));
    return {center.x + std::cos(angleRad) * r, center.y, center.z + std::sin(angleRad) * r};
}

// Stand-down removal (CHOSEN): after `standDownMs` of level 0 a unit goes when far enough away, or all go at the cap.
inline bool PoliceShouldRemoveAfterStandDown(int64_t standDownMs, double distToPlayerM) {
    return standDownMs >= kChosenStandDownMaxMs || (standDownMs > 0 && distToPlayerM >= kChosenStandDownRemoveM);
}

// ---- Sec22.2 chase (FINAL) ------------------------------------------------------------------------------------
inline constexpr double kChaseStandoffM = 4.0;        // 0x12a2f6c
inline constexpr double kChaseHandoffDistSq = 100.0;  // 0x012a2dd8: 10 m
inline constexpr double kChaseFarGain = 1.2;          // m * 1.2 * a_b * (g - s)
inline constexpr double kChaseNearSlope = 5.0;        // v_t - 5 * (1 - g / s)

struct ChaseSpeedCommand {
    double vCmd = 0.0;   // commanded speed (m/s)
    double decel = 0.0;  // requested deceleration (m/s^2), already dead-banded
};

// The target-relative speed law 0x00b48e50. vOwn/vTarget in m/s, aB = the vehicle's brake/accel figure (+0xc58),
// gap = distance from the own look-ahead point to the target point (m), flank = a flank manoeuvre is active (m = 2).
// bit6 = directive +0xe08 bit 6 (overlap stop); it is never set for a mode-4 chase started by vehicle_chase, so the
// default is false.
inline ChaseSpeedCommand ChaseCommandedSpeed(double vOwn, double vTarget, double aB, double gap, bool flank,
                                             bool bit6 = false, double standoff = kChaseStandoffM) {
    ChaseSpeedCommand c;
    const double s = standoff;
    if (gap >= s) {
        const double m = flank ? 2.0 : 1.0;
        const double over = gap - s;
        c.vCmd = vTarget + std::sqrt(m * kChaseFarGain * aB * over);
        if (vOwn > c.vCmd) {
            c.decel = over < 0.1 ? aB : std::min(aB, (vOwn * vOwn - vTarget * vTarget) / (2.0 * over));
        }
    } else {
        c.vCmd = vTarget - kChaseNearSlope * (1.0 - gap / s);
        if (bit6 && gap < 0.0) {
            const bool wasAbove5 = c.vCmd > 5.0;
            c.vCmd = 0.0;
            if (wasAbove5) c.decel = aB;
        } else if (vOwn > c.vCmd) {
            c.decel = gap != 0.0 ? (vOwn / gap) * (vOwn - c.vCmd) : aB;
        }
    }
    if (c.decel < 0.5 * aB) c.decel = 0.0;  // brake dead band (0x00b4b4d1)
    return c;
}

// Hand-off to the occupants (Sec22.2 step 4 port rule): the target is the local player, the driver is hostile to it,
// and (store interaction active, or the vehicle-to-vehicle squared distance < 100.0).
inline bool ChaseHandoff(bool targetIsLocalPlayer, bool driverEnemyIsPlayer, bool storeInteraction, double distSq) {
    return targetIsLocalPlayer && driverEnemyIsPlayer && (storeInteraction || distSq < kChaseHandoffDistSq);
}

// Siren (Sec22.2 step 6): driver team above 4 (police 5/6, civilians 7), flag bit 7 clear.
inline bool ChaseSirenOn(int teamId, bool flagBit7) { return !flagBit7 && teamId > 4; }

// Integrates the chaser's speed toward the command: accelerate at up to aB, brake at the requested decel (min aB).
inline double ChaseIntegrateSpeed(double vOwn, const ChaseSpeedCommand& c, double aB, double dt) {
    if (c.decel > 0.0) return std::max(0.0, vOwn - std::min(c.decel, aB) * dt);
    if (vOwn < c.vCmd) return std::min(c.vCmd, vOwn + aB * dt);
    return std::max(c.vCmd, vOwn - 0.5 * aB * dt);  // inside the dead band: coast down gently
}

// Direct steering (sub-state 0x0d, "no route"): kinematic bicycle. maxSteerRad / wheelbaseM are CHOSEN vehicle figures.
inline double ChaseSteerHeading(double heading, double speed, double toTargetX, double toTargetZ, double dt,
                                double wheelbaseM = 2.9, double maxSteerRad = 0.55) {
    const double want = std::atan2(toTargetZ, toTargetX);
    double diff = want - heading;
    while (diff > 3.14159265358979323846) diff -= 6.28318530717958647692;
    while (diff < -3.14159265358979323846) diff += 6.28318530717958647692;
    const double steer = std::max(-maxSteerRad, std::min(maxSteerRad, diff * 2.0));
    double dh = speed / wheelbaseM * std::tan(steer) * dt;
    if (std::fabs(dh) > std::fabs(diff)) dh = diff;  // never overshoot the wanted heading in one step
    return heading + dh;
}

}  // namespace sr3airuntime
