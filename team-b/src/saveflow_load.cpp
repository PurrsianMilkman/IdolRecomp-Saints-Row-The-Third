// Free-roam load plan - spec-save-format.md A.1. See
// include/sr3saveflow/freeroam_flow.h.

#include "sr3saveflow/freeroam_flow.h"

#include "sr3save/save_snapshot.h"

namespace sr3saveflow {

namespace {

// What each entry restores (A.1 step 5 table, section 12 names).
const char* const kEntryNames[32] = {
    "header: clock, play time, flag set, placement",
    "player: cash, cheats, collectibles, appearance, respect, weapons, wardrobe, respect multiplier",
    "capability-class objects",
    "gang customisation",
    "crew roster",
    "garage records",
    "activity progress, activity markers, mission-object states",
    "active-mission index",
    "trigger objects",
    "cribs: locked bits, interior levels, money stash",
    "tutorial states",
    "gameplay nags",
    "traffic-lane bits",
    "unlockables",
    "cheats",
    "hitman groups and chop shops",
    "radio event queue",
    "mix tape",
    "flagged radio stations",
    "stores owned and discovered",
    "flashpoints",
    "survival instances",
    "takeover regions' controlled bits",
    "world-object name set",
    "weapon upgrades",
    "session bits",
    "challenges",
    "statistics",
    "DLC blocks",
    "DLC sub-version",
    "traffic-lane bits (second set)",
    "DLC slot data",
};

bool MissionStartFlagClear(const std::vector<MissionObjectRecord>& table, const ObjectHandle& h) {
    for (const MissionObjectRecord& r : table)
        if (r.handle == h && (r.flags & 0x4u) == 0) return true;
    return false;
}

}  // namespace

LoadPlan PlanFreeRoamLoad(const std::vector<uint8_t>& image, const LoadWorld& world,
                          const LoadOptions& options) {
    const SnapshotBlocks b = SnapshotBlocks::read(image);
    const sr3save::SaveSnapshot snap =
        sr3save::SaveSnapshot::parse(sr3save::ByteView(image.data(), image.size()));

    LoadPlan plan;
    plan.zoneName = b.zoneName;
    plan.steps.push_back({LoadStepKind::StreamingWait, -1, "zone streaming completes"});
    plan.steps.push_back({LoadStepKind::SubsystemResets, -1, "subsystem resets (respect multiplier := 1.0)"});
    plan.steps.push_back({LoadStepKind::VersionGate, -1, "version 94"});
    plan.respectMultiplier = 1.0f;
    plan.versionOk = snap.formatVersion() == kSnapshotVersion;

    if (plan.versionOk) {
        for (int e = 0; e < 32; ++e) plan.steps.push_back({LoadStepKind::ApplyEntry, e, kEntryNames[e]});

        // Entry 0.
        plan.clock = b.clock;
        if (options.kind == LoadKind::Full)
            plan.playTime = static_cast<double>(static_cast<float>(b.playTimeSeconds));
        plan.flagSet = b.flagSet;
        if (options.kind == LoadKind::Full && options.sessionHostOrNoSession) {
            Placement& pl = plan.placement;
            if (options.tryMissionStartOverride && world.findMissionStart) {
                if (auto ms = world.findMissionStart("tss01")) {
                    if (MissionStartFlagClear(b.missionObjects, ms->first)) {
                        pl.source = PlacementSource::MissionStart;
                        pl.handle = ms->first;
                        pl.transform = ms->second;
                    }
                }
            }
            if (pl.source == PlacementSource::None) {
                std::optional<WorldTransform> t;
                if (!b.respawnCrib.isZero() && world.resolveHandle) t = world.resolveHandle(b.respawnCrib);
                if (t) {
                    pl.source = PlacementSource::RespawnCrib;
                    pl.handle = b.respawnCrib;
                    pl.transform = t;
                } else {
                    pl.source = PlacementSource::PlayerStartMarker;
                    if (world.playerStartMarker) pl.transform = world.playerStartMarker();
                }
            }
        }

        // Entry 1.
        plan.cashRaw = ClampCash(b.cashRaw);
        plan.respectTotal = b.respectTotal;
        plan.respectInLevel = b.respectInLevel;
        plan.respectLevel = b.respectLevel;
        plan.respectMultiplier = b.respectMultiplier;

        // Entry 9, entry 22, difficulty.
        plan.cribs = b.cribs;
        for (const sr3save::RegionRecord& r : snap.regionRecords())
            plan.regionControlled.push_back(r.controlled);
        plan.difficulty = snap.difficultyIndex();

        plan.steps.push_back({LoadStepKind::LoadFinished, -1, "load finished"});
    }

    // The post-start wait streams around the explicit start point when one
    // was set, else around the registered player-start marker.
    if (plan.placement.transform && plan.placement.source != PlacementSource::None) {
        plan.streamCentre = plan.placement.transform->position;
    } else if (world.playerStartMarker) {
        if (auto m = world.playerStartMarker()) plan.streamCentre = m->position;
    }
    plan.steps.push_back({LoadStepKind::PostStartStreamingWait, -1, "streaming around the start point"});
    return plan;
}

}  // namespace sr3saveflow
