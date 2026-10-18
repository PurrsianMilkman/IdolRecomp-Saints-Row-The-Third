// sr3player::PlayerVitals - see include/sr3player/vitals.h for the spec basis and the CHOSEN/OPEN split.
#include "sr3player/vitals.h"

#include <algorithm>
#include <cmath>

namespace sr3player {

PlayerVitals::PlayerVitals(const Config& cfg) : cfg_(cfg) {
    if (cfg_.maxHealth < 0) cfg_.maxHealth = 0;
    if (cfg_.respawnDelayMs < 0) cfg_.respawnDelayMs = 0;
    health_ = cfg_.maxHealth;
    stats_.minHealth = health_;
}

bool PlayerVitals::damageTo(int32_t newHealth, int64_t nowMs) {
    const int32_t clamped = std::clamp(newHealth, 0, cfg_.maxHealth);
    if (clamped < health_) {
        ++stats_.damageEvents;
        stats_.totalDamage += static_cast<double>(health_ - clamped);
    }
    health_ = clamped;
    stats_.minHealth = std::min(stats_.minHealth, health_);
    if (health_ <= 0 && cfg_.maxHealth > 0) {
        dead_ = true;
        diedAtMs_ = nowMs;
        ++stats_.deaths;
        return true;
    }
    return false;
}

bool PlayerVitals::applyDamage(double amount, int64_t nowMs) {
    if (dead_ || !(amount > 0.0) || cfg_.maxHealth <= 0) return false;
    const double target = std::max(0.0, static_cast<double>(health_) - amount);  // Sec15.3a: health -= damage
    return damageTo(static_cast<int32_t>(std::lround(target)), nowMs);
}

bool PlayerVitals::observeHealth(int32_t hostHealth, int64_t nowMs) {
    if (dead_ || cfg_.maxHealth <= 0) return false;
    return damageTo(hostHealth, nowMs);
}

bool PlayerVitals::respawnDue(int64_t nowMs) const { return dead_ && nowMs - diedAtMs_ >= cfg_.respawnDelayMs; }

bool PlayerVitals::respawn() {
    if (!dead_) return false;
    dead_ = false;
    health_ = cfg_.maxHealth;
    ++stats_.respawns;
    return true;
}

}  // namespace sr3player
