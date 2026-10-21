// sr3player/character_collision.h's implementation. See that header's own
// top comment for sourcing (spec-physics-runtime.md Sec4.1/Sec4.6/Sec7,
// spec-player-runtime.md Sec5.6.1/Sec5.6.4/Sec5.6.5/Sec5.8/Sec5.11 item 5 -
// Sec5.6.5 closes OPEN 35, 2026-10-07 unit W-U2) and for exactly what is and
// is not spec-derived here.
#include "sr3player/character_collision.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sr3player {

bool SweepCircleVsAabbXZ(double x0, double z0, double x1, double z1, double radius, double minX, double maxX,
                          double minZ, double maxZ, double& outF, Vec3& outNormal) {
    // Minkowski-sum stand-in: expand the box by the circle's radius (see
    // this function's own header doc comment for the corner caveat).
    minX -= radius;
    maxX += radius;
    minZ -= radius;
    maxZ += radius;

    // Already inside the expanded box at the start of this sweep: CHOSEN to
    // report "no new contact" (see header comment) rather than a
    // degenerate zero-distance hit with no well-defined face to slide
    // along - the caller's own skin margin (CharacterProxyDriver::step())
    // is what is relied on to avoid this case in ordinary play.
    if (x0 > minX && x0 < maxX && z0 > minZ && z0 < maxZ) return false;

    const double dx = x1 - x0;
    const double dz = z1 - z0;
    double tmin = 0.0, tmax = 1.0;
    Vec3 normal{};
    bool haveNormal = false;

    // X slab.
    if (std::fabs(dx) < 1e-12) {
        if (x0 < minX || x0 > maxX) return false;
    } else {
        double t1 = (minX - x0) / dx, t2 = (maxX - x0) / dx;
        Vec3 n1{-1, 0, 0}, n2{1, 0, 0};
        if (t1 > t2) {
            std::swap(t1, t2);
            std::swap(n1, n2);
        }
        if (t1 > tmin) {
            tmin = t1;
            normal = n1;
            haveNormal = true;
        }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    // Z slab.
    if (std::fabs(dz) < 1e-12) {
        if (z0 < minZ || z0 > maxZ) return false;
    } else {
        double t1 = (minZ - z0) / dz, t2 = (maxZ - z0) / dz;
        Vec3 n1{0, 0, -1}, n2{0, 0, 1};
        if (t1 > t2) {
            std::swap(t1, t2);
            std::swap(n1, n2);
        }
        if (t1 > tmin) {
            tmin = t1;
            normal = n1;
            haveNormal = true;
        }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    if (!haveNormal) return false;  // the segment never actually touches a face within [0,1]
    if (tmin < 0.0 || tmin > 1.0) return false;
    outF = tmin;
    outNormal = normal;
    return true;
}

// See this function's own doc comment (character_collision.h) for the
// closed-form derivation: the flat span is linear in t (a perpendicular/
// cross-product distance from a linearly-moving point to a fixed line),
// each round end-cap is the standard swept-circle-vs-point quadratic.
bool SweepCircleVsSegmentXZ(double x0, double z0, double x1, double z1, double radius, double ax, double az,
                             double bx, double bz, double& outF, Vec3& outNormal) {
    const double dx = x1 - x0, dz = z1 - z0;
    const double sx = bx - ax, sz = bz - az;
    const double segLen = std::sqrt(sx * sx + sz * sz);

    double bestT = 1.0;
    Vec3 bestNormal{};
    bool found = false;
    auto consider = [&](double t, double nx, double nz) {
        if (t >= 0.0 && t <= 1.0 && t < bestT) {
            bestT = t;
            bestNormal = Vec3{nx, 0.0, nz};
            found = true;
        }
    };

    if (segLen > 1e-9) {
        const double ux = sx / segLen, uz = sz / segLen;
        // value(t) = perpendicular (2D cross-product) offset of point(t)
        // from the infinite line through A along u - EXACTLY linear in t,
        // since point(t) itself is linear and the cross product with a
        // FIXED direction is linear.
        const double c0 = (x0 - ax) * uz - (z0 - az) * ux;
        const double c1 = dx * uz - dz * ux;
        if (std::fabs(c1) > 1e-12) {
            double tEntry = -1.0;
            if (c0 > radius) tEntry = (radius - c0) / c1;        // approaching the +radius boundary
            else if (c0 < -radius) tEntry = (-radius - c0) / c1; // approaching the -radius boundary
            // else: |c0| <= radius already at t=0 - CHOSEN no-new-contact,
            // same convention as the already-inside case elsewhere in this
            // file; tEntry stays -1 and is rejected below.
            if (tEntry >= 0.0 && tEntry <= 1.0) {
                const double px = x0 + tEntry * dx, pz = z0 + tEntry * dz;
                const double s = (px - ax) * ux + (pz - az) * uz;  // projection coordinate along the segment
                if (s >= 0.0 && s <= segLen) {
                    const double footx = ax + s * ux, footz = az + s * uz;
                    double nx = px - footx, nz = pz - footz;
                    const double len = std::sqrt(nx * nx + nz * nz);
                    if (len > 1e-9) {
                        nx /= len;
                        nz /= len;
                        consider(tEntry, nx, nz);
                    }
                }
            }
        }
        // c1 == 0: the sweep runs parallel to the segment's own line, so the
        // perpendicular distance never changes - either already clear the
        // whole sweep, or already touching (CHOSEN no-op, same convention).
    }

    // Round end-caps: standard swept-circle-vs-point quadratic, restricted
    // to each cap's OWN region (outside the flat span, s<0 for A / s>segLen
    // for B) so the flat-span test above and the two cap tests partition
    // the stadium's boundary instead of double-reporting the same contact.
    auto capTest = [&](double vx, double vz, bool isA) {
        const double ex = x0 - vx, ez = z0 - vz;
        const double a = dx * dx + dz * dz;
        if (a < 1e-12) return;  // zero-length sweep - nothing to solve
        const double b = 2.0 * (ex * dx + ez * dz);
        const double c = ex * ex + ez * ez - radius * radius;
        if (c <= 0.0) return;  // already within radius of this vertex at t=0 - CHOSEN no-new-contact
        const double disc = b * b - 4.0 * a * c;
        if (disc < 0.0) return;
        const double sq = std::sqrt(disc);
        const double t = (-b - sq) / (2.0 * a);  // smaller root = first entry
        if (t < 0.0 || t > 1.0) return;
        const double px = x0 + t * dx, pz = z0 + t * dz;
        if (segLen > 1e-9) {
            const double ux = sx / segLen, uz = sz / segLen;
            const double s = (px - ax) * ux + (pz - az) * uz;
            // Only this cap's own region - otherwise the flat-span test
            // above already owns this contact.
            if (isA ? (s >= 0.0) : (s <= segLen)) return;
        }
        double nx = px - vx, nz = pz - vz;
        const double len = std::sqrt(nx * nx + nz * nz);
        if (len > 1e-9) {
            nx /= len;
            nz /= len;
            consider(t, nx, nz);
        }
    };
    capTest(ax, az, true);
    capTest(bx, bz, false);

    if (!found) return false;
    outF = bestT;
    outNormal = bestNormal;
    return true;
}

bool SweepCircleVsTriangleXZ(double x0, double z0, double x1, double z1, double radius, double ax, double az,
                              double bx, double bz, double cx, double cz, double& outF, Vec3& outNormal) {
    // "Already inside the 2D triangle at t=0": CHOSEN no-new-contact, same
    // convention as SweepCircleVsAabbXZ's own doc comment (a degenerate
    // zero-distance hit has no well-defined face to slide along) - standard
    // barycentric-sign containment test.
    auto sign = [](double px, double pz, double qx, double qz, double rx, double rz) {
        return (px - rx) * (qz - rz) - (qx - rx) * (pz - rz);
    };
    const double d1 = sign(x0, z0, ax, az, bx, bz);
    const double d2 = sign(x0, z0, bx, bz, cx, cz);
    const double d3 = sign(x0, z0, cx, cz, ax, az);
    const bool hasNeg = (d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0);
    const bool hasPos = (d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0);
    if (!(hasNeg && hasPos)) return false;  // inside (or exactly on an edge of) the triangle at t=0

    double bestF = 1.0;
    Vec3 bestNormal{};
    bool found = false;
    double f;
    Vec3 n;
    if (SweepCircleVsSegmentXZ(x0, z0, x1, z1, radius, ax, az, bx, bz, f, n) && f < bestF) {
        bestF = f;
        bestNormal = n;
        found = true;
    }
    if (SweepCircleVsSegmentXZ(x0, z0, x1, z1, radius, bx, bz, cx, cz, f, n) && f < bestF) {
        bestF = f;
        bestNormal = n;
        found = true;
    }
    if (SweepCircleVsSegmentXZ(x0, z0, x1, z1, radius, cx, cz, ax, az, f, n) && f < bestF) {
        bestF = f;
        bestNormal = n;
        found = true;
    }
    if (!found) return false;
    outF = bestF;
    outNormal = bestNormal;
    return true;
}

namespace {
// File-local only - not part of this header's public pure-geometry
// surface (character_collision.h declares only what camera.h/line_of_
// sight.h actually call). Plain 3D cross product; movement.h's Vec3
// deliberately carries only +, -, *scalar, Dot (this file's own house
// pattern, see sr3airuntime/common.h's identical note on why each unit
// defines just what it needs) so a cross product belongs here, not added
// to the shared Vec3 type for one caller.
Vec3 Cross3(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Same-side (2x) technique: P is assumed already on the triangle's plane
// (callers only ever use this after projecting onto it); inside iff P is on
// the same side of every edge, tested via the sign of each edge-cross-
// product's dot with the plane normal.
bool PointInTriangle3D(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& n) {
    const double e0 = Dot(Cross3(b - a, p - a), n);
    const double e1 = Dot(Cross3(c - b, p - b), n);
    const double e2 = Dot(Cross3(a - c, p - c), n);
    return (e0 >= 0.0 && e1 >= 0.0 && e2 >= 0.0) || (e0 <= 0.0 && e1 <= 0.0 && e2 <= 0.0);
}

// Shared by SweptSphereVsTriangle3D's 3 edge sub-tests: the earliest t in
// [0,1] (and < the caller's current bestT) where the moving point
// O + t*D sits exactly `radius` from the INFINITE line through (Pa,Pb),
// restricted to the touching point's projection actually landing within
// the real segment [Pa,Pb] (the two endpoints' own regions are each
// vertex's job, VertexSweepTest below, not this function's).
void EdgeSweepTest(const Vec3& O, const Vec3& D, double radius, const Vec3& Pa, const Vec3& Pb, double& bestT,
                    Vec3& bestNormal, bool& found) {
    const Vec3 E = Pb - Pa;
    const double elen = Length(E);
    if (elen < 1e-12) return;
    const Vec3 Ehat = E * (1.0 / elen);
    const Vec3 W0 = O - Pa;
    const double w0par = Dot(W0, Ehat);
    const Vec3 Wperp0 = W0 - Ehat * w0par;
    const double Dpar = Dot(D, Ehat);
    const Vec3 Dperp = D - Ehat * Dpar;

    const double a = Dot(Dperp, Dperp);
    if (a < 1e-12) return;  // sweeping exactly parallel to this edge: the perpendicular
                              // distance never changes over the sweep - nothing NEW to report
                              // (already-touching-at-t=0 is the caller's own early-out)
    const double b = 2.0 * Dot(Wperp0, Dperp);
    const double c = Dot(Wperp0, Wperp0) - radius * radius;
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return;
    const double sq = std::sqrt(disc);
    const double t = (-b - sq) / (2.0 * a);  // smaller root = first entry
    if (t < 0.0 || t >= bestT) return;
    const double wpar = w0par + t * Dpar;
    if (wpar < 0.0 || wpar > elen) return;  // touches beyond the real edge - a vertex's region, not this edge's
    const Vec3 center = O + D * t;
    const Vec3 onLine = Pa + Ehat * wpar;
    Vec3 n = center - onLine;
    const double nlen = Length(n);
    if (nlen < 1e-9) return;
    n = n * (1.0 / nlen);
    bestT = t;
    bestNormal = n;
    found = true;
}

// Shared by SweptSphereVsTriangle3D's 3 vertex sub-tests: standard swept-
// sphere-vs-point quadratic (same shape as SweepCircleVsSegmentXZ's own 2D
// cap test above, lifted to 3D).
void VertexSweepTest(const Vec3& O, const Vec3& D, double radius, const Vec3& V, double& bestT, Vec3& bestNormal,
                      bool& found) {
    const Vec3 R0 = O - V;
    const double a = Dot(D, D);
    if (a < 1e-12) return;
    const double b = 2.0 * Dot(R0, D);
    const double c = Dot(R0, R0) - radius * radius;
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return;
    const double sq = std::sqrt(disc);
    const double t = (-b - sq) / (2.0 * a);
    if (t < 0.0 || t >= bestT) return;
    const Vec3 center = O + D * t;
    Vec3 n = center - V;
    const double nlen = Length(n);
    if (nlen < 1e-9) return;
    n = n * (1.0 / nlen);
    bestT = t;
    bestNormal = n;
    found = true;
}
}  // namespace

Vec3 ClosestPointOnTriangle3D(double px, double py, double pz, double ax, double ay, double az, double bx,
                               double by, double bz, double cx, double cy, double cz) {
    const Vec3 p{px, py, pz}, a{ax, ay, az}, b{bx, by, bz}, c{cx, cy, cz};
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = Dot(ab, ap), d2 = Dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;  // vertex region A

    const Vec3 bp = p - b;
    const double d3 = Dot(ab, bp), d4 = Dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return b;  // vertex region B

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        const double v = d1 / (d1 - d3);
        return a + ab * v;  // edge AB
    }

    const Vec3 cp = p - c;
    const double d5 = Dot(ab, cp), d6 = Dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return c;  // vertex region C

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        const double w = d2 / (d2 - d6);
        return a + ac * w;  // edge AC
    }

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w;  // edge BC
    }

    const double denom = 1.0 / (va + vb + vc);
    const double v = vb * denom, w = vc * denom;
    return a + ab * v + ac * w;  // interior
}

