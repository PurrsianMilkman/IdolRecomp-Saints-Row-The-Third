// CharacterStateSpawnHealthAi: one part of CharacterState (character_state.h).
//
// Split out of character_state.h (Team C refactor, 2026-10-17); every line
// below moved verbatim. The doc-comment conventions of character_state.h's
// own top note apply ("this header" = the engine-state header set).
#pragma once

#include "sr3luahost/character_state_records.h"

namespace sr3luahost {

// Part of CharacterState: spawn and ignore-AI state, hit points and death, AI targeting, the vehicle-group orders.
struct CharacterStateSpawnHealthAi {
    // set_ignore_ai_flag (Sec3.4) / character spawn state (Sec34.1/Sec34.3).
    // Real location: bit mask 0x02 of the byte at object +0x2bc - CONFIRMED
    // (2026-10-02, character spawn-state investigation) to be the exact
    // same byte Sec3.4's own setter `0x004e2050` writes: that helper runs
    // with `this` = character `+0x2b0` (the per-character AI-data
    // sub-object constructed in place by `0x004e46c0`) and writes its own
    // `+0xc`, i.e. `0x2b0+0xc` = `0x2bc` - so this project's existing
    // "+0x2bc" modeling was already the right byte; Sec3.4's former OPEN
    // note (same `this`? which bit?) is resolved in place, nothing to
    // correct here. Default false ("not ignoring AI") is now a CONFIRMED
    // engine default, not just this project's own choice: cleared by the
    // sub-object constructor `0x004dde30` and re-cleared by
    // `0x004e5a50(character, 1)` inside the no-bound-script-NPC default
    // path (Sec34.1 step 3) - applied at construction by
    // EngineState::getOrCreateCharacter() in engine_state.h. The ONLY way this starts
    // true is a bound script NPC whose own placement-record
    // `script_npc_flags` carries `"ignore_ai"` (Sec34.3).
    //
    // CORRECTED 2026-10-08 (sibling fix to CharacterState::scriptNpcHp's own
    // same-day correction, same root cause): this used to say the
    // `script_npc_flags` override question "lives in the still-held
    // `.czn_pc` zone-placement interior," so EngineState::
    // loadZoneObjectsSection() unconditionally forgot this field back to
    // OPEN for EVERY script_npc-classified character, override or not. The
    // `.czn_pc` hold was lifted 2026-10-07 and the real resolver exists -
    // once a character's own record is resolved, whether it carries
    // `script_npc_flags` containing `ignore_ai` is a CONFIRMED, fully-
    // decidable fact (Sec11.7: true for only 425 of 6,702 flags-bearing
    // records), not an open question - loadZoneObjectsSection() now sets
    // this field for real (true/false) instead of forgetting it. Only a
    // character with NO resolved zone record at all (markScriptNpcBoundFor
    // Testing(), the test-only "we don't even know if this name is a script
    // NPC" stand-in) still leaves this field OPEN.
    OpenValue<bool> ignoreAI{"character ignore-AI flag (+0x2bc)", "spec-lua-api-behaviour.md Sec3.4/Sec34.3"};

    // set_ignore_ai_flag's own conditional side effect (Sec3.4): "forces
    // the character's action/animation-override state to a fixed id
    // (0x19) if ... state-enum field (+0x16d4) reads 3 ... or clears the
    // override to 0 if the character currently carries a nonzero
    // attacker/threat reference." The STRUCTURE (state-enum gated
    // override write) is CONFIRMED; the real-world MEANING of id 0x19 is
    // HIGH CONFIDENCE only (spec's own words). Default -1 ("no override"),
    // reusing the same "-1 means no override" reading Sec7.3 independently
    // confirms for the sibling animation-override field elsewhere in this
    // spec (not claimed to be the SAME physical field - this project's own
    // field, scoped only to set_ignore_ai_flag).
    OpenValue<int32_t> actionOverrideId{"character action/animation override", "spec-lua-api-behaviour.md Sec3.4"};

