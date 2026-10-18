// CharacterStateScriptedActions: one part of CharacterState (character_state.h).
//
// Split out of character_state.h (Team C refactor, 2026-10-17); every line
// below moved verbatim. The doc-comment conventions of character_state.h's
// own top note apply ("this header" = the engine-state header set).
#pragma once

#include "sr3luahost/character_state_records.h"

namespace sr3luahost {

// Part of CharacterState: the wave-1 batch, engine callback firing, human shield / synced action / RC links, regeneration, scripted actions, turn_to, leash, and the batch-5 HUD-slot fields.
struct CharacterStateScriptedActions {
    // --- Batch 2026-10-08 (wave 1 batch 1, spec-lua-api-behaviour.md
    // §1-4): ai_do_scripted_move (Sec3.11), on_attack_performed (Sec3.12),
    // set_player_can_enter_exit_vehicles (Sec4.6) - see each native's own
    // doc comment (lua_spec_confirmed_stubs_b08_batch1_core.cpp) for the
    // full per-function citation. ---------------------------------------

    // ai_do_scripted_move (Sec3.11): the character's own pending-scripted-
    // move sub-record (+0x4a8..+0x500) flags byte at +0x4fd - CONFIRMED
    // (2026-10-07 direct disassembly) bit 0x04 <- arg 3, bit 0x08 <- arg 4,
    // a read-modify-write merge of only those two bits (every other bit of
    // this byte has no in-scope writer in this project). OpenBits32 since
    // only these 2 bits are ever confirmed; the real engine re-templates
    // the WHOLE sub-record (including this byte) from a fixed 88-byte
    // template on every call per Sec3.11's own text, so in practice no
    // other bit is ever non-zero going into the merge anyway - modelled
    // as a proper bit-merge regardless, for fidelity to the CONFIRMED
    // mechanism rather than relying on that coincidence.
    OpenBits32 pendingScriptedMoveFlags{
        "character pending-scripted-move flags byte (+0x4a8..+0x500)+0x55 (+0x4fd)",
        "spec-lua-api-behaviour.md Sec3.11"};
    // The destination's own resolved handle/identity - CONFIRMED stored
    // verbatim as a re-resolvable reference at the sub-record's own
    // +0x4b0/+0x4b4 (a handle, NOT a cached position, Sec3.11's own
    // 2026-10-07 resolution) - this project's standard name-string stand-
    // in for an object identity (this header's own top note).
    std::string pendingScriptedMoveDestination;
    uint64_t scriptedMoveDispatchCount = 0; // AI mode 0x18 dispatch count (0x004f4d00), CONFIRMED hardcoded mode/flag

    // on_attack_performed (Sec3.12): CONFIRMED two DIFFERENT hook slots on
    // two DIFFERENT sub-record bases for what the calling script sees as
    // one event - slot 8 on the generic/#PLAYER#-resolve path (base
    // +0x1f00, the SAME base onDeathCallback/onVehicleDestroyedPlayerSlotCallback
    // above use), slot 13 (0xd) on the plain-character-resolver fallback
    // (base +0xf8). Same single-dedicated-field-per-hook-kind convention
    // as onDeathCallback above - no generic slot array, since this
    // project's own minimal registry only ever tracks the hook kinds
    // actually in scope. Default empty ("no callback registered").
    std::string onAttackPerformedGenericSlotCallback;   // generic/#PLAYER# path, slot 8, base +0x1f00
    std::string onAttackPerformedCharacterSlotCallback; // plain-character-resolver fallback, slot 13 (0xd), base +0xf8

    // on_vehicle_enter (Sec4.12, 0x00a588e0), RESOLVED 2026-10-08 by full
    // re-disassembly of 0x005e4660/0x00a56d50: a THREE-way branch, NOT an
    // immediate numeric-event post (the spec's own earlier, self-flagged
    // "NEEDS-EXE" alternative reading is refuted and superseded). Arg 1 is
    // a callback/hook name (not an occupant); arg 2 is tried first against
    // the generic/`#PLAYER#` chain (playerClassChainResolves(), the SAME
    // map on_attack_performed/on_vehicle_destroyed/character_kill above
    // already use) - on success, registers into THIS character's own hook
    // slot 0x16, base +0x1f00 (the SAME base onDeathCallback/
    // onAttackPerformedGenericSlotCallback above use), via the SAME
    // 0x005e4660 helper. Default empty ("no callback registered"); same
    // empty-string-clears-slot convention as every other 0x005e4660-backed
    // field above.
    std::string onVehicleEnterGenericSlotCallback;

