#pragma once

// sr3airuntime/perception.h - AI runtime unit A2: perception's write side
// (spec-ai-runtime.md Sec10) and the read-side query helpers Sec9.8/Sec11.6
// resolve against the same storage.
//
// Source: spec-ai-runtime.md Sec10 (storage Sec10.1, the perception tick
// Sec10.2, producers Sec10.3, flags Sec10.4, events Sec10.5, the ally link
// Sec10.6, the query Sec10.7, port notes Sec10.8) plus the helper formulas
// Sec11.6 settles (0x004fc6d0, 0x004fdac0, 0x004fdae0, 0x004fe390,
// 0x004ffc70). Every behaviour below is CONFIRMED in that text unless a
// comment says otherwise.
//
// SCOPE. This unit owns the 22 perceived-entity lists, their capacity/
// lifetime/order rules, the entry-flag transitions of the CONFIRMED
// producers (notice/see/lost/dismiss/damage/gunfire), the damage
// accumulator, the investigation-candidate query, and the small perception
// fields (+0x244 time-since-clear-LOF, +0x250 ally link). It explicitly
// does NOT implement (per spec's own stated scope, Sec10's header, and this
// project's standing rule against inventing navmesh/vision/LOS behaviour):
//   * the vision line-of-sight service (0x004ed1e0/0x004ed550) or any 3-D
//     maths/raycast - unit A4 (spec-ai-runtime.md Sec12.4) later read both
//     halves (answer-decode and request-queue) in full, confirming the
//     underlying raycast execution itself is a physics-domain primitive;
//     not folded in here (see this project's HANDOFF-equivalent note: A4
//     landed after this unit's own implementation pass; its FOV cone
//     geometry Sec12.2, the two sound-radius producers Sec12.3, the slow
//     pass Sec12.6 and the ally-link cylinder test Sec12.8 are a documented
//     follow-up, not implemented below);
//   * the team-relationship table 0x009b7100 or the attitude function's own
//     internals (Sec10.3's 0x004fe0f0) - callers pass an already-computed
//     attitude int. A4 (Sec12.7) read the table's 12-instruction accessor
//     in full; TeamRelationship() below reproduces it against a
//     caller-supplied matrix (this project does not map the real VA);
//   * the zero-fill tunables block (Sec10's header) - any producer that
//     needs one (blur radius, cone angles, ...) takes it as a parameter or
//     documents the simplification taken instead (see noticeWithoutSight).
//     A4 (Sec12.1) later identified this block as ordinary tweak_table.xtbl
//     entries with real shipped values (the Ai_fov_* family, Pain_sound_
//     radius, Grenade_shout_radius) - relevant to the FOV test this unit
//     still does not implement, not to anything below.
// The SENSE PASS orchestration (which object to test, how often, in what
// order - Sec10.2 steps 4-7) is therefore a HOST responsibility: this class
// exposes the confirmed low-level producer operations (notice, see, lost,
// dismiss, recordDamage, ...) for a host or test to call directly.

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "sr3airuntime/common.h"
#include "sr3luahost/open_state.h"

