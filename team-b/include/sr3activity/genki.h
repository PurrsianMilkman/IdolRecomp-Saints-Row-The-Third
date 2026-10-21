// Professor Genki's Super Ethical Reality Climax (`_a_rm_*`, "Running Man") -
// spec-activity-runtime.md section 6.9. Reach the exit holding at least Required_cash
// (co-op Required_cash_coop) before Time_limit; lose on timer expiry or death. Kill cash,
// targets and trap evades come from running_man_globals.xtbl (`Running Man.str2_pc`,
// layout checked on the real file). The in-run cash is the exit-gate currency; the reward
// is the 6.1 grant.
//
// The combo (6.9.1): kills count and refill the tick budget; every cash score is paid at the
// multiplier in force before it; the budget decays each frame and the combo ends at 0.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3activity {

enum class GenkiVictim { Civilian = 0, EnemyGang = 1, Hunter = 2 };

struct GenkiKillScore {
    double base = 0.0, oneHit = 0.0, humanShield = 0.0, melee = 0.0, sword = 0.0, headshot = 0.0, nutshot = 0.0,
           trapKill = 0.0;
};

struct GenkiThresholdFactor {
    double threshold = 0.0, factor = 1.0;
};

struct GenkiGlobals {
    std::array<GenkiKillScore, 3> kills{};  // Civilian, Enemy Gang, Hunter
    double ethicalBonusTime = 0.0, ethicalBonusCash = 0.0;
    double unethicalCashDeduction = 0.0, unethicalTimeDeduction = 0.0;
    double moneyBonusCash = 0.0, timeBonusTime = 0.0, buttonBonusCash = 0.0, healthBonus = 0.0;
    double electricEvadeCash = 0.0, electricEvadeTime = 0.0, fireEvadeCash = 0.0, fireEvadeTime = 0.0;
    std::vector<GenkiThresholdFactor> multiplierTable;  // consecutive scores -> factor
    std::vector<GenkiThresholdFactor> decayRateTable;   // combo value -> decay speed-up
    double maxComboTicks = 0.0, baseDecayRate = 0.0;
};

// running_man_globals.xtbl (row Running_man_globals).
std::optional<GenkiGlobals> ReadGenkiGlobals(const sr3xtbl::Document& globalsDoc);

// The multiplier-table factor for `consecutive` scores: the largest Threshold <= it, else
// 1.0. Kill k of a combo is paid at GenkiComboFactor(g, k - 1) (6.9.1 port note).
double GenkiComboFactor(const GenkiGlobals& g, int consecutive);

struct GenkiLevel {
    double timeLimitS = 0.0;       // Time_limit
    double requiredCash = 0.0;     // Level_Exit/Required_cash
    double requiredCashCoop = 0.0; // Level_Exit/Required_cash_coop
    std::string endExit;           // Level_Exit/End_exit
};
std::optional<GenkiLevel> ReadGenkiLevel(const sr3xtbl::Document& instanceDoc, int levelIndex = 0);

struct GenkiKill {
    GenkiVictim victim = GenkiVictim::Civilian;
    bool oneHit = false, humanShield = false, melee = false, sword = false, headshot = false, nutshot = false,
         trapKill = false;
};

// The combo object (6.9.1). Rows are kept only with Threshold >= 0, strictly increasing,
// and Factor > 0. Levels step at most once per add.
class GenkiCombo {
public:
    explicit GenkiCombo(const GenkiGlobals& g);
    void reset();
    double multiplier() const;  // M
    double decayFactor() const; // F
    void add(int n, long long cash, int ticks);
    void refill(int n, long long cash, double k = 4.0);  // ticks = trunc(trunc(Base x F) x k)
    // Per frame; returns true when the combo expired this frame (caller reports B, resets).
    bool decay(double dt);
    bool live() const { return ticks_ > 0; }
    int ticks() const { return ticks_; }
    int count() const { return count_; }
    long long bank() const { return bank_; }
    int multiplierLevel() const { return m_; }
    int decayLevel() const { return d_; }

private:
    std::vector<GenkiThresholdFactor> mult_, decay_;
    int maxTicks_ = 0, baseRate_ = 0;
    int m_ = -1, d_ = -1, ticks_ = 0, peak_ = 0, count_ = 0;
    long long bank_ = 0;
    bool reached_ = false, expired_ = false;
};

enum class GenkiTarget { Ethical, Unethical, Money, Time, Button, Health };
enum class GenkiTrap { Electric, Fire };
enum class GenkiState { Running, Succeeded, Failed };

class GenkiRun {
public:
    GenkiRun(const GenkiGlobals& g, const GenkiLevel& level, bool coop = false);

    // Kill cash: trunc(Base x M) plus trunc(bonus x M) for every applicable bonus, then a
    // refill with n = 1 (6.9.1). Returns the cash added.
    double kill(const GenkiKill& k);
    // A target: time and cash bonuses or deductions; Health returns the HP bonus.
    double target(GenkiTarget t);
    void trapEvaded(GenkiTrap t);
    // Per frame: timer and combo decay. Returns the combo bank B when the combo ended this
    // frame (event 3), else -1.
    long long tick(double dt);
    const GenkiCombo& combo() const { return combo_; }
    long long lastComboEndBank() const { return lastEndBank_; }
    // At the exit door: succeeds only holding Required_cash.
    bool reachExit();
    void playerDied();

    double cash() const { return cash_; }
    double required() const { return required_; }
    double timeLeft() const { return timeLeftS_; }
    GenkiState state() const { return state_; }

private:
    GenkiGlobals g_;
    double required_ = 0.0;
    double timeLeftS_ = 0.0;
    double cash_ = 0.0;
    GenkiState state_ = GenkiState::Running;
    GenkiCombo combo_;
    long long lastEndBank_ = -1;
    void pickup(double cash, double time);
};

}  // namespace sr3activity
