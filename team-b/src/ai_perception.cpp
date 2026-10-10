#include "sr3airuntime/perception.h"

#include <algorithm>
#include <limits>

namespace sr3airuntime {

const PerceptionEntry* PerceptionState::findConst(int list, uint64_t handle) const {
    for (const auto& e : lists_[list]) {
        if (e.handle == handle) return &e;
    }
    return nullptr;
}
PerceptionEntry* PerceptionState::findMutable(int list, uint64_t handle) {
    for (auto& e : lists_[list]) {
        if (e.handle == handle) return &e;
    }
    return nullptr;
}
const PerceptionEntry* PerceptionState::findInEitherList(int listA, int listB, uint64_t handle) const {
    if (const PerceptionEntry* e = findConst(listA, handle)) return e;
    return findConst(listB, handle);
}
void PerceptionState::eraseFrom(int list, uint64_t handle) {
    auto& vec = lists_[list];
    vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const PerceptionEntry& e) { return e.handle == handle; }),
              vec.end());
}

// --- Sec10.1: insertion order -------------------------------------------------------
bool PerceptionState::insertSorted(int list, PerceptionEntry e) {
    if (e.score < 0) return false;  // "an entry with a negative score is refused"
    auto& vec = lists_[list];
    const auto& meta = kPerceptionListMeta[list];
    if (meta.order == ListOrder::MostRecentFirst) {
        if (static_cast<int>(vec.size()) >= meta.capacity) {
            if (vec.empty()) return false;
            vec.pop_back();  // evict the tail (oldest)
        }
        vec.insert(vec.begin(), e);
        return true;
    }
    // ScoreAscending: lowest score at the head.
    if (static_cast<int>(vec.size()) >= meta.capacity) {
        if (vec.empty() || !(e.score < vec.back().score)) return false;  // refused unless lower than the tail
        vec.pop_back();
    }
    auto it = std::upper_bound(vec.begin(), vec.end(), e,
                                [](const PerceptionEntry& a, const PerceptionEntry& b) { return a.score < b.score; });
    vec.insert(it, e);
    return true;
}

PostResult PerceptionState::postStimulus(int list, uint64_t handle, Vec3 position, double score, uint32_t flags,
                                          int kind, int updateMode, int64_t nowMs) {
    if (score < 0) return PostResult::Refused;
    PerceptionEntry* existing = findMutable(list, handle);
    if (existing) {
        if (updateMode == 2 && !(score < existing->score)) return PostResult::Refused;
        // "Flag bits 0x1, 0x2 and 0x4 survive an update; every other bit is
        // replaced by the record's flags." (Sec10.1)
        const uint32_t keepMask = pflag::WrittenOff | pflag::Sticky2 | pflag::NeverExpires;
        const uint32_t newFlags = (existing->flags & keepMask) | (flags & ~keepMask);
        eraseFrom(list, handle);
        PerceptionEntry e;
        e.position = position;
        e.handle = handle;
        e.flags = newFlags;
        e.noticedAtMs = nowMs;
        e.score = score;
        e.listNumber = list;
        e.kind = kind;
        return insertSorted(list, e) ? PostResult::Refreshed : PostResult::Evicted;
    }
    PerceptionEntry e;
    e.position = position;
    e.handle = handle;
    e.flags = flags;
    e.noticedAtMs = nowMs;
    e.score = score;
    e.listNumber = list;
    e.kind = kind;
    return insertSorted(list, e) ? PostResult::Created : PostResult::Refused;
}

void PerceptionState::updatePosition(int list, uint64_t handle, Vec3 newPosition) {
    if (auto* e = findMutable(list, handle)) e->position = newPosition;
}

void PerceptionState::addFlags(int list, uint64_t handle, uint32_t mask) {
    if (auto* e = findMutable(list, handle)) e->flags |= mask;
}
void PerceptionState::clearFlags(int list, uint64_t handle, uint32_t mask) {
    if (auto* e = findMutable(list, handle)) e->flags &= ~mask;
}
void PerceptionState::removeHandleFromAllLists(uint64_t handle) {
    for (int i = 0; i < kPerceptionListCount; ++i) eraseFrom(i, handle);
}
void PerceptionState::removeHandleFromTargetLists(uint64_t handle, bool keep0) {
    if (!keep0) eraseFrom(plist::KnownHostiles, handle);
    eraseFrom(plist::Unused4, handle);
    eraseFrom(plist::GunfireHeard, handle);
    eraseFrom(plist::DamageMemory, handle);
}

