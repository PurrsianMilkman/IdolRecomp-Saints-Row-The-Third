// EngineState method definitions moved verbatim out of include/sr3luahost/engine_state.h
// (structural refactor, step 1b: no behaviour change). Area: triggers_weapons.

#include "sr3luahost/engine_state.h"

namespace sr3luahost {

void EngineState::installWeaponDefinitionNames(const std::vector<std::string>& names) {
        for (const std::string& n : names) weaponDefinitionResolves_.set(AsciiLowerFold(n), true);
        weaponDefinitionTableComplete_ = true;
    }

void EngineState::setWeaponDefinitionClass(const std::string& name, int32_t weaponClass) {
        weaponClassByName_[AsciiLowerFold(name)] = weaponClass;
    }

bool EngineState::characterHoldsThrownWeapon(const std::string& character) const {
        auto inv = weaponTemporaryInventory_.find(character);
        if (inv == weaponTemporaryInventory_.end()) return false;
        for (const auto& kv : inv->second) {
            auto cls = weaponClassByName_.find(kv.first);
            if (cls != weaponClassByName_.end() && cls->second == kWeaponClassThrown && kv.second.temporaryGrant)
                return true;
        }
        return false;
    }

bool EngineState::weaponDefinitionResolve(const std::string& foldedName) {
        if (weaponDefinitionTableComplete_ && !weaponDefinitionResolves_.known(foldedName)) return false;
        return weaponDefinitionResolves_.get(foldedName);
    }

std::unordered_map<std::string, std::unordered_map<std::string, WeaponTemporaryInventorySlot>>&
    EngineState::weaponTemporaryInventory() {
        return weaponTemporaryInventory_;
    }

void EngineState::recordTriggerWaitFrame(const std::string& triggerName) {
        triggerWaitStall_.framesWaiting =
            (triggerName == triggerWaitStall_.triggerName) ? (triggerWaitStall_.framesWaiting + 1) : 1;
        triggerWaitStall_.triggerName = triggerName;
    }

void EngineState::noteTriggerRegistrationOrder(const std::string& triggerName) {
        for (const std::string& existing : triggerRegistrationOrder_) {
            if (existing == triggerName) return;
        }
        triggerRegistrationOrder_.push_back(triggerName);
    }

// Design notes moved verbatim from EngineState::recordTriggerEnable's declaration in engine_state.h
// (refactor step 3a):
// trigger_enable (Sec1.1). CONFIRMED: resolve+liveness gate (the shared
// objectResolves() map, standing in for the real `0x005e4e30`/
// `0x00853b10` pair per Sec29's "one shared name map" convention), then
// "mutates trigger-subsystem state for the named trigger (an
// enable/disable toggle plus a flag-gated secondary behaviour switch)".
// This project has no real trigger-subsystem simulation (same
// boundary as every other placed-object setter in this header) - only
// the raw (enabled, flags) pair actually passed is recorded, honestly,
// as "a request" (same convention as MinimapIconRecord/city_zone_swap
// above), never the real two tail setters' own internal effects.
void EngineState::recordTriggerEnable(const std::string& name, bool enabled, int64_t flags) {
        triggerEnable_[name] = {enabled, flags};
    }

bool EngineState::triggerEnableValue(const std::string& name) const {
        auto it = triggerEnable_.find(name);
        return it != triggerEnable_.end() && it->second.enabled;
    }

int64_t EngineState::triggerEnableFlagsValue(const std::string& name) const {
        auto it = triggerEnable_.find(name);
        return it != triggerEnable_.end() ? it->second.flags : 0;
    }

// Design notes moved verbatim from EngineState::registerOnTriggerCallback's declaration in engine_state.h
// (refactor step 3a):
// on_trigger (Sec1.2). CONFIRMED (full re-derivation 2026-10-07): the
// per-trigger `+0xa8` gate is never written anywhere inside
// `0x00a58680`/`0x00a58720`/`0x00a56cb0` themselves, and this project
// models no OTHER writer at all (none is in scope) - so in this closed
// world the gate always reads its zero-filled-allocation default
// (unset), meaning registration always proceeds once the trigger
// resolves. The real per-trigger deadline recompute (`+0x88`) has no
// in-scope reader (no Lua-visible "read this trigger's deadline"
// function is among this batch's 12 names) and is therefore not
// modeled - only the CONFIRMED, testable half of the contract (resolve-
// gate, then register-or-not) is reproduced, same "record the real
// branch, not the unreadable internal bookkeeping" convention as
// mesh_mover_reset/sidewalk_disable_nodes elsewhere in this header.
void EngineState::registerOnTriggerCallback(const std::string& triggerName, const std::string& callbackName) {
        onTriggerCallbacks_[triggerName] = callbackName;
    }

const std::string& EngineState::onTriggerCallbackValue(const std::string& triggerName) const {
        static const std::string kEmpty;
        auto it = onTriggerCallbacks_.find(triggerName);
        return it != onTriggerCallbacks_.end() ? it->second : kEmpty;
    }

// Design notes moved verbatim from EngineState::registerOnTriggerExitCallback's declaration in engine_state.h
// (refactor step 3a):
// --- Batch 2026-10-08, wave 1 batch 1: spec-lua-api-behaviour.md §1-4
// trigger/mission/teleport/combat-hook core. 15 of the batch's
// originally-assigned 21 names were actually cleared for
// implementation; 6 (on_dismiss §1.11, character_prevent_flinching
// §3.14, character_allow_ragdoll §3.16, npc_combat_enable §3.17,
// inv_weapon_add_temporary §4.10, inv_weapon_remove_temporary §4.11)
// each still carried their OWN original 2026-09-30 "NOT yet cleared for
// implementation" Review-status line with no later dated resolution
// anywhere in the document at the time (independently verified by
// reading every one of this batch's 21 sections directly, not assumed
// from the scoping report's own count) - deferred, not implemented at
// the time. See lua_spec_confirmed_stubs_b08_batch1_core.cpp's own top
// comment for the full original citation of each. ~~DONE 2026-10-08
// (later the same day): spec-lua-api-behaviour.md's own text for all 6
// was independently re-derived and dated "cleared for implementation"
// (Sec1.11/Sec3.14/Sec3.16/Sec3.17/Sec4.10/Sec4.11's own Review-status
// lines) - all 6 are now implemented, in
// lua_spec_confirmed_stubs_b28_batch1_deferred6.cpp. See that file's
// own top comment, CharacterState::onDismissCallback/
// kPreventFlinchingBit1c98/kNeverRagdollBit1c98/
// humanAiDataForceFlagsByte8 (character_state.h), and
// weaponDefinitionResolves()/weaponTemporaryInventory() below for the
// supporting state.~~ ------------------------------------------------

// on_trigger_exit (Sec1.7): structurally identical to registerOnTriggerCallback
// above (on_trigger, Sec1.2) but a SEPARATE, adjacent per-trigger
// pending-state dword (+0xfc, not +0xf8) and literal mode value 1 (not
// 0) - a second, independent callback slot on the SAME trigger
// object. Same "+0xa8 gate always reads unset in this closed world"
// reasoning as registerOnTriggerCallback (registration always
// proceeds once the trigger resolves).
void EngineState::registerOnTriggerExitCallback(const std::string& triggerName, const std::string& callbackName) {
        onTriggerExitCallbacks_[triggerName] = callbackName;
    }

const std::string& EngineState::onTriggerExitCallbackValue(const std::string& triggerName) const {
        static const std::string kEmpty;
        auto it = onTriggerExitCallbacks_.find(triggerName);
        return it != onTriggerExitCallbacks_.end() ? it->second : kEmpty;
    }

double EngineState::triggerRadiusForGeometry(const TriggerVolumeGeometry& g) {
        if (g.shapeCode == 1) return g.radius;
        if (g.shapeCode == 2) return g.radius; // +0x94 = radius (f752324); length is +0x90
        if (g.shapeCode == 0) {
            float dx = float(g.boxMaxX - g.boxMinX), dy = float(g.boxMaxY - g.boxMinY), dz = float(g.boxMaxZ - g.boxMinZ);
            float sum = float(double(dx) * dx + double(dy) * dy + double(dz) * dz);
            return double(float(0.5 * std::sqrt(double(sum))));
        }
        return 0.0;
    }

void EngineState::registerTriggerGeometry(const std::string& triggerName, const TriggerVolumeGeometry& geometry) {
        triggerGeometry_[triggerName] = geometry;
    }

const EngineState::TriggerVolumeGeometry* EngineState::triggerGeometry(const std::string& triggerName) const {
        auto it = triggerGeometry_.find(triggerName);
        return it != triggerGeometry_.end() ? &it->second : nullptr;
    }

// Design notes moved verbatim from EngineState::TriggerVolumeContainsPoint's declaration in engine_state.h
// (refactor step 3a):
// The real containment test, CONFIRMED shape (Sec1.14, `0x0093bfc0`):
// three shape codes, no others. Box is tested axis-aligned (this
// project does not decode a placed object's own orientation quaternion
// for a generic Trigger_Volume - same boundary sr3zone::
// DecodePropertyAsPosition's own doc comment already states for
// Navpoint/cover_node). Never throws; an unknown shapeCode (anything
// but 0/1/2) reports not-contained, matching Sec1.14's own "any other
// value fails the containment test" text.
bool EngineState::TriggerVolumeContainsPoint(const TriggerVolumeGeometry& g, double x, double y, double z) {
        const double dx = x - g.px, dy = y - g.py, dz = z - g.pz;
        switch (g.shapeCode) {
            case 1: // sphere
                return (dx * dx + dy * dy + dz * dz) <= (g.radius * g.radius);
            case 0: // oriented box - axis-aligned here, see this struct's own doc comment.
                     // REAL on-disk min/max corner (boxMin/boxMax), not a symmetric half-extent.
                return dx >= g.boxMinX && dx <= g.boxMaxX && dy >= g.boxMinY && dy <= g.boxMaxY &&
                       dz >= g.boxMinZ && dz <= g.boxMaxZ;
            case 2: { // capped cylinder: axis from (px,py,pz) to (px+ex,py+ey,pz+ez)
                const double segLen2 = g.ex * g.ex + g.ey * g.ey + g.ez * g.ez;
                const double t = segLen2 > 1e-12 ? (dx * g.ex + dy * g.ey + dz * g.ez) / segLen2 : 0.0;
                if (t < 0.0 || t > 1.0) return false;
                const double cx = g.px + g.ex * t, cy = g.py + g.ey * t, cz = g.pz + g.ez * t;
                const double rdx = x - cx, rdy = y - cy, rdz = z - cz;
                return (rdx * rdx + rdy * rdy + rdz * rdz) <= (g.radius * g.radius);
            }
            default:
                return false;
        }
    }

// Design notes moved verbatim from EngineState::TriggerVolumeRepresentativePoint's declaration in engine_state.h
// (refactor step 3a):
// --auto-objective's own trigger-stall teleport target (feature/auto-
// objective-trigger-vehicle-seat, 2026-10-12): a representative point
// GUARANTEED (for every shape this project decodes) to satisfy
// TriggerVolumeContainsPoint() above for the SAME geometry - the
// trigger-volume analogue of get_dist's own "warp to the target's real
// resolved position" teleport. Sphere/box/cylinder all return a point on
// (box/cylinder) or at (sphere) the shape's own real center, never an
// edge/corner, so a player warped here lands solidly inside, not right
// on a containment boundary.
WorldPosition EngineState::TriggerVolumeRepresentativePoint(const TriggerVolumeGeometry& g) {
        switch (g.shapeCode) {
            case 0: // box: midpoint of the real on-disk min/max corner, offset from (px,py,pz)
                return WorldPosition{g.px + (g.boxMinX + g.boxMaxX) * 0.5,
                                      g.py + (g.boxMinY + g.boxMaxY) * 0.5,
                                      g.pz + (g.boxMinZ + g.boxMaxZ) * 0.5};
            case 2: // capped cylinder: midpoint of the axis (px,py,pz)-(px+ex,py+ey,pz+ez)
                return WorldPosition{g.px + g.ex * 0.5, g.py + g.ey * 0.5, g.pz + g.ez * 0.5};
            case 1: // sphere
            default:
                return WorldPosition{g.px, g.py, g.pz};
        }
    }

std::string EngineState::triggerContainsObjectKey(const std::string& triggerName, const std::string& objectName) {
        return triggerName + "\x1f" + objectName;
    }

void EngineState::registerShopCribTrigger(const std::string& triggerName, ShopCribTriggerKind kind) {
        shopCribTrigger_[triggerName] = kind;
    }

bool EngineState::isShopCribTrigger(const std::string& triggerName) const {
        return shopCribTrigger_.count(triggerName) != 0;
    }

std::optional<EngineState::ShopCribTriggerKind> EngineState::shopCribTriggerKind(const std::string& triggerName) const {
        auto it = shopCribTrigger_.find(triggerName);
        if (it == shopCribTrigger_.end()) return std::nullopt;
        return it->second;
    }

// Design notes moved verbatim from EngineState::updateShopCribTriggerPrompt's declaration in engine_state.h
// (refactor step 3a):
// Per-frame-equivalent update, given the player's current world
// position: finds whether the player is now inside ANY registered
// shop/crib trigger (first match wins - this project does not model
// overlapping shop/crib volumes) and shows/clears the "press E" prompt
// on a TRANSITION only, matching this project's own pre-existing
// trigger-poll "only re-fire on enter/exit, never every frame while
// inside" convention (triggerCurrentlyInside()'s own doc comment,
// Sec1.14). Returns the name of the trigger now active, or an empty
// string if the player is not inside any shop/crib trigger - mirrors
// activeShopCribTrigger() below, exposed as a return value purely so a
// caller/test does not need a second call to observe the transition.
const std::string& EngineState::updateShopCribTriggerPrompt(double playerX, double playerY, double playerZ) {
        std::string nowInside;
        for (const auto& kv : shopCribTrigger_) {
            const TriggerVolumeGeometry* g = triggerGeometry(kv.first);
            if (!g) continue;
            if (TriggerVolumeContainsPoint(*g, playerX, playerY, playerZ)) {
                nowInside = kv.first;
                break;
            }
        }
        if (nowInside != activeShopCribTrigger_) {
            activeShopCribTrigger_ = nowInside;
            if (!activeShopCribTrigger_.empty()) {
                // Reuses hud_prompt's own existing request shape (target,
                // arg2, arg3). arg2 is this stand-in's own open-target
                // document name (see pressShopCribInteract() below for which
                // one is REAL vs CHOSEN); the real prompt's own icon/text
                // argument shape (e.g. the real "HUD_USE_CRIB" string
                // confirmed by b8c17ec for the crib case specifically) is
                // not reproduced here - CHOSEN for both cases.
                recordHudPrompt(activeShopCribTrigger_, openTargetForTrigger(activeShopCribTrigger_), "press_e_enter");
            } else {
                recordHudPromptDefaultReset();
            }
        }
        return activeShopCribTrigger_;
    }

// Design notes moved verbatim from EngineState::pressShopCribInteract's declaration in engine_state.h
// (refactor step 3a):
// Press-E while the prompt is showing -> opens the matching document.
// For a kCrib trigger this is now the REAL confirmed open-target
// ("store_crib", via the full b8c17ec chain documented above) - for a
// kShop trigger it remains this project's own CHOSEN stand-in (target
// "store_common" is itself the one REAL, confirmed-by-disassembly piece
// for shop, per spec-store-runtime.md Sec1 - only the trigger-index/
// call-path THAT REACHES it is still unidentified for shop specifically).
// No-op (returns false) if no shop/crib prompt is currently showing -
// matches this engine's own "a button press only does something if its
// matching prompt is live" shape (the vehicle-enter prompt is the same
// shape, spec-player-runtime.md Sec13.1). On success, increments
// uiScreenStackDepth() (the existing generic screen-stack-push signal
// this project's pop_screen counterpart, popScreenCount()/
// uiScreenStackDepth(), already uses) AND a dedicated
// documentOpenCount()/lastDocumentOpened()/lastDocumentOpenTrigger()
// triple, so a test/caller can observe "a screen opened" both generically
// and specifically-attributed, including WHICH document name opened.
bool EngineState::pressShopCribInteract() {
        if (activeShopCribTrigger_.empty()) return false;
        ++documentOpenCount_;
        lastDocumentOpenTrigger_ = activeShopCribTrigger_;
        lastDocumentOpened_ = openTargetForTrigger(activeShopCribTrigger_);
        ++uiScreenStackDepth();
        return true;
    }

std::string EngineState::openTargetForTrigger(const std::string& triggerName) const {
        auto it = shopCribTrigger_.find(triggerName);
        if (it != shopCribTrigger_.end() && it->second == ShopCribTriggerKind::kCrib) return "store_crib";
        return "store_common";
    }

bool EngineState::IsHeavyPickupOnlyWeaponClass(int weaponClass) {
        return weaponClass == kHeavyPickupOnlyWeaponClassFlamethrower ||
               weaponClass == kHeavyPickupOnlyWeaponClassMinigun;
    }

// Design notes moved verbatim from EngineState::giveEquipWeapon's declaration in engine_state.h
// (refactor step 3a):
// The shared give/equip writer BOTH pickup paths call - models
// Sec5.1's REAL confirmed write path: `0x00B84330` resolves/binds the
// target's slot via `0x00601380` (the slot create/bind call), then
// writes flags (+0x1a0/+0x1a1), primary ammo (+0x1a4), rounds-in-clip
// (+0x1ac - the SAME field Sec6's already-closed item names as the live
// clip counter), and optional secondary ammo (+0x1b0/+0x1b4) onto the
// resolved per-CHARACTER (+0x1b78) slot record. This project's closest
// existing live model of that `+0x1b78` slot record is
// SaveGameState::weaponInventory's WeaponInventoryItem (save_game_
// state.h - spec-save-format.md Sec10.3's own 4-ammo-field/2-flag-byte
// shape for the SAME save-format span `0x008DCC00` separately fills,
// Sec5.1's own cross-reference), already noted there as "a future
// weapon-pickup native appends to it directly" - reused here rather
// than inventing a second, parallel slot representation. Does NOT model
// the vehicle-mount ammo cap (+0x2c4) - gated on session/global flags
// this project does not track, out of scope for a pickup writer.
void EngineState::giveEquipWeapon(uint32_t weaponDefId, int32_t primaryAmmo, int32_t roundsInClip,
                          int32_t secondaryAmmo, uint8_t flag0, uint8_t flag1) {
        auto& inv = saveGame_.weaponInventory;
        auto it = std::find_if(inv.begin(), inv.end(),
                                [&](const WeaponInventoryItem& w) { return w.weaponDefId == weaponDefId; });
        if (it == inv.end()) {
            inv.push_back(WeaponInventoryItem{});
            it = inv.end() - 1;
        }
        it->weaponDefId = weaponDefId;
        it->ammo[0] = primaryAmmo;
        it->ammo[1] = roundsInClip;
        it->ammo[2] = secondaryAmmo;
        it->flag0 = flag0;
        it->flag1 = flag1;
        ++weaponGiveEquipCount_;
        lastWeaponGiveEquipId_ = weaponDefId;
    }

const WeaponInventoryItem* EngineState::weaponSlot(uint32_t weaponDefId) const {
        const auto& inv = saveGame_.weaponInventory;
        auto it = std::find_if(inv.begin(), inv.end(),
                                [&](const WeaponInventoryItem& w) { return w.weaponDefId == weaponDefId; });
        return it == inv.end() ? nullptr : &*it;
    }

void EngineState::registerWeaponDrop(const std::string& dropName, double x, double y, double z,
                             uint32_t weaponDefId, int weaponClass, int32_t primaryAmmo, int32_t roundsInClip) {
        weaponDrops_[dropName] = WeaponDropRegistration{x, y, z, weaponDefId, primaryAmmo, roundsInClip,
                                                          weaponClass, false};
    }

// Design notes moved verbatim from EngineState::updateWeaponDropPickup's declaration in engine_state.h
// (refactor step 3a):
// Per-frame-equivalent update, given the player's current world
// position: grants (via giveEquipWeapon(), the ONE shared writer) every
// registered, not-yet-consumed drop within kWeaponDropPickupRadius
// whose weapon class is NOT heavy/pickup-only, then marks it consumed
// (one-shot - a real dropped weapon does not respawn after pickup).
// Heavy/pickup-only drops are deliberately skipped here - per the
// user's own confirmed behaviour, they are only ever granted through
// the press-E path below, never by walking over them.
void EngineState::updateWeaponDropPickup(double playerX, double playerY, double playerZ) {
        for (auto& kv : weaponDrops_) {
            WeaponDropRegistration& drop = kv.second;
            if (drop.consumed) continue;
            if (IsHeavyPickupOnlyWeaponClass(drop.weaponClass)) continue;
            const double dx = playerX - drop.x, dy = playerY - drop.y, dz = playerZ - drop.z;
            if ((dx * dx + dy * dy + dz * dz) <= kWeaponDropPickupRadius * kWeaponDropPickupRadius) {
                giveEquipWeapon(drop.weaponDefId, drop.primaryAmmo, drop.roundsInClip);
                drop.consumed = true;
                ++weaponDropAutoPickupCount_;
            }
        }
    }

bool EngineState::isWeaponDropConsumed(const std::string& dropName) const {
        auto it = weaponDrops_.find(dropName);
        return it != weaponDrops_.end() && it->second.consumed;
    }

void EngineState::registerHeavyWeaponPickupTrigger(const std::string& triggerName, uint32_t weaponDefId,
                                           int32_t primaryAmmo, int32_t roundsInClip) {
        heavyWeaponPickupTrigger_[triggerName] = HeavyWeaponPickupTrigger{weaponDefId, primaryAmmo, roundsInClip};
    }

// Design notes moved verbatim from EngineState::updateHeavyWeaponPickupPrompt's declaration in engine_state.h
// (refactor step 3a):
// Same transition-only enter/exit shape as updateShopCribTriggerPrompt()
// above (triggerCurrentlyInside()'s own "only re-fire on enter/exit"
// convention, Sec1.14) - reuses the SAME registerTriggerGeometry()/
// triggerGeometry()/TriggerVolumeContainsPoint() machinery, just over a
// separate trigger-name map so a shop/crib trigger and a heavy-weapon
// pickup trigger never collide on the same registered name.
const std::string& EngineState::updateHeavyWeaponPickupPrompt(double playerX, double playerY, double playerZ) {
        std::string nowInside;
        for (const auto& kv : heavyWeaponPickupTrigger_) {
            const TriggerVolumeGeometry* g = triggerGeometry(kv.first);
            if (!g) continue;
            if (TriggerVolumeContainsPoint(*g, playerX, playerY, playerZ)) {
                nowInside = kv.first;
                break;
            }
        }
        if (nowInside != activeHeavyWeaponPickupTrigger_) {
            activeHeavyWeaponPickupTrigger_ = nowInside;
            if (!activeHeavyWeaponPickupTrigger_.empty()) {
                // arg2/arg3 are this stand-in's own CHOSEN prompt-shape
                // values (same footing as updateShopCribTriggerPrompt()'s
                // own "press_e_enter" literal above) - the real prompt-text
                // id for this specific interaction-array entry 0 kind was
                // not independently confirmed this pass.
                recordHudPrompt(activeHeavyWeaponPickupTrigger_, "weapon_pickup", "press_e_pickup");
            } else {
                recordHudPromptDefaultReset();
            }
        }
        return activeHeavyWeaponPickupTrigger_;
    }

// Design notes moved verbatim from EngineState::pressHeavyWeaponPickupInteract's declaration in engine_state.h
// (refactor step 3a):
// Press-E while the heavy-weapon prompt is showing - grants the
// registered weapon via giveEquipWeapon(), the SAME shared writer
// updateWeaponDropPickup() above calls, proving both paths converge on
// one write path rather than two divergent ones. No-op (returns false)
// if no heavy-weapon pickup prompt is currently showing, matching
// pressShopCribInteract()'s own "a button press only does something if
// its matching prompt is live" shape.
bool EngineState::pressHeavyWeaponPickupInteract() {
        if (activeHeavyWeaponPickupTrigger_.empty()) return false;
        auto it = heavyWeaponPickupTrigger_.find(activeHeavyWeaponPickupTrigger_);
        if (it == heavyWeaponPickupTrigger_.end()) return false;
        giveEquipWeapon(it->second.weaponDefId, it->second.primaryAmmo, it->second.roundsInClip);
        ++heavyWeaponPickupInteractCount_;
        return true;
    }

void EngineState::storeWeaponHideWeapon() {
        if (storeWeaponPreviewActive_) {
            ++storeWeaponDetachCount_;
            storeWeaponPreviewActive_ = false;
        }
        storeWeaponPreviewSlot_ = -1;
        ++storeWeaponHideWeaponCallCount_;
    }

} // namespace sr3luahost
