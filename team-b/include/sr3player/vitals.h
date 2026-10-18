#pragma once

// sr3player/vitals.h - player health / death / free-roam respawn state machine (task T1).
//
// Spec basis (spec-player-runtime.md, spec-tables-progression.md):
//   * Sec15.3a (CONFIRMED): health is a clamped [0, max] value; ordinary damage is `health -= damage`; a hit that
//     takes health to 0 is lethal. The damage sink itself lives in sr3luahost::EngineState::applyCharacterDamage;
//     this class does NOT re-implement it. It OBSERVES the resulting health (observeHealth) or, standalone (unit
//     tests, no EngineState), applies damage itself (applyDamage).
//   * Sec15.4 (CONFIRMED): the player death trigger 0x00702810 debounces via a per-player timer, reads
//     respawn_delay_ms (gameplay_constants.xtbl Death_and_busted, 4000 in the shipped data), sets the "controls
//     disabled" flag (+0x1d99 bit 0x08), and pushes camera mode 9 (death camera). Those four facts are modelled:
//     dead() / controlsDisabled() / cameraMode() / respawn due once respawnDelayMs has elapsed since death.
//   * Sec15.4 OPEN: what a free-roam (no active mission) death does after the delay. In a mission the respawn is the
//     checkpoint restore; in free roam it is unspecified. This class only reports respawnDue(); the destination and
//     the notoriety question are CHOSEN by the caller (see the playtest wiring).
//   * player_health.restore_* (regen) has no confirmed consumer (Sec10.2): NO regeneration is modelled here.
//
// Pure: no I/O, no clock. Time is passed in as milliseconds on any monotone clock.

#include <cstdint>

namespace sr3player {

constexpr int kDeathCameraMode = 9;  // spec-player-runtime.md Sec4.1/Sec4.2 (mode 9 = death camera), Sec15.4

class PlayerVitals {
public:
    struct Config {
        int32_t maxHealth = 0;          // > 0 required for any damage to register
        int64_t respawnDelayMs = 4000;  // Death_and_busted.respawn_delay_ms (shipped value 4000)
    };

    struct Stats {
        uint64_t damageEvents = 0;
        double totalDamage = 0.0;
        uint64_t deaths = 0;
        uint64_t respawns = 0;
        int32_t minHealth = 0;  // lowest health ever seen (starts at max)
    };

    explicit PlayerVitals(const Config& cfg);

    // Standalone damage intake: health = clamp(health - amount, 0, max). Ignored while dead or amount <= 0.
    // Returns true when this call killed the player.
    bool applyDamage(double amount, int64_t nowMs);

    // Host-driven intake: the host (EngineState) already applied the damage; feed the resulting health.
    // A decrease counts as a damage event, an increase as a heal (clamped). Ignored while dead (a dead player's
    // host health is only changed by respawn()). Returns true when this call killed the player.
    bool observeHealth(int32_t hostHealth, int64_t nowMs);

    // True once respawnDelayMs has elapsed since death (dead state only).
    bool respawnDue(int64_t nowMs) const;

    // The respawn action: restores full health, clears the death state. No-op (returns false) unless dead.
    bool respawn();

    int32_t health() const { return health_; }
    int32_t maxHealth() const { return cfg_.maxHealth; }
    bool dead() const { return dead_; }
    bool controlsDisabled() const { return dead_; }                  // +0x1d99 bit 0x08 set by 0x00702810
    int cameraMode() const { return dead_ ? kDeathCameraMode : 0; }  // 0 = follow camera
    int64_t diedAtMs() const { return diedAtMs_; }
    int64_t respawnDelayMs() const { return cfg_.respawnDelayMs; }
    const Stats& stats() const { return stats_; }

private:
    bool damageTo(int32_t newHealth, int64_t nowMs);
    Config cfg_;
    int32_t health_ = 0;
    bool dead_ = false;
    int64_t diedAtMs_ = 0;
    Stats stats_;
};

}  // namespace sr3player
