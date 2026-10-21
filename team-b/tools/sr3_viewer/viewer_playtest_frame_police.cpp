// Playtest frame step: police response to notoriety (feature/police-response).
//
// Layered on the existing notoriety recipe (notoriety_escalation.h, spec-ai-runtime.md Sec19.8) and the level-driven
// roadblocks (roadblock_notoriety_schedule.h, Sec20.6.10). Rules and constants live in sr3airuntime/police_response.h,
// which lists what the spec fixes (binary hostility Sec19.4, chase speed law / 10 m hand-off / siren Sec22.2, the
// "seen -> re-arm decay, engaging -> hold at level 1" coupling Sec19.8) and what is CHOSEN (unit counts and pacing per
// level, spawn rings, the car's crew, direct steering instead of the road-graph route planner, stand-down removal).
//
// What this step does each frame:
//   1. level = police alias track (the same source the HUD, populations and roadblocks read);
//   2. PoliceDispatcher decides whether a foot cop and/or a chase car spawns; level 0 stands every unit down;
//   3. on-foot cops run the real per-NPC AI loop (a viewer-owned sr3airuntime::NpcAiRuntime, Combatant kind,
//      hostile to the player while level >= 1) with the existing line-of-sight seam; its shots reach the player through
//      EngineState::applyCharacterDamage (the vitals path: Damage_Received_Mult, regen wait, death);
//   4. chase cars follow the Sec22.2 speed law with direct steering and hand off to their crew within 10 m;
//   5. cops that see the player re-arm the police track's first-decay timer and hold it at level >= 1 (Sec19.8).
//
// Runs in free roam and in mission frames alike, but spawns nothing at level 0, so a mission only gets police when
// its own script (or a crime) raises the police track.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

namespace {

constexpr double kChosenCarAccel = 6.0;          // m/s^2, the vehicle's a_b (+0xc58 is a per-vehicle value, not given)
constexpr double kChosenCarHalfLength = 2.3;     // m, look-ahead offset of the gap (Sec22.2 "own half-length")
constexpr double kChosenStandDownCarSpeed = 12.0;
constexpr double kChosenFootDropM = 150.0;       // an on-foot cop this far behind is removed (the player drove off; CHOSEN)
constexpr double kChosenSeenRangeM = 45.0;       // = NpcAiConfig::viewRange (the Sec21.1 default view cone length)
constexpr uint64_t kPoliceHandleBase = 100000;   // NPC AI handles (the player handle is 1)
constexpr const char* kPoliceCharPrefix = "police_";

double rand01(PlaytestSession::PoliceState& ps) {
    ps.rng = ps.rng * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<double>(ps.rng >> 11) / 9007199254740992.0;
}

double groundY(PlaytestSession& sess, double x, double z, double fallbackY) {
    if (!sess.sampleGround) return fallbackY;
    const auto g = sess.sampleGround(x, z, fallbackY + 2.0, static_cast<float>(fallbackY));
    return g.real() && std::fabs(static_cast<double>(g.y) - fallbackY) <= 25.0 ? static_cast<double>(g.y) : fallbackY;
}

void resolveModels(PlaytestSession& sess, PlaytestSession::PoliceUnit& u) {
    // Per-session state (NOT function-level statics: the registry that owns these models dies with the session).
    auto& tried = sess.police.modelsTried;
    auto& carModel = sess.police.carModel;
    auto& pedModel = sess.police.pedModel;
    if (!tried) {
        tried = true;
        if (sess.ambientModels_ && sess.ambientModels_->ready()) {
            std::string carKey, pedRow;
            carModel = sess.ambientModels_->policeCarModel(&carKey);
            pedModel = sess.ambientModels_->policePedModel(&pedRow);
            std::printf("police-trace: models car=%s ped=%s (model registry; placeholder draw when none)\n",
                        carModel ? carKey.c_str() : "none", pedModel ? pedRow.c_str() : "none");
        } else {
            std::printf("police-trace: models unavailable (no ambient model registry): placeholder draw\n");
        }
    }
    u.vmodel = carModel;
    u.pmodel = pedModel;
    u.modelResolved = true;
}

std::string playerCharacterName(sr3luahost::EngineState& es) { return es.hasCharacter("#PLAYER1#") ? "#PLAYER1#" : "player"; }

}  // namespace

