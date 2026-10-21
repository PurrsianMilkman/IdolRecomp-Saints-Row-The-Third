// Playtest frame step: homies and followers (feature/homies).
//
// Rules and constants live in sr3airuntime/homies.h (spec-ai-runtime.md Sec24, the only source of engine facts); this
// file wires them into the free-roam / mission frame loop. What it does:
//   * roster: homies.xtbl rows with the Sec24.6 record status and the 1 game day cooldown (game clock = TOD fraction
//     plus a day counter, so --tod-seconds-per-day speeds it up);
//   * call-in (debug input --call-homie=NAME[@FRAME], standing in for the phone): dial -> busy test -> 1.0 s ring ->
//     talk -> hang-up (follower cap Sec24.3, drop one Sec24.4) -> spawn point search 4 x 3 (Sec24.5 step 4) -> the crew
//     joins the party AT SPAWN and drives up by car at the Sec24.5 step 6 speed -> arrival stand-in (15 m and stopped)
//     -> the crew gets out and follows on foot;
//   * following on foot (Sec24.7): 6 m follow radius, gait from the distance steps, 2 m / 1500 ms squadmate wait,
//     teleport rules; in vehicles (Sec24.8): followers within 30 m board the player's car (3 passenger seats, CHOSEN),
//     get out when the player does;
//   * combat (Sec24.9): followers run the Sec21.1 NpcAiRuntime loop against the hostile nearest the player (target
//     re-copied every 2000 ms); friendly fire meter; downed type 0 + hospital bill;
//   * leaving (Sec24.10): dismissal, 55 m / 20 s abandon (flagged followers), player death (the Sec18.6 respawn clears the
//     party list, which this step sees as an external dismissal), every exit starts the cooldown.
// Everything is idle (no cost, no output) until a call is requested, so mission frames are unchanged unless used.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