namespace sr3airuntime {

using sr3luahost::OpenStateError;

// --- the 22-list static metadata (Sec10.1's 0x012e2510) ---------------------------
enum class ListOrder : uint8_t { ScoreAscending, MostRecentFirst };
struct PerceptionListMeta {
    int capacity;
    int64_t lifetimeMs;  // -1 = never expires by age
    ListOrder order;
};
inline constexpr int kPerceptionListCount = 22;
inline constexpr std::array<PerceptionListMeta, kPerceptionListCount> kPerceptionListMeta = {{
    /*0*/ {15, -1, ListOrder::ScoreAscending},
    /*1*/ {15, 40000, ListOrder::ScoreAscending},
    /*2*/ {4, 4000, ListOrder::ScoreAscending},
    /*3*/ {3, 5000, ListOrder::ScoreAscending},
    /*4*/ {15, 30000, ListOrder::ScoreAscending},
    /*5*/ {8, 20000, ListOrder::ScoreAscending},
    /*6*/ {4, 60000, ListOrder::ScoreAscending},
    /*7*/ {1, 30000, ListOrder::ScoreAscending},
    /*8*/ {1, 3000, ListOrder::ScoreAscending},
    /*9*/ {15, 1000, ListOrder::ScoreAscending},
    /*10*/ {4, 15000, ListOrder::ScoreAscending},
    /*11*/ {4, 1000, ListOrder::ScoreAscending},
    /*12*/ {4, 10000, ListOrder::ScoreAscending},
    /*13*/ {6, 60000, ListOrder::MostRecentFirst},
    /*14*/ {6, 60000, ListOrder::ScoreAscending},
    /*15*/ {4, 60000, ListOrder::MostRecentFirst},
    /*16*/ {6, 30000, ListOrder::ScoreAscending},
    /*17*/ {5, 3000, ListOrder::MostRecentFirst},
    /*18*/ {15, 4000, ListOrder::MostRecentFirst},
    /*19*/ {15, 30000, ListOrder::MostRecentFirst},
    /*20*/ {15, 12000, ListOrder::MostRecentFirst},
    /*21*/ {5, 6000, ListOrder::ScoreAscending},
}};

// Named list numbers (Sec10.3/Sec10.8's own meanings; the rest stay numeric
// since the spec itself does not name them beyond a producer citation).
namespace plist {
inline constexpr int KnownHostiles = 0;     // "awareness" (Sec10.8 item 2)
inline constexpr int LastSightings = 1;
inline constexpr int LineOfFire = 2;
inline constexpr int ShotsAtMe = 3;
inline constexpr int Unused4 = 4;           // no producer found (Sec10.3)
inline constexpr int NeutralFriendly = 5;
inline constexpr int Hearsay = 6;
inline constexpr int BodyFound = 7;
inline constexpr int FriendlyFire = 8;
inline constexpr int Vehicles = 9;
inline constexpr int Projectiles = 10;
inline constexpr int ImminentThreats = 11;
inline constexpr int Unnamed12 = 12;
inline constexpr int WitnessedAttacks = 13;
inline constexpr int GunfireHeard = 14;
inline constexpr int LoudEvents = 15;
inline constexpr int DamageMemory = 16;
inline constexpr int CoverMemory17 = 17;
inline constexpr int CoverMemory18 = 18;
inline constexpr int Unused19 = 19;         // no producer found (Sec10.3)
inline constexpr int CoverMemory20 = 20;
inline constexpr int FollowMemory21 = 21;
}  // namespace plist

// --- entry flags (Sec10.4) ---------------------------------------------------------
namespace pflag {
inline constexpr uint32_t WrittenOff = 0x1;
inline constexpr uint32_t Sticky2 = 0x2;         // list 16: the damage record; list 0: no setter found (Sec10.4)
inline constexpr uint32_t NeverExpires = 0x4;
inline constexpr uint32_t ActivelyKnown = 0x8;   // "actively known hostile"
inline constexpr uint32_t PositionVerified = 0x10;
inline constexpr uint32_t InView = 0x40;
inline constexpr uint32_t NonVisualNoticed = 0x80;
inline constexpr uint32_t VisuallyConfirmedOnce = 0x100;
inline constexpr uint32_t NonVisualCurrent = 0x200;
inline constexpr uint32_t LosValid = 0x400;
inline constexpr uint32_t CloseInnerRadius = 0x800;
inline constexpr uint32_t ExcludedFromInvestigation = 0x10000;
inline constexpr uint32_t Lof_Obstructed = 0x01000000;   // list 2
inline constexpr uint32_t Lof_ClearOrLost = 0x02000000;  // list 0: last LOS "blocked"; list 2: clear (alternate probe)
inline constexpr uint32_t Lof_Clear = 0x04000000;         // list 2: clear
inline constexpr uint32_t Lof_ProbeVariant = 0x08000000;  // list 2
}  // namespace pflag

// --- an entry (0x34 bytes; Sec10.1's "corrects Sec9.8": doubly linked, not needed
// as a linked list in this C++ model - a std::vector per list reproduces the
// same observable ordering) ---------------------------------------------------------
struct PerceptionEntry {
    Vec3 position;              // +0x00
    uint64_t handle = 0;        // +0x10/+0x14
    uint32_t flags = 0;         // +0x18
    int64_t noticedAtMs = 0;    // +0x1c (age = now - noticedAtMs)
    double score = 0;           // +0x20 (first lane of the score vector; Sec10.1's allocator)
    int listNumber = -1;        // +0x24
    int kind = -1;               // +0x28; -1 = unset (no live producer writes the engine's 0xff sentinel, Sec10.1)
};

enum class PostResult { Refused, Created, Refreshed, Evicted };

// --- the perception sub-object's state (storage + the small scalar fields) --------
class PerceptionState {
public:
    // --- Sec10.1: storage, insertion order, capacity/score eviction --------------
    // The generic adder 0x004fec80: create-or-update by handle.
    // updateMode mirrors the stimulus record's +0x34 ("update mode"): mode 2
    // only improves (refuses a non-lower score); any other mode overwrites
    // unconditionally (Sec10.1's "Update" rule). A negative score is always
    // refused (Sec10.1).
    PostResult postStimulus(int list, uint64_t handle, Vec3 position, double score, uint32_t flags,
                             int kind, int updateMode, int64_t nowMs);
    // Direct removal/eviction primitives used by the create path and by
    // Sec10.1's own "Order 0/Order 1" eviction rule; exposed so tests can
    // drive capacity/ordering directly without a full postStimulus call.
    bool hasEntry(int list, uint64_t handle) const { return findConst(list, handle) != nullptr; }
    const PerceptionEntry* find(int list, uint64_t handle) const { return findConst(list, handle); }
    const std::vector<PerceptionEntry>& entries(int list) const { return lists_[list]; }
    const PerceptionEntry* head(int list) const { return lists_[list].empty() ? nullptr : &lists_[list].front(); }

