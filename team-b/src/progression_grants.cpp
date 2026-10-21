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

int32_t RoundProductF32(double product) {
    const double r = std::round(static_cast<double>(static_cast<float>(product)));  // halves away from zero
    if (!(r >= -2147483648.0 && r <= 2147483647.0)) return INT32_MIN;
    return static_cast<int32_t>(r);
}

int32_t ScaleByMultiplier(float multiplier, int32_t amount) {
    return RoundProductF32(static_cast<double>(multiplier) * amount);
}

int64_t AddCashToWallet(int64_t cash, int64_t amount, float multiplier, bool localPlayer) {
    if (localPlayer && amount > 0) {
        const float dollars = static_cast<float>(static_cast<float>(amount) / 100.0f);
        const float scaled = static_cast<float>(static_cast<double>(dollars) * multiplier * 100.0);
        const double r = std::round(static_cast<double>(scaled));
        amount = static_cast<int64_t>(std::clamp(r, -2000000000.0, 2000000000.0));
    }
    // The wallet as read is already within +-2e9 (an edited 2,147,483,000 adds like 2,000,000,000 in the
    // vectors); the sum is then signed 32-bit.
    const int64_t sum = Wrap32(std::clamp<int64_t>(cash, -2000000000LL, 2000000000LL) + amount);
    return std::max<int64_t>(std::clamp<int64_t>(sum, -2000000000LL, 2000000000LL), 0);
}

ActivityAwards ActivityCompletionAwards(int32_t propsAward, float multiplier) {
    ActivityAwards a;
    a.first = propsAward;
    // Props_Award is read as unsigned 32-bit; D-1 rounding (spec-differential-results D-1/D-8).
    a.second = RoundProductF32(static_cast<double>(static_cast<uint32_t>(propsAward)) *
                               (static_cast<double>(multiplier) - 1.0));
    return a;
}

DiversionReward DiversionRewardFor(float fraction, int32_t maxRespect, int32_t maxLifetimeRespect,
                                   int32_t maxCash, int64_t lifetimeStatistic) {
    DiversionReward r;
    const double f = std::clamp(static_cast<double>(fraction), 0.0, 1.0);
    // Nothing unless f > 0.001 as a single (spec-differential-results D-2).
    if (!(f > static_cast<double>(0.001f))) return r;
    if (lifetimeStatistic >= maxLifetimeRespect)  // cash floored, the product first rounded to single
        r.cashDollars = static_cast<int32_t>(std::floor(static_cast<float>(f * static_cast<float>(maxCash))));
    else
        r.respect = static_cast<int32_t>(std::trunc(f * maxRespect));
    return r;
}

ToastFinalisation FinaliseRewardToast(const RewardToast& toast, float multiplier, PlayerRespect& p,
                                      const RespectTable& table, AppliedUnlocks& applied,
                                      const AwardContext& ctx) {
    ToastFinalisation t;
    if (toast.respect > 0) {  // respect only; the toast's cash field is ignored (D-3)
        const int32_t amount = ScaleByMultiplier(multiplier, toast.respect);
        if (amount == 0) return t;  // nothing at all
        t.award = AwardRespect(p, amount, table, applied, ctx);
        t.zeroGrantHint = amount > 0 && t.award.returned == 0;
        return t;
    }
    if (toast.cashDollars > 0.0f) {
        // Out of the 32-bit range the conversion gives -2147483648, which the clamp turns into -2e9.
        const int32_t raw = RoundProductF32(static_cast<double>(toast.cashDollars) * 100.0);
        t.cashRaw = std::clamp<int64_t>(raw, -2000000000LL, 2000000000LL);
    }
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
    // Scaled by the per-kill factor, D-1 rounding (spec-differential-results D-1).
    return RoundProductF32(static_cast<double>(base) * perKillFactor);
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
