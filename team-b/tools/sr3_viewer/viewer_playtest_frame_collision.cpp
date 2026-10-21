// Playtest frame step (free-roam gap #8, 2026-10-18): the player's car vs pedestrians (run-over) and vs other cars.
//
// SPEC BASIS (clean-room; spec-*.md only). spec-physics-runtime.md Sec10a: vehicle-vs-vehicle and vehicle-vs-world
// collision DAMAGE share one handler (0x00763400): damage = round(0.0055 * impactMagnitude), applied to each vehicle
// side (implemented in sr3physics::VehicleCollisionDamage). The PHYSICAL response is Havok's own solver (no
// game-layer formula), coefficients OPEN. Vehicle-vs-pedestrian damage is NOT specced (the (7,5) handler is
// unverified). So, summarised:
//   SPECCED   : 0.0055 damage scale, zero-damage early return, both vehicles take damage, vehicle-vs-vehicle contact
//               uses the same machinery as vehicle-vs-world, lethal damage = the -1.0 kill sink (applyGuaranteedLethalHit).
//   CHOSEN    : every box size (one plan-view OBB per car instead of per-part hkpBoxShapes), ped footprint radius,
//               restitution 0.3, masses, no friction/yaw response, impactMagnitude := normal impulse (N*s), ped
//               knock/kill speed thresholds, knockdown duration, ambient-car slide deceleration and recovery delay,
//               the crime records reported (notoriety_escalation.h kChosenCivilian*).
// Detection and response are applied in the same frame (single-threaded harness; Sec10a's deferred queue is a
// multi-thread artefact), same stance as vehicle_world_collision.h.
//
// Runs after stepTrafficPeds and before stepNotoriety, so the real contact replaces the old CHOSEN "speed > 3 m/s and
// ped within 1.5 m" crime detection.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"
#include "sr3physics/vehicle_contact.h"

