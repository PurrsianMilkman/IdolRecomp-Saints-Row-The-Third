// Player-creator customization engine state: PcrCustomizationItem,
// PcrCustomizationState.
//
// Split out of engine_state.h (2026-10-08) as a zero-behavior-change
// structural move, so that a merge can only ever land a new field in its
// own domain's file - the monolithic header kept having fields from one
// struct merged into a DIFFERENT struct whose closing brace sat next to
// it. engine_state.h includes this header, so callers keep including
// engine_state.h only; add new fields to these structs HERE.
//
// In the doc comments below, "this header", "this document", "this file"
// and "this header's own top note" keep their pre-split meaning: the
// engine-state header set as a whole (engine_state.h plus the per-domain
// headers it includes), whose top note is at the head of engine_state.h.
// "above"/"below" pointers to a type or member now in another file were
// re-worded to name that file; every other line moved verbatim.
#pragma once

#include <array>
#include <cstdint>

namespace sr3luahost {

// --- Batch 5 (ranks 251-350, spec-lua-api-behaviour.md Sec15/Sec16),
// 2026-10-08 - HUD-slot/customization/pause-menu cluster. See
// lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp's own doc
// comment above each stub_* function for the full citation/reasoning. ---

// pcr_change_hair (Sec16.6) / pcr_change_composite (Sec16.7) /
// pcr_get_player_preset+pcr_set_identity (Sec16.8/16.8a): these four share
// one underlying "per-character customization item list" mechanism and
// one player gender/ethnicity pair (Sec16.16's own cross-function note).
//
// CROSS-BATCH COORDINATION NOTE (per the orchestrator's own scoping brief,
// §4): the SAME shared 63-slot item list and dual local-player
// customization state is also touched by Batch 4's own
// customization_outfit_wear/customization_item_wear/customization_item_
// revert (not yet merged into this worktree at the time this was
// written). This struct is THIS batch's own, independently-written model
// of that mechanism - it does NOT attempt to pre-guess Batch 4's own field
// layout. Whoever reconciles the two batches at merge time should treat
// this struct and Batch 4's own customization state as two independent
// models of the same real underlying mechanism (the 63-slot
// 0x026533ac/0x026533b0 array, the per-category table 0x0265430c/
// 0x02654304, the tint storage 0x026534b0, the dirty flag 0x02653384),
// not as already unified - a future PCU/PCR-family pass (the ~35-function
// backlog surface the scoping report's own §5 flags) is the right place
// to actually merge them.
//
// This project has only ever modeled ONE implicit local player (no split-
// screen/second-local-player support anywhere in scope, same "no generic
// entity shape invented" boundary this header states throughout) - so
// this is a single EngineState-level instance (EngineState::
// pcrCustomization()), not keyed by name.
struct PcrCustomizationItem {
    int32_t categoryId = -1; // -1 = empty slot
    int32_t styleId = -1;    // pcr_change_hair's own resolved style id, -1 = none/cleared
    // pcr_change_composite's own raw arguments (Sec16.7): this project
    // stores the Lua-given numeric arguments DIRECTLY, NOT the real
    // per-item-color-list-resolved, gamma-corrected RGB byte triple
    // (0x00832230/0x00ea4300's own further transform is OPEN per Sec16.7)
    // - a stated simplification, not a claim of byte-identical storage.
    double rawArg2 = 0.0;
    double rawArg3 = 0.0;
    double rawArg4 = 1.0; // CONFIRMED default 1.0 when omitted (Sec16.7)
};
struct PcrCustomizationState {
    static constexpr int kSlotCount = 63; // CONFIRMED "up to 0x3f" (Sec16.7)
    std::array<PcrCustomizationItem, kSlotCount> items{};
    // pcr_change_composite's own special-cased category 0x16 ("fingernails",
    // HIGH CONFIDENCE id-only inference, Sec16.7) mirror item, keyed by its
    // own literal name "fingernails_r" - this project's own minimal stand-
    // in for the real second-item mirror write, not a generic named-item
    // registry.
    PcrCustomizationItem fingernailsRMirror;
    bool fingernailsRMirrorSet = false;
    uint64_t compositeLookupFailedCount = 0; // Sec16.7's own "category -1" reset/notify path
    bool dirty = false; // per-character "list changed" flag (0x02653384)

    // pcr_get_player_preset / pcr_set_identity (Sec16.8/16.8a): object
    // +0xa41 (gender) / +0xa40 (ethnicity). CONFIRMED construction
    // sentinels (Sec16.8, 0x00954340): 3 = "unspecified" gender, 5 =
    // "unspecified" ethnicity - OUT OF BOUNDS for both real lookup tables
    // (2-entry gender, 4-entry ethnicity), CONFIRMED (Sec16.8) to never
    // reach a real script in practice because game-start always resolves
    // one of {a loaded save, the player_presets.xtbl "Default" row
    // (male_white = 0/3), a co-op guest record} first - a pipeline this
    // project does not model. pcr_get_player_preset below therefore treats
    // the sentinel as "the shipped Default preset" (CONFIRMED literal
    // gender=0/ethnicity=3, Sec16.8) rather than refusing or indexing out
    // of bounds - a CHOSEN simplification grounded in a CONFIRMED spec
    // fact, not an invented one, explicitly not a claim that this project
    // models save-loading or a non-Default preset choice.
    uint8_t genderByte = 3;
    uint8_t ethnicityByte = 5;
    uint64_t setIdentityBodyRefreshCount = 0; // pcr_set_identity's own 0x00827960/0x0082e210 pair, no in-scope observable effect beyond this count

    // pcr_change_skin_color's own dedicated slot (Sec19.15): a genuine,
    // disassembly-confirmed direct consumer of this SAME 63-slot list in
    // the real engine, but Sec19.15 does not independently confirm WHICH
    // real category id it targets (OPEN - see 0x00834780's own doc note).
    // Stored in its OWN dedicated field here, matching fingernailsRMirror's
    // existing "dedicated field, not a reused slot" convention, rather than
    // aliasing real category 0 ("lips," the SAME category the kCategoryNames
    // table pcr_set_random_composite/pcr_get_composite_info use) the way a
    // prior version of the pcr_change_skin_color stub did (writing into
    // items[0] and silently corrupting real lips-category data).
    PcrCustomizationItem skinColorItem;
    bool skinColorSet = false;
};

} // namespace sr3luahost
