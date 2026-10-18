#include "sr3airuntime/npc_ai_runtime.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sr3airuntime {

namespace {

bool EqualsNoCase(const std::string& a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

int ActionIdByName(const std::string& name) {
    for (int i = 0; i < action::kCount; ++i)
        if (EqualsNoCase(name, sr3tables_trafficai::enums::kCombatActionNames[static_cast<size_t>(i)])) return i;
    return -1;
}

int64_t SecondsToMs(const sr3xtbl::Always<float>& v) { return static_cast<int64_t>(v.value * 1000.0f); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Data loading (spec-tables-traffic-ai.md Sec9/Sec10 loader rules; spec-tables-weapons-combat.md Sec11.1)
// ---------------------------------------------------------------------------------------------------------------
BehaviorRecord BehaviorRecordFromRow(const sr3tables_trafficai::AiBehavior& row,
                                      const std::array<int64_t, action::kCount>& minRepeatMs) {
    BehaviorRecord r;
    // Ability table: Repeat_min raised to the action table's minimum, Repeat_max to at least Repeat_min; enabled = 1.
    for (const auto& a : row.actions) {
        const int id = a.action.index;
        if (id < 0 || id >= action::kCount) continue;
        AbilityEntry& e = r.abilities[static_cast<size_t>(id)];
        e.repeatMinMs = static_cast<int32_t>(std::max<int64_t>(SecondsToMs(a.repeatMin), minRepeatMs[static_cast<size_t>(id)]));
        e.repeatMaxMs = static_cast<int32_t>(std::max<int64_t>(SecondsToMs(a.repeatMax), e.repeatMinMs));
        e.enabled = true;
    }
    // Action 14 (hold position) is always enabled with repeats 0 regardless of the file.
    r.abilities[action::HoldPosition] = AbilityEntry{0, 0, true};

    // Goal-enabled bytes: 1 when the goal element exists; change mode (0), frustrated (13), idle (16) always 1.
    auto& g = r.goalEnabled;
    g[goal::ChangeMode] = 1;
    g[goal::Frustrated] = 1;
    g[goal::Idle] = 1;
    g[goal::Survive] = row.survivePresent;
    g[goal::Suppress] = row.suppressPresent;
    g[goal::Cover] = row.cover.present;
    g[goal::TakingDamage] = row.takingDamagePresent;
    g[goal::Group] = row.group.present;
    g[goal::Advance] = row.advancePresent;
    g[goal::AvoidBrute] = row.avoidBrutePresent;
    g[goal::KillNearPlayer] = row.killNearPlayerPresent;
    g[goal::Retreat] = row.retreatPresent;
    g[goal::KillEnemy] = row.killEnemy.present;
    g[goal::Reload] = row.reloadPresent;
    g[goal::FlushOut] = row.flushOut.present;
    g[goal::AcquireTarget] = row.acquireTarget.present;
    g[goal::Investigate] = row.investigate.present;

    r.lowHealthPct = row.survivalLowHealthPct.value;
    r.clipPct = row.reloadClipPct.value;
    if (row.group.present) {
        r.seperatedDistance = row.group.seperatedDistance.value;
        r.seperatedDistanceInterior =
            row.group.seperatedDistanceInterior ? *row.group.seperatedDistanceInterior : r.seperatedDistance;
    }  // else: FLT_MAX stand-ins already in the record (BehaviorRecord defaults)
    if (row.cover.present) {
        r.abandonCoverTimeMs = SecondsToMs(row.cover.abandonCoverTime);
        r.findNewCoverTimeMs = SecondsToMs(row.cover.findNewCoverTime);
        r.advanceCoverTimeMs = SecondsToMs(row.cover.advanceCoverTime);
        r.stayDownTimeMs = SecondsToMs(row.cover.stayDownTime);
        r.popUpTimeMs = SecondsToMs(row.cover.popUpTime);
        r.squadCoverPct = row.cover.squadCoverPct.value;
        r.takeCoverWhenAimedAt = row.cover.takeCoverAimedAt;
        r.takeCoverWhenShotAt = row.cover.takeCoverShotAt;
        r.takeCoverWhenDamaged = row.cover.takeCoverDamaged;
    }  // else: 30000/30000/30000/1000/30000 and squad_cover_pct 100 are the BehaviorRecord defaults
    r.suppressLengthMs = row.suppressPresent ? SecondsToMs(row.suppressLength) : 0;
    // seen_in_last: zeroed if the goal is absent, then forced monotone non-decreasing.
    int64_t kill = row.killEnemy.present ? SecondsToMs(row.killEnemy.seenInLast) : 0;
    int64_t flush = row.flushOut.present ? SecondsToMs(row.flushOut.seenInLast) : 0;
    int64_t acquire = row.acquireTarget.present ? SecondsToMs(row.acquireTarget.seenInLast) : 0;
    int64_t investigate = row.investigate.present ? SecondsToMs(row.investigate.seenInLast) : 0;
    flush = std::max(flush, kill);
    acquire = std::max(acquire, flush);
    investigate = std::max(investigate, acquire);
    r.killEnemySeenInLastMs = kill;
    r.flushOutSeenInLastMs = flush;
    r.acquireTargetSeenInLastMs = acquire;
    r.investigateSeenInLastMs = investigate;
    r.dodgeChance = row.options.present ? row.options.dodgeChance.value : 0;
    // combat_aggression: first character minus '0' (0..9); forced to 1 when Scripted_Only.
    int ca = 1;
    if (!row.scriptedOnly.value && row.combatAggression && !row.combatAggression->empty()) {
        const int d = (*row.combatAggression)[0] - '0';
        if (d >= 0 && d <= 9) ca = d;
    }
    r.combatAggression = ca;
    r.goalsCanProcessEarly = row.goalsCanProcessEarly.value;
    return r;
}

NpcAiTables BuildNpcAiTables(const sr3tables_trafficai::AiGoalsTable& goals,
                              const sr3tables_trafficai::AiBehaviorTable& behaviors,
                              const std::vector<sr3tables_weapons::CombatActionOverrideRow>& actions,
                              const sr3tables_trafficai::AiPersonalitiesTable* personalities,
                              const std::string& combatantBehavior, const std::string& civilianBehavior) {
    NpcAiTables t;
    std::array<int64_t, action::kCount> minRepeat{};
    for (const auto& row : actions) {
        const int id = ActionIdByName(row.name);
        if (id < 0) continue;  // a name that matches nothing is ignored by the loader
        minRepeat[static_cast<size_t>(id)] = row.minRepeatTimeMs.value;
        t.noInterruptMs[static_cast<size_t>(id)] = row.noInterruptMs.value;
        t.preFlags[static_cast<size_t>(id)] = row.preconditionFlags;
    }
    for (const auto& gRow : goals.rows) {
        const int gid = gRow.name.index;
        if (gid < 0 || gid >= goal::kCount) continue;
        size_t n = 0;
        for (const auto& a : gRow.actions) {
            if (n >= 24) break;  // the engine keeps at most 24
            if (a.action.index < 0 || a.action.index >= action::kCount) continue;  // unknown dropped
            t.goalActionLists[static_cast<size_t>(gid)][n++] = static_cast<uint8_t>(a.action.index);
        }
    }
    auto pick = [&](const std::string& name) -> const sr3tables_trafficai::AiBehavior* {
        for (const auto& b : behaviors.rows)
            if (b.name && EqualsNoCase(*b.name, name)) return &b;
        return behaviors.rows.empty() ? nullptr : &behaviors.rows.front();
    };
    if (const auto* b = pick(combatantBehavior)) t.combatant = BehaviorRecordFromRow(*b, minRepeat);
    if (const auto* b = pick(civilianBehavior)) t.civilian = BehaviorRecordFromRow(*b, minRepeat);
    if (personalities) {
        for (const auto& p : personalities->rows) {
            if (!p.name || !EqualsNoCase(*p.name, "civilian normal")) continue;
            t.civilianFleeHealthPct = p.fleeHealthPct.value;
            t.civilianFleeEvents = static_cast<uint8_t>((p.fleeAimedAt ? 0x01 : 0) | (p.fleeMeleeHit ? 0x02 : 0) |
                                                         (p.fleeBulletHeard ? 0x04 : 0) | (p.fleeBulletHit ? 0x08 : 0) |
                                                         (p.fleeExplosionHeard ? 0x10 : 0));
            break;
        }
    }
    t.loadedFromData = true;
    return t;
}

// ---------------------------------------------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------------------------------------------
NpcAiRuntime::NpcAiRuntime(std::shared_ptr<const NpcAiTables> tables, NpcAiConfig cfg, uint64_t seed)
    : tables_(std::move(tables)), cfg_(cfg), seed_(seed) {}
NpcAiRuntime::~NpcAiRuntime() = default;

double NpcAiRuntime::rand01(NpcState& s) { return static_cast<double>(s.rng->next(1u << 24)) / static_cast<double>(1u << 24); }

void NpcAiRuntime::addNpc(const NpcSpawn& sp, int64_t nowMs) {
    (void)nowMs;
    if (npcs_.count(sp.handle)) return;
    NpcState& s = npcs_[sp.handle];
    s.handle = sp.handle;
    s.kind = sp.kind;
    s.pos = sp.pos;
    s.cantFlee = sp.cantFlee;  // the spawn's personality flags (Sec21.4 chooser reads them)
    s.cantCower = sp.cantCower;
    s.alwaysCower = sp.alwaysCower;
    s.female = sp.female;
    s.ai = std::make_unique<HumanAiState>();
    s.rng = std::make_unique<DeterministicRng>(seed_ ^ (sp.handle * 0x9E3779B97F4A7C15ULL));
    order_.push_back(sp.handle);
}

void NpcAiRuntime::setCoverNodes(std::vector<Vec3> positions) {
    for (auto& kv : npcs_) kv.second.coverNodeIdx = -1;
    coverNodes_.clear();
    coverNodes_.reserve(positions.size());
    for (const Vec3& p : positions) coverNodes_.push_back(CoverNodeSlot{p, 0});
}

void NpcAiRuntime::releaseCover(NpcState& s) {
    if (s.coverNodeIdx >= 0 && static_cast<size_t>(s.coverNodeIdx) < coverNodes_.size() &&
        coverNodes_[static_cast<size_t>(s.coverNodeIdx)].claimedBy == s.handle)
        coverNodes_[static_cast<size_t>(s.coverNodeIdx)].claimedBy = 0;
    s.coverNodeIdx = -1;
}

// Sec21.3 search 0x0050a690 (FINAL, Sec15.7): gather every live cover node inside the AABB (half-width 14.0, half-height
// 4.0) that is unclaimed and not on the failure list (perception list 20); the evaluator 0x00509e50 is a hard gate for
// "cover take def": a node that does not shield the human from the player threat is rejected; the cheapest survivor is
// claimed by compare-and-set of the owner handle. CHOSEN stand-ins (also a question to Team A):
//  - the evaluator's arc test 0x008cd1d0 needs the node's world-placement yaw and the cover_angle_* / cover_def_angle_*
//    bytes, which the zone reader does not decode yet (the position quaternion has no confirmed component order), so
//    "shielded" is taken as "the LOS seam reports the ray from the player to the node blocked" (a collision-geometry
//    shield); with no LOS seam nothing shields and every search fails;
//  - any_primary := shielded and count := 1, so the threat term (any_primary ? 0 : wA) + (count - shielded) * wB is 0
//    for every survivor and the ranking falls to the distance term;
//  - the path/cohesion cost terms and the multi-destination query 0x00a12a80 are replaced by the straight horizontal
//    distance from the human to the node (cheapest = nearest).
std::optional<Vec3> NpcAiRuntime::searchCover(NpcState& s, const NpcFacts&, const PlayerFacts& pl, int64_t now) {
    if (coverNodes_.empty()) return std::nullopt;
    HumanAiState& h = *s.ai;
    ++stats_.coverSearches;
    int best = -1;
    double bestD = 0.0;
    for (size_t i = 0; i < coverNodes_.size(); ++i) {
        const CoverNodeSlot& n = coverNodes_[i];
        {
            const double dxz = std::hypot(n.pos.x - s.pos.x, n.pos.z - s.pos.z);
            if (dxz < stats_.coverNearestNodeXZ) {
                stats_.coverNearestNodeXZ = dxz;
                stats_.coverNearestNodeDy = n.pos.y - s.pos.y;
            }
        }
        if (n.claimedBy != 0 && n.claimedBy != s.handle) continue;
        if (std::fabs(n.pos.x - s.pos.x) > cfg_.coverSearchHalfWidth || std::fabs(n.pos.z - s.pos.z) > cfg_.coverSearchHalfWidth ||
            std::fabs(n.pos.y - s.pos.y) > cfg_.coverSearchHalfHeight)
            continue;
        if (h.perception.hasEntry(plist::CoverMemory20, static_cast<uint64_t>(i) + 1)) continue;
        ++stats_.coverNodesInBox;
        if (!pl.alive || !lineOfSight) continue;  // nothing to shield from / no shield test available
        ++stats_.coverShieldTests;
        Vec3 from = pl.pos, to = n.pos;
        from.y += cfg_.losEyeHeight;
        to.y += cfg_.coverShieldHeight;
        if (lineOfSight(from, to)) continue;  // clear ray: the node does not shield (evaluator returns 0 for the player)
        const double d = std::hypot(s.pos.x - n.pos.x, s.pos.z - n.pos.z);
        if (best < 0 || d < bestD) {
            best = static_cast<int>(i);
            bestD = d;
        }
    }
    if (best < 0) {
        ++stats_.coverNoNode;
        return std::nullopt;
    }
    CoverNodeSlot& chosen = coverNodes_[static_cast<size_t>(best)];
    releaseCover(s);
    chosen.claimedBy = s.handle;  // compare-and-set: single-threaded here, so it cannot lose
    s.coverNodeIdx = best;
    ++stats_.coverFound;
    (void)now;
    return chosen.pos;
}

void NpcAiRuntime::removeNpc(uint64_t handle) {
    auto it = npcs_.find(handle);
    if (it != npcs_.end()) releaseCover(it->second);
    npcs_.erase(handle);
    order_.erase(std::remove(order_.begin(), order_.end(), handle), order_.end());
}

void NpcAiRuntime::tick(int64_t nowMs, const PlayerFacts& player, const std::vector<NpcFacts>& facts,
                        std::vector<NpcStep>& steps, std::vector<NpcShot>& shots) {
    steps.clear();
    shots.clear();
    for (const auto& f : facts) {
        auto it = npcs_.find(f.handle);
        if (it == npcs_.end()) continue;
        NpcStep st;
        try {
            tickOne(it->second, f, nowMs, player, st, shots);
        } catch (const OpenStateError& e) {
            // Some other OPEN value was read (a mode begin function the spec does not detail, ...): skip this NPC for
            // the frame and remember the message once; nothing is invented to get past it.
            ++openErrorCount_;
            const std::string msg = e.what();
            if (std::find(openErrors_.begin(), openErrors_.end(), msg) == openErrors_.end() && openErrors_.size() < 32)
                openErrors_.push_back(msg);
            st.handle = f.handle;
            st.pos = f.pos;
            st.mode = it->second.ai->mode.state.currentMode;
            st.action = it->second.ai->mode.state.currentAction;
        }
        steps.push_back(st);
    }
}

namespace {

double HorizDist(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// Moves `pos` toward `to` by at most speed*dt on the ground plane (y follows `to`'s only if it is a real position).
// Returns the distance that remained before the step.
double StepToward(Vec3& pos, const Vec3& to, double speed, double dt) {
    const double dx = to.x - pos.x, dz = to.z - pos.z;
    const double d = std::sqrt(dx * dx + dz * dz);
    if (d < 1e-9) return d;
    const double step = std::min(d, speed * dt);
    pos.x += dx / d * step;
    pos.z += dz / d * step;
    return d;
}

bool IsAdvanceLike(int a) {
    return a == action::MoveAdvance || a == action::MoveChase || a == action::AvatarChase || a == action::MoveRush ||
           a == action::MoveRushToMelee || a == action::MoveCloseIn || a == action::MoveSurround ||
           a == action::KillbaneChase;
}

}  // namespace

void NpcAiRuntime::tickOne(NpcState& s, const NpcFacts& f, int64_t now, const PlayerFacts& pl, NpcStep& step,
                           std::vector<NpcShot>& shots) {
    HumanAiState& h = *s.ai;
    ModeState& ms = h.mode.state;
    const bool combatant = s.kind == NpcKind::Combatant;
    step.handle = f.handle;
    step.pos = f.pos;

    // ---- first tick: seed the human from the shared tables (Sec9.4/Sec9.7 data) and enter Idle -------------------
    if (s.lastTickMs < 0) {
        h.behavior = combatant ? tables_->combatant : tables_->civilian;
        // OPEN (Sec9.6 goal 2 / Sec8 item 6): the kill-near-player radius is never written anywhere, so the goal is
        // left disabled rather than inventing a radius.
        h.behavior.goalEnabled[goal::KillNearPlayer] = 0;
        h.goalActionLists = tables_->goalActionLists;
        h.preFlags = tables_->preFlags;
        h.noInterruptMs = tables_->noInterruptMs;
        for (auto& t : h.actionRepeatTimers) t.arm(now, 0);  // armed and expired: an unarmed timer would block its action
        h.forceFlags.alwaysCower = s.alwaysCower;
        h.forceFlags.cantFlee = s.cantFlee;
        h.forceFlags.cantCower = s.cantCower;
        h.mode.beginInputs.cowerFlagsByteBit0.set(false);  // OPEN item 1 of Sec13.1.1: CHOSEN false
        h.mode.randomRangeMs = [this, &s](int64_t lo, int64_t hi) {
            return lo + static_cast<int64_t>(s.rng->next(static_cast<size_t>(std::max<int64_t>(1, hi - lo + 1))));
        };
        h.mode.request(mode::Idle, now);
        h.mode.apply(now);
        ms.actionFlags |= ModeState::kActionFinished;  // the first decision runs on the next gate check
        ms.idleTimer.restart(now, 0);
        s.nextSenseMs = now + static_cast<int64_t>(s.rng->next(static_cast<size_t>(cfg_.senseMaxMs - cfg_.senseMinMs + 1)));
        s.loggedMode = ms.currentMode;
        s.loggedAction = ms.currentAction;
        s.lastReevalMs = now;
    }
    const double dt = s.lastTickMs < 0 ? 0.0 : std::min(0.25, static_cast<double>(now - s.lastTickMs) / 1000.0);
    s.lastTickMs = now;
    s.pos = f.pos;
    if (!f.alive || f.ignoreAi) {
        if (!f.alive) releaseCover(s);  // a dead NPC frees its cover node claim
        step.mode = ms.currentMode;
        step.action = ms.currentAction;
        return;
    }
    h.position = f.pos;
    h.health = f.health;
    h.maxHealth = f.maxHealth;
    // Sec10 list 16 / list 8 (0x00504130): the damage reaction branches on the attitude toward the attacker. Below 3 the
    // attacker is noticed (list 0: hostile for good, the list never expires by age) unless never_turn_on_player is set;
    // at 4 or more only a list-8 friendly-fire record is written and the NPC does not turn on the player (AI-5). When the
    // host gives no attitude: 0 for a hostile NPC, else 2 (the loop's behaviour before attitudes existed).
    const int attitude = f.attitudeToPlayer >= 0 ? f.attitudeToPlayer : (f.hostileToPlayer ? 0 : 2);
    if ((f.provoked || f.damageFromPlayer > 0) && attitude < 3 && !f.neverTurnOnPlayer) s.provoked = true;
    const bool hostile = f.hostileToPlayer || (combatant && s.provoked);
    const double dist = Distance(s.pos, pl.pos);
    // Sec10/Sec12 (FINAL): line of sight is an asynchronous answer, re-asked every 700-4000 ms (+500 ms after a visible
    // answer). The ray is only cast when something reads the answer (the sense pass, the fire gates), and the answer is
    // cached until the re-check time. CHOSEN: the 700-4000 ms draw is uniform (the player/follower/activity/posture
    // selection of the interval is not specified); an unset host seam keeps the old "clear" default. The 150 x 80 degree
    // view cone is not applied: NPCs have no facing in this loop (AI-3, still open).
    const auto losNow = [&]() -> bool {
        if (!lineOfSight) return true;
        if (now >= s.losNextMs) {
            Vec3 a = s.pos, b = pl.pos;
            a.y += cfg_.losEyeHeight;
            b.y += cfg_.losEyeHeight;
            s.losCached = lineOfSight(a, b);
            ++stats_.losChecks;
            if (!s.losCached) ++stats_.losBlocked;
            s.losNextMs = now + 700 + static_cast<int64_t>(s.rng->next(3301)) + (s.losCached ? 500 : 0);
        }
        return s.losCached;
    };

    // ---- perception (Sec10): expiry, damage, the sense pass ------------------------------------------------------
    h.perception.objectGoneOrDown = [alive = pl.alive, ph = pl.handle](uint64_t hd) { return hd == ph && !alive; };
    h.perception.expireEntries(now);
    if (f.damageFromPlayer > 0 && pl.alive) {
        PerceptionState::DamageProducerInput in;
        in.attackerHandle = pl.handle;
        in.attackerPosition = pl.pos;
        in.damage = f.damageFromPlayer;
        in.maxHealth = f.maxHealth;
        in.victimPosition = s.pos;
        in.attackerAttitude = attitude;
        in.attackerIsPlayerClass = true;
        in.neverTurnOnPlayerFlag = f.neverTurnOnPlayer;
        // CHOSEN: the provocation meter's feed and threshold (0x004fa180 / 0x004fa220) are not specified, so it is never at
        // its threshold. It only matters at attitude 4+, where notice is skipped (list 8 instead); attitude < 4 passes the
        // notice gate without it.
        in.provocationAtThreshold = false;
        h.perception.recordDamage(in, now);
        h.perception.recordGunfire(pl.handle, pl.pos, PerceptionState::GunfireCase::ShotLandedClose, false, now);
    }
    if (!pl.alive) {
        h.perception.removeHandleFromAllLists(pl.handle);
    } else if (hostile && now >= s.nextSenseMs) {
        // Sense pass step 7: 650-800 ms, or 350-450 ms when the activity level is below 6 and the human is not a civilian.
        // Activity level 6 is "in combat" (0x00943a20 / 0x004fd720, not decoded): CHOSEN approximation = has a target.
        const bool nearCadence = combatant && !s.targetSet;
        const int64_t lo = nearCadence ? cfg_.senseNearMinMs : cfg_.senseMinMs;
        const int64_t hi = nearCadence ? cfg_.senseNearMaxMs : cfg_.senseMaxMs;
        s.nextSenseMs = now + lo + static_cast<int64_t>(s.rng->next(static_cast<size_t>(hi - lo + 1)));
        if (dist <= cfg_.viewRange && losNow()) {
            h.perception.see(pl.handle, pl.pos, 0, true, false, true, now);
        } else if (dist <= cfg_.proximityNoticeRange) {
            h.perception.noticeWithoutSight(pl.handle, pl.pos, 0, true, false, true, now);
            h.perception.clearFlags(plist::KnownHostiles, pl.handle, pflag::InView | pflag::LosValid);
        } else if (h.perception.hasEntry(plist::KnownHostiles, pl.handle)) {
            h.perception.clearFlags(plist::KnownHostiles, pl.handle, pflag::InView | pflag::LosValid);
        }
    }

    // AI-16 / spec-lua-api-behaviour F `on_detection`: the hook fires on a NEW list-0 entry for the player (a refresh does not;
    // the entry must be removed and created again to fire again). The host applies the registered-callback gate.
    {
        const bool hasEntry = h.perception.hasEntry(plist::KnownHostiles, pl.handle);
        if (hasEntry && !s.playerEntryPresent) step.detectedPlayer = true;
        s.playerEntryPresent = hasEntry;
    }

    // ---- civilians: the flee-or-cower reaction (Sec21.4) -----------------------------------------------------
    NpcThreat threat = NpcThreat::None;
    if (!combatant && pl.alive) {
        if (f.damageFromPlayer > 0)
            threat = NpcThreat::Attacked;
        else if (pl.firingOrAiming && dist <= cfg_.threatRange)
            threat = NpcThreat::Gunfire;
    }
    // 0x004eaf80: an attacker farther than 20 m (squared 400) is answered by the delayed event 67 (250-750 ms), which then
    // carries the reaction; nearer ones react at once. A later, nearer or stronger threat does not shorten a pending delay.
    if (threat != NpcThreat::None && DistanceSq(s.pos, pl.pos) > cfg_.distantThreatSq) {
        if (s.pendingThreatAtMs < 0) {
            s.pendingThreatAtMs = now + cfg_.distantThreatDelayMinMs +
                                  static_cast<int64_t>(s.rng->next(static_cast<size_t>(
                                      cfg_.distantThreatDelayMaxMs - cfg_.distantThreatDelayMinMs + 1)));
            s.pendingThreat = threat;
        } else if (threat == NpcThreat::Attacked) {
            s.pendingThreat = threat;
        }
        threat = NpcThreat::None;
    }
    if (s.pendingThreatAtMs >= 0 && now >= s.pendingThreatAtMs && combatant == false && pl.alive) {
        threat = s.pendingThreat;
        s.pendingThreatAtMs = -1;
        s.pendingThreat = NpcThreat::None;
    }
    const auto toIdle = [&] {
        h.mode.request(mode::Idle, now);
        h.mode.apply(now);
        ms.actionFlags |= ModeState::kActionFinished;
        ms.idleTimer.restart(now, 0);
        s.fleeActive = false;
    };
    if (threat != NpcThreat::None) {
        combat::FleeInputs fi;
        fi.cantCower = s.cantCower;
        fi.alwaysCower = s.alwaysCower;
        fi.fleeHealthPct = tables_->civilianFleeHealthPct;
        fi.healthFraction = f.maxHealth > 0 ? f.health / f.maxHealth : 1.0;
        fi.fleeEvents = tables_->civilianFleeEvents;
        fi.list15NonEmpty = !h.perception.entries(plist::LoudEvents).empty();
        fi.bullet = threat == NpcThreat::Attacked;
        if (combat::FleeRatherThanFight(fi)) {
            combat::ChooserInputs ci;
            ci.panicPending = ms.switchPending && ms.requestedMode == mode::Panic;
            ci.inCower = ms.currentMode == mode::Cower;
            ci.inFlee = ms.currentMode == mode::Flee;
            ci.cantFlee = s.cantFlee;
            ci.cantCower = s.cantCower;
            ci.alwaysCower = s.alwaysCower;
            const combat::FleeCower pick = combat::ChooseFleeOrCower(ci, rand01(s));
            if (pick == combat::FleeCower::Flee) {
                BeginFunctionInputs& bi = h.mode.beginInputs;
                ms.bit4a6_2_fleeVariant = true;
                bi.fleeCanApproachTarget = true;
                bi.fleeHasEscapePoint = true;
                s.fleeFrom = pl.pos;
                if (ms.currentMode != mode::Flee) {
                    h.mode.request(mode::Flee, now);
                    h.mode.apply(now);
                }
                s.fleeActive = ms.currentMode == mode::Flee;
            } else if (pick == combat::FleeCower::Cower) {
                BeginFunctionInputs& bi = h.mode.beginInputs;
                bi.cowerUsable = true;
                bi.targetReferenceResolves = true;
                bi.cowerCanApproachTarget = true;
                bi.helper0x00a00560 = true;  // CHOSEN: the begin's step-5 gate holds (its meaning is not decoded)
                if (ms.currentMode != mode::Cower) {
                    h.mode.request(mode::Cower, now);
                    h.mode.apply(now);
                }
                s.fleeActive = false;
                if (ms.currentMode == mode::Cower) s.cowerUntilMs = now + cfg_.cowerMs;
            }
        } else if (threat == NpcThreat::Attacked) {
            s.provoked = true;  // fights back: handled like a provoked NPC below
        }
    }

    // ---- target (Sec21.2 / Sec21.9): the headless recipe, labelled CHOSEN -----------------------------------------
    const PerceptionEntry* e0 = h.perception.find(plist::KnownHostiles, pl.handle);
    const bool fightsPlayer = hostile || (!combatant && s.provoked);
    if (!pl.alive) {
        s.targetSet = false;
    } else if (fightsPlayer && e0 && (e0->flags & pflag::ActivelyKnown)) {
        if (!s.targetSet || combat::TargetCopyDue(now, s.lastTargetCopyMs)) {
            s.targetSet = true;
            s.lastTargetCopyMs = now;
        }
    }
    h.world.targetHandleSet = s.targetSet;
    h.world.targetHandle = pl.handle;
    h.world.targetResolvesToUsableBaseTypeObject = s.targetSet;
    h.world.targetIsDownOrDead = !pl.alive;
    ms.ownerTargetHandle = s.targetSet ? pl.handle : 0;

    // ---- the world facts the goal tests and action routines read ---------------------------------------------------
    h.world.inVehicle = f.inVehicle;
    h.world.healthState = 0;
    h.world.isPlayerClass = false;
    h.world.isHumanTyped = true;
    h.world.hasHeldItem = combatant;
    h.world.heldItemIsRanged = combatant;
    h.world.targetHoldsRangedWeapon = pl.armed;
    h.world.weaponRangeA = combatant ? cfg_.weaponRangeA : 0.0;
    h.world.weaponRangeB = combatant ? cfg_.weaponRangeB : 1.5;
    h.world.personalityCombatAggression = h.behavior.combatAggression;
    h.world.combatAggressionIsTwo = h.behavior.combatAggression == 2;
    h.world.isAggressivePersonality = h.world.combatAggressionIsTwo;
    h.world.hasCoverObject = s.hasCover;
    h.world.coverObjectPosition = s.coverPos;
    h.mode.acceptance.inVehicle = f.inVehicle;
    h.mode.acceptance.healthState = 0;
    ActionWorldFacts& af = s.actionFacts;
    const bool inFireRange = combatant && !f.inVehicle && s.targetSet && pl.alive && dist <= cfg_.weaponRangeB && losNow();
    af.canFireNow = af.canFireWithGrace750 = af.canFireWithGrace1000 = inFireRange;
    af.coverApproachRadius = cfg_.coverApproachRadius;
    af.coverFireRadius = cfg_.coverFireRadius;
    af.atOwnCoverPoint = s.hasCover && HorizDist(s.pos, s.coverPos) <= cfg_.coverApproachRadius;
    af.coverEvaluatorApproves = s.hasCover;
    af.coverShieldsFromTarget = !h.coverStayDownTimer.expired(now);  // popped out once the stay-down deadline passed
    af.coverObjectFlag8cBit1 = true;                                 // CHOSEN: every node allows a pop-out
    af.flag985cd0 = true;
    af.pathfindSucceeded = true;  // no navmesh in this host: straight lines (CHOSEN)
    // No group leader / followed player (these NPCs are not followers): the leader position is the NPC itself, so the
    // leash-distance tests of follow formation / follow player / move regroup do not fire (CHOSEN; the leader facts
    // are only meaningful for a follower, which this host does not model).
    af.groupOrFollowLeaderPosition = s.pos;
    af.followPlayerRadius = 0;
    af.followMakeWayRadius = 0;
    {
        // Band point (Sec21.1 0x004df080): too close -> a + 0.1(b-a) from the target; too far -> a + 0.9(b-a); else here.
        const double a = h.world.weaponRangeA, b = h.world.weaponRangeB;
        const int band = combat::WeaponBand(dist * dist, a, b);
        if (band == 0 || dist < 1e-6) {
            af.precomputedPoint = s.pos;
        } else {
            const double od = combat::WeaponBandOutDistance(band, a, b);
            const Vec3 away = {(s.pos.x - pl.pos.x) / dist, 0.0, (s.pos.z - pl.pos.z) / dist};
            af.precomputedPoint = {pl.pos.x + away.x * od, s.pos.y, pl.pos.z + away.z * od};
        }
    }

    // ---- the decision step (Sec9.2) + apply, then the mode tick ---------------------------------------------------
    const bool inReactionMode = ms.currentMode == mode::Flee || ms.currentMode == mode::Cower;
    if (!inReactionMode) {
        h.actionRoutine = [this, &h, &s, now](int a, ActionBlock& b) {
            try {
                return registry_.call(a, h, s.actionFacts, b, now);
            } catch (const OpenStateError&) {
                ++openActionRefusals_[static_cast<size_t>(a)];  // OPEN in the spec: the routine refuses, nothing is invented
                return false;
            }
        };
        DecisionStep(h, now);
        ++decisionSteps_;
        h.mode.apply(now);
        const uint8_t ca = ms.currentAction;
        if (ca < action::kCount && kActionTable[ca].categoryMode != ms.currentMode &&
            (ms.actionFlags & ModeState::kActionFinished) == 0) {
            // the begin function declined: finish at once so the cool-down is armed and the next pass re-decides
            FinishAction(h, now, [&](int64_t lo, int64_t hi) {
                return lo + static_cast<int64_t>(s.rng->next(static_cast<size_t>(std::max<int64_t>(1, hi - lo + 1))));
            });
        }
    }
    if (ms.currentAction < action::kCount) s.lastActionSeen = ms.currentAction;
    const auto finish = [&] {
        FinishAction(h, now, [&](int64_t lo, int64_t hi) {
            return lo + static_cast<int64_t>(s.rng->next(static_cast<size_t>(std::max<int64_t>(1, hi - lo + 1))));
        });
    };
    const auto reevalDue = [&](int64_t everyMs) {
        if (now - s.lastReevalMs >= everyMs) {
            s.lastReevalMs = now;
            ms.actionFlags |= ModeState::kReEvaluateRequested;
        }
    };
    const auto fireTick = [&] {
        if (!cfg_.fireEnabled || !combatant || !pl.alive || !s.targetSet || dist > cfg_.weaponRangeB + 5.0 || !losNow()) return;
        const NpcFireModel& fm = cfg_.fire;
        if (s.burstLeft > 0) {
            if (now < s.nextShotMs) return;
            const double t = std::min(1.0, std::max(0.0, (dist - fm.closeRange) / std::max(1e-6, fm.farRange - fm.closeRange)));
            const double acc = fm.closeAccuracy + (fm.farAccuracy - fm.closeAccuracy) * t;
            NpcShot shot;
            shot.shooter = s.handle;
            shot.hit = rand01(s) < acc;
            shot.damage = shot.hit ? fm.damagePerHit : 0.0;
            shot.from = s.pos;
            shot.to = pl.pos;
            shots.push_back(shot);
            --s.burstLeft;
            s.nextShotMs = now + fm.shotGapMs;
            if (s.burstLeft == 0) s.nextBurstMs = now + fm.burstDelayMs;
        } else if (now >= s.nextBurstMs) {
            s.burstLeft = fm.burstSize;
            s.nextShotMs = now;
        }
    };

    switch (ms.currentMode) {
        case mode::Idle:
            reevalDue(cfg_.civilianDecisionMs);
            break;
        case mode::CombatMovement: {
            const int a = ms.currentAction;
            if (IsAdvanceLike(a)) {
                const double stand = h.world.weaponRangeA + 0.9 * (h.world.weaponRangeB - h.world.weaponRangeA);
                if (!pl.alive || !s.targetSet || dist <= stand) {
                    finish();
                } else {
                    StepToward(s.pos, pl.pos, cfg_.runSpeed, dt);
                    step.moved = true;
                    reevalDue(cfg_.reevaluateMs);
                }
            } else if (a == action::HoldPosition || a >= action::kCount) {
                reevalDue(cfg_.reevaluateMs);
            } else {
                const Vec3 dest = ms.committedBlock.destination;
                if (IsUnsetPosition(dest) || HorizDist(s.pos, dest) < 1.414) {
                    finish();
                } else {
                    StepToward(s.pos, dest, cfg_.runSpeed, dt);
                    step.moved = true;
                    reevalDue(cfg_.reevaluateMs);
                }
            }
            break;
        }
        case mode::FireSpray:
            fireTick();
            reevalDue(cfg_.reevaluateMs);
            break;
        case mode::CoverPopout:
            fireTick();
            reevalDue(cfg_.reevaluateMs);
            break;
        case mode::CoverStay: {
            if (!s.hasCover) {
                const std::optional<Vec3> node = findCover ? findCover(s.pos, pl.pos) : searchCover(s, f, pl, now);
                if (!node) {
                    // Sec21.3 search failure: the caller backs off a random 6-8 s.
                    h.actionRepeatTimers[action::CoverTakeDef].arm(now, 6000 + static_cast<int64_t>(s.rng->next(2001)));
                    finish();
                    break;
                }
                s.hasCover = true;
                s.coverPos = *node;
                s.coverEnteredMs = now;
                // 0x00541600 (Sec11.6): find-new / stay-down timers x U(0.8, 1.25); time-in-cover restarts.
                h.coverFindNewTimer.arm(now, static_cast<int64_t>(combat::CoverTimerMs(
                                                  static_cast<double>(h.behavior.findNewCoverTimeMs), rand01(s))));
                // Stay-down: 800 ms on the first entry while +0x315 bit 7 is clear (the function then sets it) and recent
                // damage (0x004fc8b0) is below 20%; otherwise stay_down_time x U(0.8, 1.25) (AI-6, Sec11.6 / TV2).
                const int64_t scaledStayDown = static_cast<int64_t>(
                    combat::CoverTimerMs(static_cast<double>(h.behavior.stayDownTimeMs), rand01(s)));
                const bool firstUndamaged = !s.coverEnteredBefore && h.perception.damageAccumulator(now) < 0.2;
                s.coverEnteredBefore = true;
                h.coverStayDownTimer.arm(now, firstUndamaged ? 800 : scaledStayDown);
                h.coverTimeInCoverTimer.restart(now);
            }
            if (HorizDist(s.pos, s.coverPos) > 0.5) {
                StepToward(s.pos, s.coverPos, cfg_.runSpeed, dt);
                step.moved = true;
            }
            if (h.coverTimeInCoverTimer.elapsedMs(now) > h.behavior.abandonCoverTimeMs) {
                releaseCover(s);
                s.hasCover = false;  // 0x0050a000: abandon_cover_time exceeded
                h.coverFindNewTimer.disarm();
                h.coverStayDownTimer.disarm();
                h.coverTimeInCoverTimer.disarm();
                finish();
                break;
            }
            reevalDue(cfg_.reevaluateMs);
            break;
        }
        case mode::Flee: {
            if (pl.alive) s.fleeFrom = pl.pos;  // the threat handle resolves to the LIVE position (Sec20.6.9(a); AI-9)
            const double d2 = DistanceSq(s.pos, s.fleeFrom);
            if (!combat::FleeFromStillActive(d2)) {  // beyond 30 m the flee ends (Sec21.4, FINAL)
                toIdle();
            } else {
                double dx = s.pos.x - s.fleeFrom.x, dz = s.pos.z - s.fleeFrom.z;
                double n = std::sqrt(dx * dx + dz * dz);
                if (n < 1e-6) { dx = 1.0; dz = 0.0; n = 1.0; }
                const double fleeV = s.female ? cfg_.fleeSpeedFemale : cfg_.fleeSpeed;
                s.pos.x += dx / n * fleeV * dt;
                s.pos.z += dz / n * fleeV * dt;
                step.moved = true;
            }
            break;
        }
        case mode::Cower:
            if (now >= s.cowerUntilMs) toIdle();
            break;
        default:
            reevalDue(cfg_.reevaluateMs);
            break;
    }

    step.pos = s.pos;
    step.mode = ms.currentMode;
    step.action = ms.currentAction;
    if (ms.currentAction < action::kCount) s.lastActionSeen = ms.currentAction;
    if (ms.currentMode != s.loggedMode || (ms.currentAction != s.loggedAction && ms.currentAction < action::kCount &&
                                          ms.currentMode != mode::Idle)) {
        NpcTransitionLog l;
        l.handle = s.handle;
        l.nowMs = now;
        l.fromMode = s.loggedMode;
        l.toMode = ms.currentMode;
        l.action = ms.currentAction < action::kCount ? ms.currentAction : s.lastActionSeen;
        l.goalReason = ms.committedReason;
        l.distToPlayer = dist;
        log_.push_back(l);
        s.loggedMode = ms.currentMode;
        s.loggedAction = ms.currentAction;
    }
}

int32_t NpcAiRuntime::modeOf(uint64_t handle) const {
    auto it = npcs_.find(handle);
    return it == npcs_.end() ? mode::kNone : it->second.ai->mode.state.currentMode;
}
int NpcAiRuntime::actionOf(uint64_t handle) const {
    auto it = npcs_.find(handle);
    return it == npcs_.end() ? ActionBlock::kNoAction : it->second.ai->mode.state.currentAction;
}
const NpcState* NpcAiRuntime::state(uint64_t handle) const {
    auto it = npcs_.find(handle);
    return it == npcs_.end() ? nullptr : &it->second;
}
std::vector<NpcTransitionLog> NpcAiRuntime::drainLog() {
    std::vector<NpcTransitionLog> out;
    out.swap(log_);
    return out;
}

}  // namespace sr3airuntime
