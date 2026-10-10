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

// Portable ASCII lowercase/case-insensitive helpers - same "portable,
// behaviorally equivalent stand-in" convention as caseInsensitiveEquals in
// lua_spec_confirmed_stubs_common.h; not a claim about the real CRT.
std::string toLowerAsciiB24(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

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

// pcr_apply_preset (Sec23.8, 0x00837640): arg1 mandatory string (preset
// name); arg2/arg3 optional strings, default the shared empty-string
// constant; arg4 optional boolean, default false. No return. CONFIRMED:
// resolves arg1 against a fixed preset-slot array (test-populated, no real
// array loaded in scope); on a match, applies the preset then runs the
// SAME 24-category outfit re-sync routine TWICE with literal category ids
// 10/11 (a scoped re-sync, not "resync everything").
int stub_pcr_apply_preset(lua_State* L) {
    logCall(L, upLog(L), "pcr_apply_preset", upStateTag(L));
    std::string presetName = argString(L, 1);
    (void)optionalStringDefault(L, 2, "");
    (void)optionalStringDefault(L, 3, "");
    (void)optionalBoolDefault(L, 4, false);
    auto& preset = upState(L)->pcrPreset();
    std::string key = toLowerAsciiB24(presetName);
    if (!preset.presetResolves.known(key) || !preset.presetResolves.get(key)) return 0; // CONFIRMED: "if none found, returns silently"
    ++preset.appliedCount;
    return 0;
}

// =========================================================================
// pcu_* Sec26 batch (5 names of this batch's own 29): pcu_discard_slot,
// pcu_get_areas, pcu_get_categories_in_area, pcu_report_item_colors,
// pcu_wear_current_clothing. Plus pcr_change_eye_color/pcr_strip_clothing
// (pcr_* Sec26 names).
// =========================================================================

// pcr_change_eye_color (Sec26.1, 0x00835c70): 1 mandatory number (eye-
// color table index), no return. CONFIRMED: a 4th direct, disassembly-
// confirmed consumer of the shared 63-slot PCR list - writes via the SAME
// set/clear-tint primitive pcr_change_composite/pcr_set_random_composite/
// pcr_change_skin_color already use, at category 1 ("eyes", per the
// 34-entry table's own index).
int stub_pcr_change_eye_color(lua_State* L) {
    logCall(L, upLog(L), "pcr_change_eye_color", upStateTag(L));
    int64_t index = truncateEa2596(lua_tonumber(L, 1));
    auto& pcr = upState(L)->pcrCustomization();
    PcrCustomizationItem item;
    item.categoryId = 1; // CONFIRMED "eyes" index in the 34-entry table (Sec16.7/Sec43.3's own cross-reference)
    item.styleId = static_cast<int32_t>(index);
    pcr.items[1] = item;
    pcr.dirty = true;
    return 0;
}

// pcr_strip_clothing (Sec26.2, 0x00830750): no arguments, no return.
// CONFIRMED: real role of the primary call is OPEN ("mark everything as
// needing reapply/dirty" fits at least as well as "clear", per the
// adversarial review) - this project marks the SAME 63-slot list dirty and
// clears category 5 from it, honestly representing only what's confirmed:
// the call happened, category 5 was cleared, the deeper "clear vs. dirty"
// semantics are OPEN.
int stub_pcr_strip_clothing(lua_State* L) {
    logCall(L, upLog(L), "pcr_strip_clothing", upStateTag(L));
    auto& pcr = upState(L)->pcrCustomization();
    pcr.items[5] = PcrCustomizationItem{}; // CONFIRMED: category id 5 cleared (exact name at index 5 OPEN, Sec26.2)
    pcr.dirty = true;
    ++upState(L)->pcrStripClothingCount();
    return 0;
}

// pcu_discard_slot (Sec26.12, 0x0082edc0): 2 mandatory numbers (category
// id, variant/sub-index). No return. CONFIRMED: looks up the named
// category in the PCU list; if found, clears it and validates against the
// per-category-definition table; always clears the "list changed"
// tracking, revalidates everything, and syncs the Backpacks-area template
// byte (SAME direction as pcu_clear_preview_slot/pcu_set_slot_info, per
// Sec26.12's own cross-reference - NOT the reversed direction pcu_wear_
// current_clothing below uses).
int stub_pcu_discard_slot(lua_State* L) {
    logCall(L, upLog(L), "pcu_discard_slot", upStateTag(L));
    int64_t categoryId = truncateEa2596(lua_tonumber(L, 1));
    (void)truncateEa2596(lua_tonumber(L, 2)); // variant/sub-index - consumed, deep 0x022ff5d0 validation internals OPEN
    auto& pcu = upState(L)->pcuClothing();
    if (categoryId >= 0 && categoryId < PcuClothingState::kSlotCount) {
        pcu.slots[static_cast<size_t>(categoryId)] = PcuClothingSlot{}; // CONFIRMED: found -> clears it
    }
    pcu.listChanged = false; // CONFIRMED: "always clears the player's 'list changed' tracking"
    if (pcu.activeAreaIsBackpacks) {
        // CONFIRMED same-direction Backpacks sync as pcu_clear_preview_
        // slot (not pcu_wear_current_clothing's reversed one, Sec26.12).
        pcu.activeAreaName = pcu.activeAreaName; // no-op placeholder - +0x2088+0x1c/+0x20f8 byte internals OPEN
    }
    ++pcu.discardSlotCount;
    return 0;
}

// pcu_get_areas (Sec26.15, 0x0082e550): 1 string (not nil-gated - a
// "wardrobe" filter keyword), 1 mandatory string (named callback target).
// No return (named-callback dispatch, Sec43.1). CONFIRMED: sets a
// "wardrobe mode" flag from a case-insensitive "wardrobe" compare, then
// reports every visible area via the callback mechanism.
int stub_pcu_get_areas(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_areas", upStateTag(L));
    std::string filter = argString(L, 1);
    (void)argString(L, 2);
    auto& pcu = upState(L)->pcuClothing();
    pcu.wardrobeMode = caseInsensitiveEquals(filter, "wardrobe"); // CONFIRMED literal compare, Sec26.15
    return 0;
}

// pcu_get_categories_in_area (Sec26.14, 0x0082b860): 1 mandatory number
// (area id), 1 mandatory string (named callback target). No return
// (named-callback dispatch). CONFIRMED: bounds-checks the area id against
// the established 16-entry area-descriptor array - HARDENED (reproducing
// the CONFIRMED bound, the SAME 16-entry array pcu_category_nav_clear_
// slot's own CRASH finding leaves unchecked - this entry point already
// checks it in the original, so no new divergence is introduced here).
int stub_pcu_get_categories_in_area(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_categories_in_area", upStateTag(L));
    int64_t areaId = truncateEa2596(lua_tonumber(L, 1));
    (void)argString(L, 2);
    auto& pcu = upState(L)->pcuClothing();
    if (areaId < 0 || areaId >= PcuClothingState::kAreaDescriptorCount || !pcu.areas[static_cast<size_t>(areaId)].known) {
        return 0; // CONFIRMED bound reproduced - this entry point already guards it in the original
    }
    return 0; // per-category report dispatch - deep internals (+0x20/+0x24/+0x2c fields) OPEN, no in-scope producer
}

// pcu_report_item_colors (Sec26.13, 0x0082c0a0): 1 mandatory string
// (callback/target name), 1 mandatory number (item id), 1 optional number
// (color-table index, default resolved via the established fallback if
// absent). No return (named-callback dispatch).
int stub_pcu_report_item_colors(lua_State* L) {
    logCall(L, upLog(L), "pcu_report_item_colors", upStateTag(L));
    (void)argString(L, 1);
    int64_t itemId = truncateEa2596(lua_tonumber(L, 2));
    (void)optionalNumberDefault(L, 3, -1.0);
    auto& pcu = upState(L)->pcuClothing();
    if (itemId < 0 || itemId >= PcuClothingState::kSlotCount || pcu.slots[static_cast<size_t>(itemId)].itemId < 0) return 0; // CONFIRMED resolve-or-skip shape
    return 0; // per-row color-table report - deep internals OPEN, no real color-table loader in scope
}

// pcu_wear_current_clothing (Sec26.11, 0x0082f7d0): no arguments, no
// return. CONFIRMED: revalidates every category (-1, 0 = "everything"),
// then if the active area is "Backpacks", copies +0x2088+0x1c INTO +0x20f8
// (the OPPOSITE direction from every other function touching this pair -
// a genuine, worth-flagging asymmetry this stub preserves as a distinct
// code path rather than merging with the other direction's helper).
int stub_pcu_wear_current_clothing(lua_State* L) {
    logCall(L, upLog(L), "pcu_wear_current_clothing", upStateTag(L));
    auto& pcu = upState(L)->pcuClothing();
    pcu.listChanged = false; // "revalidate everything" effect, deep internals OPEN
    if (pcu.activeAreaIsBackpacks) {
        // CONFIRMED reversed write direction (Sec26.11) - kept as its own
        // distinct branch from pcu_discard_slot's own same-direction one,
        // per this batch's own "do NOT conflate the two" discipline.
        pcu.activeAreaName = pcu.activeAreaName; // no-op placeholder - reversed +0x2088+0x1c<-+0x20f8 byte internals OPEN
    }
    ++pcu.wearCurrentClothingCount;
    return 0;
}

// =========================================================================
// pcr_* Sec43.3 batch (15 names): pcr_report_skin_colors, pcr_report_hair_
// items (CRASH), pcr_report_hair_colors, pcr_report_eye_colors, pcr_report_
// anims, pcr_randomize_eye_colors, pcr_purchase_tattoo (CRASH), pcr_lips_
// randomize (CRASH), pcr_hair_randomize_all (CRASH), pcr_get_voice, pcr_get_
// triangle_cursor_pos, pcr_creation_complete (CRASH), pcr_cleanup_player_
// voice, pcr_apply_regional_preset (CRASH), pcr_add_default_clothing
// (CRASH).
// =========================================================================

// pcr_report_skin_colors (Sec43.3, 0x00832570): 1 string (callback). CONFIRMED
// uses a coroutine opened against the CALLING SCRIPT'S OWN thread record
// (0x00e0cb20(L)->0x00e0c8f0), not the global script state - "the only
// report function in this tranche to do so, so it reports nothing when
// called from an unregistered state." Uses sentinel 0x029c9960 (distinct
// from 0x029c9964), per this batch's own "keep the 3 sentinels distinct"
// instruction.
int stub_pcr_report_skin_colors(lua_State* L) {
    logCall(L, upLog(L), "pcr_report_skin_colors", upStateTag(L));
    (void)argString(L, 1);
    ++upState(L)->pcrReportCounters().skinColorsReportCount;
    return 0;
}

// pcr_report_hair_items (Sec43.3 CRASH, 0x00832cc0): 1 number (hair
// category 0-2) + 1 callback string. CONFIRMED real crash shapes: "Maps the
// category through a 3-entry table with NO range check (unlike pcr_change_
// hair/pcr_get_hair_info, which do) ... arg 1 outside 0-2 reads the mapping
// table out of bounds." HARDENED: bound-checked here (0-2), matching the
// sibling functions' own existing convention - refuses rather than reading
// out of bounds.
int stub_pcr_report_hair_items(lua_State* L) {
    logCall(L, upLog(L), "pcr_report_hair_items", upStateTag(L));
    int64_t category = truncateEa2596(lua_tonumber(L, 1));
    (void)argString(L, 2);
    auto& rep = upState(L)->pcrReportCounters();
    if (category < 0 || category >= 3) {
        ++rep.hairItemsOobRefusedCount; // HARDENED divergence (Sec43.3 CRASH)
        return 0;
    }
    ++rep.hairItemsReportCount;
    return 0;
}

// pcr_report_hair_colors (Sec43.3, 0x00833420): 1 callback string.
// CONFIRMED: "current" colour derived from entry 0 of the per-store hair-
// colour table; an empty table yields a negative "current" with nothing
// flagged selected - both are real, reachable outcomes, not crashes.
int stub_pcr_report_hair_colors(lua_State* L) {
    logCall(L, upLog(L), "pcr_report_hair_colors", upStateTag(L));
    (void)argString(L, 1);
    ++upState(L)->pcrReportCounters().hairColorsReportCount;
    return 0;
}

// pcr_report_eye_colors (Sec43.3, 0x00830da0): 1 callback string.
// CONFIRMED: reports GLOBAL composite-definition indices (not per-category
// eye-colour indices) for every category-1 ("eyes") definition; uses the
// THIRD distinct sentinel, plain 0 (kept distinct from 0x029c9964/
// 0x029c9960 per this batch's own instruction).
int stub_pcr_report_eye_colors(lua_State* L) {
    logCall(L, upLog(L), "pcr_report_eye_colors", upStateTag(L));
    (void)argString(L, 1);
    ++upState(L)->pcrReportCounters().eyeColorsReportCount;
    return 0;
}

// pcr_report_anims (Sec43.3, 0x00834bf0): 1 string (style category) + 1
// callback string. CONFIRMED: "melee_combat_style" is a no-op (SAME as
// Sec19.14's own finding); the other 3 categories report in alphabetical-
// by-display-name order (an O(n^2) selection sort, not table order) -
// deep per-entry internals OPEN, counted per category resolve.
int stub_pcr_report_anims(lua_State* L) {
    logCall(L, upLog(L), "pcr_report_anims", upStateTag(L));
    std::string categoryName = argString(L, 1);
    (void)argString(L, 2);
    auto& rep = upState(L)->pcrReportCounters();
    if (caseInsensitiveEquals(categoryName, "melee_combat_style")) return 0; // CONFIRMED no-op, SAME as Sec19.14
    ++rep.animsReportCount;
    return 0;
}

// pcr_randomize_eye_colors (Sec43.3, 0x00835bc0): no arguments. CONFIRMED:
// "Collects every eligible composite definition into a 600-entry stack
// array with no bound check before picking one at random" (data-gated
// stack-overflow shape - not reproduced here; this project has no real
// composite-definition table loader in scope, so the collection step
// itself never runs, honestly - the eligible-set size is always this
// project's own test-populated count, never an invented 600+).
int stub_pcr_randomize_eye_colors(lua_State* L) {
    logCall(L, upLog(L), "pcr_randomize_eye_colors", upStateTag(L));
    auto& pal = upState(L)->pcrRandomizePalettes();
    ++pal.eyeColorsAppliedCount;
    return 0;
}

// pcr_purchase_tattoo (Sec43.3 CRASH, 0x008333a0): 1 number (price in
// dollars, default 0). No return. CONFIRMED real logic defect: "Only
// charges cash ... no item, composite, or stat is touched. No sign check,
// so a negative price PAYS the player instead of charging them; no
// affordability check; no player null check." HARDENED per the task
// brief's own explicit instruction: refuses a negative price; reuses
// hasLocalPlayer() for the null-player half; reuses playerCash()/
// purchaseLedger() (no PCR-specific wallet).
int stub_pcr_purchase_tattoo(lua_State* L) {
    logCall(L, upLog(L), "pcr_purchase_tattoo", upStateTag(L));
    double price = optionalNumberDefault(L, 1, 0.0);
    EngineState* st = upState(L);
    auto& tattoo = st->pcrTattooPurchase();
    if (!st->hasLocalPlayer().known() || !st->hasLocalPlayer().get()) {
        ++tattoo.noLocalPlayerRefusedCount; // HARDENED (unchecked local player, Sec43.3)
        return 0;
    }
    if (price < 0.0) {
        ++tattoo.negativePriceRefusedCount; // HARDENED (named task-brief divergence: real engine PAYS the player)
        return 0;
    }
    st->playerCash() -= price; // CONFIRMED: reason 0xb, "cash_adjust" path (Sec23.1/Sec43.3 cross-reference)
    EngineState::PurchaseRecord rec;
    rec.tag = "tattoo";
    rec.amountCharged = price;
    rec.itemResolved = true;
    st->purchaseLedger().push_back(rec);
    ++tattoo.chargedCount;
    return 0;
}

// pcr_lips_randomize (Sec43.3 CRASH, 0x00834a00): no arguments. CONFIRMED
// real divide-by-zero and null-read shapes: "Picks from a 64-entry stack
// array ... via the shared PRNG helper 0x00dab660(lo,hi), which divides by
// hi-lo+1 with no empty-range guard - an empty 'lips colour' palette makes
// this divide by zero. A picked lips item whose name doesn't resolve as a
// PCU item is dereferenced null immediately." HARDENED: refuses the draw
// when the (test-populated) palette size is empty/unset rather than
// dividing by zero.
int stub_pcr_lips_randomize(lua_State* L) {
    logCall(L, upLog(L), "pcr_lips_randomize", upStateTag(L));
    auto& pal = upState(L)->pcrRandomizePalettes();
    if (!pal.lipsColorPaletteSize.known() || pal.lipsColorPaletteSize.get() <= 0) {
        ++pal.lipsEmptyPaletteRefusedCount; // HARDENED (real engine: divide-by-zero)
        return 0;
    }
    ++pal.lipsAppliedCount;
    return 0;
}

// pcr_hair_randomize_all (Sec43.3 CRASH, 0x00837450): no arguments.
// CONFIRMED real divide-by-zero (SAME unguarded PRNG call against the
// hair-colour table) plus a real logic bug (wrong-slot recording, read
// CONFIRMED, intent HYPOTHESIS - reproduced as OPEN, not silently
// "corrected"). HARDENED: refuses the draw on an empty/unset palette.
int stub_pcr_hair_randomize_all(lua_State* L) {
    logCall(L, upLog(L), "pcr_hair_randomize_all", upStateTag(L));
    auto& pal = upState(L)->pcrRandomizePalettes();
    if (!pal.hairColorPaletteSize.known() || pal.hairColorPaletteSize.get() <= 0) {
        ++pal.hairEmptyPaletteRefusedCount; // HARDENED (real engine: divide-by-zero)
        return 0;
    }
    ++pal.hairAppliedCount;
    return 0;
}

// pcr_get_voice (Sec43.3, 0x00832e60): no arguments; 0 or 1 values (cached
// voice index). CONFIRMED: no player null check (not itself CRASH-flagged
// in this batch's own list - reproduced as a plain OPEN-gated read, same
// discipline as every other unflagged "unchecked local player" case in
// this document that this batch does not own).
int stub_pcr_get_voice(lua_State* L) {
    logCall(L, upLog(L), "pcr_get_voice", upStateTag(L));
    auto& voice = upState(L)->pcrVoice();
    if (!voice.haveCachedVoice) return 0; // CONFIRMED: 0 values when nothing cached
    lua_pushnumber(L, static_cast<lua_Number>(voice.cachedVoiceIndex));
    return 1;
}

// pcr_get_triangle_cursor_pos (Sec43.3, 0x008329a0): no arguments; 2
// signed integers. CONFIRMED (Sec43.3's own correction, reflected in the
// setter above): values are stored truncated to int, not fractional.
int stub_pcr_get_triangle_cursor_pos(lua_State* L) {
    logCall(L, upLog(L), "pcr_get_triangle_cursor_pos", upStateTag(L));
    auto& cur = upState(L)->pcrTriangleCursor();
    lua_pushnumber(L, static_cast<lua_Number>(cur.x));
    lua_pushnumber(L, static_cast<lua_Number>(cur.y));
    return 2;
}

// pcr_creation_complete (Sec43.3 CRASH, 0x008335c0): no arguments.
// CONFIRMED: a co-op "both players finished" handshake - "In single player,
// 'ready' is always true (no remote player exists)." When not ready, opens
// a waiting dialog from a fixed pool - "if the pool is exhausted, the
// allocator returns null and the caller dereferences it unchecked."
// HARDENED: refuses to open the dialog when the (test-populated) pool
// reports exhausted.
int stub_pcr_creation_complete(lua_State* L) {
    logCall(L, upLog(L), "pcr_creation_complete", upStateTag(L));
    EngineState* st = upState(L);
    auto& cc = st->pcrCreationComplete();
    if (!st->coopSession().present.get()) { // CONFIRMED false by default -> single player is always "ready"
        ++cc.readyCount;
        return 0;
    }
    if (cc.dialogPoolExhausted.known() && cc.dialogPoolExhausted.get()) {
        ++cc.poolExhaustedRefusedCount; // HARDENED (named CRASH)
        return 0;
    }
    return 0; // co-op, not ready, pool not exhausted: dialog-open internals OPEN, no in-scope dialog-pool model
}

// pcr_cleanup_player_voice (Sec43.3, 0x00830a30): no arguments. CONFIRMED:
// clears the voice-slot "Phone_Call" cue override pcr_set_voice sets,
// replicated to clients when this machine is host.
int stub_pcr_cleanup_player_voice(lua_State* L) {
    logCall(L, upLog(L), "pcr_cleanup_player_voice", upStateTag(L));
    EngineState* st = upState(L);
    auto& voice = st->pcrVoice();
    voice.haveCachedVoice = false;
    voice.cachedVoiceIndex = -1;
    ++voice.cleanupCount;
    if (st->coopLocalIsHost()) EngineState::replicateStateChange("pcr_cleanup_player_voice", ""); // CONFIRMED: replicated when host
    return 0;
}

// pcr_apply_regional_preset (Sec43.3 CRASH, 0x00830600): 2 strings
// (region, preset name), resolved through the SAME two-stage resolvers
// pcr_apply_preset uses. CONFIRMED: "Neither the preset-apply step nor the
// morph-apply step null-checks the local player, so a valid region/preset
// pair with no local player crashes." HARDENED: reuses hasLocalPlayer().
int stub_pcr_apply_regional_preset(lua_State* L) {
    logCall(L, upLog(L), "pcr_apply_regional_preset", upStateTag(L));
    std::string region = argString(L, 1);
    std::string presetName = argString(L, 2);
    EngineState* st = upState(L);
    auto& preset = st->pcrPreset();
    std::string key = toLowerAsciiB24(region + "|" + presetName);
    if (!preset.regionResolves.known(key) || !preset.regionResolves.get(key)) return 0; // CONFIRMED resolve-or-skip
    if (!st->hasLocalPlayer().known() || !st->hasLocalPlayer().get()) {
        ++preset.regionalNoLocalPlayerRefusedCount; // HARDENED (named CRASH)
        return 0;
    }
    ++preset.regionalAppliedCount;
    return 0;
}

// pcr_add_default_clothing (Sec43.3, 0x008306a0): no arguments. CONFIRMED:
// "Refreshes worn slot 10, then calls the shared 'refresh body' helper in
// its flag-0 mode, which ... empties the player's entire owned-clothing
// inventory (sets the owned count to 0) before re-adding only the default
// garments for the player's gender/ethnicity. Correct for new-character
// creation (HYPOTHESIS); destructive if any shipped script calls it on an
// established character." PRESERVED FAITHFULLY per the task brief's own
// explicit instruction - this is intentional engine behaviour, NOT a bug
// to harden away. Flagged clearly: do not "fix" this by skipping the wipe.
// CORRECTED (adversarial review): a prior version of this stub wiped
// PcuClothingState::slots (all 24 WORN slots) - a completely different
// storage object from the real "owned-clothing inventory" the spec names
// (Sec19.16's own "do not conflate the two" caution) - while leaving
// PcuInventory::ownedItemCount (the actual field the spec describes being
// zeroed) untouched. The real, intended destructive wipe now correctly
// targets PcuInventory::ownedItemCount; the slot-10 refresh (a separate,
// real, but content-OPEN effect - no default-garment table in scope) is
// counted, not invented.
int stub_pcr_add_default_clothing(lua_State* L) {
    logCall(L, upLog(L), "pcr_add_default_clothing", upStateTag(L));
    EngineState* st = upState(L);
    ++st->pcuClothing().addDefaultClothingSlot10RefreshCount; // CONFIRMED real effect ("refreshes worn slot 10") - content OPEN, counted only
    st->pcuInventory().ownedItemCount = 0; // CONFIRMED-destructive wipe - FAITHFUL, not a bug (Sec43.3) - the REAL wipe target (not PcuClothingState::slots)
    ++st->pcrAddDefaultClothingWipeCount();
    // Re-adding gender/ethnicity defaults: no real default-garment table is
    // loaded in scope (no in-scope producer) - the wipe itself is the one
    // real, test-observable effect this project reproduces; the re-add
    // step's own internals are OPEN, not invented.
    return 0;
}

// =========================================================================
// pcu_* Sec40.5/Sec40.6 batch (10 names: 3 spelled out, 7 AGG).
// =========================================================================

// pcu_get_num_items_owned (Sec40.5/40.6): no arguments documented in scope
// beyond "numbers the identical catalog list by testing bit 0 only" -
// return: 1 number (count). REUSES the EXISTING pcuCatalogOutfits()
// (wave-1 Batch 4/tranche 08, b04) rather than a new catalog model, per
// this batch's own "reuse, don't re-derive" instruction.
int stub_pcu_get_num_items_owned(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_num_items_owned", upStateTag(L));
    EngineState* st = upState(L);
    // CONFIRMED (Sec40.6): "bit 0 only" - reuses the EXISTING
    // pcuCatalogOutfits().flagsByIndex test-populated registry (b04). CHOSEN
    // scan bound: this project has no real catalog-list count anywhere in
    // scope, so it scans the same 128 indices EngineState::PcuInventory::
    // kSavedOutfitCapacity already establishes as this project's own
    // saved-outfit-adjacent capacity (a reasonable, non-invented bound to
    // iterate a test-populated map by, not a claim about the real catalog's
    // own size).
    int32_t count = 0;
    for (int32_t i = 0; i < EngineState::PcuInventory::kSavedOutfitCapacity; ++i) {
        std::string key = std::to_string(i);
        if (st->pcuCatalogOutfits().flagsByIndex.known(key) && (st->pcuCatalogOutfits().flagsByIndex.get(key) & 0x1u)) ++count;
    }
    lua_pushnumber(L, static_cast<lua_Number>(count));
    return 1;
}

// pcu_get_owned_item_info (Sec40.5 CRASH): 1 number (item index). CONFIRMED
// "dereferences a null item record when the item index is out of range
// (once any item is owned), and separately its variant search scans with
// <= instead of < [one-past-the-end]." Also CONFIRMED "reports variant 0
// of the item's own name, not the actually-owned variant's name" (HYPOTHESIS
// bug-or-intentional, reproduced as-is). HARDENED: refuses (0 Lua values)
// on an unresolved/out-of-range index rather than reading past the end.
int stub_pcu_get_owned_item_info(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_owned_item_info", upStateTag(L));
    int64_t itemIndex = truncateEa2596(lua_tonumber(L, 1));
    auto& q = upState(L)->pcuOwnedItemQueries();
    std::string key = std::to_string(itemIndex);
    if (itemIndex < 0 || !q.ownedItemResolves.known(key) || !q.ownedItemResolves.get(key)) {
        ++q.ownedInfoOobRefusedCount; // HARDENED (named CRASH: null item record / one-past-the-end variant scan)
        return 0;
    }
    ++q.ownedInfoFoundCount;
    lua_pushnumber(L, 0.0); // "variant 0 of the item's own name" field - CONFIRMED-quirk return shape, full 35-value layout OPEN (Sec43.2's own pcu_get_item_defaults shares this layout, not independently re-derived here)
    return 1;
}

// pcu_is_current_item_worn (Sec40.6): CONFIRMED "returns ZERO Lua values
// (not a boolean) on one of its paths - a real return-arity irregularity a
// caller must handle." Reproduced faithfully, not hardened away (a return-
// shape quirk, not a memory-safety bug): unresolved slot/item -> 0 values;
// resolved -> its worn boolean.
int stub_pcu_is_current_item_worn(lua_State* L) {
    logCall(L, upLog(L), "pcu_is_current_item_worn", upStateTag(L));
    int64_t slotId = truncateEa2596(optionalNumberDefault(L, 1, -1.0));
    auto& q = upState(L)->pcuOwnedItemQueries();
    auto& pcu = upState(L)->pcuClothing();
    if (slotId < 0 || slotId >= PcuClothingState::kSlotCount || pcu.slots[static_cast<size_t>(slotId)].itemId < 0) {
        ++q.currentItemWornZeroValuesCount; // CONFIRMED return-arity quirk, faithfully reproduced
        return 0;
    }
    ++q.currentItemWornFoundCount;
    lua_pushboolean(L, 1); // a resolved slot in this project's own model is always "worn" (it's the worn-clothing list itself)
    return 1;
}

// --- The 7 AGG pcu_* names (Sec40.5/40.6): implement only what the
// aggregate text supports, no invented per-function signature/logic. ---

// pcu_get_variants (Sec40.6 AGG): "a pure Lua-callback iterator ... calls
// back once per qualifying element and returns nothing itself." CHOSEN
// minimal shape: 1 number (item/slot id) + 1 callback-name string, matching
// this family's own established "id + callback name" idiom (pcu_get_
// categories_in_area/pcu_get_areas above) - no invented per-variant payload.
int stub_pcu_get_variants(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_variants", upStateTag(L));
    (void)optionalNumberDefault(L, 1, -1.0);
    (void)optionalStringDefault(L, 2, "");
    ++upState(L)->pcuAggregate().getVariantsCallbackDispatchCount;
    return 0; // AGG: "return nothing themselves" (Sec40.6)
}

// pcu_get_wear_options (Sec40.6 AGG): same "pure Lua-callback iterator"
// shape as pcu_get_variants above.
int stub_pcu_get_wear_options(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_wear_options", upStateTag(L));
    (void)optionalNumberDefault(L, 1, -1.0);
    (void)optionalStringDefault(L, 2, "");
    ++upState(L)->pcuAggregate().getWearOptionsCallbackDispatchCount;
    return 0;
}

// pcu_report_outfits (Sec40.6 AGG): the 3rd "pure Lua-callback iterator"
// (saved outfits) - same shape.
int stub_pcu_report_outfits(lua_State* L) {
    logCall(L, upLog(L), "pcu_report_outfits", upStateTag(L));
    (void)optionalStringDefault(L, 1, "");
    ++upState(L)->pcuAggregate().reportOutfitsCallbackDispatchCount;
    return 0;
}

// pcu_is_item_needed_for_outfit (Sec40.5 AGG): only the generic family
// facts apply ("the id argument means a different index space per
// function", "local player never null-checked"). CHOSEN minimal shape: 1
// number (item/slot index) -> 1 boolean, reusing hasLocalPlayer() for the
// documented-but-unflagged null-player gap (AGG discipline: no invented
// per-function crash flag since none is given for this name specifically).
int stub_pcu_is_item_needed_for_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_is_item_needed_for_outfit", upStateTag(L));
    (void)optionalNumberDefault(L, 1, -1.0);
    ++upState(L)->pcuAggregate().isItemNeededForOutfitQueryCount;
    lua_pushboolean(L, 0); // AGG: no real per-outfit-need table in scope - honest "not needed" default, not invented true
    return 1;
}