namespace {

namespace hm = sr3airuntime::homies;
using Member = PlaytestSession::HomieMember;
using Phase = Member::Phase;

constexpr uint64_t kHomieHandleBase = 200000;  // NPC AI handles of followers (the enemy "player" handle below is 2)
constexpr double kChosenCarAccel = 6.0, kChosenCarBrake = 8.0;  // m/s^2 (the vehicle's a_b is a per-vehicle value)
constexpr double kChosenStopDistM = 10.0;       // the drive-up brakes toward this distance (arrival stand-in is 15 m / 2 m/s)
constexpr double kChosenCarTopSpeed = 40.0;     // the vehicle top speed (0x00a6baa0) is a per-vehicle value: CHOSEN
constexpr int64_t kChosenTalkMs = hm::kConnectToSummonMs;  // FINAL: summon one update (1/30 s) after connect (Sec16a.2)
constexpr int64_t kRingMs = hm::kRingToConnectMs;          // FINAL: connect 1.033 s after the call starts (Sec16a.2)
constexpr int64_t kChosenDriveUpTimeoutMs = 120000;
// Follower max HP (Sec24.9, HIGH): rank Hit_Points of the character.xtbl row's Spawn_Info_Rank (spawn_info_ranks.xtbl),
// times Homie_Health_Bonus (global 0x012F5D08; base value 1.0 - the unlockable modifier stack is not tracked here).
// Special_Homie_Health_Bonus: which rows use it is not specified, so it is not applied. Missing data: the 100 stand-in.
std::string lowerStr(std::string s);
constexpr double kHomieHealthBonusBase = 1.0;
double followerMaxHp(PlaytestSession& sess, const std::string& characterRow) {
    auto& hs = sess.homies;
    if (!hs.rankHpLoaded) {
        hs.rankHpLoaded = true;
        try {
            if (sess.docOf) {
                std::unordered_map<std::string, double> rankHp;
                for (const auto& r : sr3tables_progression::ParseSpawnInfoRanksTable(sess.docOf("spawn_info_ranks.xtbl")))
                    if (r.name) rankHp[lowerStr(*r.name)] = static_cast<double>(r.hitPoints.value);
                const sr3xtbl::Document charDoc = sess.docOf("character.xtbl");
                if (const sr3xtbl::Node* ct = charDoc.table())
                    for (const sr3xtbl::Node* row : ct->children()) {
                        if (!sr3xtbl::NameEquals(row->name(), "Character")) continue;
                        const std::string* nm = sr3xtbl::ChildText(row, "Name");
                        const std::string* rk = sr3xtbl::ChildText(row, "Spawn_Info_Rank");
                        if (!nm || !rk) continue;
                        const auto it = rankHp.find(lowerStr(*rk));
                        if (it != rankHp.end()) hs.rankHpByCharacter[lowerStr(*nm)] = it->second;
                    }
                std::printf("homie-trace: follower HP table: %zu character rows joined to a spawn_info_ranks rank\n", hs.rankHpByCharacter.size());
            }
        } catch (const std::exception& ex) {
            std::printf("homie-trace: follower HP table unavailable (%s): the 100 HP stand-in applies\n", ex.what());
        }
    }
    const auto it = hs.rankHpByCharacter.find(lowerStr(characterRow));
    return hm::FollowerMaxHp(it != hs.rankHpByCharacter.end(), it != hs.rankHpByCharacter.end() ? it->second : 0.0,
                             kHomieHealthBonusBase);
}

// The player's car seat count (vehicle +0x824, writer untraced): the seats its vehicle_interaction_info row defines, else
// the hm::kFallbackSeatCount stand-in (CHOSEN derivation, see hm::SeatCountFromSeatData); free = count - 1 - occupied.
int playerCarSeatCount(const PlaytestSession& sess) {
    int resolved = 0;
    if (sess.demoVehicleInteractionInfoFound && sess.demoVehicleInteractionInfo_)
        for (size_t i = 0; i < 8; ++i)
            if (sr3tables_vehicle_world::FindSeatByResolverIndex(*sess.demoVehicleInteractionInfo_, i)) ++resolved;
    return hm::SeatCountFromSeatData(resolved);
}
constexpr double kBoardRangeM = 30.0;           // Sec24.8 action 37: a seat in the leader's vehicle within 30 m
constexpr double kEnemyRangeM = 60.0;           // CHOSEN: hostiles nearer than this to the player are the follower target
constexpr double kRegroupRangeM = 25.0;         // CHOSEN: beyond this from the player a follower ignores combat movement (Sec24.7: 17 regroup)
constexpr double kChosenParkedCarDropM = 150.0;
constexpr int64_t kChosenStandMs = 20000;       // a non-joining crew stands this long before it is removed (CHOSEN)
constexpr uint64_t kEnemyAiHandle = 2;
constexpr double kReviveRangeM = hm::kReviveRadiusM;  // 3.0 m prompt radius (Sec24.9, HIGH); the hold is 2.5 s

double rand01(PlaytestSession::HomieState& hs) {
    hs.rng = hs.rng * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<double>(hs.rng >> 11) / 9007199254740992.0;
}

double groundY(PlaytestSession& sess, double x, double z, double fallbackY) {
    if (!sess.sampleGround) return fallbackY;
    const auto g = sess.sampleGround(x, z, fallbackY + 2.0, static_cast<float>(fallbackY));
    return g.real() && std::fabs(static_cast<double>(g.y) - fallbackY) <= 25.0 ? static_cast<double>(g.y) : fallbackY;
}

std::string lowerStr(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string playerCharacterName(sr3luahost::EngineState& es) { return es.hasCharacter("#PLAYER1#") ? "#PLAYER1#" : "player"; }

double gameClockSec(const PlaytestSession& sess) {
    return (static_cast<double>(sess.homies.clockDays) + sess.todFraction) * hm::kDaySeconds;
}

std::string clockText(double sec) {
    const int day = static_cast<int>(std::floor(sec / hm::kDaySeconds));
    const int rem = static_cast<int>(sec - day * hm::kDaySeconds);
    char b[48];
    std::snprintf(b, sizeof(b), "day %d %02d:%02d:%02d", day, rem / 3600, (rem / 60) % 60, rem % 60);
    return b;
}

std::string rowName(const PlaytestSession::HomieRowState& r) { return r.row.name ? *r.row.name : std::string("?"); }

int findRow(const PlaytestSession::HomieState& hs, const std::string& name) {
    const std::string n = lowerStr(name);
    for (size_t i = 0; i < hs.rows.size(); ++i)
        if (lowerStr(rowName(hs.rows[i])) == n) return static_cast<int>(i);
    return -1;
}

// Sec24.6 FINAL: a row at status 0 turns 1 when its unlockable (type Homie) is set or homie_mission_unlock ran.
// Viewer stand-in for the unlockable byte: the respect-unlockable flags are not exposed by name here, so
// --homie-unlock=NAME|all (debug) plays the role of the unlockable being set.
static void refreshUnlocks(PlaytestSession& sess) {
    auto& hs = sess.homies;
    auto& es = *sess.combatEs_p;
    for (auto& rs : hs.rows) {
        if (rs.rec.status != hm::kLocked) continue;
        const std::string nm = rowName(rs);
        bool unlockable = false;
        for (const auto& u : sess.homieUnlock)
            if (u == "all" || lowerStr(u) == lowerStr(nm)) unlockable = true;
        const auto& ov = es.homieMissionOverride();
        const auto it = ov.find(nm);
        const bool ran = it != ov.end() && it->second == 2;
        rs.rec.status = hm::InitialStatus(unlockable, ran);
    }
}

void loadRoster(PlaytestSession& sess) {
    auto& hs = sess.homies;
    hs.loaded = true;
    try {
        if (!sess.docOf) throw std::runtime_error("no table reader");
        const sr3xtbl::Document doc = sess.docOf("homies.xtbl");
        const sr3tables_trafficai::HomiesTable t = sr3tables_trafficai::ParseHomiesTable(doc);
        for (const auto& r : t.rows) {
            PlaytestSession::HomieRowState rs;
            rs.row = r;
            // Sec24.6 FINAL: every row starts at 0; refreshUnlocks() lists it (1) once its unlock is set. DLC rows
            // (dlcN_homies.xtbl) are not loaded here.
            rs.rec.status = hm::kLocked;
            rs.rec.playerSelectsVehicle = r.flagPlayerSelectsVehicle;
            rs.rec.serviceRow = r.flagClearGangNotoriety || r.flagClearPoliceNotoriety;
            hs.rows.push_back(std::move(rs));
        }
        std::printf("homie-trace: roster loaded from homies.xtbl: %zu rows (base only), start at status 0 (listed when unlocked: "
                    "homie_mission_unlock or --homie-unlock=NAME|all)\n", hs.rows.size());
        for (const auto& rs : hs.rows) {
            const auto& r = rs.row;
            std::printf("homie-trace:   row %-22s humans=%zu vehicles=%zu flags:%s%s%s%s%s%s%s%s%s%s\n", rowName(rs).c_str(),
                        r.humans.size(), r.vehicles.size(), r.flagClearGangNotoriety ? " clear_gang" : "",
                        r.flagClearPoliceNotoriety ? " clear_police" : "", r.flagFormOwnSquad ? " own_squad" : "",
                        r.flagGiveRandomWeapons ? " rand_weapons" : "", r.flagPlayerSelectsVehicle ? " garage_pick" : "",
                        r.flagRepairPlayersVehicle ? " repair_vehicle" : "", r.flagTurnToPedAfterDriveup ? " turn_to_ped" : "",
                        r.flagEmergency ? " emergency" : "", r.flagTaxi ? " taxi" : "", r.flagRequiresSaints ? " requires_saints" : "");
        }
    } catch (const std::exception& ex) {
        hs.loadFailed = true;
        std::printf("homie-trace: homies.xtbl unavailable (%s): homies off this run\n", ex.what());
    }
}

// The leader's party (the local player's name lists in the engine state; the Sec18.6 respawn clears these).
std::vector<std::string>& partyList(sr3luahost::EngineState& es) { return es.party().membersByLeader[playerCharacterName(es)]; }

size_t partySize(sr3luahost::EngineState& es) {
    size_t n = 0;
    for (const auto& kv : es.party().membersByLeader)
        if (sr3luahost::EngineState::isLocalPlayerName(kv.first)) n += kv.second.size();
    return n;
}

bool inParty(sr3luahost::EngineState& es, const std::string& name) { return es.isFollowerOfLocalPlayer(name); }

void partyRemove(sr3luahost::EngineState& es, const std::string& name) {
    for (auto& kv : es.party().membersByLeader) {
        if (!sr3luahost::EngineState::isLocalPlayerName(kv.first)) continue;
        auto& v = kv.second;
        v.erase(std::remove(v.begin(), v.end(), name), v.end());
    }
}

Member* findMember(PlaytestSession::HomieState& hs, uint64_t id) {
    for (auto& m : hs.members)
        if (m.id == id) return &m;
    return nullptr;
}

}  // namespace

void stepHomies(PlaytestSession& sess, PlaytestFrame& frm) {
    if (sess.noHomies || !sess.combatEs_p) return;
    auto& hs = sess.homies;
    sr3luahost::EngineState& es = *sess.combatEs_p;

    // ---- game clock: TOD fraction + a day counter (cheap, runs every frame so the day count never misses a wrap) ----
    if (hs.lastTod >= 0.0 && sess.todFraction < hs.lastTod - 0.5) ++hs.clockDays;
    hs.lastTod = sess.todFraction;

    const bool wantCalls = !sess.homieCalls.empty();
    if (!hs.loaded && wantCalls) loadRoster(sess);
    if (!hs.loaded || hs.loadFailed) return;
    if (hs.firedCall.size() != sess.homieCalls.size()) hs.firedCall.assign(sess.homieCalls.size(), false);
    if (hs.firedDismissOne.size() != sess.homieDismissOneAt.size()) hs.firedDismissOne.assign(sess.homieDismissOneAt.size(), false);
    if (hs.firedDismissAll.size() != sess.homieDismissAllAt.size()) hs.firedDismissAll.assign(sess.homieDismissAllAt.size(), false);
    if (hs.firedFriendly.size() != sess.homieFriendlyFire.size()) hs.firedFriendly.assign(sess.homieFriendlyFire.size(), false);

    const int64_t now = sess.nowMsAccum;
    const double dt = frm.dt;
    if (dt <= 1e-9) return;  // paused
    const double clock = gameClockSec(sess);
    const sr3airuntime::Vec3 pp = frm.trafficReferencePos;
    const bool inVeh = sess.inVehicle;
    const bool playerDead = sess.vitals_ && sess.vitals_->dead();

    // ---- player kinematics ----
    if (hs.haveLastPlayer) {
        const double dx = pp.x - hs.lastPlayerPos.x, dz = pp.z - hs.lastPlayerPos.z;
        if (inVeh == hs.lastInVehicle && dx * dx + dz * dz < 25.0 * 25.0) hs.playerVel = {dx / dt, 0.0, dz / dt};
        else hs.playerVel = {};
    }
    hs.lastPlayerPos = pp;
    const double playerSpeed = std::hypot(hs.playerVel.x, hs.playerVel.z);
    const bool wasInVeh = hs.lastInVehicle;
    const bool firstFrame = !hs.haveLastPlayer;
    hs.haveLastPlayer = true;
    hs.lastInVehicle = inVeh;
    // CHOSEN player gait from speed (walk 1 / run 2 / sprint 3); the gait INDEX use is spec (Sec24.7), the thresholds are not.
    const int playerGait = playerSpeed < 0.3 ? 0 : (playerSpeed < 2.4 ? 1 : (playerSpeed < 5.2 ? 2 : 3));
    const sr3airuntime::Vec3 playerFwd = frm.trafficReferenceForward;
    const sr3airuntime::Vec3 carSideRight{-playerFwd.z, 0.0, playerFwd.x};

    auto removeMember = [&](size_t idx, const char* notice, const char* why, bool cooldown) {
        Member m = hs.members[idx];
        partyRemove(es, m.charName);
        if (hs.ai) hs.ai->removeNpc(m.aiHandle);
        auto& rec = hs.rows[static_cast<size_t>(m.rowIdx)].rec;
        if (cooldown) {
            hm::StartCooldown(rec, clock);
            std::printf("homie-trace: frame %d t=%lldms %s %s (%s): record %s -> status %d cooldown until %s\n", sess.frames,
                        static_cast<long long>(now), notice, m.charName.c_str(), why,
                        rowName(hs.rows[static_cast<size_t>(m.rowIdx)]).c_str(), rec.status, clockText(rec.cooldownStamp).c_str());
        } else {
            std::printf("homie-trace: frame %d t=%lldms %s %s (%s)\n", sess.frames, static_cast<long long>(now), notice,
                        m.charName.c_str(), why);
        }
        hs.members.erase(hs.members.begin() + static_cast<std::ptrdiff_t>(idx));
    };

    auto followerCount = [&]() { return static_cast<int>(partySize(es)); };
    auto currentCap = [&]() {
        hm::CapInputs ci;
        ci.recruitable = es.partyRecruitable() && !sess.homieNotRecruitable;  // global default is 1 (Sec24.3)
        ci.allowMaxFollowers = sess.homieAllowMax;
        ci.slotsUnlocked = std::max(0, std::min(3, sess.homieSlots));
        return hm::FollowerCap(ci);
    };

    // ---- party members dismissed from outside this step (player death Sec18.6 / party_dismiss_all) ----
    for (size_t i = 0; i < hs.members.size();) {
        if (hs.members[i].joined && !inParty(es, hs.members[i].charName)) {
            removeMember(i, "HOMIES_DISMISSED", "dismissed from outside (player death / script party_dismiss_all)", true);
            ++hs.dismissed;
        } else {
            ++i;
        }
    }

    // ---- scheduled debug inputs ----
    auto dismissOne = [&](size_t idx, const char* why) {
        removeMember(idx, "HOMIES_DISMISSED", why, true);
        ++hs.dismissed;
    };
    for (size_t k = 0; k < sess.homieDismissOneAt.size(); ++k) {
        if (hs.firedDismissOne[k] || sess.frames < sess.homieDismissOneAt[k]) continue;
        hs.firedDismissOne[k] = true;
        for (size_t i = 0; i < hs.members.size(); ++i)
            if (hs.members[i].joined) {  // Sec24.10 FINAL: the dismiss control (0x009e2bc0) drops the WHOLE party
                for (size_t j = i; j < hs.members.size();) {
                    if (hs.members[j].joined) dismissOne(j, "dismiss command (CBA_GAC_RECRUIT_DISMISS): whole party dismissed");
                    else ++j;
                }
                break;
            }
    }
    for (size_t k = 0; k < sess.homieDismissAllAt.size(); ++k) {
        if (hs.firedDismissAll[k] || sess.frames < sess.homieDismissAllAt[k]) continue;
        hs.firedDismissAll[k] = true;
        size_t n = 0;
        for (size_t i = 0; i < hs.members.size();) {
            if (hs.members[i].joined) {
                dismissOne(i, "party_dismiss_all");
                ++n;
            } else {
                ++i;
            }
        }
        es.recordPartyDismissAll();
        std::printf("homie-trace: frame %d party_dismiss_all: %zu followers dismissed\n", sess.frames, n);
    }
    if (sess.homieInjureAt >= 0 && !hs.firedInjure && sess.frames >= sess.homieInjureAt) {
        for (auto& m : hs.members)
            if (m.joined && !m.downed && !m.dead) {
                hs.firedInjure = true;
                m.hp = 0.0;
                m.downed = true;
                m.downedUntilMs = now + hm::DownedDurationMs(1.0);  // Homie_Revive_Timer_Mult 1.0 (normal difficulty, CHOSEN)
                m.reviveHoldMs = 0;
                hs.rows[static_cast<size_t>(m.rowIdx)].rec.status = hm::kDowned;
                ++hs.downedCount;
                std::printf("homie-trace: frame %d t=%lldms HOMIES_FALLEN \"%s is down!\" (debug --homie-injure-at): downed type 0 until "
                            "t=%lldms (30000 ms x 1.0), status 4 -> 5\n", sess.frames, static_cast<long long>(now),
                            m.charName.c_str(), static_cast<long long>(m.downedUntilMs));
                break;
            }
    }
    for (size_t k = 0; k < sess.homieFriendlyFire.size(); ++k) {
        if (hs.firedFriendly[k] || sess.frames < sess.homieFriendlyFire[k].frame) continue;
        hs.firedFriendly[k] = true;
        for (auto& m : hs.members)
            if (m.joined && !m.dead) {
                const bool over = m.ff.Hit(sess.homieFriendlyFire[k].damage, now);
                std::printf("homie-trace: frame %d t=%lldms friendly fire: player hit %s for %.0f -> meter sum %.0f (%s)\n", sess.frames,
                            static_cast<long long>(now), m.charName.c_str(), sess.homieFriendlyFire[k].damage, m.ff.sum,
                            over ? "HOSTILE" : "below 150");
                break;
            }
    }

    if (hs.firedWanted.size() != sess.homieWantedAt.size()) hs.firedWanted.assign(sess.homieWantedAt.size(), false);
    for (size_t k = 0; k < sess.homieWantedAt.size(); ++k) {
        if (hs.firedWanted[k] || sess.frames < sess.homieWantedAt[k].frame || !sess.notoriety_) continue;
        hs.firedWanted[k] = true;
        sess.notoriety_->SetLevel(sr3airuntime::NotorietyTrack::kPolice, sess.homieWantedAt[k].level, now);
        std::printf("homie-trace: frame %d t=%lldms (evidence harness) police track set to level %d\n", sess.frames,
                    static_cast<long long>(now), sess.homieWantedAt[k].level);
    }

    // ---- dial (debug input standing in for the phone) ----
    auto blockedByMission = [&](const sr3tables_trafficai::Homie& r) {
        if (sess.missionStem.empty()) return false;
        const std::string ms = lowerStr(sess.missionStem);
        for (const auto& b : r.blockedForMission)
            if (b && lowerStr(*b) == ms) return true;
        return false;
    };
    for (size_t k = 0; k < sess.homieCalls.size(); ++k) {
        if (hs.firedCall[k] || sess.frames < sess.homieCalls[k].frame) continue;
        hs.firedCall[k] = true;
        ++hs.calls;
        const int ri = findRow(hs, sess.homieCalls[k].name);
        if (ri < 0) {
            std::printf("homie-trace: frame %d dial '%s': no such homies.xtbl row\n", sess.frames, sess.homieCalls[k].name.c_str());
            continue;
        }
        auto& rs = hs.rows[static_cast<size_t>(ri)];
        for (auto& r : hs.rows) hm::ReleaseCooldown(r.rec, clock);  // the phone book build releases elapsed cooldowns
        refreshUnlocks(sess);
        const char* busyWhy = nullptr;
        if (playerDead) busyWhy = "player is dead";
        else if (hs.callPhase != 0) busyWhy = "a homie call is already in flight (0x022ad6cc)";
        else if (hm::PhoneStillOpen(now, hs.phoneCloseAtMs)) busyWhy = "the phone is still in state 5 (closes 3.033 s after the summon)";
        else if (blockedByMission(rs.row)) busyWhy = "Blocked_for_Mission: the active mission blocks this row";
        else if (!hm::Callable(rs.rec.status)) busyWhy = rs.rec.status == hm::kCooldown ? "cooldown (status 6)" : "status not callable";
        else {
            for (const auto& m : hs.members)
                if (m.rowIdx == ri && !m.dead) busyWhy = "already spawned and alive near the player";
        }
        std::printf("homie-trace: frame %d t=%lldms DIAL '%s' status=%d at %s\n", sess.frames, static_cast<long long>(now),
                    rowName(rs).c_str(), rs.rec.status, clockText(clock).c_str());
        if (busyWhy) {
            ++hs.busy;
            std::printf("homie-trace: frame %d t=%lldms BUSY '%s': %s (UI_Cell_Line_Busy, callback -1)\n", sess.frames,
                        static_cast<long long>(now), rowName(rs).c_str(), busyWhy);
            continue;
        }
        hs.callPhase = 1;
        hs.callRow = ri;
        hs.callPhaseStartMs = now;
        std::printf("homie-trace: frame %d t=%lldms RINGING '%s' (ring timer 1.0 s)\n", sess.frames, static_cast<long long>(now),
                    rowName(rs).c_str());
    }

    // ---- call state machine ----
    if (hs.callPhase == 1 && now - hs.callPhaseStartMs >= kRingMs) {
        hs.callPhase = 2;
        hs.callPhaseStartMs = now;
        std::printf("homie-trace: frame %d t=%lldms CONNECTED '%s' (summon in %lld ms)\n", sess.frames, static_cast<long long>(now),
                    rowName(hs.rows[static_cast<size_t>(hs.callRow)]).c_str(), static_cast<long long>(kChosenTalkMs));
    }
    if (hs.callPhase == 2 && now - hs.callPhaseStartMs >= kChosenTalkMs) {
        // ---- hang-up: cap check for EVERY row incl. service rows (spec-ai-runtime 24.5 step 1 / 24.11 step 2), then summon ----
        const int ri = hs.callRow;
        auto& rs = hs.rows[static_cast<size_t>(ri)];
        const auto& row = rs.row;
        bool go = true;
        hs.phoneCloseAtMs = now + hm::kSummonToPhoneCloseMs;  // state 5 timer 3.0 s: closes 91 updates (3.033 s) later (FINAL)
        {
            const int cap = currentCap();
            std::printf("homie-trace: frame %d hang-up: follower cap %d, current %d\n", sess.frames, cap, followerCount());
            if (followerCount() >= cap) {
                std::vector<hm::DismissCandidate> cands;
                std::vector<size_t> candIdx;
                for (size_t i = 0; i < hs.members.size(); ++i) {
                    if (!hs.members[i].joined) continue;
                    const Member& m = hs.members[i];
                    hm::DismissCandidate c;
                    c.hp = m.hp;
                    c.weaponCount = 1;
                    c.boundToHomieRecord = true;  // a named homie of the roster (+20)
                    cands.push_back(c);
                    candIdx.push_back(i);
                }
                const int pick = hm::PickMemberToDrop(cands, /*force=*/false);
                if (pick >= 0) {
                    const Member& m = hs.members[candIdx[static_cast<size_t>(pick)]];
                    std::printf("homie-trace: frame %d party full: dropping %s (score %.2f, lowest below 100; Sec24.4 force=0)\n", sess.frames,
                                m.charName.c_str(), hm::DismissScore(cands[static_cast<size_t>(pick)]));
                    removeMember(candIdx[static_cast<size_t>(pick)], "HOMIES_DISMISSED", "dropped to make room for a call", true);
                    ++hs.dropped;
                }
            }
            if (followerCount() >= currentCap()) {
                go = false;
                std::printf("homie-trace: frame %d t=%lldms party still full (cap %d, size %d): nobody droppable, NO SUMMON\n", sess.frames,
                            static_cast<long long>(now), currentCap(), followerCount());
            }
        }
        if (!go) {
            hs.callPhase = 0;
        } else if (row.flagClearGangNotoriety || row.flagClearPoliceNotoriety) {
            // Service row: clear notoriety (teams 0,1,2 / team 3), cooldown, end. Nobody joins.
            if (sess.notoriety_) {
                if (row.flagClearGangNotoriety)
                    for (int t = 0; t < 3; ++t) sess.notoriety_->SetLevel(static_cast<sr3airuntime::NotorietyTrack>(t), 0, now);
                if (row.flagClearPoliceNotoriety) sess.notoriety_->SetLevel(sr3airuntime::NotorietyTrack::kPolice, 0, now);
            }
            hm::StartCooldown(rs.rec, clock);
            std::printf("homie-trace: frame %d t=%lldms SERVICE '%s': cleared %s notoriety; cooldown until %s; nobody joins\n", sess.frames,
                        static_cast<long long>(now), rowName(rs).c_str(), row.flagClearGangNotoriety ? "gang" : "police",
                        clockText(rs.rec.cooldownStamp).c_str());
            hs.callPhase = 0;
        } else {
            // ---- crew spawn: status 2, vehicle pick, spawn point 4 x 3 ----
            rs.rec.status = hm::kCalled;
            std::string vehName = "(none)";
            if (!row.vehicles.empty()) {
                int total = 0;
                for (const auto& v : row.vehicles) total += std::max(0, v.weight.value);
                int roll = total > 0 ? static_cast<int>(rand01(hs) * total) : 0;
                size_t vi = 0;
                for (size_t i = 0; i < row.vehicles.size(); ++i) {
                    roll -= std::max(0, row.vehicles[i].weight.value);
                    vi = i;
                    if (roll < 0) break;
                }
                if (row.vehicles[vi].vehicle) vehName = *row.vehicles[vi].vehicle;
            }
            if (rs.rec.playerSelectsVehicle)
                std::printf("homie-trace: garage-pick row: no garage page in this viewer; the row's own first vehicle is used [CHOSEN]\n");
            const auto cands = hm::SpawnCandidates(pp, hs.playerVel, playerFwd, rand01(hs));
            bool placed = false;
            sr3airuntime::Vec3 spawnPos;
            int tried = 0;
            for (const auto& c : cands) {
                ++tried;
                sr3airuntime::Vec3 snapped = c.pos;
                bool onRoad = !sess.playtestRoadGraph;  // no graph loaded: the candidate itself (CHOSEN)
                if (sess.playtestRoadGraph) {
                    const auto& graph = *sess.playtestRoadGraph;
                    double bestD = 1e30;
                    for (uint32_t s : graph.SegmentsNear(c.pos.x, c.pos.z, 60.0)) {
                        if (s >= graph.segments.size() || (graph.segments[s].flags & sr3zone::kSpawnExcludeMask) != 0) continue;
                        sr3airuntime::Vec3 lp;
                        size_t np = 1;
                        sr3airuntime::ProjectOntoLane(graph, s, c.pos.x, c.pos.z, lp, np);
                        const double d = std::hypot(lp.x - c.pos.x, lp.z - c.pos.z);
                        if (d < bestD) {
                            bestD = d;
                            snapped = lp;
                            onRoad = true;
                        }
                    }
                }
                if (!onRoad) continue;                                        // the validator needs a road node
                if (!hm::FarEnoughFromPlayers(snapped, {pp})) continue;       // within 95 m of a player: rejected
                spawnPos = snapped;
                placed = true;
                std::printf("homie-trace: frame %d spawn point: candidate %d/12 accepted (dir %d, %.0f m) snapped to (%.1f,%.1f,%.1f), %.1f m from the player\n",
                            sess.frames, tried, c.direction, c.distance, spawnPos.x, spawnPos.y, spawnPos.z,
                            std::hypot(spawnPos.x - pp.x, spawnPos.z - pp.z));
                break;
            }
            if (!placed) {
                ++hs.spawnFails;
                rs.rec.status = hm::kAvailable;  // CHOSEN: a failed spawn leaves the record callable
                std::printf("homie-trace: frame %d t=%lldms SPAWN FAILED '%s': none of the 12 candidates valid (%d tried, %s)\n", sess.frames,
                            static_cast<long long>(now), rowName(rs).c_str(), tried,
                            sess.playtestRoadGraph ? "road graph present" : "no road graph loaded");
                hs.callPhase = 0;
            } else {
                ++hs.summons;
                hs.carPos = spawnPos;
                hs.carPos.y = groundY(sess, spawnPos.x, spawnPos.z, pp.y);
                hs.carHeading = std::atan2(pp.z - spawnPos.z, pp.x - spawnPos.x);
                hs.carSpeed = 0.0;
                hs.carTopSpeed = kChosenCarTopSpeed;
                hs.carName = vehName;
                hs.carModel = nullptr;
                std::string key;
                if (sess.ambientModels_ && sess.ambientModels_->ready() && vehName != "(none)")
                    hs.carModel = sess.ambientModels_->homieVehicleModel(vehName, &key);
                std::printf("homie-trace: frame %d t=%lldms SUMMON '%s' status 1 -> 2: vehicle %s (model %s) crew=%zu\n", sess.frames,
                            static_cast<long long>(now), rowName(rs).c_str(), vehName.c_str(), key.empty() ? "placeholder" : key.c_str(),
                            row.humans.size());
                const bool joins = !row.flagTurnToPedAfterDriveup && !row.flagFormOwnSquad;
                hs.callMembers.clear();
                for (size_t hi = 0; hi < row.humans.size() && hi < 8; ++hi) {
                    Member m;
                    m.id = hs.nextId++;
                    m.rowIdx = ri;
                    m.humanRow = row.humans[hi].character ? *row.humans[hi].character : std::string();
                    m.charName = "homie_" + lowerStr(rowName(rs)) + "_" + std::to_string(m.id);
                    m.pos = hs.carPos;
                    m.phase = Phase::InCar;
                    m.hp = m.maxHp = followerMaxHp(sess, m.humanRow);
                    m.canLeave = sess.homieAllCanLeave;
                    m.aiHandle = kHomieHandleBase + m.id;
                    if (sess.ambientModels_ && sess.ambientModels_->ready() && !m.humanRow.empty())
                        m.pmodel = sess.ambientModels_->homiePedModel(m.humanRow);
                    es.getOrCreateCharacter(m.charName);
                    if (joins) {
                        // Sec24.5 step 5: the member joins the party at once (the cap gate is skipped), still driving up.
                        partyList(es).push_back(m.charName);
                        m.joined = true;
                        rs.rec.status = hm::kSpawned;
                    }
                    std::printf("homie-trace: frame %d member %s '%s' %s (record status %d), model %s\n", sess.frames, m.charName.c_str(),
                                m.humanRow.c_str(), joins ? "JOINED the party at spawn (driving up)" : "spawned, does NOT join (turn to ped / own squad)",
                                rs.rec.status, m.pmodel ? "real" : "placeholder");
                    hs.callMembers.push_back(m.id);
                    hs.members.push_back(std::move(m));
                }
                hs.callPhase = 3;
                hs.callPhaseStartMs = now;
            }
        }
    }

    // ---- drive-up (Sec24.5 steps 6-8) ----
    if (hs.callPhase == 3) {
        const double dx = pp.x - hs.carPos.x, dz = pp.z - hs.carPos.z;
        const double dist = std::hypot(dx, dz);
        hm::ApproachInputs ai;
        ai.hasVehicle = true;
        ai.topSpeed = hs.carTopSpeed;
        ai.following = true;
        const double fx = std::cos(hs.carHeading), fz = std::sin(hs.carHeading);
        const double ux = dist > 1e-6 ? dx / dist : 0.0, uz = dist > 1e-6 ? dz / dist : 0.0;
        const bool ahead = ux * fx + uz * fz > 0.5;
        const double closing = playerSpeed > 0.5 ? (hs.playerVel.x * fx + hs.playerVel.z * fz) / playerSpeed : 0.0;
        ai.targetAheadNotHeadOn = ahead && closing > -0.7;  // "not closing head-on": CHOSEN threshold
        ai.targetInMovingVehicle = inVeh && playerSpeed > 0.5;
        ai.targetVehicleSpeed = playerSpeed;
        const double vApp = hm::ApproachSpeed(ai);
        // Brake in toward the arrival box (CHOSEN profile): the speed from which kChosenCarBrake still stops short of 12 m.
        const bool playerCarMoving = inVeh && playerSpeed > 0.5;
        const double vCmd = std::min(vApp, (playerCarMoving ? playerSpeed : 0.0) +
                                               std::sqrt(2.0 * 0.8 * kChosenCarBrake * std::max(0.0, dist - kChosenStopDistM)));
        if (hs.carSpeed < vCmd) hs.carSpeed = std::min(vCmd, hs.carSpeed + kChosenCarAccel * dt);
        else hs.carSpeed = std::max(vCmd, hs.carSpeed - kChosenCarBrake * dt);
        hs.carHeading = sr3airuntime::ChaseSteerHeading(hs.carHeading, std::max(hs.carSpeed, 1.0), dx, dz, dt);
        hs.carPos.x += std::cos(hs.carHeading) * hs.carSpeed * dt;
        hs.carPos.z += std::sin(hs.carHeading) * hs.carSpeed * dt;
        hs.carPos.y = groundY(sess, hs.carPos.x, hs.carPos.z, hs.carPos.y);
        for (uint64_t id : hs.callMembers)
            if (Member* m = findMember(hs, id)) m->pos = hs.carPos;
        if (sess.frames % 30 == 0)
            std::printf("homie-trace: frame %d t=%lldms DRIVING UP dist=%.1f m car speed %.1f (approach law %.1f, player speed %.1f in_vehicle=%d)\n",
                        sess.frames, static_cast<long long>(now), dist, hs.carSpeed, vApp, playerSpeed, inVeh ? 1 : 0);
        const bool timeout = now - hs.callPhaseStartMs >= kChosenDriveUpTimeoutMs;
        if (hs.carSpeed >= hm::kArrivalSpeedMps) hs.carStarted = true;
        // Sec24.5 step 8 FINAL: arrival = body speed < 1.0 m/s STRICT; the car must have set off first (the route is
        // driven and stops by the Sec22.3 rule; stop point here = the CHOSEN braking profile above).
        if ((hs.carStarted && hm::DriveUpArrived(hs.carSpeed)) || timeout) {
            ++hs.arrivals;
            auto& rs = hs.rows[static_cast<size_t>(hs.callRow)];
            std::printf("homie-trace: frame %d t=%lldms ARRIVED '%s' dist=%.1f m speed=%.1f%s: crew leaves the car, request ends (state 5)\n",
                        sess.frames, static_cast<long long>(now), rowName(rs).c_str(), dist, hs.carSpeed,
                        timeout ? " (drive-up timeout, CHOSEN)" : "");
            int slot = 0;
            for (uint64_t id : hs.callMembers) {
                Member* m = findMember(hs, id);
                if (!m) continue;
                const double side = (slot % 2 == 0 ? 1.0 : -1.0) * (2.2 + 0.6 * (slot / 2));
                m->pos = {hs.carPos.x - std::sin(hs.carHeading) * side, hs.carPos.y, hs.carPos.z + std::cos(hs.carHeading) * side};
                m->heading = hs.carHeading;
                if (m->joined) {
                    m->phase = Phase::OnFoot;
                    rs.rec.status = hm::kFollowing;
                    // Sec24.5 note FINAL: no boarding from the homie car; the crew is released on foot (a player in a car
                    // is handled by the enter-vehicle step below on the player's next entry).
                } else {
                    m->phase = Phase::Stand;
                    m->standUntilMs = now + kChosenStandMs;
                }
                ++slot;
            }
            if (rs.row.flagRepairPlayersVehicle)
                std::printf("homie-trace: frame %d row has `repair players vehicle`: NOT MODELLED (no vehicle damage state is repaired here)\n",
                            sess.frames);
            PlaytestSession::HomieState::ParkedCar pc;
            pc.pos = hs.carPos;
            pc.heading = hs.carHeading;
            pc.model = hs.carModel;
            pc.sinceMs = now;
            hs.parkedCars.push_back(pc);
            hs.callPhase = 0;
            hs.callMembers.clear();
        }
    }

    // ---- player enters / exits a vehicle ----
    if (!firstFrame && inVeh && !wasInVeh) {
        std::vector<std::pair<double, size_t>> order;
        for (size_t i = 0; i < hs.members.size(); ++i) {
            const Member& m = hs.members[i];
            if (m.joined && m.phase == Phase::OnFoot && !m.downed)
                order.push_back({std::hypot(m.pos.x - pp.x, m.pos.z - pp.z), i});
        }
        std::sort(order.begin(), order.end());
        int seats = 0;
        for (const auto& o : order) {
            Member& m = hs.members[o.second];
            if (o.first <= kBoardRangeM && seats < hm::FreeSeats(playerCarSeatCount(sess), 0)) {
                m.phase = Phase::Boarding;
                m.seat = seats++;
                std::printf("homie-trace: frame %d t=%lldms player entered a vehicle: %s (%.1f m) follows to car, seat %d (action 37, closest first)\n",
                            sess.frames, static_cast<long long>(now), m.charName.c_str(), o.first, m.seat);
            } else if (o.first <= kBoardRangeM) {
                std::printf("homie-trace: frame %d player entered a vehicle: %s has no free seat; 38/39 (other car / carjack) NOT MODELLED, follows on foot\n",
                            sess.frames, m.charName.c_str());
            } else {
                std::printf("homie-trace: frame %d player entered a vehicle: %s is %.1f m away (> 30 m): stays on foot\n", sess.frames,
                            m.charName.c_str(), o.first);
            }
        }
    }
    if (!firstFrame && !inVeh && wasInVeh) {
        int slot = 0;
        for (auto& m : hs.members) {
            if (m.phase != Phase::Seated && m.phase != Phase::Boarding) continue;
            const double side = (slot % 2 == 0 ? 1.0 : -1.0) * (2.6 + 0.8 * (slot / 2));
            m.pos = {pp.x + carSideRight.x * side, pp.y, pp.z + carSideRight.z * side};
            m.phase = Phase::OnFoot;
            m.seat = -1;
            ++hs.exited;
            ++slot;
            std::printf("homie-trace: frame %d t=%lldms player left the vehicle: %s gets out (follower_remain_in_car not set)\n", sess.frames,
                        static_cast<long long>(now), m.charName.c_str());
        }
    }

    // ---- combat target (Sec24.9: copy every 2000 ms) ----
    bool anyFollowerOnFoot = false;
    for (const auto& m : hs.members)
        if (m.joined && m.phase == Phase::OnFoot && !m.downed && !m.dead) anyFollowerOnFoot = true;
    PlaytestSession::PoliceUnit* enemy = nullptr;
    if (anyFollowerOnFoot) {
        auto isLive = [&](const PlaytestSession::PoliceUnit& u) { return !u.car && !u.dead && !u.standDown; };
        if (hs.enemyHandle != 0) {
            for (auto& u : sess.police.units)
                if (isLive(u) && 300000 + u.id == hs.enemyHandle) enemy = &u;
            if (!enemy) hs.enemyHandle = 0;
        }
        if (!enemy && now >= hs.nextTargetCopyMs) {
            hs.nextTargetCopyMs = now + hm::kTargetCopyMs;
            double best = kEnemyRangeM;
            for (auto& u : sess.police.units) {
                if (!isLive(u)) continue;
                const double d = std::hypot(u.pos.x - pp.x, u.pos.z - pp.z);
                if (d < best) {
                    best = d;
                    enemy = &u;
                }
            }
            if (enemy) {
                hs.enemyHandle = 300000 + enemy->id;
                std::printf("homie-trace: frame %d t=%lldms followers acquire target: police unit %llu at %.1f m from the player\n", sess.frames,
                            static_cast<long long>(now), static_cast<unsigned long long>(enemy->id), best);
            }
        }
    }

    // ---- followers ----
    std::vector<size_t> toRemoveAbandon, toRemoveAngry, toRemoveDead;
    size_t outOfRange = 0;
    std::vector<sr3airuntime::NpcFacts> facts;
    for (size_t i = 0; i < hs.members.size(); ++i) {
        Member& m = hs.members[i];
        if (m.dead) continue;
        if (m.phase == Phase::InCar) continue;
        const double dx = pp.x - m.pos.x, dz = pp.z - m.pos.z;
        const double dist = std::hypot(dx, dz);
        if (m.phase == Phase::Stand) {  // does not join: stands, removed after a while
            if (now >= m.standUntilMs) toRemoveDead.push_back(i);
            continue;
        }
        if (!m.joined) continue;
        m.ff.Tick(now);
        if (m.ff.Hostile(now)) {
            toRemoveAngry.push_back(i);
            continue;
        }
        // downed type 0
        if (m.downed) {
            const bool nearBy = !inVeh && dist <= kReviveRangeM;
            m.reviveHoldMs = nearBy ? m.reviveHoldMs + static_cast<int64_t>(dt * 1000.0 + 0.5) : 0;
            if (nearBy) m.downedUntilMs += static_cast<int64_t>(dt * 1000.0 + 0.5);  // a held revive pauses the bleed-out timer (Sec21.10.1)
            if (m.reviveHoldMs >= hm::kReviveHoldMs) {
                m.downed = false;
                m.hp = m.maxHp * 0.5;
                hs.rows[static_cast<size_t>(m.rowIdx)].rec.status = hm::kFollowing;
                ++hs.revived;
                std::printf("homie-trace: frame %d t=%lldms %s REVIVED by the player (2.5 s hold within %.1f m), hp %.0f, status 5 -> 4\n", sess.frames,
                            static_cast<long long>(now), m.charName.c_str(), kReviveRangeM, m.hp);
            } else if (now >= m.downedUntilMs) {
                m.dead = true;
                toRemoveDead.push_back(i);
            }
            continue;
        }
        // abandon (only can_leave_behind followers; the radius is x1.2 when the player is in a vehicle the follower is not in)
        const bool playerInVehFollowerNot = inVeh && m.phase != Phase::Seated;
        const bool wasOut = m.ab.OutOfRange();
        if (m.ab.Update(now, dist, playerInVehFollowerNot, m.canLeave)) {
            toRemoveAbandon.push_back(i);
            continue;
        }
        if (m.ab.OutOfRange()) {
            ++outOfRange;
            if (!wasOut)
                std::printf("homie-trace: frame %d t=%lldms %s out of range (%.1f m > %.1f m): abandon timer 20000 ms and marker started\n", sess.frames,
                            static_cast<long long>(now), m.charName.c_str(), dist, hm::AbandonRadius(playerInVehFollowerNot));
        } else if (wasOut) {
            std::printf("homie-trace: frame %d t=%lldms %s back in range: abandon timer and marker cancelled\n", sess.frames,
                        static_cast<long long>(now), m.charName.c_str());
        }
        if (m.phase == Phase::Seated) {
            m.pos = pp;
            continue;
        }
        // teleport rules
        const bool tpRule = hm::FollowTeleportApplies(dist, pp.y - m.pos.y, inVeh);
        if (tpRule) {  // the unstreamed-follower rule is streaming state, not distance: not modelled (no distance stand-in)
            const double ang = rand01(hs) * 6.283185307179586;
            m.pos = {pp.x + std::cos(ang) * 3.0, pp.y, pp.z + std::sin(ang) * 3.0};
            ++hs.teleports;
            std::printf("homie-trace: frame %d t=%lldms %s TELEPORTED to the player (%s, was %.1f m)\n", sess.frames, static_cast<long long>(now),
                        m.charName.c_str(), "follow teleport 100/150 m + height", dist);
            continue;
        }
        // boarding the player's car
        if (m.phase == Phase::Boarding) {
            if (!inVeh) {
                m.phase = Phase::OnFoot;
            } else if (dist <= 2.5) {
                m.phase = Phase::Seated;
                ++hs.boarded;
                std::printf("homie-trace: frame %d t=%lldms %s BOARDED the player's car, seat %d\n", sess.frames, static_cast<long long>(now),
                            m.charName.c_str(), m.seat);
                continue;
            } else if (playerSpeed > 10.0) {
                m.phase = Phase::OnFoot;
                std::printf("homie-trace: frame %d %s gave up boarding (car moving %.1f m/s), follows on foot\n", sess.frames, m.charName.c_str(),
                            playerSpeed);
            } else {
                const double step = hm::GaitSpeed(3) * dt;
                m.pos.x += dx / dist * std::min(step, dist);
                m.pos.z += dz / dist * std::min(step, dist);
                m.pos.y = groundY(sess, m.pos.x, m.pos.z, m.pos.y);
                continue;
            }
        }
        // on foot: combat fact, then follow movement
        sr3airuntime::NpcFacts f;
        f.handle = m.aiHandle;
        f.pos = m.pos;
        f.health = m.hp;
        f.maxHealth = m.maxHp;
        f.alive = true;
        f.hostileToPlayer = enemy != nullptr;
        f.attitudeToPlayer = enemy ? 0 : 4;
        facts.push_back(f);
    }

    // ---- followers' combat loop (Sec21.1 NpcAiRuntime; its "player" is the enemy the followers target) ----
    std::vector<sr3airuntime::NpcStep> steps;
    std::vector<sr3airuntime::NpcShot> shots;
    if (!hs.ai && sess.npcAiTables && anyFollowerOnFoot) {
        sr3airuntime::NpcAiConfig cfg;
        cfg.fireEnabled = !sess.npcAiNoFire;
        cfg.losEyeHeight = 1.6;
        cfg.coverShieldHeight = 0.9;
        hs.ai = std::make_unique<sr3airuntime::NpcAiRuntime>(sess.npcAiTables, cfg, /*seed=*/0x4801E5);
        if (!sess.npcLosWorld_ && !sess.loaded.empty()) sess.npcLosWorld_.emplace(sess.loaded);
        if (sess.npcLosWorld_) {
            auto* world = &*sess.npcLosWorld_;
            hs.ai->lineOfSight = [world](const sr3airuntime::Vec3& a, const sr3airuntime::Vec3& b) {
                return world->clear(a.x, a.y, a.z, b.x, b.y, b.z);
            };
        }
        std::printf("homie-trace: follower combat AI loop ON (Combatant kind, fire %s, LOS %s)\n", cfg.fireEnabled ? "on" : "off",
                    sess.npcLosWorld_ ? "real prop collision" : "none (always clear)");
    }
    if (hs.ai) {
        for (const auto& f : facts)
            if (!hs.ai->has(f.handle)) {
                sr3airuntime::NpcSpawn sp;
                sp.handle = f.handle;
                sp.kind = sr3airuntime::NpcKind::Combatant;
                sp.pos = f.pos;
                hs.ai->addNpc(sp, now);
            }
        sr3airuntime::PlayerFacts pf;
        pf.handle = kEnemyAiHandle;
        pf.pos = enemy ? enemy->pos : pp;
        pf.alive = enemy != nullptr;
        hs.ai->tick(now, pf, facts, steps, shots);
        (void)hs.ai->drainLog();
    }
    std::vector<uint64_t> engagedHandles;
    for (const auto& st : steps) {
        if (!st.moved) continue;
        for (auto& m : hs.members) {
            if (m.aiHandle != st.handle) continue;
            const double d = std::hypot(m.pos.x - pp.x, m.pos.z - pp.z);
            if (d <= kRegroupRangeM && enemy) {  // combat movement; beyond 25 m "move regroup" (action 17) wins
                m.pos.x = st.pos.x;
                m.pos.z = st.pos.z;
                m.pos.y = groundY(sess, m.pos.x, m.pos.z, m.pos.y);
                engagedHandles.push_back(m.aiHandle);
            }
            break;
        }
    }
    for (const auto& sh : shots) {
        ++hs.shots;
        if (!sh.hit || !enemy) continue;
        ++hs.hits;
        enemy->hp -= sh.damage;
        if (enemy->hp <= 0.0 && !enemy->dead) {
            enemy->dead = true;
            enemy->deadAtMs = now;
            ++hs.kills;
            if (sess.police.ai) sess.police.ai->removeNpc(100000 + enemy->id);
            const std::string name = "police_" + std::to_string(enemy->id);
            try {
                if (es.hasCharacter(name)) es.characters().at(name).isDeadHighConfidence = true;
            } catch (const std::exception&) {
            }
            std::printf("homie-trace: frame %d t=%lldms FOLLOWER KILL police unit %llu (shooter handle %llu, %.0f hits taken)\n", sess.frames,
                        static_cast<long long>(now), static_cast<unsigned long long>(enemy->id), static_cast<unsigned long long>(sh.shooter),
                        static_cast<double>(hs.hits));
            hs.enemyHandle = 0;
            enemy = nullptr;
        }
    }

    // ---- follow movement (on-foot followers not in combat movement this frame) ----
    for (size_t i = 0; i < hs.members.size(); ++i) {
        Member& m = hs.members[i];
        if (m.dead || !m.joined || m.phase != Phase::OnFoot || m.downed) continue;
        if (std::find(engagedHandles.begin(), engagedHandles.end(), m.aiHandle) != engagedHandles.end()) continue;
        const double dx = pp.x - m.pos.x, dz = pp.z - m.pos.z;
        const double dist = std::hypot(dx, dz);
        m.gait = 0;
        if (now < m.waitUntilMs) continue;  // squadmate wait (1500 ms, stance 0)
        if (!hm::FollowPlayerAccepts(dist)) continue;  // inside the 6 m arrival box: stands
        // squadmate spacing: another group member ahead within 45 degrees and 2 m -> stop and wait
        const sr3airuntime::Vec3 face{dx / dist, 0.0, dz / dist};
        bool blocked = false;
        for (size_t j = 0; j < hs.members.size() && !blocked; ++j) {
            if (j == i || !hs.members[j].joined || hs.members[j].dead || hs.members[j].phase != Phase::OnFoot) continue;
            blocked = hm::SquadmateAhead(face, {hs.members[j].pos.x - m.pos.x, 0.0, hs.members[j].pos.z - m.pos.z});
        }
        if (blocked) {
            m.waitUntilMs = now + hm::kSquadmateWaitMs;
            ++hs.squadWaits;
            continue;
        }
        m.gait = hm::FollowGait(playerGait, dist);
        const double step = hm::GaitSpeed(m.gait) * dt;
        m.pos.x += face.x * std::min(step, dist);
        m.pos.z += face.z * std::min(step, dist);
        m.pos.y = groundY(sess, m.pos.x, m.pos.z, m.pos.y);
    }

    // ---- removals decided above ----
    {
        std::vector<std::pair<size_t, int>> rem;  // 0 abandon, 1 angry, 2 dead/stand
        for (size_t i : toRemoveAbandon) rem.push_back({i, 0});
        for (size_t i : toRemoveAngry) rem.push_back({i, 1});
        for (size_t i : toRemoveDead) rem.push_back({i, 2});
        std::sort(rem.begin(), rem.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& r : rem) {
            Member& m = hs.members[r.first];
            if (r.second == 0) {
                ++hs.abandoned;
                removeMember(r.first, "PLAYER_ABANDON_FOLLOWER", "abandoned: out of range 20000 ms, marker freed", true);
            } else if (r.second == 1) {
                ++hs.angryLeft;
                removeMember(r.first, "HOMIES_ANGRY_LEFT", "\"has left you!\" friendly fire sum >= 150 within the 6 s window", true);
            } else if (m.phase == Phase::Stand) {
                removeMember(r.first, "crew released", "non-joining crew finished standing", false);
            } else {
                ++hs.deaths;
                const double bill = hm::HospitalBillDollars(rand01(hs));
                const int64_t cents = hm::HospitalBillCents(bill);
                const double before = es.playerCash();
                es.playerCash() = std::max(0.0, es.playerCash() - static_cast<double>(cents));
                hs.billsDollars += (before - es.playerCash()) / 100.0;
                std::printf("homie-trace: frame %d t=%lldms HOMIES_KILLED \"%s has been killed!\" (downed type 0 expired); Hospital bill for %s cost $%.2f "
                            "(%lld cents charged, player cash $%.2f -> $%.2f)\n", sess.frames, static_cast<long long>(now), m.charName.c_str(),
                            m.charName.c_str(), bill, static_cast<long long>(cents), before / 100.0, es.playerCash() / 100.0);
                removeMember(r.first, "homie dead", "bled out", true);
            }
        }
    }
    if (outOfRange != hs.lastAbandonCount) {
        if (outOfRange > 0)
            std::printf("homie-trace: frame %d PLAYER_ABANDON_FOLLOWER \"You are about to abandon %zu of your followers.\"\n", sess.frames, outOfRange);
        hs.lastAbandonCount = outOfRange;
    }

    // parked homie cars: released when far behind
    for (auto it = hs.parkedCars.begin(); it != hs.parkedCars.end();) {
        if (std::hypot(it->pos.x - pp.x, it->pos.z - pp.z) > kChosenParkedCarDropM) it = hs.parkedCars.erase(it);
        else ++it;
    }

    int followers = 0;
    for (const auto& m : hs.members)
        if (m.joined) ++followers;
    hs.maxFollowers = std::max(hs.maxFollowers, followers);
    if (!hs.members.empty() && sess.frames % 30 == 0 && hs.callPhase != 3) {
        for (const auto& m : hs.members) {
            static const char* names[] = {"in_car", "on_foot", "boarding", "seated", "stand"};
            std::printf("homie-trace: frame %d t=%lldms follow %s phase=%s joined=%d dist=%.1f m gait=%d hp=%.0f%s\n", sess.frames,
                        static_cast<long long>(now), m.charName.c_str(), names[static_cast<int>(m.phase)], m.joined ? 1 : 0,
                        std::hypot(m.pos.x - pp.x, m.pos.z - pp.z), m.gait, m.hp, m.downed ? " DOWNED" : "");
        }
    }
}

void printHomieTrace(const PlaytestSession& sess) {
    const auto& hs = sess.homies;
    if (hs.calls == 0 && hs.members.empty()) return;
    std::printf("homie-trace: SUMMARY calls=%llu busy=%llu summons=%llu spawn_fails=%llu arrivals=%llu max_followers=%d followers_now=%zu dropped_for_call=%llu "
                "dismissed=%llu abandoned=%llu angry_left=%llu boarded=%llu exited=%llu teleports=%llu squad_waits=%llu shots=%llu hits=%llu kills=%llu "
                "downed=%llu revived=%llu deaths=%llu hospital_bills=$%.2f\n",
                static_cast<unsigned long long>(hs.calls), static_cast<unsigned long long>(hs.busy), static_cast<unsigned long long>(hs.summons),
                static_cast<unsigned long long>(hs.spawnFails), static_cast<unsigned long long>(hs.arrivals), hs.maxFollowers, hs.members.size(),
                static_cast<unsigned long long>(hs.dropped), static_cast<unsigned long long>(hs.dismissed),
                static_cast<unsigned long long>(hs.abandoned), static_cast<unsigned long long>(hs.angryLeft), static_cast<unsigned long long>(hs.boarded),
                static_cast<unsigned long long>(hs.exited), static_cast<unsigned long long>(hs.teleports), static_cast<unsigned long long>(hs.squadWaits),
                static_cast<unsigned long long>(hs.shots), static_cast<unsigned long long>(hs.hits), static_cast<unsigned long long>(hs.kills),
                static_cast<unsigned long long>(hs.downedCount), static_cast<unsigned long long>(hs.revived), static_cast<unsigned long long>(hs.deaths),
                hs.billsDollars);
}

}  // namespace sr3viewer
