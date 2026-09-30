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
    uint32_t bucketIndex = 0;
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
// distinct engine entity). Every field's default is this project's OWN
// explicit choice for "a name not yet seen" - stated per-field below, not
// scraped from any real data source (the spec does not state real default
// values for a fresh/unspawned object, only the runtime FIELD SHAPES).
struct CharacterState {
    // set_ignore_ai_flag (Sec3.4): the character's "ignore AI" flag.
    // Real read location per spec: bit 1 of the byte at object +0x2bc.
    // Default false ("not ignoring AI") - this project's own chosen
    // default for "alive, ordinary, not yet specially flagged."
    bool ignoreAI = false;

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
    int32_t actionOverrideId = -1;

    // Read-only-by-us gate for the conditional branch above: the
    // "in a vehicle" state-enum value is 3 per Sec3.4/Sec3.10's own cross-
    // checked text. No Lua function among this task's 12 in-scope names
    // ever WRITES this field (character_is_in_vehicle, the function that
    // would naturally pair with it, is out of scope) - exposed only so a
    // test can set up the "newly enabling ignore-AI while in a vehicle"
    // condition directly. Default 0 ("not in a vehicle").
    int32_t stateEnum = 0;

    // The other half of the same conditional (Sec3.4): "clears the
    // override to 0 if the character currently carries a nonzero
    // attacker/threat reference." Default 0 ("no attacker/threat
    // reference") - same "no Lua setter in scope" note as stateEnum above.
    int32_t attackerThreatRef = 0;

    // get_max_hit_points (Sec7.12) / set_current_hit_points (Sec7.31):
    // CONFIRMED, cross-checked to be the SAME integer field at object
    // +0x1cac ("stored as a plain integer, converted to float on read" -
    // Sec7.12's own text). Default 100 - this project's OWN chosen
    // "sensible full-health" default (the spec states the field's real
    // SHAPE, never a real default value for an unspawned character), kept
    // as a plain int32 to honor the confirmed "stored as a plain integer"
    // detail precisely, converted to a double only at Lua-push time.
    int32_t maxHitPoints = 100;

    // set_current_hit_points (Sec7.31): object +0x1cb8, clamped into
    // [0, maxHitPoints] on write (CONFIRMED). Default equals maxHitPoints
    // ("alive, full health" - this project's own chosen default, per this
    // task's own suggested convention).
    int32_t currentHitPoints = 100;

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

    // coop_is_active (Sec3.1): a pure query in the real engine (this
    // task's 12 functions include no setter for it - it queries a real
    // co-op session object this project does not model). Default false
    // ("not in a co-op session") - this project's own chosen default for a
    // single-process reimplementation with no real networking (see this
    // task's own "no real networking layer" instruction). setCoopActive()
    // exists only so a test can exercise the true branch directly; no Lua
    // function among these 12 ever calls it.
    bool isCoopActive() const { return coopActive_; }
    void setCoopActive(bool active) { coopActive_ = active; }

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
    static uint32_t multiply33XorHashBucket(const std::string& name, uint32_t bucketCount);

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
    uint32_t currentDefaultDocHandle() const { return currentDefaultDocHandle_; }
    void setCurrentDefaultDocHandle(uint32_t h) { currentDefaultDocHandle_ = h; }

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
    // active (non-default) mode." Pure query in the real engine AND among
    // this task's 9 in-scope names (no real setter is in scope - the
    // real writer, `store_vehicle_change_mode`, is a different, out-of-
    // scope registered name, Sec10.1's own text). Default false ("not in
    // vehicle-store UI mode" - this project's own chosen default, same
    // "single-process, nothing active yet" convention as isCoopActive()
    // above). Test-only setter, matching setCoopActive()'s own precedent.
    bool isVehicleStoreActive() const { return vehicleStoreActive_; }
    void setVehicleStoreActiveForTesting(bool active) { vehicleStoreActive_ = active; }

    // --- Completion_is_client (Sec10.2) -------------------------------------