    // Read-only-by-us gate for the conditional branch above: the
    // "in a vehicle" state-enum value is 3 per Sec3.4/Sec3.10's own cross-
    // checked text. No in-scope Lua function ever WRITES this field (set_
    // ignore_ai_flag/Sec3.4 only reads it, same as the field's name
    // implies) - exposed only so a test can set up the "newly enabling
    // ignore-AI while in a vehicle" condition directly. UPDATE 2026-10-07:
    // character_is_in_vehicle (Sec3.10, cleared for implementation this
    // date) is now a second in-scope READER of this exact field - its own
    // predicate's unconditional first test, per that function's own doc
    // comment (lua_spec_confirmed_stubs.cpp).
    //
    // UPDATE 2026-10-08 (W2-1 post-merge review fix): no longer true that
    // this is never written. spec-player-runtime.md Sec7.4 step 11/Sec7.8
    // and Sec6.3.1 step 9/Sec6.6.1 step 1 (both independently re-confirmed
    // this pass) establish that the enter submitter 0x00af22a0 and the exit
    // request 0x00af3cd0 write +0x16d4 = 1/2 SYNCHRONOUSLY, inside their own
    // call, with zero world-tick dependency - not from a later-ticked
    // on-entry handler as this project originally (and wrongly) assumed. The
    // writers now in scope: stub_vehicle_enter_do's instant/named-seat
    // paths, stub_vehicle_enter_guardian_angel, and stub_vehicle_enter_
    // group_do's instant per-member path (all gated on vehicleEnterSubmit
    // Accepted/vehicleEnterSubmitAcceptedForced below, so a write fires only
    // once that OPEN fact is actually set true) write 1; stub_vehicle_exit_
    // do's and stub_vehicle_exit_guardian_angel's seated branches write 2.
    // Release (0) is still never written by any in-scope native - no
    // in-scope trigger reaches the world-tick-only release path (Sec7.8's
    // 0x00b0880b) - see lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.
    // cpp's own top comment for the full account.
    //
    // CORRECTION 2026-10-08 (mission-drive verification pass, missions
    // m05/m21/m09): the "Default 0 ('not in a vehicle')" line this comment
    // used to carry here was never actually true of the CODE (no
    // constructor/getOrCreateCharacter() path ever calls stateEnum.set(0) -
    // checked by grepping every stateEnum.set()/.forget() call site in this
    // tree; the only writers are the five enter/exit sites named above,
    // which write 1/2 only). It was aspirational prose, not a description of
    // what this field actually does, and it had gone stale against the
    // field's own declaration below (plain OpenValue<int32_t>, OPEN until
    // set() - open_state.h's own banner: "[g]iving such a global a default
    // here would be inventing a value"). Investigated and left AS OPEN,
    // deliberately, rather than quietly "fixing" the comment by adding the
    // missing .set(0) call: spec-lua-api-behaviour.md Sec3.10's own
    // 2026-10-08 "Full +0x16d4 enum cross-reference" addendum (same date as
    // this investigation) independently looked for a construction-time
    // default and found a CANDIDATE - "a 5th writer inside the character
    // constructor (0x00954be8) zeroes this field as part of a wider
    // neighborhood-clear" - but tagged that candidate itself "(OPEN, low
    // priority, adds no new enum value)", i.e. not pinned byte-for-byte to
    // +0x16d4 specifically (only "probably in range" of a wider clear). This
    // is exactly why set_ignore_ai_flag's own stateEnum.get() (Sec3.4,
    // lua_spec_confirmed_stubs_b01_initial.cpp) throws OpenStateError on
    // mission m05 (verified: it is the ONLY in-scope reader of this
    // field actually exercised by the full mission set this pass -
    // character_is_in_vehicle/vehicle_exit_group_do/
    // vehicle_exit_group_check_done are never called by any mission script
    // in the current archive cache) - the character being handed
    // ignore-AI has never entered/exited a vehicle in this run, so none of
    // the 5 confirmed writers has touched its +0x16d4 yet. TEAM A SPEC-SYNC
    // QUESTION (not resolved by Team B - this project does not disassemble):
    // can the 0x00954be8 neighborhood-clear be pinned down to confirm (or
    // refute) that it specifically covers +0x16d4, not just a nearby range?
    // That single fact is the only thing standing between this OpenValue and
    // a real, non-invented spawn default of 0.
    //
    // RE-MEASURED 2026-10-08 (same day, after merging main's real
    // .czn_pc->Lua named-object resolver, commit 7fdb8dd/db54f26): this note
    // originally named missions m05/m21/m09 as the +0x16d4 bucket, found
    // under the diagnostic --probe-unresolved-names fallback (each one's
    // REAL first blocker, at the time, was an earlier named-object-
    // resolution OPEN gate - m05/m05_heli_pierce, m21/M21_Killbane,
    // m09/Pierce - with +0x16d4 only visible as the hypothetical "next"
    // blocker once that earlier gate was synthetically bypassed). Once the
    // real resolver landed and those exact three names started genuinely
    // resolving (confirmed: 16,894 named objects ingested from 861 real
    // Objects sections across 2,971 .czn_pc files, 10/10 archives - see
    // EngineState::loadZoneObjectsSection()), a fresh NON-probe mission-drive
    // re-run shows the three missions did NOT converge on the same blocker:
    //   - m05: DOES now reach +0x16d4 for real, with no probe flag needed -
    //     this bucket's original diagnosis is confirmed accurate for m05.
    //   - m21: does NOT reach +0x16d4 any more. 'M21_Killbane' resolving for
    //     real means the real zone record now classifies that character
    //     (correctly) as a `script_npc` (crc32l match, Sec11's own class
    //     test), which per THIS header's own ignoreAI comment above marks it
    //     scriptNpcBound - reverting ignoreAI's CONFIRMED spawn default back
    //     to OPEN. set_ignore_ai_flag now refuses one field earlier (the
    //     ignoreAI read itself, "character ignore-AI flag (+0x2bc)") and
    //     never reaches the stateEnum branch at all. Not a regression - a
    //     MORE correct refusal than before, on exactly the data this
    //     project's own scriptNpcBound design was built to honor.
    //   - m09: does NOT reach +0x16d4 either. 'Pierce' now resolves, but
    //     m09_start's own next reference, 'Paparazzi_WM', does not (still a
    //     genuine named-object-resolution OPEN gate) - m09 never reaches
    //     set_ignore_ai_flag's call at all in this run.
    // So the +0x16d4 bucket this comment documents is real and still
    // unresolved, but as of this re-measurement it is a ONE-mission bucket
    // (m05 only), not three - m21/m09's blockers moved to different fields
    // entirely. The TEAM A SPEC-SYNC QUESTION above is unchanged and still
    // the one fact that would close it.
    //
    // RESOLVED 2026-10-08 (spec-player-runtime.md, same-day follow-up): the
    // TEAM A SPEC-SYNC QUESTION above is answered. Constructor `0x00954340`
    // zeroes `+0x16d4` unconditionally for every newly-constructed character
    // (`0x00954be8`, `edi` traced to a single `xor edi,edi` with no
    // redefinition before the write) - a real, CONFIRMED spawn default of 0,
    // not a guess. `EngineState::getOrCreateCharacter()` now sets it
    // (`c.stateEnum.set(0)`), closing m05's own remaining bucket: a
    // first-ever `set_ignore_ai_flag` call on an untouched character now
    // reads a real 0 ("not in a vehicle") instead of throwing OpenStateError.
    // m09's own blocker ('Paparazzi_WM' failing to resolve as a named
    // object) is separately explained, not by this fix: spec-zone-data-
    // format.md's wave-3 agent confirms `Paparazzi_WM`/`STAG_WF` are NOT
    // zone objects at all - they are 2D Wwise audio-persona names, a
    // disjoint namespace from the named-object resolver this project's
    // zone_object_resolver.h implements. Routing them through the persona/
    // audio path instead (not this header's own concern) is the real fix
    // for m09, separate from this field.
    OpenValue<int32_t> stateEnum{"character state enum (+0x16d4)", "spec-lua-api-behaviour.md Sec3.4/Sec3.10"};