void stepPolice(PlaytestSession& sess, PlaytestFrame& frm) {
    if (sess.noPoliceResponse || !sess.combatEs_p || !sess.notoriety_) return;
    auto& ps = sess.police;
    sr3luahost::EngineState& es = *sess.combatEs_p;
    const int64_t now = sess.nowMsAccum;
    const double dt = frm.dt;

    // ---- level: the police alias track (Sec19.4 binary hostility reads the sign only) ----
    int level = 0;
    {
        const auto& st = es.notorietyAliasTrackStates();
        const auto it = st.find(3);
        if (it != st.end()) level = std::max(0, std::min(5, static_cast<int>(std::floor(it->second.currentValue))));
    }
    const sr3airuntime::Vec3 pp = frm.trafficReferencePos;
    // Player velocity from the position change (a respawn warp is not a velocity).
    if (ps.haveLastPlayer && dt > 1e-6) {
        const double dx = pp.x - ps.lastPlayerPos.x, dz = pp.z - ps.lastPlayerPos.z;
        if (dx * dx + dz * dz < 25.0 * 25.0) ps.playerVel = {dx / dt, 0.0, dz / dt};
        else ps.playerVel = {};
    }
    ps.lastPlayerPos = pp;
    ps.haveLastPlayer = true;
    const bool playerDead = sess.vitals_ && sess.vitals_->dead();

    if (ps.units.empty() && level == 0) {
        ps.lastLevel = 0;
        ps.wasEngaged = false;
        return;  // idle: no cost in the common (no notoriety) case
    }
    if (level != ps.lastLevel) {
        std::printf("police-trace: frame %d t=%lldms police level %d -> %d units foot=%zu car=%zu\n", sess.frames,
                    static_cast<long long>(now), ps.lastLevel, level,
                    static_cast<size_t>(std::count_if(ps.units.begin(), ps.units.end(), [](const auto& u) { return !u.car; })),
                    static_cast<size_t>(std::count_if(ps.units.begin(), ps.units.end(), [](const auto& u) { return u.car; })));
        ps.lastLevel = level;
    }

    // ---- the on-foot AI runtime (lazy: needs the NPC AI tables and the LOS world) ----
    if (!ps.ai && sess.npcAiTables) {
        sr3airuntime::NpcAiConfig cfg;
        cfg.fireEnabled = !sess.npcAiNoFire;
        cfg.losEyeHeight = 1.6;
        cfg.coverShieldHeight = 0.9;
        ps.ai = std::make_unique<sr3airuntime::NpcAiRuntime>(sess.npcAiTables, cfg, /*seed=*/0x7C0B);
        if (!sess.npcLosWorld_ && !sess.loaded.empty()) sess.npcLosWorld_.emplace(sess.loaded);
        if (sess.npcLosWorld_) {
            auto* world = &*sess.npcLosWorld_;
            ps.ai->lineOfSight = [world](const sr3airuntime::Vec3& a, const sr3airuntime::Vec3& b) {
                return world->clear(a.x, a.y, a.z, b.x, b.y, b.z);
            };
        }
        std::printf("police-trace: on-foot cop AI loop ON (Combatant kind, fire %s, LOS %s)\n", cfg.fireEnabled ? "on" : "off",
                    sess.npcLosWorld_ ? "real prop collision" : "none (always clear)");
    } else if (!ps.ai && !sess.npcAiTables && ps.spawnedFoot == 0 && level > 0) {
        std::printf("police-trace: NPC AI tables unavailable (--no-npc-ai?): on-foot cops approach without firing\n");
    }

    // ---- dispatch ----
    int liveFoot = 0, liveCar = 0;
    for (const auto& u : ps.units)
        if (!u.standDown && !u.dead) (u.car ? liveCar : liveFoot)++;
    const sr3airuntime::PoliceDispatchDecision dec = ps.dispatcher.Update(now, level, liveFoot, liveCar);

    if (dec.disengage) {
        bool any = false;
        for (auto& u : ps.units)
            if (!u.standDown) {
                u.standDown = true;
                u.standDownAtMs = now;
                any = true;
            }
        if (any) {
            ++ps.disengages;
            std::printf("police-trace: frame %d t=%lldms DISENGAGE police level 0 -> %zu units stand down (Sec19.4 clear-to-zero: "
                        "hostile -> unfriendly, chasers return to ambient / leave)\n",
                        sess.frames, static_cast<long long>(now), ps.units.size());
        }
        ps.wasEngaged = false;
    } else {
        for (auto& u : ps.units)
            if (u.standDown) {
                u.standDown = false;  // the level rose again before the unit left: it re-engages
                std::printf("police-trace: frame %d unit id=%llu re-engages (level %d)\n", sess.frames,
                            static_cast<unsigned long long>(u.id), level);
            }
        ps.wasEngaged = true;
    }

    auto spawnFoot = [&](const sr3airuntime::Vec3& at, const char* why) {
        PlaytestSession::PoliceUnit u;
        u.id = ps.nextId++;
        u.car = false;
        u.pos = at;
        u.pos.y = groundY(sess, at.x, at.z, pp.y);
        u.spawnedMs = now;
        resolveModels(sess, u);
        if (ps.ai) {
            sr3airuntime::NpcSpawn sp;
            sp.handle = kPoliceHandleBase + u.id;
            sp.kind = sr3airuntime::NpcKind::Combatant;
            sp.pos = u.pos;
            ps.ai->addNpc(sp, now);
        }
        // The hit-scan weapon targets name police as characters in the engine state (the lethal sink kills by name).
        es.getOrCreateCharacter(kPoliceCharPrefix + std::to_string(u.id));
        ++ps.spawnedFoot;
        ++ps.spawnedByLevel[static_cast<size_t>(level)];
        std::printf("police-trace: frame %d t=%lldms SPAWN foot cop id=%llu level=%d (%s) pos=(%.1f,%.1f,%.1f) dist=%.1f m\n",
                    sess.frames, static_cast<long long>(now), static_cast<unsigned long long>(u.id), level, why, u.pos.x,
                    u.pos.y, u.pos.z, std::hypot(u.pos.x - pp.x, u.pos.z - pp.z));
        ps.units.push_back(std::move(u));
    };

    if (dec.spawnFoot) {
        const double ang = rand01(ps) * 6.283185307179586;
        spawnFoot(sr3airuntime::PoliceRingPoint(pp, ang, rand01(ps), sr3airuntime::kChosenFootSpawnMinM,
                                                sr3airuntime::kChosenFootSpawnMaxM),
                  "ring 75-90 m");
    }
    if (dec.spawnCar) {
        // A road point near the 135 m ring (Sec20.6.10(b) distance, reused): nearest lane point of a spawnable segment.
        bool placed = false;
        if (sess.playtestRoadGraph) {
            const auto& graph = *sess.playtestRoadGraph;
            for (int attempt = 0; attempt < 8 && !placed; ++attempt) {
                const double ang = rand01(ps) * 6.283185307179586;
                const sr3airuntime::Vec3 ring =
                    sr3airuntime::PoliceRingPoint(pp, ang, 0.0, sr3airuntime::kCarSpawnDistM, sr3airuntime::kCarSpawnDistM);
                double bestErr = 1e30;
                sr3airuntime::Vec3 best;
                for (uint32_t s : graph.SegmentsNear(ring.x, ring.z, 60.0)) {
                    if (s >= graph.segments.size() || (graph.segments[s].flags & sr3zone::kSpawnExcludeMask) != 0) continue;
                    sr3airuntime::Vec3 lp;
                    size_t np = 1;
                    sr3airuntime::ProjectOntoLane(graph, s, ring.x, ring.z, lp, np);
                    const double d = std::hypot(lp.x - pp.x, lp.z - pp.z);
                    const double err = std::fabs(d - sr3airuntime::kCarSpawnDistM);
                    if (d >= 100.0 && d <= 170.0 && err < bestErr) {
                        bestErr = err;
                        best = lp;
                    }
                }
                if (bestErr < 1e29) {
                    PlaytestSession::PoliceUnit u;
                    u.id = ps.nextId++;
                    u.car = true;
                    u.pos = best;
                    u.heading = std::atan2(pp.z - best.z, pp.x - best.x);
                    u.speed = 0.0;
                    u.spawnedMs = now;
                    resolveModels(sess, u);
                    ++ps.spawnedCar;
                    ++ps.spawnedByLevel[static_cast<size_t>(level)];
                    std::printf("police-trace: frame %d t=%lldms SPAWN chase car id=%llu level=%d pos=(%.1f,%.1f,%.1f) dist=%.1f m "
                                "(road lane point near the 135 m ring) siren=%d\n",
                                sess.frames, static_cast<long long>(now), static_cast<unsigned long long>(u.id), level,
                                u.pos.x, u.pos.y, u.pos.z, std::hypot(u.pos.x - pp.x, u.pos.z - pp.z),
                                sr3airuntime::ChaseSirenOn(5, false) ? 1 : 0);
                    ps.units.push_back(std::move(u));
                    placed = true;
                }
            }
        }
        if (!placed) {
            ++ps.spawnFailCar;
            if (ps.spawnFailCar <= 3)
                std::printf("police-trace: frame %d chase car spawn FAILED (no spawnable road segment on the 135 m ring%s); retry\n",
                            sess.frames, sess.playtestRoadGraph ? "" : ": no road graph loaded");
        }
    }

    // ---- chase cars (Sec22.2) ----
    std::vector<sr3airuntime::Vec3> crewSpawns;
    for (auto& u : ps.units) {
        if (!u.car) continue;
        const double dx = pp.x - u.pos.x, dz = pp.z - u.pos.z;
        const double dist = std::hypot(dx, dz);
        const double aB = kChosenCarAccel;
        if (u.standDown) {
            u.speed = std::min(kChosenStandDownCarSpeed, u.speed + aB * dt);  // keeps its heading and leaves
        } else if (sr3airuntime::ChaseHandoff(true, !playerDead, false, dx * dx + dz * dz)) {
            if (!u.handedOff) {
                u.handedOff = true;
                u.handoffAtMs = now;
                ++ps.handoffs;
                std::printf("police-trace: frame %d t=%lldms HANDOFF car id=%llu dist=%.1f m (< 10 m): car stops, crew of %d fights\n",
                            sess.frames, static_cast<long long>(now), static_cast<unsigned long long>(u.id), dist,
                            sr3airuntime::kChosenCrewSize);
            }
            u.speed = std::max(0.0, u.speed - aB * dt);
            if (!u.crewReleased && now - u.handoffAtMs >= sr3airuntime::kChosenCrewExitDelayMs) {
                u.crewReleased = true;
                for (int i = 0; i < sr3airuntime::kChosenCrewSize; ++i) {
                    const double side = (i % 2 == 0 ? 1.0 : -1.0) * 2.0;
                    crewSpawns.push_back({u.pos.x - std::sin(u.heading) * side, u.pos.y, u.pos.z + std::cos(u.heading) * side});
                }
                ++ps.crewReleased;
            }
        } else {
            u.handedOff = false;  // the hand-off is evaluated every tick (Sec22.2 step 4): out of range -> pursuit resumes
            const double vT = std::hypot(ps.playerVel.x, ps.playerVel.z);
            const double gap = dist - kChosenCarHalfLength;
            ps.minCarGap = std::min(ps.minCarGap, dist);
            const sr3airuntime::ChaseSpeedCommand cmd = sr3airuntime::ChaseCommandedSpeed(u.speed, vT, aB, gap, false);
            u.speed = sr3airuntime::ChaseIntegrateSpeed(u.speed, cmd, aB, dt);
            u.heading = sr3airuntime::ChaseSteerHeading(u.heading, u.speed, dx, dz, dt);
        }
        u.pos.x += std::cos(u.heading) * u.speed * dt;
        u.pos.z += std::sin(u.heading) * u.speed * dt;
        u.pos.y = groundY(sess, u.pos.x, u.pos.z, u.pos.y);
    }
    for (const auto& c : crewSpawns) spawnFoot(c, "crew left a handed-off car");

    // ---- on-foot cops: the Sec21.1 per-NPC loop ----
    double fireDamage = 0.0;
    if (ps.ai) {
        sr3airuntime::PlayerFacts pf;
        pf.handle = 1;
        pf.pos = pp;
        pf.alive = !playerDead;
        std::vector<sr3airuntime::NpcFacts> facts;
        for (const auto& u : ps.units) {
            if (u.car || u.dead) continue;
            sr3airuntime::NpcFacts f;
            f.handle = kPoliceHandleBase + u.id;
            f.pos = u.pos;
            f.alive = true;
            const bool hostile = !u.standDown && sr3airuntime::PoliceHostile(level);
            f.hostileToPlayer = hostile;
            f.attitudeToPlayer = hostile ? 0 : 1;  // Sec19.4: hostile (0) at level >= 1, unfriendly (1) at level 0
            facts.push_back(f);
        }
        std::vector<sr3airuntime::NpcStep> steps;
        std::vector<sr3airuntime::NpcShot> shots;
        ps.ai->tick(now, pf, facts, steps, shots);
        for (const auto& st : steps) {
            for (auto& u : ps.units) {
                if (u.car || kPoliceHandleBase + u.id != st.handle) continue;
                if (st.moved) {
                    u.pos.x = st.pos.x;
                    u.pos.z = st.pos.z;
                    u.pos.y = groundY(sess, u.pos.x, u.pos.z, u.pos.y);
                }
                break;
            }
        }
        for (const auto& sh : shots) {
            ++ps.shots;
            if (!sh.hit || sess.bustCuffing) continue;  // CHOSEN: the arresting cop holds fire while cuffing
            ++ps.hits;
            fireDamage += sh.damage;
            const std::string target = playerCharacterName(es);
            double before = 0.0, after = 0.0;
            try {
                if (es.hasCharacter(target) && es.characters().at(target).currentHitPoints.known())
                    before = es.characters().at(target).currentHitPoints.get();
                es.applyCharacterDamage(target, kPoliceCharPrefix + std::to_string(sh.shooter - kPoliceHandleBase),
                                        static_cast<float>(sh.damage), 0u);
                if (es.hasCharacter(target) && es.characters().at(target).currentHitPoints.known())
                    after = es.characters().at(target).currentHitPoints.get();
            } catch (const std::exception&) {
                // the player's hit points are an OPEN value in this host: no damage invented
            }
            ps.damageToPlayer += before - after;
            if (ps.hits <= 40)
                std::printf("police-trace: frame %d t=%lldms HIT cop id=%llu -> player raw damage %.1f (player hp %.0f -> %.0f)\n",
                            sess.frames, static_cast<long long>(now), static_cast<unsigned long long>(sh.shooter - kPoliceHandleBase),
                            sh.damage, before, after);
        }
        (void)ps.ai->drainLog();  // keep the transition log bounded (the viewer prints its own police-trace lines)
    } else {
        // No AI tables: cops walk straight at the player (Sec21.1 would also fire; unavailable here).
        for (auto& u : ps.units) {
            if (u.car || u.dead || u.standDown) continue;
            const double dx = pp.x - u.pos.x, dz = pp.z - u.pos.z, d = std::hypot(dx, dz);
            if (d > 6.0) {
                u.pos.x += dx / d * 5.0 * dt;
                u.pos.z += dz / d * 5.0 * dt;
                u.pos.y = groundY(sess, u.pos.x, u.pos.z, u.pos.y);
            }
        }
    }

    // ---- Sec18.10 arrest trigger (opt-in --enable-bust; the retail build never enters AI mode 4, see the spec's port rule) ----
    // Gates 1-9 via sr3player::BustGatesPass. CHOSEN stand-ins (Team A questions): the ragdoll gate (3) is treated as met for
    // an on-foot living player because free roam has no ragdoll/knock-down state; the approach point (0x004d32d0) is the
    // player's position. HP/maxHP <= Bust_HP_Pcnt (0.6) and the 5 m (25.0 squared) distance are the spec values.
    if (sess.bustEnabled && sess.vitals_ && !playerDead && !sess.bustCuffing && !sess.bustPending && !sess.vitals_->busted() &&
        !sess.inVehicle && level >= 1) {
        const std::string target = playerCharacterName(es);
        double hp = 0.0, maxHp = 0.0;
        try {
            if (es.hasCharacter(target) && es.characters().at(target).currentHitPoints.known()) {
                hp = es.characters().at(target).currentHitPoints.get();
                maxHp = es.drawOrGetMaxHitPoints(target);
            }
        } catch (const std::exception&) {
        }
        for (const auto& u : ps.units) {
            if (u.car || u.dead || u.standDown) continue;
            sr3player::BustGateInput g;
            g.copIsPolice = true;
            g.playerRagdolled = true;  // CHOSEN stand-in, see above
            g.hp = hp;
            g.maxHp = maxHp;
            const double dx = u.pos.x - pp.x, dy = u.pos.y - pp.y, dz = u.pos.z - pp.z;
            g.distSqToApproach = dx * dx + dy * dy + dz * dz;
            if (sess.debugBustRangeM > 0.0) g.distSqToApproach *= sr3player::kBustApproachDistSq / (sess.debugBustRangeM * sess.debugBustRangeM);  // DEBUG only
            if (sr3player::BustGatesPass(g)) {
                sess.bustPending = true;
                std::printf("police-trace: frame %d t=%lldms BUST trigger cop id=%llu dist=%.2f m player hp %.0f/%.0f (%.0f%% <= 60%%): "
                            "cop mode 4 Bust begins, player enters cuffing (Sec18.10)\n",
                            sess.frames, static_cast<long long>(now), static_cast<unsigned long long>(u.id), std::sqrt(g.distSqToApproach),
                            hp, maxHp, maxHp > 0.0 ? 100.0 * hp / maxHp : 0.0);
                break;
            }
        }
    }

    // ---- killed by the player's hit-scan (the lethal sink marks the named character dead) ----
    for (auto& u : ps.units) {
        if (u.car || u.dead) continue;
        const std::string name = kPoliceCharPrefix + std::to_string(u.id);
        if (es.hasCharacter(name) && es.characters().at(name).isDeadHighConfidence) {
            u.dead = true;
            u.deadAtMs = now;
            ++ps.killed;
            if (ps.ai) ps.ai->removeNpc(kPoliceHandleBase + u.id);
            std::printf("police-trace: frame %d t=%lldms cop id=%llu KILLED by the player\n", sess.frames,
                        static_cast<long long>(now), static_cast<unsigned long long>(u.id));
        }
    }

    // ---- Sec19.8: a cop that sees the player re-arms the first-decay timer and holds the track at level >= 1 ----
    bool seen = false;
    if (level >= 1 && !playerDead) {
        for (const auto& u : ps.units) {
            if (u.standDown || u.dead) continue;
            if (std::hypot(u.pos.x - pp.x, u.pos.z - pp.z) > kChosenSeenRangeM) continue;
            if (!u.car && sess.npcLosWorld_ && !sess.npcLosWorld_->clear(u.pos.x, u.pos.y + 1.6, u.pos.z, pp.x, pp.y + 1.6, pp.z))
                continue;
            seen = true;
            break;
        }
    }
    if (seen) {
        ++ps.seenTicks;
        sess.notoriety_->RearmFirstDecay(sr3airuntime::NotorietyTrack::kPolice, now);
        sess.notoriety_->SetHoldFlag(sr3airuntime::NotorietyTrack::kPolice);
    }

    // ---- removal: killed, stood down and far / timed out, left far behind ----
    for (auto it = ps.units.begin(); it != ps.units.end();) {
        const double d = std::hypot(it->pos.x - pp.x, it->pos.z - pp.z);
        bool remove = false;
        const char* why = "";
        if (it->dead && now - it->deadAtMs >= 3000) { remove = true; why = "dead"; }
        else if (it->standDown && sr3airuntime::PoliceShouldRemoveAfterStandDown(now - it->standDownAtMs, d)) { remove = true; why = "stood down"; }
        else if (!it->standDown && it->car && d > sr3airuntime::kChosenCarChaseKeepM) { remove = true; why = "chaser lost the target"; }
        else if (!it->standDown && !it->car && d > kChosenFootDropM) { remove = true; why = "left behind"; }
        if (remove) {
            if (!it->car) {
                if (ps.ai) ps.ai->removeNpc(kPoliceHandleBase + it->id);
            }
            ++ps.removed;
            std::printf("police-trace: frame %d unit id=%llu (%s) removed: %s dist=%.1f m\n", sess.frames,
                        static_cast<unsigned long long>(it->id), it->car ? "car" : "foot", why, d);
            it = ps.units.erase(it);
        } else {
            ++it;
        }
    }

    int foot = 0, car = 0;
    for (const auto& u : ps.units) (u.car ? car : foot)++;
    ps.maxFoot = std::max(ps.maxFoot, foot);
    ps.maxCar = std::max(ps.maxCar, car);
    if (sess.frames % 30 == 0) {
        double nearestCar = 1e30;
        for (const auto& u : ps.units)
            if (u.car) nearestCar = std::min(nearestCar, std::hypot(u.pos.x - pp.x, u.pos.z - pp.z));
        std::printf("police-trace: frame %d t=%lldms level=%d units foot=%d car=%d chasing_cars=%d nearest_car=%.1f m shots=%llu hits=%llu "
                    "damage_to_player=%.1f seen=%d player_speed=%.1f\n",
                    sess.frames, static_cast<long long>(now), level, foot, car,
                    static_cast<int>(std::count_if(ps.units.begin(), ps.units.end(),
                                                   [](const auto& u) { return u.car && !u.standDown && !u.handedOff; })),
                    nearestCar < 1e29 ? nearestCar : -1.0, static_cast<unsigned long long>(ps.shots),
                    static_cast<unsigned long long>(ps.hits), ps.damageToPlayer, seen ? 1 : 0, std::hypot(ps.playerVel.x, ps.playerVel.z));
    }
    (void)fireDamage;
}

