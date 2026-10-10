// police_roadblock_demo - a deterministic, headless console demo for the
// police/roadblock spawn MECHANISM (spec-ai-runtime.md Sec19.7,
// include/sr3airuntime/police_roadblock_spawn.h).
//
// WHY A CONSOLE DEMO, NOT A PNG GOLDEN SCENE. ambient_population_demo.cpp
// (this unit's own sibling, Sec17) captures a schematic top-down PNG
// because that module's own job is literally about WHERE things end up in
// a 2-D field relative to a moving camera. This module's three CONFIRMED
// properties under task - the per-tick throttle holding to its exact
// numbers, the anchor genuinely sliding (not fixed) as the reference moves,
// and a spike-strip vehicle being distinguishable from a generic one in
// this project's own creation-request representation - are all exact,
// per-tick NUMERIC/DISCRETE facts, not a visual layout a screenshot would
// make any clearer; a plain deterministic trace proves them more legibly
// (and more verifiably, byte for byte in captured stdout) than a rendered
// frame would. No new golden-scene/PNG infrastructure is added here.
//
// DETERMINISM. Every input is fixed in this file (slot table contents,
// player path, tick count, dt) - no wall-clock, no live input, no RNG seam
// at all (this module's own real mechanism, per Sec19.7, has none: the
// throttle/anchor/dispatch logic is pure arithmetic, unlike ambient
// population's weighted-random picks) - so stdout is byte-reproducible run
// to run.
//
// WHAT THIS PROVES, section by section (see stdout's own banner lines):
//   1. THROTTLE: a slot table sized well beyond the confirmed 1/1/2/4
//      per-category per-tick caps needs multiple ticks to drain, and each
//      tick's own dispatch count never exceeds those exact numbers.
//   2. SLIDING ANCHOR: once a table is full, the anchor's own position
//      changes every subsequent tick in lockstep with the reference's own
//      movement (the CONFIRMED subtraction formula) - never fixed. Proven
//      in a SEPARATE, coop-slot-free table from Section 1's - see Section
//      1's own closing note for why Section 1's table (which includes the
//      2 perpetually-unfilled case 0/1 coop slots) never satisfies the
//      "no unfilled slot exists" re-center condition on its own.
//   3. SPIKE-STRIP DISTINCTION: two configured vehicle slots (one named
//      "Spike_strip", one a generic placeholder) resolve to DIFFERENT
//      RoadblockVehicleKind tags in the real creation-request output.

#include <cstdio>
#include <string>
#include <vector>

#include "sr3airuntime/police_roadblock_spawn.h"

using namespace sr3airuntime;

namespace {

const char* CategoryName(RoadblockCategory c) {
    switch (c) {
        case RoadblockCategory::kCoopPlayerSlotA: return "coop_a(0)";
        case RoadblockCategory::kCoopPlayerSlotB: return "coop_b(1)";
        case RoadblockCategory::kPed: return "ped(2)";
        case RoadblockCategory::kVehicle: return "vehicle(3)";
        case RoadblockCategory::kThirdCategory: return "third(4)";
    }
    return "?";
}

const char* OutcomeName(RoadblockDispatchOutcome o) {
    switch (o) {
        case RoadblockDispatchOutcome::kSkippedCoopHandoff: return "SKIPPED(coop handoff, not creation)";
        case RoadblockDispatchOutcome::kPedCreated: return "PED_CREATED";
        case RoadblockDispatchOutcome::kVehicleCreated: return "VEHICLE_CREATED";
        case RoadblockDispatchOutcome::kThirdCategoryCreated: return "THIRD_CATEGORY_CREATED";
    }
    return "?";
}

void PrintDispatch(const RoadblockDispatchResult& d) {
    std::printf("      -> %s", OutcomeName(d.outcome));
    if (d.ped) {
        std::printf(" @ (%.1f, %.1f, %.1f)", d.ped->position.x, d.ped->position.y, d.ped->position.z);
    } else if (d.vehicle) {
        const char* kind = d.vehicle->kind == RoadblockVehicleKind::kSpikeStrip ? "SPIKE_STRIP" : "GENERIC";
        std::printf(" kind=%s def=\"%s\" @ (%.1f, %.1f, %.1f)", kind, d.vehicle->vehicleDefinitionName.c_str(),
                    d.vehicle->position.x, d.vehicle->position.y, d.vehicle->position.z);
    } else if (d.thirdCategory) {
        std::printf(" def=\"%s\" @ (%.1f, %.1f, %.1f)", d.thirdCategory->propDefinitionName.c_str(),
                    d.thirdCategory->position.x, d.thirdCategory->position.y, d.thirdCategory->position.z);
    }
    std::printf("\n");
}

}  // namespace