    // REMOVED 2026-10-09 (spec-lua-api-behaviour.md Sec3.4's own in-place
    // correction, routed from Team A, mission-blocking m07): this field
    // used to model the other half of Sec3.4's conditional as "clears the
    // override to 0 if the character currently carries a nonzero
    // attacker/threat reference" - that label was a documentation mistake.
    // The real condition (0x00a5e333-0x00a5e343) tests the AI mode byte
    // +0x501 (nonzero = "not Idle") ANDed with the AI sub-state byte +0x500
    // (!= 9); both are CharacterState::stateCodeByte501/aiSubStateByte500
    // below (REUSED, not duplicated), which `set_ignore_ai_flag`'s own stub
    // now reads instead - see that stub's own doc comment for the exact
    // gate. This field was never actually seeded anywhere (no
    // EngineState::getOrCreateCharacter() .set() call existed for it,
    // despite its own stale comment claiming a "default 0" - which is
    // exactly why it surfaced as a mission-blocking OPEN-state gap for m07
    // in the first place), so removing it drops no modelled behavior.
    //
    // A genuine attacker/threat-style handle DOES exist elsewhere, at
    // character offset +0x18f0/+0x18f4 (an 8-byte handle pair) - see
    // CharacterState::reviverHandle below for that field. It is NOT the
    // same thing this (former) field tried to model, and is not read by
    // set_ignore_ai_flag at all.

    // get_max_hit_points (Sec7.12) / set_current_hit_points (Sec7.31) /
    // character spawn state (Sec34.1/Sec34.2): CONFIRMED, cross-checked to
    // be the SAME integer field at object +0x1cac ("stored as a plain
    // integer, converted to float on read" - Sec7.12's own text), kept as a
    // plain int32 to honor that detail precisely, converted to a double
    // only at Lua-push time. NOTE: object +0x1cb0 is a SEPARATE field, the
    // max-HP BONUS (`"max_hit_point_bonus"`, own setter `0x0094d380`,
    // Sec13.20/Sec34.2 correction) - zero on every spawn path, never a base
    // and never read/written anywhere in this project; nothing here treats
    // it as +0x1cac's value.
    //
    // CONFIRMED spawn default (Sec34.2): round(M * Hit_Points), where
    // Hit_Points is the selected spawn_info_ranks.xtbl <Rank> record's
    // Hit_Points field (sr3tables_progression::SpawnRank::hitPoints) and M
    // is 1.0 for ordinary classes (the one documented exception - a
    // per-class multiplier gated on class test 0x00853b30(character, 0x22)
    // - has a HYPOTHESIS-only, never-opened source, so it is NOT
    // implemented; out of scope callers stay on M=1.0, a known residual
    // gap, not invented). Resolving WHICH rank record applies to a given
    // bare Lua name (character_definitions.xtbl preset + the OPEN
    // 0x0096e830 level-index choice) is this project's own unmodeled gap -
    // no Lua-visible spawn function in scope performs that resolution - so
    // this field stays genuinely unknown for a name with no Hit_Points
    // supplied the same as before; EngineState::applyCharacterSpawnDefaults()
    // applies the formula once a caller (a future real spawn integration, or
    // a test) supplies a resolved Hit_Points value. A script-NPC-bound
    // character's own `script_npc_hp` override (also zone-held,
    // Sec34.2/Sec34.4) is the same unavailable-data situation
    // markScriptNpcBoundForTesting() represents for ignoreAI above - see
    // scriptNpcBound below.
    //
    // UPDATE 2026-10-07 (Sec60.4's own closure, relayed via the orchestrator
    // as probe_blockers_ranked_20261007.md item 2, mission `m18`): Sec60.4
    // now states the real default plainly - "the value itself is confirmed
    // to be genuinely random per spawn for an ordinary character, pinned
    // only by class/rank type, with a script-NPC override still parked zone
    // data." Refusing forever for an ORDINARY (non-scriptNpcBound) character
    // was therefore a Team B implementation-lag bug, not a missing spec -
    // EngineState::drawOrGetMaxHitPoints() (lua_engine_state.cpp) now closes
    // this field on first reference for that case, exactly as a real spawn
    // would (random once, stable after) - see its own doc comment for the
    // draw mechanism and what it does and does not model. This field stays
    // genuinely OPEN only for a character already marked scriptNpcBound.
    OpenValue<int32_t> maxHitPoints{"character max hit points (+0x1cac)", "spec-lua-api-behaviour.md Sec7.12/Sec7.31/Sec34.2"};

