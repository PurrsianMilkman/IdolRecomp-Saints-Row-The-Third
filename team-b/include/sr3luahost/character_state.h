// Character-shaped engine state: CharacterState and the per-character record
// types it holds (EnemyTargetRecord, MinimapIconRecord, ObjectIndicatorRecord).
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
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3luahost/open_state.h"
#include "sr3luahost/world_position.h"

namespace sr3luahost {

// One `ai_add_enemy_target` (Sec3.9) threat/target record. RE-DERIVED
// 2026-10-07 (direct disassembly of `0x00a3d260`/`0x00a3d180`/
// `0x004dedb0`/`0x004f5dd0`/`0x008addb0`) - this struct's own fields
// previously modeled the PRE-re-derivation understanding (arg 4 and the
// sentinel match feeding the two flag bits directly); that reading is
// REFUTED by the current spec text, which confirms instead: both bits are
// sourced from ARG 3 (compared against the literals `0` and `2`), land on
// one of TWO DIFFERENT record offsets depending on which of two distinct
// record-management paths ran (an unconditional free-list "create," or a
// per-actor "fetch an EXISTING record, write only if found"), and arg 4's
// real effect is a wholly separate call (see
// `lastAiAddEnemyTargetArg4TargetName` below), never reaching this record
// at all. The spec itself does not resolve whether the real record lives
// on the ACTING character or is a free-standing global table keyed by
// object identity - this project's own choice (stated here, not scraped
// from anywhere): model it as a per-ACTOR map, keyed by the arg-3-derived
// integer (as a decimal string) when nonzero, else by the resolved target
// name.
struct EnemyTargetRecord {
    // int64 of arg 3 via 0x00ea2596: truncation toward zero (Sec4.1, settled
    // 2026-10-01; the earlier banker's/round-to-nearest readings are struck).
    int64_t priorityOrId = 0;
    // CONFIRMED 2026-10-07: both branches use the identical two facts (is
    // arg3 exactly 0; is arg3 exactly 2) to set what are, in both cases,
    // bits 0x02/0x04 respectively - just at two different record offsets
    // (`+0x30` on the create path, `+0x18` on the fetch path) this project
    // does not model as physically distinct fields, since nothing in
    // scope reads either offset back independently of this one record.
    bool bit02ArgIsZero = false; // arg3 == 0
    bool bit04ArgIsTwo = false;  // arg3 == 2
    // true: the unconditional free-list "create" path ran (the common
    // single-player case per this entry's own text); false: the per-actor
    // "fetch an existing record" path ran AND found one (the only way this
    // struct instance exists on that path at all - a miss writes nothing,
    // modeled by simply never inserting a record, see the stub's own
    // lookup-before-write logic).
    bool createdNewRecord = false;
};

// minimap_icon_add_do (spec-lua-api-behaviour.md Sec10.5) - one recorded
// "an icon-add was requested" call against a resolved object. The real
// engine's own body (Sec10.5, fully re-derived by adversarial review) does
// a genuinely bit-tested two-way dispatch (a direct single-target add call
// vs. a filtered-list broadcast, gated on a shared per-object-type
// descriptor table bit this project has no real data for) and resolves
// args 2/3 through a real name->id hash lookup this project also has no
// real table for - per this task's own brief, NONE of that dispatch
// selection or id-hash resolution is modeled here; only the call's own
// raw arguments are recorded, honestly, as "a request," not "the specific
// branch the real engine would have taken." iconType/group are stored as
// the raw strings actually passed (never hashed to an id - Sec10.5's own
// `0x00802f60` hash is OPEN as to its real algorithm), param4/flag5 as the
// raw numeric values after this project's own standard nil-gated-default
// extraction (Sec10.5: param4 defaults 0.0, flag5 defaults 3 - CONFIRMED
// defaults; the real bit-tested MEANING of flag5 is explicitly not
// modeled, see this struct's own top note).
struct MinimapIconRecord {
    std::string iconType;   // arg 2, mandatory
    std::string group;      // arg 3, optional, default "" (CONFIRMED default, Sec10.5)
    double param4 = 0.0;    // arg 4, optional, default 0.0 (CONFIRMED default, Sec10.5)
    int64_t flag5 = 3;      // arg 5, optional, default 3 (CONFIRMED default, Sec10.5) - real bit-tested dispatch meaning NOT modeled (see struct's own top note)
};

// object_indicator_add_do (spec-lua-api-behaviour.md Sec10.6) - one
// recorded "an indicator-add was requested" call against a resolved
// object. Same minimal-stand-in convention as MinimapIconRecord above:
// the real HUD dispatch chain (`0x008f96f0`/`0x008f50d0`, Sec10.6) is not
// modeled, only the raw arguments actually passed. arg2/arg3 are
// CONFIRMED mandatory with NO nil/absence gate (Sec10.6 - a genuine
// difference from every optional sibling argument in this cluster);
// arg4/arg5 are CONFIRMED optional with the standard nil-gated idiom,
// defaults 3 and 100.0 respectively (Sec10.6 - arg5's default is a named
// `.rdata` float constant, not a bare 0.0, unlike several sibling
// functions elsewhere in this document).
struct ObjectIndicatorRecord {
    int64_t arg2 = 0;    // mandatory, no nil-gate (CONFIRMED, Sec10.6)
    int64_t arg3 = 0;    // mandatory, no nil-gate (CONFIRMED, Sec10.6)
    int64_t arg4 = 3;    // optional, default 3 (CONFIRMED, Sec10.6)
    double arg5 = 100.0; // optional, default 100.0 (CONFIRMED, Sec10.6)
};

// One tracked "character"-shaped object (per this header's own top note:
// really just "whatever the resolved name string was", never a true
// distinct engine entity). CLOUD PHASE 2026-09-30: every engine field
// below that a stub READS is an OpenValue (open_state.h) - OPEN for a name
// not yet seen, refusing reads until set, because no spec gives a fresh
// object's values. The per-field notes below that mention a "default"
// describe the pre-conversion stand-ins and are kept for history; the
// stand-in values themselves are gone.
struct CharacterState {
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

    // --- Wave 2 batch W2-1, core dispatchers (2026-10-08): vehicle_exit_do,
    // vehicle_enter_do, move_to_do, teleport_to_object, vehicle_enter_
    // check_done, vehicle_pathfind_navmesh_do, character_damage, qte_start,
    // vehicle_exit_check_done, vehicle_enter_group_check_done, vehicle_enter_
    // group_do, vehicle_enter_guardian_angel, vehicle_exit_guardian_angel
    // (spec-lua-api-behaviour.md Sec20.9/20.32/21.11/21.22/22.1/22.3/22.4/
    // 24.9/38.1; spec-player-runtime.md Sec6/Sec7). See
    // lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp for every
    // stub's own citation. Placed here, next to the existing vehicle-
    // occupancy fields above (stateEnum/currentVehicleName/isAlive), which
    // these natives REUSE rather than duplicate. ---------------------------

    // The exit request record (spec-player-runtime.md Sec6.2: 0x33 bytes,
    // constructor 0x004dccd0, copied into the occupancy slot at +0xa0 by the
    // exit request 0x00af3cd0) - ONLY the fields this batch's two exit
    // natives write are kept, each defaulting to the constructor's own
    // CONFIRMED reset value. This project does not drive the occupancy state
    // machine (spec-player-runtime.md Sec7, sr3player's VehicleOccupancy) from
    // the Lua host, so the record is the call's observable output here, not
    // an input to a simulated exit. This comment originally continued "...
    // so +0x16d4 (stateEnum) is deliberately NOT advanced" - stale even
    // before 2026-10-10's own instant-release fix: stub_vehicle_exit_do's and
    // stub_vehicle_exit_guardian_angel's seated branches have written +0x16d4
    // = 2 synchronously since 2026-10-08, and the instant path now completes
    // to 0 (released) too - see stub_vehicle_exit_do's own doc comment, right
    // after its `c.stateEnum.set(2);` line, for the current, full account.
    struct VehicleExitRequestRecord {
        bool instant = false;             // +0x30 bit 0 - vehicle_exit_do arg 2 (Sec6.2/Sec6.3.3)
        bool flag30Bit4 = false;          // +0x30 bit 4 - vehicle_exit_do arg 4; reader U6.7, role OPEN
        int32_t animationOverrideId = -1; // +0x2c - vehicle_exit_do arg 5 via 0x004bf810; -1 = none (constructor)
        bool flag31Bit4 = false;          // +0x31 bit 4 - set by 0x00a66c70 = vehicle_exit_guardian_angel (Sec6.3.3)
        bool replicated = true;           // +0x32 bit 1 - constructor default 1 (Sec6.2)
    };
    std::optional<VehicleExitRequestRecord> lastVehicleExitRequest;
    uint64_t vehicleExitRequestCount = 0; // 0x00af3cd0 calls issued by this batch's natives
    uint64_t vehicleExitCancelCount = 0;  // 0x00af3e10 calls (vehicle_exit_do's cancel path)

    // 2026-10-09, spec-player-runtime.md Sec7.4a: the eligibility gate
    // 0x00b0ca60(character, forced, eligibility_bit) itself - Team A traced
    // it exhaustively and confirmed it is a PURE character-state check, no
    // vehicle/physics argument at all (9 refusal paths + 1 success path,
    // plain boolean, no secondary reason code). The three fields below are
    // new per-character facts this gate reads that nothing in this project
    // modelled before; every other field it reads (stateEnum +0x16d4,
    // ragdollStateEnumCd2 +0xcd2, lifeState/killFinalizedFlagE3 +0xcc8/+0xe3,
    // movementStateByteCcd +0xccd, stateCodeByte501 +0x501 below) is REUSED,
    // not duplicated - see vehicleEnterEligibilityGate() in
    // lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp for the full
    // step-by-step implementation and citation of each reuse.
    //
    // Step 3's +0x16d8/+0x16dc "busy/pending" pair (HYPOTHESIS, flagged OPEN
    // by Sec7.3/Sec13.0/Sec13.1 step 1 and Sec7.4a step 3 alike - a SECOND
    // confirmed reader with identical polarity, strengthening but not
    // resolving that open item). Checked first: no existing field models
    // this pair (sr3player's VehicleInteractionRecord::driverHandle, Sec8.1,
    // is the SAME relative offset on the VEHICLE object - a different real
    // field by the project's own object-type-keyed-offset convention, not a
    // reuse candidate). CHOSEN false (not confirmed): applies UNCONDITIONALLY
    // even to a forced request (Sec7.4a step 3 is not gated on "forced"), so
    // leaving this OpenValue-strict would throw OpenStateError on every
    // single enter attempt including vehicle_enter_guardian_angel's forced
    // path, which would make implementing this gate at all pointless (same
    // "a default is needed so as not to block everything" reasoning as
    // aiTargetOwnerGatePasses/aiTargetActorGateBit33 above) - the common
    // case for a character who is not already mid some OTHER tracked
    // interaction is that this pair is zero. A test can still flip it to
    // exercise the refusal branch.
    bool vehicleEnterEligibilityBusyPendingNonZero = false; // CHOSEN false - +0x16d8/+0x16dc, Sec7.4a step 3

    // Step 6's generic, non-vehicle-specific interaction-lock helper
    // (0x009562d0, widely reused elsewhere) - Sec7.4a's own text: "shape
    // confirmed, exact semantics OPEN, hinges on a +0x1b78-indirected
    // sub-object and two unidentified hash constants 0x1d56/0x1d7e." The
    // spec explicitly flags this as unresolved, so - unlike the busy/pending
    // pair above - this is kept genuinely OPEN (no CHOSEN default), per this
    // project's "never silently guess at something the spec itself calls
    // open" discipline: a test must .set() it to get past step 6 for a
    // NON-forced request (forced skips this check entirely, Sec7.4a step 6's
    // own "only when NOT forced" gate). True = the lock PASSES (does not
    // refuse) - same "GatePasses" polarity convention as
    // forceFlagGatePasses/controlLockGatePasses/behaviorSetVtableGatePasses
    // above.
    OpenValue<bool> genericInteractionLockGatePasses{
        "character generic interaction-lock helper 0x009562d0 (shape confirmed, exact semantics OPEN)",
        "spec-player-runtime.md Sec7.4a step 6"};

    // Step 8's "+0x2ba bit 0x10, the exact, already-named cant_enter_vehicle
    // flag" (Sec7.4a). Checked first: this project's OWN cantFleeFlag/
    // followerCanAbandon/cantCowerFlag fields below are a DIFFERENT real
    // byte at absolute +0xa (their own doc comments are explicit about that
    // offset), not this ai_force_flags-table byte at +0x2ba (sub-object
    // +0x2b0 + 0xa, the same human_ai_data sub-object humanAiDataForceFlagsByte8/
    // humanAiDataForceFlagsByte9 below already model two OTHER bytes of,
    // +8/+9) - no existing field is this byte, despite the coincidentally
    // overlapping flag NAMES (the real engine apparently has two
    // independent "cant_flee"/"can_leave_behind"-named bits at two different
    // byte offsets; not reconciled here, out of this task's scope). CHOSEN
    // false (not confirmed for THIS project's own per-instance data, since
    // this host has no .xtbl per-class AI data loader): cross-referenced
    // against sr3airuntime's OWN already-confirmed model of this exact
    // physical byte, decision.h's ForceFlags::cantEnterVehicle (+0x2ba bit
    // 4 of the 41-flag table, Sec9.9), which that unit's own A1 pass
    // defaults to false for every one of its 41 fields - the same
    // "ai_force_flags default off" conclusion a sibling, independently-
    // confirmed unit of this project already reached, reused here rather
    // than re-guessed from nothing (sr3airuntime and sr3luahost are
    // separate subsystems with no header coupling, so the VALUE is reused,
    // not the field itself).
    bool cantEnterVehicleFlag2ba = false; // CHOSEN false - +0x2ba bit 0x10, Sec7.4a step 8