// --- Sec10.2 step 2: expiry ----------------------------------------------------------
void PerceptionState::expireEntries(int64_t nowMs) {
    for (int list = 0; list < kPerceptionListCount; ++list) {
        auto& vec = lists_[list];
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                  [&](const PerceptionEntry& e) {
                                      if ((list == plist::KnownHostiles || list == plist::Unused4 ||
                                           list == plist::Hearsay) &&
                                          objectGoneOrDown(e.handle)) {
                                          return true;
                                      }
                                      if (list == plist::LastSightings && !hasEntry(plist::KnownHostiles, e.handle)) {
                                          return true;  // "never outlives the awareness it belongs to"
                                      }
                                      if (list == plist::ImminentThreats && e.kind != 0xff &&
                                          objectGoneOrDown(e.handle)) {
                                          return true;
                                      }
                                      const auto& meta = kPerceptionListMeta[list];
                                      if (meta.lifetimeMs > 0 && !(e.flags & pflag::NeverExpires) &&
                                          (nowMs - e.noticedAtMs) > meta.lifetimeMs) {
                                          return true;
                                      }
                                      return false;
                                  }),
                  vec.end());
    }
}

// --- Sec10.3: producers --------------------------------------------------------------
PostResult PerceptionState::notice(uint64_t handle, Vec3 position, int attitude, bool isPlayerClass,
                                    bool neverTurnOnPlayerFlag, bool provocationAtThreshold, int64_t nowMs) {
    bool accept = !isPlayerClass ? (attitude < 4)
                                  : (!neverTurnOnPlayerFlag && (attitude < 4 || provocationAtThreshold));
    if (!accept) return PostResult::Refused;

    eraseFrom(plist::Hearsay, handle);  // "removes the entity's list-6 entry"

    PerceptionEntry* existing = findMutable(plist::KnownHostiles, handle);
    if (existing) {
        existing->position = position;
        existing->noticedAtMs = nowMs;
        existing->flags = (existing->flags &
                            ~(pflag::WrittenOff | pflag::PositionVerified | pflag::ExcludedFromInvestigation)) |
                           pflag::ActivelyKnown;
        // Target-memory update (0x004fd1b0) and the kind-71 "sighting lost"
        // event cancel (0x004f6420) on refresh are outside this unit
        // (target bookkeeping is A1's; events are A0's) - Sec10.3.
        return PostResult::Refreshed;
    }
    PerceptionEntry e;
    e.handle = handle;
    e.position = position;
    e.score = 0;
    e.noticedAtMs = nowMs;
    e.listNumber = plist::KnownHostiles;
    e.flags = pflag::ActivelyKnown;
    return insertSorted(plist::KnownHostiles, e) ? PostResult::Created : PostResult::Refused;
}

PostResult PerceptionState::see(uint64_t handle, Vec3 position, int attitude, bool isPlayerClass,
                                 bool neverTurnOnPlayerFlag, bool provocationAtThreshold, int64_t nowMs) {
    PostResult r = notice(handle, position, attitude, isPlayerClass, neverTurnOnPlayerFlag, provocationAtThreshold,
                           nowMs);
    if (r == PostResult::Refused) return r;
    PerceptionEntry* e = findMutable(plist::KnownHostiles, handle);
    if (!e) return PostResult::Refused;  // defensive; notice() just created/refreshed this entry
    e->flags |= (pflag::ActivelyKnown | pflag::PositionVerified | pflag::InView | pflag::VisuallyConfirmedOnce |
                 pflag::LosValid);
    e->flags &= ~(pflag::NonVisualCurrent | pflag::ExcludedFromInvestigation | pflag::Lof_ClearOrLost);
    postStimulus(plist::LastSightings, handle, position, e->score, 0, e->kind, /*updateMode=*/0, nowMs);
    // "+0x25f bit 2" (Sec10.3) is an unlabeled perception scheduling flag
    // with no reader inside this unit's scope; not modelled.
    return r;
}