    // CHOSEN, host-only bookkeeping (not an engine value, no spec address):
    // set by markScriptNpcBoundForTesting() so drawOrGetMaxHitPoints() (and
    // any future equivalent for ignoreAI) knows this character's +0x1cac
    // override question is genuinely zone data this project does not have,
    // and must stay OPEN rather than being drawn around.
    bool scriptNpcBound = false;

    // UPDATE 2026-10-08 (zone-data fast-follow, orchestrator follow-up to
    // commit 50bdb29): this character's own zone-placement record's
    // `script_npc_hp` property value. sr3zone::ScriptNpcHp() (zone_objects.h,
    // Sec11.7's own "CLOSED with real-data proof" reader) decodes this value
    // from a SPECIFIC ObjectRecord; EngineState::loadZoneObjectsSection()
    // (lua_engine_state.cpp) now routes it for real, for every character
    // whose name resolves against the real, ingested `.czn_pc` Objects data
    // (the by-name resolver this comment used to say did not exist - it now
    // does, see loadZoneObjectsSection()'s own doc comment history). Only a
    // character with NO resolved zone record at all (markScriptNpcBoundFor
    // Testing(), a test-only stand-in for "we don't even know if this name
    // is a script NPC") still follows the "mechanism confirmed, no producer
    // wires it up" convention and leaves this field OPEN.
    //
    // Sec34.2's own CONFIRMED precedence, once this value IS known:
    // ">= 1" -> drawOrGetMaxHitPoints() uses it for maxHitPoints verbatim,
    // instead of the table formula ("0x009d7480 uses that value for
    // +0x1cac verbatim"). "< 1" -> the SAME section's own text ("a script
    // NPC's own constructor defaults this field to -1 ['use the table'];
    // commando_spawn-produced script NPCs never set script_npc_hp, so they
    // always fall back to the table") - folded into the ordinary table-draw
    // path, same as a non-scriptNpcBound character.
    //
    // CORRECTED 2026-10-08 (mission-blockers investigation, root cause of
    // probe_blockers_ranked_20261008_post_resolver_merge.md item 1, 26/49
    // missions): this comment used to say "!known() ... stays OPEN ... the
    // real remaining gap is the name<->record resolver" - that was true
    // only back when no resolver existed. Once a character's own record IS
    // resolved (loadZoneObjectsSection() found a real, matching script_npc
    // object), the ABSENCE of a script_npc_hp property on that specific
    // record is itself a CONFIRMED fact (Sec34.2's own "-1, use the table"
    // constructor default), not an open question - loadZoneObjectsSection()
    // now sets this field to -1 in that case instead of forgetting it, so
    // !known() today means ONLY "no resolved zone record at all" (the
    // markScriptNpcBoundForTesting() stand-in above), never "resolved, but
    // the record's own override property is absent."
    OpenValue<int32_t> scriptNpcHp{"script NPC's own record script_npc_hp property",
                                   "spec-zone-data-format.md Sec11.7; spec-lua-api-behaviour.md Sec34.2/Sec34.4"};

    // set_current_hit_points (Sec7.31): object +0x1cb8, clamped into
    // [0, maxHitPoints] on write (CONFIRMED). CONFIRMED spawn default
    // (Sec34.1 step 2): starts equal to the newly-computed max ("a
    // character spawns at full health") - applied together with
    // maxHitPoints by EngineState::applyCharacterSpawnDefaults().
    OpenValue<int32_t> currentHitPoints{"character current hit points (+0x1cb8)", "spec-lua-api-behaviour.md Sec7.31/Sec34.1"};

    // set_current_hit_points' own HIGH-CONFIDENCE-tier consequence (Sec7.31:
    // "if the new value is <= 0 and two further predicates ... are both
    // false ... calls 0x0096f8c0 - HIGH CONFIDENCE this is, or directly
    // feeds, the same death-transition pipeline character_kill's own
    // finalizer uses"). character_kill/character_is_dead are both OUT of
    // this task's 12-function scope, so this field is this project's own
    // minimal, explicitly-labelled HIGH CONFIDENCE stand-in for "hit points
    // reached zero" - never claimed CONFIRMED, never read by any other
    // in-scope function. Default false.
    bool isDeadHighConfidence = false;

    // on_take_damage (Sec3.13): the polymorphic hook-registration trio's
    // own real per-object-kind hook-slot arrays (character/vehicle/
    // "unidentified 4th kind", Sec3.18) are NOT modeled distinctly here -
    // this registry only ever models "character"-shaped objects (this
    // header's own top note) - so every on_take_damage registration is
    // recorded on this one field regardless of which of the spec's own 4
    // resolve branches a real engine call would have taken. Stated
    // simplification, not a claim that vehicle/4th-kind names route
    // through this same field on the real engine. Default empty ("no
    // callback registered").
    std::string onTakeDamageCallback;