    // The shared enter submitter 0x00af22a0 (spec-player-runtime.md Sec7.4,
    // completing Sec38.1): its boolean result depends on the eligibility
    // predicate 0x00b0ca60 (Sec7.4a, IMPLEMENTED above as
    // vehicleEnterEligibilityGate() - a refusal there is now a CONFIRMED
    // false, computed, not OPEN) and the vehicle/seat pick
    // 0x00b0fdf0/0x00b105c0 (U6.2 - world state this host still has no
    // model of). A test's own .set() on this field, when present, still
    // wins outright (checked BEFORE the gate runs at every call site) - full
    // backward compatibility with every existing test that drives the
    // overall accepted/refused result directly. Two separate facts, because
    // a "forced" request (request +0x23 bit 3, vehicle_enter_guardian_angel)
    // skips several of the eligibility refusals an ordinary one is subject
    // to (Sec7.4 step 3; Sec7.4a step 6).
    //
    // 2026-10-09 follow-up: an ELIGIBLE gate result used to fall through to
    // this OpenValue's own .get(), which throws OpenStateError whenever no
    // test has .set() it - in practice this meant EVERY real mission-drive
    // run (m05/m07/m09 confirmed by direct measurement) blocked here anyway,
    // because nothing in a real run ever calls .set() on this field: wiring
    // the eligibility gate alone did not move a single mission. Sec7.4 step 4
    // (the seat pick, U6.2) is a genuinely separate, unmodelled unit - no
    // spec text here gives its success/failure condition, and this host has
    // no vehicle seat-occupancy/availability state to compute it from. CHOSEN
    // default, in computeVehicleEnterSubmitAccepted() (lua_spec_confirmed_
    // stubs_b15_wave2_core_dispatchers.cpp): an eligible gate result with no
    // test override now resolves to `true` (seat pick assumed to succeed)
    // rather than throwing - the same "a default is needed so as not to
    // block everything" reasoning as vehicleEnterEligibilityBusyPendingNonZero
    // above, applied one level up. This is a genuine, flagged guess, not a
    // confirmed fact: it is wrong for any real case where the real
    // 0x00b0fdf0/0x00b105c0 would refuse an eligible character (e.g. a full
    // vehicle, no free seat matching the request) - this project has no way
    // to tell that case apart from a successful pick yet. Routed to the
    // disassembly team as an open question (see this task's own report):
    // what, concretely, can 0x00b0fdf0 (with a vehicle) / 0x00b105c0
    // (auto-pick) refuse, beyond "no free seat" - and does any confirmed
    // mission-script case in m05/m07/m09 actually depend on a refusal here
    // (i.e. is `true` ever observably wrong for these three missions
    // specifically)? The OpenValue itself is left in place, name and spec
    // citation unchanged, so a test's own .set() (exact seat-pick outcome)
    // still overrides the CHOSEN default exactly as before, and so a future
    // pass that actually models U6.2 only has to add .set() calls, never
    // touch this field's shape.
    OpenValue<bool> vehicleEnterSubmitAccepted{
        "0x00af22a0 enter submitter result (eligibility 0x00b0ca60 + seat pick 0x00b0fdf0/0x00b105c0)",
        "spec-player-runtime.md Sec7.4; spec-lua-api-behaviour.md Sec38.1"};
    OpenValue<bool> vehicleEnterSubmitAcceptedForced{
        "0x00af22a0 enter submitter result for a forced request (+0x23 bit 3)",
        "spec-player-runtime.md Sec7.4; spec-lua-api-behaviour.md Sec38.1"};
    // vehicle_enter_do's third dispatch path (Sec38.1): the AI order that
    // stages a default action template and starts action 0x31 (= index 49
    // "vehicle enter scpt" of the 70-entry CAE table, Sec30.4) - whether it
    // starts is OPEN.
    OpenValue<bool> vehicleEnterAiOrderStarts{"vehicle_enter_do AI-order path: action 0x31 start result",
                                              "spec-lua-api-behaviour.md Sec38.1"};
    static constexpr int kVehicleEnterPathInstant = 1;       // instant placement (submitter)
    static constexpr int kVehicleEnterPathNamedSeat = 2;     // named-seat placement (submitter)
    static constexpr int kVehicleEnterPathAiOrder = 3;       // AI order, action 0x31
    static constexpr int kVehicleEnterPathGuardianAngel = 4; // forced submit (vehicle_enter_guardian_angel)
    struct VehicleEnterRequestRecord {
        std::string vehicleName; // the resolved vehicle (direct, or the character-fallback's own vehicle)
        int path = 0;            // kVehicleEnterPath*
        int64_t seatIndex = -1;  // request +0x00 "requested seat (-1 none)" (spec-player-runtime.md Sec7.2); int64 = the 0x00ea2596 conversion's own width
        std::string seatName;    // vehicle_enter_do arg 7 ("" = absent)
        bool arg5 = false;       // vehicle_enter_do's two optional booleans - roles not given by Sec38.1
        bool arg6 = false;
        bool accepted = false;   // the dispatched path's own result (an order's result for group_do is never read)
    };
    std::optional<VehicleEnterRequestRecord> lastVehicleEnterRequest;
    uint64_t vehicleEnterRequestCount = 0;

    // vehicle_enter_check_done (Sec22.3): the state-code byte pair compared
    // against '&'/0x12. CONFIRMED (2026-10-10 Team A spec sync, closing this
    // field's own previously-OPEN spawn-default question): +0x501 is the
    // character's current AI-mode byte, spawn-default 0x00 = Idle, zeroed
    // unconditionally by the character base constructor 0x00954340 (see
    // EngineState::getOrCreateCharacter's own seed). Exactly 3 writers exist
    // project-wide: construction (above), the main gameplay AI-mode
    // transition primitive 0x004dd380 (spec-ai-runtime.md Sec3.3, every
    // voluntary mode change), and the proxy-only network read 0x004ea530
    // (non-owned/proxy human only, spec-ai-runtime.md Sec2.4 point 3). The
    // full 44-value mode enum is spec-ai-runtime.md Sec3.2 (0=Idle,
    // 18=Hijack, 43=Player, etc; see kAiModeIdle/kAiModeHijack below). The
    // "rich helper" 0x009b95c0 consulted only when arg 2 is true (its
    // internal role beyond the decoded branch shape is OPEN) is unaffected.
    OpenValue<int32_t> stateCodeByte501{"character state-code byte +0x501", "spec-lua-api-behaviour.md Sec22.3"};
    // +0x504 (requested-mode byte): spawn-default 0x2d/45, the shared
    // "no mode" sentinel for the previous/requested-mode fields (spec-lua-
    // api-behaviour.md Sec22.3, same 2026-10-10 sync) - not pre-seeded here
    // because no test currently depends on its spawn value and the "no
    // mode" sentinel is not itself a mode-enum value worth a constant; the
    // vehicle_enter_check_done 0x12 comparison below still names kAiModeHijack.
    OpenValue<int32_t> stateCodeByte504{"character state-code byte +0x504", "spec-lua-api-behaviour.md Sec22.3"};
    OpenValue<bool> enterCheckHelper9b95c0{"0x009b95c0 vehicle-enter re-check helper result (arg 2 true only)",
                                           "spec-lua-api-behaviour.md Sec22.3"};
    // Reused again, 2026-10-09, by Sec7.4a's eligibility gate step 8: the
    // SAME physical byte +0x501 is the per-human "current AI mode" field
    // sr3airuntime's own A0 unit fully models as ModeState::currentMode
    // (mode_table.h) - a DIFFERENT subsystem with no header coupling to
    // this one (checked: no sr3luahost header includes sr3airuntime's, and
    // vice versa - the Lua host's vehicle-enter path does not drive the AI
    // mode machine). "Idle" is CONFIRMED mode id 0 there (mode::Idle) and
    // independently by this very spec file's own direct citations
    // (spec-player-runtime.md lines 647/656: "+0x501 is 0 (Idle)") - a
    // cross-subsystem CONFIRMED fact, not a guess, so Sec7.4a step 8's
    // "allow if current AI mode isn't Idle" reuses stateCodeByte501 and
    // compares it against the literal 0 (kAiModeIdle below) rather than
    // including sr3airuntime's header for one constant.
    static constexpr int32_t kAiModeIdle = 0; // sr3airuntime::mode::Idle (mode_table.h) - same physical +0x501
    // CONFIRMED (spec-ai-runtime.md Sec3.2 mode 18; spec-lua-api-behaviour.md
    // Sec22.3, 2026-10-10 sync): mode 18 = Hijack in the 44-value mode enum -
    // the literal 0x12 vehicle_enter_check_done tests against stateCodeByte504
    // (see stub_vehicle_enter_check_done).
    static constexpr int32_t kAiModeHijack = 0x12; // sr3airuntime mode::Hijack equivalent - same physical +0x501/+0x504

    // Sec7.4a step 8's AI sub-state byte (+0x500) - CONFIRMED meaning
    // ("observe vehicle world accident"/"observe vehicular accident" at
    // values 0x10/0x11, resolved against the already-documented 177-entry
    // sub-state table 0x012e1168) but, like stateCodeByte504 above (the
    // requested-mode byte's own per-character runtime value, not its spawn
    // default), no per-character VALUE is given by any spec this project
    // has, so it is OPEN until a test sets it - the SAME "+0x500/+0x501" real pair
    // sr3airuntime's A0 unit already fully owns as ModeState::subState/
    // currentMode (mode_table.h), reused here only by VALUE citation per
    // this field's own doc comment above, same cross-subsystem-boundary
    // reasoning, no new field added on the sr3airuntime side for this.
    OpenValue<int32_t> aiSubStateByte500{"character AI sub-state byte (+0x500)",
                                          "spec-player-runtime.md Sec7.4a step 8; spec-ai-runtime.md Sec2.5 (sub-state table 0x012e1168)"};

    // move_to_do (Sec21.22): the per-character predicate 0x00a3bae0(target,
    // arg5) - one OPEN fact per value of its boolean argument, since the
    // result may depend on it; the mode-3 animation-tag probe (0x004b46c0 on
    // (+0xd24)+0x10, codes 0x32/0x34 - "either resolves" keeps mode 3, else
    // mode 1's code); and the move dispatch 0x00993630's result.
    OpenValue<bool> moveToPredicateA3baeArgFalse{"0x00a3bae0(character, false) move-to predicate",
                                                 "spec-lua-api-behaviour.md Sec21.22/Sec22.15"};
    OpenValue<bool> moveToPredicateA3baeArgTrue{"0x00a3bae0(character, true) move-to predicate",
                                                "spec-lua-api-behaviour.md Sec21.22/Sec22.15"};
    OpenValue<bool> moveToMode3AnimTagResolves{"0x004b46c0 lookup of code 0x32 or 0x34 on (+0xd24)+0x10 (move mode 3)",
                                               "spec-lua-api-behaviour.md Sec21.22"};
    OpenValue<bool> moveToDispatchSucceeds{"0x00993630 move dispatch result", "spec-lua-api-behaviour.md Sec21.22"};
    // The per-character move-to sub-record (Sec22.15: +0xc30 a 4-byte field
    // cleared to -1, +0xc34 a 2-byte field cleared to 0). Write-only in this
    // batch (move_to_check_done's existing stub, b04, still consults only the
    // shared request pool), so std::nullopt = "never written by an in-scope
    // native" rather than an invented construction value. +0xc30 holds the
    // 0x00d9e180(2000,4000) randomized deadline on success (Sec22.15: "hidden
    // this = character +0xc30"); this host stores a nextMonotonicStamp()
    // marker for it (same CHOSEN clock stand-in as regenRestartDeadlineStamp),
    // -1 on every early-failure path (CONFIRMED sentinel).
    std::optional<int64_t> moveToTimerC30;
    std::optional<int32_t> moveToRequestTagC34;
    struct MoveToRequestRecord {
        std::string destinationName; // arg 2 (only when arg 6 is false - the arg-6 path is refused, see the stub)
        int64_t moveMode = 0;        // arg 3, truncated
        int32_t stateCode = 0;       // the settled 1/2/3 state code passed to 0x00993630
        bool arg4 = false;
        bool arg5 = false;
        int32_t returnedId = 0;
    };
    std::optional<MoveToRequestRecord> lastMoveToRequest;
    uint64_t moveToPreMoveCallCount = 0; // 0x0095acb0(target, 1, 0.2, 0, 0) - unconditional once arg 1 resolves

    // teleport_to_object (Sec22.4) - its OWN record, deliberately separate
    // from teleport's (Sec4.3) EngineState::teleportRequests()/
    // teleportScriptedRequestCount(): the two natives share the request-id
    // kind (4) and the general applier 0x00996b50 but not their argument
    // records (arg 8 is a boolean for teleport, a heading in radians here),
    // so nothing teleport's existing fields mean is reused or overwritten.
    struct TeleportToObjectRequest {
        std::string targetObjectName;
        bool arg3 = false;           // CONFIRMED default false; role not named by Sec22.4
        bool arg4 = true;            // CONFIRMED default true (Sec22.4's own default asymmetry note)
        double offsetX = 0.0, offsetY = 0.0, offsetZ = 0.0; // args 5-7, raw - no coordinate system in this host
        double headingRadians = 0.0; // arg 8 as passed
        double headingDegrees = 0.0; // arg 8 * 57.29583 (CONFIRMED constant 0x0111a3f8)
        bool tookPlayerPath = false; // class-row +0xa bit 0x2 (0x009e2220) vs the general applier (0x00996b50)
        bool appliedLocally = false; // general path with authority: applied + kind-4 request completed at once (Sec35.4)
    };
    std::optional<TeleportToObjectRequest> lastTeleportToObjectRequest;
    uint64_t teleportToObjectCount = 0;
    uint64_t teleportToObjectShieldEjectCount = 0; // 0x00996b50's forced human-shield-partner ejection fired

    // character_damage (Sec20.32): 0x0096fa20's early exit on the target's
    // descriptor byte +0x33 bit 0x2 (no construction value given - OPEN), and
    // the non-authoritative branch's opcode-0x41 record (amount, flags word,
    // source) - the only branch whose outcome this host can reproduce.
    OpenValue<bool> damageImmuneDescriptorBit33{"character descriptor byte +0x33 bit 0x2 (0x0096fa20 early exit)",
                                                "spec-lua-api-behaviour.md Sec20.32"};
    struct CharacterDamageRecord {
        float amount = 0.0f;   // single precision, NOT rounded (Sec20.32's distinct numeric sub-idiom)
        uint32_t flags = 0;    // 0x100 always | 0x200 from arg 3
        std::string sourceName; // "" = no (resolved) source
    };
    std::optional<CharacterDamageRecord> lastReplicatedDamage;
    uint64_t characterDamageReplicateCount = 0;

