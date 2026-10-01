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

#include <cstdint>
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
    // int64 of arg 3 via 0x00ea2596. Its rounding mode was labelled
    // banker's rounding (CONFIRMED) in Sec3's primitive note, but the
    // 2026-09-30 consistency review found Sec2/Sec3/Sec3.9 disagree and marked
    // it OPEN (Sec4.1). Converted with roundToIntOpenMode()
    // (lua_spec_confirmed_stubs.cpp), this project's CHOSEN round-half-to-even.
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
    // set_ignore_ai_flag (Sec3.4): the character's "ignore AI" flag.
    // Real read location per spec: bit 1 of the byte at object +0x2bc.
    // Default false ("not ignoring AI") - this project's own chosen
    // default for "alive, ordinary, not yet specially flagged."
    OpenValue<bool> ignoreAI{"character ignore-AI flag (+0x2bc)", "spec-lua-api-behaviour.md Sec3.4"};

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

    // get_max_hit_points (Sec7.12) / set_current_hit_points (Sec7.31):
    // CONFIRMED, cross-checked to be the SAME integer field at object
    // +0x1cac ("stored as a plain integer, converted to float on read" -
    // Sec7.12's own text). Default 100 - this project's OWN chosen
    // "sensible full-health" default (the spec states the field's real
    // SHAPE, never a real default value for an unspawned character), kept
    // as a plain int32 to honor the confirmed "stored as a plain integer"
    // detail precisely, converted to a double only at Lua-push time.
    OpenValue<int32_t> maxHitPoints{"character max hit points (+0x1cac)", "spec-lua-api-behaviour.md Sec7.12/Sec7.31"};

    // set_current_hit_points (Sec7.31): object +0x1cb8, clamped into
    // [0, maxHitPoints] on write (CONFIRMED). Default equals maxHitPoints
    // ("alive, full health" - this project's own chosen default, per this
    // task's own suggested convention).
    OpenValue<int32_t> currentHitPoints{"character current hit points (+0x1cb8)", "spec-lua-api-behaviour.md Sec7.31"};

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
    CharacterState& getOrCreateCharacter(const std::string& name);
    bool hasCharacter(const std::string& name) const;
    size_t characterCount() const { return characters_.size(); }

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
    OpenValueMap<bool>& objectResolves() { return objectResolves_; }

    // --- zscene (spec-lua-api-behaviour.md Sec26.25, Sec14.23, Sec8.21;
    // batch 2026-10-01) ------------------------------------------------------
    //
    // Scene identity: the engine keys its scene table (0x0153b294, entries
    // 0xf8 bytes, parsed from cutscene.xtbl) by the CRC-32 of the lower-cased
    // name and compares entry POINTERS for "current"/"pending". This host
    // keys everything by the lower-cased name (zsceneTableKey) - a stated
    // simplification (two names with equal CRCs would be one entry in the
    // engine and two here).
    //
    // CONFIRMED globals, all OPEN at start: no spec gives their start-up
    // values, and the per-field parse of cutscene.xtbl (which entries exist,
    // their kind) is OPEN (Sec26.25).
    //  - zsceneLoadable[key]: the name is in the table AND its entry kind
    //    (+0x8) is 1 (0x00721be0 lookup + the 0x00723d20 / 0x007232e0 test).
    //    "Missing" and "kind != 1" give the same answers in every caller,
    //    so one bool per name.
    //  - 0x0153b556 the skip_all_cutscenes byte.
    //  - 0x0153b530 current entry ("" = null), 0x0153b538 pending entry.
    //  - 0x0153b51c load state: 0 idle, 1 loading, 2 loaded (all CONFIRMED).
    //  - the teardown's cutscene guard and the current handle's class
    //    (0x00721c20), read only when zscene_prep tears a current scene down.
    OpenValueMap<bool>& zsceneLoadable() { return zsceneLoadable_; }
    OpenValue<bool>& zsceneSkipAllCutscenes() { return zsceneSkipAllCutscenes_; }
    OpenValue<std::string>& zsceneCurrent() { return zsceneCurrent_; }
    OpenValue<std::string>& zscenePending() { return zscenePending_; }
    OpenValue<int>& zsceneStateCode() { return zsceneStateCode_; }
    OpenValue<bool>& zsceneTeardownCutsceneGuard() { return zsceneTeardownCutsceneGuard_; }
    OpenValue<bool>& zsceneCurrentHandleNotLive() { return zsceneCurrentHandleNotLive_; }
    static std::string zsceneTableKey(const std::string& name);
    // zscene_prep (Sec8.21 / Sec26.25 lifecycle step 1-2, CONFIRMED): the gate
    // 0x007232e0, the stub 0x0101b530 (nothing), the teardown
    // 0x00721c20(1, 0, 0) of the current scene, then pending := entry and
    // the two per-object parameters := the zero constants. All reads happen
    // before any write, so an OPEN read refuses with no partial update.
    void zscenePrep(const std::string& name);
    // zscene_is_loaded (Sec14.23 corrected truth table, CONFIRMED). hasName
    // is true for a string or number argument. Throws OpenStateError on an
    // OPEN read; also refuses (OPEN) when the named entry is the PENDING one:
    // its promotion to current (0x00720410, only caller 0x007258a0, body
    // OPEN) is driven by the cutscene state machine this host does not run,
    // so "false until promoted" has no modelled end. Counted.
    bool zsceneIsLoaded(bool hasName, const std::string& name);
    int zscenePendingPromotionRefusals() const { return zscenePendingPromotionRefusals_; }

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
    // of the per-thread record (0x00e236f0 +4/+8; HIGH CONFIDENCE: width and
    // height) divided in double precision; < 1.5 -> true, else true only when
    // the display mode 0x0132bd80 == 2. The record's writers were not dumped
    // and the mode's writer 0x00e23000 runs from callers not dumped (its file
    // value -1 is CONFIRMED, but not what scripts see), so all three are OPEN.
    OpenValue<int32_t>& vintRecordFirst() { return vintRecordFirst_; }
    OpenValue<int32_t>& vintRecordSecond() { return vintRecordSecond_; }
    OpenValue<int32_t>& vintDisplayMode() { return vintDisplayMode_; }
    bool vintIsStdRes() const;
    // vint_get_safe_frame (0x00e1b570): shape CONFIRMED (4 numbers from the
    // integers a = +0x8, b = +0xc of (thread context +0x674)+0x14, scaled by
    // two double constants and rounded); order (c1*a, c1*b, c2*a, c2*b) HIGH
    // CONFIDENCE. The constants 0x0115ba60 / 0x0116dfc0 are OPEN, as are a, b.
    OpenValue<int32_t>& vintSafeFrameA() { return vintSafeFrameA_; }
    OpenValue<int32_t>& vintSafeFrameB() { return vintSafeFrameB_; }
    OpenValue<double>& vintSafeFrameScale1() { return vintSafeFrameScale1_; }
    OpenValue<double>& vintSafeFrameScale2() { return vintSafeFrameScale2_; }

    // --- mission_end_silently (Sec15.23) ------------------------------------

    // The global mission-flags word 0x014c848c. CONFIRMED: mission_end_silently
    // always sets bit 0x4 and sets/clears bit 0x10 to mirror its argument
    // (meaning of both OPEN). Every other bit, and the initial value, is OPEN:
    // only the bits a confirmed writer has written are known (open_state.h).
    OpenBits32& missionFlagsWord() { return missionFlagsWord_; }

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
    CoopSession coopSession_;
    int64_t nextAudioVoiceHandle_ = 1;
    std::vector<PegLoadRequest> pegLoadRequests_;

    std::unordered_map<uint32_t, VdoObject> vdoObjects_;
    uint32_t nextVdoObjectHandle_ = 1;
    OpenValue<uint32_t> currentDefaultDocHandle_{"current default vint document (0x00e0ceb0)", "spec-lua-bindings.md Sec15"};

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
    OpenValueMap<bool> objectResolves_{"named-object resolution", "spec-lua-api-behaviour.md Sec3.9/Sec10.6"};

    // --- zscene (Sec26.25/Sec14.23/Sec8.21), see the accessor doc comment above ---
    OpenValueMap<bool> zsceneLoadable_{"zscene table entry with kind 1 (0x00721be0 lookup, 0x00723d20 test)",
                                       "spec-lua-api-behaviour.md Sec26.25/Sec14.23"};
    OpenValue<bool> zsceneSkipAllCutscenes_{"0x0153b556 (skip_all_cutscenes)", "spec-lua-api-behaviour.md Sec26.25/Sec8.21"};
    OpenValue<std::string> zsceneCurrent_{"0x0153b530 (current scene entry)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<std::string> zscenePending_{"0x0153b538 (pending scene entry)", "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<int> zsceneStateCode_{"0x0153b51c (zscene load state)", "spec-lua-api-behaviour.md Sec26.25/Sec14.23"};
    OpenValue<bool> zsceneTeardownCutsceneGuard_{"zscene teardown cutscene guard (*0x0153b528 +8 == 1, 0x0153b520 in 7..13)",
                                                 "spec-lua-api-behaviour.md Sec26.25"};
    OpenValue<bool> zsceneCurrentHandleNotLive_{"current scene handle class 1 (0x00dafb60)", "spec-lua-api-behaviour.md Sec26.25"};
    int zscenePendingPromotionRefusals_ = 0;

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
    OpenValue<int32_t> vintRecordFirst_{"per-thread record (0x00e236f0) +4 (HIGH CONFIDENCE: width)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintRecordSecond_{"per-thread record (0x00e236f0) +8 (HIGH CONFIDENCE: height)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintDisplayMode_{"0x0132bd80 (display mode)", "spec-lua-api-behaviour.md Sec26.26"};
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
};

// Sets the initial values Team A's specs confirm (src/lua_spec_initial_state.cpp).
// Host's constructor calls it once; slots it does not set stay OPEN.
void applySpecInitialState(EngineState& es);

} // namespace sr3luahost
