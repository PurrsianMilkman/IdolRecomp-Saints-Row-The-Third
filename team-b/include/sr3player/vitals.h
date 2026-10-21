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
//   * Regeneration, the death/respawn sequence, respawn points and the death penalty: spec-player-runtime.md Sec18
//     (2026-10-18). The pure rules are the inline functions in namespace sr3player below (RegenWaitMs,
//     RegenRatePerSec, HealthRegen, PickRespawnPoint, DeathPenaltyCents); the engine-side hooks (damage received
//     multiplier, wait re-arm, 1/30 s tick) live in sr3luahost::EngineState, the playtest wiring in
//     tools/sr3_viewer/viewer_playtest_frame_vitals.cpp.
//
// Pure: no I/O, no clock. Time is passed in as milliseconds on any monotone clock.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sr3player {

// ---- spec-player-runtime.md Sec18.2 constants (FINAL unless noted) ----------------------------------------------
constexpr int64_t kRestoreWaitTimeMs = 6000;       // 0x014A13E4 restore_wait_time_ms
constexpr double kRestorePerSecSp = 125.0;         // 0x014A13CC restore_per_sec_sp
constexpr double kRestorePerSecVampireSp = 500.0;  // 0x014A13D0 (HIGH: only while holding a human shield)
constexpr double kRegenStepSeconds = 1.0 / 30.0;   // fixed step 0x0132a0ac
constexpr double kDeathCostDollars = 500.0;        // tweak Death_cost
constexpr double kBustedCostDollars = 500.0;       // tweak Busted_cost
constexpr double kBustHpPcnt = 0.6;                // Combat_AI Bust.Bust_HP_Pcnt

// Difficulty record (single-player set, Sec18.2): levels 0 easy / 1 normal / 2 hard.
struct DifficultyVitals {
    float damageReceivedMult, healthRegenWaitMult, healthRegenSpeedMult;
};
inline DifficultyVitals SpDifficultyVitals(int level) {
    static constexpr DifficultyVitals k[3] = {{0.3f, 0.6f, 1.3f}, {0.75f, 1.0f, 1.0f}, {1.5f, 1.3f, 1.0f}};
    return k[(level < 0 || level > 2) ? 1 : level];
}

// Sec18.3 item 1: wait_ms = trunc((6000 - reduction) * Health_Regen_Wait_Mult); 0 while holding a human shield.
// Returns -1 when the character's dont_regen_health flag is set (the timer is not armed).
inline int64_t RegenWaitMs(double reductionMs, float waitMult, bool holdingHumanShield, bool dontRegenHealth) {
    if (dontRegenHealth) return -1;
    if (holdingHumanShield) return 0;
    // trunc, NO rounding guard (D-12, spec-differential-results.md): 1.3f x 6000 = 7799.9997 -> 7799, as the original.
    return static_cast<int64_t>(static_cast<double>(static_cast<float>(static_cast<double>(kRestoreWaitTimeMs) - reductionMs)) * static_cast<double>(waitMult));  // D-12: no rounding guard
}

// Sec18.3 item 2: rate (HP/s) = 125 x Health_Regen_Speed_Mult x regen-speed factor (500 instead of 125 with a shield).
inline double RegenRatePerSec(float speedMult, double unlockFactor, bool holdingHumanShield) {
    // D-13: each product is rounded to single precision.
    const float base = static_cast<float>(holdingHumanShield ? kRestorePerSecVampireSp : kRestorePerSecSp);
    const float a = base * speedMult;
    return static_cast<double>(a * static_cast<float>(unlockFactor));
}