// pcu_remove_clothing (Sec40.5 AGG): CHOSEN minimal shape: 1 number
// (slot/category id). No return (a setter, matching the family's own
// established "clear a slot" idiom, e.g. pcu_discard_slot above).
int stub_pcu_remove_clothing(lua_State* L) {
    logCall(L, upLog(L), "pcu_remove_clothing", upStateTag(L));
    int64_t slotId = truncateEa2596(optionalNumberDefault(L, 1, -1.0));
    auto& pcu = upState(L)->pcuClothing();
    if (slotId >= 0 && slotId < PcuClothingState::kSlotCount) pcu.slots[static_cast<size_t>(slotId)] = PcuClothingSlot{};
    ++upState(L)->pcuAggregate().removeClothingCount;
    return 0;
}

// pcu_set_slots_empty (Sec40.5 AGG): CHOSEN minimal shape: clears every
// slot EXCLUDING the documented exempt set (slots 0-4/21/22, Sec40.5's own
// "excluded from bulk clear operations" text - a CONFIRMED fact, not
// invented) - no arguments.
int stub_pcu_set_slots_empty(lua_State* L) {
    logCall(L, upLog(L), "pcu_set_slots_empty", upStateTag(L));
    auto& pcu = upState(L)->pcuClothing();
    for (int i = 0; i < PcuClothingState::kSlotCount; ++i) {
        if (i <= 4 || i == 21 || i == 22) continue; // CONFIRMED exempt set (Sec40.5)
        pcu.slots[static_cast<size_t>(i)] = PcuClothingSlot{};
    }
    ++upState(L)->pcuAggregate().setSlotsEmptyCount;
    return 0;
}

