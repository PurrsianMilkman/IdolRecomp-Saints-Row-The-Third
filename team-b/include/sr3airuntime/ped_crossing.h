#pragma once
// sr3airuntime/ped_crossing.h - the pedestrian crossing gate of spec-ai-runtime.md Sec20.6.9(c) (navigator state bits of
// human +0xb68: bit 3 waiting at the curb, bit 4 committed to the crossing, bit 5 past the far side) on the closed test
// of Sec20.6.13(h) (TrafficSignalController::CrossingClosed). A crossing without a light is never closed (Sec20.6.9(c),
// Sec20.6.13(h): "319 crossings have no light and are always open"), so the caller passes closed = false for it.
//
// Spec rules (FINAL for the test, HIGH for the state machine):
//   * closed + committed (bit 4): keep crossing.            * closed + waiting (bit 3): keep waiting.
//   * closed otherwise: before the near edge of the crosswalk quad -> set waiting and STOP; between the edges -> set
//     committed and finish the crossing; past the far edge -> carry on.
//   * open: bits 3-5 cleared and the ped walks on.
// CHOSEN (the spec gives no geometry numbers): the crosswalk quad is the sidewalk edge's own segment A->B (the near/far
// edges are the planes through the two end nodes perpendicular to it) with half-width max(edge width, 3 m) / 2; the near
// edge is whichever end the ped approaches from; the stop point is 0.05 m short of that edge (the kerb line).
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "sr3airuntime/near_field_ped_population.h"

namespace sr3airuntime {

struct PedCrossingState {
    uint64_t edgeId = 0;  // the crossing edge this state belongs to (0 = none)
    bool waiting = false;    // bit 3
    bool committed = false;  // bit 4
    bool waitedBefore = false;  // diagnostic: this crossing started from a wait
    int dir = 0;  // +1 entered from A (leaves past B), -1 entered from B
};

constexpr double kPedCrossingKerbStopM = 0.05;   // CHOSEN stop point short of the near edge
constexpr double kPedCrossingMinWidthM = 3.0;    // CHOSEN lower bound of the crosswalk width

// Returns the position the ped actually moves to this tick (`to`, or the kerb point / `from` while it waits).
inline Vec3 PedCrossingGate(const Vec3& from, const Vec3& to, uint64_t edgeId, const Vec3& a, const Vec3& b, double width,
                            bool closed, PedCrossingState& st) {
    const double ex = b.x - a.x, ez = b.z - a.z;
    const double L = std::sqrt(ex * ex + ez * ez);
    if (L < 1e-6) return to;
    const double ux = ex / L, uz = ez / L;
    const double half = std::max(width, kPedCrossingMinWidthM) * 0.5;
    auto S = [&](const Vec3& p) { return (p.x - a.x) * ux + (p.z - a.z) * uz; };
    auto D = [&](const Vec3& p) { return std::fabs((p.x - a.x) * -uz + (p.z - a.z) * ux); };
    auto inside = [&](double s) { return s >= 0.0 && s <= L; };
    const double sf = S(from), sto = S(to);

    if (st.edgeId != 0 && st.edgeId != edgeId) return to;  // the ped's state belongs to another crossing

    if (!closed) {
        if (st.waiting) {  // light opened: the ped steps off the kerb
            st.waiting = false;
            st.committed = true;
        }
    } else if (st.waiting) {
        return from;  // bit 3: keep waiting
    } else if (!st.committed) {
        if (inside(sf) && D(from) <= half) {  // between the edges: finish the crossing
            st.edgeId = edgeId;
            st.committed = true;
            st.dir = (sto >= sf) ? 1 : -1;
            return to;
        }
        if (!inside(sf) && inside(sto)) {  // reaches the near edge this tick
            const double boundary = sf < 0.0 ? -kPedCrossingKerbStopM : L + kPedCrossingKerbStopM;
            const double t = std::clamp((boundary - sf) / (sto - sf), 0.0, 1.0);
            const Vec3 stop{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t};
            if (D(stop) <= half) {
                st.edgeId = edgeId;
                st.waiting = true;
                st.waitedBefore = true;
                st.dir = sf < 0.0 ? 1 : -1;
                return stop;
            }
        }
        return to;
    }

    if (st.committed) {
        if (st.dir == 0) st.dir = sf <= L * 0.5 ? 1 : -1;
        const bool past = st.dir > 0 ? sto > L : sto < 0.0;  // bit 5: past the far side
        if (past) st = PedCrossingState{};
    } else if (!st.waiting) {
        st = PedCrossingState{};
    }
    return to;
}

}  // namespace sr3airuntime