    // on_vehicle_enter's SECOND branch (reached only when the first
    // fails): arg 2 tried against the type-gated resolver bit 0x4@+0xb -
    // the SAME resolver release_to_world's second attempt uses (Sec4.4) -
    // on success, registers into hook slot 0x17, base +0xf8 (the SAME
    // base onAttackPerformedCharacterSlotCallback above uses), via the
    // SAME 0x005e4660 helper. This project's flat objectResolves() map
    // cannot by itself distinguish "bit 0x4@+0xb resolved" (this field)
    // from "bit 0x8@+0xb resolved" (the THIRD branch,
    // VehicleState::onVehicleEnterCallback) - EngineState::
    // onVehicleEnterResolverKind() tells them apart (mirrors
    // release_to_world's own releaseToWorldKind(), per this task's
    // explicit instruction to reuse that exact resolution shape). Default
    // empty.
    std::string onVehicleEnterCharacterSlotCallback;

    // ---- engine callback firing (spec-lua-api-behaviour.md "Engine callback dispatch", 2026-10-14) ----
    // (on_vehicle_exit registrations are kept by target name: EngineState::setOnVehicleExitHook.)
    // Attacker of the most recent damage application (+0x1570/+0x1574, spec B): "" = no attacker. Rewritten on
    // every application, frozen once the character is dying.
    std::string lastAttackerName;
    // Death-callback latch (+0xe8 bit 0, spec B): set once on_human_killed/on_death were queued for this
    // life, cleared when the character is alive again.
    bool deathCallbackLatch = false;
    // The authored team name (zone script_npc_team / set_team's argument); "" = unknown. The engine maps the
    // team byte +0x1ca8 to a name through a team table this project does not have (OPEN), so this is the
    // authored string, passed as on_human_killed's first argument.
    std::string teamName;
    // Harness bookkeeping only (--auto-objective): this character was handed an ai_add_enemy_target order.
    bool hostileTargetOrderSeen = false;

    // ---- human shield link (spec-lua-api-behaviour.md Sec13.14, blocker dlc3_m01 + m12, FINAL) ----
    // +0x1890/+0x1894: the partner's identity ("" = none); +0x18dc bit 0: "I am the hostage, not the holder";
    // the state byte is humanShieldStateByte18d4 (1 holding, 2 grab in progress, 3/4 released, 0 none).
    std::string humanShieldPartnerName;
    bool humanShieldIsHostage = false;

    // ---- synced action instance (spec Sec18.5, blocker m07, FINAL) ----
    // The animation controller's current action animation instance (+0x20; -1 = none): start time and length in
    // game seconds. Duration <= 0 means no active instance.
    double syncedAnimStartSeconds = 0.0;
    double syncedAnimDurationSeconds = 0.0;

    // ---- RC control (spec Sec18.10, blocker m20, FINAL): the character's active-vehicle pair (+0x16d8/+0x16dc) ----
    std::string rcActiveVehicleName;

    // set_player_can_enter_exit_vehicles (Sec4.6): per-player byte +0x28ac
    // bit 0x10 - CONFIRMED by its own literal `.rdata` debug-label string
    // tail, "cant_enter_exit_vehicles". Same double-gate shape as every
    // other force-flag field in this struct (CONFIRMED true in single
    // player via forceFlagGatePasses above, Sec3.7). Stored as the bit's
    // OWN sense ("cannot enter/exit") - the setter is called with the
    // LOGICAL NEGATION of the Lua argument ("can enter/exit"), CONFIRMED.
    OpenBits32 flags28ac{"character byte +0x28ac (cant_enter_exit_vehicles 0x10)",
                          "spec-lua-api-behaviour.md Sec4.6"};
    static constexpr uint32_t kCantEnterExitVehiclesBit28ac = 0x10u;