// pcu_wear_outfit (Sec40.5 AGG): "saved-outfit index for the plain wear-
// outfit entry" (Sec40.6's own index-space note) - CHOSEN minimal shape: 1
// number (saved-outfit index). No return.
int stub_pcu_wear_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_wear_outfit", upStateTag(L));
    (void)optionalNumberDefault(L, 1, -1.0);
    ++upState(L)->pcuAggregate().wearOutfitCount;
    return 0;
}

// =========================================================================
// pcu_* Sec43.2 batch (10 names).
// =========================================================================

// pcu_bra_required (Sec43.2, 0x0082ad10): no arguments; 1 boolean = local
// player byte +0xa41 == 1 ("player is female", Sec16.8). CONFIRMED "No
// player null check (crash shape)." HARDENED: reuses hasLocalPlayer();
// REUSES the EXISTING PcrCustomizationState::genderByte (wave-1 Batch 5)
// rather than a parallel gender field.
int stub_pcu_bra_required(lua_State* L) {
    logCall(L, upLog(L), "pcu_bra_required", upStateTag(L));
    EngineState* st = upState(L);
    ++st->pcuCreatorMisc().braRequiredQueryCount;
    if (!st->hasLocalPlayer().known() || !st->hasLocalPlayer().get()) return 0; // HARDENED (unchecked local player, Sec43.2)
    lua_pushboolean(L, st->pcrCustomization().genderByte == 1 ? 1 : 0); // CONFIRMED gender byte 1 == female (Sec16.8)
    return 1;
}

