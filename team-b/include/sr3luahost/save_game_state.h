// Save-game-shaped engine state: the subset of spec-save-format.md's real
// snapshot layout this project tracks as live, mutable world state, added
// 2026-10-08 to implement spec-save-format.md Sec15's save-trigger wiring
// task (mission_autosave/game_autosave/mission_set_checkpoint, all three
// CONFIRMED to funnel through the single real save entry 0x00B94A10,
// Sec6.3/Sec15). Split out as its own per-domain header, same convention
// character_state.h's own top note establishes for this header set: add new
// save-related fields HERE, not in engine_state.h directly.
//
// Scope, stated plainly: every field below is a span spec-save-format.md
// marks CONFIRMED (Sec6.4/Sec6.5/Sec10.3) that this project simply had no
// live counterpart for before this task - this is NOT the full 32-entry
// snapshot (see sr3save::SaveSnapshotFields, save_snapshot.h, for the
// writer's own "un-modeled span stays zero" placeholder convention covering
// the rest). Adding storage for a CONFIRMED field this project hadn't
// needed yet is implementing this task's own brief, not inventing a value -
// every default below is the real "new game" save's own reported value
// (Sec6.5/Sec10.3: level 0, respect 0, difficulty 1 "normal", cheats
// unused, 0 barnstorms/stunt jumps, no active mission), never a guess.
//
// cash on hand is deliberately NOT duplicated here - EngineState::
// playerCash() (added long before this task, Sec16.2/Sec23.1/Sec43.3) is
// already the one live cash balance every store/purchase native reads and
// writes, in the same dollar units spec Sec10.3's own cashDiv100() exposes;
// save_game_bridge.h converts between it and the save's raw x100 integer at
// the read/write boundary instead of adding a second, competing balance.
//
// The real save confirms NO player position/orientation anywhere (Sec6.6,
// "checked directly... answer: not present" - the save's own zone-name
// string selects which level loads, but nothing in the mapped file carries
// where in that zone the player ends up) - so, deliberately, THIS STRUCT
// HAS NO POSITION FIELD. A round-trip test that claimed position survives a
// save/load cycle would be testing behavior the real game does not have.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3save/save_snapshot.h"

namespace sr3luahost {

// One collected collectible (mirrors sr3save::CollectibleRecord 1:1,
// spec-save-format.md Sec10.3, offset 0x3FC): an 8-byte object handle plus
// which of the 4 groups it belongs to (0 drug packages, 1 money pallets, 2
// sex dolls, 3 photo ops). This project's own minimal "inventory of
// collected world pickups" - the one collection-shaped save category this
// project can populate without a real handle-allocation system behind it
// (a raw handle is just an opaque pair here, same "per-actor map keyed by
// name/handle, not a true engine entity" convention this project's own
// EnemyTargetRecord/MinimapIconRecord already use, character_state.h).
struct CollectedCollectible {
    uint32_t handleLo = 0;
    uint32_t handleHi = 0;
    uint32_t group = 0; // 0..3
};

// One garage-owned vehicle record (mirrors sr3save::GarageRecord 1:1,
// spec-save-format.md Sec9.2, entry 5, offset 0xB760). No live vehicle
// handle field here either, matching the real format: the load routine
// always rebinds to a fixed null-handle constant, never round-trips a live
// handle (CONFIRMED 2026-10-11 - see GarageRecord's own top comment).
struct GarageVehicleRecord {
    uint32_t recordId = 0;
    uint32_t blobLength = 0;
    float unknownFlag = 0.0f;
    uint16_t vehicleTypeIndex = 0;
    uint16_t enumE = 0;
    uint16_t enum10 = 0;
    uint8_t byte12 = 0xFF;
    std::array<uint8_t, 32> optionSet{};
    std::array<uint8_t, 57> blob{};
    uint8_t stateFlags = 0;
};

// One wardrobe owned-item record (mirrors sr3save::WardrobeItemRecord 1:1,
// spec-save-format.md Sec10.1/10.2/10.3, offset 0x4F6C): an item id (opaque
// name-hash), 3 colour-slot indices (0xFF = none, matching the real
// format's own convention), a 4-bit wear-option/variant index, a 1-bit
// flag, and 3 spare bits (0 in every real sample, round-tripped anyway).
struct WardrobeOwnedItem {
    uint32_t itemId = 0;
    uint8_t colour0 = 0xFF;
    uint8_t colour1 = 0xFF;
    uint8_t colour2 = 0xFF;
    uint8_t wearOption = 0;
    bool flag = false;
    uint8_t spareBits = 0;
};

// One weapon inventory owned-weapon record (mirrors
// sr3save::WeaponInventoryRecord 1:1, spec-save-format.md Sec10.3's own
// span table row, offset 0x4A74/0x4A78): a weapon-definition id (opaque
// name-hash), an unused dword, four signed "ammo-shaped" values (meaning
// OPEN), two single-byte flags, and 2 pad bytes (0 in every real sample,
// round-tripped anyway).
struct WeaponInventoryItem {
    uint32_t weaponDefId = 0;
    uint32_t unused = 0;
    std::array<int32_t, 4> ammo{};
    uint8_t flag0 = 0;
    uint8_t flag1 = 0;
    std::array<uint8_t, 2> pad{};
};

// The live, mutable subset of a real save snapshot's player-state fields
// (spec-save-format.md Sec10.3 span-by-span table; Sec12.10.1 for
// activeMissionLabelIndex) this project tracks outside of playerCash().
struct SaveGameState {
    // 0x4418/0x441C/0x4420 (Sec10.3, CONFIRMED - disassembly): total
    // respect, respect accumulated inside the current level, player level.
    // Defaults 0/0/0 match the real new-game save's own reported values.
    uint32_t respectTotal = 0;
    uint32_t respectInLevel = 0;
    uint32_t playerLevel = 0;

