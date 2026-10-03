// IMPLEMENTED PRE-REVIEW, PENDING CLEARANCE (manager rule 2026-09-30): the
// spec-lua-api-behaviour.md / spec-lua-bindings.md sections this file rests on
// are marked "NOT yet cleared for implementation" by the 2026-09-30 desk
// review (115/122 and 57/58 review-status lines). Kept working, behaviour
// unchanged, until Team A clears them; no new behaviour from those sections
// before then (team-b/HANDOFF.md section A, standing rule).
//
// Minimal, in-memory engine-state layer backing the 13 Lua-visible names
// this task promotes from `stub_registry.h`'s generic logged-nil stub to
// real, spec-confirmed behavior (see `spec_confirmed_stubs.h`/.cpp for the
// registration side; this header is only the STATE the 13 trampolines
// read/write). Built ONLY from `spec-lua-api-behaviour.md`/
// `spec-lua-bindings.md` (sections cited per field below) - never from
// disassembly, never from `D:\SR3RTXREMIXCOMP`. 12 of the 13 are the
// original CharacterState-centric batch (spec-lua-api-behaviour.md);
// vint_object_find (spec-lua-bindings.md Sec13.7/Sec15, VdoObject below)
// was folded in mid-task once Team A found it was simply unregistered.
//
// Deliberately narrow, per this task's own brief: this is NOT a general
// "entity"/"object" system. There is no real handle-resolution mechanism
// anywhere in this project, and the 12 CharacterState-based functions in
// scope only ever refer to an object by the plain Lua name string they
// were themselves given - so `CharacterState` is keyed by that name string directly (a
// `std::string`), and every one of the spec's own documented sentinel
// forms (`"#PLAYER#"`, `"#FOLLOWER#"`/`1`/`2`/`3`, `"#CLOSEST_PLAYER#"`)
// is treated as just another distinct key, not resolved to some shared
// "current player" object. This is a stated, explicit simplification, not
// a claim that the real engine's own name-resolution chain
// (`spec-lua-api-behaviour.md` Sec3's own "Name-resolution sentinels" note,
// Sec4.13) works this way - inventing real object-identity aliasing beyond
// "look up by the name string these functions are already given" would be
// over-scoping this task, per its own brief.
//
// Only fields the 12 CharacterState-based functions actually read or write
// are here (VdoObject, further down, is the separate vint_object_find
// state) - no generic "entity shape" was guessed. Each field cites the spec
// (sub)section it came from and its own confidence tier (CONFIRMED vs HIGH
// CONFIDENCE) so a later pass never silently treats a HIGH CONFIDENCE
// reading as settled.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// sr3save::nameHash(): the engine's general lower-cased, seed-0, no-final-
// XOR reflected CRC-32 name hash (FUN_00D9E740 - spec-save-format.md
// Sec9.8/Sec6.1, table 0x01320DA0, CONFIRMED - empirical via spec-lua-api-
// behaviour.md Sec5.1's 7/7 real I/O vectors for the sibling raw-byte entry
// point FUN_00D9E790 sharing the same table/algorithm). Reused by citation,
// not reimplemented - see spec-lua-bindings.md Sec15's own text on
// vint_object_find, which hashes its `name` argument via this exact
// routine before the parent/document-scoped lookup below. Header-only, no
// new link dependency (include/ is one shared tree - see this project's
// own established convention, every library already does this).
#include "sr3luahost/lua_c_api.h"
#include "sr3luahost/open_state.h"
#include "sr3save/save_crc.h"

namespace sr3vintdoc {
struct Document; // include/sr3vintdoc/vint_doc.h - EngineState::loadVintDocument
}

namespace sr3luahost {

// One `ai_add_enemy_target` (Sec3.9) threat/target record. The spec itself
// does not resolve whether the real record lives on the ACTING character
// or is a free-standing global table keyed by object identity - this
// project's own choice (stated here, not scraped from anywhere): model it
// as a per-ACTOR map, keyed by the arg-3-derived integer (as a decimal
// string) when nonzero, else by the resolved target name - matching the
// spec's own "keyed by the arg-3-derived integer when nonzero, else by
// object identity alone" text as closely as this minimal registry allows.
struct EnemyTargetRecord {
    // int64 of arg 3 via 0x00ea2596: truncation toward zero (Sec4.1, settled
    // 2026-10-01; the earlier banker's/round-to-nearest readings are struck).
    // Converted with truncateEa2596() (lua_spec_confirmed_stubs.cpp).
    int64_t priorityOrId = 0;
    // Bit 0x04: set from arg 4 (optional bool, default false). CONFIRMED
    // structure (Sec3.9); the real-world MEANING of this bit is HIGH
    // CONFIDENCE only ("neither independently re-derived from a consumer
    // of the record" - Sec3.9's own closing tag).
    bool arg4Flag = false;
    // Bit 0x02: set iff arg 2 case-insensitively matched the reserved
    // sentinel "#CLOSEST_PLAYER#" (__stricmp, CONFIRMED per Sec3's own
    // "Name-resolution sentinels" note). The bit's own downstream MEANING
    // is HIGH CONFIDENCE only, same caveat as arg4Flag above.
    bool sentinelMatchFlag = false;
};

// One `game_peg_load_with_cb` (Sec8.24) filename slot entry.
struct PegLoadFilenameEntry {
    std::string name;
    // Real, CONFIRMED, empirically vector-validated hash bucket index
    // (spec-texture-format.md Sec8.2's "multiply-33/XOR" hash,
    // cross-confirmed by spec-lua-api-behaviour.md Sec5.3's 5/5 real I/O
    // vectors) computed with bucket_count=9000 - the exact real call shape
    // Sec8.24 cites for this function's own shared resolution chain
    // (`0x00db3530`->`0x00dab330(name,9000)`->..., reused verbatim from
    // `game_peg_unload`, Sec2.7). This project has no real PEG pointer
    // array (`0x029e1930`) to resolve the bucket into an actual loaded
    // texture, so only the bucket index itself - the one real, checkable
    // number this minimal registry can reproduce exactly - is stored.
    //
    // OPEN for some names (2026-09-30, spec-texture-format.md Sec8.2 desk
    // review): the spec states initial value 0 and 32-bit wrap, but leaves
    // signed vs unsigned modulo and the character load's signedness OPEN.
    // With 9000 buckets those change the answer when the 32-bit hash is
    // >= 2^31 or the name has a byte >= 0x80, so the index is set only
    // when every reading agrees (multiply33XorHashBucketUnambiguous) and
    // stays OPEN otherwise.
    OpenValue<uint32_t> bucketIndex{"multiply-33 bucket index (signed modulo / char signedness)",
                                    "spec-texture-format.md Sec8.2"};
};

// One `game_peg_load_with_cb` (Sec8.24) request-tracking slot.
struct PegLoadRequest {
    // Arg 1, bounds-checked to 64 bytes per Sec8.24's own confirmed text
    // ("copied (bounds-checked to 64 bytes) into a per-request
    // tracking-slot's own name field").
    std::string requestName;
    // Args 3..2+N, N clamped to the confirmed 1..6 range; reading stops
    // early at the first absent slot (CONFIRMED, Sec8.24).
    std::vector<PegLoadFilenameEntry> filenames;
    // Real, CONFIRMED-tier completion/callback-dispatch behavior (Sec8.24:
    // "once every PEG in that slot has finished loading ... queue a stored
    // command name ... then reset the slot's count to 0") is explicitly
    // NOT modeled here - this project has no real async texture-loading
    // pipeline behind this stub host, and the spec itself only reaches
    // HIGH CONFIDENCE on what the stored "+0x28" field even IS ("the
    // actual Lua-callback invocation was not traced past the queued
    // command lookup"). Stated plainly as an open gap, not guessed past.
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
    // EngineState::getOrCreateCharacter() below. The ONLY way this starts
    // true is a bound script NPC whose own placement-record
    // `script_npc_flags` carries `"ignore_ai"` (Sec34.3) - that override
    // data lives in the still-held `.czn_pc` zone-placement interior, so a
    // character explicitly marked via markScriptNpcBoundForTesting() stays
    // (or reverts to) OPEN instead of getting the default.
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
    // checked text. No Lua function among this task's 12 in-scope names
    // ever WRITES this field (character_is_in_vehicle, the function that
    // would naturally pair with it, is out of scope) - exposed only so a
    // test can set up the "newly enabling ignore-AI while in a vehicle"
    // condition directly. Default 0 ("not in a vehicle").
    OpenValue<int32_t> stateEnum{"character state enum (+0x16d4)", "spec-lua-api-behaviour.md Sec3.4/Sec3.10"};

    // The other half of the same conditional (Sec3.4): "clears the
    // override to 0 if the character currently carries a nonzero
    // attacker/threat reference." Default 0 ("no attacker/threat
    // reference") - same "no Lua setter in scope" note as stateEnum above.
    OpenValue<int32_t> attackerThreatRef{"character attacker/threat reference", "spec-lua-api-behaviour.md Sec3.4"};

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
    // this field stays OPEN for a name with no Hit_Points supplied, same as
    // before; EngineState::applyCharacterSpawnDefaults() applies the
    // formula once a caller (a future real spawn integration, or a test)
    // supplies a resolved Hit_Points value. A script-NPC-bound character's
    // own `script_npc_hp` override (also zone-held, Sec34.2/Sec34.4) is the
    // same unavailable-data situation markScriptNpcBoundForTesting()
    // represents for ignoreAI above.
    OpenValue<int32_t> maxHitPoints{"character max hit points (+0x1cac)", "spec-lua-api-behaviour.md Sec7.12/Sec7.31/Sec34.2"};

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

    // ai_add_enemy_target (Sec3.9): see EnemyTargetRecord's own doc
    // comment above for the keying choice.
    std::unordered_map<std::string, EnemyTargetRecord> enemyTargets;

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
    // own minimal, explicitly-scoped stand-in for the link, used ONLY by
    // vehicle_exit_group_do's own "remembers the first vehicle found"
    // step. No Lua-visible function in this task's scope ever writes this
    // field; a test sets it directly. OPEN until set - same "never invent
    // a fresh object's values" discipline as every other CharacterState
    // field.
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
    // itself is out of this batch's scope). Default 0 ("not downed") - this
    // project's own chosen "alive, ordinary" default.
    OpenValue<int32_t> lifeState{"character life-state field (+0xcc8)", "spec-lua-api-behaviour.md Sec7.13/Sec32.3"};
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
    OpenValue<bool> forceFlagGatePasses{"character force-flag double-gate (0x008ae480/0x008837a0)",
                                        "spec-lua-api-behaviour.md Sec3.7"};
    OpenBits32 forceFlags1c98{"character force-flag byte +0x1c98 (invulnerable 0x20 / always-apply-player-damage 0x200)",
                              "spec-lua-api-behaviour.md Sec3.7"};
    static constexpr uint32_t kInvulnerableBit1c98 = 0x20u;
    static constexpr uint32_t kAlwaysApplyPlayerDamageBit1c98 = 0x200u;

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
};

// vehicle_set_invulnerable_to_player_explosives / vehicle_disable_explosion_
// and_damage_vfx / vehicle_set_special_override_never_ghost /
// vehicle_clear_all_radio_locks / vehicle_is_vtol (spec-lua-api-behaviour.md
// Sec30.5, "ranking tranche 03") and the vehicle cluster of ranking tranche
// 05 (Sec33.1, batch 2026-10-02 - the first batch to need vehicle-shaped
// state; merged into this one struct rather than two). Same "keyed by the
// plain name string it was given" simplification CharacterState's own top
// note already establishes for characters - the real resolution chain
// Sec30.5's shared preamble describes (character-redirect-when-driving,
// THEN the per-kind vehicle resolver, liveness, a +0x68 gate, virtual slot
// +0x70) is NOT reproduced; every vehicle function in this project's scope
// instead gates on the SAME shared objectResolves() map every other object
// kind already uses (Sec29's own "one shared global name map" finding - a
// vehicle name is just another name in that one map, not a distinctly-
// resolved kind).
struct VehicleState {
    // +0x1d7c/+0x1d7d force-flag bytes (Sec30.5, CONFIRMED structure, 4
    // bits total). Packed here as ONE 16-bit value inside one OpenBits32 -
    // this project's OWN choice, not a real combined engine field: the low
    // byte holds +0x1d7c's own bits 0x2/0x4 verbatim; the high byte holds
    // +0x1d7d's own bits 0x20/0x40 shifted left 8, so both real bytes fit
    // one word without colliding. NOTE: Sec33.1 separately tracks two MORE
    // bits of this SAME +0x1d7c byte (0x8/0x10, see forceFlags1d7c below) -
    // this project keeps them as independent fields rather than unifying
    // into one bit-accurate byte model, since no in-scope function ever
    // reads the byte back as a whole.
    OpenBits32 forceFlags{"vehicle force-flag bytes (+0x1d7c/+0x1d7d)", "spec-lua-api-behaviour.md Sec30.5"};
    static constexpr uint32_t kInvulnerableToPlayerExplosivesBit = 0x0002u; // +0x1d7c bit 0x2
    static constexpr uint32_t kRadioControlsLockedBit = 0x0004u;           // +0x1d7c bit 0x4
    static constexpr uint32_t kDisableExpAndDamageVfxBit = 0x2000u;        // +0x1d7d bit 0x20, shifted <<8
    static constexpr uint32_t kSpecialOverrideNeverGhostBit = 0x4000u;     // +0x1d7d bit 0x40, shifted <<8

    // The shared "variant-1 double-gate" abstraction (Sec30.5: "either
    // true -> write the bit directly; both false -> open an opcode-0x46
    // record with hashed tags instead") - same abstracted-to-one-flag
    // convention CharacterState::CounterOnGrabbed::gatePasses already
    // establishes (Sec28.19) for an analogous real double-gate shape.
    // CONFIRMED that all 4 force-flag setters (plus
    // vehicle_clear_all_radio_locks's own per-vehicle authority check)
    // share this identical shape; OPEN until a test sets it, per vehicle.
    OpenValue<bool> forceFlagGatePasses{"vehicle force-flag double-gate (0x008ae480/0x008837a0)",
                                        "spec-lua-api-behaviour.md Sec30.5"};

    // Shared vehicle fact, read by BOTH batches: vehicle_is_vtol (Sec30.5:
    // "(+0xbf4)+0x2c equals 4 = VTOL") and vehicle_is_helicopter/
    // vehicle_is_vtol_hover/vehicle_is_vtol_jet (Sec33.1: CONFIRMED values 3
    // = helicopter, 4 = VTOL, tested by name) - ONE real field at the SAME
    // offset, unified here under Sec33.1's name since more consumers use
    // it; the field's own real identity ("class/definition record" +
    // "flying-type enum") is HYPOTHESIS per Sec33.1's own text. No in-scope
    // setter writes it (a test sets it directly, same convention as
    // CharacterState::stateEnum above).
    OpenValue<int32_t> flyingType{"vehicle flying-type enum ((+0xbf4)+0x2c)", "spec-lua-api-behaviour.md Sec30.5/Sec33.1"};

    // vehicle_is_vtol_hover / vehicle_is_vtol_jet (Sec33.1): vehicle +0x5e8.
    // CONFIRMED the two functions test disjoint {0,3}/{1,2} subsets; the
    // real-world "hover vs jet" meaning of each value is HYPOTHESIS.
    OpenValue<int32_t> vtolState{"vehicle VTOL hover/jet state (+0x5e8)", "spec-lua-api-behaviour.md Sec33.1"};

    // vehicle_is_ready (Sec33.1): three independently-gated bits. Real-
    // world meaning of the two "not ready" bits is OPEN per spec; the
    // "fully set up" bit cross-links to vehicle_set_kneecappers's own
    // deferred-write gate below (HIGH CONFIDENCE, same spec paragraph).
    OpenValue<bool> notReadyBit3b{"vehicle not-ready flag (+0x3b bit 0)", "spec-lua-api-behaviour.md Sec33.1"};
    OpenValue<bool> notReadyBit3a{"vehicle not-ready flag (+0x3a bit 0)", "spec-lua-api-behaviour.md Sec33.1"};
    OpenValue<bool> fullySetUp{"vehicle fully-set-up flag (+0x33 bit 0x8)", "spec-lua-api-behaviour.md Sec33.1"};

    // vehicle_never_flatten_tires (Sec33.1): double-gate setter, local bit
    // +0x1d7b bit 0x1 (this project always applies the local write - see
    // EngineState::replicateStateChange's own doc comment for why). Reader
    // not traced per spec (OPEN) - no Lua-visible query of this bit is in
    // scope, so that gap has no further effect here.
    OpenBits32 forceFlags1d7b{"vehicle force-flags byte +0x1d7b", "spec-lua-api-behaviour.md Sec33.1"};
    // vehicle_set_weapons_disarmed (Sec33.1): +0x1d7e bit 0x4.
    OpenBits32 forceFlags1d7e{"vehicle force-flags byte +0x1d7e", "spec-lua-api-behaviour.md Sec33.1"};
    // vehicle_set_sirenlights (Sec33.1): +0x1d7c bit 0x10 (flashing
    // headlights) / bit 0x8 (siren lights) - a DIFFERENT byte from the two
    // above (CONFIRMED distinct offsets).
    OpenBits32 forceFlags1d7c{"vehicle force-flags byte +0x1d7c (headlight 0x10 / siren 0x8)",
                              "spec-lua-api-behaviour.md Sec33.1"};
    // vehicle_set_no_chase (Sec33.1): vehicle-AI sub-record (+0xc68) own
    // sub-byte +5 bit 0x1 - CONFIRMED the SAME setter this document's
    // existing vehicle_disable_chase entry reaches (out of this batch's
    // own scope to implement), through an extra wrapper this project does
    // not model (same "always apply the write directly" convention as
    // every other double-gate setter in this header).
    OpenBits32 vehicleAiForceFlags{"vehicle-AI force-flags byte (+0xc68)+5", "spec-lua-api-behaviour.md Sec33.1"};

    // vehicle_set_kneecappers (Sec33.1): this project has no per-wheel
    // physics/collision pool to simulate (48-vehicle fixed pool, CONFIRMED
    // structure, stated simplification same as several `*_add_do` records
    // elsewhere in this header) - only the two CONFIRMED gate outcomes are
    // tracked: whether kneecappers are currently requested enabled, and
    // whether that request is still deferred (requested before the
    // vehicle was "fully set up", fullySetUp above).
    bool kneecappersEnabled = false;
    bool kneecappersDeferred = false;

    // vehicle_set_sirenlights (Sec33.1): the vehicle-class enum
    // ((+0xbf4)+0x4c4, a DIFFERENT field from flyingType above despite the
    // shared +0xbf4 base - CONFIRMED distinct offsets) and the body-active
    // gate (+0xbd0). The actual Wwise siren SOUND side effect is NOT
    // modeled anywhere here (no Wwise event table in this project - same
    // gap as game_audio_get_audio_id's own Wwise-hash resolver elsewhere).
    OpenValue<int32_t> vehicleClass{"vehicle class enum ((+0xbf4)+0x4c4)", "spec-lua-api-behaviour.md Sec33.1"};
    OpenValue<bool> bodyActive{"vehicle body-active flag (+0xbd0)", "spec-lua-api-behaviour.md Sec33.1"};

    // vehicle_set_ambient (Sec33.1): +0x16c0 bit 0x1 marker, the vehicle-AI
    // sub-record's mode/sub-mode (CONFIRMED values 6/0x11, HYPOTHESIS real-
    // world meaning), and the speed-cap target this document's existing
    // vehicle_speed_override entry's own setter receives (out of this
    // batch's scope to implement its own Lua entry point) - stored
    // UNCONVERTED per Sec33.1's own explicit "passes the number
    // unconverted" finding, clamped to at most 1000.0.
    OpenBits32 ambientFlags{"vehicle ambient marker byte +0x16c0", "spec-lua-api-behaviour.md Sec33.1"};
    int32_t aiMode = 0;    // 0 = never set; CONFIRMED value when set: 6
    int32_t aiSubMode = 0; // 0 = never set; CONFIRMED value when set: 0x11
    bool hasSpeedCap = false;
    double speedCapRaw = 0.0; // unconverted Lua number, clamped to <= 1000.0

    // vehicle_spotlight_is_target_spotted (Sec33.1): the side effect this
    // project CAN model (re-aiming the spotlight at the resolved target,
    // every call) is tracked; the actual range/angle/raycast boolean test
    // needs real position/geometry data this project has nowhere in its
    // state (no coordinate system at all, stated project-wide
    // simplification) - NOT modeled, refused as OPEN by the stub rather
    // than fabricated (same convention as game_audio_get_audio_id's
    // Wwise-hash gap).
    std::string spotlightTargetName; // last re-aimed target name, "" = none

    // vehicle_engine_check_running (Sec38.1, "ranking tranche 06"): "On
    // failure (unresolved, or a specific object flag clear) pushes false
    // and returns 1 value; on success it calls the engine-running-bit
    // getter, discards the result, and returns 0 values" - so "success"
    // here means resolved AND this gate passes, NOT "the engine is
    // actually running" (that bit is read and thrown away either way,
    // CONFIRMED - the mandated "structurally can never answer true"
    // quirk). The exact real bit this "specific object flag" tests is not
    // named by address in scope - this project's own minimal stand-in,
    // scoped only to this one query. OPEN until set.
    OpenValue<bool> engineCheckGatePasses{"vehicle_engine_check_running success gate (a specific object flag)",
                                          "spec-lua-api-behaviour.md Sec38.1"};

    // --- vehicle_set_vulnerable (Sec20.1) / turn_invulnerable (Sec3.7) /
    // vehicle_is_invulnerable (Sec46.5) - ranking tranche 14's own Sec46.1
    // bit-conflict resolution, batch 2026-10-02 -------------------------

    // Vehicle +0x1d7a, bit 0x01 ("invulnerable") / bit 0x02 ("always apply
    // player damage"). RESOLVED 2026-10-02 (ranking tranche 14, Sec46.1):
    // bit 0x01 is CONFIRMED correct by direct disassembly of the merge
    // idiom (Sec3.7 was right all along); Sec20.1's own prior "bit 0x8"
    // reading was the error, now corrected in the spec text itself. A
    // DIFFERENT byte from forceFlags/forceFlags1d7b/forceFlags1d7c/
    // forceFlags1d7e above (all CONFIRMED distinct offsets, Sec30.5/Sec33.1).
    OpenBits32 forceFlags1d7a{"vehicle force-flags byte +0x1d7a (invulnerable 0x01 / always-apply-player-damage 0x02)",
                              "spec-lua-api-behaviour.md Sec3.7/Sec20.1/Sec46.1"};
    static constexpr uint32_t kInvulnerableBit1d7a = 0x01u;
    static constexpr uint32_t kAlwaysApplyPlayerDamageBit1d7a = 0x02u;

    // --- vehicle_lights_on (Sec46.5, "ranking tranche 14") - a faithful
    // naming trap, NOT "fixed": vehicle_lights_on(v, false) does NOT turn
    // the lights off - it clears BOTH the force-on and force-off flags,
    // returning the lights to automatic control (CONFIRMED). The true-path's
    // own exact flag combination beyond "force on" is not separately given
    // by this tranche's text - this project's own minimal, stated choice:
    // true sets force-on and clears force-off (the natural complement of
    // the CONFIRMED false-path's "clear both").
    OpenBits32 lightsForceFlags{"vehicle headlight force-on/force-off flags (naming trap: false = automatic, not off)",
                                "spec-lua-api-behaviour.md Sec46.5"};
    static constexpr uint32_t kLightsForceOnBit = 0x1u;
    static constexpr uint32_t kLightsForceOffBit = 0x2u;

    // --- vehicle_tire_indicators_alive (Sec46.5) - a faithful naming trap:
    // reports 8 ("all alive") whenever the vehicle's tire-indicator object
    // is simply DISABLED, not actually alive (CONFIRMED). This project has
    // no real per-tire alive/damage simulation (same gap as
    // kneecappersEnabled's own "no per-wheel physics pool" precedent,
    // Sec33.1) - only the disabled-object short-circuit is modeled; test-
    // settable, OPEN until set.
    OpenValue<bool> tireIndicatorObjectDisabled{"vehicle tire-indicator object disabled flag",
                                                "spec-lua-api-behaviour.md Sec46.5"};

    // --- vehicle_turret_base_to_do (Sec46.4) - batch 2026-10-02 ---------

    // Whether the vehicle's seat 0 is occupied - gates the CONFIRMED null-
    // read crash (Sec46.4's own text: "a resolvable vehicle that has a
    // seat-0 occupant"). This project has no real seat/occupant array (same
    // gap as kneecappersEnabled's own "no per-wheel physics pool" precedent)
    // - test-settable only, OPEN until set.
    OpenValue<bool> seat0Occupied{"vehicle seat-0 occupant present", "spec-lua-api-behaviour.md Sec46.4"};

    // --- vehicle_set_tire_durability / vehicle_set_tire_damage_multiplier
    // (Sec46.5), batch 2026-10-02 (resumed session) ----------------------