// Pure regeneration accumulator: feed real elapsed seconds; it runs the fixed 1/30 s steps. `hp` is the exact
// (fractional) health; the host rounds when it writes an integer health.
struct HealthRegen {
    double carrySeconds = 0.0;
    // Returns the new hp. `ready` = the wait timer has expired. Regeneration runs to maxHp (restore_max_pct_sp 1.0).
    double step(double hp, double maxHp, bool ready, double ratePerSec, double dtSeconds) {
        carrySeconds += dtSeconds;
        while (carrySeconds >= kRegenStepSeconds - 1e-9) {
            carrySeconds -= kRegenStepSeconds;
            if (ready && hp < maxHp) {
                // D-13: health is single precision; delta = single(rate x single(1/30)); health = clamp(single(health + delta)).
                const float delta = static_cast<float>(ratePerSec) * (1.0f / 30.0f);
                const float h = static_cast<float>(hp) + delta;
                hp = std::clamp(static_cast<double>(h), 0.0, maxHp);
            }
        }
        return hp;
    }
};

// ---- Sec18.5 respawn points ---------------------------------------------------------------------------------------
constexpr uint32_t kRespawnFlagDeath = 0x1, kRespawnFlagBusted = 0x2, kRespawnFlagPassedOut = 0x8;
struct RespawnPoint {
    const char* name;
    double x, y, z;
    double qx, qy, qz, qw;  // orientation quaternion as listed in the spec
    uint32_t flags;         // level_respawn_flags bits
    int hoodType;           // island type of the point's hood: -1 none, 0 ocean, 1 mainland, 2 prison, 3 power plant
};
// The complete shipped set (both city archives, spec Sec18.5 table). hoodType 1 is HIGH, not read (the spec only
// says the island rule exists); the picker treats 1 and -1 alike.
inline const RespawnPoint* ShippedRespawnPoints(size_t& count) {
    static constexpr RespawnPoint k[4] = {
        {"ne_hospital", -1641.3849, 8.5352, -644.6879, 0.0, -0.7071, 0.0, 0.7072, kRespawnFlagDeath, 1},
        {"dt_hospital", 117.5149, 12.15, 52.4055, 0.0, 0.7071, 0.0, 0.7071, kRespawnFlagDeath, 1},
        {"sw_hospital", 859.4623, 6.0703, 1080.6064, 0.0, -1.0, 0.0, 0.0, kRespawnFlagDeath, 1},
        {"nw_hospital", 1375.2847, 26.1484, 64.979, 0.0, -0.7071, 0.0, 0.7071, kRespawnFlagDeath, 1}};
    count = 4;
    return k;
}
// Step 4 of Sec18.5: qualifies if (flags&1 && obj&1) || (flags&2 && obj&2) || (flags&8 && obj&8); island rule; keep the
// smallest 3-D distance. `playerIsland` = T (-1 none). Returns nullptr when nothing qualifies.
inline const RespawnPoint* PickRespawnPoint(const RespawnPoint* pts, size_t n, double px, double py, double pz,
                                            uint32_t flags, int playerIsland) {
    const RespawnPoint* best = nullptr;
    double bestD = 3.4028234663852886e38;
    for (size_t i = 0; i < n; ++i) {
        const RespawnPoint& p = pts[i];
        if (!(((flags & 1) && (p.flags & 1)) || ((flags & 2) && (p.flags & 2)) || ((flags & 8) && (p.flags & 8))))
            continue;
        const bool restrictedIsland = playerIsland == 2 || playerIsland == 3;
        if (restrictedIsland ? p.hoodType != playerIsland : (p.hoodType == 2 || p.hoodType == 3)) continue;
        const double dx = p.x - px, dy = p.y - py, dz = p.z - pz;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < bestD) {
            bestD = d;
            best = &p;
        }
    }
    return best;
}

// Sec18.7: cents taken. cost > cash/100 -> everything; else int(cost*100). Cash never goes below 0.
inline int64_t DeathPenaltyCents(int64_t cashCents, double costDollars) {
    if (cashCents <= 0) return 0;
    if (costDollars > static_cast<double>(cashCents) / 100.0) return cashCents;
    return static_cast<int64_t>(costDollars * 100.0);
}

