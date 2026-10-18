// Playtest frame step (task T1): player health, death and free-roam respawn skeleton.
//
// Spec basis: spec-player-runtime.md Sec15.3a (generic damage sink, health = clamp(health - damage, 0, max)),
// Sec15.4 (death trigger 0x00702810: respawn_delay_ms timer, controls-disabled flag, death camera mode 9),
// spec-tables-progression.md Sec10.2 (Death_and_busted.respawn_delay_ms).
//
// Design: damage goes through the EXISTING engine path (EngineState::applyCharacterDamage on the character
// "player", the same name the HUD and the weapon sink use); sr3player::PlayerVitals observes the resulting health and
// owns the death state, the respawn timer and the stats. No second damage model.
//
// FREE ROAM ONLY. With --mission the mission host owns player death (on_death callback -> script, Sec15.4's
// mission-failure chain), so this step does nothing and the mission path is unchanged.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

namespace {
constexpr int kDebugDamageFrame = 30;
// CHOSEN stand-ins, used only when the real source is unavailable (labelled in the log line that uses them):
constexpr int32_t kFallbackMaxHealth = 100;
constexpr int64_t kFallbackRespawnDelayMs = 4000;  // the shipped Death_and_busted.respawn_delay_ms value
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
        std::printf("vitals   : free-roam player vitals ON (max health %d from %s; respawn delay %lld ms from %s)\n",
                    cfg.maxHealth, maxSrc, static_cast<long long>(cfg.respawnDelayMs), delaySrc);
    }
    sr3player::PlayerVitals& v = *sess.vitals_;

    // Debug damage through the engine sink (same path an NPC hit takes: applyCharacterDamage on the player).
    if (sess.debugDamagePlayer > 0 && sess.frames == kDebugDamageFrame && !v.dead()) {
        std::printf("[vitals] frame %d DEBUG damage %d applied via EngineState::applyCharacterDamage\n", sess.frames,
                    sess.debugDamagePlayer);
        es.applyCharacterDamage("player", "", static_cast<float>(sess.debugDamagePlayer), 0u);
    }

    // Observe the engine's health (picks up debug damage and any other hit that reached the character).
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
            char line[160];
            std::snprintf(line, sizeof line, "damage frame=%d t=%lldms amount=%d health=%d/%d\n", sess.frames,
                          static_cast<long long>(nowMs), hpBefore - v.health(), v.health(), v.maxHealth());
            sess.vitalsTrace += line;
            std::printf("[vitals] %s", line);
        }
        if (died) {
            char line[160];
            std::snprintf(line, sizeof line, "death frame=%d t=%lldms controls_disabled=%d camera_mode=%d respawn_in=%lldms\n",
                          sess.frames, static_cast<long long>(nowMs), v.controlsDisabled() ? 1 : 0, v.cameraMode(),
                          static_cast<long long>(v.respawnDelayMs()));
            sess.vitalsTrace += line;
            std::printf("[vitals] %s", line);
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

        if (v.respawnDue(nowMs)) {
            // CHOSEN respawn destination: the playtest spawn point (--spawn, default start), at the current
            // ground height. Free-roam respawn destination is OPEN in spec-player-runtime.md Sec15.4.
            // CHOSEN: notoriety is left untouched (no spec says death clears it).
            if (sess.inVehicle) {  // CHOSEN: dying in a vehicle puts the player back on foot (vehicle stays put)
                sess.inVehicle = false;
                if (sess.radioPlayer) {
                    sess.radioLastStation = sess.radioPlayer->station() > 0 ? sess.radioPlayer->station() : 0xFF;
                    sess.radioPlayer->exitVehicle();
                }
            }
            pm.warp(sr3player::Vec3{static_cast<double>(sess.spawnX), static_cast<double>(sess.finalGroundY),
                                    static_cast<double>(sess.spawnZ)});
            sr3playeranim::DriveCameraTarget(followCam, pm);
            followCam.reset();
            // Reset the engine character's death state (the inverse of finalizeLethalCharacter).
            sr3luahost::CharacterState& c = es.getOrCreateCharacter("player");
            c.currentHitPoints.set(v.maxHealth());
            c.isDeadHighConfidence = false;
            c.lifeState.set(0);
            c.killFinalizedFlagE3.set(false);
            c.deathCallbackLatch = false;
            c.playerDeathTimerArmedAtSeconds = -1.0;
            c.controlsDisabled.set(false);
            v.respawn();
            pm.world.controlsDisabled.set(false);
            followCam.world.controlsDisabled.set(false);
            char line[200];
            std::snprintf(line, sizeof line, "respawn frame=%d t=%lldms at=(%.1f %.1f) health=%d/%d\n", sess.frames,
                          static_cast<long long>(nowMs), static_cast<double>(sess.spawnX), static_cast<double>(sess.spawnZ),
                          v.health(), v.maxHealth());
            sess.vitalsTrace += line;
            std::printf("[vitals] %s", line);
        }
    }
}

}  // namespace sr3viewer
