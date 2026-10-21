// Playtest collectibles and challenges through sr3collect (Team C, HANDOFF-C.md section 9;
// spec-tables-progression.md C.2-C.6). With the HUD on, the real collectibles.xtbl and
// sr3_city_challenges.xtbl are loaded. Challenges advance from the events the viewer has:
// the distance driven feeds stat "distance by vehicle" (Drive Distance). A notice shows the
// hud_challenge banner; a completion pays respect (x the respect multiplier) and cash when its
// banner ends. A collected collectible shows the collection screen and pays when it ends.
//
// CHOSEN / test aids: the 80 world placements are OPEN in the spec (no zone record located),
// so `--collectible-test-pickup-every S` registers 20 stand-in handles per group and collects
// one every S seconds, cycling the groups; `--challenges-unlock-all` unlocks every challenge
// (the viewer has no mission progress); the banner/screen time is 3 s.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

#include <algorithm>
#include <cmath>

#include "sr3collect/challenges.h"
#include "sr3collect/collectibles.h"
#include "sr3progression/respect.h"

namespace sr3viewer {

namespace {

constexpr double kBannerS = 3.0;  // CHOSEN banner / collection-screen time

void SetCollectLabel(PlaytestSession& sess, const std::string& text) {
    if (!sess.hudEs || !sess.hudCollectH) return;
    sr3luahost::VintTaggedValue v;
    v.kind = sr3luahost::VintTaggedValue::Kind::String;
    v.text = text;
    sess.hudEs->setVintProperty(sess.hudCollectH, "text_tag", {v});
}

// game_award_respect (through the respect multiplier) then game_award_cash, into the HUD state.
void Pay(PlaytestSession& sess, int32_t respect, double cashDollars, const char* why) {
    sr3luahost::EngineState& es = *sess.hudEs;
    sr3progression::PlayerRespect pr{static_cast<int32_t>(es.respectTotal()), static_cast<int32_t>(es.respectBanked()),
                                     static_cast<int32_t>(es.respectLevel())};
    const sr3progression::RespectTable table = es.realRespectTableActive()
                                                   ? sr3progression::RespectTable::FromParsed(es.respectLevelTable())
                                                   : sr3progression::RespectTable::ShippedCosts();
    sr3progression::AppliedUnlocks applied;
    const float mult = es.respectMultiplier().known() ? static_cast<float>(es.respectMultiplier().get()) : 1.0f;
    const double before = es.playerCash();
    auto pay = [&](double raw) {  // the wallet add of spec-differential-results D-22 (cash multiplier 1.0)
        es.playerCash() = static_cast<double>(sr3progression::AddCashToWallet(
            static_cast<int64_t>(es.playerCash()), static_cast<int64_t>(raw), 1.0f));
    };
    if (respect > 0) {
        const sr3progression::AwardResult a =
            sr3progression::AwardRespect(pr, sr3progression::ScaleByMultiplier(mult, respect), table, applied);
        pay(static_cast<double>(a.overflowCashRaw));
        es.restoreRespectFromSave(static_cast<uint32_t>(pr.total), static_cast<uint32_t>(pr.inLevel),
                                  static_cast<uint32_t>(pr.level));
    }
    pay(std::round(cashDollars * 100.0));
    std::printf("collect  : %s paid $%.2f, respect now %d (level %d)\n", why, (es.playerCash() - before) / 100.0,
                pr.total, pr.level);
}

void ShowChallenge(PlaytestSession& sess, const sr3collect::ChallengeUpdate& u) {
    const sr3collect::ChallengeDef& d = sess.challenges->defs()[static_cast<size_t>(u.index)];
    const sr3collect::ChallengeState& s = sess.challenges->states()[static_cast<size_t>(u.index)];
    static const char* kUnit[] = {"", " m", " miles", " s", " min", " h"};
    char buf[160];
    std::snprintf(buf, sizeof buf, "CHALLENGE %s %.1f/%.1f%s%s", d.name.c_str(),
                  sr3collect::ChallengeDisplayValue(d.displayUnit, s.value),
                  sr3collect::ChallengeDisplayValue(d.displayUnit, d.goal), kUnit[static_cast<int>(d.displayUnit)],
                  u.completedNow ? " COMPLETE" : "");
    std::printf("collect  : %s\n", buf);
    SetCollectLabel(sess, buf);
    sess.collectBannerS = kBannerS;
    if (u.completedNow) sess.challengePayQueue.push_back(u.index);
}

}  // namespace

void setupCollect(PlaytestSession& sess) {
    if (!sess.hudEs) return;
    try {
        sess.collectDefs = sr3collect::CollectibleDefsFromTable(
            sr3tables_progression::ParseCollectiblesTable(sess.docOf("collectibles.xtbl")));
        sess.challenges.emplace(sr3collect::ReadChallenges(sess.docOf("sr3_city_challenges.xtbl")));
    } catch (const std::exception& e) {
        std::printf("collect  : tables not loaded (%s)\n", e.what());
        return;
    }
    if (sess.challengesUnlockAll) sess.challenges->unlockAll();
    if (sess.collectTestPickupEveryS > 0.0)
        for (int g = 0; g < sr3collect::kCollectibleGroups; ++g)
            for (uint64_t h = 1; h <= sr3collect::kSlotsPerGroup; ++h)
                sess.collectibles.registerHandle(g, (static_cast<uint64_t>(g) << 32) | h);  // stand-in handles
    std::printf("collect  : %zu challenge(s)%s, %d collectible handle(s) registered%s\n",
                sess.challenges->defs().size(), sess.challengesUnlockAll ? " (all unlocked: test aid)" : "",
                sess.collectibles.totalRegistered(), sess.collectTestPickupEveryS > 0.0 ? " (test aid)" : "");
    sess.hudCollectH = sess.hudEs->registerTypedVdoObjectForTesting("hud_collect_text", "text", 0, 0);
    sr3luahost::VintTaggedValue vx, vy;
    vx.kind = vy.kind = sr3luahost::VintTaggedValue::Kind::Number;
    vx.number = 900;
    vy.number = 140;
    sess.hudEs->setVintProperty(sess.hudCollectH, "offset", {vx, vy});
}

void stepCollect(PlaytestSession& sess, PlaytestFrame& frm) {
    if (!sess.challenges || !sess.pm_) return;
    // Stat "distance by vehicle" from the distance the player moved while in a vehicle.
    // While driving, the vehicle's own position (the player body is not moved with it).
    const bool driving = sess.inVehicle && sess.vehicleRig_ && sess.vehicleRig_->sim;
    struct { double x, z; } p = {sess.pm_->st.position.x, sess.pm_->st.position.z};
    if (driving) p = {sess.vehicleRig_->sim->state_.posX, sess.vehicleRig_->sim->state_.posZ};
    if (sess.collectHavePrev && driving) {
        const double d = std::hypot(p.x - sess.collectPrevX, p.z - sess.collectPrevZ);
        if (d > 0.0 && d < 50.0) {  // ignore teleports
            for (const auto& u : sess.challenges->statChanged("distance by vehicle", d)) ShowChallenge(sess, u);
            sess.collectDrivenM += d;
            if (sess.collectDrivenM >= 100.0 && !sess.collectDriveLogged) {
                sess.collectDriveLogged = true;
                std::printf("collect  : 100 m driven fed to stat 'distance by vehicle'\n");
            }
        }
    }
    sess.collectPrevX = p.x;
    sess.collectPrevZ = p.z;
    sess.collectHavePrev = true;

    if (sess.collectTestPickupEveryS > 0.0 && !sess.pendingCollect) {
        sess.collectTestClockS += frm.dt;
        if (sess.collectTestClockS >= sess.collectTestPickupEveryS) {
            sess.collectTestClockS = 0.0;
            const int g = sess.collectTestNext % sr3collect::kCollectibleGroups;
            const uint64_t h = (static_cast<uint64_t>(g) << 32) | static_cast<uint64_t>(sess.collectTestNext / 4 + 1);
            ++sess.collectTestNext;
            const sr3collect::CollectResult r = sess.collectibles.collect(g, h, sess.collectDefs);
            if (r.accepted) {
                char buf[160];
                std::snprintf(buf, sizeof buf, "%s %d/%d  $%.0f", r.title.c_str(), r.collected, r.registered, r.cash);
                std::printf("collect  : test pickup: %s (screen type %d)\n", buf, static_cast<int>(r.screen));
                SetCollectLabel(sess, buf);
                sess.pendingCollect = r;
                sess.collectBannerS = kBannerS;
            }
        }
    }

    if (sess.collectBannerS > 0.0) {
        sess.collectBannerS -= frm.dt;
        if (sess.collectBannerS <= 0.0) {
            SetCollectLabel(sess, std::string());
            if (sess.pendingCollect) {  // hud_collection_finished: pay, then (autosave - not wired)
                Pay(sess, sess.pendingCollect->respect, sess.pendingCollect->cash, "collectible");
                const auto a = sr3collect::EvaluateCollectionAchievements(sess.collectibles.totalCollected(),
                                                                          sess.challenges->completedCount());
                if (a.gettingTheGoods && !sess.achievedGettingTheGoods) {
                    sess.achievedGettingTheGoods = true;
                    std::printf("collect  : achievement Getting the Goods\n");
                }
                sess.pendingCollect.reset();
            }
            for (int i : sess.challengePayQueue) {  // hud_diversion_chal_complete
                const sr3collect::ChallengeDef& d = sess.challenges->defs()[static_cast<size_t>(i)];
                Pay(sess, d.respect, d.cash, ("challenge " + d.name).c_str());
                std::printf("collect  : challenges completed %d\n", sess.challenges->completedCount());
            }
            sess.challengePayQueue.clear();
        }
    }
}

}  // namespace sr3viewer