// spec-tables-progression.md (cash cap): new = clamp(cash + raw, -2e9, +2e9) saturating, then floored at 0; maximum 2,000,000,000
// cents ($20,000,000), overflow dropped. Raw adders are themselves saturated to +/-2e9 first (the x100 step 0x009601D0).
constexpr double kMaxCashCents = 2000000000.0;
inline double AddCashSaturating(double cashCents, double addCents) {
    addCents = std::max(-kMaxCashCents, std::min(kMaxCashCents, addCents));
    double n = std::max(-kMaxCashCents, std::min(kMaxCashCents, cashCents + addCents));
    return n < 0.0 ? 0.0 : n;
}

// spec-differential-results.md D-22 (vectors: spec-differential-vectors-cash.md). Wallet add in raw cents.
// Local player + positive amount: scaled by the global cash multiplier (type 38 Cash_Bonus_Modifier); the sum is signed 32-bit and wraps
// BEFORE the +-2e9 clamp and the floor at 0.
inline int32_t AddCashWallet(int32_t cash, int32_t amount, float multiplier, bool localPlayer) {
    constexpr int64_t kCap = 2000000000;
    int64_t amt = amount;
    if (localPlayer && amount > 0) {
        const float dollars = static_cast<float>(amount) / 100.0f;
        const float scaled = static_cast<float>(static_cast<double>(dollars) * static_cast<double>(multiplier) * 100.0);
        double r = std::round(static_cast<double>(scaled));  // half away from zero
        if (r > static_cast<double>(kCap)) r = static_cast<double>(kCap);
        if (r < -static_cast<double>(kCap)) r = -static_cast<double>(kCap);
        amt = static_cast<int64_t>(r);
    }
    if (amt > kCap) amt = kCap;
    if (amt < -kCap) amt = -kCap;
    const int32_t sum = static_cast<int32_t>(static_cast<uint32_t>(std::clamp<int64_t>(cash, -kCap, kCap) + amt));  // 32-bit wrap (stored cash is already within the cap)
    int64_t n = sum;
    if (n > kCap) n = kCap;
    if (n < -kCap) n = -kCap;
    return static_cast<int32_t>(n < 0 ? 0 : n);
}

// ---- Sec18.11 the fallback start object ---------------------------------------------------------------------------
// The single `player_start` navpoint (record 11 of zone sr3_city~f1709~al, type Ground; FINAL). Used when no level_respawn
// point qualifies: every bust (no `busted` point exists), and a death in a prison (2) / power-plant (3) hood.
inline const RespawnPoint& PlayerStartPoint() {
    static constexpr RespawnPoint k = {"player_start(sr3_city~f1709~al#11)", 1220.9398, 12.1574, 1047.1863,
                                       0.0, -0.9827, 0.0, 0.185, 0, 1};
    return k;
}
struct RespawnChoice {
    const RespawnPoint* point = nullptr;  // never null
    bool fallbackStart = false;           // true: the Sec18.5 step 6 start object was used
};
// Sec18.5 + Sec18.11: the picker over `pts`; when nothing qualifies the start object.
inline RespawnChoice ResolveRespawn(const RespawnPoint* pts, size_t n, double px, double py, double pz, uint32_t flags,
                                    int playerIsland) {
    RespawnChoice c;
    c.point = PickRespawnPoint(pts, n, px, py, pz, flags, playerIsland);
    if (!c.point) {
        c.point = &PlayerStartPoint();
        c.fallbackStart = true;
    }
    return c;
}
// Sec18.5 step 5 (co-op, FINAL for the offset): 1.5 m along orientation row 0, negated on the non-host.
// CHOSEN: row 0 is taken from the standard unit-quaternion rotation matrix (the spec gives the quaternion, not the matrix).
struct Vec3d {
    double x, y, z;
};
inline Vec3d CoopRespawnPosition(const RespawnPoint& p, bool isHost) {
    const double row0x = 1.0 - 2.0 * (p.qy * p.qy + p.qz * p.qz);
    const double row0y = 2.0 * (p.qx * p.qy - p.qz * p.qw);
    const double row0z = 2.0 * (p.qx * p.qz + p.qy * p.qw);
    const double s = isHost ? 1.5 : -1.5;
    return {p.x + row0x * s, p.y + row0y * s, p.z + row0z * s};
}

