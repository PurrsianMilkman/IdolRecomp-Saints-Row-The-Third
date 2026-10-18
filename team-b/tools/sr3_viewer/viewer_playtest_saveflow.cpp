// Playtest save/load through sr3saveflow (Team C, HANDOFF-C.md section 6 step 5):
// --load-save applies a snapshot in the spec-save-format.md A.1 order (placement at the
// respawn crib, cash through the clamp, respect verbatim, the respect multiplier, the
// clock's time of day); --save-at-exit writes the A.2 save over the loaded image (or a
// new-game image), with the nearest owned crib as the respawn point and no position.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

#include <cmath>
#include <fstream>
#include <iterator>

#include "sr3io/output_path_guard.h"
#include "sr3save/errors.h"
#include "sr3save/save_snapshot.h"
#include "sr3saveflow/freeroam_flow.h"
#include "sr3saveflow/snapshot_blocks.h"

namespace sr3viewer {

namespace {

sr3saveflow::WorldTransform CribTransform(int index) {
    const sr3saveflow::KnownCrib& k = sr3saveflow::kKnownCribs[static_cast<size_t>(index)];
    sr3saveflow::WorldTransform t;
    t.position = {k.x, k.y, k.z};  // orientation: identity (CHOSEN - the zone object's own is not wired here)
    return t;
}

}  // namespace

void applyLoadedSave(PlaytestSession& sess) {
    std::ifstream in(sess.loadSavePath, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() != sr3save::kSaveSnapshotSize) {
        std::printf("save     : --load-save %s not loaded (%zu bytes, expected %zu)\n", sess.loadSavePath.c_str(),
                    bytes.size(), static_cast<size_t>(sr3save::kSaveSnapshotSize));
        return;
    }
    sr3saveflow::LoadWorld world;
    world.resolveHandle = [](const sr3saveflow::ObjectHandle& h) -> std::optional<sr3saveflow::WorldTransform> {
        const int i = sr3saveflow::KnownCribIndex(h);
        if (i < 0) return std::nullopt;
        return CribTransform(i);
    };
    // The viewer registers no player-start marker; its own spawn point stands in (CHOSEN).
    const double sx = sess.spawnX, sz = sess.spawnZ;
    world.playerStartMarker = [sx, sz]() -> std::optional<sr3saveflow::WorldTransform> {
        sr3saveflow::WorldTransform t;
        t.position = {sx, 0.0, sz};
        return t;
    };
    sr3saveflow::LoadOptions opt;
    opt.tryMissionStartOverride = false;  // no live mission-object table in the viewer
    sr3saveflow::LoadPlan plan;
    try {
        plan = sr3saveflow::PlanFreeRoamLoad(bytes, world, opt);
    } catch (const sr3save::FormatError& e) {
        std::printf("save     : --load-save %s refused (%s)\n", sess.loadSavePath.c_str(), e.what());
        return;
    }
    if (!plan.versionOk) {
        std::printf("save     : --load-save %s: version is not 94, nothing applied\n", sess.loadSavePath.c_str());
        return;
    }
    sess.loadedSaveImage = std::move(bytes);

    // Placement: x/z only; the ground follow below picks the height (CHOSEN - the crib object's
    // own y is its door/marker height, not necessarily walkable ground for this player model).
    const char* where = "player-start marker (the viewer's spawn)";
    if (plan.placement.source == sr3saveflow::PlacementSource::RespawnCrib && plan.placement.transform) {
        sess.spawnX = static_cast<float>(plan.placement.transform->position.x);
        sess.spawnZ = static_cast<float>(plan.placement.transform->position.z);
        const int ci = sr3saveflow::KnownCribIndex(plan.placement.handle);
        where = ci >= 0 ? sr3saveflow::kKnownCribs[static_cast<size_t>(ci)].name : "respawn crib";
    }
    if (sess.hudEs) {
        sess.hudEs->playerCash() = static_cast<double>(plan.cashRaw);
        sess.hudEs->restoreRespectFromSave(plan.respectTotal, plan.respectInLevel, plan.respectLevel);
        sess.hudEs->respectMultiplier().set(plan.respectMultiplier);
    }
    sess.todFraction = sr3saveflow::TimeOfDaySeconds(plan.clock.timeOfDayFixed) / 86400.0;
    std::printf("save     : loaded %s: placed at %s (%.1f, %.1f); cash $%.2f; respect %u/%u level %u; "
                "multiplier %.2f; time %02d:%02d:%02d; play time %.0f s%s\n",
                sess.loadSavePath.c_str(), where, sess.spawnX, sess.spawnZ, plan.cashRaw / 100.0, plan.respectTotal,
                plan.respectInLevel, plan.respectLevel, plan.respectMultiplier, plan.clock.hour, plan.clock.minute,
                plan.clock.second, plan.playTime.value_or(0.0), sess.hudEs ? "" : " (no --hud: cash/respect not applied)");
}

void writeSaveAtExit(PlaytestSession& sess) {
    if (sess.saveAtExitPath.empty()) return;
    std::string reason;
    if (!sr3io::isOutputPathSafe(sess.saveAtExitPath, ".sr3s_pc", 4 * sr3save::kSaveSnapshotSize, reason)) {
        std::printf("save     : --save-at-exit refused: %s\n", reason.c_str());
        return;
    }
    std::vector<uint8_t> image =
        sess.loadedSaveImage.empty() ? sr3saveflow::BuildNewGameSnapshot() : sess.loadedSaveImage;
    const sr3saveflow::SnapshotBlocks b = sr3saveflow::SnapshotBlocks::read(image);

    sr3saveflow::FreeRoamSaveState st;
    st.zoneName = sr3saveflow::kCityZone;
    if (sess.inVehicle && sess.vehicleRig_ && sess.vehicleRig_->sim)
        st.playerPosition = {sess.vehicleRig_->sim->state_.posX, 0.0, sess.vehicleRig_->sim->state_.posZ};
    else if (sess.pm_)
        st.playerPosition = {sess.pm_->st.position.x, sess.pm_->st.position.y, sess.pm_->st.position.z};
    for (size_t i = 0; i < sr3saveflow::kKnownCribs.size(); ++i) {
        sr3saveflow::CribLocation c;
        c.handle = sr3saveflow::kKnownCribs[i].handle;
        c.position = CribTransform(static_cast<int>(i)).position;
        for (const sr3saveflow::CribRecord& r : b.cribs.records)
            if (r.handle == c.handle) c.locked = r.locked;
        st.cribPositions.push_back(c);
    }
    st.clock = b.clock;
    const double todSec = std::fmod(sess.todFraction, 1.0) * 86400.0;
    const int tod = static_cast<int>(todSec);
    sr3saveflow::SetClockTimeOfDay(st.clock, tod / 3600, (tod / 60) % 60, tod % 60);
    const double basePlay = sess.loadedSaveImage.empty() ? 0.0 : static_cast<double>(b.playTimeSeconds);
    st.clock.playTime = static_cast<float>(basePlay + static_cast<double>(sess.nowMsAccum) / 1000.0);
    st.cribs = b.cribs;
    if (sess.hudEs) {
        st.cashRaw = std::llround(sess.hudEs->playerCash());
        st.respectTotal = sess.hudEs->respectTotal();
        st.respectInLevel = sess.hudEs->respectBanked();
        st.respectLevel = sess.hudEs->respectLevel();
        st.respectMultiplier = sess.hudEs->respectMultiplier().known()
                                   ? static_cast<float>(sess.hudEs->respectMultiplier().get())
                                   : b.respectMultiplier;
    } else {
        st.cashRaw = b.cashRaw;
        st.respectTotal = b.respectTotal;
        st.respectInLevel = b.respectInLevel;
        st.respectLevel = b.respectLevel;
        st.respectMultiplier = b.respectMultiplier;
    }
    sr3saveflow::PatchFreeRoamSave(image, st);
    std::ofstream out(sess.saveAtExitPath, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    const sr3saveflow::SnapshotBlocks w = sr3saveflow::SnapshotBlocks::read(image);
    const int ci = sr3saveflow::KnownCribIndex(w.respawnCrib);
    std::printf("save     : wrote %s (%s; respawn crib %s; cash $%.2f; respect %u level %u; play time %u s)\n",
                sess.saveAtExitPath.c_str(), out ? "ok" : "WRITE FAILED",
                ci >= 0 ? sr3saveflow::kKnownCribs[static_cast<size_t>(ci)].name : "none (player-start marker)",
                w.cashRaw / 100.0, w.respectTotal, w.respectLevel, w.playTimeSeconds);
}

}  // namespace sr3viewer