    void addFlags(int list, uint64_t handle, uint32_t mask);
    void clearFlags(int list, uint64_t handle, uint32_t mask);
    // Overwrites just the position field, leaving age/score/flags/kind
    // untouched - used by A3's actions 42/43 (Sec11.2's navmesh-snap
    // write-back), which is a pure position correction, not a re-notice.
    void updatePosition(int list, uint64_t handle, Vec3 newPosition);
    // 0x004fdb20(handle): remove from all 22 lists.
    void removeHandleFromAllLists(uint64_t handle);
    // 0x004fdb60(handle, keep0): remove from lists 0 (unless keep0), 4, 14, 16.
    void removeHandleFromTargetLists(uint64_t handle, bool keep0);

    // --- Sec10.2 step 2: expiry pass ----------------------------------------------
    // objectGoneOrDown: 0x004d7f30/0x0096f4f0 - "the handle no longer
    // resolves, or the object is down/dead" - an external object-manager
    // predicate this unit does not own. Defaults to "always false" (nothing
    // expires early for lack of this input) so age-based expiry still works
    // in tests that never register it.
    std::function<bool(uint64_t handle)> objectGoneOrDown = [](uint64_t) { return false; };
    void expireEntries(int64_t nowMs);

    // --- Sec10.3 producers (the ones this unit implements in full) ---------------
    // "notice" 0x00500f70: the only creator of sensed list-0 entries.
    PostResult notice(uint64_t handle, Vec3 position, int attitude, bool isPlayerClass,
                      bool neverTurnOnPlayerFlag, bool provocationAtThreshold, int64_t nowMs);
    // "see" 0x005013b0: notice() then the in-view flag set + a list-1 memory record.
    PostResult see(uint64_t handle, Vec3 position, int attitude, bool isPlayerClass,
                   bool neverTurnOnPlayerFlag, bool provocationAtThreshold, int64_t nowMs);
    // "notice without sight" 0x00503210/0x00503900: promotes a list-6 entry to
    // list 0, else creates one via notice(). The position BLUR (Sec10.3: a
    // random offset scaled by the zero-fill tunable 0x013b8450) is NOT
    // reproduced - the tunable has no known value (Sec10's own scope note) -
    // so this passes the given position through unblurred. That changes the
    // POSITION an entry carries, never the flags/booleans any goal test or
    // action routine reads (Sec9.6/Sec11 read ages and flags off this list,
    // never raw position precision), so it does not change any A1/A3 test
    // outcome - a deliberate, documented simplification, not a guess at the
    // tunable's value.
    PostResult noticeWithoutSight(uint64_t handle, Vec3 position, int attitude, bool isPlayerClass,
                                   bool neverTurnOnPlayerFlag, bool provocationAtThreshold, int64_t nowMs);
    // "lost" 0x004fd840: called for an unusable/hidden object, or on a
    // blocked LOS answer (the caller additionally adds flag 0x02000000 on
    // the blocked-LOS path per Sec10.3's vision-test text; that add is not
    // folded into lost() itself since the spec attributes it to the caller).
    void lost(uint64_t handle);
    // "dismiss" 0x004fe980 (Sec10.4): team/exclusion gating (0x0094f690) is
    // the caller's job; this applies only the list-level effects.
    void dismiss();