    // qte_start (Sec20.9): the named-hook-slot-array registration at base
    // +0x1f00, preset slot 0x18 (0x005e4660 - the SAME setter on_death/
    // on_attack_performed's generic path reach; empty string clears, same as
    // onDeathCallback). Same single-dedicated-field-per-hook-kind convention
    // as onAttackPerformedGenericSlotCallback (slot 8 on this same base).
    std::string qteResultHookCallback;
    uint64_t qteStartReplicateCount = 0; // 0x0060dd60's non-authoritative opcode-0x59 record

    // --- Ranking tranche 05 (spec-lua-api-behaviour.md Sec33), batch 2026-10-02 ---

    // ai_clear_priority_target (Sec33.2): the character's AI forced-target
    // handle (human_ai_data/forced_target_handle), cleared to the null id
    // pair by that call. No Lua-visible query of this field exists among
    // this project's in-scope names; "" is this project's own chosen
    // "null id pair" stand-in (same convention as onTakeDamageCallback's
    // own empty-string default above).
    std::string forcedTargetHandle;

    // ai_set_in_scripted_cover (Sec33.2): AI force-flag byte +0x2bc bit
    // 0x1 - the SAME physical byte ignoreAI above models bit 0x2 of
    // (Sec33.2's own cross-reference). This minimal registry keeps the two
    // bits as independent fields (stated simplification, not a shared bit
    // word - same convention this header already uses throughout, see
    // this struct's own top note) rather than retrofitting ignoreAI into a
    // bit word.
    OpenValue<bool> inScriptedCover{"AI force-flag byte +0x2bc bit 0x1 (in_scripted_cover)",
                                    "spec-lua-api-behaviour.md Sec33.2"};

    // ai_set_in_scripted_cover (false path) / ai_do_scripted_rush (Sec33.2/
    // Sec33.3): the character's current "scripted action" id - CONFIRMED
    // mechanism (a single id field this document's own scripted-action
    // family writes, e.g. action 8 "take cover" elsewhere, 13 here, 22 for
    // rush); the real-world meaning of each id beyond ones already named
    // elsewhere is OPEN/HYPOTHESIS per entry, not claimed here.
    OpenValue<int32_t> scriptedAction{"character scripted-action id", "spec-lua-api-behaviour.md Sec33.2/Sec33.3"};

    // ai_do_scripted_rush (Sec33.3), "with a target" path: the rush
    // target's own name - this project has no position data to copy the
    // real target position into (stated simplification, same "record the
    // raw request, not real physics" convention as MinimapIconRecord/
    // ObjectIndicatorRecord above).
    std::string scriptedRushTargetName;

    // ai_pay_attention_to_position (Sec33.2): this project has no real
    // position/coordinate data for any named object anywhere (stated,
    // project-wide simplification) - records only the resolved source-
    // object's own name and the derived attention mode (7 = true/"attend",
    // 3 = false, CONFIRMED mapping; bit 0x4 of the real mode IS the Lua
    // boolean, per spec).
    std::string attentionSourceObjectName;
    int32_t attentionMode = 0; // 0 = never called; else 7 or 3 (CONFIRMED values)

    // boss_battle_matt_begin (Sec33.5): dword +0xec bit 0x4000 on the
    // character literally named "Matt" - a new, generic per-character bit
    // this header has not previously modeled (real-world meaning OPEN per
    // spec). OpenBits32 since only this one bit is ever confirmed.
    OpenBits32 flagsEc{"character dword +0xec", "spec-lua-api-behaviour.md Sec33.5"};

    // player_revive (Sec32.3): object +0xcc8, 6 = "downed" (the same test
    // Sec7.13 `human_is_downed` uses, cross-referenced there - that function
    // itself is out of this batch's scope); 5 = dead/kill-finalized (Sec34.5,
    // written by the kill finalizer). CONFIRMED (Sec34.5, 2026-10-03): the
    // constructor zeroes this field, so a freshly-spawned character starts
    // at 0 ("alive, ordinary," neither downed nor dead) - seeded at
    // construction (EngineState::getOrCreateCharacter), not left OPEN.
    OpenValue<int32_t> lifeState{"character life-state field (+0xcc8)", "spec-lua-api-behaviour.md Sec7.13/Sec32.3/Sec34.5"};
    // Sec34.5: byte +0xe3 bit 0x01, the second of the two real conditions
    // the 0x0096f4f0 alive/not-gone predicate tests (alongside lifeState ==
    // 5 above) - see CharacterState::notDeadForActionPlay()'s own doc
    // comment for how the two combine. HIGH CONFIDENCE (Sec34.5): clear at
    // construction, inferred from a zero-filled allocation rather than an
    // observed constructor write - seeded at construction all the same
    // (EngineState::getOrCreateCharacter).
    OpenValue<bool> killFinalizedFlagE3{"character dead/kill-finalized flag (+0xe3 bit 0x01)",
                                        "spec-lua-api-behaviour.md Sec34.5"};

    // Attacker/threat-style handle pair, character +0x18f0/+0x18f4 (found
    // 2026-10-09 during spec-lua-api-behaviour.md Sec3.4's own in-place
    // correction - see CharacterState::stateCodeByte501/aiSubStateByte500's
    // own doc comments and set_ignore_ai_flag's stub for what that
    // correction actually fixed; this field is the genuine handle the
    // correction found while tracing where a REAL attacker/threat
    // reference might live, since Sec3.4's own text did not name one).
    // CONFIRMED zeroed unconditionally at character construction (via
    // 0x00954340 -> 0x00942920 - the same zeroing-constructor chain
    // Sec34.1 already traces for lifeState/stateEnum/maxHitPoints above),
    // so a freshly-spawned or script-created character (the m07 kind)
    // always starts at 0 ("no reference") - seeded at construction
    // (EngineState::getOrCreateCharacter), not left OPEN. CONFIRMED
    // cleared mutually on death by the SAME kill-pipeline finalizer
    // (0x009a7610) killFinalizedFlagE3 above already models - see
    // character_kill's own stub (lua_spec_confirmed_stubs_b30_
    // tranchef_camera.cpp) for where this is applied. Its only confirmed
    // consumer anywhere in this project is the already-documented native
    // human_being_revived_by (Sec54.3) - the "reviver handle" (who can
    // revive this character). No writer that sets it nonzero during
    // normal gameplay (taking damage, engaging/being engaged) was found -
    // whether this field doubles as a genuine combat attacker-reference or
    // is purely a reviver handle stays OPEN; do not assume a combat-
    // engagement writer exists for it without further tracing. It is NOT
    // the same thing as the former (removed) attackerThreatRef field above
    // and must not be read by set_ignore_ai_flag's own gate.
    OpenValue<int64_t> reviverHandle{"character attacker/threat-style reviver handle (+0x18f0/+0x18f4)",
                                     "spec-lua-api-behaviour.md Sec3.4 (2026-10-09 correction)/Sec54.3"};

    // character_kill (Sec3.15, 0x00a41a60, cleared 2026-10-08/tranche F).
    // CONFIRMED: resolves via the SAME generic/#PLAYER#-then-fallback chain
    // as on_vehicle_destroyed/character_is_dead (playerClassChainResolves()
    // primary, objectResolves() fallback), then routes a FIXED lethal-
    // damage constant (-1.0f) through the shared generic damage-application
    // routine (0x0096fa20) with a flags word carrying bit 0x100 alone when
    // arg 2 is true (tranche-F resolution: no conflict with Sec20.32's
    // different caller). This project does not simulate 0x0096fa20's own
    // ~5,200-instruction body (hit-point math, notoriety/network-authority
    // checks, ragdoll impulse) - "lethal" is modeled as deterministic: the
    // call always results in death (lifeState -> 5, the SAME value
    // character_is_dead/player_revive already read as "dead/kill-
    // finalized"), since the constant's own name and the entry's own "not a
    // bypass" framing both read as "guaranteed death via the normal
    // pipeline," not a possibly-survivable hit. CHOSEN, not independently
    // re-derivable from a static read of 0x0096fa20 itself (out of scope).
    // arg 2's own precise real-world meaning stays OPEN (the bit itself is
    // CONFIRMED, its purpose is not) - recorded test-observably, not acted
    // on beyond that.
    uint64_t characterKillCallCount = 0;
    bool lastCharacterKillArg2 = false;
    // arg 3 (death-reaction/animation name, default absent): looked up in
    // the SAME animation-state name->id table set_animation_state (Sec7.11)
    // uses (EngineState::lookupAnimationStateId()), only once the
    // character is confirmed dead - CONFIRMED gate, HIGH CONFIDENCE lookup
    // table reuse per Sec3.15's own citation of Sec7.11's table.
    int32_t lastDeathReactionAnimationId = -1;
    uint64_t deathReactionDispatchCount = 0;
    // arg 4 (optional boolean, default true - CONFIRMED opt-in: the
    // finalizer runs only when explicitly false). CONFIRMED finalizer gate:
    // only when lifeState is currently 6 ("downed," the same value
    // human_is_downed/player_revive already test) does the finalizer's own
    // dead-state writer (0x00952760) run, writing the SAME literal 5
    // lifeState already uses; the finalizer's own "clears the character's
    // threat/attacker-reference fields" effect has no other in-scope reader
    // in this project, so only that it ran is recorded.
    uint64_t characterKillFinalizerCount = 0;

    // Test-observable count of successful player_revive calls (downed ->
    // processed). The real post-revive life-state value is not stated by
    // Sec32.3 (HIGH CONFIDENCE only that the character is no longer downed
    // afterward) - not modeled here, to avoid guessing a numeric value the
    // spec itself doesn't give.
    uint64_t reviveCount = 0;

    // npc_go_idle (Sec32.4): count of AI-orders-sub-object resets (+0x510:
    // clears several handles, zeroes several fields, resets 22 fixed-size
    // slots - not individually modeled, only that a reset occurred is
    // counted).
    uint64_t aiOrdersResetCount = 0;

    // --- turn_invulnerable (spec-lua-api-behaviour.md Sec3.7, re-touched by
    // ranking tranche 14's own Sec46.1 bit-conflict resolution), batch
    // 2026-10-02 -------------------------------------------------------

    // Sec3.7: object +0x1c98 bit 0x20 ("invulnerable", 0x00946190) and bit
    // 0x200 ("always apply player damage", 0x00946300). CONFIRMED (Sec3.7's
    // own text): "Both setters are the SAME record-and-replicate idiom
    // ...not a dead branch" - the double-gate (0x008ae480/0x008837a0) is
    // modeled explicitly here, mirroring VehicleState::forceFlagGatePasses
    // below (Sec30.5's identical shape for the vehicle side of this same
    // function) - a deliberate choice NOT to simplify to "always write
    // directly" the way ai_set_in_scripted_cover's own stated simplification
    // does (Sec33.2), because Sec3.7's text is unusually explicit that this
    // particular gate is real and tested, not a dead/unreachable branch.
    // UPDATE 2026-10-03 (Sec3.7's own single-player resolution, cross-ref
    // Sec6.22): with no co-op session installed, gate 1 (0x008ae480)
    // resolves true on every reachable branch (traced through its own
    // connection-state guard and sentinel fallthrough) and gate 2's
    // (0x008837a0) backing global stays at its zero-fill default, since its
    // one writer anywhere in the binary only ever runs while actively
    // applying an already-received network record - unreachable with no
    // session. So the whole idiom family takes the direct-local-apply
    // branch UNCONDITIONALLY in single player; the network-record fallback
    // is dead code there, not merely rare. CONFIRMED, not CHOSEN: seeded
    // true at construction (EngineState::getOrCreateCharacter), not a
    // default awaiting a test - a test can still .set(false) to exercise
    // the (now known-dead) replicate-only branch for structural coverage.
    OpenValue<bool> forceFlagGatePasses{"character force-flag double-gate (0x008ae480/0x008837a0)",
                                        "spec-lua-api-behaviour.md Sec3.7"};
    OpenBits32 forceFlags1c98{"character force-flag byte +0x1c98 (invulnerable 0x20 / always-apply-player-damage 0x200)",
                              "spec-lua-api-behaviour.md Sec3.7"};
    static constexpr uint32_t kInvulnerableBit1c98 = 0x20u;
    static constexpr uint32_t kAlwaysApplyPlayerDamageBit1c98 = 0x200u;
    // character_prevent_explosion_fling/character_prevent_bumping/
    // character_set_never_catch_fire (Sec7.20/Sec7.26/Sec7.29) - 3 more
    // confirmed bits on this SAME shared field (7 total across Sec3/Sec7).
    static constexpr uint32_t kPreventExplosionFlingBit1c98 = 0x10u;
    static constexpr uint32_t kPreventBumpingBit1c98 = 0x1000u;
    static constexpr uint32_t kNeverCatchFireBit1c98 = 0x4000u;
    // character_prevent_flinching (Sec3.14) / character_allow_ragdoll
    // (Sec3.16), batch 2026-10-08 (the 6-native clearance fast-follow): 2
    // further confirmed bits on this SAME shared +0x1c98 field (9 total).
    // Both CONFIRMED by independent direct disassembly 2026-10-08
    // (resolvers `0x00a281a0`/`0x00945eb0` for Sec3.14, `0x00a281a0`/
    // `0x00945d40` for Sec3.16; both reached via the generic/`#PLAYER#`-
    // chain resolver, same family as turn_invulnerable above, and gated by
    // the SAME forceFlagGatePasses double-gate). kNeverRagdollBit1c98 is
    // stored in the BIT'S OWN sense ("never ragdoll") - CONFIRMED the real
    // setter is called with the LOGICAL NEGATION of the Lua `allow`
    // argument (`test bl,bl; sete dl` at the call site), the same
    // "negated allow_*/enable_*" convention Sec3.16/Sec6.18/Sec12.9 already
    // establish elsewhere in this header.
    static constexpr uint32_t kPreventFlinchingBit1c98 = 0x800u; // Sec3.14 - stored in its own direct sense, no negation
    static constexpr uint32_t kNeverRagdollBit1c98 = 0x80u;      // Sec3.16 - stored as "never ragdoll" (negated from the Lua `allow` argument)