bool RayVsTriangle3D(double x0, double y0, double z0, double x1, double y1, double z1, double ax, double ay,
                      double az, double bx, double by, double bz, double cx, double cy, double cz, double& outF,
                      Vec3& outNormal) {
    const Vec3 orig{x0, y0, z0};
    const Vec3 dir = Vec3{x1, y1, z1} - orig;  // spans the WHOLE [from,to] segment - outF is a fraction, not a distance
    const Vec3 A{ax, ay, az}, B{bx, by, bz}, C{cx, cy, cz};
    const Vec3 e1 = B - A, e2 = C - A;
    const Vec3 pvec = Cross3(dir, e2);
    const double det = Dot(e1, pvec);
    if (std::fabs(det) < 1e-12) return false;  // parallel to the triangle's own plane
    const double invDet = 1.0 / det;
    const Vec3 tvec = orig - A;
    const double u = Dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) return false;
    const Vec3 qvec = Cross3(tvec, e1);
    const double v = Dot(dir, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) return false;
    const double t = Dot(e2, qvec) * invDet;
    if (t < 0.0 || t > 1.0) return false;
    Vec3 n = Cross3(e1, e2);
    const double nlen = Length(n);
    if (nlen > 1e-12) n = n * (1.0 / nlen);
    if (Dot(n, dir) > 0.0) n = n * -1.0;  // face back toward the ray's own origin side
    outF = t;
    outNormal = n;
    return true;
}

