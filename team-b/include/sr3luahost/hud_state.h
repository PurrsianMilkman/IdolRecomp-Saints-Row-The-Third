// HUD engine state: the bar/timer/fake-notoriety slots, HudHardeningCounters,
// HudDisplayStatePool and QteSlotEntry.
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
#include <string>
#include <vector>

namespace sr3luahost {

// --- Batch 2026-10-08 (Sec5-9 fan-out, Batch 2): shared HUD bar/timer/
// fake-notoriety slot shapes - see EngineState's own hudBarSlot()/
// hudTimerSlot()/fakeNotorietySlots() doc comments for the full
// citation/reasoning (hud_bar_off/on/set_value, Sec8.14/8.15/8.16;
// hud_timer_set/stop, Sec8.19/8.20; hud_set_fake_notoriety, Sec8.18).
constexpr int32_t kHudBarSlotCount = 4;   // CONFIRMED "0-3" id range, Sec8.14
constexpr int32_t kHudTimerSlotCount = 4; // CONFIRMED "< 4" cap, Sec8.19

// hud_bar_off (Sec8.14) / hud_bar_on (Sec8.15) / hud_bar_set_value
// (Sec8.16): one shared per-slot record. `on` is this project's own
// HIGH-CONFIDENCE boolean stand-in for the real state-code writer
// (hud_bar_off's own literal 7, "inferred... from the registered
// function's own name" per Sec8.14's own text; hud_bar_on's own path
// does not share that same state-code literal, so this is a plain
// derived flag, not a literal mirror of a single real field).
// immediateValue/animatedTargetValue are the two REAL, CONFIRMED fields
// (Sec8.16's own +0x24/+0x28 pair; Sec8.15's own +0x4c/+0x50 pair on its
// own larger record - this project does not track the two pairs
// separately, since no in-scope reader distinguishes them). secondaryId/
// label are hud_bar_on's own arg 2 (passed through unresolved, CONFIRMED)
// and arg 3 (a raw text key - the 3-stage localization chain it shares
// with objective_text, Sec1.3, is OUT of this batch's scope, so the raw
// string is kept, never localized). The 3 counters are test-observable
// stand-ins for real engine side effects this project does not otherwise
// model (dirty-byte marks, the opcode-0x3e queue, and the local-refresh
// call 0x007eaec0) - never a real render/network effect.
struct HudBarSlot {
    bool on = false;
    std::string secondaryId;
    std::string label;
    double immediateValue = 0.0;
    double animatedTargetValue = 0.0;
    uint64_t dirtyCount = 0;
    uint64_t queuedCommandCount = 0;
    uint64_t localRefreshCount = 0;
};

// hud_timer_set (Sec8.19) / hud_timer_stop (Sec8.20): same shape/
// reasoning as HudBarSlot above, on the timer array's own distinct base.
// textKey/textKeyResolved stand in for arg 3's own small fixed-capacity
// name<->id table (0x00a1fcc0) this project has no real data for - a
// non-empty key is simply treated as "resolved" (CHOSEN, no real table).
struct HudTimerSlot {
    bool on = false;
    double value = 0.0;
    std::string textKey;
    bool textKeyResolved = false;
    uint64_t dirtyCount = 0;
    uint64_t queuedCommandCount = 0;
    uint64_t localRefreshCount = 0;
    uint64_t staleHandleClearCount = 0; // 0x005e4520, shared with hud_timer_stop
    // hud_timer_get_remainder (Sec20.22): the record's own +0x28 field, a
    // SEPARATE signed remaining-ticks int32 from `value` above (that field's
    // own real role is write-side, set by hud_timer_set's own arguments;
    // this one is this native's own distinct read target - CONFIRMED
    // distinct per Sec20.22's own text, not independently cross-checked
    // against `value`'s real offset). CHOSEN 0 default (no spec default).
    int32_t remainingTicks = 0;
};

// hud_set_fake_notoriety (Sec8.18): one of the 4 real canonical track
// slots (0-3) or the CONFIRMED sentinel slot 5 for an unmatched name
// (index 4 is never produced by resolveFakeNotorietySlot() in the stub
// file and is left unused - not a bug, just an unused array slot).
struct FakeNotorietySlot {
    bool active = false;
    double level = 0.0;
};

// HARDENING POLICY counters (per-site, named) - see hud_bar_off/on/
// set_value's and hud_timer_set/stop's own stub doc comments
// (src/lua_spec_confirmed_stubs_b09_batch2_hud_audio.cpp) for which
// native fires which counter and why: none of hud_bar_off/on/set_value's
// nor hud_timer_set/stop's own spec text (Sec8.14/8.15/8.16/8.19/8.20)
// documents a bounds check on the Lua-supplied slot id, yet all 5 index
// a small fixed-size array on the real engine (4 bar slots / 4 timer
// slots) - an out-of-range id would be an OOB array access on the real
// binary. HARDENED here (CHOSEN divergence): refused, never replicated.
struct HudHardeningCounters {
    uint64_t barOffOobCount = 0;
    uint64_t barOnOobCount = 0;
    uint64_t barSetValueOobCount = 0;
    uint64_t timerSetOobCount = 0;
    uint64_t timerStopOobCount = 0;
    // hud_timer_get_remainder (Sec20.22): CONFIRMED "no bounds check on the
    // index" at all (neither upper NOR lower) - the SAME shape as
    // hud_timer_set's own documented "< 4" cap above, not a worse one: that
    // cap is itself only a "table isn't full" check on the refresh step,
    // not an index bound (post-merge correction - the stub's own original
    // comment overstated this as "worse than the setter's cap"; the getter
    // has the identical gap, no better or worse). HARDENING POLICY (CHOSEN
    // hardened divergence): id in [0,4) reproduces the real read exactly;
    // any other id (negative OR >= 4) is refused (0.0 returned, no real
    // array read) rather than replicate an out-of-bounds read this host has
    // no safe way to perform.
    uint64_t timerGetRemainderOobCount = 0;
};

// --- Batch 5 (ranks 251-350, spec-lua-api-behaviour.md Sec15/Sec16),
// 2026-10-08 - HUD-slot/customization/pause-menu cluster. See
// lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp's own doc
// comment above each stub_* function for the full citation/reasoning. ---

// hud_display_create_state/remove_state/commit_state (Sec15.24/15.12/
// 15.25), reused by qte_cleanup (Sec15.6): the real pool is "~20-slot,
// indices 15..." with "5 slots reserved below index 15 for an
// unidentified purpose" (Sec15.29 item 4) - this project models only the
// 5 CONFIRMED-allocatable slots (ids 15-19), since nothing in scope ever
// exercises the unidentified reserved-below-15 range. The "5-deep
// pending-transition queue" commit_state enqueues into and remove_state
// advances (Sec15.29 item 4) is modeled as a plain FIFO of slot ids - a
// stated simplification of the real "15 parallel per-slot sub-fields
// reset to value 3" internal bookkeeping (Sec15.12's own text), since no
// in-scope Lua function reads that bookkeeping back; only the queue's
// own membership (reject-if-full/already-queued) is test-observable.
struct HudDisplayStatePool {
    static constexpr int kSlotCount = 5;
    static constexpr int kFirstSlotId = 15;
    std::array<bool, kSlotCount> used{};
    std::array<bool, kSlotCount> removed{};
    std::vector<int> pendingQueue; // CONFIRMED max depth 5 (Sec15.25)
    bool lastStashedBool = false;
    uint64_t createCount = 0;
    uint64_t removeCount = 0;   // the unconditional record-and-replicate call (Sec15.12) - fires every call, valid or not
    uint64_t commitCount = 0;   // the unconditional record-and-replicate call (Sec15.25) - fires every call, valid or not
    uint64_t commitRejectedCount = 0; // HOST-SAFETY/test-observable: queue full, already queued, or invalid slot
    uint64_t removeInvalidCount = 0;  // HOST-SAFETY/test-observable: out-of-range, not-used, or already-removed slot