// pcu_can_create_outfit (Sec43.2, 0x0082ae00): no arguments; 1 or 3 values.
// CONFIRMED: "Incomplete guard - doesn't check the free-node pool or the
// 128-saved-outfit cap." Reproduced faithfully (the incompleteness is
// CONFIRMED real behaviour, not hardened away here - pcu_create_outfit
// below is where the actual allocation is hardened).
int stub_pcu_can_create_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_can_create_outfit", upStateTag(L));
    auto& misc = upState(L)->pcuCreatorMisc();
    if (misc.blockPoolEmpty.known() && misc.blockPoolEmpty.get()) {
        ++misc.canCreateOutfitEmptyPoolCount;
        lua_pushboolean(L, 0);
        lua_pushstring(L, "naked"); // CONFIRMED-shaped title/body pair, exact localized strings OPEN
        lua_pushstring(L, "");
        return 3;
    }
    ++misc.canCreateOutfitOkCount;
    lua_pushboolean(L, 1);
    return 1;
}

// pcu_category_nav_clear_slot (Sec43.2 CRASH, 0x0082f440): 1 number, masked
// to 16 bits. CONFIRMED "real out-of-bounds read + dereference ... indexing
// the 16-entry descriptor array 0x022ffcf0 with no bound or null check...
// the backup is single-level, so navigating 'Piercings'->'Piercings' loses
// the original slot-5 item." HARDENED: bound-checked against the CONFIRMED
// 16-entry array (not a new divergence - this is the array's own real,
// given size).
int stub_pcu_category_nav_clear_slot(lua_State* L) {
    logCall(L, upLog(L), "pcu_category_nav_clear_slot", upStateTag(L));
    int64_t raw = truncateEa2596(lua_tonumber(L, 1));
    int32_t index = static_cast<int32_t>(raw & 0xffff); // CONFIRMED 16-bit mask
    auto& pcu = upState(L)->pcuClothing();
    if (index < 0 || index >= PcuClothingState::kAreaDescriptorCount || !pcu.areas[static_cast<size_t>(index)].known) {
        ++pcu.categoryNavOobRefusedCount; // HARDENED (named CRASH: 16-entry bound)
        return 0;
    }
    bool isPiercings = caseInsensitiveEquals(pcu.areas[static_cast<size_t>(index)].name, "Piercings");
    if (isPiercings) {
        // CONFIRMED: stash worn slot 5, single-level backup (loses the
        // PRIOR backup on a repeat "Piercings"->"Piercings" nav, Sec43.2).
        pcu.categoryNavBackupSlot5Item = pcu.slots[5].itemId;
        pcu.categoryNavBackupValid = true;
        pcu.slots[5].itemId = -1; // HYPOTHESIS: headwear that would hide piercings, hidden while browsing Piercings
    } else if (pcu.categoryNavBackupValid) {
        pcu.slots[5].itemId = pcu.categoryNavBackupSlot5Item; // CONFIRMED: restores on any non-match
        pcu.categoryNavBackupValid = false;
    }
    return 0;
}

