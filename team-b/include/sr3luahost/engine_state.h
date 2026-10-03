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
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
};

// vehicle_set_invulnerable_to_player_explosives / vehicle_disable_explosion_
// and_damage_vfx / vehicle_set_special_override_never_ghost /
// vehicle_clear_all_radio_locks / vehicle_is_vtol (spec-lua-api-behaviour.md
// Sec30.5, "ranking tranche 03", 2026-10-02): one tracked vehicle-shaped
// object. Same "keyed by the plain name string it was given" simplification
// CharacterState's own top note already establishes for characters - the
// real resolution chain Sec30.5's shared preamble describes (character-
// redirect-when-driving, THEN the per-kind vehicle resolver, liveness, a
// +0x68 gate, virtual slot +0x70) is NOT reproduced; every vehicle function
// in this project's scope instead gates on the SAME shared objectResolves()
// map every other object kind already uses (Sec29's own "one shared global
// name map" finding - a vehicle name is just another name in that one map,
// not a distinctly-resolved kind).
struct VehicleState {
    // +0x1d7c/+0x1d7d force-flag bytes (Sec30.5, CONFIRMED structure, 4
    // bits total). Packed here as ONE 16-bit value inside one OpenBits32 -
    // this project's OWN choice, not a real combined engine field: the low
    // byte holds +0x1d7c's own bits 0x2/0x4 verbatim; the high byte holds
    // +0x1d7d's own bits 0x20/0x40 shifted left 8, so both real bytes fit
    // one word without colliding.
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

    // Shared vehicle fact (Sec30.5): "(+0xbf4)+0x2c equals 4 = VTOL".
    // Read by vehicle_is_vtol; no in-scope setter writes it (a test sets
    // it directly, same convention as CharacterState::stateEnum above).
    OpenValue<int32_t> vehicleClass{"vehicle class ((+0xbf4)+0x2c)", "spec-lua-api-behaviour.md Sec30.5"};
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
    // above, for VehicleState (spec-lua-api-behaviour.md Sec30.5). Exposed
    // by mutable reference so vehicle_clear_all_radio_locks can walk every
    // vehicle this host currently knows about (this project's own stand-in
    // for "every vehicle in the world object manager" - see VehicleState's
    // own doc comment) and so tests can read/write state directly.
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
    // Deliberately NOT modeled, all per this task's own established "deep
    // multi-step dispatch chain this project has no real data table for"
    // convention (see the Sec27/Sec28 batch's own file-header note): the
    // real per-type property-descriptor table (Sec9.2's own FUN_00e290c0
    // registration mechanism is not implemented anywhere in this project -
    // no real catalog of property names/type-size codes exists to resolve a
    // name against a concrete VDO subclass), the tween start_value/end_value
    // redirect (Sec20.1's own "confirmed, concrete real-game quirk" -
    // requires knowing a property's real type/size code from that same
    // missing table), and the callback-claim mechanism (Sec20.1's own
    // vint_callback_lua pool interaction, case 8 - same reason). Each would
    // require guessing which properties are of which type, which no spec-
    // *.md file or real game data this project parses provides.
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

    std::unordered_map<uint32_t, VdoObject> vdoObjects_;
    uint32_t nextVdoObjectHandle_ = 1;
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
};

// Sets the initial values Team A's specs confirm (src/lua_spec_initial_state.cpp).
// Host's constructor calls it once; slots it does not set stay OPEN.
void applySpecInitialState(EngineState& es);

} // namespace sr3luahost