    // --- character_dont_regenerate (spec-lua-api-behaviour.md Sec30.1,
    // "ranking tranche 03"), batch 2026-10-08 (Batch 7, dedicated agent) ---

    // Bit 0x2 of the SAME force-flag byte +0x1c98 forceFlags1c98 above
    // already models (Sec3.7's invulnerable 0x20/always-apply-player-damage
    // 0x200, plus the 3 further Sec7 bits) - CONFIRMED a new, 7th bit on
    // this one shared byte, not a separate field.
    static constexpr uint32_t kDontRegenHealthBit1c98 = 0x2u;

    // The dword at +0x19c0 ("HYPOTHESIS: a regen-restart deadline, disabled
    // while the flag is set"): true -> -1 (CONFIRMED the literal write,
    // real-world meaning HYPOTHESIS); false -> restamped to "now" via
    // 0x00d9e140(0). This project has no real clock anywhere (no per-frame
    // update loop) - "now" is this project's own CHOSEN stand-in, a plain
    // monotonically-incrementing counter (EngineState::nextMonotonicStamp(),
    // same "unconfirmed-but-plausible counter shape" precedent as
    // EngineState::nextAudioVoiceHandle()). 0 = never written. No in-scope
    // reader of this field exists (write-only in scope, same convention as
    // humanShieldHostageName above).
    int64_t regenRestartDeadlineStamp = 0;

    // character_clear_combat_move (Sec30.1): the word at +0x1358.
    // CONFIRMED sentinel 0xffff means "no combat move/state assigned"
    // (HYPOTHESIS real-world meaning, per spec's own text) - gates the
    // whole call to a no-op. OPEN until set (no CONFIRMED construction
    // value given).
    OpenValue<int32_t> combatMoveSlot1358{"character combat-move/state slot (+0x1358)",
                                          "spec-lua-api-behaviour.md Sec30.1"};
    // Test-observable: how many times the call proceeded past the +0x1358
    // gate above. The real downstream predicate (0x0095d720) and its two
    // branches' own callees (0x0095f250 / 0x00967f60 + the three
    // 0x00957ee0(character, id, 0.2f, 0, -1, 0) action-request calls with
    // ids 0x1d53/0x1d54/0x1d8c) are each individually OPEN (Sec30.1's own
    // text: "OPEN: the identities of ... and what the three ids name") and
    // have no Lua-visible return value either way (this native returns
    // none) - so this counter is the one real, checkable fact this project
    // can model without fabricating which branch ran or what it did.
    uint64_t combatMoveClearedCount = 0;

    // --- ai_suggest_action / ai_do_scripted_advance (spec-lua-api-
    // behaviour.md Sec30.4, "ranking tranche 03", the 70-entry CAE combat-
    // action table at 0x012e1c60), batch 2026-10-08 (Batch 7) -------------
    //
    // Per-character "combat action" record (character +0x438..+0x4ff,
    // Sec30.4's own field map). This project's own minimal stand-in: the
    // staged (+0x4a8) and committed (+0x450/+0x445) action INDEX are real,
    // CONFIRMED-structure values; the target is tracked by NAME (this
    // project has no coordinate/handle-pair system anywhere - same stated
    // simplification as CharacterState::scriptedRushTargetName above), not
    // the real 12-byte position/handle-pair. -1 = "never staged"/"never
    // committed" (this project's own sentinel; no CONFIRMED construction
    // value is given for either real field).
    int32_t caeStagedActionIndex = -1;
    std::string caeStagedTargetName;
    int32_t caeCommittedActionIndex = -1;
    std::string caeCommittedTargetName;
    // ai_do_scripted_advance's own no-target path: whether 0x0053a520's
    // advance-destination computation succeeds - OPEN per spec (no detail
    // on when it would fail beyond the character itself resolving).
    OpenValue<bool> caeAdvanceDestinationComputes{"0x0053a520 advance-destination computation succeeds (no-target path)",
                                                  "spec-lua-api-behaviour.md Sec30.4"};
    uint64_t caeSuggestCount = 0;       // ai_suggest_action staged successfully N times
    uint64_t caeAdvanceCommitCount = 0; // ai_do_scripted_advance committed action 18 N times