    // on_damage (Sec7.4): a fourth member of the on_* named-hook-
    // registration family (on_death/on_take_damage/on_attack_performed/
    // on_vehicle_destroyed, Sec3.2/3.3/3.12/3.13). Same single-dedicated-
    // field-per-hook-kind simplification as onTakeDamageCallback above -
    // the spec's own 3 resolve-order branches (generic/#PLAYER#, plain
    // character, vehicle) all land on this one field when the target
    // resolves as a character; the vehicle-resolver branch is not modeled
    // separately (same "only character-shaped objects" scope note).
    std::string onDamageCallback;
    // on_damage's registered threshold T (spec F: stored at player +0x28a8 / record +0x17c). The hook fires when the
    // health fraction crosses from above T to at or below T on one application.
    double onDamageThreshold = 0.0;
    // Player death callback timing (spec B "Firing for players", 2026-10-14 RESOLVED): the gameplay frame update
    // queues on_death about respawn_delay_ms (4000) after the finalizer arms the timer. -1 = not armed. Seconds on
    // EngineState::syncedActionNowSeconds().
    double playerDeathTimerArmedAtSeconds = -1.0;

    // on_detection (Sec7.23): resolves via the plain character resolver
    // directly (bypassing the generic/#PLAYER# front door, CONFIRMED) -
    // registered unconditionally once resolved, same simplification as
    // onTakeDamageCallback (the real double-write-to-the-same-slot detail,
    // Sec7.23's own OPEN item, is not modeled - one field, last write wins).
    std::string onDetectionCallback;

    // follower_make_independent (Sec7.2): object byte +0x1e01 bit 0x01.
    // CONFIRMED double-gate (variant 1) setter - gated on the SAME
    // forceFlagGatePasses below (CONFIRMED true in single player, Sec3.7/
    // Sec6.22's cross-reference covers this gate pair generically, not
    // just turn_invulnerable's own call site). No stated construction
    // default - OPEN until set, same as every other previously-unmodeled
    // byte in this struct.
    OpenValue<bool> independentFollower{"character byte +0x1e01 bit 0x01 (independent_follower)",
                                         "spec-lua-api-behaviour.md Sec7.2"};

    // follower_set_can_abandon (Sec7.6): object byte +0xa bit 0x02. Same
    // double-gate shape as independentFollower above.
    OpenValue<bool> followerCanAbandon{"character byte +0xa bit 0x02 (can_abandon/can_leave_behind)",
                                        "spec-lua-api-behaviour.md Sec7.6"};

    // set_dont_attack_me_on_sight_flag (Sec7.14): object byte +0x1e00 bit
    // 0x08. Same double-gate shape as independentFollower above.
    OpenValue<bool> dontAttackMeOnSight{"character byte +0x1e00 bit 0x08 (dont_attack_me_on_sight)",
                                         "spec-lua-api-behaviour.md Sec7.14"};

    // npc_suppress_persona (Sec7.19): object byte +0xb bit 0x01. Same
    // double-gate shape as independentFollower above.
    OpenValue<bool> npcSuppressPersona{"character byte +0xb bit 0x01 (disallow_persona_lines)",
                                        "spec-lua-api-behaviour.md Sec7.19"};

    // clear_animation_state/set_animation_state's own failure path
    // (Sec7.3/Sec7.11): object int field +0x1cc4 (a whole field, not a
    // bit). HIGH CONFIDENCE "-1 means no override" (3 consistent call
    // sites, Sec7.3's own text) - not independently confirmed from a
    // reader. Same double-gate shape as independentFollower above.
    OpenValue<int32_t> animationStateOverride{"character animation-state override (+0x1cc4)",
                                               "spec-lua-api-behaviour.md Sec7.3/Sec7.11"};

    // ai_scripted_action_is_complete (Sec7.28): whether the object's
    // +0x4aa action-id registry lookup (0x004f2880/0x006f63c0) still
    // succeeds - "absence means complete" (HIGH CONFIDENCE reading). A
    // single per-character scalar predicate, OPEN until a test sets it
    // (same shape as isAlive below) - the unresolved-name case does NOT
    // use this field at all (CONFIRMED: true on failed resolve, the only
    // predicate in Sec7's batch defaulting true rather than false).
    OpenValue<bool> scriptedActionLookupNonNull{"character +0x4aa action-id registry lookup result (0x004f2880/0x006f63c0)",
                                                 "spec-lua-api-behaviour.md Sec7.28"};

    // spec-ai-runtime.md Sec21.5 / Sec22.11 (a166141): the kind-3 scripted-order ticket (+0x4aa). ai_do_scripted_move
    // stages action 24 `move scripted` and allocates a ticket that stays PENDING until the mode's finish routine
    // completes it (EngineState::tickScriptedOrders: arrival within the 1.414 m box test); ai_clear_scripted_action
    // frees it. A test-set scriptedActionLookupNonNull still wins over the ticket.
    sr3airuntime::combat::ScriptedOrderTicket scriptedOrderTicket;
    bool scriptedMoveActive = false;  // action 24 is the current order and the mode tick is running
    bool scriptedMoveRun = true;      // Lua arg 3 (flagA): run, else walk
    // The 0x006f63c0 lookup reads non-null unless a ticket exists and is still pending (Sec7.28 FINAL).
    bool scriptedLookupNonNull() const {
        return scriptedActionLookupNonNull.known() ? scriptedActionLookupNonNull.get() : !scriptedOrderTicket.pending();
    }