    // CONFIRMED (Sec46.5): both setters "silently ignore any value <= 0
    // (the field can never be reset to 0 from Lua this way)" - the stub
    // enforces that gate before calling set(), so these are only ever
    // known holding a value > 0. The real numeric unit/range is OPEN (not
    // stated beyond "a value"); no in-scope Lua getter reads either field
    // back, so nothing downstream depends on a specific default.
    OpenValue<double> tireDurability{"vehicle tire durability", "spec-lua-api-behaviour.md Sec46.5"};
    OpenValue<double> tireDamageMultiplier{"vehicle tire damage multiplier", "spec-lua-api-behaviour.md Sec46.5"};
};

// vint_object_find (spec-lua-bindings.md Sec15, folded in mid-task per the
// orchestrator's relay of Team A's Sec13.7/Sec15 finding: this native
// global was simply UNREGISTERED, accounting for 43% of all hook-fire
// errors - a nil-global call, not a "stub returns the wrong thing" case).
// One entry = one "VDO UI-element instance" the real engine would track;
// this project's own minimal stand-in, populated ONLY via
// EngineState::registerVdoObjectForTesting() (no Lua-visible function in
// this pass's scope populates it - vint_object_create remains an ordinary
// generic logging stub, per the orchestrator's own "you don't need to
// implement all of those this pass" allowance) - stated plainly, not
// hidden: every REAL script call this pass can exercise therefore takes
// the real, CONFIRMED "not found" path (returns 0.0), which is itself
// correct, real behavior for a never-populated object space, not a
// simplification of the return contract itself.
//
// UPDATE 2026-10-03: no longer test-only. EngineState::loadVintDocument()
// is a real loading path - a parsed `.vint_doc` (sr3vintdoc::parseDocument,
// layout CONFIRMED in spec-vint-doc-format.md at main 67455c3) becomes one
// VdoObject per element/animation record, with real names, parents, first
// children and file-applied properties. Hosts that never load a document
// (every existing test) see exactly the old behaviour.
struct VdoObject {
    std::string name;
    // Real, CONFIRMED-mechanism hash of `name` (FUN_00D9E740, lower-cased,
    // this project's own default seed 0 - see EngineState::findVdoObject's
    // own doc comment for why 0, specifically).
    uint32_t nameHash = 0;
    // 0 = "no parent" (a document-root object) - this project's own chosen
    // sentinel (real handle 0 is never issued by nextVdoObjectHandle(),
    // which starts at 1, so 0 is unambiguous as "none").
    uint32_t parentHandle = 0;
    // Which "document" this object belongs to - 0 is this project's own
    // chosen sentinel for "the current default document" (real
    // FUN_00e0ceb0's own stand-in, EngineState::currentDefaultDocHandle()).
    uint32_t docHandle = 0;
    // vint_object_first_child (spec-lua-bindings.md Sec18.1): the real
    // record's own offset +0x1c, "the record's first-child pointer." 0 = "no
    // children" - this project's own chosen sentinel, same reasoning as
    // parentHandle above (handle 0 is never issued by nextVdoObjectHandle()).
    // Only ever set by EngineState::setVdoObjectFirstChildForTesting() - no
    // Lua-visible function in this pass's scope (vint_object_create remains a
    // generic stub, same as parentHandle/docHandle's own established
    // precedent) populates a real parent/child tree, so a real script's
    // vint_object_first_child call on any object this host can resolve
    // honestly takes the real, CONFIRMED "no children" path unless a test set
    // one up directly.
    uint32_t firstChildHandle = 0;

    // --- Set only by EngineState::loadVintDocument (2026-10-03), the real
    // loading path; left empty/0 by registerVdoObjectForTesting(). ---
    // The record's registered type name (`bitmap`, `group`, `tween`, ...),
    // resolved through the document's string table (spec-vint-doc-format.md
    // Sec4).
    std::string typeName;
    // The next record under the same parent in file order (0 = last). Not
    // read by any Lua-visible function yet (vint_object_next_sibling is not
    // implemented); kept so the tree's sibling order is not lost.
    uint32_t nextSiblingHandle = 0;
    bool fromDocument = false;
};

// One document loaded by EngineState::loadVintDocument (2026-10-03).
struct LoadedVintDocument {
    std::string name;          // the caller's document name, e.g. "cmp_mission.vint_doc"
    uint32_t handle = 0;       // this project's own document handle (see loadVintDocument)
    std::string luaScriptFile; // metadata "lua_script_file", "" if absent
    std::vector<uint32_t> elementHandles;   // top-level elements (header +0x1A), file order
    std::vector<uint32_t> animationHandles; // top-level animations (header +0x1C), file order
    size_t objectCount = 0;    // every record, recursively
    size_t propertyCount = 0;  // every applied property, over all records
};

// vint_dataitem_get (spec-lua-bindings.md Sec19.2): one generic captured Lua
// scalar - this project's own minimal stand-in for the real 24-byte tagged-
// value record this pass's spec text finds reused across several of these
// natives' own argument/field marshaling (Sec19.2's data-item fields,
// Sec21.2's data-responder trailing args). `None` covers a slot this project
// has no typed value for (a nil, or - per Sec21.2's own text - "any other
// type gets an untyped/default slot") rather than guessing a type.
struct VintTaggedValue {
    enum class Kind { Number, Boolean, String, None };
    Kind kind = Kind::None;
    double number = 0.0;
    bool boolean = false;
    std::string text;
};

// vint_dataitem_get (Sec19.2): one "data item" record - a second, SEPARATE
// handle space from the VDO-object handles above (Sec19.2's own text: "a
// different resolver... data items are therefore a genuinely separate
// handle/record space"). This project has no real data-item loader (the
// resolved record's real own fields come from UI runtime memory this host
// does not model) - populated ONLY via
// EngineState::registerVintDataItemForTesting(). Bounded at 32 fields
// (CONFIRMED, the real record's own fixed local array size); real shipped
// source only ever reads the first 24 (`vint_lib.lua:393`).
struct VintDataItem {
    std::vector<VintTaggedValue> fields;
};

// vint_dataresponder_finished / vint_internal_dataresponder_request
// (spec-lua-bindings.md Sec21): one named-record registry entry - CONFIRMED a
// genuinely different shape from every numeric-handle resolver elsewhere in
// this document (Sec21's own text: "a name-keyed record registry"). CONFIRMED
// (Sec21.2): no Lua-visible function in this pass's 2-name scope ever
// CREATES one of these records (`vint_internal_dataresponder_request`'s own
// text: "this native never creates the named record") - so, same convention
// as VdoObject/VintDataItem above, this registry is populated ONLY via
// EngineState::registerDataResponderForTesting().
struct VintDataResponderRecord {
    // +0x258, CONFIRMED: the completion flag vint_dataresponder_finished reads.
    bool finished = false;
    // Not a real engine field: this project's own count of how many times
    // vint_internal_dataresponder_request reached its own real "type-validated,
    // record exists" gate (Sec21.2) for this name - test-observable proof the
    // gate logic runs, without claiming any further engine effect (the actual
    // dispatch, FUN_00e1e660, was not independently decompiled this pass and
    // is not modeled - see EngineState::dataResponderRequest's own doc comment).
    int dispatchAttempts = 0;
};

// pause_map_stag_current_district_control / pause_map_stag_takeover_do_reward
// (spec-lua-api-behaviour.md Sec31.2/Sec31.4/Sec31.6): one zone-membership
// record from the world singleton's own typed sub-list (0x03171a64, count
// +0x198, u16 indices +0x190, object pointers +0x58 - a different sub-list
// from this document's OTHER uses of the same global, e.g. the zscene
// "nearest world object" note above or Sec30.3's ambient-emitter list - this
// project has no real world-object-manager loader at all (same "no world
// objects in this host" precedent already established for
// zsceneNearestWorldObjectScene_ below) - populated ONLY via
// EngineState::registerZoneMemberForTesting().
struct ZoneMember {
    float weight = 0.0f; // +0x3c, CONFIRMED (Sec31.2) - the member's weighted contribution to the zone total
    bool owned = false;  // +0x3a bit 0x02, CONFIRMED (Sec31.4) - "owned by the player" (claimed via pause_map_stag_takeover_do_reward)
};

// pause_map_set_gps's own stag-mode branch (Sec31.3): the hovered-zone
// global 0x0229a2e4, written by the pause map's per-update refresher from
// the real cursor position (0x0084ba60, a zone-containment lookup this
// project does not run). This project has no cursor/pointer-input system -
// a test sets this fixture directly, standing in for "the player is
// currently hovering this zone on the pause map." zoneHandle 0 = "nothing
// hovered" (this project's own chosen sentinel, matching the selected-zone
// global's own "0 = none selected" reading, Sec31.2).
struct PauseMapHoveredZone {
    uint32_t zoneHandle = 0;
    bool hasParentDistrict = false; // the hovered zone's own +0x44 non-null (CONFIRMED gate, Sec31.3)
    int32_t fieldPlus0x54 = 0;      // the hovered zone's own +0x54 (CONFIRMED gate: must be > 0, Sec31.3)
};

// The shared, cross-script, whole-run engine-state instance - one per
// `Host` (host.h), matching `HitLog`/`ThreadScheduler`'s own existing
// per-Host-instance convention (stub_registry.h/thread_scheduler.h).
class EngineState {
public:
    // Looks up `name` (the plain Lua name string, see this header's own
    // top note on why no further resolution happens), creating a
    // fresh, all-defaults entry on first reference. Returned by mutable
    // reference so both the 12 trampolines AND this project's own tests
    // can read/write real state directly - there is no separate Lua-
    // visible "does this character exist" query among this task's 12
    // in-scope names, so a dedicated const-only accessor would serve no
    // real caller.
    // Applies the CONFIRMED spawn-time defaults (character spawn state,
    // Sec34.1/Sec34.2/Sec34.3): ignoreAI = false, so a fresh name created
    // by getOrCreateCharacter() below already carries this (see its own
    // comment) - this call exists so a caller that already knows the
    // name's resolved Hit_Points can ALSO apply the max-HP default path:
    // maxHitPoints = round(multiplier * hitPoints) (M defaults to 1.0,
    // CONFIRMED for ordinary classes - see CharacterState::maxHitPoints's
    // own doc comment for the special-class M gap this does not cover),
    // currentHitPoints set equal (a character spawns at full health).
    // Resolving a bare Lua name to a real Hit_Points value is this
    // project's own unmodeled gap (no in-scope spawn function performs
    // character_definitions.xtbl preset / level-index resolution), so
    // nothing in this host calls this automatically today - exposed for a
    // future real spawn-call integration and for tests, same
    // "mechanism confirmed, no in-scope producer wires it up" convention
    // as registerVdoObjectForTesting() and its siblings.
    void applyCharacterSpawnDefaults(const std::string& name, uint32_t hitPoints, double multiplier = 1.0);

    // Test-only (Sec34.4): marks `name` as bound to a script NPC whose own
    // placement-record overrides (`script_npc_hp` / `script_npc_flags`
    // containing `"ignore_ai"`) are zone data - the still-held `.czn_pc`
    // interior. This project cannot know whether such a record carries
    // either override (that IS the parked question), so this represents
    // ONLY "the override question is live for this character" by
    // forgetting the CONFIRMED defaults back to OPEN; it never fabricates
    // what an override would actually contain. No Lua-visible function in
    // this host's scope ever establishes this binding for a real name
    // (Sec34.4's own census: commando_spawn's builder `0x00a33b80` and the
    // script-NPC spawn routine `0x00a34f00` never set either property) -
    // populated only via this entry point, for a test demonstrating the
    // refusal.
    void markScriptNpcBoundForTesting(const std::string& name);

    // Looks up `name` (the plain Lua name string, see this header's own
    // top note on why no further resolution happens), creating a
    // fresh, all-defaults entry on first reference. Returned by mutable
    // reference so both the 12 trampolines AND this project's own tests
    // can read/write real state directly - there is no separate Lua-
    // visible "does this character exist" query among this task's 12
    // in-scope names, so a dedicated const-only accessor would serve no
    // real caller. CONFIRMED (Sec34.1/Sec34.3): a FRESH entry's ignoreAI
    // is set to false here, not left OPEN - the engine default is a real,
    // disassembly-confirmed constant (see CharacterState::ignoreAI's own
    // doc comment), not an invented stand-in. maxHitPoints/currentHitPoints
    // stay OPEN (see their own doc comments: the real default needs a
    // resolved Hit_Points value this project has no way to attach to a
    // bare name).
    CharacterState& getOrCreateCharacter(const std::string& name);
    bool hasCharacter(const std::string& name) const;
    size_t characterCount() const { return characters_.size(); }

    // Same convention as getOrCreateCharacter/hasCharacter/characterCount
    // above, for VehicleState (spec-lua-api-behaviour.md Sec30.5/Sec33.1).
    // Exposed by mutable reference so vehicle_clear_all_radio_locks can walk
    // every vehicle this host currently knows about (this project's own
    // stand-in for "every vehicle in the world object manager" - see
    // VehicleState's own doc comment) and so tests can read/write state
    // directly.
    VehicleState& getOrCreateVehicle(const std::string& name);
    bool hasVehicle(const std::string& name) const;
    size_t vehicleCount() const { return vehicles_.size(); }
    std::unordered_map<std::string, VehicleState>& vehicles() { return vehicles_; }

    // --- co-op session (spec-lua-api-behaviour.md Sec26.28, Sec3.1, Sec8.27,
    // Sec10.2; batch 2026-10-01) -------------------------------------------
    //
    // The session singleton 0x024d8534 (accessor 0x0087ba20) and the fields
    // of the session object the three Lua queries read. CONFIRMED (Sec26.28):
    // the singleton is zero at load and no resolved code installs a session
    // ("a host starts with no session"), so `present` starts false
    // (applySpecInitialState). Every per-session field below is only read
    // when a session exists; nothing in this host installs one, so they stay
    // OPEN (tests set them). Whether single player installs a one-member host
    // session is OPEN (Sec26.28); its HYPOTHESIS is not implemented.
    struct CoopSession {
        OpenValue<bool> present{"co-op session singleton 0x024d8534 non-null (0x0087ba20)",
                                "spec-lua-api-behaviour.md Sec26.28/Sec3.1"};
        // +0x5c == +0x58: the local member is the host member (Sec8.27).
        OpenValue<bool> localIsHost{"session host check (+0x5c == +0x58)", "spec-lua-api-behaviour.md Sec8.27/Sec10.2"};
        // Sec3.1 condition 2, client side: the session is not idle (0x0059fbe0
        // false: +0xf4/+0xf8/+0x224) and byte +0xfd is clear.
        OpenValue<bool> clientGate{"session client gate (not idle per 0x0059fbe0, +0xfd clear)",
                                   "spec-lua-api-behaviour.md Sec3.1/Sec26.28"};
        // +0x60 via 0x00681370; coop_is_active needs >= 2 (unsigned compare).
        OpenValue<uint32_t> memberCount{"session member count (+0x60)", "spec-lua-api-behaviour.md Sec3.1"};
        // Sec3.1 condition 4: every member but the local one passes 0x00877a90.
        OpenValue<bool> otherMembersPassSlotCheck{"session member walk slot check (0x00877a90)",
                                                  "spec-lua-api-behaviour.md Sec3.1"};
    };
    CoopSession& coopSession() { return coopSession_; }
    // coop_is_active's core predicate 0x00867830 (Sec3.1, CONFIRMED): session
    // present; host check first, else the client gate; member count >= 2;
    // the member walk. Throws OpenStateError on an OPEN read it needs.
    bool coopIsActive() const;
    // game_get_is_host (Sec8.27) and the host gate of the 0x53 fade
    // broadcasts (Sec26.24): session present and +0x5c == +0x58.
    bool coopLocalIsHost() const;
    // Completion_is_client (Sec10.2): session present and +0x5c != +0x58.
    bool coopLocalIsClient() const;

    // game_UI_audio_play (Sec2.2): "Returns the resulting voice/play-
    // instance handle as a Lua number" - the spec confirms a handle is
    // always returned (regardless of whether a sound argument resolved)
    // but not the real engine's own numeric scheme. This project's own
    // chosen stand-in: a plain, monotonically-incrementing counter
    // starting at 1, matching this project's own established convention
    // for an unconfirmed-but-plausible handle shape (see
    // ThreadScheduler::newThread's identical choice, thread_scheduler.h).
    int64_t nextAudioVoiceHandle() { return nextAudioVoiceHandle_++; }

    std::vector<PegLoadRequest>& pegLoadRequests() { return pegLoadRequests_; }
    const std::vector<PegLoadRequest>& pegLoadRequests() const { return pegLoadRequests_; }

    // The record-and-replicate idiom's own no-op stand-in (see
    // `spec-lua-api-behaviour.md` Sec3.4/Sec3.7/Sec4.13/Sec7.33: several of
    // this task's in-scope setters open a real coop/network-sync record
    // instead of - or alongside - a direct local write). This project does
    // NOT build a real networking layer (explicit instruction) - this
    // function is the single, clearly-labelled no-op stand-in for that
    // real mechanism, called from every real call site the spec says uses
    // it, so those call sites are honestly marked rather than silently
    // dropped. `fieldTag` is a short, human-readable identifier (this
    // project's own choice, not a real opcode/debug-tag transcription)
    // naming which setter's own call site invoked it, for anyone reading a
    // trace/log later.
    static void replicateStateChange(const std::string& fieldTag, const std::string& objectName);

    // --- 0x0095da50 (Sec14.1 CORRECTED + Sec33.3 action_play_synced_do) ---
    //
    // 0x0095da50 is a plain name -> synced-action-definition index lookup
    // (hash the name, scan a global table, sentinel kSyncedActionNotFound
    // for "not found") - NOT a hidden-argument identity match against an
    // actor, as this document's own Sec14.1 entry (action_play_synced_state,
    // out of this batch's scope to implement) previously read it before the
    // 2026-10-02 correction this same investigation applied in place there.
    // This project has no real table of synced-action definitions loaded
    // anywhere (no in-scope function populates one), so every name honestly
    // takes the real, CONFIRMED "not found" path unless a test registers
    // one directly - same "never-populated object space" precedent as
    // VdoObject/vint_object_find above.
    static constexpr int32_t kSyncedActionNotFound = 0xffff;
    void registerSyncedActionForTesting(const std::string& name, int32_t index);
    int32_t lookupSyncedActionIndex(const std::string& name) const;

    // FUN_00DAB330 (spec-texture-format.md Sec8.2's "multiply-33/XOR"
    // hash: fold each character to lowercase, `hash = (hash*0x21) XOR c`,
    // then `hash % bucketCount`) - CONFIRMED, empirically vector-validated
    // (spec-lua-api-behaviour.md Sec5.3, 5/5 real I/O vectors matched
    // exactly). Used by game_peg_load_with_cb (Sec8.24) via its own
    // confirmed shared chain with game_peg_unload (Sec2.7),
    // bucket_count=9000. A free function (no state needed) - exposed here
    // rather than in a texture-domain header since this is the only
    // consumer among this task's 12 in-scope names.
    //
    // Spec update 2026-09-30 (Sec8.2): initial value 0 and 32-bit wrap are
    // now stated there, as implemented. Still OPEN there: signed vs
    // unsigned modulo and the character load's signedness. This function
    // takes the unsigned reading of both; it is the answer only when
    // multiply33XorHashBucketUnambiguous() is true.
    static uint32_t multiply33XorHashBucket(const std::string& name, uint32_t bucketCount);
    // True when every reading Sec8.2 leaves OPEN gives the same bucket:
    // every byte < 0x80 (no signedness question on the load or the
    // lowercase fold), and either a power-of-two bucket count (the modulo
    // only keeps low bits) or a 32-bit hash < 2^31 (signed and unsigned
    // modulo agree on non-negative values).
    static bool multiply33XorHashBucketUnambiguous(const std::string& name, uint32_t bucketCount);

    // --- vint_object_find (spec-lua-bindings.md Sec15) ---------------------

    // Test/setup-only entry point (see VdoObject's own doc comment on why
    // no Lua-visible function in this pass's 13-name scope populates this
    // registry). Returns the fresh handle assigned (starts at 1,
    // incrementing - matching this project's own established "plain
    // incrementing counter" convention for an unconfirmed real numeric
    // scheme, same choice as ThreadScheduler::newThread/
    // nextAudioVoiceHandle() above - the REAL engine's own handle is "read
    // from offset +0x28 of the resolved native record," an opaque id this
    // project has no real record to read).
    uint32_t registerVdoObjectForTesting(const std::string& name, uint32_t parentHandle, uint32_t docHandle);

    // Test-only direct read of a registered VDO object's own fields - no
    // Lua-visible getter exists for parentHandle/docHandle among this
    // document's 8-name Sec18-Sec21 scope (vint_object_parent/
    // vint_object_get_doc are both out of scope), so cloneVdoObject's own
    // parent/document assignment (Sec18.2) can only be checked this way.
    // Returns nullptr for an unregistered handle.
    const VdoObject* vdoObjectForTesting(uint32_t handle) const;

    // FUN_00e0ceb0's own stand-in ("a 'current default document' global
    // lookup") - default 0 (this project's own "no default document yet"
    // sentinel, VdoObject's own doc comment). Test-only setter; no Lua
    // function among this task's 13 in-scope names ever changes it.
    OpenValue<uint32_t>& currentDefaultDocHandle() { return currentDefaultDocHandle_; }

    // Implements vint_object_find's own CONFIRMED mechanism (Sec15): hash
    // `name` (FUN_00D9E740 - this project's own default seed 0, since the
    // spec's own Sec15 text does not state a seed for this specific call
    // site, unlike several OTHER cited call sites elsewhere in this
    // project that explicitly pass 0 - HIGH CONFIDENCE by convention, not
    // independently confirmed for this exact site). If `hasParent` AND
    // `parentHandle` resolves to a real registered object, search only
    // that parent's children; OTHERWISE (parent absent/wrong-type, OR a
    // given-but-unresolvable handle - both read as "no parent was
    // resolved" per the spec's own exact words) search document-wide
    // against `docHandle` if `hasDoc`, else `currentDefaultDocHandle()`.
    // Returns 0 (the real, CONFIRMED 0.0-on-failure return value, per
    // Sec15's own explicit "do not change it to -1" instruction) if
    // nothing matches either way.
    // 2026-10-03, with real documents loadable: (a) the OPEN current
    // document is read only when the answer depends on it - if no
    // registered object anywhere carries `name`'s hash, "not found" holds
    // in every document and 0 is returned without reading it (the same
    // reasoning as the old "registry empty" shortcut, generalised); (b) on
    // several matches the lowest handle wins, i.e. the first in file
    // pre-order for a loaded document - CHOSEN, the real search order is
    // not specified (Sec15 says only "looked up among that parent's children,
    // or document-wide").
    uint32_t findVdoObject(const std::string& name, bool hasParent, uint32_t parentHandle,
                           bool hasDoc, uint32_t docHandle) const;

    // --- vint_object_first_child / vint_object_clone (spec-lua-bindings.md
    // Sec18) ------------------------------------------------------------

    // Test/setup-only entry point (see VdoObject::firstChildHandle's own doc
    // comment). No-op if `parentHandle` is not a registered object.
    void setVdoObjectFirstChildForTesting(uint32_t parentHandle, uint32_t childHandle);

    // vint_object_first_child's own CONFIRMED mechanism (Sec18.1): resolve
    // `handle` (the shared resolver, same as findVdoObject), read its
    // firstChildHandle. Returns 0 for EITHER a bad handle OR a resolved
    // object with no first child - both real, CONFIRMED paths that push zero
    // Lua values (never 0.0), which the caller (the Lua stub) implements by
    // simply not pushing anything when this returns 0 (handle 0 is never a
    // real child handle - same sentinel reasoning as VdoObject::parentHandle).
    uint32_t vdoObjectFirstChild(uint32_t handle) const;

    // vint_object_clone's own CONFIRMED mechanism (Sec18.2): resolves "the
    // current document" first (currentDefaultDocHandle() - literally the
    // SAME OPEN value as findVdoObject's own doc_handle fallback, per
    // Sec18.2's own explicit cross-reference to Sec15 - so an OPEN read here
    // refuses exactly like it already does there, rather than being
    // special-cased to the real "document resolution failed -> 0.0" path;
    // consistent with this project's own established precedent of never
    // silently converting "this host doesn't know" into a fabricated
    // definite answer). Then resolves `origHandle` via the shared resolver;
    // not found -> returns 0 (CONFIRMED 0.0-on-failure, the real engine also
    // logs a debug string here, not Lua-visible, not reproduced). Then
    // resolves the requested parent (`hasParentArg`/`parentHandleArg`) via
    // the same shared resolver, falling back to the ORIGINAL object's own
    // parentHandle if absent/wrong-type/unresolvable (CONFIRMED, Sec18.2's
    // own "falls back to the original's own +0x24 parent field" text).
    // Creates a new VdoObject scoped to the resolved parent and the resolved
    // current document and returns its fresh handle. The real virtual clone
    // call's own deep-copy of the source object's content (name, type,
    // properties) was NOT independently decompiled this pass (Sec18.2's text
    // only confirms the receiver/parent/flag call arguments, not what the
    // callee internally copies) - NOT modeled: the new object's own name/hash
    // stay empty/0 rather than guessed, so it is never itself findable by
    // name via vint_object_find/vint_object_first_child afterward. This
    // minimal stand-in has no further failure condition to model honestly
    // (same precedent already established for object_indicator_add_do
    // above), so every call that gets past the OPEN/not-found checks
    // succeeds.
    uint32_t cloneVdoObject(uint32_t origHandle, bool hasParentArg, uint32_t parentHandleArg);