    // --- turn_to_do (spec-lua-api-behaviour.md Sec30.6), batch 2026-10-08
    // (Batch 7) -----------------------------------------------------------
    //
    // 0x00987410's own 3 internal branch outcomes, each individually OPEN
    // per spec (the callee itself was never independently dumped past this
    // document's own 3-sentence summary of its return shape): the refusal
    // predicate that releases the request id and returns true; the "busy"
    // flag that returns false; and the actual turn-start routine's own
    // result on the remaining path. Kept as 3 separate OpenValue<bool>
    // fields (not one combined field) since the spec's own text gives 3
    // textually distinct branches, same "do not invent a shared meaning
    // the text does not state" discipline as every other multi-branch OPEN
    // callee in this header.
    OpenValue<bool> turnRefusalPredicateFires{"0x00987410 refusal predicate (OPEN callee)",
                                              "spec-lua-api-behaviour.md Sec30.6"};
    OpenValue<bool> turnRequestBusy{"character turn-request busy flag (0x00987410)",
                                    "spec-lua-api-behaviour.md Sec30.6"};
    OpenValue<bool> turnStartRoutineSucceedsImmediately{"0x00987410's own turn-start routine result (OPEN callee)",
                                                        "spec-lua-api-behaviour.md Sec30.6"};

    // --- Batch 2026-10-08 (Sec5-9 fan-out, Batch 2): npc_leash_remove/
    // npc_leash_to_nav (Sec7.1/Sec7.5), set_attack_player_flag (Sec7.10),
    // set_animation_state (Sec7.11), ai_set_action_delay (Sec7.30),
    // human_gravity_enable (Sec7.32), set_script_animation_state (Sec7.17),
    // inv_item_equip (Sec9.19) - see
    // src/lua_spec_confirmed_stubs_b09_batch2_hud_audio.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning.
    // ---------------------------------------------------------------

    // npc_leash_remove (Sec7.1) / npc_leash_to_nav (Sec7.5): one shared
    // "leash anchor" record (object +0x30/+0x34/+0x38/+0x3c position,
    // +0x40 range/radius, +0x65 nav-mode byte). CONFIRMED gate shape:
    // single-gate, 0x008ae480-only (variant 3, Sec7's own front matter) -
    // the EXACT SAME first-stage predicate the double-gate (variant 1)
    // family's own gate 1 is (Sec7's front matter: "0x008ae480 itself is
    // not an independent predicate - it does its own object-resolution/
    // type-check first, then calls the SAME underlying function,
    // 0x00884760"), already CONFIRMED true in single player (Sec3.7) -
    // reusing forceFlagGatePasses above rather than adding a third,
    // near-duplicate always-true gate field for the same real predicate.
    // The radius reset value on a clear (npc_leash_remove, or npc_leash_
    // to_nav's own negative-radius branch) is CONFIRMED -1.0: the literal
    // at 0x012a2d54 was independently byte-confirmed elsewhere in this
    // document (ranks-451-550 tranche's own cross-reference: "the
    // already-established constant -1.0 (byte-confirmed at 0x012a2d54,
    // matching this document's own existing citation of that address)").
    // The position reset's own real payload (seeded from 0x01115d60) and
    // npc_leash_to_nav's own two "+0x28/+0x2c" fixed defaults
    // (0x01115d50/0x01115d54) are never independently given a numeric
    // value anywhere in this document and have no in-scope Lua-visible
    // reader either way - not modeled; a clear instead reverts position
    // to OPEN (honestly "an engine default this project does not have the
    // real value for", not an invented zero vector).
    OpenValue<double> leashRadius{"character leash-anchor range/radius (+0x40)", "spec-lua-api-behaviour.md Sec7.1/Sec7.5"};
    OpenValue<WorldPosition> leashPosition{"character leash-anchor position (+0x30/+0x34/+0x38/+0x3c)",
                                            "spec-lua-api-behaviour.md Sec7.1/Sec7.5"};
    bool leashNavMode = false; // npc_leash_to_nav's own arg 4 (+0x65 nav byte), CHOSEN default false

