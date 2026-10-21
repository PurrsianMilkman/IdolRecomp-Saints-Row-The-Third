// Collectibles - spec-tables-progression.md C.2. See include/sr3collect/collectibles.h.

#include "sr3collect/collectibles.h"

#include <algorithm>
#include <cctype>

namespace sr3collect {

namespace {

bool SameName(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return i == a.size() && !b[i];
}

}  // namespace

std::array<CollectibleGroupDef, kCollectibleGroups> CollectibleDefsFromTable(
    const std::vector<sr3tables_progression::Collectible>& rows) {
    std::array<CollectibleGroupDef, kCollectibleGroups> out;
    for (const auto& r : rows) {
        int g = -1;
        for (int i = 0; i < kCollectibleGroups; ++i)
            if (SameName(r.name, sr3tables_progression::kCollectibleNames[i])) g = i;
        if (g < 0) continue;
        CollectibleGroupDef& d = out[static_cast<size_t>(g)];
        d.name = r.name;
        d.title = r.titleMessage.value_or("");
        d.respect = r.respectAward.value_or(0);
        d.cash = r.cashAward.value_or(0.0f);
        d.thresholds.clear();
        for (const auto& t : r.thresholdRewards) {
            if (d.thresholds.size() >= 6) break;
            if (t.threshold.value <= 0) continue;  // a record is dropped unless Threshold > 0
            d.thresholds.push_back({t.threshold.value, t.respectAward.value, t.cashAward.value, t.unlockable});
        }
    }
    return out;
}

bool CollectibleRegistry::registerHandle(int group, uint64_t handle, bool collected) {
    if (group < 0 || group >= kCollectibleGroups) return false;
    auto& slots = groups_[static_cast<size_t>(group)];
    for (Slot& s : slots)
        if (s.handle == handle) {
            s.collected = s.collected || collected;
            return true;
        }
    if (static_cast<int>(slots.size()) >= kSlotsPerGroup) return false;
    slots.push_back({handle, collected});
    return true;
}

CollectResult CollectibleRegistry::collect(int group, uint64_t handle,
                                           const std::array<CollectibleGroupDef, kCollectibleGroups>& defs) {
    CollectResult r;
    if (group < 0 || group >= kCollectibleGroups) return r;
    auto& slots = groups_[static_cast<size_t>(group)];
    auto it = std::find_if(slots.begin(), slots.end(), [&](const Slot& s) { return s.handle == handle; });
    if (it == slots.end() || it->collected) return r;
    it->collected = true;
    const CollectibleGroupDef& d = defs[static_cast<size_t>(group)];
    r.accepted = true;
    r.group = group;
    r.collected = collected(group);
    r.registered = registered(group);
    r.cash = d.cash;
    r.respect = d.respect;
    for (const CollectibleThreshold& t : d.thresholds)
        if (t.threshold == r.collected) {  // only an exact match pays, so each threshold pays once
            r.cash += t.cash;
            r.respect += t.respect;
            r.unlockable = t.unlockable;
            break;
        }
    r.screen = group == 3 ? CollectionScreen::PhotoOp : CollectionScreen::Generic;
    r.title = d.title;
    return r;
}

int CollectibleRegistry::registered(int group) const {
    return group < 0 || group >= kCollectibleGroups ? 0 : static_cast<int>(groups_[static_cast<size_t>(group)].size());
}

int CollectibleRegistry::collected(int group) const {
    if (group < 0 || group >= kCollectibleGroups) return 0;
    const auto& s = groups_[static_cast<size_t>(group)];
    return static_cast<int>(std::count_if(s.begin(), s.end(), [](const Slot& x) { return x.collected; }));
}

int CollectibleRegistry::totalRegistered() const {
    int n = 0;
    for (int g = 0; g < kCollectibleGroups; ++g) n += registered(g);
    return n;
}

int CollectibleRegistry::totalCollected() const {
    int n = 0;
    for (int g = 0; g < kCollectibleGroups; ++g) n += collected(g);
    return n;
}

double CollectibleRegistry::completion() const {
    const int reg = totalRegistered();
    return reg > 0 ? static_cast<double>(totalCollected()) / reg : 0.0;
}

bool CollectibleRegistry::hasMarker(int group, uint64_t handle) const {
    if (group < 0 || group >= kCollectibleGroups) return false;
    for (const Slot& s : groups_[static_cast<size_t>(group)])
        if (s.handle == handle) return !s.collected;
    return false;
}

std::vector<sr3save::CollectibleRecord> CollectibleRegistry::saveRecords() const {
    std::vector<sr3save::CollectibleRecord> out;
    for (int g = 0; g < kCollectibleGroups; ++g)
        for (const Slot& s : groups_[static_cast<size_t>(g)])
            if (s.collected)
                out.push_back({static_cast<uint32_t>(s.handle & 0xffffffffu), static_cast<uint32_t>(s.handle >> 32),
                               static_cast<uint32_t>(g), 0u});
    return out;
}

void CollectibleRegistry::restore(const std::vector<sr3save::CollectibleRecord>& records) {
    for (const auto& r : records)
        registerHandle(static_cast<int>(r.group), (static_cast<uint64_t>(r.handleHi) << 32) | r.handleLo, true);
}

CollectionAchievements EvaluateCollectionAchievements(int collectiblesFound, int challengesCompleted) {
    return {collectiblesFound >= 20, collectiblesFound >= 80, challengesCompleted >= 1, challengesCompleted >= 41};
}

}  // namespace sr3collect
