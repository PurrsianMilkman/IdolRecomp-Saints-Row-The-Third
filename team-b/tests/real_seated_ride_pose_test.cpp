// Real-data test for sr3player::seated_ride_pose.h (2026-10-09 seated-
// driving-pose task, spec-player-runtime.md Sec7.4b): loads the real shipped
// vi_ride.xtbl/vehicle_interaction_info.xtbl (misc_tables.vpp_pc) and a real
// brad.rig_pc-compatible clip (preload_anim.vpp_pc) out of
// CRREISH_GAME_CACHE_DIR (exit 77 - ctest SKIPPED, never a pass - when the
// archive is absent; nothing from it is copied into the repository) and
// exercises the FULL resolution+application path end to end against real
// data:
//   1. the real "truck_2dr_classic01" row's Driver seat really says
//      Ride_Animations = "standard driver" (ground truth for
//      ResolveRideAnimationString()'s own fixture in the synthetic test);
//   2. ResolveRideAnimationString() against the REAL parsed vi_ride.xtbl
//      table resolves that seat to the REAL Animation string "drive network"
//      (spec-player-runtime.md Sec7.4b's own cited example, confirmed byte-
//      for-byte against the real archive, not re-typed from the spec text);
//   3. ChosenRealClipForRideAnimation("drive network") names a clip that
//      ACTUALLY EXISTS in preload_anim.vpp_pc and is loadable;
//   4. SeatedRidePoseApplier::ComposeSeatedPose() against that real loaded
//      clip returns resolved=true with a full, finite, unit-length-rotation
///     pose for every bone of the real brad.rig_pc (68 bones) - and that
//      pose is NOT the bind pose (at least one bone's rotation differs from
//      identity beyond a tight tolerance) - i.e. this really changes what
//      gets rendered, not a no-op dressed up as success.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "sr3player/seated_ride_pose.h"
#include "sr3rig/rig.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

using namespace sr3player;
using namespace sr3xtbl;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    const bool usable = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                        r.status == vpp::DecodeStatus::ContentValidated ||
                        r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                        r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!usable) return false;
    out = std::move(r.data);
    return true;
}

bool findEntryRecursive(const vpp::Container& c, const std::string& wanted, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name == wanted) {
            if (entryBytes(c, i, out)) return true;
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                if (findEntryRecursive(c.openNested(i), wanted, out)) return true;
            } catch (const std::exception&) {
            }
        }
    }
    return false;
}

}  // namespace

