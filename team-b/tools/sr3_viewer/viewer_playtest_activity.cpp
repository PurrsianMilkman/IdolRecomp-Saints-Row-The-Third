// Playtest activities through sr3activity (Team C, HANDOFF-C.md section 8): the eight instanced
// activity types of spec-activity-runtime.md 6. `--activity <instance>` arms one instance at its
// start point, `--activities` arms every shipped one; walking into a start point begins the run.
// The instance .xtbl comes from `<instance>_modal.str2_pc`, its placed objects (start points,
// navpoints, script NPCs) from its own zone `<instance>.str2_pc`. A HUD line with progress and
// time bars, the per-frame win/fail tick, death as the lose condition, and the 6.1 reward grant
// on success (respect and cash into the HUD EngineState).
//
// CHOSEN stand-ins (section 8 / 8.1): the start point is the Warp_Location navpoint within 3 m
// (the in-world trigger is untraced); Fraud's scoring area counts as reached (the hood latch
// needs the hood volume). The viewer has no player ragdoll/contacts and no NPC AI for activity
// characters, so `--auto-activity` (the end-to-end harness: autopilot travel, scripted Fraud car
// strikes, sr3activity/harness.h) and the per-type `--*-test-*` flags are labelled test aids.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "sr3activity/activity.h"
#include "sr3activity/escort.h"
#include "sr3activity/fraud.h"
#include "sr3activity/genki.h"
#include "sr3activity/harness.h"
#include "sr3activity/heli.h"
#include "sr3activity/mayhem.h"
#include "sr3activity/snatch.h"
#include "sr3activity/trafficking.h"
#include "sr3tables_diversions/tables.h"

