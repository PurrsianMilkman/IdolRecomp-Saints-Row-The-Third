// HUD display model - spec-hud-runtime.md H.2 / H.4(b). See
// include/sr3hud/display_model.h.

#include "sr3hud/display_model.h"

#include <algorithm>
#include <cmath>

namespace sr3hud {

float HealthFillEndAngle(float pct, int32_t level) {
    const int32_t l = std::clamp<int32_t>(level, 0, 4);
    const float end = kHealthRingEndAngle[static_cast<size_t>(l)];
    return (end - kHealthRingStartAngle) * pct + kHealthRingStartAngle;
}

HealthUpdate HealthDisplay::update(float pct, int32_t level) {
    HealthUpdate u;
    if (!(std::fabs(pct - pct_) > 0.001f) && level == level_) return u;
    u.reacted = true;
    u.fillEnd = HealthFillEndAngle(pct, level);
    u.bgEnd = u.fillEnd - 0.035f;
    u.drop = pct < pct_;
    if (pct < 0.3f)
        u.vignetteAlpha = 0.70f - pct;
    else if (pct < 0.5f)
        u.vignetteAlpha = 0.5f - pct;
    u.retract = pct == 1.0f;
    pct_ = pct;
    level_ = level;
    return u;
}

std::string FormatCash(double value) {
    const double f = std::floor(static_cast<double>(static_cast<float>(value)));
    const bool negative = f < 0.0;
    const long long whole = static_cast<long long>(negative ? -f : f);
    std::string digits = std::to_string(whole);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3 + 1);
    const size_t n = digits.size();
    for (size_t i = 0; i < n; ++i) {
        out.push_back(digits[i]);
        const size_t remaining = n - 1 - i;
        if (remaining > 0 && remaining % 3 == 0) out.push_back(',');
    }
    return negative ? "-" + out : out;
}

bool CashDisplay::setCash(float cash, bool forceShow) {
    const int64_t v = static_cast<int64_t>(std::floor(static_cast<double>(cash)));
    const bool changed = !primed_ || v != target_;
    primed_ = true;
    target_ = v;
    return changed || forceShow;
}

void CashDisplay::tick() {
    int64_t diff = target_ - shown_;
    if (diff > 5 || diff < -5)
        diff = static_cast<int64_t>(std::floor(static_cast<double>(diff) * 0.5));
    shown_ += diff;
}

NotorietyIconsUpdate NotorietyIconRow::update(float level) {
    NotorietyIconsUpdate u;
    const int lit = std::clamp(static_cast<int>(std::floor(level)), 0, kNotorietyIconCount);
    u.lit = lit;
    for (int i = 0; i < kNotorietyIconCount; ++i) {
        const bool nowLit = i < lit;
        const bool wasLit = primed_ && i < lit_;
        if (!primed_)
            u.icons[i] = nowLit ? IconAnim::Add : IconAnim::Remove;
        else if (nowLit && !wasLit)
            u.icons[i] = IconAnim::Add;
        else if (!nowLit && wasLit)
            u.icons[i] = IconAnim::Remove;
    }
    if (primed_ && lit == lit_ && lit > 0 && level != level_)
        u.top = level > level_ ? TopIconAnim::Pulse : TopIconAnim::Decay;
    primed_ = true;
    lit_ = lit;
    level_ = level;
    return u;
}

NotorietyDisplayUpdate NotorietyDisplay::update(const std::array<float, 4>& levels) {
    NotorietyDisplayUpdate u;
    const float gang = std::max({levels[0], levels[1], levels[2]});
    u.police = police_.update(levels[3]);
    u.gang = gang_.update(gang);
    u.ringVisible = levels[3] >= 1.0f || gang >= 1.0f;
    u.playExpand = u.ringVisible && !ringVisible_;
    u.playContract = !u.ringVisible && ringVisible_;
    ringVisible_ = u.ringVisible;
    return u;
}

RespectMeterUpdate RespectDisplay::update(int32_t total, float pct, int32_t level) {
    RespectMeterUpdate u;
    u.levelText = std::to_string(level + 1);
    u.meterEnd = 2.65f - 5.25f * std::clamp(pct, 0.0f, 1.0f);
    u.meterBgEnd = u.meterEnd - 0.03f;
    if (primed_ && total > prevTotal_) u.gained = total - prevTotal_;
    primed_ = true;
    prevTotal_ = total;
    return u;
}

std::string AmmoText(const AmmoTextInput& in) {
    if (in.noAmmo) return std::string();
    if (in.noMag)
        return in.infAmmo ? std::string(kInfiniteAmmoImage) : std::to_string(in.rdy + in.rsv);
    if (in.contFire) {
        const auto rdy = static_cast<long long>(std::floor(in.rdy / 100.0));
        const auto rsv = static_cast<long long>(std::floor(in.rsv / 100.0));
        return std::to_string(rdy) + "/" + std::to_string(rsv);
    }
    if (in.infAmmo) return std::to_string(in.rdy) + "/" + kInfiniteAmmoImage;
    return std::to_string(in.rdy) + "/" + std::to_string(in.rsv);
}

std::string GrenadeText(int32_t count) {
    return count == 0 ? std::string() : std::to_string(count);
}

std::string WeaponIconBitmap(const std::string& published) {
    return published.empty() ? std::string(kFistBitmap) : published;
}

float MinimapGroupAlpha(float fineAimTransition) {
    return std::clamp(1.2f - fineAimTransition, 0.0f, 1.0f);
}

}  // namespace sr3hud