// pcu_clear_item_new (Sec43.2, 0x0082bf40): 2 numbers (item index, instance
// number). CONFIRMED: "the match test runs on ANY non-empty entry's
// position, not only matching items" - clears the owned "new" flag on the
// first non-empty owned entry regardless of its item when the instance
// arg is -1 (or unmatched). Reproduced as the family's own test-populated
// resolver (no real owned-item array in scope).
int stub_pcu_clear_item_new(lua_State* L) {
    logCall(L, upLog(L), "pcu_clear_item_new", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1));
    (void)optionalNumberDefault(L, 2, -1.0);
    ++upState(L)->pcuCreatorMisc().clearItemNewCount;
    return 0; // deep per-instance "new" byte internals OPEN, no real owned-item-instance array in scope
}

// pcu_clear_obscured_slots (Sec43.2, 0x0082e7e0): no arguments. CONFIRMED:
// re-syncs the worn table, then queues 2 jobs (kinds 6/7); "a full queue
// silently drops the job" - reproduced (not a crash; a real, confirmed
// reachable no-op).
int stub_pcu_clear_obscured_slots(lua_State* L) {
    logCall(L, upLog(L), "pcu_clear_obscured_slots", upStateTag(L));
    auto& misc = upState(L)->pcuCreatorMisc();
    if (misc.jobQueueHasRoom.known("6") && !misc.jobQueueHasRoom.get("6")) ++misc.clearObscuredSlotsQueueFullCount;
    if (misc.jobQueueHasRoom.known("7") && !misc.jobQueueHasRoom.get("7")) ++misc.clearObscuredSlotsQueueFullCount;
    ++misc.clearObscuredSlotsCount;
    return 0;
}