    // --- Real document loading (2026-10-03) --------------------------------
    //
    // Builds the live element tree of one parsed document
    // (sr3vintdoc::parseDocument) into this registry, the way the real
    // binary loader (spec-vint-doc-format.md Sec4/Sec5 at 67455c3:
    // 0x00e2a280 creates each element and attaches it to its parent, then
    // 0x00e29860 applies its properties through the type's descriptor
    // setters) does:
    //  * a fresh document handle (this project's own counter, starting at 1
    //    and separate from object handles - the real engine resolves
    //    documents through a different resolver, FUN_00e1f330, Sec18.2/
    //    Sec19.1; its numbering is not specified, same "plain incrementing
    //    counter" convention as registerVdoObjectForTesting);
    //  * one VdoObject per element AND per animation record (Sec4: the same
    //    record shape, both lists under the same document), handles issued in
    //    file pre-order, name hashed with sr3save::nameHash (Sec15), parent =
    //    the enclosing record (0 for a top-level record - this project's
    //    existing "document root" sentinel; whether the real engine puts a
    //    root object above them is not specified), firstChildHandle = the
    //    first child record (Sec18.1's +0x1c), nextSiblingHandle = the next
    //    record under the same parent;
    //  * every applied property stored into the SAME per-(object, name-hash)
    //    bag vint_set_property writes and vint_get_property reads, as the
    //    loader's setter call would leave it (CONFIRMED that the loader
    //    applies file values through the descriptor's +0x08 setter, Sec5 /
    //    spec-lua-bindings.md Sec9.3). Value mapping (CHOSEN, one Lua value per
    //    stored component, matching Sec20.2's "N values per the type" rule):
    //    tag 1 -> one number from a SIGNED 32-bit integer, tag 2 -> one
    //    number from an UNSIGNED 32-bit integer (both CONFIRMED widths,
    //    67455c3), tag 3 -> one number (float), tag 4 -> one string (the
    //    document's string at that index), tag 5 -> one boolean, tag 6 ->
    //    three numbers, tag 7 -> two numbers.
    // Which list is applied: the effective properties for `activeResolution`
    // (Sec5's override-then-baseline rule, ElementNode::effectiveProperties).
    // The real active resolution comes from a resolution table this project
    // does not model; the default "" (no override matches - what every
    // resolution other than the only shipped override, "640x480", gives) is
    // CHOSEN.
    // NOT modelled, deliberately: per-type descriptor tables (a stored
    // property whose hash the real type does not register would be consumed
    // and skipped by the real loader; this project stores every property,
    // since every shipped hash belongs to a name the shipped scripts use),
    // property defaults for names the file does not store (only scale/anchor
    // have a spec'd default, Sec7, and its seeding scope is OPEN), and the
    // instancing of a `document`-typed record's referenced widget document
    // (its `document_name` property names another .vint_doc - the runtime
    // instancing is not specified anywhere).
    uint32_t loadVintDocument(const std::string& docName, const sr3vintdoc::Document& doc,
                              const std::string& activeResolution = std::string());
    // nullptr for a handle loadVintDocument never issued.
    const LoadedVintDocument* loadedVintDocument(uint32_t docHandle) const;
    size_t loadedVintDocumentCount() const { return loadedVintDocuments_.size(); }

    // --- vint_get_time_index (Sec19.1) --------------------------------------

    // FUN_00e0ceb0's own stand-in reused a third time (Sec19.1's own text:
    // "the third sighting in this document of the identical +0x14-field read
    // feeding a 'current document' resolution") - this project's host has no
    // document registry distinct from the resolved handle number itself, so
    // a genuine "FUN_00e1f330 failed to resolve" cannot be told apart from
    // "no test has supplied a time index for this handle yet"; both become
    // an OpenStateError refusal (never a fabricated number). The real
    // "zero Lua values pushed" failure path is therefore not separately
    // reachable in this minimal host - same "no failure condition to model
    // honestly" precedent as cloneVdoObject above.
    OpenValueMap<double>& vintTimeIndexByDoc() { return vintTimeIndexByDoc_; }
    // `hasExplicitNonZeroDoc`/`explicitDoc`: CONFIRMED (Sec19.1) that absent,
    // nil, OR the literal number 0 all take the SAME fallback
    // (currentDefaultDocHandle()) path - a genuine difference from every
    // other optional numeric-handle argument elsewhere in this document,
    // which only fall back on absence/nil, not on an explicit 0.
    double vintGetTimeIndex(bool hasExplicitNonZeroDoc, uint32_t explicitDoc) const;

    // --- vint_dataitem_get (Sec19.2) ----------------------------------------

    // Test/setup-only entry point (see VintDataItem's own doc comment).
    // Truncates `fields` to the real 32-slot cap (CONFIRMED) rather than
    // guessing what a longer list would mean. Returns the fresh handle
    // (starts at 1, incrementing - same unconfirmed-numeric-scheme stand-in
    // convention as registerVdoObjectForTesting, a SEPARATE counter since
    // Sec19.2 confirms this is a separate handle space).
    uint32_t registerVintDataItemForTesting(std::vector<VintTaggedValue> fields);
    // Returns nullptr for a bad handle (CONFIRMED: "bad handle -> zero Lua
    // values pushed" - never populated by any Lua-visible call in this
    // pass's scope, so this is itself the real, honest default), else every
    // populated field in order.
    const std::vector<VintTaggedValue>* findVintDataItemFields(uint32_t handle) const;

    // --- vint_set_property / vint_get_property (Sec20) ----------------------
    //
    // This project's own minimal stand-in: a plain per-(resolved VDO object,
    // property-name-hash) bag of whatever raw trailing Lua values a
    // vint_set_property call was given, echoed back verbatim by
    // vint_get_property. Property names are hashed the real way (CONFIRMED,
    // Sec9.2/Sec20.1: the engine's own lower-cased string hash,
    // sr3save::nameHash here, same citation as VdoObject::nameHash above).
    // Tweens (CORRECTED 2026-10-03, spec-lua-bindings.md Sec20.1 as revised
    // by Team A's U1 pass): setting start_value/end_value on a tween writes
    // the TWEEN's own property - there is no write-through to the target
    // object. Only the target property's type code is borrowed to PARSE the
    // value. This bag already stores the value on the handle it was given,
    // so the write behaviour matches; the type-code borrowing is part of the
    // descriptor-driven parsing below, which is not implemented.
    // Not implemented yet: the real per-type property-descriptor tables
    // (Sec9.1/Sec9.2/Sec9.2a, now specified for all 13 element types by U1;
    // using them is a separate follow-up task) - so no value is parsed or
    // converted by its type/size code, values are stored exactly as the
    // Lua caller passed them - and the callback-claim mechanism (Sec20.1's
    // vint_callback_lua pool interaction, case 8), which needs those same
    // type codes.
    //
    // `setVintProperty` silently does nothing if `handle` does not resolve
    // to a registered VdoObject (CONFIRMED, Sec20.1: "an unresolvable handle
    // simply fails to resolve later" - matching vint_set_property's own
    // always-zero-Lua-values return, there is no path where this needs to
    // be visible to a caller).
    void setVintProperty(uint32_t handle, const std::string& propertyName, std::vector<VintTaggedValue> values);
    // Returns nullptr for a bad handle or a property-name miss (CONFIRMED,
    // Sec20.2: "Property not found, or bad handle -> zero Lua values pushed
    // (not even nil)"), else the exact values last set.
    const std::vector<VintTaggedValue>* findVintProperty(uint32_t handle, const std::string& propertyName) const;

    // --- vint_dataresponder_finished / vint_internal_dataresponder_request
    // (Sec21) -----------------------------------------------------------

    // Test/setup-only entry point (see VintDataResponderRecord's own doc
    // comment on why no Lua-visible function in this pass's 2-name scope
    // populates this registry).
    void registerDataResponderForTesting(const std::string& name, bool finished);
    // CONFIRMED (Sec21.1), including the real, honestly-flagged quirk: no
    // record for `name` at all -> true ("finished"), not false and not a
    // refusal - this project's never-populated-by-default registry already
    // IS that real "no record" state, so this is itself the real, honest
    // default, same "never-populated object space" precedent as VdoObject.
    bool dataResponderFinished(const std::string& name) const;
    // CONFIRMED (Sec21.2): a complete, silent no-op when no record exists
    // for `name`, or when `validCallback`/`validMax` (the caller's own
    // lua_type checks on args 2/3) are false - never creates the record
    // itself (Sec21.2's own text: "this native never creates the named
    // record"). When a record exists and both types validate, increments
    // this project's own dispatchAttempts counter (test-observable proof the
    // gate runs) - the real further dispatch (FUN_00e1e660, the trailing-
    // argument array, and the +0x25c context stamp) has no Lua-visible
    // consequence among this pass's two in-scope functions and is not
    // modeled (FUN_00e1e660's own body was not independently decompiled this
    // pass either, per Sec21.2's own explicit "not traced" note).
    void dataResponderRequest(const std::string& name, bool validCallback, bool validMax);
    int dataResponderDispatchAttempts(const std::string& name) const;

    // --- teleport_check_done / turn_to_check_done / move_to_check_done /
    // vehicle_pathfind_check_done (spec-lua-api-behaviour.md Sec35, Sec15.17,
    // Sec22.15, Sec9.10; batch 2026-10-02, the `teleport_coop` investigation -
    // Sec35's own "why" note: this is the real blocker for 3 missions,
    // dlc2_m01/m13/m19, parked forever inside `teleport_coop`, a game_lib.lua
    // script helper, NOT a native). ---------------------------------------
    //
    // The real engine's own shared "scripted request" completion pool
    // (Sec35.2): 200 records, each keyed by (object handle, kind) - kind 0 =
    // turn-to, kind 2 = move-to/pathfind (shared by BOTH move_to_check_done
    // AND vehicle_pathfind_check_done - Sec35.2/Sec22.15's own text), kind 4 =
    // teleport. Status query CONFIRMED (Sec35.2, re-derived from the raw
    // instruction stream, correcting an earlier inverted decompile-based
    // reading): 0 = a request exists and is still pending; 1 = a request
    // exists and is done (released back to the free list as a side effect of
    // THIS very read, matching the real engine's own release-on-read
    // behavior); 2 = no matching request at all (never issued, already
    // consumed, superseded, or an unresolved name). This project has no real
    // handle-resolution mechanism (this header's own top note) and models no
    // request-ALLOCATING native in scope - `teleport`, `turn_to`, `move_to`,
    // and `vehicle_pathfind_navmesh_do` all remain generic logging stubs, out
    // of this task's own scope - so the pool is keyed by the plain Lua name
    // string directly (same convention as CharacterState) and is populated
    // ONLY via registerScriptedRequestForTesting(), never by any in-scope Lua
    // call. This is not a guess: per Sec35.6's own explicitly-endorsed
    // "Minimum correct model," a host that models no asynchronous request at
    // all correctly answers "done" (status 2, no request) for every real
    // call - the honest, CONFIRMED answer for an unmodeled allocator, not a
    // simplification of the query's own CONFIRMED mechanism/polarity.
    static constexpr int kScriptedRequestKindTurnTo = 0;          // Sec35.2
    static constexpr int kScriptedRequestKindMoveOrPathfind = 2;  // Sec35.2
    static constexpr int kScriptedRequestKindTeleport = 4;        // Sec35.2

    // Test/setup-only entry point (see this section's own doc comment above
    // on why no in-scope Lua call ever populates this pool).
    void registerScriptedRequestForTesting(const std::string& name, int kind, bool done);

    // The shared status query itself (Sec35.2's own 3-code result, CONFIRMED,
    // corrected polarity), consuming (erasing) the record on a genuine "done"
    // read, exactly like the real engine's `0x006f6380`/`0x006f6040` pair.
    int scriptedRequestStatusCode(const std::string& name, int kind);

    // teleport_check_done (Sec15.17) / turn_to_check_done / move_to_check_done
    // (Sec22.15 - see that stub's own doc comment in
    // lua_spec_confirmed_stubs.cpp for what of its fuller 8-argument body is
    // deliberately NOT modeled): the shared boolean convention all three
    // push, CONFIRMED (Sec35.2): `status != 0` - true for "done" or "no
    // request," false only while genuinely pending.
    bool scriptedRequestCheckDone(const std::string& name, int kind) {
        return scriptedRequestStatusCode(name, kind) != 0;
    }

    // vehicle_pathfind_check_done (Sec9.10, the REFERENCE reading Sec35.2's
    // own cross-check cites as "already correct"): the real wrapper resolves
    // the vehicle AND checks for an occupant/part in slot 0 (most plausibly a
    // driver check, Sec9.10's own text) BEFORE ever consulting the shared
    // pool; failing either converges on the SAME literal fallback constant
    // the pool's own "no request" code already uses, 2.0 (Sec9.10: "the SAME
    // numeric code the success path itself uses... both routes converge on
    // one consistent answer"). This project has no vehicle-resolution/
    // occupant registry (same "no invented entity system" boundary as
    // CharacterState's own top note) - every name defaults UNRESOLVABLE,
    // which is itself the real, CONFIRMED fallback path for an unmodeled
    // vehicle, not a guess. setVehiclePathfindResolvableForTesting() is the
    // only way to open the gate, so a test can still exercise the shared
    // pool's own 0/1/2 polarity through this wrapper.
    void setVehiclePathfindResolvableForTesting(const std::string& vehicleName, bool resolvable);
    // Pushed AS A NUMBER by the real native (CONFIRMED, Sec9.10 - NOT a
    // boolean, unlike the three siblings above): the raw 0/1/2 status code.
    double vehiclePathfindCheckDoneCode(const std::string& vehicleName);

    // --- store_vehicle_get_state (Sec10.1) ----------------------------------

    // Real body: reads global flag `0x022cdf08`, pushes 0.0 if it reads 0,
    // else 1.0 (CONFIRMED, disassembly); HIGH CONFIDENCE that the flag's
    // real-world meaning is "is the vehicle-store UI currently in an
    // active (non-default) mode." CONFIRMED (Sec10.1/Sec26.28, 2026-10-01):
    // the flag is 0 at load (applySpecInitialState). Its three writers
    // (0x00815120 = store_vehicle_change_mode, 0x005fa820, 0x00820cd0) are
    // CONFIRMED bodies, but none is implemented here: the Lua argument shape
    // of store_vehicle_change_mode is not in the spec and the triggers of the
    // other two are OPEN, so the flag only changes from a test.
    OpenValue<bool>& vehicleStoreActive() { return vehicleStoreActive_; }

    // --- game_hud_update_inventory (Sec10.3) --------------------------------

    // Real body (CONFIRMED, disassembly): reads the current-local-player
    // accessor and, if a local player object exists, triggers a refresh
    // of that player's own HUD inventory display (OPEN: whether the real
    // refresh is a full rebuild vs. a dirty-flag set - not modeled, per
    // this task's own brief). This project has no real player-existence
    // tracking, so `hasLocalPlayer_` is this project's own minimal,
    // explicit stand-in, defaulting true ("a running game always has a
    // local player" - this project's own chosen single-instance default).
    // Test-only setter. `recordHudInventoryRefresh()`/
    // `hudInventoryRefreshCount()` are this project's own no-op stand-in
    // for "a real HUD refresh happened" (no real HUD to refresh) - a
    // plain incrementing counter, same shape choice as
    // nextAudioVoiceHandle() above.
    OpenValue<bool>& hasLocalPlayer() { return hasLocalPlayer_; }
    int hudInventoryRefreshCount() const { return hudInventoryRefreshCount_; }
    void recordHudInventoryRefresh() { ++hudInventoryRefreshCount_; }

    // --- tutorial_advance (Sec10.4) -----------------------------------------

    // Batch 2026-10-01 (spec-lua-api-behaviour.md Sec10.4, Sec6.19, Sec26.28).
    // The name resolver 0x00717780 searches the static, file-backed table of
    // 210 name pointers at 0x012f5930 case-insensitively and returns the
    // first matching index or -1 (CONFIRMED). The names are the functional
    // identifiers Sec26.28 lists (owner's content ruling 2026-10-01; kept in
    // src/lua_tutorial_names.cpp). Index 176's string is OPEN: the spec only
    // says it is shorter than 4 characters or non-ASCII.
    static constexpr int kTutorialEntryCount = 210;
    // The name at `index`, or nullptr for index 176 (OPEN) / out of range.
    static const char* tutorialName(int index);
    struct TutorialLookup {
        int index = -1;           // first known name matching, else -1
        bool couldBeIndex176 = false; // -1 only because index 176's string is OPEN
    };
    // 0x00717780 over the 209 known names. couldBeIndex176 is set when no
    // known name matches (or one matches only after index 176) and `name`
    // could be the OPEN string (shorter than 4 characters or non-ASCII).
    static TutorialLookup tutorialLookup(const std::string& name);
    // Per-entry run-time STATE (entry +0x0c of the table at 0x0151d600, 0-4,
    // not a kind - Sec6.19). Keyed by the decimal index. CONFIRMED initial
    // values (applySpecInitialState): entries 0-188 state 0, 189-209 state 1
    // after the registration fill 0x007178c0 (Sec26.28).
    OpenValueMap<int>& tutorialState() { return tutorialState_; }
    static std::string tutorialStateKey(int index) { return std::to_string(index); }
    // tutorial_advance's success path builds and dispatches a named UI
    // message (HYPOTHESIS: to the tutorial UI document); not modelled, only
    // counted per resolved index.
    int tutorialAdvanceCount(int index) const;
    void recordTutorialAdvance(int index);

    // --- on_qte_animation_trigger (Sec10.7) ---------------------------------

    // Real body (CONFIRMED, disassembly): registers (non-empty callback
    // name) or clears (empty callback name) a SINGLE GLOBAL QTE-animation-
    // trigger hook slot, rooted at a fixed global base rather than inside
    // a per-object slot array (Sec10.7) - a genuinely different shape
    // from the on_death/on_take_damage/on_revived per-CHARACTER hook-slot
    // family (CharacterState::onTakeDamageCallback/onRevivedCallback
    // above), so this project's own matching stand-in lives directly on
    // EngineState itself, not on any CharacterState. Default empty ("no
    // callback registered").
    const std::string& qteAnimationTriggerCallback() const { return qteAnimationTriggerCallback_; }
    void setQteAnimationTriggerCallback(const std::string& callbackName) { qteAnimationTriggerCallback_ = callbackName; }

    // --- game_get_coop_join_type (Sec10.9) ----------------------------------

    // Real body: reads a single global (`0x012f44fc`) holding a code `0`,
    // `1`, or `2` and pushes it back as a Lua number (CONFIRMED,
    // disassembly, full body read). The global's sole writer is real,
    // host-authoritative, and network-replicated (CONFIRMED, Sec10.9) -
    // this project builds no real networking layer (explicit project
    // convention, same as coopSession_/replicateStateChange above), so
    // `coopJoinType_` is this project's own minimal `int` stand-in,
    // default 0. Pure query among this task's 9 in-scope names (the real
    // writer is a different, out-of-scope function) - test-only setter.
    OpenValue<int>& coopJoinType() { return coopJoinType_; }

    // --- name resolution the host does not model (cloud phase 2026-09-30) --

    // Whether a name resolves to a live engine object through the resolver
    // the calling function uses (ai_add_enemy_target Sec3.9: "on successful
    // resolution of both names"; object_indicator_add_do Sec10.6). One map
    // for all resolvers - a stated simplification; OPEN until set.
    //
    // Sec29 (2026-10-02) documents the real write side this stands in for:
    // every per-kind resolver this document catalogues (Sec1.1/Sec25.29/
    // Sec28 and others) is a thin kind-filter over ONE shared global name
    // map (singleton 0x02442750's embedded sub-object at 0x02444db0), a
    // case-insensitive multiply-by-33 (0x00dab330) bucket hash, CONFIRMED
    // as a single instance of 2,999 slots (0xBB7) enabled exactly once at
    // start-up (Sec29.3) - not per-zone, not per-kind, so a name registered
    // by any producer below is resolvable by every resolver here, with no
    // per-producer distinction visible at lookup time. This map is keyed by
    // exact std::string equality (unlike the real _stricmp/hash chain) -
    // a simplification, not yet modelled as case-insensitive (Sec29.3).
    //
    // Sec29.2: FOUR producers feed names into that one map: (1) zone object
    // placement's 0x2234 "Game objects, as placed in the WE" record -
    // PARKED, lives inside the held .czn_pc object-placement interior, not
    // lifted, not read here; (2) a vehicle definition's embedded cover-node
    // batch (same loader); (3) "the bulk of the 173 call sites" - in-memory
    // records built directly by engine code (spawners, path/navpoint
    // builders, group objects), fed to 0x00456e30 - NOT individually traced
    // (Sec29.2's own words), so it could register essentially any name a
    // resolver might query; (4) a stream deserialiser (0x00a36a80),
    // HYPOTHESIS co-op replication, not Lua-reachable, replays a name that
    // already existed rather than originating one.
    //
    // Sec29.4: no Lua-bound function can ever CHOOSE a name for an object
    // it creates (unnamed, or engine-named with a generated/uniquified
    // suffix). Engine code separately registers a small CLOSED set of
    // literal names regardless of any zone/data file - "homies",
    // "shopkeepers", "-- Cutscene Script Group --", and the special-cased
    // "#PLAYER1#"/"#PLAYER2#" pair - applySpecInitialState() pre-populates
    // exactly these five as known-true, the full extent of what Sec29
    // confirms unconditionally. Everything else a mission script names by
    // a fixed authored name (e.g. 'Killbane') traces back to producer (1)
    // above - the parked .czn_pc interior - so it correctly, honestly
    // stays OPEN: since producer (3) was never individually traced and
    // backs resolvers for groups/triggers/vehicles/NPCs too, not just
    // mission characters, defaulting any OTHER unknown name to false here
    // would be inventing an answer this project does not have, not
    // reporting a confirmed one (the OpenValue/OpenStateError discipline
    // this whole map exists to honour) - do not widen this pre-population
    // beyond the five literals without new CONFIRMED spec text.
    OpenValueMap<bool>& objectResolves() { return objectResolves_; }

    // DIAGNOSTIC ONLY (tools/lua_host_run.cpp's --probe-unresolved-names
    // flag, off by default, never enabled by any normal code path or test):
    // widens objectResolves() to synthetically resolve any miss, and
    // remembers which names were synthesized so probedFieldOr() below can
    // extend the same diagnostic stance to a per-character/per-vehicle OPEN
    // field whose real value this project could never compute for a
    // SYNTHETIC name anyway (e.g. max hit points depends on the character's
    // own preset, Sec34.2 - a probe object has no preset, only a fabricated
    // "resolves" placeholder, so refusing to also fabricate ITS value would
    // just move the same "is this a real name" question one field deeper
    // without answering anything new). Real, legitimately-resolved names
    // are never affected by either mechanism.
    void enableUnresolvedNameProbe() { objectResolves_.enableProbeFallback(true, &probedNames_); }
    bool isProbedName(const std::string& name) const { return probedNames_.count(name) != 0; }
    const std::unordered_set<std::string>& probedNames() const { return probedNames_; }
    uint64_t probedFieldReadCount() const { return probedFieldReadCount_; }

    // DIAGNOSTIC ONLY (see enableUnresolvedNameProbe() above): `v` as normal
    // if known; if OPEN and `ownerName` is itself a probed (synthetic) name,
    // returns `fallback` instead of throwing (test-observably counted, never
    // silent); otherwise throws exactly as `v.get()` would (a real name's
    // own real OPEN gap is never widened by this).
    template <typename T>
    T probedFieldOr(const OpenValue<T>& v, const std::string& ownerName, const T& fallback) {
        if (v.known()) return v.get();
        if (isProbedName(ownerName)) {
            ++probedFieldReadCount_;
            return fallback;
        }
        return v.get(); // throws OpenStateError: a real name's real OPEN gap
    }

    // --- zscene (spec-lua-api-behaviour.md Sec26.25, Sec14.23, Sec8.21;
    // batch 2026-10-01, lifecycle driver + scene table 2026-10-01 nnlt text;
    // implementation in src/lua_cutscene.cpp) -------------------------------
    //
    // Scene identity: the engine keys its scene table (0x0153b294, entries
    // 0xf8 bytes) by the CRC-32 of the lower-cased name (0x00d9e8b0, seed 0)
    // and compares entry POINTERS for "current"/"pending". This host names an
    // entry by its lower-cased name (zsceneTableKey). With the real table
    // installed (installZsceneTable) a name resolves through the CRC, first
    // match, like 0x00721be0, so two names with one CRC reach one entry.
    //
    // Scene table (REAL, Sec26.25 item 5): built from cutscene.xtbl (names)
    // and one <name>.cte_xtbl per scene (fields) by sr3tables_cutscene;
    // lua_host_run installs it from cutscene_tables.vpp_pc. A name missing
    // from an installed table is "missing" (REAL). Without an installed table
    // the per-name map zsceneLoadable[key] is used, OPEN until a test sets it.
    //
    // Start-up values: 0x0153b556 the skip_all_cutscenes byte (false) and
    // 0x0153b530 the current entry ("" = null, Sec26.25 Globals, RESOLVED
    // 2026-10-02) are set by applySpecInitialState (lua_spec_initial_state.cpp).
    // CONFIRMED globals with no specced start-up value - all OPEN at start:
    //  - 0x0153b538 pending entry.
    //  - 0x0153b51c load state: 0 idle, 1 loading, 2 loaded (all CONFIRMED).
    //  - 0x0153b541 / 0x0153b542 (labels HIGH CONFIDENCE).
    //  - the soundtrack stream 0x0153b71c, its status and its timer 0x0153b724.
    // OPEN because the host has no streaming, audio or world objects (each a
    // labelled, test-settable OpenValue; never a silent default):
    //  - the class 0x00dafb60 gives an entry's selected request-group handle
    //    (1 not live, 3 resident, 5 failed, anything else: wait). Per entry.
    //    The selection between +0xc and +0x10 (the female variant) is folded
    //    into this one "selected handle" class - a stated simplification.
    //  - whether the soundtrack stream has ended (status 0x66).
    //  - the nearest scene-bearing world object (list 0x03171a64) the idle
    //    driver 0x00728440 would auto-prep ("" = none found).
    struct ZsceneTableRow {
        std::string name;               // +0x0 as written in cutscene.xtbl
        uint32_t crc = 0;               // +0x4: NameHash(lower-cased name, 0)
        std::optional<int> kind;        // +0x8 (nullopt: OPEN, see sr3tables_cutscene)
        // +0x20 resources (names for the host's 0x00722f10 load log);
        // nullopt: OPEN (the spec describes the list only for kinds 1 and 2).
        std::optional<std::vector<std::string>> resources;
    };
    void installZsceneTable(std::vector<ZsceneTableRow> rows);
    bool zsceneTableInstalled() const { return zsceneTableInstalled_; }
    size_t zsceneTableSize() const { return zsceneTable_.size(); }