PostResult PerceptionState::noticeWithoutSight(uint64_t handle, Vec3 position, int attitude, bool isPlayerClass,
                                                bool neverTurnOnPlayerFlag, bool provocationAtThreshold,
                                                int64_t nowMs) {
    const PerceptionEntry* existing = findConst(plist::KnownHostiles, handle);
    if (existing && (existing->flags & pflag::InView)) {
        return PostResult::Refreshed;  // "returns an in-view entry unchanged"
    }
    // notice() already erases any list-6 entry and (re)creates/refreshes the
    // list-0 entry, which is the same net effect as "promote a list-6 entry
    // to list 0" (Sec10.3). The position BLUR (a run-time tunable,
    // 0x013b8450, zero-fill - out of this unit's scope per its header note)
    // is intentionally not reproduced; the given position is used as-is.
    PostResult r = notice(handle, position, attitude, isPlayerClass, neverTurnOnPlayerFlag, provocationAtThreshold,
                           nowMs);
    if (r == PostResult::Refused) return r;
    PerceptionEntry* e = findMutable(plist::KnownHostiles, handle);
    if (e && !(e->flags & pflag::InView)) {
        e->flags |= (pflag::NonVisualNoticed | pflag::NonVisualCurrent);  // "adds flags 0x280 unless 0x40 is set"
    }
    return r;
}

void PerceptionState::lost(uint64_t handle) {
    clearFlags(plist::KnownHostiles, handle, pflag::PositionVerified | pflag::InView);
}

void PerceptionState::dismiss() {
    // Sec10.4: "score set to the maximum". Precise value per Sec12.9 (A4):
    // the constant at 0x0137fa90, literal source 0x011173a8, ~3.0625e38 -
    // the same "dismissed/maximum" sentinel 0x004fc100 compares against
    // (Sec12.5) and direct-awareness uses (Sec10.3).
    constexpr double kDismissMaxScore = 3.0625e38;
    for (auto& e : lists_[plist::KnownHostiles]) {
        e.flags = (e.flags & ~(pflag::ActivelyKnown | pflag::PositionVerified | pflag::InView)) | pflag::WrittenOff;
        e.score = kDismissMaxScore;
    }
    // The bulk score bump invalidates the ascending-score ordering; restore it.
    std::sort(lists_[plist::KnownHostiles].begin(), lists_[plist::KnownHostiles].end(),
              [](const PerceptionEntry& a, const PerceptionEntry& b) { return a.score < b.score; });
    lists_[plist::LastSightings].clear();
    hasThreatHandle280 = false;
    threatHandle280 = 0;
    // "Forgets the players" (0x004fdb60 per player handle, Sec10.4) is the
    // host's job: this unit does not track which handles are players; call
    // removeHandleFromTargetLists(playerHandle, /*keep0=*/false) per known
    // player alongside this call. The 0x004f67e0/0x004f6e10(8,3) calls on
    // +0x270 (Sec10.4) are unidentified and out of scope.
}

void PerceptionState::recordDamage(const DamageProducerInput& in, int64_t nowMs) {
    if (in.damage <= 0 || in.maxHealth <= 0) return;
    const double K = 100.0;
    const int64_t L = kPerceptionListMeta[plist::DamageMemory].lifetimeMs;  // 30000ms
    const PerceptionEntry* existing = findConst(plist::DamageMemory, in.attackerHandle);
    double R = 0;
    if (existing) {
        const double age = static_cast<double>(nowMs - existing->noticedAtMs);
        const double decayed = existing->score + (K - existing->score) * age / static_cast<double>(L);
        R = 1.0 - decayed / K;
    }
    double newScore = K * (1.0 - in.damage / in.maxHealth - R);
    if (newScore < 0) newScore = 0;

    postStimulus(plist::DamageMemory, in.attackerHandle, in.victimPosition, newScore, pflag::Sticky2, -1,
                 /*updateMode=*/0, nowMs);

    if (in.attackerAttitude < 3) {
        noticeWithoutSight(in.attackerHandle, in.attackerPosition, in.attackerAttitude, in.attackerIsPlayerClass,
                            in.neverTurnOnPlayerFlag, in.provocationAtThreshold, nowMs);
    } else {
        postStimulus(plist::FriendlyFire, in.attackerHandle, in.victimPosition, newScore * 2.0, 0, -1,
                     /*updateMode=*/2, nowMs);
    }
}