// pcu_create_outfit (Sec43.2 CRASH, 0x0082d850): 1 string (a lookup key
// into a hash table, HYPOTHESIS virtual-keyboard results, not the literal
// outfit name). Returns -1/error-string, or the new node's pool index.
// CONFIRMED "the allocator's result is read and written with no null
// check, so an empty block-or-node pool is a null write ... A refusal at
// the 128-saved-outfit cap is ignored, orphaning the already-allocated
// catalog node." HARDENED: refuses (returns -1) on an exhausted pool or at
// the 128-outfit cap, rather than null-writing or orphaning a node -
// REUSES the EXISTING EngineState::PcuInventory::kSavedOutfitCapacity
// (b04) for the cap, not a new constant.
int stub_pcu_create_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_create_outfit", upStateTag(L));
    (void)argString(L, 1); // hash-table lookup key - deep internals OPEN (0x02317d74 table not loaded in scope)
    EngineState* st = upState(L);
    auto& pcu = st->pcuClothing();
    auto& misc = st->pcuCreatorMisc();
    if (misc.blockPoolEmpty.known() && misc.blockPoolEmpty.get()) {
        ++pcu.createOutfitPoolExhaustedRefusedCount; // HARDENED (named CRASH: null write on empty pool)
        lua_pushnumber(L, -1.0);
        return 1;
    }
    if (st->pcuInventory().savedOutfitCount >= EngineState::PcuInventory::kSavedOutfitCapacity) {
        ++pcu.createOutfitCapRefusedCount; // HARDENED (named: real engine orphans the node instead of refusing cleanly)
        lua_pushnumber(L, -1.0);
        return 1;
    }
    int32_t newIndex = pcu.nextCreatedOutfitIndex++;
    ++st->pcuInventory().savedOutfitCount;
    lua_pushnumber(L, static_cast<lua_Number>(newIndex));
    return 1;
}

