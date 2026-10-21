// Heli Assault - spec-activity-runtime.md section 6.5 (data; no per-type reader located).
// Complete Objectives_Needed objectives from the gunner seat; the objective kind for the
// instance's level comes from the top-level Vehicles_destroy_objectives /
// Aircraft_destroy_objectives / Protect_target_objective / Scripted_gang_fight_objective
// blocks, keyed by Level_number. Lose: death, the player heli destroyed, or the protected
// car destroyed. Timer Time_For_Level only when Use_Timer (False in all six instances).
#pragma once

#include <optional>
#include <string>

#include "sr3xtbl/xtbl.h"

namespace sr3activity {

enum class HeliObjectiveKind { None, VehiclesDestroy, AircraftDestroy, ProtectTarget, ScriptedGangFight };

struct HeliProtectInfo {
    double vehicleHitPoints = 0.0;        // Vehicle_hit_points
    double timeBetweenSpawnsS = 0.0;      // Time_between_spawns
    double timeBetweenSpawnsCoopS = 0.0;  // Time_between_spawns_coop
    double chanceOfRocketLauncher = 0.0;  // Chance_of_rocket_launcher
};

struct HeliParams {
    int objectivesNeeded = 0;          // Objectives_Needed
    double heliHealthMultiplier = 1.0; // Heli_Health_Multiplier
    double friendlyFireMult = 0.0;     // Friendly_Fire_Mult
    double timeForLevelS = 0.0;        // Time_For_Level
    bool useTimer = false;             // Use_Timer
    HeliObjectiveKind kind = HeliObjectiveKind::None;
    std::optional<HeliProtectInfo> protect;
};

// Level `levelIndex` of an `_a_ha_*` instance. The objective block is the one holding a
// Level_number equal to levelIndex + 1 (CHOSEN: 1-based, as the shipped single-level files
// carry Level_number 1).
std::optional<HeliParams> ReadHeliParams(const sr3xtbl::Document& instanceDoc, int levelIndex = 0);

enum class HeliState { Running, Succeeded, Failed };
enum class HeliFail { None, PlayerDied, HeliDestroyed, ProtectedCarDestroyed, Time };

class HeliRun {
public:
    explicit HeliRun(const HeliParams& params);

    void objectiveCompleted();
    void tick(double dt);  // the timer counts only when Use_Timer
    void playerDied() { fail(HeliFail::PlayerDied); }
    void heliDestroyed() { fail(HeliFail::HeliDestroyed); }
    // Only a Protect_target_objective level has a protected car.
    void protectedCarDestroyed() {
        if (p_.kind == HeliObjectiveKind::ProtectTarget) fail(HeliFail::ProtectedCarDestroyed);
    }

    int objectivesDone() const { return done_; }
    int objectivesNeeded() const { return p_.objectivesNeeded; }
    double timeLeft() const { return p_.timeForLevelS - elapsedS_; }
    HeliState state() const { return state_; }
    HeliFail failReason() const { return failReason_; }

private:
    void fail(HeliFail r);

    HeliParams p_;
    int done_ = 0;
    double elapsedS_ = 0.0;
    HeliState state_ = HeliState::Running;
    HeliFail failReason_ = HeliFail::None;
};

}  // namespace sr3activity