namespace sr3viewer {

namespace {
constexpr double kPedRadiusM = 0.35;               // CHOSEN ped plan-view footprint
constexpr double kAmbientCarHalfWidthM = 0.95;     // CHOSEN (half length = kNearFieldVehicleHalfLength)
constexpr double kAmbientCarMassKg = 1500.0;       // CHOSEN (ambient models carry no mass data)
constexpr double kRestitution = 0.3;               // CHOSEN (Sec10a: coefficient sourcing OPEN)
constexpr double kPlayerHalfWidthMarginM = 0.25;   // CHOSEN: half-track + margin
constexpr double kPlayerHalfLengthMarginM = 0.6;   // CHOSEN: same overhang as the world-collision sphere
constexpr double kSeparationSlopM = 0.02;
constexpr double kPedKnockdownMs = 2500.0;         // CHOSEN
constexpr double kPedReHitLockoutMs = 1500.0;      // CHOSEN
constexpr double kCarPairLockoutMs = 600.0;        // CHOSEN
constexpr double kMinContactSpeedMs = 0.5;         // CHOSEN
constexpr double kMinCarCarKnockSpeedMs = 0.5;     // CHOSEN

void bodyToWorld(const sr3physics::VehicleInstanceState& st, double& vx, double& vz) {
    const double c = std::cos(st.heading), s = std::sin(st.heading);
    vx = st.velForwardBody * c + st.velRightBody * s;
    vz = st.velForwardBody * s - st.velRightBody * c;
}
void worldToBody(sr3physics::VehicleInstanceState& st, double vx, double vz) {
    const double c = std::cos(st.heading), s = std::sin(st.heading);
    st.velForwardBody = vx * c + vz * s;
    st.velRightBody = vx * s - vz * c;
}
}  // namespace

void stepVehicleCollisions(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& stats = sess.vcStats;
    const double dtMs = frm.dt * 1000.0;
    const double nowMs = static_cast<double>(sess.nowMsAccum);

    // Hygiene: lockout timers tick down; knock state for despawned cars is dropped.
    for (auto it = sess.pedContactCooldownMs.begin(); it != sess.pedContactCooldownMs.end();) {
        it->second -= dtMs;
        it = it->second <= 0.0 ? sess.pedContactCooldownMs.erase(it) : std::next(it);
    }
    for (auto it = sess.carPairCooldownMs.begin(); it != sess.carPairCooldownMs.end();) {
        it->second -= dtMs;
        it = it->second <= 0.0 ? sess.carPairCooldownMs.erase(it) : std::next(it);
    }
    for (auto it = sess.vehicleKnock.begin(); it != sess.vehicleKnock.end();) {
        const bool live = std::any_of(sess.nearFieldLive.begin(), sess.nearFieldLive.end(),
                                      [id = it->first](const sr3airuntime::NearFieldVehicle& v) { return v.id == id; });
        it = live ? std::next(it) : sess.vehicleKnock.erase(it);
    }

    if (!sess.inVehicle || !sess.vehicleRig_ || !sess.vehicleRig_->sim || sess.craft.rig.active) return;  // AW-15: craft contacts not modelled
    auto& rig = *sess.vehicleRig_;
    auto& st = rig.sim->state_;

    // TEST-ONLY target injection (--script-collision-targets=FRAME), a test-harness convenience like --script-fire's
    // aim-at-nearest-ped: a scripted drive has no guarantee of meeting a random ped/car. Targets sit in the car's path;
    // the ped is frozen (knocked-until far future) and the car is a stationary "knocked" car so neither moves away.
    if (sess.scriptCollisionTargetsAt >= 0 && sess.frames >= sess.scriptCollisionTargetsAt) {
        sess.scriptCollisionTargetsAt = -1;
        const double fx = std::cos(st.heading), fz = std::sin(st.heading);
        auto frozenPed = [&](double dist, double lateral) {
            sr3airuntime::NearFieldPed p;
            p.id = sess.nearFieldPedNextId++;
            p.pos = sr3airuntime::Vec3{st.posX + fx * dist - fz * lateral, static_cast<double>(sess.vehicleGroundY),
                                       st.posZ + fz * dist + fx * lateral};
            sess.nearFieldPedsLive.push_back(p);
            sess.pedKnockedUntilMs[p.id] = nowMs + 1.0e9;
            std::printf("[vehicle-collision] TEST target ped id=%llu injected %.0f m ahead\n",
                        static_cast<unsigned long long>(p.id), dist);
        };
        frozenPed(12.0, 0.0);
        frozenPed(18.0, 1.5);
        sr3airuntime::NearFieldVehicle car;
        car.id = sess.nearFieldNextId++;
        car.pos = sr3airuntime::Vec3{st.posX + fx * 24.0 - fz * 3.4, static_cast<double>(sess.vehicleGroundY),
                                    st.posZ + fz * 24.0 + fx * 3.4};  // offset onto the scripted drive's curving path
        sess.nearFieldLive.push_back(car);
        sess.nearFieldVehicleHeading[car.id] = st.heading;
        sess.nearFieldVehicleSpeed[car.id] = 0.0;
        auto& k = sess.vehicleKnock[car.id];
        k.stoppedMs = -1.0e9;  // parked target: no recovery during the test
        std::printf("[vehicle-collision] TEST target car id=%llu injected 24 m ahead\n",
                    static_cast<unsigned long long>(car.id));
    }

    sr3physics::PlanObb player;
    player.cx = st.posX;
    player.cz = st.posZ;
    player.heading = st.heading;
    player.halfLength = 0.5 * rig.tuning.wheelBase + kPlayerHalfLengthMarginM;
    player.halfWidth = 0.5 * rig.tuning.trackWidth + kPlayerHalfWidthMarginM;
    const double playerMass = rig.tuning.mass;

    // ---- 1. Player car vs pedestrians ----
    for (auto& nfp : sess.nearFieldPedsLive) {
        if (sess.pedKilledByVehicle.count(nfp.id)) continue;
        const sr3physics::PlanContact c = sr3physics::ObbVsCircle(player, nfp.pos.x, nfp.pos.z, kPedRadiusM);
        if (!c.hit) continue;
        // Contact normal points car -> ped. Closing speed = the car's velocity component into the ped (ped treated as
        // stationary: its own walk speed is ~1.4 m/s, below the thresholds' resolution).
        double vx, vz;
        bodyToWorld(st, vx, vz);
        const double closing = vx * c.nx + vz * c.nz;
        // Always push the ped out of the box so it never sits inside the car.
        nfp.pos.x += c.nx * c.depth;
        nfp.pos.z += c.nz * c.depth;
        if (closing < kMinContactSpeedMs) continue;
        if (sess.pedContactCooldownMs.count(nfp.id)) continue;
        sess.pedContactCooldownMs[nfp.id] = kPedReHitLockoutMs;
        const sr3physics::PedHitOutcome outcome = sr3physics::ClassifyPedHit(closing);
        if (outcome == sr3physics::PedHitOutcome::kNone) continue;
        stats.maxImpactSpeed = std::max(stats.maxImpactSpeed, closing);
        sess.vehicleHitPedIds.insert(nfp.id);
        const double speedBefore = std::hypot(vx, vz);
        // Effect on the car: momentum lost to the (80 kg, CHOSEN) body.
        sr3physics::PlanBody car{playerMass, vx, vz};
        sr3physics::ApplyCarPedMomentumLoss(car, c.nx, c.nz);
        worldToBody(st, car.vx, car.vz);
        stats.speedBeforePedHit = std::max(stats.speedBeforePedHit, speedBefore);
        stats.minSpeedAfterPedHit = std::min(stats.minSpeedAfterPedHit, std::hypot(car.vx, car.vz));
        const char* what = "bump";
        if (outcome == sr3physics::PedHitOutcome::kBump) {
            ++stats.pedBumps;
        } else {
            nfp.downOrDead = true;  // Sec20.4 reaction gate input: a downed ped does not react
            sess.nearFieldPedFleeDestination.erase(nfp.id);
            notorietyReportCrime(sess, sr3airuntime::kChosenCivilianAssault, "vehicle hit ped");
            if (outcome == sr3physics::PedHitOutcome::kKill) {
                sess.pedKilledByVehicle.insert(nfp.id);
                sess.pedKnockedUntilMs.erase(nfp.id);
                if (sess.combatEs_p)
                    sess.combatEs_p->applyGuaranteedLethalHit("nearfieldped_" + std::to_string(nfp.id), "player");
                notorietyReportCrime(sess, sr3airuntime::kChosenCivilianKill, "vehicle killed ped");
                what = "KILL";
                ++stats.pedKills;
            } else {
                sess.pedKnockedUntilMs[nfp.id] = nowMs + kPedKnockdownMs;
                what = "knockdown";
                ++stats.pedKnockdowns;
            }
        }
        std::printf("[vehicle-collision] frame %d ped id=%llu closing=%.2f m/s outcome=%s car speed %.2f -> %.2f m/s\n",
                    sess.frames, static_cast<unsigned long long>(nfp.id), closing, what, speedBefore,
                    std::hypot(car.vx, car.vz));
    }

    // ---- 2. Player car vs ambient cars (lane followers) and parked cars ----
    auto collideWith = [&](const sr3physics::PlanObb& other, double otherMass, double ovx, double ovz,
                           uint64_t otherId, bool isStatic, double& outVx, double& outVz, double& outDx, double& outDz,
                           bool& contacted) {
        contacted = false;
        const sr3physics::PlanContact c = sr3physics::ObbVsObb(player, other);
        if (!c.hit) return;
        contacted = true;
        double pvx, pvz;
        bodyToWorld(st, pvx, pvz);
        sr3physics::PlanBody a{playerMass, pvx, pvz}, b{otherMass, ovx, ovz};
        const sr3physics::ImpulseResult r = sr3physics::ApplyNormalImpulse(a, b, c.nx, c.nz, kRestitution);
        double dAx, dAz, dBx, dBz;
        sr3physics::SeparateBodies(playerMass, otherMass, c.nx, c.nz, c.depth, kSeparationSlopM, dAx, dAz, dBx, dBz);
        st.posX += dAx;
        st.posZ += dAz;
        player.cx = st.posX;
        player.cz = st.posZ;
        outDx = dBx;
        outDz = dBz;
        outVx = ovx;
        outVz = ovz;
        if (!r.applied) return;
        worldToBody(st, a.vx, a.vz);
        outVx = b.vx;
        outVz = b.vz;
        if (sess.carPairCooldownMs.count(otherId) || r.closingSpeed < kMinContactSpeedMs) return;
        sess.carPairCooldownMs[otherId] = kCarPairLockoutMs;
        stats.maxImpactSpeed = std::max(stats.maxImpactSpeed, r.closingSpeed);
        if (isStatic) {
            ++stats.carStaticContacts;
        } else {
            ++stats.carCarContacts;
            stats.maxCarCarImpactSpeed = std::max(stats.maxCarCarImpactSpeed, r.closingSpeed);
        }
        // SPECCED damage skeleton (0.0055 scale, vehicle pair: both sides); impactMagnitude := j (CHOSEN).
        const int dmg = sr3physics::VehicleCollisionDamage(r.impulse, /*otherIsVehicle=*/true);
        sess.vehicleDamage[kAmbientRigCarId] += dmg;
        sess.vehicleDamage[otherId] += dmg;
        stats.damagePlayerTotal += dmg;
        stats.damageAmbientTotal += dmg;
        std::printf("[vehicle-collision] frame %d %s id=%llu closing=%.2f m/s impulse=%.0f Ns damage=%d each; player "
                    "speed -> %.2f m/s\n",
                    sess.frames, isStatic ? "car-vs-parked-car" : "car-vs-car", static_cast<unsigned long long>(otherId),
                    r.closingSpeed, r.impulse, dmg, std::hypot(a.vx, a.vz));
    };

    for (auto& nfv : sess.nearFieldLive) {
        sr3physics::PlanObb o;
        o.cx = nfv.pos.x;
        o.cz = nfv.pos.z;
        const auto hIt = sess.nearFieldVehicleHeading.find(nfv.id);
        o.heading = hIt != sess.nearFieldVehicleHeading.end() ? hIt->second : 0.0;
        o.halfLength = kNearFieldVehicleHalfLength;
        o.halfWidth = kAmbientCarHalfWidthM;
        double ovx = 0.0, ovz = 0.0;
        const auto knock = sess.vehicleKnock.find(nfv.id);
        const bool wasKnocked = knock != sess.vehicleKnock.end();
        if (wasKnocked) {
            ovx = knock->second.vx;
            ovz = knock->second.vz;
        } else {
            const auto sp = sess.nearFieldVehicleSpeed.find(nfv.id);
            const double spd = sp != sess.nearFieldVehicleSpeed.end() ? sp->second : 0.0;
            ovx = std::cos(o.heading) * spd;
            ovz = std::sin(o.heading) * spd;
        }
        double nvx = ovx, nvz = ovz, dBx = 0.0, dBz = 0.0;
        bool contacted = false;
        collideWith(o, kAmbientCarMassKg, ovx, ovz, nfv.id, false, nvx, nvz, dBx, dBz, contacted);
        if (!contacted) continue;
        nfv.pos.x += dBx;
        nfv.pos.z += dBz;
        // The ambient car leaves its lane follower: it slides with the post-impact velocity until it stops, then
        // rejoins (CHOSEN; see the knock branch in stepTrafficVehicles). Only when the impact actually changed its
        // velocity (or it is already knocked), so a graze does not derail a car for nothing.
        if (wasKnocked || std::hypot(nvx - ovx, nvz - ovz) >= kMinCarCarKnockSpeedMs) {
            auto& k = sess.vehicleKnock[nfv.id];
            k.vx = nvx;
            k.vz = nvz;
            k.stoppedMs = 0.0;
        }
    }
    for (const auto& pk : sess.parkedVehicles) {
        if (std::hypot(pk.x - st.posX, pk.z - st.posZ) < 1.0) continue;  // the driven car itself
        sr3physics::PlanObb o;
        o.cx = pk.x;
        o.cz = pk.z;
        o.heading = pk.heading;
        o.halfLength = kNearFieldVehicleHalfLength;
        o.halfWidth = kAmbientCarHalfWidthM;
        double nvx = 0.0, nvz = 0.0, dBx = 0.0, dBz = 0.0;
        bool contacted = false;
        collideWith(o, sr3physics::kImmovableMass, 0.0, 0.0, pk.id, true, nvx, nvz, dBx, dBz, contacted);
    }
}

void printVehicleCollisionTrace(const PlaytestSession& sess) {
    const auto& s = sess.vcStats;
    std::printf("vehicle-collision-trace: ped_hits=%llu (bumps=%llu knockdowns=%llu) ped_deaths=%llu "
                "car_car_contacts=%llu car_parked_contacts=%llu max_impact_speed=%.2f max_car_car_impact_speed=%.2f "
                "damage_player=%d damage_ambient=%d knocked_ambient_cars_now=%zu knock_recoveries=%llu "
                "max_player_speed_before_ped_hit=%.2f min_player_speed_after_ped_hit=%.2f crime_reports=%llu\n",
                static_cast<unsigned long long>(s.pedKnockdowns + s.pedBumps + s.pedKills),
                static_cast<unsigned long long>(s.pedBumps), static_cast<unsigned long long>(s.pedKnockdowns),
                static_cast<unsigned long long>(s.pedKills), static_cast<unsigned long long>(s.carCarContacts),
                static_cast<unsigned long long>(s.carStaticContacts), s.maxImpactSpeed, s.maxCarCarImpactSpeed,
                s.damagePlayerTotal, s.damageAmbientTotal, sess.vehicleKnock.size(),
                static_cast<unsigned long long>(s.knockRecoveries), s.speedBeforePedHit,
                s.minSpeedAfterPedHit > 1e8 ? 0.0 : s.minSpeedAfterPedHit,
                static_cast<unsigned long long>(sess.crimeReports));
}

}  // namespace sr3viewer
