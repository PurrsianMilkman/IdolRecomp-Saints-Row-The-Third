// Insurance Fraud - spec-activity-runtime.md section 6.2. See include/sr3activity/fraud.h.

#include "sr3activity/fraud.h"

#include <algorithm>
#include <cmath>

namespace sr3activity {

namespace {

constexpr double kHitCooldownS = 3.0;   // 3000 ms after a banked hit
constexpr double kWinDelayS = 1.0;      // the 1000 ms result timer
constexpr size_t kMaxHitVehicles = 6;

FraudCategory FromParsed(const sr3tables_diversions::FraudMultiplierCategory& c) {
    FraudCategory out;
    if (!c.tieredElementPresent) return out;  // only the *_Element blocks are read
    out.present = true;
    out.cashInstead = c.cashInstead.value;
    for (const auto& t : c.tiers) {
        if (out.tiers.size() >= 4) break;  // up to 4 tiers
        out.tiers.push_back({t.minimum.value, t.multiplierMin.value, t.multiplierMax.value, t.perMultiplier.value});
    }
    return out;
}

const sr3xtbl::Node* FraudValuesRow(const sr3xtbl::Document& doc) {
    const sr3xtbl::Node* t = doc.table();
    if (!t) t = doc.root();
    return t ? sr3xtbl::FindChild(t, "Fraud_Values") : nullptr;
}

// Cash_Instead is a flag element: the shipped fraud_globals.xtbl marks Airtime, Linear_Dist
// and Windshield with an empty <Cash_Instead/> (6.2 lists exactly those three as cash), which
// a text-valued bool read sees as false. Present and empty, or a true text, sets the flag.
void ApplyCashInsteadFlag(const sr3xtbl::Node* row, const char* element, FraudCategory& c) {
    const sr3xtbl::Node* e = sr3xtbl::FindChild(row, element);
    const sr3xtbl::Node* flag = e ? sr3xtbl::FindChild(e, "Cash_Instead") : nullptr;
    if (!flag) return;
    if (!flag->text() || flag->text()->empty()) c.cashInstead = true;
    else if (const auto b = sr3xtbl::GetBool(e, "Cash_Instead")) c.cashInstead = *b;
}

}  // namespace

double FraudCategoryValue(const FraudCategory& c, double x) {
    if (!c.present) return 0.0;
    double best = 0.0;
    for (const FraudTier& t : c.tiers) {
        double v = 0.0;
        if (x >= t.minimum) {
            v = t.multiplierMin + (x - t.minimum) * t.perMultiplier;
            if (t.multiplierMax > 0.0) v = std::min(v, t.multiplierMax);
        }
        best = std::max(best, v);
    }
    return best;
}

FraudParams FraudParamsFromTables(const sr3tables_diversions::FraudGlobals& g,
                                  const sr3xtbl::Document& fraudGlobalsDoc) {
    FraudParams p;
    p.airtime = FromParsed(g.airtime);
    p.linearDist = FromParsed(g.linearDist);
    p.windshield = FromParsed(g.windshield);
    p.vehicle = FromParsed(g.vehicle);
    p.surfing = FromParsed(g.surfing);
    p.crazyDriver = FromParsed(g.crazyDriver);
    if (g.bodyMultipliersPresent) {
        p.bodyMultipliers = {g.bodyMultipliers.head.value, g.bodyMultipliers.chest.value,
                             g.bodyMultipliers.groin.value, g.bodyMultipliers.arm.value, g.bodyMultipliers.leg.value};
    }
    p.luxuryCars = g.luxuryCars;
    for (const auto& t : g.adrenalineGrid) p.adrenalineGrid.push_back({t.cash.value, t.bonus.value});
    std::sort(p.adrenalineGrid.begin(), p.adrenalineGrid.end());
    if (g.damageMultiplier.present) p.damageMultiplier = g.damageMultiplier.value;
    if (g.moneyConversion.present) p.moneyConversion = g.moneyConversion.value;
    if (g.minMoney.present) p.minMoney = g.minMoney.value;
    if (g.onFireBonus.present) p.onFireBonus = g.onFireBonus.value;
    if (const sr3xtbl::Node* row = FraudValuesRow(fraudGlobalsDoc)) {
        auto rd = [&](const char* name, double& out) {
            if (auto v = sr3xtbl::GetFloat(row, name)) out = *v;
        };
        rd("Mass_Min_Multiplier", p.massMinMultiplier);
        rd("Mass_Max_Multiplier", p.massMaxMultiplier);
        rd("Mass_Min_Mass", p.massMinMass);
        rd("Mass_Max_Mass", p.massMaxMass);
        rd("Mass_Min_Speed", p.massMinSpeed);
        rd("Crush_Damage_Per_Speed", p.crushDamagePerSpeed);
        rd("Scrape_Damage_Per_Speed", p.scrapeDamagePerSpeed);
        rd("Adrenaline_InArea_Mult", p.adrenalineInAreaMult);
        rd("Adrenaline_Dollar_Multiplier", p.adrenalineDollarMultiplier);
        ApplyCashInsteadFlag(row, "Airtime_Multiplier_Element", p.airtime);
        ApplyCashInsteadFlag(row, "Linear_Dist_Multiplier_Element", p.linearDist);
        ApplyCashInsteadFlag(row, "Windshield_Multiplier_Element", p.windshield);
        ApplyCashInsteadFlag(row, "Vehicle_Multiplier_Element", p.vehicle);
        ApplyCashInsteadFlag(row, "Surfing_Multiplier_Element", p.surfing);
        ApplyCashInsteadFlag(row, "Crazy_Driver_Multiplier_Element", p.crazyDriver);
    }
    return p;
}

FraudRun::FraudRun(const FraudParams& params, const ActivityLevel& level, bool coop) : p_(params) {
    goal_ = coop ? level.coopGoal.value_or(level.goal.value_or(0.0) * 2.0) : level.goal.value_or(0.0);
    durationS_ = level.duration.value_or(0.0);
    const int spots = static_cast<int>(level.maxSpots.value_or(0.0));
    spotTotal_.assign(static_cast<size_t>(std::max(spots, 0)), 0.0);
    spotConsumed_.assign(spotTotal_.size(), false);
}

bool FraudRun::scoringAllowed(const FraudScoringContext& ctx) const {
    const bool inSpot = ctx.bonusSpot >= 0 && static_cast<size_t>(ctx.bonusSpot) < spotTotal_.size() &&
                        !spotConsumed_[static_cast<size_t>(ctx.bonusSpot)];
    return ctx.inScoringArea || inSpot || adrenalineMode_;
}

void FraudRun::ragdollBegin() {
    if (state_ != FraudState::Running) return;
    hitBase_ = 0.0;
    hitVehicles_.clear();
    hitArmed_ = cooldownS_ <= 0.0;  // CHOSEN: the cooldown blocks arming a new hit
    hitAdrenaline_ = adrenalineMode_;
}

double FraudRun::impact(const FraudImpact& imp, const FraudScoringContext& ctx) {
    if (!hitArmed_ || state_ != FraudState::Running) return 0.0;
    const double t = 60.0 * imp.dt;
    double massMult = 1.0;
    const double span = p_.massMaxMass - p_.massMinMass;
    const double s = span > 0.0 ? std::clamp((imp.otherMass - p_.massMinMass) / span, 0.0, 1.0) : 0.0;
    bool fast = true;
    for (double v : imp.relativeVelocity) fast = fast && std::fabs(v) > p_.massMinSpeed;
    if (s > 0.0 && fast)
        massMult = std::max(1.0, p_.massMinMultiplier + (p_.massMaxMultiplier - p_.massMinMultiplier) * s);
    const double crush = std::fabs(imp.normalSpeed) * massMult * p_.crushDamagePerSpeed * t;
    const double v2 = imp.relativeVelocity[0] * imp.relativeVelocity[0] +
                      imp.relativeVelocity[1] * imp.relativeVelocity[1] +
                      imp.relativeVelocity[2] * imp.relativeVelocity[2];
    const double scrape = v2 * p_.scrapeDamagePerSpeed * t;
    if (imp.vehicleId >= 0 && hitVehicles_.size() < kMaxHitVehicles &&
        std::none_of(hitVehicles_.begin(), hitVehicles_.end(),
                     [&](const StruckVehicle& v) { return v.id == imp.vehicleId; })) {
        bool lux = false;
        if (imp.vehicleName)
            for (const std::string& n : p_.luxuryCars) lux = lux || sr3xtbl::NameEquals(n, *imp.vehicleName);
        hitVehicles_.push_back({imp.vehicleId, lux, imp.vehicleCrazyDriver});
    }
    if (!scoringAllowed(ctx)) return 0.0;
    const double raw = (crush + scrape) * p_.moneyConversion;
    if (raw < p_.damageMultiplier * p_.minMoney) return 0.0;
    const double body = imp.part == BodyPart::Other ? 1.0 : p_.bodyMultipliers[static_cast<size_t>(imp.part)];
    const double added = raw + std::round(body * raw);
    hitBase_ += added;
    return added;
}

double FraudRun::hitValue(const FraudHitInputs& in) const {
    double cash = 0.0, mult = 0.0;
    auto add = [&](const FraudCategory& c, double x) {
        const double v = FraudCategoryValue(c, x);
        (c.cashInstead ? cash : mult) += v;
    };
    int crazy = 0;
    bool luxury = false;
    for (const StruckVehicle& v : hitVehicles_) {
        crazy += v.crazy ? 1 : 0;
        luxury = luxury || v.luxury;
    }
    add(p_.airtime, in.airtimeSeconds);
    add(p_.linearDist, in.linearDistance);
    add(p_.windshield, in.windshield);
    add(p_.vehicle, static_cast<double>(hitVehicles_.size()));
    add(p_.surfing, in.surfing);
    add(p_.crazyDriver, in.crazyDrivers.value_or(static_cast<double>(crazy)));
    const double bonusSlots = 1.0 + 2.0 * (luxury ? 1 : 0) + 2.0 * (in.coopChain ? 1 : 0) +
                              2.0 * (in.coopTeamHit ? 1 : 0) + 2.0 * (in.coopGangBonus ? 1 : 0);
    double value = (hitBase_ + cash) * (mult + bonusSlots);
    if (hitCap_) value = std::min(value, *hitCap_);
    if (hitAdrenaline_) value *= p_.adrenalineDollarMultiplier;
    return value;
}

double FraudRun::adrenalineGain(double v) const {
    const auto& g = p_.adrenalineGrid;
    if (g.empty()) return 0.0;
    if (v <= g.front().first) return g.front().second;  // CHOSEN: clamped at the grid's ends
    if (v >= g.back().first) return g.back().second;
    for (size_t i = 1; i < g.size(); ++i)
        if (v <= g[i].first) {
            const double f = (v - g[i - 1].first) / (g[i].first - g[i - 1].first);
            return g[i - 1].second + f * (g[i].second - g[i - 1].second);
        }
    return g.back().second;
}

std::optional<FraudBankResult> FraudRun::ragdollEnd(const FraudHitInputs& in, const FraudScoringContext& ctx) {
    if (!hitArmed_) return std::nullopt;
    hitArmed_ = false;
    cooldownS_ = kHitCooldownS;
    FraudBankResult r;
    r.hitValue = hitValue(in);
    score_ += r.hitValue;
    const bool inSpot = ctx.bonusSpot >= 0 && static_cast<size_t>(ctx.bonusSpot) < spotTotal_.size() &&
                        !spotConsumed_[static_cast<size_t>(ctx.bonusSpot)];
    if (inSpot) {
        const size_t s = static_cast<size_t>(ctx.bonusSpot);
        spotTotal_[s] += r.hitValue;
        if (spotTotal_[s] > goal_ * p_.ringMoneyPercent) {
            spotConsumed_[s] = true;
            r.spotConsumed = true;
        }
    }
    if (r.hitValue > 0.0 && !adrenalineMode_) {
        adrenalineMeter_ += adrenalineGain(r.hitValue) * (inSpot ? p_.adrenalineInAreaMult : 1.0);
        if (adrenalineMeter_ >= 1.0) {
            adrenalineMeter_ = 1.0;
            adrenalineMode_ = true;
            r.adrenalineReady = true;
        }
    }
    return r;
}

FraudTickResult FraudRun::tick(double dt, const FraudScoringContext& ctx, bool playerRagdoll, bool partnerRagdoll,
                               bool onFire) {
    FraudTickResult r;
    if (state_ == FraudState::Succeeded || state_ == FraudState::Failed) return r;
    elapsedS_ += dt;
    cooldownS_ = std::max(0.0, cooldownS_ - dt);
    if (onFire && hitArmed_) hitBase_ += p_.onFireBonus * dt;
    r.goScoringArea = !ctx.inScoringArea;
    if (state_ == FraudState::Winning) {
        winTimerS_ += dt;
        if (winTimerS_ >= kWinDelayS) {
            state_ = FraudState::Succeeded;
            r.justSucceeded = true;
        }
        return r;
    }
    if (score_ >= goal_) {
        state_ = FraudState::Winning;
        winTimerS_ = 0.0;
        r.showWinHint = true;
        return r;
    }
    if (elapsedS_ >= durationS_ && !(playerRagdoll || partnerRagdoll || adrenalineMode_)) {
        state_ = FraudState::Failed;
        r.justFailed = true;
    }
    return r;
}

void FraudRun::playerDied() {
    if (state_ == FraudState::Running || state_ == FraudState::Winning) state_ = FraudState::Failed;
}

}  // namespace sr3activity