    // --- character_fake_revival_start/_end (Sec45.1, "ranking tranche 13"),
    // batch 2026-10-02 --------------------------------------------------

    // Dword +0xe4 bit 0x10000 ("fake revival in progress", HIGH CONFIDENCE -
    // established together by both functions' own shared no-null-check
    // shape, Sec45.1). A new per-character bit on a new dword, same
    // "generic per-character bit this header has not previously modeled"
    // convention as flagsEc above (Sec33.5).
    OpenBits32 flagsE4{"character dword +0xe4", "spec-lua-api-behaviour.md Sec45.1"};
    static constexpr uint32_t kFakeRevivalInProgressBit = 0x10000u;

    // --- character_take_human_shield_check_done (Sec45.1), batch
    // 2026-10-02 ---------------------------------------------------------

    // The taker's own held-hostage name. Sec45.1's own text: "A character
    // carries two separate hostage-handle fields, one for network-owned
    // characters and one for locally-owned ones" - this project has no real
    // networking layer (stated, project-wide simplification), so only the
    // single, locally-owned field is modeled; CHOSEN, write-only in scope
    // (no in-scope setter populates it - a test sets it directly, same
    // "test sets it directly" convention as CharacterState::currentVehicleName
    // above). Empty = "holds nothing".
    std::string humanShieldHostageName;

    // --- character_hidden (Sec45.1), batch 2026-10-02 (resumed session) ---

    // Bit 0 of +0x3b. CONFIRMED (Sec45.1): "the same bit is the 'skip hidden
    // object' test debris_flow_set_inactive's worker uses" - supports
    // "object is hidden" as the real meaning. NOTE the inverted-from-usual
    // unresolved behaviour lives in the STUB (lua_spec_confirmed_stubs.cpp),
    // not here: "an unresolved name reads as hidden (true), not an error" -
    // this field only ever holds the RESOLVED-case value.
    OpenValue<bool> hiddenFlag{"character hidden flag (+0x3b bit 0)", "spec-lua-api-behaviour.md Sec45.1"};

    // --- flee_to_navpoint (Sec44.5/Sec44.6), batch 2026-10-02 (resumed
    // session) -----------------------------------------------------------

    // CONFIRMED (Sec44.5): queues a per-character AI event (type 0x30) with
    // the navpoint position and a threat handle, "silently dropped if the
    // event pool is empty, the character is dead, or it is a player-class
    // object." This project has no real AI-event free-list/pool to exhaust
    // (same "no generic entity shape invented" boundary this header's own
    // top note states) - the dead/player-class gates ARE modeled (the stub
    // checks them before incrementing), but "pool empty" is not reachable
    // here, so this counter undercounts only in that one, documented way.
    // Plain counter, not OpenValue, matching aiOrdersResetCount's own
    // precedent just above for "a reset/request occurred N times," not a
    // specific record's own content.
    uint64_t fleeToNavpointRequestCount = 0;

    // --- action_play_do (spec-lua-api-behaviour.md Sec22.29), real-hit
    // probe-mode batch (2026-10-03) ---------------------------------------

    // Sec22.29's own text: "if the character resolves but is either forced
    // (arg 4) or currently NOT dead/flagged (the already-established
    // predicate 0x0096f4f0, item 22.3/Sec18.14)". This is the SAME real
    // predicate CharacterState::isAlive above already cites (Sec30.5) -
    // kept as a SEPARATELY SCOPED predicate rather than reused, matching
    // isAlive's own doc comment's explicit reasoning ("a DIFFERENT field
    // from isDeadHighConfidence... a separate call site this predicate is
    // never confirmed to share"): Sec22.29 never confirms this call site
    // shares timing/scope with vehicle_exit_group_check_done's own, so
    // widening isAlive to cover both would be assuming a fact neither
    // section states.
    //
    // UPDATE 2026-10-03 (Sec34.5, closing the 7-mission notDeadForActionPlay
    // blocker Sec22.29's own batch found): the real 0x0096f4f0 body for
    // THIS call site is CONFIRMED - dead if the resolved pointer is null
    // (handled upstream by this project's own objectResolves() resolve
    // step, before this predicate is ever reached), OR lifeState == 5
    // ("kill-finalized," above), OR killFinalizedFlagE3 is set (above).
    // Computed live from those two real fields rather than stored as its
    // own independently-settable flag, so a later write to either (e.g. a
    // future kill-finalizer implementation setting lifeState to 5) is
    // reflected here automatically instead of silently going stale.
    // Throws OpenStateError if either underlying field is itself OPEN
    // (e.g. a test .forget()s one to explore that case) - same discipline
    // as every other computed predicate in this file.
    bool notDeadForActionPlay() const { return lifeState.get() != 5 && !killFinalizedFlagE3.get(); }

    // 0x0096f4f0 alive predicate (spec sync e6691f1, Sec30.5/Sec34.5 round-2 FINAL): alive = !null &&
    // state != 5 && !(byte_e3 & 1). The null case is handled by the resolve step upstream. An explicit
    // isAlive.set() (tests) still wins; otherwise computed live from lifeState/killFinalizedFlagE3 (seeded
    // at construction, so no longer OPEN by default).
    bool aliveByPredicate() const { return isAlive.known() ? isAlive.get() : notDeadForActionPlay(); }

    // get_dist's own shared position-lookup function (0x00a455a0, Sec4.2),
    // reused by several other natives (object_is_in_trigger's arg-2 parser
    // Sec1.12, the vehicle-seated-character redirect Sec20.24). OPEN by
    // default: no spec gives a CONFIRMED spawn position for any object (the
    // save format doesn't carry one either - spec-save-format.md Sec6.6,
    // confirmed 2026-10-03), so this starts unset for every character and
    // stays that way until something writes it (a future teleport/spawn
    // implementation). A read while unset throws OpenStateError, which is
    // the correct, honest outcome for "resolved object, unknown position" -
    // NOT the same case as "name failed to resolve at all", which get_dist
    // handles separately via objectResolves() and its own CONFIRMED
    // FLT_MAX sentinel (Sec4.2), never this field.
    OpenValue<WorldPosition> worldPosition{"character world position (get_dist's own 0x00a455a0 lookup)",
                                            "spec-lua-api-behaviour.md Sec4.2"};

    // --- player_controls_disable / player_controls_enable /
    // player_movement_disable (spec-lua-api-behaviour.md Sec7.22/Sec7.7/
    // Sec7.25), wired to the player control-state stack
    // (spec-player-runtime.md Sec2), batch 2026-10-03 -------------------

    // The byte at object +0x1d99: bit 0x08 "controls disabled" (setters
    // 0x009e2dd0 / 0x009e4940) and bit 0x10 "movement disabled" (0x009e2fa0 /
    // 0x009e4a60). Two further bits of the same byte are documented
    // elsewhere (0x20, Sec27.x; 0x80, Sec31.x) and not modelled here. Kept as
    // two independent OpenValue<bool> fields rather than one bit word - the
    // same per-bit simplification ignoreAI/inScriptedCover already use for
    // the shared byte +0x2bc - so a probe-mode read can go through
    // EngineState::probedFieldOr(). CONFIRMED/HIGH spawn default (spec-
    // player-runtime.md Sec2.10, synced 2026-10-03, after an earlier pass
    // found no spec gave this byte's construction value at all): HIGH
    // CONFIDENCE all-clear (both bits 0) at spawn - no constructor touches
    // +0x1d99 and no initialiser exists, so it rests on the pool allocation
    // being zero-filled, the same caveat already established for
    // lifeState/killFinalizedFlagE3 (Sec34.5). Seeded false at construction
    // (EngineState::getOrCreateCharacter) - a test can still .set()/.forget()
    // to exercise the disabled/OPEN cases directly.
    OpenValue<bool> controlsDisabled{"character byte +0x1d99 bit 0x08 (controls disabled)",
                                     "spec-lua-api-behaviour.md Sec7.7/Sec7.22"};
    OpenValue<bool> movementDisabled{"character byte +0x1d99 bit 0x10 (movement disabled)",
                                     "spec-lua-api-behaviour.md Sec7.8/Sec7.25"};
    static constexpr uint32_t kControlsDisabledBit1d99 = 0x08u;
    static constexpr uint32_t kMovementDisabledBit1d99 = 0x10u;

    // The single gate 0x008addb0 (variant 2) all three setters test: true =
    // apply directly; false = the co-op-session-gated network record
    // (opcode 0x32). CHOSEN (not CONFIRMED - Sec7.7's own text says "which
    // branch dominates in single-player is not established here"): seeded
    // TRUE at construction (EngineState::getOrCreateCharacter), this
    // project's standing "single player only, the co-op path is never
    // taken" simplification for co-op-gated branches, and the only reading
    // under which these setters do anything at all in single player - with
    // no session installed (Sec26.28, coopSession().present == false), the
    // gate-false branch's own session check fails and the call would be a
    // silent no-op. A test can .set(false) to exercise that branch.
    OpenValue<bool> controlLockGatePasses{"character single gate 0x008addb0 (variant 2)",
                                          "spec-lua-api-behaviour.md Sec7.7/Sec7.22/Sec7.25"};

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

    // =====================================================================
    // Batch 4 (spec-lua-api-behaviour.md Sec13+Sec14, ranks ~151-250,
    // native-implementation batch 2026-10-08, 55 names) - see each field's
    // own citation below; full per-native reasoning lives in the stub
    // file's own doc comments
    // (lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp).
    // =====================================================================

    // object_indicator_add_objective_text (Sec13.1): one recorded "an
    // objective-text indicator was requested" call against this resolved
    // object - same minimal "record the raw request, not the real HUD
    // dispatch" convention as MinimapIconRecord/ObjectIndicatorRecord above
    // (0x008f4ef0/0x008f76e0's own bodies are OPEN, not modelled).
    struct ObjectiveTextIndicatorRecord {
        std::string iconKey; // arg 2, mandatory, no nil-gate (CONFIRMED, Sec13.1)
        std::string textKey; // arg 3, mandatory, no nil-gate (CONFIRMED, Sec13.1)
    };
    std::vector<ObjectiveTextIndicatorRecord> objectiveTextIndicators;

    // character_set_no_explosive_panic (Sec13.5): object dword +0x1c9c bit
    // 0x4 - a NEW dword this header has not previously modelled (distinct
    // from +0x1c98/+0xec/+0xe4, all CONFIRMED-distinct offsets elsewhere in
    // this header). Same double-gate shape as the forceFlagGatePasses
    // family (gated on the SAME CONFIRMED-true-in-single-player gate,
    // Sec3.7's own cross-reference, raw-disassembly-confirmed hidden-`this`
    // delegate per Sec13.29 item 1).
    OpenBits32 forceFlags1c9c{"character force-flag dword +0x1c9c (no_explosive_panic bit 0x4)",
                              "spec-lua-api-behaviour.md Sec13.5"};
    static constexpr uint32_t kNoExplosivePanicBit1c9c = 0x4u;

    // ai_scripted_action_is_processing (Sec13.7): the gate 0x008addb0's own
    // real-world identity at THIS call site is explicitly unattributed by
    // Sec13.7's own text ("a check this document has not previously
    // attributed to a specific subsystem") - kept SEPARATELY SCOPED from
    // CharacterState::controlLockGatePasses above (Sec7.7/Sec7.22/Sec7.25's
    // own CONFIRMED-true-in-single-player instance of the SAME literal
    // address) for the same reason CharacterState::isAlive is kept separate
    // from isDeadHighConfidence above: nothing in this spec confirms the
    // two call sites share scope/timing, so merging them would assume a
    // fact neither section states. OPEN until a test sets it (no spec gives
    // THIS call site's own single-player resolution).
    OpenValue<bool> scriptedActionDispatchGatePasses{
        "ai_scripted_action_is_processing's own gate 0x008addb0 (separately scoped from controlLockGatePasses)",
        "spec-lua-api-behaviour.md Sec13.7"};

    // set_team (Sec13.18): object +0x1ca8, the character's current team/
    // faction id - CHOSEN sentinel -1 ("never set"), since no spec gives a
    // real spawn-time default for this byte.
    int32_t currentTeamCode = -1;

    // inv_item_in_slot (Sec13.21): keyed by slot CODE, test-registrable
    // only - this project has no real inventory/item-catalog data anywhere
    // (stated, project-wide gap), so any resolved, LIVE slot's held item
    // name is test-registrable only; absent a registration, the function's
    // own CONFIRMED "none" fallback applies honestly (not a guess: an
    // unresolved item record really does fall back to "none" per Sec13.21's
    // own text). CLOSED 2026-10-09: the slot-NAME table 0x008dc6b0 is now
    // fully recovered (11 names, codes 0-0xa: unarmed/melee/pistol/smg/
    // shotgun/rifle/explosive/special/vehicle/grenade/single_use) - of
    // those, codes 0-8 are LIVE (dispatch to the array-of-8-byte-entries
    // lookup, `0x005fec20`'s default arm) and keyed here by their own code;
    // codes 9 (grenade, hard-coded dead inside `0x005fec20`'s own switch)
    // and 0xa (single_use, real inside `0x005fec20` but never reached by
    // this native's own caller-side switch) are CONFIRMED dead, always
    // "none", and never looked up here. The pre-existing 5 "best-*" literal
    // names (outside the table) add codes 0xd/0xe/0xf (LIVE - the dedicated
    // helpers 0x005ff7a0/0x005ff220/0x005ff680, whose own internal item-
    // selection logic is OPEN/not independently decompiled, same "test-
    // registrable, honest none" treatment) and 0x10/0x11 (CONFIRMED dead).
    // Code 0xc ("currently selected slot") is handled inside `0x005fec20`
    // for OTHER callers but is never produced by any of the 11+5 resolvable
    // Lua slot-name strings, so it is unreachable via this native and not
    // modeled here either.
    std::unordered_map<int, std::string> inventoryBestSlotItemForTesting;