namespace sr3viewer {

namespace {

std::string LowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A file inside a top-level str2 package of the city archives (names compared without case).
std::optional<std::string> ReadPackagedFile(PlaytestSession& sess, const std::string& packageName,
                                            const std::string& fileName) {
    const std::string package = LowerStr(packageName);
    const std::string file = LowerStr(fileName);
    for (const auto& a : sess.archives) {
        const vpp::Container& top = *a->top;
        for (size_t i = 0; i < top.entries().size(); ++i) {
            if (LowerStr(top.entries()[i].name) != package) continue;
            try {
                vpp::Container nested = top.openNested(i);
                for (size_t j = 0; j < nested.entries().size(); ++j) {
                    if (LowerStr(nested.entries()[j].name) != file) continue;
                    std::vector<uint8_t> out, sib;
                    if (findEntry(nested, nested.entries()[j].name, out, sib, std::string()) && !out.empty())
                        return std::string(reinterpret_cast<const char*>(out.data()), out.size());
                }
            } catch (const std::exception&) {
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> ReadInstanceXtbl(PlaytestSession& sess, const std::string& instance) {
    return ReadPackagedFile(sess, instance + "_modal.str2_pc", instance + ".xtbl");
}

// The instance's own streamed zone, `<instance>.str2_pc` (e.g. `sr3_city~f_a_sn_ne_01`.czn_pc`): its
// placed objects go into the HUD EngineState's zone-object resolver (the same ingest --mission uses)
// and into sess.activityObjects for the activity step functions.
std::vector<ActivityZoneObject> ReadInstanceZone(PlaytestSession& sess, const std::string& instance, bool ingest) {
    std::vector<ActivityZoneObject> objects;
    const std::string package = LowerStr(instance + ".str2_pc");
    for (const auto& a : sess.archives) {
        const vpp::Container& top = *a->top;
        for (size_t i = 0; i < top.entries().size(); ++i) {
            if (LowerStr(top.entries()[i].name) != package) continue;
            try {
                vpp::Container nested = top.openNested(i);
                for (size_t j = 0; j < nested.entries().size(); ++j) {
                    if (!endsWithNoCase(nested.entries()[j].name, ".czn_pc")) continue;
                    std::vector<uint8_t> bytes;
                    if (!entryBytes(nested, j, bytes) || bytes.empty()) continue;
                    for (const sr3zone::ObjectsSection& sec :
                         sr3zone::ZoneObjects::locate(vpp::ByteView(bytes.data(), bytes.size()))) {
                        if (!sec.complete) continue;
                        if (ingest && sess.hudEs) sess.hudEs->loadZoneObjectsSection(sec);
                        for (const sr3zone::ObjectRecord& rec : sec.records) {
                            ActivityZoneObject o;
                            if (const auto* e = sr3zone::FindTailEntryById(rec, sr3zone::kCtgObjectNamePropertyId))
                                o.name = sr3zone::DecodePropertyAsText(*e);
                            if (const auto cn = sr3zone::ClassNameForHash(rec.classHash)) {
                                o.className = *cn;
                            } else {
                                char hex[16];
                                std::snprintf(hex, sizeof hex, "0x%08x", rec.classHash);
                                o.className = hex;
                            }
                            if (const auto p = sr3zone::ObjectPosition(rec)) {
                                o.hasPos = true;
                                o.x = p->x;
                                o.y = p->y;
                                o.z = p->z;
                            }
                            objects.push_back(std::move(o));
                        }
                    }
                }
            } catch (const std::exception&) {
            }
        }
    }
    return objects;
}

void LoadInstanceZone(PlaytestSession& sess) {
    sess.activityObjects = ReadInstanceZone(sess, sess.activityName, true);
    std::printf("activity : %s zone: %zu placed object(s)\n", sess.activityName.c_str(),
                sess.activityObjects.size());
    if (sess.activityListObjects)
        for (const ActivityZoneObject& o : sess.activityObjects)
            std::printf("activity : object %-32s %-20s %s(%.2f, %.2f, %.2f)\n", o.name.c_str(),
                        o.className.c_str(), o.hasPos ? "" : "no position ", o.x, o.y, o.z);
}

std::optional<sr3activity::ActivityPos> ObjectPos(const std::vector<ActivityZoneObject>& objects,
                                                 const std::string& name) {
    for (const ActivityZoneObject& o : objects)
        if (o.hasPos && sr3xtbl::NameEquals(o.name, name)) return sr3activity::ActivityPos{o.x, o.y, o.z};
    return std::nullopt;
}

sr3activity::ActivityPos PlayerPos(PlaytestSession& sess) {
    if (!sess.pm_) return {};
    const auto& p = sess.pm_->st.position;
    return {p.x, p.y, p.z};
}

void SetActivityLabel(PlaytestSession& sess, const std::string& text) {
    if (!sess.hudEs || !sess.hudActivityH) return;
    sr3luahost::VintTaggedValue v;
    v.kind = sr3luahost::VintTaggedValue::Kind::String;
    v.text = text;
    sess.hudEs->setVintProperty(sess.hudActivityH, "text_tag", {v});
}

void GrantRewards(PlaytestSession& sess) {
    if (!sess.hudEs) return;
    sr3luahost::EngineState& es = *sess.hudEs;
    sr3progression::PlayerRespect pr{static_cast<int32_t>(es.respectTotal()), static_cast<int32_t>(es.respectBanked()),
                                     static_cast<int32_t>(es.respectLevel())};
    const sr3progression::RespectTable table = es.realRespectTableActive()
                                                   ? sr3progression::RespectTable::FromParsed(es.respectLevelTable())
                                                   : sr3progression::RespectTable::ShippedCosts();
    sr3progression::AppliedUnlocks applied;
    const float mult = es.respectMultiplier().known() ? static_cast<float>(es.respectMultiplier().get()) : 1.0f;
    const sr3activity::GrantResult g = sr3activity::GrantActivityRewards(
        *sess.activityInstance, sess.activitySave, sr3activity::ActivityTypeInfo{}, mult, pr, table, applied);
    // Wallet in cents: each add saturates at 2,000,000,000 and is floored at 0 (spec-tables-progression.md
    // cash cap, RP-2). The respect awards come first and pay their overflow cash (respect past the
    // last level) into the same wallet; then Cash_Award.
    const double before = es.playerCash();
    auto pay = [&](int64_t raw) {
        // The wallet add of spec-differential-results D-22 (cash multiplier 1.0: the Cash_Bonus state is not
        // tracked by the engine yet).
        es.playerCash() = static_cast<double>(sr3progression::AddCashToWallet(
            static_cast<int64_t>(es.playerCash()), raw, 1.0f));
    };
    for (const sr3activity::LevelGrant& lg : g.levels) {
        pay(lg.respectFirst.overflowCashRaw);
        pay(lg.respectBonus.overflowCashRaw);
        pay(lg.cashRaw);
        for (const std::string& u : lg.unlocks) std::printf("activity : unlock %s\n", u.c_str());
    }
    const double cash = es.playerCash() - before;  // what the wallet actually gained
    es.restoreRespectFromSave(static_cast<uint32_t>(pr.total), static_cast<uint32_t>(pr.inLevel),
                              static_cast<uint32_t>(pr.level));
    std::printf("activity : rewards $%.2f, respect now %d (level %d)\n", cash / 100.0, pr.total, pr.level);
}

void StepFraud(PlaytestSession& sess, PlaytestFrame& frm) {
    sr3activity::FraudRun& run = *sess.fraudRun;
    const sr3activity::FraudScoringContext ctx{true, -1};  // scoring area: CHOSEN reached
    if (sess.fraudTestHitEveryS > 0.0) {
        sess.fraudTestHitClockS += frm.dt;
        if (sess.fraudTestHitClockS >= sess.fraudTestHitEveryS) {
            sess.fraudTestHitClockS = 0.0;
            run.ragdollBegin();
            sr3activity::FraudImpact imp;  // test aid: one head impact
            imp.dt = frm.dt > 0.0 ? frm.dt : 1.0 / 60.0;
            imp.relativeVelocity = {8.0, 0.0, 8.0};
            imp.normalSpeed = 10.0;
            imp.part = sr3activity::BodyPart::Head;
            run.impact(imp, ctx);
            if (const auto b = run.ragdollEnd(sr3activity::FraudHitInputs{}, ctx))
                std::printf("activity : test hit banked $%.0f (total $%.0f)\n", b->hitValue, run.score());
        }
    }
    if (sess.fraudStrikes) {  // --auto-activity: a scripted car strike (test aid, harness.h)
        if (const auto b = sess.fraudStrikes->step(run, frm.dt, ctx))
            std::printf("activity : harness car strike %d banked $%.0f (total $%.0f)\n",
                        sess.fraudStrikes->strikes(), b->hitValue, run.score());
    }
    const bool ragdoll = sess.fraudStrikes && sess.fraudStrikes->inEpisode();
    if (sess.vitals_ && sess.vitals_->dead()) run.playerDied();
    const sr3activity::FraudTickResult t = run.tick(frm.dt, ctx, ragdoll, false, false);
    char buf[128];
    std::snprintf(buf, sizeof buf, "FRAUD $%.0f / $%.0f  %d s", run.score(), run.goal(),
                  static_cast<int>(std::max(0.0, run.timeLeft())));
    SetActivityLabel(sess, buf);
    if (t.justSucceeded) {
        sess.activityDone = true;
        std::printf("activity : %s SUCCEEDED ($%.0f of $%.0f)\n", sess.activityName.c_str(), run.score(), run.goal());
        GrantRewards(sess);
    } else if (run.state() == sr3activity::FraudState::Failed) {
        sess.activityDone = true;
        std::printf("activity : %s FAILED ($%.0f of $%.0f)%s\n", sess.activityName.c_str(), run.score(), run.goal(),
                    (sess.vitals_ && sess.vitals_->dead()) ? " - player died" : " - time");
    }
}

void StepSnatch(PlaytestSession& sess, PlaytestFrame& frm) {
    sr3activity::SnatchRun& run = *sess.snatchRun;
    if (sess.snatchTestDeliverEveryS > 0.0) {
        sess.snatchTestDeliverClockS += frm.dt;
        if (sess.snatchTestDeliverClockS >= sess.snatchTestDeliverEveryS) {
            sess.snatchTestDeliverClockS = 0.0;
            run.deliver();
            std::printf("activity : test delivery (%d / %d)\n", run.delivered(), run.target());
        }
    }
    if (sess.snatchHostages && sess.snatchDropoff) {
        const auto u = sess.snatchHostages->update(PlayerPos(sess), sess.inVehicle, *sess.snatchDropoff,
                                                   sess.snatchLayout.recruitDist, run);
        if (u.recruited > 0)
            std::printf("activity : %d hostage(s) joined (%d following)\n", u.recruited,
                        sess.snatchHostages->count(sr3activity::SnatchHostages::State::Following));
        if (u.delivered > 0)
            std::printf("activity : delivered %d at %s (%d / %d)\n", u.delivered,
                        sess.snatchLayout.dropoffNavpoint.c_str(), run.delivered(), run.target());
    }
    if (sess.vitals_ && sess.vitals_->dead()) run.playerDied();
    const sr3activity::SnatchTickResult t = run.tick(frm.dt);
    const int following =
        sess.snatchHostages ? sess.snatchHostages->count(sr3activity::SnatchHostages::State::Following) : 0;
    char buf[128];
    std::snprintf(buf, sizeof buf, "SNATCH %d / %d  %d s  following %d", run.delivered(), run.target(),
                  static_cast<int>(std::max(0.0, run.timeLeft())), following);
    SetActivityLabel(sess, buf);
    if (t.justSucceeded) {
        sess.activityDone = true;
        std::printf("activity : %s SUCCEEDED (%d delivered)\n", sess.activityName.c_str(), run.delivered());
        GrantRewards(sess);
    } else if (run.state() == sr3activity::SnatchState::Failed) {
        sess.activityDone = true;
        std::printf("activity : %s FAILED (%d of %d)%s\n", sess.activityName.c_str(), run.delivered(), run.target(),
                    (sess.vitals_ && sess.vitals_->dead()) ? " - player died" : " - time");
    }
}

void StepMayhem(PlaytestSession& sess, PlaytestFrame& frm) {
    sr3activity::MayhemRun& run = *sess.mayhemRun;
    if (sess.mayhemTestKillEveryS > 0.0 && !(sess.autoActivity && run.banked() >= run.goal())) {  // auto: let the combo close
        sess.mayhemTestKillClockS += frm.dt;
        if (sess.mayhemTestKillClockS >= sess.mayhemTestKillEveryS) {
            sess.mayhemTestKillClockS = 0.0;
            const double add = run.event({sr3activity::MayhemClass::Human});  // test aid: one human kill
            std::printf("activity : test kill +$%.0f (banked $%.0f, combo x%.2f)\n", add, run.banked(),
                        run.comboMultiplier());
        }
    }
    if (sess.vitals_ && sess.vitals_->dead()) run.playerDied();
    // The Activity_Center navpoint is not resolved in the viewer: distance 0 (CHOSEN, no leash).
    const sr3activity::MayhemTickResult t = run.tick(frm.dt, 0.0, true, false);
    if (t.comboEnded) std::printf("activity : combo total $%.0f\n", t.comboTotal);
    char buf[128];
    std::snprintf(buf, sizeof buf, "MAYHEM $%.0f / $%.0f  %d s  x%.2f", run.banked(), run.goal(),
                  static_cast<int>(std::max(0.0, run.timeLeft())), run.comboMultiplier());
    SetActivityLabel(sess, buf);
    if (t.justSucceeded) {
        sess.activityDone = true;
        std::printf("activity : %s SUCCEEDED ($%.0f of $%.0f)\n", sess.activityName.c_str(), run.banked(), run.goal());
        GrantRewards(sess);
    } else if (run.state() == sr3activity::MayhemState::Failed) {
        sess.activityDone = true;
        std::printf("activity : %s FAILED ($%.0f of $%.0f)%s\n", sess.activityName.c_str(), run.banked(), run.goal(),
                    (sess.vitals_ && sess.vitals_->dead()) ? " - player died" : " - time");
    }
}

void StepEscort(PlaytestSession& sess, PlaytestFrame& frm) {
    sr3activity::EscortRun& run = *sess.escortRun;
    // CHOSEN: the player's own vehicle stands in for the client's car (no client or limo is
    // spawned); no paparazzi exist yet.
    const bool inCar = sess.escortTestDrive || sess.inVehicle;
    if (inCar) sess.escortEnteredVehicle = true;
    sr3activity::EscortFrame f;
    f.dt = frm.dt;
    f.playerDrivingClientVehicle = inCar;
    f.playerInClientVehicle = inCar || !sess.escortEnteredVehicle;
    if (sess.vitals_ && sess.vitals_->dead()) run.playerDied();
    const sr3activity::EscortTickResult t = run.tick(f);
    if (t.satisfactionFull) {
        std::printf("activity : satisfaction full - drive to the drop-off\n");
        if (sess.escortTestDrive) run.arriveAtDropoff();  // test aid: arrive at once
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "ESCORT %d%%  FOOTAGE %d%%%s", static_cast<int>(run.satisfactionMeter() * 100.0),
                  static_cast<int>(run.footageMeter() * 100.0), t.getBackMessage ? "  GET BACK IN" : "");
    SetActivityLabel(sess, buf);
    if (run.phase() == sr3activity::EscortPhase::Succeeded) {
        sess.activityDone = true;
        std::printf("activity : %s SUCCEEDED\n", sess.activityName.c_str());
        GrantRewards(sess);
    } else if (run.phase() == sr3activity::EscortPhase::Failed) {
        sess.activityDone = true;
        std::printf("activity : %s FAILED (reason %d)\n", sess.activityName.c_str(),
                    static_cast<int>(run.failReason()));
    }
}

// The navpoint the run asks for next (the current deal's Deal_Nav, then Ending_Nav) and its
// position in the instance zone.
std::optional<sr3activity::ActivityPos> TraffickingTarget(PlaytestSession& sess, std::string* name = nullptr) {
    const sr3activity::TraffickingRun& run = *sess.traffickingRun;
    if (run.state() == sr3activity::TraffickingState::Succeeded || run.state() == sr3activity::TraffickingState::Failed)
        return std::nullopt;
    const std::string nav = run.currentDeal() ? run.currentDeal()->dealNav : run.params().endingNav;
    if (name) *name = nav;
    return ObjectPos(sess.activityObjects, nav);
}

// CHOSEN arrival radius at a deal or ending navpoint (6.3 gives none).
constexpr double kTraffickingArriveRadius = 5.0;

void StepTrafficking(PlaytestSession& sess, PlaytestFrame& frm) {
    sr3activity::TraffickingRun& run = *sess.traffickingRun;
    {
        std::string nav;
        const auto at = TraffickingTarget(sess, &nav);
        if (at && sr3activity::Distance(PlayerPos(sess), *at) <= kTraffickingArriveRadius) {
            if (run.currentDeal()) {
                std::printf("activity : deal done at %s\n", nav.c_str());
                run.completeDeal();
            } else if (run.reachEnding()) {
                std::printf("activity : reached the ending navpoint %s\n", nav.c_str());
            }
        }
    }
    if (sess.traffickingTestDealEveryS > 0.0) {
        sess.traffickingTestDealClockS += frm.dt;
        if (sess.traffickingTestDealClockS >= sess.traffickingTestDealEveryS) {
            sess.traffickingTestDealClockS = 0.0;
            if (const sr3activity::TraffickingDeal* d = run.currentDeal()) {
                std::printf("activity : test deal at %s\n", d->dealNav.c_str());
                run.completeDeal();
            } else if (run.reachEnding()) {
                std::printf("activity : test arrival at the ending navpoint\n");
            }
        }
    }
    if (sess.vitals_ && sess.vitals_->dead()) run.playerDied();
    char buf[128];
    std::snprintf(buf, sizeof buf, "TRAFFICKING %d / %d deals%s", run.dealsDone(), run.dealsNeeded(),
                  run.state() == sr3activity::TraffickingState::ToEnding ? "  GO TO THE END" : "");
    SetActivityLabel(sess, buf);
    if (run.state() == sr3activity::TraffickingState::Succeeded) {
        sess.activityDone = true;
        std::printf("activity : %s SUCCEEDED\n", sess.activityName.c_str());
        GrantRewards(sess);
    } else if (run.state() == sr3activity::TraffickingState::Failed) {
        sess.activityDone = true;
        std::printf("activity : %s FAILED (reason %d)\n", sess.activityName.c_str(),
                    static_cast<int>(run.failReason()));
    }
}

void StepGenki(PlaytestSession& sess, PlaytestFrame& frm) {
    sr3activity::GenkiRun& run = *sess.genkiRun;
    if (sess.genkiTestKillEveryS > 0.0) {
        sess.genkiTestKillClockS += frm.dt;
        if (sess.genkiTestKillClockS >= sess.genkiTestKillEveryS) {
            sess.genkiTestKillClockS = 0.0;
            const double add = run.kill({sr3activity::GenkiVictim::Hunter});  // test aid
            std::printf("activity : test hunter kill +$%.0f (cash $%.0f)\n", add, run.cash());
            if (run.reachExit()) std::printf("activity : test exit reached\n");
        }
    }
    if (sess.vitals_ && sess.vitals_->dead()) run.playerDied();
    if (const long long ended = run.tick(frm.dt); ended >= 0)
        std::printf("activity : combo ended, bank $%lld\n", ended);
    char buf[128];
    std::snprintf(buf, sizeof buf, "GENKI $%.0f / $%.0f  %d s  x%.1f", run.cash(), run.required(),
                  static_cast<int>(std::max(0.0, run.timeLeft())), run.combo().multiplier());
    SetActivityLabel(sess, buf);
    if (run.state() == sr3activity::GenkiState::Succeeded) {
        sess.activityDone = true;
        std::printf("activity : %s SUCCEEDED ($%.0f)\n", sess.activityName.c_str(), run.cash());
        GrantRewards(sess);
    } else if (run.state() == sr3activity::GenkiState::Failed) {
        sess.activityDone = true;
        std::printf("activity : %s FAILED ($%.0f of $%.0f)%s\n", sess.activityName.c_str(), run.cash(),
                    run.required(), (sess.vitals_ && sess.vitals_->dead()) ? " - player died" : " - time");
    }
}

void StepHeli(PlaytestSession& sess, PlaytestFrame& frm) {
    sr3activity::HeliRun& run = *sess.heliRun;
    if (sess.heliTestObjectiveEveryS > 0.0) {
        sess.heliTestObjectiveClockS += frm.dt;
        if (sess.heliTestObjectiveClockS >= sess.heliTestObjectiveEveryS) {
            sess.heliTestObjectiveClockS = 0.0;
            run.objectiveCompleted();  // test aid
            std::printf("activity : test objective (%d / %d)\n", run.objectivesDone(), run.objectivesNeeded());
        }
    }
    if (sess.vitals_ && sess.vitals_->dead()) run.playerDied();
    run.tick(frm.dt);
    char buf[128];
    std::snprintf(buf, sizeof buf, "HELI ASSAULT %d / %d objectives", run.objectivesDone(), run.objectivesNeeded());
    SetActivityLabel(sess, buf);
    if (run.state() == sr3activity::HeliState::Succeeded) {
        sess.activityDone = true;
        std::printf("activity : %s SUCCEEDED\n", sess.activityName.c_str());
        GrantRewards(sess);
    } else if (run.state() == sr3activity::HeliState::Failed) {
        sess.activityDone = true;
        std::printf("activity : %s FAILED (reason %d)\n", sess.activityName.c_str(),
                    static_cast<int>(run.failReason()));
    }
}

void ResetRuns(PlaytestSession& sess) {
    sess.activityInstance.reset();
    sess.fraudRun.reset();
    sess.snatchRun.reset();
    sess.mayhemRun.reset();
    sess.escortRun.reset();
    sess.traffickingRun.reset();
    sess.genkiRun.reset();
    sess.heliRun.reset();
    sess.fraudStrikes.reset();
    sess.snatchHostages.reset();
    sess.snatchDropoff.reset();
    sess.escortEnteredVehicle = false;
    sess.activityDone = false;
    sess.activityRunning = false;
    sess.activityMeter = sess.activityTimer = -1.0;
    sess.activityTimerTotalS = sess.activityMeterTotal = 0.0;
}

// Starts sess.activityName: the instance row, its zone, and the per-type run.
bool BeginRun(PlaytestSession& sess) {
    ResetRuns(sess);
    const sr3activity::ActivityType type = sr3activity::TypeFromInstanceName(sess.activityName);
    const bool isMayhem = type == sr3activity::ActivityType::Mayhem || type == sr3activity::ActivityType::TankMayhem;
    if (type != sr3activity::ActivityType::InsuranceFraud && type != sr3activity::ActivityType::Snatch && !isMayhem && type != sr3activity::ActivityType::Trafficking && type != sr3activity::ActivityType::Genki && type != sr3activity::ActivityType::HeliAssault &&
        type != sr3activity::ActivityType::Escort) {
        std::printf("activity : %s - only Fraud (_a_if_*), Snatch (_a_sn_*) and Mayhem (_a_mh_*, _a_tm_*) are wired so far\n",
                    sess.activityName.c_str());
        return false;
    }
    const std::optional<std::string> xml = ReadInstanceXtbl(sess, sess.activityName);
    if (!xml) {
        std::printf("activity : %s_modal.str2_pc / %s.xtbl not found in the city archives\n",
                    sess.activityName.c_str(), sess.activityName.c_str());
        return false;
    }
    const sr3xtbl::Document instDoc = sr3xtbl::ParseDocument(std::string_view(*xml));
    sess.activityInstance = sr3activity::ReadActivityInstance(instDoc);
    if (!sess.activityInstance || sess.activityInstance->levels.empty()) {
        std::printf("activity : %s has no Activity/Levels row\n", sess.activityName.c_str());
        sess.activityInstance.reset();
        return false;
    }
    LoadInstanceZone(sess);
    const sr3activity::ActivityLevel& lv = sess.activityInstance->levels[0];
    if (type == sr3activity::ActivityType::InsuranceFraud) {
        sr3activity::FraudParams params;
        try {
            const sr3xtbl::Document doc = sess.docOf("fraud_globals.xtbl");
            if (const auto g = sr3tables_diversions::ParseFraudGlobalsTable(doc))
                params = sr3activity::FraudParamsFromTables(*g, doc);
        } catch (const std::exception& e) {
            std::printf("activity : fraud_globals.xtbl not loaded (%s) - scoring uses neutral defaults\n", e.what());
        }
        sess.fraudRun.emplace(params, lv);
        std::printf("activity : fraud scalars: crush %.4g/speed, scrape %.4g/speed^2, money x%.4g, min $%.4g x %.4g, "
                    "mass x%.3g-%.3g over %.0f-%.0f kg above %.3g m/s\n",
                    params.crushDamagePerSpeed, params.scrapeDamagePerSpeed, params.moneyConversion, params.minMoney,
                    params.damageMultiplier, params.massMinMultiplier, params.massMaxMultiplier, params.massMinMass,
                    params.massMaxMass, params.massMinSpeed);
        sess.activityTimerTotalS = lv.duration.value_or(0.0);
        if (sess.autoActivity) sess.fraudStrikes.emplace();  // harness car strikes (test aid)
        std::printf("activity : %s started: goal $%.0f in %.0f s, %d bonus spot(s), award $%d / %d respect%s\n",
                    sess.activityName.c_str(), sess.fraudRun->goal(), lv.duration.value_or(0.0),
                    static_cast<int>(lv.maxSpots.value_or(0.0)), lv.cashAward, lv.propsAward,
                    sess.fraudTestHitEveryS > 0.0 ? " (synthetic test hits ON)" : "");
    } else if (type == sr3activity::ActivityType::Escort) {
        const std::string lower = LowerStr(sess.activityName);
        const sr3activity::EscortVariant variant = lower.rfind("dlc1_", 0) == 0 ? sr3activity::EscortVariant::Genki
                                                   : (lower.find("_sw") != std::string::npos ? sr3activity::EscortVariant::Tiger
                                                                                             : sr3activity::EscortVariant::Base);
        const auto ep = sr3activity::ReadEscortParams(instDoc, variant);
        if (!ep) {
            std::printf("activity : %s has no Escort level row\n", sess.activityName.c_str());
            return false;
        }
        sess.escortRun.emplace(*ep);
        sess.activityMeterTotal = ep->satisfactionTimeS;
        sess.activityTimerTotalS = ep->footageLimitS;
        std::printf("activity : %s started: %.0f s of satisfaction, footage limit %.0f s, award $%d / %d respect%s\n",
                    sess.activityName.c_str(), ep->satisfactionTimeS, ep->footageLimitS, lv.cashAward, lv.propsAward,
                    sess.escortTestDrive ? " (test drive ON)" : "");
    } else if (type == sr3activity::ActivityType::Trafficking) {
        const auto tp = sr3activity::ReadTraffickingParams(instDoc);
        if (!tp) {
            std::printf("activity : %s has no Trafficking level row\n", sess.activityName.c_str());
            return false;
        }
        sess.traffickingRun.emplace(*tp);
        std::printf("activity : %s started: %d deals of %zu, dealer %s, award $%d / %d respect%s\n",
                    sess.activityName.c_str(), tp->dealUseCount, tp->deals.size(), tp->dealerType.c_str(),
                    lv.cashAward, lv.propsAward, sess.traffickingTestDealEveryS > 0.0 ? " (test deals ON)" : "");
    } else if (type == sr3activity::ActivityType::Genki) {
        const auto gl = sr3activity::ReadGenkiLevel(instDoc);
        const std::optional<std::string> gx = ReadPackagedFile(sess, "Running Man.str2_pc", "running_man_globals.xtbl");
        std::optional<sr3activity::GenkiGlobals> gg;
        if (gx) gg = sr3activity::ReadGenkiGlobals(sr3xtbl::ParseDocument(std::string_view(*gx)));
        if (!gl || !gg) {
            std::printf("activity : %s: level row or running_man_globals.xtbl missing\n", sess.activityName.c_str());
            return false;
        }
        sess.genkiRun.emplace(*gg, *gl);
        sess.activityTimerTotalS = gl->timeLimitS;
        std::printf("activity : %s started: bring $%.0f to %s within %.0f s, award $%d / %d respect%s\n",
                    sess.activityName.c_str(), gl->requiredCash, gl->endExit.c_str(), gl->timeLimitS, lv.cashAward,
                    lv.propsAward, sess.genkiTestKillEveryS > 0.0 ? " (test kills ON)" : "");
    } else if (type == sr3activity::ActivityType::HeliAssault) {
        const auto hp = sr3activity::ReadHeliParams(instDoc);
        if (!hp) {
            std::printf("activity : %s has no Heli Assault level row\n", sess.activityName.c_str());
            return false;
        }
        sess.heliRun.emplace(*hp);
        if (hp->useTimer) sess.activityTimerTotalS = hp->timeForLevelS;
        std::printf("activity : %s started: %d objective(s) (kind %d), %s, award $%d / %d respect%s\n",
                    sess.activityName.c_str(), hp->objectivesNeeded, static_cast<int>(hp->kind),
                    hp->useTimer ? "timed" : "untimed", lv.cashAward, lv.propsAward,
                    sess.heliTestObjectiveEveryS > 0.0 ? " (test objectives ON)" : "");
    } else if (isMayhem) {
        const sr3activity::MayhemKind kind = type == sr3activity::ActivityType::TankMayhem
                                                 ? sr3activity::MayhemKind::TankMayhem
                                                 : sr3activity::MayhemKind::Mayhem;
        const auto mp = sr3activity::ReadMayhemParams(instDoc, kind);
        if (!mp) {
            std::printf("activity : %s has no Mayhem level row\n", sess.activityName.c_str());
            return false;
        }
        sess.mayhemRun.emplace(*mp);
        sess.activityTimerTotalS = mp->durationS;
        std::printf("activity : %s started: destroy $%.0f in %.0f s, %zu combo cutoffs, award $%d / %d respect%s\n",
                    sess.activityName.c_str(), mp->goal, mp->durationS, mp->cutoffs.size(), lv.cashAward,
                    lv.propsAward, sess.mayhemTestKillEveryS > 0.0 ? " (synthetic test kills ON)" : "");
    } else {
        sess.snatchRun.emplace(sr3activity::ReadSnatchParams(instDoc), lv);
        sess.activityTimerTotalS = lv.duration.value_or(0.0);
        // The rescued NPCs: each pickup's first option, placed at its first Location_Navpoint
        // (CHOSEN, HANDOFF-C section 8; the game's choice among options and navpoints is not
        // specified). The drop-off is Snatch_Info/Navpoint_Name.
        sess.snatchLayout = sr3activity::ReadSnatchLayout(instDoc);
        sess.snatchDropoff = ObjectPos(sess.activityObjects, sess.snatchLayout.dropoffNavpoint);
        sess.snatchHostages.emplace();
        for (const sr3activity::SnatchPickup& p : sess.snatchLayout.pickups) {
            if (p.options.empty() || p.locationNavpoints.empty()) continue;
            const auto at = ObjectPos(sess.activityObjects, p.locationNavpoints.front());
            if (!at) continue;
            for (const std::string& ho : p.options.front().hos) sess.snatchHostages->add(ho, *at);
            std::printf("activity : pickup %s: %zu hostage(s) at %s (%.1f, %.1f, %.1f), %zu option(s)\n",
                        p.scriptGroup.c_str(), p.options.front().hos.size(), p.locationNavpoints.front().c_str(),
                        at->x, at->y, at->z, p.options.size());
        }
        if (sess.snatchDropoff)
            std::printf("activity : drop-off %s at (%.1f, %.1f, %.1f)\n", sess.snatchLayout.dropoffNavpoint.c_str(),
                        sess.snatchDropoff->x, sess.snatchDropoff->y, sess.snatchDropoff->z);
        else
            std::printf("activity : drop-off %s not in the instance zone - deliveries only via the test aid\n",
                        sess.snatchLayout.dropoffNavpoint.c_str());
        std::printf("activity : %s started: deliver %d in %.0f s, award $%d / %d respect%s\n",
                    sess.activityName.c_str(), sess.snatchRun->target(), lv.duration.value_or(0.0), lv.cashAward,
                    lv.propsAward, sess.snatchTestDeliverEveryS > 0.0 ? " (synthetic test deliveries ON)" : "");
    }
    if (sess.hudEs && !sess.hudActivityH) {
        sess.hudActivityH = sess.hudEs->registerTypedVdoObjectForTesting("hud_activity_text", "text", 0, 0);
        sr3luahost::VintTaggedValue vx, vy;
        vx.kind = vy.kind = sr3luahost::VintTaggedValue::Kind::Number;
        vx.number = 900;
        vy.number = 64;
        sess.hudEs->setVintProperty(sess.hudActivityH, "offset", {vx, vy});
    }
    // --auto-activity for the types without a harness driver yet: turn on their test aid at a
    // CHOSEN default period unless one was given (labelled test aids, HANDOFF-C 8.1).
    if (sess.autoActivity) {
        if (sess.mayhemRun && sess.mayhemTestKillEveryS <= 0.0) sess.mayhemTestKillEveryS = 0.25;
        if (sess.escortRun) sess.escortTestDrive = true;
        if (sess.traffickingRun && sess.traffickingTestDealEveryS <= 0.0 && !TraffickingTarget(sess))
            sess.traffickingTestDealEveryS = 5.0;  // only when the deal navpoints are not in the zone
        if (sess.genkiRun && sess.genkiTestKillEveryS <= 0.0) sess.genkiTestKillEveryS = 1.0;
        if (sess.heliRun && sess.heliTestObjectiveEveryS <= 0.0) sess.heliTestObjectiveEveryS = 5.0;
    }
    sess.activityRunning = true;
    return true;
}

void EnsureHudLabel(PlaytestSession& sess) {
    if (!sess.hudEs || sess.hudActivityH) return;
    sess.hudActivityH = sess.hudEs->registerTypedVdoObjectForTesting("hud_activity_text", "text", 0, 0);
    sr3luahost::VintTaggedValue vx, vy;
    vx.kind = vy.kind = sr3luahost::VintTaggedValue::Kind::Number;
    vx.number = 900;
    vy.number = 64;
    sess.hudEs->setVintProperty(sess.hudActivityH, "offset", {vx, vy});
}

// Arms `instance` at its start point (the Warp_Location navpoint of its own zone).
bool ArmMarker(PlaytestSession& sess, const std::string& instance, bool quiet) {
    const std::optional<std::string> xml = ReadInstanceXtbl(sess, instance);
    if (!xml) return false;
    const auto inst = sr3activity::ReadActivityInstance(sr3xtbl::ParseDocument(std::string_view(*xml)));
    if (!inst || !inst->warpLocation) return false;
    const auto at = ObjectPos(ReadInstanceZone(sess, instance, false), *inst->warpLocation);
    if (!at) return false;
    sess.activityMarkers.push_back({instance, *inst->warpLocation, *at});
    if (!quiet)
        std::printf("activity : %s armed: start point '%s' at (%.2f, %.2f, %.2f), radius %.0f m [CHOSEN start point]\n",
                    instance.c_str(), inst->warpLocation->c_str(), at->x, at->y, at->z,
                    sr3activity::kActivityStartRadius);
    return true;
}

// --activities: every shipped instance (`<instance>_modal.str2_pc` of one of the eight types).
void ArmAllInstances(PlaytestSession& sess) {
    static const std::string kSuffix = "_modal.str2_pc";
    std::set<std::string> seen;
    size_t found = 0;
    for (const auto& a : sess.archives) {
        for (const vpp::Entry& e : a->top->entries()) {
            const std::string lower = LowerStr(e.name);
            if (lower.size() <= kSuffix.size() || lower.compare(lower.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
                continue;
            const std::string stem = e.name.substr(0, e.name.size() - kSuffix.size());
            if (sr3activity::TypeFromInstanceName(stem) == sr3activity::ActivityType::Unknown) continue;
            if (!seen.insert(LowerStr(stem)).second) continue;
            ++found;
            ArmMarker(sess, stem, true);
        }
    }
    std::printf("activity : --activities: %zu instance(s) found, %zu armed at their start points\n", found,
                sess.activityMarkers.size());
}

void StepRun(PlaytestSession& sess, PlaytestFrame& frm) {
    if (sess.fraudRun) StepFraud(sess, frm);
    else if (sess.snatchRun) StepSnatch(sess, frm);
    else if (sess.mayhemRun) StepMayhem(sess, frm);
    else if (sess.escortRun) StepEscort(sess, frm);
    else if (sess.traffickingRun) StepTrafficking(sess, frm);
    else if (sess.genkiRun) StepGenki(sess, frm);
    else if (sess.heliRun) StepHeli(sess, frm);
}

// The HUD bars: progress toward the win and the time left (no activity meter is specified
// in spec-hud-runtime.md, so both bars are CHOSEN).
void UpdateMeters(PlaytestSession& sess) {
    auto frac = [](double a, double b) { return b > 0.0 ? std::clamp(a / b, 0.0, 1.0) : -1.0; };
    const double tt = sess.activityTimerTotalS;
    double meter = -1.0, timer = -1.0;
    if (sess.fraudRun) {
        meter = frac(sess.fraudRun->score(), sess.fraudRun->goal());
        timer = frac(sess.fraudRun->timeLeft(), tt);
    } else if (sess.snatchRun) {
        meter = frac(sess.snatchRun->delivered(), sess.snatchRun->target());
        timer = frac(sess.snatchRun->timeLeft(), tt);
    } else if (sess.mayhemRun) {
        meter = frac(sess.mayhemRun->banked(), sess.mayhemRun->goal());
        timer = frac(sess.mayhemRun->timeLeft(), tt);
    } else if (sess.escortRun) {
        if (sess.activityMeterTotal > 0.0)
            meter = 1.0 - frac(sess.escortRun->satisfactionRemainingS(), sess.activityMeterTotal);
        timer = frac(sess.escortRun->footageRemainingS(), tt);
    } else if (sess.traffickingRun) {
        meter = frac(sess.traffickingRun->dealsDone(), sess.traffickingRun->dealsNeeded());
    } else if (sess.genkiRun) {
        meter = frac(sess.genkiRun->cash(), sess.genkiRun->required());
        timer = frac(sess.genkiRun->timeLeft(), tt);
    } else if (sess.heliRun) {
        meter = frac(sess.heliRun->objectivesDone(), sess.heliRun->objectivesNeeded());
        timer = frac(sess.heliRun->timeLeft(), tt);
    }
    sess.activityMeter = meter;
    sess.activityTimer = timer;
}

// --auto-activity (test aid; every rule here is a CHOSEN harness stand-in, like the mission
// --auto-objective teleport): the player is teleported 8 m from the start point of
// --activity and walks into it at 4 m/s; then the run's objectives are driven in straight
// lines at 20 m/s (no collision, no vehicle). Snatch: the nearest waiting hostage, then the
// drop-off once enough hostages follow to finish (or none are left waiting). Fraud: the
// player stays put and FraudStrikeHarness scripts the car strikes.
constexpr double kAutoWalkSpeed = 4.0;
constexpr double kAutoDriveSpeed = 20.0;

void Autopilot(PlaytestSession& sess, PlaytestFrame& frm) {
    if (!sess.pm_ || sess.activityDone) return;
    std::optional<sr3activity::ActivityPos> target;
    double speed = kAutoDriveSpeed;
    if (!sess.activityRunning) {
        const ActivityMarker* m = nullptr;
        for (const ActivityMarker& mk : sess.activityMarkers)
            if (sr3xtbl::NameEquals(mk.name, sess.activityName)) m = &mk;
        if (!m) return;
        if (!sess.autoActivityTeleported) {
            sess.autoActivityTeleported = true;
            sess.autoPos = sr3activity::ActivityPos{m->start.x, m->start.y, m->start.z - 8.0};
            std::printf("activity : auto: teleported 8 m from the start point of %s [CHOSEN test-harness stand-in]\n",
                        m->name.c_str());
        }
        target = m->start;
        speed = kAutoWalkSpeed;
    } else if (sess.snatchRun && sess.snatchHostages && sess.snatchDropoff) {
        using HS = sr3activity::SnatchHostages;
        const int following = sess.snatchHostages->count(HS::State::Following);
        const int need = sess.snatchRun->target() - sess.snatchRun->delivered();
        const sr3activity::ActivityPos from = sess.autoPos ? *sess.autoPos : PlayerPos(sess);
        std::optional<sr3activity::ActivityPos> nearest;
        double best = 0.0;
        for (const HS::Hostage& h : sess.snatchHostages->hostages()) {
            if (h.state != HS::State::Waiting) continue;
            const double d = sr3activity::Distance(from, h.pos);
            if (!nearest || d < best) {
                nearest = h.pos;
                best = d;
            }
        }
        if (following > 0 && (following >= need || !nearest)) target = *sess.snatchDropoff;
        else if (nearest) target = *nearest;
    } else if (sess.traffickingRun) {
        target = TraffickingTarget(sess);  // the next deal, then the ending navpoint
    }
    if (!sess.autoPos) sess.autoPos = PlayerPos(sess);
    if (target) sess.autoPos = sr3activity::StepToward(*sess.autoPos, *target, speed, frm.dt);
    sess.pm_->warp(sr3player::Vec3{sess.autoPos->x, sess.autoPos->y, sess.autoPos->z});
}

}  // namespace

void setupActivity(PlaytestSession& sess) {
    if (sess.activitiesAll) ArmAllInstances(sess);
    if (!sess.activityName.empty()) {
        bool armed = false;
        for (const ActivityMarker& m : sess.activityMarkers)
            if (sr3xtbl::NameEquals(m.name, sess.activityName)) {
                armed = true;
                std::printf("activity : %s armed: start point '%s' at (%.2f, %.2f, %.2f)\n", m.name.c_str(),
                            m.warpName.c_str(), m.start.x, m.start.y, m.start.z);
            }
        if (!armed && !ArmMarker(sess, sess.activityName, false)) {
            std::printf("activity : %s: no start point resolved - starting where the player stands\n",
                        sess.activityName.c_str());
            BeginRun(sess);
        }
    }
    if (!sess.activityMarkers.empty() || sess.activityRunning) EnsureHudLabel(sess);
}

void stepActivity(PlaytestSession& sess, PlaytestFrame& frm) {
    if (sess.activityMarkers.empty() && !sess.activityRunning) return;
    if (sess.autoActivity) Autopilot(sess, frm);
    if (sess.activityRunning && !sess.activityDone) {
        StepRun(sess, frm);
        UpdateMeters(sess);
        if (sess.activityDone && sess.autoActivity && (sess.maxFrames < 0 || sess.maxFrames > sess.frames + 15))
            sess.maxFrames = sess.frames + 15;  // the harness run ends shortly after the result
        return;
    }
    // Not running (or finished): the start points trigger on entry.
    const sr3activity::ActivityPos p = PlayerPos(sess);
    int inside = -1, nearest = -1;
    double best = 0.0;
    for (size_t i = 0; i < sess.activityMarkers.size(); ++i) {
        const double d = sr3activity::Distance(p, sess.activityMarkers[i].start);
        if (sr3activity::AtActivityStart(p, sess.activityMarkers[i].start)) inside = static_cast<int>(i);
        if (nearest < 0 || d < best) {
            nearest = static_cast<int>(i);
            best = d;
        }
    }
    if (inside >= 0 && inside != sess.activityInsideMarker) {
        sess.activityInsideMarker = inside;
        const ActivityMarker m = sess.activityMarkers[static_cast<size_t>(inside)];
        std::printf("activity : entered the start point of %s ('%s') - starting\n", m.name.c_str(), m.warpName.c_str());
        sess.activityName = m.name;
        if (!BeginRun(sess)) std::printf("activity : %s could not be started\n", m.name.c_str());
        return;
    }
    sess.activityInsideMarker = inside;
    if (sess.activityDone) return;  // keep the result on the HUD
    char buf[128] = "";
    if (nearest >= 0 && best <= 250.0)
        std::snprintf(buf, sizeof buf, "ACTIVITY %s: start point %.0f m",
                      sess.activityMarkers[static_cast<size_t>(nearest)].name.c_str(), best);
    SetActivityLabel(sess, buf);
}

void drawActivityHud(PlaytestSession& sess, sr3luahost::EngineState& es, const HudDemoFont& font,
                     VintQuadPipeline& pipe, ID3D11DeviceContext* ctx) {
    const uint8_t white[4] = {255, 255, 255, 255};
    if (sess.hudCollectH) drawHudLabel(es, sess.hudCollectH, font, pipe, ctx, white);  // collectibles/challenges
    if (!sess.hudActivityH) return;
    drawHudLabel(es, sess.hudActivityH, font, pipe, ctx, white);
    auto bar = [&](double y0, double f, const uint8_t fg[4]) {
        const double x0 = 900, x1 = 1200;
        const uint8_t bg[4] = {30, 30, 30, 200};
        pipe.rect(ctx, x0, y0, x1, y0 + 10, bg);
        if (f > 0.0) pipe.rect(ctx, x0, y0, x0 + (x1 - x0) * (std::min)(f, 1.0), y0 + 10, fg);
    };
    const uint8_t gold[4] = {230, 190, 40, 255};
    const uint8_t blue[4] = {70, 150, 230, 255};
    if (sess.activityMeter >= 0.0) bar(102, sess.activityMeter, gold);   // progress (CHOSEN layout)
    if (sess.activityTimer >= 0.0) bar(116, sess.activityTimer, blue);  // time left
}

}  // namespace sr3viewer
