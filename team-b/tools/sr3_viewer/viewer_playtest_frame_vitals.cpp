// Playtest frame step: player vitals (regeneration, death, fade, respawn point, penalty).
//
// Spec basis: spec-player-runtime.md Sec18 (2026-10-18) - Sec18.3 regeneration, Sec18.4 order of events, Sec18.5 respawn
// point selection, Sec18.6 what a respawn resets, Sec18.7 death penalty; Sec15.3a/Sec15.4 for the damage sink and the
// death trigger. Review PV-1..PV-9 (spec-conformance-review.md) are answered by Sec18.
//
// Design: damage goes through the EXISTING engine path (EngineState::applyCharacterDamage on the character "player",
// the same name the HUD and the weapon sink use). The Damage_Received_Mult (PV-6) and the regeneration wait re-arm
// live INSIDE that sink; the regeneration tick is EngineState::tickHealthRegen, called here once per frame (and by the
// mission frame loop for "#PLAYER1#"). sr3player::PlayerVitals observes the resulting health and owns the death state
// and the phase timers (4000 ms to on_death, 4000 ms fade to black, then the respawn).
//
// FREE ROAM ONLY. With --mission the mission host owns player death (on_death callback -> script, Sec15.4's
// mission-failure chain); the mission frame loop ticks the regen and the sink applies the received multiplier, but the
// death/respawn sequence below is not run (how mission death meets the respawn chain is OPEN in Sec18).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

namespace {
constexpr int kDebugDamageFrame = 30;
// CHOSEN stand-ins, used only when the real source is unavailable (labelled in the log line that uses them):
constexpr int32_t kFallbackMaxHealth = 100;
constexpr int64_t kFallbackRespawnDelayMs = 4000;  // the shipped Death_and_busted.respawn_delay_ms value
constexpr int kTutorialHintSmoked = 14;            // Sec18.7: tutorial_hint(14 "smoked", 2000)
constexpr int kTutorialHintBusted = 13;            // Sec18.7: tutorial_hint(13 "busted", 2000)
// Sec18.12 (FINAL data, HIGH ms): the fade back in is a 0.83 s left-to-right wipe (sr3player::kWipeInMs), not an alpha fade.
// CHOSEN: the free-roam viewer keeps the whole city resident, so the destination counts as loaded at fade-out completion.
constexpr double kPlayerHealthTierModifier[5] = {1.0, 1.25, 1.5, 1.75, 2.0};  // Sec18.14 Player_Health_1..4

void trace(PlaytestSession& sess, const char* line) {
    sess.vitalsTrace += line;
    std::printf("[vitals] %s", line);
}
}  // namespace