double PerceptionState::damageAccumulator(int64_t nowMs) const {
    const double K = 100.0;
    const int64_t L = kPerceptionListMeta[plist::DamageMemory].lifetimeMs;
    double sum = 0;
    for (const auto& e : lists_[plist::DamageMemory]) {
        const double age = static_cast<double>(nowMs - e.noticedAtMs);
        const double term = (1.0 - e.score / K) * (1.0 - age / static_cast<double>(L));
        if (term > 0) sum += term;
    }
    return sum;
}
double PerceptionState::damageFromAttacker(uint64_t attackerHandle, int64_t nowMs) const {
    const double K = 100.0;
    const int64_t L = kPerceptionListMeta[plist::DamageMemory].lifetimeMs;
    const PerceptionEntry* e = findConst(plist::DamageMemory, attackerHandle);
    if (!e) return 0;
    const double age = static_cast<double>(nowMs - e->noticedAtMs);
    const double term = (1.0 - e->score / K) * (1.0 - age / static_cast<double>(L));
    return term > 0 ? term : 0;
}

void PerceptionState::recordGunfire(uint64_t shooterHandle, Vec3 shooterOrShotPosition, GunfireCase whichCase,
                                     bool isNearMiss, int64_t nowMs) {
    if (const PerceptionEntry* existing3 = findConst(plist::ShotsAtMe, shooterHandle)) {
        if (nowMs - existing3->noticedAtMs < 500) return;  // "returns at once while < 500ms old"
    }
    switch (whichCase) {
        case GunfireCase::NervousPersonalityHeardTrouble:
        case GunfireCase::ShotLandedClose:
            postStimulus(plist::ShotsAtMe, shooterHandle, shooterOrShotPosition, 0, 0, -1, /*updateMode=*/2, nowMs);
            break;
        case GunfireCase::NearMissOrHeard:
            postStimulus(plist::GunfireHeard, shooterHandle, shooterOrShotPosition, 0, 0, -1, /*updateMode=*/2,
                         nowMs);
            break;
    }
    (void)isNearMiss;
    // The near-miss/impact-distance raycast geometry, the "notice the
    // shooter's target instead" non-hostile branch, and +0x316 bit 1 are
    // the CALLER's job (Sec10.3): this models only the confirmed list
    // writes once a case has already been decided.
}

// --- Sec10.7: query ------------------------------------------------------------------
const PerceptionEntry* PerceptionState::query(const std::vector<QueryRow>& rows, int64_t nowMs,
                                               uint64_t handleOverride) const {
    for (const auto& row : rows) {
        if (row.list < 0 || row.list >= kPerceptionListCount) continue;
        auto passes = [&](const PerceptionEntry& e) {
            if (row.maxAgeMs >= 0 && (nowMs - e.noticedAtMs) > row.maxAgeMs) return false;
            if ((e.flags & row.allOf) != row.allOf) return false;
            if (row.anyOf != 0 && (e.flags & row.anyOf) == 0) return false;
            if ((e.flags & row.noneOf) != 0) return false;
            return true;
        };
        const uint64_t h = handleOverride != 0 ? handleOverride : row.handle;
        if (h != 0) {
            const PerceptionEntry* e = findConst(row.list, h);
            if (e && passes(*e)) return e;
        } else {
            for (const auto& e : lists_[row.list]) {
                if (passes(e)) return &e;
            }
        }
    }
    return nullptr;
}