// ---- Sec18.10 arrest (dormant in the retail build; opt-in here) ---------------------------------------------------
constexpr double kBustApproachDistSq = 25.0;  // 5 m, squared
constexpr double kCuffRampSeconds = 1.5;      // +0xd48 += (1/30)/1.5 per tick
constexpr double kCuffCommitPhase = 0.75;     // animation phase that commits the bust
struct BustGateInput {
    bool copIsPolice = true;          // 1: team byte +0x1ca8 == 5
    bool arrestLockClear = true;      // 2/8: player +0x28ac bit 0x40 clear, +0x1d99 bit 0x40 clear
    bool playerRagdolled = false;     // 3 (CHOSEN stand-in in free roam, see the playtest wiring)
    bool playerDead = false;          // 4
    double hp = 0.0, maxHp = 0.0;     // 5
    bool inAction7Sub9 = false;       // 6
    bool bustBlocked = false;         // 7: +0x1c98 bit 0x40 (set_player_can_be_busted false) or global gate 0x00720640
    double distSqToApproach = 1e30;   // 9
};
inline bool BustHpGate(double hp, double maxHp) { return maxHp > 0.0 && hp / maxHp <= kBustHpPcnt; }
inline bool BustGatesPass(const BustGateInput& g) {
    return g.copIsPolice && g.arrestLockClear && g.playerRagdolled && !g.playerDead && BustHpGate(g.hp, g.maxHp) &&
           !g.inAction7Sub9 && !g.bustBlocked && g.distSqToApproach <= kBustApproachDistSq;
}
// The cuffing state (7,10): +0xd48 ramps to 1.0 over 1.5 s at the fixed 1/30 s step; commit when the animation phase >= 0.75.
struct CuffState {
    double ramp = 0.0;  // +0xd48
    int ticks = 0;
    void tick() {
        ramp = std::min(1.0, ramp + kRegenStepSeconds / kCuffRampSeconds);
        ++ticks;
    }
    static bool Commit(double animPhase) { return animPhase >= kCuffCommitPhase; }
};

// ---- Sec18.12 fade back in --------------------------------------------------------------------------------------
constexpr int64_t kWipeInMs = 833;                // left_to_right_none_twn 0.8333 s (style 0 wipe, not an alpha fade)
constexpr int64_t kLoadLogoAfterMs = 1000;        // logo stamped at fade-out completion + 1000
constexpr int64_t kMissionWipeToBlackMs = 1033;   // Sec18.16 item 2: left_to_right_black_twn 1.0333 s
struct FadeInPlan {
    int64_t wipeStartMs = 0, wipeEndMs = 0;
    bool logoShown = false;
    int64_t logoAtMs = 0;
};
// `fadeOutDoneMs` = T0 (fade-out completion, phase 2 runs); `destLoadedMs` = when the destination finished loading.
// Loaded by the logo time: the wipe starts at the load time. Otherwise the logo shows at T0+1000 and the held fade-in is
// replayed at max(load, logo + 1000). [HIGH]
inline FadeInPlan PlanFadeIn(int64_t fadeOutDoneMs, int64_t destLoadedMs) {
    FadeInPlan p;
    p.logoAtMs = fadeOutDoneMs + kLoadLogoAfterMs;
    if (destLoadedMs <= p.logoAtMs) {
        p.wipeStartMs = std::max(destLoadedMs, fadeOutDoneMs);
    } else {
        p.logoShown = true;
        p.wipeStartMs = std::max(destLoadedMs, p.logoAtMs + kLoadLogoAfterMs);
    }
    p.wipeEndMs = p.wipeStartMs + kWipeInMs;
    return p;
}