    // ai_set_personality (Sec13.27): the profile-name hash/table-scan
    // helper 0x00507e60's own real selection semantics are OPEN (not fully
    // traced against its implicit context argument) - see EngineState::
    // lookupAiPersonalityProfileCode for the test-only registration hook
    // (mirrors lookupNotorietyTrackCode's own "never-populated object
    // space" precedent, Sec3.6). The further 0x011177e5 gating nuance
    // (Sec13.27's own OPEN item) is not separately modelled - this project
    // always proceeds to the double-gate write once a profile code is
    // resolved, matching the common, CONFIRMED top-level path.
    int32_t aiPersonalityProfileCode = -1; // CHOSEN -1 = "never set"

    // character_collision_enable (Sec13.26): the real reason-coded
    // dispatcher (0x0094f450) is NOT modelled (several unidentified
    // predicates/reason codes, Sec13.26's own OPEN list) - same "record
    // that a call happened, not the real internal dispatch" convention as
    // EngineState::callbacksClearedCount_ (Sec28.17). Per-character count
    // and last-requested value only.
    uint64_t collisionEnableCallCount = 0;
    bool lastCollisionEnableValue = false;

    // character_ragdoll_set_last_valid_position (Sec13.25): a per-character
    // 10-entry rolling history ring buffer - DIFFERENT from the already-
    // registered character_ragdoll_set_last_resort_position AND from
    // character_ragdoll_clear_last_resort_position (Sec13.6, a pure GLOBAL
    // with no per-character field at all - see EngineState::
    // ragdollLastResortPositionClearCount for that one) - see the scoping
    // report's own near-duplicate warning. This project has no coordinate
    // system (stated, project-wide gap) - the resolved destination's own
    // NAME stands in for "the position fields" (same convention
    // VehicleState::chaseTargetName already establishes for an identical
    // gap, Sec9.4).
    std::array<std::string, 10> ragdollValidPositionHistory{};
    int ragdollValidPositionHistoryIndex = 0;

    // action_play_synced_state (Sec14.1): the local-apply path's own
    // "cross-write each actor's own position-pair reference into the
    // other" (0x0095f8d0) - this project has no coordinate system (stated
    // gap), so the partner's NAME stands in, same convention as
    // ragdollValidPositionHistory above.
    std::string syncedActionPartnerName;
    bool syncedActionPaired = false;

    // set_cant_flee_flag (Sec14.26): object byte +0xa bit 0x20 - a
    // DIFFERENT bit of the SAME real byte CharacterState::followerCanAbandon
    // above already models bit 0x02 of (Sec7.6) - kept as an independent
    // field, same "two bits of the same byte, independent fields" precedent
    // ignoreAI/inScriptedCover already establish for byte +0x2bc.
    OpenValue<bool> cantFleeFlag{"character byte +0xa bit 0x20 (cant_flee)", "spec-lua-api-behaviour.md Sec14.26"};

    // set_behavior_set (Sec14.27): the name-table resolver 0x004f27f0's own
    // table contents are OPEN - see EngineState::lookupBehaviorSetCode for
    // the test-only registration hook (same "never-populated object space"
    // precedent as ai_set_personality above); sentinel 0x2c (44) is the
    // CONFIRMED real "no match" fallback. +0xe4 real field.
    int32_t behaviorSetIndex = -1; // CHOSEN -1 = "never set"
    // vtable +0x70 gate (Sec14.27): which of the two apply paths (validated
    // vs. raw-write) ran last - test-observable only; both paths write the
    // SAME observable field in this project's model (the validator/
    // finalizer 0x004f36a0's own internals are OPEN).
    OpenValue<bool> behaviorSetVtableGatePasses{"set_behavior_set's own vtable +0x70 gate",
                                                "spec-lua-api-behaviour.md Sec14.27"};

    // on_ai_do_action_complete (Sec14.7): character hook slot 0x14 (20) -
    // reached ONLY via the plain-character resolve path (EngineState::
    // playerClassChainResolves() false/absent for this name - see the
    // stub's own doc comment). Same single-dedicated-field-per-hook-kind
    // convention as onDeathCallback/onRevivedCallback above.
    std::string onAiDoActionCompleteCallback;

    // player_script_controlled_clear (Sec14.29): object +0x2888 mode field
    // - CHOSEN 0 = "not script-controlled". The companion status bit lives
    // on CharacterState::statusByte28ac just below (bit 0x2).
    int32_t scriptControlledMode = 0;

    // --- Player-category status-byte family (Sec14.3-Sec14.5/Sec14.29,
    // independently confirmed via literal debug-tag strings at every site -
    // Sec14.31's own cross-function census): reached via the generic name/
    // "#PLAYER#" resolver (0x00a280c0), whose handle is used only as a
    // validity gate - this project's flat objectResolves() stands in for
    // that gate (Sec29). Modelled on CharacterState directly (the resolved
    // name is tracked via getOrCreateCharacter(), same simplification
    // CharacterState::controlsDisabled/movementDisabled above already use
    // for an identical "player-category object" case, +0x1d99). ---
    OpenBits32 statusByte28ac{
        "character/player status byte +0x28ac (cant_take_human_shields 0x80 / script_controlled 0x2)",
        "spec-lua-api-behaviour.md Sec14.4/Sec14.29/Sec14.31"};
    static constexpr uint32_t kCantTakeHumanShieldsBit28ac = 0x80u;
    static constexpr uint32_t kScriptControlledBit28ac = 0x2u;
    OpenBits32 statusByte28ad{"character/player status byte +0x28ad (cant_be_downed 0x1 / human_shield_locked 0x8)",
                              "spec-lua-api-behaviour.md Sec14.3/Sec14.5/Sec14.31"};
    static constexpr uint32_t kCantBeDownedBit28ad = 0x1u;
    static constexpr uint32_t kHumanShieldLockedBit28ad = 0x8u;

    // character_has_specific_human_shield (Sec13.14): the "qualifies as
    // currently using a human shield" combined predicate (0x009aa260 AND
    // NOT 0x009aa660) - this project's own minimal stand-in, OPEN until a
    // test sets it (the real byte/offset semantics inside 0x009aa260's own
    // duplicated branches are OPEN per Sec13.14's own text). Reuses
    // CharacterState::humanShieldHostageName (Sec45.1) directly for "the
    // actual currently-held-shield handle" - same real concept (the
    // taker's own held-hostage name), already modelled, not duplicated.
    OpenValue<bool> usingHumanShieldQualifies{
        "character_has_specific_human_shield's own combined gate (0x009aa260/0x009aa660)",
        "spec-lua-api-behaviour.md Sec13.14"};

    // npc_revive (Sec13.11): a DIFFERENT real call chain from player_revive
    // (Sec32.3)'s own reviveCount above (0x009a7c90/0x009a78d0 vs.
    // player_revive's own routine) - kept as its own separately-scoped
    // counters per this header's own "separately scoped, not reused"
    // convention (CharacterState::isAlive's own doc comment).
    uint64_t npcReviveFromDownedCount = 0;
    uint64_t npcReviveFromDeadCount = 0;

    // persona_override_persona_start (Sec14.16): the override's own target
    // persona name - the resolver tail's (0x00462960->0x0046fd00) real
    // table contents are OPEN; same "never-populated object space, test-
    // registrable" precedent as ai_set_personality above (gated only on
    // objectResolves(), not separately modelled here).
    std::string personaOverrideName;

    // --- Batch 3 (2026-10-08), spec-lua-api-behaviour.md Sec11+Sec12 (PCR
    // quick-queries + vehicle/HUD/effect cluster) - see each stub's own doc
    // comment (lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp) for the
    // full per-function citation/reasoning. ---

    // character_set_skydiving_state (Sec12.4, 0x00a45380): CONFIRMED the
    // setter 0x009aee20 is called unconditionally with the numeric state
    // code, no gate - the state code's own real enumeration is OPEN. 0 =
    // never called (this project's own "never set" sentinel, same
    // convention as chaseTargetName's "" on VehicleState, vehicle_state.h).
    int32_t skydivingStateCode = 0;

    // character_set_skydiving (Sec12.13, 0x00a42750): CONFIRMED a true/false
    // argument dispatches to two COMPLETELY DIFFERENT internal functions
    // (0x009ae810/0x009ae150, both OPEN) rather than one setter with a
    // boolean - this project tracks only the observable boolean outcome a
    // test can check, not which of the two internal functions "really" ran.
    bool skydivingOn = false;

    // character_hide (Sec12.5, 0x00a45080): CONFIRMED the SAME visibility-
    // toggle primitive (0x008ccb90) and the SAME "hide" mode-argument pair
    // mesh_mover_hide (Sec9.16, PlacedObjectState::flags082) already uses,
    // here applied to a character instead of a placed object. The attached-
    // object linked-list walk (bit 0x8@+6 class-descriptor gate) is OPEN -
    // this project has no per-character attached-object list anywhere in
    // scope, so only the character's own hide bit is tracked.
    bool hidden = false;

    // set_always_sees_player_flag (Sec12.7, 0x00a5cc80): CONFIRMED resolve +
    // dispatch to an AI-perception setter (0x004dfc80, OPEN internal/hidden-
    // argument convention) - the real construction-time default is not
    // stated anywhere in scope, so CHOSEN false (consistent with this
    // header's "unconfirmed flag defaults false, write-only in scope"
    // convention elsewhere, e.g. npcSuppressPersona above).
    bool alwaysSeesPlayer = false;

    // set_player_can_enter_exit_rc_vehicles (Sec12.27, 0x00a5d390): CONFIRMED
    // third sighting of this document's "stored as the negated sense of an
    // allow_*-style argument" convention (alongside character_allow_ragdoll
    // Sec3.16 and notoriety_set_can_decay Sec6.18) - stored INVERTED, so
    // false here means "can enter/exit RC vehicles" (CHOSEN default, no
    // stated construction value either way).
    bool rcVehiclesBlocked = false;

    // player_parachute_wear_backpack (Sec12.28, 0x00a5a380): the gate this
    // function tests is cited by address as 0x008addb0 - the IDENTICAL
    // address this struct's own controlLockGatePasses above already models
    // as "variant 2," independently CONFIRMED true in single player
    // (Sec3.7/Sec6.22, see controlLockGatePasses's own doc comment). HIGH
    // CONFIDENCE, not independently re-derived for THIS specific call site
    // (per WALLS.md's own caution against asserting shared identity from a
    // bare address match alone) - kept as its OWN field rather than
    // literally reusing controlLockGatePasses, but seeded the SAME way
    // (EngineState::getOrCreateCharacter) for the SAME reason: true selects
    // the direct-local-apply branch (Sec12.28's own text: "the change
    // applies directly and locally ... with no replication at all"); a test
    // can still .set(false) to exercise the dead (in single player) opcode-
    // 0x43 replicate-only branch.
    OpenValue<bool> parachuteBackpackGatePasses{"gate 0x008addb0 for player_parachute_wear_backpack",
                                                 "spec-lua-api-behaviour.md Sec12.28"};
    bool parachuteBackpackWorn = false;

    // =====================================================================
    // Wave 2, Batch W2-3 (2026-10-08), spec-lua-api-behaviour.md Sec20+Sec21
    // ("ranks 351-450 tranche," parts A/C gameplay residue, 57 names) - see
    // each field's own citation below; full per-native reasoning lives in
    // the stub file's own doc comments
    // (lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp).
    // =====================================================================

    // character_set_only_scripted_grabs (Sec20.26) / character_set_counter_
    // on_grabbed_by_player (Sec20.27) / character_set_only_player_grabs
    // (Sec21.8): 3 further confirmed bits on the SAME shared +0x1c98 force-
    // flag word this struct already models above (forceFlags1c98) -
    // TRANSPOSITION HAZARD, flagged explicitly by the orchestrator's own
    // dispatch: kCounterOnGrabbedByPlayerBit1c98 (Sec20.27, address
    // 0x00a42200) is a COMPLETELY DIFFERENT real function from the ALREADY-
    // REGISTERED `character_set_counter_on_grabbed` (Sec28.19, address
    // 0x00a421a0, lua_spec_confirmed_stubs_b02_sec27_28.cpp) despite the
    // near-identical name - that sibling writes CounterOnGrabbed::value[name]
    // below, a totally separate mechanism; THIS bit must never be written by
    // that other stub, and vice versa (see
    // tests/synthetic_luahost_wave2_w23_test.cpp's own dedicated cross-check
    // test).
    static constexpr uint32_t kOnlyScriptedGrabsBit1c98 = 0x2000000u;       // Sec20.26
    static constexpr uint32_t kCounterOnGrabbedByPlayerBit1c98 = 0x4000000u; // Sec20.27 - NOT CounterOnGrabbed, see above
    static constexpr uint32_t kOnlyPlayerGrabsBit1c98 = 0x8000000u;         // Sec21.8

    // character_disable_revival (Sec21.9) / character_disable_human_downed_
    // interactions (Sec20.31): 2 further confirmed bits on the SAME shared
    // +0x1c9c force-flag dword this struct already models above
    // (forceFlags1c9c, Sec13.5's own "no_explosive_panic" bit 0x4) - CONFIRMED
    // distinct bit positions, per each native's own spec text.
    static constexpr uint32_t kRevivalDisabledBit1c9c = 0x20000u;               // Sec21.9
    static constexpr uint32_t kHumanDownedInteractDisabledBit1c9c = 0x40000u;   // Sec20.31

    // npc_hearing_enable (Sec21.21): character's own "human_ai_data" sub-
    // object (+0x610/+0x614 identity pair, not separately modeled - same
    // "the write is real, the sub-object is not separately modeled"
    // convention as several fields elsewhere in this header) own +9 byte,
    // bit 0x10 ("deaf" - CONFIRMED stored as the NEGATION of the Lua
    // `enable` argument, a further instance of the "negated allow_*/
    // enable_*" convention Sec3.16/Sec6.18/Sec12.9 already establish).
    // Double-gated by the SAME forceFlagGatePasses double-gate above
    // (CONFIRMED true in single player).
    OpenBits32 humanAiDataForceFlagsByte9{"character human_ai_data sub-object (+0x610/+0x614) own +9 byte",
                                          "spec-lua-api-behaviour.md Sec21.21"};
    static constexpr uint32_t kDeafBit = 0x10u;

