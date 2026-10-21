// Escort (base, Tiger, DLC Genki) - spec-activity-runtime.md section 6.8 (meters port recipe
// and fail checks, FINAL). Satisfaction fills with pure driving time in the client's car
// while nothing hinders it; paparazzi filming drains the footage meter instead; full
// satisfaction moves the run to the drop-off. Lose: footage exhausted (base and Genki),
// the client dead, the car destroyed, the client abandoned for 10 s, Genki farther than
// 30 m, or death.
#pragma once

#include <optional>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3activity {

enum class EscortVariant { Base = 0, Tiger = 1, Genki = 2 };

// One Levels/Level row of an `_a_es_*` instance.
struct EscortParams {
    EscortVariant variant = EscortVariant::Base;
    double satisfactionTimeS = 0.0;  // Satisfaction_Time
    double footageLimitS = 0.0;      // Footage_Limit
    double coopFootageLimitS = 0.0;  // Coop_Footage_Limit
    int numComplications = 0;        // Num_Complications
    int combosToAdvance = 0;         // Number_of_combos_to_advance
};

std::optional<EscortParams> ReadEscortParams(const sr3xtbl::Document& instanceDoc, EscortVariant variant,
                                             int levelIndex = 0);

constexpr double kEscortFilmDistance = 25.0;    // filming: closer, with line of sight
constexpr double kEscortNearDistance = 50.0;    // "near": a complaint line
constexpr double kEscortFilmReactionS = 0.5;    // the footage reaction at most every 500 ms
constexpr double kEscortComplaintCooldownS = 15.0;
constexpr double kEscortFootageWarningS = 5.0;  // below 5000 ms left
constexpr double kEscortAbandonS = 10.0;        // ACT_ESCORT_GETBACK countdown
constexpr double kEscortGenkiMaxDistance = 30.0;  // 900 = 30 squared
constexpr double kEscortEnemyNearDistance = 25.0;  // IRS / Spouse within 25 stops satisfaction

struct EscortPursuer {
    double distance = 0.0;  // to the client's vehicle
    bool lineOfSight = false;
};

// One frame of the world as the run sees it.
struct EscortFrame {
    double dt = 0.0;
    std::vector<EscortPursuer> paparazzi;  // live paparazzi / pursuers
    bool complicationActive = false;       // a client request in progress
    bool irsOrSpouseWithin25 = false;
    bool hudLock = false;                  // cutscene / HUD lock up
    bool playerDrivingClientVehicle = false;  // seat state 3 in the client's car
    bool playerInClientVehicle = true;     // abandon test (driving phases)
    double genkiSatisfactionDrainS = 0.0;  // Genki: the per-frame amount from its minigame
    double genkiDistance = 0.0;            // Genki: the player's distance metric to the vehicle
    bool clientDead = false;
    bool clientVehicleDestroyed = false;
};

enum class EscortPhase { Driving, ToDropoff, Succeeded, Failed };

enum class EscortFailReason { None, Footage, ClientDead, CarDestroyed, LeftCar, GenkiTooFar, PlayerDied };

struct EscortTickResult {
    bool filming = false;
    bool footageReaction = false;   // posted (500 ms throttle)
    bool complaint = false;         // a "near" complaint (15 s cooldown)
    bool footageWarning = false;
    bool getBackMessage = false;    // ACT_ESCORT_GETBACK_MSG with the countdown
    double getBackSecondsLeft = 0.0;
    bool satisfactionFull = false;  // the drop-off checkpoint appears
    bool justSucceeded = false;
    bool justFailed = false;
    EscortFailReason failReason = EscortFailReason::None;
};

class EscortRun {
public:
    EscortRun(const EscortParams& params, bool coop = false);

    EscortTickResult tick(const EscortFrame& f);
    // The client delivered at the drop-off (the later phases' arrival and walk-in).
    bool arriveAtDropoff();
    void playerDied();
    // A Tiger rage slap that fails subtracts Slap_Rage_Value from the footage.
    void tigerSlapFailed(double slapRageValueS) { footageS_ -= slapRageValueS; }
    // Co-op's passenger minigame (wrong-move penalty) is not modelled: 6.8 says a wrong move
    // "subtracts round(Satisfaction_Time / (combos + complications + moves done))" from a
    // counter that counts DOWN to full, so its direction is unclear (OPEN, HANDOFF-C).

    double satisfactionRemainingS() const { return satS_; }
    double footageRemainingS() const { return footageS_; }
    double satisfactionMeter() const;  // 1 - sat / Satisfaction_Time
    double footageMeter() const;       // 1 - footage / Footage_Limit
    EscortPhase phase() const { return phase_; }
    EscortFailReason failReason() const { return failReason_; }

private:
    void fail(EscortFailReason r, EscortTickResult& out);

    EscortParams p_;
    double footageLimitS_ = 0.0;
    double satS_ = 0.0;
    double footageS_ = 0.0;
    double sinceReactionS_ = 1e9;
    double sinceComplaintS_ = 1e9;
    bool abandonRunning_ = false;
    double abandonLeftS_ = 0.0;
    EscortPhase phase_ = EscortPhase::Driving;
    EscortFailReason failReason_ = EscortFailReason::None;
};

}  // namespace sr3activity