// ---- Sec18.14 Player_Health_Bonus ---------------------------------------------------------------------------------
struct MaxHpBonusState {
    int32_t maxHp = 0, bonus = 0, hp = 0;
};
// base = maxHp - bonus; bonus' = trunc(base * (M - 1)); maxHp += bonus' - bonus; hp clamped, never healed.
inline void ApplyPlayerHealthBonus(MaxHpBonusState& s, double modifier) {
    const int32_t base = s.maxHp - s.bonus;
    const int32_t nb = static_cast<int32_t>(static_cast<double>(base) * (modifier - 1.0));
    s.maxHp += nb - s.bonus;
    s.bonus = nb;
    if (s.hp > s.maxHp) s.hp = s.maxHp;
}

// ---- Sec18.13 full-restore timer (ambulance marker) -----------------------------------------------------------
constexpr int64_t kAmbulanceFullRestoreMs = 500;  // literal; full_restore_time_ms 2000 has no reader

// ---- Sec18.16 deaths during a mission ---------------------------------------------------------------------------
enum class MissionDeathChoice { Retry, Quit };
struct MissionDeathOutcome {
    bool hospitalRespawn = false;   // 0x00703840 phase 2 runs (nearest point, $500, followers, notoriety)
    bool checkpointRevive = false;  // revive in place at the checkpoint (HP max, timer disarmed, latch clear)
    bool penalty = false;
    bool instantWipeToBlack = false;
};
// Retry before phase 2 -> checkpoint revive only (no hospital, no penalty). Quit -> the free-roam chain at once (unless
// phase 2 already ran). [HIGH]
inline MissionDeathOutcome ResolveMissionDeath(MissionDeathChoice c, bool phase2AlreadyRan) {
    MissionDeathOutcome o;
    if (c == MissionDeathChoice::Retry) {
        o.checkpointRevive = true;
        return o;
    }
    o.instantWipeToBlack = true;
    o.hospitalRespawn = !phase2AlreadyRan;
    o.penalty = !phase2AlreadyRan;
    return o;
}

// ---- Sec18.6 "respawn reset calls, in order" (spec-player-runtime.md, 2026-10-19 R3 note; 0x009e6f20) -------------
// The 18 ordered steps of the revive. The ORDER and the writes are FINAL; `modelled` is false where the spec gives only a
// role (or the target subsystem is absent from this port) so the viewer logs the step without a state change.
enum class RespawnStep : int {
    GateLocalPlayer = 1,        // return unless local player
    ReleaseCarriedObject = 2,   // role only
    HudReset = 3,               // hud_player_reset_complete / hud_hit_clear_all (no HUD backend here)
    HealthRestore = 4,          // +0x1cb8 := +0x1cac; +0x1d99 bit 0x40 cleared
    RagdollStop = 5,            // stop ragdoll / clear +0xe8 bit 2, +0xcd2 := 0
    DownedLeave = 6,            // leave the downed state
    MovementAnimReset = 7,      // role only
    FlagClears = 8,             // +0xe8 / +0xe0 bits, +0x20b8 / +0x20bc / +0xa14 (no model here)
    VehicleSeatExit = 9,        // leave a vehicle or seat state
    CrouchClear = 10,           // +0xe8 bit 0x20 (crouch)
    ControllerAnimStop = 11,    // role only
    Teleport = 12,              // to the chosen respawn point
    InventorySlot0 = 13,        // select slot 0 (no selection backend here)
    Notoriety = 14,             // single-player: clear tracks
    ControlsOn = 15,            // +0x1d99 bit 0x08 cleared
    BustedClear = 16,           // +0x1d94 bit 0x02 (busted), +0x1d96 bit 0x01
    ReleaseHandles = 17,        // +0x2800 / +0x2808 / +0x2810
    DeathLatchAndHook = 18      // +0xe8 bit 1 (on_death latch), +0x1ef4 := -1; ...; on_respawn hook slot 5
};
struct RespawnStepInfo {
    RespawnStep step;
    const char* name;
    bool modelled;  // true: the viewer applies a state change for this step
};
inline const RespawnStepInfo* RespawnResetOrder(size_t& n) {
    static const RespawnStepInfo kOrder[18] = {
        {RespawnStep::GateLocalPlayer, "gate_local_player", true},
        {RespawnStep::ReleaseCarriedObject, "release_carried_object", false},
        {RespawnStep::HudReset, "hud_reset", false},
        {RespawnStep::HealthRestore, "health_restore", true},
        {RespawnStep::RagdollStop, "ragdoll_stop", true},
        {RespawnStep::DownedLeave, "downed_leave", true},
        {RespawnStep::MovementAnimReset, "movement_anim_reset", false},
        {RespawnStep::FlagClears, "flag_clears", false},
        {RespawnStep::VehicleSeatExit, "vehicle_seat_exit", true},
        {RespawnStep::CrouchClear, "crouch_clear", true},
        {RespawnStep::ControllerAnimStop, "controller_anim_stop", false},
        {RespawnStep::Teleport, "teleport", true},
        {RespawnStep::InventorySlot0, "inventory_slot0", false},
        {RespawnStep::Notoriety, "notoriety", true},
        {RespawnStep::ControlsOn, "controls_on", true},
        {RespawnStep::BustedClear, "busted_clear", true},
        {RespawnStep::ReleaseHandles, "release_handles", false},
        {RespawnStep::DeathLatchAndHook, "death_latch_and_on_respawn_hook", true},
    };
    n = 18;
    return kOrder;
}