    // ai_add_enemy_target (Sec3.9): see EnemyTargetRecord's own doc
    // comment above for the keying choice.
    std::unordered_map<std::string, EnemyTargetRecord> enemyTargets;

    // ai_add_enemy_target (Sec3.9, re-derived 2026-10-07): the ACTING
    // character's own descriptor byte +0x33 bit 0x8 gate - CONFIRMED to be
    // required (alongside the shared "not dead" predicate, Sec3.5's own
    // 0x0096f4f0) for the whole call to do anything at all; a failed
    // actor resolve, a failed dead-check, OR this bit being clear are all
    // equally a silent no-op (CONFIRMED: both hardcoded `lua_pushboolean`
    // sites collapse every bail-out path to `false`). A SIBLING bit of the
    // SAME byte `damageImmuneDescriptorBit33` (Sec20.32) already models at
    // bit 0x2 above - kept as its own, separately-cited field since the
    // two bits are independently confirmed, not proven to share one
    // write path.
    //
    // CHOSEN default 2026-10-08 (Team A spec-sync, lua-api Sec3.9
    // clarification): this bit is independently confirmed, by TWO other
    // spec documents (spec-player-runtime.md line 1086's "+0x33 bit 8"
    // AND-condition alongside descriptor +0xa bit 1; spec-anim-runtime.md
    // line 432's graph-write wrapper requiring the SAME bit alongside a
    // live controller pointer), to mean "object fully instantiated /
    // finished setup" - a per-instance, one-way latch, NOT a field of the
    // 0x02cc9900 per-class descriptor table (that earlier framing was a
    // genuine implementation-side mix-up, now corrected). No in-scope Lua
    // call ever CLEARS this latch or drives a not-yet-spawned state
    // machine, and every caller of ai_add_enemy_target names an already-
    // resolvable live character (the same precondition this project's own
    // getOrCreateCharacter() already requires to construct a CharacterState
    // at all) - so, same reasoning class as lifeState/killFinalizedFlagE3's
    // own "seeded at construction" default a few fields below, this is
    // CHOSEN true by default: a resolved character is presumed to have
    // already finished spawning, absent any in-scope signal to the
    // contrary. Kept as a plain bool (not OpenValue) because leaving it
    // OPEN would simply reproduce the exact bug this clarification exists
    // to fix - every ai_add_enemy_target call site through a resolved actor
    // would still refuse with an OpenStateError, which is what blocked
    // 28/49 missions in the 2026-10-08 post-max-HP-fix mission-drive probe
    // (results/verify_probe_blockers_ranked_20261008_post_maxhp_phase8_fix/
    // verdict_mission_drive_summary.txt).
    bool aiTargetActorGateBit33 = true;
    // ai_add_enemy_target's own per-actor owner/network gate
    // (0x008addb0(actingCharacter, 0)). CONFIRMED "structurally identical
    // in shape" to turn_invulnerable's own double-gate first half
    // (0x008ae480, both tail-calling the SAME underlying owner-check
    // 0x00884760, Sec3.7/Sec3.9) - but a GENUINELY DIFFERENT settled
    // single-player default: this entry's own text states gate FALSE is
    // "the common single-player case for most actors," the OPPOSITE
    // polarity from forceFlagGatePasses' own settled-true default
    // (Sec3.7/Sec6.22). Modeled as its own CHOSEN-false plain bool (not a
    // reuse of forceFlagGatePasses) - sharing that field here would
    // silently contradict this entry's own explicit text. A test can
    // still flip it to exercise the fetch-existing-record branch.
    bool aiTargetOwnerGatePasses = false; // CHOSEN false - Sec3.9's own "common single-player case" text, not a guess
    // arg 4's own separate effect (Sec3.9, 2026-10-07 correction): hands
    // the resolved TARGET's identity pair to the actor's own AI-data
    // sub-object (+0x2b0, the SAME sub-object set_ignore_ai_flag/Sec3.4
    // operates on) via 0x004e7b00 - this project has no field-level model
    // of that sub-object's own contents, so only that the call fired (and
    // with which target) is recorded.
    std::string lastAiAddEnemyTargetArg4TargetName; // "" = arg4 never fired (false, or the common path never reached it)
    uint64_t aiAddEnemyTargetArg4CallCount = 0;

    // on_revived (Sec10.8): the target character's own hook slot `2` -
    // CONFIRMED registered via the SAME generic character-hook-slot
    // mechanism `on_death`/`on_take_damage`/`on_attack_performed`/
    // `on_vehicle_destroyed` all reach (`0x005e4660`), slot `2` genuinely
    // new (no collision against this document's own existing catalogue:
    // `0`=on_death, `4`/`6`=on_take_damage, `8`/`13`=on_attack_performed,
    // `0x12`=on_vehicle_destroyed's fallback). This registry follows the
    // SAME single-dedicated-field-per-hook-kind convention
    // onTakeDamageCallback above already established (not a generic slot
    // array - this project's own minimal registry only ever tracks the 2
    // hook kinds in scope, on_take_damage and on_revived, so a real slot-
    // indexed array would be pure unused generality). Default empty ("no
    // callback registered"); CONFIRMED real behavior: registering with an
    // empty-string callback name CLEARS the slot rather than setting it
    // (Sec10.8's own re-decompilation of `0x005e4660`'s body).
    std::string onRevivedCallback;