    // list 16/8 damage producer, 0x00504130 (Sec10.3; formula CONFIRMED by
    // Sec10.3's own text and Sec10.3's "the fade" paragraph). Steps 2
    // (provocation meter feed, voice lines) and 3 (+0x317 bit 2, A0's field)
    // are outside this unit; callers apply those themselves.
    struct DamageProducerInput {
        uint64_t attackerHandle = 0;
        Vec3 attackerPosition;   // for the attitude<3 "notice the attacker" call
        double damage = 0;
        double maxHealth = 0;
        Vec3 victimPosition;     // the list-16 record's own position field (Sec10.3: "the victim's own position")
        int attackerAttitude = 0;
        bool attackerIsPlayerClass = false;
        bool neverTurnOnPlayerFlag = false;
        bool provocationAtThreshold = false;
    };
    void recordDamage(const DamageProducerInput& in, int64_t nowMs);
    // 0x004fc8b0: sum over list 16, CONFIRMED formula (Sec10.3's "the fade").
    // All four lanes are equal (Sec10.3), so a single double is exact.
    double damageAccumulator(int64_t nowMs) const;
    // 0x004fc960(handle): the same sum restricted to one attacker.
    double damageFromAttacker(uint64_t attackerHandle, int64_t nowMs) const;

    // list 3 ("shots at me") / list 14 ("gunfire heard") producer, 0x005046e0
    // (Sec10.3). This models only the CONFIRMED write targets (list 3/14
    // records and their gating thresholds); the near-miss/impact-distance
    // geometry and the "non-hostile shooter notices the shooter's target"
    // branch need a raycast/weapon-info query this unit does not own, so
    // callers pass the already-decided case.
    enum class GunfireCase { NervousPersonalityHeardTrouble, ShotLandedClose, NearMissOrHeard };
    void recordGunfire(uint64_t shooterHandle, Vec3 shooterOrShotPosition, GunfireCase whichCase,
                        bool isNearMiss, int64_t nowMs);

    // --- Sec10.6: timer +0x244, handle +0x250 (ally link), +0x248 reverse link ----
    EngineTimer clearLineOfFireTimer;  // +0x244 (human +0x754): restarted only by a clear list-2 probe result
    void recordClearLineOfFire(int64_t nowMs) { clearLineOfFireTimer.restart(nowMs); }

    bool hasAllyLink = false;          // +0x250 (human +0x760)
    uint64_t allyLinkHandle = 0;
    void setAllyLink(uint64_t friendHandle) { hasAllyLink = true; allyLinkHandle = friendHandle; }
    void clearAllyLink(int64_t nowMs) {
        hasAllyLink = false;
        allyLinkHandle = 0;
        // "timer +0x218 is restarted" (Sec10.6) - that staggered timer is
        // sense-pass scheduling content, not modelled here.
        (void)nowMs;
    }
    bool hasReverseAllyLink = false;   // +0x248: who linked to me
    uint64_t reverseAllyLinkHandle = 0;
    // +0x280 (human +0x790): "the friend's own perception threat handle"
    // read by ANOTHER human's friend scan (Sec10.6's information-sharing
    // paragraph); cleared by dismiss() (Sec10.4).
    bool hasThreatHandle280 = false;
    uint64_t threatHandle280 = 0;

    // --- Sec10.7: the query 0x004fcce0 --------------------------------------------
    struct QueryRow {
        int list = 0;                 // +0x00 (<= 22; 22 is a latent off-by-one the spec notes, not reproduced)
        uint64_t handle = 0;           // +0x08: 0 = "walk the list", non-zero = "test only this entity's entry"
        int64_t maxAgeMs = -1;         // +0x10: ignored when negative
        uint32_t allOf = 0;            // +0x14
        uint32_t anyOf = 0;            // +0x18: ignored when 0
        uint32_t noneOf = 0;           // +0x1c
    };
    // Returns the first match across rows in order (rows are a priority
    // order; within a row, the list's own order decides), or null.
    // handleOverride, when non-zero, restricts EVERY row to that one
    // entity's entry (Sec10.7: "with a non-null handle argument ... only
    // that entity's entry ... is tested").
    const PerceptionEntry* query(const std::vector<QueryRow>& rows, int64_t nowMs,
                                  uint64_t handleOverride = 0) const;
    // The investigation candidate's own fixed row set (Sec9.6/Sec10.7):
    // lists {1,0,15,16,14,13,7} in that order (list 7 dropped for a
    // follower), maxAge = Investigate/seen_in_last (the caller's latched or
    // fresh value - Sec9.6's quirk about the once-per-process latch is NOT
    // reproduced; this always uses the given seenInLastMs, which is the
    // simpler and more correct reading for a fresh port), noneOf = excluded.
    static std::vector<QueryRow> InvestigationCandidateRows(int64_t seenInLastMs, bool isFollower);

