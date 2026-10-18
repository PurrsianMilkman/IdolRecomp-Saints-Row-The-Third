// EngineState slice: weapons, inventory, items, grenades, pickups.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateWeaponsData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateWeaponsData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// Sec32.1 `satellite_weapon_mode_exit`: the satellite-weapon-controller
// singleton 0x0130eee8's own "active" gate, plus this project's own
// per-selector exit counters. **The 0x009df3d0 (remote co-op player)
// correction (Sec14.31/Sec32.8) is applied at this function's own call
// site** (lua_spec_confirmed_stubs.cpp): bit 0x2 only ever selects a
// player when playerRig().coopPlayerPresent is true (the CONFIRMED "no
// co-op session -> no remote entry" reading, not a "matching" lookup
// that might wrongly fall back to the local player).
struct SatelliteWeaponController {
    OpenValue<bool> active{"satellite-weapon-controller active (0x0130eee8)", "spec-lua-api-behaviour.md Sec32.1"};
    uint64_t localExitCount = 0;
    uint64_t remoteExitCount = 0;

    // --- satellite_weapon_mode_set (Sec51.5, wave-2 batch W2-7,
    // 2026-10-08): "enable/disable on a per-machine controller
    // singleton for a selectable player mask (local, remote co-op, or
    // both); a remote player's request is simply forwarded to that
    // player's own owning machine. No crash shape found; whether
    // re-enabling an already-active controller is separately guarded
    // is OPEN." Reuses `active` above for the local controller (the
    // SAME 0x0130eee8 singleton satellite_weapon_mode_exit already
    // models) rather than a second flag. The remote-forward half has
    // no local effect to simulate (no second machine in this host) -
    // only the fact that a remote request was forwarded is counted.
    uint64_t localSetCount = 0;
    uint64_t remoteForwardCount = 0;
};

// Sec32.6 `item_anim_play`: last-requested animation per item, split by
// the CONFIRMED branch (blend-transition vs single-state-start); the
// layer-slot internals and the exact flag-value meanings are OPEN/
// HYPOTHESIS per spec and are not modeled further than the branch
// itself.
struct ItemAnimState {
    std::string lastBlendTransitionAnim;
    std::string lastSingleStateAnim;
    int lastFlag = 0; // CONFIRMED branch values 0x40/0x10; HYPOTHESIS real-world meaning
    uint64_t blendTransitionCount = 0;
    uint64_t singleStateStartCount = 0;
};

struct WeaponDropRegistration {
    double x = 0, y = 0, z = 0;
    uint32_t weaponDefId = 0;
    int32_t primaryAmmo = 0;
    int32_t roundsInClip = 0;
    int weaponClass = 0;
    bool consumed = false;
};

// --- Path 2: press-E pickup for heavy/pickup-only weapons --------------

struct HeavyWeaponPickupTrigger {
    uint32_t weaponDefId = 0;
    int32_t primaryAmmo = 0;
    int32_t roundsInClip = 0;
};

// inv_weapon_disable_all_but_this_slot/inv_weapon_disable_slot
// (Sec12.8/Sec12.16, 0x00a51270/0x00a51360): CONFIRMED identical clamp-
// switch (slot 0-7 pass through, anything else -> sentinel 0xff, whose
// own real effect is OPEN - "not traced") feeding two DIFFERENT terminal
// dispatches (0x007f4650/0x007f4460, both OPEN) - no per-character/
// per-player name argument at all (implicitly the local player, this
// project's single-player-only scope). CONFIRMED side effects per each
// function's own text: "_all_but_this_slot" disables every OTHER slot
// and enables the given one; "_slot" disables EXACTLY the given slot,
// leaving the rest untouched. Index 8 is this project's own bucket for
// the sentinel 0xff (out-of-range) case - tracked only as a counter
// (real effect OPEN, not guessed).
struct WeaponSlotState {
    std::array<bool, 8> disabled{};
    uint64_t sentinelOobCount = 0; // an out-of-range slot index, real effect OPEN - see doc comment
};

