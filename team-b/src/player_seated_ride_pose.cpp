#include "sr3player/seated_ride_pose.h"

#include <cmath>
#include <cstring>

#include "sr3player/vehicle_occupancy.h"  // MatchElementKey() - reused, not re-derived (see header banner)

namespace sr3player {

namespace {

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
#if defined(_MSC_VER)
    return _stricmp(a.c_str(), b.c_str()) == 0;
#else
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
#endif
}

}  // namespace

const sr3tables_vehicle_world::VehicleInteractionSeatInfo* FindSeatByResolverIndex(
    const sr3tables_vehicle_world::VehicleInteractionInfo& info, int resolverIndex) {
    if (resolverIndex < 0 || static_cast<size_t>(resolverIndex) >= sr3tables_vehicle_world::kSeatResolverGenericNames.size())
        return nullptr;
    const std::string_view generic = sr3tables_vehicle_world::kSeatResolverGenericNames[static_cast<size_t>(resolverIndex)];
    const std::string_view specific =
        sr3tables_vehicle_world::kSeatResolverVehicleSpecificNames[static_cast<size_t>(resolverIndex)];
    for (const auto& seat : info.seats) {
        if (!seat.seatText.has_value()) continue;
        if (EqualsIgnoreCase(*seat.seatText, std::string(generic)) ||
            EqualsIgnoreCase(*seat.seatText, std::string(specific)))
            return &seat;
    }
    return nullptr;
}

std::optional<std::string> ResolveRideAnimationString(
    const sr3tables_vehicle_world::VehicleInteractionInfo& info, int seatResolverIndex,
    const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationSet>& rideSets) {
    const sr3tables_vehicle_world::VehicleInteractionSeatInfo* seat = FindSeatByResolverIndex(info, seatResolverIndex);
    if (seat == nullptr || !seat->rideAnimations.has_value()) return std::nullopt;

    const sr3tables_vehicle_world::VehicleInteractionAnimationSet* matchedSet = nullptr;
    for (const auto& set : rideSets) {
        if (set.name.has_value() && EqualsIgnoreCase(*set.name, *seat->rideAnimations)) {
            matchedSet = &set;
            break;
        }
    }
    if (matchedSet == nullptr || matchedSet->elements.empty()) return std::nullopt;

    // All-"any" key: every position is a key-side wildcard (Sec9.7 item2),
    // so this unconditionally returns the FIRST element's Animation string.
    // This is MatchElementKey() (0x00b0a650), the ENTER/EXIT wildcard
    // matcher - NOT the real ride matcher (0x00b0a580/0x00b08aa0/
    // 0x00b08b80, Sec9.1/Sec9.6.4). Reusing it here is a CHOSEN shortcut
    // that reproduces the real ride matcher's own GUARANTEED convergence
    // for a single-Element set (see header banner's "IMPORTANT CORRECTION"
    // for why that convergence holds), not a port of its retry cascade; it
    // is a documented CHOSEN default (first element wins, unverified
    // against the live key) for a richer multi-element set this task does
    // not model combat/aim-variant reselection for.
    static const std::array<std::string, 5> kAnyKey = {"any", "any", "any", "any", "any"};
    return MatchElementKey(matchedSet->elements, kAnyKey);
}

std::optional<std::string> ChosenRealClipForRideAnimation(const std::string& animationString) {
    static const std::unordered_map<std::string, std::string> kBridge = {
        // See this header's own file banner for how this single entry was
        // found and why - CHOSEN, not spec-confirmed.
        {"drive network", "auto_ridel.anim_pc"},
    };
    auto it = kBridge.find(animationString);
    if (it == kBridge.end()) return std::nullopt;
    return it->second;
}

SeatedRidePoseApplier::Result SeatedRidePoseApplier::ComposeSeatedPose(
    const sr3tables_vehicle_world::VehicleInteractionInfo& info, int seatResolverIndex,
    const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationSet>& rideSets,
    const std::unordered_map<std::string, int32_t>& recordByClipName, sr3animpose::ClipSource& clips,
    double loopPhaseSeconds, size_t boneCount) const {
    Result result;

    std::optional<std::string> animationString = ResolveRideAnimationString(info, seatResolverIndex, rideSets);
    if (!animationString.has_value()) return result;  // resolved=false, caller keeps bind pose
    result.animationString = *animationString;

    std::optional<std::string> clipName = ChosenRealClipForRideAnimation(*animationString);
    if (!clipName.has_value()) return result;
    result.chosenClipName = *clipName;

    auto recIt = recordByClipName.find(*clipName);
    if (recIt == recordByClipName.end()) return result;  // caller never loaded this clip - honest miss, not fabricated

    const sr3animpose::ResolvedClip* resolved = clips.lookup(recIt->second);
    if (resolved == nullptr || resolved->animation == nullptr) return result;

    // A single, non-additive, group-0 layer at full weight (Sec7.4b's
    // "steady ride pose" scope - no second layer/blend to reproduce here).
    const float durationUnits = static_cast<float>(resolved->animation->durationTotal());
    float clipTimeUnits = 0.0f;
    if (durationUnits > 0.0f) {
        const float phase = std::fmod(static_cast<float>(loopPhaseSeconds) * sr3animpose::kClipTimeUnitsPerSecond,
                                      durationUnits);
        clipTimeUnits = (phase < 0.0f) ? phase + durationUnits : phase;
    }
    std::vector<sr3animpose::ActiveLayer> layers = {
        {recIt->second, 1.0f, 0, false, clipTimeUnits},
    };
    result.pose = sr3animpose::ComposeClipPose(layers, clips, boneCount);
    result.resolved = true;
    return result;
}

}  // namespace sr3player
