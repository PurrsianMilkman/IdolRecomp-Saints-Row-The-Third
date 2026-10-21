#pragma once

// sr3airuntime/homies.h - homies and followers: roster status, follower cap, which follower a full party drops, the
// summon (spawn point search, approach speed), the call cooldown, follow gait / radii, friendly-fire meter, the
// abandon rule, the hospital bill. Pure functions/objects (no host, no rendering).
//
// SOURCE: spec-ai-runtime.md Sec24 (and only that). Tags below follow the spec: FINAL / HIGH / CHOSEN (= this port's
// own stand-in where the spec is OPEN or GUESS; each CHOSEN is a Team A question in the task report).
//
// Answered by Team A 2026-10-20 (spec-conformance-review.md "Answers to Team B CHOSEN questions"): roster rows start at
// status 0 (InitialStatus); drive-up arrival = speed < 1.0 STRICT (no distance test, no boarding from the homie car);
// walk/run speeds are the clip root motion (1.386 / 6.373); no distance rule for the unstreamed teleport; the dismiss
// control dismisses the whole party; the revive prompt radius is 3.0 m; free seats = seat count - driver - occupied.
//
// CHOSEN (spec OPEN/GUESS):
//   * sprint gait speed (not measured);
//   * hp term of the dismissal score is clamped at 1 hp (1/HP is undefined at 0).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "sr3airuntime/common.h"

