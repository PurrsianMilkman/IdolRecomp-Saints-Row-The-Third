// Synthetic (hand-built fixtures, no real .xtbl/.anim_pc data) tests for
// sr3player::seated_ride_pose.h - the seated-driving-pose task's own new
// module, spec-player-runtime.md Sec7.4b. Covers the pure data-transform
// half (seat resolution, ride-set lookup, the CHOSEN clip bridge) and the
// honest-miss paths of SeatedRidePoseApplier::ComposeSeatedPose() that need
// no real clip. The full success path (a real clip actually composed into a
// pose) is covered separately by tests/real_seated_ride_pose_test.cpp
// against this machine's own real shipped data (skips when
// CRREISH_GAME_CACHE_DIR is absent).
#include <iostream>
#include <vector>

#include "sr3player/seated_ride_pose.h"

using namespace sr3player;
using sr3tables_vehicle_world::VehicleInteractionAnimationElement;
using sr3tables_vehicle_world::VehicleInteractionAnimationSet;
using sr3tables_vehicle_world::VehicleInteractionInfo;
using sr3tables_vehicle_world::VehicleInteractionSeatInfo;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

VehicleInteractionInfo MakeTruckStyleInfo() {
    // Mirrors the REAL shipped "truck_2dr_classic01" row (misc_tables.vpp_pc
    // vehicle_interaction_info.xtbl, verified directly this task): Driver
    // seat -> Ride_Animations "standard driver".
    VehicleInteractionInfo info;
    VehicleInteractionSeatInfo driver;
    driver.seatText = "Driver";
    driver.rideAnimations = "standard driver";
    info.seats.push_back(driver);
    VehicleInteractionSeatInfo pax;
    pax.seatText = "Passenger 1";
    pax.rideAnimations = "thru passenger right";
    info.seats.push_back(pax);
    return info;
}

std::vector<VehicleInteractionAnimationSet> MakeRideSets() {
    // Mirrors the REAL shipped vi_ride.xtbl "standard driver" row (verified
    // directly this task): one Element, Animation = "drive network".
    std::vector<VehicleInteractionAnimationSet> sets;
    VehicleInteractionAnimationSet standardDriver;
    standardDriver.name = "standard driver";
    VehicleInteractionAnimationElement el;
    el.parameters = {"passenger", "forward", "none", "none", "none"};
    el.animation = "drive network";
    standardDriver.elements.push_back(el);
    sets.push_back(standardDriver);
    return sets;
}

// -------------------------- FindSeatByResolverIndex -------------------------

void testFindSeatByResolverIndexDriver() {
    VehicleInteractionInfo info = MakeTruckStyleInfo();
    const VehicleInteractionSeatInfo* seat = FindSeatByResolverIndex(info, kDriverSeatResolverIndex);
    CHECK(seat != nullptr);
    CHECK(seat->seatText == "Driver");
}

void testFindSeatByResolverIndexCaseInsensitive() {
    VehicleInteractionInfo info;
    VehicleInteractionSeatInfo seat;
    seat.seatText = "FRONT DRIVER";  // vehicle-specific alias, upper-case
    info.seats.push_back(seat);
    const VehicleInteractionSeatInfo* found = FindSeatByResolverIndex(info, kDriverSeatResolverIndex);
    CHECK(found != nullptr);
}

void testFindSeatByResolverIndexNoMatch() {
    VehicleInteractionInfo info;
    VehicleInteractionSeatInfo seat;
    seat.seatText = "Gunner";  // not in either 8-name alias list at index 0
    info.seats.push_back(seat);
    const VehicleInteractionSeatInfo* found = FindSeatByResolverIndex(info, kDriverSeatResolverIndex);
    CHECK(found == nullptr);
}

void testFindSeatByResolverIndexOutOfRange() {
    VehicleInteractionInfo info = MakeTruckStyleInfo();
    CHECK(FindSeatByResolverIndex(info, -1) == nullptr);
    CHECK(FindSeatByResolverIndex(info, 8) == nullptr);
}

// -------------------------- ResolveRideAnimationString -----------------------

void testResolveRideAnimationStringRealShapeMatch() {
    VehicleInteractionInfo info = MakeTruckStyleInfo();
    auto sets = MakeRideSets();
    std::optional<std::string> anim = ResolveRideAnimationString(info, kDriverSeatResolverIndex, sets);
    CHECK(anim.has_value());
    CHECK(anim.has_value() && *anim == "drive network");
}

void testResolveRideAnimationStringNoSeat() {
    VehicleInteractionInfo info;  // no seats at all
    auto sets = MakeRideSets();
    CHECK(!ResolveRideAnimationString(info, kDriverSeatResolverIndex, sets).has_value());
}

void testResolveRideAnimationStringNoRideAnimationsText() {
    VehicleInteractionInfo info;
    VehicleInteractionSeatInfo driver;
    driver.seatText = "Driver";
    // rideAnimations left unset
    info.seats.push_back(driver);
    auto sets = MakeRideSets();
    CHECK(!ResolveRideAnimationString(info, kDriverSeatResolverIndex, sets).has_value());
}

void testResolveRideAnimationStringUnmatchedSetName() {
    VehicleInteractionInfo info = MakeTruckStyleInfo();
    std::vector<VehicleInteractionAnimationSet> sets;  // "standard driver" never defined
    CHECK(!ResolveRideAnimationString(info, kDriverSeatResolverIndex, sets).has_value());
}