std::vector<PerceptionState::QueryRow> PerceptionState::InvestigationCandidateRows(int64_t seenInLastMs,
                                                                                    bool isFollower) {
    const std::array<int, 7> listOrder = {plist::LastSightings, plist::KnownHostiles, plist::LoudEvents,
                                           plist::DamageMemory,  plist::GunfireHeard,  plist::WitnessedAttacks,
                                           plist::BodyFound};
    const int count = isFollower ? 6 : 7;  // "the last row dropped for a follower"
    std::vector<QueryRow> rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i) {
        QueryRow r;
        r.list = listOrder[i];
        r.handle = 0;
        r.maxAgeMs = seenInLastMs;
        r.allOf = 0;
        r.anyOf = 0;
        r.noneOf = pflag::ExcludedFromInvestigation;
        rows.push_back(r);
    }
    return rows;
}

// --- Sec11.6 read-side helpers --------------------------------------------------------
int64_t PerceptionState::timeSinceLastSeen(uint64_t handle, int64_t nowMs) const {
    const PerceptionEntry* e0 = findConst(plist::KnownHostiles, handle);
    if (e0 && (e0->flags & pflag::InView)) return 0;
    const PerceptionEntry* e1 = findConst(plist::LastSightings, handle);
    if (e1) return nowMs - e1->noticedAtMs;
    return kNeverSeenMs;
}

bool PerceptionState::anyInView() const {
    for (const auto& e : lists_[plist::KnownHostiles]) {
        if (e.flags & pflag::InView) return true;
    }
    return false;
}

bool PerceptionState::visibleNow(uint64_t handle, bool hasCoverObject) const {
    const PerceptionEntry* e0 = findConst(plist::KnownHostiles, handle);
    if (e0 && (e0->flags & pflag::InView)) return true;
    // Sec11.6 reads this as "the entity's list-0 entry has flag 0x01000000",
    // but Sec10.4's own flag-setter table documents 0x01000000 only for
    // list 2 ("obstructed"). Read here as the entity's LIST-2 entry (the
    // line-of-fire-to-target record), which is the internally consistent
    // interpretation: with a cover object, an obstructed-but-tracked line
    // of fire still counts as "visible" (JUDGMENT CALL, cited here).
    if (hasCoverObject) {
        const PerceptionEntry* e2 = findConst(plist::LineOfFire, handle);
        if (e2 && (e2->flags & pflag::Lof_Obstructed)) return true;
    }
    return false;
}

bool PerceptionState::targetLineOfSightValidOrNone(bool hasTargetHandle, uint64_t targetHandle) const {
    if (!hasTargetHandle) return true;
    const PerceptionEntry* e = findConst(plist::KnownHostiles, targetHandle);
    return e != nullptr && (e->flags & pflag::LosValid) != 0;
}

bool PerceptionState::engageable(uint64_t handle, int64_t killEnemySeenInLastMs, int64_t nowMs) const {
    const PerceptionEntry* e0 = findConst(plist::KnownHostiles, handle);
    if (!e0) return false;
    const PerceptionEntry* e2 = findConst(plist::LineOfFire, handle);
    if (e2) return (e2->flags & 0x06000000u) != 0;
    return timeSinceLastSeen(handle, nowMs) <= killEnemySeenInLastMs;
}

PerceptionState::LastKnownPositionResult PerceptionState::lastKnownPosition(uint64_t handle, Vec3 velocity,
                                                                             bool aim, bool exact,
                                                                             int64_t nowMs) const {
    const PerceptionEntry* e = findInEitherList(plist::KnownHostiles, plist::Hearsay, handle);
    if (!e) return {false, {}};
    if (!(e->flags & pflag::ActivelyKnown)) return {false, e->position};
    if (e->flags & pflag::InView) {
        if (aim) {
            throw OpenStateError("0x004fe390 aim-point computation",
                                  "spec-ai-runtime.md Sec11.6 (aim-point geometry not detailed)");
        }
        if (exact) return {true, e->position};
        return {true, e->position + velocity * 0.1};
    }
    const double age = static_cast<double>(nowMs - e->noticedAtMs);
    return {true, e->position + velocity * age};
}

}  // namespace sr3airuntime
