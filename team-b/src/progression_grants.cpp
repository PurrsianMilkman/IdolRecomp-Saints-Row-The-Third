// Respect grant sources and the respect multiplier - spec-tables-progression.md
// B.1.1/B.1.2. See include/sr3progression/respect.h.

#include "sr3progression/respect.h"

#include <algorithm>
#include <cmath>

namespace sr3progression {

const DiversionRespectData kShippedStuntDiversions[11] = {
    {"drifting", 30, 3000, 80, 0xB4},
    {"near miss", 25, 10000, 100, 0xC0},
    {"oncoming", 30, 10000, 100, 0xD3},
    {"peel-out", 40, 5000, 100, 0xB3},
    {"hijacking", 10, 1000, 10, 0xBB},
    {"low flying", 75, 5000, 100, 0xBE},
    {"back-seat driver", 125, 10000, 250, 0xC4},
    {"stunt jumping", 150, 8000, 150, 0xB0},
    {"two wheels", 50, 4000, 200, -1},
    {"wheelie", 50, 6000, 200, -1},
    {"stoppie", 50, 6000, 200, -1},
};

int32_t ScaleByMultiplier(float multiplier, int32_t amount) {
    return static_cast<int32_t>(std::lround(static_cast<double>(multiplier) * amount));
}

ActivityAwards ActivityCompletionAwards(int32_t propsAward, float multiplier) {
    ActivityAwards a;
    a.first = propsAward;
    a.second = static_cast<int32_t>(
        std::lround(static_cast<double>(propsAward) * (static_cast<double>(multiplier) - 1.0)));
    return a;
}

DiversionReward DiversionRewardFor(float fraction, int32_t maxRespect, int32_t maxLifetimeRespect,
                                   int32_t maxCash, int64_t lifetimeStatistic) {
    DiversionReward r;
    const double f = std::clamp(static_cast<double>(fraction), 0.0, 1.0);
    // "If it is ~0, nothing": a tolerance of 1e-6 (CHOSEN; the spec gives
    // no epsilon).
    if (f < 1e-6) return r;
    if (lifetimeStatistic >= maxLifetimeRespect)
        r.cashDollars = static_cast<int32_t>(std::lround(f * maxCash));
    else
        r.respect = static_cast<int32_t>(std::trunc(f * maxRespect));
    return r;
}

ToastFinalisation FinaliseRewardToast(const RewardToast& toast, float multiplier, PlayerRespect& p,
                                      const RespectTable& table, AppliedUnlocks& applied,
                                      const AwardContext& ctx) {
    ToastFinalisation t;
    t.award = AwardRespect(p, ScaleByMultiplier(multiplier, toast.respect), table, applied, ctx);
    t.zeroGrantHint = t.award.returned == 0;
    const double raw = std::round(static_cast<double>(toast.cashDollars) * 100.0);
    t.cashRaw = static_cast<int64_t>(std::clamp(raw, -2000000000.0, 2000000000.0));
    return t;
}

int32_t KillRespect(const KillRespectTable& props, int victimRank, bool victimIsGangMember,
                    bool victimExcluded, float perKillFactor) {
    if (!victimIsGangMember || victimExcluded) return 0;
    int32_t base;
    if (victimRank >= 3)
        base = props.kingpin;
    else if (victimRank == 2)
        base = props.gangsta;
    else if (victimRank == 1)
        base = props.killa;
    else
        base = props.thug;
    // Scaled by the per-kill factor and rounded (CHOSEN rounding; the spec
    // gives only "multiplied by a per-kill float").
    return static_cast<int32_t>(std::lround(static_cast<double>(base) * perKillFactor));
}

int32_t StorePurchaseRespect(double price, double storeRespectMultiplier, float multiplier) {
    const double base = std::round(price * storeRespectMultiplier);
    return static_cast<int32_t>(std::lround(base * static_cast<double>(multiplier)));
}

float ApplyRespectBonusUnlockable(float multiplier, const sr3tables_progression::Unlockable& u) {
    if (!u.respectBonusModifier) return multiplier;
    return AddRespectBonus(multiplier, *u.respectBonusModifier);
}

PayCashForRespectResult ApplyPayCashForRespectUnlockable(const sr3tables_progression::Unlockable& u,
                                                         float multiplier, PlayerRespect& p,
                                                         const RespectTable& table, AppliedUnlocks& applied,
                                                         const AwardContext& ctx) {
    PayCashForRespectResult r;
    if (!u.payCashForRespect) return r;
    r.applies = true;
    r.cashCost = u.payCashForRespect->cashCost.value_or(0);
    const int32_t received = u.payCashForRespect->respectReceived.value_or(0);
    r.award = AwardRespect(p, ScaleByMultiplier(multiplier, received), table, applied, ctx);
    return r;
}

}  // namespace sr3progression
