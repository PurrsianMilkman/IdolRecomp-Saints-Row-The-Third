// Small one-off engine-state records: ObjectiveTextSlot (objective_text),
// TeleportRequest (teleport), ConversationPlay (audio_conversation_play).
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

#include <cstdint>
#include <string>
#include <unordered_map>

#include "sr3luahost/open_state.h"

namespace sr3luahost {

// objective_text (spec-lua-api-behaviour.md Sec1.3), batch 2026-10-08
// (wave 1 batch 1, §1-4): one of the real 4-slot fixed record array's own
// slots. This project has no real localization table anywhere
// (0x00849df0/0x026e7e68/0x00604740/0x00604500 are all OPEN per Sec1.3's
// own text - "which concrete table it points to at runtime is still
// unidentified") - so only the raw, CONFIRMED request actually passed is
// recorded (arg 2's own raw text key, standing in for "whatever text
// pointer results" since real localization resolution/fallback cannot be
// reproduced), plus the CONFIRMED arg-3/arg-4 combining rule and the two
// raw flag values - same "record the request, not the resolved
// internals" convention as MinimapIconRecord/ObjectIndicatorRecord in character_state.h.
// Keyed by slot index in a map (not a fixed 4-element array) so no slot
// value can ever write out of bounds the way the real engine's own
// `< 4`-only bound check could for a negative index - a map is simply
// safe-by-construction here, not a claimed hardening of a confirmed bug
// (Sec1.3's own text does not state the comparison's signedness, so this
// project makes no claim that the original ever actually goes out of
// bounds). Slots outside [0,4) are still rejected by the stub before
// ever reaching this map, matching the one real, CONFIRMED bound check
// ("must be < 4") exactly.
//
// UPDATED (spec-hud-runtime.md Sec6.5 HUD-objective-overlay task,
// 2026-10-09): objective_text_clear (Sec1.4) now erases a slot's entry
// from this map on clear (EngineState::recordObjectiveTextClear()), so a
// slot's mere presence in the map is a correct, test-observable "is this
// slot currently active" signal, not just "was this slot EVER written"
// with no way to tell a later clear happened. This is what the new HUD
// objective-text overlay (tools/sr3_viewer.cpp) reads each frame.
struct ObjectiveTextSlot {
    std::string rawTextKey;      // arg 2, mandatory
    std::string combinedAuxText; // CONFIRMED "%s/%s" (arg3,arg4) when arg4 present AND non-empty, else arg3 verbatim
    int64_t flag5 = 3;           // arg 5, optional, default 3 (CONFIRMED)
    int64_t flag6 = 0;           // arg 6, optional, default 0 (CONFIRMED)
    uint64_t writeCount = 0;
};

// teleport (spec-lua-api-behaviour.md Sec4.3), batch 2026-10-08 (wave 1
// batch 1, §1-4): the last successful call's own raw request, keyed by
// the moved object's name. CONFIRMED: on EITHER resolve failure (dest via
// the type-gated resolver family §4.13, or the mover via the player-first
// character chain) the function returns immediately with NO request
// allocated at all - this project models that by leaving this map
// untouched on failure. This project has no coordinate system modelled
// anywhere (the same stated project-wide gap VehicleState::worldPosition's
// own doc comment already notes), so args 5-7 are kept only as the raw
// numbers actually passed, never applied to a real position.
struct TeleportRequest {
    std::string destinationName;
    double offsetX = 0.0, offsetY = 0.0, offsetZ = 0.0; // args 5-7, raw (CONFIRMED added to the dest's position by the real engine; no coordinate system modelled here)
    bool exitVehiclesArg3 = false; // CONFIRMED forwarded ONLY on the player path, never on the non-player path
    bool arg4 = true;
    bool arg8 = true;
    bool tookPlayerPath = false; // which of the two mutually exclusive apply paths the LAST call took
    uint64_t callCount = 0;
};

// audio_conversation_play (spec-lua-api-behaviour.md Sec2.10), batch
// 2026-10-08 (wave 1 batch 1, §1-4). CONFIRMED the opcode-0x51 host-gated
// queue path shares the identical host check (0x0087ba20,
// EngineState::coopLocalIsHost()) audio_conversation_end (Sec8.2) already
// establishes as CONFIRMED FALSE in single player - that path therefore
// never fires in this project's scope, same "reduces to argument parsing"
// boundary that function already documents for the identical gate, and is
// not separately tracked here.
//
// CLOSED 2026-10-09 (mission-census item, missions m10/mm_m16_5): the
// mark/interrupt step and the independent 0x008788e0 direct dispatch key
// off the LOOKUP side of a real, fully-understood 3-slot active-
// conversation-id table (`0x014eb4a0`, 20 bytes/slot) - NOT one unrelated
// "is there a current conversation" global as the prior text had it. All 3
// slots are zero/sentinel-initialized once at module init (CONFIRMED,
// `0x006de8e0`), so a fresh engine always, CONFIRMED-ly, resolves "no
// match" for every id - never an unknown. The LOOKUP side (`0x006de8a0`)
// takes a 1-byte key; a stack-slot trace proves that AT THIS CALL SITE the
// key is reloaded from this SAME function's own first Lua argument - i.e.
// "is there a current conversation" really means "does THIS call's own
// numeric id already occupy one of the 3 slots," explaining why playing
// the same id again self-interrupts. The REGISTER side (`0x006de820`,
// which claims a free slot for an id) has exactly 2 real callers
// (`0x006df0b0`'s local "start a new conversation" path and `0x006df4f0`'s
// network-consumer path) - NEITHER of which is this native (`0x00a3c9d0`/
// `0x006df210`) - so this native's own body never populates a slot itself;
// absent a Lua-native model of either real caller, this native can only
// ever observe an id as "active" if a test puts it there first (same
// "test-registrable, honestly false/none absent a registration"
// convention as CharacterState::inventoryBestSlotItemForTesting).
struct ConversationPlay {
    uint64_t playCount = 0;
    uint8_t lastIdByte = 0; // arg 1, truncated to a byte (CONFIRMED)
    // Keyed by the id byte. Presence = this id occupies one of the 3 real
    // slots (test-asserted precondition, standing in for the real
    // pointer-!=-sentinel test); value = whether THIS slot's own second
    // byte flag has already been marked by a prior lookup that found it -
    // the real engine's own "mark, then interrupt if already marked" step,
    // now correctly scoped per-id instead of globally (the prior single
    // `alreadyActiveFlag` bool below was a real modeling bug once two
    // DIFFERENT ids were in play - fixed by this same 2026-10-09 pass).
    // Absent = CONFIRMED not-active (the table's real zero-init state at
    // module load), never an OPEN unknown.
    std::unordered_map<uint8_t, bool> activeConversationIdForTesting;
    uint64_t interruptCount = 0;      // this id's own slot was already marked -> interrupt/stop pair fired (CONFIRMED)
    uint64_t directDispatchCount = 0; // 0x008788e0 fired (gated on this id's slot being found + mode bit 0x2, always true here, CONFIRMED)
};

// inv_weapon_add_temporary (spec-lua-api-behaviour.md Sec4.10) /
// inv_weapon_remove_temporary (Sec4.11), batch 2026-10-08 (the 6-native
// clearance fast-follow onto wave 1 batch 1's own original §1-4 scope -
// these two were originally deferred alongside on_dismiss/character_
// prevent_flinching/character_allow_ragdoll/npc_combat_enable, see this
// batch's own top comment, until spec-lua-api-behaviour.md's 2026-10-08
// re-derivation pass cleared all 6 for implementation).
//
// One real per-target, per-weapon inventory-slot record - CONFIRMED to
// exist (both natives' own apply routines locate-or-create it via
// `weapon_def+0x1b78` sub-object lookups, Sec4.10/Sec4.11's own 2026-10-08
// disassembly). No InventoryState/weapon-table model exists anywhere else
// in this project either (the SAME "no model exists yet, new minimal map"
// finding Sec41.3/Sec54.3's own batch header already made for this
// header's inventoryAmmo_/inventoryItemCounts_ etc. above) - this is a
// new, dedicated record for the SAME reason: a genuinely different real
// mechanism (a per-weapon "temporary grant," not the plain ammo/item-count
// maps above).
//
// CONFIRMED fields: temporaryGrant (Sec4.10 "a 'temporary grant' marker,"
// Sec4.11 "clears the ... marker" - the add/remove natives' own shared
// apply routines write/clear it respectively) and ammoCount (Sec4.10's own
// arg 3, "an ammo/round count," rounded, default 1 - Sec4.11 has no
// equivalent argument at all and instead clears "the recorded ammo/flags,"
// reproduced here as zeroing this same field).
//
// HIGH-CONFIDENCE-ONLY fields: Sec4.10's own text is explicit that "the
// specific role of each of the four boolean flags and the trailing numeric
// tag" (args 4-7, arg 8) was read but "recovering a full field dictionary
// for that densely bit-packed record was out of this task's own budget" -
// so, same "record the request, not the resolved internals" convention as
// ObjectiveTextSlot above, these 5 values are stored RAW, by their literal
// Lua-argument position, rather than inventing a semantic name (e.g.
// "autoSelect") for whichever one the spec's own prose gestures at without
// pinning down which bit each is.
struct WeaponTemporaryInventorySlot {
    bool temporaryGrant = false;                        // CONFIRMED marker, Sec4.10 sets / Sec4.11 clears
    int64_t ammoCount = 0;                               // CONFIRMED arg 3 (Sec4.10 only); Sec4.11 zeroes this on remove
    bool rawArg4 = false, rawArg5 = false, rawArg6 = false, rawArg7 = false; // HIGH CONFIDENCE only - raw, unattributed
    int64_t rawArg8 = 0;                                 // HIGH CONFIDENCE only - raw "numeric tag," unattributed
    uint64_t addCount = 0;    // inv_weapon_add_temporary call count on this slot
    uint64_t removeCount = 0; // inv_weapon_remove_temporary call count on this slot
    // Equip-state update ("if the removed weapon was the character's
    // currently-equipped one, updates the current-equip state accordingly,"
    // Sec4.11) is a real, structurally-CONFIRMED side effect
    // (+0x1a1 bit 0x2 -> +0x2c0/+0x2c4) this project has no "currently
    // equipped weapon" model anywhere to drive - genuinely OPEN, not
    // modeled (Sec4.11's own text: "not traced further, same HIGH-
    // CONFIDENCE scope as before" - deferred, not guessed at).
};

} // namespace sr3luahost