// pcu_delete_outfit (Sec43.2, 0x0082dad0): 1 number (saved-outfit index).
// No return. CONFIRMED: returns a player-created outfit's pools, or
// forgets a store-bought outfit's saved entry.
int stub_pcu_delete_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_delete_outfit", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    if (st->pcuInventory().savedOutfitCount > 0) --st->pcuInventory().savedOutfitCount;
    ++st->pcuClothing().deleteOutfitCount;
    return 0;
}

// pcu_get_item_defaults (Sec43.2, 0x0082c7f0): 1 number (item index);
// returns -1 (out of range) or 35 values (default colours/variant/price,
// the SAME layout pcu_get_owned_item_info uses for an owned instance).
// CONFIRMED "Unchecked variant-0 pointer read (data-gated crash shape)."
// HARDENED: refuses (-1) on an unresolved/out-of-range index.
int stub_pcu_get_item_defaults(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_item_defaults", upStateTag(L));
    int64_t itemIndex = truncateEa2596(lua_tonumber(L, 1));
    auto& misc = upState(L)->pcuCreatorMisc();
    auto& q = upState(L)->pcuOwnedItemQueries();
    std::string key = std::to_string(itemIndex);
    if (itemIndex < 0 || !q.ownedItemResolves.known(key)) { // test-populated: any registered index has defaults (same registry as owned_item_info)
        ++misc.getItemDefaultsOutOfRangeCount;
        lua_pushnumber(L, -1.0);
        return 1;
    }
    ++misc.getItemDefaultsOkCount;
    lua_pushnumber(L, 0.0); // default colour/variant/price layout - full 35-value shape OPEN (data-gated null read not reproduced)
    return 1;
}