    // Real body (CONFIRMED, disassembly, cross-checked directly against
    // Sec8.27's own already-decompiled `game_get_is_host` body reading the
    // identical two fields): "is there an active co-op session AND is
    // this machine NOT the host" - `isCoopActive() && !isHost()`. No
    // session at all -> false (NOT the literal complement of
    // game_get_is_host, which is out of this task's scope). This
    // project's single-process reimplementation has no real multi-machine
    // session, so `isHost_` defaults true (the natural single-instance
    // default - matches this project's own "no real networking layer"
    // convention already established for coopActive_/replicateStateChange
    // above). Test-only setter; no Lua function among this task's 9
    // in-scope names ever changes it.
    bool isHost() const { return isHost_; }
    void setHostForTesting(bool host) { isHost_ = host; }

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
    bool hasLocalPlayer() const { return hasLocalPlayer_; }
    void setHasLocalPlayerForTesting(bool has) { hasLocalPlayer_ = has; }
    int hudInventoryRefreshCount() const { return hudInventoryRefreshCount_; }
    void recordHudInventoryRefresh() { ++hudInventoryRefreshCount_; }

    // --- tutorial_advance (Sec10.4) -----------------------------------------

    // Real body resolves arg 1 through the SAME 210-entry, 36-byte-stride
    // tutorial-descriptor table `tutorial_start` (Sec6.19) uses, gated on
    // a bounds check AND a per-entry kind/type tag equal to the literal
    // `3` (CONFIRMED, disassembly, Sec10.4) - this project has no real
    // xtbl-loaded copy of that table anywhere in this codebase (checked:
    // no reader/table for it exists), so re-deriving the real bounds/tag
    // gate is out of scope. This project's own stated, explicit
    // simplification (not a claim of having the real table): treat any
    // non-empty string id as "resolved" (returns true), any empty/absent
    // id as "not resolved" (returns false) - honestly modeling the shape
    // of a resolve gate without fabricating its real content. On a
    // resolved id, this project's own no-op stand-in for the real
    // named event/telemetry-shaped scope the real function opens (OPEN,
    // Sec10.4 - not confirmed to even BE telemetry) is a plain per-id
    // call counter - `tutorialAdvanceCount()` reads it, defaulting 0 for
    // any id never advanced.
    int tutorialAdvanceCount(const std::string& id) const;
    void recordTutorialAdvance(const std::string& id);

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
    // convention, same as coopActive_/replicateStateChange above), so
    // `coopJoinType_` is this project's own minimal `int` stand-in,
    // default 0. Pure query among this task's 9 in-scope names (the real
    // writer is a different, out-of-scope function) - test-only setter.
    int coopJoinType() const { return coopJoinType_; }
    void setCoopJoinTypeForTesting(int code) { coopJoinType_ = code; }

    // --- zscene_is_loaded (spec-lua-api-behaviour.md Sec14.23) --------------

    // Real body: a two-tier dispatch (CONFIRMED, disassembly). Tier 1, only
    // when a name is given: a per-name cutscene state (0x00723d20) read
    // exactly `1` -> true. Tier 2 (0x00721db0): a global flag (0x0153b556 -
    // the same busy flag zscene_prep gates on, Sec8.21) set -> true; else,
    // if a name was given and resolves through the name-hash table lookup
    // (0x00d9e8b0 -> 0x00721be0) to a record other than the fixed "current"
    // sentinel, a per-record test whose sense is OPEN (read as inverted
    // relative to tier 1, not reconciled); else a global state code
    // (0x0153b51c) equal to `2` -> true.
    //
    // This project has no cutscene/scene-streaming subsystem and no copy of
    // the real scene table, so every field below is a raw, opaque stand-in
    // with no meaning assigned to its codes (the real meaning of state codes
    // 1/2 is OPEN, Sec14.23). Defaults (empty maps, flag clear, code 0) are
    // this project's own choice: the real initial values and every real
    // writer (zscene_prep's load sequence, Sec8.21, is only HIGH CONFIDENCE
    // and not traced past its first call) are not in any spec yet. All
    // setters are test-only.
    //
    // zsceneFastPathState(): the tier-1 per-name state, keyed by the exact
    // Lua string (the real per-name lookup inside 0x00723d20 is not
    // described). zsceneHasTableRecord(): whether a name resolves through
    // the tier-2 table lookup; keyed lowercased, since 0x00d9e8b0 hashes the
    // lowercased string (HANDOFF Sec3). The record's own state is not
    // stored: the only test that reads it is the OPEN branch.
    // zsceneOpenBranchHits() counts how often that OPEN branch was reached
    // (it returns false, the same falsy result the generic stub gave before)
    // so a real run can report whether the OPEN item ever decides anything.
    bool zsceneFastPathState(const std::string& name, int& stateOut) const;
    void setZsceneFastPathStateForTesting(const std::string& name, int state) { zsceneFastPathStates_[name] = state; }
    bool zsceneBusyFlag() const { return zsceneBusyFlag_; }
    void setZsceneBusyFlagForTesting(bool set) { zsceneBusyFlag_ = set; }
    int zsceneGlobalStateCode() const { return zsceneGlobalStateCode_; }
    void setZsceneGlobalStateCodeForTesting(int code) { zsceneGlobalStateCode_ = code; }
    bool zsceneHasTableRecord(const std::string& name) const;
    void addZsceneTableRecordForTesting(const std::string& name);
    int zsceneOpenBranchHits() const { return zsceneOpenBranchHits_; }
    void recordZsceneOpenBranchHit() { ++zsceneOpenBranchHits_; }

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