bool SweptSphereVsTriangle3D(double x0, double y0, double z0, double x1, double y1, double z1, double radius,
                              double ax, double ay, double az, double bx, double by, double bz, double cx,
                              double cy, double cz, double& outF, Vec3& outNormal) {
    const Vec3 O{x0, y0, z0};
    const Vec3 D = Vec3{x1, y1, z1} - O;
    const Vec3 A{ax, ay, az}, B{bx, by, bz}, C{cx, cy, cz};

    // Already touching/overlapping the triangle at t=0: CHOSEN no-new-
    // contact (see this function's own header doc comment for the
    // convention this mirrors).
    {
        const Vec3 cp = ClosestPointOnTriangle3D(x0, y0, z0, ax, ay, az, bx, by, bz, cx, cy, cz);
        if (Length(O - cp) <= radius) return false;
    }

    double bestT = 1.0;
    Vec3 bestNormal{};
    bool found = false;

    // Face sub-test: offset half-plane (toward whichever side the sweep
    // starts on) intersection, accepted only if the touching point's
    // projection onto the real triangle plane actually lands inside the
    // triangle's own footprint (otherwise an edge or vertex sub-test below
    // owns that contact instead).
    {
        Vec3 n = Cross3(B - A, C - A);
        const double nlen = Length(n);
        if (nlen > 1e-12) {
            n = n * (1.0 / nlen);
            const double d0 = Dot(O - A, n);
            const double denom = Dot(D, n);
            if (std::fabs(denom) > 1e-12) {
                const double sign = (d0 >= 0.0) ? 1.0 : -1.0;
                const double t = (sign * radius - d0) / denom;
                if (t >= 0.0 && t < bestT) {
                    const Vec3 center = O + D * t;
                    const Vec3 contact = center - n * (sign * radius);
                    if (PointInTriangle3D(contact, A, B, C, n)) {
                        bestT = t;
                        bestNormal = n * sign;
                        found = true;
                    }
                }
            }
        }
    }

    EdgeSweepTest(O, D, radius, A, B, bestT, bestNormal, found);
    EdgeSweepTest(O, D, radius, B, C, bestT, bestNormal, found);
    EdgeSweepTest(O, D, radius, C, A, bestT, bestNormal, found);
    VertexSweepTest(O, D, radius, A, bestT, bestNormal, found);
    VertexSweepTest(O, D, radius, B, bestT, bestNormal, found);
    VertexSweepTest(O, D, radius, C, bestT, bestNormal, found);

    if (!found) return false;
    outF = bestT;
    outNormal = bestNormal;
    return true;
}