    // set_attack_player_flag (Sec7.10): genuinely splits across two flag
    // locations - this one (+0xac bit 0x04) is written UNCONDITIONALLY,
    // with no gate and no replication of any kind (CONFIRMED).
    OpenValue<bool> attackPlayerBitAc{"character byte +0xac bit 0x04 (unconditional, unreplicated)",
                                       "spec-lua-api-behaviour.md Sec7.10"};
    // The vtable +0x68 predicate gating whether the SECOND flag location
    // (+9 bit 0x01, via the separate double-gate setter 0x004e0100) is
    // ever reached at all - OPEN (Sec7.10: "the vtable +0x68 predicate's
    // real meaning" is explicitly not identified this pass). A test can
    // .set() it to exercise the gated branch; an untouched character
    // refuses only THIS one sub-step, after the unconditional +0xac write
    // above has already applied - matching Sec7.10's own description that
    // the +0xac write happens "unconditionally and with no gate check
    // anywhere in this function" BEFORE the vtable +0x68 predicate is even
    // evaluated.
    OpenValue<bool> attackPlayerSecondaryGatePredicate{"character vtable +0x68 predicate (unidentified)",
                                                        "spec-lua-api-behaviour.md Sec7.10"};
    OpenValue<bool> attackPlayerOnSightBit9{"character byte +9 bit 0x01 (ai_force_flagsattack_player_on_sight)",
                                            "spec-lua-api-behaviour.md Sec7.10"};

    // set_animation_state (Sec7.11): the character's own per-object
    // state-table entry for whichever state id the shared name table
    // (0x004bf810) produced. CHOSEN simplification (Sec7.11 describes TWO
    // real lookups this project has no data for either: name->id via
    // 0x004bf810, then id->per-object-entry via 0x004b1630): this project
    // collapses both into ONE test-registerable name->id map
    // (EngineState::lookupAnimationStateId()/registerAnimationStateIdFor
    // Testing() in engine_state.h), where an unregistered name reads back the SAME -1
    // sentinel the real per-object lookup's own documented "not found"
    // case already uses (Sec7.11: "If the state id doesn't resolve to an
    // entry (-1): clears the override") - so "name never registered" and
    // "name resolves to an id but this character has no entry for it"
    // collapse to the one observable outcome this project can honestly
    // support. Writes land on animationStateOverride above - the SAME
    // physical field (+0x1cc4) clear_animation_state (Sec7.3) already
    // uses, reused directly, not duplicated (Sec7.3/Sec7.11 share one
    // setter, 0x0094b070).

    // ai_set_action_delay (Sec7.30): "ALL" branch (0x004f4b30) - this
    // project has no real per-character action list, so only the raw
    // delay most recently applied-to-everything is tracked, plus a count
    // (test-observable, not a real per-action write).
    double allActionDelay = 0.0;
    uint64_t allActionDelayApplyCount = 0;
    // Non-"ALL" branch: delay-by-action-index (0x004f4ae0), keyed by the
    // CONFIRMED 70-entry name-table index (EngineState::lookupActionNameIndex()
    // in engine_state.h) - real per-action fidelity, bounded to the CONFIRMED [0,70)
    // range by construction (the lookup itself never returns an
    // out-of-range index - see that method's own doc comment).
    std::unordered_map<int32_t, double> actionDelayByIndex;
    // The CONFIRMED diagnostic path (Sec7.30: "prints all 70 valid action
    // names to the console ... a genuine, confirmed diagnostic path, not a
    // silent no-op") - this project does not reproduce actual console
    // output (no in-scope consumer reads real stdout), only that the path
    // was taken, honestly, as a count.
    uint64_t actionDelayUnrecognizedNameCount = 0;

