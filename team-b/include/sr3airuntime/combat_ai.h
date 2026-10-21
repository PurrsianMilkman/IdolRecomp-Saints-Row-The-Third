#pragma once
// sr3airuntime/combat_ai.h - combat and NPC behaviour rules, spec-ai-runtime.md Sec21 (2026-10-15) and the
// mission-driving rules of Sec22. Header-only, pure functions; every rule cites its spec section. Test vectors:
// Sec21.6, Sec22.9, Sec22.11 (tests/combat_ai_test.cpp).
//
// Status of what is encoded here (spec tags):
//   FINAL  - flee-rather-than-fight (21.4), flee/cower chooser (21.4), flee stop rule (21.4), advance band
//            (21.1), cover search box and threat term (21.3), cover timer scaling (21.3), vehicle-hostage release
//            (21.4), downed selector, type 0/1 rules and get-up heal (21.10.1), scripted order ticket and move
//            arrival (21.5, 22.11), chase hand-off and first-ram delay (22.2), drive-to and waypoint arrival (22.3).
//   CHOSEN - NearerPlayerIndex / TargetCopyDue (21.9 headless recipe): not engine behaviour; the engine writes the
//            target only through damage, events, scripted orders and the spawner (21.2).
// Notoriety is deliberately NOT an input anywhere in this file (21.1 "What is NOT an input").
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sr3airuntime {
namespace combat {

// ---------------------------------------------------------------------------------------------------------
// 21.4 Personality: 0x004ead80 "flee rather than fight". FINAL.
// Step 5 compares a PERCENT (Flee_health_pct, 0-100) with a health FRACTION (0..1): kept as in retail, so a
// record with Flee_health_pct >= 1 passes at any health and 0 never passes.
struct FleeInputs {
    bool eligible = true;          // 0x004ead10 flee eligibility
    bool cantCower = false;        // +0x2ba bit 3
    bool flag317Bit6 = false;      // +0x317 bit 6
    bool canFight = true;          // 0x004e9110 fight-capability test
    bool alwaysCower = false;      // +0x2b9 bit 7
    int fleeHealthPct = 0;         // record +5
    double healthFraction = 1.0;   // +0x1cb8 / +0x1cac
    uint8_t fleeEvents = 0;        // record +6: aimed 1, melee 2, bullet heard 4, bullet hit 8, explosion 0x10
    bool list15NonEmpty = false;   // perception list 15
    bool bullet = false;           // caller passes the bullet flag (the idle gunfire sink passes 0)
};
inline bool FleeRatherThanFight(const FleeInputs& in) {
    if (!in.eligible && in.cantCower) return false;
    if (in.flag317Bit6) return true;
    if (!in.canFight) return true;
    if (in.alwaysCower) return true;
    if (static_cast<double>(in.fleeHealthPct) > in.healthFraction) return true;
    if ((in.fleeEvents & 0x10) && in.list15NonEmpty) return true;
    return in.bullet ? (in.fleeEvents & 0x08) != 0 : (in.fleeEvents & 0x02) != 0;
}

// ---------------------------------------------------------------------------------------------------------
// 21.4 Flee/cower chooser 0x004eaae0. FINAL. `rand01` is a uniform [0,1) draw.
enum class FleeCower { None, Flee, Cower };
struct ChooserInputs {
    bool panicPending = false;       // a Panic request is pending
    bool inCower = false;            // currently mode 9
    bool cowerBits45 = false;        // already Cower with +0x8d0 bits 4/5 set
    bool inFlee = false;             // currently mode 13
    bool cantFlee = false;           // +0x2ba bit 5
    bool cantCower = false;          // +0x2ba bit 3
    bool alwaysCower = false;
    bool fleeGateOk = true;          // 0x004d8ff0
    bool cowerEligible = true;       // 0x004eaa40
};
inline FleeCower ChooseFleeOrCower(const ChooserInputs& in, double rand01) {
    if (in.panicPending) return FleeCower::None;
    if (in.inCower && in.cowerBits45) return FleeCower::None;
    bool preferFlee = in.inFlee ? true : (rand01 <= (in.inCower ? 0.5 : 0.8));
    if (in.cantFlee) preferFlee = false;
    if (in.cantCower) preferFlee = true;
    if (in.alwaysCower) preferFlee = false;  // always_cower skips the flee branch entirely
    if (preferFlee && in.fleeGateOk) return FleeCower::Flee;
    if (in.cowerEligible && !in.inCower) return FleeCower::Cower;
    return FleeCower::None;
}

// 21.4 Flee tick 0x004da9f0: the flee-from bit is cleared once the SQUARED distance exceeds 900 (30 m).
inline bool FleeFromStillActive(double distSq) { return !(distSq > 900.0); }

// ---------------------------------------------------------------------------------------------------------
// 21.1 Advance step 6 / 0x004e68e0 and the weapon-band test 0x004df080. FINAL.
// a = minimum range, b = maximum range, d2 = squared distance. -1 too close, +1 too far, 0 inside the band.
inline int WeaponBand(double d2, double a, double b) {
    // D-27 minor: d2 is float32; the too-close test is d2 < float32(a*a).
    if (static_cast<float>(d2) < static_cast<float>(a * a)) return -1;
    if (d2 > b * b) return 1;
    return 0;
}
// Distance from the TARGET along the human direction at which the band test places its out point.
inline double WeaponBandOutDistance(int band, double a, double b) {
    // D-27 minor: out fraction is float32 0.1 widened (0.10000000149); result rounded to float32.
    if (band < 0) return static_cast<double>(static_cast<float>(a + (b - a) * 0.10000000149011612));
    if (band > 0) return a + 0.9 * (b - a);
    return 0.0;  // out = the human's own position
}
inline int AdvanceBand(bool targetKnown, bool humanDownedOr9a1dc0, bool targetWithinAxisBox2, double d2, double a,
                       double b) {
    if (!targetKnown) return 1;
    if (humanDownedOr9a1dc0 && !targetWithinAxisBox2) return 1;
    return WeaponBand(d2, a, b);
}
inline bool AdvanceStepSixPasses(int band) { return band == 1; }  // "must return exactly 1"

// ---------------------------------------------------------------------------------------------------------
// 21.3 Cover. FINAL for the search box, evaluator, threat term and timers.
inline bool CoverNodeInSearchBox(double dx, double dy, double dz, bool brute) {
    const double hw = brute ? 15.0 : 14.0;
    return std::fabs(dx) <= hw && std::fabs(dz) <= hw && std::fabs(dy) <= 4.0;
}
struct ThreatEntry {
    bool primaryArc;  // 0x008cd1d0 bit 0
    bool defArc;      // bit 1: the node shields this threat
    bool inList16;    // damage list
    bool inList3;     // shot-at list
};
struct CoverEval {
    bool anyPrimary = false;
    int count = 0;
    int shielded = 0;
    bool disqualified = false;  // an unshielded player threat: the evaluator returns 0 with count 0
};
// Walks the list-0 entries that carry flag 0x8 and resolve to a live object (the caller filters).
inline CoverEval EvaluateCoverThreats(const ThreatEntry* entries, int n, bool threatIsPlayer) {
    CoverEval r;
    for (int i = 0; i < n; ++i) {
        const ThreatEntry& e = entries[i];
        if (e.primaryArc) r.anyPrimary = true;
        const int s = e.defArc ? 1 : 0;
        if (s == 0 && threatIsPlayer) {
            r.count = 0;
            r.shielded = 0;
            r.disqualified = true;
            return r;
        }
        r.count += 1;
        r.shielded += s;
        if (e.inList16 || e.inList3) {
            r.count += 1;
            r.shielded += s;
        }
    }
    return r;
}
// Threat term of the node cost: (any_primary ? 0 : wA) + (count - shielded) * wB; combat_aggression 2 -> 8/7 else 5/10.
inline double CoverThreatCost(bool anyPrimary, int count, int shielded, int combatAggression) {
    const double wA = combatAggression == 2 ? 8.0 : 5.0;
    const double wB = combatAggression == 2 ? 7.0 : 10.0;
    return (anyPrimary ? 0.0 : wA) + static_cast<double>(count - shielded) * wB;
}
// Cover timers (Sec11.6 / 21.3): base x U(0.8, 1.25).
inline double CoverTimerMs(double baseMs, double rand01) { return baseMs * (0.8 + rand01 * (1.25 - 0.8)); }
inline bool CoverAdvanceEligible(int64_t timeInCoverMs, int64_t advanceCoverTimeMs) {
    return timeInCoverMs >= advanceCoverTimeMs;
}
inline int64_t CoverPopUpWindowMs(int64_t popUpTimeMs, bool beingShotAt) { return beingShotAt ? 1000 : popUpTimeMs; }

// ---------------------------------------------------------------------------------------------------------
// 21.4 Vehicle hostage (mode 20) release. FINAL. Limits: 15 mph, 5 mph for an RC vehicle (tweak-table values;
// the 10.0 constant at 0x012e1128 is only the registration default). Delays: initial 5000 / RC 6000 (+U(0,100)),
// general 2000 / RC 4000.
struct HostageState {
    int64_t exitAtMs = 0;
};
inline double HostageMph(double metresPerSecond) { return metresPerSecond * 2.236936; }
inline void HostageEnter(HostageState& h, bool rc, int64_t nowMs, double rand01) {
    const int64_t d = rc ? 6000 : 5000;
    h.exitAtMs = nowMs + d + static_cast<int64_t>(rand01 * 100.0);
}
// Returns true when the hostage should leave the vehicle this tick.
inline bool HostageTick(HostageState& h, bool rc, double mph, bool airborne, int64_t nowMs) {
    const double limit = rc ? 5.0 : 15.0;
    if (mph >= limit || airborne) {
        h.exitAtMs = nowMs + (rc ? 4000 : 2000);
        return false;
    }
    return nowMs >= h.exitAtMs;
}

// ---------------------------------------------------------------------------------------------------------
// 21.10.1 Downed NPCs (Dbno), FINAL (supersedes the 21.4 third-pass "type-0 expiry stands up healed" rule).
// Type 0 (followers): the bleed-out deadline KILLS at expiry (0x009a7610 -> death finalizer, stance 5). The team
// percent heal belongs ONLY to the get-up / revive path (0x009a7c90 -> 0x009a78d0) and to the type-1 expiry.
inline int64_t DownedDeadlineMs(int64_t nowMs, double difficultyScale, bool gameMode5) {
    if (gameMode5) return nowMs + 15000;
    return nowMs + static_cast<int64_t>(30000.0 * difficultyScale);
}
// Type-0 tick 0x009723f0 step 3: deadline passed (no-timer bit clear) -> death.
inline bool DownedType0ExpiryKills(int64_t nowMs, int64_t deadlineMs, bool noTimerFlag) {
    return !noTimerFlag && deadlineMs >= 0 && nowMs >= deadlineMs;
}
// Get-up body 0x009a78d0: HP is SET (HP setter 0x0094cfc0) to int(max_hp) x pct; pct = Human_revive_percent_hit_points
// 1.0 when the team byte is 0, else Enemy_revive_percent_hit_points 0.25.
// D-27 FINAL: the result is a FLOAT, float32(float32(max HP) x pct), clamped to [0, max HP], NOT rounded to a whole number.
inline float DownedGetUpHeal(int maxHp, bool enemySide) {
    const float mx = static_cast<float>(maxHp);
    float hp = static_cast<float>(static_cast<double>(mx) * (enemySide ? 0.25 : 1.0));
    if (hp > mx) hp = mx;
    if (hp < 0.0f) hp = 0.0f;
    return hp;
}

// Selector 0x009735d0, first match wins. Every input is a host fact (default false); -1 = no downed state.
struct DownedSelectorInputs {
    bool noDownedBit17 = false;          // +0x1c9c bit 17
    bool isNonPlayerFollower = false;    // 0x0097e280 non-null
    bool coopPartnerRule = false;        // the whole co-op player clause (partner exists, owned, mode gates)
    bool killbaneWrestling = false;      // record npc_Killbane_wrestling
    bool killbaneSciFi = false;          // record npc_Killbane_scifi
    bool isAvatarNotPlayerClass = false; // 0x005e6ce0 and not player-class
    bool isBrute = false;                // 0x0052a230
    bool isGangMember = false;           // 0x00943190
    bool brute0x9422d0 = false;          // 0x009422d0 (excludes)
    bool bit22 = false;                  // +0x1c9c bit 22 (excludes)
};
inline int SelectDownedType(const DownedSelectorInputs& in) {
    if (in.noDownedBit17) return -1;
    if (in.isNonPlayerFollower) return 0;
    if (in.coopPartnerRule) return 0;
    if (in.killbaneWrestling) return 2;
    if (in.killbaneSciFi) return 3;
    if (in.isAvatarNotPlayerClass) return 4;
    if (in.isBrute && in.isGangMember && !in.brute0x9422d0 && !in.bit22) return 1;
    return -1;
}

// Type 1 (gang brute). Predicate 0x00972790 (owner side; false = plain death): `+0x1588` is the HP left after the
// killing hit (negative = overkill); threshold = Bleed_Out_Hit_Points (record +0x0C).
struct Type1Inputs {
    bool attackerIsLiveBruteWithBit7 = false;
    int hpAfterKillingHit = 0;  // +0x1588 (<= 0)
    int threshold = 0;          // 0x00972740
    bool burning = false;       // 0x0097cdd0 / +0xe8 bit 8
    bool other = false;         // 0x009b9180, Whored mode, 0x0095d720 + vehicle: folded into one host fact
};
inline bool Type1Predicate(const Type1Inputs& in) {
    if (in.attackerIsLiveBruteWithBit7) return false;
    if (in.hpAfterKillingHit < -in.threshold) return false;
    if (in.burning || in.other) return false;
    return true;
}
inline constexpr int64_t kBruteDownTimeMs = 7000;     // HD_brute_down_time_ms (0x026252c0)
inline constexpr int64_t kBruteDamageToTimerMs = 50;  // HD_brute_damage_to_timer_ms (0x026252bc)
// Enter 0x009728c0: buffer = int(+0x1588) + threshold.
inline int Type1EnterBuffer(int hpAfterKillingHit, int threshold) { return hpAfterKillingHit + threshold; }
// Damage hook 0x00972bf0 (HP <= 0, timer armed): buffer -= int(damage); buffer < 0 or damage == -1.0 -> death;
// else remaining = min(remaining + 50 x damage, 7000).
struct Type1DamageResult {
    bool dies = false;
    int buffer = 0;
    int64_t remainingMs = 0;
};
inline Type1DamageResult Type1ApplyDamage(int buffer, int64_t remainingMs, double damage, bool lethalConstant) {
    Type1DamageResult r;
    r.buffer = buffer - static_cast<int>(damage);
    r.remainingMs = remainingMs;
    if (r.buffer < 0 || lethalConstant) {
        r.dies = true;
        return r;
    }
    r.remainingMs = std::min<int64_t>(
        remainingMs + static_cast<int64_t>(static_cast<double>(kBruteDamageToTimerMs) * damage), kBruteDownTimeMs);
    return r;
}

// ---------------------------------------------------------------------------------------------------------
// 21.5 / 22.11 Scripted order ticket and move arrival. FINAL.
// Arrival test A: clamp the human's position, in the destination's local frame, into the destination's local box
// and compare the SQUARED distance with 2.0 (1.414 m for a point object).
inline bool ScriptedMoveArrived(double hx, double hy, double hz, double minx, double miny, double minz, double maxx,
                                double maxy, double maxz) {
    const double cx = std::min(std::max(hx, minx), maxx);
    const double cy = std::min(std::max(hy, miny), maxy);
    const double cz = std::min(std::max(hz, minz), maxz);
    const double dx = hx - cx, dy = hy - cy, dz = hz - cz;
    return dx * dx + dy * dy + dz * dz < 2.0;
}

// One kind-3 ticket per character (+0x4aa). Status of the lookup: 0 pending, 1 done (consumed by the read),
// 2 none. ai_scripted_action_is_complete reads "status != 0".
class ScriptedOrderTicket {
public:
    void stage(int action) {  // 0x004f4d00: a new order frees any earlier kind-3 ticket
        state_ = State::Pending;
        action_ = action;
    }
    // 0x004f4f50: only the staged action's own finish completes it.
    bool finish(int action) {
        if (state_ != State::Pending || action != action_) return false;
        state_ = State::Done;
        return true;
    }
    void giveUp() {}  // 0x004f50b0: the give-up end leaves the ticket pending (no timeout, no failure code)
    void clear() { state_ = State::None; }  // ai_clear_scripted_action
    int statusCode() {
        switch (state_) {
            case State::Pending: return 0;
            case State::Done: state_ = State::None; return 1;
            default: return 2;
        }
    }
    bool isComplete() { return statusCode() != 0; }
    bool pending() const { return state_ == State::Pending; }
    int stagedAction() const { return action_; }

private:
    enum class State { None, Pending, Done };
    State state_ = State::None;
    int action_ = 0x47;
};

// ---------------------------------------------------------------------------------------------------------
// 21.2 / 21.9 CHOSEN headless stand-in (NOT engine behaviour): the nearer of two players (strictly smaller wins),
// refreshed on the follower-copy cadence of 2000 ms.
inline int NearerPlayerIndex(double distSqPlayer0, double distSqPlayer1) { return distSqPlayer1 < distSqPlayer0 ? 1 : 0; }
inline bool TargetCopyDue(int64_t nowMs, int64_t lastCopyMs) { return nowMs - lastCopyMs >= 2000; }

// ---------------------------------------------------------------------------------------------------------
// 22.2 Chase (mode 4). FINAL.
// Hand-off to the occupants: the garage/store state is set, or the squared 3-D distance between the two
// vehicles is below 100 (10 m).
inline bool ChaseHandsOffToOccupants(double distSq, bool storeGarageState) { return storeGarageState || distSq < 100.0; }
// First ram check delay by chase class (0x00b47600): 3 -> 9000-10000, 4 -> 6000-7000, 5 -> 3000-4000, else 1000.
inline int64_t FirstRamDelayLoMs(int chaseClass) {
    return chaseClass == 3 ? 9000 : chaseClass == 4 ? 6000 : chaseClass == 5 ? 3000 : 1000;
}
inline int64_t FirstRamDelayHiMs(int chaseClass) {
    return chaseClass == 3 ? 10000 : chaseClass == 4 ? 7000 : chaseClass == 5 ? 4000 : 1000;
}
// Chase class from the driver's team (0x00b473a0): teams 1-3 the low byte of the per-team table dword, teams 5/6 -> 0,
// team 4, 0 or above 6 -> 3.
inline int ChaseClassFromTeam(int team, int teamTableLowByte) {
    if (team >= 1 && team <= 3) return teamTableLowByte;
    if (team == 5 || team == 6) return 0;
    return 3;
}

// ---------------------------------------------------------------------------------------------------------
// 22.3 Drive-to completion (0x00b2e720) and the waypoint directive (mode 7). FINAL. d is the X/Z distance.
inline bool PathfindBrakingStarts(double d, double speed, double maxBrakeDecel, double radius = 3.0) {
    return speed >= 0.0 && d < speed * speed / maxBrakeDecel + radius;
}
inline bool PathfindArrived(double d, bool stopAtGoal, double speed, double maxBrakeDecel, double radius = 3.0) {
    if (!stopAtGoal) return d < radius;
    return speed < 1.0 && PathfindBrakingStarts(d, speed, maxBrakeDecel, radius);
}
// Node A passes when (A - p) . (B - A) <= 0.
inline bool WaypointNodePassed(double apx, double apy, double apz, double abx, double aby, double abz) {
    return apx * abx + apy * aby + apz * abz <= 0.0;
}
// Last node: squared X/Z distance <= 16 (4 m); 4.0 with stop-at-end; mode 2 overrides to 2500.
inline bool WaypointLastNodeArrived(double d2, bool stopAtEnd, bool mode2) {
    return d2 <= (mode2 ? 2500.0 : stopAtEnd ? 4.0 : 16.0);
}
inline bool WaypointFirstNodeSkipped(double d2ToNode0) { return d2ToNode0 <= 4.0; }

}  // namespace combat
}  // namespace sr3airuntime