    // on_dismiss (Sec1.11, batch 2026-10-08 - the 6-native clearance
    // fast-follow onto wave 1 batch 1's own original §1-4 scope): character
    // hook slot 10 (0xa) on the SAME generic per-character hook-slot
    // mechanism (0x005e4660) this header's own slot catalogue already
    // tracks (0=onDeathCallback, 1=onIncapacitatedCallback,
    // 2=onRevivedCallback, 4/6=on_take_damage, 6 on the +0x1f00 base=
    // on_weapon_pickup, 7=onWeaponEquipCallback, 8/13=
    // onAttackPerformed*Callback, 0x12=onVehicleDestroyedPlayerSlotCallback,
    // 0x13=onCollisionCallback, 0x14=on_ai_do_action_complete) - slot 10 is
    // genuinely new, no collision. RE-CONFIRMED 2026-10-08 by independent
    // direct disassembly of `0x00a57dd0` from scratch: UNLIKE every other
    // hook in this list, on_dismiss's own resolver (`0x005e4dd0`) makes NO
    // attempt at the generic/`#PLAYER#`-chain resolver first - it goes
    // straight to the plain by-name character resolver, so
    // `on_dismiss("cb","#PLAYER#")` (or any `#FOLLOWER#` sentinel) cannot
    // register - a genuine behavioral gap from its on_* siblings, not
    // shared with onRevivedCallback above despite using the same plain
    // character-resolver fallback for ITS OWN arg 2 (on_revived's own
    // resolver just happens to never try the generic chain either, but for
    // a different, independently-stated reason, Sec10.8). The resolver's
    // own filter is purely the generic "character" class-descriptor bit at
    // the mechanism level, NOT a companion/homie-only restriction (RE-
    // CONFIRMED 2026-10-08) - the "homie dismissal" reading some of this
    // document's earlier text used is a calling-script convention only,
    // not an engine-enforced one, so this project applies the hook to ANY
    // character name, matching the real unrestricted mechanism. Same
    // empty-string-clears-slot convention as onRevivedCallback above (the
    // SAME underlying setter, CONFIRMED). Default empty.
    std::string onDismissCallback;

    // on_death (Sec3.2, native-implementation batch 2026-10-07/2026-10-08):
    // the character's own hook-slot `0` ("death" slot) - the SAME generic
    // per-character hook-slot mechanism (`0x005e4660`) this document's own
    // slot catalogue already establishes (see onRevivedCallback's own doc
    // comment just above for the full list: `0`=this field, `4`/`6`=on_take_
    // damage, `8`/`13`=on_attack_performed, `0x12`=on_vehicle_destroyed's
    // generic-path fallback below, `2`=on_revived). CONFIRMED (Sec3.2, full
    // re-derivation 2026-10-07): registering with an empty-string callback
    // name CLEARS the slot rather than setting it (plain assignment already
    // reproduces this - "" is this field's own documented "no callback"
    // default). arg 2 resolves via EITHER the generic/`#PLAYER#` chain
    // (`0x00a280c0`) or the plain-character fallback (`0x005e4dd0`); both
    // paths write the SAME slot with no behavioral difference between them
    // (Sec3.2's own text), so this project's single flat objectResolves()
    // gate (Sec29) already covers both without needing to distinguish which
    // one a real call would have taken - unlike on_vehicle_destroyed/
    // character_is_dead below, where the two paths genuinely diverge.
    // Default empty ("no callback registered").
    std::string onDeathCallback;

    // on_vehicle_destroyed (Sec3.3)'s OWN generic-path branch: hook slot
    // `0x12` (18) on the SAME per-character hook mechanism above - reached
    // ONLY when arg 2 resolves via the player-class-gated generic chain
    // (EngineState::playerClassChainResolves() in engine_state.h), never for an
    // ordinary named vehicle (Sec3.3's own 2026-10-07 direct-disassembly
    // refutation of an earlier "kind-agnostic" hypothesis: the generic
    // chain's first-tier resolver, `0x00a27e20`, masks every hit against the
    // object's own class-descriptor bit - the independently-established
    // PLAYER CLASS bit - so a vehicle's own name can never resolve here).
    // Same empty-string-clears-slot convention as onDeathCallback above
    // (same underlying setter, `0x005e4660`). Default empty.
    std::string onVehicleDestroyedPlayerSlotCallback;

