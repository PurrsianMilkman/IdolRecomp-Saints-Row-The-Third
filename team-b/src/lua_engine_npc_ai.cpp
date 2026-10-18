// EngineState::tickNpcAi - the mission host's per-NPC AI tick (spec-ai-runtime.md Sec21.1).
// See the declaration in engine_state.h and include/sr3airuntime/npc_ai_runtime.h for the CONFIRMED / CHOSEN split.
#include <algorithm>
#include <cmath>

#include "sr3airuntime/notoriety.h"
#include "sr3airuntime/npc_ai_runtime.h"
#include "sr3luahost/engine_state.h"

namespace sr3luahost {

namespace {

// "#PLAYER1#" (the live-bridged mission name) and "player" (the name the playtest weapon sink attributes hits to).
bool IsPlayerName(const std::string& n) { return n.rfind("#PLAYER", 0) == 0 || n == "player"; }

}  // namespace

// canonical team name -> id 0..7 (the table 0x02623a80); 9 for anything else.
int EngineState::teamIdOfCharacter(const CharacterState& c) {
    if (!c.teamName.empty()) {
        for (int id = 0; id <= 7; ++id)
            if (std::string(EngineState::canonicalTeamName(c.teamName)) == EngineState::teamNameForId(id)) return id;
    }
    return c.currentTeamCode >= 0 && c.currentTeamCode <= 7 ? c.currentTeamCode : 9;
}

// Design notes moved verbatim from EngineState::enableNpcAi's declaration in engine_state.h
// (refactor step 3a):
// --- Per-NPC AI loop (sr3airuntime::NpcAiRuntime; spec-ai-runtime.md Sec21.1) ------------------------------------
// Off until a host calls enableNpcAi(); then tickCharacterDeaths() runs tickNpcAi() right after the scripted-order
// mode tick (the mode tick of the per-human update). For every non-player character whose position resolves and
// that is not under a scripted move order: perception -> decision -> mode -> action (see npc_ai_runtime.h for the
// CONFIRMED vs CHOSEN split). Hostility toward the player is the team relation (CONFIRMED: matrix value 0, the
// matrix the team_make_* natives write) or the attack_player_on_sight force flag or an ai_add_enemy_target naming
// the player. Notoriety is not an AI input (Sec21.1 negative); its effect is the Sec19.4 team-matrix flip, which
// tickNpcAi applies on every notoriety track level change (level >= 1 -> relation(player team 0, heat-coded team)
// = hostile, level 0 -> unfriendly) before it reads the matrix. NPC fire is the CHOSEN stand-in of the runtime's
// NpcFireModel; hits go through applyDamageModelHit (the damage sink that feeds the death callbacks).
void EngineState::enableNpcAi(std::shared_ptr<const sr3airuntime::NpcAiTables> tables, const sr3airuntime::NpcAiConfig& cfg) {
    npcAi_ = std::make_shared<sr3airuntime::NpcAiRuntime>(std::move(tables), cfg);
    pushNpcAiCoverNodes();
}

void EngineState::pushNpcAiCoverNodes() {
    if (!npcAi_) return;
    std::vector<sr3airuntime::Vec3> pts;
    pts.reserve(npcAiCoverNodes_.size());
    for (const auto& p : npcAiCoverNodes_) pts.push_back({p[0], p[1], p[2]});
    npcAi_->setCoverNodes(std::move(pts));
}

void EngineState::tickNpcAi() {
    if (!npcAi_) return;
    ++npcAiStats_.ticks;
    // ---- the player: "#PLAYER1#" (the live-bridged name), position must be known --------------------------------
    std::string playerName;
    if (characters_.count("#PLAYER1#") && characters_.at("#PLAYER1#").worldPosition.known()) {
        playerName = "#PLAYER1#";
    } else {
        for (const auto& name : characterOrder_) {
            auto pit = characters_.find(name);
            if (IsPlayerName(name) && pit != characters_.end() && pit->second.worldPosition.known()) { playerName = name; break; }
        }
    }
    if (playerName.empty()) { ++npcAiStats_.skippedNoPlayerPosition; return; }
    CharacterState& player = getOrCreateCharacter(playerName);
    if (!player.worldPosition.known()) { ++npcAiStats_.skippedNoPlayerPosition; return; }
    const WorldPosition pp = player.worldPosition.get();

    // ---- Sec19.4: a notoriety track level change flips the team relation (player baseline team 0 <-> heat-coded
    // team): level >= 1 -> hostile, level 0 -> unfriendly. The host's notoriety state is the alias-track currentValue.
    for (int code = 0; code < 4; ++code) {
        auto it = notorietyAliasTrackStates_.find(code);
        const int level = it == notorietyAliasTrackStates_.end() ? 0 : static_cast<int>(std::floor(it->second.currentValue));
        if (level != npcAiLastTrackLevel_[static_cast<size_t>(code)]) {
            npcAiLastTrackLevel_[static_cast<size_t>(code)] = level;
            setTeamRelationIfInBounds(sr3airuntime::kPlayerBaselineTeam,
                                      sr3airuntime::kHeatCodeByTrack[static_cast<size_t>(code)],
                                      level >= 1 ? sr3airuntime::kTeamRelationHostile : sr3airuntime::kTeamRelationUnfriendly);
        }
    }

    const int64_t nowMs = static_cast<int64_t>(syncedActionNowSeconds() * 1000.0 + 0.5);
    sr3airuntime::PlayerFacts pf;
    pf.handle = 1;
    pf.pos = {pp.x, pp.y, pp.z};
    pf.alive = !characterReadsDead(player);
    pf.firingOrAiming = npcAiPlayerFiringThisFrame;
    npcAiPlayerFiringThisFrame = false;

    std::vector<sr3airuntime::NpcFacts> facts;
    std::vector<std::string> names;
    for (const auto& name : characterOrder_) {
        if (IsPlayerName(name)) continue;
        auto it = characters_.find(name);
        if (it == characters_.end()) continue;
        CharacterState& c = it->second;
        if (c.scriptedMoveActive) continue;  // a pending scripted move order owns the motion (tickScriptedOrders)
        // Position: the live value if the host wrote one, else the zone placement (read without caching, so a far-away
        // placement is not given a position it never had).
        WorldPosition wp{};
        if (c.worldPosition.known()) {
            wp = c.worldPosition.get();
        } else {
            const sr3zone::ObjectRecord* rec = resolveZoneObject(name);
            std::optional<sr3zone::Position3f> zp = rec ? sr3zone::ObjectPosition(*rec) : std::nullopt;
            if (!zp) continue;
            wp = WorldPosition{static_cast<double>(zp->x), static_cast<double>(zp->y), static_cast<double>(zp->z)};
        }
        // Only NPCs near the player run AI: a zone holds thousands of script_npc placements, but only the ones streamed
        // in around the player are live humans (CHOSEN radius, EngineState::npcAiActiveRadius; hysteresis 1.25x).
        {
            const double dxp = wp.x - pp.x, dzp = wp.z - pp.z;
            const double d2 = dxp * dxp + dzp * dzp;
            const double r = npcAiActiveRadius * (npcAiHandles_.count(name) ? 1.25 : 1.0);
            if (d2 > r * r) {
                auto th = npcAiHandles_.find(name);
                if (th != npcAiHandles_.end()) {  // left the active region: drop its AI state
                    npcAi_->removeNpc(th->second);
                    npcAiNames_.erase(th->second);
                    npcAiHandles_.erase(th);
                    npcAiLastHp_.erase(name);
                }
                continue;
            }
        }
        if (!c.worldPosition.known()) c.worldPosition.set(wp);  // the same cache get_dist fills
        if (c.hiddenFlag.known() && c.hiddenFlag.get()) continue;
        // A downed human (stance 6) does not act: the Dbno mode only waits (Sec21.10.1), so it counts like a dead one here.
        const bool dead = characterReadsDead(c) || (c.lifeState.known() && c.lifeState.get() == 6);
        const int teamId = teamIdOfCharacter(c);

        uint64_t handle;
        auto hit = npcAiHandles_.find(name);
        if (hit == npcAiHandles_.end()) {
            handle = npcAiNextHandle_++;
            npcAiHandles_[name] = handle;
            npcAiNames_[handle] = name;
            sr3airuntime::NpcSpawn sp;
            sp.handle = handle;
            sp.kind = teamId == 7 ? sr3airuntime::NpcKind::Civilian : sr3airuntime::NpcKind::Combatant;
            sp.pos = {wp.x, wp.y, wp.z};
            sp.alwaysCower = c.alwaysCowerFlag;
            npcAi_->addNpc(sp, nowMs);
        } else {
            handle = hit->second;
        }

        sr3airuntime::NpcFacts f;
        f.handle = handle;
        f.pos = {wp.x, wp.y, wp.z};
        f.alive = !dead;
        f.maxHealth = c.maxHitPoints.known() && c.maxHitPoints.get() > 0 ? static_cast<double>(c.maxHitPoints.get()) : 100.0;
        const int32_t hp = c.currentHitPoints.known() ? c.currentHitPoints.get() : static_cast<int32_t>(f.maxHealth);
        f.health = static_cast<double>(hp);
        {
            auto lh = npcAiLastHp_.find(name);
            if (lh != npcAiLastHp_.end() && hp < lh->second && IsPlayerName(c.lastAttackerName)) {
                f.damageFromPlayer = static_cast<double>(lh->second - hp);
                f.provoked = true;
            }
            npcAiLastHp_[name] = hp;
        }
        f.inVehicle = c.stateEnum.known() && c.stateEnum.get() == 3;
        f.ignoreAi = c.ignoreAI.known() && c.ignoreAI.get();
        // Attitude toward the player: the team matrix (0 = hostile; -1 = never written is NOT hostile), or the
        // attack_player_on_sight force flag, or an ai_add_enemy_target naming the player.
        bool hostile = false;
        const char* why = "";
        if (teamId >= 0 && teamId < kTeamRelationMatrixDim && teamRelation_[static_cast<size_t>(teamId)][0] == 0) {
            hostile = true;
            why = "team relation";
        }
        if (c.attackPlayerOnSightBit9.known() && c.attackPlayerOnSightBit9.get()) { hostile = true; why = "attack_player_on_sight"; }
        if (c.attackPlayerBitAc.known() && c.attackPlayerBitAc.get()) { hostile = true; why = "set_attack_player_flag"; }
        for (const auto& kv : c.enemyTargets)
            if (IsPlayerName(kv.first)) { hostile = true; why = "ai_add_enemy_target"; }
        if (hostile && !dead && !f.ignoreAi && npcAiLogSink && npcAiHostileLogged_.insert(name).second)
            npcAiLogSink("[npc-ai] npc=" + name + " team=" + EngineState::teamNameForId(teamId) +
                         " is hostile to the player (" + why + ")");
        f.hostileToPlayer = hostile;
        // Attitude toward the player (Sec10 0x004fe0f0): 0 when hostile, else the team relation (1 unfriendly ... 4 allied)
        // when it was ever written; -1 = none (the runtime derives 2). Force flag never_turn_on_player (+0x2b9 bit 6).
        if (hostile) {
            f.attitudeToPlayer = 0;
        } else if (teamId >= 0 && teamId < kTeamRelationMatrixDim && teamRelation_[static_cast<size_t>(teamId)][0] > 0) {
            f.attitudeToPlayer = std::min(4, static_cast<int>(teamRelation_[static_cast<size_t>(teamId)][0]));
        }
        {
            auto nt = neverTurnOnPlayerFlag_.find(name);
            f.neverTurnOnPlayer = nt != neverTurnOnPlayerFlag_.end() && nt->second;
        }
        if (hostile && (dead || f.ignoreAi)) {
            ++npcAiStats_.hostileSuppressedTicks;  // dead or ignore_ai: hostile on paper, but the AI cannot act
        } else if (hostile) {
            ++npcAiStats_.hostileNpcTicks;
            npcAiStats_.minHostileDistance = std::min(npcAiStats_.minHostileDistance, std::sqrt((wp.x - pp.x) * (wp.x - pp.x) + (wp.z - pp.z) * (wp.z - pp.z)));
        }
        ++npcAiStats_.teamNpcTicks[static_cast<size_t>(teamId >= 0 && teamId <= 9 ? teamId : 9)];
        facts.push_back(f);
        names.push_back(name);
    }

    std::vector<sr3airuntime::NpcStep> steps;
    std::vector<sr3airuntime::NpcShot> shots;
    if (npcAiLineOfSight) {
        npcAi_->lineOfSight = [this](const sr3airuntime::Vec3& a, const sr3airuntime::Vec3& b) {
            return npcAiLineOfSight(a.x, a.y, a.z, b.x, b.y, b.z);
        };
    } else {
        npcAi_->lineOfSight = nullptr;
    }
    npcAi_->tick(nowMs, pf, facts, steps, shots);
    {
        const auto& rs = npcAi_->stats();
        npcAiStats_.losChecks = rs.losChecks;
        npcAiStats_.losBlocked = rs.losBlocked;
        npcAiStats_.coverSearches = rs.coverSearches;
        npcAiStats_.coverFound = rs.coverFound;
        npcAiStats_.coverNoNode = rs.coverNoNode;
        npcAiStats_.coverNodes = npcAi_->coverNodeCount();
        npcAiStats_.coverNodesInBox = rs.coverNodesInBox;
        npcAiStats_.coverShieldTests = rs.coverShieldTests;
        npcAiStats_.coverNearestNodeXZ = rs.coverNearestNodeXZ;
        npcAiStats_.coverNearestNodeDy = rs.coverNearestNodeDy;
    }
    npcAiStats_.npcsTicked += steps.size();

    for (const auto& st : steps) {
        auto nit = npcAiNames_.find(st.handle);
        if (nit != npcAiNames_.end() && st.detectedPlayer) {
            // on_detection (spec-lua-api-behaviour F, FINAL): queued once when this NPC's AI first becomes aware of the
            // player as a hostile, with (detector_name, target_name); only when a callback is registered for it.
            auto dit = characters_.find(nit->second);
            if (dit != characters_.end() && !dit->second.onDetectionCallback.empty())
                fireEngineCallback(dit->second.onDetectionCallback,
                                   {CallbackArg::str(nit->second), CallbackArg::str(playerName)}, "on_detection");
        }
        if (nit == npcAiNames_.end() || !st.moved) continue;
        auto cit = characters_.find(nit->second);
        if (cit == characters_.end()) continue;
        const WorldPosition cur = cit->second.worldPosition.get();
        cit->second.worldPosition.set(WorldPosition{st.pos.x, cur.y, st.pos.z});
    }
    for (const auto& sh : shots) {
        ++npcAiStats_.shotsFired;
        if (!sh.hit) continue;
        ++npcAiStats_.shotsHit;
        auto nit = npcAiNames_.find(sh.shooter);
        if (nit == npcAiNames_.end()) continue;
        try {
            const DamageModelResult r = applyDamageModelHit(playerName, nit->second, static_cast<float>(sh.damage), 0u);
            if (r.applied) npcAiStats_.damageToPlayer += r.damage;
        } catch (const OpenStateError&) {
            ++npcAiStats_.damageRefused;  // the player's hit points are an OPEN value in this host: no damage invented
        }
    }
    if (npcAiLogSink) {
        for (; npcAiOpenReported_ < npcAi_->openErrors().size(); ++npcAiOpenReported_)
            npcAiLogSink("[npc-ai] OPEN item skipped an NPC tick: " + npcAi_->openErrors()[npcAiOpenReported_]);
        for (const auto& l : npcAi_->drainLog()) {
            auto nit = npcAiNames_.find(l.handle);
            char buf[256];
            std::snprintf(buf, sizeof(buf), "[npc-ai] t=%lldms npc=%s mode %d->%d action=%d goal=%d dist=%.1f",
                          static_cast<long long>(l.nowMs), nit == npcAiNames_.end() ? "?" : nit->second.c_str(), l.fromMode,
                          l.toMode, l.action, l.goalReason, l.distToPlayer);
            npcAiLogSink(buf);
        }
    }
}

}  // namespace sr3luahost
