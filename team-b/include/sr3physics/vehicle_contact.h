#pragma once

// sr3physics/vehicle_contact.h - pure plan-view (XZ) contact tests and responses for vehicle-vs-vehicle and
// vehicle-vs-pedestrian collision (free-roam gap #8, 2026-10-18). Header-only, no D3D11 / engine dependency.
//
// WHAT THE SPECS GIVE (spec-physics-runtime.md Sec10a, spec-vehicle-damage-runtime.md Sec3/Sec6a):
//   * Detection is the game's contact listener over per-part hkpBoxShapes (a box per vehicle part). Per-part extents
//     need the loaded model; this header takes ONE plan-view oriented box per vehicle (CHOSEN stand-in, see PlanObb).
//   * The PHYSICAL response is Havok's own solver ("no separate Volition-authored impulse/restitution formula at
//     the game layer"); restitution/friction sourcing is OPEN (Sec10a, 0x00769c30). So the impulse model below
//     (equal-and-opposite normal impulse, restitution e) is CHOSEN, labelled where used.
//   * Damage: vehicle-vs-vehicle and vehicle-vs-world share ONE handler, 0x00763400 (Sec10a, CONFIRMED):
//         scale = 0.0055 (default); damage = round(scale * impactMagnitude); damage == 0 -> return;
//         (vehicle-vs-vehicle) damage = round(objB[+0x1588] * damage); applied to each side that is a vehicle.
//     The impactMagnitude term is a geometry x material quantity (0x00762ab0) that Sec10a says is NOT the closing
//     speed and that this project cannot compute (the material "bounciness" lookup is unsourced). CHOSEN stand-in:
//     the contact normal impulse magnitude j (N*s). The 0.0055 scale, the zero-damage early return, the per-instance
//     scalar step and "both sides take damage" are the specced parts.
//   * Run-over of a PEDESTRIAN: no per-hit damage/knockdown rule for vehicle-vs-character is specced (the (7,5)
//     handler 0x007663e0 is unverified, Sec10a residual OPEN; type 5 = character is only a HYPOTHESIS). The
//     lethal outcome reuses the specced -1.0 lethal damage sink (spec-weapons-runtime.md Sec4.5); every threshold
//     below (PedHitThresholds) is CHOSEN.

#include <algorithm>
#include <cmath>

