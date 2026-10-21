// Mayhem and Tank Mayhem (one class; Ball Mayhem shares it) - spec-activity-runtime.md
// sections 6.6 and 6.7 (port recipes, FINAL): destruction $ scored per kill/destroy event
// through a combo multiplier, credited at once; a combo timer whose speed rises at the
// Combo_Decay_Cutoffs; win at Destruction_Goal, fail on the level timer, the Tank leash,
// the tank destroyed, or death.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3activity {

enum class MayhemKind { Mayhem = 10, TankMayhem = 9, BallMayhem = 0x10 };

// tweak_table.xtbl values the class reads (shipped values; the "Tank_mayhem_" names apply to
// all three kinds).
struct MayhemTweaks {
    double humanBaseValue = 100.0;       // Tank_mayhem_human_base_value
    double comboStartTimeMs = 15000.0;   // Tank_mayhem_combo_start_time_ms
    double comboMaxTimeMs = 30000.0;     // Tank_mayhem_combo_max_time_ms
    double comboIncrementMs = 2500.0;    // Tank_mayhem_combo_increment_time_sp_ms
    double comboIncrementCoopMs = 2500.0;  // ..._coop_ms
};

struct MayhemCutoff {
    double destructionValue = 0.0;  // Destruction_Value
    double decayMult = 0.0;         // Decay_Mult (absolute speed, not cumulative)
};

// One level of an instance (Levels/Level): goal, duration, Combo_Info, Activity_Center.
struct MayhemParams {
    MayhemKind kind = MayhemKind::Mayhem;
    double goal = 0.0, goalCoop = 0.0;  // Destruction_Goal / Destruction_Goal_Coop
    double durationS = 0.0;
    double humanMult = 0.0, vehicleMult = 0.0, moverMult = 0.0;  // *_Combo_Multiplier
    double completionMult = 0.0;   // Completion_Mult
    double comboMaxMult = 0.0;     // Combo_Max_Multiplier (absent: 0, no cap)
    std::vector<MayhemCutoff> cutoffs;
    bool hasActivityCenter = false;
    std::string centerName;        // Activity_Center/Center (a navpoint name)
    double radius = 0.0;           // Activity_Center/Radius
};

// Level `levelIndex` of an `_a_mh_*` / `_a_tm_*` instance document; nullopt without it.
std::optional<MayhemParams> ReadMayhemParams(const sr3xtbl::Document& instanceDoc, MayhemKind kind,
                                             int levelIndex = 0);

// The class's base value of a mover/object: min(sqrt(bbox volume) x 100 + 300, 1500).
double MayhemMoverValue(double boundingBoxVolume);

enum class MayhemClass { Human, Vehicle, Mover };

struct MayhemEvent {
    MayhemClass cls = MayhemClass::Human;
    // The class base value: humans use the tweak, movers MayhemMoverValue, vehicles their
    // own record value. A High_Value_Targets member's Value replaces it.
    std::optional<double> baseValue;
    std::optional<double> hvtValue;
    bool victimInAssignedHood = true;
    bool killedByNonPlayerAttacker = false;  // excluded (humans)
};

enum class MayhemState { Running, WrapUp, Succeeded, Failed };

struct MayhemTickResult {
    bool comboEnded = false;        // ACT_TANK_MAYHEM_COMBO_TOTAL
    double comboTotal = 0.0;
    bool justSucceeded = false;
    bool justFailed = false;        // ACT_MAYHEM_FAIL / ACT_TANK_MAYHEM_FAIL
    bool strayingAway = false;      // ACT_TANK_MAYHEM_STRAYING_AWAY (leash running)
    bool showRadiusRing = false;    // from 0.75 x Radius
};

class MayhemRun {
public:
    MayhemRun(const MayhemParams& params, const MayhemTweaks& tweaks = {}, bool coop = false);

    // A kill/destroy event. Returns the $ credited (0 when it does not score).
    double event(const MayhemEvent& e);
    // Per frame. `distanceToCenter`: the closer player's distance to Activity_Center
    // (ignored without one); `hvtSettled`: every HVT group leader resolved (no groups: true);
    // `tankDestroyed`: Tank/Ball kinds only.
    MayhemTickResult tick(double dt, double distanceToCenter = 0.0, bool hvtSettled = true,
                          bool tankDestroyed = false);
    void playerDied();

    double banked() const { return banked_; }
    double goal() const { return goal_; }
    double timeLeft() const { return durationS_ - elapsedS_; }
    double comboCash() const { return comboCash_; }
    double comboMultiplier() const { return comboMult_; }
    int comboCount() const { return comboCount_; }
    bool comboRunning() const { return comboRunning_; }
    double comboRemainingMs() const { return comboRemainingMs_; }
    double comboWindowMs() const { return comboWindowMs_; }
    double comboSpeed() const { return speed_; }
    bool leashRunning() const { return leashRunning_; }
    double leashRemainingS() const { return leashRemainingS_; }
    MayhemState state() const { return state_; }

private:
    void setSpeed(double newSpeed);
    void endCombo(MayhemTickResult& r);
    bool goalMet() const { return banked_ >= goal_; }

    MayhemParams p_;
    MayhemTweaks tw_;
    bool coop_ = false;
    double goal_ = 0.0;
    double durationS_ = 0.0;
    double elapsedS_ = 0.0;
    double banked_ = 0.0;
    MayhemState state_ = MayhemState::Running;
    int comboCount_ = 0;
    double comboMult_ = 1.0;
    double comboCash_ = 0.0;
    size_t cutoffIndex_ = 0;
    bool comboRunning_ = false;
    double comboRemainingMs_ = 0.0;
    double comboWindowMs_ = 0.0;
    double speed_ = 1.0;
    bool leashRunning_ = false;  // paused while inside (or without a center)
    double leashRemainingS_ = 20.0;
};

constexpr double kMayhemLeashSeconds = 20.0;
constexpr uint32_t kMayhemBankedMax = 0xFFFFFFFFu;  // unsigned, saturating

}  // namespace sr3activity