constexpr int kDeathCameraMode = 9;  // spec-player-runtime.md Sec4.1/Sec4.2 (mode 9 = death camera), Sec15.4

class PlayerVitals {
public:
    struct Config {
        int32_t maxHealth = 0;          // > 0 required for any damage to register
        int64_t respawnDelayMs = 4000;  // Death_and_busted.respawn_delay_ms (shipped value 4000)
        // Sec18.4: the fade to black uses respawn_delay_ms again; fadeMs < 0 means "same as respawnDelayMs".
        int64_t fadeMs = -1;
    };

    struct Stats {
        uint64_t damageEvents = 0;
        double totalDamage = 0.0;
        uint64_t deaths = 0;
        uint64_t respawns = 0;
        uint64_t arrests = 0;  // profile stat 11 (Sec18.10)
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

    // Sec18.4 phase 2: the on_death moment. True once respawnDelayMs has elapsed since death (dead state only).
    // (The name predates Sec18: the actual teleport waits for the fade below.)
    bool respawnDue(int64_t nowMs) const;

    // Sec18.4 phase 3: when the on_death script has returned, start the fade to black (once). Returns true if started.
    bool beginFade(int64_t nowMs);
    bool fading() const { return fadeStartedMs_ >= 0; }
    int64_t fadeMs() const { return cfg_.fadeMs < 0 ? cfg_.respawnDelayMs : cfg_.fadeMs; }
    // 0..1 black level of the fade-out (0 when not fading).
    double fadeAlpha(int64_t nowMs) const;
    // True once the fade has run its full duration: the teleport/revive step (Sec18.4 phase 4) runs now.
    bool fadeComplete(int64_t nowMs) const { return fading() && nowMs - fadeStartedMs_ >= fadeMs(); }

    // The respawn action: restores full health, clears the death state. No-op (returns false) unless dead.
    bool respawn();

    int32_t health() const { return health_; }
    int32_t maxHealth() const { return cfg_.maxHealth; }
    // Sec18.14: a max-HP change (Player_Health_Bonus) never heals; current HP is clamped to the new max.
    void setMaxHealth(int32_t m) {
        cfg_.maxHealth = m < 0 ? 0 : m;
        if (health_ > cfg_.maxHealth) health_ = cfg_.maxHealth;
    }
    bool dead() const { return dead_; }
    // Sec18.10 (0x009dab20 + 0x00703770): the bust commit arms the same death timer, sets the busted flag (+0x1d94 bit 2),
    // controls off, stat 11 += 1. No-op (false) if already dead/busted. Health is NOT changed.
    bool bust(int64_t nowMs);
    bool busted() const { return busted_; }
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
    bool busted_ = false;
    int64_t diedAtMs_ = 0;
    int64_t fadeStartedMs_ = -1;
    Stats stats_;
};

}  // namespace sr3player