    // character_is_dead (Sec3.5): CONFIRMED by direct disassembly of
    // `0x0096f4f0` itself - "dead" when the resolved object +0xcc8 equals
    // `5`, OR flag byte +0xe3 bit 0 is set (`0x01`), OR the resolved pointer
    // is null (handled upstream by this project's own objectResolves()/
    // playerClassChainResolves() gates, before this predicate is ever
    // reached). These are the EXACT same two real fields `lifeState`
    // (+0xcc8, Sec7.13/Sec32.3/Sec34.5) and `killFinalizedFlagE3` (+0xe3 bit
    // 0x01, Sec34.5) above already model, for the SAME real predicate
    // 0x0096f4f0, already seeded (not OPEN) at construction per Sec34.5's
    // own zero-filled-allocation reasoning - see notDeadForActionPlay()'s
    // own doc comment just above for the exact same combination. Checked
    // directly before adding anything new: no separate field is added here,
    // character_is_dead's own stub (lua_spec_confirmed_stubs.cpp) reads
    // lifeState/killFinalizedFlagE3 directly.

    // minimap_icon_add_do (Sec10.5): every recorded icon-add request
    // against this resolved object, in call order. See MinimapIconRecord's
    // own doc comment above for what is/isn't modeled.
    std::vector<MinimapIconRecord> minimapIcons;

    // object_indicator_add_do (Sec10.6): every recorded indicator-add
    // request against this resolved object, in call order. See
    // ObjectIndicatorRecord's own doc comment above for what is/isn't
    // modeled.
    std::vector<ObjectIndicatorRecord> objectIndicators;

    // --- vehicle_exit_group_do / vehicle_exit_group_check_done
    // (spec-lua-api-behaviour.md Sec30.5, "ranking tranche 03",
    // 2026-10-02) ---------------------------------------------------

    // Sec30.5's shared preamble: "+0x16c0/+0x16c4 non-zero" means this
    // character is currently in a vehicle. This project has no real
    // character<->vehicle linkage (CharacterState::stateEnum already
    // models THAT a character is seated/entering/exiting via the real
    // +0x16d4 state enum, but not WHICH vehicle) - this is this project's
    // own minimal, explicitly-scoped stand-in for the link, used by
    // vehicle_exit_group_do's own "remembers the first vehicle found"
    // step. No Lua-visible function in this task's scope ever writes this
    // field; a test sets it directly. OPEN until set - same "never invent
    // a fresh object's values" discipline as every other CharacterState
    // field.
    //
    // SECOND CONSUMER, 2026-10-07 (Sec3.10, character_is_in_vehicle,
    // cleared for implementation this date): the SAME physical +0x16c0/
    // +0x16c4 pair is also what Sec3.10 compares against a specific,
    // resolved vehicle's own +8/+0xc self-identity pair (its vtable +0x70
    // accessor's return value). Checked here before adding anything new -
    // this field already models that exact physical link, just as a NAME
    // rather than a raw dword pair (this project's own uniform, project-
    // wide object-identity convention - see this header's own top
    // comment). character_is_in_vehicle therefore compares
    // currentVehicleName against its own arg 2's resolved name directly;
    // no parallel/duplicate field was added for it.
    //
    // UPDATE 2026-10-08 (W2-1 post-merge review fix): no longer read-only by
    // every in-scope native. spec-player-runtime.md Sec7.8 confirms state
    // 0's on-entry handler (reached synchronously from the enter submitter
    // 0x00af22a0's own final step, Sec7.4 step 11) writes +0x16c0/+0x16c4 =
    // the vehicle handle alongside +0x16d4 = 1. stub_vehicle_enter_do's
    // instant/named-seat paths, stub_vehicle_enter_guardian_angel, and
    // stub_vehicle_enter_group_do's instant per-member path now write the
    // resolved vehicle name here, gated the same way as stateEnum above (see
    // its own UPDATE note) - see lua_spec_confirmed_stubs_b15_wave2_core_
    // dispatchers.cpp's top comment for the full account.
    //
    // UPDATE 2026-10-10 (m05 mission-drive deadlock at vehicle_exit_teleport,
    // the exit-side mirror of the enter-side fix just above): "Exit does NOT
    // clear this field" was true only for the NON-instant exit path, which
    // stays out of scope exactly as stated. stub_vehicle_exit_do's own
    // INSTANT (arg 2 true) seated branch now clears this field to "" (this
    // project's own established "empty = no current vehicle" convention,
    // already read by resolveEnterVehicle() above) alongside completing
    // stateEnum to 0 - see that stub's own FIXED note for the full
    // spec-player-runtime.md Sec6 citation of why the instant path's real
    // release (Sec7.8's +0x16c0/+0x16c4 = null, 0x00b0881c) is now considered
    // in scope the same way its +0x16d4 = 0 sibling write is.
    OpenValue<std::string> currentVehicleName{"character's current vehicle (+0x16c0/+0x16c4 link)",
                                               "spec-lua-api-behaviour.md Sec30.5"};

    // Sec30.5's shared preamble: "0x0096f4f0 is an 'is dead/gone'
    // predicate" - read by vehicle_exit_group_check_done's own CONFIRMED
    // "resolves, is alive, and is in state 2 or 3" gate. Scoped only to
    // that one function (no other in-scope name reads this) - a
    // DIFFERENT field from isDeadHighConfidence above, which is this
    // project's own HIGH-CONFIDENCE stand-in tied specifically to
    // set_current_hit_points' own internal logic (Sec7.31), a separate
    // call site this predicate is never confirmed to share. OPEN until
    // set.
    OpenValue<bool> isAlive{"character alive/not-gone predicate (0x0096f4f0)", "spec-lua-api-behaviour.md Sec30.5"};
};

} // namespace sr3luahost