CharacterProxyDriver::CharacterProxyDriver(const ICollisionWorld& world, double radius, double height,
                                           double groundY)
    : world_(world), radius_(radius), height_(height), groundY_(groundY) {}

CapsuleStepResult CharacterProxyDriver::step(const Vec3& position, const Vec3& desiredVelocity, double dt) {
    CapsuleStepResult result;
    if (!(dt > 0.0)) {
        // 0x0099e7e0 does nothing when dt is exactly 0 (spec-player-
        // runtime.md Sec5.6.2) - this driver mirrors that for its own
        // vertical/horizontal integration too.
        result.position = position;
        result.velocity = desiredVelocity;
        result.grounded = position.y <= groundY_ + 1e-9;
        return result;
    }

    // --- vertical: grounded/falling binary (CHOSEN stand-in for the real
    // Havok support query, see this file's own header comment) + the now-
    // CONFIRMED gravity formula (spec-player-runtime.md Sec5.6.5, closes
    // OPEN 35): quadratic terminal-velocity falloff against the CONFIRMED
    // speed cap (controller+0x170 = 44.704, Sec5.6.1), scaled BEFORE this
    // frame's velocity += gravity*dt, using sinkSpeed from the INCOMING
    // (pre-integration) velocity exactly as the spec's own pseudocode order
    // states it.
    double y = position.y;
    double vy = desiredVelocity.y;
    const bool wasGrounded = y <= groundY_ + 1e-9;
    if (wasGrounded && vy <= 0.0) {
        y = groundY_;
        vy = 0.0;
    } else {
        const double sinkSpeed = -vy;  // -dot(velocity, up), up = (0,1,0)
        double gravity = kHavokWorldGravityY;
        if (sinkSpeed > 0.0) {
            const double ratio = sinkSpeed / terminalFallSpeed_;
            gravity *= (1.0 - ratio * ratio);
        }
        vy += gravity * dt;
    }
    double newY = y + vy * dt;
    bool grounded = false;
    if (newY <= groundY_) {
        newY = groundY_;
        vy = 0.0;
        grounded = true;
    }

    // --- horizontal: the REAL lock-step pass loop ------------------------
    // sr3physics::CharacterProxyStepper::run() - reused unmodified (this
    // header's own top comment). CharacterProxyListener models the queue-
    // only contract; a fresh listener per step() call is fine (its queues
    // start and end empty either way - MyCharacterListener's own queues are
    // drained every frame too, spec-player-runtime.md Sec5.8).
    auto keyEq = [](const WallHitRecord& a, const WallHitRecord& b) { return a.surfaceId == b.surfaceId; };
    auto merge = [](WallHitRecord& existing, const WallHitRecord& incoming) {
        // Running average, matching "merging duplicate (human, object)
        // pairs by a running average" (spec-physics-runtime.md Sec2.2 /
        // spec-player-runtime.md Sec5.8).
        ++existing.mergeCount;
        const double n = static_cast<double>(existing.mergeCount);
        existing.point = existing.point + (incoming.point - existing.point) * (1.0 / n);
        existing.normal = existing.normal + (incoming.normal - existing.normal) * (1.0 / n);
    };
    sr3physics::CharacterProxyListener<WallHitRecord, WallHitRecord> listener(keyEq, merge, keyEq, merge);

    Vec3 pos2d{position.x, 0.0, position.z};
    Vec3 hvel{desiredVelocity.x, 0.0, desiredVelocity.z};
    const double radius = radius_, height = height_, groundY = groundY_;
    const ICollisionWorld& world = world_;

    sr3physics::CharacterProxyStepper::SolvePass solve = [&](double remaining) -> double {
        const Vec3 from{pos2d.x, newY, pos2d.z};
        const Vec3 attempted = from + hvel * remaining;
        WallContact contact;
        if (!world.sweepCylinder(from, attempted, radius, groundY, height, contact)) {
            pos2d.x = attempted.x;
            pos2d.z = attempted.z;
            return remaining;  // open space: this pass consumes all remaining time (matches the
                                // "whether time remains" exhaustion test, spec-player-runtime.md Sec5.6.4 step 3)
        }
        const double f = std::clamp(contact.travelFraction, 0.0, 1.0);
        // Stop just short of the contact (CHOSEN 1 cm skin margin against
        // float-edge re-penetration on the next pass - no spec gives a skin
        // width for this stand-in).
        const double moveLen = Length(attempted - from);
        const double skinF = moveLen > 1e-9 ? std::min(f, 0.01 / moveLen) : 0.0;
        const double safeF = std::max(0.0, f - skinF);
        pos2d.x = from.x + (attempted.x - from.x) * safeF;
        pos2d.z = from.z + (attempted.z - from.z) * safeF;

        // Slide along the wall (remove the velocity component along its
        // normal) so a multi-pass corner genuinely resolves across
        // passes - the real reason Sec5.6.4 step 3's loop is multi-pass at
        // all ("every proxy asks whether time remains ... and if so joins
        // the next list"); the per-contact RESPONSE itself (as opposed to
        // the pass-loop shape) is not spec-derived - 0x007a5b10's own
        // contact handling is OPEN item 35.
        const Vec3 n{contact.normal.x, 0.0, contact.normal.z};
        const double vn = hvel.x * n.x + hvel.z * n.z;
        hvel.x -= vn * n.x;
        hvel.z -= vn * n.z;

        // Queue-only: push through MyCharacterListener's own stand-in,
        // never applied directly (this header's top comment / character_
        // proxy.h). The callback captures only the snapshot value it needs
        // to push - the same discipline tests/synthetic_physics_runtime_
        // test.cpp's own testListenerQueueOnlyContract() already exercises
        // for this listener type.
        const WallHitRecord record{contact.surfaceId, contact.point, contact.normal, 1};
        listener.onObjectInteraction = [record](sr3physics::EnqueueOnly<WallHitRecord>& q) { q.push(record); };
        listener.fireObjectInteraction();

        return remaining * safeF;  // only the travelled portion of this pass's time is spent;
                                    // the rest is left for the next pass (up to kMaxPasses)
    };
    const int passes = stepper_.run(dt, solve);

    // Flush OUTSIDE the pass loop - "flushed back into game state
    // afterward by ordinary game code" (spec-player-runtime.md Sec5.8,
    // CONFIRMED timing).
    listener.flush([&](const WallHitRecord& r) { result.hits.push_back(r); }, [](const WallHitRecord&) {});

    result.position = Vec3{pos2d.x, newY, pos2d.z};
    result.velocity = Vec3{hvel.x, vy, hvel.z};
    result.grounded = grounded;
    result.passesRun = passes;
    return result;
}

}  // namespace sr3player