    OpenValueMap<bool>& zsceneLoadable() { return zsceneLoadable_; }
    OpenValue<bool>& zsceneSkipAllCutscenes() { return zsceneSkipAllCutscenes_; }
    OpenValue<std::string>& zsceneCurrent() { return zsceneCurrent_; }
    OpenValue<std::string>& zscenePending() { return zscenePending_; }
    OpenValue<int>& zsceneStateCode() { return zsceneStateCode_; }
    OpenValue<bool>& zsceneAutoSelectNearest() { return zsceneAutoSelectNearest_; }
    OpenValue<bool>& zsceneRequeueOnReset() { return zsceneRequeueOnReset_; }
    OpenValueMap<int>& zsceneHandleClass() { return zsceneHandleClass_; }
    OpenValue<bool>& zsceneSoundtrackActive() { return zsceneSoundtrackActive_; }
    OpenValue<int64_t>& zsceneSoundtrackStartMs() { return zsceneSoundtrackStartMs_; }
    OpenValue<bool>& zsceneSoundtrackEnded() { return zsceneSoundtrackEnded_; }
    OpenValue<std::string>& zsceneNearestWorldObjectScene() { return zsceneNearestWorldObjectScene_; }
    static std::string zsceneTableKey(const std::string& name);
    // zscene_prep (Sec8.21 / Sec26.25 lifecycle step 1-2, CONFIRMED): the gate
    // 0x007232e0, the stub 0x0101b530 (nothing), the teardown
    // 0x00721c20(1, 0, 0) of the current scene, then pending := entry and
    // the two per-object parameters := the zero constants. All reads happen
    // before any write, so an OPEN read refuses with no partial update.
    void zscenePrep(const std::string& name);
    // zscene_is_loaded (Sec14.23 corrected truth table, CONFIRMED). hasName
    // is true for a string or number argument. Throws OpenStateError on an
    // OPEN read. For the PENDING entry the engine answers false until the
    // per-frame driver promotes it (cutsceneHostFrame); when the host's most
    // recent cutscene frame stopped on an OPEN read, that promotion cannot
    // happen in this host, so the call refuses naming that read instead of
    // returning a false with no modelled end. Counted.
    bool zsceneIsLoaded(bool hasName, const std::string& name);
    int zscenePendingBlockedRefusals() const { return zscenePendingBlockedRefusals_; }

    // --- cutscene machine (Sec26.25 item 6, "The cutscene machine proper";
    // src/lua_cutscene.cpp) ---------------------------------------------------
    //
    // The engine runs, once per frame, the state machine 0x0072d660 (jump
    // table of 20 states) and then its second step 0x007258a0 (five cases:
    // 0, 2, 4, 5, 15). The cutscene state is 0x0153b520 (ScreenFade::
    // cutsceneState, shared with the fade routine's logo test). States 0..0xf
    // are CONFIRMED; 0x10..0x13 are HIGH CONFIDENCE (by elimination) and are
    // refused, as are the states whose helper bodies are OPEN (6..14) and
    // the continuation of 0x00725df0 after state 2's load wait.
    struct CutsceneManager { // *0x0153b528
        OpenValue<bool> present{"*0x0153b528 non-null (cutscene manager)", "spec-lua-api-behaviour.md Sec26.25"};
        // Host identity of the scene the manager plays (the engine holds it in
        // the manager object; which field is not specced).
        OpenValue<std::string> sceneKey{"cutscene manager's scene entry", "spec-lua-api-behaviour.md Sec26.25"};
        OpenValue<int32_t> field8{"cutscene manager +8", "spec-lua-api-behaviour.md Sec26.25"};
        OpenValue<std::string> chainTarget{"cutscene manager +0x35cc (next cutscene)", "spec-lua-api-behaviour.md Sec26.25"};
    };
    CutsceneManager& cutsceneManager() { return cutsceneManager_; }
    OpenValue<int32_t>& cutsceneState() { return screenFade_.cutsceneState; }
    OpenValue<bool>& cutscenePreloadMounted() { return cutscenePreloadMounted_; }   // 0x0153b543
    OpenValue<std::string>& cutsceneChainTarget() { return cutsceneChainTarget_; }  // 0x0153b52c ("" = none)
    OpenValue<bool>& cutscenePlayerChecksPass() { return cutscenePlayerChecksPass_; }
    OpenValue<int64_t>& cutsceneLoadStamp() { return cutsceneLoadStamp_; }          // 0x012f5d30
    OpenValue<bool>& cutscenePlayingByte() { return cutscenePlayingByte_; }         // 0x0153b525
    OpenValue<bool>& cutsceneInProgressByte() { return cutsceneInProgressByte_; }   // 0x0153b526
    // Host logs of what states 4 and 5 started (no packfile mounting or
    // resource streaming in this host).
    const std::vector<std::string>& cutsceneMountLog() const { return cutsceneMountLog_; }
    const std::vector<std::string>& cutsceneLoadLog() const { return cutsceneLoadLog_; }

    // One host frame of the cutscene machine and the zscene lifecycle, in
    // this order (Sec26.25): 0x0072d660's case for the current state; then
    // 0x007258a0 (bytes 0x0153b525/0x0153b526, its case: in states 0 and 2
    // the reset-check 0x00720320 - run whether or not an entry is pending -
    // and, when it is true, promotion of the pending zscene, mounts in 4,
    // loads in 5, chain/teardown in 15); then the zscene completion
    // 0x007285c0. The Host summary of Sec26.25 states "every frame: promotion
    // ..., then completion"; the engine calls 0x007285c0 from two cases of
    // 0x0072d660 (which two is not stated), so running it every frame after
    // the promotion is the host summary's instruction, not a reading of
    // 0x0072d660. Uses the host clock screenFadeClockMs() (advanced by
    // screenFadeHostFrame, which lua_host_run calls first each tick). The
    // first OPEN read stops the frame (each step reads before it writes);
    // counted, with the blocking global.
    void cutsceneHostFrame();
    struct CutsceneCounters {
        uint64_t framesRun = 0;
        uint64_t framesBlockedOnOpen = 0;
        bool lastFrameBlocked = false;
        std::string lastFrameBlocker;
        uint64_t stateTransitions = 0;
        uint64_t zscenePromotions = 0;   // 0x00720410 runs
        uint64_t zsceneCompletions = 0;  // 0x007285c0: state := 2
        uint64_t zsceneFailedLoads = 0;  // 0x007285c0: class 5 -> teardown
        uint64_t zsceneIdleAutoPreps = 0; // 0x00728440 -> 0x007232e0
    };
    const CutsceneCounters& cutsceneCounters() const { return cutsceneCounters_; }
    // "frames:N blocked_on_open:N promotions:N completions:N ..." for lua_host_run.
    std::string cutsceneFrameSummary() const;