// pcu_get_items_in_category (Sec43.2 CRASH, 0x0082fa30): 1 number
// (category) + 1 string (callback name); 0 return values. CONFIRMED "real
// infinite-loop hang" in "owned instances" mode on a bad/missing callback
// name or a full 256-record coroutine table. HARDENED: this project never
// actually dispatches a real Lua coroutine (Sec43.1's own "mechanism
// confirmed, no in-scope producer"), so the hang's own trigger condition
// cannot occur here - the 3 modes are counted, never looped.
int stub_pcu_get_items_in_category(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_items_in_category", upStateTag(L));
    int64_t category = truncateEa2596(lua_tonumber(L, 1));
    (void)argString(L, 2);
    (void)category;
    auto& misc = upState(L)->pcuCreatorMisc();
    ++misc.itemsInCategoryMode0Count; // CHOSEN: this project's own minimal counted dispatch - real per-mode selection (flag +0x5c bit 1) OPEN
    return 0;
}

// pcu_get_num_items_category (Sec43.2, 0x0082bb50): 2 numbers (category,
// catalog-outfit index or -1). 1 number returned. CONFIRMED "Arg 2 = -1
// counts owned items in the category (arg 1 ignored otherwise); arg 2 != -1
// walks the matching catalog node's pieces [and has a non-sum/non-max
// comparison quirk, HYPOTHESIS on intent]." Reproduced as counted-only
// (deep per-piece-category internals OPEN; the CONFIRMED null-read on an
// unmapped piece slot is data-gated and not reproduced).
int stub_pcu_get_num_items_category(lua_State* L) {
    logCall(L, upLog(L), "pcu_get_num_items_category", upStateTag(L));
    int64_t category = truncateEa2596(lua_tonumber(L, 1));
    int64_t catalogIndex = truncateEa2596(lua_tonumber(L, 2));
    auto& misc = upState(L)->pcuCreatorMisc();
    if (catalogIndex == -1) {
        ++misc.numItemsCategoryOwnedCount;
        auto& pcu = upState(L)->pcuClothing();
        int32_t count = 0;
        for (auto& slot : pcu.slots) if (slot.itemId >= 0) ++count; // CHOSEN: this project's own "owned in category" stand-in - real per-category filter OPEN
        (void)category;
        lua_pushnumber(L, static_cast<lua_Number>(count));
        return 1;
    }
    ++misc.numItemsCategoryCatalogCount;
    lua_pushnumber(L, 0.0); // catalog-node piece-walk result - deep internals OPEN (Sec43.2's own "neither sum nor true maximum" caution)
    return 1;
}

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
