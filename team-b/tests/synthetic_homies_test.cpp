// Synthetic tests for include/sr3airuntime/homies.h: spec-ai-runtime.md Sec24.12 test vectors 1-12 (plus helpers).
#include <cmath>
#include <iostream>

#include "sr3airuntime/homies.h"

using namespace sr3airuntime;
using namespace sr3airuntime::homies;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)
bool Near(double a, double b, double e = 1e-9) { return std::fabs(a - b) <= e; }
double Clock(int day, int h, int m = 0, int s = 0) { return day * kDaySeconds + h * 3600.0 + m * 60.0 + s; }

// Vector 1: new game, cap 1; call B while A follows -> A (named homie, score < 100) is dropped.
void V1() {
    CapInputs in;
    in.slotsUnlocked = UnlockedSlots(true, false, false);
    CHECK(FollowerCap(in) == 1);
    DismissCandidate a;
    a.hp = 100.0;
    a.boundToHomieRecord = true;
    std::vector<DismissCandidate> party = {a};
    CHECK(static_cast<int>(party.size()) >= FollowerCap(in));
    CHECK(PickMemberToDrop(party, false) == 0);
    in.slotsUnlocked = UnlockedSlots(true, true, false);
    CHECK(FollowerCap(in) == 2);
    in.slotsUnlocked = UnlockedSlots(true, true, true);
    CHECK(FollowerCap(in) == 3);
    CHECK(UnlockedSlots(false, true, true) == 0);  // the count stops at the first locked slot
    CHECK(UnlockedSlots(true, false, true) == 1);
}
void V2() {
    CapInputs in;
    in.slotsUnlocked = 1;
    in.allowMaxFollowers = true;
    CHECK(FollowerCap(in) == 3);
    in.allowMaxFollowers = false;
    in.recruitable = false;
    CHECK(FollowerCap(in) == 0);
    in.allowMaxFollowers = true;  // allow-max overrides the recruitable global
    CHECK(FollowerCap(in) == 3);
}
void V3() {
    CapInputs in;
    in.multiPlayerSession = true;
    in.slotsUnlocked = 3;
    CHECK(FollowerCap(in) == 1);
    in.gameMode11or12 = true;
    CHECK(FollowerCap(in) == 2);
    CapInputs npc;
    npc.leaderIsPlayer = false;
    CHECK(FollowerCap(npc) == 5);
}
void V4() {
    const Vec3 p{0, 0, 0}, v{0, 0, 10}, f{0, 0, 1};
    const auto c = SpawnCandidates(p, v, f, 0.7);  // coin >= 0.5: the (hz,0,-hx) side first
    CHECK(c.size() == 12);
    CHECK(c[0].direction == 0 && Near(c[0].distance, 200.0) && Near(c[0].pos.z, 200.0));
    CHECK(Near(c[1].distance, 150.0) && Near(c[1].pos.z, 150.0));  // 150 m ahead is the second tried
    CHECK(Near(c[2].distance, 100.0));
    CHECK(c[3].direction == 1 && Near(c[3].distance, 200.0) && Near(c[3].pos.x, 200.0) && Near(c[3].pos.z, 0.0));
    CHECK(c[6].direction == 2 && Near(c[6].pos.x, -200.0));
    CHECK(c[9].direction == 3 && Near(c[9].pos.z, -200.0) && Near(c[11].pos.z, -100.0));
    const auto d = SpawnCandidates(p, v, f, 0.2);  // coin < 0.5: cross(heading, up) = (-hz, 0, hx) first
    CHECK(Near(d[3].pos.x, -200.0));
    // Slow player: the facing is the heading.
    const auto s = SpawnCandidates(p, {0, 0, 3}, {1, 0, 0}, 0.7);
    CHECK(Near(s[0].pos.x, 200.0));
    // 95 m rule.
    CHECK(!FarEnoughFromPlayers({94.9, 0, 0}, {{0, 0, 0}}));
    CHECK(FarEnoughFromPlayers({95.0, 0, 0}, {{0, 0, 0}}));
    CHECK(!FarEnoughFromPlayers({200, 0, 0}, {{0, 0, 0}, {150, 0, 0}}));  // any player
}
void V5() {
    ApproachInputs a;
    a.topSpeed = 50.0;
    a.targetAheadNotHeadOn = true;
    a.targetInMovingVehicle = true;
    a.targetVehicleSpeed = 25.0;
    CHECK(Near(ApproachSpeed(a), 25.0));
    a.targetVehicleSpeed = 45.0;
    CHECK(Near(ApproachSpeed(a), 35.0));
    a.targetVehicleSpeed = 5.0;
    CHECK(Near(ApproachSpeed(a), 20.0));  // clamped to [20, 35]
    a.targetInMovingVehicle = false;      // the player on foot
    CHECK(Near(ApproachSpeed(a), 35.0));
    a.hasVehicle = false;
    CHECK(Near(ApproachSpeed(a), 10.0));
    ApproachInputs slow;
    slow.topSpeed = 28.0;
    CHECK(Near(ApproachSpeed(slow), 28.0));
}
void V6() {
    Record r;
    StartCooldown(r, Clock(3, 14));
    CHECK(r.status == kCooldown && Near(r.cooldownStamp, Clock(4, 14)));
    ReleaseCooldown(r, Clock(4, 13, 59));
    CHECK(r.status == kCooldown && !Callable(r.status));
    ReleaseCooldown(r, Clock(4, 14, 0, 1));
    CHECK(r.status == kAvailable && Callable(r.status));
    Record g;
    g.playerSelectsVehicle = true;
    StartCooldown(g, Clock(3, 14));
    ReleaseCooldown(g, Clock(3, 15, 0, 1));
    CHECK(g.status == kAvailable);
    Record locked;
    locked.status = kLocked;
    StartCooldown(locked, 0.0);
    CHECK(locked.status == kLocked);
    Record hidden;
    hidden.status = kHidden;
    StartCooldown(hidden, 0.0);
    CHECK(hidden.status == kLocked);
}
void V7() {
    CHECK(FollowGait(1, 10.0) == 2);
    CHECK(FollowGait(1, 20.0) == 3);
    CHECK(FollowGait(3, 3.0) == 3);
    CHECK(FollowGait(0, 1.0) == 1);  // gait 0 is never chosen
    CHECK(FollowGait(1, 7.5) == 1 && FollowGait(1, 7.6) == 2 && FollowGait(1, 15.1) == 3);
    CHECK(FollowGait(2, 20.0) == 3);
    CHECK(!FollowPlayerAccepts(5.0));
    CHECK(!FollowPlayerAccepts(6.0));
    CHECK(FollowPlayerAccepts(7.0));
    CHECK(FollowFormationAccepts(2.6) && !FollowFormationAccepts(2.5));
}
void V8() {
    FriendlyFireMeter m;
    CHECK(!m.Hit(60, 1000) && !m.Hostile(1000));
    CHECK(!m.Hit(60, 2000));
    CHECK(m.Hit(60, 3000) && Near(m.sum, 180.0) && m.Hostile(3000));
    CHECK(m.Hostile(8999));
    CHECK(!m.Hostile(9000));  // 6000 ms after the last hit
    m.Tick(9000);
    CHECK(Near(m.sum, 0.0));
    FriendlyFireMeter one;
    CHECK(one.Hit(200, 0) && Near(one.sum, 150.0) && one.Hostile(0));
    FriendlyFireMeter zero;
    CHECK(zero.Hit(0.0, 0) && Near(zero.sum, 150.0));  // dmg <= 0 adds the full threshold
    FriendlyFireMeter gap;  // two hits more than 6 s apart do not add up
    gap.Hit(100, 0);
    gap.Tick(7000);
    CHECK(!gap.Hit(100, 7000));
}
void V9() {
    AbandonTracker t;
    bool removed = false;
    for (int64_t ms = 0; ms < 20000 && !removed; ms += 100) removed = t.Update(ms, 60.0, false, true);
    CHECK(!removed && t.OutOfRange());
    CHECK(t.Update(20000, 60.0, false, true));  // removed at 20 s
    AbandonTracker u;
    CHECK(!u.Update(0, 60.0, false, true));
    CHECK(!u.Update(19000, 50.0, false, true) && !u.OutOfRange());  // back in range: cancelled
    CHECK(!u.Update(19100, 60.0, false, true) && !u.Update(39000, 60.0, false, true));  // restarts at 19.1 s
    CHECK(u.Update(39100, 60.0, false, true));
    CHECK(Near(AbandonRadius(true), 66.0));
    AbandonTracker c;
    CHECK(!c.Update(0, 60.0, true, true) && !c.Update(30000, 60.0, true, true));  // inside 66 m
    AbandonTracker n;
    CHECK(!n.Update(0, 60.0, false, false) && !n.Update(100000, 60.0, false, false));  // no flag: never removed
}
void V10() {
    DismissCandidate recruit;
    recruit.hp = 100;
    recruit.weaponCount = 1;  // ~1.49
    DismissCandidate homie;
    homie.hp = 100;
    homie.weaponCount = 1;
    homie.boundToHomieRecord = true;
    homie.instanceOverride = true;
    CHECK(DismissScore(recruit) > 0.9 && DismissScore(recruit) < 2.0);
    CHECK(DismissScore(homie) > 25.0);
    CHECK(PickMemberToDrop({homie, recruit}, false) == 1);
    CHECK(PickMemberToDrop({recruit, homie}, false) == 0);
    DismissCandidate locked = recruit;
    locked.neverTaken = true;
    CHECK(Near(DismissScore(locked), 100.0) && PickMemberToDrop({locked}, true) == -1);
    DismissCandidate temp = recruit;
    temp.flag1da5bit4 = true;
    CHECK(PickMemberToDrop({temp}, false) == -1 && PickMemberToDrop({temp}, true) == 0);
    DismissCandidate weak = homie;  // a weaker homie goes before a healthier one
    weak.hp = 2;
    CHECK(PickMemberToDrop({homie, weak}, false) == 1);
}
void V11() {
    const double pi = 3.14159265358979;
    const double c30 = std::cos(30.0 * pi / 180.0), s30 = std::sin(30.0 * pi / 180.0);
    const double c50 = std::cos(50.0 * pi / 180.0), s50 = std::sin(50.0 * pi / 180.0);
    CHECK(SquadmateAhead({0, 0, 1}, {1.5 * s30, 0, 1.5 * c30}));
    CHECK(!SquadmateAhead({0, 0, 1}, {1.5 * s50, 0, 1.5 * c50}));
    CHECK(!SquadmateAhead({0, 0, 1}, {0, 0, 2.5}));   // beyond 2 m
    CHECK(!SquadmateAhead({0, 0, 1}, {0, 0, -1.0}));  // behind
    CHECK(kSquadmateWaitMs == 1500);
}
void V12() {
    const double bill = HospitalBillDollars(0.5);
    CHECK(Near(bill, 200.0) && HospitalBillCents(bill) == 20000);
    CHECK(Near(HospitalBillDollars(0.0), 150.0) && Near(HospitalBillDollars(1.0), 250.0));
    CHECK(DownedDurationMs(1.0) == 30000 && DownedDurationMs(0.5) == 15000);
}
void TeleportAndRecord() {
    CHECK(!FollowTeleportApplies(120, 2.0, false) && FollowTeleportApplies(120, 5.0, false));
    CHECK(!FollowTeleportApplies(120, 5.0, true) && FollowTeleportApplies(160, 5.0, true));
    CHECK(Callable(kAvailable) && !Callable(kCooldown) && !Callable(kLocked) && !Callable(kCalled));
}
// Team A answers 2026-10-20 (spec-conformance-review.md "Answers to Team B CHOSEN questions").
void TeamAAnswers() {
    // 6.1: rows start at 0; listed only with the unlockable or homie_mission_unlock.
    CHECK(InitialStatus(false, false) == kLocked && InitialStatus(true, false) == kAvailable && InitialStatus(false, true) == kAvailable);
    // 6.2: connect 1.033 s after the call, summon one update later.
    CHECK(kRingToConnectMs == 1033 && kConnectToSummonMs == 33);
    // TA-6: the phone closes 91 updates (3.033 s) after the summon; a dial before that finds the phone open.
    CHECK(kSummonToPhoneCloseMs == 3033);
    CHECK(!PhoneStillOpen(5000, -1) && PhoneStillOpen(1000 + 3032, 1000 + kSummonToPhoneCloseMs) &&
          !PhoneStillOpen(1000 + 3033, 1000 + kSummonToPhoneCloseMs));
    // TA-7: per-vehicle seat count from the seat data; no data -> the 4-seat stand-in; clamped to 8.
    CHECK(SeatCountFromSeatData(2) == 2 && SeatCountFromSeatData(0) == kFallbackSeatCount && SeatCountFromSeatData(12) == 8);
    CHECK(FreeSeats(SeatCountFromSeatData(2), 0) == 1);
    // TA-8: follower HP = rank Hit_Points (clamped 0x7FFF) x Homie_Health_Bonus; missing rank -> 100.
    CHECK(Near(FollowerMaxHp(true, 250.0, 1.0), 250.0) && Near(FollowerMaxHp(true, 200.0, 1.5), 300.0));
    CHECK(Near(FollowerMaxHp(false, 999.0, 2.0), 100.0) && Near(FollowerMaxHp(true, 99999.0, 1.0), 32767.0));
    // 6.3: arrival = speed < 1.0 STRICT, no distance test.
    CHECK(DriveUpArrived(0.99) && !DriveUpArrived(1.0) && !DriveUpArrived(1.2) && DriveUpArrived(0.0));
    // 6.5: free seats = seat count - driver - occupied.
    CHECK(FreeSeats(4, 0) == 3 && FreeSeats(4, 2) == 1 && FreeSeats(2, 1) == 0 && FreeSeats(8, 0) == 7 && FreeSeats(1, 0) == 0);
    // 6.6b: walk 1.386, run 6.373 (clip root motion).
    CHECK(Near(GaitSpeed(1), 1.386) && Near(GaitSpeed(2), 6.373));
    // 6.9: revive prompt radius 3.0 m.
    CHECK(Near(kReviveRadiusM, 3.0));
}
}  // namespace

int main() {
    TeamAAnswers(); V1(); V2(); V3(); V4(); V5(); V6(); V7(); V8(); V9(); V10(); V11(); V12(); TeleportAndRecord();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "synthetic_homies_test OK\n";
    return 0;
}