struct EngineStateWeaponsData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using SatelliteWeaponController = sr3luahost::SatelliteWeaponController;
    using ItemAnimState = sr3luahost::ItemAnimState;
    using WeaponDropRegistration = sr3luahost::WeaponDropRegistration;
    using HeavyWeaponPickupTrigger = sr3luahost::HeavyWeaponPickupTrigger;
    using WeaponSlotState = sr3luahost::WeaponSlotState;

public:

    // inv_weapon_add_temporary (spec-lua-api-behaviour.md Sec4.10) /
    // inv_weapon_remove_temporary (Sec4.11), batch 2026-10-08 (the
    // 6-native clearance fast-follow). Both natives share ONE real
    // "dedicated weapon-definition lookup" (CONFIRMED, Sec4.10/Sec4.11's
    // own 2026-10-08 disassembly: a case-insensitive linear scan - the
    // real public C runtime `_stricmp` - over a fixed-stride, 0x7ec-byte,
    // global weapon-definition table at `0x028dc4dc`, filtered to records
    // with flags bit `0x4` at `+0x18`). This project has a SEPARATE,
    // already-built clean-room `weapons.xtbl` CONTENT reader
    // (sr3tables_weapons, include/sr3tables_weapons/weapons.h) but nothing
    // anywhere wires that static table CONTENT into the ENGINE's own
    // runtime array this spec describes (a genuinely different, in-scope-
    // elsewhere concern - building that loader is out of this task's own
    // scope). So, matching this project's own standing convention for a
    // real lookup table with no backing population modeled yet
    // (lookupNotorietyTrackCode()/resolveInventorySlotId() above are the
    // closest precedent: a flat, test-settable map, "OPEN until set," not
    // a guessed real value), this is a flat, test-settable resolver - OPEN
    // (throws) for any weapon name no test (or, in a future pass, a real
    // weapon-table loader) has registered. Case-folded via the SAME
    // AsciiLowerFold() this project's zone-object resolver already
    // established for ITS OWN real, independently-confirmed `_stricmp`-
    // equivalent case-insensitive lookup (sr3luahost/zone_object_
    // resolver.h) - the natural, already-built place for this project's
    // one shared "fold like the real engine's case-insensitive compare"
    // convention to live, rather than a second, separately-written fold.
    OpenValueMap<bool>& weaponDefinitionResolves() { return weaponDefinitionResolves_; }
    bool weaponDefinitionTableComplete() const { return weaponDefinitionTableComplete_; }
    // weapons.xtbl `Weapon_Class` (spec-tables-weapons-combat.md Sec2.5: 5 = `thrown`) per row name,
    // for the force_throw_do "holds a thrown/grenade-class weapon " rule (Sec55.4,
    // 2026-10-12). Optional: a host that installs only names leaves every class unknown.
    static constexpr int32_t kWeaponClassThrown = 5;

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec30 ("ranking
    // tranche 03") - see lua_spec_confirmed_stubs.cpp's own file-header
    // note above these functions for the full per-function citation. ---

    // Sec30.3 `auto_pickup_enable` (0x00a3c880): no arguments; writes the
    // hardcoded byte 1 to global 0x01308698. CONFIRMED: "file-backed
    // default: enabled" - so this project's own default is true (the
    // spec's own stated starting value, not a guess), matching the
    // existing cribWeaponAddEnabled_ precedent (Sec28.13/Sec28.14, "CONFIRMED
    // file value 1"). The call always re-sets it true (this entry "can
    // only enable" per spec); there is no in-scope disable counterpart
    // (the adjacent registrar slot's own disable sibling is HYPOTHESIS,
    // not dumped, and not one of this tranche's 25 confirmed names).
    bool& autoPickupEnabled() { return autoPickupEnabled_; }

    // auto_pickup_disable (Sec33.5): CONFIRMED - writes a named, session-
    // synchronized variable "allow_weapon_auto_pickup" off; no in-scope
    // function reads it back, and the spec text gives no confirmed initial
    // value, so this project's own chosen default (true, "enabled") only
    // matters for a test that checks it before any call - same "write-
    // only in scope, plain chosen default" convention as cribWeaponAddEnabled_.
    bool& allowWeaponAutoPickup() { return allowWeaponAutoPickup_; }
    SatelliteWeaponController& satelliteWeapon() { return satelliteWeapon_; }

    // Sec32.6 `item_show`: +0x3b bit 0x1 ("hidden"), write-only in scope
    // (CHOSEN per-name map; item_show always clears it - "shows" - per
    // Sec32.6's own text; the HYPOTHESIS sibling `item_hide` is out of this
    // batch's scope and not implemented).
    std::unordered_map<std::string, bool>& itemHidden() { return itemHidden_; }
    std::unordered_map<std::string, ItemAnimState>& itemAnimState() { return itemAnimState_; }

    // Weapon pickup (spec-weapons-runtime.md Sec5.1) -----------------------
    //
    // Two confirmed-shape paths, both funnelling through ONE shared give/
    // equip writer rather than duplicating weapon-grant logic:
    //
    //   1. Walk-over auto-pickup for ordinary (non-heavy) enemy-dropped
    //      weapons - registerWeaponDrop()/updateWeaponDropPickup() below.
    //      The REAL write side is CONFIRMED (0x00B84330 -> 0x00601380,
    //      Sec5.1's "real write path, traced end-to-end" paragraph) and the
    //      BEHAVIOUR is user-confirmed (walking over a dropped weapon picks
    //      it up automatically, except heavy pickup-only weapons). What is
    //      NOT confirmed is the real per-frame proximity TRIGGER condition
    //      itself: Sec5.1 names `0x0064c580`/`0x0064bef0` as the strongest
    //      candidate grant functions (position-match scan against a
    //      REGISTERED-SLOT array, no button press), but flags them as more
    //      likely a fixed "weapon cache station" design than the literal
    //      "walk over an NPC-dropped gun" case, and the actual per-frame
    //      dispatcher that fires them was not found at all (reached only
    //      via DATA table slots, zero direct-call xrefs - Sec5.1's own
    //      2026-10-11 changelog entry). So the exact radius/shape used here
    //      is CHOSEN, static search exhausted (same HANDOFF.md framing as
    //      this project's shop-entry stand-in, commit 36470e8) - see
    //      kWeaponDropPickupRadius below for the precise label.
    //
    //   2. Press-E pickup for heavy/"pickup-only" weapons (minigun,
    //      flamethrower, similar) - registerHeavyWeaponPickupTrigger()/
    //      updateHeavyWeaponPickupPrompt()/pressHeavyWeaponPickupInteract()
    //      below. Reuses the EXACT SAME two generic mechanisms the shop/
    //      crib unit above established (TriggerVolumeGeometry/
    //      TriggerVolumeContainsPoint for "is the player inside," and
    //      recordHudPrompt()/recordHudPromptDefaultReset() for "show the
    //      press-E prompt"), wired to interaction-array entry 0 instead of
    //      entry 8 (Sec5.1's 2026-10-11 "pickup-only interaction-prompt
    //      path" update: `0x0111f2d0[0]` -> `0x009da510` -> `0x00971b10` ->
    //      per-kind handler table `0x01309f10` -> kind handler
    //      `0x00972a70`, which calls the already-confirmed `+0x1b78`
    //      weapon-inventory locator `0x005fe460` directly - a genuine
    //      "pick up/exchange a weapon via interact prompt" action,
    //      independent of 0x00B84330's own callers). Same posture as
    //      shop/crib's own trigger-volume stand-in: the real `+0x18e0`
    //      per-"kind" table lookup and bit-availability machinery are NOT
    //      modelled (this project has no C++ model of that engine-side
    //      table at all), but the GRANT itself, once triggered, calls the
    //      real confirmed give/equip writer below - not invented.
    //
    // Both paths' weapon-definition/ammo values are caller-supplied (this
    // project has no xtbl-backed weapon-definition resolver wired to this
    // unit) - same "caller already knows what it wants to grant, this
    // project only models the grant mechanism" boundary as
    // registerShopCribTrigger()'s own open-target handling above.

    // Heavy/"pickup-only" weapon classification. The spec's own `+0x10`
    // flags-dword bits documented so far (0x200000 "has secondary ammo,"
    // 0x80000000 "runtime unlocked," Sec5.1/Sec26.7) do NOT include any bit
    // identified as "pickup-only" - the user's own ground truth (Sec5.1,
    // 2026-10-11) names exactly two examples ("minigun, flamethrower and
    // similar") with no further enumeration. CHOSEN, static search
    // exhausted: classify by Weapon_Class enum value (spec-tables-weapons-
    // combat.md's own 23-entry table) against literally the two named
    // examples only - "flamethrower" (index 14) and "minigun" (index 17).
    // No other Weapon_Class value is treated as heavy/pickup-only here; a
    // future pass with a more specific spec item (e.g. a real confirmed
    // flag bit, or the real `+0x18e0` kind value Sec5.1's own 2026-10-11
    // update says is "plausibly" this class) should replace this enum-value
    // check rather than silently growing this list by guesswork.
    static constexpr int kHeavyPickupOnlyWeaponClassFlamethrower = 14;
    static constexpr int kHeavyPickupOnlyWeaponClassMinigun = 17;
    uint64_t weaponGiveEquipCount() const { return weaponGiveEquipCount_; }
    uint32_t lastWeaponGiveEquipId() const { return lastWeaponGiveEquipId_; }

    // --- Path 1: walk-over auto-pickup for non-heavy dropped weapons ------

    // CHOSEN proximity radius, static search exhausted (see this unit's own
    // top doc comment for why: the real per-frame dispatcher that would
    // confirm an exact condition was not found by any static pass, Sec5.1).
    // A flat 1.5-metre sphere around the drop's registered position.
    static constexpr double kWeaponDropPickupRadius = 1.5;
    uint64_t weaponDropAutoPickupCount() const { return weaponDropAutoPickupCount_; }
    bool heavyWeaponPickupPromptShowing() const { return !activeHeavyWeaponPickupTrigger_.empty(); }
    const std::string& activeHeavyWeaponPickupTrigger() const { return activeHeavyWeaponPickupTrigger_; }
    uint64_t heavyWeaponPickupInteractCount() const { return heavyWeaponPickupInteractCount_; }
    WeaponSlotState& weaponSlots() { return weaponSlots_; }

    // player_grenades_disable (Sec21.17): the exact enable/disable pair for
    // the NOT-in-this-batch `player_grenades_enable` (Sec18.25, out of
    // scope) over the SAME two real globals (0x0140e6e7/0x0140e6f4). CHOSEN
    // false defaults (no in-scope writer touches them before the first
    // disable call either way).
    bool& grenadesDisableFlagA() { return grenadesDisableFlagA_; } // 0x0140e6e7, always set true by disable()
    bool& grenadesDisableFlagB() { return grenadesDisableFlagB_; } // 0x0140e6f4, set true only when disable(true)

    // inv_item_remove_in_slot (Sec21.4) / inv_item_equip_slot (Sec21.23):
    // the real slot-name->id conversion (0x00a25f80) first tries a general
    // slot-name table (0x008dc6b0, no CONFIRMED concrete entries given -
    // test-settable via slotNameToId_ below), falling back to FIVE literal,
    // case-insensitive special names read directly from `.rdata`
    // (CONFIRMED, Sec21.23's own text): "best"->0xd, "best-firearm"->0xe,
    // "best-melee"->0xf, "best-non-lethal"->0x10, "best-non-explosive"->
    // 0x11; anything else -> 0xff ("not found"). Shared by BOTH natives
    // (CONFIRMED, Sec21.4's own text: "the SAME 0x00a25f80 lookup item 21.4
    // uses").
    std::unordered_map<std::string, int32_t> slotNameToId_;
    void registerInventorySlotIdForTesting(const std::string& slotName, int32_t id) { slotNameToId_[slotName] = id; }

    // inv_item_equip_slot (Sec21.23): the dispatcher's own ~12 special-
    // selector case bodies (0x005fec20/0x005ff220/.../0x004f0400) are OPEN
    // (none independently decompiled, consistent with this document's own
    // practice for a large shared routine outside a task's specific scope)
    // - a dedicated, test-settable map from selector code (as a string key)
    // to "the item this selector would resolve to," OPEN until a test sets
    // one.
    OpenValueMap<std::string>& bestWeaponSelectionByCode() { return bestWeaponSelectionByCode_; }

    // npc_drop_grenades (Sec24.28): a bare global boolean, no resolver, no
    // record (CONFIRMED, Sec24.28's own text) - CHOSEN default false.
    bool& npcDropGrenades() { return npcDropGrenades_; }

    // --- vehicle_disable_weapon_physics (Sec38.1) --------------------------
    // GLOBAL byte (no vehicle name argument at all, CONFIRMED): "no host
    // gate, no replication, and exactly ONE writer anywhere in the binary -
    // once a script disables it, it stays disabled for the rest of the
    // process, across missions and loads." The stub writes the LOGICAL
    // NOT of the (optional, default-true) argument (CONFIRMED) - modelled
    // here as an "enabled" sense field (so the CONFIRMED "no-argument call
    // disables the effect" case, NOT(true)=false, reads naturally as
    // "not enabled"). CHOSEN true initial (weapon physics enabled at boot).
    bool& weaponPhysicsEnabled() { return weaponPhysicsEnabled_; }
    // (reuses the existing screenFadeRequest()/Sec26.24 mechanism for its own fade call)

    // --- Sec26.19 game_set_equipped_grenade: -1 = none; CONFIRMED internal
    // slots 0-3 map to Lua-facing ids 8-11.
    int32_t& equippedGrenadeSlot() { return equippedGrenadeSlot_; }

    // --- Inventory (§41.3/§54.3, 6 names: inv_item_add_ammo/_add_do/
    // _get_weapon_upgrades/_dual_wield/_get_anim_group/_get_weapon_slot).
    // No InventoryState/weapon-table/ammo-pool model exists anywhere in
    // this project (confirmed by search) - new, minimal per-character/
    // per-item maps, CHOSEN plain-map convention (same shape as
    // spawnRegionFilterBits_ above). Sentinels CONFIRMED by spec: anim
    // group "Default" (§54.3), weapon slot 255 (§54.3), weapon-upgrade
    // lookup miss -1 alone (§41.3).
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>& inventoryAmmo() { return inventoryAmmo_; }
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>& inventoryItemCounts() { return inventoryItemCounts_; }
    std::unordered_map<std::string, bool>& dualWieldFlags() { return dualWieldFlags_; }
    std::unordered_map<std::string, std::string>& itemAnimGroups() { return itemAnimGroups_; }
    std::unordered_map<std::string, int32_t>& itemWeaponSlots() { return itemWeaponSlots_; }
    std::unordered_map<std::string, int32_t>& weaponUpgradeLevels() { return weaponUpgradeLevels_; }

    // player_grenades_enable (Sec18.25): genuinely zero-argument-sensitive -
    // unconditionally clears two globals to 0 every call, regardless of any
    // script-side argument.
    double& playerGrenadeGlobalA() { return playerGrenadeGlobalA_; }
    double& playerGrenadeGlobalB() { return playerGrenadeGlobalB_; }
    uint64_t& playerGrenadesEnableCallCount() { return playerGrenadesEnableCallCount_; }

    // inv_item_remove (Sec17.25): the item definition lookup (0x008dcb10 ->
    // 0x005feaa0) collapsed to one flat resolves-or-not predicate (same "no
    // fixed id table" class of gap).
    OpenValueMap<bool>& inventoryItemDefinitionResolves() { return inventoryItemDefinitionResolves_; }
    bool inventoryItemTableComplete() const { return inventoryItemTableComplete_; }

    // character_add_prop (Sec17.13): the prop/customization-item lookup
    // (0x006cf0c0/0x00904c10) collapsed to one flat resolves-or-not
    // predicate.
    OpenValueMap<bool>& propDefinitionResolves() { return propDefinitionResolves_; }
    bool items3dTableComplete() const { return items3dTableComplete_; }

protected: // private in EngineState before the split
    // inv_weapon_add_temporary/inv_weapon_remove_temporary (Sec4.10/
    // Sec4.11) - weaponDefinitionResolves()/weaponTemporaryInventory()'s
    // own backing store, see those methods' own doc comments above.
    OpenValueMap<bool> weaponDefinitionResolves_{"weapon-definition name resolution (0x028dc4dc, stride 0x7ec)",
                                                 "spec-lua-api-behaviour.md Sec4.10/Sec4.11"};
    bool weaponDefinitionTableComplete_ = false;
    std::unordered_map<std::string, int32_t> weaponClassByName_;
    std::unordered_map<std::string, std::unordered_map<std::string, WeaponTemporaryInventorySlot>> weaponTemporaryInventory_;

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec30 ("ranking
    // tranche 03") - see each public accessor's own doc comment above. ---
    bool autoPickupEnabled_ = true; // Sec30.3, CONFIRMED file-backed default "enabled"
    bool allowWeaponAutoPickup_ = true; // Sec33.5, CHOSEN default (no confirmed file value in scope)
    SatelliteWeaponController satelliteWeapon_;
    std::unordered_map<std::string, bool> itemHidden_;
    std::unordered_map<std::string, ItemAnimState> itemAnimState_;
    // Weapon pickup (Sec5.1) - see the doc comment above
    // giveEquipWeapon()/registerWeaponDrop()/registerHeavyWeaponPickupTrigger()
    // for the full reasoning (shared give/equip writer is REAL confirmed;
    // the auto-pickup proximity radius and the press-E prompt text are both
    // CHOSEN stand-ins, same posture as the shop-entry trigger above).
    uint64_t weaponGiveEquipCount_ = 0;
    uint32_t lastWeaponGiveEquipId_ = 0;
    std::unordered_map<std::string, WeaponDropRegistration> weaponDrops_;
    uint64_t weaponDropAutoPickupCount_ = 0;
    std::unordered_map<std::string, HeavyWeaponPickupTrigger> heavyWeaponPickupTrigger_;
    std::string activeHeavyWeaponPickupTrigger_;
    uint64_t heavyWeaponPickupInteractCount_ = 0;
    WeaponSlotState weaponSlots_;
    bool grenadesDisableFlagA_ = false;
    bool grenadesDisableFlagB_ = false;
    OpenValueMap<std::string> bestWeaponSelectionByCode_{"inv_item_equip_slot's own best-weapon-selector resolution",
                                                         "spec-lua-api-behaviour.md Sec21.23"};
    bool npcDropGrenades_ = false;
    bool weaponPhysicsEnabled_ = true;
    int32_t equippedGrenadeSlot_ = -1;
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> inventoryAmmo_;
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> inventoryItemCounts_;
    std::unordered_map<std::string, bool> dualWieldFlags_;
    std::unordered_map<std::string, std::string> itemAnimGroups_;
    std::unordered_map<std::string, int32_t> itemWeaponSlots_;
    std::unordered_map<std::string, int32_t> weaponUpgradeLevels_;
    double playerGrenadeGlobalA_ = 0.0;
    double playerGrenadeGlobalB_ = 0.0;
    uint64_t playerGrenadesEnableCallCount_ = 0;
    bool inventoryItemTableComplete_ = false;
    bool items3dTableComplete_ = false;
    OpenValueMap<bool> inventoryItemDefinitionResolves_{"inventory item definition resolution (0x008dcb10/0x005feaa0)",
                                                         "spec-lua-api-behaviour.md Sec17.25"};
    OpenValueMap<bool> propDefinitionResolves_{"character prop/customization-item definition resolution",
                                               "spec-lua-api-behaviour.md Sec17.13"};
};

} // namespace sr3luahost