    // npc_combat_enable (Sec3.17), batch 2026-10-08 (the 6-native clearance
    // fast-follow): a SIBLING byte on the SAME "human_ai_data" sub-object
    // humanAiDataForceFlagsByte9 above already models (RESOLVED 2026-10-08
    // by independent direct disassembly: `0x004e0cf0`'s hidden `this` is
    // the character's AI-data sub-object at handle offset `+0x2b0` -
    // identical sub-object npc_hearing_enable's own `0x004e0e70` writes,
    // confirmed by the shared `+0x610`/`+0x614` identity-pair read inside
    // the record-open branch) - but this native's own bit is at the
    // sub-object's `+8` byte (absolute character `+0x2b8`), NOT the `+9`
    // byte npc_hearing_enable uses, so a separate OpenBits32 field is
    // needed (this header has nowhere else modeled a byte-addressable
    // sub-object as two independent OpenBits32 fields rather than one
    // wider one - kept this way deliberately, matching the project's own
    // "the write is real, the sub-object itself is not separately modeled"
    // convention: there is no shared backing storage to alias between the
    // two bytes). kCombatDisabledBit is stored in the bit's OWN sense
    // ("combat disabled") - CONFIRMED the real call site passes the
    // LOGICAL NEGATION of the Lua `enable` argument (same negated-argument
    // convention as kNeverRagdollBit1c98 above). Same forceFlagGatePasses
    // double-gate as every other force-flag field in this struct.
    OpenBits32 humanAiDataForceFlagsByte8{"character human_ai_data sub-object (+0x610/+0x614) own +8 byte",
                                          "spec-lua-api-behaviour.md Sec3.17"};
    static constexpr uint32_t kCombatDisabledBit = 0x10u;

    // npc_refire_rate_override (Sec21.20): bare, ungated, non-replicated
    // direct write to the character's own +0x1512 word field. 0xffff ("no
    // override") CONFIRMED sentinel default.
    uint16_t npcRefireRateOverride = 0xffffu;

    // human_skydive_create_npc_backpack (Sec20.20): capability gate (class-
    // descriptor row +0xa bit 0x2, "can carry a backpack") - this project's
    // flat objectResolves() cannot carry a per-kind descriptor bit (Sec29's
    // "one shared global name map" simplification), so this is its OWN
    // per-character OpenValue, OPEN until a test sets it (no spec gives a
    // real default per character class).
    OpenValue<bool> canCarryBackpack{"character class-descriptor bit (+0xa bit 0x2, 'can carry a backpack')",
                                     "spec-lua-api-behaviour.md Sec20.20"};

    // character_is_jumping (Sec20.28) / character_ignite (Sec20.29): BOTH
    // natives' own spec text independently cites the SAME real offset
    // (+0xccd) tested against the SAME literal value (4) - Sec20.28 calls it
    // the "jump" state, Sec20.29 calls it "downed" - most plausibly one
    // shared movement/action-state enum where value 4 happens to matter to
    // both readers (not independently reconciled this pass, but modeled as
    // ONE shared field rather than two coincidentally-identical ones, since
    // inventing a second field at the identical cited offset would itself be
    // the less faithful choice). OPEN until a test sets it.
    OpenValue<int32_t> movementStateByteCcd{"character movement/action-state byte (+0xccd)",
                                            "spec-lua-api-behaviour.md Sec20.28/Sec20.29"};
    // character_is_jumping's OWN additional pre-check (arg2=true path only):
    // a SEPARATE movement-state byte (+0xccc, CONFIRMED distinct offset from
    // +0xccd above) tested for value 3, AND an exclusion predicate
    // (0x009b9160) independently cross-referenced elsewhere in this same
    // spec batch (Sec21.11) as "+0x16d4 == 3, specifically 'seated in a
    // vehicle'" - modeled as its own dedicated field (NOT shared with
    // CharacterState::currentVehicleName above, a genuinely different real
    // offset/mechanism: a cached vehicle-association handle vs. a bare
    // occupancy-phase enum).
    OpenValue<int32_t> movementStateByteCcc{"character movement-state byte (+0xccc)",
                                            "spec-lua-api-behaviour.md Sec20.28"};
    // character_is_jumping's own "seated in a vehicle" test (Sec20.28/
    // Sec21.11) reuses stateEnum directly (above, +0x16d4) rather than a
    // second, duplicate field for the SAME real byte - see
    // stub_character_is_jumping's own doc comment (lua_spec_confirmed_
    // stubs_b16_wave2_sec20_21.cpp) for the full post-merge correction note.

    // character_is_in_a_driver_seat (Sec21.26): the real body is a 3-hop
    // chain (identity-key hash-bucket lookup against a fixed tag, a cached-
    // vehicle-association compare, then up to 3 separate seat-occupant
    // accessors, NONE of whose own bodies were independently decompiled this
    // pass - OPEN per the spec's own text) with no other in-scope reader of
    // any intermediate value - abstracted to ONE flag, the final comparison
    // outcome, the same "abstract an unconfirmed multi-step chain to one
    // flag" convention CounterOnGrabbed::gatePasses/the vehicle double-gate
    // already establish elsewhere in this header. Gated additionally on
    // CharacterState::currentVehicleName above being known and non-empty
    // (the "trust a known cached association, skip the further OPEN gates"
    // convention get_char_vehicle_name/Sec12.9 already established).
    OpenValue<bool> inDriverSeatSpecifically{"character_is_in_a_driver_seat's own final 3-accessor comparison outcome",
                                             "spec-lua-api-behaviour.md Sec21.26"};

    // character_ignite (Sec20.29): dword +0xe8 bit 0x100 ("on fire") - a NEW
    // dword this header has not previously modelled (distinct from +0x1c98/
    // +0x1c9c/+0xec/+0xe4, all CONFIRMED-distinct offsets elsewhere in this
    // header). The fixed 12-simultaneous-fires cap is modeled at the
    // EngineState level (charactersOnFire_ below), not here.
    OpenBits32 flagsE8{"character dword +0xe8", "spec-lua-api-behaviour.md Sec20.29"};
    static constexpr uint32_t kOnFireBit = 0x100u;
    uint64_t igniteCatchFireReactionCount = 0; // 0x00986920 dispatch count (internals OPEN)
    uint64_t igniteHookNotifyCount = 0;        // 0x009e6510 dispatch count, when a hook partner is present instead

    // character_has_human_shield (Sec20.30): the real body is an intricate
    // multi-field chain (+0x1898/+0x189c, +0x1890/+0x1894, +0x18dc/+0x18dd,
    // +0x18d4, a partner-resolution helper 0x009aa130 never independently
    // decompiled) with no other in-scope reader of any intermediate field -
    // abstracted to ONE flag, same convention as inDriverSeatSpecifically
    // above. OPEN until a test sets it.
    OpenValue<bool> hasActiveHumanShieldRelationship{"character_has_human_shield's own final exempt-bit-negation outcome",
                                                     "spec-lua-api-behaviour.md Sec20.30"};

    // human_enter_downed (Sec20.21): reuses CharacterState::lifeState above
    // (+0xcc8, CONFIRMED 6 = "downed") for the "already downed" pre-check.
    // The authoritative-branch predicate 0x00973d40 has no other in-scope
    // reader - abstracted to one OPEN flag (this project's standing single-
    // player simplification: the non-authoritative network-record branch is
    // dead code here, same as the force-flag double-gate family, so only
    // the authoritative branch's own effect is modeled).
    OpenValue<bool> enterDownedApplyGate{"human_enter_downed's own authoritative-branch predicate (0x00973d40)",
                                         "spec-lua-api-behaviour.md Sec20.21"};
    double lastEnterDownedDuration = -1.0; // raw arg 2, CONFIRMED sentinel default
    uint64_t enterDownedAppliedCount = 0;

    // on_collision (Sec21.1): PRIMARY path only (generic/`"#PLAYER#"`-
    // resolved object, hook code 0x13 at its own +0x1f00 sub-record) - see
    // PlacedObjectState's own doc comment for why the spec's own SEPARATE
    // vehicle-fallback branch is not modeled.
    std::string onCollisionCallback;

    // on_weapon_equip (Sec21.18): hook code 7, the SAME +0x1f00-anchored
    // sub-record as onCollisionCallback above. NO vehicle-fallback branch
    // (CONFIRMED, unlike on_collision).
    std::string onWeaponEquipCallback;

    // inv_item_is_equipped (Sec20.16) / inv_item_get_equipped_item (Sec20.17)
    // / set_script_animation_group (Sec21.15) / inv_item_equip_slot
    // (Sec21.23): all FOUR natives' own spec text cites the IDENTICAL
    // underlying accessor (0x005fe3e0, hidden `this` = character+0x1b78,
    // flag=false) for "the character's own currently-equipped item" - ONE
    // shared field, not four independent ones (Sec21.15's own text: "the
    // SAME accessor item 21.15's own hidden-`this` correction identifies").
    // "" = nothing equipped.
    std::string equippedItemName;

    // inv_has_item (Sec20.18): this project has no real inventory-contents
    // model (same "no generic entity shape invented" boundary this header's
    // own top note states elsewhere) - a plain map, CHOSEN false default
    // ("does not have the item" is the sane zero-default for an unmodeled,
    // empty-by-default inventory), not an OpenValueMap/OPEN refusal, since
    // "has none of anything" is itself a reasonable, labeled CHOSEN starting
    // state, not a genuinely unknown engine value.
    std::unordered_map<std::string, bool> inventoryItemPresence;

    // inv_item_remove_in_slot (Sec21.4) / inv_item_equip_slot (Sec21.23):
    // per-slot contents, keyed by the RESOLVED NUMERIC slot id (not the slot
    // NAME - both natives' own arg 1 is a slot name/selector, converted to a
    // numeric id first, per each native's own text). "" = slot empty. CHOSEN
    // naming note: this field is intentionally named distinctly from any
    // OTHER wave-2 batch's own `inv_item_remove` (a DIFFERENT, non-slotted
    // native, different spec entry, different batch/file entirely) so the
    // two cannot collide even informally.
    std::unordered_map<int32_t, std::string> inventorySlotContentsBySlotId;
    uint64_t equippedCount = 0;                       // inv_item_equip_slot: real re-equip happened
    uint64_t reequipSkippedAlreadyEquippedCount = 0;  // inv_item_equip_slot: new selection == already-equipped item

    // set_script_animation_group (Sec21.15): the character's own +0x178
    // animation-group id (via its own +0x1da8 controller sub-object, de-
    // duplicating setter 0x004b2140 - only written on a real value change,
    // CONFIRMED). -1 CHOSEN ("never set"). animationGroupPropagatedToWeaponCount
    // counts the SAME de-duplicating write applied to the currently-equipped
    // item's own listener (+0xe8), when one is held (equippedItemName above);
    // scriptAnimationGroupReplicateCount counts the UNCONDITIONAL opcode-0x43
    // record this native's own text documents ("no gate check of any kind
    // found anywhere in the body").
    int32_t scriptAnimationGroupId = -1;
    uint64_t animationGroupPropagatedToWeaponCount = 0;
    uint64_t scriptAnimationGroupReplicateCount = 0;

    // npc_use_closest_action_node_of_type (Sec21.2): the character's own
    // +0x16c/+0x170 search-request fields (radius/type id), written only on
    // a successful resolve+type-validate (CONFIRMED). The derived +0x16a
    // internal success byte has no in-scope reader (the pushed Lua boolean
    // is a hardcoded `true`, CONFIRMED NOT the same value - Sec21.2's own
    // "real, confirmed divergence" finding) so it is not modeled; the
    // unconditional opcode-0x43 network record (success-branch-scoped, per
    // this native's own text) is tracked as a plain counter.
    double lastActionNodeRadius = 10.0; // CONFIRMED nil-gated default
    int32_t lastActionNodeTypeId = -1;
    uint64_t actionNodeNetworkRecordCount = 0;

    // action_stop_custom (Sec21.28): class-descriptor row +0xa bit 0x2 -
    // the SAME bit action_play_is_finished's own "+10 bit 0x02" cites
    // (Sec7.21/Sec7.33), per Sec21.28's own text - NOT independently
    // cross-wired to whatever field (if any) that sibling native's own
    // implementation uses (out of this batch's scope to verify/reconcile;
    // kept as its own dedicated field rather than guess at reuse).
    OpenValue<bool> actionStopCustomCapabilityBit2{"character class-descriptor row +0xa bit 0x2",
                                                   "spec-lua-api-behaviour.md Sec21.28"};
    uint64_t actionStopCustomCallCount = 0;
    uint64_t actionStopCustomInternalDispatchCount = 0; // the capability-gated 0x009e3200 dispatch

    // =====================================================================
    // Wave 2, Batch W2-4 (Sec22+Sec24+Sec25 gameplay residue, 78 names,
    // 2026-10-08) - see src/lua_spec_confirmed_stubs_b22_wave2_sec22.cpp and
    // src/lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp's own per-stub doc
    // comments for the full citation/reasoning behind each field below.
    // =====================================================================

    // npc_dont_drop_weapon_on_death (Sec22.12): a NEW byte +0x1e00, joining
    // the already-established "human_force_flags"/"vai_force_flags"/
    // "sync_flags"/"vehicle m_force_flags" double-gate tag-pair families
    // with a FOURTH, byte-confirmed "npc" (0x01167294) + "force_flags..."
    // family - same double-gate (variant 1, forceFlagGatePasses above)
    // shape, a new field because it is a genuinely different byte from
    // forceFlags1c98/forceFlags1c9c above.
    OpenBits32 npcForceFlags1e00{"character npc-force-flag byte +0x1e00 (dont_drop_weapon_on_death bit 0x20)",
                                 "spec-lua-api-behaviour.md Sec22.12"};
    static constexpr uint32_t kDontDropWeaponOnDeathBit1e00 = 0x20u;

    // set_suppress_synced_melee_attacks_flag (Sec24.16) / player_super_powered
    // (Sec24.19): 2 more confirmed bits on the ALREADY-MODELLED forceFlags1c98/
    // forceFlags1c9c shared bytes above (Sec3.7/Sec7.20/Sec7.26/Sec7.29) - new
    // tag-pair members "human_force_flagssuppress_synced_melee_attacks" and
    // "human_force_flagssuper_powered", same double-gate shape, no new field
    // needed beyond these 2 constants.
    static constexpr uint32_t kSuppressSyncedMeleeAttacksBit1c98 = 0x40000000u;
    static constexpr uint32_t kSuperPoweredBit1c9c = 0x80000u;