    // human_gravity_enable (Sec7.32): the ONE flagged name confirmed to
    // skip the either/or gate idiom entirely - always writes AND always
    // (the Lua wrapper hardcodes its own 3rd parameter to 1) opens a
    // record, never a choice between the two. +0x150 bit 0x02 of the
    // physics/ragdoll sub-object at the character's own +0xcc0 field, only
    // written "if that pointer is non-null" per Sec7.32's own text - this
    // project has no evidence that pointer is ever null for an ordinary,
    // already-resolved character (a core physics/ragdoll sub-object,
    // unlike the script-NPC-specific +0x1da8 sub-object set_script_
    // animation_state below gates on), so a resolved character is always
    // treated as having one - a JUDGMENT CALL, stated plainly, not
    // independently confirmed either way.
    OpenValue<bool> gravityEnabled{"character physics sub-object +0x150 bit 0x02 (gravity enable)",
                                   "spec-lua-api-behaviour.md Sec7.32"};

    // set_script_animation_state (Sec7.17): the per-character sub-object
    // at +0x1da8 (null for an ordinary character - CONFIRMED zeroed by the
    // ordinary human constructor 0x009d4700, Sec34.1 - non-null only for a
    // character bound to a script NPC) - this project already models
    // exactly that distinction via scriptNpcBound above
    // (markScriptNpcBoundForTesting()); a genuine finding of this batch,
    // not a new field: +0x1da8's own real nullness IS the same "is this
    // character a bound script NPC" question scriptNpcBound already
    // stands in for (Sec7.17 cross-referenced against Sec34.3's own
    // "character +0x1da8 ... bound to a script NPC object" text, and the
    // constructor note that "0x009d4700 also zeroes +0x1da8"). The sub-
    // object's own +0x174 field (write-only in this project's scope - no
    // in-scope reader; check_script_animation_state, Sec25.24, is the real
    // read-side counterpart but is OUT of this batch's scope) is tracked
    // as a plain int32, not an OpenValue, since nothing ever needs to
    // refuse reading it.
    int32_t scriptAnimationStateId = 0; // write-only; -1 = "no override" (CONFIRMED default-absent shape, Sec7.17)

    // inv_item_equip (Sec9.19): no real fixed weapon/item-definition
    // catalog exists in this project's scope (inventing one would be
    // inventing data, per the orchestrator's own HARD classification of
    // this exact name) - so only the raw request is recorded, honestly,
    // same MinimapIconRecord/ObjectIndicatorRecord-style convention this
    // whole file already uses for a routine this project has no real
    // internals for. "" = no item name given (the literal-selector-0
    // path).
    std::string equipItemRequest;
    uint64_t equipItemRequestCount = 0;

    // --- Batch 5 (ranks 251-350, Sec15/Sec16), 2026-10-08 - HUD-slot/
    // customization/pause-menu cluster. See
    // lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning;
    // these fields are deliberately plain (not OpenValue) wherever no
    // in-scope Lua function ever reads them back - this project's own
    // "write-only field, CHOSEN default, no refusal machinery needed"
    // convention already established throughout this header (e.g.
    // chaseTargetName (vehicle_state.h)/forcedTargetHandle above). ---

    // character_set_damage_multiplier (Sec15.1): CHOSEN default 1.0 ("no
    // override" - the engine's own neutral multiplier), since no in-scope
    // Lua function reads this back; 0x0094a960's own internal write was not
    // independently decompiled (Sec15.1's own OPEN item).
    double damageMultiplier = 1.0;

    // set_always_cower_flag (Sec15.4): per-character sub-object at +0x2b0,
    // CHOSEN default false (no stated construction value; no in-scope
    // reader). aiEvent26QueuedCount counts the corrected (Sec44.6) AI-
    // event-0x26 queue call that fires only on the true-setting edge.
    bool alwaysCowerFlag = false;
    uint64_t aiEvent26QueuedCount = 0;