void stepPlayerVitals(PlaytestSession& sess, PlaytestFrame& frm) {
    if (sess.missionEnabled) return;  // mission mode: existing mission death path, untouched
    if (!sess.combatEs_p) return;
    sr3luahost::EngineState& es = *sess.combatEs_p;
    auto& pm = *sess.pm_;
    auto& followCam = *sess.followCam_;
    const int64_t nowMs = sess.nowMsAccum;

    if (!sess.vitalsInit) {
        sess.vitalsInit = true;
        sr3player::PlayerVitals::Config cfg;
        const char* maxSrc = "engine drawOrGetMaxHitPoints('player')";
        try {
            cfg.maxHealth = es.drawOrGetMaxHitPoints("player");
            es.drawOrGetCurrentHitPoints("player");  // lazy draw: starts at max (Sec34.2); the regen tick needs it known
        } catch (const std::exception&) {
            cfg.maxHealth = kFallbackMaxHealth;  // CHOSEN stand-in (max health draw unavailable)
            maxSrc = "CHOSEN fallback";
        }
        cfg.respawnDelayMs = kFallbackRespawnDelayMs;
        const char* delaySrc = "CHOSEN fallback 4000";
        try {
            const auto gc = sr3tables_progression::ParseGameplayConstants(sess.docOf("gameplay_constants.xtbl"));
            if (gc) {
                cfg.respawnDelayMs = gc->respawnDelayMs.value;
                delaySrc = "gameplay_constants.xtbl Death_and_busted.respawn_delay_ms";
            }
        } catch (const std::exception&) {
        }
        sess.vitals_.emplace(cfg);
        if (sess.debugPlayerCashDollars >= 0.0) es.playerCash() = sess.debugPlayerCashDollars * 100.0;
        const auto dv = sr3player::SpDifficultyVitals(es.difficultyLevel());
        std::printf("vitals   : free-roam player vitals ON (max health %d from %s; respawn delay %lld ms from %s; fade %lld ms)\n",
                    cfg.maxHealth, maxSrc, static_cast<long long>(cfg.respawnDelayMs), delaySrc,
                    static_cast<long long>(sess.vitals_->fadeMs()));
        std::printf("vitals   : Sec18 difficulty level %d (SP record): Damage_Received_Mult %.2f, Health_Regen_Wait_Mult %.2f, "
                    "Health_Regen_Speed_Mult %.2f; regen-speed unlock factor %.2f (Health_Regen_n unlocks are not exposed by "
                    "the progression state: 1.0); vampire regen (human shield) only if a shield is held; cash $%.2f\n",
                    es.difficultyLevel(), static_cast<double>(dv.damageReceivedMult),
                    static_cast<double>(dv.healthRegenWaitMult), static_cast<double>(dv.healthRegenSpeedMult),
                    es.healthRegenUnlockFactor, es.playerCash() / 100.0);
    }
    sr3player::PlayerVitals& v = *sess.vitals_;

    // Sec18.3 item 2: the regeneration tick (fixed 1/30 s steps inside), before this frame's damage so a hit on this
    // frame re-arms the wait first (the engine order is not specified beyond "per character update").
    es.tickHealthRegen(frm.dt);

    // Sec18.14: Player_Health_n unlock applied through the engine (debug trigger: the progression state does not expose it).
    if (sess.debugHealthTier >= 1 && sess.debugHealthTier <= 4 && sess.frames == 5) {
        const int32_t before = es.drawOrGetMaxHitPoints("player");
        const int32_t after = es.applyPlayerHealthBonus(kPlayerHealthTierModifier[sess.debugHealthTier]);
        char line[512];
        std::snprintf(line, sizeof line, "health_tier frame=%d Player_Health_%d modifier=%.2f max_hp %d -> %d (base + trunc(base*(M-1)); hp not healed)\n",
                      sess.frames, sess.debugHealthTier, kPlayerHealthTierModifier[sess.debugHealthTier], before, after);
        trace(sess, line);
        v.setMaxHealth(after);  // never heals; clamps current HP
    }

    // Sec18.10 arrest, opt-in (--enable-bust): stepPolice raises bustPending when the gates pass; the cuffing state ramps
    // +0xd48 at the fixed 1/30 s step and commits (CHOSEN: the cuff animation phase is proxied by the ramp, see below).
    if (sess.bustEnabled && !v.dead()) {
        if (sess.bustPending && !sess.bustCuffing) {
            sess.bustCuffing = true;
            sess.bustCuff = sr3player::CuffState{};
            sess.bustCuffCarry = 0.0;
            char line[512];
            std::snprintf(line, sizeof line, "bust cuffing begin frame=%d t=%lldms action_state (7,10), controls locked (+0x1d99 bit 0x40)\n",
                          sess.frames, static_cast<long long>(nowMs));
            trace(sess, line);
        }
        sess.bustPending = false;
        if (sess.bustCuffing) {
            pm.world.controlsDisabled.set(true);
            followCam.world.controlsDisabled.set(true);
            sess.moveSrc.A = 0.0f;
            sess.moveSrc.B = 0.0f;
            sess.moveSrc.sprintHeld = false;
            frm.fireKeyDownNow = false;
            sess.bustCuffCarry += frm.dt;
            while (sess.bustCuffCarry >= sr3player::kRegenStepSeconds - 1e-9) {
                sess.bustCuffCarry -= sr3player::kRegenStepSeconds;
                sess.bustCuff.tick();
            }
            // CHOSEN (Team A question): the animation phase of clip 0xdc (0x00956fd0) is not specified numerically, so the
            // ramp (0..1 over 1.5 s) stands in for it; commit at >= 0.75 as the spec says.
            if (sr3player::CuffState::Commit(sess.bustCuff.ramp)) {
                sess.bustCuffing = false;
                if (v.bust(nowMs)) {
                    ++sess.vitalsArrests;  // stat 11 "arrests" += 1 at the bust itself
                    pm.st.b1d94 |= sr3player::stance::kBusted;  // busted = +0x1d94 bit 0x02 (cleared at respawn step 16)
                    sess.vitalsOnDeathFired = false;
                    char line[512];
                    std::snprintf(line, sizeof line,
                                  "bust commit frame=%d t=%lldms ramp=%.3f ticks=%d arrests=%lld controls_disabled=1 camera_mode=%d "
                                  "on_death_in=%lldms then fade %lldms then respawn (hook slot 0xa: no Lua registration, nothing fires; "
                                  "mission failure \"You got busted.\" has no mission here)\n",
                                  sess.frames, static_cast<long long>(nowMs), sess.bustCuff.ramp, sess.bustCuff.ticks,
                                  static_cast<long long>(sess.vitalsArrests), sr3player::kDeathCameraMode,
                                  static_cast<long long>(v.respawnDelayMs()), static_cast<long long>(v.fadeMs()));
                    trace(sess, line);
                }
            }
        }
    } else {
        sess.bustPending = false;
    }

    // Debug damage through the engine sink (same path an NPC hit takes: applyCharacterDamage on the player).
    if (sess.debugDamagePlayer > 0 && sess.frames == kDebugDamageFrame && !v.dead()) {
        std::printf("[vitals] frame %d DEBUG damage %d (raw, before Damage_Received_Mult) applied via EngineState::applyCharacterDamage\n",
                    sess.frames, sess.debugDamagePlayer);
        es.applyCharacterDamage("player", "", static_cast<float>(sess.debugDamagePlayer), 0u);
    }

    // Observe the engine's health (picks up debug damage, regeneration and any other hit that reached the character).
    if (!v.dead()) {
        const uint64_t before = v.stats().damageEvents;
        const int32_t hpBefore = v.health();
        int32_t hp = hpBefore;
        try {
            hp = es.drawOrGetCurrentHitPoints("player");
        } catch (const std::exception&) {
        }
        const bool died = v.observeHealth(hp, nowMs);
        if (v.stats().damageEvents != before) {
            char line[512];
            std::snprintf(line, sizeof line, "damage frame=%d t=%lldms amount=%d health=%d/%d\n", sess.frames,
                          static_cast<long long>(nowMs), hpBefore - v.health(), v.health(), v.maxHealth());
            trace(sess, line);
            sess.vitalsRegenActive = false;  // a hit re-arms the wait: regeneration (if running) pauses
        } else if (!died && v.health() > hpBefore) {
            // Regeneration observed (health went up with no damage).
            char line[512];
            if (!sess.vitalsRegenActive) {
                sess.vitalsRegenActive = true;
                sess.vitalsLastRegenLogMs = nowMs;
                std::snprintf(line, sizeof line, "regen begin frame=%d t=%lldms health=%d/%d\n", sess.frames,
                              static_cast<long long>(nowMs), v.health(), v.maxHealth());
                trace(sess, line);
            } else if (nowMs - sess.vitalsLastRegenLogMs >= 1000 && v.health() < v.maxHealth()) {
                sess.vitalsLastRegenLogMs = nowMs;
                std::snprintf(line, sizeof line, "regen tick  frame=%d t=%lldms health=%d/%d\n", sess.frames,
                              static_cast<long long>(nowMs), v.health(), v.maxHealth());
                trace(sess, line);
            }
            if (v.health() >= v.maxHealth()) {
                sess.vitalsRegenActive = false;
                std::snprintf(line, sizeof line, "regen full  frame=%d t=%lldms health=%d/%d\n", sess.frames,
                              static_cast<long long>(nowMs), v.health(), v.maxHealth());
                trace(sess, line);
            }
        }
        if (died) {
            char line[512];  // was 200: the message is ~215 chars, so the trailing newline was truncated
            std::snprintf(line, sizeof line,
                          "death frame=%d t=%lldms controls_disabled=%d camera_mode=%d on_death_in=%lldms then fade %lldms then respawn "
                          "(HUD: Death Display + audio Player_Dies + hud_busted_fade_in(true,4.0,4.0): no backend, logged)\n",
                          sess.frames, static_cast<long long>(nowMs), v.controlsDisabled() ? 1 : 0, v.cameraMode(),
                          static_cast<long long>(v.respawnDelayMs()), static_cast<long long>(v.fadeMs()));
            trace(sess, line);
            sess.vitalsOnDeathFired = false;
            // The camera itself stays the follow camera: death camera mode 9 is only recorded (its behaviour,
            // 0x00569a60/0x00569c60, is not modelled here).
        }
    }

    if (v.dead()) {
        // Sec15.4: controls disabled (+0x1d99 bit 0x08) - the movement chain and the follow camera both read it.
        pm.world.controlsDisabled.set(true);
        followCam.world.controlsDisabled.set(true);
        es.getOrCreateCharacter("player").controlsDisabled.set(true);
        // The raw input seams are zeroed too (they are filled before this step).
        sess.moveSrc.A = 0.0f;
        sess.moveSrc.B = 0.0f;
        sess.moveSrc.sprintHeld = false;
        frm.fireKeyDownNow = false;
        frm.vehEDownNow = false;
        frm.vehInputLive = sr3physics::DriverInput{0.0, 0.0, 0.0};

        // Sec18.4 phase 2: respawn_delay_ms after the death, on_death fires (free roam: no script thread, so it
        // "returns" at once), then phase 3 starts the fade to black (respawn_delay_ms again).
        if (!sess.vitalsOnDeathFired && v.respawnDue(nowMs)) {
            sess.vitalsOnDeathFired = true;
            char line[512];
            std::snprintf(line, sizeof line, "on_death frame=%d t=%lldms (free roam: no script thread; fade-out starts now)\n",
                          sess.frames, static_cast<long long>(nowMs));
            trace(sess, line);
            v.beginFade(nowMs);
        }
        if (v.fading()) {
            sess.vitalsFadeAlpha = v.fadeAlpha(nowMs);  // no overlay facility in the free-roam renderer: value only
        }

        if (v.fadeComplete(nowMs)) {
            // ---- Sec18.4 phase 4 (0x00703840 phase 2 + 0x009e6f20): point, followers, penalty, revive ----
            sr3luahost::CharacterState& c = es.getOrCreateCharacter("player");
            const sr3player::Vec3 here = pm.st.position;
            // Sec18.5: nearest level_respawn point flagged `death` (flags = 1: died). The four shipped points come from
            // the spec table (the free-roam viewer has no zone-object scan of class 0x14). Island type T: no hood
            // data in the playtest -> -1 (the shipped hospitals are all on the mainland).
            size_t nPts = 0;
            const sr3player::RespawnPoint* pts = sr3player::ShippedRespawnPoints(nPts);
            // 0x00703840 passes flags = 2 when busted, else 1 (died). No shipped point carries `busted`, so a bust (and a
            // death in a prison/power-plant hood, not modelled here) lands on the player_start navpoint (Sec18.11).
            // R4F-2: the flags are read from the stored busted bit (+0x1d94 bit 0x02, set at the bust commit), not v.busted().
            const bool busted = (pm.st.b1d94 & sr3player::stance::kBusted) != 0;
            const sr3player::RespawnChoice choice = sr3player::ResolveRespawn(
                pts, nPts, here.x, here.y, here.z, busted ? sr3player::kRespawnFlagBusted : sr3player::kRespawnFlagDeath, -1);
            const sr3player::RespawnPoint* pick = choice.point;
            // Sec18.15 / 18.6 step 18: on_respawn(script name) fires at the END of the ordered resets (see the loop below).
            bool respawnHookFired = false;
            // Sec18.6 followers: dismiss every follower of the local player.
            size_t dismissed = 0;
            for (auto& kv : es.party().membersByLeader)
                if (sr3luahost::EngineState::isLocalPlayerName(kv.first)) {
                    dismissed += kv.second.size();
                    kv.second.clear();
                }
            // Sec18.7 penalty: Death_cost $500, capped at what the player has; cash is in cents.
            const int64_t cashBefore = static_cast<int64_t>(std::llround(es.playerCash()));
            const int64_t penalty = sr3player::DeathPenaltyCents(
                cashBefore, busted ? sr3player::kBustedCostDollars : sr3player::kDeathCostDollars);
            es.playerCash() = static_cast<double>(cashBefore - penalty);
            if (busted) sess.vitalsBailCents += penalty;  // stat 5 "bail money"
            else sess.vitalsHospitalBillsCents += penalty;  // stat 4 "hospital bills"
            const int hintId = busted ? kTutorialHintBusted : kTutorialHintSmoked;
            es.recordTutorialAdvance(hintId);  // tutorial_hint(14 "smoked" | 13 "busted", 2000)

            double destX, destY, destZ;
            const char* destName;
            if (pick) {
                destX = pick->x;
                destY = pick->y;
                destZ = pick->z;
                destName = pick->name;
            } else {
                destX = static_cast<double>(sess.spawnX);  // unreachable: ResolveRespawn always returns a point
                destY = static_cast<double>(sess.finalGroundY);
                destZ = static_cast<double>(sess.spawnZ);
                destName = "CHOSEN --spawn";
            }
            // Sec18.6 (R3 note, FINAL order): the 18 ordered revive steps of 0x009e6f20, applied in the spec's order via the
            // pure list sr3player::RespawnResetOrder (unit-tested). Steps with `modelled == false` are role-only in the spec
            // or have no backend in this viewer and are logged, not applied.
            //   health: +0x1cb8 := +0x1cac; ragdoll/downed cleared; crouch cleared (10); vehicle/seat exit (9); teleport (12);
            //   notoriety tracks 0-3 = 0 (14, single-player); controls on (15); busted = +0x1d94 bit 0x02 cleared (16);
            //   on_death latch + death timer cleared and the on_respawn hook (18).
            // Slot-0 inventory select (13) has no selection backend here; weapons are KEPT.
            int notorietyCleared = 0;
            std::string stepsLog;
            {
                size_t nSteps = 0;
                const sr3player::RespawnStepInfo* steps = sr3player::RespawnResetOrder(nSteps);
                for (size_t si = 0; si < nSteps; ++si) {
                    using RS = sr3player::RespawnStep;
                    switch (steps[si].step) {
                        case RS::HealthRestore:
                            c.currentHitPoints.set(v.maxHealth());
                            break;
                        case RS::RagdollStop:
                            if (c.ragdollStateEnumCd2.known()) c.ragdollStateEnumCd2.set(0);  // 0x009a1e10 (HIGH)
                            break;
                        case RS::DownedLeave:
                            c.downedDeadlineSeconds = -1.0;  // 0x00973700 (HIGH)
                            break;
                        case RS::VehicleSeatExit:
                            if (sess.inVehicle) {  // vehicle stays put; the player is back on foot
                                sess.inVehicle = false;
                                if (sess.radioPlayer) {
                                    sess.radioLastStation = sess.radioPlayer->station() > 0 ? sess.radioPlayer->station() : 0xFF;
                                    sess.radioPlayer->exitVehicle();
                                }
                            }
                            break;
                        case RS::CrouchClear:
                            pm.st.crouched = false;  // +0xe8 bit 0x20 (the bit 0x00985cd0 tests); step 10 lists nothing else
                            break;
                        case RS::Teleport:
                            // The point's quaternion is not applied: the walk/follow-camera heading is input driven.
                            pm.warp(sr3player::Vec3{destX, destY, destZ});
                            sr3playeranim::DriveCameraTarget(followCam, pm);
                            followCam.reset();
                            break;
                        case RS::Notoriety:
                            if (sess.notoriety_ && sess.debugNotoriety < 0) {
                                sess.notoriety_->ClearPending();
                                for (int t = 0; t < 4; ++t) {
                                    const auto track = static_cast<sr3airuntime::NotorietyTrack>(t);
                                    if (sess.notoriety_->level(track) != 0) ++notorietyCleared;
                                    sess.notoriety_->SetLevel(track, 0, nowMs);
                                }
                            }
                            break;
                        case RS::ControlsOn:
                            pm.world.controlsDisabled.set(false);
                            c.controlsDisabled.set(false);  // +0x1d99 bit 0x08
                            followCam.world.controlsDisabled.set(false);
                            break;
                        case RS::BustedClear:
                            pm.st.b1d94 &= static_cast<uint8_t>(~sr3player::stance::kBusted);  // +0x1d94 bit 0x02
                            pm.st.b1d96 &= static_cast<uint8_t>(~0x01);                         // +0x1d96 bit 0x01
                            break;
                        case RS::DeathLatchAndHook:
                            // CHOSEN (PV-7): lifeState / isDeadHighConfidence / killFinalizedFlagE3 / regen bookkeeping are our
                            // model of the finalizer's outputs; Sec18.6 does not name the call that clears them, so they are
                            // cleared here (inverse of the finalizer) so the host model reads alive again. Team A question.
                            c.healthRegenExactHp = -1.0;
                            c.healthRegenReadyAtSeconds = 0.0;
                            c.isDeadHighConfidence = false;
                            c.lifeState.set(0);
                            c.killFinalizedFlagE3.set(false);
                            c.deathCallbackLatch = false;               // +0xe8 bit 0x01
                            c.playerDeathTimerArmedAtSeconds = -1.0;    // +0x1ef4 := -1
                            v.respawn();
                            sess.vitalsOnDeathFired = false;
                            sess.vitalsRegenActive = false;
                            sess.bustCuffing = false;
                            sess.bustPending = false;
                            respawnHookFired = es.fireOnRespawn(es.hasCharacter("#PLAYER1#") ? "#PLAYER1#" : "player");
                            break;
                        default:
                            break;
                    }
                    if (!stepsLog.empty()) stepsLog += ',';
                    stepsLog += std::to_string(static_cast<int>(steps[si].step));
                    if (!steps[si].modelled) stepsLog += "(log)";
                }
            }
            // Sec18.12: the destination counts as loaded at once (CHOSEN, resident city): the style-0 wipe runs 833 ms.
            const sr3player::FadeInPlan fp = sr3player::PlanFadeIn(nowMs, nowMs);
            sess.vitalsFadeInStartMs = fp.wipeStartMs;
            sess.vitalsFadeInEndMs = fp.wipeEndMs;
            char line[1024];
            std::snprintf(line, sizeof line,
                          "respawn frame=%d t=%lldms kind=%s point=%s at=(%.4f %.4f %.4f) health=%d/%d followers_dismissed=%zu "
                          "penalty=$%.2f cash $%.2f -> $%.2f hospital_bills=$%.2f bail=$%.2f arrests=%lld hint=%d "
                          "notoriety_tracks_cleared=%d on_respawn_hook=%s camera_mode=0 (reset, snapped behind player) "
                          "weapons=kept (slot-0 select: no selection backend here, not applied) "
                          "(HUD hud_player_reset_complete/hud_hit_clear_all + audio Player_Respawned: no backend, logged) "
                          "fade-in wipe %lld..%lld ms (Sec18.12 style 0, 833 ms; destination resident: CHOSEN) reset_steps=%s\n",
                          sess.frames, static_cast<long long>(nowMs), busted ? "busted" : "death", destName, destX, destY, destZ,
                          v.health(), v.maxHealth(), dismissed, static_cast<double>(penalty) / 100.0,
                          static_cast<double>(cashBefore) / 100.0, es.playerCash() / 100.0,
                          static_cast<double>(sess.vitalsHospitalBillsCents) / 100.0,
                          static_cast<double>(sess.vitalsBailCents) / 100.0, static_cast<long long>(sess.vitalsArrests), hintId,
                          notorietyCleared, respawnHookFired ? "fired" : "none registered",
                          static_cast<long long>(sess.vitalsFadeInStartMs), static_cast<long long>(sess.vitalsFadeInEndMs), stepsLog.c_str());
            trace(sess, line);
            sess.vitalsFadeAlpha = 0.0;
        }
    }
    if (sess.vitalsFadeInEndMs >= 0 && nowMs >= sess.vitalsFadeInEndMs) {
        char line[512];
        std::snprintf(line, sizeof line, "fade-in complete frame=%d t=%lldms\n", sess.frames, static_cast<long long>(nowMs));
        trace(sess, line);
        sess.vitalsFadeInStartMs = sess.vitalsFadeInEndMs = -1;
    }
}

}  // namespace sr3viewer
