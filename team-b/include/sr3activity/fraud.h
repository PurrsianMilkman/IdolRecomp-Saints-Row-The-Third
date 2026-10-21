// Insurance Fraud - spec-activity-runtime.md section 6.2 (scoring port recipe, win/fail),
// with fraud_globals.xtbl (spec-tables-diversions.md section 10, parsed by
// sr3tables_diversions). One "hit" is one player ragdoll episode: impacts during the
// ragdoll build the hit, the ragdoll end banks it.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3activity/activity.h"
#include "sr3tables_diversions/tables.h"

namespace sr3activity {

// One tier of a Multiplier_Data grid.
struct FraudTier {
    double minimum = 0.0;
    double multiplierMin = 0.0;
    double multiplierMax = 0.0;  // <= 0: no cap
    double perMultiplier = 0.0;
};

// A category's *_Element block (the only form the engine reads): Cash_Instead plus up
// to 4 tiers.
struct FraudCategory {
    bool present = false;
    bool cashInstead = false;
    std::vector<FraudTier> tiers;
};

// f(x) = max over the tiers of (x < Minimum ? 0 : min(Multiplier_Min + (x - Minimum) x
// Per_Multiplier, Multiplier_Max)); 0 for an absent category.
double FraudCategoryValue(const FraudCategory& c, double x);

enum class BodyPart { Head, Chest, Groin, Arm, Leg, Other };

struct FraudParams {
    FraudCategory airtime, linearDist, windshield, vehicle, surfing, crazyDriver;
    // Witness, Cop_Witness, Civil_Vehicle, Pinball, Cliff_Diver have no *_Element block in
    // the shipped table: they contribute 0.
    std::array<double, 5> bodyMultipliers{5, 4, 3, 2, 2};  // Head, Chest, Groin, Arm, Leg; Other = 1
    std::vector<std::string> luxuryCars;
    std::vector<std::pair<double, double>> adrenalineGrid;  // {Cash, Bonus}, ascending cash
    double damageMultiplier = 1.0, moneyConversion = 1.0, minMoney = 0.0;
    double onFireBonus = 50.0;  // $/s
    double massMinMultiplier = 1.0, massMaxMultiplier = 1.0, massMinMass = 0.0, massMaxMass = 1.0,
           massMinSpeed = 0.0;
    double crushDamagePerSpeed = 0.0, scrapeDamagePerSpeed = 0.0;
    double adrenalineInAreaMult = 3.0, adrenalineDollarMultiplier = 1.0;
    double ringMoneyPercent = 1.0;  // per instance (constant 1.0 in the shipped instances)
};

// From the parsed fraud_globals.xtbl plus the Fraud_Values row for the scalars the
// sr3tables_diversions reader does not model (Mass_*, Crush_/Scrape_Damage_Per_Speed,
// Adrenaline_InArea_Mult, Adrenaline_Dollar_Multiplier; names from activity spec 6.2).
FraudParams FraudParamsFromTables(const sr3tables_diversions::FraudGlobals& g,
                                  const sr3xtbl::Document& fraudGlobalsDoc);

// One physics contact during the player's ragdoll (the local player only).
struct FraudImpact {
    double dt = 1.0 / 60.0;                  // frame time
    double otherMass = 0.0;                  // mass of the other body (0: static)
    std::array<double, 3> relativeVelocity{};
    double normalSpeed = 0.0;                // impact normal speed
    BodyPart part = BodyPart::Other;
    std::optional<std::string> vehicleName;  // a vehicle struck (feeds Vehicle / luxury)
    int vehicleId = -1;                      // distinct-vehicle key
    bool vehicleCrazyDriver = false;
};

// Per-hit category inputs the host measures (airtime seconds, linear distance, windshield
// count, surfing, crazy drivers). The Vehicle input is the number of distinct vehicles
// struck, counted by the run itself.
struct FraudHitInputs {
    double airtimeSeconds = 0.0;
    double linearDistance = 0.0;
    double windshield = 0.0;
    double surfing = 0.0;
    std::optional<double> crazyDrivers;  // default: the crazy-driver vehicles struck
    bool coopChain = false, coopTeamHit = false, coopGangBonus = false;
};

struct FraudScoringContext {
    bool inScoringArea = false;  // the player has reached the active hood (latched)
    int bonusSpot = -1;          // the bonus spot containing the player, -1 = none
};

enum class FraudState { Running, Winning, Succeeded, Failed };

struct FraudBankResult {
    double hitValue = 0.0;
    bool spotConsumed = false;   // ACT_FRAUD_BONUS_DONE
    bool adrenalineReady = false;  // ACT_FRAUD_ADRENALINE_READY
};

struct FraudTickResult {
    bool showWinHint = false;     // result HUD hint 9
    bool goScoringArea = false;   // ACT_FRAUD_GO_SCORING_AREA
    bool justSucceeded = false;
    bool justFailed = false;      // "fail time"
};

class FraudRun {
public:
    // Level row of the instance: Goal (or Coop_Goal), Duration, Max_Spots.
    FraudRun(const FraudParams& params, const ActivityLevel& level, bool coop = false);

    // Ragdoll begin/end hooks. Begin arms a fresh hit (not during the 3000 ms cooldown);
    // end banks an armed hit and starts the cooldown.
    void ragdollBegin();
    std::optional<FraudBankResult> ragdollEnd(const FraudHitInputs& inputs, const FraudScoringContext& ctx);
    // A contact during the ragdoll. Returns the money added to the hit (0 when ignored).
    double impact(const FraudImpact& imp, const FraudScoringContext& ctx);
    // Per frame. `onFire`: the player is burning (On_Fire_Bonus x dt into an armed hit).
    FraudTickResult tick(double dt, const FraudScoringContext& ctx, bool playerRagdoll, bool partnerRagdoll,
                         bool onFire);
    void playerDied();  // the universal lose condition

    double score() const { return score_; }
    double goal() const { return goal_; }
    double timeLeft() const { return durationS_ - elapsedS_; }
    double adrenalineMeter() const { return adrenalineMeter_; }
    bool adrenalineMode() const { return adrenalineMode_; }
    void endAdrenalineMode() { adrenalineMode_ = false; adrenalineMeter_ = 0.0; }  // see HANDOFF-C (OPEN)
    FraudState state() const { return state_; }
    double hitBase() const { return hitBase_; }
    bool hitArmed() const { return hitArmed_; }
    const std::vector<double>& spotTotals() const { return spotTotal_; }
    // The per-hit cap (+0x5c; its writer is not located): none unless set.
    void setHitCap(std::optional<double> cap) { hitCap_ = cap; }

    // Hit value of the current hit for given inputs (without banking).
    double hitValue(const FraudHitInputs& inputs) const;

private:
    bool scoringAllowed(const FraudScoringContext& ctx) const;
    double adrenalineGain(double hitValue) const;

    FraudParams p_;
    double goal_ = 0.0;
    double durationS_ = 0.0;
    double elapsedS_ = 0.0;
    double score_ = 0.0;
    FraudState state_ = FraudState::Running;
    double winTimerS_ = 0.0;
    bool hitArmed_ = false;
    double hitBase_ = 0.0;
    bool hitAdrenaline_ = false;
    struct StruckVehicle {
        int id;
        bool luxury;
        bool crazy;
    };
    std::vector<StruckVehicle> hitVehicles_;  // at most 6 distinct
    double cooldownS_ = 0.0;
    double adrenalineMeter_ = 0.0;
    bool adrenalineMode_ = false;
    std::vector<double> spotTotal_;
    std::vector<bool> spotConsumed_;
    std::optional<double> hitCap_;
};

}  // namespace sr3activity