void printPoliceTrace(const PlaytestSession& sess) {
    const auto& ps = sess.police;
    if (ps.spawnedFoot + ps.spawnedCar + ps.spawnFailCar == 0 && ps.disengages == 0) return;
    std::printf("police-trace: SUMMARY spawned foot=%llu car=%llu (car spawn failures %llu) by level [1..5]=%llu/%llu/%llu/%llu/%llu "
                "max live foot=%d car=%d handoffs=%llu crew released=%llu shots=%llu hits=%llu damage_to_player=%.1f killed=%llu "
                "disengages=%llu removed=%llu seen_ticks=%llu min_car_dist=%.1f m\n",
                static_cast<unsigned long long>(ps.spawnedFoot), static_cast<unsigned long long>(ps.spawnedCar),
                static_cast<unsigned long long>(ps.spawnFailCar), static_cast<unsigned long long>(ps.spawnedByLevel[1]),
                static_cast<unsigned long long>(ps.spawnedByLevel[2]), static_cast<unsigned long long>(ps.spawnedByLevel[3]),
                static_cast<unsigned long long>(ps.spawnedByLevel[4]), static_cast<unsigned long long>(ps.spawnedByLevel[5]),
                ps.maxFoot, ps.maxCar, static_cast<unsigned long long>(ps.handoffs), static_cast<unsigned long long>(ps.crewReleased),
                static_cast<unsigned long long>(ps.shots), static_cast<unsigned long long>(ps.hits), ps.damageToPlayer,
                static_cast<unsigned long long>(ps.killed), static_cast<unsigned long long>(ps.disengages),
                static_cast<unsigned long long>(ps.removed), static_cast<unsigned long long>(ps.seenTicks),
                ps.minCarGap < 1e29 ? ps.minCarGap : -1.0);
}

}  // namespace sr3viewer