    // --- screen fade (spec-lua-api-behaviour.md Sec26.24, Sec26.9, Sec2.9,
    // Sec8.13; batch 2026-10-01; implementation in src/lua_screen_fade.cpp) --
    //
    // CONFIRMED (Sec26.24): one state dword 0x012e6aa4 (0 fading in, 1 fading
    // out, 2 fully in - the start-up state, 3 fully out) and a target
    // 0x012e6aa8 (3 out, 2 in). The request helpers 0x0059f8c0 (out) and
    // 0x0059fc40 (in) only set the direction and call the UI-state Lua global
    // screen_fade_do(flag, alpha, durationMs); a fade completes when the UI
    // script calls the UI-state native Screen_fade_transition_complete
    // (0x005a0110), which this host registers in the UI state. A request
    // parked while the opposite transition runs is replayed by the
    // per-frame routine 0x0059fe70 with 250 ms.
    //
    // Every global below is an OpenValue: applySpecInitialState sets the
    // file-backed initial values Sec26.24 lists; the mode-stack top and the
    // cutscene state the per-frame routine reads are OPEN.
    using FadeCallback = void (*)(EngineState& es, uint32_t target);
    struct ScreenFade {
        OpenValue<uint32_t> state{"0x012e6aa4 (fade state)", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<uint32_t> target{"0x012e6aa8 (fade target)", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<uint32_t> flag{"0x013effc8 (fade flag)", "spec-lua-api-behaviour.md Sec26.24"};
        // 0x012e6aa0 != -1: the "screen_fade" UI document record is loaded
        // (its id is HIGH CONFIDENCE as a label). -1 in the file.
        OpenValue<bool> documentLoaded{"0x012e6aa0 != -1 (screen_fade document id)", "spec-lua-api-behaviour.md Sec26.24"};
        // Millisecond stamps, -1 = unset (Sec26.24 table).
        OpenValue<int64_t> logoAt{"0x012e6aac (show loading logo at)", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<int64_t> holdLogoUntil{"0x012e6ab0 (hold fade-in until, logo)", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<int64_t> imagesAt{"0x012e6ab4 (show load images at)", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<int64_t> holdImagesUntil{"0x012e6ab8 (hold fade-in until, images)", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<int64_t> autoSaveStamp{"0x012e6abc (auto-save indicator stamp)", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<int32_t> autoSaveCounter{"0x013effd4 (auto-save counter)", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<bool> useLoadImages{"0x0149365c (sfx_use_load_images byte)", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<bool> lastBroadcastWasOut{"0x013effc5 (last 0x53 record was a fade-out)", "spec-lua-api-behaviour.md Sec26.24"};
        // Read by the per-frame routine; OPEN (no spec value).
        OpenValue<int32_t> modeStackTop{"mode stack top (0x00706ab0: 0x01503b50[0x012f4a80])", "spec-lua-api-behaviour.md Sec26.24"};
        OpenValue<int32_t> cutsceneState{"0x0153b520 (cutscene state)", "spec-lua-api-behaviour.md Sec26.24/Sec26.25"};
        // 0x013effcc in-flight and 0x013effd0 deferred completion callbacks:
        // 0 in the file (CONFIRMED); the Lua wrappers always pass 0.
        FadeCallback inFlight = nullptr;
        FadeCallback deferred = nullptr;
    };
    ScreenFade& screenFade() { return screenFade_; }
    const ScreenFade& screenFade() const { return screenFade_; }

    // The UI Lua state that stands in for the "screen_fade" document: where
    // screen_fade_do and the other screen_fade_* script globals are looked
    // up. Set when Screen_fade_transition_complete is registered there.
    void attachScreenFadeUiState(lua_State* ui) { screenFadeUi_ = ui; }
    lua_State* screenFadeUiState() const { return screenFadeUi_; }
    // Init 0x0059fa30 (CONFIRMED body): document found -> id loaded, state :=
    // 2, target := 2, flag := 1; not found -> id stays -1.
    void screenFadeInit(bool documentFound);

    // Request helpers 0x0059f8c0 (out = true) / 0x0059fc40 (out = false),
    // CONFIRMED full bodies, (durationMs, completionCallback, flag). The
    // audio-id posts are HYPOTHESIS and not modelled. Throws OpenStateError
    // (before any write) on an OPEN read.
    void screenFadeRequest(bool out, int32_t durationMs, FadeCallback cb, uint32_t flag);
    // Body of Screen_fade_transition_complete 0x005a0110 (CONFIRMED).
    // Returns true when it flipped a running transition (0 -> 2, 1 -> 3).
    bool screenFadeCompletionBody();
    // The Lua-visible native: the body, counted as the real completion path.
    void screenFadeCompletionNative();

    // Host frame. Advances the host's millisecond clock (the engine's clock
    // 0x01320d9c; this host's clock starts at 0 and moves only here), then:
    //  1. HOST-SIDE SUBSTITUTE (not engine behaviour; Sec26.24 allows it and
    //     requires the label): a transition still running durationMs after
    //     its request started it is completed by running the completion
    //     body. Only used when screen_fade_do was not a function in the UI
    //     state (or the document is not loaded) - fallback_undefined - or
    //     was called but never called Screen_fade_transition_complete -
    //     fallback_no_callback. The preferred path is the script's own call.
    //  2. the per-frame routine 0x0059fe70 (CONFIRMED body). An OPEN read
    //     stops that frame's routine; counted (screenFadeFramesBlockedOnOpen).
    void screenFadeHostFrame(int64_t elapsedMs);
    int64_t screenFadeClockMs() const { return screenFadeClockMs_; }

    struct ScreenFadeCounters {
        uint64_t realCompletions = 0;         // the UI script's own call flipped the state
        uint64_t fallbackUndefined = 0;       // substitute: screen_fade_do undefined / no document
        uint64_t fallbackNoCallback = 0;      // substitute: screen_fade_do called, no completion
        uint64_t screenFadeDoCalls = 0;
        uint64_t screenFadeDoErrors = 0;      // Lua errors inside screen_fade_do (swallowed)
        uint64_t uiScriptCalls = 0;           // screen_fade_logo_show/images_show/auto_save_*
        uint64_t framesRun = 0;
        uint64_t framesBlockedOnOpen = 0;
        std::string lastFrameBlocker;         // the OPEN global that last stopped a frame
        std::string lastScreenFadeDoError;
    };
    const ScreenFadeCounters& screenFadeCounters() const { return screenFadeCounters_; }
    // "real:N fallback_undefined:N fallback_no_callback:N" (lua_host_run's
    // verdict_summary.txt line fade_completion_path=...).
    std::string screenFadeCompletionPathSummary() const;

    // --- fade_out (spec-lua-api-behaviour.md Sec2.9) ------------------------

    // CONFIRMED: the three colour components (0.0 when absent) plus a fixed
    // alpha of 255 go to the overlay colour setter, which scales each by
    // 1/255. Stored here already scaled. hasScreenFadeColour() is false
    // until the first fade_out: the real initial overlay colour is not in
    // any spec.
    struct ScreenFadeColour {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    };
    bool hasScreenFadeColour() const { return hasScreenFadeColour_; }
    const ScreenFadeColour& screenFadeColour() const { return screenFadeColour_; }
    void setScreenFadeColour(const ScreenFadeColour& c) { screenFadeColour_ = c; hasScreenFadeColour_ = true; }

    // Every request a Lua wrapper (fade_out / fade_in bit 0x1) handed to the
    // request helper, in call order: trunc(seconds x 1000) and the alpha the
    // helper passes to screen_fade_do (1.0 out, 0.0 in) - a log for tests.
    struct ScreenFadeRequest {
        double durationMs = 0.0;
        float targetAlpha = 0.0f;
    };
    const std::vector<ScreenFadeRequest>& screenFadeRequests() const { return screenFadeRequests_; }
    void recordScreenFadeRequest(const ScreenFadeRequest& r) { screenFadeRequests_.push_back(r); }
    // Bit 0x2: the broadcast helpers 0x005a0270 / 0x005a0400 (CONFIRMED):
    // nothing unless the host gate holds (session present, +0x5c == +0x58);
    // then an opcode-0x53 record to the peers (no networking here: counted)
    // and byte 0x013effc5 := 1 (out) / 0 (in). Throws on an OPEN gate read.
    void screenFadeBroadcast(bool out);
    int screenFadeOpcode53Count() const { return screenFadeOpcode53Count_; }

    // --- UI resolution queries (spec-lua-api-behaviour.md Sec26.26) ---------

    // vint_is_std_res (0x00e1a150, CONFIRMED rule): the two signed integers
    // of the per-thread record (0x00e236f0 +4/+8) divided in double
    // precision; < 1.5 -> true, else true only when the display mode
    // 0x0132bd80 == 2. CONFIRMED (nnlt): the record's integers are copies of
    // the global width 0x02a5a180 / height 0x02a5a184, written by the UI
    // subsystem init 0x00e23910 (front-end bring-up) and the resolution-change
    // path 0x00e23a20, which both recompute the mode (0x00e23000) and the
    // layout index 0x0132c0ac. Before either writer runs all of these are
    // OPEN (the record is allocated zeroed on first use, but scripts only run
    // after the init). The record is per thread (HIGH CONFIDENCE: filled on
    // the UI thread only); this host has one thread and one record.
    OpenValue<int32_t>& vintRecordFirst() { return vintRecordFirst_; }
    OpenValue<int32_t>& vintRecordSecond() { return vintRecordSecond_; }
    OpenValue<int32_t>& vintDisplayMode() { return vintDisplayMode_; }
    OpenValue<int32_t>& vintGlobalWidth() { return vintGlobalWidth_; }
    OpenValue<int32_t>& vintGlobalHeight() { return vintGlobalHeight_; }
    OpenValue<int32_t>& vintLayoutIndex() { return vintLayoutIndex_; }
    bool vintIsStdRes() const;
    // 0x00e23000(a, b) (CONFIRMED ladder): a / b in single precision, mode
    // -1, raised by strict tests > 2.48 -> 0, > 3.18 -> 1, > 3.72 -> 2,
    // > 4.77 -> 3, > 5.58 -> 4. 2.48 is the single at 0x01256908 (CONFIRMED);
    // the four doubles are HIGH CONFIDENCE "as rendered", so a ratio above
    // 2.48 (where they decide the mode) is refused as OPEN. Every ratio at or
    // below 2.48 - any single display - gives -1 from CONFIRMED values only.
    static int32_t vintDisplayModeLadder(int32_t a, int32_t b);
    // 0x00e2ad30 (CONFIRMED: the same rule as vint_is_std_res on the given
    // pair and mode): 1 = standard, 0 = wide (labels HIGH CONFIDENCE).
    static int32_t vintLayoutIndexFor(int32_t width, int32_t height, int32_t mode);
    // UI subsystem init 0x00e23910 (CONFIRMED, called by 0x008489e0 during
    // front-end bring-up; the pair comes from its second argument - the
    // display resolution, a HOST INPUT here: lua_host_run's --display). Stores
    // the globals and the record copy, then the mode and the layout index.
    // A ratio the ladder refuses leaves the mode and layout index OPEN.
    void vintUiSubsystemInit(int32_t width, int32_t height);
    // 0x00e23a20(width, height) (CONFIRMED; caller 0x005df6e0, HYPOTHESIS the
    // resize handler; what triggers it is OPEN, so nothing in lua_host_run
    // calls it). Unchanged pair: nothing, returns false. Otherwise: globals,
    // record copy, mode, layout index; then the UI-state Lua global
    // vint_lib_init_constants() if it is a function; and when the
    // standard/wide class flipped, <document name>_reset() for every loaded
    // UI document - this host has no UI document list, so those calls are
    // only counted (vintDocumentResetsNotModelled). Throws before writing on
    // an OPEN read.
    bool vintResolutionChange(int32_t width, int32_t height);
    int vintLibInitConstantsCalls() const { return vintLibInitConstantsCalls_; }
    int vintDocumentResetsNotModelled() const { return vintDocumentResetsNotModelled_; }
    // vint_get_safe_frame (0x00e1b570): shape CONFIRMED (4 numbers from the
    // integers a = +0x8, b = +0xc of (thread context +0x674)+0x14, scaled by
    // two double constants and rounded); order (c1*a, c1*b, c2*a, c2*b) HIGH
    // CONFIDENCE. The constants are CONFIRMED (nnlt): 0x0115ba60 =
    // 0x3FB3333340000000 (0.075f widened), 0x0116dfc0 = 0x3FED9999A0000000
    // (0.925f widened); applySpecInitialState sets them. a and b stay OPEN:
    // the object at (context +0x674)+0x14 has no specced writer (reading it
    // as width/height is HIGH CONFIDENCE only).
    OpenValue<int32_t>& vintSafeFrameA() { return vintSafeFrameA_; }
    OpenValue<int32_t>& vintSafeFrameB() { return vintSafeFrameB_; }
    OpenValue<double>& vintSafeFrameScale1() { return vintSafeFrameScale1_; }
    OpenValue<double>& vintSafeFrameScale2() { return vintSafeFrameScale2_; }
    // The two constants as their CONFIRMED bit patterns.
    static constexpr uint64_t kSafeFrameScale1Bits = 0x3FB3333340000000ull; // 0x0115ba60
    static constexpr uint64_t kSafeFrameScale2Bits = 0x3FED9999A0000000ull; // 0x0116dfc0
    static double doubleFromBits(uint64_t bits);
    // round(c * v) to nearest (Sec26.26 "round to nearest"). With the
    // CONFIRMED constants no integer below 2^23 ties; an exact tie (mode not
    // stated) or a result outside int32 is refused as OPEN.
    static double vintSafeFrameRound(double product);

    // --- mission_end_silently (Sec15.23) ------------------------------------

    // The global mission-flags word 0x014c848c. CONFIRMED: mission_end_silently
    // always sets bit 0x4 and sets/clears bit 0x10 to mirror its argument
    // (meaning of both OPEN). Every other bit, and the initial value, is OPEN:
    // only the bits a confirmed writer has written are known (open_state.h).
    OpenBits32& missionFlagsWord() { return missionFlagsWord_; }

    // --- Batch 2026-10-01, spec-lua-api-behaviour.md Sec27 (25 UI-cluster
    // functions, "ranks 551-650" tranche part D) / Sec28 (25 gameplay-cluster
    // functions, part C) - see src/lua_spec_confirmed_stubs.cpp's own per-
    // function doc comment for each field's full reasoning/citation; only a
    // short pointer is kept here. Same conventions as every batch above:
    // OpenValue/OpenValueMap for a real engine slot this project cannot know a
    // value for (OPEN until a test sets it; applySpecInitialState sets the few
    // CONFIRMED initial values); a plain field (default member initializer)
    // for a CONFIRMED initial/file value or a value that is write-only within
    // this batch's own scope (CHOSEN stand-in, explicitly labelled below).
    // -------------------------------------------------------------------

    // Sec27.1 `cat_mouse_results_select` / Sec27.25 `game_get_in_progress_type`
    // (both read the same 0x014c2d10 singleton).
    struct CatMouseMinigame {
        OpenValue<bool> present{"0x014c2d10 (cat-and-mouse minigame singleton) non-null",
                                "spec-lua-api-behaviour.md Sec27.1/Sec27.25"};
        OpenValue<bool> fieldIc1IsOne{"minigame object +0x1c == 1", "spec-lua-api-behaviour.md Sec27.1"};
        int32_t selection = 0; // the object's own +0xe0 field this batch writes; CHOSEN, write-only in scope
        uint64_t sessionMessageCount = 0; // "message to co-op session" (0x0086f1b0) - no real networking layer, counted only
    };
    CatMouseMinigame& catMouseMinigame() { return catMouseMinigame_; }

    // Sec27.2 `cell_is_mission_complete` (0x00a525a0, same function as Sec20.14's `mission_is_complete`):
    // keyed by mission name - the resolved object's own +0x88 bit 0x4 (resolve itself reuses objectResolves()).
    OpenValueMap<bool>& missionComplete() { return missionComplete_; }

    // Sec27.3 `Completion_should_wait_for_coop` / Sec27.4 `Completion_user_is_done_viewing`.
    struct CompletionScreen {
        OpenValue<bool> present{"'completion_screen' UI context resolved (0x00878ca0)",
                                "spec-lua-api-behaviour.md Sec27.3/Sec27.4"};
        OpenValue<bool> flagClusterSet{"0x02282282 (completion-screen flag cluster, 3rd member) nonzero",
                                       "spec-lua-api-behaviour.md Sec27.3"};
        OpenValue<bool> derefByteAtLeastOne{"resolved context's own double-pointer-deref +4 byte >= 1",
                                            "spec-lua-api-behaviour.md Sec27.3"};
        OpenValue<bool> everyPlayerAtThreshold1{
            "0x00877ca0(context,1): every player's per-slot state >= 1 (per-player walk, abstracted to one flag)",
            "spec-lua-api-behaviour.md Sec27.3"};
        OpenValue<bool> recordEnabled{"context +4 record's own +2 byte nonzero (gates Sec27.4's write)",
                                      "spec-lua-api-behaviour.md Sec27.4"};
        int32_t lastStateWritten = -1; // Sec27.4's own state byte (always 1 when it writes) - CHOSEN, write-only in scope
        uint64_t messageSentCount = 0; // Sec27.4's own "sends a message" side effect - counted only
    };
    CompletionScreen& completionScreen() { return completionScreen_; }

    // Sec27.5 `vcust_set_camera_pos`.
    struct VcustCamera {
        OpenValue<bool> targetPresent{"current vehicle-customization target identity (0x00812e40)",
                                      "spec-lua-api-behaviour.md Sec27.5"};
        OpenValue<bool> targetValid{"target capability+class gate (+0x33 bit 0x10 clear AND class bit 0x80@+6 set)",
                                    "spec-lua-api-behaviour.md Sec27.5"};
        OpenValue<bool> targetAlive{"0x00853b10 alive (true = alive; see Sec27.5's own resolved polarity note)",
                                    "spec-lua-api-behaviour.md Sec27.5"};
        // The real CRC-32 of the preset name (sr3save::nameHash - CONFIRMED match to 0x00d9e8b0, see this
        // header's own top note); the 0x00a9b080 positioning math itself is OPEN and not simulated - this is
        // the one real, checkable number this project can reproduce exactly.
        uint32_t lastAppliedPresetHash = 0;
        bool isStandardActive = true; // 0x01300b88, CONFIRMED file value 1
    };
    VcustCamera& vcustCamera() { return vcustCamera_; }

    // Sec27.6 `pause_menu_has_seen_display_cal_screen`: 0x02297c80, CONFIRMED zero at start; no in-scope setter.
    bool& pauseMenuSeenDisplayCalScreen() { return pauseMenuSeenDisplayCalScreen_; }
    // Sec27.7 `msn_text_adventure_set_screen`: 0x01301124, CONFIRMED -1 initial.
    int32_t& textAdventureScreenIndex() { return textAdventureScreenIndex_; }
    // Sec27.8 `horde_results_set_end_action`: 0x012f3b48, CONFIRMED initial 2.
    int32_t& hordeResultsEndAction() { return hordeResultsEndAction_; }

    // Sec27.9 `garage_preview_vehicle`.
    struct GaragePreview {
        OpenValueMap<int32_t> vehicleTypeAtIndex{"0x014a1a80[index] (vehicle-type/model id table)",
                                                 "spec-lua-api-behaviour.md Sec27.9"};
        OpenValue<int32_t> previewCache{"0x014a1d1c (cached preview model id)", "spec-lua-api-behaviour.md Sec27.9"};
        uint64_t refreshCount = 0; // times 0x005f8670 would have fired - internals OPEN, counted only
    };
    GaragePreview& garagePreview() { return garagePreview_; }

    // Sec27.10 `game_lobby_coop_finished`: 0x0224244c, CONFIRMED zero-filled; reuses the existing
    // screenFadeRequest()/Sec26.24 mechanism for its own fade-out(-1, null, 1) call.
    bool& coopLobbyFinished() { return coopLobbyFinished_; }

    // Sec27.11 `dialog_box_force_close`.
    struct DialogForceClose {
        OpenValueMap<bool> slotMatchesId{
            "dialog slot holds this id (self-consistency gate shared with dialog_box_set_result, Sec23.18)",
            "spec-lua-api-behaviour.md Sec27.11"};
        OpenValueMap<bool> timerArmedNotExpired{"dialog slot +0xc timer armed and not expired (0x00d9e4c0/0x00d9e400)",
                                                "spec-lua-api-behaviour.md Sec27.11"};
        OpenValueMap<bool> alreadyClosing{"dialog slot +0x13b already closing", "spec-lua-api-behaviour.md Sec27.11"};
        OpenValueMap<bool> hasResultCallback{"dialog slot +0x13c result callback present",
                                             "spec-lua-api-behaviour.md Sec27.11"};
        OpenValueMap<bool> callbackNameBlank{"dialog slot +0x140 name blank (0x00da73c0)",
                                             "spec-lua-api-behaviour.md Sec27.11"};
        OpenValueMap<bool> fullyRemoved{"dialog slot +0x138 fully removed", "spec-lua-api-behaviour.md Sec27.11"};
        uint64_t closePendingCount = 0;
        uint64_t resultCallbackFiredCount = 0;
        uint64_t namedCallbackDispatchCount = 0;
        uint64_t slotResetCount = 0;
    };
    DialogForceClose& dialogForceClose() { return dialogForceClose_; }

    // Sec27.12 `game_autosave`.
    struct Autosave {
        OpenValue<bool> suppressFlag{"0x012fcadc (CONFIRMED file value 1 = suppressed)",
                                     "spec-lua-api-behaviour.md Sec27.12"};
        OpenValue<bool> secondFlagSet{"0x0290ceca nonzero", "spec-lua-api-behaviour.md Sec27.12"};
        OpenValue<bool> missionActive{"0x006cecb0 (mission active)", "spec-lua-api-behaviour.md Sec27.12"};
        OpenValue<bool> thirdGateBlocks{"0x006f8370 result (itself OPEN per spec)", "spec-lua-api-behaviour.md Sec27.12"};
        OpenValue<bool> fourthFlagNonzero{"0x0229a317 nonzero", "spec-lua-api-behaviour.md Sec27.12"};
        uint64_t triggeredCount = 0; // 0x00b94ff0 itself OPEN; counts "every gate passed, would have saved"
    };
    Autosave& autosave() { return autosave_; }

    // Sec27.13/Sec27.14/Sec27.21/Sec27.22: all bounds-check against the SAME 0x02289b94 slot count.
    struct PlayerSlots {
        OpenValue<uint32_t> count{"0x02289b94 (player slot count)",
                                  "spec-lua-api-behaviour.md Sec27.13/Sec27.14/Sec27.21/Sec27.22"};
        OpenValueMap<bool> sendInviteOk{"0x0088e690(record,0) == 0", "spec-lua-api-behaviour.md Sec27.13"};
        OpenValueMap<bool> canSendInvite{"0x0088dfd0 result", "spec-lua-api-behaviour.md Sec27.14"};
        OpenValueMap<bool> joinFriendInProgressOk{"0x0088e040 result", "spec-lua-api-behaviour.md Sec27.22"};
        uint64_t gamercardShownCount = 0; // Sec27.21: 0x00870810 internals OPEN, counted only
    };
    PlayerSlots& playerSlots() { return playerSlots_; }

    // Sec27.15 `game_set_coop_friendly_fire` / Sec27.16 `game_get_coop_friendly_fire`:
    // 0x012f4500, CONFIRMED file value 1. The setter's own host/solo write gate reuses coopSession() above.
    OpenValue<int32_t>& coopFriendlyFireRaw() { return coopFriendlyFireRaw_; }

    // Sec27.19 `game_is_signed_in`: 0x0086fdd0 -> 0x008703b0, Steam user interface pointer (0x0101c54c) non-null.
    OpenValue<bool>& steamInterfaceAvailable() { return steamInterfaceAvailable_; }

    // Sec27.20 `game_sign_into_network`: this project has no real platform SDK - only the call's own raw
    // arguments are recorded, honestly, as "a request" (same convention as MinimapIconRecord/ObjectIndicatorRecord
    // above), since the real async-callback registration is a confirmed no-op stub on this platform anyway.
    struct SignInRequest {
        bool wantSignIn = false;
        std::string callbackName;
    };
    std::vector<SignInRequest>& signInRequests() { return signInRequests_; }
    uint64_t& signInErrorDialogCount() { return signInErrorDialogCount_; } // CONFIRMED unconditional every call

    // Sec27.23 `game_coop_start_new_live` / Sec27.24 `game_coop_start_new_syslink`:
    // 0x014ff6c1/0x014ff6c2, CONFIRMED zero-filled, mutually exclusive.
    bool& coopLiveSessionActive() { return coopLiveSessionActive_; }
    bool& coopSyslinkSessionActive() { return coopSyslinkSessionActive_; }

    // Sec27.25 `game_get_in_progress_type`'s other two branches (the minigame branch reuses catMouseMinigame()).
    struct InProgressType {
        OpenValue<bool> activeMissionPresent{"0x014c8460 non-null and phase 0x014c7a14 != 8",
                                             "spec-lua-api-behaviour.md Sec27.25"};
        OpenValue<int32_t> activeMissionType{"active mission's own +0xa0 type field", "spec-lua-api-behaviour.md Sec27.25"};
        OpenValue<bool> activityPresent{"phase 6/7 with 0x014c8328 non-null", "spec-lua-api-behaviour.md Sec27.25"};
        OpenValue<int32_t> activityType{"that object's own +0xa0 field", "spec-lua-api-behaviour.md Sec27.25"};
    };
    InProgressType& inProgressType() { return inProgressType_; }

    // Sec28.1 `helicopter_set_dont_death_spiral`: the vehicle's own flag this batch writes (gated on
    // objectResolves()); CHOSEN plain map, write-only in scope (either real write path converges on this one
    // Lua-visible flag per this batch's own no-networking-layer convention).
    std::unordered_map<std::string, bool>& helicopterDontDeathSpiral() { return helicopterDontDeathSpiral_; }

    // Sec28.2 `helicopter_fly_to_set_goal_direction`.
    struct HelicopterFlyTo {
        OpenValueMap<bool> qualifies{"0x00ad31a0: heli AI record +0x2c in {3,4}", "spec-lua-api-behaviour.md Sec28.2"};
        OpenValueMap<bool> applyGate{"heli AI record (0x00a79470) +0x4b0 == 1", "spec-lua-api-behaviour.md Sec28.2"};
        struct Request {
            std::string targetName;
            bool useOrientationMode = false;
            bool applied = false;
        };
        // The real position/orientation arithmetic (Sec28.2's own body) is not reproduced - nothing in this
        // batch's scope reads a computed vector back, so only the real BRANCH taken is recorded, honestly, not
        // invented numbers (same "record the request" convention as MinimapIconRecord above).
        std::unordered_map<std::string, Request> lastRequest;
    };
    HelicopterFlyTo& helicopterFlyTo() { return helicopterFlyTo_; }

    // Sec28.3 `hdr_bloom_set_multiplier`: 0x012ec280, CONFIRMED initial 1.0.
    float& hdrBloomMultiplier() { return hdrBloomMultiplier_; }

    // Sec28.4 `guardian_angel_enable_indicators`.
    struct GuardianAngel {
        OpenValue<bool> modeIsThree{"0x006d2910 mode == 3 (via 0x00614d00, OPEN what the mode means)",
                                    "spec-lua-api-behaviour.md Sec28.4"};
        OpenValue<bool> objectPresent{"0x00614cb0() non-null", "spec-lua-api-behaviour.md Sec28.4"};
        bool indicatorsEnabled = false; // the object's own +0x57d byte this batch writes; CHOSEN, write-only in scope
    };
    GuardianAngel& guardianAngel() { return guardianAngel_; }

    // Sec28.5 `group_get_next_npc` / Sec28.6 `group_get_first_npc`: an empty known string means "no
    // next/no members" (the real null-pointer/wraparound-stop case); unknown means OPEN.
    OpenValueMap<std::string>& groupNextNpcName() { return groupNextNpcName_; }   // keyed by current NPC name
    OpenValueMap<std::string>& groupFirstNpcName() { return groupFirstNpcName_; } // keyed by group name

    // Sec28.7 `get_num_humans_in_trigger`.
    OpenValueMap<int32_t>& humansInTriggerCount() { return humansInTriggerCount_; }

    // Sec28.8 `get_char_vehicle_is_in_air`.
    OpenValueMap<bool>& vehicleInAirByCharacter() { return vehicleInAirByCharacter_; }

    // Sec28.9 `effect_play_finisher`.
    struct EffectFinisher {
        OpenValue<bool> gamepadMode{"gamepad flag 0x0141250d", "spec-lua-api-behaviour.md Sec28.9"};
        OpenValueMap<bool> effectIndexValid{"0x005c50b0 icon effect index != -1, keyed by icon name",
                                            "spec-lua-api-behaviour.md Sec28.9"};
        OpenValue<double> successReturn{"0x00a48af0's own return value (Sec9.1/Sec12.10/Sec12.12 family, not modelled further)",
                                        "spec-lua-api-behaviour.md Sec28.9"};
    };
    EffectFinisher& effectFinisher() { return effectFinisher_; }

    // Sec28.10 `dlc3_m03_set_sprint_waning`: 0x0263ae3a, CONFIRMED zero at start.
    bool& dlc3SprintWaning() { return dlc3SprintWaning_; }

    // Sec28.11 `debris_flow_recycle_object`: 0x006fd4f0's own role is HYPOTHESIS ("return object to pool"); only
    // the real gated call count is modelled, keyed by flow id, not a simulated pool.
    std::unordered_map<int64_t, int>& debrisFlowRecycleCount() { return debrisFlowRecycleCount_; }

    // Sec28.12 `customization_restore_player_rig` / Sec28.21 `character_get_gender` (characterGender() below,
    // a SEPARATE per-name map: Sec28.21 resolves by name through the generic chain, Sec28.12 reads a dedicated
    // "local player 1"/"co-op player" accessor instead - two genuinely different access paths per the spec text).
    struct PlayerRig {
        OpenValue<int32_t> localPlayer1Gender{"local player 1's own +0xa41 gender byte (0x009da4e0)",
                                              "spec-lua-api-behaviour.md Sec28.12"};
        OpenValue<bool> coopPlayerPresent{"0x009df3d0 non-null (second/co-op player)", "spec-lua-api-behaviour.md Sec28.12"};
        OpenValue<int32_t> coopPlayerGender{"co-op player's own +0xa41 gender byte", "spec-lua-api-behaviour.md Sec28.12"};
        uint64_t restorePlayer1Count = 0;
        uint64_t restoreCoopCount = 0;
    };
    PlayerRig& playerRig() { return playerRig_; }

    // Sec28.13 `crib_weapon_add_enable` / Sec28.14 `crib_weapon_add_disable`: 0x012ecb34, CONFIRMED file value 1.
    bool& cribWeaponAddEnabled() { return cribWeaponAddEnabled_; }

    // Sec28.15 `crib_unlock_strongold`.
    struct Stronghold {
        OpenValueMap<bool> stillLocked{"stronghold byte +0x7a bit 0x2 set ('still locked')",
                                       "spec-lua-api-behaviour.md Sec28.15"};
        uint64_t unlockedCount = 0;             // host-branch, bit was set
        uint64_t clientOrSoloRequestCount = 0;  // the "without a session or as client" branch - internals OPEN, counted only
    };
    Stronghold& stronghold() { return stronghold_; }

    // Sec28.16 `continuous_explosion_start`: reconciled with Sec22.24 `continuous_explosion_stop`, one global
    // continuous-explosion state.
    struct ContinuousExplosion {
        OpenValueMap<bool> definitionResolves{
            "continuous-explosion definition row resolves (0x005eb390, NOT the 0x02442750 singleton family)",
            "spec-lua-api-behaviour.md Sec28.16"};
        OpenValue<bool> active{"0x012ec964 (continuous explosion global state)", "spec-lua-api-behaviour.md Sec28.16"};
        std::string lastDefName, lastTargetName; // CHOSEN, test-observable
        uint64_t startCount = 0;
    };
    ContinuousExplosion& continuousExplosion() { return continuousExplosion_; }

    // Sec28.17 `clear_callbacks_for_obj`: the real 6-wrapper/6-sub-record-base dispatch tree (Sec28.17's own
    // seven hidden-`this` findings) is NOT modelled - only that a call was gated on objectResolves() and
    // cleared SOMETHING is counted, honestly, per name (same "record the request" convention as
    // minimap_icon_add_do/object_indicator_add_do above).
    std::unordered_map<std::string, uint64_t>& callbacksClearedCount() { return callbacksClearedCount_; }

    // Sec28.18 `city_zone_swap_is_active`: 0x0242dc90, a list of active-swap name hashes. Starts empty - the
    // honest "nothing marked active yet" default (an empty array IS zero entries, not a guess), set by Sec1.9's
    // `city_zone_swap` (out of this batch's scope) or directly by a test.
    std::unordered_set<uint32_t>& activeCityZoneSwapHashes() { return activeCityZoneSwapHashes_; }

    // Sec28.19 `character_set_counter_on_grabbed`.
    struct CounterOnGrabbed {
        OpenValueMap<bool> gatePasses{
            "double-gate (0x008ae480/0x008837a0 direct OR 0x008ae3a0 replicate) passes, abstracted to one flag",
            "spec-lua-api-behaviour.md Sec28.19"};
        std::unordered_map<std::string, bool> value; // the per-character flag this batch writes; CHOSEN, write-only in scope
    };
    CounterOnGrabbed& counterOnGrabbed() { return counterOnGrabbed_; }

    // Sec28.20 `character_remove_child_item_by_name`: this project has no real class-descriptor/children-list
    // engine data; this is this project's own minimal, directly-editable stand-in for the real +0x1c/+0x20
    // list, matching the functional contract (remove every item named X, case-insensitively) without simulating
    // the two descriptor-bit gates (Sec28.20's own corrected 0x00853b30 reading).
    std::unordered_map<std::string, std::vector<std::string>>& characterChildItems() { return characterChildItems_; }

    // Sec28.21 `character_get_gender`: keyed by character name (see the PlayerRig note above for why this is a
    // separate map from localPlayer1Gender/coopPlayerGender).
    OpenValueMap<int32_t>& characterGender() { return characterGender_; }

    // Sec28.22 `character_evacuate_from_all_vehicles`: 0x00af3e10's own occupant-state dispatch (1/2/3) is OPEN
    // per spec; only the gated call count is modelled, keyed by character name.
    std::unordered_map<std::string, uint64_t>& evacuateFromVehiclesCount() { return evacuateFromVehiclesCount_; }

    // Sec28.23 `cellphone_animate_start_do`: reuses the existing hasLocalPlayer() above for "player 1 exists".
    OpenValue<bool>& cellphoneAnimSuppressed() { return cellphoneAnimSuppressed_; } // 0x00943a20(player) result
    uint64_t& cellphoneAnimPlayedCount() { return cellphoneAnimPlayedCount_; }

    // Sec28.24 `boss_battle_matt_get_cheat` / Sec28.25 `boss_battle_matt_cheats_start`, the Sec24.1
    // `boss_battle_matt_cheats_stop` cluster (0x012ec71c.. 0x012ec73c).
    struct BossBattleMatt {
        // 0x012ec730: the cheat-slot sentinel Sec28.24 reads. Its only writer in any spec read so far is
        // Sec24.1's `boss_battle_matt_cheats_stop`, out of this batch's own scope - stays OPEN here.
        OpenValue<int32_t> cheatSlot{"0x012ec730 (cheat-slot sentinel; writer is Sec24.1's cheats_stop, out of this batch's scope)",
                                     "spec-lua-api-behaviour.md Sec28.24"};
        // 0x012ec734/0x012ec738/0x012ec73c, CHOSEN, write-only in scope. n2/n3 are stored as the raw lua_tonumber
        // doubles forwarded to 0x005e59f0 - Sec28.25's own text does not state they are rounded.
        int32_t lastId = 0;
        double lastN2 = 0.0, lastN3 = 0.0;
        bool active = false;                        // 0x012ec724
        OpenValue<int32_t> retryCounter{"0x012ec720", "spec-lua-api-behaviour.md Sec28.25"};
        static constexpr int32_t kRetryLimit = 4;     // 0x012ec71c, CONFIRMED static constant
        int64_t deadlineMs = 0;                       // 0x012ec728, CHOSEN, write-only in scope
    };
    BossBattleMatt& bossBattleMatt() { return bossBattleMatt_; }

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec30 ("ranking
    // tranche 03") - see lua_spec_confirmed_stubs.cpp's own file-header
    // note above these functions for the full per-function citation. ---

    // Sec30.3 `auto_pickup_enable` (0x00a3c880): no arguments; writes the
    // hardcoded byte 1 to global 0x01308698. CONFIRMED: "file-backed
    // default: enabled" - so this project's own default is true (the
    // spec's own stated starting value, not a guess), matching the
    // existing cribWeaponAddEnabled_ precedent (Sec28.13/Sec28.14, "CONFIRMED
    // file value 1"). The call always re-sets it true (this entry "can
    // only enable" per spec); there is no in-scope disable counterpart
    // (the adjacent registrar slot's own disable sibling is HYPOTHESIS,
    // not dumped, and not one of this tranche's 25 confirmed names).
    bool& autoPickupEnabled() { return autoPickupEnabled_; }

    // Sec30.5 `vehicle_exit_group_do`'s own CONFIRMED crash-shaped edge
    // (Sec30.7): the real engine calls 0x00a7bdc0(arg3) on the first
    // vehicle found among the resolved, state-3 characters in its table
    // argument - or, when none was found across the whole table, with a
    // NULL `this` ("confirmed from the listing, XOR ECX,ECX"; Sec30.7:
    // "OPEN whether 0x00a7bdc0 tolerates that"). HOST-SAFETY DEVIATION
    // (not a spec fact, per this task's own explicit instruction): the
    // real engine reaches that unconfirmed-safety null-`this` call here;
    // this host guards the found-vehicle name explicitly and never
    // attempts the equivalent call on an unresolved target, matching the
    // established "this project does not simulate crashes" precedent
    // (cf. group_get_next_npc, Sec28.5, item 57 in lua_spec_confirmed_
    // stubs.cpp). This counter is this project's OWN test-observable
    // proof the guard fired - NOT a real engine field - same convention
    // as VintDataResponderRecord::dispatchAttempts.
    int vehicleExitGroupNullThisGuardCount() const { return vehicleExitGroupNullThisGuardCount_; }
    void recordVehicleExitGroupNullThisGuard() { ++vehicleExitGroupNullThisGuardCount_; }

    // Sec30.6 `team_make_unfriendly` / Sec20.6 `team_make_allies` (the
    // latter out of this tranche's own scope, not Lua-registered by this
    // project): both resolve a team name via the established helper
    // 0x0094cc60 (hash 0x00dab330 + lookup 0x005961a0, default sentinel 9
    // for an unrecognized name - CONFIRMED mechanism, both Sec20.6 and
    // Sec30.6). This project has no real team-name registry (no spec
    // gives the actual in-game team name strings or their real ids) -
    // resolveTeamId() is this project's own minimal stand-in: a name a
    // test has not registered takes the real, CONFIRMED "unknown name"
    // path (sentinel id 9) - the SAME "never-populated real registry,
    // so the honest default IS the real behavior" precedent as
    // EngineState::findVdoObject's own doc comment.
    void registerTeamIdForTesting(const std::string& name, int id) { teamIdByName_[name] = id; }
    int resolveTeamId(const std::string& name) const {
        auto it = teamIdByName_.find(name);
        return it != teamIdByName_.end() ? it->second : 9; // CONFIRMED sentinel, 0x0094cc60
    }

    // 0x009b7130(id1, id2, value, true) writes `value` symmetrically into
    // an 8x8 (CONFIRMED, Sec20.6) relation-matrix byte block (base
    // 0x0262d940, stride 8) - valid indices 0..7. Sentinel id 9 (or any id
    // outside 0..7, such as a resolver bug this project cannot rule out)
    // indexes PAST that block - Sec30.7's own cross-function note flags
    // this as "a crash-shaped edge... OPEN, not confirmed" (whether it
    // truly faults or just corrupts adjacent memory is not independently
    // settled). HOST-SAFETY DEVIATION (not a spec fact, per this task's
    // own explicit instruction): either way, this host must not perform
    // the equivalent out-of-bounds write into its own 8x8 array - both
    // ids are bounds-checked first; an out-of-range id makes this return
    // false and skip the write entirely (the caller logs the averted path
    // - see stub_team_make_unfriendly), matching the same "this project
    // does not simulate crashes" precedent used throughout this file.
    // Also applies Sec30.6's own refinement of Sec20.6: "whenever team 5
    // is one side, team 6 gets the identical relation" (the SAME base
    // matrix at rows/columns 6, not a separate auxiliary array -
    // CONFIRMED, address arithmetic) - done here so both team_make_allies
    // (out of scope) and team_make_unfriendly share one correct
    // implementation if team_make_allies is ever added.
    static constexpr int kTeamRelationMatrixDim = 8; // CONFIRMED 8x8 block, Sec20.6/Sec30.6
    bool setTeamRelationIfInBounds(int id1, int id2, int value) {
        if (id1 < 0 || id1 >= kTeamRelationMatrixDim || id2 < 0 || id2 >= kTeamRelationMatrixDim) return false;
        teamRelation_[static_cast<size_t>(id1)][static_cast<size_t>(id2)] = value;
        teamRelation_[static_cast<size_t>(id2)][static_cast<size_t>(id1)] = value;
        if (id1 == 5) { teamRelation_[6][static_cast<size_t>(id2)] = value; teamRelation_[static_cast<size_t>(id2)][6] = value; }
        if (id2 == 5) { teamRelation_[6][static_cast<size_t>(id1)] = value; teamRelation_[static_cast<size_t>(id1)][6] = value; }
        return true;
    }
    // Test-only direct read; -2 (never a real relation value this project
    // writes) for an out-of-bounds pair, else whatever was last set (-1 =
    // "never written" default, this project's own sentinel, not a spec fact -
    // the real matrix's own true at-rest contents are OPEN).
    int teamRelationForTesting(int id1, int id2) const {
        if (id1 < 0 || id1 >= kTeamRelationMatrixDim || id2 < 0 || id2 >= kTeamRelationMatrixDim) return -2;
        return teamRelation_[static_cast<size_t>(id1)][static_cast<size_t>(id2)];
    }
    int teamRelationOobGuardCount() const { return teamRelationOobGuardCount_; }
    void recordTeamRelationOobGuard() { ++teamRelationOobGuardCount_; }
    // --- Ranking tranche 05 (Sec33), batch 2026-10-02: non-vehicle, non-
    // character state. See each function's own trampoline doc comment
    // (lua_spec_confirmed_stubs.cpp) for the full citation. -------------

    // action_sequence_end (Sec33.3): this project has no real "scripted
    // cutscene sequence" object, "tracked handle," or per-player scripted-
    // camera/target field elsewhere - a self-contained, test-observable
    // stand-in: a shared "sequence active" flag and per-player scripted-
    // camera/target names, cleared by this call; a combined tracked-handle
    // release count (the real body releases up to 2 distinct handles;
    // nothing else in scope distinguishes them); a host-only broadcast
    // count (opcode 0x49, byte value 2, CONFIRMED gated on coopLocalIsHost()).
    struct ActionSequence {
        bool active = false;
        std::string localScriptedCameraTarget;
        std::string remoteScriptedCameraTarget;
        int trackedHandleReleaseCount = 0;
        int hostBroadcastCount = 0;
    };
    ActionSequence& actionSequence() { return actionSequence_; }
    void actionSequenceEnd();

    // audio_set_listener_override / audio_clear_listener_override
    // (Sec33.4): CONFIRMED - both always store an id (clear stores the
    // null id) and always send a record, no authority gate - this
    // project's own no-op replicateStateChange stand-in models the
    // "always sends" record. The per-frame position/orientation snapshot
    // and listener substitution are NOT modeled (no position/camera data
    // anywhere in this project, stated project-wide simplification).
    std::string& audioListenerOverrideTarget() { return audioListenerOverrideTarget_; }

    // audio_any_conversation_playing (Sec33.4): CONFIRMED mechanism - loops
    // every session member's own slot on a named per-session channel
    // ("mission_conv" here; the spec's own text notes this is "a generic,
    // reusable mechanism ... one such channel among possibly several").
    // This project tracks no per-member array anywhere (CoopSession above
    // is a handful of summary predicates, not a member list) - collapsed
    // to one "is anyone active on this channel" flag per channel name, a
    // stated simplification; OPEN until a test sets it.
    OpenValueMap<bool>& conversationChannelActive() { return conversationChannelActive_; }

    // waiting_for_player_dialog (Sec33.5): CONFIRMED reference-counted
    // show/hide, including the real, confirmed hazard this project
    // reproduces deliberately: neither this function's own show nor hide
    // path ever clears "currently shown" (only an untraced third writer,
    // out of scope, does) - so once a real display happens, currentlyShown
    // stays true for the rest of the run, same as the real engine. The
    // "suppressing game-state check" gating whether a show actually
    // displays is NOT modeled (this project has no such check anywhere) -
    // every show that isn't already "shown" is treated as actually
    // displaying, a stated simplification.
    struct WaitingForPlayerDialog {
        int refCount = 0;
        bool pending = false;
        bool currentlyShown = false;
        int showBroadcastCount = 0;
        int hideBroadcastCount = 0;
    };
    WaitingForPlayerDialog& waitingForPlayerDialog() { return waitingForPlayerDialog_; }

    // auto_pickup_disable (Sec33.5): CONFIRMED - writes a named, session-
    // synchronized variable "allow_weapon_auto_pickup" off; no in-scope
    // function reads it back, and the spec text gives no confirmed initial
    // value, so this project's own chosen default (true, "enabled") only
    // matters for a test that checks it before any call - same "write-
    // only in scope, plain chosen default" convention as cribWeaponAddEnabled_.
    bool& allowWeaponAutoPickup() { return allowWeaponAutoPickup_; }
    // =====================================================================
    // pause-map stag mode / district control (spec-lua-api-behaviour.md
    // Sec31) + pause_map_tutorial_mode (Sec32.1) - batch 2026-10-02.
    // =====================================================================

    // 0x0229a317 - the SAME global Sec27.12's `game_autosave` already reads
    // (autosave().fourthFlagNonzero: "0x0229a317 nonzero" blocks autosave -
    // Sec31.5's own cross-reference: "the autosave gate... autosave is
    // suppressed while stag mode is on"). Reused directly rather than
    // duplicated, so a stag-mode write from the pause-map functions below is
    // honestly visible to the autosave gate too, and vice versa.
    OpenValue<bool>& pauseMapStagMode() { return autosave_.fourthFlagNonzero; }

    // 0x0229a318 - the pause-map tutorial-mode flag `pause_map_is_tutorial_mode`
    // (Sec31.1 table item 5) reads and `pause_map_tutorial_mode` (Sec32.1)
    // writes. A SEPARATE global from the stag-mode flag above (Sec31.5's own
    // text: the pause-map close handler "clears the tutorial-mode flag
    // 0x0229a318... alongside the stag-mode flag" - two distinct bytes
    // cleared together by one handler, not one byte read two ways).
    OpenValue<bool>& pauseMapTutorialMode() { return pauseMapTutorialMode_; }

    // 0x0229a2ac - the pause-map's own "currently selected zone" global
    // (Sec31.2/Sec31.3). Default 0 ("no zone selected") - not independently
    // confirmed via an explicit static-initializer/exhaustive-write-census
    // check this pass (unlike 0x0153b530/0x0153b556's own dedicated such
    // checks elsewhere in this file), but HIGH CONFIDENCE: Sec31.2's own
    // CONFIRMED function body treats a 0 read as the normal, meaningful
    // "nothing selected" case, not as missing information, and Sec31.3's own
    // exhaustive 7-use census finds only two writers - pause_map_set_gps
    // (sets a real zone) and a teardown helper (explicitly resets to 0) - so
    // 0 is a real, reachable, well-understood state of this global, not a
    // guess at unknown content.
    uint32_t pauseMapSelectedZone() const { return pauseMapSelectedZone_; }
    void setPauseMapSelectedZoneForTesting(uint32_t zoneHandle) { pauseMapSelectedZone_ = zoneHandle; }

    // Test-only direct fixture for the hovered-zone global's own confirmed
    // gate fields (see PauseMapHoveredZone's own doc comment for why this is
    // test-only - no cursor/pointer-input system exists in this host).
    void setPauseMapHoveredZoneForTesting(uint32_t zoneHandle, bool hasParentDistrict, int32_t fieldPlus0x54) {
        pauseMapHoveredZone_ = PauseMapHoveredZone{zoneHandle, hasParentDistrict, fieldPlus0x54};
    }

    // Test-only zone-membership fixture (see ZoneMember's own doc comment
    // above for why this registry is test-only).
    void registerZoneMemberForTesting(uint32_t zoneHandle, float weight, bool owned) {
        zoneMembersByZone_[zoneHandle].push_back(ZoneMember{weight, owned});
    }
    // Test-only direct read, so a test can verify pause_map_stag_takeover_do_reward's
    // own claiming effect (ZoneMember::owned flipping true). Returns nullptr
    // for a zone with no registered members (including zone handle 0).
    const std::vector<ZoneMember>* zoneMembersForTesting(uint32_t zoneHandle) const {
        auto it = zoneMembersByZone_.find(zoneHandle);
        return it == zoneMembersByZone_.end() ? nullptr : &it->second;
    }

    // pause_map_stag_current_district_control's own CONFIRMED computation
    // (Sec31.2): owned-weight / total-weight for the given zone, or 1.0 when
    // total <= 0 (including NaN) - CONFIRMED, including the "a zone with no
    // registered members" case (an unregistered zoneHandle, e.g. 0 on the
    // no-selection path, naturally has total==0 here, matching Sec31.2's own
    // "in practice over members with no zone, giving 1.0" reading exactly -
    // no special-casing of zone 0 is needed, the general formula already
    // produces it).
    double pauseMapZoneControlFraction(uint32_t zoneHandle) const;

    // pause_map_set_gps's own CONFIRMED stag-mode branch (Sec31.3) - see this
    // method's own .cpp doc comment for the full gate. The real non-stag
    // GPS-route behavior is not traced by this section and is not modeled
    // (this project's own stated simplification - see the .cpp doc comment).
    void pauseMapSetGpsStagBranch();

    // pause_map_stag_takeover_do_reward's own CONFIRMED claim/clear-stag-
    // mode/request-autosave mechanism (Sec31.6) - see this method's own .cpp
    // doc comment for the full reasoning, including why this host refuses
    // the real engine's own unchecked null-dereference safely instead of
    // reproducing it (same §32.8 "flag, don't reproduce" precedent as the
    // three crash paths below).
    void pauseMapStagTakeover();

    uint64_t pauseMapStagCompletionHookFiredCount() const { return pauseMapStagCompletionHookFiredCount_; }
    uint64_t pauseMapTakeoverClaimedCount() const { return pauseMapTakeoverClaimedCount_; }
    uint64_t pauseMapTakeoverAutosaveRequestCount() const { return pauseMapTakeoverAutosaveRequestCount_; }

    // =====================================================================
    // Ranking tranche 04 (spec-lua-api-behaviour.md Sec32), 25 names - batch
    // 2026-10-02. Every name below resolves through this project's existing
    // objectResolves() shared-resolver simplification (same "one-map-for-
    // all-resolvers" convention the Sec27/Sec28 batch already established) -
    // no new resolver concept is introduced.
    // =====================================================================

    // Sec32.1 `store_interface_is_active`: 0x022cce86 bit 0x1. OPEN writer
    // (7 direct references, all reads, per spec - not traced).
    OpenValue<bool>& storeInterfaceActive() { return storeInterfaceActive_; }

    // Sec32.1 `spawn_region_max_spawn_dist`/`spawn_region_max_spawn_dist_reset`:
    // 0x013092f0, a squared max-spawn-distance cap. CONFIRMED file-backed
    // default FLT_MAX ("no limit"). No range check on the setter (CONFIRMED:
    // negative squares positive, 0 makes the cap 0) - the stub applies that
    // arithmetic directly (lua_spec_confirmed_stubs.cpp), this is a plain
    // read/write pair.
    float spawnRegionMaxSpawnDistSquared() const { return spawnRegionMaxSpawnDistSquared_; }
    void setSpawnRegionMaxSpawnDistSquared(float v) { spawnRegionMaxSpawnDistSquared_ = v; }
    void resetSpawnRegionMaxSpawnDist(); // .cpp: FLT_MAX, CONFIRMED "reset" value

    // Sec32.1 `set_ped_override_density`: 0x01308af0. CONFIRMED file-backed
    // default -1.0 ("no override").
    float pedOverrideDensity() const { return pedOverrideDensity_; }
    void setPedOverrideDensity(float v) { pedOverrideDensity_ = v; } // caller (the stub) applies the CONFIRMED >0/<=1.0/NaN branch logic before calling this

    // The game clock (0x014ff338), Sec48 - corrects Sec32.1. hour/minute
    // (0x014ff33c/0x014ff33d) are now CONFIRMED known from construction, not
    // OPEN: Sec48.2's own resolved correction establishes that a fresh
    // single-player game (no save loaded, no co-op session - this host
    // never models either) always starts at 10:00:00 via the new-game
    // routine, since the world-start random-key snap (the only other
    // source) is CONFIRMED to never fire without a session object. This is
    // what actually closed the dlc3_m01/m19/etc. blocker: those missions
    // call set_time_of_day, whose own forward-delta arithmetic (below) reads
    // the CURRENT hour/minute first - previously OPEN (no initial value),
    // now a real read.
    //
    // set_time_of_day's real effect (Sec48.4, corrects Sec32.1's own "the
    // clock now reads the requested time" assumption): the engine
    // unconditionally snaps the clock to the NEAREST time-of-day KEY of the
    // active definition after the forward jump - usually NOT leaving
    // hour/minute equal to what was requested. The key list is loaded data
    // this project does not have (OPEN, Sec48.5's own "Residual OPEN
    // items"), so the real post-snap value cannot be computed, only that it
    // changed. Modeled honestly: setTimeOfDay() computes and records the
    // CONFIRMED forward delta (lastAdvanceSeconds, test-observable), then
    // forgets hour/minute rather than fabricating the snapped result - a
    // second set_time_of_day call (or a read inside this project's own
    // per-frame advance below) correctly sees OPEN again past that point,
    // the honest consequence of genuinely not knowing where the snap landed.
    //
    // Per-frame advance (Sec48.3, CONFIRMED mechanism): advanceFrame()
    // advances by realSeconds * kTimeScale game-seconds, CHOSEN to be called
    // once per this host's own modelled tick (tools/lua_host_run.cpp passes
    // its own per-tick frame-time constant, matching get_frame_time's own
    // per-resume value - see that call site's own doc comment). A no-op
    // while hour/minute are unknown (advancing an unknown base is still
    // unknown, not a value to fabricate). STATED SIMPLIFICATION: wraps
    // within one 86400-second day (hour/minute/second only) and does not
    // advance date/weekday/moon-day - nothing in scope reads those fields
    // (grepped directly: only hour/minute are ever read via Lua), and no
    // budget used by this project crosses a full in-game day (40x scale
    // needs ~36 real minutes per in-game day; the longest run so far is a
    // few real seconds). Revisit if a future budget or spec need ever
    // requires the date fields.
    struct GameClock {
        OpenValue<int32_t> hour{"game clock hour byte (0x014ff33c)", "spec-lua-api-behaviour.md Sec32.1/Sec48.2/Sec48.4"};
        OpenValue<int32_t> minute{"game clock minute byte (0x014ff33d)", "spec-lua-api-behaviour.md Sec32.1/Sec48.2/Sec48.4"};
        int32_t second = 0; // CONFIRMED 0 at new-game (Sec48.2); not independently exposed to Lua in scope
        static constexpr double kTimeScale = 40.0; // CONFIRMED (Sec48.1/Sec48.3), game-seconds per real second
        int64_t lastAdvanceSeconds = 0;  // CONFIRMED computed total delta (forced non-negative via the hour-wrap rule), seconds
        uint64_t advanceRequestCount = 0;
        double frameAdvanceSecondsAccumulated = 0.0; // test-observable: total real seconds fed to advanceFrame()
    };
    GameClock& gameClock() { return gameClock_; }
    void setTimeOfDay(int64_t newHour, int64_t newMinute); // .cpp
    void gameClockAdvanceFrame(double realSeconds); // .cpp - Sec48.3

    // Sec32.1 `satellite_weapon_mode_exit`: the satellite-weapon-controller
    // singleton 0x0130eee8's own "active" gate, plus this project's own
    // per-selector exit counters. **The 0x009df3d0 (remote co-op player)
    // correction (Sec14.31/Sec32.8) is applied at this function's own call
    // site** (lua_spec_confirmed_stubs.cpp): bit 0x2 only ever selects a
    // player when playerRig().coopPlayerPresent is true (the CONFIRMED "no
    // co-op session -> no remote entry" reading, not a "matching" lookup
    // that might wrongly fall back to the local player).
    struct SatelliteWeaponController {
        OpenValue<bool> active{"satellite-weapon-controller active (0x0130eee8)", "spec-lua-api-behaviour.md Sec32.1"};
        uint64_t localExitCount = 0;
        uint64_t remoteExitCount = 0;
    };
    SatelliteWeaponController& satelliteWeapon() { return satelliteWeapon_; }
    void satelliteWeaponExit(bool remote); // .cpp

    // Sec32.2 character force-flag setters (write-only in scope, no Lua
    // getter among these three names) - CHOSEN per-name maps, same
    // "write-only, CHOSEN" convention as helicopterDontDeathSpiral above.
    std::unordered_map<std::string, bool>& seatbeltForceFlag() { return seatbeltForceFlag_; }         // +0x1c9c bit 0x40
    std::unordered_map<std::string, bool>& trailingAimForceFlag() { return trailingAimForceFlag_; }   // +0x1c9c bit 0x1
    std::unordered_map<std::string, bool>& neverTurnOnPlayerFlag() { return neverTurnOnPlayerFlag_; } // +0x2b9 bit 0x40 (AI sub-object)

    // Sec32.3 `player_warp_to_shore_disable`: +0x28ad bit 0x2, write-only in
    // scope (CHOSEN per-name map; always true per Sec32.3's own hardcoded
    // setter argument).
    std::unordered_map<std::string, bool>& warpToShoreDisabled() { return warpToShoreDisabled_; }

    // Sec32.3 `skydive_setup_tank_bailout`: the mission-18 cargo-plane/tank
    // set piece. No real name resolve at all (both vehicles are hardcoded
    // literals, never argument-driven) - a plain counter trio, CONFIRMED
    // per-stage branch structure; the named-vehicle-animation dispatch
    // internals are not modeled.
    struct SkydiveTankBailout {
        uint64_t broadcastCount = 0;   // CONFIRMED: always sent, every stage, regardless of vehicle existence
        uint64_t armedCount = 0;       // stage 1
        uint64_t animStartedCount = 0; // stage 2
    };
    SkydiveTankBailout& skydiveTankBailout() { return skydiveTankBailout_; }

    // Sec32.3 `qte_human_is_used`: two fixed QTE slot records. This
    // project's own name-keyed simplification of "the slot's owning player
    // is the local or remote co-op player depending on a session-indexed
    // byte" (that real local/remote selection mechanism is itself
    // HYPOTHESIS/OPEN per spec) - a test sets whichever name currently owns
    // a slot directly (by whatever sentinel it chooses, e.g. "#PLAYER1#"),
    // rather than this project re-deriving local-vs-remote identity from
    // playerRig() here.
    struct QteSlot {
        bool active = false;
        std::string owningPlayerName;
        std::vector<std::string> participantNames; // the slot's up to five id-pair-identified participants, modeled by name
    };
    void setQteSlotForTesting(int index, bool active, std::string owningPlayerName, std::vector<std::string> participants);
    bool qteHumanIsUsed(const std::string& name) const; // .cpp

    // Sec32.4 `party_add_do`/`npc_is_in_party`: this project's own name-keyed
    // stand-in for the real party-record lookup (0x005088c0, OPEN per spec).
    // `membersByLeader` is bounded to 5 real adds per call, matching the
    // real function's own fixed 5-slot local array (Sec32.4's own CONFIRMED
    // structural bound) - NOT the full capacity/force-override/network-
    // ownership-handoff/leader-kind-bit AI setup mechanism, which has no
    // real data this project can reproduce.
    struct PartyState {
        std::unordered_map<std::string, std::vector<std::string>> membersByLeader;
        // CONFIRMED real stack-overrun hazard (Sec32.4/Sec32.8: a 6th-or-
        // later follower overwrites the saved return address and beyond) -
        // NOT reproduced as an actual memory-unsafe crash; only flagged and
        // counted (same "flag, don't reproduce" precedent as the other two
        // crash paths this tranche finds, Sec32.8).
        uint64_t overflowDetectedCount = 0;
    };
    PartyState& party() { return party_; }

    // Sec32.5 `object_destroy`: this project's own minimal "was a destroy
    // processed against a resolved object" record - the liveness check,
    // capability-predicate gate and per-kind destroy virtual are OPEN/not
    // modeled.
    std::unordered_set<std::string>& destroyedObjects() { return destroyedObjects_; }

    // Sec32.5 `object_indicator_remove_do`: counts the opcode 0x40 sub-tag 4
    // record (bit 0x2 of the arg-2 bitmask) - this project builds no
    // networking layer (see replicateStateChange's own doc comment above).
    // Bit 0x1's own "clear every indicator" effect reuses
    // CharacterState::objectIndicators directly (the stub clears that
    // vector) rather than a separate field here.
    uint64_t& indicatorRemoveRecordCount() { return indicatorRemoveRecordCount_; }

    // Sec32.5 `shop_enable_nearest`: a test-only shop fixture (this project
    // has no spatial/world-position system - the real "first within 15
    // units, else closest within 75" distance search, and arg 1's own role
    // in seeding the search origin, are not modeled; tests configure each
    // candidate shop's own distance-tier flags directly). Iteration order =
    // registration order, standing in for "the world object manager's shop
    // sub-list" scan order.
    struct Shop {
        bool withinFifteen = false;
        bool withinSeventyFive = false;
        bool disabled = true; // this project's own "closed by default" choice - no CONFIRMED initial state given
    };
    std::vector<Shop>& shops() { return shops_; }
    uint64_t& shopEnableRecordCount() { return shopEnableRecordCount_; } // the host-replicated opcode 0x45 sub-tag 7 record; no host/authority layer modeled, counted unconditionally

    // Sec32.6 `item_show`: +0x3b bit 0x1 ("hidden"), write-only in scope
    // (CHOSEN per-name map; item_show always clears it - "shows" - per
    // Sec32.6's own text; the HYPOTHESIS sibling `item_hide` is out of this
    // batch's scope and not implemented).
    std::unordered_map<std::string, bool>& itemHidden() { return itemHidden_; }

    // Sec32.6 `item_anim_play`: last-requested animation per item, split by
    // the CONFIRMED branch (blend-transition vs single-state-start); the
    // layer-slot internals and the exact flag-value meanings are OPEN/
    // HYPOTHESIS per spec and are not modeled further than the branch
    // itself.
    struct ItemAnimState {
        std::string lastBlendTransitionAnim;
        std::string lastSingleStateAnim;
        int lastFlag = 0; // CONFIRMED branch values 0x40/0x10; HYPOTHESIS real-world meaning
        uint64_t blendTransitionCount = 0;
        uint64_t singleStateStartCount = 0;
    };
    std::unordered_map<std::string, ItemAnimState>& itemAnimState() { return itemAnimState_; }

    // Sec32.7 `radio_set_station`: per-vehicle(-or-character) radio state.
    // `hasRadio`/`stationCount`/`stationRefused` are all OPEN (no real per-
    // vehicle radio-table data this project parses); `currentStation` is
    // this project's own plain tracked field (0 = "no station tuned").
    struct VehicleRadio {
        OpenValue<bool> hasRadio{"vehicle has a radio", "spec-lua-api-behaviour.md Sec32.7"};
        OpenValue<int32_t> stationCount{"vehicle radio station count", "spec-lua-api-behaviour.md Sec32.7"};
        OpenValueMap<bool> stationRefused{"vehicle radio station refused, by station number",
                                          "spec-lua-api-behaviour.md Sec32.7"};
        int32_t currentStation = 0; // 0 = "no station tuned" (this project's own choice; stations are CONFIRMED 1-based)
        uint64_t recordSentCount = 0;
    };
    VehicleRadio& vehicleRadio(const std::string& name) { return vehicleRadios_[name]; }

    // Sec32.7 `helicopter_shoot_vehicle`: the AI-drive-state gate per
    // helicopter name, plus the shared fire dispatcher's own OPEN return
    // value (Sec32.7: "its own return value is invisible in the decompiled
    // view" - a SINGLE shared OpenValue, not per-call/per-name data, since
    // this is a structural "we don't know this dispatcher's return contract
    // on success" marker, not state belonging to any one helicopter).
    struct Helicopter {
        OpenValue<bool> aiDriveStateOk{"helicopter AI-drive-state gate", "spec-lua-api-behaviour.md Sec32.7"};
    };
    Helicopter& helicopter(const std::string& name) { return helicopters_[name]; }
    OpenValue<bool>& helicopterFireDispatcherResult() { return helicopterFireDispatcherResult_; }

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec38/Sec39/Sec40
    // ("ranking tranches 06/07/08") - see each field's own doc comment for
    // citation/reasoning, and lua_spec_confirmed_stubs.cpp's own per-batch
    // header comments for which of each tranche's 25 names this pass
    // implements and why the rest stay generic stubs. -------------------

    // vcust_preview_wheel_sizing (Sec38.5): the mandated crash guard. All 4
    // vcust_preview_*/vcust_purchase_wheels entries share "no live
    // customization target -> the vehicle pointer is zero, dereferenced
    // unconditionally" (CONFIRMED, Sec38.5's own intro paragraph); this
    // pass implements only vcust_preview_wheel_sizing (the one explicitly
    // mandated), so this field is scoped to it alone - no real garage/
    // customization-UI state exists elsewhere in this project to unify it
    // with. See `targetLive`'s own doc comment just below for why this is
    // a CHOSEN plain default rather than OPEN.
    struct VcustPreview {
        // CHOSEN default false, not OPEN: this project has no garage/
        // customization-UI subsystem anywhere in scope, so no code path
        // here ever establishes a live target - "false" is the honest
        // depiction of THIS host's own behavior (never live), not a
        // guess about the real engine's own variable at-rest state
        // (same "never-populated registry is the honest default"
        // precedent as EngineState::resolveTeamId()'s sentinel-9 default).
        // A test may still set this true to exercise the non-guarded path.
        bool targetLive = false;
        uint64_t wheelSizingNullTargetGuardCount = 0; // HOST-SAFETY, test-observable only - not a real engine field
    };
    VcustPreview& vcustPreview() { return vcustPreview_; }
    void recordVcustPreviewWheelSizingNullTargetGuard() { ++vcustPreview_.wheelSizingNullTargetGuardCount; }

    // store_weapon_purchase_ammo (Sec38.3) / shared by any future
    // *_purchase_*-style entry: this project's own minimal "player purse"
    // stand-in - no real currency/economy system anywhere in this project.
    // CONFIRMED: no affordability check in store_weapon_purchase_ammo at
    // all; it charges the price and records the purchase even when the
    // item lookup FAILS (a confirmed real quirk, not a bug to guard
    // against). `tag` matches the real notification mechanism's own
    // literal string (Sec38.3: `"weapon-ammo"`); `itemResolved` is always
    // false from this host (no real store-list table in scope - the
    // honest default for a never-populated registry, same precedent as
    // EngineState::resolveTeamId()/findVdoObject()).
    struct PurchaseRecord {
        std::string tag;
        double amountCharged = 0.0;
        bool itemResolved = false;
    };
    double& playerCash() { return playerCash_; }
    std::vector<PurchaseRecord>& purchaseLedger() { return purchaseLedger_; }

    // skydive_move_to_do (Sec39.1): "the target resolves through the
    // generic third-tier resolver with NO guard on failure: an absent/
    // unresolved name leaves the target pointer at literal zero, and the
    // function then unconditionally copies 12 bytes from address 0x40 into
    // the character - a genuine access violation" (CONFIRMED real defect,
    // new this tranche). HOST-SAFETY DEVIATION (not a spec fact): this
    // host skips the equivalent copy on an explicitly-known-unresolved
    // non-path target instead of performing it, counting the averted path
    // here - "this project does not simulate crashes" (cf. group_get_
    // next_npc, Sec28.5, the established precedent for this exact idiom).
    int skydiveMoveToNullTargetGuardCount() const { return skydiveMoveToNullTargetGuardCount_; }
    void recordSkydiveMoveToNullTargetGuard() { ++skydiveMoveToNullTargetGuardCount_; }

    // Sec39.4 save-system UI cluster (save_system_save_game/_load_game/
    // _cancel_coop_load): one shared singleton; slot records are a fixed
    // table whose count is read from one field (OPEN - the real at-rest
    // value/writer isn't given in scope). CONFIRMED real defect (HIGH
    // CONFIDENCE per spec, sibling-confirmed): both the save-confirm
    // callback and the load path bound the slot with `slot <= count`,
    // accepting `slot == count` - one past the end of the table. The
    // mandated faithful quirk: this host allows that exact off-by-one
    // (not "correctly" rejecting it) and logs when it fires, rather than
    // tightening the bound to `<`.
    struct SaveSystemUi {
        OpenValue<uint32_t> slotCount{"save-system UI slot-record count", "spec-lua-api-behaviour.md Sec39.4"};
        uint64_t saveCallCount = 0;
        uint64_t loadCallCount = 0;
        uint64_t saveOffByOneCount = 0; // CONFIRMED-adjacent real defect: slot==count accepted (<=, not <)
        uint64_t loadOffByOneCount = 0;
        // save_system_cancel_coop_load's own state field (Sec39.4: "sets
        // the save-system's own state field to 5 unless already there").
        // CHOSEN write-only default - the real at-rest value is OPEN/not
        // given in scope, same "CHOSEN write-only in scope" convention as
        // CatMouseMinigame::selection above.
        int32_t coopLoadState = 0;
    };
    SaveSystemUi& saveSystemUi() { return saveSystemUi_; }

    // store_gang_show_question_marks / store_gallery_download_hide_list
    // (Sec39.5): two further CONFIRMED-new null-dereference defects,
    // guarded/logged rather than reproduced as crashes (this task's own
    // explicit instruction - these are NOT the mandated faithful-quirk
    // kind, they are plain original-game bugs this host must not
    // simulate). store_gallery_download_hide_list's local-player half
    // reuses the existing hasLocalPlayer() accessor above (the SAME
    // "player 1 exists" concept cellphone_animate_start_do already reuses
    // it for, Sec28.23) rather than inventing a second, separate local-
    // player flag.
    struct StorePreviewGuards {
        // CHOSEN default false, not OPEN, same reasoning as VcustPreview::
        // targetLive above: this project has no gang-customization-preview
        // or gallery-list subsystem anywhere in scope, so neither asset
        // ever resolves from this host's own default state. A test may
        // set either true to exercise the non-guarded path.
        bool gangPreviewAssetOrFallbackResolved = false;
        bool gangPreviewShowingQuestionMarks = false; // CHOSEN write-only toggle, this project's own stand-in
        uint64_t gangQuestionMarksNullGuardCount = 0;
        bool galleryListObjectResolved = false;
        uint64_t galleryHideListNullGuardCount = 0;
    };
    StorePreviewGuards& storePreviewGuards() { return storePreviewGuards_; }

    // store_common_rotate_mouse_drag (Sec39.5): "reads mouse-delta values
    // from three fixed stack slots, but only copies the real values in
    // when a specific input-mode bit is set ... when clear, the stack
    // slots are never initialized at all ... rotate[s] ... by a garbage
    // angle derived from that uninitialized stack memory. A faithful
    // reimplementation must treat 'mouse delta unavailable' as zero, not
    // skip the rotation or read whatever happens to be on the stack."
    // CHOSEN (host-safety stand-in for undefined behaviour, not a spec
    // fact, per this task's own explicit instruction): this host has no
    // real mouse-delta-capture layer at all (not modeled anywhere else in
    // this codebase either), so BOTH the "bit clear" case (real
    // uninitialized memory) and the "bit set" case (a real value this
    // host has no source for) land on the same explicit, defined 0 -
    // never raw/undefined C++ memory, never an invented nonzero value.
    struct StoreCommonRotateMouseDrag {
        double lastDeltaX = 0.0, lastDeltaY = 0.0, lastDeltaZ = 0.0;
        uint64_t callCount = 0;
    };
    StoreCommonRotateMouseDrag& storeCommonRotateMouseDrag() { return storeCommonRotateMouseDrag_; }

    // pcu_is_bra_category / pcu_is_underwear_category (Sec40.5): "dereference
    // a null category record when the index is out of range" (CONFIRMED
    // real defect). This project's own minimal stand-in for the real
    // category table: a test populates each known index's own "kind" tag
    // directly (no real category table in scope) - an index a test never
    // populated takes the same "out of range" guarded path a real
    // out-of-bounds index would.
    struct PcuCategoryTable {
        static constexpr int32_t kOther = 0, kBra = 1, kUnderwear = 2; // CHOSEN tags, this project's own enum
        OpenValueMap<int32_t> kindByIndex{"PCU category record kind, by index", "spec-lua-api-behaviour.md Sec40.5"};
        uint64_t categoryIndexOobGuardCount = 0;
    };
    PcuCategoryTable& pcuCategoryTable() { return pcuCategoryTable_; }

    // pcu_wear_store_outfit / pcu_purchase_outfit (Sec40.5/Sec40.6): one
    // shared catalog-outfit list, this project's own minimal stand-in (a
    // test populates each list position's own raw flag word directly - no
    // real catalog table in scope). CONFIRMED real indexing inconsistency:
    // pcu_wear_store_outfit numbers qualifying nodes by testing "any flag
    // bit, excluding one specific bit" while pcu_purchase_outfit (and,
    // out of this pass's own implemented scope, pcu_get_num_items_owned)
    // number the SAME list by testing "bit 0 only" - two INDEPENDENT
    // filters over one shared list, each with its own 0-based "Nth
    // qualifying node" numbering; this header deliberately does NOT unify
    // them into one shared counting helper (this task's own explicit
    // instruction). WHICH single bit the text's own "excluding one
    // specific bit" names is not given by address - read here as bit 0
    // (0x1), the SAME bit pcu_purchase_outfit/pcu_get_num_items_owned
    // test exclusively: a stated, reasoned interpretation of the text's
    // own direct juxtaposition, NOT an independently confirmed address-
    // level fact.
    struct PcuCatalogOutfits {
        OpenValueMap<uint32_t> flagsByIndex{"PCU catalog-outfit node raw flag word, by list position",
                                            "spec-lua-api-behaviour.md Sec40.5/Sec40.6"};
    };
    PcuCatalogOutfits& pcuCatalogOutfits() { return pcuCatalogOutfits_; }

    // pcu_purchase_slot / pcu_purchase_outfit (Sec40.5): "both IGNORE the
    // inventory-append function's 'full' return value - a player who is
    // already at capacity (2048 items or 128 outfits) is charged the full
    // price and receives nothing silently" (CONFIRMED real logic defect,
    // the mandated faithful quirk). This project's own minimal stand-in:
    // only COUNTS are tracked (no individual item/outfit identity - no
    // real item/outfit catalog in scope beyond PcuCatalogOutfits above).
    // Reuses its own `cashBalance` rather than playerCash() above - PCU is
    // explicitly "local-player-only" (Sec40.5) and this project does not
    // claim the two purchase systems (weapon-store cash vs. PCU cash)
    // share one real wallet anywhere in scope.
    struct PcuInventory {
        static constexpr int kOwnedItemCapacity = 2048;   // CONFIRMED
        static constexpr int kSavedOutfitCapacity = 128;  // CONFIRMED
        int ownedItemCount = 0;
        int savedOutfitCount = 0;
        double cashBalance = 0.0; // CHOSEN starting balance - no real initial value given anywhere in scope
        uint64_t purchaseSlotChargedWhileFullCount = 0;
        uint64_t purchaseOutfitChargedWhileFullCount = 0;
        int lastWornCatalogPosition = -1; // -1 = none worn yet; pcu_wear_store_outfit's own test-observable result
        // pcu_purchase_outfit's own test-observable resolution via its OWN
        // "bit 0 only" rule - kept as a SEPARATE field from
        // lastWornCatalogPosition above (not unified) specifically so a
        // test can directly compare the SAME raw Lua index resolving to
        // TWO DIFFERENT catalog-list positions across the two functions,
        // proving the mandated divergence rather than merely asserting it.
        int lastPurchasedOutfitCatalogPosition = -1;
    };
    PcuInventory& pcuInventory() { return pcuInventory_; }

    // --- Sec44/Sec45/Sec46 tranche follow-up (ranking tranches 12-14),
    // batch 2026-10-02 (resumed session) -----------------------------------

    // ambient_gang_spawn_enable (Sec45.6): CONFIRMED "a single boolean fans
    // out to all four ambient-gang-enable bytes at once - no per-gang Lua
    // control exists at this entry point." Since nothing in this project's
    // scope can ever observe the 4 bytes independently (no per-gang getter,
    // and the only writer sets them in lockstep), they are modeled as ONE
    // global value rather than 4 redundant copies - a stated simplification,
    // not a claim the real engine stores only one byte.
    OpenValue<bool>& ambientGangSpawnEnabled() { return ambientGangSpawnEnabled_; }

    // cell_camera_enable / cell_camera_is_enabled (Sec45.4): CONFIRMED "a
    // global that has exactly one other reference anywhere in the binary -
    // its own getter ... confirmed Lua-side-only state with no engine
    // consumer." A single bare global, not per-name.
    OpenValue<bool>& cellCameraEnabled() { return cellCameraEnabled_; }

    // ambient_cop_spawn_enable / action_nodes_shouldnt_flee /
    // action_nodes_restrict_spawning (Sec46.3): CONFIRMED "simple global-
    // byte toggles gating AI spawn/flee behavior, none replicated." Three
    // genuinely distinct bytes (unlike the ambient-gang case above, nothing
    // in Sec46.3's text says these three share storage) - each write-only
    // in this project's scope (no in-scope Lua getter reads any of them
    // back; a test can).
    OpenValue<bool>& ambientCopSpawnEnabled() { return ambientCopSpawnEnabled_; }
    OpenValue<bool>& actionNodesShouldntFlee() { return actionNodesShouldntFlee_; }
    OpenValue<bool>& actionNodesRestrictSpawning() { return actionNodesRestrictSpawning_; }

    // --- Batch 2026-10-03, spec-lua-api-behaviour.md Sec49 ("ranking
    // tranche 16") - see each accessor's own doc comment below, and
    // lua_spec_confirmed_stubs.cpp's own Sec49 batch header comment, for
    // which of its 25 names this pass implements: the 8 explicitly
    // mandated crash guards/faithful quirks, PLUS `spawning_boats` (the
    // one name this batch's own fresh mission-drive run found a REAL
    // measured hit for - every other name was zero real hits, the same
    // outcome Sec38/Sec39/Sec40's own batch already found for their
    // combined 75 names). ------------------------------------------------

    // shop_purchase_purchase_shop (Sec49.1, one-name registrar
    // `0x00a03600`, `ui`). No Lua arguments - the real function resolves
    // "the" shop via an AMBIENT trigger-handle lookup (CONFIRMED: "a
    // trigger-handle lookup that returns null when the trigger is stale
    // or absent"), not a Lua-visible name/handle argument. This project
    // has no spatial/trigger-overlap system (same boundary as
    // shop_enable_nearest's own test-only Shop fixture above) -
    // `triggerResolves` is this project's own CHOSEN plain stand-in for
    // "the ambient lookup currently resolves to a shop," default false
    // (the honest "nothing wired up" answer, same precedent as
    // VcustPreview::targetLive above). CONFIRMED real defect pair
    // (Sec49.1): a null shop pointer is dereferenced unconditionally on
    // the very next read (guarded here, HOST-SAFETY, via
    // nullShopGuardCount); separately, once a shop DOES resolve, every
    // side effect except the one-time ownership bit (`owned`) - the
    // payment, the trigger-disable, the owned-object broadcast, and the
    // property fly-by - repeats on EVERY subsequent call with no "already
    // purchased" guard. Implemented faithfully (NOT "fixed" into a
    // once-only purchase) via the four *RepeatCount fields below, which
    // increment on every resolved call regardless of `owned`.
    struct ShopPurchase {
        bool triggerResolves = false;    // CHOSEN default false, see comment above
        bool owned = false;              // CONFIRMED: the ONE guarded bit
        double price = 0.0;              // CHOSEN, test-settable - no real price table in scope
        uint64_t nullShopGuardCount = 0; // HOST-SAFETY - test-observable only, not a real engine field
        uint64_t paymentRepeatCount = 0;        // CONFIRMED defect: repeats every resolved call
        uint64_t triggerDisableRepeatCount = 0; // CONFIRMED defect: repeats every resolved call
        uint64_t ownedBroadcastRepeatCount = 0; // CONFIRMED defect: repeats every resolved call
        uint64_t flyByRepeatCount = 0;          // CONFIRMED defect: repeats every resolved call
    };
    ShopPurchase& shopPurchase() { return shopPurchase_; }

    // spawn_override_set_override_category_for_hood (Sec49.2,
    // `0x00a20840` gameplay registrar, `gameplay`). 2 strings
    // (neighbourhood, category name). CONFIRMED real defect: the
    // category-name lookup result is never checked before being stored
    // and dispatched; on a host with a session, the dispatch inside THIS
    // call immediately dereferences the null category; everywhere else
    // the null is merely stored (dereferenced later by the per-hood
    // reader or the co-op join-snapshot serializer, both out of this
    // batch's own scope - not modeled here, since nothing in THIS
    // function crashes on that path). Direct contrast (Sec49.2): the
    // sibling global setter `spawn_global_override_set_category` DOES
    // check its own lookup - that sibling is NOT "fixed" onto this one;
    // `categoryNameResolves` is guard-checked here ONLY for the
    // immediate-dispatch host branch, faithfully leaving the per-hood
    // setter as the one missing its sibling's guard.
    struct SpawnOverride {
        OpenValueMap<bool> categoryNameResolves{"spawn-override category-name lookup resolves",
                                                 "spec-lua-api-behaviour.md Sec49.2"};
        std::unordered_map<std::string, std::string> perHoodCategory; // CHOSEN: last successfully-resolved category, by hood
        std::unordered_set<std::string> perHoodCategoryNullStored;    // CHOSEN: hoods currently holding the real defect's unchecked null (deferred read, not modeled further)
        uint64_t hoodCategoryImmediateNullDerefGuardCount = 0; // HOST-SAFETY - test-observable only, not a real engine field
    };
    SpawnOverride& spawnOverride() { return spawnOverride_; }

    // spawning_boats (Sec49.2, `0x00a20840` gameplay registrar,
    // `gameplay`) - added to this batch after its own fresh mission-drive
    // pass found a REAL measured hit (1 incremental call from a real
    // mission script, not just a static call-site count - see this
    // header's own top-of-batch comment and lua_spec_confirmed_stubs.cpp's
    // own Sec49 batch header comment). 1 mandatory boolean. CONFIRMED:
    // writes a host-authoritative, session-replicated byte (registered
    // name `"os_boat"`) with NO host gate on the write itself - unlike
    // tranche 13's sibling `audio_suppress_ambient_player_lines` (Sec45,
    // itself unimplemented anywhere in this project), which DOES gate.
    struct SpawningBoats {
        bool value = false; // CHOSEN initial value - the "os_boat" replicated byte itself
        uint64_t writeCount = 0;
    };
    SpawningBoats& spawningBoats() { return spawningBoats_; }

    // skydive_move_to_check_done (Sec49.3, `0x00a20840` gameplay
    // registrar, `gameplay`). Argument shape NOT independently re-derived
    // this pass (same "HIGH CONFIDENCE, not CONFIRMED" caveat
    // turn_to_check_done's own doc comment already carries for an
    // analogous gap) - CHOSEN, labelled: 1 string (the target name) + 1
    // boolean ("list mode" selector; true = the sibling branch the spec
    // calls already-correct, false = the single-object-target-mode branch
    // the crash shape belongs to). CONFIRMED (Sec49.3): in single-object
    // mode, when the target doesn't resolve, the real code computes
    // `null_pointer+0x40` and tests THAT SUM (which survives the null
    // check) rather than the pointer itself - a one-off miss; the
    // list-mode branch handles the identical failure correctly and
    // returns "done". HOST-SAFETY DEVIATION (not a spec fact): rather than
    // reproducing the crash, this host gives the SAME answer the
    // already-correct sibling branch gives for the identical failure
    // ("done", pushed true) - counted via nullTargetGuardCount. A
    // resolved target's own done/pending determination (in EITHER mode)
    // is OPEN - Sec49.3 gives no further detail beyond the crash shape
    // itself, so the stub refuses that case via OpenStateError rather
    // than inventing one.
    struct SkydiveMoveToCheckDone {
        uint64_t nullTargetGuardCount = 0; // HOST-SAFETY - test-observable only, not a real engine field
    };
    SkydiveMoveToCheckDone& skydiveMoveToCheckDone() { return skydiveMoveToCheckDone_; }

    // set_char_in_string (Sec49.4, `ui`). 1 string + 1 0-based index
    // (unlike Lua's usual 1-based convention, CONFIRMED) + 1 single-
    // character string. CONFIRMED real crash shapes: (1) "builds its
    // result in a stack buffer sized by the index with no cap - a large
    // or NaN-derived index can overrun the stack guard outright, reachable
    // from script data alone"; (2) "either string argument being non-a-
    // string reads through null" - already avoided by construction here
    // via this file's own established NULL-safe argString() wrapper (same
    // precedent noted for team_make_hostile's identical-shaped defect,
    // Sec38.2), nothing further to guard for (2). This project has no
    // literal stack guard to overrun (standing project note), so (1) is
    // modeled as a MANDATED bounds check: a CHOSEN cap, `kMaxIndex`, below
    // the real buffer's own unconfirmed size - chosen as 1024, the SAME
    // magnitude independently confirmed elsewhere in this binary for a
    // fixed stack string buffer (spec-lua-api-behaviour.md Sec44's
    // `game_coop_kick_player`/`0x00db0510` finding: "always tells the CRT
    // its buffer holds 1024 wide characters") - NOT claimed to be the SAME
    // buffer, only a reasoned size-class stand-in, clearly not a spec
    // fact. An index outside [0, kMaxIndex] is guarded and logged rather
    // than built. The in-bounds result-construction itself (pad with a
    // CHOSEN space fill to reach the index, then write the given
    // character there) is this project's own CHOSEN, labelled minimal
    // reading of "set a char in a string" - Sec49.4 gives no further
    // detail on the real padding/construction algorithm beyond the crash
    // shape, so nothing here claims to be the real byte-for-byte result.
    struct SetCharInString {
        static constexpr int64_t kMaxIndex = 1024; // CHOSEN cap, see comment above
        uint64_t indexOutOfBoundsGuardCount = 0;   // HOST-SAFETY - test-observable only, not a real engine field
    };
    SetCharInString& setCharInString() { return setCharInString_; }

    // store_stronghold_game_purchase_upgrade (Sec49.1, `ui`). No Lua
    // arguments - same ambient-context situation as shop_purchase_
    // purchase_shop above (`strongholdResolves`, CHOSEN default false).
    // CONFIRMED (Sec49.1, reconciled 2026-10-03 against Sec8.27/Sec26.28):
    // charges the stronghold's price UNCONDITIONALLY once a stronghold
    // resolves (no affordability check - same shape as store_weapon_
    // purchase_ammo's own unconditional charge, Sec38.3), then attempts a
    // level-up whose own gate - a session exists AND this machine is host
    // - is CONFIRMED false in single player (EngineState::coopLocalIsHost(),
    // Sec8.27/Sec26.28's exhaustive negative check) - so the level-up
    // silently no-ops, every time, in this host as in real single-player
    // play: cash is always deducted, the stronghold level never changes.
    // Separately CONFIRMED: "No local-player null check either" - a
    // second, independent crash shape, guarded here (HOST-SAFETY) via
    // hasLocalPlayer() (reused - the SAME "player 1 exists" concept
    // store_gallery_download_hide_list's own guard already reuses,
    // Sec39.5/Sec28.23).
    struct StrongholdPurchaseUpgrade {
        bool strongholdResolves = false; // CHOSEN default false, see comment above
        double price = 0.0;              // CHOSEN, test-settable - no real price table in scope
        uint64_t chargeCount = 0;                // CONFIRMED: charged unconditionally once resolved
        uint64_t levelUpGateFailedNoopCount = 0; // CONFIRMED: session-exists-and-host gate false in single player
        uint64_t localPlayerNullGuardCount = 0;  // HOST-SAFETY - test-observable only, not a real engine field
    };
    StrongholdPurchaseUpgrade& strongholdPurchaseUpgrade() { return strongholdPurchaseUpgrade_; }

    // squad_enable (Sec49.3, `gameplay`). 1 character name + 1 boolean
    // (CONFIRMED default false). CONFIRMED real asymmetric-effect defect:
    // `squad_enable(name, false)` actually dismisses the character from
    // the player's crew (restores stats, removes it from the roster,
    // releases resources - this project's own minimal stand-in only models
    // the roster-membership bit, `inRoster`; the stat-restore/resource-
    // release internals are OPEN, not given beyond the fact they happen);
    // `squad_enable(name, true)` only flips two unrelated flag bits
    // (`flagBitA`/`flagBitB` - CHOSEN names, real meanings OPEN) and does
    // NOT re-add the character to the crew - `inRoster` is deliberately
    // left untouched on the true path, faithfully reproducing the defect
    // rather than "fixing" the two calls into real inverses.
    struct SquadMember {
        bool inRoster = false;
        bool flagBitA = false;
        bool flagBitB = false;
        uint64_t dismissCount = 0;
        uint64_t enableTrueCount = 0;
    };
    SquadMember& squadMember(const std::string& name) { return squadMembers_[name]; }

    // screen_capture_preview_should_upload (Sec49.4, `ui`). 1 boolean, 0
    // Lua return values either way (CONFIRMED). CONFIRMED: an explicit
    // `false` argument takes a "cancel" path; a denied platform privilege
    // silently takes the SAME cancel path (so the calling script cannot
    // tell the two apart from the return value alone - there is none).
    // This project has no real platform-privilege layer anywhere in scope
    // to query (CHOSEN, explicitly labelled, NOT a confirmed value): the
    // "platform denied" fork is modeled as ALWAYS "not denied" here, so
    // only the explicit-false path is ever actually reachable from this
    // host - `explicitFalseCancelCount` is the one CONFIRMED path;
    // `uploadProceedCount` is this host's own stand-in answer for every
    // `true` call (never "denied").
    struct ScreenCapturePreview {
        uint64_t explicitFalseCancelCount = 0; // CONFIRMED: explicit false -> the cancel path
        uint64_t uploadProceedCount = 0;       // CHOSEN "always not denied" stand-in for a true call, see comment above
    };
    ScreenCapturePreview& screenCapturePreview() { return screenCapturePreview_; }

    // sfx_use_load_images (Sec49.4, `ui`) needs no state - CONFIRMED
    // hardcoded true (0x0149365c set unconditionally at start-up,
    // independently re-confirmed this tranche; already in Sec26.24's own
    // screen-fade table) - the stub pushes a literal true directly, no
    // accessor needed here.

    // --- Ranking tranches 15/17/20/22/23 (spec-lua-api-behaviour.md
    // Sec52/Sec51/Sec53/Sec55/Sec57), real-hit names only (2026-10-03,
    // orchestrator-directed pass over the whole Sec50-Sec59 backlog: only
    // names with a real, measured non-zero mission-drive call count get a
    // real implementation; every other name in that backlog stays a
    // generic logging stub - see the implementing commit's own message for
    // the per-section tally). -----------------------------------------------

    // tutorial_lock (Sec52.3, tranche 15). CONFIRMED: "it only accepts
    // tutorial indices up to 188... the 21 entries from 189-209 can never
    // be locked through this native at all." The resulting lock
    // state/mechanism itself is not given beyond that bound (OPEN) - this
    // project counts a "would lock" hit per in-range index rather than
    // fabricate a state code into tutorialState()'s own stride-9 enum
    // (whose "locked" value is nowhere confirmed in this spec). Reuses
    // EngineState::tutorialLookup() (Sec10.4, above) for name resolution -
    // the SAME 210-entry table every sibling in this family shares.
    // Test-observable only.
    int tutorialLockCount(int index) const;
    void recordTutorialLock(int index);

    // radio_newsbreak_clear (Sec51.3, tranche 17). CONFIRMED only as "a
    // straightforward local-only state change[...], no further crash
    // shapes" - no further detail given, so this is modeled the same
    // minimal way as the Sec46.3 write-only flags above: one boolean,
    // write-only in this project's scope (no in-scope Lua getter reads it
    // back; a test can).
    OpenValue<bool>& radioNewsbreakActive() { return radioNewsbreakActive_; }

    // group_create_do / group_create_hidden_do (Sec53.3, tranche 20).
    // CONFIRMED: "the same underlying creation routine differing only in
    // one passed literal, and creation happens exactly once - calling
    // group_create_hidden_do on a group group_create_do already created
    // does nothing at all." One shared "has this name already been
    // created" set - which literal (visible/hidden) was used first is
    // never read back by anything in this project's scope, so it is not
    // separately tracked (would be fabricated detail).
    bool groupAlreadyCreated(const std::string& name) const { return groupsCreated_.count(name) != 0; }
    void markGroupCreated(const std::string& name) { groupsCreated_.insert(name); }

    // dlc2_m02_clapboards_set/_get/_reset (Sec55.5, tranche 22). CONFIRMED:
    // a 10-byte flag array plus a count, file-default count -1 ("so every
    // index fails the upper bound too" before the first _reset call). Only
    // _get and _reset show a real mission-drive hit this pass; _set does
    // not and stays a generic stub per this pass's own scope rule, so
    // _set's own separate out-of-bounds WRITE shape is deliberately not
    // reproduced or guarded here. _get's CONFIRMED upper-bound check (out
    // of range -> "no value at all", not nil/false pushed, not even a
    // boolean) is modeled directly; _get's own missing LOWER bound (any
    // argument <= 0 is the real arbitrary-read defect) is a HOST-SAFETY
    // DEVIATION refused here instead of reproduced - this project does not
    // simulate crashes/arbitrary reads (the established group_get_next_npc
    // / Sec28.5 precedent this task's own instructions name directly) -
    // counted separately from the ordinary upper-bound "out of range" case.
    int dlc2ClapboardCount() const { return dlc2ClapboardCount_; }
    void dlc2ClapboardsReset(int32_t rawCount);
    // Returns -1 for "no value" (the real native's own CONFIRMED out-of-
    // range answer - nil, not false - and the HOST-SAFETY-refused case
    // below); otherwise 0 or 1, the flag byte at the resolved index.
    int dlc2ClapboardsGet(int32_t clapboardNumber);
    int dlc2ClapboardsGetNegativeIndexGuardCount() const { return dlc2ClapboardsGetNegativeIndexGuardCount_; }

    // cutscene_play_do / cutscene_play_check_done (Sec57.2, tranche 23) -
    // see src/lua_cutscene.cpp's own doc comment above cutscenePlayDo() for
    // the full design (reuses the EXISTING zscene prep gate and screen-
    // fade-request mechanism, both already Sec26.25-confirmed; the
    // destination-table and "written even when refused" quirks are modeled
    // directly). NOT modeled (unobservable through any in-scope native, so
    // nothing to fabricate): the uninitialized script-group-handle copy
    // and the failed-scene-manager-allocation null-write path (reachability
    // HYPOTHESIS per Sec57.2 itself).
    void cutscenePlayDo(const std::string& name, std::vector<std::string> destinations);
    const std::string& cutscenePlayDestination1() const { return cutscenePlayDestination1_; }
    const std::string& cutscenePlayDestination2() const { return cutscenePlayDestination2_; }
    bool cutscenePlayInProgress() const { return cutscenePlayInProgress_; }
    // CONFIRMED (Sec57.2): "treats 'no manager' as equivalent to
    // 'finished'" - this host never allocates cutsceneManager_.present
    // (the allocation path itself is OPEN/HYPOTHESIS, not modeled, see
    // above), so this honestly and permanently reproduces the documented
    // "reports done prematurely" bug rather than a simplification of it
    // (same "minimum correct model" precedent as Sec35.6's scripted-
    // request pool).
    bool cutscenePlayCheckDone() const {
        return !(cutsceneManager_.present.known() && cutsceneManager_.present.get());
    }

    // --- OPEN-slot inventory (manager prep 2026-10-01) -----------------------

    // One row per engine-state slot the stubs read, grouped by area (co-op,
    // tutorial, vehicle-store, zscene, fade, vint, other). Rows stay listed
    // after a spec answer fills them (known / knownKeys then say so).
    // `known` is "value set" for a scalar, "every bit known" for a bit word;
    // `knownKeys` counts set keys for a per-name map (scalars: 0/1). lua_host_run
    // writes it as verdict_open_state.tsv, so a re-run shows which slots a
    // newly implemented answer filled.
    struct OpenSlotStatus {
        std::string area;
        std::string global;
        std::string spec;
        std::string kind; // "value", "per-name map" or "bit word"
        bool known = false;
        size_t knownKeys = 0;
    };
    std::vector<OpenSlotStatus> openSlotInventory() const;

private:
    std::unordered_map<std::string, CharacterState> characters_;
    std::unordered_map<std::string, VehicleState> vehicles_;
    CoopSession coopSession_;
    int64_t nextAudioVoiceHandle_ = 1;
    std::vector<PegLoadRequest> pegLoadRequests_;

    // std::map (not unordered): findVdoObject's searches walk handles in
    // ascending order, which for a loaded document is the file's pre-order
    // (loadVintDocument issues handles in that order) - see findVdoObject.
    std::map<uint32_t, VdoObject> vdoObjects_;
    uint32_t nextVdoObjectHandle_ = 1;
    std::map<uint32_t, LoadedVintDocument> loadedVintDocuments_;
    uint32_t nextVintDocumentHandle_ = 1;
    OpenValue<uint32_t> currentDefaultDocHandle_{"current default vint document (0x00e0ceb0)", "spec-lua-bindings.md Sec15"};

    // --- Vint UI API, Sec18-Sec21 batch (2026-10-02) - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    OpenValueMap<double> vintTimeIndexByDoc_{"vint document time index (+0x574), by resolved document handle",
                                              "spec-lua-bindings.md Sec19.1"};
    std::unordered_map<uint32_t, VintDataItem> vintDataItems_;
    uint32_t nextVintDataItemHandle_ = 1;
    // Outer key: resolved VDO object handle; inner key: sr3save::nameHash of
    // the (case-folded-by-that-hash) property name (Sec20.1/Sec9.2).
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::vector<VintTaggedValue>>> vintProperties_;
    // Keyed by sr3save::nameHash of the data-responder name (Sec21's own
    // "hash the name with the engine's standard string-hash" text).
    std::unordered_map<uint32_t, VintDataResponderRecord> vintDataResponders_;

    // --- teleport_check_done/turn_to_check_done/move_to_check_done/
    // vehicle_pathfind_check_done (Sec35.2), see each public accessor's own
    // doc comment above for citation/reasoning. Keyed by (name, kind) - see
    // kScriptedRequestKind* above for the 3 real kind values. `bool` = done
    // (true) vs. pending (false); absent = "no matching request" (status 2).
    std::map<std::pair<std::string, int>, bool> scriptedRequestDone_;
    std::unordered_set<std::string> vehiclePathfindResolvable_; // Sec9.10, test-only gate

    // --- fields backing the 9 functions from spec-lua-api-behaviour.md
    // Sec10.1-Sec10.9, see each field's own public-accessor doc comment
    // above for citation/reasoning. ---
    OpenValue<bool> vehicleStoreActive_{"vehicle-store active flag (0x022cdf08)", "spec-lua-api-behaviour.md Sec10.1/Sec26.28"};
    OpenValue<bool> hasLocalPlayer_{"local player exists (0x009da4e0)", "spec-lua-api-behaviour.md Sec10.3"};
    int hudInventoryRefreshCount_ = 0;                         // Sec10.3
    std::unordered_map<int, int> tutorialAdvanceCounts_;       // Sec10.4, by table index
    std::string qteAnimationTriggerCallback_;                  // Sec10.7
    OpenValue<int> coopJoinType_{"0x012f44fc (co-op join type)", "spec-lua-api-behaviour.md Sec10.9"};
    OpenValueMap<int> tutorialState_{"tutorial entry state (0x0151d600[i] +0x0c)", "spec-lua-api-behaviour.md Sec6.19/Sec10.4/Sec26.28"};
    OpenValueMap<bool> objectResolves_{"named-object resolution", "spec-lua-api-behaviour.md Sec3.9/Sec10.6/Sec29"};
    // DIAGNOSTIC ONLY state (see enableUnresolvedNameProbe()/probedFieldOr()
    // above) - empty, and probedFieldReadCount_ zero, unless a caller
    // explicitly opts in; never populated by any normal code path or test.
    std::unordered_set<std::string> probedNames_;
    uint64_t probedFieldReadCount_ = 0;

    // --- zscene (Sec26.25/Sec14.23/Sec8.21), see the accessor doc comment above ---
    OpenValueMap<bool> zsceneLoadable_{"zscene table entry with kind 1 (0x00721be0 lookup, 0x00723d20 test)",
                                       "spec-lua-api-behaviour.md Sec26.25/Sec14.23"};
    bool zsceneTableInstalled_ = false;
    std::vector<ZsceneTableRow> zsceneTable_;
    OpenValue<bool> zsceneSkipAllCutscenes_{"0x0153b556 (skip_all_cutscenes)", "spec-lua-api-behaviour.md Sec26.25/Sec8.21"};
    OpenValue<std::string> zsceneCurrent_{"0x0153b530 (current scene entry)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<std::string> zscenePending_{"0x0153b538 (pending scene entry)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<int> zsceneStateCode_{"0x0153b51c (zscene load state)", "spec-lua-api-behaviour.md Sec26.25/Sec14.23"};
    OpenValue<bool> zsceneAutoSelectNearest_{"0x0153b541 (auto-select the nearest scene when idle)",
                                             "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> zsceneRequeueOnReset_{"0x0153b542 (re-queue the current entry on reset)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValueMap<int> zsceneHandleClass_{"selected request-group handle class (0x00dafb60: 1 not live, 3 resident, 5 failed) of scene entry",
                                         "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> zsceneSoundtrackActive_{"0x0153b71c (soundtrack stream started, not released)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<int64_t> zsceneSoundtrackStartMs_{"0x0153b724 (soundtrack timer start, host ms)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> zsceneSoundtrackEnded_{"soundtrack stream status 0x66 (ended; no audio in this host)",
                                           "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<std::string> zsceneNearestWorldObjectScene_{"nearest scene-bearing world object (list 0x03171a64; no world objects in this host)",
                                                          "spec-lua-api-behaviour.md Sec26.25"};
    int zscenePendingBlockedRefusals_ = 0;
    // The cutsceneCounters_.framesRun value as of the most recent time a NEW
    // (non-empty) key was written into zscenePending_ - 2026-10-02 correction
    // (the `mm_p_01` zscene-promotion-driver investigation): EngineState is
    // shared across an entire mission-drive run, so cutsceneCounters_.
    // lastFrameBlocked can be true only because of some earlier, unrelated
    // mission's own frames (nothing pending there either), long before this
    // name was ever prepped. zsceneIsLoaded's pending-blocked refusal (below)
    // must not fire on that stale signal; it needs at least one frame to have
    // actually run for THIS pending entry first.
    uint64_t zscenePendingSetAtFrame_ = 0;

    // --- cutscene machine (Sec26.25 item 6) ---
    CutsceneManager cutsceneManager_;
    OpenValue<bool> cutscenePreloadMounted_{"0x0153b543 (preload packfiles mounted)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<std::string> cutsceneChainTarget_{"0x0153b52c (next cutscene to chain into)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> cutscenePlayerChecksPass_{"cutscene state 3 player-side checks", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<int64_t> cutsceneLoadStamp_{"0x012f5d30 (120 s resource-load stamp)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> cutscenePlayingByte_{"0x0153b525 (a cutscene is playing)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> cutsceneInProgressByte_{"0x0153b526 (a cutscene is in progress)", "spec-lua-api-behaviour.md Sec26.25"};
    std::vector<std::string> cutsceneMountLog_;
    std::vector<std::string> cutsceneLoadLog_;
    CutsceneCounters cutsceneCounters_;

    // src/lua_cutscene.cpp internals: each "plan" only reads (and may throw
    // OpenStateError); each "apply" only writes.
    struct ZsceneResolved {
        bool found = false;        // a table entry (or, without a table, the per-name map) exists
        bool loadable = false;     // found and kind == 1
        std::string key;           // zsceneTableKey of the entry
    };
    ZsceneResolved zsceneResolve(const std::string& name) const;
    enum class TeardownKind { Nothing, NotLive, Live };
    TeardownKind zsceneTeardownPlan(int c) const;
    void zsceneTeardownApply(TeardownKind kind, int a, int b);
    struct ResetPlan { // 0x00720320
        bool result = false;
        enum class Write { None, SetAutoSelect, ResetNotLive } write = Write::None;
        std::string current;
        bool requeue = false;
    };
    ResetPlan zsceneResetPlan() const;
    void zsceneResetApply(const ResetPlan& plan);
    bool zsceneCutsceneGuard() const;
    bool zsceneSoundtrackFinished() const;
    struct GatePlan {
        bool proceeds = false;     // false: the gate returns (missing / kind != 1 / skip / already current)
        std::string key;
        TeardownKind teardown = TeardownKind::Nothing;
    };
    GatePlan zsceneGatePlan(const std::string& name) const;
    void zsceneGateApply(const GatePlan& plan);
    void zscenePromotionStep();
    void zsceneCompletionStep();
    void cutsceneMachineStep();       // 0x0072d660
    void cutsceneSecondStep();        // 0x007258a0
    void cutsceneSetState(int32_t s);

    // --- screen fade (Sec26.24), see the accessor doc comment above ---
    ScreenFade screenFade_;
    lua_State* screenFadeUi_ = nullptr;
    int64_t screenFadeClockMs_ = 0; // host clock, see screenFadeHostFrame
    enum class FadeFallbackReason { Undefined, NoCallback };
    struct FadeFallbackTimer {
        bool armed = false;
        int64_t dueMs = 0;
        FadeFallbackReason reason = FadeFallbackReason::Undefined;
    } screenFadeFallback_;
    ScreenFadeCounters screenFadeCounters_;
    void screenFadeFrameRoutine();
    bool screenFadeCallUi(const char* name, int nargs, double a0, double a1, double a2, bool* errored);

    // --- vint_is_std_res / vint_get_safe_frame (Sec26.26) ---
    OpenValue<int32_t> vintRecordFirst_{"per-thread record (0x00e236f0)+4 (copy of width 0x02a5a180; UI init 0x00e23910 not run)",
                                        "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintRecordSecond_{"per-thread record (0x00e236f0)+8 (copy of height 0x02a5a184; UI init 0x00e23910 not run)",
                                         "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintDisplayMode_{"0x0132bd80 (display mode)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintGlobalWidth_{"0x02a5a180 (display width)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintGlobalHeight_{"0x02a5a184 (display height)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintLayoutIndex_{"0x0132c0ac (layout index: 1 standard, 0 wide)", "spec-lua-api-behaviour.md Sec26.26"};
    int vintLibInitConstantsCalls_ = 0;
    int vintDocumentResetsNotModelled_ = 0;
    OpenValue<int32_t> vintSafeFrameA_{"safe-frame source +0x8 ((context +0x674)+0x14)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintSafeFrameB_{"safe-frame source +0xc ((context +0x674)+0x14)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<double> vintSafeFrameScale1_{"double constant 0x0115ba60", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<double> vintSafeFrameScale2_{"double constant 0x0116dfc0", "spec-lua-api-behaviour.md Sec26.26"};

    // --- fade_out (Sec2.9) / mission_end_silently (Sec15.23) ---
    bool hasScreenFadeColour_ = false;
    ScreenFadeColour screenFadeColour_;
    std::vector<ScreenFadeRequest> screenFadeRequests_;
    int screenFadeOpcode53Count_ = 0;
    OpenBits32 missionFlagsWord_{"0x014c848c", "spec-lua-api-behaviour.md Sec15.23"};

    // --- Batch 2026-10-01, spec-lua-api-behaviour.md Sec27/Sec28 - see each
    // public accessor's own doc comment above for citation/reasoning. ---
    CatMouseMinigame catMouseMinigame_;
    OpenValueMap<bool> missionComplete_{"character/mission object +0x88 bit 0x4 ('is mission complete')",
                                        "spec-lua-api-behaviour.md Sec27.2/Sec20.14"};
    CompletionScreen completionScreen_;
    VcustCamera vcustCamera_;
    bool pauseMenuSeenDisplayCalScreen_ = false; // Sec27.6, CONFIRMED zero at start
    int32_t textAdventureScreenIndex_ = -1;      // Sec27.7, CONFIRMED -1 initial
    int32_t hordeResultsEndAction_ = 2;          // Sec27.8, CONFIRMED initial 2
    GaragePreview garagePreview_;
    bool coopLobbyFinished_ = false; // Sec27.10, CONFIRMED zero-filled
    DialogForceClose dialogForceClose_;
    Autosave autosave_;
    PlayerSlots playerSlots_;
    OpenValue<int32_t> coopFriendlyFireRaw_{"0x012f4500 (coop friendly-fire internal mode)",
                                            "spec-lua-api-behaviour.md Sec27.15/Sec27.16"};
    OpenValue<bool> steamInterfaceAvailable_{"Steam user interface pointer (0x0101c54c import) non-null",
                                             "spec-lua-api-behaviour.md Sec27.19"};
    std::vector<SignInRequest> signInRequests_;
    uint64_t signInErrorDialogCount_ = 0;
    bool coopLiveSessionActive_ = false;    // 0x014ff6c1, Sec27.23, CONFIRMED zero-filled
    bool coopSyslinkSessionActive_ = false; // 0x014ff6c2, Sec27.24, CONFIRMED zero-filled
    InProgressType inProgressType_;

    std::unordered_map<std::string, bool> helicopterDontDeathSpiral_;
    HelicopterFlyTo helicopterFlyTo_;
    float hdrBloomMultiplier_ = 1.0f; // Sec28.3, CONFIRMED initial 1.0
    GuardianAngel guardianAngel_;
    OpenValueMap<std::string> groupNextNpcName_{"character +0x9c next name (circular list, group head stops it)",
                                                "spec-lua-api-behaviour.md Sec28.5"};
    OpenValueMap<std::string> groupFirstNpcName_{"group +0x40 head name", "spec-lua-api-behaviour.md Sec28.6"};
    OpenValueMap<int32_t> humansInTriggerCount_{"trigger occupancy count (0x0075d4b0/category 47)",
                                                "spec-lua-api-behaviour.md Sec28.7"};
    OpenValueMap<bool> vehicleInAirByCharacter_{"character's cached active vehicle is airborne (0x00ad4040)",
                                                "spec-lua-api-behaviour.md Sec28.8"};
    EffectFinisher effectFinisher_;
    bool dlc3SprintWaning_ = false; // Sec28.10, CONFIRMED zero at start
    std::unordered_map<int64_t, int> debrisFlowRecycleCount_;
    PlayerRig playerRig_;
    bool cribWeaponAddEnabled_ = true; // Sec28.13/Sec28.14, CONFIRMED file value 1
    Stronghold stronghold_;
    ContinuousExplosion continuousExplosion_;
    std::unordered_map<std::string, uint64_t> callbacksClearedCount_;
    std::unordered_set<uint32_t> activeCityZoneSwapHashes_;
    CounterOnGrabbed counterOnGrabbed_;
    std::unordered_map<std::string, std::vector<std::string>> characterChildItems_;
    OpenValueMap<int32_t> characterGender_{"character +0xa41 gender byte (1=female, else male)",
                                           "spec-lua-api-behaviour.md Sec28.21/Sec28.12/Sec16.8"};
    std::unordered_map<std::string, uint64_t> evacuateFromVehiclesCount_;
    OpenValue<bool> cellphoneAnimSuppressed_{"0x00943a20(player) result", "spec-lua-api-behaviour.md Sec28.23"};
    uint64_t cellphoneAnimPlayedCount_ = 0;
    BossBattleMatt bossBattleMatt_;

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec30 ("ranking
    // tranche 03") - see each public accessor's own doc comment above. ---
    bool autoPickupEnabled_ = true; // Sec30.3, CONFIRMED file-backed default "enabled"
    int vehicleExitGroupNullThisGuardCount_ = 0; // Sec30.5/Sec30.7 - test-observable only, not a real engine field
    std::unordered_map<std::string, int> teamIdByName_; // Sec30.6/Sec20.6, test-only; unregistered -> sentinel 9
    // CONFIRMED 8x8 (Sec20.6); -1 = "never written" (this project's own sentinel, not a spec fact).
    std::array<std::array<int, kTeamRelationMatrixDim>, kTeamRelationMatrixDim> teamRelation_ = [] {
        std::array<std::array<int, kTeamRelationMatrixDim>, kTeamRelationMatrixDim> m{};
        for (auto& row : m) row.fill(-1);
        return m;
    }();
    int teamRelationOobGuardCount_ = 0; // Sec30.6/Sec30.7 - test-observable only, not a real engine field

    // --- Ranking tranche 05 (Sec33), batch 2026-10-02 - see each public
    // accessor's own doc comment above for citation/reasoning. vehicles_
    // itself is declared once already above (shared with Sec30.5's own
    // vehicle cluster, same VehicleState struct). ---
    std::unordered_map<std::string, int32_t> syncedActionIndexByName_;
    ActionSequence actionSequence_;
    std::string audioListenerOverrideTarget_;
    OpenValueMap<bool> conversationChannelActive_{"per-session conversation channel active slot",
                                                   "spec-lua-api-behaviour.md Sec33.4"};
    WaitingForPlayerDialog waitingForPlayerDialog_;
    bool allowWeaponAutoPickup_ = true; // Sec33.5, CHOSEN default (no confirmed file value in scope)
    // --- pause-map / Sec31, Sec32.1 pause_map_tutorial_mode - batch
    // 2026-10-02 - see each public accessor's own doc comment above for
    // citation/reasoning. ---
    OpenValue<bool> pauseMapTutorialMode_{"0x0229a318 (pause-map tutorial-mode flag)",
                                          "spec-lua-api-behaviour.md Sec31.5/Sec32.1"};
    uint32_t pauseMapSelectedZone_ = 0; // 0x0229a2ac, see pauseMapSelectedZone()'s own doc comment
    PauseMapHoveredZone pauseMapHoveredZone_;
    std::unordered_map<uint32_t, std::vector<ZoneMember>> zoneMembersByZone_;
    uint64_t pauseMapStagCompletionHookFiredCount_ = 0;
    uint64_t pauseMapTakeoverClaimedCount_ = 0;
    uint64_t pauseMapTakeoverAutosaveRequestCount_ = 0;

    // --- ranking tranche 04, Sec32 - batch 2026-10-02 - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    OpenValue<bool> storeInterfaceActive_{"store-interface active (0x022cce86 bit 0x1)", "spec-lua-api-behaviour.md Sec32.1"};
    float spawnRegionMaxSpawnDistSquared_ = 3.402823466e+38f; // FLT_MAX, CONFIRMED file-backed default
    float pedOverrideDensity_ = -1.0f;                        // CONFIRMED file-backed default ("no override")
    GameClock gameClock_;
    SatelliteWeaponController satelliteWeapon_;
    std::unordered_map<std::string, bool> seatbeltForceFlag_;
    std::unordered_map<std::string, bool> trailingAimForceFlag_;
    std::unordered_map<std::string, bool> neverTurnOnPlayerFlag_;
    std::unordered_map<std::string, bool> warpToShoreDisabled_;
    SkydiveTankBailout skydiveTankBailout_;
    QteSlot qteSlots_[2];
    PartyState party_;
    std::unordered_set<std::string> destroyedObjects_;
    uint64_t indicatorRemoveRecordCount_ = 0;
    std::vector<Shop> shops_;
    uint64_t shopEnableRecordCount_ = 0;
    std::unordered_map<std::string, bool> itemHidden_;
    std::unordered_map<std::string, ItemAnimState> itemAnimState_;
    std::unordered_map<std::string, VehicleRadio> vehicleRadios_;
    std::unordered_map<std::string, Helicopter> helicopters_;
    OpenValue<bool> helicopterFireDispatcherResult_{
        "shared helicopter-fire dispatcher's own return value (invisible in the decompiled view)",
        "spec-lua-api-behaviour.md Sec32.7"};

    // --- Batch 2026-10-02, spec-lua-api-behaviour.md Sec38/Sec39/Sec40
    // ("ranking tranches 06/07/08") - see each public accessor's own doc
    // comment above. ---
    VcustPreview vcustPreview_;
    double playerCash_ = 0.0; // CHOSEN starting balance - no real initial value given anywhere in scope
    std::vector<PurchaseRecord> purchaseLedger_;
    int skydiveMoveToNullTargetGuardCount_ = 0; // Sec39.1 - test-observable only, not a real engine field
    SaveSystemUi saveSystemUi_;
    StorePreviewGuards storePreviewGuards_;
    StoreCommonRotateMouseDrag storeCommonRotateMouseDrag_;
    PcuCategoryTable pcuCategoryTable_;
    PcuCatalogOutfits pcuCatalogOutfits_;
    PcuInventory pcuInventory_;

    // Sec44/Sec45/Sec46 tranche follow-up (batch 2026-10-02, resumed session) - see the public accessors above.
    OpenValue<bool> ambientGangSpawnEnabled_{"ambient-gang spawn-enable bytes (all 4, fanned out together)",
                                             "spec-lua-api-behaviour.md Sec45.6"};
    OpenValue<bool> cellCameraEnabled_{"cell_camera_enable global", "spec-lua-api-behaviour.md Sec45.4"};
    OpenValue<bool> ambientCopSpawnEnabled_{"ambient_cop_spawn_enable global byte", "spec-lua-api-behaviour.md Sec46.3"};
    OpenValue<bool> actionNodesShouldntFlee_{"action_nodes_shouldnt_flee global byte", "spec-lua-api-behaviour.md Sec46.3"};
    OpenValue<bool> actionNodesRestrictSpawning_{"action_nodes_restrict_spawning global byte",
                                                 "spec-lua-api-behaviour.md Sec46.3"};

    // --- Batch 2026-10-03, spec-lua-api-behaviour.md Sec49 ("ranking
    // tranche 16") - see each public accessor's own doc comment above. ---
    ShopPurchase shopPurchase_;
    SpawnOverride spawnOverride_;
    SpawningBoats spawningBoats_;
    SkydiveMoveToCheckDone skydiveMoveToCheckDone_;
    SetCharInString setCharInString_;
    StrongholdPurchaseUpgrade strongholdPurchaseUpgrade_;
    std::unordered_map<std::string, SquadMember> squadMembers_;
    ScreenCapturePreview screenCapturePreview_;

    // Ranking tranches 15/17/20/22/23 real-hit batch (2026-10-03) - see the
    // public accessors above for each field's own citation.
    std::unordered_map<int, int> tutorialLockCounts_;  // Sec52.3, by table index
    OpenValue<bool> radioNewsbreakActive_{"radio_newsbreak_clear global byte", "spec-lua-api-behaviour.md Sec51.3"};
    std::unordered_set<std::string> groupsCreated_;    // Sec53.3
    std::array<bool, 10> dlc2ClapboardFlags_{};        // Sec55.5, zero-fill (file-default)
    int32_t dlc2ClapboardCount_ = -1;                  // Sec55.5, CONFIRMED file-default -1
    int dlc2ClapboardsGetNegativeIndexGuardCount_ = 0; // HOST-SAFETY, test-observable only
    std::string cutscenePlayDestination1_;             // Sec57.2, "" = none/dropped
    std::string cutscenePlayDestination2_;
    bool cutscenePlayInProgress_ = false;              // host bookkeeping stand-in, not a direct spec field
};

// Sets the initial values Team A's specs confirm (src/lua_spec_initial_state.cpp).
// Host's constructor calls it once; slots it does not set stay OPEN.
void applySpecInitialState(EngineState& es);

} // namespace sr3luahost
