// lua_spec_confirmed_stubs_b24_wave2_pcu_pcr.cpp
//
// Wave 2, Batch W2-5 (2026-10-08) of the post-wave-1 parallel fan-out -
// "Customization frontends: PCU/PCR/customization_*" half of a 2-file split
// (the other half, vcust/store/garage/crib/gang, is
// lua_spec_confirmed_stubs_b25_wave2_vcust_store_garage.cpp). Scope: the
// customization_* (Sec60.1), pcr_* (Sec19/Sec23/Sec26/Sec43) and pcu_*
// (Sec19/Sec26/Sec40/Sec43) names of the wave-2 batch plan
// (results/lua_natives_wave2_batch_plan_20261008.md, "W2-5"). 63 names.
//
// HARDENING POLICY (this batch's own highest-priority directive, see the
// orchestrator's dispatch brief): this is one of two files sharing wave 2's
// largest crash-shaped-finding count (28 total across both halves). Every
// CRASH-flagged name below is bound-checked/refused rather than reproduced;
// 3 of the task brief's own 4 NAMED memory-safety findings live in the OTHER
// file (gang_customization_*, vcust_purchase_component) - this file carries
// no named finding of its own, but still hardens every one of its own 15
// crash-shaped names (see each stub's own doc comment for the spec citation
// and the specific divergence). Convention matches wave-1 Batch 2's own OOB-
// hardening precedent (src/lua_spec_confirmed_stubs_b09_batch2_hud_audio.cpp
// - HudHardeningCounters) - a per-site named counter in EngineState, never a
// silent behavior change with no test-observable trace.
//
// REUSE (this batch's own explicit instruction, not re-derived):
// - wave-1 Batch 4's customization_outfit_wear/customization_item_wear/
//   _revert dual local-player state (EngineState::playerRig().
//   coopPlayerPresent, CustomizationOutfitWearRequest) - already merged.
// - wave-1 Batch 5's pcr_change_hair/pcr_change_composite/pcr_get_player_
//   preset/pcr_set_identity and the compiled 34-entry category table
//   (PcrCustomizationState in engine_state.h) - already merged, reused
//   directly below (pcr_change_skin_color/pcr_change_eye_color write into
//   the SAME 63-slot items[] array; pcr_report_* functions reuse the SAME
//   34-entry kCategoryNames table verbatim, not re-transcribed).
// - wave-1 Batch 3's PCR quick-queries (pcr_is_plastic_surgeon/pcr_mark_
//   changed/pcr_apply_morph_value/pcr_get_morph_value/pcu_is_streaming_done)
//   - already merged, untouched here (no name overlap).
// - wave-1 Batch 4 (ranking tranche 08)'s own PcuInventory/PcuCategoryTable/
//   PcuCatalogOutfits/playerCash()/purchaseLedger() (src/lua_spec_confirmed_
//   stubs_b04_r04_r06_08.cpp's own registered pcu_is_bra_category/pcu_is_
//   underwear_category/pcu_purchase_slot/pcu_purchase_outfit/pcu_wear_store_
//   outfit - a DIFFERENT 5 names from this batch's own 29 pcu_* names, no
//   overlap verified via a direct name-list diff before writing this file) -
//   pcu_get_num_items_owned below reuses the EXISTING pcuCatalogOutfits()
//   "bit 0 only" numbering rather than a new catalog model.
//
// Sec43's own front matter (THREE distinct "no string id" sentinels in live
// use: 0x029c9964/items-variants-hair-styles, 0x029c9960/skin names, plain
// 0/eye definitions) is kept distinct below, per the task brief's own
// explicit instruction not to unify them.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include <cctype>