    // on_weapon_pickup (Sec15.8): character hook slot 6 on the SAME
    // generic/#PLAYER#-path +0x1f00 array this document's own slot
    // catalogue already tracks (onDeathCallback=0, on_take_damage=4/6,
    // on_attack_performed=8/13, on_vehicle_destroyed fallback=0x12,
    // on_revived=2) - slot 6 here does NOT collide with on_take_damage's
    // DIFFERENT-base slot 6 (Sec15.8's own cross-reference, +0xf8 vs
    // +0x1f00). Same empty-string-clears-slot convention. Default empty.
    std::string onWeaponPickupCallback;

    // character_is_ragdolled (Sec15.14): object +0xcd2 state-enum byte.
    // CONFIRMED exclusion test (!=0 && !=6), not a positive equality - the
    // exact "ragdolled" value(s) are OPEN (Sec15.14's own flagged item).
    //
    // CONFIRMED default 0 at construction (applied by
    // EngineState::getOrCreateCharacter(), lua_engine_state.cpp - same
    // "pre-seed a newly-CONFIRMED default instead of leaving an OpenValue
    // OPEN" convention as CharacterState::ignoreAI/stateEnum's own
    // construction-time seeds): spec-player-runtime.md Sec5.4.2 names
    // +0xcd2 directly as "the human's physics-representation byte: 0 =
    // character proxy" - the ordinary, ever-present, non-ragdolled state;
    // Sec5.4.4's closed form for the player on foot independently
    // corroborates it ("condition row 0, +0xcd2 = 0"). Sec7.4a's full
    // 9-step vehicle-enter eligibility gate trace (step 4, "refuse if
    // ragdolling, +0xcd2 ∉ {0,6}") and Sec15.3a's end-to-end damage/death/
    // ragdoll hand-off trace (ragdoll only ever activates via the real
    // "activate ragdoll physics" primitive `0x009a5520` on the death path)
    // together confirm this field only transitions away from 0 through an
    // explicit ragdoll/death event - there is no other, more-common
    // starting value a freshly constructed, not-yet-ragdolled character
    // could hold. Values 3/6 (ragdoll/recovery, Sec5.4.2) and 1/4 (NPC
    // rig-test pass-through, Sec5.4.1 step 2) remain otherwise OPEN/
    // HYPOTHESIS exactly as before - only the default changed. Remains a
    // plain OpenValue, not a bare int: `.set()`/`.forget()` stay available,
    // so a test exercising a ragdolling/recovering character still
    // overrides this explicitly, exactly as before this pre-seed (matching
    // CharacterState::stateEnum's identical OpenValue precedent).
    OpenValue<int32_t> ragdollStateEnumCd2{"character ragdoll/state-enum byte (+0xcd2)",
                                           "spec-lua-api-behaviour.md Sec15.14"};

    // follower_remain_in_car (Sec15.13): object byte +0x1c9c bit 0x100
    // ("cannot exit vehicles" - CONFIRMED by the setter's own literal
    // debug-sync strings "human"/"m_force_flagscannot_exit_vehicles").
    // Gated by the SAME forceFlagGatePasses double-gate above (CONFIRMED
    // true in single player, Sec3.7/Sec6.22's cross-reference covers this
    // gate pair generically - reused here by direct analogy, same
    // precedent as independentFollower/dontAttackMeOnSight above).
    OpenBits32 flags1c9c{"character force-flags byte +0x1c9c (cannot_exit_vehicles 0x100)",
                         "spec-lua-api-behaviour.md Sec15.13"};
    static constexpr uint32_t kCannotExitVehiclesBit1c9c = 0x100u;

    // set_player_undercover_team (Sec15.20): object +0x27f0 byte, the
    // team code resolved via the SAME 0x0094cc60 hash/lookup
    // lookupNotorietyTrackCode() already models (Sec3.6) - a genuine
    // cross-function finding (Sec15.20's own text), reused rather than
    // re-derived. CHOSEN default 9 ("unknown team" - the SAME CONFIRMED
    // not-found sentinel both call sites share), gated by forceFlagGatePasses
    // the same way as follower_remain_in_car above.
    uint8_t undercoverTeamCode = 9;

