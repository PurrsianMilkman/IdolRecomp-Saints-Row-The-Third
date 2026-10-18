// Ambient-vehicle entry (free-roam): the pure selection / bookkeeping rules.
//
// Source: spec-player-runtime.md Sec13.3 (the auto-pick 0x00b105c0) and Sec13.4 (the per-candidate validator
// 0x00b0fdf0), Sec13.2 (the player always submits vehicle = 0 so the nearest-vehicle scan decides the vehicle).
//
//   CONFIRMED (Sec13.3):
//     - proximity alone decides the vehicle (the request's seat/mode fields never narrow the vehicle search; there
//       is NO facing/angle rule in the choice - the entry-angle class bits are computed later, Sec13.4 step 8);
//     - candidates beyond a fixed max range are rejected outright, squared-distance compared;
//     - the survivors go into a 2-slot array sorted closest first; a "sticky" candidate (the player's previously
//       tagged vehicle, player +0x1e30/+0x1e34) has its key forced to 0 so it sorts first;
//     - up to 2 candidates are validated in order; the first that validates wins; if the request's fast flag
//       (+0x23 bit 0) is set a failed candidate is retried ONCE before the next slot is tried;
//     - the validator is pure and its true return means "submittable", not "seat chosen" (Sec13.8 item 5).
//   CHOSEN (value not in any spec):
//     - the max-range constant at 0x01115204 has no numeric value in the specs; the caller passes a stand-in.
//     - distance is measured on the ground plane (X/Z) between the player and the car origin.
//
// Everything here is plain arithmetic so tests/ambient_vehicle_entry_test.cpp can cover it without D3D.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace sr3player {

// One thing the player could climb into. `eligible` is the caller's already-evaluated answer to the Sec13.4
// validator (deterministic per frame, so the single-retry rule below cannot change a result, but it is kept so the
// call pattern matches the spec). `occupied` = the driver seat holds an ambient driver (a carjack, Sec10).
struct AmbientEntryCandidate {
    uint64_t id = 0;
    double x = 0.0, z = 0.0;
    bool occupied = false;
    bool eligible = true;
};

struct AmbientEntryPick {
    int index = -1;       // index into the candidate vector, -1 = nothing
    double dist = 0.0;    // ground-plane distance to the pick
    bool carjack = false; // the picked car has an occupant in the driver seat
};

// 2-slot sorted pick. `maxRange` <= 0 means nothing qualifies. `stickyId` = 0 means no sticky candidate.
// `fastFlag` = request +0x23 bit 0 (sprint held): retry a failed validation of the same candidate once.
// `validate(candidate)` returns the Sec13.4 answer.
template <class Validate>
inline AmbientEntryPick PickAmbientEntryTarget(double px, double pz, const std::vector<AmbientEntryCandidate>& cands,
                                               double maxRange, uint64_t stickyId, bool fastFlag, Validate validate) {
    AmbientEntryPick none;
    if (maxRange <= 0.0) return none;
    const double maxSq = maxRange * maxRange;
    int slotIdx[2] = {-1, -1};
    double slotKey[2] = {0.0, 0.0};
    for (size_t i = 0; i < cands.size(); ++i) {
        const double dx = cands[i].x - px, dz = cands[i].z - pz;
        const double d2 = dx * dx + dz * dz;
        if (d2 > maxSq) continue;
        const double key = (stickyId != 0 && cands[i].id == stickyId) ? 0.0 : d2;
        // sorted insert, closest first; equal keys keep the earlier candidate first (stable)
        int pos = 2;
        if (slotIdx[0] < 0 || key < slotKey[0]) pos = 0;
        else if (slotIdx[1] < 0 || key < slotKey[1]) pos = 1;
        if (pos == 0) {
            slotIdx[1] = slotIdx[0];
            slotKey[1] = slotKey[0];
            slotIdx[0] = static_cast<int>(i);
            slotKey[0] = key;
        } else if (pos == 1) {
            slotIdx[1] = static_cast<int>(i);
            slotKey[1] = key;
        }
    }
    for (int s = 0; s < 2; ++s) {
        if (slotIdx[s] < 0) break;
        const AmbientEntryCandidate& c = cands[static_cast<size_t>(slotIdx[s])];
        bool ok = validate(c);
        if (!ok && fastFlag) ok = validate(c);  // Sec13.3 step 4: one retry of the same candidate
        if (ok) {
            AmbientEntryPick p;
            p.index = slotIdx[s];
            const double dx = c.x - px, dz = c.z - pz;
            p.dist = std::sqrt(dx * dx + dz * dz);
            p.carjack = c.occupied;
            return p;
        }
    }
    return none;
}

// Convenience: validator = the candidate's own `eligible` flag.
inline AmbientEntryPick PickAmbientEntryTarget(double px, double pz, const std::vector<AmbientEntryCandidate>& cands,
                                               double maxRange, uint64_t stickyId = 0, bool fastFlag = false) {
    return PickAmbientEntryTarget(px, pz, cands, maxRange, stickyId, fastFlag,
                                  [](const AmbientEntryCandidate& c) { return c.eligible; });
}

// Per-run entry/exit accounting for the headless `ambient-entry-trace:` line.
struct AmbientEntryStats {
    int entries = 0;       // every successful enter of a car that was not the original parked rig car
    int genkiEntries = 0;  // entries of the original parked rig car
    int carjacks = 0;      // entries that removed an ambient driver
    int exits = 0;
    double distanceDriven = 0.0;  // metres of ground-plane travel while driving
    int reentries = 0;     // entries of a car the player had driven and parked earlier

    void AddDrive(double dx, double dz) { distanceDriven += std::sqrt(dx * dx + dz * dz); }
};

}  // namespace sr3player