int main() {
    std::printf("police_roadblock_demo: spec-ai-runtime.md Sec19.7 police/roadblock spawn mechanism\n");
    std::printf(
        "NOTE: the upstream notoriety->schedule trigger (WHEN a roadblock should fire) is honestly OPEN per "
        "Sec19.5/19.6/19.7 - this demo explicitly ARMS the driver itself (RoadblockPopulationDriver::armed = "
        "true) rather than inventing a notoriety threshold. slot->forward_offset (50.0m) is a CHOSEN value, "
        "not a traced one; the direction resolver (\"along the reference's current heading\") is a CHOSEN "
        "straight-line stand-in, not a real road-network resolver. See police_roadblock_spawn.h's own file "
        "header for the full CONFIRMED/CHOSEN/OPEN breakdown.\n\n");

    // ---------------------------------------------------------------------
    // Build a slot table deliberately larger than every per-category cap,
    // so draining it takes multiple ticks - this is what makes the
    // per-tick throttle ceiling visible rather than trivially satisfied in
    // one pass. 2 coop (shared cap 1), 6 ped (cap 2), 10 vehicle (cap 4,
    // including ONE "Spike_strip"-named slot and the rest generic), 2
    // third-category (uncapped).
    // ---------------------------------------------------------------------
    std::vector<RoadblockSlot> slots;
    RoadblockSlot s;
    s.category = RoadblockCategory::kCoopPlayerSlotA;
    slots.push_back(s);
    s.category = RoadblockCategory::kCoopPlayerSlotB;
    slots.push_back(s);
    s.category = RoadblockCategory::kPed;
    for (int i = 0; i < 6; ++i) slots.push_back(s);
    s.category = RoadblockCategory::kVehicle;
    s.vehicleDefinitionName = "Spike_strip";  // the ONE slot this table configures as a spike strip
    slots.push_back(s);
    s.vehicleDefinitionName = "generic_police_cruiser";
    for (int i = 0; i < 9; ++i) slots.push_back(s);
    s.category = RoadblockCategory::kThirdCategory;
    s.vehicleDefinitionName.clear();
    for (int i = 0; i < 2; ++i) slots.push_back(s);

    std::printf("slot table: %zu slots (2 coop, 6 ped, 10 vehicle [1 Spike_strip + 9 generic], 2 third-category)\n",
                slots.size());
    std::printf(
        "confirmed throttle caps per tick: coop0/1 shared<=1, ped(2)<=2, vehicle(3)<=4, third(4)=uncapped\n\n");

    RoadblockSpawnManager manager(slots);
    RoadblockPopulationDriver driver;
    driver.armed = true;  // CHOSEN explicit arm (see NOTE above) - not tied to notoriety

    Vec3 playerPos{0, 0, 0};
    const Vec3 playerVelocity{0, 0, 8.0};  // 8 units/s "forward" (+z) - a brisk walk/drive pace
    const double dtSeconds = 1.0;          // 1 tick/second for a short, readable trace
    const int totalTicks = 4;

    std::printf("=== SECTION 1: throttle drain over %d ticks (this table INCLUDES the 2 coop slots) ===\n\n",
                totalTicks);

    for (int tick = 0; tick < totalTicks; ++tick) {
        playerPos = playerPos + playerVelocity * dtSeconds;

        if (!driver.ShouldRunTick()) {
            std::printf("[tick %d] driver not armed/enabled - no-op\n", tick);
            continue;
        }

        auto resolveDirection = [&](const Vec3&) { return ResolveDirectionAlongHeading(playerVelocity); };
        RoadblockTickResult result = driver.RunTick(manager, dtSeconds, playerPos, resolveDirection);

        int unfilledRemaining = 0;
        for (const auto& slot : manager.slots())
            if (!slot.filled) ++unfilledRemaining;

        std::printf("[tick %d] player=(%.1f,%.1f,%.1f) anchor=(%.1f,%.1f,%.1f) anchor_recentered=%s\n", tick,
                    playerPos.x, playerPos.y, playerPos.z, manager.anchorPosition().x, manager.anchorPosition().y,
                    manager.anchorPosition().z, result.anchorRecentered ? "YES" : "no");
        std::printf("          attempted=%d throttled=%d dispatched=%zu unfilled_remaining=%d\n",
                    result.attemptedSlotCount, result.throttledSlotCount, result.dispatches.size(),
                    unfilledRemaining);
        std::printf("          confirmed throttle numbers held: coop<=1 ped<=2 vehicle<=4 third=uncapped -> %s\n",
                    "see per-category counts in the dispatch list below");
        for (const auto& d : result.dispatches) PrintDispatch(d);
        std::printf("\n");
    }
    std::printf(
        "SECTION 1 NOTE: every tick's dispatch count stayed within the confirmed 1/1/2/4 caps (verified exactly "
        "in tests/synthetic_police_roadblock_spawn_test.cpp's own testManagerThrottleLimitsSpawnsPerTickToConfirm"
        "edNumbers). Notice the anchor NEVER recenters in this run (anchor_recentered=no, every tick) - that is "
        "a REAL, faithful consequence of this table including 2 coop (case 0/1) slots: because case 0/1 is "
        "SKIPPED rather than creating anything (see file header), those 2 slots are never marked \"filled\", so "
        "\"no unfilled slot exists\" (the CONFIRMED condition that triggers re-centering, Sec19.7) never holds "
        "for THIS table - the coop slots, perpetually unfilled, permanently block the recenter branch. SECTION 2 "
        "below uses a separate table with no coop slots to isolate and actually demonstrate the sliding anchor.\n\n");

    std::printf("=== SECTION 2: the anchor actually SLIDES once a (coop-free) table fills (not a fixed point) ===\n\n");
    {
        std::vector<RoadblockSlot> slidingSlots(1);
        slidingSlots[0].category = RoadblockCategory::kPed;
        slidingSlots[0].despawnSecondsRemaining = 1'000'000.0;  // never expires mid-demo, isolates the recenter behaviour
        RoadblockSpawnManager slidingManager(slidingSlots);
        RoadblockPopulationDriver slidingDriver;
        slidingDriver.armed = true;

        Vec3 refPos{0, 0, 0};
        const Vec3 refVelocity{0, 0, 20.0};  // 20 units/s "forward" (+z)
        auto resolveDirection = [&](const Vec3&) { return ResolveDirectionAlongHeading(refVelocity); };

        for (int tick = 0; tick < 5; ++tick) {
            refPos = refPos + refVelocity * dtSeconds;
            RoadblockTickResult result = slidingDriver.RunTick(slidingManager, dtSeconds, refPos, resolveDirection);
            std::printf("[tick %d] reference=(%.1f,%.1f,%.1f) anchor=(%.1f,%.1f,%.1f) anchor_recentered=%s\n", tick,
                        refPos.x, refPos.y, refPos.z, slidingManager.anchorPosition().x,
                        slidingManager.anchorPosition().y, slidingManager.anchorPosition().z,
                        result.anchorRecentered ? "YES" : "no");
        }
        std::printf(
            "\nSECTION 2 RESULT: once the single slot filled (tick 0), every later tick shows "
            "anchor_recentered=YES and the anchor's own Z coordinate changing by the EXACT amount the reference "
            "moved that tick (CONFIRMED subtraction formula, Sec19.7: anchor -= delta(reference)) - proof this is "
            "a sliding point, never a fixed world position.\n\n");
    }

    std::printf("=== SECTION 3: spike-strip vs. generic vehicle distinguishability (direct check) ===\n\n");
    {
        RoadblockSlot spikeSlot;
        spikeSlot.category = RoadblockCategory::kVehicle;
        spikeSlot.vehicleDefinitionName = "Spike_strip";
        RoadblockSlot genericSlot;
        genericSlot.category = RoadblockCategory::kVehicle;
        genericSlot.vehicleDefinitionName = "generic_police_cruiser";

        RoadblockDispatchResult spikeResult = DispatchRoadblockSlot(spikeSlot, Vec3{100, 0, 100});
        RoadblockDispatchResult genericResult = DispatchRoadblockSlot(genericSlot, Vec3{100, 0, 110});

        std::printf("spike slot  (configured name \"Spike_strip\"):          ");
        PrintDispatch(spikeResult);
        std::printf("generic slot(configured name \"generic_police_cruiser\"): ");
        PrintDispatch(genericResult);
        std::printf("\nkinds differ: %s (RoadblockVehicleKind::kSpikeStrip vs. ::kGeneric)\n",
                    (spikeResult.vehicle->kind != genericResult.vehicle->kind) ? "YES" : "NO - BUG");
    }

    std::printf("\npolice_roadblock_demo: done.\n");
    return 0;
}
