// lua_spec_confirmed_stubs_b24_wave2_pcu_pcr_part2.cpp
//
// Second half of lua_spec_confirmed_stubs_b24_wave2_pcu_pcr.cpp's stub functions, moved
// here unchanged to keep each file under ~1,500 lines (Team C refactor, 2026-10-17). The
// names/registration pair stays in that file; see its header comment for scope, sources
// and policy.

#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include <cctype>

namespace sr3luahost {
namespace stubs_b24 {

using PcuClothingState = EngineState::PcuClothingState;

using PcuClothingSlot = EngineState::PcuClothingSlot;

// Defined in lua_spec_confirmed_stubs_b24_wave2_pcu_pcr.cpp.
std::string toLowerAsciiB24(const std::string& s);

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

} // namespace stubs_b24
} // namespace sr3luahost