    // player_actions_enable (Sec15.21): CHOSEN simplification - neither of
    // the real body's two branches (network-replicate on "not yet
    // enabled"; a local-only setter 0x009e30d0 under bit 0x20 at +0x1d99
    // when already enabled) has an in-scope reader, so this project always
    // takes the documented DEFAULT-case branch (a fresh/not-yet-enabled
    // character, the common real-world case) rather than refusing on the
    // undetermined 0x008addb0 "already enabled" gate. actionsEnableBit20
    // is the real, newly-modeled bit this cluster's own text identifies at
    // the SAME +0x1d99 byte controlsDisabled/movementDisabled above use
    // (0x08/0x10) - "Two further bits of the same byte are documented
    // elsewhere (0x20, Sec27.x...) and not modelled here" - now modeled,
    // write-only (no in-scope setter touches it either; exposed for a
    // future Sec27.x pass).
    //
    // BIT-SENSE POLARITY (resolved 2026-10-08): despite this field's own
    // "Enable" name, the bit is actually a *disabled*-sense bit, the SAME
    // family as controlsDisabled/movementDisabled above (set=disabled,
    // clear=enabled) - NOT set=enabled as the name suggests. CONFIRMED by
    // spec-player-runtime.md Sec2.10 "Question B," which names all three
    // bits together: "the player's `+0x1d99` bits (`0x08`/`0x10`/`0x20` -
    // controls/movement/actions disabled) ... all clear (0) - enabled" and
    // "controls, movement and actions all enabled" at spawn. See
    // stub_player_actions_disable's own doc comment (lua_spec_confirmed_
    // stubs_b21_wave2_sec17_18.cpp, Sec18.27) for the full citation chain.
    // Field kept under its original name (ripple-minimizing) but treat
    // true == "actions disabled", false == "actions enabled".
    OpenValue<bool> actionsEnableBit20_1d99{"character byte +0x1d99 bit 0x20, actions-disabled sense (Sec27.x, cross-ref Sec15.21/spec-player-runtime.md Sec2.10) - true=disabled, false=enabled",
                                            "spec-lua-api-behaviour.md Sec15.21"};
    uint64_t playerActionsEnableCount = 0;

    // npc_leash_to_object (Sec15.22): this project has no real
    // position/coordinate data for any named object (the SAME stated,
    // project-wide gap attentionSourceObjectName above already notes) -
    // records only the resolved anchor-object's own name, the raw radius
    // and arg4, matching the SAME convention. A negative radius takes the
    // documented early-return "clear leash" path instead (0x004e3bf0, no
    // further Lua-observable effect beyond the fact that it ran).
    std::string npcLeashAnchorName; // "" = never set, or cleared
    double npcLeashRadius = 0.0;
    bool npcLeashArg4 = false;
    uint64_t npcLeashSetCount = 0;
    uint64_t npcLeashClearCount = 0;

    // character_parachute_detach (Sec15.28): CHOSEN simplification - the
    // real body's local-apply branch (clearing bit 0x04000000 at +0xe8,
    // with a delayed re-check against an unidentified +0x1c linked object,
    // Sec15.28's own OPEN item) has no in-scope reader, and the branch
    // condition itself (0x008addb0, "already in a no-op state") is not
    // independently given a construction default - this project always
    // takes the documented replicate-path branch instead (the common
    // real-world case: a character whose parachute is actually deployed),
    // recorded only as a call count (the replicate call itself is this
    // project's established no-op stand-in, EngineState::replicateStateChange).
    uint64_t parachuteDetachCount = 0;

    // customization: see PcrCustomizationState (pcr_customization_state.h)
    // for pcr_change_hair/pcr_change_composite/
    // pcr_get_player_preset/pcr_set_identity's own shared state - that
    // state is modeled as ONE EngineState-level "current player" instance
    // (PcrCustomizationState via EngineState::pcrCustomization()), not a
    // per-CharacterState field, since this project has only ever modeled
    // one implicit local player (no split-screen/second-local-player
    // customization support anywhere in scope) - see that struct's own
    // doc comment for the full cross-batch-coordination note.
};

} // namespace sr3luahost