    // hud_display_set_element (Sec17.26, wave 2 batch W2-2, 2026-10-08): a
    // SEPARATE 20-slot table within this SAME real HUD display-state slot
    // family (0x022b7095 "active" byte array stride 0x40, 0x022b7058 value
    // array, up to 12 sub-fields/slot) - genuinely distinct storage from
    // the 5-slot alloc-tracking arrays above (`used`/`removed` only cover
    // the CONFIRMED-allocatable ids 15-19; this table's own CONFIRMED range
    // is the full 0-19). OPEN whether the "active" byte here is the SAME
    // real bit `used` above tracks - not asserted either way (no spec text
    // ties the two addresses together); kept as independent storage,
    // test-settable via EngineState::setHudDisplayElementActiveForTesting().
    static constexpr int kElementSlotCount = 20;     // CONFIRMED (slotId < 0x14)
    static constexpr int kElementSubFieldCount = 12; // CONFIRMED ("up to 12 sub-fields")
    static constexpr int kElementSetAllSentinel = 0xf; // CONFIRMED subIndex == 0xf means "all sub-fields"
    std::array<bool, kElementSlotCount> elementActive{};
    std::array<std::array<double, kElementSubFieldCount>, kElementSlotCount> elementValues{};
    uint64_t setElementCount = 0;        // the unconditional record-and-replicate call - fires every call
    uint64_t setElementAppliedCount = 0; // the direct-apply write actually landed (valid slot, active)
    // CHOSEN (hardened divergence, Sec17.26): the spec's own text documents
    // only two subIndex shapes (a single sub-field 0..11, or the 0xf "all"
    // sentinel) feeding a fixed 12-element array with no stated bounds
    // check of its own - an unbounded subIndex outside {0..11, 0xf} (or a
    // negative/>=20 slotId, also never explicitly bounds-checked below the
    // stated "< 0x14" test) is exactly the shape of OOB-write this
    // project's hardening policy addresses. This project refuses the write
    // (no OOB access, matching the common-case "out of range -> no
    // observable local-apply effect" shape the slot-id bound already
    // documents) rather than reproduce an unconfirmed real crash, and
    // counts every such refusal separately from the ordinary "slot not
    // active" miss above.
    uint64_t setElementHardenedSubIndexCount = 0;
    uint64_t setElementHardenedSlotIdCount = 0;
};

// qte_cleanup (Sec15.6): "every currently-tracked QTE slot" lives in a
// fixed global list this project does not independently size (OPEN,
// Sec15.6's own "the many parallel 0x014b01xx QTE-context arrays were not
// mapped field-by-field" item) - no Lua-visible function in THIS batch's
// 41-name scope ever populates one (qte_start is explicitly out of this
// wave's scope, per the scoping report's backlog), so this project's own
// registry is populated ONLY via EngineState::registerActiveQteSlotForTesting(),
// matching this header's own established "mechanism confirmed, no in-
// scope producer wires it up" convention (markScriptNpcBoundForTesting(),
// registerNotorietyTrackCodeForTesting(), etc.). linkedHudSlotId models
// the real cross-link Sec15.6's own text confirms ("the same slot-release
// helper hud_display_remove_state's own dispatch reaches") - -1 = no
// linked HUD-display slot for this QTE entry.
//
// EXTENDED 2026-10-08 (wave 2 batch W2-1, qte_start, Sec20.9): qte_start is
// now an in-scope PRODUCER of these entries - its authoritative branch
// (0x0060dd60) "populates a shared per-slot QTE-context array family already
// flagged OPEN at Sec15.6/Sec15.30/Sec15.29" - the SAME 0x014b01xx family
// qte_cleanup's own teardown walks, so qte_start appends here (via
// EngineState::qteStartActivate()) rather than a parallel structure, and a
// later qte_cleanup tears its entries down exactly like test-registered
// ones. The fields after linkedHudSlotId are what qte_start itself knows;
// they keep their defaults for registerActiveQteSlotForTesting() entries.
// NOT populated: EngineState::QteSlot (qte_human_is_used's two fixed
// 0x014b01c0/0x014b0230 records, Sec32.3) - qte_start's slot index comes from
// a per-character byte (+0x158 off a sub-object, via 0x008ae020) whose value
// Sec20.9 does not give, and Sec32.3 itself leaves OPEN whether its two slots
// are the ones qte_cleanup tears down.
struct QteSlotEntry {
    int linkedHudSlotId = -1;  // -1: qte_start's local-player HUD routing (0x007efb40/0x007efd80/0x007efab0) is HIGH CONFIDENCE-inferred only, not modelled
    std::string characterName; // qte_start arg 1 ("" for a test-registered entry)
    uint32_t definitionHash = 0; // qte_start arg 2 via 0x00d9e8b0 (sr3save::nameHash, CONFIRMED match - see b02)
    std::string definitionName;  // arg 2 as passed (diagnostic; the engine keeps only the hash)
    std::string anchor1Name;     // arg 4 if given AND resolved, else "" = global default pair 0x01180028/0x0118002c
    std::string anchor2Name;     // arg 6 if given AND resolved, else "" = the same global default
    bool flag = false;           // arg 5, default false
};

} // namespace sr3luahost
