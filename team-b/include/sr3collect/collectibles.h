// Collectibles - spec-tables-progression.md "Free-roam collectibles, challenges and stat
// tracking" C.2 (and section 9 for collectibles.xtbl). Four groups of at most 20 registered
// world objects each; collecting one pays the group record's Cash_Award / Respect_Award plus
// the threshold reward whose Threshold equals the new collected count, queues the
// collection screen, and notifies stats 24 + group and 66.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3save/save_snapshot.h"
#include "sr3tables_progression/tables_world.h"

namespace sr3collect {

constexpr int kCollectibleGroups = 4;   // Drug Package, Money Pallet, Sex Doll, Photo Op
constexpr int kSlotsPerGroup = 20;      // the registration cap
constexpr int kStatTotalCollectibles = 66;
inline int StatIdForGroup(int group) { return 24 + group; }

struct CollectibleThreshold {
    int32_t threshold = 0;
    int32_t respect = 0;
    double cash = 0.0;
    std::optional<std::string> unlockable;
};

// One collectibles.xtbl record, by group index.
struct CollectibleGroupDef {
    std::string name;
    std::string title;  // Title_Message (localisation key)
    int32_t respect = 0;
    double cash = 0.0;
    std::vector<CollectibleThreshold> thresholds;  // Threshold > 0, at most 6
};

// The parsed table to the four group records (rows whose Name is not one of the four are
// skipped; names compare without case).
std::array<CollectibleGroupDef, kCollectibleGroups> CollectibleDefsFromTable(
    const std::vector<sr3tables_progression::Collectible>& rows);

enum class CollectionScreen { Generic = 1, PhotoOp = 2 };  // HUD_COLLECTION_COLLECTIBLE_*

struct CollectResult {
    bool accepted = false;
    int group = -1;
    int collected = 0;   // n, after this pickup
    int registered = 0;  // the screen's total
    double cash = 0.0;   // paid when the collection screen finishes
    int32_t respect = 0;
    std::optional<std::string> unlockable;  // a threshold's unlockable
    CollectionScreen screen = CollectionScreen::Generic;
    std::string title;
};

class CollectibleRegistry {
public:
    // Find-or-append; a 21st handle in a group is refused. A handle restored as collected
    // counts as collected and has no map marker.
    bool registerHandle(int group, uint64_t handle, bool collected = false);
    // The collect routine: rejected for a bad group, an unregistered or collected handle.
    CollectResult collect(int group, uint64_t handle,
                          const std::array<CollectibleGroupDef, kCollectibleGroups>& defs);

    int registered(int group) const;
    int collected(int group) const;
    int totalRegistered() const;
    int totalCollected() const;
    double completion() const;  // collected / registered over all groups
    bool hasMarker(int group, uint64_t handle) const;  // registered and not collected

    // Save entry 1 (0x3FC): one record per collected slot, group by group in slot order.
    std::vector<sr3save::CollectibleRecord> saveRecords() const;
    void restore(const std::vector<sr3save::CollectibleRecord>& records);

private:
    struct Slot {
        uint64_t handle = 0;
        bool collected = false;
    };
    std::array<std::vector<Slot>, kCollectibleGroups> groups_;
};

// The collectible and challenge achievements (C.5).
struct CollectionAchievements {
    bool gettingTheGoods = false;  // collectibles >= 20
    bool lifeOfTheParty = false;   // collectibles >= 80
    bool shakeAndBake = false;     // challenges completed >= 1
    bool youreMyHero = false;      // challenges completed >= 41
};
CollectionAchievements EvaluateCollectionAchievements(int collectiblesFound, int challengesCompleted);

}  // namespace sr3collect