    // CONFIRMED: flags bit 0x1 queues the engine's "screen_fade_do" UI
    // command with the duration x 1000.0 (ms) and a target alpha (1.0 from
    // fade_out). This project has no UI command queue and does not fire the
    // Lua `screen_fade_do` hook from here (Sec26.23 gives that callback 3
    // numeric arguments, only 2 of which are known), so each request is just
    // recorded. What the fade state machine (0x0059f8c0) does with it is the
    // open Team A request (HANDOFF "Requests to Team A", item 1).
    struct ScreenFadeRequest {
        double durationMs = 0.0;
        float targetAlpha = 0.0f;
    };
    const std::vector<ScreenFadeRequest>& screenFadeRequests() const { return screenFadeRequests_; }
    void recordScreenFadeRequest(const ScreenFadeRequest& r) { screenFadeRequests_.push_back(r); }
    // CONFIRMED: flags bit 0x2 queues a separate opcode-0x53 command
    // (Sec8.13: a host-gated fade broadcast). No networking here; counted.
    int screenFadeOpcode53Count() const { return screenFadeOpcode53Count_; }
    void recordScreenFadeOpcode53() { ++screenFadeOpcode53Count_; }

    // --- mission_end_silently (Sec15.23) ------------------------------------

    // Raw stand-in for the global mission-flags word 0x014c848c. CONFIRMED:
    // mission_end_silently always sets bit 0x4 and sets/clears bit 0x10 to
    // mirror its argument (meaning of both bits OPEN). Default 0 is this
    // project's choice; the real initial value and other writers are not
    // specced.
    uint32_t missionFlagsWord() const { return missionFlagsWord_; }
    void setMissionFlagsWord(uint32_t v) { missionFlagsWord_ = v; }

private:
    std::unordered_map<std::string, CharacterState> characters_;
    bool coopActive_ = false;
    int64_t nextAudioVoiceHandle_ = 1;
    std::vector<PegLoadRequest> pegLoadRequests_;

    std::unordered_map<uint32_t, VdoObject> vdoObjects_;
    uint32_t nextVdoObjectHandle_ = 1;
    uint32_t currentDefaultDocHandle_ = 0;

    // --- fields backing the 9 functions from spec-lua-api-behaviour.md
    // Sec10.1-Sec10.9, see each field's own public-accessor doc comment
    // above for citation/reasoning. ---
    bool vehicleStoreActive_ = false;                        // Sec10.1
    bool isHost_ = true;                                      // Sec10.2
    bool hasLocalPlayer_ = true;                               // Sec10.3
    int hudInventoryRefreshCount_ = 0;                         // Sec10.3
    std::unordered_map<std::string, int> tutorialAdvanceCounts_; // Sec10.4
    std::string qteAnimationTriggerCallback_;                  // Sec10.7
    int coopJoinType_ = 0;                                      // Sec10.9

    // --- zscene_is_loaded (Sec14.23), see the accessor doc comment above ---
    std::unordered_map<std::string, int> zsceneFastPathStates_;
    bool zsceneBusyFlag_ = false;
    int zsceneGlobalStateCode_ = 0;
    std::unordered_set<std::string> zsceneTableRecords_; // lowercased names
    int zsceneOpenBranchHits_ = 0;

    // --- fade_out (Sec2.9) / mission_end_silently (Sec15.23) ---
    bool hasScreenFadeColour_ = false;
    ScreenFadeColour screenFadeColour_;
    std::vector<ScreenFadeRequest> screenFadeRequests_;
    int screenFadeOpcode53Count_ = 0;
    uint32_t missionFlagsWord_ = 0;
};

} // namespace sr3luahost