namespace sr3physics {

// Oriented box in the XZ plane. heading: (cos,sin) = forward in (X,Z), the vehicle_substeps.h convention.
// CHOSEN stand-in for the per-part hkpBoxShape set (Sec10a: whether parts combine into one compound is OPEN).
struct PlanObb {
    double cx = 0.0, cz = 0.0;
    double heading = 0.0;
    double halfLength = 2.0;  // along heading
    double halfWidth = 0.9;   // across heading
};

struct PlanContact {
    bool hit = false;
    double nx = 0.0, nz = 0.0;  // unit normal pointing from A toward B
    double depth = 0.0;         // penetration depth along the normal (>= 0)
    double px = 0.0, pz = 0.0;  // approximate contact point
};

namespace detail {
inline double Dot(double ax, double az, double bx, double bz) { return ax * bx + az * bz; }
// Projection radius of box `b` onto unit axis (ax,az).
inline double Radius(const PlanObb& b, double ax, double az) {
    const double c = std::cos(b.heading), s = std::sin(b.heading);
    return b.halfLength * std::fabs(Dot(ax, az, c, s)) + b.halfWidth * std::fabs(Dot(ax, az, -s, c));
}
}  // namespace detail

// Separating-axis test of two plan-view oriented boxes (4 candidate axes). On overlap returns the minimum-
// translation axis oriented from A to B.
inline PlanContact ObbVsObb(const PlanObb& a, const PlanObb& b) {
    PlanContact out;
    const double dx = b.cx - a.cx, dz = b.cz - a.cz;
    const double axes[4][2] = {{std::cos(a.heading), std::sin(a.heading)},
                               {-std::sin(a.heading), std::cos(a.heading)},
                               {std::cos(b.heading), std::sin(b.heading)},
                               {-std::sin(b.heading), std::cos(b.heading)}};
    double bestDepth = 1e300;
    double bestNx = 0.0, bestNz = 0.0;
    for (const auto& ax : axes) {
        const double dist = detail::Dot(dx, dz, ax[0], ax[1]);
        const double overlap = detail::Radius(a, ax[0], ax[1]) + detail::Radius(b, ax[0], ax[1]) - std::fabs(dist);
        if (overlap <= 0.0) return out;  // separating axis found
        if (overlap < bestDepth) {
            bestDepth = overlap;
            const double sgn = dist >= 0.0 ? 1.0 : -1.0;
            bestNx = ax[0] * sgn;
            bestNz = ax[1] * sgn;
        }
    }
    out.hit = true;
    out.nx = bestNx;
    out.nz = bestNz;
    out.depth = bestDepth;
    // Contact point: on A's surface along the normal, half-way into the overlap.
    const double reachA = detail::Radius(a, bestNx, bestNz);
    out.px = a.cx + bestNx * (reachA - 0.5 * bestDepth);
    out.pz = a.cz + bestNz * (reachA - 0.5 * bestDepth);
    return out;
}

// Box vs circle (a pedestrian's plan-view footprint). Normal points from the box toward the circle centre.
inline PlanContact ObbVsCircle(const PlanObb& box, double px, double pz, double radius) {
    PlanContact out;
    const double c = std::cos(box.heading), s = std::sin(box.heading);
    const double rx = px - box.cx, rz = pz - box.cz;
    const double lx = rx * c + rz * s;    // along heading
    const double lz = -rx * s + rz * c;   // across heading
    const double clx = std::clamp(lx, -box.halfLength, box.halfLength);
    const double clz = std::clamp(lz, -box.halfWidth, box.halfWidth);
    double nlx = lx - clx, nlz = lz - clz;
    double dist = std::sqrt(nlx * nlx + nlz * nlz);
    double depth;
    if (dist > 1e-9) {
        if (dist >= radius) return out;
        depth = radius - dist;
        nlx /= dist;
        nlz /= dist;
    } else {  // centre inside the box: leave through the nearest face
        const double dxFace = box.halfLength - std::fabs(lx), dzFace = box.halfWidth - std::fabs(lz);
        if (dxFace < dzFace) { nlx = lx >= 0 ? 1.0 : -1.0; nlz = 0.0; depth = dxFace + radius; }
        else { nlx = 0.0; nlz = lz >= 0 ? 1.0 : -1.0; depth = dzFace + radius; }
    }
    out.hit = true;
    out.depth = depth;
    out.nx = nlx * c - nlz * s;
    out.nz = nlx * s + nlz * c;
    out.px = px - out.nx * radius;
    out.pz = pz - out.nz * radius;
    return out;
}

// ---- impulse response -------------------------------------------------------------------------------------------

struct PlanBody {
    double mass = 1500.0;       // kg; >= kImmovableMass is treated as infinite
    double vx = 0.0, vz = 0.0;  // world-space plan velocity
};
constexpr double kImmovableMass = 1.0e8;

struct ImpulseResult {
    bool applied = false;
    double closingSpeed = 0.0;  // relative speed along the normal before the impulse (>= 0 when applied)
    double impulse = 0.0;       // normal impulse magnitude j (N*s)
};

// Equal-and-opposite normal impulse with restitution `e` (CHOSEN, Sec10a leaves coefficient sourcing OPEN). Only
// closing contacts (relative velocity into the normal) get an impulse. Normal points A -> B. No friction and no
// angular term (CHOSEN, same stance as vehicle_world_collision.h).
inline ImpulseResult ApplyNormalImpulse(PlanBody& a, PlanBody& b, double nx, double nz, double e) {
    ImpulseResult r;
    const double vn = detail::Dot(b.vx - a.vx, b.vz - a.vz, nx, nz);
    if (vn >= 0.0) return r;  // separating
    const double invA = a.mass >= kImmovableMass ? 0.0 : 1.0 / a.mass;
    const double invB = b.mass >= kImmovableMass ? 0.0 : 1.0 / b.mass;
    if (invA + invB <= 0.0) return r;
    const double j = -(1.0 + e) * vn / (invA + invB);
    a.vx -= j * invA * nx; a.vz -= j * invA * nz;
    b.vx += j * invB * nx; b.vz += j * invB * nz;
    r.applied = true;
    r.closingSpeed = -vn;
    r.impulse = j;
    return r;
}

// Positional separation: moves the bodies apart along the normal, split by inverse mass. `slop` leaves a tiny
// overlap so a resting contact does not flicker. Returns the displacement applied to A (B gets the opposite share).
inline void SeparateBodies(double massA, double massB, double nx, double nz, double depth, double slop,
                           double& dAx, double& dAz, double& dBx, double& dBz) {
    const double d = std::max(0.0, depth - slop);
    const double invA = massA >= kImmovableMass ? 0.0 : 1.0 / massA;
    const double invB = massB >= kImmovableMass ? 0.0 : 1.0 / massB;
    const double inv = invA + invB;
    if (inv <= 0.0) { dAx = dAz = dBx = dBz = 0.0; return; }
    dAx = -nx * d * invA / inv; dAz = -nz * d * invA / inv;
    dBx = nx * d * invB / inv;  dBz = nz * d * invB / inv;
}

// ---- damage ------------------------------------------------------------------------------------------------------

// Sec10a 0x00763400, the specced skeleton. `impactMagnitude` is the caller's stand-in (see header comment).
// scale 0.0055 CONFIRMED (0x01153c18). The vs-world special-surface scales 0.0825/0.0275 need two unidentified
// surface predicates and a speed state > 5.0 and are NOT implemented. `instanceScalar` is objB[+0x1588] (value
// source unknown; 1.0 default). Returns the integer damage (0 = early return, nothing applied).
constexpr double kVehicleCollisionDamageScale = 0.0055;
inline int VehicleCollisionDamage(double impactMagnitude, bool otherIsVehicle, double instanceScalar = 1.0) {
    int damage = static_cast<int>(std::lround(kVehicleCollisionDamageScale * impactMagnitude));
    if (damage == 0) return 0;
    if (otherIsVehicle) damage = static_cast<int>(std::lround(instanceScalar * static_cast<double>(damage)));
    return damage;
}

// ---- vehicle vs pedestrian ---------------------------------------------------------------------------------------

enum class PedHitOutcome { kNone, kBump, kKnockdown, kKill };

// ALL CHOSEN (no specced vehicle-vs-character rule): closing speed (m/s) between the car and the pedestrian along
// the contact normal.
struct PedHitThresholds {
    double minContactSpeed = 0.5;  // below: the car just touches the ped, nothing happens
    double knockdownSpeed = 2.0;   // >= : ped knocked down (frozen briefly), no engine damage
    double killSpeed = 6.0;        // >= : lethal (routes into the specced -1.0 lethal damage sink), ~13 mph
};

inline PedHitOutcome ClassifyPedHit(double closingSpeed, const PedHitThresholds& t = {}) {
    if (closingSpeed < t.minContactSpeed) return PedHitOutcome::kNone;
    if (closingSpeed < t.knockdownSpeed) return PedHitOutcome::kBump;
    if (closingSpeed < t.killSpeed) return PedHitOutcome::kKnockdown;
    return PedHitOutcome::kKill;
}

// Momentum cost to the car of hitting a pedestrian: a perfectly inelastic exchange with a stationary body of mass
// `pedMass` (CHOSEN 80 kg), applied along the contact normal (normal points car -> ped). Mutates the car velocity,
// returns the closing speed along the normal before the exchange (0 if separating).
inline double ApplyCarPedMomentumLoss(PlanBody& car, double nx, double nz, double pedMass = 80.0) {
    PlanBody ped;
    ped.mass = pedMass;
    ped.vx = ped.vz = 0.0;
    const ImpulseResult r = ApplyNormalImpulse(car, ped, nx, nz, /*e=*/0.0);
    return r.applied ? r.closingSpeed : 0.0;
}

}  // namespace sr3physics
