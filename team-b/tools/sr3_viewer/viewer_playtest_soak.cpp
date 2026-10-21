// --script-soak=SEED: deterministic "chaos" player for long stability runs (first-playable stability pass, TEAM B).
// Runs at the END of stepInput (so it overrides the scripted --no-input shape) and every frame:
//   * picks a random action (walk / sprint / strafe+turn / jump / crouch / fire / reload / weapon switch / ambient car
//     enter+drive+exit / police chase + roadblock / die+respawn / call a homie / heli or boat spawn+fly / warp across the
//     city / idle), holds it for a random number of frames, then picks the next;
//   * prints a "[soak] hb" heartbeat every soak.heartbeatEvery frames: frame, game time, player position, live actor
//     counts, working set + private bytes (MB), ms/frame since the previous heartbeat;
//   * aborts the process (exit 5) when private bytes pass SR3_SOAK_MEM_LIMIT_MB (default 5000) so a runaway run stops
//     before it can hurt the machine.
// The RNG is seeded from (seed, round) only, so a given seed reproduces the same action sequence (the game itself is
// deterministic under --no-input: fixed 1/30 s step).
// Everything here is a CHOSEN test driver, not an engine behaviour: no spec facts are encoded.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

#include <psapi.h>

#include <cstdio>
#include <cstdlib>