    // npc_weapon_spread (Sec24.26): a NEW float field +0x1e04, SAME "npc"+
    // "spread_multiplier" double-gate family as npcForceFlags1e00 above (a
    // different byte again) but carrying a FLOAT payload, not a bit - this
    // project's own established "non-boolean double-gate payload" shape
    // (cf. VehicleState::explosionDamageMultiplier below, Sec24.6). CHOSEN
    // default 1.0 (Sec24.26's own nil-gated-argument default, byte-confirmed
    // 0x01117a4c) - plain, not OpenValue, since the double-gate always
    // passes in single player (Sec3.7) and no in-scope reader needs a
    // refusal here.
    double npcSpreadMultiplier1e04 = 1.0;

    // human_set_force_holster (Sec25.9): writes DIRECTLY on the resolved
    // character handle itself (CONFIRMED, a real asymmetry against human_
    // set_always_sprint below, which instead targets the +0x2b0 AI-data
    // sub-object) - CHOSEN default false, no in-scope reader beyond this
    // batch's own test.
    bool forceHolster = false;

    // human_set_always_sprint (Sec25.10) / character_set_can_take_human_
    // shield (Sec25.26): 2 more confirmed bits on the per-character AI-data
    // sub-object at +0x2b0 (joining alwaysCowerFlag above, Sec15.4, and the
    // ignoreAI/actionOverrideId fields at the TOP of this
    // struct, which already share this exact +0x2b0 base) - CHOSEN
    // default false/true respectively (no CONFIRMED construction value for
    // either), plain bools, same "no in-scope reader beyond this batch's own
    // test" convention. character_set_can_take_human_shield's own real
    // write is CONFIRMED stored in the NEGATED sense (Sec25.26) - this
    // project keeps the NATURAL (non-negated) "can take shield" sense in
    // this field instead, diverging from the real bit's negated storage;
    // the stub's own write site negates the Lua argument when writing the
    // real bit, so a test reading THIS field sees the natural sense while
    // the real engine's own bit would read inverted (post-merge correction -
    // this comment previously, wrongly, said the negation happens "only at
    // the stub's own write site" as if this field itself stayed negated;
    // it does not - a fresh reviewer confirmed no negation happens anywhere
    // against this field, only (correctly) at the real-bit write site).
    bool alwaysSprintAiData2b0 = false;
    bool canTakeHumanShield = true; // CHOSEN default true ("can" by default)

    // character_release_human_shield (Sec22.26): reuses humanShieldHostageName
    // above directly (already modelled, Sec45.1, "the taker's own held-
    // hostage name") as the single "is this character currently holding a
    // hostage" state; this project does not model the real shield-state
    // byte's own full enum (+0x18d4, no in-scope reader needs it) - only
    // per-branch call counts, CHOSEN record-the-request convention (same as
    // MinimapIconRecord/ObjectIndicatorRecord elsewhere in this header).
    uint64_t humanShieldForcedEjectCount = 0;
    uint64_t humanShieldDelayedReleaseCount = 0;
    uint64_t humanShieldNormalReleaseCount = 0;

    // is_rappelling (Sec22.16): 2 NEW fields, +0xcc8 (state code, true only
    // at 0x12/18) and +0xcce (a companion byte, true only at '#'/0x23) - no
    // CONFIRMED construction default given anywhere in Sec22.16's own text;
    // CHOSEN to values that never match the predicate absent a test
    // explicitly configuring them, matching this project's established
    // "zero-filled, no writer in scope -> doesn't trip" convention (e.g.
    // CharacterState::lifeState/killFinalizedFlagE3 above, Sec34.5).
    int32_t rappelStateCodeCc8 = 0;
    char rappelCompanionByteCce = 0;

    // check_animation_state (Sec22.25): a NEW field +0x1cc4 ("the character's
    // own cached current animation-state id"), compared against EngineState::
    // lookupAnimationStateId(stateName) - the SAME 0x004bf810 shared name->
    // id table stand-in Sec7.11/Sec7.17/Sec25.24 already share (EngineState::
    // registerAnimationStateIdForTesting/lookupAnimationStateId above).
    // CHOSEN default (-2) deliberately distinct from that shared table's own
    // "-1, not found" sentinel, so an unconfigured character can never
    // spuriously equal an unconfigured/unregistered state name's own lookup
    // result.
    int32_t currentAnimationStateId = -2;

    // check_script_animation_state (Sec25.24): a pure READER of
    // scriptAnimationStateId/scriptNpcBound above (already modelled for
    // set_script_animation_state, Sec7.17) - no new field needed; see the
    // stub's own doc comment for the exact field-level match this closes.

    // cower (Sec22.22): per-character "cower" AI reaction state - the
    // target/threat position is NOT modelled (no coordinate system
    // anywhere in this project, the stated project-wide gap), only the
    // CONFIRMED structural facts: whether the state actually took, who the
    // threat was, and the 2 packed boolean modifiers (+0x4a5/+0x4a6, real-
    // world meaning HYPOTHESIS per Sec22.22's own text).
    bool cowering = false;
    std::string cowerThreatName;
    bool cowerArg3 = false;
    bool cowerArg4 = false;
    uint64_t cowerCount = 0;

    // wander_start (Sec24.4): CHOSEN "record the request, not the real
    // position/orientation arithmetic" convention (same as HelicopterFlyTo::
    // Request elsewhere in this header) - default height -1.0 matches
    // Sec24.4's own nil-gated-argument default (byte-confirmed 0x012a2d54).
    std::string wanderTargetName; // "" = sourced from the character's own default (+0x2b0), per Sec24.4
    double wanderHeight = -1.0;
    uint64_t wanderStartCount = 0;

    // player_supress_falling_scream (Sec24.18): bit 0x80 of +0x1d99 -
    // CONFIRMED "always-set, never-clear from this one entry point" (a
    // real clearer exists elsewhere in the binary, 0x00e0e919, out of this
    // batch's scope per Sec24.18's own correction). CHOSEN default false.
    bool fallingScreamSuppressed = false;

    // npc_unholster_best_weapon (Sec24.27): dispatches through the already-
    // catalogued +0x1b78 "equipment" sub-object (a THIRD confirmed consumer,
    // Sec24.27's own text) - 0x00600720's own ~2,900-instruction body is OPEN
    // (not independently decompiled), so only that a resolved character's
    // dispatch ran is counted, honestly.
    uint64_t unholsterBestWeaponCount = 0;

    // human_riding_trigger_end (Sec25.11): a predicate-gated end action; this
    // project has no in-scope setter for the real predicate (0x009a8430), so
    // it stays a plain, test-settable bool (CHOSEN default false - "not
    // currently riding/triggered", matching the common "never configured"
    // case).
    bool ridingTriggerActive = false;
    uint64_t humanRidingTriggerEndCount = 0;

    // inv_item_is_firearm_equipped (Sec25.8): a NEW per-character query flag,
    // CHOSEN default false (0x009ba7e0's own internal check is OPEN).
    bool firearmEquipped = false;

    // force_ai_mode_check_done (Sec25.19): the per-character +0x4aa pending-
    // request tag, CHOSEN plain bool stand-in (no in-scope setter for the
    // real 16-bit tag or the shared queue-lookup helper 0x006f63c0, whose own
    // polarity is flagged OPEN/tension against Sec7.28 by the spec itself) -
    // default false ("no pending tag"), matching the common "never issued a
    // force-AI-mode request" case.
    bool forceAiModePendingTagSet = false;

    // character_is_released (Sec25.28): the per-character vtable+0x70 "is
    // bound/possessed by something" predicate this project does not model a
    // real vtable for - CHOSEN plain bool, default false ("not bound"), so an
    // unconfigured, resolved character reads as "released" (true) by
    // default, matching Sec25.28's own stated "including on any resolve/
    // liveness failure -> true" default-safe direction.
    bool boundOrPossessed = false;

    // character_set_combat_ready (Sec25.25): single-gate (0x008addb0,
    // reusing forceFlagGatePasses as this project's one shared double-gate/
    // single-gate stand-in, same simplification this header uses throughout)
    // "combat ready" state plus an optional numeric mode parameter. CHOSEN
    // defaults (false/0) - no CONFIRMED construction value stated.
    bool combatReady = false;
    int64_t combatReadyMode = 0;
    uint64_t combatReadySetCount = 0;

    // =====================================================================
    // Wave 2, Batch W2-7 (2026-10-08): spec-lua-api-behaviour.md Sec38-60 -
    // see src/lua_spec_confirmed_stubs_b27_wave2_spawn_world_audio.cpp.
    // =====================================================================

    // world_despawn_all_pedestrians (Sec46.3): same "deferred despawn,
    // protected entries skipped" shape as VehicleState::despawnPlayerOccupied
    // et al. above - CHOSEN false defaults (an ordinary ambient pedestrian
    // is not protected).
    bool despawnProtected = false;
    bool despawnedPendingDelete = false;

    // force_throw_do / force_throw_char_do (Sec55.4): "requires the
    // character to have a throwable item equipped/held." This project has
    // no real inventory/equip model (stated gap, same class as every other
    // "no item-table" boundary elsewhere) - test-settable, OPEN until set;
    // a real call against a character with no override throws
    // OpenStateError (honest refusal, not a fabricated "always equipped").
    OpenValue<bool> hasThrowableItemEquipped{"character has a throwable item equipped/held (Sec55.4)",
                                            "spec-lua-api-behaviour.md Sec55.4"};

    // =====================================================================
    // Wave 2, Batch W2-2 (spec-lua-api-behaviour.md Sec17+Sec18, 2026-10-08)
    // - see src/lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning.
    // =====================================================================

    // set_perfect_aim (Sec17.3): an EIGHTH confirmed bit on the shared
    // +0x1c98 flags field (forceFlags1c98 above already models 0x20/0x200/
    // 0x800/0x80/0x10/0x1000/0x4000) - gated by the SAME forceFlagGatePasses
    // double-gate (Sec3.7).
    static constexpr uint32_t kPerfectAimBit1c98 = 0x100u;
    // character_set_always_combat_ready (Sec18.17): a further bit on the
    // SAME +0x1c98 field.
    static constexpr uint32_t kAlwaysCombatReadyBit1c98 = 0x1u;
    // set_suppress_synced_attacks_flag (Sec18.22): a further bit on the
    // SAME +0x1c98 field.
    static constexpr uint32_t kSuppressSyncedAttacksBit1c98 = 0x20000000u;
    // character_make_priority_target (Sec18.18): a further bit on the SAME
    // +0x1c98 field, PLUS an always-written (regardless of the bit's own
    // value) squared-radius field at +0x1ca4 - CONFIRMED asymmetry: the
    // local-apply path stores the radius SQUARED; the network-replicate
    // path (not modeled beyond EngineState::replicateStateChange) carries
    // it un-squared (Sec18.18's own flagged, unresolved receiving-side
    // question).
    static constexpr uint32_t kPriorityTargetBit1c98 = 0x400u;
    double priorityTargetRadiusSquared = 0.0;

    // character_set_never_fall (Sec17.10) / character_set_immune_to_
    // explosion_vomit (Sec18.16): two further bits on the EXISTING shared
    // +0x1c9c dword (forceFlags1c9c above already models bit 0x4,
    // no_explosive_panic, Sec13.5).
    static constexpr uint32_t kNeverFallBit1c9c = 0x10000u;
    static constexpr uint32_t kImmuneToExplosionVomitBit1c9c = 0x1000u;

    // character_set_attack_multiplier (Sec17.11): object +0x1cc0, a raw
    // value field (not a bit) - gated by the SAME double-gate as every
    // other force-flag setter in this struct. CHOSEN default 1.0 ("no
    // scaling" - no stated construction value is given anywhere in scope).
    double attackMultiplier = 1.0;

    // set_attack_enemies_flag (Sec17.5): structurally identical to
    // set_attack_player_flag's own documented "two flag locations, only one
    // replicates" split (attackPlayerBitAc/attackPlayerSecondaryGatePredicate/
    // attackPlayerOnSightBit9 above, Sec7.10) - this is its own, independent
    // sibling instance, NOT a re-use of those fields (a DIFFERENT bit of the
    // SAME +0xac byte, and a DIFFERENT derived "ai_data" sub-object byte,
    // +9 bit 0x2 rather than +9 bit 0x1). The +0xac bit write is CONFIRMED
    // unconditional (no gate at all, written before the vtable +0x68
    // predicate below is even evaluated).
    bool attackEnemiesBitAc = false;
    OpenValue<bool> attackEnemiesSecondaryGatePredicate{
        "character vtable +0x68 predicate for set_attack_enemies_flag (unidentified)",
        "spec-lua-api-behaviour.md Sec17.5"};
    // The derived "human_ai_data" sub-object's own +9 byte - a DIFFERENT
    // byte from aiDataFlags0xa below (+0xa, set_cant_cower_flag, Sec17.20).
    // Gated by forceFlagGatePasses directly (the SAME 0x008ae480/0x008837a0
    // pair, Sec3.7 - the sub-object base differs but the gate predicate
    // itself is the one shared project-wide primitive).
    OpenBits32 aiDataFlags9{"character derived human_ai_data sub-object byte +9",
                            "spec-lua-api-behaviour.md Sec17.5"};
    static constexpr uint32_t kAttackEnemiesOnSightBit9 = 0x2u;

    // set_cant_cower_flag (Sec17.20): object +0xa bit 0x8 - a THIRD
    // confirmed bit on that byte (followerCanAbandon's own 0x2, Sec7.6;
    // cantFleeFlag's own 0x20, Sec14.26) - kept as its own plain field (same
    // "independent fields on the same byte" convention those two
    // establish), gated by forceFlagGatePasses (identity +0x610/+0x614,
    // same "human_ai_data" sub-object delegate family).
    bool cantCowerFlag = false;