namespace sr3airuntime::homies {

// ---- Sec24.6 record status (+0xBC) -----------------------------------------------------------------------------
enum Status : int {
    kLocked = 0,
    kAvailable = 1,
    kCalled = 2,     // called, en route
    kSpawned = 3,    // spawned and joined
    kFollowing = 4,
    kDowned = 5,
    kCooldown = 6,
    kHidden = 7,
    kSpecial = 8,
};

// ---- Sec24.3 follower cap (FINAL) -------------------------------------------------------------------------------
struct CapInputs {
    bool leaderIsPlayer = true;
    bool recruitable = true;           // party_set_recruitable (global, default 1)
    bool allowMaxFollowers = false;    // party_allow_max_followers (player flag)
    bool multiPlayerSession = false;   // networked session with more than one player
    bool gameMode11or12 = false;       // only matters in a multi-player session
    bool localPlayerFlag1ef0 = true;   // player byte +0x1ef0 == 1 (GUESS meaning: local player in normal play)
    int slotsUnlocked = 1;             // HomieSlot1..3 unlocked in order
};
inline constexpr int kNpcLeaderCap = 5;

// 0x0071b740: how many of the three slots are unlocked in order; the count stops at the first locked slot.
inline int UnlockedSlots(bool slot1, bool slot2, bool slot3) {
    if (!slot1) return 0;
    if (!slot2) return 1;
    return slot3 ? 3 : 2;
}

inline int FollowerCap(const CapInputs& in) {
    if (!in.leaderIsPlayer) return kNpcLeaderCap;
    if (!in.recruitable && !in.allowMaxFollowers) return 0;
    int cap = in.multiPlayerSession ? 1 : 3;
    if (in.multiPlayerSession && in.gameMode11or12) cap = 2;
    if (!in.allowMaxFollowers && in.localPlayerFlag1ef0) cap = std::min(cap, in.slotsUnlocked);
    return cap;
}

// ---- Sec24.4 which follower a full party drops (FINAL arithmetic) --------------------------------------------------
struct DismissCandidate {
    double hp = 1.0;               // +0x1cb8
    int weaponCount = 0;           // k (HIGH: inventory count)
    int personaInt = 0;            // +0xf8
    bool instanceOverride = false; // +0x1da8 exists (+5)
    bool flag1e00bit7 = false;     // +10, and force-only
    bool boundToHomieRecord = false;  // +20
    bool neverTaken = false;       // +0x1e02 bit 3: score fixed at 100
    bool flag1da5bit4 = false;     // force-only (temporary, not dismissable by a call)
};
inline double DismissScore(const DismissCandidate& c) {
    if (c.neverTaken) return 100.0;
    const double hp = std::max(1.0, c.hp);
    double s = (1.0 - 1.0 / hp) + (1.0 - 1.0 / (static_cast<double>(c.weaponCount) + 1.0)) + c.personaInt;
    if (c.instanceOverride) s += 5.0;
    if (c.flag1e00bit7) s += 10.0;
    if (c.boundToHomieRecord) s += 20.0;
    return s;
}
// Index of the member removed (lowest score below 100), or -1. Ties: the earliest member.
inline int PickMemberToDrop(const std::vector<DismissCandidate>& party, bool force) {
    int best = -1;
    double bestScore = 100.0;
    for (size_t i = 0; i < party.size(); ++i) {
        const DismissCandidate& c = party[i];
        if ((c.flag1da5bit4 || c.flag1e00bit7) && !force) continue;
        const double s = DismissScore(c);
        if (s < bestScore) {
            bestScore = s;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// ---- Sec24.6 cooldown (FINAL; boundary HIGH) ------------------------------------------------------------------------
inline constexpr double kDaySeconds = 86400.0;
inline constexpr double kHourSeconds = 3600.0;
// Stamp = game clock + 1 game day (1 game hour for a `player selects vehicle` row).
inline double CooldownStamp(double gameClockSec, bool playerSelectsVehicle) {
    return gameClockSec + (playerSelectsVehicle ? kHourSeconds : kDaySeconds);
}
// 0x007e61a0: status 6 turns back into 1 once the clock has passed the stamp (strictly after, HIGH).
inline bool CooldownReleased(double gameClockSec, double stamp) { return gameClockSec > stamp; }
// 0x007e61e0: a locked/hidden record (0 or 7) becomes locked (0); everything else enters the cooldown (6).
inline int StatusAfterRemoval(int status) { return (status == kLocked || status == kHidden) ? kLocked : kCooldown; }
// Sec24.6 FINAL (Team A 2026-10-20): every row starts at 0; it is listed (1) only when the unlockable (type Homie, live
// byte +0xDC) is set or homie_mission_unlock ran for it.
inline int InitialStatus(bool unlockableSet, bool missionUnlockRan) { return (unlockableSet || missionUnlockRan) ? kAvailable : kLocked; }
// Callable from the phone: listed and available (status 6 answers busy).
inline bool Callable(int status) { return status == kAvailable || status == kSpecial; }

// ---- Sec24.5 step 4 spawn point (FINAL) -----------------------------------------------------------------------------
inline constexpr std::array<double, 3> kSpawnDistancesM = {200.0, 150.0, 100.0};
inline constexpr double kSpawnMinFromPlayerM = 95.0;       // squared 9025
inline constexpr double kHeadingSpeedThreshold = 5.0;      // m/s: velocity is the heading above this, else the facing

struct SpawnCandidate {
    Vec3 pos;
    int direction = 0;   // 0 ahead, 1/2 the perpendiculars in the tried order, 3 behind
    double distance = 0.0;
};
// The 12 candidates in the order they are tried. `coin` in [0,1): < 0.5 tries the cross(heading, up) side first.
inline std::vector<SpawnCandidate> SpawnCandidates(const Vec3& playerPos, const Vec3& playerVel, const Vec3& facing,
                                                   double coin) {
    Vec3 h = playerVel;
    h.y = 0.0;
    double len = std::hypot(h.x, h.z);
    if (len <= kHeadingSpeedThreshold) {
        h = facing;
        h.y = 0.0;
        len = std::hypot(h.x, h.z);
    }
    if (len < 1e-9) h = {0.0, 0.0, 1.0}, len = 1.0;
    const Vec3 fwd{h.x / len, 0.0, h.z / len};
    const Vec3 perpA{-fwd.z, 0.0, fwd.x};  // cross(heading, (0,1,0))
    const Vec3 perpB{fwd.z, 0.0, -fwd.x};
    const bool aFirst = coin < 0.5;
    const Vec3 dirs[4] = {fwd, aFirst ? perpA : perpB, aFirst ? perpB : perpA, {-fwd.x, 0.0, -fwd.z}};
    std::vector<SpawnCandidate> out;
    for (int d = 0; d < 4; ++d)
        for (double dist : kSpawnDistancesM)
            out.push_back({{playerPos.x + dirs[d].x * dist, playerPos.y, playerPos.z + dirs[d].z * dist}, d, dist});
    return out;
}
// The validator's player-distance rule: a candidate within 95 m of any player is rejected.
inline bool FarEnoughFromPlayers(const Vec3& p, const std::vector<Vec3>& players) {
    for (const Vec3& q : players) {
        const double dx = p.x - q.x, dz = p.z - q.z;
        if (dx * dx + dz * dz < kSpawnMinFromPlayerM * kSpawnMinFromPlayerM) return false;
    }
    return true;
}

// ---- Sec24.5 step 6 approach speed (HIGH) ---------------------------------------------------------------------------
inline constexpr double kApproachNoVehicleSpeed = 10.0;
inline constexpr double kApproachMaxSpeed = 35.0;
inline constexpr double kApproachMatchMin = 20.0;
struct ApproachInputs {
    bool hasVehicle = true;
    double topSpeed = 40.0;            // 0x00a6baa0
    bool following = true;
    bool targetAheadNotHeadOn = false; // unit offset . vehicle forward > 0.5 and not closing head-on
    bool targetInMovingVehicle = false;
    double targetVehicleSpeed = 0.0;
};
inline double ApproachSpeed(const ApproachInputs& in) {
    if (!in.hasVehicle) return kApproachNoVehicleSpeed;
    double v = std::min(in.topSpeed, kApproachMaxSpeed);
    if (in.following && in.targetAheadNotHeadOn && in.targetInMovingVehicle)
        v = std::max(kApproachMatchMin, std::min(kApproachMaxSpeed, in.targetVehicleSpeed));
    return v;
}
// Sec24.5 step 8 (FINAL, Team A 2026-10-20): the drive-up (mode 3 sub-state 7) arrives when the body speed is below
// 1.0 m/s STRICT (exactly 1.0 does not arrive; speed 0 when the state byte != 1); the crew then gets out and is released.
inline constexpr double kArrivalSpeedMps = 1.0;
inline bool DriveUpArrived(double speed) { return speed < kArrivalSpeedMps; }
// Sec24.8 (FINAL source, HIGH use): free passenger seats = seat count (vehicle +0x824) - driver - occupied.
// Sec24.9 (HIGH): follower max HP = the rank Hit_Points of the character's character.xtbl Spawn_Info_Rank (the engine
// clamps the u16 field to 0x7FFF), scaled at join by Homie_Health_Bonus (global 0x012F5D08, base 1.0). A missing rank
// keeps the 100 stand-in. Rounding of the scaled value is not specified: the product is kept as is (CHOSEN).
inline constexpr double kFallbackFollowerHp = 100.0;
inline double FollowerMaxHp(bool rankFound, double rankHitPoints, double homieHealthBonus) {
    if (!rankFound) return kFallbackFollowerHp;
    const double hp = std::min(std::max(rankHitPoints, 0.0), 32767.0) * homieHealthBonus;
    return hp > 0.0 ? hp : kFallbackFollowerHp;
}
// The seat count comes from the vehicle (0x00ab4af0: vehicle +0x824, 8 for other kinds); its writer was not traced, so the
// per-vehicle value here is the number of seats the vehicle's own seat data (vehicle_interaction_info.xtbl Seat_Info rows
// that resolve to one of the 8 seat names) defines (CHOSEN reading of "from the vehicle's seat data"), clamped to 1..8;
// 0 resolved seats means no seat data was found and the stand-in kFallbackSeatCount applies.
inline constexpr int kFallbackSeatCount = 4;
inline int SeatCountFromSeatData(int resolvedSeats) {
    if (resolvedSeats <= 0) return kFallbackSeatCount;
    return std::min(resolvedSeats, 8);
}
inline int FreeSeats(int seatCount, int occupiedPassengers) { return std::max(0, seatCount - 1 - occupiedPassengers); }
// Phone timers (Sec16a.2, FINAL Team D emulation): updates step 1/30 s; second ring at 1.000 s, connect at 1.033 s,
// summon at 1.067 s, the phone closes at 3.033 s.
inline constexpr int64_t kRingToConnectMs = 1033, kConnectToSummonMs = 33;
// Close: the 3.0 s timer (0x01116444) set at the summon update goes strictly below 0 after 91 updates = 3.033 s
// (update 123, t = 4.100 s from the dial). The phone is then back in state 0 and the end-of-call callback fires.
inline constexpr int64_t kSummonToPhoneCloseMs = 3033;
inline bool PhoneStillOpen(int64_t nowMs, int64_t closeAtMs) { return closeAtMs >= 0 && nowMs < closeAtMs; }

// ---- Sec24.7 following on foot -----------------------------------------------------------------------------------
inline constexpr double kReviveRadiusM = 3.0;               // Sec24.9 (HIGH): prompt radius, squared 9
inline constexpr double kFormationRadiusM = 2.5;        // action 32 accepts beyond (on-foot leader)
inline constexpr double kFollowPlayerRadiusM = 6.0;     // action 34 accepts beyond (0x013bae40 = 36 squared)
inline constexpr double kMakeWayRadiusM = 1.2;          // action 30 (1.44 squared), player in a vehicle state
inline constexpr double kTeleportOnFootM = 100.0, kTeleportInVehicleM = 150.0, kTeleportHeightM = 4.0;
inline constexpr double kGaitRunBeyondM = 7.5, kGaitSprintBeyondM = 15.0;
inline constexpr double kSquadmateWaitDistM = 2.0;
inline constexpr double kSquadmateConeCos = 0.7071;
inline constexpr int64_t kSquadmateWaitMs = 1500;
inline constexpr int64_t kTargetCopyMs = 2000;          // Sec21.2

inline bool FollowPlayerAccepts(double dist) { return dist > kFollowPlayerRadiusM; }
inline bool FollowFormationAccepts(double dist) { return dist > kFormationRadiusM; }
// 1 walk, 2 run, 3 sprint. Gait 0 is never chosen while following.
inline int FollowGait(int playerGait, double dist) {
    int g = std::max(playerGait, 1) + (dist > kGaitRunBeyondM ? 1 : 0) + (dist > kGaitSprintBeyondM ? 1 : 0);
    return std::min(g, 3);
}
// Sec24.7 (HIGH): no constants exist; speed = clip root motion. Measured male ped: walk 1.386, run 6.373 m/s. The sprint
// speed was not measured (CHOSEN: max(6.5, run)).
inline double GaitSpeed(int gait) {
    switch (gait) {
        case 1: return 1.386;
        case 2: return 6.373;
        default: return 6.5;
    }
}
// Action 35 follow teleport: beyond 100 m (150 m with the player in a vehicle) with more than 4 m height difference.
inline bool FollowTeleportApplies(double dist, double heightDiff, bool playerInVehicle) {
    return dist > (playerInVehicle ? kTeleportInVehicleM : kTeleportOnFootM) && std::fabs(heightDiff) > kTeleportHeightM;
}
// The "unstreamed follower is teleported" rule is a STREAMING STATE, not a distance (Team A 2026-10-20): no distance
// stand-in exists; the 100 m / 150 m + height rule above is the only distance rule.
// Squadmate spacing: another group member ahead within 45 degrees and 2 m horizontally -> stop and wait 1500 ms.
inline bool SquadmateAhead(const Vec3& selfFacing, const Vec3& offsetToMate) {
    const double dx = offsetToMate.x, dz = offsetToMate.z;
    const double d2 = dx * dx + dz * dz;
    if (d2 >= kSquadmateWaitDistM * kSquadmateWaitDistM || d2 < 1e-12) return false;
    const double fl = std::hypot(selfFacing.x, selfFacing.z);
    if (fl < 1e-9) return false;
    const double dot = (dx * selfFacing.x + dz * selfFacing.z) / (std::sqrt(d2) * fl);
    return dot > kSquadmateConeCos;
}

// ---- Sec24.9 friendly fire: the provocation meter (FINAL) -------------------------------------------------------------
inline constexpr double kFriendlyFireThreshold = 150.0;       // Player_friendly_fire_threshold
inline constexpr int64_t kFriendlyFireExpireMs = 6000;        // Player_friendly_fire_expire_time 6 s x 1000
struct FriendlyFireMeter {
    double sum = 0.0;
    int64_t expiresMs = -1;  // -1 disarmed
    // 0x004fa180: add min(dmg, T) (the full T when dmg <= 0), re-arm the expiry; returns sum >= T.
    bool Hit(double dmg, int64_t nowMs) {
        sum += dmg > 0.0 ? std::min(dmg, kFriendlyFireThreshold) : kFriendlyFireThreshold;
        expiresMs = nowMs + kFriendlyFireExpireMs;
        return sum >= kFriendlyFireThreshold;
    }
    // The sense pass zeroes the sum when the timer runs out.
    void Tick(int64_t nowMs) {
        if (expiresMs >= 0 && nowMs >= expiresMs) {
            sum = 0.0;
            expiresMs = -1;
        }
    }
    // 0x004fa220: the timer is still running and the sum is at least T.
    bool Hostile(int64_t nowMs) const { return expiresMs >= 0 && nowMs < expiresMs && sum >= kFriendlyFireThreshold; }
};

// ---- Sec24.10 leaving ----------------------------------------------------------------------------------------------
inline constexpr double kAbandonRadiusM = 55.0;            // Follow_abandon_radius
inline constexpr double kAbandonVehicleScale = 1.2;
inline constexpr int64_t kAbandonTimeoutMs = 20000;        // Follow_abandon_time_out
inline double AbandonRadius(bool playerInVehicleFollowerNotIn) {
    return kAbandonRadiusM * (playerInVehicleFollowerNotIn ? kAbandonVehicleScale : 1.0);
}
struct AbandonTracker {
    int64_t startedMs = -1;  // -1 = in range / not tracking (the marker exists while >= 0)
    // Returns true when the follower is removed this call. Only `canLeaveBehind` followers are ever abandoned.
    bool Update(int64_t nowMs, double dist, bool playerInVehicleFollowerNotIn, bool canLeaveBehind) {
        const bool out = canLeaveBehind && dist > AbandonRadius(playerInVehicleFollowerNotIn);
        if (!out) {
            startedMs = -1;  // back in range cancels the timer and the marker
            return false;
        }
        if (startedMs < 0) startedMs = nowMs;
        return nowMs - startedMs >= kAbandonTimeoutMs;
    }
    bool OutOfRange() const { return startedMs >= 0; }
};

// Hospital bill (HIGH): uniform in [150, 250] dollars, charged as cost x 100 cents.
inline constexpr double kHospitalBillMin = 150.0, kHospitalBillMax = 250.0;
inline double HospitalBillDollars(double draw01) {
    return kHospitalBillMin + (kHospitalBillMax - kHospitalBillMin) * std::max(0.0, std::min(1.0, draw01));
}
inline int64_t HospitalBillCents(double dollars) { return static_cast<int64_t>(std::llround(dollars * 100.0)); }

// Downed type 0 (Sec21.10.1 via Sec24.9): 30000 ms x Homie_Revive_Timer_Mult; a 2.5 s hold revives; expiry = death.
inline constexpr int64_t kDownedBaseMs = 30000;
inline constexpr int64_t kReviveHoldMs = 2500;
inline int64_t DownedDurationMs(double reviveTimerMult) { return static_cast<int64_t>(std::llround(kDownedBaseMs * reviveTimerMult)); }

// ---- roster record ---------------------------------------------------------------------------------------------------
struct Record {
    int status = kAvailable;     // +0xBC
    double cooldownStamp = 0.0;  // +0xC4 (game clock seconds)
    bool playerSelectsVehicle = false;
    bool serviceRow = false;     // clear gang/police notoriety rows: nobody joins
};

// Removal notices (killed, angry-left, dismissed, hospital bill, downed, service end): start the cooldown.
inline void StartCooldown(Record& r, double gameClockSec) {
    r.status = StatusAfterRemoval(r.status);
    if (r.status == kCooldown) r.cooldownStamp = CooldownStamp(gameClockSec, r.playerSelectsVehicle);
}
// Phone book build (0x0083d461): release cooldowns whose stamp has passed.
inline void ReleaseCooldown(Record& r, double gameClockSec) {
    if (r.status == kCooldown && CooldownReleased(gameClockSec, r.cooldownStamp)) r.status = kAvailable;
}

}  // namespace sr3airuntime::homies