    // --- read-side helpers Sec11.6 settles against this same storage -------------
    static constexpr int64_t kNeverSeenMs = EngineTimer::kNeverSeenMs;
    // 0x004fc760(handle): 0 if currently in view (list-0 flag InView), else
    // the age of the list-1 "last sighting" entry, else kNeverSeenMs.
    int64_t timeSinceLastSeen(uint64_t handle, int64_t nowMs) const;
    // 0x004fc7e0: any list-0 entry currently in view.
    bool anyInView() const;
    // 0x004fc6d0(handle) / 0x004fdac0: visible now - InView, or (list-0 flag
    // Lof_ClearOrLost... no: per Sec11.6 it is flag 0x01000000 "while the
    // human has a cover object" - hasCoverObject is the caller's AI-state
    // fact, not perception's own).
    bool visibleNow(uint64_t handle, bool hasCoverObject) const;
    // 0x004fdae0: true when there is no target handle, or the target's
    // list-0 entry has flag LosValid (0x400).
    bool targetLineOfSightValidOrNone(bool hasTargetHandle, uint64_t targetHandle) const;
    // 0x004ffc70(handle): "engageable" (Sec9.6/Sec11.6). The "never when the
    // target is the +0x250 object" exclusion is the CALLER's (flush_out's
    // own text applies it specially, Sec9.6) - not baked in here.
    bool engageable(uint64_t handle, int64_t killEnemySeenInLastMs, int64_t nowMs) const;

    struct LastKnownPositionResult {
        bool ok = false;  // true exactly when the entity is an actively known hostile (flag 0x8) - Sec11.6
        Vec3 position;
    };
    // 0x004fe390(out, entity, aim, exact) for a HUMAN entity (a non-human's
    // bounding-box centre is the caller's/world's business, not perception's).
    LastKnownPositionResult lastKnownPosition(uint64_t handle, Vec3 entityVelocity, bool aim, bool exact,
                                               int64_t nowMs) const;

    // 0x009b7100(teamA, teamB) - the team-relationship read accessor (12
    // instructions, CONFIRMED by unit A4, spec-ai-runtime.md Sec12.7). Its
    // write side is spec-lua-api-behaviour.md Sec20.6/Sec30.6's set_team/
    // team_make_allies/_unfriendly/_hostile, which keep an 8x8 byte matrix
    // at a fixed VA this project does not map; callers pass that matrix (or
    // their own equivalent) in rather than this project owning the global.
    // Either team == 9 returns 2 (neutral) unconditionally - the read
    // side's defence against the write side's "unknown team name -> id 9"
    // defect (Sec12.7). Values observed: 0 hostile, 1 unfriendly, 4 allied,
    // matching perception's own attitude scale (low = more hostile).
    static int TeamRelationship(const std::array<std::array<uint8_t, 8>, 8>& matrix, int teamA, int teamB) {
        if (teamA == 9 || teamB == 9) return 2;
        if (teamA < 0 || teamA >= 8 || teamB < 0 || teamB >= 8) return 2;
        return matrix[static_cast<size_t>(teamA)][static_cast<size_t>(teamB)];
    }

private:
    std::array<std::vector<PerceptionEntry>, kPerceptionListCount> lists_;

    PerceptionEntry* findMutable(int list, uint64_t handle);
    const PerceptionEntry* findConst(int list, uint64_t handle) const;
    const PerceptionEntry* findInEitherList(int listA, int listB, uint64_t handle) const;
    // Inserts, maintaining Sec10.1's order rule; returns false if refused
    // (full, and the newcomer does not beat the eviction candidate).
    bool insertSorted(int list, PerceptionEntry e);
    void eraseFrom(int list, uint64_t handle);
};

}  // namespace sr3airuntime