    // player_force_vehicle_seat (Sec17.6): two independent global "forced
    // seat" state fields (Sec17.6's own flagged item: "not gated on each
    // other"). The seat-index half is per-target (this project's own
    // CharacterState, gated by forceFlagGatePasses); the vehicle-identity
    // half is EngineState::forcedSeatVehicleName() below (tracked globally,
    // since the spec's own text runs it "independently of whether the
    // target resolved" - its own real `this` object is not confirmed to be
    // the same target character). CHOSEN -1 = "never set".
    int32_t forcedVehicleSeatIndex = -1;

    // character_set_persona (Sec17.9): the persona name/id this project
    // tracks directly as a string (no real name->id hash table, 0x0046fd00,
    // modeled - same "no fixed id table" class of gap as several other
    // name-lookup natives in this header). Gated by controlLockGatePasses
    // directly (the SAME single-gate 0x008addb0 "variant 2" the literal `0`
    // argument at this call site always satisfies, per the stub's own doc
    // comment).
    std::string currentPersonaName;
    uint64_t personaApplyCount = 0;

    // character_ragdoll (Sec17.12): no real physics system in this project
    // (same stated gap as vehicle_chase's own spotlight/position gaps) -
    // only the raw request is tracked. -1 = never called; 0 = last call
    // cleared the ragdoll.
    int64_t ragdollDurationMs = -1;
    double ragdollImpulseMagnitude = 0.0;
    bool ragdollImpulseWasDirectional = false; // true = a resolved source object drove the impulse direction
    uint64_t ragdollStartCount = 0;
    uint64_t ragdollClearCount = 0;

    // character_add_prop (Sec17.13): no real worn-prop object graph in this
    // project - only the resolved, successfully-attached prop names are
    // recorded (same "record the request" convention as
    // ObjectiveTextIndicatorRecord above). Gated on EngineState::
    // propDefinitionResolves() (collapsing the two real lookups,
    // 0x006cf0c0/0x00904c10, into one flat predicate).
    std::vector<std::string> attachedProps;

    // inv_item_remove (Sec17.25): no real inventory list in this project
    // (write-only in scope - nothing reads a character's inventory back) -
    // only that a removal was attempted, and via which path, is counted.
    // Gated by forceFlagGatePasses (variant 3, single-gate-0x008ae480-only -
    // the SAME shared predicate npc_leash_remove/npc_leash_to_nav already
    // reuse, Sec7's own front matter: "0x008ae480 itself is not an
    // independent predicate... the SAME underlying function", CONFIRMED
    // true in single player).
    uint64_t inventoryItemRemoveAppliedCount = 0;
    uint64_t inventoryItemRemoveReplicateCount = 0;

    // set_civilians_flee (Sec17.4): the per-civilian "flee toward/away from
    // anchor" directive the mass-update loop applies via the shared +0xc68
    // AI-directive sub-record (CONFIRMED NOT vehicle-specific, Sec17.31's
    // own cross-function note) - record the request, not real physics (no
    // coordinate system anywhere in this project). fleeAnchorName empty
    // with fleeActive true = the "self-position" variant (no real anchor
    // resolved); fleeActive false = the "clear" variant.
    bool fleeActive = false;
    std::string fleeAnchorName;
    uint64_t fleeDirectiveApplyCount = 0;
    uint64_t fleeDirectiveReplicateCount = 0;

    // ai_do_scripted_take_cover (Sec18.3): the "is already fleeing/covering
    // toward the anchor" state test (0x00540b90) - collapsed to one OPEN
    // predicate per character (no spec-given construction default). Reuses
    // pendingScriptedMoveDestination (Sec3.11) directly for the resolved
    // anchor's own +0x4b0/+0x4b4 handle field - CONFIRMED the SAME physical
    // field, not a separate one.
    OpenValue<bool> alreadyCoveringTowardAnchor{"ai_do_scripted_take_cover's own 'already covering' test (0x00540b90)",
                                                "spec-lua-api-behaviour.md Sec18.3"};
    uint64_t scriptedTakeCoverDispatchCount = 0; // 0x004f4d00(char, 8, 0) - fires on EITHER success path

    // action_stop_synced_state (Sec18.4): reuses syncedActionPartnerName/
    // syncedActionPaired (Sec14.1) directly for the "stop" side of the SAME
    // pairing action_play_synced_state establishes. The global replication-
    // mode flag (0x024d4461) this native also tests is EngineState::
    // globalReplicationModeFlag() below - CHOSEN default false (single-
    // player-only project stance), so this always takes the documented
    // "normal local path" branch.
    bool lastSyncedStopArg = false; // raw arg 2, test-observable only
    uint64_t syncedStopLocalApplyCount = 0;

    // action_play_synced_is_finished (Sec18.5): collapses the real per-
    // object animation-controller query (+0xd24->+0x10, then 0x00487e80's
    // -1 "no active state" test, else an elapsed-time compare against
    // tag 0x1d54) into two OPEN predicates per character - no spec-given
    // construction default for either.
    OpenValue<bool> syncedAnimControllerActive{"character animation-controller has an active synced state",
                                                "spec-lua-api-behaviour.md Sec18.5"};
    OpenValue<double> syncedAnimElapsedTime{"character synced-action elapsed time (controller tag 0x1d54)",
                                            "spec-lua-api-behaviour.md Sec18.5"};

    // set_animation_set (Sec18.8): the set-name itself is tracked directly
    // (no real name->id table, 0x004b16c0, modeled). The "re-apply in-
    // progress blend" branch (0x004b21a0 chain) only fires when the
    // controller already has an active instance - CHOSEN false default (no
    // stated construction value; avoids an unnecessary OPEN refusal on the
    // common fresh-character case).
    std::string animationSetName;
    bool hasActiveAnimationInstance = false;
    uint64_t animationSetReapplyCount = 0;

    // player_release_human_shield (Sec18.9): a genuinely rich state machine
    // this project only models at the observable-branch level (no real
    // position/velocity data for the "forcible eject" offset, no timer for
    // the 500-2000ms wait). "isLocalAuthority" CHOSEN true (single-player-
    // only project stance, same reasoning as every other local/network
    // authority split in this header). shieldStateByte18d4 CHOSEN 0 ("no
    // shield" - the natural default, distinct from the CONFIRMED active
    // value 1 and the CONFIRMED cooldown value 4 the setter itself writes).
    bool humanShieldReleaseIsLocalAuthority = true;
    int32_t humanShieldStateByte18d4 = 0;
    OpenValue<bool> humanShieldNormalReleaseGatesPass{
        "player_release_human_shield's own two further gates (0x0091f5e0/0x009feeb0)",
        "spec-lua-api-behaviour.md Sec18.9"};
    uint64_t humanShieldForcibleEjectCount = 0;
    // humanShieldDelayedReleaseCount/humanShieldNormalReleaseCount are NOT
    // re-declared here - this stub's own doc comment (lua_spec_confirmed_
    // stubs_b21_wave2_sec17_18.cpp) explicitly cross-references
    // character_release_human_shield (Sec22.26, above) as the same
    // underlying counters, not a separate pair.
    uint64_t humanShieldReplicateCount = 0;

    // on_incapacitated (Sec18.11): character-kind hook base +0x68/+0x6c,
    // slot 1 - same single-dedicated-field-per-hook-kind convention as
    // onDeathCallback/onRevivedCallback above.
    std::string onIncapacitatedCallback;

    // player_is_rc_driver (Sec18.10): see EngineState::isRcDriverPredicate()
    // below - kept at the EngineState level (keyed by character name) since
    // the resolved VEHICLE identity is also part of the real test and this
    // project has no per-character vehicle-association field to hang it on.

    // player_actions_disable (Sec18.27): reuses the EXISTING, previously
    // write-only actionsEnableBit20_1d99 field (Sec15.21/Sec27.x) directly -
    // this IS that field's own first real reader/writer pass. Gated by
    // controlLockGatePasses (the SAME single-gate 0x008addb0 "variant 2"
    // family, CONFIRMED true in single player). Writes `.set(true)` to
    // DISABLE (resolved 2026-10-08: the field's own "Enable" name is
    // misleading - the bit is actually disabled-sense, per spec-player-
    // runtime.md Sec2.10 "Question B" - see this field's own doc comment
    // above).
    uint64_t playerActionsDisableReplicateCount = 0;

    // --- Wave 6 spec sync (2026-10-09), spec-player-runtime.md Sec15.3a
    // ("Generic damage application, death, and ragdoll hand-off"): closes
    // stub_character_damage's own former "0x0096fa20 authoritative branch"
    // OpenStateError (src/lua_spec_confirmed_stubs_b15_wave2_core_
    // dispatchers.cpp). Health storage itself is the EXISTING maxHitPoints/
    // currentHitPoints pair above (+0x1cac/+0x1cb8, Sec7.12/Sec7.31) -
    // re-confirmed by Sec15.3a, not re-declared here. Sec15.3a describes
    // +0x1cb8 as "(float)"; this project keeps it int32 per Sec7.12's own
    // disassembly-confirmed text ("stored as a plain integer, converted to
    // float on read") - read as: the 0x0094cfc0/0x0094d520 call boundary
    // operates in float, the underlying storage stays the int32 Sec7.12
    // already pinned. Not reconciled further - a genuine, narrow tension
    // between two independently-confirmed passes, flagged rather than
    // silently resolved either way.

    // +0x1584, CONFIRMED: "a 'last damage magnitude' scratch field,
    // max(-delta, 0) of the most recent HP-reducing change." CONFIRMED zero
    // at spawn ("the base constructor zeroes these fields"). Updated by the
    // real damage-apply math in stub_character_damage; no other writer in
    // this project's scope.
    double lastDamageMagnitude = 0.0;

    // The sink's own resistance-scale modifier (Sec15.3a's own "correction
    // to the prior 'caller always computes the final amount' phrasing"):
    // "an integer percentage read off attacker+0x1cac, the same max-health
    // field reused generically here." CONFIRMED the modifier itself exists
    // and is multiplicative; OPEN what populates it for an ORDINARY
    // attacker in the real engine's own generic/reused-layout sense. This
    // project's CharacterState is explicitly "character"-shaped only (this
    // header's own top note), and an ordinary attacking character's own
    // maxHitPoints genuinely means ITS OWN hit-point cap (typically in the
    // hundreds) - reusing it directly as a percentage here, the way the
    // real engine's generic/reused-offset convention would for some OTHER,
    // non-character "kind" of instigator object, would fabricate wildly-
    // wrong scaling (a 400-HP attacker producing 400% damage) this
    // project's own model gives no evidence for. CHOSEN instead: a
    // dedicated, neutral-default (100 = no scaling) field on the
    // INSTIGATOR's own CharacterState, left for a future pass (or a test)
    // to drive non-neutral per-source resistance scaling once a real
    // producer is found - never wired to maxHitPoints.
    int32_t instigatorDamageScalePercent = 100;

    // The sink's own "detecting a lethal hit" kill-eligibility predicate,
    // 0x0096f990 (spec-player-runtime.md Sec15.3a; spec-weapons-runtime.md
    // Sec2.3's own "unidentified predicate", shared with the melee
    // finisher/takedown timing). CONFIRMED the gate exists and must pass
    // before the death-transition (0x00970e70) side-effects below run;
    // OPEN what it actually tests. CHOSEN pass-through default true (so an
    // ordinary lethal hit reaches death normally) - a test can set this
    // false to exercise "health forced to 0, but the death-transition/
    // ragdoll/finalize side-effects below do not run" as a distinct,
    // separately-observable outcome.
    bool lethalHitKillEligible = true;

    // Counts a reach of the death transition 0x00970e70's "reached
    // normally (health already at zero)" branch - the real death
    // side-effects (a virtual dispatch through the victim's own vtable
    // +0x70, a Havok on-demand clear-position raycast via 0x0075d7e0, and a
    // push-back vector). This project's sr3luahost tier has no polymorphic
    // engine-object/vtable system and no physics/Havok integration (that is
    // sr3physics's own tier, not depended on here - the same layering
    // sr3weapons/weapon_fire.h's own doc comment states for itself) - host-
    // side bookkeeping only, not a reproduction of the vtable target or the
    // ray geometry (both explicitly OPEN per spec: "concrete target not
    // resolved - no character-class RTTI/vtable dump was available").
    uint64_t deathTransitionCount = 0;

    // The ragdoll/death-animation hand-off's real decision point,
    // 0x0098ce40's per-character sub-stage byte +0xcce (5-case jump table,
    // values 8-0xc) - CONFIRMED to exist and to be the real branch point;
    // the exact case-value mapping is explicitly OPEN (spec-player-
    // runtime.md Sec15.4's own "Honest gaps" line), so it is not reproduced
    // as a raw byte here. This project instead drives the SAME two named,
    // CONFIRMED branches directly via one CHOSEN test-settable flag:
    //   - true (default): the ragdoll-activation case (readiness 0x0098cc30
    //     + eligibility 0x009a2b00, both folded into this one flag) - calls
    //     the real "activate ragdoll physics" primitive 0x009a5520(victim,
    //     1, 0). Modelled by setting ragdollStateEnumCd2 (above) to a
    //     CHOSEN sentinel value satisfying its own known "!=0 && !=6"
    //     ragdolled exclusion test (character_is_ragdolled, Sec15.14) - the
    //     real engine's own specific ragdolled enum value stays OPEN.
    //   - false: the readiness check fails. Sec15.3a names a real outcome
    //     ONLY for a locally-owned PLAYER-CLASS victim in this case (the
    //     non-ragdoll fallback: force health to 0 - already done by the
    //     main damage math - then call the finalizer 0x00952760(victim, 0)
    //     directly, "a genuine scripted/animation death"). 0x00952760 is
    //     the SAME finalizer character_kill's own arg4=false branch already
    //     models (lifeState.set(5) + killFinalizedFlagE3 +
    //     characterKillFinalizerCount, stub_character_kill, b30) - reused
    //     verbatim here, not a parallel copy, since spec confirms it is the
    //     identical function for both callers; no new field needed for
    //     this branch. A non-player-class victim with this flag false is a
    //     genuine, spec-acknowledged gap (no named outcome either way) -
    //     left unmodelled rather than guessed.
    bool ragdollActivationEligible = true;
    uint64_t deathRagdollActivateCount = 0; // the ragdoll-activation case ran (0x009a5520 stand-in)
};

} // namespace sr3luahost