int main() {
    const fs::path miscPath = fs::path(CRREISH_GAME_CACHE_DIR) / "misc_tables.vpp_pc";
    const fs::path animPath = fs::path(CRREISH_GAME_CACHE_DIR) / "preload_anim.vpp_pc";
    const fs::path charPath = fs::path(CRREISH_GAME_CACHE_DIR) / "characters.vpp_pc";
    if (!fs::exists(miscPath) || !fs::exists(animPath) || !fs::exists(charPath)) {
        std::cout << "SKIP: one of misc_tables.vpp_pc/preload_anim.vpp_pc/characters.vpp_pc not present under "
                  << CRREISH_GAME_CACHE_DIR << "\n";
        return 77;
    }

    // ---- 1/2: real vi_ride.xtbl + vehicle_interaction_info.xtbl ----
    auto miscArchive = vppcache::openArchive(miscPath);
    std::vector<uint8_t> rideBytes, infoBytes;
    CHECK(findEntryRecursive(miscArchive->container, "vi_ride.xtbl", rideBytes));
    CHECK(findEntryRecursive(miscArchive->container, "vehicle_interaction_info.xtbl", infoBytes));
    if (rideBytes.empty() || infoBytes.empty()) {
        std::cerr << g_failures << " failure(s) - cannot continue without the real xtbl entries.\n";
        return 1;
    }

    Document rideDoc = ParseDocument(rideBytes.data(), rideBytes.size());
    Document infoDoc = ParseDocument(infoBytes.data(), infoBytes.size());
    std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationSet> rideSets =
        sr3tables_vehicle_world::ParseViRideTable(rideDoc);
    std::vector<sr3tables_vehicle_world::VehicleInteractionInfo> infos =
        sr3tables_vehicle_world::ParseVehicleInteractionInfoTable(infoDoc);
    std::cout << "Real vi_ride.xtbl: " << rideSets.size() << " sets; vehicle_interaction_info.xtbl: " << infos.size()
              << " rows\n";
    CHECK(rideSets.size() > 10);
    CHECK(infos.size() > 10);

    const sr3tables_vehicle_world::VehicleInteractionInfo* truck = nullptr;
    for (const auto& i : infos)
        if (i.name.has_value() && *i.name == "truck_2dr_classic01") {
            truck = &i;
            break;
        }
    CHECK(truck != nullptr);
    if (truck == nullptr) {
        std::cerr << g_failures << " failure(s) - real 'truck_2dr_classic01' row not found.\n";
        return 1;
    }

    const sr3tables_vehicle_world::VehicleInteractionSeatInfo* driverSeat =
        FindSeatByResolverIndex(*truck, kDriverSeatResolverIndex);
    CHECK(driverSeat != nullptr);
    CHECK(driverSeat != nullptr && driverSeat->rideAnimations.has_value() &&
          *driverSeat->rideAnimations == "standard driver");

    std::optional<std::string> animationString = ResolveRideAnimationString(*truck, kDriverSeatResolverIndex, rideSets);
    CHECK(animationString.has_value());
    CHECK(animationString.has_value() && *animationString == "drive network");
    std::cout << "Real driver-seat ride animation string: '" << (animationString ? *animationString : "<none>")
              << "'\n";

    // ---- 3: the CHOSEN bridge names a clip that really exists ----
    std::optional<std::string> clipName = ChosenRealClipForRideAnimation(*animationString);
    CHECK(clipName.has_value());
    if (!clipName.has_value()) {
        std::cerr << g_failures << " failure(s) - no CHOSEN clip bridged for '" << *animationString << "'.\n";
        return 1;
    }

    auto animArchive = vppcache::openArchive(animPath);
    std::vector<uint8_t> clipBytes;
    CHECK(findEntryRecursive(animArchive->container, *clipName, clipBytes));
    if (clipBytes.empty()) {
        std::cerr << g_failures << " failure(s) - CHOSEN clip '" << *clipName << "' not found in "
                  << animPath.string() << "\n";
        return 1;
    }

    sr3anim::Animation animation;
    sr3anim::Payload payload;
    try {
        vpp::ByteView bytes(clipBytes.data(), clipBytes.size());
        animation = sr3anim::Animation::parse(bytes);
        payload = sr3anim::Payload::walk(bytes, animation);
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: real clip '" << *clipName << "' failed to parse/walk: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "Real clip '" << *clipName << "': " << clipBytes.size() << " bytes, "
              << payload.tracks().size() << " tracks, durationUnits=" << animation.durationTotal() << "\n";
    CHECK(animation.durationTotal() > 100);  // the probed 458-unit idle loop, not a short one-shot

    // ---- 4: real brad.rig_pc + end-to-end ComposeSeatedPose() ----
    auto charArchive = vppcache::openArchive(charPath);
    std::vector<uint8_t> rigBytes;
    CHECK(findEntryRecursive(charArchive->container, "brad.rig_pc", rigBytes));
    if (rigBytes.empty()) {
        std::cerr << g_failures << " failure(s) - brad.rig_pc not found.\n";
        return 1;
    }
    sr3rig::Rig bradRig = sr3rig::Rig::parse(vpp::ByteView(rigBytes.data(), rigBytes.size()));
    std::cout << "Real brad.rig_pc: " << bradRig.bones().size() << " bones\n";
    CHECK(bradRig.bones().size() == 68);

    sr3animpose::ResolvedClip resolved;
    resolved.animation = &animation;
    resolved.payload = &payload;
    resolved.bytes = vpp::ByteView(clipBytes.data(), clipBytes.size());

    class SingleClipSource : public sr3animpose::ClipSource {
    public:
        explicit SingleClipSource(const sr3animpose::ResolvedClip* c) : clip_(c) {}
        const sr3animpose::ResolvedClip* lookup(int32_t record) override { return record == 0 ? clip_ : nullptr; }

    private:
        const sr3animpose::ResolvedClip* clip_;
    };
    SingleClipSource clips(&resolved);
    std::unordered_map<std::string, int32_t> recordByClipName = {{*clipName, 0}};

    SeatedRidePoseApplier applier;
    SeatedRidePoseApplier::Result result = applier.ComposeSeatedPose(*truck, kDriverSeatResolverIndex, rideSets,
                                                                      recordByClipName, clips, 0.0,
                                                                      bradRig.bones().size());
    CHECK(result.resolved);
    CHECK(result.animationString == "drive network");
    CHECK(result.chosenClipName == *clipName);
    CHECK(result.pose.bones.size() == bradRig.bones().size());

    bool anyFiniteRotation = true;
    bool anyNonIdentity = false;
    for (const auto& b : result.pose.bones) {
        const bool finite = std::isfinite(b.rotation.x) && std::isfinite(b.rotation.y) &&
                            std::isfinite(b.rotation.z) && std::isfinite(b.rotation.w);
        if (!finite) {
            anyFiniteRotation = false;
            continue;
        }
        const double lenSq = double(b.rotation.x) * b.rotation.x + double(b.rotation.y) * b.rotation.y +
                             double(b.rotation.z) * b.rotation.z + double(b.rotation.w) * b.rotation.w;
        CHECK(std::fabs(lenSq - 1.0) < 1e-3);
        // Identity quaternion is (0,0,0,1) in this project's own convention
        // (sr3rig::identityRotation3()/BoneSample default) - a bone genuinely
        // posed by the real clip will differ from that by more than float
        // noise on at least one component.
        if (std::fabs(b.rotation.w - 1.0f) > 1e-3f || std::fabs(b.rotation.x) > 1e-3f ||
            std::fabs(b.rotation.y) > 1e-3f || std::fabs(b.rotation.z) > 1e-3f)
            anyNonIdentity = true;
    }
    CHECK(anyFiniteRotation);
    CHECK(anyNonIdentity);  // THE central claim: this is a real pose, not bind pose in disguise
    std::cout << "ComposeSeatedPose(): resolved=1, " << result.pose.bones.size()
              << " bones, at least one bone posed away from identity = " << (anyNonIdentity ? "yes" : "NO") << "\n";

    if (g_failures == 0) {
        std::cout << "All real seated-ride-pose checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