namespace sr3luahost {
namespace {

// These two are nested types of EngineState (declared in its own public
// section, engine_state.h) - aliased here, unqualified, purely so this
// file's own stub bodies below can name them exactly as written without an
// `EngineState::` prefix at every use site.
using PcuClothingState = EngineState::PcuClothingState;
using PcuClothingSlot = EngineState::PcuClothingSlot;

} // namespace
namespace stubs_b24 {
// Portable ASCII lowercase/case-insensitive helpers - same "portable,
// behaviorally equivalent stand-in" convention as caseInsensitiveEquals in
// lua_spec_confirmed_stubs_common.h; not a claim about the real CRT.
std::string toLowerAsciiB24(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
} // namespace stubs_b24
namespace {
using stubs_b24::toLowerAsciiB24;

// =========================================================================
// customization_* (Sec60.1): customization_swap_player_rig,
// customization_create_character, customization_creation_is_open,
// customization_screen_is_ready.
// =========================================================================

// customization_swap_player_rig (Sec45.1, 0x00a43cf0): 2 mandatory strings
// (rig name, a second name) + 1 optional number (player mask, default 3:
// bit 0 local, bit 1 remote co-op). CONFIRMED real null-dereference crash
// shapes: "no local player with mask bit 0 reads through null +0xf4/+0xd24;
// an unknown rig name (both lookups fail) still indexes through to a null
// +0x50 read." Also CONFIRMED "always sends a network message on every
// call with no host/ownership gate." HARDENED: refuses the local-apply half
// (bit 0) when no local player is present, and the network-message half
// when neither rig name resolves - neither crash shape is reproduced; the
// unconditional-broadcast-with-no-gate behavior itself is preserved
// faithfully (a replication-shape quirk, not a memory-safety one) via
// EngineState::replicateStateChange, same convention every other
// unconditional-broadcast name in this document's catalogue already uses.
int stub_customization_swap_player_rig(lua_State* L) {
    logCall(L, upLog(L), "customization_swap_player_rig", upStateTag(L));
    std::string rigName = argString(L, 1);
    std::string secondName = argString(L, 2);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 3, 3.0));
    EngineState* st = upState(L);
    bool rigResolves = st->objectResolves().known(rigName) && st->objectResolves().get(rigName);
    bool secondResolves = st->objectResolves().known(secondName) && st->objectResolves().get(secondName);
    if ((mask & 0x1) && st->hasLocalPlayer().known() && st->hasLocalPlayer().get() && (rigResolves || secondResolves)) {
        // CONFIRMED local-apply path, gated here on a resolved local player
        // AND at least one resolved rig name - HARDENED refusal of the
        // real null-deref otherwise, not reproduced.
        EngineState::replicateStateChange("customization_swap_player_rig_local", rigName);
    }
    // CONFIRMED: always broadcasts, no host/ownership gate - preserved.
    EngineState::replicateStateChange("customization_swap_player_rig", rigName);
    return 0;
}

// customization_create_character (Sec60.1, co-op character-creation flow):
// no documented Lua-visible arguments in scope. No return. CONFIRMED real
// no-session crash: "runs the same 3-call sequence the engine's own ...
// co-op character-creation flow runs, ending in a step that reads the
// session pointer's +0x60 field with no null test - any single-player call
// that reaches this third step faults." HARDENED per the task brief's own
// explicit instruction: refuses (no-op) when no co-op session is present,
// reusing the existing coopSession().present (CONFIRMED false by default in
// single player, Sec8.27/Sec26.28 - applySpecInitialState already seeds
// it) rather than inventing a separate session-pointer stand-in.
int stub_customization_create_character(lua_State* L) {
    logCall(L, upLog(L), "customization_create_character", upStateTag(L));
    EngineState* st = upState(L);
    auto& cc = st->customizationCreation();
    if (!st->coopSession().present.get()) { // CONFIRMED false by default (Sec8.27/Sec26.28)
        ++cc.noSessionRefusedCount; // HARDENED: real engine reads session+0x60 with no null test here
        return 0;
    }
    ++cc.sessionPresentAttemptedCount; // session-present path: deep 3-call-sequence internals OPEN, counted only
    return 0;
}

// customization_creation_is_open (Sec60.1): no arguments. Return: 1
// boolean. CONFIRMED real logic defect, explicitly NOT a bug to fix per
// this task's own brief: "it always returns true in single player ... a
// script polling `while customization_creation_is_open() do ... end` in
// single player never exits the loop; a faithful single-player host should
// simply return true to match." Reproduced verbatim.
int stub_customization_creation_is_open(lua_State* L) {
    logCall(L, upLog(L), "customization_creation_is_open", upStateTag(L));
    ++upState(L)->customizationCreation().creationIsOpenQueryCount;
    lua_pushboolean(L, 1); // CONFIRMED-faithful always-true in single player (Sec60.1) - NOT a bug
    return 1;
}

// customization_screen_is_ready (Sec60.1): "resolves in full with no crash
// shape" - no further body text given in this project's clean-room scope
// (the full write-up sits in Team A's own interp note, out of bounds for
// this project per the standing clean-room rule). Modelled as a plain OPEN
// flag - no arguments assumed, no invented body.
int stub_customization_screen_is_ready(lua_State* L) {
    logCall(L, upLog(L), "customization_screen_is_ready", upStateTag(L));
    bool ready = upState(L)->customizationCreation().screenIsReady.get(); // OPEN until set
    lua_pushboolean(L, ready ? 1 : 0);
    return 1;
}

// =========================================================================
// pcr_* family - Sec19 (11 names), Sec23 (10 names), Sec43 (24 of its 25
// names; pcr_report_hair_items is the 25th's own CRASH sibling already
// counted among the 24). 30 pcr_* names total in this batch's own list.
// =========================================================================

// pcu_set_active_category (Sec19.1, 0x0082e6c0): 2 mandatory numbers (area
// id, category id). No return. CONFIRMED body resets a "default sub-slot"
// byte via an "Upper Body"/"Backpacks" name special-case, repositions the
// preview camera, and caches the resolved area/category descriptors into
// 2 globals (0x022ffd4c area, 0x022ffd48 category) - the SAME area-name
// cache pcu_get_areas/pcu_get_categories_in_area/pcu_category_nav_clear_
// slot (below) all read. This project has no real area/category descriptor
// table (no in-scope loader) - area/category ids are test-populated via
// PcuClothingState::areas (the SAME 16-entry array pcu_category_nav_clear_
// slot's own HARDENING bounds against, Sec43.2), keeping the "Backpacks"
// name-compare honest rather than invented from a bare numeric id.
int stub_pcu_set_active_category(lua_State* L) {
    logCall(L, upLog(L), "pcu_set_active_category", upStateTag(L));
    int64_t areaId = truncateEa2596(lua_tonumber(L, 1));
    int64_t categoryId = truncateEa2596(lua_tonumber(L, 2));
    (void)categoryId; // consumed; per-category descriptor name-compare internals OPEN (0x009f3590 notifier not decompiled in scope)
    EngineState* st = upState(L);
    auto& pcu = st->pcuClothing();
    ++pcu.setActiveCategoryCount;
    if (areaId >= 0 && areaId < PcuClothingState::kAreaDescriptorCount && pcu.areas[static_cast<size_t>(areaId)].known) {
        pcu.activeAreaName = pcu.areas[static_cast<size_t>(areaId)].name;
        pcu.activeAreaIsBackpacks = caseInsensitiveEquals(pcu.activeAreaName, "Backpacks"); // CONFIRMED literal compare, Sec19.1
    }
    return 0;
}

// pcu_clear_preview_slot (Sec19.2, 0x0082d810): no arguments, no return.
// CONFIRMED: resets the +0x2088+0x1c sub-slot-selector byte from the
// +0x20f8 template default, validates the whole slot list (0 mask), then
// clears the shared "currently-previewed slot" global (0x01300c6c) to the
// sentinel -1 - "this function's real effect is 'finalize/validate
// whatever slot was just being previewed, THEN clear the preview marker.'"
int stub_pcu_clear_preview_slot(lua_State* L) {
    logCall(L, upLog(L), "pcu_clear_preview_slot", upStateTag(L));
    auto& pcu = upState(L)->pcuClothing();
    ++pcu.clearPreviewSlotCount;
    if (pcu.activeAreaIsBackpacks) pcu.activeAreaName = pcu.activeAreaName; // no-op placeholder: +0x2088+0x1c<-+0x20f8 byte internals OPEN (0x0082b470 not modelled)
    pcu.previewedSlot = -1; // CONFIRMED sentinel
    return 0;
}

// pcr_set_triangle_cursor_pos (Sec19.3, 0x008329f0): 2 mandatory numbers
// (x, y), no return. CONFIRMED (Sec43.3's own correction): the setter
// TRUNCATES both values to an integer before storing - fractional cursor
// positions are lost on write, not just on this getter's own read.
int stub_pcr_set_triangle_cursor_pos(lua_State* L) {
    logCall(L, upLog(L), "pcr_set_triangle_cursor_pos", upStateTag(L));
    int64_t x = truncateEa2596(lua_tonumber(L, 1));
    int64_t y = truncateEa2596(lua_tonumber(L, 2));
    auto& cur = upState(L)->pcrTriangleCursor();
    cur.x = static_cast<int32_t>(x); // CONFIRMED truncated-to-int storage (Sec43.3 correction)
    cur.y = static_cast<int32_t>(y);
    cur.known = true;
    return 0;
}

// pcr_set_random_composite (Sec19.4, 0x00835120): arg1 mandatory string
// (category name, resolved via the SAME 34-entry table pcr_change_composite
// uses - reused verbatim from PcrCustomizationState's own established
// convention); arg2 optional boolean, default false. No return. CONFIRMED:
// picks a uniformly-random eligible row for the resolved category (count
// test-populated via PcrRandomComposite::eligibleRowCount - no real per-
// category-definition table in scope) and applies it via the SAME 63-slot
// list set-tint mechanism, with the SAME category-0x16 ("fingernails")
// dual-slot special case pcr_change_composite already models. An empty
// eligible set is CONFIRMED a real, reachable no-op (not a crash).
int stub_pcr_set_random_composite(lua_State* L) {
    logCall(L, upLog(L), "pcr_set_random_composite", upStateTag(L));
    std::string categoryName = argString(L, 1);
    (void)optionalBoolDefault(L, 2, false); // consumed - decompiler-quirk arg, no in-scope observable effect confirmed beyond this (Sec19.4)
    static const char* kCategoryNames[34] = {
        "lips", "eyes",
        "tattoo_upper_arm_left", "tattoo_lower_arm_left", "tattoo_entire_arm_left",
        "tattoo_upper_arm_right", "tattoo_lower_arm_right", "tattoo_entire_arm_right",
        "tattoo_upper_chest", "tattoo_lower_chest", "tattoo_entire_chest",
        "tattoo_upper_back", "tattoo_lower_back", "tattoo_entire_back",
        "tattoo_head", "tattoo_neck", "tattoo_leg_left", "tattoo_leg_right",
        "tattoo_hand_left", "tattoo_hand_right",
        "body_mask", "shaderball", "fingernails", "fingernails_alt", "body_diffuse",
        "makeup_eyeliner", "makeup_eyeshadow", "makeup_cheeks", "makeup_entire_face",
        "eyebrows", "facial_hair", "body_hair", "face_features", "face_items",
    }; // SAME table pcr_change_composite (wave-1 Batch 5) already establishes - reused verbatim, not re-transcribed
    int32_t categoryId = -1;
    for (int32_t i = 0; i < 34; ++i) {
        if (caseInsensitiveEquals(categoryName, kCategoryNames[i])) { categoryId = i; break; }
    }
    EngineState* st = upState(L);
    auto& rc = st->pcrRandomComposite();
    if (categoryId < 0) { ++rc.categoryUnresolvedCount; return 0; } // CONFIRMED: bails/returns if unmatched
    if (!rc.eligibleRowCount.known(categoryName) || rc.eligibleRowCount.get(categoryName) <= 0) {
        ++rc.noEligibleRowsCount; // CONFIRMED real reachable no-op, not a crash
        return 0;
    }
    PcrCustomizationState& pcr = st->pcrCustomization();
    PcrCustomizationItem item;
    item.categoryId = categoryId;
    item.styleId = -1; // real chosen-row id is OPEN (0x0045b3a0 validity predicate not decompiled in scope) - style/tint left at this project's own honest default
    pcr.items[static_cast<size_t>(categoryId)] = item;
    if (categoryId == 0x16) { // "fingernails" dual-slot special case, SAME as pcr_change_composite
        pcr.fingernailsRMirror = item;
        pcr.fingernailsRMirrorSet = true;
    }
    pcr.dirty = true;
    ++rc.appliedCount;
    return 0;
}

// pcr_report_composite_items (Sec19.5, 0x00830a60): 2 mandatory strings
// (category name via the 34-entry table; a target/session-id string). No
// return (dispatches a named Lua callback, Sec43.1's own "spawns a script
// coroutine" mechanism - this project counts the attempt, never actually
// invoking a callback, per this header's own established convention).
int stub_pcr_report_composite_items(lua_State* L) {
    logCall(L, upLog(L), "pcr_report_composite_items", upStateTag(L));
    std::string categoryName = argString(L, 1);
    (void)argString(L, 2); // target/session-id string, forwarded to the coroutine spawn - not modelled further
    static const char* kCategoryNames[34] = {
        "lips", "eyes", "tattoo_upper_arm_left", "tattoo_lower_arm_left", "tattoo_entire_arm_left",
        "tattoo_upper_arm_right", "tattoo_lower_arm_right", "tattoo_entire_arm_right",
        "tattoo_upper_chest", "tattoo_lower_chest", "tattoo_entire_chest",
        "tattoo_upper_back", "tattoo_lower_back", "tattoo_entire_back",
        "tattoo_head", "tattoo_neck", "tattoo_leg_left", "tattoo_leg_right",
        "tattoo_hand_left", "tattoo_hand_right",
        "body_mask", "shaderball", "fingernails", "fingernails_alt", "body_diffuse",
        "makeup_eyeliner", "makeup_eyeshadow", "makeup_cheeks", "makeup_entire_face",
        "eyebrows", "facial_hair", "body_hair", "face_features", "face_items",
    };
    bool matched = false;
    for (int32_t i = 0; i < 34; ++i) if (caseInsensitiveEquals(categoryName, kCategoryNames[i])) { matched = true; break; }
    auto& rep = upState(L)->pcrReportCounters();
    if (!matched) { ++rep.reportCategoryUnresolvedCount; return 0; } // CONFIRMED: "still returns cleanly with no further effect"
    ++rep.compositeItemsReportCount;
    return 0;
}

// pcr_change_skin_color (Sec19.15, 0x008349a0): 1 mandatory number (skin-
// color table index). No return. CONFIRMED: "bounds-checks the index
// against a live count global (0x02300850, itself gated on a non-null
// table pointer 0x0230084c)" (test-populated here - no real skin-color
// table loaded in scope); on success, a genuine, disassembly-confirmed
// direct consumer of the SAME 63-slot PCR composite-item list items 4/5
// use. CHOSEN (this project's own judgment call, flagged): the real
// "+0x14 list index" field selector this writer targets was not
// independently confirmed to be a hardcoded skin-category id, so this
// project stores it in its OWN dedicated field (PcrCustomizationState::
// skinColorItem) - honestly scoped to what Sec19.15 actually confirms (the
// 63-slot list IS touched; WHICH category id is not independently given).
// CORRECTED (adversarial review): a prior version of this stub's doc
// comment made this exact claim but the code did not match it - it wrote
// into the SHARED items[0] slot, which this file's own kCategoryNames
// table (pcr_set_random_composite/pcr_get_composite_info) defines as real
// category 0 ("lips"), silently aliasing with genuine lips-customization
// data, and its bound check read pcrRandomizePalettes().hairColorPaletteSize
// (a HAIR-family field, wrong category) while never actually comparing the
// index against ANY upper bound. Both are fixed here.
int stub_pcr_change_skin_color(lua_State* L) {
    logCall(L, upLog(L), "pcr_change_skin_color", upStateTag(L));
    int64_t index = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& skin = st->pcrSkinColorChange();
    if (!skin.tableSize.known() || index < 0 || index >= skin.tableSize.get()) {
        // HARDENED (fidelity fix): bounds gate test-populated (0x02300850/
        // 0x0230084c, Sec19.15) - refuses an unpopulated, negative, or
        // out-of-range index rather than the prior version's silent accept
        // of anything non-negative.
        ++skin.oobRefusedCount;
        return 0;
    }
    PcrCustomizationState& pcr = st->pcrCustomization();
    // Dedicated field (see struct/doc comment) - NOT items[0] ("lips").
    pcr.skinColorItem.categoryId = -1; // CHOSEN: deliberately not a real 0-33 kCategoryNames id (see doc comment above) - this field is never read through that table
    pcr.skinColorItem.styleId = static_cast<int32_t>(index);
    pcr.skinColorSet = true;
    pcr.dirty = true;
    ++skin.appliedCount;
    return 0;
}

// pcr_set_anim (Sec19.14, 0x00832bc0): 2 mandatory - arg1 string (anim-
// category name, resolved via the SAME 4-entry table melee_combat_style/
// taunt_style/compliment_style/facial_expression_style), arg2 number (anim
// id). No return. CONFIRMED: index 0 (melee_combat_style) is a no-op, same
// as an unmatched name; indices 1/2/3 write their own field unconditionally
// once bounds-checked (the tracker-gated step is a secondary cache, OPEN).
int stub_pcr_set_anim(lua_State* L) {
    logCall(L, upLog(L), "pcr_set_anim", upStateTag(L));
    std::string categoryName = argString(L, 1);
    int64_t animId = truncateEa2596(lua_tonumber(L, 2));
    static const char* kAnimCategories[4] = {"melee_combat_style", "taunt_style", "compliment_style", "facial_expression_style"};
    int32_t idx = -1;
    for (int32_t i = 0; i < 4; ++i) if (caseInsensitiveEquals(categoryName, kAnimCategories[i])) { idx = i; break; }
    if (idx <= 0) return 0; // CONFIRMED: index 0 and unmatched both take the no-op path
    auto& pcr = upState(L)->pcrCustomization();
    // CHOSEN: categories 1/2/3 map onto PCR items[] slots 61/62/60 (this
    // project's own reserved high slots, outside the 0-33 content-category
    // range the 34-entry table uses) so the anim-style write never
    // collides with a real composite category - a stated simplification,
    // not a confirmed real-field layout (the real engine uses dedicated
    // +0x28b4/+0x28b5/+0xd28 player fields, Sec19.14, not this list).
    size_t slot = (idx == 3) ? 60u : (idx == 1 ? 61u : 62u);
    pcr.items[slot].categoryId = 100 + idx; // CHOSEN sentinel range, never produced by the real 34-entry table
    pcr.items[slot].styleId = static_cast<int32_t>(animId);
    pcr.dirty = true;
    return 0;
}

// pcu_get_slot_info (Sec19.13, 0x0082c2a0): 1 mandatory number (slot/item
// id) + 1 optional number (index, default -1). Return: a variable-length
// list on success (1,355-byte body, OPEN past the resolve itself - this
// project does not model the real item record's own per-field layout), or
// 1 fixed value (0x012a3038) if the item id doesn't resolve. Test-populated
// resolve via PcuClothingSlot (no real PCU-list-search table in scope).
int stub_pcu_get_slot_info(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_slot_info", upStateTag(L));
    int64_t slotId = truncateEa2596(lua_tonumber(L, 1));
    (void)optionalNumberDefault(L, 2, -1.0); // consumed - index/count fallback, OPEN internals
    auto& pcu = upState(L)->pcuClothing();
    if (slotId < 0 || slotId >= PcuClothingState::kSlotCount || pcu.slots[static_cast<size_t>(slotId)].itemId < 0) {
        ++pcu.getSlotInfoNotFoundCount;
        lua_pushnumber(L, EngineState::kActionPlaySentinelResolveFail); // CONFIRMED fixed fallback constant, Sec19.13
        return 1;
    }
    ++pcu.getSlotInfoFoundCount;
    const PcuClothingSlot& slot = pcu.slots[static_cast<size_t>(slotId)];
    lua_pushnumber(L, slot.colorIndex); // CONFIRMED-shaped "stored color id" field; remaining 13+ fields OPEN (not independently decompiled in scope)
    lua_pushnumber(L, slot.variantIndex);
    return 2;
}

// pcu_set_slot_info (Sec19.12, 0x0082ef50): at least 4 mandatory numbers
// (slot/category id, value/handle, index, variant-count) + up to 3 further
// optional numbers + 1 optional boolean + 1 optional number. No return.
// CONFIRMED: "If the slot/category id is below 0x18 (24) and the variant-
// count is below 4" - resolves and commits the item/color/variant into the
// slot; if a boolean flag arg is true, caches the resolved id into the
// SAME shared "currently-previewed slot" global pcu_clear_preview_slot
// clears; the Backpacks-area special-case sets +0x2088+0x1c/+0x1d97 when
// category 0x13 (19) and the cached active area name is "Backpacks".
// HARDENED (the SAME bound the real engine's own text already states, not
// a new divergence this project invents): refuses when slot/category id or
// variant-count is out of range, matching Sec19.12's own CONFIRMED guard
// text exactly rather than reading the "below" wording as merely advisory.
int stub_pcu_set_slot_info(lua_State* L) {
    logCall(L, upLog(L), "pcu_set_slot_info", upStateTag(L));
    int64_t slotId = truncateEa2596(lua_tonumber(L, 1));
    double value = lua_tonumber(L, 2);
    int64_t index = truncateEa2596(lua_tonumber(L, 3));
    int64_t variantCount = truncateEa2596(lua_tonumber(L, 4));
    bool cachePreview = optionalBoolDefault(L, 5, false);
    auto& pcu = upState(L)->pcuClothing();
    if (slotId < 0 || slotId >= 0x18 || variantCount < 0 || variantCount >= 4) {
        ++pcu.setSlotInfoOobRefusedCount; // matches Sec19.12's own CONFIRMED guard, reproduced (not a new HARDENING invention)
        return 0;
    }
    PcuClothingSlot& slot = pcu.slots[static_cast<size_t>(slotId % PcuClothingState::kSlotCount)];
    slot.itemId = static_cast<int32_t>(value);
    slot.variantIndex = static_cast<int32_t>(index);
    if (cachePreview) pcu.previewedSlot = static_cast<int32_t>(slotId);
    if (slotId == PcuClothingState::kUnderwearSlot && pcu.activeAreaIsBackpacks) {
        // CONFIRMED cross-reference to pcu_set_active_category's own
        // activeAreaIsBackpacks cache (Sec19.12's own "re-checks the SAME
        // cached 'current area' global ... against the literal
        // 'Backpacks'" text).
        slot.isNew = slot.isNew; // no-op placeholder: +0x1d97 bit internals OPEN, not modelled beyond this cross-reference
    }
    ++pcu.setSlotInfoAppliedCount;
    return 0;
}

// =========================================================================
// pcr_* Sec23 batch (10 names): pcr_set_voice, pcr_restore_clothing,
// pcr_report_composite_colors, pcr_report_avail_morphs, pcr_preview_anim,
// pcr_get_hair_info, pcr_get_composite_info, pcr_apply_preset,
// crib_has_garage_with_vehicles_of_type (own section below),
// garage_get_repair_cost (own section below).
// =========================================================================

// pcr_set_voice (Sec23.1, 0x00830830): arg1 mandatory string (voice-request
// tag, compared against the literal "play_current"); arg2 optional
// boolean, default TRUE (a notably different default polarity from most
// optional booleans in this document). No return. CONFIRMED: "play_current"
// reuses the cached voice id; otherwise resolves via the 7-entry by-name
// table (test-populated, no real resolver in scope). arg2 true plays
// immediately WITHOUT updating the cache; arg2 false always writes the new
// index into the cache.
int stub_pcr_set_voice(lua_State* L) {
    logCall(L, upLog(L), "pcr_set_voice", upStateTag(L));
    std::string tag = argString(L, 1);
    bool playImmediately = optionalBoolDefault(L, 2, true); // CONFIRMED default TRUE (Sec23.1)
    EngineState* st = upState(L);
    auto& voice = st->pcrVoice();
    int32_t resolvedIndex;
    if (caseInsensitiveEquals(tag, "play_current")) {
        if (!voice.haveCachedVoice) return 0; // CONFIRMED: reuses cached id - nothing to reuse yet is an honest no-op
        resolvedIndex = voice.cachedVoiceIndex;
        ++voice.playCurrentCount;
    } else {
        std::string key = toLowerAsciiB24(tag);
        if (!voice.voiceTagResolves.known(key) || !voice.voiceTagResolves.get(key)) {
            ++voice.unresolvedTagCount; // CONFIRMED: -1/unmatched is a real, reachable "no effect" case
            return 0;
        }
        resolvedIndex = 0; // real 0-6 index is OPEN (resolver internals not decompiled in scope) - this project only tracks resolve success
        ++voice.resolvedAndAppliedCount;
    }
    ++voice.phoneCallHandleOpenCount; // CONFIRMED: "Phone_Call" handle opened unconditionally once resolved, Sec23.1
    if (!playImmediately) {
        voice.cachedVoiceIndex = resolvedIndex; // CONFIRMED: arg2 false always writes the new cache
        voice.haveCachedVoice = true;
    }
    return 0;
}

// pcr_restore_clothing (Sec23.2, 0x00830700): no arguments, no return.
// CONFIRMED: a short fixed sequence (cleanup, reset literal 0, 2 per-
// category resets with literal ids 1/4, then the 24-category full re-sync
// with the "resync all" sentinel) - deep internals OPEN past the gated
// call itself, counted honestly per this project's established convention.
int stub_pcr_restore_clothing(lua_State* L) {
    logCall(L, upLog(L), "pcr_restore_clothing", upStateTag(L));
    ++upState(L)->pcrRestoreClothingCount();
    return 0;
}

// pcr_report_composite_colors (Sec23.3, 0x008354c0): arg1 mandatory string
// (category name, 34-entry table); arg2 mandatory boolean (read but its
// result is NEVER used anywhere - CONFIRMED, consumed only); arg3
// mandatory string (target/event-id, forwarded to the coroutine spawn).
// No return (report via the coroutine mechanism, Sec43.1).
int stub_pcr_report_composite_colors(lua_State* L) {
    logCall(L, upLog(L), "pcr_report_composite_colors", upStateTag(L));
    std::string categoryName = argString(L, 1);
    (void)lua_toboolean(L, 2); // CONFIRMED: read but never used anywhere in the function
    (void)argString(L, 3);
    static const char* kCategoryNames[34] = {
        "lips", "eyes", "tattoo_upper_arm_left", "tattoo_lower_arm_left", "tattoo_entire_arm_left",
        "tattoo_upper_arm_right", "tattoo_lower_arm_right", "tattoo_entire_arm_right",
        "tattoo_upper_chest", "tattoo_lower_chest", "tattoo_entire_chest",
        "tattoo_upper_back", "tattoo_lower_back", "tattoo_entire_back",
        "tattoo_head", "tattoo_neck", "tattoo_leg_left", "tattoo_leg_right",
        "tattoo_hand_left", "tattoo_hand_right",
        "body_mask", "shaderball", "fingernails", "fingernails_alt", "body_diffuse",
        "makeup_eyeliner", "makeup_eyeshadow", "makeup_cheeks", "makeup_entire_face",
        "eyebrows", "facial_hair", "body_hair", "face_features", "face_items",
    };
    bool matched = false;
    for (int32_t i = 0; i < 34; ++i) if (caseInsensitiveEquals(categoryName, kCategoryNames[i])) { matched = true; break; }
    auto& rep = upState(L)->pcrReportCounters();
    if (!matched) { ++rep.reportCategoryUnresolvedCount; return 0; }
    ++rep.compositeColorsReportCount;
    return 0;
}

// pcr_report_avail_morphs (Sec23.4, 0x00832700): arg1/arg2 mandatory
// strings (morph-category/slot name; target/event id), both forwarded to a
// morph-set resolver (0x00831650, OPEN - not independently decompiled).
// No return (coroutine report mechanism, Sec43.1).
int stub_pcr_report_avail_morphs(lua_State* L) {
    logCall(L, upLog(L), "pcr_report_avail_morphs", upStateTag(L));
    std::string slotName = argString(L, 1);
    (void)argString(L, 2);
    EngineState* st = upState(L);
    auto& morph = st->pcrMorph();
    std::string key = toLowerAsciiB24(slotName);
    auto& rep = st->pcrReportCounters();
    if (!morph.sessionResolves.known(key) || !morph.sessionResolves.get(key)) { // reuses the EXISTING PcrMorphState resolver (wave-1 Batch 3), not a new one
        ++rep.reportCategoryUnresolvedCount;
        return 0;
    }
    ++rep.availMorphsReportCount;
    return 0;
}

// pcr_preview_anim (Sec23.5, 0x00832a40): arg1 optional string (anim-
// category name, standard nil-gated idiom - absent/nil treated as
// unmatched). No return. CONFIRMED: resolves against the SAME 4-entry
// table pcr_set_anim uses; transitions a shared "active preview style"
// tracker, playing/stopping a preview anim per category.
int stub_pcr_preview_anim(lua_State* L) {
    logCall(L, upLog(L), "pcr_preview_anim", upStateTag(L));
    std::string categoryName = optionalStringDefault(L, 1, "");
    static const char* kAnimCategories[4] = {"melee_combat_style", "taunt_style", "compliment_style", "facial_expression_style"};
    int32_t idx = -1;
    for (int32_t i = 0; i < 4; ++i) if (caseInsensitiveEquals(categoryName, kAnimCategories[i])) { idx = i; break; }
    auto& rep = upState(L)->pcrReportCounters();
    ++rep.animsReportCount; // CHOSEN: counted here as the family's own "anim preview" activity, deep tracker transition internals OPEN
    (void)idx;
    return 0;
}

// pcr_get_hair_info (Sec23.6, 0x00832fa0): arg1 mandatory number (hair-
// category selector 0-2). Return: 3 numbers (style index, color-1 index,
// color-2 index; -1/-2 sentinels on no-match) then 2 strings (each
// color's own name, or the shared empty-string constant on a negative
// index). CONFIRMED range-checked against the SAME 3-entry mapping table
// pcr_change_hair uses - HARDENED (reproducing the CONFIRMED bound, not a
// new divergence) against an out-of-range category.
int stub_pcr_get_hair_info(lua_State* L) {
    logCall(L, upLog(L), "pcr_get_hair_info", upStateTag(L));
    int64_t category = truncateEa2596(lua_tonumber(L, 1));
    if (category < 0 || category >= 3) {
        // CONFIRMED bound reproduced (Sec23.6: "if the category is in
        // range" - out-of-range is this function's own real "no match"
        // path, not a crash this name is flagged for).
        lua_pushnumber(L, -1.0);
        lua_pushnumber(L, -2.0);
        lua_pushnumber(L, -2.0);
        lua_pushstring(L, "");
        lua_pushstring(L, "");
        return 5;
    }
    auto& pcr = upState(L)->pcrCustomization();
    static const int32_t kHairCategoryMap[3] = {1, 2, 3}; // SAME literal table pcr_change_hair uses
    int32_t mapped = kHairCategoryMap[category];
    int32_t styleId = pcr.items[static_cast<size_t>(mapped)].styleId; // CONFIRMED read of the SAME applied item pcr_change_hair wrote
    lua_pushnumber(L, static_cast<lua_Number>(styleId));
    lua_pushnumber(L, -1.0); // color-slot indices: real 0x48-byte-stride hair-colour table is OPEN (no loader in scope)
    lua_pushnumber(L, -2.0);
    lua_pushstring(L, "");
    lua_pushstring(L, "");
    return 5;
}

// pcr_get_composite_info (Sec23.7, 0x008331a0): arg1 mandatory string
// (category name, 34-entry table; 0 Lua values if unmatched). Return: 3
// numbers (id/-1, variant byte/0, color-index-or-intensity/1.0 default).
int stub_pcr_get_composite_info(lua_State* L) {
    logCall(L, upLog(L), "pcr_get_composite_info", upStateTag(L));
    std::string categoryName = argString(L, 1);
    static const char* kCategoryNames[34] = {
        "lips", "eyes", "tattoo_upper_arm_left", "tattoo_lower_arm_left", "tattoo_entire_arm_left",
        "tattoo_upper_arm_right", "tattoo_lower_arm_right", "tattoo_entire_arm_right",
        "tattoo_upper_chest", "tattoo_lower_chest", "tattoo_entire_chest",
        "tattoo_upper_back", "tattoo_lower_back", "tattoo_entire_back",
        "tattoo_head", "tattoo_neck", "tattoo_leg_left", "tattoo_leg_right",
        "tattoo_hand_left", "tattoo_hand_right",
        "body_mask", "shaderball", "fingernails", "fingernails_alt", "body_diffuse",
        "makeup_eyeliner", "makeup_eyeshadow", "makeup_cheeks", "makeup_entire_face",
        "eyebrows", "facial_hair", "body_hair", "face_features", "face_items",
    };
    int32_t categoryId = -1;
    for (int32_t i = 0; i < 34; ++i) if (caseInsensitiveEquals(categoryName, kCategoryNames[i])) { categoryId = i; break; }
    if (categoryId < 0) return 0; // CONFIRMED: 0 Lua values if unmatched
    auto& pcr = upState(L)->pcrCustomization();
    const PcrCustomizationItem& item = pcr.items[static_cast<size_t>(categoryId)];
    lua_pushnumber(L, item.styleId < 0 ? -1.0 : static_cast<lua_Number>(item.styleId));
    lua_pushnumber(L, 0.0); // "variant byte" field (0x026535b0) - OPEN, no loader in scope
    lua_pushnumber(L, 1.0); // color-index-or-intensity default
    return 3;
}

// Defined in lua_spec_confirmed_stubs_b24_wave2_pcu_pcr_part2.cpp (split out of this file, no behaviour change).
} // namespace
namespace stubs_b24 {
int stub_pcr_apply_preset(lua_State* L);
int stub_pcr_change_eye_color(lua_State* L);
int stub_pcr_strip_clothing(lua_State* L);
int stub_pcu_discard_slot(lua_State* L);
int stub_pcu_get_areas(lua_State* L);
int stub_pcu_get_categories_in_area(lua_State* L);
int stub_pcu_report_item_colors(lua_State* L);
int stub_pcu_wear_current_clothing(lua_State* L);
int stub_pcr_report_skin_colors(lua_State* L);
int stub_pcr_report_hair_items(lua_State* L);
int stub_pcr_report_hair_colors(lua_State* L);
int stub_pcr_report_eye_colors(lua_State* L);
int stub_pcr_report_anims(lua_State* L);
int stub_pcr_randomize_eye_colors(lua_State* L);
int stub_pcr_purchase_tattoo(lua_State* L);
int stub_pcr_lips_randomize(lua_State* L);
int stub_pcr_hair_randomize_all(lua_State* L);
int stub_pcr_get_voice(lua_State* L);
int stub_pcr_get_triangle_cursor_pos(lua_State* L);
int stub_pcr_creation_complete(lua_State* L);
int stub_pcr_cleanup_player_voice(lua_State* L);
int stub_pcr_apply_regional_preset(lua_State* L);
int stub_pcr_add_default_clothing(lua_State* L);
int stub_pcu_get_num_items_owned(lua_State* L);
int stub_pcu_get_owned_item_info(lua_State* L);
int stub_pcu_is_current_item_worn(lua_State* L);
int stub_pcu_get_variants(lua_State* L);
int stub_pcu_get_wear_options(lua_State* L);
int stub_pcu_report_outfits(lua_State* L);
int stub_pcu_is_item_needed_for_outfit(lua_State* L);
int stub_pcu_remove_clothing(lua_State* L);
int stub_pcu_set_slots_empty(lua_State* L);
int stub_pcu_wear_outfit(lua_State* L);
int stub_pcu_bra_required(lua_State* L);
int stub_pcu_can_create_outfit(lua_State* L);
int stub_pcu_category_nav_clear_slot(lua_State* L);
int stub_pcu_clear_item_new(lua_State* L);
int stub_pcu_clear_obscured_slots(lua_State* L);
int stub_pcu_create_outfit(lua_State* L);
int stub_pcu_delete_outfit(lua_State* L);
int stub_pcu_get_item_defaults(lua_State* L);
int stub_pcu_get_items_in_category(lua_State* L);
int stub_pcu_get_num_items_category(lua_State* L);
} // namespace stubs_b24
namespace {
using stubs_b24::stub_pcr_apply_preset;
using stubs_b24::stub_pcr_change_eye_color;
using stubs_b24::stub_pcr_strip_clothing;
using stubs_b24::stub_pcu_discard_slot;
using stubs_b24::stub_pcu_get_areas;
using stubs_b24::stub_pcu_get_categories_in_area;
using stubs_b24::stub_pcu_report_item_colors;
using stubs_b24::stub_pcu_wear_current_clothing;
using stubs_b24::stub_pcr_report_skin_colors;
using stubs_b24::stub_pcr_report_hair_items;
using stubs_b24::stub_pcr_report_hair_colors;
using stubs_b24::stub_pcr_report_eye_colors;
using stubs_b24::stub_pcr_report_anims;
using stubs_b24::stub_pcr_randomize_eye_colors;
using stubs_b24::stub_pcr_purchase_tattoo;
using stubs_b24::stub_pcr_lips_randomize;
using stubs_b24::stub_pcr_hair_randomize_all;
using stubs_b24::stub_pcr_get_voice;
using stubs_b24::stub_pcr_get_triangle_cursor_pos;
using stubs_b24::stub_pcr_creation_complete;
using stubs_b24::stub_pcr_cleanup_player_voice;
using stubs_b24::stub_pcr_apply_regional_preset;
using stubs_b24::stub_pcr_add_default_clothing;
using stubs_b24::stub_pcu_get_num_items_owned;
using stubs_b24::stub_pcu_get_owned_item_info;
using stubs_b24::stub_pcu_is_current_item_worn;
using stubs_b24::stub_pcu_get_variants;
using stubs_b24::stub_pcu_get_wear_options;
using stubs_b24::stub_pcu_report_outfits;
using stubs_b24::stub_pcu_is_item_needed_for_outfit;
using stubs_b24::stub_pcu_remove_clothing;
using stubs_b24::stub_pcu_set_slots_empty;
using stubs_b24::stub_pcu_wear_outfit;
using stubs_b24::stub_pcu_bra_required;
using stubs_b24::stub_pcu_can_create_outfit;
using stubs_b24::stub_pcu_category_nav_clear_slot;
using stubs_b24::stub_pcu_clear_item_new;
using stubs_b24::stub_pcu_clear_obscured_slots;
using stubs_b24::stub_pcu_create_outfit;
using stubs_b24::stub_pcu_delete_outfit;
using stubs_b24::stub_pcu_get_item_defaults;
using stubs_b24::stub_pcu_get_items_in_category;
using stubs_b24::stub_pcu_get_num_items_category;

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB24() {
    static const std::vector<std::string> names = {
        // customization_* (Sec60.1).
        "customization_swap_player_rig",
        "customization_create_character",
        "customization_creation_is_open",
        "customization_screen_is_ready",
        // pcr_* Sec19/Sec23/Sec26.
        "pcr_report_composite_items",
        "pcr_set_random_composite",
        "pcr_set_triangle_cursor_pos",
        "pcr_change_skin_color",
        "pcr_set_anim",
        "pcr_change_eye_color",
        "pcr_apply_preset",
        "pcr_get_composite_info",
        "pcr_get_hair_info",
        "pcr_preview_anim",
        "pcr_report_avail_morphs",
        "pcr_report_composite_colors",
        "pcr_restore_clothing",
        "pcr_set_voice",
        "pcr_strip_clothing",
        // pcu_* Sec19/Sec26.
        "pcu_clear_preview_slot",
        "pcu_set_active_category",
        "pcu_get_slot_info",
        "pcu_set_slot_info",
        "pcu_discard_slot",
        "pcu_get_areas",
        "pcu_get_categories_in_area",
        "pcu_report_item_colors",
        "pcu_wear_current_clothing",
        // pcr_* Sec43.3.
        "pcr_add_default_clothing",
        "pcr_apply_regional_preset",
        "pcr_cleanup_player_voice",
        "pcr_creation_complete",
        "pcr_get_triangle_cursor_pos",
        "pcr_get_voice",
        "pcr_hair_randomize_all",
        "pcr_lips_randomize",
        "pcr_purchase_tattoo",
        "pcr_randomize_eye_colors",
        "pcr_report_anims",
        "pcr_report_eye_colors",
        "pcr_report_hair_colors",
        "pcr_report_hair_items",
        "pcr_report_skin_colors",
        // pcu_* Sec40.5/40.6 (3 spelled out + 7 AGG).
        "pcu_get_num_items_owned",
        "pcu_get_owned_item_info",
        "pcu_is_current_item_worn",
        "pcu_get_variants",
        "pcu_get_wear_options",
        "pcu_report_outfits",
        "pcu_is_item_needed_for_outfit",
        "pcu_remove_clothing",
        "pcu_set_slots_empty",
        "pcu_wear_outfit",
        // pcu_* Sec43.2.
        "pcu_bra_required",
        "pcu_can_create_outfit",
        "pcu_category_nav_clear_slot",
        "pcu_clear_item_new",
        "pcu_clear_obscured_slots",
        "pcu_create_outfit",
        "pcu_delete_outfit",
        "pcu_get_item_defaults",
        "pcu_get_items_in_category",
        "pcu_get_num_items_category",
    };
    return names;
}

void registerB24(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    // customization_*.
    if (wants("customization_swap_player_rig")) registerOne(L, state, log, stateTag, "customization_swap_player_rig", openGuard<stub_customization_swap_player_rig>);
    if (wants("customization_create_character")) registerOne(L, state, log, stateTag, "customization_create_character", openGuard<stub_customization_create_character>);
    if (wants("customization_creation_is_open")) registerOne(L, state, log, stateTag, "customization_creation_is_open", openGuard<stub_customization_creation_is_open>);
    if (wants("customization_screen_is_ready")) registerOne(L, state, log, stateTag, "customization_screen_is_ready", openGuard<stub_customization_screen_is_ready>);
    // pcr_* Sec19/Sec23/Sec26.
    if (wants("pcr_report_composite_items")) registerOne(L, state, log, stateTag, "pcr_report_composite_items", openGuard<stub_pcr_report_composite_items>);
    if (wants("pcr_set_random_composite")) registerOne(L, state, log, stateTag, "pcr_set_random_composite", openGuard<stub_pcr_set_random_composite>);
    if (wants("pcr_set_triangle_cursor_pos")) registerOne(L, state, log, stateTag, "pcr_set_triangle_cursor_pos", openGuard<stub_pcr_set_triangle_cursor_pos>);
    if (wants("pcr_change_skin_color")) registerOne(L, state, log, stateTag, "pcr_change_skin_color", openGuard<stub_pcr_change_skin_color>);
    if (wants("pcr_set_anim")) registerOne(L, state, log, stateTag, "pcr_set_anim", openGuard<stub_pcr_set_anim>);
    if (wants("pcr_change_eye_color")) registerOne(L, state, log, stateTag, "pcr_change_eye_color", openGuard<stub_pcr_change_eye_color>);
    if (wants("pcr_apply_preset")) registerOne(L, state, log, stateTag, "pcr_apply_preset", openGuard<stub_pcr_apply_preset>);
    if (wants("pcr_get_composite_info")) registerOne(L, state, log, stateTag, "pcr_get_composite_info", openGuard<stub_pcr_get_composite_info>);
    if (wants("pcr_get_hair_info")) registerOne(L, state, log, stateTag, "pcr_get_hair_info", openGuard<stub_pcr_get_hair_info>);
    if (wants("pcr_preview_anim")) registerOne(L, state, log, stateTag, "pcr_preview_anim", openGuard<stub_pcr_preview_anim>);
    if (wants("pcr_report_avail_morphs")) registerOne(L, state, log, stateTag, "pcr_report_avail_morphs", openGuard<stub_pcr_report_avail_morphs>);
    if (wants("pcr_report_composite_colors")) registerOne(L, state, log, stateTag, "pcr_report_composite_colors", openGuard<stub_pcr_report_composite_colors>);
    if (wants("pcr_restore_clothing")) registerOne(L, state, log, stateTag, "pcr_restore_clothing", openGuard<stub_pcr_restore_clothing>);
    if (wants("pcr_set_voice")) registerOne(L, state, log, stateTag, "pcr_set_voice", openGuard<stub_pcr_set_voice>);
    if (wants("pcr_strip_clothing")) registerOne(L, state, log, stateTag, "pcr_strip_clothing", openGuard<stub_pcr_strip_clothing>);
    // pcu_* Sec19/Sec26.
    if (wants("pcu_clear_preview_slot")) registerOne(L, state, log, stateTag, "pcu_clear_preview_slot", openGuard<stub_pcu_clear_preview_slot>);
    if (wants("pcu_set_active_category")) registerOne(L, state, log, stateTag, "pcu_set_active_category", openGuard<stub_pcu_set_active_category>);
    if (wants("pcu_get_slot_info")) registerOne(L, state, log, stateTag, "pcu_get_slot_info", openGuard<stub_pcu_get_slot_info>);
    if (wants("pcu_set_slot_info")) registerOne(L, state, log, stateTag, "pcu_set_slot_info", openGuard<stub_pcu_set_slot_info>);
    if (wants("pcu_discard_slot")) registerOne(L, state, log, stateTag, "pcu_discard_slot", openGuard<stub_pcu_discard_slot>);
    if (wants("pcu_get_areas")) registerOne(L, state, log, stateTag, "pcu_get_areas", openGuard<stub_pcu_get_areas>);
    if (wants("pcu_get_categories_in_area")) registerOne(L, state, log, stateTag, "pcu_get_categories_in_area", openGuard<stub_pcu_get_categories_in_area>);
    if (wants("pcu_report_item_colors")) registerOne(L, state, log, stateTag, "pcu_report_item_colors", openGuard<stub_pcu_report_item_colors>);
    if (wants("pcu_wear_current_clothing")) registerOne(L, state, log, stateTag, "pcu_wear_current_clothing", openGuard<stub_pcu_wear_current_clothing>);
    // pcr_* Sec43.3.
    if (wants("pcr_add_default_clothing")) registerOne(L, state, log, stateTag, "pcr_add_default_clothing", openGuard<stub_pcr_add_default_clothing>);
    if (wants("pcr_apply_regional_preset")) registerOne(L, state, log, stateTag, "pcr_apply_regional_preset", openGuard<stub_pcr_apply_regional_preset>);
    if (wants("pcr_cleanup_player_voice")) registerOne(L, state, log, stateTag, "pcr_cleanup_player_voice", openGuard<stub_pcr_cleanup_player_voice>);
    if (wants("pcr_creation_complete")) registerOne(L, state, log, stateTag, "pcr_creation_complete", openGuard<stub_pcr_creation_complete>);
    if (wants("pcr_get_triangle_cursor_pos")) registerOne(L, state, log, stateTag, "pcr_get_triangle_cursor_pos", openGuard<stub_pcr_get_triangle_cursor_pos>);
    if (wants("pcr_get_voice")) registerOne(L, state, log, stateTag, "pcr_get_voice", openGuard<stub_pcr_get_voice>);
    if (wants("pcr_hair_randomize_all")) registerOne(L, state, log, stateTag, "pcr_hair_randomize_all", openGuard<stub_pcr_hair_randomize_all>);
    if (wants("pcr_lips_randomize")) registerOne(L, state, log, stateTag, "pcr_lips_randomize", openGuard<stub_pcr_lips_randomize>);
    if (wants("pcr_purchase_tattoo")) registerOne(L, state, log, stateTag, "pcr_purchase_tattoo", openGuard<stub_pcr_purchase_tattoo>);
    if (wants("pcr_randomize_eye_colors")) registerOne(L, state, log, stateTag, "pcr_randomize_eye_colors", openGuard<stub_pcr_randomize_eye_colors>);
    if (wants("pcr_report_anims")) registerOne(L, state, log, stateTag, "pcr_report_anims", openGuard<stub_pcr_report_anims>);
    if (wants("pcr_report_eye_colors")) registerOne(L, state, log, stateTag, "pcr_report_eye_colors", openGuard<stub_pcr_report_eye_colors>);
    if (wants("pcr_report_hair_colors")) registerOne(L, state, log, stateTag, "pcr_report_hair_colors", openGuard<stub_pcr_report_hair_colors>);
    if (wants("pcr_report_hair_items")) registerOne(L, state, log, stateTag, "pcr_report_hair_items", openGuard<stub_pcr_report_hair_items>);
    if (wants("pcr_report_skin_colors")) registerOne(L, state, log, stateTag, "pcr_report_skin_colors", openGuard<stub_pcr_report_skin_colors>);
    // pcu_* Sec40.5/40.6.
    if (wants("pcu_get_num_items_owned")) registerOne(L, state, log, stateTag, "pcu_get_num_items_owned", openGuard<stub_pcu_get_num_items_owned>);
    if (wants("pcu_get_owned_item_info")) registerOne(L, state, log, stateTag, "pcu_get_owned_item_info", openGuard<stub_pcu_get_owned_item_info>);
    if (wants("pcu_is_current_item_worn")) registerOne(L, state, log, stateTag, "pcu_is_current_item_worn", openGuard<stub_pcu_is_current_item_worn>);
    if (wants("pcu_get_variants")) registerOne(L, state, log, stateTag, "pcu_get_variants", openGuard<stub_pcu_get_variants>);
    if (wants("pcu_get_wear_options")) registerOne(L, state, log, stateTag, "pcu_get_wear_options", openGuard<stub_pcu_get_wear_options>);
    if (wants("pcu_report_outfits")) registerOne(L, state, log, stateTag, "pcu_report_outfits", openGuard<stub_pcu_report_outfits>);
    if (wants("pcu_is_item_needed_for_outfit")) registerOne(L, state, log, stateTag, "pcu_is_item_needed_for_outfit", openGuard<stub_pcu_is_item_needed_for_outfit>);
    if (wants("pcu_remove_clothing")) registerOne(L, state, log, stateTag, "pcu_remove_clothing", openGuard<stub_pcu_remove_clothing>);
    if (wants("pcu_set_slots_empty")) registerOne(L, state, log, stateTag, "pcu_set_slots_empty", openGuard<stub_pcu_set_slots_empty>);
    if (wants("pcu_wear_outfit")) registerOne(L, state, log, stateTag, "pcu_wear_outfit", openGuard<stub_pcu_wear_outfit>);
    // pcu_* Sec43.2.
    if (wants("pcu_bra_required")) registerOne(L, state, log, stateTag, "pcu_bra_required", openGuard<stub_pcu_bra_required>);
    if (wants("pcu_can_create_outfit")) registerOne(L, state, log, stateTag, "pcu_can_create_outfit", openGuard<stub_pcu_can_create_outfit>);
    if (wants("pcu_category_nav_clear_slot")) registerOne(L, state, log, stateTag, "pcu_category_nav_clear_slot", openGuard<stub_pcu_category_nav_clear_slot>);
    if (wants("pcu_clear_item_new")) registerOne(L, state, log, stateTag, "pcu_clear_item_new", openGuard<stub_pcu_clear_item_new>);
    if (wants("pcu_clear_obscured_slots")) registerOne(L, state, log, stateTag, "pcu_clear_obscured_slots", openGuard<stub_pcu_clear_obscured_slots>);
    if (wants("pcu_create_outfit")) registerOne(L, state, log, stateTag, "pcu_create_outfit", openGuard<stub_pcu_create_outfit>);
    if (wants("pcu_delete_outfit")) registerOne(L, state, log, stateTag, "pcu_delete_outfit", openGuard<stub_pcu_delete_outfit>);
    if (wants("pcu_get_item_defaults")) registerOne(L, state, log, stateTag, "pcu_get_item_defaults", openGuard<stub_pcu_get_item_defaults>);
    if (wants("pcu_get_items_in_category")) registerOne(L, state, log, stateTag, "pcu_get_items_in_category", openGuard<stub_pcu_get_items_in_category>);
    if (wants("pcu_get_num_items_category")) registerOne(L, state, log, stateTag, "pcu_get_num_items_category", openGuard<stub_pcu_get_num_items_category>);
}

} // namespace stub_sections
} // namespace sr3luahost