void testResolveRideAnimationStringFirstElementWinsOnRicherSet() {
    // A motorcycle-shaped richer set (Sec7.4b: "Drive Ready SM" + 5 Alt
    // variants) - the all-"any" key must still pick the FIRST element
    // (documented CHOSEN default, see header banner), not silently miss.
    VehicleInteractionInfo info;
    VehicleInteractionSeatInfo driver;
    driver.seatText = "Driver";
    driver.rideAnimations = "motorcycle driver";
    info.seats.push_back(driver);

    VehicleInteractionAnimationSet moto;
    moto.name = "motorcycle driver";
    VehicleInteractionAnimationElement base;
    base.parameters = {"driver", "forward"};
    base.animation = "Drive Ready SM";
    moto.elements.push_back(base);
    VehicleInteractionAnimationElement alt;
    alt.parameters = {"driver", "aim"};
    alt.animation = "Drive Ready SM Alt1";
    moto.elements.push_back(alt);
    std::vector<VehicleInteractionAnimationSet> sets = {moto};

    std::optional<std::string> anim = ResolveRideAnimationString(info, kDriverSeatResolverIndex, sets);
    CHECK(anim.has_value() && *anim == "Drive Ready SM");
}

// -------------------------- ChosenRealClipForRideAnimation -------------------

void testChosenBridgeKnownEntry() {
    std::optional<std::string> clip = ChosenRealClipForRideAnimation("drive network");
    CHECK(clip.has_value() && *clip == "auto_ridel.anim_pc");
}

void testChosenBridgeUnknownEntryIsHonestMiss() {
    CHECK(!ChosenRealClipForRideAnimation("some unmapped animation name").has_value());
    CHECK(!ChosenRealClipForRideAnimation("Drive Ready SM").has_value());  // motorcycle, out of this task's scope
}

// -------------------------- SeatedRidePoseApplier honest-miss paths ---------

// A ClipSource that never resolves anything - enough to exercise every
// honest-miss branch of ComposeSeatedPose() without needing real clip data.
class EmptyClipSource : public sr3animpose::ClipSource {
public:
    const sr3animpose::ResolvedClip* lookup(int32_t) override { return nullptr; }
};

void testComposeSeatedPoseNoSeatReturnsUnresolved() {
    VehicleInteractionInfo info;  // no seats
    auto sets = MakeRideSets();
    EmptyClipSource clips;
    SeatedRidePoseApplier applier;
    auto result = applier.ComposeSeatedPose(info, kDriverSeatResolverIndex, sets, {}, clips, 0.0, 68);
    CHECK(!result.resolved);
    CHECK(result.animationString.empty());
}

void testComposeSeatedPoseUnmappedAnimationStringReturnsUnresolved() {
    VehicleInteractionInfo info;
    VehicleInteractionSeatInfo driver;
    driver.seatText = "Driver";
    driver.rideAnimations = "Boat";  // real row name, not in the small CHOSEN bridge
    info.seats.push_back(driver);
    VehicleInteractionAnimationSet boatSet;
    boatSet.name = "Boat";
    VehicleInteractionAnimationElement el;
    el.animation = "Driver Network";  // a DIFFERENT real Animation string, deliberately not bridged
    boatSet.elements.push_back(el);
    std::vector<VehicleInteractionAnimationSet> sets = {boatSet};
    EmptyClipSource clips;
    SeatedRidePoseApplier applier;
    auto result = applier.ComposeSeatedPose(info, kDriverSeatResolverIndex, sets, {}, clips, 0.0, 68);
    CHECK(!result.resolved);
    CHECK(result.animationString == "Driver Network");  // resolution succeeded; only the clip bridge missed
    CHECK(result.chosenClipName.empty());
}

void testComposeSeatedPoseClipNotRegisteredReturnsUnresolved() {
    VehicleInteractionInfo info = MakeTruckStyleInfo();
    auto sets = MakeRideSets();
    EmptyClipSource clips;
    SeatedRidePoseApplier applier;
    // recordByClipName deliberately empty - caller never loaded "auto_ridel.anim_pc".
    auto result = applier.ComposeSeatedPose(info, kDriverSeatResolverIndex, sets, {}, clips, 0.0, 68);
    CHECK(!result.resolved);
    CHECK(result.animationString == "drive network");
    CHECK(result.chosenClipName == "auto_ridel.anim_pc");
}

void testComposeSeatedPoseUnresolvableRecordReturnsUnresolved() {
    VehicleInteractionInfo info = MakeTruckStyleInfo();
    auto sets = MakeRideSets();
    EmptyClipSource clips;  // lookup() always returns nullptr, even for a "registered" record
    SeatedRidePoseApplier applier;
    std::unordered_map<std::string, int32_t> recordByClipName = {{"auto_ridel.anim_pc", 0}};
    auto result = applier.ComposeSeatedPose(info, kDriverSeatResolverIndex, sets, recordByClipName, clips, 0.0, 68);
    CHECK(!result.resolved);
}

}  // namespace

int main() {
    testFindSeatByResolverIndexDriver();
    testFindSeatByResolverIndexCaseInsensitive();
    testFindSeatByResolverIndexNoMatch();
    testFindSeatByResolverIndexOutOfRange();

    testResolveRideAnimationStringRealShapeMatch();
    testResolveRideAnimationStringNoSeat();
    testResolveRideAnimationStringNoRideAnimationsText();
    testResolveRideAnimationStringUnmatchedSetName();
    testResolveRideAnimationStringFirstElementWinsOnRicherSet();

    testChosenBridgeKnownEntry();
    testChosenBridgeUnknownEntryIsHonestMiss();

    testComposeSeatedPoseNoSeatReturnsUnresolved();
    testComposeSeatedPoseUnmappedAnimationStringReturnsUnresolved();
    testComposeSeatedPoseClipNotRegisteredReturnsUnresolved();
    testComposeSeatedPoseUnresolvableRecordReturnsUnresolved();

    if (g_failures == 0) {
        std::cout << "All seated-ride-pose synthetic checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