    // 0x2B64 (Sec6.5): 0 casual, 1 normal, 2 hardcore. Default 1 ("normal")
    // is the setter's own CONFIRMED default (Sec10.3: "Default 1; setter
    // 0x005F4A80 accepts < 3"), not a guess.
    uint32_t difficulty = 1;

    // 0x0C8 (Sec10.3, CONFIRMED): set by the cheat-activation routine,
    // cleared at session reset. Default false ("cheats never used").
    bool cheatsUsed = false;

    // 0x3F0/0x3F8 (Sec10.3, CONFIRMED): barnstorms found / stunt jumps
    // found. Default 0, matching every real new-game sample.
    uint32_t barnstormsFound = 0;
    uint32_t stuntJumpsFound = 0;

    // 0x3FC (Sec10.3): the collected-collectibles list. Starts empty - no
    // in-scope native in this project currently produces one; a test (or a
    // future collectible-pickup native) appends to it directly.
    std::vector<CollectedCollectible> collectibles;

    // 0xB760 (Sec9.2): the garage's owned-vehicle records. Starts empty -
    // no in-scope native currently produces one; a test (or a future
    // garage/vehicle-purchase native) appends to it directly. Default
    // next-id 1 matches the real new-game save (Sec9.2: counter initialised
    // to 1 at boot).
    std::vector<GarageVehicleRecord> garageVehicles;
    uint32_t garageNextId = 1;

    // 0x4F6C (Sec10.1/10.2/10.3): the wardrobe's owned-item records. Starts
    // empty - no in-scope native currently produces one; a test (or a
    // future wardrobe-purchase native) appends to it directly.
    std::vector<WardrobeOwnedItem> wardrobeItems;

    // 0x4A74/0x4A78 (Sec10.3): the weapon inventory's owned-weapon records.
    // Starts empty - no in-scope native currently produces one; a test (or
    // a future weapon-pickup native) appends to it directly.
    std::vector<WeaponInventoryItem> weaponInventory;

    // 0x084 (Sec12.10.1): index of the active mission in a 50-slot table,
    // 0x7F = none. This project's own task brief is explicit that this
    // field is DISPLAY-ONLY on load (per the orchestrator's save-format
    // sync) - round-tripped as inert data, never used to resume a mission.
    // Default 0x7F ("no active mission").
    uint32_t activeMissionLabelIndex = 0x7F;

    // The save's own zone-name string (Sec6.3 header, 0x014) - selects
    // which level/district would load, per the real game. This project's
    // Lua host has no world-loading system to apply it to, so it is
    // round-tripped as inert data too, same as activeMissionLabelIndex.
    std::string levelName;

    // 0x113B8/0x113C0 (Sec12.4.1): the city-takeover region records, up to
    // 156, 154 used in every real sample. Reuses sr3save::RegionRecord
    // directly (handleLo/handleHi/controlled/tail) rather than a parallel
    // SaveGameState-local struct - same object-list shape as
    // garageVehicles/wardrobeItems/weaponInventory, but with nothing this
    // project's own records need to translate (no vehicle-handle-shaped
    // exclusion, no colour/wear sub-fields), so the sr3save type itself is
    // the simplest live representation. Starts empty - no in-scope native
    // currently produces one; a test (or a future region/takeover native)
    // appends to it directly.
    std::vector<sr3save::RegionRecord> regionRecords;

    // 0x2B90/0x2E98 (Sec9.3a/Sec9.3b): the activity progress table (up to
    // 64 records, 60 used in every real sample) and its companion 64-bit
    // marker-flag set. Reuses sr3save::ActivityRecord directly for the
    // same reason as regionRecords above. Starts empty/0 - no in-scope
    // native currently produces one; a test (or a future activity-complete
    // native) appends to it directly.
    std::vector<sr3save::ActivityRecord> activityRecords;
    uint64_t activityMarkerFlags = 0;

    // 0x0CC (Sec9.5): ids of the currently-active cheats, up to 200.
    // Default empty, matching cheatsUsed's own "cheats never used" default
    // above - a real new-game save activates none.
    std::vector<uint32_t> activeCheatIds;

    // 0x3340 (Sec12.6.1): the 149 per-save statistics, ids 68..216.
    // Reuses sr3save::SnapshotStat/kSnapshotStatCount directly (a plain
    // value array, not an object list, so there is no separate
    // SaveGameState-local struct to mirror, matching
    // SaveSnapshotFields::statistics's own array type exactly). Default
    // all-zero - every statistic's own documented "not yet earned" value,
    // matching a real new-game save.
    std::array<sr3save::SnapshotStat, sr3save::kSnapshotStatCount> statistics{};
};

} // namespace sr3luahost
