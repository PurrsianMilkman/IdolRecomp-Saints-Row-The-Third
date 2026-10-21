// Synthetic tests for sr3physics/vehicle_contact.h (free-roam gap #8). See that header for what is specced vs CHOSEN.
#include <cmath>
#include <iostream>

#include "sr3physics/vehicle_contact.h"

using namespace sr3physics;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)
bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }
constexpr double kPi = 3.14159265358979323846;

void testObbOverlapAndSeparation() {
    PlanObb a{0, 0, 0, 2.0, 1.0};
    PlanObb b{3.5, 0, 0, 2.0, 1.0};  // 0.5 m overlap along X
    PlanContact c = ObbVsObb(a, b);
    CHECK(c.hit);
    CHECK(near(c.depth, 0.5));
    CHECK(near(c.nx, 1.0) && near(c.nz, 0.0));
    PlanObb far{4.5, 0, 0, 2.0, 1.0};
    CHECK(!ObbVsObb(a, far).hit);
    c = ObbVsObb(b, a);  // reversed order flips the normal
    CHECK(c.hit && near(c.nx, -1.0));
    PlanObb t{0, 2.5, kPi / 2, 2.0, 1.0};  // length axis along Z: spans z 0.5..4.5, overlaps a (z -1..1) by 0.5
    c = ObbVsObb(a, t);
    CHECK(c.hit && near(c.depth, 0.5) && near(c.nz, 1.0));
    PlanObb d{3.9, 2.9, kPi / 4, 2.0, 1.0};  // 45 deg box whose AABB overlaps but whose shape does not
    CHECK(!ObbVsObb(a, d).hit);
}

void testObbVsCircle() {
    PlanObb car{0, 0, 0, 2.0, 1.0};
    PlanContact c = ObbVsCircle(car, 2.2, 0.0, 0.35);  // nose, 0.15 m penetration
    CHECK(c.hit && near(c.depth, 0.15) && near(c.nx, 1.0));
    CHECK(!ObbVsCircle(car, 2.5, 0.0, 0.35).hit);
    c = ObbVsCircle(car, 0.0, 1.2, 0.35);  // side
    CHECK(c.hit && near(c.nz, 1.0) && near(c.depth, 0.15));
    c = ObbVsCircle(car, 1.9, 0.0, 0.35);  // centre inside: leaves via the nose
    CHECK(c.hit && near(c.nx, 1.0) && near(c.depth, 0.1 + 0.35));
    CHECK(!ObbVsCircle(car, 2.3, 1.3, 0.35).hit);  // beyond the corner
    PlanObb rot{10, 10, kPi / 2, 2.0, 1.0};         // forward = +Z
    c = ObbVsCircle(rot, 10, 12.2, 0.35);
    CHECK(c.hit && near(c.nz, 1.0) && near(c.nx, 0.0, 1e-9));
}

void testImpulse() {
    PlanBody a{1500, 10, 0}, b{1500, 0, 0};
    ImpulseResult r = ApplyNormalImpulse(a, b, 1, 0, 0.0);  // perfectly inelastic, equal mass
    CHECK(r.applied && near(r.closingSpeed, 10.0) && near(r.impulse, 1500.0 * 5.0));
    CHECK(near(a.vx, 5.0) && near(b.vx, 5.0));
    PlanBody c{1500, 10, 0}, d{1500, 0, 0};
    ApplyNormalImpulse(c, d, 1, 0, 1.0);  // elastic equal mass: velocities swap
    CHECK(near(c.vx, 0.0) && near(d.vx, 10.0));
    PlanBody e{1500, 0, 0}, f{1500, 5, 0};  // already separating along +n
    CHECK(!ApplyNormalImpulse(e, f, 1, 0, 0.3).applied);
    PlanBody g{1500, 12, 3}, h{3000, 1, -2};  // momentum conserved for unequal masses, restitution 0.3
    const double px0 = g.mass * g.vx + h.mass * h.vx, pz0 = g.mass * g.vz + h.mass * h.vz;
    CHECK(ApplyNormalImpulse(g, h, 1, 0, 0.3).applied);
    CHECK(near(g.mass * g.vx + h.mass * h.vx, px0, 1e-6) && near(g.mass * g.vz + h.mass * h.vz, pz0, 1e-6));
    CHECK(near(h.vx - g.vx, 0.3 * 11.0, 1e-6));  // restitution on the normal component
    PlanBody car{1500, 8, 0}, wall{kImmovableMass, 0, 0};  // immovable object: car stops, wall unchanged
    ApplyNormalImpulse(car, wall, 1, 0, 0.0);
    CHECK(near(car.vx, 0.0) && near(wall.vx, 0.0));
}

void testSeparate() {
    double ax, az, bx, bz;
    SeparateBodies(1500, 1500, 1, 0, 0.5, 0.0, ax, az, bx, bz);
    CHECK(near(ax, -0.25) && near(bx, 0.25) && near(az, 0.0));
    SeparateBodies(1500, kImmovableMass, 1, 0, 0.5, 0.0, ax, az, bx, bz);
    CHECK(near(bx, 0.0, 1e-4) && near(ax, -0.5, 1e-4));
    SeparateBodies(1500, 1500, 1, 0, 0.01, 0.02, ax, az, bx, bz);
    CHECK(ax == 0.0 && bx == 0.0);
}

void testDamage() {
    CHECK(VehicleCollisionDamage(50.0, true) == 0);          // 0.275 rounds to 0 -> early return
    CHECK(VehicleCollisionDamage(1000.0, true) == 6);        // 5.5 -> 6
    CHECK(VehicleCollisionDamage(1000.0, true, 2.0) == 12);  // instance scalar applied for a vehicle pair
    CHECK(VehicleCollisionDamage(1000.0, false, 2.0) == 6);  // vs-world: no scalar
}

void testPedClassify() {
    CHECK(ClassifyPedHit(0.2) == PedHitOutcome::kNone);
    CHECK(ClassifyPedHit(1.0) == PedHitOutcome::kBump);
    CHECK(ClassifyPedHit(3.0) == PedHitOutcome::kKnockdown);
    CHECK(ClassifyPedHit(8.0) == PedHitOutcome::kKill);
    PlanBody car{1500, 10, 0};
    const double cs = ApplyCarPedMomentumLoss(car, 1, 0);
    CHECK(near(cs, 10.0));
    CHECK(car.vx < 10.0 && car.vx > 9.0);  // ~5% loss at 80/1580
    PlanBody away{1500, -3, 0};
    CHECK(near(ApplyCarPedMomentumLoss(away, 1, 0), 0.0) && near(away.vx, -3.0));
}
}  // namespace

int main() {
    testObbOverlapAndSeparation();
    testObbVsCircle();
    testImpulse();
    testSeparate();
    testDamage();
    testPedClassify();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All synthetic_physics_vehicle_contact_test checks passed\n";
    return 0;
}