namespace sr3viewer {

namespace {

enum Act {
    kIdle, kWalk, kSprint, kStrafeTurn, kJump, kCrouch, kFire, kReload, kWeaponSwitch, kCar, kPolice, kRoadblock,
    kDie, kHomie, kCraft, kWarp, kLook, kActCount
};
const char* const kActNames[kActCount] = {"idle", "walk", "sprint", "strafe_turn", "jump", "crouch", "fire", "reload",
                                          "weapon_switch", "car", "police", "roadblock", "die", "homie", "craft", "warp",
                                          "look"};

struct Mem { double wsMB = 0, privMB = 0; };

Mem readMem() {
    Mem m;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        m.wsMB = pmc.WorkingSetSize / 1048576.0;
        m.privMB = pmc.PrivateUsage / 1048576.0;
    }
    return m;
}

int rnd(SoakState& s, int lo, int hi) {  // inclusive
    return lo + static_cast<int>(s.rng() % static_cast<uint32_t>(hi - lo + 1));
}
double rndf(SoakState& s, double lo, double hi) { return lo + (hi - lo) * ((s.rng() & 0xFFFFFF) / 16777215.0); }

long long qpcNow() {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

void heartbeat(PlaytestSession& sess) {
    SoakState& s = sess.soak;
    const Mem m = readMem();
    s.peakWsMB = (std::max)(s.peakWsMB, m.wsMB);
    s.peakPrivMB = (std::max)(s.peakPrivMB, m.privMB);
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    const long long now = qpcNow();
    double msPerFrame = 0.0;
    if (s.hbWallLast != 0 && sess.frames > s.hbFrameLast)
        msPerFrame = 1000.0 * static_cast<double>(now - s.hbWallLast) / static_cast<double>(f.QuadPart) /
                     (sess.frames - s.hbFrameLast);
    s.hbWallLast = now;
    s.hbFrameLast = sess.frames;
    int level = 0;
    if (sess.notoriety_) level = sess.notoriety_->level(sr3airuntime::NotorietyTrack::kPolice);
    const double gameSec = static_cast<double>(sess.nowMsAccum) / 1000.0;
    double px = 0, py = 0, pz = 0;
    if (sess.pm_) { px = sess.pm_->st.position.x; py = sess.pm_->st.position.y; pz = sess.pm_->st.position.z; }
    std::printf("[soak] hb round=%d frame=%d game=%.0fs tod=%.3f pos=(%.1f %.1f %.1f) veh=%d dead=%d wanted=%d "
                "actors{cars=%zu peds=%zu parked=%zu} act=%s ws=%.0fMB priv=%.0fMB ms/frame=%.2f\n",
                s.round, sess.frames, gameSec, sess.todFraction, px, py, pz, sess.inVehicle ? 1 : 0,
                (sess.vitals_ && sess.vitals_->dead()) ? 1 : 0, level, sess.nearFieldLive.size(),
                sess.nearFieldPedsLive.size(), sess.parkedVehicles.size(),
                s.actKind >= 0 ? kActNames[s.actKind] : "none", m.wsMB, m.privMB, msPerFrame);
    std::fflush(stdout);
    static const double limitMB = [] {
        const char* e = std::getenv("SR3_SOAK_MEM_LIMIT_MB");
        return e && *e ? std::atof(e) : 5000.0;
    }();
    if (m.privMB > limitMB || m.wsMB > limitMB) {
        std::printf("[soak] MEMORY LIMIT %.0f MB exceeded (ws %.0f priv %.0f) at frame %d - aborting this run (exit 5)\n",
                    limitMB, m.wsMB, m.privMB, sess.frames);
        std::fflush(stdout);
        ExitProcess(5);
    }
}

void pickAction(PlaytestSession& sess, int frames) {
    SoakState& s = sess.soak;
    // While a vehicle is occupied only the vehicle-compatible actions make sense; leaving needs an E press, which the
    // car/craft actions schedule themselves. If we are still in a vehicle here, force the exit first.
    int k = rnd(s, 0, 99);
    int kind;
    if (k < 8) kind = kIdle;
    else if (k < 22) kind = kWalk;
    else if (k < 30) kind = kSprint;
    else if (k < 38) kind = kStrafeTurn;
    else if (k < 44) kind = kJump;
    else if (k < 48) kind = kCrouch;
    else if (k < 56) kind = kFire;
    else if (k < 59) kind = kReload;
    else if (k < 63) kind = kWeaponSwitch;
    else if (k < 74) kind = kCar;
    else if (k < 80) kind = kPolice;
    else if (k < 83) kind = kRoadblock;
    else if (k < 86) kind = kDie;
    else if (k < 89) kind = kHomie;
    else if (k < 93) kind = kCraft;
    else if (k < 97) kind = kWarp;
    else kind = kLook;
    if (sess.inVehicle && kind != kCar && kind != kCraft) kind = kCar;  // keep driving until the car action exits it
    s.actKind = kind;
    s.actStart = frames;
    s.subPhase = 0;
    int len = rnd(s, 30, 240);
    switch (kind) {
        case kCar: {
            s.driveFrames = rnd(s, 90, 600);
            len = s.driveFrames + 80;
            if (!sess.inVehicle) {
                sess.scriptEnterAmbientAt = frames + 1;
                sess.scriptEnterAmbientCount = 1;
                sess.scriptEnterAmbientDriveFrames = s.driveFrames;
                sess.scriptEnterAmbientReenter = false;
            } else {
                len = s.driveFrames;  // already inside (previous craft/car): drive, then exit below
                sess.scriptEnterAmbientAt = -1;
            }
            s.driveNextChange = 0;
            break;
        }
        case kPolice:
            sess.debugNotorietyPoints = rnd(s, 50, 600);
            len = rnd(s, 200, 700);
            break;
        case kRoadblock: len = rnd(s, 150, 400); break;
        case kDie: len = 420; break;  // 4 s + 4 s fade at 30 fps, then respawn
        case kHomie: len = rnd(s, 120, 400); break;
        case kCraft: len = 40 + rnd(s, 200, 700) + 60; s.driveFrames = len - 100; break;
        default: break;
    }
    s.actEnd = frames + len;
    ++s.actionCounts[kind];
}

}  // namespace

void soakStep(PlaytestSession& sess, PlaytestFrame& frm) {
    SoakState& s = sess.soak;
    const int frames = sess.frames;
    if (!s.seeded) {
        s.seeded = true;
        s.rng.seed(s.seed * 2654435761u + static_cast<uint32_t>(s.round) * 40503u + 12345u);
        std::printf("[soak] active: seed=%u round=%d heartbeat every %d frames\n", s.seed, s.round, s.heartbeatEvery);
    }
    if (frames % s.heartbeatEvery == 0) heartbeat(sess);
    {   // Diagnostic (watchdog self-test): SR3_DIAG_HANG_AT_FRAME=N makes the loop hang at frame N of round 0.
        static const int hangAt = [] { const char* e = std::getenv("SR3_DIAG_HANG_AT_FRAME"); return e ? std::atoi(e) : -1; }();
        if (hangAt >= 0 && s.round == 0 && frames == hangAt) {
            std::printf("[soak] DIAG: hanging on purpose at frame %d (watchdog self-test)\n", frames);
            std::fflush(stdout);
            Sleep(INFINITE);
        }
    }
    if (!sess.pm_) return;
    auto& moveSrc = sess.moveSrc;
    auto& lookSrc = sess.lookSrc;
    moveSrc.A = moveSrc.B = 0.0f;
    moveSrc.sprintHeld = false;
    moveSrc.crouchHeld = false;
    moveSrc.crouchTogglePressed = false;
    lookSrc.rotateValue = lookSrc.elevateValue = 0.0f;
    frm.vehEDownNow = false;
    frm.roadblockKeyDownNow = false;
    frm.fireKeyDownNow = false;
    frm.reloadKeyDownNow = false;
    frm.weaponSelKeysDownNow = 0;
    sess.craft.liveControls = sr3viewer_craft::CraftControls{};

    if (s.actKind < 0 || frames >= s.actEnd) {
        // Leaving a vehicle at an action boundary: if still inside, press E once and wait a few frames.
        if (sess.inVehicle && s.actKind == kCar) {
            frm.vehEDownNow = ((frames - s.actEnd) % 20) == 0;
            if (frames - s.actEnd < 120) return;
        }
        if (sess.inVehicle && s.actKind == kCraft) {
            frm.vehEDownNow = ((frames - s.actEnd) % 20) == 0;
            if (frames - s.actEnd < 120) return;
        }
        pickAction(sess, frames);
    }
    const int rel = frames - s.actStart;
    const bool dead = sess.vitals_ && sess.vitals_->dead();
    if (dead && s.actKind != kDie) return;  // the death/respawn sequence owns the player
    switch (s.actKind) {
        case kIdle: break;
        case kWalk: moveSrc.B = 1.0f; break;
        case kSprint: moveSrc.B = 1.0f; moveSrc.sprintHeld = true; lookSrc.rotateValue = ((rel / 40) % 3 == 0) ? 0.03f : 0.0f; break;
        case kStrafeTurn:
            moveSrc.A = ((rel / 30) % 2) ? 0.7f : -0.7f;
            moveSrc.B = ((rel / 45) % 2) ? 0.5f : 0.0f;
            lookSrc.rotateValue = 0.04f * (((rel / 60) % 2) ? 1.0f : -1.0f);
            break;
        case kJump:
            moveSrc.B = 1.0f;
            if (rel % 25 == 0 && !sess.inVehicle && sess.jump_) sess.jump_->requestJump();
            break;
        case kCrouch: moveSrc.crouchHeld = true; moveSrc.B = (rel / 40) % 2 ? 0.5f : 0.0f; break;
        case kFire:
            lookSrc.rotateValue = 0.01f * static_cast<float>((rel / 50) % 3 - 1);
            lookSrc.elevateValue = 0.005f * static_cast<float>((rel / 70) % 3 - 1);
            frm.fireKeyDownNow = rel % 10 == 0;
            break;
        case kReload: frm.reloadKeyDownNow = rel == 5; frm.fireKeyDownNow = rel % 10 == 0 && rel > 20; break;
        case kWeaponSwitch:
            if (rel % 20 == 0) frm.weaponSelKeysDownNow |= 1u << ((rel / 20) % 11);
            frm.fireKeyDownNow = rel % 10 == 5;
            break;
        case kCar: {
            // E presses come from the scriptEnterAmbient* machinery (frame input step, noInput||soak). Drive inputs here.
            const int ambRel = frames - sess.scriptEnterAmbientAt;
            if (sess.scriptEnterAmbientAt >= 0) {
                if (ambRel == 1 || ambRel == 1 + sess.scriptEnterAmbientDriveFrames) frm.vehEDownNow = true;
            } else if (rel == s.driveFrames) {
                frm.vehEDownNow = true;  // pre-occupied: leave after the drive window
            }
            if (sess.inVehicle) {
                if (frames >= s.driveNextChange) {
                    s.driveNextChange = frames + rnd(s, 15, 70);
                    s.driveThrottle = rnd(s, 0, 9) < 7 ? rndf(s, 0.3, 1.0) : 0.0;
                    s.driveBrake = s.driveThrottle == 0.0 && rnd(s, 0, 1) ? rndf(s, 0.3, 1.0) : 0.0;
                    s.driveSteer = rnd(s, 0, 2) == 0 ? 0.0 : rndf(s, -0.6, 0.6);
                }
                frm.vehInputLive = sr3physics::DriverInput{s.driveThrottle, s.driveBrake, s.driveSteer};
            }
            break;
        }
        case kPolice:
            moveSrc.B = 1.0f;
            moveSrc.sprintHeld = (rel / 60) % 2 == 0;
            lookSrc.rotateValue = 0.02f * static_cast<float>((rel / 80) % 3 - 1);
            break;
        case kRoadblock:
            frm.roadblockKeyDownNow = rel == 3 || rel == s.actEnd - s.actStart - 3;
            moveSrc.B = 1.0f;
            break;
        case kDie:
            if (rel == 2 && sess.combatEs_p && sess.vitals_ && !sess.vitals_->dead()) {
                std::printf("[soak] frame %d: killing the player (applyCharacterDamage 9999)\n", frames);
                sess.combatEs_p->applyCharacterDamage("player", "", 9999.0f, 0u);
            }
            break;
        case kHomie:
            if (rel == 3 && !sess.noHomies && !sess.homies.rows.empty()) {
                const auto& r = sess.homies.rows[static_cast<size_t>(rnd(s, 0, static_cast<int>(sess.homies.rows.size()) - 1))];
                if (r.row.name) {
                    PlaytestSession::HomieCallRequest req;
                    req.name = *r.row.name;
                    req.frame = frames;
                    sess.homieCalls.push_back(req);
                    std::printf("[soak] frame %d: calling homie '%s'\n", frames, req.name.c_str());
                }
            }
            moveSrc.B = (rel / 50) % 2 ? 1.0f : 0.0f;
            break;
        case kCraft: {
            const char* names = std::getenv("SR3_SOAK_CRAFT");  // comma list of --spawn-vehicle names (heli, boat)
            if (rel == 1 && !sess.inVehicle) {
                std::vector<std::string> v;
                std::string all = names && *names ? names : "heli_standard_02,boat_speed01@water";
                size_t p = 0;
                while (p <= all.size() && !all.empty()) {
                    const size_t q = all.find(',', p);
                    v.push_back(all.substr(p, q == std::string::npos ? std::string::npos : q - p));
                    if (q == std::string::npos) break;
                    p = q + 1;
                }
                if (!v.empty()) {
                    s.craftName = v[static_cast<size_t>(rnd(s, 0, static_cast<int>(v.size()) - 1))];
                    sess.craft.spawnName = s.craftName;
                    sess.craft.spawnDone = false;
                    sess.craft.spawnWarped = false;
                    sess.craft.spawnAtFrame = frames;
                    std::printf("[soak] frame %d: spawning craft '%s'\n", frames, s.craftName.c_str());
                }
            }
            if (rel == 30 && !sess.inVehicle) frm.vehEDownNow = true;
            if (sess.inVehicle) {
                if (frames >= s.driveNextChange) {
                    s.driveNextChange = frames + rnd(s, 20, 80);
                    s.driveThrottle = rndf(s, -0.2, 1.0);
                    s.driveSteer = rndf(s, -1.0, 1.0);
                    s.driveBrake = rndf(s, 0.0, 1.0);
                }
                sr3viewer_craft::CraftControls& cl = sess.craft.liveControls;
                cl.fwd = s.driveThrottle;
                cl.right = s.driveSteer;
                cl.up = s.driveBrake > 0.5 ? 1.0 : 0.0;
                cl.down = s.driveBrake < 0.15 ? 1.0 : 0.0;
                cl.yawKeys = s.driveSteer * 0.5;
                frm.vehInputLive = sr3physics::DriverInput{s.driveThrottle > 0 ? s.driveThrottle : 0.0, 0.0, s.driveSteer * 0.5};
                if (rel == 30 + s.driveFrames) frm.vehEDownNow = true;
            }
            break;
        }
        case kWarp:
            if (rel == 2 && sess.playtestRoadGraph && !sess.playtestRoadGraph->segments.empty()) {
                const size_t sg = static_cast<size_t>(s.rng() % sess.playtestRoadGraph->segments.size());
                const auto& a = sess.playtestRoadGraph->SegA(sg);
                if (!sess.inVehicle) {
                    sess.pm_->warp(sr3player::Vec3{a.x, a.y + 1.0, a.z});
                    std::printf("[soak] frame %d: warp to (%.1f %.1f %.1f)\n", frames, static_cast<double>(a.x),
                                static_cast<double>(a.y), static_cast<double>(a.z));
                }
            }
            moveSrc.B = 1.0f;
            break;
        case kLook:
            lookSrc.rotateValue = 0.05f * static_cast<float>((rel / 20) % 3 - 1);
            lookSrc.elevateValue = 0.03f * static_cast<float>((rel / 25) % 3 - 1);
            break;
        default: break;
    }
}

}  // namespace sr3viewer
