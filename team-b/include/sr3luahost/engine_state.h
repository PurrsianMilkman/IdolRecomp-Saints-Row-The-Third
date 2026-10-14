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
// vint_object_find (spec-lua-bindings.md Sec13.7/Sec15, VdoObject in vint_state.h)
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
// are here (VdoObject, in vint_state.h, is the separate vint_object_find
// state) - no generic "entity shape" was guessed. Each field cites the spec
// (sub)section it came from and its own confidence tier (CONFIRMED vs HIGH
// CONFIDENCE) so a later pass never silently treats a HIGH CONFIDENCE
// reading as settled.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// sr3save::nameHash(): the engine's general lower-cased, seed-0, no-final-
// XOR reflected CRC-32 name hash (0x00d9e740 - spec-save-format.md
// Sec9.8/Sec6.1, table 0x01320DA0, CONFIRMED - empirical via spec-lua-api-
// behaviour.md Sec5.1's 7/7 real I/O vectors for the sibling raw-byte entry
// point 0x00d9e790 sharing the same table/algorithm). Reused by citation,
// not reimplemented - see spec-lua-bindings.md Sec15's own text on
// vint_object_find, which hashes its `name` argument via this exact
// routine before the parent/document-scoped lookup below. Header-only, no
// new link dependency (include/ is one shared tree - see this project's
// own established convention, every library already does this).
#include "sr3cutscene/camera_script.h"
#include "sr3luahost/lua_c_api.h"
#include "sr3luahost/open_state.h"
#include "sr3player/control_state.h"
#include "sr3player/input.h"
#include "sr3save/save_crc.h"

// Per-domain engine-state structs, split out of this header 2026-10-08 as a
// zero-behavior-change structural move (every struct, field and doc comment
// moved verbatim). Each domain owns its own file so a merge can only ever
// land a new field in that domain's file: with every struct in this one
// header, merges kept landing one struct's new fields inside a DIFFERENT,
// adjacent struct whose closing brace sat next to it. Add new fields to the
// struct's own header below, never here. They are all included ahead of
// EngineState, so a struct an EngineState doc comment calls "above" is
// still declared before EngineState - in the header listed here for it.
#include "sr3luahost/world_position.h"            // WorldPosition
#include "sr3luahost/character_state.h"           // CharacterState, EnemyTargetRecord, MinimapIconRecord, ObjectIndicatorRecord
#include "sr3luahost/vehicle_state.h"             // VehicleState
#include "sr3luahost/placed_object_state.h"       // PlacedObjectState
#include "sr3luahost/hud_state.h"                 // kHudBarSlotCount/kHudTimerSlotCount, HudBarSlot, HudTimerSlot, FakeNotorietySlot, HudHardeningCounters, HudDisplayStatePool, QteSlotEntry
#include "sr3luahost/pcr_customization_state.h"   // PcrCustomizationItem, PcrCustomizationState
#include "sr3luahost/vint_state.h"                // VdoObject, LoadedVintDocument, VintTaggedValue, VintStoredProperty, VintPropertyStats, VintDataItem, VintDataResponderRecord
#include "sr3luahost/zone_state.h"                // ZoneMember, PauseMapHoveredZone
#include "sr3luahost/zone_object_resolver.h"       // ZoneObjectResolver - real name<->sr3zone::ObjectRecord link (2026-10-08)
#include "sr3luahost/peg_load_state.h"            // PegLoadFilenameEntry, PegLoadRequest
#include "sr3luahost/misc_state.h"                // ObjectiveTextSlot, TeleportRequest, ConversationPlay
#include "sr3luahost/save_game_state.h"           // CollectedCollectible, SaveGameState (spec-save-format.md Sec15 wiring, 2026-10-08)

// sr3tables_progression::RespectLevel: the real respect_levels.xtbl per-level
// record type (spec-tables-progression.md Sec5.2), reused directly below as
// the live runtime's level-up cost/unlock lookup (Sec5.4: "0x0060eb60 is not
// a separate algorithm ... it is the exact same accessor" as the save-shaped
// path's own cost table) - NOT a second, parallel cost struct. Only the
// struct's DEFINITION is used here (a plain data record: Always<int32_t>,
// optional<string>, vector<string> - all header-only, sr3xtbl/xtbl.h pulls
// in no out-of-line symbol this header calls), so this remains header-only:
// sr3luahost does not gain a new link dependency on sr3tables_progression,
// and this subsystem never reads a real respect_levels.xtbl file (reading
// real xtbl content is a separate concern, same footing as this task's own
// collectibles.xtbl Respect_Award fields being out of scope) - tests supply
// the per-level records directly via respectLevelTable().
#include "sr3tables_progression/tables_world.h"   // sr3tables_progression::RespectLevel

// Same header-only footing as tables_world.h just above: only the
// Unlockable/UnlockableAutoUnlock struct/enum DEFINITIONS are used here
// (plain data: std::string, enum, optional, vector - no out-of-line symbol
// this header calls), so grantNewGameAutoUnlocks() below adds no new link
// dependency on sr3tables_progression - callers that want to parse a real
// unlockables.xtbl still go through ParseUnlockablesTable() themselves and
// hand the result in.
#include "sr3tables_progression/tables_core.h"    // sr3tables_progression::Unlockable, UnlockableAutoUnlock

namespace sr3vintdoc {
struct Document; // include/sr3vintdoc/vint_doc.h - EngineState::loadVintDocument
}

namespace sr3luahost {

// Forward-declared only (full definition: bare_globals.h) so this header
// does not cross-include it - pushGamePauseReason()/popGamePauseReason()
// below are implemented out-of-line in src/lua_menu_runtime.cpp, which
// does include it.
class BareThreadTable;

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

    // The generic damage-apply sink's own AUTHORITATIVE-branch math
    // (0x0096fa20, spec-player-runtime.md Sec15.3a, wave 6 2026-10-09):
    // resistance scale * difficulty scalar on the caller's base `amount`,
    // health -= scaled amount (clamped [0, maxHitPoints]), the +0x1584
    // last-damage-magnitude update, and - at the zero-HP threshold - the
    // death transition and ragdoll/finalize hand-off. Assumes the CALLER
    // has already done target resolution and the two outer gates
    // (CharacterState::damageImmuneDescriptorBit33/controlLockGatePasses) -
    // this method is only the inner, already-authoritative core, factored
    // out so stub_character_damage (src/lua_spec_confirmed_stubs_b15_
    // wave2_core_dispatchers.cpp) and a future non-Lua caller (e.g. an
    // IWeaponDamageSink adapter, once feature/hitscan-fire-dispatch's own
    // weapon_fire.h merges - see include/sr3luahost/character_damage_sink.h)
    // share one real implementation instead of two copies. `sourceName`
    // empty = no resolved instigator (CHOSEN neutral: no resistance scale,
    // no difficulty scalar - see CharacterState::instigatorDamageScalePercent's
    // own doc comment). See that same field's doc comment, lethalHitKillEligible's,
    // and ragdollActivationEligible's for the full per-step citation this
    // method's body leans on.
    void applyCharacterDamage(const std::string& targetName, const std::string& sourceName, float amount);

    // A GUARANTEED lethal hit (spec-lua-api-behaviour.md Sec3.15's own
    // character_kill shape - "a fixed -1.0 lethal constant... guaranteed
    // death via the normal pipeline, not a possibly-survivable hit" -
    // re-confirmed as the SAME recursive "force-kill" shape spec-player-
    // runtime.md Sec15.3a's death transition 0x00970e70 builds when reached
    // with a null instigator). Unlike applyCharacterDamage() above, this
    // does NOT require maxHitPoints/currentHitPoints to already be known
    // (character_kill's own existing stub, stub_character_kill b30, never
    // depended on health being known either - it is purely a lifeState
    // transition) - if health IS known, it is also force-snapped to 0 for
    // consistency with get_max_hit_points/get_current_hit_points-style
    // readers; if not, health stays OPEN/untouched, exactly as character_
    // kill's own existing behaviour already is. Shares the SAME death-
    // transition/ragdoll-or-finalize tail as applyCharacterDamage() (see
    // finalizeLethalCharacter(), private below) - the real engine's own
    // "-1.0 recurses into 0x0096fa20 again" account, not a parallel,
    // divergent kill path. This is the method include/sr3luahost/
    // character_damage_sink.h's adapter calls (its own doc comment covers
    // why that header exists and why it is not sr3weapons::
    // IWeaponDamageSink itself).
    void applyGuaranteedLethalHit(const std::string& targetName, const std::string& sourceName);

    // Test-only (Sec34.4): marks `name` as bound to a script NPC whose own
    // placement-record overrides (`script_npc_hp` / `script_npc_flags`
    // containing `"ignore_ai"`) are zone data. UPDATE 2026-10-08: the
    // `.czn_pc` zone-placement interior itself is no longer held (lifted
    // 2026-10-07) and spec-zone-data-format.md Sec11.7 now gives real,
    // population-wide proof that both properties are genuinely authored
    // (script_npc_hp 1,028 times) - but this project still has no by-name
    // resolver that would tell WHICH of a given mission's Lua-visible
    // characters is backed by WHICH specific zone record
    // (spec-lua-api-behaviour.md Sec34.4's own "Practical reading": "cannot
    // be determined from code alone"). So this method still represents
    // ONLY "the override question is live for this character" by
    // forgetting the CONFIRMED defaults back to OPEN; it never fabricates
    // what an override would actually contain on its own. No Lua-visible
    // function in this host's scope ever establishes this binding for a
    // real name (Sec34.4's own census: commando_spawn's builder
    // `0x00a33b80` and the script-NPC spawn routine `0x00a34f00` never set
    // either property) - populated only via this entry point, for a test
    // demonstrating the refusal, or via applyScriptNpcHitPointsForTesting()
    // below when a caller DOES already know the override value itself.
    void markScriptNpcBoundForTesting(const std::string& name);

    // UPDATE 2026-10-08 (zone-data fast-follow): companion to
    // markScriptNpcBoundForTesting() above, for the one further case that
    // IS now fully CONFIRMED once a caller happens to already know a
    // specific character's own script_npc_hp value (e.g. via
    // sr3zone::ScriptNpcHp() on the matching ObjectRecord, once some future
    // task builds the still-missing name<->record resolver noted on
    // scriptNpcHp's own doc comment above) - marks `name` scriptNpcBound
    // (same as markScriptNpcBoundForTesting()) and records the supplied
    // value so drawOrGetMaxHitPoints() can apply Sec34.2's own CONFIRMED
    // rule on first reference: >= 1 wins verbatim over the table; < 1 (the
    // script NPC constructor's own -1 "use the table" default) falls
    // through to the ordinary table draw. Does not itself draw
    // maxHitPoints - same "forget the defaults, draw lazily" shape as
    // markScriptNpcBoundForTesting(). No Lua-visible function in this
    // host's scope calls this today (same unmodeled-resolver gap) -
    // test-only until a real producer exists.
    void applyScriptNpcHitPointsForTesting(const std::string& name, int32_t scriptNpcHp);

    // 2026-10-07 (Sec60.4's closure of the max-hit-points random-per-spawn
    // model; probe_blockers_ranked_20261007.md item 2, mission `m18`): the
    // REAL set of Hit_Points values spawn_info_ranks.xtbl ships, installed
    // from the real archive cache by lua_host_run.cpp
    // (installContinuousExplosionTableFromCache's own sibling for this
    // table - see that file's comment for the archive precedence question).
    // Empty (the default - every synthetic unit test, and any run that
    // never installs it) means drawOrGetMaxHitPoints() below falls back to
    // the single already-established CHOSEN diagnostic constant, 100.
    void installSpawnRankHitPoints(std::vector<int32_t> values) { spawnRankHitPoints_ = std::move(values); }
    size_t spawnRankHitPointsCount() const { return spawnRankHitPoints_.size(); }

    // NEW 2026-10-10 (vehicle-class xtbl wiring task): the REAL class-by-
    // type-name table sr3vehicleinfo::ParseAllVehicles already computes
    // correctly (include/sr3vehicleinfo/vehicle_entry.h,
    // VehicleTypeUnion::vehicleClass - 0 Automobile/1 Motorcycle/
    // 2 Airplane/3 Helicopter/4 Vtol/5 Watercraft, the SAME numeric space
    // VehicleState::flyingType's own CONFIRMED values use, Sec30.5/Sec33.1),
    // keyed by sr3luahost::AsciiLowerFold(VehicleEntry::name) - the exact
    // fold convention zone_object_resolver.h already established for this
    // project's one shared case-insensitive-name lookup. Installed by a
    // caller (tools/lua_host_run.cpp's real-cache install step,
    // installVehicleTypeClassTableFromCache) from every real
    // `<name>_veh.xtbl` found in misc_tables.vpp_pc + dlc1/2/3.vpp_pc
    // (spec-vehicle-data.md Sec7.9: "123 total"). Stored as a plain
    // unordered_map<string,int32_t> rather than sr3vehicleinfo's own
    // VehicleClass enum so this header does not need to include
    // sr3vehicleinfo/vehicle_type.h just for a one-field install function -
    // the caller casts VehicleClass to int32_t when building the map it
    // passes in.
    //
    // loadZoneObjectsSection() below is the ONE consumer: a zone-placed
    // `Script_Vehicle` record's own `script_vehicle_type_name` property
    // (sr3zone::ScriptVehicleTypeName(), include/sr3zone/zone_objects.h) is
    // looked up here (case-folded) to populate that instance's
    // VehicleState::flyingType and, when the resolved class is 3 (heli) or
    // 4 (Vtol), HelicopterFlyTo::qualifies - replacing the single hardcoded
    // "m05_heli_pierce" name special case getOrCreateVehicle() used to
    // carry (removed this same task; see that method's own history in
    // src/lua_engine_state.cpp's git log) with the real, general,
    // data-driven mechanism that special case's own doc comment said this
    // project was missing. Empty (the default - every synthetic unit test,
    // and any run that never installs it) means loadZoneObjectsSection()
    // can still resolve a script_vehicle record's own NAME (objectResolves())
    // but never its type's class - flyingType/qualifies stay OPEN exactly as
    // before this task, matching this project's standing "empty install = no
    // behavior change" convention (installSpawnRankHitPoints/
    // installZsceneTable's own doc comments).
    void installVehicleTypeClassTable(std::unordered_map<std::string, int32_t> classByLowerTypeName) {
        vehicleTypeClassByLowerName_ = std::move(classByLowerTypeName);
    }
    size_t vehicleTypeClassTableSize() const { return vehicleTypeClassByLowerName_.size(); }

    // get_max_hit_points (Sec7.12) / set_current_hit_points (Sec7.31): see
    // CharacterState::maxHitPoints's own "UPDATE 2026-10-07" doc comment for
    // the full rationale. For an ORDINARY character (not scriptNpcBound)
    // with maxHitPoints not yet known, draws ONE value (uniformly, from
    // installSpawnRankHitPoints()'s real set when installed, else the
    // CHOSEN 100 fallback), seeded deterministically from `name` alone
    // (splitmix64 - same host-substitute algorithm as
    // BareRandomSource::hostStep, bare_globals.h, but its own independent
    // instance, not shared RNG state) so repeated calls for the SAME name
    // draw the SAME value (matching "random per spawn, stable afterward" -
    // not re-rolled per query), then calls set() to close OPEN for that name
    // permanently. A scriptNpcBound character is left untouched - its own
    // `get()` below still throws OpenStateError for it, CONFIRMED-correct
    // per Sec34.2/Sec34.4's own zone-data hold. Does NOT model which
    // preset/rank-type a given name "should" draw from (a stated
    // simplification: uniform over the whole real value set, boss-tier
    // outliers like Killbane's 20000 included, not real per-character
    // fidelity).
    int32_t drawOrGetMaxHitPoints(const std::string& name);
    // Current HP (+0x1cb8): Sec34.2 "starts equal to the table value (step 2 of Sec34.1)" - the same value
    // maxHitPoints takes at spawn. A character whose current HP was never set takes its (drawn-or-known)
    // maximum. Stays OPEN (throws) exactly where drawOrGetMaxHitPoints() does (script-NPC override unknown).
    int32_t drawOrGetCurrentHitPoints(const std::string& name);

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
    // Same convention as vehicles() below - exposed by mutable reference so
    // the real seat-pick validator's step 6/7 occupant-conflict scan
    // (spec-player-runtime.md Sec13.4, computeVehicleEnterSubmitAccepted() /
    // attemptGroupInstantEnter() in lua_spec_confirmed_stubs_b15_wave2_core_
    // dispatchers.cpp) and vehicle_enter_group_check_done's per-member
    // completion walk (spec-lua-api-behaviour.md Sec38.1) can walk every
    // character this host currently knows about, reusing the one existing
    // map rather than a parallel occupancy registry this project does not
    // have (added 2026-10-09).
    std::unordered_map<std::string, CharacterState>& characters() { return characters_; }

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

    // --- Mission-host Phase 2: live position bridge + name->live-object
    // resolver (results/design_mission_host_in_engine_loop_20261009.md
    // §3(c), "a bridge it does not have today... a new, currently-
    // nonexistent name-to-live-object resolver") ----------------------------
    //
    // UPDATED 2026-10-09, post-merge of Team A's spec-zone-data-format.md
    // §11.10 ("Named-object resolution, end to end") + spec-lua-api-
    // behaviour.md §4.13's corrected resolver-family paragraph: the REAL
    // engine has exactly ONE global name->handle map (registry singleton
    // `0x02442750`, `+0x2660`), and - unlike this project's own objectResolves()
    // below - that map is itself inherently LIVE: an entry exists in it if
    // and only if some real producer (zone placement, vehicle cover-node
    // batches, an in-memory engine record including a group member, or
    // co-op-replication replay - §11.10 item 2's own closed 4-producer
    // list) has actually registered a live handle for that name via
    // `0x00457650`, called from registry base-vtable `+0x2c`. Resolving a
    // name in the real engine and finding it LIVE are therefore the SAME
    // event, not two separate facts the way this project's headless-testing
    // objectResolves() treats them (a flat, hand-seeded boolean that answers
    // the SAME way whether this host is running headless or hosting a real
    // runPlaytest() session, because nothing ever populated it FROM a live
    // session in the first place). This resolver is this project's first
    // attempt at modelling that real "resolves because it's live" fact
    // directly, rather than the pre-existing hand-seeded stand-in.
    //
    // §11.10 item 3 / §4.13's own corrected resolver-family paragraph also
    // confirms a SECOND, separate step this resolver deliberately does NOT
    // model: once a name resolves to a handle, a per-native "kind" bit-test
    // against that handle's own row in the real class-descriptor table
    // `0x02cc9900` (5 confirmed resolver-family members, each testing one
    // bit/offset) decides whether THIS PARTICULAR native accepts that kind
    // of object. §4.13's own text is explicit that even Team A does not yet
    // know the plain-English meaning of any of those bits, and 2 of the
    // family's own bit/offsets are still unpinned - so there is no real
    // mechanism here for Team B to faithfully reproduce yet. This resolver's
    // own kCharacter/kVehicle split below is NOT an attempt at that - it
    // simply mirrors which of this project's two PRE-EXISTING, Phase-2-
    // independent storage maps (characters_/vehicles_ above) a caller wrote
    // a live position into, the same character-vs-vehicle split
    // get_dist's own doc comment already admits this project has no real
    // way to distinguish from a bare name alone. Treat kCharacter/kVehicle
    // as "which existing map this was written through," never as a claim
    // about a resolved handle's real `0x02cc9900` class row.
    //
    // §11.10 item 4 independently strengthens, rather than changes, this
    // resolver's one real production use (the bridge call for "#PLAYER1#"
    // in tools/sr3_viewer.cpp's own runPlaytest()): `"#PLAYER1#"` is one of
    // exactly 8 closed, special-cased sentinels that the REAL generic
    // resolver (`0x00a280c0`) short-circuits via a literal-string compare,
    // resolving straight to "the current player object" - it never touches
    // the name map at all. So in the real engine "#PLAYER1#" is ALWAYS
    // resolvable whenever a local player exists, by construction - exactly
    // the one case this project's own minimal playtest can actually offer a
    // live value for, and exactly why it is the one name bridged today (see
    // that call site's own doc comment for the full "no other name is
    // bridged" reasoning, unchanged by this spec update).
    //
    // CHOSEN shape (unchanged by the above - this just grounds it): a plain
    // name->kind map, written ONLY by the two bridge*Position() setters
    // below (never by a mission script, and never defaulted true for any
    // name) - so "is X live" can only become anything but kNone because a
    // real per-frame caller (runPlaytest()) actually wrote a real position
    // for it THIS session. A name this map has never seen is kNone ("not
    // live") - a normal, honest, reportable result, not an error and not
    // the same fact as objectResolves() being false (a name can be a real
    // resolved mission-fiction object per THIS project's own hand-seeded
    // stand-in and still not be live in a minimal playtest that only ever
    // tracks the local player - concrete proof: applySpecInitialState()
    // pre-seeds BOTH "#PLAYER1#" and "#PLAYER2#" true in objectResolves(),
    // Sec29.4, but this project's single-player-only scope means
    // "#PLAYER2#" is NEVER live in any session this project can construct).
    enum class LiveObjectKind { kNone, kCharacter, kVehicle };
    LiveObjectKind liveObjectKind(const std::string& name) const {
        auto it = liveObjectKinds_.find(name);
        return it == liveObjectKinds_.end() ? LiveObjectKind::kNone : it->second;
    }
    bool isLiveObject(const std::string& name) const { return liveObjectKind(name) != LiveObjectKind::kNone; }

    // Writes a REAL, currently-simulated position into the SAME
    // CharacterState::worldPosition / VehicleState::worldPosition field
    // get_dist and the other position-shaped natives already read
    // (character_state.h/vehicle_state.h's own doc comments on
    // `worldPosition`) - this bridges into existing storage, it does not
    // create a second one. Marks `name` live (kCharacter/kVehicle) for the
    // resolver above. Call once per rendered frame, before the mission's
    // own per-frame scheduler pass runs, so a coroutine resumed this frame
    // observes THIS frame's position, never a stale one.
    void bridgeLiveCharacterPosition(const std::string& name, const WorldPosition& pos) {
        getOrCreateCharacter(name).worldPosition.set(pos);
        liveObjectKinds_[name] = LiveObjectKind::kCharacter;
    }
    void bridgeLiveVehiclePosition(const std::string& name, const WorldPosition& pos) {
        getOrCreateVehicle(name).worldPosition.set(pos);
        liveObjectKinds_[name] = LiveObjectKind::kVehicle;
    }
    // Test-only: resets the resolver's own "is it live" answer for every
    // name (e.g. to model "before the live loop's first frame ever ran")
    // WITHOUT touching any worldPosition already written - matches this
    // project's existing OpenValue "sticky until a new .set()/.forget()"
    // convention elsewhere; this map is a separate liveness fact layered on
    // top of that storage, not a replacement for it.
    void clearLiveObjectsForTesting() { liveObjectKinds_.clear(); }

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

    // --- Player runtime U0/U1 (spec-player-runtime.md Sec1/Sec2), batch
    // 2026-10-03 ----------------------------------------------------------
    //
    // The input query contract (control context 0x01411b18, binding
    // tables, every query) and the control-state machine (state table
    // 0x012e6bb8, request stack 0x012e6c58). Both are pure machinery in
    // sr3player (include/sr3player/*.h). The input system runs on the
    // headless NullDeviceSource: this host has no window, so per
    // spec-player-runtime.md Sec1.1 the context freezes in its cleared state
    // and every query reads "nothing" - the spec's own rule for an inactive
    // window, not a stand-in. Nothing in this host runs the per-frame latch
    // or the dispatcher yet (their callers are the world update, Sec1.1/
    // Sec2.3, not built here); the Lua natives below push/pop the real stack
    // and call the real "clear latched".
    sr3player::InputSystem& inputSystem() { return inputSystem_; }
    sr3player::ControlStateMachine& controlStateMachine() { return controlStateMachine_; }
    // player_controls_disable's lock handle, the global 0x0263ae3c
    // (Sec7.22/Sec7.7; spec-player-runtime.md Sec2.6 push site 0x009e2ef2).
    // OPEN: no spec gives its static value. (Every keeper tests its handle
    // for 0 and zeroes it after popping, Sec2.6, which suggests a zero
    // static - an inference, not a stated value, so not applied.)
    OpenValue<uint32_t>& playerControlsLockHandle() { return playerControlsLockHandle_; }

    EngineState();
    // The input system and the control-state machine are wired to each
    // other through callbacks that capture `this` (rows 0/1 enter -> clear
    // latched; the latch's step-5 state getter), so an EngineState is not
    // copyable or movable.
    EngineState(const EngineState&) = delete;
    EngineState& operator=(const EngineState&) = delete;

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

    // 0x00dab330 (spec-texture-format.md Sec8.2's "multiply-33/XOR"
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
    // The same lookup for a non-test reader of the live tree (2026-10-03:
    // the UI draw path, sr3_viewer vint-doc, walks parent/first-child/
    // next-sibling links and reads type names). nullptr = no such handle.
    const VdoObject* vdoObject(uint32_t handle) const { return vdoObjectForTesting(handle); }

    // 0x00e0ceb0's own stand-in ("a 'current default document' global
    // lookup") - default 0 (this project's own "no default document yet"
    // sentinel, VdoObject's own doc comment). Test-only setter; no Lua
    // function among this task's 13 in-scope names ever changes it.
    OpenValue<uint32_t>& currentDefaultDocHandle() { return currentDefaultDocHandle_; }

    // Implements vint_object_find's own CONFIRMED mechanism (Sec15): hash
    // `name` (0x00d9e740 - this project's own default seed 0, since the
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
    //    documents through a different resolver, 0x00e1f330, Sec18.2/
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
    // UPDATED 2026-10-03 (property descriptors + spec-vint-render.md):
    //  * every element-family object (VdoObject::typeName is-an-element) is
    //    first seeded with the element initializer's defaults
    //    (spec-vint-render.md Sec8, "a host should seed every element with
    //    these before applying the loaded document's own property records") -
    //    except unscaled_size for bitmap/gradient/text/bitmap_circle, which
    //    their own size paths set (OPEN here, Sec5.4);
    //  * each file record is then matched against the object's type's
    //    descriptor chain: no row -> consumed and skipped (CONFIRMED,
    //    spec-vint-doc-format.md Sec5); a fail-stub row (screen_nw/se,
    //    unscaled_size, spec-vint-render.md Sec7.3) -> skipped too (the
    //    loader writes through the same setter); otherwise stored typed when
    //    the file tag's shape is the row's code (tag n for code n, a tag-4
    //    string for codes 9/0xa/0xb, a tag-1/2 integer or a resolvable tag-4
    //    enumerator name for 0xd - the latter the loader's own 0x00e307c0
    //    conversion, vint-doc Sec5 HIGH CONFIDENCE / spec-lua-bindings.md
    //    Sec9.2a), raw otherwise. An object of a type name none of the 13
    //    keeps every record raw (the pre-descriptor behaviour).
    // Still NOT modelled: the auto_offset applier (spec-vint-render.md
    // Sec5.4 - when it fires after a load is not specified), the instancing
    // of a `document`-typed record's referenced widget document (its
    // `document_name` names another .vint_doc - not specified anywhere).
    // LoadedVintDocument::propertyCount counts applied FILE records only
    // (not seeded defaults, not skipped records).
    //
    // UPDATED 2026-10-03 (UI draw unit U3, spec-vint-render.md Sec12.1):
    // `imageSize`, when given, is consulted for every `image` record (tag-4
    // string) of a `bitmap`-family object (bitmap and gradient - gradient's
    // parent type is bitmap, spec-lua-bindings.md Sec9.1) AT THAT RECORD'S
    // POSITION in the applied order, i.e. exactly where the real loader runs
    // the image setter. It returns the image's own size (w, h) - the
    // bitmap-resource pool lookup itself (sheet sub-image vs standalone
    // image, Sec12.1) is the caller's, since this host has no resource pool.
    // On a hit the CONFIRMED Sec12.1 side effects run, in this order:
    //  1. if custom_source_coords is clear, source_nw := (0,0) and
    //     source_se := (w,h). CHOSEN: an object with no stored
    //     custom_source_coords counts as clear (bitmap's constructor default
    //     for it is not given in clean text; no shipped HUD document stores
    //     it - 17 of 1,820 shipped bitmap records do, all `true`);
    //  2. unscaled_size := (w,h) (re-seeded from the image's own size);
    //  3. the auto_offset applier (Sec5.4, vtable +0xc4 - "re-fetched on
    //     every size-changing path of bitmap") with the auto_offset value
    //     stored at that moment.
    // Later records of the same block then overwrite whatever they name,
    // exactly as the real setters would. On a miss nothing happens (the
    // real pool lookup returned null - no size change) and unscaled_size
    // stays OPEN for vintQueryBox, as before. NOT modelled: image_crc
    // records (resolving a CRC to an image needs the pool's CRC index) and
    // whether auto_offset's OWN setter re-runs the applier (not given in
    // clean text - a document storing auto_offset after image with no later
    // `offset` record keeps the offset it had). Without `imageSize` the
    // loader behaves exactly as before (every existing caller).
    using VintImageSizeResolver = std::function<std::optional<std::pair<double, double>>(const std::string& imageName)>;
    uint32_t loadVintDocument(const std::string& docName, const sr3vintdoc::Document& doc,
                              const std::string& activeResolution = std::string(),
                              const VintImageSizeResolver& imageSize = VintImageSizeResolver());
    // nullptr for a handle loadVintDocument never issued.
    const LoadedVintDocument* loadedVintDocument(uint32_t docHandle) const;
    size_t loadedVintDocumentCount() const { return loadedVintDocuments_.size(); }

    // --- vint_get_time_index (Sec19.1) --------------------------------------

    // 0x00e0ceb0's own stand-in reused a third time (Sec19.1's own text:
    // "the third sighting in this document of the identical +0x14-field read
    // feeding a 'current document' resolution") - this project's host has no
    // document registry distinct from the resolved handle number itself, so
    // a genuine "0x00e1f330 failed to resolve" cannot be told apart from
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
    // One per-(resolved VDO object, property-name-hash) store
    // (VintStoredProperty), written by vint_set_property, the document
    // loader and the element defaults, read by vint_get_property. Names are
    // hashed the real way (CONFIRMED, Sec9.2/Sec20.1: the engine's own
    // lower-cased string hash, sr3save::nameHash).
    //
    // TYPED objects (2026-10-03: VdoObject::typeName is one of the 13
    // registered types - every document-loaded object, and
    // registerTypedVdoObjectForTesting) go through the real per-type
    // property-descriptor tables (spec-lua-bindings.md Sec9.1/Sec9.2/Sec9.2a,
    // include/sr3luahost/vint_property_types.h - read that header for which
    // rows have a code in clean text and which are OPEN):
    //  * the name is looked up in the object's own type's table, then up the
    //    parent chain (CONFIRMED Sec9.1); no row anywhere -> set writes
    //    nothing, get pushes zero Lua values (CONFIRMED Sec20.1/Sec20.2);
    //  * a row with a known code: set parses the trailing Lua values by that
    //    code - exactly vintCodeArity(code) of them are consumed (extra
    //    trailing values are ignored: the switch builds ONE value record of
    //    the code's shape, Sec20.1), numbers for 1/2/3/6/7 (floats rounded to
    //    32-bit, the field width spec-vint-render.md Sec1 gives; 1/2 must be
    //    whole numbers, 2 non-negative), a string for 4/8/9/0xa/0xb, a
    //    boolean for 5; 0xd takes a whole number or an enumerator name
    //    resolved through Sec9.2a's tables. Lua values that do not fit the
    //    code (wrong count, wrong Lua type, a fractional integer) are an
    //    OpenStateError - the real switch's behaviour on such input is not
    //    specified, so this host refuses rather than coerce;
    //  * a row whose code is OPEN in clean text: CHOSEN pass-through - the
    //    Lua values are stored and echoed exactly as given (the
    //    pre-descriptor behaviour), never converted by a guessed type;
    //  * a 0xd name that the clean tables cannot resolve (an unknown name, or
    //    any name tween.algorithm lists only as "8 families x {in, out,
    //    in-out}"): stored raw (CHOSEN - mirrors the document loader's own
    //    string fallback on a lookup miss, vint-doc Sec5, HIGH CONFIDENCE);
    //  * TWEEN start_value/end_value (CONFIRMED Sec20.1, as CORRECTED
    //    2026-10-03): the gate is exact - object type tween, row code 0xe,
    //    name start_value/end_value case-insensitively. The values are then
    //    parsed with the code of the property the tween drives (the tween's
    //    own stored target_handle -> object, target_property -> that object's
    //    descriptor) and the write lands on the TWEEN's own property, never
    //    on the target. When this host cannot determine that code (no
    //    target_handle/target_property stored, target not a typed object, the
    //    target row's own code OPEN) the values are stored raw (CHOSEN);
    //  * code 8 (callback name): the name is stored; the vint_callback_lua
    //    pool claim/refcount Sec20.1 describes is NOT modelled (this host has
    //    no callback pool) - deferred;
    //  * spec-vint-render.md Sec7.3 setters: screen_nw, screen_se and
    //    unscaled_size are the universal fail-stub (no write); screen_size is
    //    a real setter (see setVintScreenSize below); group/point screen_size
    //    fail; text's own screen_size setter is OPEN (layout-ready bit).
    // get, for a typed object: screen_nw/screen_se/screen_size are computed
    // on every call (spec-vint-render.md Sec7, vintQueryBox below);
    // mouse_depth reads back depth while it holds the 0x7fffffff sentinel
    // (Sec1); codes 8 and 0xd refuse (OpenStateError: what 0x00e1a420
    // pushes for a callback slot or an enum is not specified, and no shipped
    // script reads one - measured); a row with no stored value refuses
    // (OpenStateError: no spec'd default - element-family objects are seeded
    // with spec-vint-render.md Sec8's defaults, so this is only subtype rows
    // and tween/animation); otherwise the stored values are pushed.
    //
    // UNTYPED objects (registerVdoObjectForTesting, cloneVdoObject - no type
    // name) and objects whose type name is none of the 13 keep the exact
    // pre-descriptor behaviour: any name is stored raw and echoed back,
    // never-set -> zero values (CHOSEN, the test-only path keeps working).
    //
    // `setVintProperty` silently does nothing if `handle` does not resolve
    // to a registered VdoObject (CONFIRMED, Sec20.1).
    void setVintProperty(uint32_t handle, const std::string& propertyName, std::vector<VintTaggedValue> values);
    // vint_get_property's value list: nullopt = zero Lua values (bad handle,
    // name miss - CONFIRMED Sec20.2); throws OpenStateError as described
    // above.
    std::optional<std::vector<VintTaggedValue>> getVintProperty(uint32_t handle, const std::string& propertyName) const;
    // Raw store access (tests, diagnostics): nullptr when nothing is stored
    // under that name; the stored values exactly, with no type dispatch.
    const std::vector<VintTaggedValue>* findVintProperty(uint32_t handle, const std::string& propertyName) const;
    const VintStoredProperty* findVintStoredProperty(uint32_t handle, const std::string& propertyName) const;
    const VintPropertyStats& vintPropertyStats() const { return vintPropertyStats_; }

    // Test/setup-only: a VdoObject of a registered type, attached as the last
    // child of `parentHandle` (0 = none) and seeded with the element
    // defaults when it is element-family - the same shape loadVintDocument
    // builds, without a file.
    uint32_t registerTypedVdoObjectForTesting(const std::string& name, const std::string& typeName,
                                              uint32_t parentHandle, uint32_t docHandle);
    // Test/setup-only stand-in for a type-specific size path this host does
    // not model (bitmap/gradient/bitmap_circle size from their image, text
    // layout - spec-vint-render.md Sec5.4/Sec11 item 6): stores
    // unscaled_size directly (Lua cannot - its setter is the fail-stub).
    void setVintUnscaledSizeForTesting(uint32_t handle, double w, double h);

    // --- Query-time geometry (spec-vint-render.md Sec7, 2026-10-03) ----------
    //
    // The axis-aligned box behind screen_nw/screen_se/screen_size, in the
    // document's own authored coordinates (Sec7.2: the query chain starts
    // from identity and zero, NOT the screen fit), recomputed on every call
    // (Sec7.1 - nothing cached, culling plays no part):
    //  * pos/M (0x00e272c0): recurse to the parent while it is element-family,
    //    else start from pos 0, M = identity; then pos = P.pos + P.M*anchor,
    //    M = P.M * R(rotation) * S(scale) (Sec5.2's compose, op-for-op; the
    //    matrix products are computed in double and rounded to float, Sec5.2);
    //  * the four corners pos + M*(offset + q), q over the unscaled_size box
    //    (0x00e280d0), their axis-aligned box, unioned with every
    //    element-family child's own box, recursively (0x00e28540) - children
    //    in tree order via firstChildHandle/nextSiblingHandle; tween/animation
    //    children are skipped (not element-family);
    //  * group: the union of its element children's boxes only; a childless
    //    group "collapses to a single point" whose position Sec7.1 does not
    //    give -> nullopt here (its screen_size is then (0,0); screen_nw/se and
    //    any parent union that includes it refuse, OPEN);
    //  * text: OPEN (layout re-run, Sec7.1/Sec11 item 6) -> OpenStateError;
    //  * unscaled_size of bitmap/gradient/text/bitmap_circle (sized by their
    //    own paths, Sec5.4) is OPEN unless a test set it.
    // auto_offset's applier (Sec5.4, vtable +0xc4) is NOT run here: the spec
    // names when it fires only for the size-changing paths of bitmap/text/
    // bitmap_circle, which this host does not model, so `offset` is whatever
    // was stored. Throws OpenStateError for anything above that is OPEN.
    struct VintBox { double minX = 0, minY = 0, maxX = 0, maxY = 0; };
    std::optional<VintBox> vintQueryBox(uint32_t handle) const;
    // The query-time pos/M chain itself (0x00e272c0, Sec7.1) for one
    // element, in document coordinates: `px,py` = the pivot position, `m` =
    // the 2x2 matrix row-major {m00, m01, m10, m11} (x' = m00*x + m01*y).
    // Sec7.1: an operation-for-operation re-implementation of the render-time
    // compose step (Sec5.2), differing only in its starting frame (identity/
    // zero instead of the screen fit, Sec7.2) - so a renderer gets the
    // render-time state as fitOffset + fitScale*(px,py) and fitScale*m
    // (the fit scale is uniform, Sec5.1). Exposed 2026-10-03 for the UI draw
    // path (sr3_viewer vint-doc). Throws OpenStateError as vintQueryBox does
    // for an unknown/untyped ancestor.
    struct VintQueryTransform { double px = 0, py = 0; double m[4] = {1, 0, 0, 1}; };
    VintQueryTransform vintQueryTransform(uint32_t handle) const;
    // The screen_size setter (Sec7.3, shared body 0x00e27c70): clamps the
    // request to >= 1e-4 per axis, then scale := scale * requested /
    // current screen_size. HOST-SAFETY (coordinator-mandated, not engine
    // behaviour): the real body divides by a zero current size unguarded
    // (float division - a non-finite scale); this host writes nothing in
    // that case and counts it (VintPropertyStats::screenSizeDivZeroGuards).
    void setVintScreenSize(uint32_t handle, double requestedW, double requestedH);

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
    // gate runs) - the real further dispatch (0x00e1e660, the trailing-
    // argument array, and the +0x25c context stamp) has no Lua-visible
    // consequence among this pass's two in-scope functions and is not
    // modeled (0x00e1e660's own body was not independently decompiled this
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
    // Sec9.10a (spec e8dc04d): vehicle path-follow arrival condition. Called by
    // vehicle_pathfind_to_do once it accepts a request: registers a PENDING kind-2
    // request keyed by the vehicle, remembering the goal object name.
    void registerVehiclePathfindRequest(const std::string& vehicleName, const std::string& targetName);
    // Pure arrival test, Sec9.10a item 1 (0x00b4ea00, last node): horizontal (X/Z
    // only, Y ignored) squared distance <= threshold; threshold 4.0 (sub +0x1c bit 0
    // set) else 16.0; mode 2 overrides to 2500.0.
    static bool vehiclePathfindArrived(const WorldPosition& vehicle, const WorldPosition& goal,
                                       bool tightThreshold = false, bool mode2 = false);

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
    // this task's own brief).
    //
    // CLOSED 2026-10-09 (mission-census item, missions m17/m01; routed as
    // "local_player_exists" but Sec10.3's own text confirms no Lua native
    // by that name, or any close variant, exists in the full 1,490-entry
    // registered list - `game_hud_update_inventory` is simply one of 20+
    // natives sharing this SAME accessor, 0x009da4e0, and is this
    // project's own chosen home for the finding): `0x009da4e0`'s real
    // body is trivial - it returns the dword at the fixed global address
    // `0x0262edfc` directly, the local player's own live game-object
    // instance, not a resolver chain. Traced all 3 real writers: SET
    // (non-null) by `0x009e1fc0` (the boot-time player-creation/spawn
    // routine) and, independently, by `0x009e3bf0` (a character-preset-
    // swap/rebuild routine, same class id); CLEARED to null by
    // `0x009da150`, a generic per-instance teardown routine, but ONLY when
    // the object being destroyed is the one the global currently points
    // at (a plain pointer-identity check - any other instance's teardown
    // takes an unrelated branch). So "a local player exists" is really
    // "has either constructor path run since the last matching teardown" -
    // populated once per player-object lifetime, nulled exactly when that
    // SAME object is torn down.
    //
    // `hasLocalPlayer_` therefore now carries a CONFIRMED default of true,
    // pre-seeded by applySpecInitialState (lua_spec_initial_state.cpp,
    // item 16 - renumbered when merged alongside the pre-existing items
    // 13-15 added independently on main) - same "pre-seed a newly-CONFIRMED
    // default instead of leaving an OpenValue OPEN" convention the
    // notoriety spawn-group name-table fix used (Sec6.2/Sec13.2,
    // 2026-10-09): a running
    // single-player game has already run one of the 2 real constructor
    // paths before any script's own first tick, and the live playtest
    // viewer (tools/sr3_viewer.cpp) independently reaches the identical
    // conclusion by explicitly calling `.set(true)` for its own HUD/
    // mission sessions. OPEN, honestly flagged, NOT papered over: the "1
    // conditional clearer" is real and identifiable (ordinary player-
    // object teardown, e.g. a respawn/session-end rebuild cycle that
    // tears down the old instance before `0x009e1fc0`/`0x009e3bf0` builds
    // the new one) - this project does not model that teardown window, so
    // a script tick that genuinely runs DURING it would see this field
    // still (wrongly) true. Remains a plain OpenValue, not a bare bool:
    // `.set(false)`/`.forget()` stay available, and every existing test
    // that deliberately exercises the "no local player" path keeps doing
    // so by calling them explicitly, exactly as before this pre-seed.
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

    // --- tutorial_start (Sec6.19, 0x00a603f0), batch 2026-10-08 ------------
    //
    // Arg 1 (string, mandatory) resolves through tutorialLookup() above
    // (the SAME 210-entry table tutorial_advance/tutorial_stop already
    // share - reused directly). Args 2 (number, default 0), 3-5 (booleans,
    // each default false) plus an internal hidden literal 1 (never
    // Lua-visible) are handed, with the resolved index, to the 12-step
    // dispatcher 0x007169e0 (CONFIRMED structure, re-derived 2026-10-01,
    // job 20261001T020213-team-a-dksj). Several of its own gates read
    // engine globals whose real-world MEANING - and, for these five, even
    // their own CONFIRMED initial value - is genuinely OPEN (unlike
    // continuous_spawn_start's args 5-7 above, which DO have confirmed
    // default values): modelled as OpenValue, not plain fields, per this
    // task's own "OPEN means value unknown here, not just meaning
    // unknown" distinction. Three of the five share a LITERAL address with
    // an already-modelled, unrelated field elsewhere in this header; none
    // of those connections is asserted by any spec text cross-referencing
    // Sec6.19 with the other section, so each gets its OWN field here,
    // flagged with the coincidence in its own comment, not silently
    // aliased (this project's standing convention for an unconfirmed
    // cross-reference, Sec4.13's own door_open/teleport precedent).
    struct TutorialStart {
        // Step 1: "A < 189 and the global byte 0x0151d5a6 is non-zero ->
        // stop (indices 189 and up bypass this byte)." Also read by step
        // 8 ("A>=189 -> set state 1 through 0x00716170, itself gated on
        // 0x0151d5a6 being zero") and step 9. Reads, from its own gating
        // role, as a "base-game tutorial prompts suppressed" toggle - this
        // project's own inference, not an independently confirmed field
        // name. No confirmed initial value anywhere in the spec.
        OpenValue<bool> suppressionByte0151d5a6{
            "tutorial dispatcher global byte 0x0151d5a6 (meaning OPEN; non-zero suppresses base-game [index<189] prompts)",
            "spec-lua-api-behaviour.md Sec6.19"};
        // Step 2: "The global 0x029443bc is 1, 2 or 3 -> stop." **CORRECTED,
        // 2026-10-10, Team A spec sync (dlc2_m03 mission-blocking routing,
        // scratchpad/dlc2_mission_blockers_0x029443bc_vtable68_2026-10-10.md):
        // retract the "tutorial dispatcher global" framing entirely -
        // 0x029443bc is NOT tutorial-owned. It is the machinima recorder's
        // OWN recording-state machine, written exclusively by the
        // 0x00bc4f60-0x00bce150 module (same global cell_machinima_is_
        // recording/game_machinima_is_recording test via MachinimaState::
        // recording above, Sec44.4/Sec59.1 - kept as its own separate
        // OpenValue, not aliased to that bool field, per this struct's own
        // top "no silent cross-link" convention). Full writer map: `0` =
        // idle/not recording (CONFIRMED the zero-fill default - a fresh
        // single-player game reads 0 - this is what applySpecInitialState()
        // now seeds); `1` = actively recording (written by the start-request
        // handler 0x00bce0d0 after 5 other engine-state gates pass); `2`/`3`
        // = stopping/finalizing (an 8-step async drain/flush sub-state
        // machine in companion global 0x029443e0, driven by 0x00bc6d80);
        // `4` = teardown in progress (set by 0x00bc6160, cleared back to 0
        // once 0x00706ab0 reports 2). This dispatcher step is a *consumer*
        // refusing to run during/after a machinima capture, not a
        // tutorial-owned sentinel - tutorial_start itself has no stake in
        // this global's meaning. Field/key name kept as globalMode029443bc
        // (not renamed) since it is this struct's own local step-2 gate
        // value, referenced by address elsewhere in this project; the
        // meaning correction lives in this comment and in the seed below.
        OpenValue<int32_t> globalMode029443bc{
            "machinima-recorder recording-state global 0x029443bc (0 idle [default]/1 recording/2,3 finalizing/4 teardown; step 2 stops the tutorial dispatcher while 1, 2 or 3) - NOT a tutorial-owned concept, corrected 2026-10-10",
            "spec-lua-api-behaviour.md Sec6.19 (correction)"};
        // Step 5: "0x00706ab0 returns 6 -> stop." CONFIRMED (2026-10-10 Team
        // A spec sync, routed mission-blocking dlc2_m03, agent
        // a53c6769fd437715d): 0x00706ab0 is a top-of-mode-stack accessor -
        // `if (0x012f4a80 >= 0) return mode_stack_array[0x012f4a80]; else
        // return 0;` - and the stack-top index 0x012f4a80 is file-backed,
        // initialized to -1, i.e. the mode stack starts EMPTY on a fresh
        // game, so this accessor's CONFIRMED spawn default is 0, not 6.
        // Mode 6 (the only value that stops this step) is the control-
        // scheme/key-bindings configuration sub-screen (spec-frontend-menu-
        // runtime.md Sec1) - step 5 only stops tutorial_start while that
        // options sub-screen is the active mode-stack entry, a state a
        // fresh/default game is never in. See EngineState::EngineState()'s
        // own seed for this field (the same construction-time "pre-seed a
        // newly-CONFIRMED default instead of leaving an OpenValue OPEN"
        // convention as CharacterState::ragdollStateEnumCd2/stateCodeByte501
        // above). NOTE: the exact same address is already modelled as
        // ScreenFade::modeStackTop ("mode stack top... read by the
        // per-frame routine", Sec26.24) - plausibly the same shared
        // per-frame engine-mode query, but not cross-referenced by any
        // spec text, so kept as its own separate field (see struct's own
        // top comment). Remains a plain OpenValue, not a bare int:
        // .set()/.forget() stay available, so a test exercising the
        // key-bindings-screen-open case can still override this explicitly.
        OpenValue<int32_t> gate5Value00706ab0{
            "tutorial dispatcher 0x00706ab0 result (CONFIRMED default 0, mode stack empty on a fresh game; ==6 -> stop, only while the key-bindings config screen is active; NOTE: same address as ScreenFade::modeStackTop, Sec26.24 - not confirmed to be the same field)",
            "spec-lua-api-behaviour.md Sec6.19"};
        // Step 6, first half: "If the byte 0x014f3d34 is non-zero,
        // continue only for A=188 or 196<=A<=201. [OPEN - what the
        // 0x014f3d34 mode is.]"
        OpenValue<bool> mode014f3d34{
            "tutorial dispatcher byte 0x014f3d34 (meaning OPEN)",
            "spec-lua-api-behaviour.md Sec6.19"};
        // Step 6, second half (the "it is zero" branch's own fallback):
        // "...or otherwise only when no mission object is live (0x006d2910
        // returns -1)." NOTE: the exact same address is already modelled
        // as GuardianAngel::modeIsThree ("0x006d2910 mode == 3 via
        // 0x00614d00", Sec28.4) - a DIFFERENT comparison (==3, not ==-1)
        // and not cross-referenced by any spec text to this section, so
        // kept separate (see struct's own top comment).
        OpenValue<bool> noMissionLive006d2910{
            "tutorial dispatcher 0x006d2910 == -1 (\"no mission object is live\"); NOTE: same address as GuardianAngel::modeIsThree, Sec28.4 (a different, ==3 comparison) - not confirmed to be the same field",
            "spec-lua-api-behaviour.md Sec6.19"};
        // Step 7: "A = 76 and the byte 0x0141250d is zero -> A becomes
        // 73. [OPEN - meaning.]" NOTE: the exact same address is already
        // modelled as EffectFinisher::gamepadMode ("gamepad flag
        // 0x0141250d", Sec28.9) - thematically plausible (several of the
        // 210-entry table's own names are explicit PC/gamepad variant
        // pairs, e.g. "combat_grenades"/"combat_grenades_pc"), but not
        // cross-referenced by any spec text, so kept separate (see
        // struct's own top comment). True here means "byte is non-zero"
        // (the remap fires when this is FALSE).
        OpenValue<bool> byteNonZero0141250d{
            "tutorial dispatcher byte 0x0141250d (meaning OPEN; zero -> remap index 76 to 73; NOTE: same address as EffectFinisher::gamepadMode, Sec28.9 - not confirmed to be the same field)",
            "spec-lua-api-behaviour.md Sec6.19"};

        // Per-entry descriptor data (Sec6.19's own "descriptor record"):
        // compile-time-baked constants this project has no transcription
        // of (unlike the 210-name table itself, fully transcribed) - OPEN
        // per index until a test supplies the specific entry it is
        // exercising. flagsByte24: bit 0x04 "use the rich (animated/
        // video) widget", bit 0x08 "allowed while a mission is active",
        // bit 0x10 "host-only" (CONFIRMED bit meanings; OPEN per-index
        // value). defaultParam20: the "+0x20 default parameter" copied
        // into the per-entry record's own +0x18 field when arg 5 is
        // false (CONFIRMED mechanism; OPEN per-index value).
        OpenValueMap<uint32_t> flagsByte24{
            "tutorial descriptor flags byte +0x24, keyed by table index (0x04 rich widget/0x08 mission-active-ok/0x10 host-only)",
            "spec-lua-api-behaviour.md Sec6.19"};
        OpenValueMap<int32_t> defaultParam20{
            "tutorial descriptor default parameter +0x20, keyed by table index",
            "spec-lua-api-behaviour.md Sec6.19"};

        // Runtime per-entry record (Sec6.19's own entry-layout table,
        // fields +0x14/+0x18/+0x1c - the +0x00/+0x04 linked-list links and
        // +0x08/+0x0c descriptor-pointer/state fields are already modelled
        // above via tutorialState()/applySpecInitialState(); +0x10 is
        // "not touched by the dumped code" per that table, so it is not
        // modelled at all). Written ONLY by step 11's simple-prompt
        // (non-rich-widget) branch, per that step's own text ("fill
        // +0x14/+0x18/+0x1c as in the table above") - the rich-widget
        // branch never touches these.
        struct EntryRecord {
            bool queuedForSimplePrompt = false; // step 11's own "if the entry is not already queued" gate
            int64_t argument2Value = 0;         // +0x14: arg 2 (N), truncateEa2596
            int32_t argument5GatedValue = 0;    // +0x18: 0 if arg 5 (B5) true, else defaultParam20[index]
            int32_t hiddenLiteralP4 = 1;        // +0x1c: the hidden literal 1 (CONFIRMED, never Lua-visible)
        };
        std::unordered_map<int32_t, EntryRecord> entries;

        // Step 11's rich-widget branch: "build the rich widget... keep its
        // handle in 0x0151d5b4, set state 4." This project has no real
        // UI-widget object (no coordinate/UI-document system in scope) -
        // counted per index only, same "dispatch happened" convention as
        // every other widget-handle field project-wide.
        std::unordered_map<int32_t, uint64_t> richWidgetDispatchCount;
        // Step 12's own "a dword from 0x0086be20" and the opcode-0x54
        // record itself: CONFIRMED to fire (gated on session+host only,
        // since P4=1 is always true from Lua - Sec6.19's own "a host
        // therefore always replicates tutorial_start" closing line), but
        // this project does not simulate real network replication (same
        // project-wide convention as every other record-and-replicate
        // idiom, Sec4.13's cross-function note) - counted only.
        uint64_t replicateCallCount = 0;
    };
    TutorialStart& tutorialStart() { return tutorialStart_; }

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

    // NEW 2026-10-08 (zone-data fast-follow, wiring the real
    // sr3zone::ZoneObjects reader (include/sr3zone/zone_objects.h,
    // spec-zone-data-format.md Sec11) into this host's own named-object
    // lookup - the exact gap objectResolves()'s own doc comment above and
    // CharacterState::scriptNpcHp's doc comment both flag by name). Ingests
    // every NAMED record in `section` (sr3luahost::ZoneObjectResolver::
    // ingest() - see zone_object_resolver.h's own top comment for why
    // `ctg_object_name` is the confirmed Lua-visible identity field, not
    // assumed) and, for each one:
    //
    //  1. Pre-populates objectResolves() with `true` under the record's own
    //     exact on-disk name (NOT case-folded - objectResolves() is the
    //     existing, much older flat OpenValueMap<bool> shared by many
    //     unrelated per-name concepts project-wide, kept exact-case and
    //     otherwise completely unmodified so every one of its ~60 existing
    //     call sites sees this for free with zero signature/behavior
    //     change elsewhere - see zone_object_resolver.h's own "CASE-
    //     INSENSITIVITY" paragraph for why the spec-confirmed case-fold
    //     instead lives only in resolveZoneObject()/ZoneObjectResolver
    //     below). A name already known in objectResolves() (e.g. a test's
    //     own .set()/.forget() on the SAME literal string) is overwritten -
    //     real zone data WINS when it exists, matching the doc comment's
    //     own "falls back to the test-settable map only when a name isn't
    //     found in any loaded zone" design: nothing calls this method
    //     during any existing test's own setup, so no currently-passing
    //     test observes any change in behavior from this step.
    //  2. If the record's own class resolves (ClassNameForHash) to the
    //     literal string `"script_npc"` - the exact same class/mechanism
    //     spec-zone-data-format.md's own real `Killbane` resolution already
    //     used (`crc32l('script_npc') = 0xa518e725`, matching that class's
    //     published 6,788-object population count) - marks the matching
    //     character scriptNpcBound (same "forget the CONFIRMED defaults"
    //     shape as markScriptNpcBoundForTesting() above) and, if the record
    //     also carries a decodable `script_npc_hp` property
    //     (sr3zone::ScriptNpcHp()), applies it via the SAME precedence path
    //     applyScriptNpcHitPointsForTesting() uses - except this is a REAL
    //     production path, driven by real ingested zone data, not a
    //     test-only entry point. A script_npc record with NO script_npc_hp
    //     property is left scriptNpcBound with scriptNpcHp still OPEN,
    //     exactly as markScriptNpcBoundForTesting() already does - the real
    //     "most script NPCs never set this" population finding
    //     (spec-zone-data-format.md Sec11.7: 1,028 of 6,788 script_npc
    //     objects) is reported honestly, not papered over.
    //  3. NEW 2026-10-10 (vehicle-class xtbl wiring task): if the record's
    //     own class instead resolves to the literal string "Script_Vehicle"
    //     (also a real, named class, spec-zone-data-format.md's own
    //     "Resolved classes include" list) AND it carries a decodable
    //     `script_vehicle_type_name` property (sr3zone::ScriptVehicleTypeName())
    //     that matches (case-folded) an entry of installVehicleTypeClassTable()
    //     above, populates that vehicle instance's VehicleState::flyingType
    //     with the REAL xtbl-sourced class value, and - when that class is 3
    //     (Helicopter) or 4 (Vtol), HelicopterFlyTo::qualifies' own CONFIRMED
    //     gate (Sec28.2: "class in {3,4}") - also marks it true. This is the
    //     real, general, data-driven replacement for the single hardcoded
    //     "m05_heli_pierce" name special case getOrCreateVehicle() used to
    //     carry (removed this same task). A script_vehicle record with no
    //     script_vehicle_type_name property, or one whose type is not in the
    //     installed table (e.g. installVehicleTypeClassTable() was never
    //     called), is left exactly as OPEN as before - never guessed at.
    //
    // Never opens a file itself - `section` must already be parsed (via
    // sr3zone::ZoneObjects::locate()/parseAt()) by the caller; see
    // tools/lua_host_run.cpp's own real-cache install step for where that
    // happens against the real shipped `.czn_pc` population.
    void loadZoneObjectsSection(const sr3zone::ObjectsSection& section);

    // Case-insensitive direct accessor onto the real ingested zone data
    // (ZoneObjectResolver::find() - see zone_object_resolver.h's own
    // top comment), for a caller that wants the record itself (class hash,
    // script_npc_* properties, etc.), not just the boolean objectResolves()
    // already exposes. Returns nullptr if `name` was never seen in any
    // ingested zone (case-insensitively).
    const sr3zone::ObjectRecord* resolveZoneObject(const std::string& name) const { return zoneObjects_.find(name); }

    // Diagnostic/population count - how many distinct named records have
    // been ingested across every loadZoneObjectsSection() call so far.
    size_t zoneObjectsLoadedCount() const { return zoneObjects_.size(); }

    // inv_weapon_add_temporary (spec-lua-api-behaviour.md Sec4.10) /
    // inv_weapon_remove_temporary (Sec4.11), batch 2026-10-08 (the
    // 6-native clearance fast-follow). Both natives share ONE real
    // "dedicated weapon-definition lookup" (CONFIRMED, Sec4.10/Sec4.11's
    // own 2026-10-08 disassembly: a case-insensitive linear scan - the
    // real public C runtime `_stricmp` - over a fixed-stride, 0x7ec-byte,
    // global weapon-definition table at `0x028dc4dc`, filtered to records
    // with flags bit `0x4` at `+0x18`). This project has a SEPARATE,
    // already-built clean-room `weapons.xtbl` CONTENT reader
    // (sr3tables_weapons, include/sr3tables_weapons/weapons.h) but nothing
    // anywhere wires that static table CONTENT into the ENGINE's own
    // runtime array this spec describes (a genuinely different, in-scope-
    // elsewhere concern - building that loader is out of this task's own
    // scope). So, matching this project's own standing convention for a
    // real lookup table with no backing population modeled yet
    // (lookupNotorietyTrackCode()/resolveInventorySlotId() above are the
    // closest precedent: a flat, test-settable map, "OPEN until set," not
    // a guessed real value), this is a flat, test-settable resolver - OPEN
    // (throws) for any weapon name no test (or, in a future pass, a real
    // weapon-table loader) has registered. Case-folded via the SAME
    // AsciiLowerFold() this project's zone-object resolver already
    // established for ITS OWN real, independently-confirmed `_stricmp`-
    // equivalent case-insensitive lookup (sr3luahost/zone_object_
    // resolver.h) - the natural, already-built place for this project's
    // one shared "fold like the real engine's case-insensitive compare"
    // convention to live, rather than a second, separately-written fold.
    OpenValueMap<bool>& weaponDefinitionResolves() { return weaponDefinitionResolves_; }
    // Real weapons.xtbl (+ loaded DLC) row names (spec-lua-api-behaviour.md
    // Sec4.10 "Table contents"): every name resolves true, and once installed
    // the table is COMPLETE, so any other name resolves false (native does
    // nothing) instead of OPEN-throwing. Names are case-folded here.
    void installWeaponDefinitionNames(const std::vector<std::string>& names) {
        for (const std::string& n : names) weaponDefinitionResolves_.set(AsciiLowerFold(n), true);
        weaponDefinitionTableComplete_ = true;
    }
    bool weaponDefinitionTableComplete() const { return weaponDefinitionTableComplete_; }
    // weapons.xtbl `Weapon_Class` (spec-tables-weapons-combat.md Sec2.5: 5 = `thrown`) per row name,
    // for the force_throw_do "holds a thrown/grenade-class weapon " rule (Sec55.4,
    // 2026-10-12). Optional: a host that installs only names leaves every class unknown.
    static constexpr int32_t kWeaponClassThrown = 5;
    void setWeaponDefinitionClass(const std::string& name, int32_t weaponClass) {
        weaponClassByName_[AsciiLowerFold(name)] = weaponClass;
    }
    // CHOSEN rule per spec-lua-api-behaviour.md Sec55.4 (2026-10-12, FINAL as guidance): true when the
    // character holds a weapon of the thrown class (Weapon_Class == 5; the 2026-10-12 second pass drops the
    // ammo condition and adds a carried-throwable-prop case this host does not model); false otherwise. Only the
    // temporary-grant inventory (inv_weapon_add_temporary) is modelled here - the character's
    // permanent inventory is not.
    bool characterHoldsThrownWeapon(const std::string& character) const {
        auto inv = weaponTemporaryInventory_.find(character);
        if (inv == weaponTemporaryInventory_.end()) return false;
        for (const auto& kv : inv->second) {
            auto cls = weaponClassByName_.find(kv.first);
            if (cls != weaponClassByName_.end() && cls->second == kWeaponClassThrown && kv.second.temporaryGrant)
                return true;
        }
        return false;
    }
    // Resolve a case-folded weapon name; OPEN-throws only if no real table installed.
    bool weaponDefinitionResolve(const std::string& foldedName) {
        if (weaponDefinitionTableComplete_ && !weaponDefinitionResolves_.known(foldedName)) return false;
        return weaponDefinitionResolves_.get(foldedName);
    }

    // The real per-target, per-weapon inventory-slot record both natives
    // write/clear (WeaponTemporaryInventorySlot, misc_state.h - see its own
    // doc comment for exactly which fields are CONFIRMED vs. HIGH-
    // CONFIDENCE-only raw storage). Outer key: target name (NOT case-
    // folded - matches objectResolves()'s own exact-case convention, since
    // the target resolves through the generic/character resolver, not the
    // weapon-definition one). Inner key: AsciiLowerFold(weapon name) -
    // matches the real `_stricmp` lookup weaponDefinitionResolves() above
    // already models, so "Gdhc" and "GDHC" hit the SAME slot, exactly as
    // the real engine's shared weapon-definition table would.
    std::unordered_map<std::string, std::unordered_map<std::string, WeaponTemporaryInventorySlot>>&
    weaponTemporaryInventory() {
        return weaponTemporaryInventory_;
    }

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

    // DIAGNOSTIC ONLY, a second --probe-unresolved-names extension
    // (2026-10-03, peer follow-up once get_dist's own worldPosition OPEN
    // gap became the shared next blocker for 5 missions): UNLIKE
    // probedFieldOr() above, this is not gated on the owning name itself
    // having been a probed (synthetic) name - a legitimately-resolved name
    // (one of the 5 CONFIRMED literals, e.g. "#PLAYER1#") can reach
    // get_dist just as easily as a synthetic one, and its position is
    // EQUALLY unknown either way (no spec gives a real spawn position for
    // ANY object, confirmed name or not - see WorldPosition's own doc
    // comment). So this probes on "is an OPEN position itself" directly,
    // once explicitly enabled, rather than requiring a probed owner name -
    // a deliberately different, slightly broader gate than
    // probedFieldOr()'s, same off-by-default/diagnostic-only discipline.
    void enableWorldPositionProbe() { worldPositionProbeEnabled_ = true; }
    uint64_t worldPositionProbeReadCount() const { return worldPositionProbeReadCount_; }
    WorldPosition worldPositionOr(const OpenValue<WorldPosition>& v, const WorldPosition& fallback) {
        if (v.known()) return v.get();
        if (worldPositionProbeEnabled_) {
            ++worldPositionProbeReadCount_;
            return fallback;
        }
        return v.get(); // throws OpenStateError: real OPEN gap, diagnostic not enabled
    }

    // REAL fallback (2026-10-10, m07 get_dist/npc_leash_to_nav live-
    // playtest follow-up - NOT the diagnostic probe above): `worldPosition`
    // is written live for exactly one name, "#PLAYER1#"
    // (bridgeLiveCharacterPosition()'s own doc comment - deliberately the
    // ONLY one). Any OTHER resolved name (an NPC, a navpoint, a trigger
    // anchor, ...) that a get_dist/npc_leash_to_nav/teleport_vehicle/
    // get_object_pos/turn_to_do/vehicle_rc_get_signal_strength call reaches
    // has never had anything write its worldPosition this way, so it reads
    // as OPEN even though the name itself resolved correctly - a real gap
    // in COVERAGE, not evidence the field's own reasoning (character-
    // shaped, Sec4.2) is wrong. But the real engine's own zone data
    // (.czn_pc) already gives a REAL, on-disk position for exactly this
    // class of name - the SAME sr3zone::ObjectRecord/ObjectPosition()
    // source this project's own in-world-trigger auto-registration
    // already uses live (runPlaytest()'s tryAutoRegister lambda, spec-
    // lua-api-behaviour.md Sec1.14) - confirmed (not assumed) to cover
    // navpoints too: tests/real_zone_objects_test.cpp's own
    // navpointWithPosition counter. So before surfacing OpenStateError,
    // try that same real source: if `name` resolves to a zone-object
    // record with a position property, that IS a real, confirmed position
    // for it (not a fabricated 0,0,0 stand-in) - cache it into `v` via one
    // real .set() so repeat reads are O(1) and consistent, and report a
    // genuine hit. Returns false (never throws itself) when `name` either
    // isn't a real zone object or has no position property on its own
    // record - the caller's existing `v.get()`/`worldPositionOr()` call
    // is the correct, honest fallback for that genuinely-unresolved case
    // (e.g. the one demo playtest vehicle, which Sec11.10's own closed
    // 4-producer list confirms the real engine's name map never gets a
    // live entry for either).
    bool tryResolveWorldPositionFromZoneData(const std::string& name, OpenValue<WorldPosition>& v) {
        if (v.known()) return true;
        const sr3zone::ObjectRecord* rec = resolveZoneObject(name);
        std::optional<sr3zone::Position3f> pos = rec ? sr3zone::ObjectPosition(*rec) : std::nullopt;
        if (!pos) return false;
        v.set(WorldPosition{static_cast<double>(pos->x), static_cast<double>(pos->y), static_cast<double>(pos->z)});
        ++zoneDataWorldPositionHitCount_;
        return true;
    }
    uint64_t zoneDataWorldPositionHitCount() const { return zoneDataWorldPositionHitCount_; }

    // --- generic get_dist "same stall point" tracker (feature/auto-objective-
    // harness, 2026-10-12) --------------------------------------------------
    // The m07 spawn-at-barge diagnostic (results/verify_m07_spawn_at_barge_
    // 20261012/) manually confirmed that a navigation-gated mission re-issues
    // the SAME get_dist(a,b) call every scheduler resume while its own wait
    // loop sits unsatisfied (340 identical calls observed for m07), and that
    // the non-player name's own tryResolveWorldPositionFromZoneData() position
    // IS the real target a scripted player needs to reach. This generalizes
    // that observation into reusable state stub_get_dist (src/lua_spec_
    // confirmed_stubs_b05_invuln_r15_23.cpp) records on EVERY call, zero
    // behavior change to get_dist's own return value - --auto-objective
    // (tools/sr3_viewer.cpp) is the only reader. Not get_dist-specific in
    // spirit (any "same two names, N frames running" check would work), but
    // get_dist is the ONLY position-shaped native this project's missions are
    // confirmed (m07, live) to stall on, so it's the only one wired here -
    // not inventing a GPS-waypoint or objective-marker tracker this project
    // has no CONFIRMED source for (engine_state.h's own waypoint_add/_remove
    // comments: "this project has no real waypoint/HUD-marker tracking").
    struct GetDistStallInfo {
        std::string nameA;
        std::string nameB;
        WorldPosition posA;
        WorldPosition posB;
        bool knownA = false;
        bool knownB = false;
        uint64_t repeatCount = 0; // consecutive calls with the SAME unordered {a,b} pair, including this one
    };
    void recordGetDistCall(const std::string& a, const std::string& b,
                            const WorldPosition& pa, bool knownA,
                            const WorldPosition& pb, bool knownB) {
        const bool samePair = (a == getDistStall_.nameA && b == getDistStall_.nameB) ||
                               (a == getDistStall_.nameB && b == getDistStall_.nameA);
        getDistStall_.repeatCount = samePair ? (getDistStall_.repeatCount + 1) : 1;
        getDistStall_.nameA = a;
        getDistStall_.nameB = b;
        getDistStall_.posA = pa;
        getDistStall_.knownA = knownA;
        getDistStall_.posB = pb;
        getDistStall_.knownB = knownB;
    }
    const GetDistStallInfo& getDistStallInfo() const { return getDistStall_; }

    // --- generic on_trigger "stalled wait" tracker (feature/auto-objective-
    // trigger-vehicle-seat, 2026-10-12) ---------------------------------
    // Mirrors GetDistStallInfo/recordGetDistCall() above, but for the
    // SEPARATE wait-shape m09/m13 actually stall on: a mission script that
    // waits on an on_trigger()/on_trigger_exit() callback to fire (e.g.
    // m13.lua's own m13_goto_a_trigger, per results/verify_combined_with_
    // seatedpose_20261010's own stall-point dump) never calls get_dist at
    // all, so the get_dist tracker above cannot see it stall. runPlaytest()'s
    // own per-frame in-world-trigger block (tools/sr3_viewer.cpp) calls
    // recordTriggerWaitFrame() once per frame, naming the EARLIEST-registered
    // trigger (triggerRegistrationOrder() below) that has geometry but has
    // never produced an ENTER transition yet (triggerEverEntered() below) -
    // the trigger-volume analogue of "the same get_dist pair, N frames
    // running." A name CHANGE (a different trigger becomes the earliest
    // still-pending one, e.g. because the previous one finally fired) resets
    // the counter to 1, exactly like recordGetDistCall()'s own unordered-pair
    // reset rule.
    struct TriggerWaitStallInfo {
        std::string triggerName;
        uint64_t framesWaiting = 0;
    };
    void recordTriggerWaitFrame(const std::string& triggerName) {
        triggerWaitStall_.framesWaiting =
            (triggerName == triggerWaitStall_.triggerName) ? (triggerWaitStall_.framesWaiting + 1) : 1;
        triggerWaitStall_.triggerName = triggerName;
    }
    const TriggerWaitStallInfo& triggerWaitStallInfo() const { return triggerWaitStall_; }

    // Registration order (FIFO, first-seen-wins, no duplicates) of every
    // trigger name runPlaytest()'s own tryAutoRegister() lambda has
    // successfully registered geometry for this session - deterministic
    // "which trigger is the mission most likely waiting on first" ordering
    // that an unordered_map's own iteration order cannot give.
    void noteTriggerRegistrationOrder(const std::string& triggerName) {
        for (const std::string& existing : triggerRegistrationOrder_) {
            if (existing == triggerName) return;
        }
        triggerRegistrationOrder_.push_back(triggerName);
    }
    const std::vector<std::string>& triggerRegistrationOrder() const { return triggerRegistrationOrder_; }

    // True once a trigger name's own ENTER transition (triggerCurrentlyInside()
    // going false->true) has fired at least once this session - separate from
    // triggerCurrentlyInside() itself, which can go back to false on EXIT and
    // would otherwise look identical to "never entered" to a stall tracker.
    bool& triggerEverEntered(const std::string& triggerName) { return triggerEverEntered_[triggerName]; }
    // fix/auto-objective-driving (2026-10-13): triggers the running script POLLED for the live player via
    // object_is_in_trigger (the polling analogue of an on_trigger wait), in query order, drained once per
    // frame by the --auto-objective harness. Harness bookkeeping only; no engine semantics.
    void noteTriggerPollQuery(const std::string& triggerName) { triggerPollQueries_.push_back(triggerName); }
    std::vector<std::string> drainTriggerPollQueries() { std::vector<std::string> out; out.swap(triggerPollQueries_); return out; }
    // feature/route-table-driving: negative character_is_in_vehicle(#PLAYER1#[, V]) queries this frame (harness
    // bookkeeping only; V == "" for the any-vehicle form). Drained once per frame by --auto-objective.
    void noteVehicleWaitQuery(const std::string& vehicleName) { vehicleWaitQueries_.push_back(vehicleName); }
    std::vector<std::string> drainVehicleWaitQueries() { std::vector<std::string> out; out.swap(vehicleWaitQueries_); return out; }

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
    // Start-up values: 0x0153b556 the skip_all_cutscenes byte (false),
    // 0x0153b530 the current entry ("" = null, Sec26.25 Globals, RESOLVED
    // 2026-10-02), 0x0153b542 the re-queue-on-reset byte (false - Sec26.25
    // Globals' own 2026-10-02 correction: "permanently zero in practice...
    // all seven of its call sites in the binary pass 0", not merely a
    // start-up guess) and the nearest scene-bearing world object (list
    // 0x03171a64; "" = none found - this host has no world-object manager
    // anywhere, so the real list is unconditionally empty here, not merely
    // unpopulated - zscene_prep blocker fix, 2026-10-09) are set by
    // applySpecInitialState (lua_spec_initial_state.cpp).
    // CONFIRMED globals with no specced start-up value - all OPEN at start:
    //  - 0x0153b538 pending entry.
    //  - 0x0153b51c load state: 0 idle, 1 loading, 2 loaded (all CONFIRMED).
    //  - 0x0153b541 (label HIGH CONFIDENCE).
    //  - the soundtrack stream 0x0153b71c, its status and its timer 0x0153b724.
    // OPEN because the host has no streaming or audio subsystem (each a
    // labelled, test-settable OpenValue; an explicit .set()/.forget() always
    // wins):
    //  - the class 0x00dafb60 gives an entry's selected request-group handle
    //    (1 not live, 3 resident, 5 failed, anything else: wait). Per entry.
    //    The selection between +0xc and +0x10 (the female variant) is folded
    //    into this one "selected handle" class - a stated simplification.
    //    zscenePromotionStep() (lua_cutscene.cpp) now writes a REAL default
    //    of 1 ("not live"/null handle) for a name's FIRST-EVER promotion,
    //    narrowly, only when no test has already given it a class: Sec2.7's
    //    own classifier names "null handle" as its first-checked, catch-all
    //    case, and Sec26.25's Host summary spells out the exact, CONFIRMED
    //    consequence in full for a host like this one with no real
    //    streaming backend ("a host whose streaming model never marks the
    //    group resident leaves the scene at load state 1 forever"/"a refused
    //    handle start silently drops the scene ... the scene is silently
    //    forgotten"). This is a write at the one real event (the simulated
    //    0x00dafea0 handle-start call) that would produce it, not a blanket
    //    read-time default - it never touches a name a test has set, and it
    //    never resolves the SEPARATE, still-genuinely-OPEN "what 0x00dafad0
    //    leaves the flag word as" question (Sec26.25 review status) a LIVE
    //    teardown's own .forget() raises - that case keeps refusing exactly
    //    as before (see testFailedLoad, synthetic_luahost_cutscene_test.cpp).
    //  - whether the soundtrack stream has ended (status 0x66).
    struct ZsceneTableRow {
        std::string name;               // +0x0 as written in cutscene.xtbl
        uint32_t crc = 0;               // +0x4: NameHash(lower-cased name, 0)
        std::optional<int> kind;        // +0x8 (nullopt: OPEN, see sr3tables_cutscene)
        // +0x20 resources (names for the host's 0x00722f10 load log);
        // nullopt: OPEN (the spec describes the list only for kinds 1 and 2).
        std::optional<std::vector<std::string>> resources;
        // sr3tables_cutscene::SceneFields::cameraScriptText (added 2026-10-08,
        // the cutscene_play_do camera-script wiring task) - the CameraScript
        // element's raw text, unresolved. nullopt: OPEN (absent in the real
        // .cte_xtbl, or a row built without this field, e.g. an existing
        // test's own hand-built rows - see that header's own doc comment).
        std::optional<std::string> cameraScriptName;
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
    // file-backed initial values Sec26.24 lists (and, 2026-10-07, the
    // cutscene state's own zero-fill default, Sec60.4); the mode-stack top
    // the per-frame routine reads is still OPEN.
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
        // CONFIRMED known at construction (Sec60.4, 2026-10-07 closure,
        // applySpecInitialState() sets it to 0/"idle") - zero-fill .data with
        // no static initializer, same mechanism as 0x0153b530/0x0153b556. See
        // applySpecInitialState's own item 4 comment (lua_spec_initial_state.cpp).
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

    // --- Sec37.3's shared game-pause depth counter: a concrete instance ----
    // (spec-frontend-menu-runtime.md Sec4, the fade-completion teardown's own
    // push/pop of reason 0xd). The real engine: ~60 push/pop sites, 11
    // reason codes, ONE shared counter, "a pop decrements the depth without
    // checking its own reason was the one pushed" (Sec37.3's own text -
    // bookkeeping only, which is why popGamePauseReason() below never
    // verifies `reason` against what was pushed). The one CONFIRMED real
    // CONSEQUENCE this project can actually model is bare_globals.h's own
    // documented finding: every real pusher/popper (0x007074a0/0x00708a80
    // push, 0x00707540/0x00708330 pop) passes the GAMEPLAY Lua state to the
    // scheduler's refcounted exempt list - so a 0->1 push attaches the
    // gameplay state to BareThreadTable's exempt list (if one was attached
    // via attachGamePauseScheduler()) and a 1->0 pop detaches it, exactly
    // matching that file's own "the gameplay state is exempted for exactly
    // as long as any game-pause level is held" reading. BareThreadTable is
    // only forward-declared here (no cross-include with bare_globals.h) -
    // push/pop are implemented out-of-line in src/lua_menu_runtime.cpp,
    // which includes it.
    struct GamePauseDepthState {
        int32_t depth = 0;
        int32_t lastReason = -1;
    };
    const GamePauseDepthState& gamePauseDepth() const { return gamePauseDepth_; }
    // Host wiring, same "attach once" convention as attachScreenFadeUiState()
    // above. Both null (the default) means push/pop still track depth/reason
    // correctly but never touch the scheduler - exercised that way by any
    // test that does not call this.
    void attachGamePauseScheduler(BareThreadTable* threads, lua_State* gameplayL) {
        gamePauseThreads_ = threads;
        gamePauseGameplayL_ = gameplayL;
    }
    void pushGamePauseReason(int32_t reason);
    void popGamePauseReason(int32_t reason);

    // --- the fade-completion teardown's own dialog-pool class-bit scan -----
    // (spec-frontend-menu-runtime.md Sec4, `0x007b4cb0` -> `0x007b3e80`/
    // `0x007b3de0`). This project models no UI dialog pool (`0x02247b80`,
    // the real engine's own tracked-entry array with per-entry class-flag
    // bytes) - these two bools are the NET predicates that pool-walk would
    // resolve to for the one real consumer this project can wire (the pause
    // push/pop below), transcribed directly from the spec's own described
    // inputs rather than inventing the pool itself. The gate `0x00867830`
    // (push additionally requires it return false - OPEN, not traced) and
    // the pop-side query `0x00707490` (OPEN) are both left unmodelled: NOT
    // gating here is the deliberate, labelled choice (every real fade this
    // project requests - including new game's and continue's own - routes
    // through this exact callback, so throwing on an OPEN read here would
    // make EVERY fade un-completable, not a safe default).
    struct FadeTeardownDialogState {
        bool anyOpenClassBit0x8Dialog = false;  // §4's own pause-push/pop predicate input
        bool anyOpenClassBit0x40Dialog = false; // §4's own second, non-pause side effect's input (0x007f1070, OPEN what it does) - counted only
    };
    FadeTeardownDialogState& fadeTeardownDialogState() { return fadeTeardownDialogState_; }
    uint64_t fadeTeardownSecondEffectCount() const { return fadeTeardownSecondEffectCount_; }
    void recordFadeTeardownSecondEffect() { ++fadeTeardownSecondEffectCount_; }

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
    // Sec20.14/Sec27.2 port pointer (2026-10-12, FINAL, m22): on a new game, or a harness starting a mission
    // directly, no mission is complete: false unless the host's own state (missionComplete().set) says otherwise.
    bool missionIsComplete(const std::string& name) const {
        return missionComplete_.known(name) ? missionComplete_.get(name) : false;
    }

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

    // Sec28.2 `helicopter_fly_to_set_goal_direction`. UPDATE 2026-10-10
    // (Team A spec sync, HANDOFF.md commit 849a88e): "heli AI record +0x2c"
    // in this label's own name is now known to be a holdover from this
    // project's prior (wrong) reading - the real field is the vehicle-TYPE
    // definition record's own class enum at (vehicle+0xbf4)+0x2c, static
    // per-type data with no runtime writer (CONFIRMED values 0 car/2
    // airplane/3 heli/4 VTOL; 1/5/6 OPEN) - the SAME physical field
    // VehicleState::flyingType above already models under its own,
    // already-corrected doc comment. Label text kept verbatim (it is also
    // this field's own OpenStateError diagnostic string, matched by
    // existing tests). UPDATE 2026-10-10 (vehicle-class xtbl wiring task):
    // EngineState::loadZoneObjectsSection() (src/lua_engine_state.cpp) now
    // populates this map for real, for any zone-placed vehicle instance
    // whose own `script_vehicle_type_name` property resolves - via
    // installVehicleTypeClassTable()'s real xtbl-sourced class data - to
    // class 3 (Helicopter) or 4 (Vtol), replacing the single hardcoded
    // "m05_heli_pierce" name seed this project used to carry here (see
    // getOrCreateVehicle()'s own history for the full removal note).
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

        // helicopter_go_to_set_target (Sec25.14) / helicopter_fly_to_stop
        // (Sec25.15), wave 2 batch W2-4 - EXTENDS this SAME struct rather
        // than a parallel one, per that batch's own cross-batch coordination
        // note (the helicopter-AI family spans wave 1 Batch 3's
        // helicopter_fly_to_do/helicopter_fly_to_set_goal_direction above
        // and this wave-2 batch). helicopter_go_to_set_target's own 4-tier
        // target-resolution fallback chain (generic/waypoint-table/character/
        // vehicle, each via a DIFFERENT real resolver) collapses to this
        // project's one flat objectResolves() map, the SAME simplification
        // this header already applies project-wide (Sec29's "one shared
        // global name map" finding) - which TIER matched is not
        // distinguishable and is not modelled, only that SOME target
        // resolved. helicopter_fly_to_stop reuses `qualifies` above (the
        // SAME heli-AI-record gate helicopter_fly_to_do/_set_goal_direction
        // already share, Sec12.11/Sec28.2) rather than inventing a second
        // gate - stopping just clears whichever of lastRequest/
        // lastGoToTarget is present for that helicopter, honestly, since
        // 0x00b37050/0x00b38280's own internals are OPEN.
        struct GoToTarget {
            std::string targetName;
            bool applied = false;
        };
        std::unordered_map<std::string, GoToTarget> lastGoToTarget;
        uint64_t goToSetTargetCount = 0;
        uint64_t flyToStopCount = 0;
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

        // --- guardian_angel_start_zone_pinning / _clear_zone_pinning /
        // _release_attack_groups (Sec53.3, wave-2 batch W2-7, 2026-10-08) ---
        //
        // CONFIRMED (Sec53.3): the release runs UNCONDITIONALLY before the
        // name lookup - "an unknown or misspelled zone-set name releases
        // whatever is currently pinned and then pins nothing new" - so
        // `startZonePinning` below always clears currentZonePinSetName
        // first, THEN attempts the new pin. CONFIRMED: "a zone set with
        // more than 8 zones pins nothing at all, yet the function still
        // records that set as 'the current pin' internally" - a real
        // state/reality mismatch this project reproduces via the separate
        // zonePinSucceeded flag (the name is recorded either way; whether
        // it actually took effect is tracked honestly, not conflated).
        // Real zone-SET membership/size data lives in `.czn_pc`-adjacent
        // zone tables this project has no by-name resolver for (same
        // "no real zone data" gap as elsewhere) - zone-set SIZE is test-
        // settable (OpenValueMap<int32_t>, OPEN until set) rather than
        // invented; a real call against an unregistered name throws
        // OpenStateError (honest refusal), matching this project's
        // established convention for "needs real zone data this project
        // doesn't have."
        std::string currentZonePinSetName;    // "" = none pinned (also the post-release transient state)
        bool zonePinSucceeded = false;        // false when size > 8 (recorded as current anyway) or never pinned
        OpenValueMap<int32_t> zoneSetSize{"guardian-angel zone-set size (zone-table data this project has no resolver for)",
                                          "spec-lua-api-behaviour.md Sec53.3"};
        uint64_t releaseAttackGroupsCount = 0; // guardian_angel_release_attack_groups: resolved in full per spec,
                                                // no further internal detail given - call count only, same "record
                                                // that a call happened" convention used throughout this header.
    };
    GuardianAngel& guardianAngel() { return guardianAngel_; }
    // Shared body for guardian_angel_start_zone_pinning / _clear_zone_pinning
    // (Sec53.3): `zoneSetName` empty or absent means "clear only" (the
    // _clear_ entry point). See GuardianAngel::currentZonePinSetName's own
    // doc comment above for the unconditional-release-first shape.
    void guardianAngelStartZonePinning(const std::string& zoneSetName); // .cpp

    // Sec28.5 `group_get_next_npc` / Sec28.6 `group_get_first_npc`: the group's own
    // +0x40 member-list head and each member's own +0x9c "next" link (Sec28.6/
    // Sec18.14 - NOT the light-group's unrelated +0x40 flags byte, Sec17.24,
    // which this project never touches here). Re-derived directly from the SAME
    // ordered member roster `group_members_alive` (Sec18.14) already maintains
    // (`groupMemberNames_`/`registerGroupMemberForTesting`) instead of a second,
    // separately-seeded OPEN map - this project has only ever modelled one
    // "which characters belong to which named group" registry, and Sec28.5/
    // Sec28.6 read the exact same list Sec18.14 counts, just from the other end.
    // An empty/unregistered group is a well-defined "no members known" answer
    // (nothing pushed) - the same "collapses to 0" precedent groupMembersAliveCount
    // already established for this registry - not a fabricated name and not OPEN.
    const std::string* groupFirstNpcName(const std::string& groupName) const {
        auto it = groupMemberNames_.find(groupName);
        if (it == groupMemberNames_.end() || it->second.empty()) return nullptr;
        return &it->second.front();
    }
    // Returns the member immediately after currentName in groupName's own
    // registered order, or nullptr if currentName is not a registered member,
    // is the last registered member (wraps back to the head - Sec28.5's own
    // documented stop condition), or the group has no registered members.
    const std::string* groupNextNpcName(const std::string& groupName, const std::string& currentName) const {
        auto it = groupMemberNames_.find(groupName);
        if (it == groupMemberNames_.end()) return nullptr;
        const auto& members = it->second;
        for (size_t i = 0; i < members.size(); ++i) {
            if (members[i] == currentName) {
                if (i + 1 < members.size()) return &members[i + 1];
                return nullptr; // wraps back to the group head: stop, push nothing
            }
        }
        return nullptr;
    }

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

        // teleport_player_vehicles (Sec22.5, wave 2 batch W2-4): a NEW
        // cached-vehicle-association field pair per player, +0x2880/+0x2884
        // on the PLAYER object - CONFIRMED distinct from the generic
        // character +0x16c0/+0x16c4 pair (CharacterState::currentVehicleName)
        // every other vehicle-association reader in this project already
        // uses. "" = no associated vehicle. coopPlayerPresent above (already
        // modelled, Sec28.12) gates whether player2's own half of this
        // native's relocate ever runs.
        std::string player1VehicleName;
        std::string player2VehicleName;
        uint64_t teleportPlayer1Count = 0;
        uint64_t teleportPlayer2Count = 0;

        // party_allow_max_followers (Sec22.9, wave 2 batch W2-4): per-player
        // "allow max followers" sync flag via the SAME double-gate (variant
        // 1, 0x008ae480/0x008837a0) this header's own forceFlagGatePasses
        // fields already model elsewhere - CONFIRMED (Sec3.7's own single-
        // player resolution) that SAME primitive pair always takes the
        // direct-local-apply branch with no session installed, so a plain,
        // always-true-by-default bool (not an OpenValue gate) stands in,
        // matching forceFlagGatePasses's own "seeded true at construction,
        // not OPEN" precedent, scaled down since PlayerRig has no per-
        // instance construction hook to seed from. Default true (Sec22.9's
        // own nil-gated-argument default).
        bool player1AllowMaxFollowers = true;
        bool player2AllowMaxFollowers = true;
        uint64_t allowMaxFollowersReplicateCount = 0;
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
        // Sec28.16 port pointer (2026-10-12, FINAL): the rows are exactly continuous_explosions.xtbl's;
        // a name matching no row is a silent no-op. Once the real table's names are installed it is COMPLETE.
        bool tableComplete = false;
        void installNames(const std::vector<std::string>& names) {
            for (const std::string& n : names) definitionResolves.set(n, true);
            tableComplete = true;
        }
        bool resolve(const std::string& name) const {
            if (tableComplete && !definitionResolves.known(name)) return false;
            return definitionResolves.get(name);
        }
        OpenValue<bool> active{"0x012ec964 (continuous explosion global state)", "spec-lua-api-behaviour.md Sec28.16"};
        std::string lastDefName, lastTargetName; // CHOSEN, test-observable
        uint64_t startCount = 0;

        // continuous_explosion_stop (Sec22.24, wave 2 batch W2-4): CONFIRMED
        // (reconciled above, 2026-10-01) the SAME one global running flag
        // `active` already models - this native just clears it
        // unconditionally, regardless of any argument (genuinely zero-
        // argument-sensitive per Sec22.24's own text). No new field needed
        // beyond a call count.
        uint64_t stopCount = 0;

        // continuous_explosion_follow_target_add (Sec25.23, wave 2 batch
        // W2-4) - CORRECTED by Sec57's own tranche-23 pass (the liveness
        // check IS made, inside the resolver itself; `+8/+0xc` is a handle
        // pair, not a position). Appends a resolved object's handle (this
        // project's own flat objectResolves()-backed name stands in for the
        // real handle) to a follow-target list capped at 2 entries, ONLY
        // while `active` above is true - a third add silently drops, no
        // duplicate check (both CONFIRMED, Sec57.6/Sec25.23). CHOSEN record
        // shape, kept deliberately SIMPLE (a plain vector of names) so the
        // sibling wave-2 batch that owns continuous_explosion_follow_
        // target_remove (out of THIS batch's scope - different wave-2
        // batch) can extend it later (erase-by-name) without redesigning
        // this list's own shape; this batch does not invent that sibling's
        // own removal semantics.
        static constexpr size_t kFollowTargetCap = 2;
        std::vector<std::string> followTargets;
        uint64_t followTargetAddCount = 0;         // the add actually applied (active + under cap)
        uint64_t followTargetAddDroppedCount = 0;  // inactive OR already at the 2-entry cap
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
        // UPDATE 2026-10-03 (Sec3.7's own single-player resolution, cross-
        // ref Sec6.22): this is the SAME 0x008ae480/0x008837a0 gate pair
        // Sec3.7 settles, so it is CONFIRMED true in single player too -
        // seeded true on first touch in stub_character_set_counter_on_
        // grabbed (lua_spec_confirmed_stubs.cpp), not left OPEN awaiting a
        // test. A test can still .set(name, false) to exercise the now
        // known-dead replicate-only branch for structural coverage.
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

        // --- boss_battle_matt_set_player_as_avatar / boss_battle_matt_
        // hide_wings (Sec30.2, "ranking tranche 03"), batch 2026-10-08
        // (Batch 7, dedicated agent) -----------------------------------

        // Global 0x0268d2d0 bit 0: written UNCONDITIONALLY by set_player_
        // as_avatar per its own input flag (CONFIRMED), regardless of the
        // worker's own outcome.
        bool avatarModeBit = false;
        // The two further gated lookups the worker performs beyond
        // hasLocalPlayer()/playerRig().coopPlayerPresent (both reused from
        // Sec10.3/Sec28.12 above, not duplicated): "npc_bahamut" resolving
        // (become-avatar path) and the model itself resolving after the
        // customization item is applied (a textually SEPARATE failure
        // point per Sec30.2's own 2 sentences: "not found -> false" /
        // "...failure -> false"); "StyleTest_PC" resolving (restore path).
        // This project has no real customization-item/model-resolution
        // table (same "no real data table" gap already established
        // elsewhere, e.g. vehicle_set_sirenlights's per-class allow flag) -
        // OPEN until set.
        OpenValue<bool> npcBahamutResolves{"\"npc_bahamut\" customization item lookup resolves",
                                           "spec-lua-api-behaviour.md Sec30.2"};
        OpenValue<bool> avatarModelResolves{"avatar model resolve (post customization-item apply)",
                                            "spec-lua-api-behaviour.md Sec30.2"};
        OpenValue<bool> styleTestPcResolves{"\"StyleTest_PC\" customization item lookup resolves",
                                            "spec-lua-api-behaviour.md Sec30.2"};
        // Global byte 0x012ec740, host-side only (CONFIRMED gated on
        // coopLocalIsHost()): 1 on a successful become-avatar, 0 on a
        // successful restore. Never written on any other path.
        bool hostAvatarActiveByte = false;
        uint64_t becomeAvatarSuccessCount = 0; // test-observable
        uint64_t restoreSuccessCount = 0;      // test-observable

        // boss_battle_matt_hide_wings: per-entry hide/show of every
        // attached object whose own +0xe0 record hashes to "Avatar_wings"
        // (Sec30.2) - this project has no real attached-object list
        // (same "no generic entity shape invented" boundary), so only the
        // last flag value is tracked, test-observable. Word global
        // 0x012ec742 (Matt's own 16-bit object id, hide + host only, never
        // cleared on show) has no real object-id scheme in this project -
        // only the "was it stored" fact is tracked (same "the write is
        // real, the sub-object is not separately modelled" convention as
        // VehicleState::unjackableSecondarySubObjectFlag).
        bool wingsHidden = false;
        bool wingsHideHostMarkerSet = false;

        // --- boss_battle_matt_qte_begin / boss_battle_matt_set_phase
        // (Sec59.5, wave-2 batch W2-7, 2026-10-08) -----------------------
        //
        // CONFIRMED (Sec59.5): set_phase "stores ANY value with no bound
        // against the table's own 4-entry size and never resets it even
        // when it rejects the value as out-of-range" - so `phase` below is
        // a bare, unclamped int32_t (CHOSEN initial value 1, "phase one" -
        // no construction-time default is stated). HARDENED
        // (host-safety, this project's own choice, not a spec fact): the
        // real engine's own phases 5-8 read whatever garbage sits past the
        // 4-entry name table and phases >= 9 hand a non-pointer number to a
        // string-compare routine and fault - this project has no such
        // adjacent memory to read, so qte_begin below refuses (does not
        // start a QTE) for any phase outside [1,4], counting the refusal
        // via qteBeginOutOfRangePhaseGuardCount rather than fabricating
        // what the garbage name would have been.
        int32_t phase = 1;
        static constexpr int32_t kPhaseTableSize = 4;
        uint64_t qteBeginOutOfRangePhaseGuardCount = 0;
        // HARDENED: real engine reads straight through a null "Matt" object
        // when the resolver's liveness-rejecting result is mishandled
        // (Sec59.5 path 1) - this host instead refuses to start the QTE
        // when "Matt" does not resolve as a live object (objectResolves()),
        // counted here.
        uint64_t qteBeginMattNotLiveGuardCount = 0;
        // HARDENED: real engine writes a completion hook through the
        // literal fixed address 0x1f00 when the player argument doesn't
        // resolve to a proper character (Sec59.5 path 3) - this host
        // refuses instead, counted here.
        uint64_t qteBeginUnresolvedPlayerGuardCount = 0;
        // Matt's OWN QTE-in-progress flag - NOT the same flag the
        // "Killbane QTE-active flag" boss_battle_kb_enable/boss_battle_
        // mars_killbane_active (Sec45.5) share (a DIFFERENT boss fight,
        // Killbane, tracked separately as killbaneQteActive_ below) -
        // kept apart deliberately so these two bosses' own state never
        // gets conflated. CHOSEN false initial (no construction value
        // stated).
        bool qteActive = false;
    };
    BossBattleMatt& bossBattleMatt() { return bossBattleMatt_; }

    // CHOSEN, project-wide "now" stand-in (no real clock/per-frame update
    // loop exists in this host) - a plain, monotonically-incrementing
    // counter, same "unconfirmed-but-plausible counter/handle shape"
    // precedent as nextAudioVoiceHandle() above. Used by character_dont_
    // regenerate's own +0x19c0 restamp (Sec30.1, Batch 7, 2026-10-08).
    int64_t nextMonotonicStamp() { return nextMonotonicStamp_++; }

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
    static int32_t tutorialDescriptorDefaultParam20(int32_t index) {
        // Spec sync e6691f1 (Sec6.19 FINAL): 203 descriptors hold 2; indices 188 and 196-201 hold 1.
        return (index == 188 || (index >= 196 && index <= 201)) ? 1 : 2;
    }
    // Spec sync e6691f1 (Sec33.4): "mission_conv" active slot starts at 0xff (no conversation), so
    // audio_any_conversation_playing is false until a conversation is played. An explicit .set() wins.
    bool missionConvActive() const {
        return conversationChannelActive_.known("mission_conv") ? conversationChannelActive_.get("mission_conv") : false;
    }
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

        // --- satellite_weapon_mode_set (Sec51.5, wave-2 batch W2-7,
        // 2026-10-08): "enable/disable on a per-machine controller
        // singleton for a selectable player mask (local, remote co-op, or
        // both); a remote player's request is simply forwarded to that
        // player's own owning machine. No crash shape found; whether
        // re-enabling an already-active controller is separately guarded
        // is OPEN." Reuses `active` above for the local controller (the
        // SAME 0x0130eee8 singleton satellite_weapon_mode_exit already
        // models) rather than a second flag. The remote-forward half has
        // no local effect to simulate (no second machine in this host) -
        // only the fact that a remote request was forwarded is counted.
        uint64_t localSetCount = 0;
        uint64_t remoteForwardCount = 0;
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

        // party_set_dismissable (Sec22.8, wave 2 batch W2-4): a bare global
        // byte (0x0130af61, CONFIRMED the byte immediately adjacent to the
        // already-documented party_set_recruitable's own 0x0130af60,
        // Sec18.28) - no resolver, no gate. CHOSEN default false (no
        // CONFIRMED construction value stated).
        bool dismissable = false;

        // The 4 reserved "#FOLLOWER...#" name-resolution sentinels (spec-
        // lua-api-behaviour.md Sec3 front matter, line ~1469-1470, "Name-
        // resolution sentinels" - the 2nd-5th of the 8-sentinel CLOSED
        // inventory commit 1681666 establishes): CONFIRMED "checked by
        // `0x00a26010`, resolved through `0x00a26110` to the Nth entry of a
        // linked companion/homie list" - but the spec text never pins which
        // N each of the 4 literals maps to, and an exhaustive grep of every
        // spec-*.md for "FOLLOWER"/"0x00a26010"/"0x00a26110" (2026-10-08,
        // this field's own introduction) found no section that closes
        // either of two genuinely separate open questions:
        //
        //   (a) whether the BARE "#FOLLOWER#" literal shares the SAME
        //       resolved index as "#FOLLOWER1#" (an alias) or denotes a
        //       distinct "any/first live follower" case - Sec3's own front
        //       matter just lists all four side by side with no textual
        //       distinction given for the bare form;
        //
        //   (b) whether the numbered trio is 0- or 1-indexed into the list
        //       (does "#FOLLOWER1#" mean list entry [0] or [1]?) - this is
        //       the SAME pair repeatedly flagged, by name, as "the exact
        //       group-vs-character semantics of `0x00a26010`/`0x00a26110`"
        //       every time this document cites it (Sec14.26
        //       `set_cant_flee_flag`'s own side-effects line; a cross-
        //       function open-questions list elsewhere in the same tranche,
        //       item 5; Sec20.23 `get_team`'s own 4-stage-cascade writeup)
        //       without ever being closed by a later section, even after
        //       the 2026-10-08 sentinel-inventory closure pass (which closed
        //       WHICH 8 strings exist, not what each one indexes to).
        //
        // Per this project's own "no invented fixes" house rule, NEITHER
        // question is decided here with a guessed formula (e.g. "numbered
        // literals are 1-indexed, bare means index 0" would be exactly as
        // plausible, and exactly as unconfirmed, as the opposite choice).
        // Instead each of the 4 literals gets its OWN independent OpenValue
        // slot, keyed by the exact literal string - deriving #FOLLOWER2#/
        // #FOLLOWER3# as some fixed offset from #FOLLOWER1#, or aliasing
        // bare #FOLLOWER# to #FOLLOWER1#, would itself be inventing the
        // very relationship (a)/(b) leave open. A slot stays OPEN (throws
        // OpenStateError, naming the literal, via OpenValueMap<int>::get())
        // until a test - or a future spec update - sets it explicitly;
        // resolveFollowerAwareObject() (lua_spec_confirmed_stubs_common.h)
        // never defaults one.
        //
        // Backing list: CHOSEN as this SAME struct's own `membersByLeader`
        // (party_add_do, Sec32.4), keyed by this project's single-player
        // leader literal "#PLAYER1#" - HIGH CONFIDENCE, NOT independently
        // spec-confirmed to be the SAME physical list `0x00a26010`/
        // `0x00a26110` walk (the spec's own words are just "a linked
        // companion/homie list," never cross-referenced by address/offset
        // to party_add_do's own fixed 5-slot array - a genuinely separate
        // OPEN identity question from (a)/(b) above, documented here rather
        // than silently assumed). The reuse follows this project's OWN
        // existing precedent, not a new assumption invented for this field:
        // lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp's own
        // `isFollowingALeader()` already treats `membersByLeader` generically
        // as "is this character a follower of anyone" for the Sec38.1 "enter
        // flips the follower flag" side effect, not narrowly scoped to
        // `party_add_do`'s own text either.
        OpenValueMap<int> followerSentinelIndex{"follower sentinel index",
                                                 "spec-lua-api-behaviour.md Sec3 front matter (line ~1469-1470)"};
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

        // helicopter_enter_retreat (Sec17.27), wave 2 batch W2-2
        // (2026-10-08): the SAME heli-specific sub-object (0x00a79470)
        // HelicopterFlyTo::applyGate above already cites, here testing a
        // DIFFERENT condition on its own +0x4b0 mode enum (!= 6, not == 1) -
        // kept as its own OPEN field rather than overloading applyGate's
        // own established "== 1" semantics. retreatTransitionCount only
        // increments when the mode enum is confirmed != 6 (CONFIRMED: the
        // whole clear/transition/notify/height-check/cleanup chain is
        // gated on that one test).
        OpenValue<int32_t> subObjectModeEnum{"heli sub-object (0x00a79470) mode enum +0x4b0",
                                             "spec-lua-api-behaviour.md Sec17.27"};
        uint64_t retreatTransitionCount = 0; // 0x00b350a0(vehicle, 5, 0) fired
        bool field158Cleared = false;
        // The height check (0x00b41670 vs the fixed constant 0x012a2d70) -
        // no real position/geometry data in scope (same stated project-wide
        // gap as vehicle_chase's own spotlight check) - OPEN until a test
        // sets it.
        OpenValue<bool> heightCheckTrips{"helicopter retreat height check (0x00b41670 vs 0x012a2d70)",
                                         "spec-lua-api-behaviour.md Sec17.27"};
        bool heightTripFlag268 = false;
        int64_t retreatDeadlineStamp = 0; // 0 = never scheduled; else EngineState::nextMonotonicStamp()+1500
        uint64_t retreatReplicateCount = 0;
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
    //
    // UPDATE (Batch W2-5, 2026-10-08, wave 2 fan-out, spec-lua-api-
    // behaviour.md Sec52.1): Sec52.1's own headline finding confirms this
    // batch's own guess above was right in spirit - "only 2 of the 9
    // vcust_* names in [tranche 15] actually check that a live
    // 'customization vehicle' exists before using it ... a binding layer
    // should simply treat every name in this family as 'requires a live
    // customization vehicle' and refuse up front." `targetLive` is
    // therefore now the SHARED gate every vcust_* stub in
    // lua_spec_confirmed_stubs_b25_wave2_vcust_store_garage.cpp checks
    // before touching any of this struct's (or its own) further fields -
    // not re-derived as a second, parallel flag. Its own default (false)
    // and reasoning are UNCHANGED from the original note above.
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

        // --- Batch W2-5 additions below (2026-10-08): per-name HARDENING
        // refusal counters (HOST-SAFETY, test-observable only - not real
        // engine fields) for every further vcust_* name this wave-2 batch
        // implements against the SAME targetLive gate above. See each
        // stub_vcust_*'s own doc comment (b25 file) for which counter it
        // increments and its own spec citation. ---
        uint64_t previewColorNoTargetRefusedCount = 0;        // vcust_preview_color, Sec38.5
        uint64_t previewPaletteNoTargetRefusedCount = 0;      // vcust_preview_palette, Sec38.5
        // vcust_preview_palette's own test-populated "does this (zone,
        // palette) pair resolve" registry (no real per-zone palette-list
        // loader in scope) - keyed by "zone:palette" string.
        OpenValueMap<bool> zonePaletteCount{"vcust_preview_palette's own per-zone palette-list resolve",
                                            "spec-lua-api-behaviour.md Sec38.5"};
        uint64_t previewPaletteOobRefusedCount = 0;           // vcust_preview_palette's own unchecked zone/palette/colour indices, Sec38.5
        uint64_t purchaseWheelsNoTargetRefusedCount = 0;      // vcust_purchase_wheels, Sec38.5
        uint64_t adjustCameraAngleNoTargetRefusedCount = 0;   // vcust_adjust_camera_angle, Sec52.1
        uint64_t previewRimSizingNoTargetRefusedCount = 0;    // vcust_preview_rim_sizing, Sec52.1
        uint64_t previewRimSizingOobRefusedCount = 0;         // vcust_preview_rim_sizing's own unchecked combined size+style index, Sec52.1
        uint64_t purchaseComponentNoTargetRefusedCount = 0;   // vcust_purchase_component, Sec52.1
        uint64_t purchaseComponentStaleIndexRefusedCount = 0; // vcust_purchase_component's own named OOB finding, Sec52.1 (see selectedComponentItem below)
        uint64_t purchasePaintNoTargetRefusedCount = 0;       // vcust_purchase_paint, Sec52.1
        uint64_t purchasePaintNegativePriceRefusedCount = 0;  // vcust_purchase_paint's own "no price validation at all", Sec52.1
        uint64_t purchasePerformanceUpgradeNoTargetRefusedCount = 0; // vcust_purchase_performance_upgrade, Sec52.1
        uint64_t selectPaintSlotNoTargetRefusedCount = 0;     // vcust_select_paint_slot, Sec52.1
        uint64_t selectPaintSlotOobRefusedCount = 0;          // vcust_select_paint_slot's own unchecked slot index, Sec52.1
        uint64_t setUnderglowOobRefusedCount = 0;             // vcust_set_underglow's own unchecked palette-colour index, Sec52.1 (no target-gate per spec text - applies colour BEFORE charging either way)
        uint64_t showNosHydraulicWarningNoTargetRefusedCount = 0; // vcust_show_nos_hydraulic_warning, Sec52.1

        // vcust_purchase_component (Sec52.1 MEMSAFE - the batch's own named
        // finding): "a globally-tracked 'currently selected item' index...
        // file-initialized to -1 and is reset back to -1 on every exit from
        // this function - so calling it twice in a row, or calling it at
        // all before a fresh selection, computes an 'item' pointer 60 bytes
        // before the real item array [and can] charge[] that garbage price
        // to the player and then write[] a bit into the dword immediately
        // preceding the array - a real heap-corruption shape." HARDENED
        // here: modelled as a test-populated resolver (no raw array this
        // project sizes) - selectedComponentItemIndex is this project's own
        // mirror of the real stale/-1-reset global (CONFIRMED shape: -1 at
        // rest, and reset to -1 after every call below, win or lose);
        // selectedComponentItemPrice is populated ONLY by a prior, still-
        // fresh selection (no in-scope producer sets one - a test calls
        // EngineState::selectVcustComponentForTesting() to simulate "a
        // selection was just made"). A read against an unresolved/-1 index
        // is refused (no charge, no write) - exactly covering the real
        // bug's own two trigger conditions (double-call, or no prior
        // selection) without reproducing the OOB read+write itself.
        int32_t selectedComponentItemIndex = -1;
        OpenValueMap<double> selectedComponentItemPrice{
            "vcust_purchase_component's own 'currently selected item' price field (60 bytes before the real array on a stale/-1 index)",
            "spec-lua-api-behaviour.md Sec52.1"};
        uint64_t purchaseComponentChargedCount = 0;
        double purchaseComponentTotalCharged = 0.0;

        // vcust_change_top_level_menu (Sec52.1 CRASH - "a new 'poisons its
        // own next call' shape"): "An out-of-range absolute selection
        // stores null into the 'current top-level node' global
        // unconditionally - and the very next relative (+/-1) call
        // dereferences that same global with no check at all." HARDENED:
        // an out-of-range absolute selection REFUSES the store (leaves the
        // prior node alone) rather than poisoning it with null; a relative
        // call with no current node REFUSES rather than dereferencing it.
        OpenValue<int32_t> currentTopLevelNode{"vcust_change_top_level_menu's own 'current top-level node' global",
                                                "spec-lua-api-behaviour.md Sec52.1"};
        uint64_t changeMenuAbsoluteCount = 0;
        uint64_t changeMenuRelativeCount = 0;
        uint64_t changeMenuAbsoluteOutOfRangeRefusedCount = 0;   // HARDENED (named divergence)
        uint64_t changeMenuRelativeNoNodeRefusedCount = 0;       // HARDENED (named divergence)

        // vcust_preview_component (Sec19.11) / vcust_revert_color (Sec19.10)
        // / vcust_is_camera_tool_mode (Sec26.16): no CRASH flagged for these
        // 3 in scope - plain CHOSEN/OPEN state, same discipline as every
        // other field in this struct.
        int32_t previewedComponentIndex = -1;
        uint64_t previewComponentMode0Count = 0;
        uint64_t previewComponentMode2Count = 0;
        uint64_t previewComponentUnhandledModeCount = 0;
        double currentColorValue = 0.0;
        double originalColorValue = 0.0;
        bool colorRevertGateClear = true; // 0x022cf90b "gate flag clear" - CHOSEN default matching file-zero-fill, Sec19.10
        uint64_t revertColorCount = 0;
        OpenValue<bool> cameraToolMode{"vcust_is_camera_tool_mode backing accessor (0x006948c0)",
                                       "spec-lua-api-behaviour.md Sec26.16"};

        // --- Further per-name state the b25 stubs read/write directly
        // (HOST-SAFETY/CHOSEN fields, test-observable only - see each
        // stub_vcust_*'s own doc comment for citation). ---
        bool wheelPreviewCommitted = false;        // vcust_purchase_wheels: CONFIRMED "commits regardless of charge outcome"
        double cameraAngle = 0.0;                  // vcust_adjust_camera_angle
        uint64_t adjustCameraAngleCount = 0;
        int32_t rimSizingIndex = -1;                // vcust_preview_rim_sizing
        uint64_t previewRimSizingCount = 0;
        double paintTotalCharged = 0.0;             // vcust_purchase_paint
        uint64_t purchasePaintCount = 0;
        double performanceUpgradeTotalCharged = 0.0; // vcust_purchase_performance_upgrade
        uint64_t purchasePerformanceUpgradeCount = 0;
        int32_t selectedPaintSlot = -1;              // vcust_select_paint_slot
        uint64_t selectPaintSlotCount = 0;
        int32_t underglowColorIndex = -1;            // vcust_set_underglow
        uint64_t setUnderglowCount = 0;
        uint64_t showNosHydraulicWarningCount = 0;   // vcust_show_nos_hydraulic_warning
    };
    VcustPreview& vcustPreview() { return vcustPreview_; }
    void recordVcustPreviewWheelSizingNullTargetGuard() { ++vcustPreview_.wheelSizingNullTargetGuardCount; }
    // Test-only hook for vcust_purchase_component's own stale-selection
    // HARDENING (see VcustPreview::selectedComponentItemIndex/Price above):
    // simulates "a fresh selection was just made" so a test can exercise
    // BOTH the valid-input faithful path (price charged) and, by NOT
    // calling this (or calling vcust_purchase_component twice), the
    // hardened-refusal path (the real engine's own -1-reset trigger).
    void selectVcustComponentForTesting(int32_t index, double price) {
        vcustPreview_.selectedComponentItemIndex = index;
        vcustPreview_.selectedComponentItemPrice.set(std::to_string(index), price);
    }

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

    // New-game-with-no-save starting cash, raw units (cash x100, matching
    // the same x100 scale as cash_add/playerCash() elsewhere and the
    // save-file field's own encoding, spec-save-format.md Sec10.3/§10.1).
    // spec-frontend-menu-runtime.md Sec9 item 1: the real engine's own
    // write-site for this default is NOT confirmed - two independent search
    // passes (data: m01.lua/player_presets.xtbl/xtbl corpus census; engine:
    // both cash-setter layers' full caller lists + the player-record
    // constructor chain down to its placement constructor) came up empty.
    // This is Team B's CHOSEN application point (EngineState's own default-
    // constructed value, used whenever no save snapshot gets applied via
    // applySaveSnapshotFields()/lua_save_game_bridge.cpp, i.e. the "new
    // game, no save present" case this project already distinguishes from
    // "continue"), not a confirmed reproduction of the real engine's
    // mechanism. Value: 5700 raw (= $57.00), spec-frontend-menu-runtime.md
    // Sec9 item 1 / spec-save-format.md line ~736 ("new game 5,700 (=
    // 57.00)").
    static constexpr double kNewGameStartingCashCents = 5700.0;

    // New-game-with-no-save auto-granted unlocks (spec-frontend-menu-
    // runtime.md Sec9 item 1, CLOSED 2026-10-11): the real base
    // unlockables.xtbl has exactly ONE row with <Auto_Unlock> set at all
    // (HomieSlot1, Category=Homies) - the same population spec-save-
    // format.md Sec10.6's own empirical finding gives for the new-game
    // save's state set A (purchased/active): exactly 1 bit. HIGH
    // CONFIDENCE, not disassembly-confirmed end-to-end (the exact runtime
    // function that reads the parsed <Auto_Unlock> enum at +0xF4 and sets
    // the +0xE0/+0xE1 state bits accordingly was not located this pass -
    // see the spec's own "Unknowns" note). Team B's CHOSEN application
    // point (same status as kNewGameStartingCashCents just above): scans
    // the CALLER-SUPPLIED parsed unlockables table for every row with
    // autoUnlock != None and marks each one purchased, by name (no
    // confirmed hash-to-bit-index mapping exists to do otherwise) - this
    // is deliberately data-driven, NOT a hardcoded "HomieSlot1" string, so
    // it stays correct if the shipped unlockables.xtbl data ever changes.
    std::unordered_set<std::string>& newGamePurchasedUnlocks() { return newGamePurchasedUnlocks_; }
    const std::unordered_set<std::string>& newGamePurchasedUnlocks() const { return newGamePurchasedUnlocks_; }
    // Returns the set of unlock names just granted (for callers/tests that
    // want the data-driven result without re-scanning the table
    // themselves). Additive - does not clear any previously-granted name.
    const std::unordered_set<std::string>& grantNewGameAutoUnlocks(
        const std::vector<sr3tables_progression::Unlockable>& unlockables) {
        for (const auto& u : unlockables) {
            if (u.autoUnlock != sr3tables_progression::UnlockableAutoUnlock::None) {
                newGamePurchasedUnlocks_.insert(u.name);
            }
        }
        return newGamePurchasedUnlocks_;
    }

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

        // store_common_mouse_zoom (Sec11.9, 0x0080dcd0, batch 3, 2026-10-08):
        // CONFIRMED to scale the SAME 3rd snapshot value this struct's own
        // lastDeltaX/Y/Z above already stand in for (0.0, CHOSEN - "this
        // project has no real mouse-delta-capture layer anywhere in scope,"
        // see this struct's own doc comment above) by a fixed -0.05
        // multiplier and the optional Lua argument, then accumulates into a
        // single global pending camera-zoom delta (0x00567c00). Under this
        // project's own CHOSEN 0 stand-in for the unavailable delta, the
        // accumulator's real numeric contribution is always 0 - tracked
        // anyway (rather than omitted) so a test can observe that the write
        // path itself genuinely ran, consistent with this project's
        // "accumulate even when the real contribution is a stated 0"
        // convention (cf. debrisFlowRecycleCount_ elsewhere in this header).
        double pendingZoomDelta = 0.0;
        uint64_t zoomCallCount = 0;
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
    // ce4556f Sec29 (m19 brutes): also flips any placed Script_Npc member registered as "pending group spawn" to resolvable.
    void markGroupCreated(const std::string& name);

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
    //
    // No longer purely a query (2026-10-11, the m07 end-of-cutscene-
    // placement task): when the done-check reports true, it now also fires
    // cutscenePlayApplyEndPlacement() below (idempotent per destination
    // write - see cutscenePlayPlacementApplied_'s own doc comment) -
    // non-const for exactly this reason.
    bool cutscenePlayCheckDone() {
        const bool done = !(cutsceneManager_.present.known() && cutsceneManager_.present.get());
        if (done) cutscenePlayApplyEndPlacement();
        return done;
    }

    // cutscenePlayApplyEndPlacement() - CHOSEN stand-in, spec-lua-api-
    // behaviour.md Sec57.2a's 2026-10-11 "new finding" note (three-pass
    // investigation, orchestrator-routed for `m07`): the real engine's
    // non-skip ("a manager exists") branch writes `cutscene_play`'s two
    // resolved destinations into the cutscene manager's own fields
    // (`+0x35f0`-`+0x362c` local, `+0x3630`-`+0x367c` partner), but an
    // exhaustive 3-pass search (the full 20-state cutscene state machine,
    // the `.cte_xtbl` RestorePlayerPos reader, and a 91-function xref sweep
    // of the manager-pointer global) found NO consumer anywhere that reads
    // those bytes back and commits them to the player. Per this project's
    // own negative-result rule, that is read as "there is a consumer we
    // have not found yet," not "shipped missions never actually get
    // placed" - `m07.lua`'s own `cutscene_play` call sites never follow up
    // with a redundant `teleport_coop`, showing the shipped SCRIPT trusts
    // the engine to place the player by itself.
    //
    // This host has no manager allocation at all (cutscenePlayCheckDone()'s
    // own doc comment above), so it can never observe which of the two real
    // branches "really" applies - but the ONE branch that IS fully, End-to-
    // end disassembly-confirmed is the `skip_all_cutscenes` one: it resolves
    // both destination names through the SAME by-name resolver (`0x005982e0`)
    // `teleport` itself uses, then commits via the SAME real player-class
    // async-teleport entry point (`0x009e2220`, the generic vtable `+0x40`
    // transform-setter) `teleport`'s own stub now drives (see stub_teleport,
    // lua_spec_confirmed_stubs_b08_batch1_core.cpp, commit b209dd3). CHOSEN
    // here: apply that SAME proven mechanism universally - on cutscene
    // completion (cutscenePlayCheckDone() reporting true) exactly as on an
    // explicit skip - rather than block indefinitely on an unresolved real
    // consumer, or invent a different, unproven one. This is NOT a claim
    // that the real non-skip path works this way; it is the most
    // architecturally defensible stand-in available (same destination
    // names, same resolver, same commit primitive already proven correct
    // for the skip case), used here for BOTH players: the local player
    // (`cutscenePlayDestination1()`, "#PLAYER1#") and the co-op partner
    // (`cutscenePlayDestination2()`, "#PLAYER2#") - matching arg 3's own
    // documented "exactly two destination names" shape (local + partner).
    // Position-only (no orientation commit): this host has never modeled
    // an orientation field for any character - the SAME scope limit
    // stub_teleport's own real position write (b209dd3) already accepted
    // for its identical `+0x4c` orientation half.
    void cutscenePlayApplyEndPlacement();

    // --- cutscene_play_do's real camera-script pipeline (spec-lua-api-
    // behaviour.md Sec57.2a / spec-cutscene-camera-format.md Sec10.6 item 7,
    // 2026-10-08 wiring task) ------------------------------------------------
    //
    // REAL, Sec57.2a: cutscene_play_do's engine entry 0x00725df0 starts the
    // 20-state cutscene machine (0x0072d660); one state handler
    // (0x0072bff0) is the sole caller of the .csc_pc load driver
    // (0x00737290), which is gated by the kind test 0x00721bc0 before
    // calling the parser 0x00569290 (this project's sr3cutscene::
    // camera_script, already implemented and tested from spec-cutscene-
    // camera-format.md Sec10 directly). REAL, spec-cutscene-camera-
    // format.md Sec10.6 item 7: the kind test "tests ... whether that
    // entry's kind field ... equals 0 (Designer)" - ONLY Designer-kind
    // cutscenes skip the camera-script load; Zscene (kind 1) and Story
    // (kind 2) both require and load one, and "a missing or unparsable
    // script aborts the cutscene load" (a REAL, defined abort, not a crash
    // and not an OPEN unknown).
    //
    // This host has no manager-allocation / state-machine timing for this
    // step (cutsceneManager_.present is never allocated here - same "this
    // host never allocates X" scope limit cutscenePlayCheckDone() already
    // documents above), so it is modeled as its own directly-callable step
    // (cutsceneCameraScriptStep(), private, below), run synchronously from
    // cutscenePlayDo() on every actual start (never on a refused one, same
    // as the existing zscene-gate call it sits next to) - NOT folded into
    // cutsceneMachineStep()'s case 7 above, because exactly how 0x0072bff0's
    // own success/failure feeds the NEXT state transition is still OPEN
    // (case 8's own helpers, 0x00720200/0x0072c260) and this task's own
    // scope is the load pipeline itself, not a guess at that transition.
    //
    // The actual byte source for a named .csc_pc is NOT built into this
    // class - same convention as loadVintDocument/loadZoneObjectsSection
    // (both take an ALREADY-PARSED object), except that cutscene_play_do's
    // Lua wrapper has no per-call opportunity to hand one in (Sec57.2: it
    // only takes a name, the read-and-discarded 2nd argument, and a
    // destination table), so the opener is installed ONCE, like
    // installZsceneTable, by whoever wires this host to a real cache
    // (tools/lua_host_run.cpp) or a test - mirrors sr3tables_cutscene's own
    // SceneFileOpener shape exactly (include/sr3tables_cutscene/
    // scene_table.h), one level further down the same real pipeline.
    using CameraScriptOpener = std::function<std::optional<std::vector<uint8_t>>(const std::string& csFileName)>;
    void installCameraScriptOpener(CameraScriptOpener opener) { cameraScriptOpener_ = std::move(opener); }

    enum class CutsceneCameraScriptOutcome {
        NotAttempted,        // cutscenePlayDo has never actually started a cutscene yet
        SkippedDesignerKind, // REAL: kind 0, the 0x00721bc0 gate - the load is never attempted
        SkippedKindUnknown,  // CHOSEN (conservative default): kind not determinable (no scene
                             // table installed, or the name resolves to no entry) - never guess
                             // whether a script is needed when this host cannot tell
        Aborted,             // REAL: kind 1/2 but no bytes could be opened/parsed for this name
                             // ("a missing or unparsable script aborts the cutscene load")
        Loaded,              // REAL: kind 1/2, a real .csc_pc was opened and parsed
    };
    CutsceneCameraScriptOutcome cutsceneCameraScriptOutcome() const { return cutsceneCameraScriptOutcome_; }
    // Counts actual INVOCATIONS of the installed opener - 0 for
    // SkippedDesignerKind/SkippedKindUnknown proves the load was genuinely
    // never attempted, not merely that its result was discarded (the
    // assertion instruction #4/#5 of this task's own brief asks for
    // exactly this: "confirm no .csc_pc read was attempted").
    uint64_t cutsceneCameraScriptOpenerCalls() const { return cutsceneCameraScriptOpenerCalls_; }
    // non-null only when cutsceneCameraScriptOutcome() == Loaded.
    const sr3cutscene::CameraScript* cutsceneActiveCameraScript() const {
        return cutsceneCameraScript_ ? &*cutsceneCameraScript_ : nullptr;
    }
    // sr3cutscene::CameraScript::sample(t) (the EXISTING real sampler,
    // spec-cutscene-camera-format.md Sec10.3/Sec10.6) on the active script.
    // Throws std::logic_error if none is loaded - a direct, immediate C++
    // error rather than OpenStateError: WHICH of the REAL/CHOSEN reasons
    // above applies is already known via cutsceneCameraScriptOutcome() by
    // the time this is callable, so there is no OPEN fact left to report.
    sr3cutscene::CameraSample sampleCutsceneCamera(double t) const;

    // --- action_play_do / action_play_is_finished (spec-lua-api-behaviour.md
    // Sec22.29/Sec7.21), real-hit probe-mode batch (2026-10-03): see
    // lua_spec_confirmed_stubs.cpp's own doc comment above stub_action_play_do
    // for the full design and what this closes/leaves OPEN. ---------------

    // Sec22.29: "returns the FIXED sentinel -1.0 ... the SAME 0x012a3038
    // constant effect_play/effect_play_on_script_object (Sec9.1/Sec12.10)
    // already establish for their own failure case." Neither effect_play
    // nor effect_play_on_script_object is implemented anywhere in this
    // codebase yet (grepped directly - no stub_effect_play/
    // stub_effect_play_on_script_object exist), so this constant has no
    // prior shared home to reuse; defined here, named for a future
    // effect_play pass to reuse instead of re-deriving.
    static constexpr double kActionPlaySentinelResolveFail = -1.0;
    // Sec22.29: "a bare -1 ... a genuinely distinct sentinel from the
    // resolve-failure case above (a plain literal -1 here vs. the named
    // 0x012a3038 constant there - both textually '-1' but sourced from
    // different code paths)." Textually identical to -1.0 as a Lua number;
    // kept as its own named constant (not reused) to mirror that spec
    // distinction and so a test can tell which branch actually ran via the
    // counters below, not just the pushed value.
    static constexpr double kActionPlaySentinelDeadNotForced = -1.0;

    // Sec22.29's action-id lookup (0x004bf810, the animation-name->id
    // table) has ZERO confirmed entries anywhere in this project's spec -
    // the same boundary Sec29's named-object map documents for Killbane
    // ("producer (3)'s 173 call sites were never individually traced,
    // could register essentially any name"), here for animation names
    // instead of object names. This project therefore has no real table
    // data to look an action name up against, so every lookup takes the
    // "not found" path - the SAME "honest behavior of a never-populated
    // registry, not a simplification of the return contract" this project
    // already established for team_make_unfriendly's team-name registry
    // (Sec30.6, lua_spec_confirmed_stubs.cpp). Per Sec22.29's own AND
    // logic ("the final return code is 1 only if BOTH the action-lookup
    // succeeded earlier AND this dispatch itself succeeded"), a failed
    // lookup alone already forces a non-1 return regardless of what the
    // still-OPEN dispatch (0x00a275e0) would have done - so this project
    // never needs to know 0x00a275e0's own internal result to answer this
    // honestly. The EXACT non-1 numeric encoding is not itself given by
    // Sec22.29 (only the two CONFIRMED sentinels above are spelled out) -
    // CHOSEN here as 0, this document's own common boolean-as-number
    // "false" idiom used throughout (e.g. mode arguments elsewhere in
    // spec-lua-api-behaviour.md), NOT a claimed real value.
    static constexpr double kActionPlaySentinelLookupMiss = 0.0;

    int actionPlayDeadGuardCount() const { return actionPlayDeadGuardCount_; }
    void recordActionPlayDeadGuard() { ++actionPlayDeadGuardCount_; }
    int actionPlayLookupMissCount() const { return actionPlayLookupMissCount_; }
    void recordActionPlayLookupMiss() { ++actionPlayLookupMissCount_; }

    // action_play_is_finished (Sec7.21): CONFIRMED structure - "if resolved
    // AND a zero-argument context check (0x00487e80) succeeds (result !=
    // -1) AND the object's action-slot value ... is still below the
    // threshold argument, returns false. Otherwise returns true[, +
    // conditional notify]." The "context check" queries whether the
    // character's animation controller currently has an active sub-state -
    // something only action_play_do's own OPEN internal dispatch
    // (0x00a275e0, Sec22.29) could ever establish, and this project does
    // not model those internals (see stub_action_play_do's own doc comment
    // and the lookup-miss reasoning just above). Nothing in this project's
    // scope can therefore ever make the context check report anything
    // other than "no active state" - the SAME "this host never allocates
    // X, so it honestly and permanently reproduces the 'reports done
    // prematurely' [structural branch]" reasoning cutscenePlayCheckDone()
    // above already documents, here applied to a different CONFIRMED
    // branch of a different function. Both the "character resolves" and
    // "character doesn't resolve" paths converge on this SAME "otherwise:
    // true" branch in this project's scope (Sec22.29's own text: "if
    // resolved AND ... Otherwise returns true" - not-resolved is one of
    // the ways the AND is false), so resolving the character is not
    // needed to answer honestly here. The conditional notify
    // (0x009e3200(0,0)) has no Lua-visible effect anywhere in this
    // project's scope (an internal engine call, not a further native) and
    // so is not separately modeled, same as the unobservable internals
    // this file already declines to fabricate elsewhere (e.g.
    // cutscenePlayDo's own uninitialized script-group-handle array).
    bool actionPlayIsFinished() const { return true; }

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

    // --- Native-implementation batch 2026-10-07 (fresh mission-drive stub-
    // hit ranking, cross-referenced against spec coverage) - see each
    // accessor's own doc comment and lua_spec_confirmed_stubs.cpp's own
    // batch header comment for the full per-function citation/reasoning. ---

    // audio_object_post_event (Sec2.4, individually executable-re-derived
    // and cleared by Sec60.3 despite sitting in the manager's own Sec1-5
    // "not yet cleared for implementation" freeze - Sec60.3's own text:
    // "re-verified with their prior OPEN items closed... no corrections
    // needed beyond that"). This project models no real Wwise ID table
    // anywhere (same stated gap game_audio_get_audio_id/Sec8.22 already
    // documents) - rather than throw on every real call (which would
    // regress every mission that currently passes this call silently,
    // unlike game_audio_get_audio_id whose whole job IS the id), this
    // follows game_UI_audio_play's own precedent (Sec2.2): treat every
    // event name as "resolves" and hand back a fresh instance handle from
    // the SAME shared counter, rather than modelling Wwise resolution
    // failure at all. CHOSEN simplification, not a guess at a specific
    // Wwise id.
    int64_t nextEffectPostHandle() { return nextAudioVoiceHandle(); }

    // effect_play/explosion_create (Sec9.1/Sec9.3, 2026-10-07 batch): both
    // CONFIRMED to share one finalize-then-push pair (0x005cf190 then
    // 0x00a48af0) for their own success return, a packed double-precision
    // "instance identifier" this project does not reconstruct (the real
    // packing format is not independently traced). A fresh, monotonically-
    // increasing counter stands in, same "CHOSEN simplification, not a
    // guess at a specific real id" class as nextAudioVoiceHandle() above -
    // kept as its OWN counter, deliberately NOT nextAudioVoiceHandle()/
    // nextEffectPostHandle() (both reserved for the audio/Wwise-post
    // handle namespace), since a visual-effect instance id and an audio
    // post id are two different real namespaces and sharing one counter
    // would conflate them for no reason.
    int64_t nextVisualEffectHandle() { return nextVisualEffectHandle_++; }

    // character_is_ready (Sec7.27) / character_playing_combat_move
    // (Sec25.27). Both OPEN until set - no spec gives a default, and unlike
    // CharacterState::isAlive (0x0096f4f0, already seeded at construction
    // per Sec34.5's own zero-filled-allocation reasoning), neither of these
    // predicates has any stated construction-time value anywhere in scope.
    OpenValueMap<bool>& characterReadyBlocked() { return characterReadyBlocked_; }
    OpenValueMap<bool>& characterPlayingCombatMove() { return characterPlayingCombatMove_; }

    // audio_persona_line_playing (Sec8.4, 2026-10-07 batch): resolves via
    // the SAME shared objectResolves() map audio_play_persona_line
    // (Sec2.5) also uses; CONFIRMED a null/unresolved handle reads as
    // false "by convention" (not an error), so only the RESOLVED case's
    // own "is it actually playing" predicate is genuinely OPEN until a
    // test sets it (0x0094c9e0/0x0046afd0, neither decompiled).
    OpenValueMap<bool>& personaLinePlaying() { return personaLinePlaying_; }

    // audio_is_playing/audio_stop (Sec8.3/Sec8.9, 2026-10-07 batch): both
    // key off the SAME fixed 32-slot audio-instance table - this project
    // builds no registry for it (no in-scope function ever marks a
    // numeric id "live"), so the honest, CONFIRMED-by-construction answer
    // for every id is "not playing" (the same "a registry this project
    // deliberately does not build is itself the confirmed fallback"
    // reasoning vehicle_pathfind_check_done/group_create_check_done
    // already establish) - a plain default-false map, not OpenValueMap,
    // since there is no separate "resolve" step for a bare numeric id to
    // genuinely refuse on.
    bool audioPlaying(int id) const {
        auto it = audioPlaying_.find(id);
        return it != audioPlaying_.end() && it->second;
    }
    void setAudioPlaying(int id, bool playing) { audioPlaying_[id] = playing; }

    // door_close/door_lock/mesh_mover_hide/mesh_mover_show/mesh_mover_reset/
    // mesh_mover_set_invulnerable (Sec9.12/9.14/9.15/9.16/9.17/14.9) - all
    // resolved through the same "named placed-object" resolver (0x006434a0)
    // Sec9.12 itself observes is "a generic named placed-object registry
    // rather than a door-specific one," shared across doors and
    // mesh-movers. Gated on the SAME shared objectResolves() map every
    // other object kind in this project's scope already uses (Sec29's "one
    // shared global name map" finding - VehicleState's own top note already
    // applies the identical precedent to vehicles) - no separate resolver
    // map invented for this family.
    PlacedObjectState& getOrCreatePlacedObject(const std::string& name) { return placedObjects_[name]; }
    const PlacedObjectState* findPlacedObject(const std::string& name) const {
        auto it = placedObjects_.find(name);
        return it != placedObjects_.end() ? &it->second : nullptr;
    }

    // city_zone_swap (Sec1.9, individually executable-re-derived and
    // cleared by Sec60.3). CONFIRMED: something happens to queue, or (under
    // a mission-active+name-match gate this project does not model, Sec1.9's
    // own remaining OPEN item) immediately apply, a zone-pair swap keyed by
    // name and the boolean - neither path has any Lua-visible return value
    // or other project-tracked side effect to distinguish, so which
    // internal path runs does not change this project's own observable
    // contract. Recorded for test-observability only.
    void recordCityZoneSwap(const std::string& name, bool value) { cityZoneSwaps_[name] = value; }
    bool cityZoneSwapKnown(const std::string& name) const { return cityZoneSwaps_.count(name) != 0; }
    bool cityZoneSwapValue(const std::string& name) const {
        auto it = cityZoneSwaps_.find(name);
        return it != cityZoneSwaps_.end() && it->second;
    }

    // traffic_disable_lanes (Sec12.19, individually cleared by Sec60.3).
    // CONFIRMED: resolved via the shared trigger/object resolver, then
    // enables/disables a named trigger volume of traffic lanes (the exact
    // internal dispatch, 0x00a6dd70, is OPEN beyond "shared with
    // traffic_disable_drawbridge_area, Sec6.4" - not modelled further).
    void setTrafficLanesEnabled(const std::string& name, bool enabled) { trafficLanesEnabled_[name] = enabled; }
    bool trafficLanesKnown(const std::string& name) const { return trafficLanesEnabled_.count(name) != 0; }
    bool trafficLanesEnabledValue(const std::string& name) const {
        auto it = trafficLanesEnabled_.find(name);
        return it != trafficLanesEnabled_.end() && it->second;
    }

    // party_dismiss_all (Sec14.17, individually cleared by Sec60.3) - a
    // genuine zero-argument command, dismisses the local player's own
    // party/companion roster (both representations when a co-op session is
    // present, per the SAME 0x009df3d0 check satellite_weapon_mode_exit/
    // Sec32.1 already reuses - st->playerRig().coopPlayerPresent). This
    // project has no real roster-membership walk (same "no registry"
    // simplification class as groupsCreated_ above) - test-observable call
    // count only.
    uint64_t partyDismissAllCount() const { return partyDismissAllCount_; }
    void recordPartyDismissAll() { ++partyDismissAllCount_; }

    // sidewalk_disable_nodes (Sec15.3, individually cleared by Sec60.3).
    // CONFIRMED: resolved via the shared trigger/object resolver, then
    // enables/disables a named group of sidewalk/pathing action-nodes (the
    // vtable `+0x58` method and the final apply's own exact semantics are
    // OPEN - not modelled further, same "resolve + flag, no deeper replay"
    // simplification as the placed-object family above).
    void setSidewalkNodesDisabled(const std::string& name, bool disabled) { sidewalkNodesDisabled_[name] = disabled; }
    bool sidewalkNodesKnown(const std::string& name) const { return sidewalkNodesDisabled_.count(name) != 0; }
    bool sidewalkNodesDisabledValue(const std::string& name) const {
        auto it = sidewalkNodesDisabled_.find(name);
        return it != sidewalkNodesDisabled_.end() && it->second;
    }

    // mip_streaming_pause (Sec60.1). CONFIRMED real cross-script collision:
    // the Lua path always passes bit index 0 of some shared pause bitfield,
    // aliasing every caller onto the exact same bit regardless of the
    // string argument scripts pass (real call sites: 2, 1 script, argument
    // shape `boolean,string` - empirically observed, `verdict_stub_hits_
    // with_missions.tsv`; the string is read but, per Sec60.1's own text,
    // plays no role in which bit is touched). One process-wide bool stands
    // in for that single shared bit. CHOSEN starting default (false, "not
    // paused") - no spec states this bit's construction value either way.
    bool mipStreamingPaused() const { return mipStreamingPaused_; }
    void setMipStreamingPaused(bool paused) { mipStreamingPaused_ = paused; }

    // group_create_check_done (Sec22.18). CONFIRMED: an unresolved/not-alive
    // group is treated as "done" (true). This project builds no per-member
    // registry for any group (group_create_do/_hidden_do above only record
    // that a name was created, never a member list), so the resolved-group
    // branch's own two-list completion walk (0x00a2afc0, OPEN internal
    // field offsets per Sec22.18) is vacuously "both lists exhausted clean"
    // under this project's own stated no-member-registry simplification -
    // the same "a registry this project deliberately does not build is
    // itself the confirmed fallback" reasoning vehicle_pathfind_check_done
    // (Sec9.10) already established, not a fresh guess. Always true.
    bool groupCreateCheckDone(const std::string& /*name*/) const { return true; }

    // --- Section 6 batch (2026-10-07, full-document spec sweep) -
    // set_ped_density/set_traffic_density/notoriety_set_can_decay/
    // waypoint_remove/spawning_pedestrians/spawning_vehicles/traffic_
    // disable_drawbridge_area/guardian_angel_register_script_group/
    // guardian_angel_unregister_script_group (Sec6.9/6.14/6.18/6.17/6.13/
    // 6.15/6.4/6.6/6.8). See each accessor's own doc comment below for
    // citation/reasoning. The notoriety track-value setters (Sec6.3/6.5/
    // 6.7/6.11/6.16) and disable_specific_notoriety_group/notoriety_force_
    // no_spawn_group (Sec6.2/6.10) were deliberately NOT in THIS batch -
    // they were thought to need a real alias/group-name table the spec
    // text does not fully supply; mission_end_success/mission_end_to_
    // activity/mission_set_cancel_warp_location (Sec6.12/6.20/6.21) were
    // deliberately NOT in this batch either, pending a mission-state model.
    // BOTH blockers are resolved by batch b29 (2026-10-08, see below): the
    // MissionContext struct (Sec1.5/1.6, implemented in the interim by the
    // Team-A-cleared batch below) already models the "mission is active"
    // gate these three share with mission_end_failure, and the alias/group
    // tables' own CONFIRMED mechanism (case-insensitive scan, a stated
    // sentinel on no match) is modeled the SAME "OPEN-until-a-test-sets-it"
    // way every other not-fully-enumerated table in this project already
    // is (lookupNotorietyTrackCode's own identical precedent, just above
    // this batch) - not an invention, the established idiom for exactly
    // this situation. See the b29 fields further below (notoriety alias
    // track state, the group-record extensions on NotorietySpawnGroupState,
    // and the mission-gate reuses) for the full citations. tutorial_start
    // (Sec6.19) stays OUT of scope - its 210-entry descriptor table and
    // 12-step dispatcher remain a disproportionate new subsystem for one
    // native, and no later spec update has changed that.

    // set_ped_density/set_traffic_density (Sec6.9/Sec6.14). CONFIRMED: two
    // sibling global floats (0x01308ab4/0x01308ab0, address-adjacent - HIGH
    // CONFIDENCE sibling fields of one small world-population-density
    // settings block), each written directly from arg 1; nothing else is
    // read or written. No other function in this document reads either
    // value back, and no CONFIRMED startup value is given either way -
    // write-only in this project's own observable scope; the starting value
    // 0.0f is CHOSEN, not a spec fact.
    float pedDensity() const { return pedDensity_; }
    void setPedDensity(float v) { pedDensity_ = v; }
    float trafficDensity() const { return trafficDensity_; }
    void setTrafficDensity(float v) { trafficDensity_ = v; }

    // notoriety_set_can_decay (Sec6.18). CONFIRMED: a single GLOBAL flag
    // (0x014a4c0f), not per-track - unlike every other notoriety_set_*
    // function in this cluster. CONFIRMED: the stored byte is the LOGICAL
    // NEGATION of the Lua-visible argument (the same "flag stored as the
    // negated sense of an allow_*-style argument" convention Sec3.16
    // already established for character_allow_ragdoll). CHOSEN starting
    // value (false, "decay allowed") - no spec states this byte's
    // construction value either way.
    bool notorietyDecaySuppressed() const { return notorietyDecaySuppressed_; }
    void setNotorietyCanDecay(bool canDecay) { notorietyDecaySuppressed_ = !canDecay; }

    // waypoint_remove (Sec6.17). CONFIRMED: the argument (optional number,
    // default 3) selects a local-removal path (bit 1), a network-replicated
    // path (bit 2), or both (the default 3 runs both) - no Lua-visible
    // return value on any path, and this project has no real waypoint/HUD-
    // navigation object to remove, so neither path has an observable effect
    // to distinguish. Recorded for test-observability only (call count +
    // last mode value) - same "no Lua-visible effect beyond the call
    // itself to model" class as party_dismiss_all above.
    uint64_t waypointRemoveCount() const { return waypointRemoveCount_; }
    int32_t lastWaypointRemoveMode() const { return lastWaypointRemoveMode_; }
    void recordWaypointRemove(int32_t mode) {
        ++waypointRemoveCount_;
        lastWaypointRemoveMode_ = mode;
    }

    // spawning_pedestrians/spawning_vehicles (Sec6.13/Sec6.15). CONFIRMED:
    // each sets or clears slot 1 of its own per-category state-slot
    // array(s) - this project models only this CONFIRMED master on/off
    // toggle, not the full 8-slot arrays (the other 7 slots' real-world
    // identity is OPEN) or the combined cross-array cleanup
    // spawning_pedestrians' own optional arg 2 can trigger when every
    // tracked slot in both arrays reads inactive (gated on "two more
    // internal checks," OPEN - not modelled further). CHOSEN starting
    // value (true, "ambient spawning on") - no spec states either array's
    // construction value, but an ambient-population game starting with all
    // spawning OFF by default is not a credible reading either; flagged as
    // CHOSEN, not derived.
    bool pedestrianSpawningEnabled() const { return pedestrianSpawningEnabled_; }
    void setPedestrianSpawningEnabled(bool enabled) { pedestrianSpawningEnabled_ = enabled; }
    bool vehicleSpawningEnabled() const { return vehicleSpawningEnabled_; }
    void setVehicleSpawningEnabled(bool enabled) { vehicleSpawningEnabled_ = enabled; }

    // traffic_disable_drawbridge_area (Sec6.4). CONFIRMED: arg 1 (rounded)
    // builds the fixed name "Spline_Disabler<%03d>" (zero-padded to 3
    // digits), resolved through the SAME shared trigger/object resolver
    // this project already models generically via objectResolves() (Sec1.1,
    // 0x005e4e30); on success, bit (arg1-1) of a 32-bit global bitmask
    // (0x02705fe4) is set/cleared to match arg 2. The resolved trigger's own
    // further dispatch (0x00a6dd70) is OPEN beyond the bitmask write - not
    // modelled further (shared, per Sec6.22's own cross-reference, with
    // traffic_disable_lanes/Sec12.19's identical OPEN boundary). An
    // out-of-range area index (outside 1-32) is a silent no-op on the
    // bitmask - no real failure path is given either way, and a 32-bit mask
    // cannot represent a bit outside that range; this project does not
    // invent one.
    uint32_t drawbridgeDisabledMask() const { return drawbridgeDisabledMask_; }
    void setDrawbridgeAreaDisabled(int32_t area1Based, bool disabled) {
        if (area1Based < 1 || area1Based > 32) return;
        uint32_t bit = 1u << static_cast<uint32_t>(area1Based - 1);
        if (disabled) drawbridgeDisabledMask_ |= bit;
        else drawbridgeDisabledMask_ &= ~bit;
    }

    // guardian_angel_register_script_group/guardian_angel_unregister_
    // script_group (Sec6.6/Sec6.8). CONFIRMED: structurally identical -
    // same resolver (0x005eab60, a further member of the SAME shared
    // type-gated by-name resolver family objectResolves() already models,
    // same underlying generic resolver 0x004588f0/class-descriptor table
    // 0x02cc9900), same liveness check, same position-pair read off the
    // resolved object; differ only in their terminal call (register vs
    // unregister). Those terminal calls' (0x00627260/0x00627290) own
    // internal behaviour is OPEN - this project records only the resolve-
    // gated register/unregister boolean per name, the same "resolve + flag,
    // no deeper replay" simplification as traffic_disable_lanes/sidewalk_
    // disable_nodes above (not the resolved object's own world position,
    // which no other Sec6 consumer reads back either).
    void setGuardianAngelGroupRegistered(const std::string& name, bool registered) {
        guardianAngelGroups_[name] = registered;
    }
    bool guardianAngelGroupKnown(const std::string& name) const { return guardianAngelGroups_.count(name) != 0; }
    bool guardianAngelGroupRegistered(const std::string& name) const {
        auto it = guardianAngelGroups_.find(name);
        return it != guardianAngelGroups_.end() && it->second;
    }

    // --- batch b29 (2026-10-08): the rest of Section 6 -------------------
    //
    // notoriety_set_min_and_max/notoriety_set_max/notoriety_set/
    // notoriety_set_min/notoriety_reset (Sec6.3/6.5/6.7/6.11/6.16). CONFIRMED
    // (Sec6.3/Sec6.22): a THIRD, independent notoriety-track name resolver
    // (0x00605160), distinct from lookupNotorietyTrackCode's own
    // 0x0094cc60/sentinel-9 resolver above (kept SEPARATE on purpose - the
    // same WALLS.md caution against conflating two resolvers that merely
    // resemble each other) - "a case-insensitive scan across three parallel
    // 4-entry alias-name arrays... returning a shared 0-3 index or the
    // literal 5 on no match." Only the first alias of each 4-entry array is
    // given verbatim from .rdata ("luchadores"/"brotherhood"/"los_carnales"
    // -> codes 0/1/2, seeded at construction below, applySpecInitialState)
    // - the remaining 3 members of each array are not given by spec, so
    // this project models the CONFIRMED mechanism (case-insensitive lookup,
    // deterministic sentinel 5 on no match - itself a real, confirmed
    // behaviour, not an OPEN unknown) via a plain seeded map a test can add
    // further aliases to, the same "test-registrable, not invented" shape
    // lookupNotorietyTrackCode itself already established for its own
    // resolver.
    static constexpr int32_t kNotorietyAliasTrackNotFound = 5;
    void registerNotorietyAliasTrackCodeForTesting(const std::string& name, int32_t code) {
        notorietyAliasTrackCodes_[AsciiLowerFold(name)] = code;
    }
    int32_t lookupNotorietyAliasTrackCode(const std::string& name) const {
        auto it = notorietyAliasTrackCodes_.find(AsciiLowerFold(name));
        return it != notorietyAliasTrackCodes_.end() ? it->second : kNotorietyAliasTrackNotFound;
    }
    // Per-(alias-track-code) state, the five setters' own shared record -
    // CONFIRMED distinct storage from lookupNotorietyTrackCode's own track
    // records (Sec6.22: "a THIRD, independent... mechanism", not proven to
    // share an instance with anything else in this file). Every write is
    // gated the SAME way (Sec6.22: "every record-and-replicate call site
    // newly traced in THIS cluster... gates on... 0x0087ba20" -
    // EngineState::coopLocalIsHost(), NOT the forceFlagGatePasses double-
    // gate the rest of this file uses) - host: direct write; non-host:
    // EngineState::replicateStateChange() only, no local write (the SAME
    // either/or shape every other record-and-replicate setter in this file
    // already follows).
    struct NotorietyAliasTrackState {
        double currentValue = 0.0;  // 0x006079c0 (notoriety_set, Sec6.7, sub-tag 5)
        double minValue = 0.0;      // 0x00607ea0 (notoriety_set_min, Sec6.11, sub-tag 2)
        double maxValue = 0.0;      // 0x006080d0 (notoriety_set_max, Sec6.5, sub-tag 3)
        uint64_t setCount = 0, setMinCount = 0, setMaxCount = 0, setMinAndMaxCount = 0, resetCount = 0;
    };
    std::unordered_map<int32_t, NotorietyAliasTrackState>& notorietyAliasTrackStates() { return notorietyAliasTrackStates_; }
    // notoriety_reset's own first call (0x00609810, Sec6.16): a wholesale
    // notoriety UI/meter reset - CONFIRMED host-only, with NO replicated
    // fallback at all on a non-host machine (a genuine, confirmed asymmetry
    // against its four sibling setters above, none of which silently do
    // nothing on the non-authoritative path). This project has no real
    // 800-byte buffer/UI meter to clear - a test-observable call counter
    // only, same "no Lua-visible effect beyond the call itself" convention
    // as waypointRemoveCount above.
    uint64_t notorietyUiMeterResetCount() const { return notorietyUiMeterResetCount_; }
    void recordNotorietyUiMeterReset() { ++notorietyUiMeterResetCount_; }

    // disable_specific_notoriety_group/notoriety_force_no_spawn_group
    // (Sec6.2/6.10). CONFIRMED: BOTH resolve arg 1 through the SAME 24-name
    // GROUP table (0x008ed350) notoriety_spawn_group_set_delay/_destination
    // already model via notorietySpawnGroupResolves()/
    // getOrCreateNotorietySpawnGroup() below (Sec13.2/Sec25.1) - reused
    // directly here, not a new table, since it is confirmed to be the exact
    // same real resolver (see NotorietySpawnGroupState's own doc comment
    // further below for the two new fields this batch adds to it:
    // forceNoSpawn/tierItemFlags).
    //
    // disable_specific_notoriety_group's own per-group tier range-check
    // (+0x3e0/+0x3e4 min/max VALUES, i.e. which tier numbers are valid for a
    // given group - distinct from the group NAME resolving at all, see
    // notorietySpawnGroupResolves()'s own doc comment above for that half):
    // NOT given by spec for ANY of the 24 groups, including "police" -
    // spec-tables-progression.md Sec6.3/6.4 gives the field's layout/
    // meaning and the full 24-name list but not one real per-group tier
    // count or level_info row (base-only xtbl, no sample). Modeled as a
    // dedicated OPEN-until-set resolve, keyed "<group>#<tier>", same
    // discipline as every other not-given bound in this project (inventing
    // a plausible-looking range would be exactly the kind of guess this
    // project's OpenValue discipline exists to refuse).
    OpenValueMap<bool>& notorietyGroupTierInRangeResolves() { return notorietyGroupTierInRangeResolves_; }

    // mission_end_success/mission_end_to_activity/mission_set_cancel_warp_
    // location (Sec6.12/6.20/6.21) - reuse MissionContext (Sec1.5/1.6,
    // below) for their shared "mission is active" gate, now that it exists;
    // see that struct's own new fields (missionEndSuccessDispatchCount etc.)
    // and MissionCancelWarpLocation (further below) for the rest.

    // --- Team-A-cleared batch, spec-lua-api-behaviour.md Sec1.1/1.2/1.4/
    // 1.5/1.6/2.1/2.5/2.6/3.2/3.3/3.5/3.6 (commit 97a9b8d), implemented
    // 2026-10-07/2026-10-08 ------------------------------------------------

    // on_vehicle_destroyed (Sec3.3) / character_is_dead (Sec3.5): the
    // generic/`#PLAYER#` chain's own class-bit gate (`0x00a27e20`: the
    // resolved object's class-descriptor row `0x02cc9900[+0x34]`, byte
    // `+0xa` bit `0x2` - independently established project-wide as the
    // PLAYER CLASS bit, Sec3.3's own 2026-10-07 refutation of a
    // "kind-agnostic" hypothesis, FOUR independent confirmations cited
    // directly in Sec3.3's own text, re-confirmed again by Sec4.13's
    // "type-gated resolver family" cross-function pass: `0x00a27e20` is one
    // of five siblings, each testing one bit of the SAME per-object-type
    // descriptor row). Needed wherever a function's own CONFIRMED text makes
    // a genuinely different observable choice depending on WHICH of the two
    // resolve chains (this player-class-gated one vs. the plain/vehicle-
    // specific fallback, modeled by the flat objectResolves() map below)
    // answered - on_vehicle_destroyed (character-vs-vehicle hook target) and
    // character_is_dead (mission-end-timer override applies only on this
    // branch) are Sec3.3/Sec3.5's own two call sites, but §4.13 confirms
    // `0x00a280c0` (which embeds this exact gate as its own first-tier fast
    // path) is the base resolver reached directly by many more natives this
    // project has since implemented: on_attack_performed (Sec3.12),
    // teleport/teleport_to_object (Sec4.3/Sec22.4), on_ai_do_action_complete
    // (Sec14.7), on_mount (Sec42.3), vehicle_enter_do/vehicle_enter_check_done
    // (Sec38.1/Sec22.3), party_dismiss_do (Sec50.5), character_kill (Sec3.15)
    // and on_vehicle_enter (Sec4.12) all branch on it too (grep
    // playerClassChainResolves( across src/ for the full, current call-site
    // list - this comment is not re-counted mechanically on every edit).
    // Every OTHER in-scope function that also cites "generic/`#PLAYER#`
    // chain then fallback" (on_death, Sec3.2) does the SAME thing regardless
    // of which path resolved, so it keeps using the flat objectResolves()
    // gate alone - this map is deliberately NOT a universal replacement for
    // objectResolves(), only the narrower "which path" fact these functions'
    // own text needs.
    //
    // playerClassChainResolves() (0-arg): the raw per-name override map, for
    // a test to .set()/.forget()/.known() exactly as before - unchanged.
    //
    // playerClassChainResolves(name) (1-arg, below): a test's explicit
    // .set() value always wins when present; otherwise, instead of throwing
    // OpenStateError for every untouched name (this map's ORIGINAL shape,
    // before 2026-10-08's player-class-chain-gate fix), this applies a REAL
    // default now that the bit's own MEANING is spec-confirmed (Sec3.3/
    // Sec4.13 above): TRUE for a character/human-class name, FALSE for a
    // vehicle (the two kinds this bit actually discriminates - no other
    // object kind reaches this particular gate in any call site this
    // project has traced). Classifying an arbitrary mission-script name
    // STRING as character-vs-vehicle is, unlike the bit's meaning, NOT given
    // by any spec - this project's own tooling was checked first and has no
    // reusable answer: tools/lua_all_registered_1490_tagged.txt tags Lua
    // NATIVE FUNCTION names (e.g. "gameplay"/"ui"), not mission OBJECT
    // names; tools/mission_package_per_container.tsv maps scripts to
    // containers, no object-kind column at all; releaseToWorldKind()/
    // onVehicleEnterResolverKind() (both OpenValueMap<int32_t>, Sec4.4/
    // Sec4.12) are a DIFFERENT real mechanism's own per-name classification
    // (confirmed independently, not assumed to share data with this bit) and
    // are, in practice, only ever .set() by tests - never populated from
    // real probe/mission data either; getOrCreateCharacter()/hasCharacter()
    // auto-create on first touch, so they are circular here (this gate is
    // typically the FIRST touch of a name a mission script references).
    // CHOSEN (NOT spec-confirmed - flagged plainly, unlike the bit's own
    // meaning above) heuristic, calibrated directly against this project's
    // own mission-drive probe output (results/verify_probe_blockers_ranked_
    // 20261008_post_missioncontextname/verdict_mission_drive_summary.txt):
    // every character name the probe's own 49-mission corpus hit this gate
    // for ('NPC Pierce', 'npc_zimos', 'npc_pierce', 'char_stag_main_stage
    // 005', 'char_stag_grunt_dead 001', 'Pierce', 'Angel') contains neither
    // signal below; both vehicle names it hit ('debris_vehicle 001', 'Veh CP
    // SJ Spotlight Heli') do, and so do this project's own release_to_world-
    // adjacent vehicle names ('Starting_Vehicles', 'Script_Vehicle 001',
    // Sec4.4) - see looksLikeVehicleNameForPlayerClassGate()'s own doc
    // comment just below for the exact two-part string match.
    // REUSED AGAIN, 2026-10-09 (spec-player-runtime.md Sec7.4a step 7, the
    // vehicle-enter eligibility gate 0x00b0ca60): a SECOND class-row bit on
    // the SAME 0x02cc9900 row, +0xa bit 0x04 ("has the standard AI-state
    // block at all," HIGH CONFIDENCE by analogy to this row's own
    // already-named +0xa bit 0x02 "is-player" test this map already stands
    // in for) - exactly the teleport (Sec4.3) precedent just above: NOT a
    // claim the two bits are the SAME physical bit, only that this project
    // answers both class-row questions with the one map it already has for
    // the purpose. In every real call site this gate reaches, the "character"
    // argument has already resolved as a character (never a vehicle) before
    // the gate runs, so in practice this almost always reads true for a
    // legitimate vehicle-enter attempt - consistent with "has an AI-state
    // block at all" being closer to a baseline capability than a rare
    // exception, and with this project not modelling any non-AI-capable
    // character class.
    OpenValueMap<bool>& playerClassChainResolves() { return playerClassChainResolves_; }

    // CHOSEN (not spec-confirmed - see playerClassChainResolves(name)'s own
    // doc comment above for the full citation chain and why no better
    // classification source exists in this project today) heuristic: a name
    // is treated as vehicle-shaped, and so defaults to FALSE on the
    // player-class gate, when it either contains "vehicle" (case-
    // insensitive, matches anywhere - covers 'debris_vehicle 001',
    // 'Starting_Vehicles', 'Script_Vehicle 001') or begins with the short
    // token "veh" followed by a space/underscore/hyphen/end-of-string
    // (case-insensitive - covers 'Veh CP SJ Spotlight Heli' without
    // matching any character name in this project's own probe corpus, none
    // of which begin with "veh"). Every other name defaults to TRUE
    // (character/human class). Static, pure string match - no engine state,
    // so it is trivially test-observable and does not need its own
    // OpenValueMap entry.
    static bool looksLikeVehicleNameForPlayerClassGate(const std::string& name) {
        std::string lower = AsciiLowerFold(name);
        if (lower.find("vehicle") != std::string::npos) return true;
        if (lower.size() >= 3 && lower.compare(0, 3, "veh") == 0) {
            if (lower.size() == 3) return true;
            char next = lower[3];
            if (next == ' ' || next == '_' || next == '-') return true;
        }
        return false;
    }

    bool playerClassChainResolves(const std::string& name) {
        if (playerClassChainResolves_.known(name)) return playerClassChainResolves_.get(name);
        return !looksLikeVehicleNameForPlayerClassGate(name);
    }

    // --- Mission context shared by mission_end_failure (Sec1.5) and
    // mission_set_checkpoint (Sec1.6); the mission-end-transition timer
    // (0x012f2630) is ALSO read by character_is_dead's own override
    // (Sec3.5) - all three cite the identical real globals, cross-checked
    // against each other by this same batch, not independently guessed per
    // function. ---------------------------------------------------------
    struct MissionContext {
        // 0x014c8460 non-null AND phase 0x014c7a14 != 8 (CONFIRMED - both
        // Sec1.5 and Sec1.6 test the exact same two globals the same way,
        // collapsed into one combined predicate, the same convention this
        // project's own InProgressType::activeMissionPresent, Sec27.25,
        // already established for the identical pair - a SEPARATELY SCOPED
        // instance, not reused, since that struct is out of this batch's
        // own 12-section scope to touch). OPEN until a test sets it, no
        // spec gives a start-up value.
        OpenValue<bool> activeAndRunning{"mission context active, phase != 8 (0x014c8460/0x014c7a14)",
                                         "spec-lua-api-behaviour.md Sec1.5/Sec1.6"};
        // mission_end_failure's own identity-gate condition (Sec1.5, shared
        // verbatim by mission_end_success/Sec6.12 and mission_end_to_
        // activity/Sec6.20 - all three read this same field): arg 1 must
        // name the SAME mission this global currently holds (this project's
        // "key by name string" simplification standing in for real pointer
        // identity, per this header's own top note).
        //
        // FIX 2026-10-08 (mission-context-name task, second-order blocker
        // behind probe_blockers_ranked_20261008_post_resolver_merge.md's own
        // "mission context phase != 8" item, 9/49 missions - dlc1_mm_01..06/
        // mm_p_02/04/05): this field was left permanently OPEN with NO
        // production writer anywhere - EngineState::bindMissionPhaseMachine()
        // (0x006d6920, "bind mission") sets missionContext_.boundMissionName
        // (the phase machine's own identity stand-in) but never touched this
        // SEPARATE field, so every real mission script that reached
        // mission_end_failure/mission_end_success/mission_end_to_activity
        // threw OpenStateError reading an OPEN value instead of getting a
        // real (possibly gate-failing, never-throwing) answer. Sec21.27a
        // CONFIRMS 0x014c8460's first dword (the SAME global this field
        // stands in for) is written by exactly one function, 0x006d6920, at
        // the exact instant phase goes to 8 - the one real bind-time write
        // bindMissionPhaseMachine() already models. Now set THERE (see that
        // method, below), at the same real call site, not a second
        // independent write path - no longer OPEN once any mission has
        // bound.
        OpenValue<std::string> currentMissionName{
            "current mission name (pointer-identity stand-in, 0x014c8460's own target)",
            "spec-lua-api-behaviour.md Sec1.5"};
        // mission_end_failure's OTHER identity-gate condition (Sec1.5, same
        // sharing as currentMissionName above): the CURRENT mission's own
        // liveness check must come back DEAD (its +0x33 bit 0x4 set, or
        // +0x34 == 0xff - "already inside its own ending/destroy
        // transition") - "a mission 'found and still fully alive' does NOT
        // pass" (Sec1.5's own text).
        //
        // FIX 2026-10-08 (same task/root-cause as currentMissionName above):
        // also now set by bindMissionPhaseMachine(), to false ("alive, not
        // yet tearing down") - the one honestly-reachable real value at the
        // exact instant a mission is freshly bound (a just-created handle's
        // own liveness bits cannot simultaneously read "being destroyed" or
        // "freed"), the SAME "OPEN facade -> CHOSEN real reachable value"
        // fix already applied to activeAndRunning (EngineState::EngineState()
        // 's own doc comment). This makes the gate fail CLOSED (silent
        // no-op, matching Sec1.5's own "still fully alive does NOT pass"
        // text) rather than throw, for every mission that has bound but not
        // yet begun tearing down - which is every real mission this
        // project's probe drives today, since no spec passage anywhere in
        // Sec1.5/Sec21.27a/Sec21.27b ties this liveness bit to the real
        // phase counter (6/7) or to any other event this project can trace -
        // genuinely OPEN past this point, left test-settable (.set(true))
        // for a test that wants to exercise the gate's full-pass path,
        // rather than invented.
        OpenValue<bool> currentMissionEnding{"current mission liveness bit (+0x33/+0x34, 'already ending')",
                                             "spec-lua-api-behaviour.md Sec1.5"};
        // The debounce pair mission_set_checkpoint's own dispatch
        // (0x006d5060) reads (Sec1.6: "two adjacent cooldown/timer objects,
        // 0x012f2630/0x012f2634 ... both NOT still counting down"); timer 1
        // is ALSO the exact mission-end-transition timer mission_end_failure
        // arms (via its own final dispatch, 0x006da400 - independently
        // identified elsewhere in this project as "the mission-end routine"
        // that arms this same global for 2,000-4,000ms, Sec3.5's own
        // citation) and character_is_dead's override reads (Sec3.5). This
        // project drives no real countdown clock (nothing in scope
        // decrements either timer once armed) - a plain armed/idle bool
        // stand-in for each, CHOSEN false ("idle") at construction: timer 2's
        // own real writer is never identified anywhere in Sec1.6's own text
        // (OPEN, out of scope), so nothing in this closed world ever arms
        // it; timer 1 starts false for the same "nothing has happened yet"
        // reason, becoming true only via armMissionEndTransitionTimer()
        // below, this project's own single in-scope writer.
        bool missionEndTimer1Armed = false; // 0x012f2630 - failure's own timer, armed by armMissionEndTransitionTimer() below
        // FIX (Sec21.27c, 2026-10-09): no longer "OPEN who arms this" - full
        // disassembly of the shared presentation dispatch 0x006da400 shows
        // mission_end_success arms THIS timer (armMissionEndSuccessTimer()
        // below) - a SEPARATE timer from failure's own timer 1, correcting
        // this file's prior (batch b29) assumption that success shared
        // timer 1 purely because both callers call the same dispatch
        // function. See dispatchMissionEndPresentation() below.
        bool missionEndTimer2Armed = false; // 0x012f2634 - success's own timer
        // Test-observable count of successful mission_end_failure dispatches
        // (gate passed, 0x006da400's FAILURE branch actually taken - i.e.
        // NOT a type-2-coerced call, see
        // missionEndFailureCoercedToSuccessCount below) - not a spec fact,
        // just a plain call counter for test convenience, same convention
        // as partyDismissAllCount above.
        uint64_t missionEndFailureDispatchCount = 0;
        // mission_end_success (Sec6.12, batch b29, 2026-10-08): CONFIRMED
        // to share mission_end_failure's own gate (above); its own final
        // dispatch (0x006da400) is the SAME shared function failure calls,
        // but FIX (Sec21.27c, 2026-10-09, full disassembly of that shared
        // function): it arms its OWN separate timer (missionEndTimer2Armed
        // above), not failure's timer 1 as previously assumed - kept as a
        // SEPARATE counter so a test can tell the two callers apart
        // regardless.
        uint64_t missionEndSuccessDispatchCount = 0;
        // mission_end_success's own arg 3 (polymorphic: a table's first two
        // indexed elements, or a plain string's one slot, or absent):
        // CONFIRMED each resolved name's position-pair would anchor the
        // success UI, but this project tracks no real transform/position
        // data (same "only WHICH branch ran" convention mission_set_
        // checkpoint, Sec1.6, already established) - only whether each slot
        // resolved is recorded.
        bool lastMissionEndSuccessSlot1Resolved = false;
        bool lastMissionEndSuccessSlot2Resolved = false;
        // mission_end_to_activity (Sec6.20, batch b29): CONFIRMED to share
        // the SAME gate, then (gate passing) attempts arg 2's own "activity"
        // object-kind resolve; 0x006da960 copies position-pair into two
        // globals ONLY when (resolved AND host) - this project has no real
        // transform data, so only whether that capture happened is counted;
        // 0x006da7b0 itself runs unconditionally once the gate passes
        // (CONFIRMED, Sec6.20's own text), tracked as its own counter since
        // its own internal behaviour is OPEN (Sec6.20).
        uint64_t missionEndToActivityPositionCaptureCount = 0;
        uint64_t missionEndToActivityDispatchCount = 0;

        // --- Sec21.27c upgrade (new task, 2026-10-09): "0x006da400 opened
        // in full" - see dispatchMissionEndPresentation() further below for
        // the one real model of that shared function, and each field's own
        // doc comment just below for citations. ----------------------------
        std::string lastFailReasonText;          // 0x014c7a08 - CONFIRMED write; renderer OPEN (see below)
        std::string lastSuccessAnnouncementText;  // 0x014c846c - CONFIRMED write; renderer OPEN (see below)
        // Sec21.27c's own text flags two items as explicitly OPEN (not
        // guessed): who creates a NEW fail-dialog instance (0x006da400 only
        // ever closes a STALE one, via the existing 4-slot modal-dialog
        // pool 0x02282d40/0x007c1dd0/0x007c31b0 already tied to the
        // "dialog_build" Lua hook - no creation call found inside this
        // function itself); and what, on a later frame, actually RENDERS
        // either text global above (most plausibly the separately-
        // documented cmp_mission_success native UI entry, Sec14.3, not
        // itself Lua-dispatched - not traced end-to-end). This project has
        // no in-scope dialog-pool/renderer simulation to hook either OPEN
        // item into, so neither gets an invented field - only the two
        // writes Sec21.27c itself confirms happen are modelled, above.

        // CONFIRMED mechanism (Sec21.27c): failure's HUD-screen-teardown/
        // fade effect is gated on a flags-byte bit (0x014c848c & 0x8) - no
        // spec-given start-up value for that byte, so this is OPEN until a
        // test sets it (same "CONFIRMED mechanism, OPEN value" discipline
        // as every other not-given flags byte in this file, e.g.
        // MissionCheckpoint::usePlayerVehicleTransform above), NOT guessed
        // true or false by default.
        OpenValue<bool> hudTeardownGateBit{"mission-end flags byte 0x014c848c bit 0x8 (HUD-teardown gate)",
                                           "spec-lua-api-behaviour.md Sec21.27c"};
        // CHOSEN representation (this task, explicitly invited by its own
        // brief): stands in for the real 0x007b4990(-1) "tear down EVERY
        // currently active HUD screen" call on the generic HUD-screen
        // registry 0x012fced8 - this project has no real HUD-screen
        // registry to simulate (explicitly out of scope per the brief), so
        // only whether/how-often the teardown-ALL primitive fired is
        // tracked - same "count the call, not the subsystem" convention as
        // waypointRemoveCount/notorietyUiMeterResetCount above.
        uint64_t hudTeardownCount = 0;
        // Test-observable summary of the MOST RECENT
        // dispatchMissionEndPresentation() call: true only when that call
        // fired the teardown-all primitive. CONFIRMED asymmetry (Sec21.27c):
        // success NEVER sets this true, regardless of the gate bit above -
        // only the (non-coerced) failure path can.
        bool lastDispatchTornDownHud = false;

        // NEW finding, CONFIRMED by raw push-order/branch structure
        // (Sec21.27c, 2026-10-09): for mission TYPE 2 specifically, a call
        // to mission_end_failure is silently coerced into
        // 0x006d9f70(mission, 1) - hardcoded SUCCESS via the SAME
        // end-of-mission super-dispatcher missionEndDispatch() below
        // already models - skipping ALL of dispatchMissionEndPresentation()'s
        // own fail-text/debounce/HUD-clear logic entirely (that function is
        // never even entered on this path). Test-observable count of how
        // many times this coercion actually fired.
        uint64_t missionEndFailureCoercedToSuccessCount = 0;

        // CONFIRMED (Sec21.27c): timer 1 (0x12f2630) arms for 2000ms
        // normally, or 4000ms "if a second mission-end fires while the
        // first is cooling down" - modelled as "while timer 1 is already
        // armed from an earlier, not-yet-cleared dispatch." This project
        // drives no real countdown clock (same "armed/idle bool stand-in"
        // limitation this struct's own top note already states) - this
        // field records which of the two durations the most recent arm
        // actually used, for test observability only.
        int32_t missionEndTimer1LastArmedDurationMs = 0; // 0 = never armed; else 2000 or 4000

        // --- Sec21.27a/21.27b upgrade (this task, 2026-10-08): the REAL
        // 0x014c7a14 phase counter (int, 0-8) and the data the REAL end-of-
        // mission dispatcher (0x006d9f70, Sec21.27b) needs - additive to
        // everything above. `activeAndRunning` stays EXACTLY as it was (a
        // couple dozen existing tests call its own .set() directly; its own
        // meaning, "phase != 8," is unchanged) rather than being silently
        // re-derived from `phase`, which would change behaviour for a test
        // that sets ONLY activeAndRunning and never touches the real phase
        // machine at all. EngineState::bindMissionPhaseMachine() (below) is
        // the one production path that keeps both the real phase AND every
        // legacy facade consistent; a test exercising the legacy facades
        // alone is unaffected either way.
        //
        // Mission TYPE (the bound object's own +0xa0 field, 0-3, Sec21.27a/
        // Sec21.27b) has no real per-mission population table anywhere in
        // this project (no mission-object loader reads a real +0xa0 value
        // in scope) - EngineState::bindMissionPhaseMachine()'s own `type`
        // parameter defaults to a CHOSEN 0 (Sec21.27b's own type-0/3
        // "shared handler" branch - the most common/generic case, per this
        // task's own explicit instruction), overridable per-call for a test
        // that wants to exercise type 1/2's own distinct branches.
        int32_t phase = 0;                    // 0x014c7a14 - CONFIRMED real start-up value (0x006d6010)
        bool missionBound = false;            // 0x014c8460 non-null
        std::string boundMissionName;         // pointer-identity stand-in, same convention as currentMissionName above
        int32_t boundMissionType = 0;         // the bound object's own +0xa0 field (0-3) - CHOSEN default 0, see above
        bool missionStartMarkerArmed = false; // Sec21.27a phase 2/5 - no real HUD marker/checkpoint-icon object in scope; test-observable only
        uint64_t phaseTransitionCount = 0;    // test-observable count of setMissionPhase() writes, including its own internal self-loop
        // missionEndDispatch() (0x006d9f70, Sec21.27b) bookkeeping - see
        // that method's own doc comment for the full per-branch citation.
        uint64_t missionEndDispatchCount = 0;
        bool lastMissionEndDispatchSuccessFlag = false;
        uint64_t missionEndType1UnlockAndCompleteCount = 0;   // type 1: fires the completion writer AND mission_unlock's own setter (Sec21.27b)
        uint64_t missionEndType2CompleteAndOpenStubCount = 0; // type 2: fires the completion writer; 0x006d9470 itself stays OPEN, counted only
        uint64_t missionEndType0Or3ScriptHookAttemptCount = 0; // types 0/3: a +0x1f8 script-hook ATTEMPT would be made here - no real +0x1f8 name table in scope (same gap as the already-documented +0x1f0/script_mission_start real-name lookup, Sec36.4), counted only
        uint64_t missionEndSilentAutoSuccessCount = 0; // 0x006db7fa fired via missionSilentClass() below
    };
    MissionContext& missionContext() { return missionContext_; }

    // The general-purpose phase setter, `0x006d86f0(newPhase)` (Sec21.27a).
    // CONFIRMED real behaviour: phase 1 is "never actually observable as a
    // steady value" - its own case-1 handler's 4-entry jump table
    // immediately self-loops again INSIDE the same call, keyed by the
    // bound mission's own type: type 2 unconditionally, type 1
    // (Sec21.27a's own text gives no detail on the condition gating type
    // 1's self-loop - CHOSEN to treat it identically to type 2 here, since
    // nothing distinguishes them further and this project's own default
    // mission type is 0, never 1, unless a test explicitly overrides it)
    // -> phase 3; types 0 and 3 -> phase 5, which (Sec21.27a's own phase-5
    // table row) ALSO arms the mission-start marker at that point for
    // those two types.
    void setMissionPhase(int32_t newPhase) {
        missionContext_.phase = newPhase;
        ++missionContext_.phaseTransitionCount;
        if (newPhase == 1) {
            if (missionContext_.boundMissionType == 1 || missionContext_.boundMissionType == 2) {
                missionContext_.phase = 3;
            } else {
                missionContext_.phase = 5;
                missionContext_.missionStartMarkerArmed = true;
            }
            ++missionContext_.phaseTransitionCount;
        }
    }

    // `0x006d6920`, "bind mission" (Sec21.27a) - the ONLY real writer of
    // phase 8, and this project's own one hook for the entire 8 -> (2,
    // fresh+named only) -> 1 -> (3 or 5) sequence Sec21.27a's own text says
    // resolves "within a few frames" of real engine time - collapsed into
    // one synchronous call, the SAME simplification this project's own
    // prior beginMissionPhaseTracking() already applied to the boolean-only
    // facade (see that method's own doc comment, below, for the full root-
    // cause history this collapsing resolves): CONFIRMED to be the one
    // honestly-reachable sequence a host that only ever resumes a mission's
    // own `_start` once it's safe to do so (this project's own
    // kMissionStartIsRestart=false fresh-start convention,
    // tools/lua_host_run.cpp) could ever observe.
    //
    // `isRestart`/a non-empty `name` select whether phase 2's own marker-arm
    // branch runs at all (Sec21.27a's phase-2 table row: "fires only when
    // the bound mission is NOT a restart and has a name... otherwise...
    // skips phase 2 entirely and relaunches script_mission_start directly"
    // - this project's host always does a fresh, named start, so phase 2
    // fires on every real call today; the restart branch exists for
    // completeness/future test coverage, not because this project
    // currently drives it).
    //
    // After settling at phase 3 or 5, if `name` is flagged silentClass (see
    // missionSilentClass() below) this ALSO fires the organic "silent"-
    // class auto-success trigger (`0x006db7fa`, Sec21.27b's own organic-
    // trigger table: "Success, unconditionally") - the CHOSEN best-effort
    // organic SUCCESS path this task wires; see missionSilentClass()'s own
    // doc comment for why it is a documented no-op for every one of the
    // real 49 probed missions today (no population table for which of them
    // carries class bit 0x20).
    //
    // FIX 2026-10-08 (mission-context-name task): ALSO sets
    // MissionContext::currentMissionName/currentMissionEnding here, at this
    // SAME real bind call site (0x006d6920 - the identical function
    // Sec21.27a already cites as the one real writer of 0x14c8460's first
    // dword, which this field stands in for), not a second, independent
    // write path. Closes the second-order blocker Sec1.5's own identity gate
    // (mission_end_failure, shared verbatim by mission_end_success/Sec6.12
    // and mission_end_to_activity/Sec6.20) hit once the 12-mission phase-8
    // blocker above was fixed: those three natives were already wired to
    // read these two fields correctly, but nothing ever set them, so every
    // real mission reaching one of them threw OpenStateError instead of
    // getting a real (gate-failing or gate-passing) answer. See each
    // field's own doc comment (above) for the full citation.
    void bindMissionPhaseMachine(const std::string& name, int32_t type = 0, bool isRestart = false) {
        missionContext_.missionBound = true;
        missionContext_.boundMissionName = name;
        missionContext_.boundMissionType = type;
        missionContext_.currentMissionName.set(name);
        missionContext_.currentMissionEnding.set(false); // freshly bound: alive, not yet tearing down - see field's own doc comment
        setMissionPhase(8);
        if (!isRestart && !name.empty()) {
            setMissionPhase(2); // 0x006D9310's own direct write
            missionContext_.missionStartMarkerArmed = true; // tail-call into 0x006d5720, the real marker-arm function
        }
        setMissionPhase(1); // self-loops to 3 or 5 inside setMissionPhase() above
        if (!name.empty() && missionSilentClass_.known(name) && missionSilentClass_.get(name)) {
            setMissionPhase(4);
            if (missionEndDispatch(name, /*successFlag=*/true)) ++missionContext_.missionEndSilentAutoSuccessCount;
        }
    }

    // `0x006d9f70`, the end-of-mission super-dispatcher (Sec21.27b). Entry
    // gates (Sec21.27b's own text, CONFIRMED): refuses if no mission is
    // bound, if phase is already 8 (mid-bind), or if `name` doesn't match
    // the currently-bound mission (this project's "key by name string"
    // stand-in for real pointer identity, same convention as
    // MissionContext::currentMissionName above). Implements the 4-way TYPE
    // switch structurally:
    //  - type 1: fires the completion-flag writer AND mission_unlock's own
    //    setter with literal 1 (Sec21.27b: "this independently confirms...
    //    mission type 1 is a silent/no-completion-UI variant"). CHOSEN:
    //    gated on successFlag - Sec21.27b's own text flags "whether
    //    0x006d9f70's type-1/type-2 handlers further gate this call on
    //    successFlag internally" as "one honest loose end, not re-chased";
    //    gating conservatively on successFlag here never marks a FAILED
    //    mission complete through this path, matching every OTHER
    //    confirmed completion-flag caller (all unambiguously completion-
    //    specific per that same text).
    //  - type 2: fires the completion-flag writer again (same gating), then
    //    would call the still-undecoded `0x006d9470` - explicitly OPEN per
    //    Sec21.27b's own text ("a further, not-decoded function"), counted
    //    but not invented.
    //  - types 0/3 (shared handler): on success, would resolve the
    //    mission's own `+0x1f8` second named Lua entry point and launch it
    //    - this project has no real +0x1f8 name table (same gap as the
    //    already-documented +0x1f0/script_mission_start real-name lookup,
    //    Sec36.4), so only the ATTEMPT is counted, never invented.
    // Every branch falls through to the shared phase-6 ("ending") tail.
    bool missionEndDispatch(const std::string& name, bool successFlag) {
        if (!missionContext_.missionBound || missionContext_.phase == 8 || name != missionContext_.boundMissionName)
            return false;
        ++missionContext_.missionEndDispatchCount;
        missionContext_.lastMissionEndDispatchSuccessFlag = successFlag;
        switch (missionContext_.boundMissionType) {
            case 1:
                if (successFlag) {
                    writeMissionCompletionFlagBits(name);
                    writeMissionUnlockedFlagBit(name);
                    ++missionContext_.missionEndType1UnlockAndCompleteCount;
                }
                break;
            case 2:
                if (successFlag) {
                    writeMissionCompletionFlagBits(name);
                }
                ++missionContext_.missionEndType2CompleteAndOpenStubCount; // 0x006d9470 - OPEN, counted only
                break;
            case 0:
            case 3:
            default:
                if (successFlag) ++missionContext_.missionEndType0Or3ScriptHookAttemptCount;
                break;
        }
        setMissionPhase(6);
        return true;
    }

    // `0x006db7fa` (Sec21.27b's own organic-trigger table): the per-tick
    // driver's own case-4 "silent"-class branch - keyed per mission name,
    // OPEN/test-settable, the SAME "no spec-given real population, flat
    // test-settable resolver" convention as weaponDefinitionResolves()/
    // lookupNotorietyTrackCode() elsewhere in this file (no table of which
    // of the real 49 missions carry class-descriptor bit 0x20 was read by
    // this project). Read by bindMissionPhaseMachine() above via `.known()`
    // only (never a blind `.get()` that could throw) - for every real
    // mission in today's probe population this stays unset, so the trigger
    // is a documented no-op there, not a disguised always-true default.
    OpenValueMap<bool>& missionSilentClass() { return missionSilentClass_; }

    // The real +0x88 bit this task was asked to report on directly:
    // "did this mission's own completion flag (bit 0x4) get set." See
    // writeMissionCompletionFlagBits()'s own doc comment (below, Sec15.9/
    // Sec15.10's own section) for the single real writer this reads back.
    bool missionCompletionFlagBitSet(const std::string& name) const {
        auto it = missionFlagBits_.find(name);
        return it != missionFlagBits_.end() && (it->second & kMissionFlagCompleted) != 0;
    }

    // mission_end_failure's own dispatch (0x006da400) arming effect (Sec1.5,
    // cross-cited by Sec3.5) - the only in-scope writer of timer 1.
    // UPGRADED (Sec21.27c, 2026-10-09): also models the real 2000ms/4000ms
    // doubling - 4000ms when this call arms timer 1 again while it is
    // already armed from an earlier, not-yet-cleared dispatch; 2000ms
    // otherwise. Called only from dispatchMissionEndPresentation()'s own
    // non-coerced failure branch now (see below), not directly by the
    // mission_end_failure stub.
    void armMissionEndTransitionTimer() {
        missionContext_.missionEndTimer1LastArmedDurationMs =
            missionContext_.missionEndTimer1Armed ? 4000 : 2000;
        missionContext_.missionEndTimer1Armed = true;
        ++missionContext_.missionEndFailureDispatchCount;
    }

    // mission_end_success's own dispatch (0x006da400, the SAME shared
    // function failure uses) arming effect - the REAL separate timer this
    // branch arms, 0x12f2634 (Sec21.27c) - the only in-scope writer of
    // timer 2. CORRECTS this file's own prior (batch b29, 2026-10-08)
    // assumption that success armed the SAME timer 1 as failure, purely
    // because both callers share one dispatch function - see
    // MissionContext::missionEndTimer2Armed's own doc comment above for the
    // full citation. No duration is given for this timer anywhere in
    // Sec21.27c's own text (unlike timer 1's confirmed 2000/4000ms pair) -
    // armed/idle only, not guessed.
    void armMissionEndSuccessTimer() { missionContext_.missionEndTimer2Armed = true; }

    // `0x006da400` opened in full (Sec21.27c, 2026-10-09) - the ONE shared
    // presentation dispatch both mission_end_failure's non-coerced path and
    // mission_end_success reach after their own identical shared gate
    // (0x006d7240: MissionContext::activeAndRunning/currentMissionName/
    // currentMissionEnding) passes. `isSuccess` selects which of the two
    // CONFIRMED branches runs: false = failure (stores `presentationText`
    // into the fail-text stand-in, arms timer 1 for 2000/4000ms, and - only
    // when hudTeardownGateBit reads true - fires the HUD-screen-teardown-all
    // primitive); true = success (stores `presentationText` into the
    // SEPARATE success-text stand-in, arms the SEPARATE timer 2, and -
    // CONFIRMED asymmetry - NEVER fires the HUD-teardown primitive,
    // regardless of the gate bit).
    //
    // Callers are expected to have already run the shared gate themselves
    // (both existing stub functions already do, unchanged) - this method
    // does not re-check it. mission_end_failure's own type-2 branch must
    // skip this method ENTIRELY (routing to missionEndDispatch()/
    // 0x006d9f70 instead, per Sec21.27c's own coercion finding, modeled in
    // that stub, not here) rather than entering it and taking some branch
    // inside - so this method has no coercion logic of its own.
    void dispatchMissionEndPresentation(bool isSuccess, const std::string& presentationText) {
        auto& mc = missionContext_;
        if (!isSuccess) {
            mc.lastFailReasonText = presentationText; // 0x014c7a08
            armMissionEndTransitionTimer();            // 0x12f2630, 2000/4000ms
            bool tearDown = mc.hudTeardownGateBit.known() && mc.hudTeardownGateBit.get();
            mc.lastDispatchTornDownHud = tearDown;
            if (tearDown) ++mc.hudTeardownCount; // 0x007b4990(-1) on registry 0x012fced8
        } else {
            mc.lastSuccessAnnouncementText = presentationText; // 0x014c846c
            armMissionEndSuccessTimer();                        // 0x12f2634
            // CONFIRMED asymmetry (Sec21.27c): success never tears down HUD.
            mc.lastDispatchTornDownHud = false;
            ++mc.missionEndSuccessDispatchCount;
        }
    }

    // FIX 2026-10-08 (mission-blockers investigation, root cause of
    // probe_blockers_ranked_20261008_post_resolver_merge.md item 2 -
    // "mission context active, phase != 8 (0x014c8460/0x014c7a14) OPEN",
    // 12/49 missions). spec-lua-api-behaviour.md Sec21.27a (2026-10-08, new)
    // fully characterizes the real `0x014c8460`/`0x014c7a14` phase machine:
    // phase 0 ("idle - no mission bound") is the real engine's own start-up
    // value (`0x006d6010`, CONFIRMED); phase 8 ("the FIRST phase of a newly-
    // bound mission") is set ONLY by the bind function `0x006d6920` and
    // auto-resolves to phase 1 within a few frames - well before any script
    // code, including a mission's own `_start`, is ever reachable. Every
    // `!= 8` gate this project tracks (EngineState::MissionContext::
    // activeAndRunning, InProgressType::activeMissionPresent,
    // MissionExcludedRewardsState::activeAndRunning - three SEPARATELY
    // SCOPED facades over the identical real pair of globals, see each
    // field's own doc comment for why they are not merged into one) exists
    // "specifically to keep scripts from mutating a mission that is still
    // mid-bind," per that section's own text - NOT a steady/terminal value a
    // host could ever legitimately observe once a mission's own script is
    // actually running.
    //
    // Previously all three facades were left permanently OPEN (never set by
    // any production path), so EVERY mission whose own `_start` script
    // called a native gated on one of them - before this host had ever told
    // EngineState a mission had begun - threw OpenStateError immediately,
    // from inside `_start` itself (confirmed: all 12 blocked missions' own
    // `first_error_message` is prefixed `<stem>_start: ...`). This method is
    // the one hook this host's own mission driver (tools/lua_host_run.cpp's
    // per-mission loop, immediately before its one `callMissionStart()` call)
    // has for "a mission's own `_start` is about to be invoked" - which, per
    // Sec21.27a's own table, is CONFIRMED to be well past the one-shot
    // phase-8 window, so flipping all three gates to the real "ready" value
    // here is not an invented default, it is the one honestly-reachable
    // real phase value a host that has gotten this far could ever observe.
    // The companion idle default (false, applied once at construction - see
    // EngineState::EngineState()) is the OTHER real, CONFIRMED value
    // (`0x006d6010`'s own phase-0/null-context start-up state) - together
    // the two replace "permanently unknown" with the real engine's own two
    // reachable states, inventing neither.
    //
    // UPGRADED this task (2026-10-08, Sec21.27a/Sec21.27b full-document
    // read): this method no longer just flips the three legacy boolean
    // facades - it also drives the REAL integer phase machine
    // (bindMissionPhaseMachine(), MissionContext's own section above)
    // through its real 8 -> (2, fresh+named) -> 1 -> (3 or 5) sequence, and
    // (if `name` is flagged missionSilentClass()) the organic silent-class
    // auto-success trigger. The three legacy facades are kept, set
    // exactly as before, for every native that still gates on them
    // directly (several do, e.g. set_mission_enemy_team,
    // spec-lua-api-behaviour.md Sec21.11) - this is purely additive.
    // `type` defaults to the CHOSEN mission-type 0 (see
    // bindMissionPhaseMachine()'s own doc comment); `isRestart` defaults to
    // false, matching this project's own kMissionStartIsRestart convention
    // (tools/lua_host_run.cpp - every mission this host drives is a fresh
    // start, never a restart).
    //
    // MISSION-HOST PHASE 3 (2026-10-09): this method (and bindMissionPhase-
    // Machine() below it) are deliberately KEPT, not removed, even though
    // tools/sr3_viewer.cpp's own production `--mission=<stem>` playtest bind
    // now goes through the REAL sr3luahost::MissionPhaseMachine class
    // (include/sr3luahost/menu_runtime.h) instead of calling this method
    // directly - MissionPhaseMachine::launchStartFunction() itself calls
    // THIS method, at the same real bind call site, once a mission's own
    // `_start` is actually about to run (see that method's own doc comment,
    // menu_runtime.h), so every legacy facade below still gets set through
    // the real path. This method also has its own SEPARATE, still-legitimate
    // direct caller: tools/lua_host_run.cpp's own standalone headless
    // mission-drive loop (`host.engineState().beginMissionPhaseTracking(...)`,
    // immediately before that file's own callMissionStart()) - a different
    // tool with no MissionPhaseMachine instance of its own, not a dead
    // parallel path left over from before this switch.
    void beginMissionPhaseTracking(const std::string& name = std::string(), int32_t type = 0,
                                    bool isRestart = false) {
        missionContext_.activeAndRunning.set(true);
        inProgressType_.activeMissionPresent.set(true);
        missionExcludedRewards_.activeAndRunning.set(true);
        bindMissionPhaseMachine(name, type, isRestart);
    }

    // mission_set_cancel_warp_location (Sec6.21, batch b29, 2026-10-08).
    // CONFIRMED: both args optional strings (standard nil-gated idiom,
    // default absent), each resolved via objectResolves() (0x005982e0, the
    // SAME third-tier resolver door_open/teleport's own destination use,
    // Sec1.10/Sec4.13) gated by the SAME "mission active, phase != 8"
    // predicate MissionContext::activeAndRunning above already models (the
    // identical two globals, 0x014c8460/0x014c7a14, per Sec6.21's own
    // cross-reference). Both absent/unresolved -> clears the override
    // (0x006cf050); BOTH resolved -> sets it (0x006d1960); EXACTLY ONE
    // resolved -> CONFIRMED no-op, neither path runs. This project has no
    // real `+0x4c` field/transform data to read off either resolved object
    // (the same OPEN item Sec6.21's own text leaves for teleport, Sec4.3,
    // too) - only which of the three outcomes occurred is recorded.
    struct MissionCancelWarpLocation {
        bool set = false; // true after a successful two-name set, false after a clear or at construction
        std::string fromName;
        std::string toName;
        uint64_t setCount = 0;
        uint64_t clearCount = 0;
        uint64_t partialNoOpCount = 0; // test-observable only - exactly one of the two names resolved
    };
    MissionCancelWarpLocation& missionCancelWarpLocation() { return missionCancelWarpLocation_; }

    // mission_set_checkpoint (Sec1.6)'s own record-keeping. Kept separate
    // from MissionContext above since it is scoped to this one function's
    // own arguments/effects, not shared with mission_end_failure/
    // character_is_dead.
    struct MissionCheckpoint {
        // Stashed via the mission-context bookkeeping call (0xbd9c10,
        // Sec1.6) - test-observable only, the real bookkeeping effect
        // itself is not modeled further (no in-scope reader).
        std::string lastName;
        uint64_t setCount = 0;
        // arg 2 == false's own further gate (Sec1.6): the fixed, hardcoded
        // checkpoint-vehicle object (literal hash 0x31d152c, singleton
        // 0x24433a8) resolves AND the current default character is
        // confirmed inside it via the ALREADY-CONFIRMED occupancy predicate
        // 0x009b9290 (Sec3.10) - collapsed into one combined, derived
        // predicate since the resolved object's own real-world name is
        // explicitly NOT pinned to a human-readable string this project
        // could key a normal objectResolves()/getOrCreateCharacter() lookup
        // by (Sec1.6's own text: "HIGH CONFIDENCE... not independently
        // pinned to a human-readable name"). OPEN until a test sets it;
        // only read when arg 2 is false - the spec's own short-circuit ("if
        // arg 2 is true... the function always snapshots from the generic
        // default-character accessor" without consulting this at all), so
        // this project's stub mirrors that short-circuit exactly and never
        // forces a test to set this when arg 2 is true.
        OpenValue<bool> usePlayerVehicleTransform{
            "fixed checkpoint-vehicle resolves and local player occupies it "
            "(0x31d152c/0x24433a8/0xad3340/0xad3350/0x009b9290)",
            "spec-lua-api-behaviour.md Sec1.6"};
        bool lastUsedVehicleTransform = false; // which branch the last successful call actually took - test-observable
        bool lastLoadoutSlotsZeroed = false;   // arg 3's own branch (Sec1.6) - test-observable; no real template data loaded (see the stub's own doc comment)
    };
    MissionCheckpoint& missionCheckpoint() { return missionCheckpoint_; }

    // spec-save-format.md Sec15 wiring (2026-10-08): the live, mutable
    // subset of a real save snapshot's player-state fields this project
    // tracks (save_game_state.h's own top note has the full scope
    // statement - respect/level/difficulty/cheats/barnstorm/stunt-jump
    // counters/collectibles/the display-only mission label/zone name; cash
    // reuses the existing playerCash() below instead of a second balance).
    SaveGameState& saveGame() { return saveGame_; }

    // Where mission_autosave/game_autosave (save_game_bridge.h's
    // writeAutosaveSnapshot()) write the real sr3save_NN.sr3s_pc file this
    // task's own brief asks for. No prior save-path concept existed in this
    // project (checked before adding this - save_browser.cpp takes a
    // directory as a CLI argument instead) - defaults to a clearly-labelled
    // scratch location (the OS temp directory, process-id-qualified so
    // concurrent test binaries never race on the same slot file) rather
    // than any real game save-data location, which this project has no
    // business writing into (see sr3io::isOutputPathSafe's own protected-
    // root list, output_path_guard.h, which every write still goes through
    // regardless of this setting). A test overrides it to its own sandbox
    // directory.
    std::string& saveGameDirectory() { return saveGameDirectory_; }

    // mission_set_checkpoint's OTHER real mode (Sec11.2/Sec15.3): the same
    // writer, built into caller-owned memory instead of a file - "no hash,
    // no file write... an in-memory checkpoint snapshot, not a file save"
    // (this task's own brief). Stored here (rather than discarded) so a
    // later load can retrieve it - see save_game_bridge.h's
    // applyCheckpointSnapshot(). std::nullopt = "no checkpoint set yet."
    std::optional<std::vector<uint8_t>>& lastCheckpointSnapshot() { return lastCheckpointSnapshot_; }
    uint64_t& checkpointSnapshotBuildCount() { return checkpointSnapshotBuildCount_; }

    // Test/observability counters for the real file-write side (save_game_
    // bridge.h's writeAutosaveSnapshot()) - mirrors this project's own
    // established "plain call counter, not a spec fact" convention (e.g.
    // MissionContext::missionEndFailureDispatchCount above).
    uint64_t& autosaveFileWriteCount() { return autosaveFileWriteCount_; }
    std::string& lastAutosaveFilePath() { return lastAutosaveFilePath_; }
    std::string& lastAutosaveError() { return lastAutosaveError_; }

    // trigger_enable (Sec1.1). CONFIRMED: resolve+liveness gate (the shared
    // objectResolves() map, standing in for the real `0x005e4e30`/
    // `0x00853b10` pair per Sec29's "one shared name map" convention), then
    // "mutates trigger-subsystem state for the named trigger (an
    // enable/disable toggle plus a flag-gated secondary behaviour switch)".
    // This project has no real trigger-subsystem simulation (same
    // boundary as every other placed-object setter in this header) - only
    // the raw (enabled, flags) pair actually passed is recorded, honestly,
    // as "a request" (same convention as MinimapIconRecord/city_zone_swap
    // above), never the real two tail setters' own internal effects.
    void recordTriggerEnable(const std::string& name, bool enabled, int64_t flags) {
        triggerEnable_[name] = {enabled, flags};
    }
    bool triggerEnableKnown(const std::string& name) const { return triggerEnable_.count(name) != 0; }
    bool triggerEnableValue(const std::string& name) const {
        auto it = triggerEnable_.find(name);
        return it != triggerEnable_.end() && it->second.enabled;
    }
    int64_t triggerEnableFlagsValue(const std::string& name) const {
        auto it = triggerEnable_.find(name);
        return it != triggerEnable_.end() ? it->second.flags : 0;
    }

    // on_trigger (Sec1.2). CONFIRMED (full re-derivation 2026-10-07): the
    // per-trigger `+0xa8` gate is never written anywhere inside
    // `0x00a58680`/`0x00a58720`/`0x00a56cb0` themselves, and this project
    // models no OTHER writer at all (none is in scope) - so in this closed
    // world the gate always reads its zero-filled-allocation default
    // (unset), meaning registration always proceeds once the trigger
    // resolves. The real per-trigger deadline recompute (`+0x88`) has no
    // in-scope reader (no Lua-visible "read this trigger's deadline"
    // function is among this batch's 12 names) and is therefore not
    // modeled - only the CONFIRMED, testable half of the contract (resolve-
    // gate, then register-or-not) is reproduced, same "record the real
    // branch, not the unreadable internal bookkeeping" convention as
    // mesh_mover_reset/sidewalk_disable_nodes elsewhere in this header.
    void registerOnTriggerCallback(const std::string& triggerName, const std::string& callbackName) {
        onTriggerCallbacks_[triggerName] = callbackName;
    }
    bool onTriggerCallbackKnown(const std::string& triggerName) const { return onTriggerCallbacks_.count(triggerName) != 0; }
    const std::string& onTriggerCallbackValue(const std::string& triggerName) const {
        static const std::string kEmpty;
        auto it = onTriggerCallbacks_.find(triggerName);
        return it != onTriggerCallbacks_.end() ? it->second : kEmpty;
    }
    // For runPlaytest()'s own per-frame trigger-geometry auto-register (see
    // EngineState::TriggerVolumeGeometry's own doc comment): every trigger
    // name an on_trigger() call has registered a callback for so far.
    const std::unordered_map<std::string, std::string>& onTriggerCallbacksMap() const { return onTriggerCallbacks_; }

    // objective_text_clear (Sec1.4). CONFIRMED: no resolve/handle-lookup
    // step at all (unlike every other function in the §1 cluster) - just a
    // slot index, a zero-argument housekeeping call (0x00d1e0c0, no
    // observable effect in this project's scope) and a dispatch
    // (0x007eda00 -> 0x007ed7d0, opcode 9) clearing the global objective-
    // text/HUD slot (spec-lua-api-behaviour.md Sec1.4's own text: "clears
    // the named objective-text/HUD slot", independently strengthened by
    // direct disassembly of 0x007eda00 itself - not merely inferred from
    // address proximity to Sec1.3's setter).
    //
    // UPDATED (spec-hud-runtime.md §6.5 HUD-objective-overlay task,
    // 2026-10-09): when this doc comment was first written, objective_text
    // (Sec1.3) was NOT yet one of this batch's implemented names, so only
    // the raw call itself (which slot index, how many times) was test-
    // observable - there was no slot state to actually clear yet. Sec1.3 is
    // now implemented (lua_spec_confirmed_stubs_b08_batch1_core.cpp,
    // objectiveTextSlots()), so this call now ALSO erases the slot from
    // that map, completing the CONFIRMED "clears the slot" behaviour rather
    // than leaving it a counter-only stub forever. Sec1.4's own text
    // documents no bounds check on the slot index at all (unlike Sec1.3's
    // setter's CONFIRMED "< 4"), so no range check is added here either -
    // std::unordered_map::erase() on any int64_t key, in or out of [0,4),
    // is always safe-by-construction (a no-op if the key is absent), with
    // no hardening divergence to label or count.
    void recordObjectiveTextClear(int64_t slotIndex) {
        ++objectiveTextClearCounts_[slotIndex];
        objectiveTextSlots_.erase(slotIndex);
    }
    uint64_t objectiveTextClearCount(int64_t slotIndex) const {
        auto it = objectiveTextClearCounts_.find(slotIndex);
        return it != objectiveTextClearCounts_.end() ? it->second : 0;
    }

    // notoriety_force_no_spawn (Sec3.6). CONFIRMED: arg 1 names a
    // notoriety/gang TRACK, resolved via a lookup DISTINCT from the general
    // named-object resolver (objectResolves()) - 0x0094cc60, a team/gang-
    // name-to-id hash, into one of codes {1,2,3,4,5,6} or the CONFIRMED
    // real failure sentinel `9` on a miss. The real string<->code mapping
    // is explicitly OPEN per Sec3.6's own text (two independent passes,
    // using two different methods, both failed to locate the registry's
    // populating code) - this project exposes a test-only registration
    // hook, mirroring the already-established lookupSyncedActionIndex()/
    // registerSyncedActionForTesting() "never-populated object space"
    // precedent above. An unregistered name answers the REAL CONFIRMED
    // failure sentinel, 9 - not an invented default, exactly the code's own
    // documented fallback (Sec3.6: "an unrecognized track name is silently
    // a no-op").
    static constexpr int32_t kNotorietyTrackCodeNotFound = 9;
    void registerNotorietyTrackCodeForTesting(const std::string& name, int32_t code) {
        notorietyTrackCodes_[name] = code;
    }
    int32_t lookupNotorietyTrackCode(const std::string& name) const {
        auto it = notorietyTrackCodes_.find(name);
        return it != notorietyTrackCodes_.end() ? it->second : kNotorietyTrackCodeNotFound;
    }
    // One per-track flag byte, keyed by the EXACT literal track-record base
    // address Sec3.6's own re-confirmed disassembly enumerates (the
    // `0x008ecda0(this, flag)` setter's own `this` pointer - CONFIRMED
    // `mov byte ptr [ecx+0x455], al`, a direct SET to the passed boolean,
    // not an XOR toggle despite this section's own looser "toggles" prose
    // elsewhere). CHOSEN false ("never toggled") initial state for each -
    // no spec states an initial real game-data value for any of these
    // bytes, only the toggle mechanism itself.
    void setNotorietyTrackFlag(uint32_t trackBaseAddress, bool value) { notorietyTrackFlags_[trackBaseAddress] = value; }
    bool notorietyTrackFlagKnown(uint32_t trackBaseAddress) const { return notorietyTrackFlags_.count(trackBaseAddress) != 0; }
    bool notorietyTrackFlagValue(uint32_t trackBaseAddress) const {
        auto it = notorietyTrackFlags_.find(trackBaseAddress);
        return it != notorietyTrackFlags_.end() && it->second;
    }

    // game_is_active_input_gamepad (Sec2.6): 0x0141250d (control context
    // 0x01411b18+0x9f5), CONFIRMED zero-filled .data (initial value 0/
    // "false", "not gamepad" - a real stated construction value, unlike
    // most OpenValue fields in this header). The real per-frame writer
    // chain (0x00707440->0x005be880->0x005bcde0->0x005bc9d0) is not run by
    // this host (no per-frame update loop drives player-runtime input yet,
    // per inputSystem()'s own doc comment above) - a plain bool stand-in,
    // test-settable, starting at its real CONFIRMED construction value
    // rather than an OpenValue (there is nothing to be OPEN about: the
    // start-up value itself is CONFIRMED, only the per-frame updates are
    // unmodeled).
    bool& activeInputIsGamepad() { return activeInputIsGamepad_; }

    // --- Batch 2026-10-08, wave 1 batch 1: spec-lua-api-behaviour.md §1-4
    // trigger/mission/teleport/combat-hook core. 15 of the batch's
    // originally-assigned 21 names were actually cleared for
    // implementation; 6 (on_dismiss §1.11, character_prevent_flinching
    // §3.14, character_allow_ragdoll §3.16, npc_combat_enable §3.17,
    // inv_weapon_add_temporary §4.10, inv_weapon_remove_temporary §4.11)
    // each still carried their OWN original 2026-09-30 "NOT yet cleared for
    // implementation" Review-status line with no later dated resolution
    // anywhere in the document at the time (independently verified by
    // reading every one of this batch's 21 sections directly, not assumed
    // from the scoping report's own count) - deferred, not implemented at
    // the time. See lua_spec_confirmed_stubs_b08_batch1_core.cpp's own top
    // comment for the full original citation of each. ~~DONE 2026-10-08
    // (later the same day): spec-lua-api-behaviour.md's own text for all 6
    // was independently re-derived and dated "cleared for implementation"
    // (Sec1.11/Sec3.14/Sec3.16/Sec3.17/Sec4.10/Sec4.11's own Review-status
    // lines) - all 6 are now implemented, in
    // lua_spec_confirmed_stubs_b28_batch1_deferred6.cpp. See that file's
    // own top comment, CharacterState::onDismissCallback/
    // kPreventFlinchingBit1c98/kNeverRagdollBit1c98/
    // humanAiDataForceFlagsByte8 (character_state.h), and
    // weaponDefinitionResolves()/weaponTemporaryInventory() below for the
    // supporting state.~~ ------------------------------------------------

    // on_trigger_exit (Sec1.7): structurally identical to registerOnTriggerCallback
    // above (on_trigger, Sec1.2) but a SEPARATE, adjacent per-trigger
    // pending-state dword (+0xfc, not +0xf8) and literal mode value 1 (not
    // 0) - a second, independent callback slot on the SAME trigger
    // object. Same "+0xa8 gate always reads unset in this closed world"
    // reasoning as registerOnTriggerCallback (registration always
    // proceeds once the trigger resolves).
    void registerOnTriggerExitCallback(const std::string& triggerName, const std::string& callbackName) {
        onTriggerExitCallbacks_[triggerName] = callbackName;
    }
    bool onTriggerExitCallbackKnown(const std::string& triggerName) const { return onTriggerExitCallbacks_.count(triggerName) != 0; }
    const std::string& onTriggerExitCallbackValue(const std::string& triggerName) const {
        static const std::string kEmpty;
        auto it = onTriggerExitCallbacks_.find(triggerName);
        return it != onTriggerExitCallbacks_.end() ? it->second : kEmpty;
    }
    const std::unordered_map<std::string, std::string>& onTriggerExitCallbacksMap() const { return onTriggerExitCallbacks_; }

    // --- Trigger-volume geometry + the real per-frame overlap test
    // (spec-lua-api-behaviour.md Sec1.14, 2026-10-09 batch) -------------
    //
    // Sec1.14 CONFIRMS the RUNTIME trigger handle's own field layout (shape
    // code `+0x8c`: 0=oriented box, 1=sphere, 2=capped cylinder; position
    // `+0x40`; box half-extents/cylinder axis endpoint `+0x58`/`+0x5c`/
    // `+0x60`; radius `+0x90`; length `+0x94`) and the real per-frame poll
    // mechanism's own SHAPE (a tracked "currently inside" set - enter/exit
    // is a set-membership TRANSITION, never a steady per-frame refire).
    // This struct/test models exactly that CONFIRMED shape.
    //
    // RESOLVED 2026-10-10 (spec-lua-api-behaviour.md Sec1.14's own "On-disk
    // gaps - RESOLVED 2026-10-10" paragraph, cross-referenced from spec-zone-
    // data-format.md Sec11.7's own "A third class resolved" paragraph):
    // `Trigger_Volume`'s class-specific tail-property parser (vtable `+0x04`,
    // `0x0093e520`) is now read in full. The on-disk encoding: `trigger_shape_
    // type` is an authored STRING resolved through a name-to-enum lookup into
    // the same runtime shape code `+0x8c` (0/1/2); `trigger_shape_param1`/
    // `trigger_shape_param2` are then read depending on that code - box
    // (0): BOTH as 3-component float vectors, the box's own local-space
    // min/max corner (`+0x90`-`+0x98`/`+0x9c`-`+0xa4`), independently
    // cross-confirmed against the object's own bounding-box virtual method;
    // sphere (1): param1 only, a scalar f32 radius (`+0x90`), param2 not
    // read; cylinder (2): both scalars, param1=radius (`+0x90`), param2=
    // length (`+0x94`). This project reads this real data via sr3zone's own
    // TriggerShapeType()/TriggerShapeParam1()/TriggerShapeParam2()/
    // DecodePropertyAsFloat()/DecodePropertyAsVec3() (include/sr3zone/
    // zone_objects.h) plus TriggerShapeCodeForTypeString()'s own empirical
    // (real-population-scan-grounded, NOT guessed - see that function's own
    // doc comment) string-to-enum mapping - Sec1.14 itself does not give the
    // on-disk string's exact spellings ("literal shape-name strings not
    // traced to full depth"), so this project determined them the only
    // clean-room-safe way available to it: by reading real `.czn_pc` data
    // (tools/zone_object_resolver_probe.cpp), never by disassembling the
    // name-to-enum lookup itself. **Still genuinely OPEN, per Sec1.14's own
    // text, and therefore NOT modelled here either:** the cylinder's second
    // axis-endpoint field (`+0x58`-`+0x60`, used by the precise cylinder
    // containment test but written by neither the tail parser nor the
    // constructor) - `ex`/`ey`/`ez` below stay a CHOSEN caller-supplied value
    // for a cylinder trigger specifically, exactly as before this task;
    // only the box/sphere/cylinder radius+length fields moved from CHOSEN to
    // REAL. registerTriggerGeometry() still takes a caller-supplied
    // geometry - the REAL loader (tools/sr3_viewer.cpp's own runPlaytest())
    // now populates it from real `.czn_pc` tail properties instead of a
    // flat CHOSEN sphere stand-in, but a geometry is still constructed and
    // handed in by a caller, not read directly by this struct.
    struct TriggerVolumeGeometry {
        double px = 0, py = 0, pz = 0;     // +0x40 - real, from sr3zone::ObjectPosition() when known
        int shapeCode = 1;                 // +0x8c - CONFIRMED values 0/1/2 only; CHOSEN default 1 (sphere)
        // Box only (shapeCode==0): REAL on-disk local-space min/max corner
        // (trigger_shape_param1/param2), relative to (px,py,pz) - NOT a
        // symmetric half-extent around the position (an authored box's own
        // min/max corner need not be centred on its placed-object origin).
        double boxMinX = 0, boxMinY = 0, boxMinZ = 0;
        double boxMaxX = 0, boxMaxY = 0, boxMaxZ = 0;
        double ex = 0, ey = 0, ez = 0;      // +0x58/+0x5c/+0x60 - cylinder's second axis endpoint
                                            // ONLY (box no longer uses this - see boxMin/boxMax
                                            // above); still CHOSEN/caller-supplied - Sec1.14's own
                                            // "true source not identified" OPEN item, unresolved by
                                            // this task.
        double radius = 0;                 // sphere: param1/+0x90; cylinder: param2/+0x94 (f752324)
        double length = 0;                 // cylinder axis length: param1/+0x90 (f752324), not
                                            // separately consumed by the containment test below
                                            // (which uses the two axis endpoints directly) - Sec1.14
                                            // does not spell out its exact relationship to the
                                            // endpoint pair, so nothing here guesses at one.
        bool positionKnown = false;        // true once a real zone-data position was applied
        bool shapeKnown = false;           // true once real on-disk trigger_shape_type/param1/param2
                                            // were decoded for this trigger (vs. a CHOSEN fallback)
    };

    // trigger_get_radius final value (Sec21.13, e6691f1). Cylinder returns the +0x94 field (stored in
    // `length`, the on-disk trigger_shape_param2), per the spec's corrected offsets.
    static double triggerRadiusForGeometry(const TriggerVolumeGeometry& g) {
        if (g.shapeCode == 1) return g.radius;
        if (g.shapeCode == 2) return g.radius; // +0x94 = radius (f752324); length is +0x90
        if (g.shapeCode == 0) {
            float dx = float(g.boxMaxX - g.boxMinX), dy = float(g.boxMaxY - g.boxMinY), dz = float(g.boxMaxZ - g.boxMinZ);
            float sum = float(double(dx) * dx + double(dy) * dy + double(dz) * dz);
            return double(float(0.5 * std::sqrt(double(sum))));
        }
        return 0.0;
    }
    void registerTriggerGeometry(const std::string& triggerName, const TriggerVolumeGeometry& geometry) {
        triggerGeometry_[triggerName] = geometry;
    }
    // Registers geometry for `triggerName` from the loaded zone data (real position + real shape when
    // decodable, else the CHOSEN radius-3 sphere fallback). Returns false when the name does not resolve
    // or has no position property. `note` (optional) receives a one-line description for logging.
    // Shared by the viewer's per-frame auto-register and object_is_in_trigger (Sec1.12: the same geometry
    // serves the containment test and the ENTER/EXIT dispatch).
    bool registerTriggerGeometryFromZoneData(const std::string& triggerName, std::string* note = nullptr);
    const TriggerVolumeGeometry* triggerGeometry(const std::string& triggerName) const {
        auto it = triggerGeometry_.find(triggerName);
        return it != triggerGeometry_.end() ? &it->second : nullptr;
    }
    // For runPlaytest()'s own per-frame walk: every trigger this session has
    // loaded geometry for, by name.
    const std::unordered_map<std::string, TriggerVolumeGeometry>& triggerGeometryMap() const { return triggerGeometry_; }

    // The real per-frame poll's own CONFIRMED edge-detector shape (Sec1.14:
    // "a trigger is only re-tested/fired while tracked in this set, and
    // set-membership transitions are the enter/exit events") - one bool per
    // trigger name, defaulting to "not inside" (a session always starts
    // with the player outside every trigger, never mid-volume).
    bool& triggerCurrentlyInside(const std::string& triggerName) { return triggerInsideState_[triggerName]; }

    // The real containment test, CONFIRMED shape (Sec1.14, `0x0093bfc0`):
    // three shape codes, no others. Box is tested axis-aligned (this
    // project does not decode a placed object's own orientation quaternion
    // for a generic Trigger_Volume - same boundary sr3zone::
    // DecodePropertyAsPosition's own doc comment already states for
    // Navpoint/cover_node). Never throws; an unknown shapeCode (anything
    // but 0/1/2) reports not-contained, matching Sec1.14's own "any other
    // value fails the containment test" text.
    static bool TriggerVolumeContainsPoint(const TriggerVolumeGeometry& g, double x, double y, double z) {
        const double dx = x - g.px, dy = y - g.py, dz = z - g.pz;
        switch (g.shapeCode) {
            case 1: // sphere
                return (dx * dx + dy * dy + dz * dz) <= (g.radius * g.radius);
            case 0: // oriented box - axis-aligned here, see this struct's own doc comment.
                     // REAL on-disk min/max corner (boxMin/boxMax), not a symmetric half-extent.
                return dx >= g.boxMinX && dx <= g.boxMaxX && dy >= g.boxMinY && dy <= g.boxMaxY &&
                       dz >= g.boxMinZ && dz <= g.boxMaxZ;
            case 2: { // capped cylinder: axis from (px,py,pz) to (px+ex,py+ey,pz+ez)
                const double segLen2 = g.ex * g.ex + g.ey * g.ey + g.ez * g.ez;
                const double t = segLen2 > 1e-12 ? (dx * g.ex + dy * g.ey + dz * g.ez) / segLen2 : 0.0;
                if (t < 0.0 || t > 1.0) return false;
                const double cx = g.px + g.ex * t, cy = g.py + g.ey * t, cz = g.pz + g.ez * t;
                const double rdx = x - cx, rdy = y - cy, rdz = z - cz;
                return (rdx * rdx + rdy * rdy + rdz * rdz) <= (g.radius * g.radius);
            }
            default:
                return false;
        }
    }

    // --auto-objective's own trigger-stall teleport target (feature/auto-
    // objective-trigger-vehicle-seat, 2026-10-12): a representative point
    // GUARANTEED (for every shape this project decodes) to satisfy
    // TriggerVolumeContainsPoint() above for the SAME geometry - the
    // trigger-volume analogue of get_dist's own "warp to the target's real
    // resolved position" teleport. Sphere/box/cylinder all return a point on
    // (box/cylinder) or at (sphere) the shape's own real center, never an
    // edge/corner, so a player warped here lands solidly inside, not right
    // on a containment boundary.
    static WorldPosition TriggerVolumeRepresentativePoint(const TriggerVolumeGeometry& g) {
        switch (g.shapeCode) {
            case 0: // box: midpoint of the real on-disk min/max corner, offset from (px,py,pz)
                return WorldPosition{g.px + (g.boxMinX + g.boxMaxX) * 0.5,
                                      g.py + (g.boxMinY + g.boxMaxY) * 0.5,
                                      g.pz + (g.boxMinZ + g.boxMaxZ) * 0.5};
            case 2: // capped cylinder: midpoint of the axis (px,py,pz)-(px+ex,py+ey,pz+ez)
                return WorldPosition{g.px + g.ex * 0.5, g.py + g.ey * 0.5, g.pz + g.ez * 0.5};
            case 1: // sphere
            default:
                return WorldPosition{g.px, g.py, g.pz};
        }
    }

    // spawn_region_enable (Sec1.8): per-spawn-region byte +0x94 bit 0x1,
    // CONFIRMED branchless conditional-XOR, single-bit-only scope. This
    // project's flat objectResolves() map stands in for the dedicated
    // 0x006ed4d0 registry (Sec29's "one shared name map" convention,
    // already applied project-wide to vehicles/characters/placed objects).
    std::unordered_map<std::string, bool>& spawnRegionEnabled() { return spawnRegionEnabled_; }

    // objective_text (Sec1.3): see ObjectiveTextSlot's own doc comment
    // above for the full reasoning (map-keyed-by-slot, not a fixed array;
    // localization is OPEN and not modelled; the arg3/arg4 combining rule
    // is reproduced). A slot's PRESENCE in this map (count() != 0) is this
    // project's own "is this slot currently showing text" signal - the HUD
    // objective-text overlay (tools/sr3_viewer.cpp, spec-hud-runtime.md
    // §6.5) iterates slots 0..3 and only draws a slot present here.
    // objective_text_clear (Sec1.4, recordObjectiveTextClear() above) erases
    // the entry on clear, so a cleared slot is never left stale in this map.
    std::unordered_map<int64_t, ObjectiveTextSlot>& objectiveTextSlots() { return objectiveTextSlots_; }

    // door_open (Sec1.10): resolved through the SAME named-placed-object
    // registry door_close/door_lock/mesh_mover_* already share
    // (getOrCreatePlacedObject() above) - see PlacedObjectState's own new
    // door_open fields for the per-door state this native reads/writes.

    // audio_conversation_play (Sec2.10): see ConversationPlay's own doc
    // comment above for the full reasoning.
    ConversationPlay& conversationPlay() { return conversationPlay_; }

    // game_peg_unload (Sec2.7): this project has no real PEG-pointer-
    // array/load-state tracking anywhere (game_peg_load_with_cb, Sec8.24,
    // does not model one either - see PegLoadRequest's own doc comment
    // above) - a per-filename OPEN gate stands in for "is this resource
    // currently in one of the two CONFIRMED loaded states (2 or 3)",
    // test-settable; `known(name)==false` (never configured) is treated
    // as the CONFIRMED "not found/not loaded -> silent no-op" default
    // rather than a genuine OPEN refusal, since Sec2.7's own text already
    // states that outcome plainly for an unregistered/not-loaded name.
    OpenValueMap<bool>& pegInLoadedState() { return pegInLoadedState_; }
    void recordPegUnload(const std::string& name) { ++pegUnloadCount_[name]; }
    uint64_t pegUnloadCount(const std::string& name) const {
        auto it = pegUnloadCount_.find(name);
        return it != pegUnloadCount_.end() ? it->second : 0;
    }

    // pop_screen (Sec2.8): no in-scope push in this batch (push_screen,
    // Sec8.26, is out of scope) to populate a real starting depth - this
    // project's own starting depth is CHOSEN 0 (a fresh/empty screen
    // stack), not a spec-stated value. A test can increment this directly
    // to exercise a real pop.
    int32_t& uiScreenStackDepth() { return uiScreenStackDepth_; }
    uint64_t& popScreenCount() { return popScreenCount_; }

    // object_is_in_trigger (Sec1.12): CONFIRMED (2026-10-01) arg 1 =
    // trigger (objectResolves()), arg 2 = object (ALSO objectResolves() -
    // this project's flat stand-in for the real position-lookup resolver
    // 0x00a455a0, same convention VehicleState::worldPosition's own doc
    // comment already applies) - arg 2 is parsed UNCONDITIONALLY even if
    // arg 1 failed to resolve (CONFIRMED), reproduced by reading both
    // before branching. The real geometric containment test (0x0093bfc0,
    // box/sphere/cylinder against the trigger's own position+extents) has
    // no in-scope geometry/extents data anywhere in this project (no
    // coordinate system modelled, the same stated project-wide gap
    // VehicleState::worldPosition's own doc comment notes) - the test's
    // own boolean OUTCOME is tracked as its own OPEN fact, keyed by the
    // (trigger, object) pair, rather than invented from position data
    // this project does not have.
    OpenValueMap<bool>& triggerContainsObject() { return triggerContainsObject_; }
    static std::string triggerContainsObjectKey(const std::string& triggerName, const std::string& objectName) {
        return triggerName + "\x1f" + objectName;
    }

    // Shop/crib walk-in entry -> "press E" prompt -> document open.
    //
    // UPDATED 2026-10-11 (commit b8c17ec, same day as this task): the CRIB
    // half of this is NO LONGER a stand-in - spec-store-runtime.md Sec2's
    // "CLOSED, 2026-10-11" paragraph gives the full, every-hop-confirmed
    // chain: press `0x52` -> `0x005b6870` (the real 22-entry bit-walk) ->
    // `0x0111f2d0[8]` (`0x009da580`, prompt text `"HUD_USE_CRIB"`) ->
    // `0x005eee70` -> `0x0080cc90(this=0x022ccef8)` - the already-named
    // "store_crib" static object - -> `0x007b5fe0(this)`, the GENERIC
    // per-object document-open call (the structural equivalent, for an
    // arbitrary document object, of what `0x0083ECC0` does by hardcoded name
    // specifically for `store_common`). SHOP's own entry was explicitly NOT
    // separately identified that pass (no `"HUD_USE_STORE"`-named slot found
    // among the ones sampled) - it almost certainly goes through the exact
    // same `0x0080cc90`/`0x007b5fe0` generic-open pattern on the
    // `store_common` static object instead, from a different, still
    // unidentified array index (spec-store-runtime.md Sec2's own "cheap,
    // bounded follow-up" framing) - so SHOP entry remains the CHOSEN
    // stand-in this file originally landed, labeled per this project's
    // established "CHOSEN, static search exhausted" convention (HANDOFF.md):
    // TODO: real native trigger condition (which array index) unresolved for
    // SHOP specifically, see spec-store-runtime.md Sec2.
    //
    // What neither half of this models (both before and after the above
    // update) is the interaction-prompt table-walk/bit-availability
    // machinery itself (`0x005b6870`'s own 22-entry bit-walk, or what sets
    // bit 8/whichever shop bit in the player's `+0x2084` context word) -
    // this project has no C++ implementation of that engine-side table at
    // all (it is not Lua-bound). Instead, for BOTH shop and crib, this
    // reuses the SAME two EXISTING generic mechanisms end to end: the
    // trigger-volume overlap/geometry machinery just above
    // (registerTriggerGeometry()/triggerGeometryMap()/
    // TriggerVolumeContainsPoint(), already wired into tools/sr3_viewer.cpp's
    // own per-frame trigger poll) for "is the player inside" (standing in,
    // for both cases alike, for the still-unmodeled "+0x2084 bit N is set"
    // condition - CHOSEN for both, this part did not change with b8c17ec),
    // and recordHudPrompt() (Sec13.3's hud_prompt native, stub_hud_prompt in
    // src/lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp) for "show the
    // press-E prompt" - the SAME HUD-prompt-show call the vehicle-enter
    // prompt documentation (spec-player-runtime.md Sec13.1) describes this
    // engine using for its own "press E to enter a vehicle" case, reused
    // here rather than inventing a second prompt-show path. What DID change
    // with b8c17ec is which document name the press actually opens, and
    // whether that open-target attribution is REAL or CHOSEN - see
    // ShopCribTriggerKind and pressShopCribInteract() below.
    //
    // Deliberately does NOT encode on-disk trigger "kind" (spec-save-
    // format.md Sec9.4: kind 3 = "crib purchase," for an UNOWNED crib only -
    // the owned-crib entry kind is itself still unidentified per that
    // section's own note) - a caller marks any trigger name it already knows
    // is a walk-in shop or crib door via registerShopCribTrigger(),
    // independent of which on-disk kind index (if any) it would carry; the
    // kind this struct DOES care about is shop-vs-crib, because (post-
    // b8c17ec) that now selects a genuinely different, differently-sourced
    // open-target document name.
    enum class ShopCribTriggerKind {
        kShop = 0, // opens "store_common" - CHOSEN target+trigger-index (see above)
        kCrib = 1, // opens "store_crib" - REAL confirmed target+chain (b8c17ec)
    };
    void registerShopCribTrigger(const std::string& triggerName, ShopCribTriggerKind kind = ShopCribTriggerKind::kShop) {
        shopCribTrigger_[triggerName] = kind;
    }
    bool isShopCribTrigger(const std::string& triggerName) const {
        return shopCribTrigger_.count(triggerName) != 0;
    }
    std::optional<ShopCribTriggerKind> shopCribTriggerKind(const std::string& triggerName) const {
        auto it = shopCribTrigger_.find(triggerName);
        if (it == shopCribTrigger_.end()) return std::nullopt;
        return it->second;
    }
    const std::unordered_map<std::string, ShopCribTriggerKind>& shopCribTriggerMap() const { return shopCribTrigger_; }

    // Per-frame-equivalent update, given the player's current world
    // position: finds whether the player is now inside ANY registered
    // shop/crib trigger (first match wins - this project does not model
    // overlapping shop/crib volumes) and shows/clears the "press E" prompt
    // on a TRANSITION only, matching this project's own pre-existing
    // trigger-poll "only re-fire on enter/exit, never every frame while
    // inside" convention (triggerCurrentlyInside()'s own doc comment,
    // Sec1.14). Returns the name of the trigger now active, or an empty
    // string if the player is not inside any shop/crib trigger - mirrors
    // activeShopCribTrigger() below, exposed as a return value purely so a
    // caller/test does not need a second call to observe the transition.
    const std::string& updateShopCribTriggerPrompt(double playerX, double playerY, double playerZ) {
        std::string nowInside;
        for (const auto& kv : shopCribTrigger_) {
            const TriggerVolumeGeometry* g = triggerGeometry(kv.first);
            if (!g) continue;
            if (TriggerVolumeContainsPoint(*g, playerX, playerY, playerZ)) {
                nowInside = kv.first;
                break;
            }
        }
        if (nowInside != activeShopCribTrigger_) {
            activeShopCribTrigger_ = nowInside;
            if (!activeShopCribTrigger_.empty()) {
                // Reuses hud_prompt's own existing request shape (target,
                // arg2, arg3). arg2 is this stand-in's own open-target
                // document name (see pressShopCribInteract() below for which
                // one is REAL vs CHOSEN); the real prompt's own icon/text
                // argument shape (e.g. the real "HUD_USE_CRIB" string
                // confirmed by b8c17ec for the crib case specifically) is
                // not reproduced here - CHOSEN for both cases.
                recordHudPrompt(activeShopCribTrigger_, openTargetForTrigger(activeShopCribTrigger_), "press_e_enter");
            } else {
                recordHudPromptDefaultReset();
            }
        }
        return activeShopCribTrigger_;
    }
    bool shopCribPromptShowing() const { return !activeShopCribTrigger_.empty(); }
    const std::string& activeShopCribTrigger() const { return activeShopCribTrigger_; }

    // Press-E while the prompt is showing -> opens the matching document.
    // For a kCrib trigger this is now the REAL confirmed open-target
    // ("store_crib", via the full b8c17ec chain documented above) - for a
    // kShop trigger it remains this project's own CHOSEN stand-in (target
    // "store_common" is itself the one REAL, confirmed-by-disassembly piece
    // for shop, per spec-store-runtime.md Sec1 - only the trigger-index/
    // call-path THAT REACHES it is still unidentified for shop specifically).
    // No-op (returns false) if no shop/crib prompt is currently showing -
    // matches this engine's own "a button press only does something if its
    // matching prompt is live" shape (the vehicle-enter prompt is the same
    // shape, spec-player-runtime.md Sec13.1). On success, increments
    // uiScreenStackDepth() (the existing generic screen-stack-push signal
    // this project's pop_screen counterpart, popScreenCount()/
    // uiScreenStackDepth(), already uses) AND a dedicated
    // documentOpenCount()/lastDocumentOpened()/lastDocumentOpenTrigger()
    // triple, so a test/caller can observe "a screen opened" both generically
    // and specifically-attributed, including WHICH document name opened.
    bool pressShopCribInteract() {
        if (activeShopCribTrigger_.empty()) return false;
        ++documentOpenCount_;
        lastDocumentOpenTrigger_ = activeShopCribTrigger_;
        lastDocumentOpened_ = openTargetForTrigger(activeShopCribTrigger_);
        ++uiScreenStackDepth();
        return true;
    }
    uint64_t documentOpenCount() const { return documentOpenCount_; }
    const std::string& lastDocumentOpened() const { return lastDocumentOpened_; }
    const std::string& lastDocumentOpenTrigger() const { return lastDocumentOpenTrigger_; }

    // "store_common" for a kShop trigger, "store_crib" for a kCrib trigger -
    // see the ShopCribTriggerKind doc comment above for which is REAL vs
    // CHOSEN. Falls back to "store_common" for an unregistered name (should
    // never happen given this is only ever called with a key already present
    // in shopCribTrigger_; never throws regardless).
    std::string openTargetForTrigger(const std::string& triggerName) const {
        auto it = shopCribTrigger_.find(triggerName);
        if (it != shopCribTrigger_.end() && it->second == ShopCribTriggerKind::kCrib) return "store_crib";
        return "store_common";
    }

    // Weapon pickup (spec-weapons-runtime.md Sec5.1) -----------------------
    //
    // Two confirmed-shape paths, both funnelling through ONE shared give/
    // equip writer rather than duplicating weapon-grant logic:
    //
    //   1. Walk-over auto-pickup for ordinary (non-heavy) enemy-dropped
    //      weapons - registerWeaponDrop()/updateWeaponDropPickup() below.
    //      The REAL write side is CONFIRMED (0x00B84330 -> 0x00601380,
    //      Sec5.1's "real write path, traced end-to-end" paragraph) and the
    //      BEHAVIOUR is user-confirmed (walking over a dropped weapon picks
    //      it up automatically, except heavy pickup-only weapons). What is
    //      NOT confirmed is the real per-frame proximity TRIGGER condition
    //      itself: Sec5.1 names `0x0064c580`/`0x0064bef0` as the strongest
    //      candidate grant functions (position-match scan against a
    //      REGISTERED-SLOT array, no button press), but flags them as more
    //      likely a fixed "weapon cache station" design than the literal
    //      "walk over an NPC-dropped gun" case, and the actual per-frame
    //      dispatcher that fires them was not found at all (reached only
    //      via DATA table slots, zero direct-call xrefs - Sec5.1's own
    //      2026-10-11 changelog entry). So the exact radius/shape used here
    //      is CHOSEN, static search exhausted (same HANDOFF.md framing as
    //      this project's shop-entry stand-in, commit 36470e8) - see
    //      kWeaponDropPickupRadius below for the precise label.
    //
    //   2. Press-E pickup for heavy/"pickup-only" weapons (minigun,
    //      flamethrower, similar) - registerHeavyWeaponPickupTrigger()/
    //      updateHeavyWeaponPickupPrompt()/pressHeavyWeaponPickupInteract()
    //      below. Reuses the EXACT SAME two generic mechanisms the shop/
    //      crib unit above established (TriggerVolumeGeometry/
    //      TriggerVolumeContainsPoint for "is the player inside," and
    //      recordHudPrompt()/recordHudPromptDefaultReset() for "show the
    //      press-E prompt"), wired to interaction-array entry 0 instead of
    //      entry 8 (Sec5.1's 2026-10-11 "pickup-only interaction-prompt
    //      path" update: `0x0111f2d0[0]` -> `0x009da510` -> `0x00971b10` ->
    //      per-kind handler table `0x01309f10` -> kind handler
    //      `0x00972a70`, which calls the already-confirmed `+0x1b78`
    //      weapon-inventory locator `0x005fe460` directly - a genuine
    //      "pick up/exchange a weapon via interact prompt" action,
    //      independent of 0x00B84330's own callers). Same posture as
    //      shop/crib's own trigger-volume stand-in: the real `+0x18e0`
    //      per-"kind" table lookup and bit-availability machinery are NOT
    //      modelled (this project has no C++ model of that engine-side
    //      table at all), but the GRANT itself, once triggered, calls the
    //      real confirmed give/equip writer below - not invented.
    //
    // Both paths' weapon-definition/ammo values are caller-supplied (this
    // project has no xtbl-backed weapon-definition resolver wired to this
    // unit) - same "caller already knows what it wants to grant, this
    // project only models the grant mechanism" boundary as
    // registerShopCribTrigger()'s own open-target handling above.

    // Heavy/"pickup-only" weapon classification. The spec's own `+0x10`
    // flags-dword bits documented so far (0x200000 "has secondary ammo,"
    // 0x80000000 "runtime unlocked," Sec5.1/Sec26.7) do NOT include any bit
    // identified as "pickup-only" - the user's own ground truth (Sec5.1,
    // 2026-10-11) names exactly two examples ("minigun, flamethrower and
    // similar") with no further enumeration. CHOSEN, static search
    // exhausted: classify by Weapon_Class enum value (spec-tables-weapons-
    // combat.md's own 23-entry table) against literally the two named
    // examples only - "flamethrower" (index 14) and "minigun" (index 17).
    // No other Weapon_Class value is treated as heavy/pickup-only here; a
    // future pass with a more specific spec item (e.g. a real confirmed
    // flag bit, or the real `+0x18e0` kind value Sec5.1's own 2026-10-11
    // update says is "plausibly" this class) should replace this enum-value
    // check rather than silently growing this list by guesswork.
    static constexpr int kHeavyPickupOnlyWeaponClassFlamethrower = 14;
    static constexpr int kHeavyPickupOnlyWeaponClassMinigun = 17;
    static bool IsHeavyPickupOnlyWeaponClass(int weaponClass) {
        return weaponClass == kHeavyPickupOnlyWeaponClassFlamethrower ||
               weaponClass == kHeavyPickupOnlyWeaponClassMinigun;
    }

    // The shared give/equip writer BOTH pickup paths call - models
    // Sec5.1's REAL confirmed write path: `0x00B84330` resolves/binds the
    // target's slot via `0x00601380` (the slot create/bind call), then
    // writes flags (+0x1a0/+0x1a1), primary ammo (+0x1a4), rounds-in-clip
    // (+0x1ac - the SAME field Sec6's already-closed item names as the live
    // clip counter), and optional secondary ammo (+0x1b0/+0x1b4) onto the
    // resolved per-CHARACTER (+0x1b78) slot record. This project's closest
    // existing live model of that `+0x1b78` slot record is
    // SaveGameState::weaponInventory's WeaponInventoryItem (save_game_
    // state.h - spec-save-format.md Sec10.3's own 4-ammo-field/2-flag-byte
    // shape for the SAME save-format span `0x008DCC00` separately fills,
    // Sec5.1's own cross-reference), already noted there as "a future
    // weapon-pickup native appends to it directly" - reused here rather
    // than inventing a second, parallel slot representation. Does NOT model
    // the vehicle-mount ammo cap (+0x2c4) - gated on session/global flags
    // this project does not track, out of scope for a pickup writer.
    void giveEquipWeapon(uint32_t weaponDefId, int32_t primaryAmmo, int32_t roundsInClip,
                          int32_t secondaryAmmo = 0, uint8_t flag0 = 0, uint8_t flag1 = 0) {
        auto& inv = saveGame_.weaponInventory;
        auto it = std::find_if(inv.begin(), inv.end(),
                                [&](const WeaponInventoryItem& w) { return w.weaponDefId == weaponDefId; });
        if (it == inv.end()) {
            inv.push_back(WeaponInventoryItem{});
            it = inv.end() - 1;
        }
        it->weaponDefId = weaponDefId;
        it->ammo[0] = primaryAmmo;
        it->ammo[1] = roundsInClip;
        it->ammo[2] = secondaryAmmo;
        it->flag0 = flag0;
        it->flag1 = flag1;
        ++weaponGiveEquipCount_;
        lastWeaponGiveEquipId_ = weaponDefId;
    }
    uint64_t weaponGiveEquipCount() const { return weaponGiveEquipCount_; }
    uint32_t lastWeaponGiveEquipId() const { return lastWeaponGiveEquipId_; }
    bool hasWeapon(uint32_t weaponDefId) const { return weaponSlot(weaponDefId) != nullptr; }
    const WeaponInventoryItem* weaponSlot(uint32_t weaponDefId) const {
        const auto& inv = saveGame_.weaponInventory;
        auto it = std::find_if(inv.begin(), inv.end(),
                                [&](const WeaponInventoryItem& w) { return w.weaponDefId == weaponDefId; });
        return it == inv.end() ? nullptr : &*it;
    }

    // --- Path 1: walk-over auto-pickup for non-heavy dropped weapons ------

    // CHOSEN proximity radius, static search exhausted (see this unit's own
    // top doc comment for why: the real per-frame dispatcher that would
    // confirm an exact condition was not found by any static pass, Sec5.1).
    // A flat 1.5-metre sphere around the drop's registered position.
    static constexpr double kWeaponDropPickupRadius = 1.5;

    struct WeaponDropRegistration {
        double x = 0, y = 0, z = 0;
        uint32_t weaponDefId = 0;
        int32_t primaryAmmo = 0;
        int32_t roundsInClip = 0;
        int weaponClass = 0;
        bool consumed = false;
    };
    void registerWeaponDrop(const std::string& dropName, double x, double y, double z,
                             uint32_t weaponDefId, int weaponClass, int32_t primaryAmmo, int32_t roundsInClip) {
        weaponDrops_[dropName] = WeaponDropRegistration{x, y, z, weaponDefId, primaryAmmo, roundsInClip,
                                                          weaponClass, false};
    }
    // Per-frame-equivalent update, given the player's current world
    // position: grants (via giveEquipWeapon(), the ONE shared writer) every
    // registered, not-yet-consumed drop within kWeaponDropPickupRadius
    // whose weapon class is NOT heavy/pickup-only, then marks it consumed
    // (one-shot - a real dropped weapon does not respawn after pickup).
    // Heavy/pickup-only drops are deliberately skipped here - per the
    // user's own confirmed behaviour, they are only ever granted through
    // the press-E path below, never by walking over them.
    void updateWeaponDropPickup(double playerX, double playerY, double playerZ) {
        for (auto& kv : weaponDrops_) {
            WeaponDropRegistration& drop = kv.second;
            if (drop.consumed) continue;
            if (IsHeavyPickupOnlyWeaponClass(drop.weaponClass)) continue;
            const double dx = playerX - drop.x, dy = playerY - drop.y, dz = playerZ - drop.z;
            if ((dx * dx + dy * dy + dz * dz) <= kWeaponDropPickupRadius * kWeaponDropPickupRadius) {
                giveEquipWeapon(drop.weaponDefId, drop.primaryAmmo, drop.roundsInClip);
                drop.consumed = true;
                ++weaponDropAutoPickupCount_;
            }
        }
    }
    bool isWeaponDropConsumed(const std::string& dropName) const {
        auto it = weaponDrops_.find(dropName);
        return it != weaponDrops_.end() && it->second.consumed;
    }
    uint64_t weaponDropAutoPickupCount() const { return weaponDropAutoPickupCount_; }

    // --- Path 2: press-E pickup for heavy/pickup-only weapons --------------

    struct HeavyWeaponPickupTrigger {
        uint32_t weaponDefId = 0;
        int32_t primaryAmmo = 0;
        int32_t roundsInClip = 0;
    };
    void registerHeavyWeaponPickupTrigger(const std::string& triggerName, uint32_t weaponDefId,
                                           int32_t primaryAmmo, int32_t roundsInClip) {
        heavyWeaponPickupTrigger_[triggerName] = HeavyWeaponPickupTrigger{weaponDefId, primaryAmmo, roundsInClip};
    }
    // Same transition-only enter/exit shape as updateShopCribTriggerPrompt()
    // above (triggerCurrentlyInside()'s own "only re-fire on enter/exit"
    // convention, Sec1.14) - reuses the SAME registerTriggerGeometry()/
    // triggerGeometry()/TriggerVolumeContainsPoint() machinery, just over a
    // separate trigger-name map so a shop/crib trigger and a heavy-weapon
    // pickup trigger never collide on the same registered name.
    const std::string& updateHeavyWeaponPickupPrompt(double playerX, double playerY, double playerZ) {
        std::string nowInside;
        for (const auto& kv : heavyWeaponPickupTrigger_) {
            const TriggerVolumeGeometry* g = triggerGeometry(kv.first);
            if (!g) continue;
            if (TriggerVolumeContainsPoint(*g, playerX, playerY, playerZ)) {
                nowInside = kv.first;
                break;
            }
        }
        if (nowInside != activeHeavyWeaponPickupTrigger_) {
            activeHeavyWeaponPickupTrigger_ = nowInside;
            if (!activeHeavyWeaponPickupTrigger_.empty()) {
                // arg2/arg3 are this stand-in's own CHOSEN prompt-shape
                // values (same footing as updateShopCribTriggerPrompt()'s
                // own "press_e_enter" literal above) - the real prompt-text
                // id for this specific interaction-array entry 0 kind was
                // not independently confirmed this pass.
                recordHudPrompt(activeHeavyWeaponPickupTrigger_, "weapon_pickup", "press_e_pickup");
            } else {
                recordHudPromptDefaultReset();
            }
        }
        return activeHeavyWeaponPickupTrigger_;
    }
    bool heavyWeaponPickupPromptShowing() const { return !activeHeavyWeaponPickupTrigger_.empty(); }
    const std::string& activeHeavyWeaponPickupTrigger() const { return activeHeavyWeaponPickupTrigger_; }

    // Press-E while the heavy-weapon prompt is showing - grants the
    // registered weapon via giveEquipWeapon(), the SAME shared writer
    // updateWeaponDropPickup() above calls, proving both paths converge on
    // one write path rather than two divergent ones. No-op (returns false)
    // if no heavy-weapon pickup prompt is currently showing, matching
    // pressShopCribInteract()'s own "a button press only does something if
    // its matching prompt is live" shape.
    bool pressHeavyWeaponPickupInteract() {
        if (activeHeavyWeaponPickupTrigger_.empty()) return false;
        auto it = heavyWeaponPickupTrigger_.find(activeHeavyWeaponPickupTrigger_);
        if (it == heavyWeaponPickupTrigger_.end()) return false;
        giveEquipWeapon(it->second.weaponDefId, it->second.primaryAmmo, it->second.roundsInClip);
        ++heavyWeaponPickupInteractCount_;
        return true;
    }
    uint64_t heavyWeaponPickupInteractCount() const { return heavyWeaponPickupInteractCount_; }

    // ai_do_scripted_move (Sec3.11): see CharacterState's own new
    // pendingScriptedMoveFlags/pendingScriptedMoveDestination/
    // scriptedMoveDispatchCount fields for the per-character state this
    // native writes.

    // on_attack_performed (Sec3.12): see CharacterState's own new
    // onAttackPerformedGenericSlotCallback/onAttackPerformedCharacterSlotCallback
    // fields.

    // teleport (Sec4.3): see TeleportRequest's own doc comment above.
    // CONFIRMED the resolved target's own class-row bit (0x02cc9900,
    // byte +0xa bit 0x2, HIGH CONFIDENCE "player class") selects between
    // the two mutually exclusive apply paths - this project reuses
    // playerClassChainResolves() (Sec3.3/Sec3.5's own field) as the
    // stand-in for that SAME real-world distinction: a different real
    // resolver family by address, but the identical "is this name player-
    // classed" question, per this header's own Sec29 "one flat map stands
    // in for several similar per-kind resolvers" convention - not a claim
    // that 0x02cc9900's own byte +0xa bit 0x2 and 0x00a27e20's class-bit
    // gate are the SAME physical field, only that this project answers
    // both questions with the one map it already has for the purpose,
    // rather than adding a third near-duplicate field for the same
    // concept.
    std::unordered_map<std::string, TeleportRequest>& teleportRequests() { return teleportRequests_; }
    uint64_t& teleportScriptedRequestCount() { return teleportScriptedRequestCount_; }

    // release_to_world (Sec4.4): CONFIRMED three-tier resolver family
    // (group/character/vehicle, stopping at first success) that this
    // project's own flat objectResolves() map cannot by itself
    // distinguish (Sec29's "one shared map" convention deliberately does
    // not carry per-kind identity). This project's own minimal, dedicated
    // kind classifier for JUST this function: 1 = tier-1 group, 2 =
    // tier-2 character, 3 = tier-3 vehicle - OPEN until a test sets it for
    // a name objectResolves() also reports true for (the two are
    // independent facts a test must configure together). Tier 1 (group)
    // additionally needs its own pre-filtered "currently active member"
    // list (this project has no generic group-membership-with-active-bit
    // structure anywhere - a test supplies the already-filtered list
    // directly, same "test fills in what it needs to exercise" precedent
    // as registerVdoObjectForTesting/markScriptNpcBoundForTesting
    // elsewhere in this header). The shared vtable-slot-+0x5c "release"
    // method itself has no further observable effect in this project's
    // scope (no in-scope reader of "was this object released") - only
    // that it fired, once per released object, is counted.
    OpenValueMap<int32_t>& releaseToWorldKind() { return releaseToWorldKind_; }
    std::unordered_map<std::string, std::vector<std::string>>& releaseToWorldGroupMembers() { return releaseToWorldGroupMembers_; }
    std::unordered_map<std::string, uint64_t>& releasedCount() { return releasedCount_; }
    // release_to_world calls whose name resolved (objectResolves) but matched no group/character/vehicle record
    // this host holds, so the real tier could not be determined (Sec4.4 2026-10-12: native does nothing).
    uint64_t& releaseToWorldUnclassifiedCount() { return releaseToWorldUnclassifiedCount_; }

    // vehicle_suppress_npc_exit (Sec4.5): see VehicleState's own new
    // vehicleAiForceFlagsByte4 field.

    // set_player_can_enter_exit_vehicles (Sec4.6): see CharacterState's
    // own new flags28ac field.

    // continuous_spawn_stop (Sec4.7): CONFIRMED resolves via the SAME
    // resolver family release_to_world's tier-1 (group/spawn-region)
    // member uses (bit 0x20@+0xa) - this project's flat objectResolves()
    // stand-in, same convention as every other single-resolver setter in
    // this header. If a slot for this region exists, it is unconditionally
    // reset regardless of the boolean (CONFIRMED); the boolean is
    // consulted only in the narrower per-entity-dispatch path reached ONLY
    // when the slot's own activity marker was already non-zero (CONFIRMED)
    // - continuous_spawn_start (Sec4.8, the only in-scope writer of that
    // marker) is OUT of this batch's scope, so in THIS project's closed
    // world no slot is EVER marked active by any in-scope native; a test
    // can still mark one active directly to exercise that path for
    // structural coverage. The per-entity vtable-+0x58/+0x5c dispatch
    // itself has no plain-English identity (genuinely OPEN per spec,
    // "structurally unresolvable by static disassembly alone") - not
    // modelled beyond which boolean a since-marked-active region's stop
    // call carried.
    std::unordered_map<std::string, bool>& continuousSpawnActive() { return continuousSpawnActive_; }
    std::unordered_map<std::string, uint64_t>& continuousSpawnStopCount() { return continuousSpawnStopCount_; }
    std::unordered_map<std::string, bool>& continuousSpawnLastStopBoolean() { return continuousSpawnLastStopBoolean_; }

    // continuous_spawn_start (Sec4.8, real address 0x00a435a0), batch
    // 2026-10-08: writes into the SAME shared spawn-region table
    // continuous_spawn_stop resets above (see that stub's own new
    // erase() call, lua_spec_confirmed_stubs_b08_batch1_core.cpp). RESOLVED
    // 2026-10-08 (re-derived from 0x00a435a0->0x005eda60, scratchpad/
    // lua_natives_argcount_tutorial_table_2026-10-08.md): a brand-new
    // region gets every argument written into a fresh record (count,
    // respawn-interval-ms, three raw numeric fields args 5-7, the
    // type-name filter arg 4 plus two trailing optional strings args 8/9
    // via three dedicated setters 0x005ec4d0/0x005ec3f0/0x005ec460, and
    // the third trailing string arg 10 as a raw-pointer field store with
    // no setter) and is then activated once (CONFIRMED: a per-slot
    // "started" marker, freshly clear on a brand-new record, gates a
    // one-time activation call). An EXISTING record (one
    // continuous_spawn_stop has not fully reset) takes a fast re-start
    // path instead: ONLY the active flag (continuousSpawnActive() above)
    // is touched, nothing else is rewritten (CONFIRMED). "Region handle"
    // (one of the ten values the spec's own prose lists as written) is
    // this project's map key itself - the SAME flat-name convention
    // continuousSpawnActive() above already uses - not a separate stored
    // field.
    //
    // Args 6/7's own rounded values are individually cited as raw dword
    // stores at +0x38/+0x3c (CONFIRMED). Arg 5 is grouped in the same
    // sentence as "three raw numeric fields (args 5-7)" but its own exact
    // offset byte is NOT individually cited the way 6/7's are - modelled
    // here as its own plain field anyway, per this task's own "OPEN means
    // meaning unknown, not value unknown" distinction (CONFIRMED: a real
    // default value (1) and a real storage location exist for it; OPEN:
    // which exact byte, and all three numbers' real-world purpose). Arg 4's
    // own default just below was formerly this same kind of case (value
    // unconfirmed), but Team A has since confirmed the literal itself (see
    // continuousSpawnDefaultTypeFilter()'s own doc comment below).
    struct ContinuousSpawnRegionRecord {
        int64_t count = 0;                 // arg 2, truncateEa2596, default 0 (CONFIRMED)
        int64_t respawnIntervalMs = 0;      // arg 3 * 1000.0, truncateEa2596, default 0 (CONFIRMED)
        std::string typeNameFilter;         // arg 4, via setter 0x005ec4d0; default from continuousSpawnDefaultTypeFilter() below when arg 4 is absent (CONFIRMED "", see that accessor's doc comment)
        int64_t numericArg5 = 1;            // arg 5, truncateEa2596, default 1 (CONFIRMED value; OPEN meaning + exact offset, see doc comment above)
        int64_t numericArg6 = 14;           // arg 6, truncateEa2596, default 14, raw dword store at +0x38 (CONFIRMED location; OPEN meaning)
        int64_t numericArg7 = 19;           // arg 7, truncateEa2596, default 19, raw dword store at +0x3c (CONFIRMED location; OPEN meaning)
        bool hasArg8 = false;
        std::string arg8String;              // via setter 0x005ec3f0; default absent/NULL (CONFIRMED)
        bool hasArg9 = false;
        std::string arg9String;              // via setter 0x005ec460; default absent/NULL (CONFIRMED)
        bool hasArg10 = false;
        std::string arg10RawPointerString;   // raw pointer store at +0x40, NO setter call; default absent/NULL (CONFIRMED)
        bool started = false;                // per-slot "started" marker byte (CONFIRMED structure); always freshly clear on a brand-new record in this project's closed-world model
        uint64_t activationCount = 0;         // how many times the dedicated activation call fired (CONFIRMED: once, on the first registration only)
    };
    std::unordered_map<std::string, ContinuousSpawnRegionRecord>& continuousSpawnRegions() { return continuousSpawnRegions_; }
    // Arg 4's own default ("a fixed literal constant" - Sec4.8's own text,
    // a spawned-object/type-name filter): CONFIRMED by Team A
    // (spec-lua-api-behaviour.md Sec4.8) to be the literal empty string
    // "" - not just that a literal constant exists, but the literal value
    // itself. Formerly modelled as OpenValue<std::string> (value
    // genuinely unknown, unlike args 5-7's already-confirmed numeric
    // defaults); now a plain defaulted member, same pattern as
    // numericArg5/6/7 above.
    std::string& continuousSpawnDefaultTypeFilter() { return continuousSpawnDefaultTypeFilter_; }

    // on_vehicle_enter (Sec4.12): mirrors release_to_world's own
    // releaseToWorldKind() (Sec4.4) exactly, per this task's explicit
    // instruction to reuse that resolution shape - this project's flat
    // objectResolves() map alone cannot distinguish WHICH of the two
    // type-gated resolver-family members (bit 0x4@+0xb "character" vs bit
    // 0x8@+0xb "vehicle") matched for a name it reports true for. 1 =
    // bit-0x4 character tier (CharacterState::onVehicleEnterCharacterSlot
    // Callback, hook slot 0x17), 2 = bit-0x8 vehicle tier
    // (VehicleState::onVehicleEnterCallback, hook slot 7). OPEN until a
    // test sets it for a name objectResolves() also reports true for
    // (independent facts a test must configure together, same convention
    // as releaseToWorldKind()). Not reached at all when arg 2 resolves via
    // the generic/`#PLAYER#` chain first (playerClassChainResolves()).
    OpenValueMap<int32_t>& onVehicleEnterResolverKind() { return onVehicleEnterResolverKind_; }

    // =====================================================================
    // Batch 2026-10-08 (Batch 7, dedicated agent - spec-lua-api-behaviour.md
    // Sec30, "ranking tranche 03", the 16 of its 25 names NOT already wired
    // by lua_spec_confirmed_stubs_b03_vintui_r03_r05.cpp's own 9). See each
    // stub's own doc comment (lua_spec_confirmed_stubs_b14_batch7_
    // tranche03.cpp) for the full per-function citation. The shared
    // vehicle-resolver/force-flag infrastructure above (objectResolves(),
    // getOrCreateVehicle(), VehicleState::forceFlagGatePasses,
    // applyVehicleForceFlagSetter in lua_spec_confirmed_stubs_common.h) is
    // REUSED as-is, not re-derived - see this batch's own stub file header
    // for which of b03's 9 siblings each new function was sanity-checked
    // against.
    // =====================================================================

    // --- audio_ambient_emitter_pause / audio_ambient_emitter_resume
    // (Sec30.3) ------------------------------------------------------------
    //
    // This project has no real world-object-manager ambient-emitter list
    // (global 0x03171a64, same "no world objects in this host" precedent
    // already established for several other Sec28/Sec31 fields) - same
    // never-populated-registry convention as VdoObject/VintDataResponder-
    // Record: a real script's call against any name this host has never
    // had registered honestly takes the real, CONFIRMED "no match -> do
    // nothing" path, which is itself correct behavior for a never-
    // populated list, not a simplification of the pause/resume contract.
    void registerAmbientEmitterForTesting(const std::string& name) { ambientEmitterNames_.insert(name); }
    bool ambientEmitterKnown(const std::string& name) const { return ambientEmitterNames_.count(name) != 0; }
    std::unordered_map<std::string, bool>& ambientEmitterPaused() { return ambientEmitterPaused_; }

    // --- Shared script-request-id allocator 0x006f62b0 (Sec30.5's own
    // preamble: "cancels any tracked request for the same object+channel,
    // allocates a 12-bit sequence id OR-ed with a machine-index nibble
    // (HYPOTHESIS); channel 0 = turn_to_do, 2 = vehicle_pathfind_to_do, 3 =
    // the AI action writers"). This project is single-player/host-only
    // everywhere (stated, project-wide simplification - see
    // replicateStateChange's own doc comment) so the machine-index nibble
    // is always 0 here; only the 12-bit sequence counter is modelled,
    // wrapping at 0xFFF (CONFIRMED structure). A fresh allocate for the
    // SAME (object, channel) pair simply overwrites whatever was tracked
    // before (CONFIRMED "cancels any tracked request"). -----------------
    int32_t allocateScriptRequestId(const std::string& objectName, int channel);
    void releaseScriptRequestId(const std::string& objectName, int channel);
    // Read-only test/diagnostic view of the allocator above (wave 2 batch
    // W2-1): whether (object, channel) currently holds a tracked request id,
    // and that id (0 = none). No native reads it - the request-pool QUERY
    // natives (teleport_check_done/move_to_check_done/...) use the separate
    // Sec35.2 pool, which stays on Sec35.6's "minimum correct model".
    bool hasActiveScriptRequest(const std::string& objectName, int channel) const {
        return activeScriptRequestId_.count(std::make_pair(objectName, channel)) != 0;
    }
    int32_t activeScriptRequestIdOf(const std::string& objectName, int channel) const {
        auto it = activeScriptRequestId_.find(std::make_pair(objectName, channel));
        return it != activeScriptRequestId_.end() ? it->second : 0;
    }

    // --- move_to_do's own sequential "move action id" (Sec21.22, wave 2
    // batch W2-1): a global counter at 0x0130cb94, "wrapping, skipping 0",
    // whose value is stored into the target's +0xc34 word and returned to
    // Lua. Its INITIAL value is not given anywhere in the spec, so it is OPEN
    // (a returned id is Lua-visible - inventing a start value would be
    // inventing data). HIGH CONFIDENCE: 16-bit wrap (+0xc34 is a 2-byte field,
    // Sec22.15) and increment-then-skip-zero (the natural reading of
    // "wrapping, skipping 0"). allocateMoveToActionId() throws while OPEN.
    OpenValue<int32_t>& moveToActionIdCounter() { return moveToActionIdCounter_; }
    int32_t allocateMoveToActionId() {
        int32_t next = (moveToActionIdCounter_.get() + 1) & 0xFFFF; // OPEN until set
        if (next == 0) next = 1; // CONFIRMED: skips 0
        moveToActionIdCounter_.set(next);
        return next;
    }

    // --- vehicle_pathfind_navmesh_do (Sec22.1, wave 2 batch W2-1): the raw
    // result of the record-and-replicate dispatcher 0x00b24fa0, pushed to Lua
    // as a number. Its return semantics are not given (Sec22.1 describes
    // only its branch structure) - OPEN, one host-level value, same shape as
    // vehicle_pathfind_to_do's own pathfindResult() for its DIFFERENT
    // dispatcher 0x00b2b140 (kept separate: different address).
    OpenValue<double>& vehicleNavmeshDispatchResult() { return vehicleNavmeshDispatchResult_; }

    // --- The 70-entry CAE combat-action table (0x012e1c60, Sec30.4) -----
    //
    // 0x004f2610's own case-insensitive name->index scan; CONFIRMED
    // sentinel 71 (0x47) for "not found" - which also covers the literal
    // "NONE" sentinel ai_suggest_action tests for separately in the real
    // engine (both land on the same index here, same no-op outcome, so
    // this project does not need a separate special case for it). Only the
    // name->index direction is modelled (what ai_suggest_action/ai_do_
    // scripted_advance actually need) - the reverse index->name mapping
    // (0x004f2840) is NOT implemented, since no in-scope native in this
    // batch calls it.
    static constexpr int32_t kCaeActionNotFound = 71;
    int32_t lookupCaeActionIndex(const std::string& name) const;

    // --- vehicle_pathfind_to_do (Sec30.5) --------------------------------
    //
    // 0x00b2b140's own return value: OPEN per spec ("OPEN: 0x00b2b140's
    // own semantics/return meaning, what arg 3/4 select") - a single
    // shared stand-in (this project has no per-vehicle pathfinding model
    // to give a finer-grained value), test-settable; a real call with no
    // test override throws OpenStateError, the honest "never fabricate a
    // physics result" outcome.
    OpenValue<double>& pathfindResult() { return pathfindResult_; }
    // HOST-SAFETY (orchestrator's standing hardening policy, not a spec
    // fact): the real engine takes strlen() of arg 2 BEFORE testing its
    // Lua type (CONFIRMED crash-shaped edge, Sec30.5: "Takes strlen of arg
    // 2 before the NULL test - a non-string arg 2 is dereferenced"). This
    // host never dereferences a non-string Lua value as a C string - see
    // the stub's own doc comment for exactly which safe path it takes
    // instead. Test-observable only, not a real engine field.
    int vehiclePathfindNonStringArg2GuardCount() const { return vehiclePathfindNonStringArg2GuardCount_; }
    void recordVehiclePathfindNonStringArg2Guard() { ++vehiclePathfindNonStringArg2GuardCount_; }

    // --- vehicle_set_2d_tank_controls (Sec30.5): global byte 0x0278cad8 -
    bool& twoDTankControlsEnabled() { return twoDTankControlsEnabled_; }

    // --- vehicle_rc_get_signal_strength (Sec30.5): global float
    // 0x0278cad4, the RC signal range R used in `1 - d/R` - OPEN per spec
    // ("OPEN: R's own source (only writer 0x00a906b0)"). -----------------
    OpenValue<double>& rcSignalRange() { return rcSignalRange_; }

    // --- vehicle_set_malfunctioning (Sec30.5): the local player's own
    // screen-distortion float(s) at +0x1650..+0x165c (tracked as one
    // scalar stand-in, no per-axis model - this project has no screen-
    // effect system beyond this one gated write), the global byte
    // 0x02625852, and the "cyber_m16_glitch" screen-effect preset slot
    // (tracked as a bool - this project has no real effect-preset table).
    double& localPlayerScreenDistortion1650() { return localPlayerScreenDistortion1650_; }
    bool& malfunctionGlobalByte02625852() { return malfunctionGlobalByte02625852_; }
    bool& cyberM16GlitchEffectActive() { return cyberM16GlitchEffectActive_; }

    // --- vehicle_stop_do / teleport_vehicle_to_object's own SHARED
    // 0x00abe510 stop routine (Sec30.5: "vehicle_stop_do... 0x00abe510(1)
    // ...; 0x00abe510 (shared with teleport_vehicle_to_object)..."). Only
    // the CONFIRMED gate/branch structure is modelled (direct-apply vs.
    // replicate-only, same double-/single-gate family every other setter
    // in this header already resolves true-in-single-player) - the real
    // physics stop (0x00b46ca0/0x00a90c70) and the AI-driving-controller
    // stop on the +0x808 sub-object are NOT simulated (no physics/AI-
    // subobject model in this project). `authorityPasses` is computed by
    // the caller from whichever real gate applies to ITS own path
    // (character's controlLockGatePasses for the via-character path,
    // vehicle's own forceFlagGatePasses for the direct/vehicle-name path -
    // Sec30.5: "authority checked on the character (if via character) or
    // the vehicle (if direct)").
    void stopVehicle(const std::string& vehicleName, bool authorityPasses);

    // --- Batch 2026-10-08 (Sec5-9 fan-out, Batch 2) - see each field's
    // own doc comment (CharacterState/VehicleState above, or inline below)
    // for the full citation/reasoning. ---

    // set_animation_state (Sec7.11) / set_script_animation_state
    // (Sec7.17): shared name->state-id table stand-in (0x004bf810) - see
    // CharacterState's own doc comment (above, near the batch header just
    // before attackPlayerBitAc) for the full "collapses two real lookups
    // into one" reasoning. -1 = unregistered/absent (CHOSEN, matches the
    // spec's own already-established "-1 means no override/no entry"
    // convention, Sec7.3/Sec7.11/Sec7.17).
    void registerAnimationStateIdForTesting(const std::string& name, int32_t id) { animationStateIds_[name] = id; }
    int32_t lookupAnimationStateId(const std::string& name) const {
        if (name.empty()) return -1;
        auto it = animationStateIds_.find(name);
        return it != animationStateIds_.end() ? it->second : -1;
    }

    // ai_set_action_delay (Sec7.30): the 70-entry action-name table
    // (0x012e1c60), case-insensitive per spec (__stricmp) - CONFIRMED
    // count (70) and CONFIRMED real "not found" sentinel (the count
    // itself). registerActionNameForTesting()'s own `name` is matched
    // case-insensitively by lookupActionNameIndex(), same as the real
    // table's own comparison.
    static constexpr int32_t kActionNameTableSize = 70;
    static constexpr int32_t kActionNameNotFound = kActionNameTableSize;
    void registerActionNameForTesting(const std::string& name, int32_t index) { actionNameIndices_[name] = index; }
    int32_t lookupActionNameIndex(const std::string& name) const {
        for (const auto& kv : actionNameIndices_) {
            if (kv.first.size() != name.size()) continue;
            bool eq = true;
            for (size_t i = 0; i < name.size() && eq; ++i) {
                if (std::tolower(static_cast<unsigned char>(kv.first[i])) != std::tolower(static_cast<unsigned char>(name[i])))
                    eq = false;
            }
            if (eq) return kv.second;
        }
        return kActionNameNotFound;
    }

    // hud_bar_off/hud_bar_on/hud_bar_set_value (Sec8.14/8.15/8.16): fixed
    // 4-slot array (0x022b6828, CONFIRMED "0-3" id range, Sec8.14). A
    // script-controlled id outside that range would index past this fixed
    // array on the real engine too (no bounds check is documented at any
    // of these 3 call sites) - HARDENED here: an out-of-range id is
    // refused (nullptr) rather than read/written OOB; see
    // hudHardeningCounters() below for the per-site firing counts
    // (HARDENING POLICY).
    HudBarSlot* hudBarSlot(int64_t id) {
        if (id < 0 || id >= kHudBarSlotCount) return nullptr;
        return &hudBarSlots_[static_cast<size_t>(id)];
    }

    // hud_timer_set/hud_timer_stop (Sec8.19/8.20): same fixed-size-array
    // shape and the same undocumented-bounds-check gap as the bar-slot
    // family above (Sec8.19's own text: "the slot table isn't already
    // full (0x022b6600 < 4)" - CONFIRMING a 4-slot cap) - HARDENED the
    // same way.
    HudTimerSlot* hudTimerSlot(int64_t id) {
        if (id < 0 || id >= kHudTimerSlotCount) return nullptr;
        return &hudTimerSlots_[static_cast<size_t>(id)];
    }
    // 0x022b65f5 - a GLOBAL "a timer is active" byte, distinct from any
    // one slot's own liveness (hud_timer_stop's own text: "clears a
    // global 'timer active' flag ... when the slot's own liveness byte is
    // already clear"). CHOSEN false initial state - no spec gives this
    // byte's own real construction value.
    bool& hudTimerGlobalActive() { return hudTimerGlobalActive_; }

    HudHardeningCounters& hudHardeningCounters() { return hudHardeningCounters_; }

    // hud_prompt_clear (Sec8.17): the local, always-taken clear path. The
    // genuinely network-replicated path (opcode 0x4a, gated on the target
    // matching the remote co-op player getter 0x009df3d0) can never fire
    // in this project's single-player-only scope (no remote co-op player
    // entry is ever modeled - the same class of reasoning
    // player_names_get_all, Sec51.4, already applies), so only this one
    // counter is test-observable.
    uint64_t& hudPromptClearCount() { return hudPromptClearCount_; }

    // hud_set_fake_notoriety (Sec8.18): the 6-slot (0-3 real tracks + the
    // CONFIRMED sentinel slot 5; index 4 unused) fixed array - bounded by
    // construction (the stub's own resolveFakeNotorietySlot() always
    // returns 0-3 or 5), no OOB risk, no hardening needed.
    std::array<FakeNotorietySlot, 6>& fakeNotorietySlots() { return fakeNotorietySlots_; }

    // --- Batch 5 (ranks 251-350, spec-lua-api-behaviour.md Sec15/Sec16),
    // 2026-10-08 - HUD-slot/customization/pause-menu cluster. See
    // lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning. ---

    // refraction_situation_clear (Sec15.5): bit 0x1 of the flag arg (default
    // 3) zeroes slot 0 of the global refraction-override array (CONFIRMED,
    // no stated prior value - CHOSEN "off" semantics); bit 0x2 opens an
    // unconditional (not host-gated, per Sec15.5's own text) record-and-
    // replicate. Both can fire on the same call (default value 3 sets
    // both bits).
    void recordRefractionSituationClear(int64_t flags) {
        if (flags & 0x1) { refractionGlobalOverrideCleared_ = true; ++refractionClearLocalCount_; }
        if (flags & 0x2) { EngineState::replicateStateChange("refraction_situation_clear", ""); ++refractionClearReplicateCount_; }
    }
    bool refractionGlobalOverrideCleared() const { return refractionGlobalOverrideCleared_; }
    uint64_t refractionClearLocalCount() const { return refractionClearLocalCount_; }
    uint64_t refractionClearReplicateCount() const { return refractionClearReplicateCount_; }

    // hud_display_create_state/remove_state/commit_state (Sec15.24/15.12/
    // 15.25) - see HudDisplayStatePool's own doc comment above for the full
    // shared-infra reasoning.
    HudDisplayStatePool& hudDisplayStatePool() { return hudDisplayStatePool_; }
    int hudDisplayCreateState() {
        for (int i = 0; i < HudDisplayStatePool::kSlotCount; ++i) {
            if (!hudDisplayStatePool_.used[i]) {
                hudDisplayStatePool_.used[i] = true;
                hudDisplayStatePool_.removed[i] = false;
                ++hudDisplayStatePool_.createCount;
                // CONFIRMED (Sec15.24): only a SUCCESSFUL allocation opens
                // the record-and-replicate block.
                EngineState::replicateStateChange("hud_display_create_state", "");
                return HudDisplayStatePool::kFirstSlotId + i;
            }
        }
        return -1; // CONFIRMED exhaustion sentinel (Sec15.24)
    }
    void hudDisplayRemoveState(int slotId, bool stashBool) {
        // CONFIRMED (Sec15.12): the record-and-replicate call is
        // unconditional, fired before the local validation below.
        EngineState::replicateStateChange("hud_display_remove_state", "");
        ++hudDisplayStatePool_.removeCount;
        int idx = slotId - HudDisplayStatePool::kFirstSlotId;
        if (idx < 0 || idx >= HudDisplayStatePool::kSlotCount || !hudDisplayStatePool_.used[idx] ||
            hudDisplayStatePool_.removed[idx]) {
            ++hudDisplayStatePool_.removeInvalidCount;
            return;
        }
        hudDisplayStatePool_.removed[idx] = true;
        auto& q = hudDisplayStatePool_.pendingQueue;
        for (auto it = q.begin(); it != q.end(); ++it) {
            if (*it == slotId) { q.erase(it); break; }
        }
        hudDisplayStatePool_.lastStashedBool = stashBool;
    }
    void hudDisplayCommitState(int slotId, bool stashBool) {
        // CONFIRMED (Sec15.25): the record-and-replicate call is
        // unconditional, fired before the local enqueue below.
        EngineState::replicateStateChange("hud_display_commit_state", "");
        ++hudDisplayStatePool_.commitCount;
        int idx = slotId - HudDisplayStatePool::kFirstSlotId;
        bool validSlot = idx >= 0 && idx < HudDisplayStatePool::kSlotCount && hudDisplayStatePool_.used[idx] &&
                         !hudDisplayStatePool_.removed[idx];
        auto& q = hudDisplayStatePool_.pendingQueue;
        bool alreadyQueued = false;
        for (int v : q) if (v == slotId) { alreadyQueued = true; break; }
        if (!validSlot || alreadyQueued || q.size() >= 5) {
            ++hudDisplayStatePool_.commitRejectedCount;
            return;
        }
        q.push_back(slotId);
        hudDisplayStatePool_.lastStashedBool = stashBool;
    }

    // qte_cleanup (Sec15.6) - see QteSlotEntry's own doc comment above for
    // the "mechanism confirmed, no in-scope producer" reasoning.
    void registerActiveQteSlotForTesting(int linkedHudSlotId) { qteActiveSlots_.push_back({linkedHudSlotId}); }
    size_t qteActiveSlotCount() const { return qteActiveSlots_.size(); }
    uint64_t qteCleanupCount() const { return qteCleanupCount_; }
    void qteCleanup() {
        for (auto& e : qteActiveSlots_) {
            if (e.linkedHudSlotId >= 0) hudDisplayRemoveState(e.linkedHudSlotId, false);
        }
        qteActiveSlots_.clear();
        ++qteCleanupCount_;
    }
    // qte_start (Sec20.9, wave 2 batch W2-1): the authoritative branch's slot
    // activation. HIGH CONFIDENCE one slot per character (Sec20.9: the slot
    // index is "the character's own capability-byte-derived slot index"), so
    // a second qte_start for the same character overwrites its entry instead
    // of claiming another. See QteSlotEntry's own doc comment.
    void qteStartActivate(const QteSlotEntry& entry) {
        for (auto& e : qteActiveSlots_) {
            if (!e.characterName.empty() && e.characterName == entry.characterName) {
                e = entry;
                return;
            }
        }
        qteActiveSlots_.push_back(entry);
    }
    const std::vector<QteSlotEntry>& qteActiveSlots() const { return qteActiveSlots_; }

    // persona_clear_global_situation_override (Sec15.7): CONFIRMED
    // unconditional reset of the global override value to 0; the
    // record-and-replicate call is CONFIRMED host-only (coopLocalIsHost(),
    // CONFIRMED false in single player, same precedent as
    // audio_conversation_end Sec8.2) - structurally reproduced via the gate
    // below even though it never fires in this project's scope.
    void personaClearGlobalSituationOverride() {
        personaGlobalSituationOverride_ = 0;
        ++personaGlobalSituationOverrideResetCount_;
        if (coopLocalIsHost()) EngineState::replicateStateChange("persona_clear_global_situation_override", "");
    }
    int32_t personaGlobalSituationOverride() const { return personaGlobalSituationOverride_; }
    uint64_t personaGlobalSituationOverrideResetCount() const { return personaGlobalSituationOverrideResetCount_; }

    // mission_unlock (Sec15.9) / mission_set_completed (Sec15.10): both
    // resolve their mission-name argument via objectResolves() (this
    // project's flat Sec29 stand-in for the newly-identified mission
    // resolver 0x005f4c30 - structurally identical to the vehicle resolver,
    // Sec15.9's own cross-reference).
    //
    // Sec21.27b (2026-10-08) FOUND the real writer of the already-
    // documented completion flag: `0x006d6870`, a single unconditional
    // "byte = (byte & 0xf7) | 0x44" on the mission handle's own `+0x88`
    // byte (clears bit 0x8, sets bits 0x4 "completed" and 0x40 together,
    // always, no gating) - and CONFIRMED, via mission_unlock's own bit-0x1
    // "unlocked" setter testing/setting a DIFFERENT bit of the SAME byte,
    // that `+0x88` is one shared packed multi-bit flags field, not three
    // independent facts. This project previously modeled mission_unlock
    // (missionUnlocked_), mission_set_completed (missionCompletedMap_) and
    // cell_is_mission_complete/mission_is_complete (missionComplete_, an
    // OpenValueMap, Sec27.2/Sec20.14) as THREE SEPARATE, never-cross-wired
    // maps standing in for that one real byte - exactly the kind of
    // invented-parallel-field duplication this header's own top note warns
    // against. Fixed here: missionFlagBits_ is the one real packed-bits
    // store; writeMissionCompletionFlagBits()/writeMissionUnlockedFlagBit()
    // are its only two writers (0x006d6870's own real shape, and
    // mission_unlock's own bit, respectively), and BOTH also update the
    // pre-existing per-native facades below them so every one of this
    // header's own already-shipped tests (which read/write those facades
    // directly) keeps working unchanged - missionComplete_ in particular
    // keeps its own pre-existing "OPEN/throws until written" semantics for
    // a name NOTHING has ever completed (Sec27.2's own test coverage
    // relies on exactly that); it is simply now also .set() by the real
    // writers below, not only by a test. missionEndDispatch() above
    // (Sec21.27b's type-1/2 branches) is the OTHER real production caller
    // of writeMissionCompletionFlagBits()/writeMissionUnlockedFlagBit().
    static constexpr uint8_t kMissionFlagUnlocked = 0x1;            // mission_unlock, Sec15.9
    static constexpr uint8_t kMissionFlagClearedByWriter = 0x8;     // cleared (not set) by 0x006d6870 - real meaning OPEN
    static constexpr uint8_t kMissionFlagCompleted = 0x4;           // Sec15.10/Sec21.27b
    static constexpr uint8_t kMissionFlagCompletedSecondary = 0x40; // set alongside kMissionFlagCompleted by the SAME writer - real meaning OPEN
    void writeMissionCompletionFlagBits(const std::string& name) {
        uint8_t& bits = missionFlagBits_[name]; // starts 0 on first reference - CHOSEN, no CONFIRMED +0x88 construction value either way (same "write-only in this project's own observable scope" discipline as pedDensity_ above)
        bits = static_cast<uint8_t>((bits & ~kMissionFlagClearedByWriter) |
                                     (kMissionFlagCompleted | kMissionFlagCompletedSecondary));
        missionCompletedMap_[name] = true; // legacy facade - Sec15.10's own existing test coverage
        missionComplete_.set(name, true);  // legacy facade - Sec27.2/Sec20.14's own existing test coverage
    }
    void writeMissionUnlockedFlagBit(const std::string& name) {
        missionFlagBits_[name] |= kMissionFlagUnlocked;
        missionUnlocked_[name] = true; // legacy facade - Sec15.9's own existing test coverage
    }
    void recordMissionUnlock(const std::string& name) { writeMissionUnlockedFlagBit(name); }
    bool missionUnlockedValue(const std::string& name) const {
        auto it = missionUnlocked_.find(name);
        return it != missionUnlocked_.end() && it->second;
    }
    // CONFIRMED (Sec15.10): the "mission list changed" notify and the
    // record-and-replicate call both fire regardless of resolve/
    // idempotency. The REAL writer (0x006d6870) is itself unconditional on
    // every call (Sec21.27b: "no gating condition before it") - the
    // idempotency Sec15.10's own text describes belongs to mission_set_
    // completed's OWN wrapper routine (0x006d6cf0's `+0xa0` check), not to
    // 0x006d6870 itself; this project does not model that separate +0xa0
    // field, so writing unconditionally here (same observable result as
    // the old "skip if already true" guard, since both write the same
    // value) matches the real unconditional writer more closely.
    void recordMissionSetCompleted(const std::string& name, bool resolved) {
        EngineState::replicateStateChange("mission_set_completed", resolved ? name : "");
        ++missionSetCompletedCallCount_;
        if (resolved) writeMissionCompletionFlagBits(name);
    }
    bool missionCompletedValue(const std::string& name) const {
        auto it = missionCompletedMap_.find(name);
        return it != missionCompletedMap_.end() && it->second;
    }
    uint64_t missionSetCompletedCallCount() const { return missionSetCompletedCallCount_; }

    // message (Sec15.11): CONFIRMED a message/popup-instance id is always
    // returned regardless of which of the 2 dispatch paths runs (Sec15.11's
    // own raw-disassembly-confirmed single return value) - no real id
    // scheme is given, so this project reuses its own established
    // monotonically-increasing-handle convention (nextAudioVoiceHandle()/
    // nextVisualEffectHandle()'s own precedent), as its own dedicated
    // counter (Sec15.11's own text: "a SEPARATE message-slot mechanism
    // from the hud_display_* trio's own slot-queue - no shared state was
    // found between them").
    int64_t nextMessageHandle() { return nextMessageHandle_++; }

    // skydive_set_player_dive_fall_speed (Sec15.18): global 0x0130a8e0,
    // unconditionally overwritten every call; the record-and-replicate
    // call is CONFIRMED host-only (same coopLocalIsHost()-false-in-single-
    // player precedent as persona_clear_global_situation_override above).
    void setPlayerSkydiveFallSpeed(double value) {
        if (coopLocalIsHost()) EngineState::replicateStateChange("skydive_set_player_dive_fall_speed", "");
        playerSkydiveFallSpeed_ = value;
    }
    double playerSkydiveFallSpeed() const { return playerSkydiveFallSpeed_; }

    // game_choice_set_state (Sec15.26): persistent global bitmask
    // 0x026e7e60, bit `index` set/cleared unconditionally. HOST-SAFETY:
    // `index` is read as a byte (0-255, Sec15.26's own "kept as a byte")
    // but the real bitmask is a 32-bit word - this project clamps the
    // shift to avoid undefined behaviour in ITS OWN C++ for index >= 32
    // (the spec text never states what the real engine does past bit 31
    // either; no observable consequence either way since nothing in this
    // project's scope reads a bit beyond 31 back). The two hardcoded
    // special cases (index 0's notify; index 1's triple-gated achievement
    // hook, 0x013123d1/0x029897a5/0x029897a6, all three OPEN/not modeled
    // in this project) are counted but not claimed to have fired the real
    // achievement event, since the 3 enable-flags this project would need
    // to know are OPEN.
    void setGameChoiceState(uint8_t index, bool state) {
        if (coopLocalIsHost()) EngineState::replicateStateChange("game_choice_set_state", "");
        if (index < 32) {
            if (state) gameChoiceBitmask_ |= (1u << index);
            else gameChoiceBitmask_ &= ~(1u << index);
        } else {
            ++gameChoiceIndexOutOfRangeGuardCount_; // HOST-SAFETY, test-observable only
        }
        ++gameChoiceSetCount_;
        if (index == 0) ++gameChoiceIndex0NotifyCount_;
        if (index == 1) ++gameChoiceIndex1AchievementHookAttemptCount_;
    }
    uint32_t gameChoiceBitmaskValue() const { return gameChoiceBitmask_; }
    bool gameChoiceBitValue(uint8_t index) const { return index < 32 && (gameChoiceBitmask_ & (1u << index)) != 0; }
    uint64_t gameChoiceSetCount() const { return gameChoiceSetCount_; }

    // dlc_achievement_counter_set (Sec15.27). HARDENING POLICY (CHOSEN
    // hardened divergence): Sec15.27's own text is explicit - "if arg1 is
    // < 3, writes arg2 directly into a global array of exactly 3 dword
    // slots ... indexed by arg1. No bounds error" - the real check (`< 3`)
    // does NOT exclude a NEGATIVE arg1 (e.g. -5 < 3 is also true), so the
    // real binary performs an unchecked, script-controlled OOB write
    // relative to the 3-slot array for any negative index - a genuine
    // memory-safety bug, not a HYPOTHESIS. Per the orchestrator's standing
    // hardening policy: every in-range index (0/1/2) is replicated exactly
    // (direct write, no further effect); the negative-index OOB write is
    // refused (skipped, not replicated) rather than corrupting adjacent
    // host memory, since there is no safe way to know what the real
    // write's target address would have overlapped in this project's own
    // memory layout. index >= 3 is NOT a divergence - it is the real
    // binary's own CONFIRMED, correct no-op (the `< 3` check already
    // excludes it).
    static constexpr int32_t kDlcAchievementCounterSlotCount = 3;
    void setDlcAchievementCounter(int64_t index, int32_t value) {
        if (index < 0) { ++dlcAchievementCounterNegativeIndexGuardCount_; return; } // CHOSEN (hardened divergence), Sec15.27
        if (index >= kDlcAchievementCounterSlotCount) return; // CONFIRMED real no-op (the `< 3` check), not a divergence
        dlcAchievementCounters_[static_cast<size_t>(index)] = value;
    }
    int32_t dlcAchievementCounterValue(int64_t index) const {
        if (index < 0 || index >= kDlcAchievementCounterSlotCount) return 0;
        return dlcAchievementCounters_[static_cast<size_t>(index)];
    }
    uint64_t dlcAchievementCounterNegativeIndexGuardCount() const { return dlcAchievementCounterNegativeIndexGuardCount_; }

    // game_get_language (Sec16.1): 12 CONFIRMED real table entries (floor,
    // not necessarily the full list - Sec16.1's own "table not walked past
    // entry 12" caveat). No in-scope setter exists (no "set_language" in
    // this batch) - a test-only hook populates the index; CHOSEN default
    // -1 ("no language selected" -> the CONFIRMED "" fallback). An index
    // past the 12 CONFIRMED entries is genuinely unknown (not fabricated -
    // this IS the function's whole observable return value, the one case
    // in this batch where "no in-scope reader" does not apply, since the
    // calling Lua script itself is the reader) - refused as OPEN.
    void setLanguageIndexForTesting(int32_t idx) { currentLanguageIndex_ = idx; }
    std::string languageString() const {
        if (currentLanguageIndex_ < 0) return ""; // CONFIRMED fallback (Sec16.1)
        static const char* kTable[12] = {"US", "ES", "IT", "JP", "DE", "FR", "NL", "SE", "DK", "CZ", "PL", "SK"};
        if (currentLanguageIndex_ < 12) return kTable[currentLanguageIndex_];
        throw OpenStateError("game_get_language table index >= 12 (floor, not confirmed full list)",
                             "spec-lua-api-behaviour.md Sec16.1");
    }

    // game_award_respect (Sec16.2): the real applied-amount formula
    // (count * an unspecified global multiplier, 0x012fd1b0, then fed
    // through 2 further calls whose OWN explicit-argument call shape is
    // itself a disassembly-confirmed anomaly against their established
    // zero-argument shape elsewhere - Sec16.2's own 2 flagged OPEN items)
    // is genuinely unknown, and unlike this batch's other write-only
    // fields, the RETURN VALUE here is directly consumed by the calling
    // Lua script - a wrong number would misrepresent a CONFIRMED-vs-
    // guessed fact to that script, so this project refuses rather than
    // guesses at the multiplier (OpenValue, test-settable) while fully
    // implementing the CONFIRMED argument shape and routing/notify side
    // effects around it.
    OpenValue<double>& respectMultiplier() { return respectMultiplier_; }
    void recordRespectGrant(bool targeted, bool notifyRequested) {
        EngineState::replicateStateChange("game_award_respect", ""); // CONFIRMED unconditional (opcode 0xa1 or targeted), not host-gated
        ++respectGrantCount_;
        if (targeted) ++respectGrantTargetedCount_; else ++respectGrantBroadcastCount_;
        // CORRECTED 2026-10-09 (spec-tables-progression.md Sec5.4, cross-
        // referenced into spec-lua-api-behaviour.md Sec16.2/Sec16.16's own
        // Changelog): this terminal call, `0x006ced90`, is NOT an
        // undecompiled "optional UI notification" as the old comment here
        // (and the two sections above it) characterized it - full
        // disassembly shows it is a SECOND invocation of the award routine
        // (0x00b97340/0x00b98d50) against up to two other save-shaped
        // progress blocks reached via globals 0x014c8574/0x014d8d1c, gated
        // by a co-op/session-state check - i.e. a co-op progression-mirror
        // ("mirror this respect award onto a split-screen partner's own
        // snapshot"), never a toast/popup of any kind. `0x014c8574` is
        // independently already named in spec-save-format.md Sec10.4 as a
        // retained snapshot-shaped buffer for a second player's state,
        // consistent with this reading. This project has no live
        // second-player save-shaped snapshot object to actually mirror
        // onto (SaveGameState, save_game_state.h, models only the LOCAL
        // player), so the mirror's own body is not implemented - only the
        // fact that a resolved call reached it is counted, honestly, same
        // "count only, real gate/body not modeled" convention as
        // tutorialUnlockCount/recordTutorialUnlock above.
        if (notifyRequested) ++respectGrantNotifyCount_;
    }
    uint64_t respectGrantCount() const { return respectGrantCount_; }
    // Test/diagnostic accessor for the corrected co-op-mirror-attempt count
    // documented above (same counter recordRespectGrant increments).
    uint64_t respectGrantCoopMirrorAttemptCount() const { return respectGrantNotifyCount_; }

    // --- Sec5.4 respect/progression live runtime (spec-tables-progression.md,
    // 2026-10-09, CONFIRMED by disassembly) -------------------------------
    //
    // Real live player-object fields (global pointer 0x0262edfc, getter
    // 0x009da4e0), independently re-derived via a different accessor chain
    // than spec-save-format.md Sec10.3's own SaveGameState fields (closing a
    // cross-check that was open between the two specs - the two mirror each
    // other exactly, but are two genuinely separate live objects in the
    // real engine, so this project keeps them as two separate structs too,
    // same "don't silently alias an unconfirmed cross-reference" convention
    // as Sec4.13's own door_open/teleport precedent): `+0x1ed0` total
    // respect, `+0x1ed4` respect banked inside the current level, `+0x1ed8`
    // current level (0..50). `0x009db8b0` is a one-line accessor returning
    // `*(playerObj+0x1ed8)`.
    uint32_t respectTotal() const { return respectTotal_; }
    uint32_t respectBanked() const { return respectBanked_; }
    uint32_t respectLevel() const { return respectLevel_; }

    // The loaded level count 0x0060eba0() == 50 - CONFIRMED (Sec5.2/Sec5.4),
    // never a table-size-derived value (the real loader "stops storing at
    // 50" even if a row beyond it existed).
    static constexpr uint32_t kRespectLevelCap = 50;
    // stats.xtbl stat id this project has CONFIRMED drives on every respect
    // award (Sec5.4's own new finding), Sec1.1.
    static constexpr int32_t kRespectAwardStatId = 0x44;
    // hint id 0xa9's own 0-based index into the 210-entry tutorial/hint
    // table (Sec10.4/Sec16.3's own 0x0151d600 table; Sec5.4: "the SAME
    // 210-message HUD hint/tutorial dispatcher").
    static constexpr int kRespectLevelUpHintId = 0xA9;

    // `respect_levels.xtbl`'s per-level cost/unlock table (sr3tables_
    // progression::RespectLevel, included above) - the SAME table Sec5.2
    // names for the save-shaped path and Sec5.4 confirms the live runtime
    // path reuses verbatim via 0x0060eb60 (not a second algorithm). This
    // project has no real xtbl-reading wired to this subsystem (same "out
    // of scope" footing as collectibles.xtbl's own Respect_Award fields,
    // Sec5.4's own "Open items"), so a test populates it directly.
    std::vector<sr3tables_progression::RespectLevel>& respectLevelTable() { return respectLevelTable_; }

    // 0x0060eb40(n): record n's Respect cost, or 22000 (0x55F0) for n past
    // the loaded table (CONFIRMED, Sec5.2) - the exact accessor 0x0060eb60
    // wraps for the live path (Sec5.4). `n` is the 0-based record index
    // equal to the number of levels already gained (Sec5.2's own
    // "L = levels gained, and record L... is the cost of the next level").
    uint32_t respectLevelCost(uint32_t level) const {
        if (level < respectLevelTable_.size()) {
            return static_cast<uint32_t>(respectLevelTable_[level].respect.value);
        }
        return 22000;
    }

    // Per-name live unlock flag (CONFIRMED: the global unlockables table
    // `0x01522750`, record offset `+0xe1`) - CHOSEN per-name map, same "no
    // generic hash table modelled" convention as triggerHoldToRepeat_
    // above, since this project never populates the real unlockables.xtbl
    // id space this flag is really keyed by (Sec4). Missing = not yet
    // unlocked, matching the real all-zero-at-start bit state.
    bool respectUnlockApplied(const std::string& name) const {
        auto it = respectUnlockablesFlag_.find(name);
        return it != respectUnlockablesFlag_.end() && it->second;
    }
    // The literal name `"HomieSlot1"` (CONFIRMED, Sec5.4) gets its own
    // distinct notification at the moment it is unlocked, separate from
    // the generic per-name flag above so a test can tell the two apart -
    // this counter increments ONLY for that literal name, never for an
    // ordinary unlock.
    uint64_t respectHomieSlot1UnlockCount() const { return respectHomieSlot1UnlockCount_; }

    // stat-system record-open count, per stat id (CONFIRMED: every respect
    // award also opens a record for stat id 0x44, stats.xtbl, Sec1.1/
    // Sec5.4) - this project has no pre-existing stats.xtbl/stat-id live
    // tracking anywhere else to reuse (checked: no hit for "stat id" / achievement
    // counters elsewhere in this header set beyond the unrelated DLC
    // achievement-counter slots, Sec15.27), so this is this subsystem's own
    // new per-id counter map, same "count only, real body not modeled"
    // convention as tutorialAdvanceCounts_/tutorialUnlockCounts_ above -
    // generalized per-id (not hardcoded to 0x44 alone) in case a later task
    // finds another native driving the same pipeline.
    uint64_t statRecordOpenCount(int32_t statId) const {
        auto it = statRecordOpenCounts_.find(statId);
        return it != statRecordOpenCounts_.end() ? it->second : 0;
    }
    void recordStatRecordOpen(int32_t statId) { ++statRecordOpenCounts_[statId]; }

    // The decade-level (10/20/30/40/50) milestone hook, `0x00bd3150`
    // (Sec5.4, new finding this pass): fires on every level crossing, but
    // only WRITES when the newly reached level is a decade level AND the
    // player is inside the `sr3_city` open-world zone - stashing a distinct
    // event-state id (0x22, or 0x23 specifically for level 50) plus the
    // player's own world position into a pair of globals. Write-side only,
    // per this task's own brief: the actual consumer (a cutscene, a radio
    // line, a scripted encounter) was never traced in the spec pass and is
    // deliberately NOT invented here.
    struct RespectDecadeMilestone {
        // CHOSEN: no live "current zone" tracker exists anywhere else in
        // this project to read instead (checked: no hit for "current
        // zone"/"open world" state in this header set) - a minimal,
        // test-settable stand-in, default true (this project's own
        // MainMenuNewGame::worldName already hardcodes "sr3_city" as the
        // CONFIRMED default world, Sec41.2, so "in the open world" is the
        // natural default here too, not a guess at the real initial
        // value).
        bool inOpenWorldZone = true;
        int32_t lastEventId = 0;      // 0 = never fired; else 0x22 or 0x23 (level 50)
        WorldPosition lastPosition;   // player's own world position at the moment of the write
        uint64_t fireCount = 0;       // how many times the write actually happened (not every level crossing)
    };
    RespectDecadeMilestone& respectDecadeMilestone() { return respectDecadeMilestone_; }
    // CHOSEN: the player's own live position this hook would stash is not
    // otherwise tracked for the local player anywhere in this project
    // (CharacterState::worldPosition is keyed per-name and per-call, not a
    // standing "local player position," Sec4.2) - a test sets this directly
    // to whatever position the award happens at, same "write-only, no
    // in-scope reader to resolve a real value from" convention as
    // playerSkydiveFallSpeed_ above (Sec15.18).
    WorldPosition& respectMilestonePlayerPosition() { return respectMilestonePlayerPosition_; }

    // Award path, CONFIRMED (Sec5.4): the Lua native `game_award_respect`
    // (0x00843bf0) -> 0x009da4e0 (resolve local player) -> 0x009db8e0's own
    // caller 0x009e2440 (full body opened): cap-checks `amount`, updates
    // the running total via 0x009db8c0, calls the level-up routine below,
    // and opens a stat-system record for stat id 0x44 on every award (not
    // only a successful level-up).
    uint64_t respectAwardNonPositiveGuardCount() const { return respectAwardNonPositiveGuardCount_; }

    // The real per-award cap, CONFIRMED (Sec5.4, 2026-10-09 WAVE 10
    // follow-up): a lifetime-total ceiling populated once at load by the
    // respect_levels.xtbl loader as Sigma(row.Respect for all loaded rows)
    // minus the LAST row's own Respect (238,875 with the real 50-row
    // shipped table, L50's own Respect being 0). Computed from the loaded
    // table directly rather than hardcoded, so a test-supplied table still
    // produces the correct cap for its own data.
    uint32_t respectLifetimeCap() const {
        if (respectLevelTable_.empty()) return 0;
        uint32_t sum = 0;
        for (const auto& row : respectLevelTable_) sum += static_cast<uint32_t>(row.respect.value);
        return sum - static_cast<uint32_t>(respectLevelTable_.back().respect.value);
    }
    void awardRespect(int32_t amount) {
        if (amount <= 0) { ++respectAwardNonPositiveGuardCount_; return; }
        // CONFIRMED (Sec5.4): strictly after the amount>0 guard. If total >=
        // cap or level >= levelCount-1, the award grants nothing at all - a
        // full skip (no running-total update, no stat record, no level-up),
        // not a 0-clamp.
        const uint32_t cap = respectLifetimeCap();
        const size_t levelCount = respectLevelTable_.size();
        if (respectTotal_ >= cap || (levelCount > 0 && respectLevel_ >= levelCount - 1)) return;
        // Otherwise the actual grant is min(amount, cap - total), and it is
        // this CLAMPED grant - not the raw amount argument - that feeds both
        // the running total and the level-up routine.
        const uint32_t grant = std::min(static_cast<uint32_t>(amount), cap - respectTotal_);
        respectTotal_ += grant;
        recordStatRecordOpen(kRespectAwardStatId);
        applyRespectLevelUp(grant);
    }

    // Level-up mechanism, CONFIRMED (Sec5.4): 0x009db8e0(playerObj, amount)
    // loops: compares `+0x1ed4 + amount` against 0x0060eb60(playerObj) (the
    // cost of the next level) - 0x0060eb60 is NOT a separate algorithm from
    // Sec5.2's own 0x0060eb40/0x0060ebb0 pair, it is the exact same
    // accessor wrapped to pull the level index from the player-object
    // pointer first, so this reuses respectLevelCost() above directly,
    // never a second cost table; while over threshold: increments
    // `+0x1ed8`, calls the unlock-consumer and the decade-milestone hook
    // below, and carries the remainder; caps at kRespectLevelCap.
    void applyRespectLevelUp(uint32_t amount) {
        uint32_t remaining = respectBanked_ + amount;
        while (respectLevel_ < kRespectLevelCap) {
            const uint32_t cost = respectLevelCost(respectLevel_);
            if (remaining < cost) break;
            remaining -= cost;
            // CHOSEN indexing: Sec5.2's own CONFIRMED finding is specifically
            // about 0x0060eb40/0x0060ebb0's own "record L (0-based) is the
            // cost of the next level" mapping, not independently
            // re-confirmed for 0x0060ebf0's own call-site argument timing
            // (before vs. after this increment). This project reads the
            // record whose COST was just paid (0-based index == the level
            // count BEFORE this increment) as the same row that carries the
            // level just reached's own Unlockables, since Sec5.2 already
            // ties cost and unlockables to the same <respect_level> row -
            // the natural, non-fabricated reading, not independently
            // re-derived from 0x0060ebf0's own disassembly.
            const uint32_t reachedLevelRow = respectLevel_;
            ++respectLevel_;
            consumeRespectLevelUnlocks(reachedLevelRow);
            runRespectDecadeMilestone(respectLevel_);
            postRespectLevelUpHint();
        }
        respectBanked_ = remaining;
    }

    // 0x0060ebf0(level) (Sec5.2/Sec5.4): for each of that row's Unlockable
    // ids NOT YET APPLIED (CONFIRMED gate), calls the real shared "unlock
    // this named thing" primitive 0x0071ccf0 (also used elsewhere for
    // stronghold-completion unlocks, not reproduced here - only its own
    // observable live-flag effect is modeled, same convention as every
    // other "shared primitive, effect modeled not the primitive itself"
    // field in this header) and flips the live per-name flag above -
    // special-casing the literal name "HomieSlot1" with its own distinct
    // notification (CONFIRMED).
    void consumeRespectLevelUnlocks(uint32_t row) {
        if (row >= respectLevelTable_.size()) return;
        for (const std::string& name : respectLevelTable_[row].unlockables) {
            if (respectUnlockApplied(name)) continue;
            respectUnlockablesFlag_[name] = true;
            if (name == "HomieSlot1") ++respectHomieSlot1UnlockCount_;
        }
    }

    // Level-up notification (Sec5.4): hint id 0xa9 posted through
    // 0x007169e0 - the SAME 210-message HUD hint/tutorial dispatcher this
    // project already counts via recordTutorialAdvance/tutorialAdvanceCount
    // above (Sec10.4) for ordinary first-use tutorial tips - reused
    // directly, not a second, bespoke "level up!" popup widget.
    void postRespectLevelUpHint() { recordTutorialAdvance(kRespectLevelUpHintId); }

    void runRespectDecadeMilestone(uint32_t newLevel) {
        if (newLevel == 0 || newLevel % 10 != 0) return;
        if (!respectDecadeMilestone_.inOpenWorldZone) return;
        respectDecadeMilestone_.lastEventId = (newLevel == 50) ? 0x23 : 0x22;
        respectDecadeMilestone_.lastPosition = respectMilestonePlayerPosition_;
        ++respectDecadeMilestone_.fireCount;
    }

    // tutorial_set_hud_help_closing (Sec16.3): global 0x0151d5a4, direct
    // unconditional write, no gate.
    bool& tutorialHudHelpClosing() { return tutorialHudHelpClosing_; }

    // completion_load_lut (Sec16.4): the "none"-prefix clear path is fully
    // CONFIRMED and safe (slot 2 := {-1, 0.0}); the resolve-a-named-LUT
    // path has no real name->id table in this project's scope (same class
    // of gap as vehicle_anim_start above) - reuses this project's own
    // established "no real table, a fresh monotonically-increasing handle
    // stands in" convention (audio_conversation_load_direct's own
    // precedent, Sec8.1) rather than refusing, since nothing in scope
    // reads the resolved id back either way.
    struct ColorGradingLutSlot { int32_t lutId = -1; double intensity = 0.0; };
    void completionLoadLut(const std::string& name) {
        // _strnicmp-style 4-char CASE-INSENSITIVE PREFIX match (Sec16.4's
        // own text: "any string starting with those 4 characters... not
        // only an exact match").
        bool isNonePrefixed = name.size() >= 4 &&
            std::tolower(static_cast<unsigned char>(name[0])) == 'n' &&
            std::tolower(static_cast<unsigned char>(name[1])) == 'o' &&
            std::tolower(static_cast<unsigned char>(name[2])) == 'n' &&
            std::tolower(static_cast<unsigned char>(name[3])) == 'e';
        if (isNonePrefixed) {
            colorGradingLutSlots_[2] = {-1, 0.0};
        } else {
            colorGradingLutSlots_[2] = {static_cast<int32_t>(nextLutId_++), 1.0};
        }
    }
    const ColorGradingLutSlot& colorGradingLutSlot(int i) const { return colorGradingLutSlots_[i]; }

    // completion_cutscene_was_active (Sec16.5): pure read of global
    // 0x02282283 - this project's own real writer is out of scope (Sec16.5's
    // own text), so a test sets this directly.
    bool& completionCutsceneWasActive() { return completionCutsceneWasActive_; }

    // pcr_change_hair/pcr_change_composite/pcr_get_player_preset/
    // pcr_set_identity (Sec16.6/16.7/16.8/16.8a) - see PcrCustomizationState's
    // own doc comment above for the full shared-state/cross-batch reasoning.
    PcrCustomizationState& pcrCustomization() { return pcrCustomization_; }

    // dialog_box_disconnect (Sec16.9): every real internal consequence
    // (session/pause-UI teardown) is out of this project's modeled scope
    // (no session/pause-UI state anywhere in this project) - a pure call
    // counter is the only test-observable fact.
    uint64_t dialogBoxDisconnectCount() const { return dialogBoxDisconnectCount_; }
    void recordDialogBoxDisconnect() { ++dialogBoxDisconnectCount_; }

    // main_menu_supress_profile_change (Sec16.10): global 0x022425f6,
    // direct unconditional write, no gate.
    bool& mainMenuProfileChangeSuppressed() { return mainMenuProfileChangeSuppressed_; }

    // pause_menu_control_scheme_init (Sec16.11): global 0x0229a558 (the
    // scheme id); the 2 stored-preset globals (0x01411b08/0x01411b10) have
    // no in-scope WRITER (this batch names no "pause_menu_save_control_
    // scheme" - a test sets them directly). Return shape (0/2/3 pushes) is
    // reproduced in the stub itself, not here.
    int32_t& pauseMenuControlScheme() { return pauseMenuControlScheme_; }
    int32_t& pauseMenuPresetScheme0() { return pauseMenuPresetScheme0_; }
    int32_t& pauseMenuPresetScheme1Or2() { return pauseMenuPresetScheme1Or2_; }

    // pause_menu_update_option (Sec16.12) / pause_menu_restore_defaults
    // (Sec16.13): the real dispatcher maps ~25 individual (category,
    // option) pairs to individual engine setters (audio-bus volumes,
    // mouse-sensitivity curves, gamma, ...) - Sec16.12's own text gives
    // only 4 representative examples, not the full enumeration ("full case
    // enumeration not reproduced here, matching house style"), and none of
    // those ~25 real target subsystems exist anywhere else in this
    // project's scope. Inventing the other ~20 mappings would be
    // fabricating data this spec section does not supply - this project
    // instead records the raw (category, option, flag, value) request,
    // the SAME "record the raw request, not the unreadable real internal
    // effect" convention this header already uses throughout (e.g.
    // MinimapIconRecord). restore_defaults is the sibling "reset" request,
    // keyed only by category (no option/value).
    struct PauseMenuOptionRequest {
        int32_t optionId = 0;
        bool flag = false;
        double value = 0.0;
        uint64_t count = 0;
    };
    void recordPauseMenuUpdateOption(int32_t category, int32_t optionId, bool flag, double value) {
        auto& rec = pauseMenuOptionRequests_[{category, optionId}];
        rec.optionId = optionId;
        rec.flag = flag;
        rec.value = value;
        ++rec.count;
    }
    const PauseMenuOptionRequest* pauseMenuOptionRequest(int32_t category, int32_t optionId) const {
        auto it = pauseMenuOptionRequests_.find({category, optionId});
        return it != pauseMenuOptionRequests_.end() ? &it->second : nullptr;
    }
    void recordPauseMenuRestoreDefaults(int32_t category) { ++pauseMenuRestoreDefaultsCounts_[category]; }
    uint64_t pauseMenuRestoreDefaultsCount(int32_t category) const {
        auto it = pauseMenuRestoreDefaultsCounts_.find(category);
        return it != pauseMenuRestoreDefaultsCounts_.end() ? it->second : 0;
    }

    // store_common_bg_anim_complete (Sec16.14): global 0x022ccee8, set to
    // 1 unconditionally, no arguments.
    bool& storeCommonBgAnimComplete() { return storeCommonBgAnimComplete_; }

    // store_weapon_hide_weapon (Sec16.15): global handle 0x022ce900 - no
    // in-scope writer ever sets it non-null (same "mechanism confirmed, no
    // in-scope producer" convention), so a test sets storeWeaponPreviewActive
    // directly to exercise the detach branch. The 3 always-reset globals
    // (0x01300aa0/0x022ce904/+0x90d) collapse to one flat reset, matching
    // this header's "record the real branch, not unreadable bookkeeping"
    // convention.
    bool& storeWeaponPreviewActive() { return storeWeaponPreviewActive_; }
    void storeWeaponHideWeapon() {
        if (storeWeaponPreviewActive_) {
            ++storeWeaponDetachCount_;
            storeWeaponPreviewActive_ = false;
        }
        storeWeaponPreviewSlot_ = -1;
        ++storeWeaponHideWeaponCallCount_;
    }
    int32_t storeWeaponPreviewSlot() const { return storeWeaponPreviewSlot_; }
    uint64_t storeWeaponDetachCount() const { return storeWeaponDetachCount_; }

    // =====================================================================
    // Batch 4 (spec-lua-api-behaviour.md Sec13+Sec14, ranks ~151-250,
    // native-implementation batch 2026-10-08, 55 names) - see each field's
    // own citation below; full per-native reasoning lives in the stub
    // file's own doc comments
    // (lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp).
    // =====================================================================

    // notoriety_spawn_group_set_delay (Sec13.2): arg 1 names a notoriety
    // GROUP, resolved via a 24-entry group-name table (0x008ed350) - a
    // genuinely DIFFERENT, disjoint table from lookupNotorietyTrackCode's
    // own TRACK table (Sec3.6). An unregistered group name is a real,
    // CONFIRMED no-op (Sec13.2's own text has no stated failure sentinel to
    // reuse, so this project's own OpenValueMap<bool> OPEN-until-set
    // discipline applies directly). Sec6.2's own text leaves WHICH 24 names
    // populate the table OPEN in isolation, but applySpecInitialState
    // (lua_spec_initial_state.cpp, item 13, 2026-10-09) now pre-seeds the
    // exact 24 to resolve=true, CONFIRMED by cross-reference against
    // spec-tables-progression.md Sec6.3 (the notoriety_spawn.xtbl loader's
    // own independently-derived 24-name table and matching +0x3E0/+0x3E4/
    // +0x3E8 destination-structure offsets - conclusive that both specs
    // describe the SAME fixed array). Any name outside those 24 (or a
    // different-case spelling of one of them - case-folding this lookup is
    // out of that fix's scope) stays genuinely OPEN-until-set.
    OpenValueMap<bool>& notorietySpawnGroupResolves() { return notorietySpawnGroupResolves_; }
    struct NotorietySpawnGroupState {
        uint64_t clearFlagCount = 0; // +0x3c1 clear, unconditional on success (CONFIRMED)
        // The generic expiry-timestamp setter 0x00d9e140 needs a real "now"
        // counter (0x01320d94) this project does not model anywhere (no
        // frame-time/now global exists in this header) - the raw rounded
        // delta argument is recorded honestly instead of a fabricated
        // wrapped sum, same "record the raw request" convention as
        // MinimapIconRecord above.
        int64_t lastDelayRequested = 0;
        uint64_t delayRequestCount = 0;

        // notoriety_spawn_group_set_destination (Sec25.1) / notoriety_spawn_
        // group_clear_destination (Sec25.2), wave 2 batch W2-4: SAME group
        // table (0x008ed350) as the rest of this struct, a shared setter
        // (0x008edfb0, newly identified by Sec25.1's own text - this
        // project's own field-level writes past the call shape are not
        // further decompiled, OPEN). "" = no destination set (the real
        // cleared/zeroed-buffer case).
        std::string destinationTargetName;
        uint64_t setDestinationCount = 0;
        uint64_t clearDestinationCount = 0;

        // notoriety_force_no_spawn_group (Sec6.2's sibling, Sec6.10, batch
        // b29, 2026-10-08): the SAME `+0x455` "force no spawn" flag byte
        // Sec3.6's own notoriety_force_no_spawn already models (track-
        // keyed, lookupNotorietyTrackCode) - here reached through THIS
        // group resolver instead, confirmed (Sec6.10) to be a single,
        // unconditional, ungated instruction write (no host-check, no
        // replicate - unlike the alias-track setters above). HIGH
        // CONFIDENCE, not independently proven with a shared-instance
        // check (Sec6.22's own hedge), that the group/track records share
        // one underlying type - kept as its OWN field regardless, since a
        // DIFFERENT resolver reaches it here.
        bool forceNoSpawn = false;
        uint64_t forceNoSpawnSetCount = 0;

        // disable_specific_notoriety_group (Sec6.2, batch b29): per-(tier,
        // item-name) flag byte within this group's own tier/sub-item table
        // (+0x3e8). CONFIRMED mechanism (resolve group -> range-check tier
        // -> case-insensitive item-name scan -> write); the per-group tier
        // MIN/MAX bounds are not given by spec (see
        // notorietyGroupTierInRangeResolves() above) so the in-range gate
        // is OPEN-until-set, not guessed - this map only holds the write
        // itself, once in-range is confirmed.
        std::map<int64_t, std::unordered_map<std::string, bool>> tierItemFlags;
        uint64_t disableSpecificGroupWriteCount = 0;
    };
    NotorietySpawnGroupState& getOrCreateNotorietySpawnGroup(const std::string& name) {
        return notorietySpawnGroups_[name];
    }

    // hud_prompt (Sec13.3): success path (arg 1 resolves) records a
    // request; failure path resets the global default/HUD-prompt state
    // (0x00a36e60(0), independently decompiled, 3 real callers project-
    // wide) - counted, not modelled further (its own internal fields are
    // OPEN).
    struct HudPromptRequest {
        std::string target, arg2, arg3;
    };
    std::vector<HudPromptRequest>& hudPromptRequests() { return hudPromptRequests_; }
    void recordHudPrompt(const std::string& target, const std::string& a2, const std::string& a3) {
        hudPromptRequests_.push_back({target, a2, a3});
        hudPromptActive_ = true; // 0x00a370c0 stores a nonzero prompt id into 0x02703cac ("prompt active")
        hudPromptCallbackName_ = a3; // slot 9 (local) interned callback name, 0x005e46b0
    }
    void recordHudPromptDefaultReset() {
        ++hudPromptDefaultResetCount_;
        hudPromptActive_ = false; // 0x00a36e60(0): zeroes 0x02703cac, clears slots 9/10
        hudPromptCallbackName_.clear();
    }
    // Sec13.3 (e8dc04d): hud_prompt's arg-3 callback fires on an interact press
    // (handler-table 0x0111f2d0 entry 13, 0x009da810) while 0x02703cac != 0 (and
    // no lower-bit handler consumed the press): the named global is called with
    // NO args, then the prompt is cleared (0x00a36e60(0)). Returns the callback
    // name to invoke ("" when no prompt is active - the press is not consumed)
    // and clears the prompt. The caller (the interact-press dispatcher) does the
    // by-name call.
    bool hudPromptActive() const { return hudPromptActive_; }
    std::string takeHudPromptCallbackOnInteract() {
        if (!hudPromptActive_) return std::string();
        std::string name = std::move(hudPromptCallbackName_);
        hudPromptActive_ = false;
        hudPromptCallbackName_.clear();
        return name;
    }
    uint64_t hudPromptDefaultResetCount() const { return hudPromptDefaultResetCount_; }

    // customization_outfit_wear (Sec13.4): arg 1 is a data-table key hashed
    // via sr3save::nameHash (CONFIRMED match to 0x00d9e8b0, see this
    // header's own top comment) - NOT a live-object resolve. Bit 0x1
    // (default on) applies locally + always replicates (Sec13.4's own
    // text: the record-and-replicate call is entirely inside the apply
    // delegate, keyed by the hash, no per-object double-gate at this
    // level); bit 0x2 (default on) additionally applies to the co-op
    // player representation, gated on the SAME playerRig().coopPlayerPresent
    // this project already models for the identical real global
    // (0x009df3d0, Sec13.29 item 8 / Sec14.31's own "dual local-player
    // representation" finding - the SAME real global, reused directly, not
    // re-derived).
    struct CustomizationOutfitWearRequest {
        uint32_t hashedName = 0;
        double variant = 0.0;
        int64_t flags = 3;
        bool secondRepresentationApplied = false;
    };
    std::vector<CustomizationOutfitWearRequest>& customizationOutfitWearRequests() {
        return customizationOutfitWearRequests_;
    }

    // customization_item_wear (Sec14.12) / customization_item_revert
    // (Sec14.13): share the SAME dual local-player customization state as
    // customization_outfit_wear above (Sec14.31's own cross-batch note) -
    // the much larger PCU/PCR family (backlog) will need to look at this
    // same modelling choice. Record-only (the real item/variant/color
    // resolve chain and the 63-slot item list are OPEN/not modelled).
    struct CustomizationItemWearRequest {
        std::string item, variant, color;
        bool immediateWear = true;
        int64_t flags = 3;
        bool refreshThumbnails = false;
        bool appliedPrimary = false;
        bool appliedSecondRepresentation = false;
    };
    std::vector<CustomizationItemWearRequest>& customizationItemWearRequests() {
        return customizationItemWearRequests_;
    }
    bool& customizationRevertPending() { return customizationRevertPending_; }
    uint64_t& customizationItemRevertCount() { return customizationItemRevertCount_; }
    uint64_t& customizationUiRefreshTailCount() { return customizationUiRefreshTailCount_; }

    // character_ragdoll_clear_last_resort_position (Sec13.6): no per-object
    // target at all (CONFIRMED - the function reads no argument). A single
    // GLOBAL cache clear, always replicated (literal 3 runs both branches,
    // CONFIRMED). DIFFERENT from the already-registered character_ragdoll_
    // set_last_resort_position and from CharacterState::
    // ragdollValidPositionHistory above (Sec13.25) - see the scoping
    // report's own near-duplicate warning.
    uint64_t& ragdollLastResortPositionClearCount() { return ragdollLastResortPositionClearCount_; }

    // refraction_situation_set (Sec13.10): arg 1 is a data-table key hashed
    // via sr3save::nameHash (same CRC-32 as customization_outfit_wear
    // above). Bit 0x1 (default on): local apply (the real destination slot
    // index this project has no source for, Sec13.10's own OPEN item, is
    // not modelled - only that an apply happened). Bit 0x2 (default on):
    // replicate.
    struct RefractionSituationRequest {
        uint32_t hashedName = 0;
        int64_t flags = 3;
        bool appliedLocally = false;
    };
    std::vector<RefractionSituationRequest>& refractionSituationRequests() { return refractionSituationRequests_; }

    // hud_achievement_show_progress (Sec13.22): arg 1 is a data-table key
    // hashed via sr3save::nameHash, looked up in a small fixed achievement
    // roster this project has no real contents for - test-registrable only
    // (same "never-populated object space" precedent). An unregistered
    // name is a real, CONFIRMED no-match no-op.
    std::unordered_set<std::string>& knownAchievementsForTesting() { return knownAchievements_; }
    uint64_t& achievementToastCount() { return achievementToastCount_; }
    uint64_t& achievementReplicateCount() { return achievementReplicateCount_; }
    // current==total, 1 or 2 unidentified completion handlers fire
    // (Sec13.22's own OPEN item re: which bits of arg 4 select which) - not
    // individually counted, one shared counter.
    uint64_t& achievementCompletionCount() { return achievementCompletionCount_; }

    // set_team (Sec13.18): reuses lookupNotorietyTrackCode/
    // registerNotorietyTrackCodeForTesting/kNotorietyTrackCodeNotFound
    // above directly - Sec13.18's own real, CONFIRMED finding that a
    // character's team is drawn from the IDENTICAL gang/faction enumeration
    // as the notoriety-track system (0x0094cc60, the SAME resolver
    // notoriety_force_no_spawn's own arg 1 already uses, Sec3.6).

    // hud_timer_hide (Sec13.12): a fixed pool of (at most) 4 HUD-timer
    // slots - CHOSEN all-false ("not hidden") at construction, since no
    // spec gives a real start-up value for either byte array.
    struct HudTimerSlots {
        std::array<bool, 4> hidden{};          // 0x022b6665, stride 0x40 (per-slot visibility byte)
        std::array<bool, 4> layoutCompanion{}; // 0x022b6664 (companion byte)
        bool anyVisibleSummary = false;        // 0x022b65f5
        int32_t layoutMode = 0;                // 0x022b6630 (CHOSEN 0; escalates 2 -> 4 per Sec13.12's own text)
    };
    HudTimerSlots& hudTimerSlots() { return hudTimerSlotsPool13_; }

    // hud_critical_timer_set (Sec13.13): "only actually runs once per
    // timer" is CONFIRMED gated on a single GLOBAL (0x014c835c == 0, not
    // per-slot) - this project's own direct reproduction: the FIRST call
    // across the whole run configures; every later call is a no-op for
    // that part (CHOSEN false = "not yet configured").
    struct HudCriticalTimerState {
        bool configured = false;
        int64_t timerId = 0;
        std::string labelText;
        std::string secondaryKey;
        bool secondaryKeyResolved = false;
    };
    HudCriticalTimerState& hudCriticalTimer() { return hudCriticalTimer_; }
    uint64_t& hudCriticalTimerLogCount() { return hudCriticalTimerLogCount_; }
    uint64_t& hudCriticalTimerNotifyCount() { return hudCriticalTimerNotifyCount_; }
    // 0x00a1fcc0's own real mechanism (a ~350-slot fixed name->id registry
    // with insertion-on-miss) IS modelled exactly, by sequential-id
    // assignment on first use - the registry's own real starting id/base is
    // not given, so this project's own monotonic counter starts at 1
    // (CHOSEN, same "a fresh monotonically-increasing handle stands in for
    // the real packed identifier" convention as nextVisualEffectHandle
    // above).
    int32_t internHudSecondaryKey(const std::string& key) {
        auto it = hudCriticalTimerSecondaryKeyIds_.find(key);
        if (it != hudCriticalTimerSecondaryKeyIds_.end()) return it->second;
        int32_t id = nextHudCriticalTimerSecondaryKeyId_++;
        hudCriticalTimerSecondaryKeyIds_[key] = id;
        return id;
    }

    // spawn_region_toggle_filter (Sec13.9): the byte at the resolved
    // region's own +0x95 field - CHOSEN 0 ("never toggled"), same
    // convention as notorietyTrackFlags_ above (Sec3.6).
    uint8_t& spawnRegionFilterBits(const std::string& name) { return spawnRegionFilterBits_[name]; }

    // ai_set_personality (Sec13.27): see CharacterState::
    // aiPersonalityProfileCode above for the per-character write; this is
    // the name->code test-registration hook (same "never-populated object
    // space" precedent as lookupNotorietyTrackCode/lookupSyncedActionIndex
    // above), sentinel 0x14 (20) the CONFIRMED real fallback.
    static constexpr int32_t kAiPersonalityProfileFallback = 0x14;
    void registerAiPersonalityProfileForTesting(const std::string& name, int32_t code) {
        aiPersonalityProfileCodes_[name] = code;
    }
    int32_t lookupAiPersonalityProfileCode(const std::string& name) const {
        auto it = aiPersonalityProfileCodes_.find(name);
        return it != aiPersonalityProfileCodes_.end() ? it->second : kAiPersonalityProfileFallback;
    }

    // set_behavior_set (Sec14.27): same "never-populated object space"
    // test-registration hook as above, sentinel 0x2c (44) the CONFIRMED
    // real "no match" fallback.
    static constexpr int32_t kBehaviorSetNotFound = 0x2c;
    void registerBehaviorSetForTesting(const std::string& name, int32_t code) { behaviorSetCodes_[name] = code; }
    int32_t lookupBehaviorSetCode(const std::string& name) const {
        auto it = behaviorSetCodes_.find(name);
        return it != behaviorSetCodes_.end() ? it->second : kBehaviorSetNotFound;
    }

    // action_play_synced_state (Sec14.1): reuses lookupSyncedActionIndex/
    // registerSyncedActionForTesting/kSyncedActionNotFound above directly
    // (the SAME real mechanism, 0x0095da50, already corrected and modelled
    // for action_play_synced_do's own identical call, Sec33.3). The per-
    // actor local-authority gate (0x008addb0) is explicitly "not previously
    // attributed to a specific subsystem" by Sec14.1's own text - kept
    // SEPARATELY SCOPED from controlLockGatePasses, same reasoning as
    // CharacterState::scriptedActionDispatchGatePasses above, but here
    // keyed per NAMED actor (Sec14.1's own "per-actor" phrasing), not a
    // flat global.
    OpenValueMap<bool>& syncedActionAppliesLocally() { return syncedActionAppliesLocally_; }

    // mission_set_excluded_reward (Sec14.8): SEPARATELY SCOPED from
    // EngineState::MissionContext::activeAndRunning above (same real pair
    // of globals, 0x014c8460/0x014c7a14, but this project's own established
    // convention - see that field's own doc comment - is to NOT merge
    // across call sites unless a spec explicitly confirms shared scope).
    struct MissionExcludedRewardsState {
        OpenValue<bool> activeAndRunning{
            "mission active, phase != 8 (0x014c8460/0x014c7a14) - mission_set_excluded_reward's own gate",
            "spec-lua-api-behaviour.md Sec14.8"};
        OpenValue<bool> descriptorBit8At0xc{"per-object-type descriptor bit 0x8 at +0xc (0x02cc9900)",
                                            "spec-lua-api-behaviour.md Sec14.8"};
        std::vector<uint32_t> excludedRewardHashes; // de-duped, CRC-32 (sr3save::nameHash) of the reward name
        uint64_t dedupSkipCount = 0;
    };
    MissionExcludedRewardsState& missionExcludedRewards() { return missionExcludedRewards_; }

    // debris_flow_set_active (Sec14.20): record-only (no coordinate system
    // anywhere in this project, same stated gap WorldPosition's own doc
    // comment establishes) - only the real, CONFIRMED-structure parts are
    // tracked: whether the relative-to location resolved, the 3-slot fixed
    // global table lookup (size CONFIRMED, contents not), and the host-
    // check gate (reuses EngineState::coopLocalIsHost() directly, the SAME
    // real singleton, Sec8.27/Sec6.22/Sec10.9).
    struct DebrisFlowState {
        std::array<bool, 3> slotActive{}; // the 3-slot fixed global table, 0x014fde60
        uint64_t activateCount = 0;
        uint64_t relativeLocationAppliedCount = 0;

        // debris_flow_create (Sec25.21) / debris_flow_destroy (Sec25.20) /
        // debris_flow_add_object (Sec22.21) / debris_flow_add_avoid_object
        // (Sec25.22), wave 2 batch W2-4: CONFIRMED (Sec22.21's own text) the
        // "flow id" debris_flow_add_object walks is the SAME small fixed
        // ~3-entry table (0x014fde60) debris_flow_set_active's own `index`
        // argument already indexes above (slotActive) - this project reuses
        // the SAME 3-slot space rather than inventing a second, disjoint id
        // space. A slot is "allocated" by debris_flow_create and freed by
        // debris_flow_destroy; this is INDEPENDENT of slotActive above
        // (debris_flow_set_active's own, unrelated "active" flag on the same
        // table) - the two features share a table, not a meaning.
        //
        // DESIGN NOTE for the sibling wave-2 batch that owns debris_flow_
        // set_inactive (a REAL memory-safety finding there - a null virtual
        // call on a stale handle, out of THIS batch's scope): `allocated`/
        // `destroyed` are kept as SEPARATE booleans, deliberately NOT folded
        // into one tri-state enum, specifically so that sibling can add its
        // own `inactive` bool alongside them without redesigning this record
        // - do not assume `allocated && !destroyed` implies "safe to use
        // every way" beyond what THIS batch's own natives state.
        struct FlowInstance {
            bool allocated = false;
            bool destroyed = false;
            std::vector<std::string> objects;      // debris_flow_add_object's own resolved names, call order
            std::vector<std::string> avoidObjects;  // debris_flow_add_avoid_object's own resolved names, call order
        };
        static constexpr size_t kFlowSlotCount = 3;
        std::array<FlowInstance, kFlowSlotCount> flows{};
        uint64_t createCount = 0;
        uint64_t createRecycledCount = 0; // CHOSEN (hardened divergence): all 3 slots already live -> reuses slot 0
        uint64_t destroyCount = 0;
        uint64_t destroySkippedCount = 0;       // out-of-range or already-not-allocated id (HARDENING POLICY)
        uint64_t addObjectCount = 0;
        uint64_t addObjectNoMatchCount = 0;     // id does not match an allocated slot -> real "no apply" branch (Sec22.21)
        uint64_t addAvoidObjectCount = 0;
        uint64_t addAvoidObjectSkippedCount = 0; // out-of-range id (HARDENING POLICY - this project always bounds its own array access)
    };
    DebrisFlowState& debrisFlow() { return debrisFlow_; }

    // camera_restrict_set_limits (Sec14.21): one fixed camera-restriction
    // record (this project's own stand-in for a singular, un-named real
    // record Sec14.21's own text does not further identify) plus the
    // local-player-name comparison its own 3-way dispatch needs (a
    // SEPARATELY SCOPED stand-in - no prior field in this header carries
    // the local player's own NAME as a string, only presence/gender
    // booleans, hasLocalPlayer()/playerRig() above).
    struct CameraRestrictLimits {
        double minYaw = 0, maxYaw = 0, minPitch = 0, maxPitch = 0;
        bool arg6 = false;
        bool written = false;
    };
    CameraRestrictLimits& cameraRestrictLimits() { return cameraRestrictLimits_; }
    OpenValue<std::string>& localPlayerNameForTesting() { return localPlayerName_; }

    // camera_restrict_set_active (Sec8.12, 0x00a445e0, batch b30,
    // 2026-10-08). CONFIRMED: a SEPARATE flag from CameraRestrictLimits
    // above - Sec8.12's own text never cross-cites Sec14.21's record, and
    // the two are different real addresses, so this project does not
    // assume an undocumented shared instance (same caution as the
    // forceNoSpawn/tierItemFlags split above). CHOSEN starting value
    // (false, "not restricted") - no spec states this flag's construction
    // value either way.
    bool cameraRestrictionActive() const { return cameraRestrictionActive_; }
    void setCameraRestrictionActive(bool active) { cameraRestrictionActive_ = active; }

    // audio_conversation_playing (Sec14.22): a GENUINELY DIFFERENT mechanism
    // from audio_any_conversation_playing/conversationChannelActive above
    // (Sec33.4's own per-session-channel flag) - this one walks a linked
    // list of SLOT-INDEXED conversation entries (0x006de990) or queries one
    // slot directly (0x00877dc0). Kept as its own, separately-keyed map -
    // same near-duplicate caution this header already applies elsewhere
    // (e.g. CharacterState::isAlive vs. isDeadHighConfidence).
    OpenValueMap<bool>& conversationSlotPlaying() { return conversationSlotPlaying_; }
    // Sec14.22 "Per-slot query, exact" (2026-10-12, FINAL) port rule: report true while the conversation's
    // lines are playing; a host with no voice playback reports false. A slot nobody marked playing is false.
    bool conversationSlotActive(const std::string& slotKey) const {
        return conversationSlotPlaying_.known(slotKey) ? conversationSlotPlaying_.get(slotKey) : false;
    }

    // drug_effect_set_override_values (Sec14.19): a fixed channel-0 entry of
    // a structurally multi-channel pair of parallel arrays (0x02625848/
    // 0x02625840) this project only ever reaches channel 0 of (HIGH
    // CONFIDENCE other channels exist and are reachable elsewhere, per
    // Sec14.19's own text) - CHOSEN 0.0 defaults.
    double& drugEffectOverrideValueA() { return drugEffectOverrideA_; }
    double& drugEffectOverrideValueB() { return drugEffectOverrideB_; }

    // roadblocks_enable (Sec14.28): a single global byte - CHOSEN true
    // ("enabled"), matching optionalBoolDefault's own default=true reading
    // for an absent/non-boolean argument (Sec14.28's own text: "applies
    // both when absent and whenever the presence/type test itself fails").
    bool& roadblocksEnabled() { return roadblocksEnabled_; }
    uint64_t& roadblocksDisableCleanupCount() { return roadblocksDisableCleanupCount_; }

    // crib_disable_interface (Sec13.24): a single global byte - CHOSEN
    // false ("not disabled").
    bool& cribInterfaceDisabled() { return cribInterfaceDisabled_; }

    // action_nodes_enable (Sec13.17): a single global byte - CHOSEN false
    // ("not enabled" - no spec gives a real start-up value).
    bool& actionNodesEnabled() { return actionNodesEnabled_; }

    // camera_shake_play (Sec13.15): last-request stand-in, always
    // replicated (CONFIRMED - no double-gate skip at this native).
    struct CameraShakeState {
        std::string lastProfile;
        double lastMagnitude = 1.0;
        uint64_t playCount = 0;

        // camera_shake_stop (Sec17.14) / camera_shake_play_looping
        // (Sec17.15), wave 2 batch W2-2 (2026-10-08): the SAME shared
        // global camera-shake record family (CONFIRMED - both share the
        // recovered opcode-0x43 subtype 0xf) - a DISTINCT "looping" slot
        // from the one-shot lastProfile/lastMagnitude above (camera_shake_
        // play, Sec13.15, is a genuinely different, simpler dispatch per
        // this batch's own transposition-risk note - never conflated).
        std::string loopingProfile;
        double loopingMagnitude = 1.0;
        bool loopingActive = false;
        uint64_t loopingPlayCount = 0;
        uint64_t stopCount = 0;
    };
    CameraShakeState& cameraShake() { return cameraShake_; }

    // camera_end_look_through (Sec13.16): the fixed 3-call teardown
    // sequence's own internals (0x0056cc30/0x00564910/0x007f1070) are OPEN -
    // counted only, same "record that a call happened" convention used
    // throughout this batch.
    uint64_t& cameraEndLookThroughCount() { return cameraEndLookThroughCount_; }
    uint64_t& cameraEndLookThroughSmoothCount() { return cameraEndLookThroughSmoothCount_; } // the boolean-gated 4th call

    // =====================================================================
    // Batch 3 (2026-10-08), spec-lua-api-behaviour.md Sec11 ("ranks 151-250
    // tranche, part D") + Sec12 ("ranks 151-250 tranche, part A") - PCR
    // quick-queries + vehicle/HUD/effect cluster. See each stub's own doc
    // comment (lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp) for the
    // full per-function citation/reasoning this section's fields rest on.
    // Per-character/per-vehicle fields for this batch live on
    // CharacterState/VehicleState above (own doc comments there); this
    // section holds only the EngineState-level (singleton/global) slots.
    // =====================================================================

    // pcr_is_plastic_surgeon (Sec11.1, 0x00830d10): CONFIRMED a single
    // global byte flag, pushed directly - no spec-stated construction
    // value, OPEN until a test sets it.
    OpenValue<bool>& pcrPlasticSurgeonMode() { return pcrPlasticSurgeonMode_; }

    // pcr_mark_changed (Sec11.2, 0x00831020): CONFIRMED unconditional write
    // of a dirty bit - no read-side OPEN question (every call sets it, no
    // gate), so a plain bool stands in rather than an OpenValue.
    bool& pcrDirty() { return pcrDirty_; }

    // pcr_apply_morph_value/pcr_get_morph_value (Sec11.3/Sec11.4,
    // 0x00834fe0/0x00832880): CONFIRMED resolver shape - arg 1 resolves
    // against a small named session/context table (0x00831650) whose real
    // CONTENTS this spec text does not supply (same "no real table data in
    // scope" boundary as EngineState::resolveTeamId()'s team-name registry/
    // PcuCategoryTable above - a never-populated registry is this project's
    // own honest default, not a simplification of the resolve contract
    // itself); once resolved, the morph-slot argument (arg 2, type-
    // polymorphic: a number reads as an index, anything else as a category
    // name) resolves the SAME way against a second, per-session table whose
    // contents are equally not given. Both maps are test-populated (same
    // convention as PcuCategoryTable::kindByIndex) - an unpopulated
    // session/slot is the CONFIRMED "not found" case (Sec11.3's own text:
    // the setter does nothing further; Sec11.4's own text: the getter
    // returns zero Lua values), not an OPEN refusal, so OpenValueMap<bool>
    // is used here purely as a test-populated presence registry via
    // .known()/.get(), never via a direct .get() that could throw on a
    // genuinely-unpopulated (not just "never resolved") key.
    struct PcrMorphState {
        OpenValueMap<bool> sessionResolves{"pcr session/context resolves (0x00831650 case-insensitive scan)",
                                            "spec-lua-api-behaviour.md Sec11.3/Sec11.4"};
        OpenValueMap<bool> slotResolves{"pcr morph slot resolves (index-or-category-name scan)",
                                        "spec-lua-api-behaviour.md Sec11.3/Sec11.4"};
        // Keyed by pcrMorphKey(sessionLower, slotKey). CONFIRMED write
        // target: "writes the raw value into the live morph-value array" -
        // this project's own flat stand-in for the real singleton's +0x2088
        // array (0x009da4e0, this document's own front-matter addendum to
        // Sec11). Absent = never written (read side defaults 0.0, matching
        // the real getter's OWN "returns 0.0 if either pointer is null"
        // fallback, 0x009f5c00).
        std::unordered_map<std::string, double> values;
        // Sec11.3's own "mark this character as having a pending morph
        // recompute" dedup queue (0x00831e90, "splice-to-front-with-
        // membership-check, i.e. queue this, no-op if already queued") -
        // collapses losslessly to one idempotent bool per session, since
        // this project does not model a real queue draining mechanism
        // anywhere (nothing in scope ever reads this queue back).
        std::unordered_map<std::string, bool> pendingRecompute;
    };
    PcrMorphState& pcrMorph() { return pcrMorph_; }
    static std::string pcrMorphKey(const std::string& sessionLower, const std::string& slotKey) {
        return sessionLower + "\x1f" + slotKey;
    }

    // pcu_is_streaming_done (Sec11.5, 0x0082ad90): CONFIRMED two
    // independent pending-work checks ANDed together (both must be clear to
    // report "done") - which real asset class each one tracks is OPEN
    // (Sec11.5's own text), so both stay OpenValue, refusing honestly until
    // a test sets them, rather than guessing a domain split.
    struct PcuStreaming {
        OpenValue<bool> rowCounterPending{"pcu per-row streaming counter/flag pending (0x009f0aa0 row check)",
                                          "spec-lua-api-behaviour.md Sec11.5"};
        OpenValue<bool> listMembershipPending{"pcu circular-list membership pending (0x009f07d0)",
                                              "spec-lua-api-behaviour.md Sec11.5"};
    };
    PcuStreaming& pcuStreaming() { return pcuStreaming_; }

    // horde_mode_is_active (Sec11.6, 0x00842ad0): CONFIRMED a single global
    // byte, pushed directly - writer not traced (Sec11.6's own text), OPEN
    // until a test sets it.
    OpenValue<bool>& hordeModeActive() { return hordeModeActive_; }

    // game_audio_stop (Sec11.7, 0x00843ac0): CONFIRMED the ENTIRE function
    // has no Lua-visible effect this project can model - the real worker
    // only mutates a throwaway local copy of the id argument and two
    // internal status bits of a secondary/legacy audio-instance mechanism
    // this project builds no registry for anywhere (same "no in-scope
    // consumer" class as several audio natives elsewhere in this project).
    // Call-count only, for test observability.
    uint64_t gameAudioStopCount() const { return gameAudioStopCount_; }
    void recordGameAudioStop() { ++gameAudioStopCount_; }

    // store_set_camera_pos (Sec11.10, 0x00830670): CONFIRMED resolve via a
    // hash+linear-scan against a fixed 60-byte-stride preset table whose
    // real row CONTENTS this spec text does not supply (same never-
    // populated-registry convention as PcrMorphState above) - test-
    // populated presence only; the deep camera-math/field-layout internals
    // (3-way flag gate, per-row distance/pitch/yaw fields) are OPEN and not
    // reproduced (this project has no camera/world-position system for the
    // store/character-preview UI anywhere in scope).
    struct StoreCameraPreset {
        OpenValueMap<bool> presetResolves{"store camera preset name table scan (0x00830670 hash+linear-scan)",
                                          "spec-lua-api-behaviour.md Sec11.10"};
        std::string lastAppliedPreset; // "" = none applied yet
        uint64_t appliedCount = 0;
    };
    StoreCameraPreset& storeCameraPreset() { return storeCameraPreset_; }

    // pause_menu_accept_options (Sec11.11, 0x007e0430): CONFIRMED a 3-way
    // AND gate (one thunked predicate, OPEN target, ANDed with two flag
    // globals) selects whether pending options changes commit. Every link
    // of that AND is individually OPEN with nothing this project can
    // default honestly - collapsed to ONE OpenValue<bool> representing the
    // gate's own combined outcome (same "collapse a multi-condition AND
    // chain whose sub-conditions are all individually OPEN into one gate
    // field" convention as VcustCamera::targetValid above), refusing until
    // a test sets it rather than guessing any individual link.
    struct PauseMenuOptions {
        OpenValue<bool> acceptGatePasses{"pause-menu options accept 3-way gate (0x007060e0)",
                                         "spec-lua-api-behaviour.md Sec11.11"};
        bool committed = false; // CONFIRMED effect once the gate passes
        uint64_t acceptCalls = 0;
    };
    PauseMenuOptions& pauseMenuOptions() { return pauseMenuOptions_; }

    // vint_options_remap_set_key_binding (Sec11.12, 0x007cd3d0): CONFIRMED
    // structural pre-check - a key code of the sentinel -1 is a clean no-op
    // (no gate even consulted); otherwise a gate (0x007c2060(0), HIGH
    // CONFIDENCE "is a remap confirmation dialog already open," not
    // independently traced to a real target) must be FALSE for the
    // function to stage a pending (action, key, flag) triple and raise a
    // confirmation dialog. OPEN until a test sets the gate.
    struct VintRemapStaging {
        OpenValue<bool> dialogAlreadyOpen{"remap confirmation dialog already open (0x007c2060(0), HIGH CONFIDENCE reading)",
                                          "spec-lua-api-behaviour.md Sec11.12"};
        bool hasPending = false;
        int32_t pendingActionIndex0Based = 0;
        int32_t pendingKeyCode = 0;
        bool pendingFlag = false;
        uint64_t dialogRaisedCount = 0;
    };
    VintRemapStaging& vintRemapStaging() { return vintRemapStaging_; }

    // hud_critical_timer_stop (Sec12.1, 0x00a4ed00): CONFIRMED a genuine
    // zero-argument command, single fixed widget - the internal helper
    // (0x006d0dc0) and the stale-handle-clear tag (0xc) are both OPEN past
    // their own call structure (this project builds no HUD timer/bar slot-
    // array infrastructure yet, same deferred-infra note as this batch's
    // own task brief). Call-count only.
    uint64_t hudCriticalTimerStopCount() const { return hudCriticalTimerStopCount_; }
    void recordHudCriticalTimerStop() { ++hudCriticalTimerStopCount_; }

    // audio_play_for_mission_cellphone (Sec12.2, 0x00a3daa0): CONFIRMED
    // registers up to two named Lua callbacks (args 4/5) into their own two
    // hook slots on a per-cellphone-audio-record basis, reusing the SAME
    // callback-name->hook-reference primitive (0x00a1fcc0) the character
    // event-hook family uses elsewhere in this project, but on a NEW record
    // kind this project has no other state for. The record-allocation
    // internals (0x00a38f30) and the exact real-world role of each hook
    // slot are OPEN - only the two named-callback writes are tracked, keyed
    // by the cellphone-audio identifier string (arg 1).
    struct MissionCellphoneAudio {
        std::string hookSlot1; // arg 4, "" = none registered
        std::string hookSlot2; // arg 5, "" = none registered
    };
    MissionCellphoneAudio& missionCellphoneAudio(const std::string& id) { return missionCellphoneAudio_[id]; }

    // inv_weapon_disable_all_but_this_slot/inv_weapon_disable_slot
    // (Sec12.8/Sec12.16, 0x00a51270/0x00a51360): CONFIRMED identical clamp-
    // switch (slot 0-7 pass through, anything else -> sentinel 0xff, whose
    // own real effect is OPEN - "not traced") feeding two DIFFERENT terminal
    // dispatches (0x007f4650/0x007f4460, both OPEN) - no per-character/
    // per-player name argument at all (implicitly the local player, this
    // project's single-player-only scope). CONFIRMED side effects per each
    // function's own text: "_all_but_this_slot" disables every OTHER slot
    // and enables the given one; "_slot" disables EXACTLY the given slot,
    // leaving the rest untouched. Index 8 is this project's own bucket for
    // the sentinel 0xff (out-of-range) case - tracked only as a counter
    // (real effect OPEN, not guessed).
    struct WeaponSlotState {
        std::array<bool, 8> disabled{};
        uint64_t sentinelOobCount = 0; // an out-of-range slot index, real effect OPEN - see doc comment
    };
    WeaponSlotState& weaponSlots() { return weaponSlots_; }

    // get_char_vehicle_name (Sec12.9, 0x00a4b220): reuses
    // CharacterState::currentVehicleName directly (+0x16c0/+0x16c4, the SAME
    // field radio_block below also reads) - no new EngineState-level state
    // needed. See stub_get_char_vehicle_name's own doc comment for the
    // CHOSEN "trust a known association, skip the further OPEN liveness/
    // class-bit gates" simplification.

    // effect_play_on_script_object/effect_play_on_human (Sec12.10/Sec12.12,
    // 0x00a491b0/0x00a48f80): CONFIRMED to share effect_play's (Sec9.1,
    // already implemented) own fixed effect-name registry singleton, index/
    // instance-allocation chain and finalizer, differing only in which
    // resolved object's position fields the instance attaches to - reuses
    // the SAME -1.0 failure sentinel (kActionPlaySentinelResolveFail) and
    // nextVisualEffectHandle() success-handle stand-in effect_play already
    // established, rather than inventing a second, parallel convention. No
    // new EngineState-level state needed.

    // helicopter_fly_to_do (Sec12.11, 0x00a4ff20): CONFIRMED to share the
    // qualifying-vehicle gate (0x00ad31a0, "true only for internal state
    // values 3/4") this project's EXISTING HelicopterFlyTo struct above
    // (Sec28.2) already models as `qualifies` - reused directly rather than
    // re-derived, per this batch's own cross-batch coordination note (the
    // helicopter-AI family spans this batch and Batch 4). `lastRequest`
    // (also pre-existing) is likewise reused to record the resolved
    // destination name and the orientation-mode boolean (arg 8) this
    // native's own CONFIRMED argument shape supplies. No new EngineState-
    // level state needed - see HelicopterFlyTo's own doc comment above.

    // on_vehicle_exit (Sec12.15, 0x00a58a40): CONFIRMED a real, three-tier
    // fallback resolver chain (generic/#PLAYER#, then plain character, then
    // plain vehicle) posting one of three different event codes
    // (0x17/0x18/6) - this project's flat objectResolves() map (Sec29's
    // "one shared global name map" finding) cannot distinguish WHICH tier
    // matched, so which event code would have fired is OPEN; only that the
    // native fired, for which (occupant, vehicle-or-seat) pair, is recorded
    // - a stated simplification, not an invented event code.
    struct OnVehicleExitEvent {
        std::string occupant;
        std::string vehicleOrSeat;
    };
    std::vector<OnVehicleExitEvent>& onVehicleExitEvents() { return onVehicleExitEvents_; }

    // guardian_angel_increment_node_progress (Sec12.17, 0x00a4abb0):
    // CONFIRMED a genuine zero-argument command, single unconditional call
    // (0x00626b10, OPEN - "which specific 'current' guardian-angel context
    // it advances" is not traced). Call-count only.
    uint64_t guardianAngelNodeProgressCount() const { return guardianAngelNodeProgressCount_; }
    void recordGuardianAngelNodeProgress() { ++guardianAngelNodeProgressCount_; }

    // radio_block (Sec12.20, 0x00a5c290): CONFIRMED two-tier resolve - tried
    // first as a vehicle directly, then (on failure) as a character,
    // following their cached vehicle association (CharacterState::
    // currentVehicleName, the SAME field get_char_vehicle_name/Sec12.9
    // reads - "a confirmed reuse of the character's cached-vehicle-
    // association fields between two of this tranche's own functions").
    // This project's flat objectResolves() map cannot structurally
    // distinguish "is this name a vehicle" from "is this name a character"
    // the way the real two-tier resolver does - see
    // stub_radio_block's own doc comment for the CHOSEN tie-break (an
    // already-materialized VehicleState record, or no CharacterState record
    // at all, is read as "tier 1: a vehicle name directly"). The terminal
    // dispatch (0x00556680) is OPEN - only which vehicle got blocked is
    // tracked.
    std::unordered_set<std::string>& radioBlockedVehicles() { return radioBlockedVehicles_; }
    void recordRadioBlock(const std::string& vehicleName) { radioBlockedVehicles_.insert(vehicleName); }
    bool radioBlocked(const std::string& vehicleName) const { return radioBlockedVehicles_.count(vehicleName) != 0; }

    // vehicle_set_invulnerable (Sec12.21, 0x00a62f70): RESOLVED, no new
    // state needed - CONFIRMED (Sec46.1) to reach the exact same setter
    // (0x00a7a830) turn_invulnerable's own vehicle branch already uses,
    // merging the boolean into VehicleState::forceFlags1d7a's existing
    // kInvulnerableBit1d7a bit directly.

    // camera_restrict_target_object (Sec12.23, 0x00a45860): CONFIRMED a
    // genuinely subtle two-part gate (arg 3, "except for this viewer,"
    // selects which of {local-apply, network-replicate} happens) plus a
    // two-resolver target lookup whose own vtable-method internals are
    // OPEN. This project represents "the local player" the SAME way every
    // other function in scope already does - the reserved "#PLAYER#"
    // sentinel string (this document's own Sec3 front matter) - rather than
    // inventing a separate identity check; see stub_camera_restrict_
    // target_object's own doc comment for the full CHOSEN gate reasoning.
    // The deep position/radius dispatch selection (0x0057a950/0x0057ab80/
    // 0x0057aca0) is OPEN and not reproduced (no camera/world-position
    // system in scope).
    struct CameraRestrictTarget {
        bool hasTarget = false;
        std::string targetName;
        double radius = 0.0;
        bool localApplied = false;
        bool replicated = false;
        uint64_t callCount = 0;
    };
    CameraRestrictTarget& cameraRestrictTarget() { return cameraRestrictTarget_; }

    // waypoint_add (Sec12.24, 0x00a68cf0): CONFIRMED fixed -1.0 failure
    // sentinel (the SAME kActionPlaySentinelResolveFail effect_play/the
    // effect_play_on_* family use), overwritten with a plain 0.0 on "at
    // least one success path" - the 3-tier-plus-fallback resolver priority
    // chain collapses to this project's one shared objectResolves() gate
    // (Sec29 convention), and the real numeric "waypoint id" question
    // (Sec12.24's own OPEN item) is not guessed at - this project only ever
    // produces the two CONFIRMED sentinel values. No new EngineState-level
    // state needed beyond kActionPlaySentinelResolveFail (already shared
    // with effect_play).

    // get_current_hit_points (Sec12.25, 0x00a4bb00): the generic/follower/
    // character-fallback-to-mount paths all CONFIRMED read the SAME +0x1cb8
    // field CharacterState::currentHitPoints already models - reused
    // directly, no new state needed there. The vehicle path's own distinct
    // source is VehicleState::hitPointsForQuery above; the unidentified
    // 4th-kind placed-object path is OPEN and not modeled (a stated gap,
    // consistent with this project's existing placed-object boundary).

    // =========================================================================
    // Wave 2, Batch W2-3 (2026-10-08), spec-lua-api-behaviour.md Sec20+Sec21 -
    // EngineState-level (not per-character/vehicle/placed-object) state. See
    // lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp for full per-native
    // citations.
    // =========================================================================

    // dlc_achievement_counter_get (Sec20.25): reads the SAME 3-slot array
    // dlc_achievement_counter_set (Sec15.27) already writes
    // (dlcAchievementCounters_). CONFIRMED: "1 number if index < 3; else
    // pushes NOTHING (0 return values)" - this includes a NEGATIVE index
    // (also "< 3"), which the REAL getter reads UNCHECKED - a genuine
    // memory-safety OOB READ, distinct from the setter's own already-
    // hardened OOB WRITE (setDlcAchievementCounter above). HARDENING POLICY
    // (CHOSEN hardened divergence): index in [0,3) reproduces the real read
    // exactly; index < 0 is refused (no safe way to know what address the
    // real unchecked read would have touched) rather than fabricated, and
    // counted via a NAMED per-site counter, separate from the setter's own
    // (a different real bug, a different real call site); index >= 3 is the
    // real, CONFIRMED "push nothing" case, not a divergence.
    bool dlcAchievementCounterGet(int64_t index, double& outValue) {
        if (index < 0) { ++dlcAchievementCounterGetNegativeIndexGuardCount_; return false; } // CHOSEN (hardened divergence), Sec20.25
        if (index >= kDlcAchievementCounterSlotCount) return false; // CONFIRMED real "push nothing" case
        outValue = static_cast<double>(dlcAchievementCounterValue(index));
        return true;
    }
    uint64_t dlcAchievementCounterGetNegativeIndexGuardCount() const { return dlcAchievementCounterGetNegativeIndexGuardCount_; }

    // player_grenades_disable (Sec21.17): the exact enable/disable pair for
    // the NOT-in-this-batch `player_grenades_enable` (Sec18.25, out of
    // scope) over the SAME two real globals (0x0140e6e7/0x0140e6f4). CHOSEN
    // false defaults (no in-scope writer touches them before the first
    // disable call either way).
    bool& grenadesDisableFlagA() { return grenadesDisableFlagA_; } // 0x0140e6e7, always set true by disable()
    bool& grenadesDisableFlagB() { return grenadesDisableFlagB_; } // 0x0140e6f4, set true only when disable(true)

    // tutorial_vehicle_controls_enabled (Sec20.4): a bare global boolean, no
    // resolver, no gate, no record - CHOSEN false default (no spec default
    // given; matches this project's usual "never touched" starting point).
    bool& tutorialVehicleControlsEnabled() { return tutorialVehicleControlsEnabled_; }

    // spawning_allow_cop_ambient_spawns (Sec21.14): a bare global boolean.
    // CHOSEN true default (the nil-gated Lua argument's own CONFIRMED
    // default is true; absent any other in-scope writer, starting the
    // global at that same "allowed" value is the more faithful choice than
    // an arbitrary false).
    bool& copAmbientSpawnsAllowed() { return copAmbientSpawnsAllowed_; }

    // persona_suppress_ambient_lines (Sec20.11): a reference-counted
    // "suppress" counter (floor-at-zero decrement, NOT a plain toggle) -
    // CONFIRMED shape; the counter itself is test-observable, its real
    // consumer is OPEN (not modeled).
    void personaSuppressAmbientLinesAdjust(bool suppress) {
        if (suppress) ++personaSuppressAmbientLinesCounter_;
        else if (personaSuppressAmbientLinesCounter_ > 0) --personaSuppressAmbientLinesCounter_;
    }
    int64_t personaSuppressAmbientLinesCounter() const { return personaSuppressAmbientLinesCounter_; }

    // get_object_pos (Sec20.24): the trailing 3 "placeholder" values ARE the
    // SAME fixed `.rdata` constant (0x0112a1e0) on EVERY call, success or
    // failure (CONFIRMED - "never anything object-derived"). No pass of
    // this spec ever independently reads this address's own real bit
    // pattern (unlike 0x01117a4c, later confirmed 1.0 - Sec24.9's own
    // "byte-confirmed" pass) - genuinely OPEN, not merely uncited. Refusing
    // this one honestly (rather than reusing an unrelated already-resolved
    // constant, which would be a silent guess) means get_object_pos itself
    // is OPEN until a test supplies this value - the correct, honest
    // outcome per this project's own discipline.
    OpenValue<double> objectPosTrailingConstant{"get_object_pos's own fixed trailing placeholder constant (0x0112a1e0)",
                                                "spec-lua-api-behaviour.md Sec20.24"};

    // get_team (Sec20.23): the real 4-stage resolver cascade (each stage
    // reading a DIFFERENT team-id-byte offset off a DIFFERENT kind of
    // handle) collapses to this project's one shared objectResolves() map
    // (Sec29 convention) - nothing in this project's own scope ever WRITES
    // a per-object team id (vehicle_set_team, Sec18.20, is not itself
    // implemented in this project yet), so this is a pure, write-less query
    // surface: OPEN until a test sets a team name directly.
    OpenValueMap<std::string>& teamNameByObject() { return teamNameByObject_; }

    // mission_set_enemy_team (Sec20.13): a bare global, written only while
    // missionContext().activeAndRunning is true (the SAME shared gate
    // change_mission_enemy_team, Sec21.27, also reads). CHOSEN sentinel -1
    // ("never set").
    int32_t missionEnemyTeamId = -1;

    // change_mission_enemy_team (Sec21.27): a DIFFERENT field from
    // mission_set_enemy_team's own missionEnemyTeamId above (CONFIRMED
    // distinct offsets, +0xa0 one-shot gate vs. +0xa5 team-code byte,
    // Sec21.27's own text: "the exact relationship between the two fields
    // was not resolved this pass, flagged OPEN"). CONFIRMED polarity: the
    // write proceeds only when the real +0xa0 field reads 0 - modeled here
    // as `false` ("not yet blocked/fired"); `true` means the real field
    // read non-zero, blocking the write. The one-shot gate itself is OPEN
    // until a test sets it.
    OpenValue<bool> missionOneShotGateA0{"mission context +0xa0 one-shot gate (change_mission_enemy_team)",
                                         "spec-lua-api-behaviour.md Sec21.27"};
    int32_t missionEnemyTeamCodeA5 = -1; // CHOSEN sentinel "never set"
    uint64_t changeMissionEnemyTeamNotifyCount = 0;

    // set_force_combat_ready_team (Sec21.16): a bare global, reusing the
    // SAME name->id lookup resolveTeamId() already models (0x0094cc60,
    // Sec30.6/Sec20.6) - CONFIRMED sentinel 9 default (matches
    // resolveTeamId()'s own not-found default, the same "team-wide, no
    // gate, no resolve" global this native's own text describes).
    int32_t forcedCombatReadyTeamId = 9;

    // set_script_animation_group (Sec21.15): the group-NAME to id table
    // (0x004cce70) was never diffed against any other already-documented
    // table (OPEN, per the spec's own text) - a dedicated, test-settable
    // map, distinct from resolveTeamId()/lookupNotorietyTrackCode()'s own
    // tables.
    std::unordered_map<std::string, int32_t> animationGroupIdByName;

    // inv_item_remove_in_slot (Sec21.4) / inv_item_equip_slot (Sec21.23):
    // the real slot-name->id conversion (0x00a25f80) first tries a general
    // slot-name table (0x008dc6b0, no CONFIRMED concrete entries given -
    // test-settable via slotNameToId_ below), falling back to FIVE literal,
    // case-insensitive special names read directly from `.rdata`
    // (CONFIRMED, Sec21.23's own text): "best"->0xd, "best-firearm"->0xe,
    // "best-melee"->0xf, "best-non-lethal"->0x10, "best-non-explosive"->
    // 0x11; anything else -> 0xff ("not found"). Shared by BOTH natives
    // (CONFIRMED, Sec21.4's own text: "the SAME 0x00a25f80 lookup item 21.4
    // uses").
    std::unordered_map<std::string, int32_t> slotNameToId_;
    int32_t resolveInventorySlotId(const std::string& slotName) const {
        auto it = slotNameToId_.find(slotName);
        if (it != slotNameToId_.end()) return it->second;
        std::string lower = slotName;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return std::tolower(ch); });
        if (lower == "best") return 0xd;
        if (lower == "best-firearm") return 0xe;
        if (lower == "best-melee") return 0xf;
        if (lower == "best-non-lethal") return 0x10;
        if (lower == "best-non-explosive") return 0x11;
        return 0xff; // CONFIRMED "not found" sentinel
    }
    void registerInventorySlotIdForTesting(const std::string& slotName, int32_t id) { slotNameToId_[slotName] = id; }

    // inv_item_equip_slot (Sec21.23): the dispatcher's own ~12 special-
    // selector case bodies (0x005fec20/0x005ff220/.../0x004f0400) are OPEN
    // (none independently decompiled, consistent with this document's own
    // practice for a large shared routine outside a task's specific scope)
    // - a dedicated, test-settable map from selector code (as a string key)
    // to "the item this selector would resolve to," OPEN until a test sets
    // one.
    OpenValueMap<std::string>& bestWeaponSelectionByCode() { return bestWeaponSelectionByCode_; }

    // ingame_effect_add_trigger (Sec21.5): effect-name -> numeric id
    // (0x005c50b0, "hashes/interns the name... returns -1 on failure" -
    // CONFIRMED -1 sentinel, a broader named-resource lookup not
    // independently given concrete entries). A plain map (not OpenValueMap)
    // since "-1, not found" is itself the real, CONFIRMED default - not an
    // unknown engine value.
    std::unordered_map<std::string, int32_t>& effectIdByName() { return effectIdByName_; }

    // trigger_get_radius / ingame_effect_add_trigger / notoriety_set_
    // override_timer / hud_push_screen (Sec21.3/21.5/21.6/21.13): host-only
    // broadcasts reuse the EXISTING coopLocalIsHost() gate above (Sec8.27) -
    // no new gate field needed.

    // notoriety_set_override_timer (Sec21.3): resolved via the SAME 24-
    // entry track table lookupNotorietyTrackCode() already models (Sec3.6,
    // HIGH CONFIDENCE per Sec21.3's own cross-reference to Sec6.2's
    // identical-shaped group table). Per-track override state, keyed by
    // track name (this project's own name-keying convention, standing in
    // for the real index-keyed table).
    struct NotorietyOverrideTimer {
        bool enabled = false;
        double minValue = 0.0;
        double maxValue = 0.0;
    };
    std::unordered_map<std::string, NotorietyOverrideTimer>& notorietyOverrideTimers() { return notorietyOverrideTimers_; }

    // hud_push_screen (Sec21.6): a DIFFERENT, dedicated HUD-screen registry
    // (0x012fced8) from the 0x02442750 multi-kind singleton objectResolves()
    // stands in for elsewhere - kept as its OWN map, not folded into
    // objectResolves(), matching this native's own explicit "a DIFFERENT
    // singleton" text. hudScreenTypeField is the screen's own +0x30 field
    // (role: "Planet_Saints" special-case trigger value 0x29, AND the
    // mask-bit-1 broadcast payload) - OPEN until a test sets it (no spec
    // default per screen).
    OpenValueMap<bool>& hudScreenResolves() { return hudScreenResolves_; }
    OpenValueMap<int32_t>& hudScreenTypeField() { return hudScreenTypeField_; }
    std::vector<std::string>& hudScreenStack() { return hudScreenStack_; }
    uint64_t hudPushScreenPlanetSaintsCount = 0;
    uint64_t hudPushScreenBroadcastCount = 0;

    // continuous_explosion_avoid_object_add (Sec21.7): the real fixed 8-slot
    // global position-pair array, gated on a global "active" flag byte and
    // a hard cap of 8 (CONFIRMED). This project has no real position data
    // for the resolved record's own linked-object chain (see
    // PlacedObjectState's own doc comment) - tracks resolved NAMES instead
    // of positions, same cap, same "skip entirely once full" shape.
    static constexpr size_t kContinuousExplosionAvoidObjectCap = 8;
    std::vector<std::string>& continuousExplosionAvoidObjects() { return continuousExplosionAvoidObjects_; }

    // human_skydive_create_npc_backpack (Sec20.20): single-slot global
    // tracker (0x0262d3b0) - "" = no character currently tracked.
    std::string currentTrackedBackpackCharacterName;
    uint64_t backpackCreatedCount = 0;
    uint64_t backpackPriorReleasedCount = 0;
    uint64_t backpackSecondaryVisualCreatedCount = 0;

    // character_ignite (Sec20.29): the fixed 12-simultaneous-fires tracking
    // array (CONFIRMED hard cap, previously-uncited) - this project tracks
    // the SET of currently-on-fire character names directly (order/slot
    // reuse is not independently observable without a real timer system),
    // same cap.
    static constexpr size_t kCharactersOnFireCap = 12;
    std::unordered_set<std::string>& charactersOnFire() { return charactersOnFire_; }

    // interior_disable_exterior (Sec20.19): the SAME global replication-mode
    // flag action_stop_synced_state (Sec18.4, out of this batch's scope)
    // already establishes (0x024d4461) - OPEN until a test sets it (no
    // in-scope writer here either).
    OpenValue<bool> replicationModeFlag0x024d4461{"global replication-mode flag (0x024d4461)",
                                                  "spec-lua-api-behaviour.md Sec18.4/Sec20.19"};

    // npc_use_closest_action_node_of_type (Sec21.2): the type-NAME to id
    // conversion (0x008c8550) plus its own validator (0x008c7ba0) have no
    // CONFIRMED concrete table entries anywhere in scope - a dedicated,
    // test-settable validity map, OPEN until a test registers a type name.
    OpenValueMap<bool>& actionNodeTypeNameValid() { return actionNodeTypeNameValid_; }

    // reticle_override_set (Sec20.8): the two fixed 16-byte buffers
    // (0x022b7030/0x022b7020) and the "override active"/duration globals
    // are GLOBAL singletons, not per-object state (CONFIRMED - no object
    // resolve of any kind happens in this native's own body at all).
    // CONFIRMED truncation to a 16-byte (15 char + NUL) buffer via the
    // real "fixed-size copy helper" 0x00da78d0 - a safe, bounded copy is
    // the real CONFIRMED behavior here, not a hardening divergence.
    struct ReticleOverride {
        bool active = false;
        std::string str1;
        std::string str2;
        double duration = 0.0;
    };
    ReticleOverride& reticleOverride() { return reticleOverride_; }
    static constexpr size_t kReticleOverrideBufferCap = 15; // 16-byte buffer, 15 chars + NUL

    // skydive_set_catching_shaundi (Sec20.7): fixed-name entities (no Lua
    // name argument at all) - a single EngineState-level record, matching
    // the real function's own fixed globals (0x0130a8e4 "active" sentinel,
    // 0x0262d3b8 "catching" sub-mode, 0x0130a8e8 cached conversation id).
    struct SkydiveCatchingShaundi {
        int32_t activeSentinel = -1; // -1 = inactive (CONFIRMED sentinel); 0 = active
        bool catchingSubMode = false;
        int32_t cachedConversationId = -1; // -1 = none
        uint64_t acquireCount = 0;
        uint64_t releaseCount = 0;
        uint64_t conversationStopCount = 0;
        uint64_t networkRecordCount = 0; // unconditional every call (hardcoded 3rd arg false, CONFIRMED)
    };
    SkydiveCatchingShaundi& skydiveCatchingShaundi() { return skydiveCatchingShaundi_; }

    // drug_effect_clear_override_values (Sec21.25): reuses drugEffectOverrideValueA/B()
    // (Sec14.19's own channel-0 fields) directly - the sibling clear/set
    // pair for the identical real channel-0 slot. No new state needed
    // beyond those two already-existing fields.

    // team_make_allies (Sec20.6): reuses setTeamRelationIfInBounds()/
    // recordTeamRelationOobGuard() directly (that function's own doc
    // comment already anticipated this: "team_make_allies ... and
    // team_make_unfriendly share one correct implementation if
    // team_make_allies is ever added") - relation value 4, no new state.
    // Per spec-lua-api-behaviour.md Sec30.6 ("the two 'auxiliary arrays'
    // that note names are the SAME relation matrix at rows/column 6"),
    // team 5's own mirroring onto team 6 is NOT a separate, unmodeled
    // mechanism - it IS setTeamRelationIfInBounds()'s own existing team-5-
    // mirrors-team-6 refinement (established by team_make_unfriendly/
    // Sec30.6), already exercised by this native through the shared call.
    // (Post-merge correction: this comment previously claimed a distinct,
    // unmodeled "team 5 two-auxiliary-array" gap here - a fresh reviewer
    // found that claim false; the address arithmetic for row 6
    // (0x0262d940 + 6*8 = 0x0262d970) confirms it is the same mechanism.)

    // =========================================================================
    // Wave 2, Batch W2-4 (Sec22+Sec24+Sec25 gameplay residue, 78 names,
    // 2026-10-08) - the EngineState-level (not per-character/per-vehicle) new
    // state this batch needs. See src/lua_spec_confirmed_stubs_b22_wave2_sec22.
    // cpp and src/lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp's own per-
    // stub doc comments for the full citation/reasoning behind each field.
    // =========================================================================

    // spotlight_heli_laser_enable (Sec22.6): a single global, no resolver, no
    // gate, no record - CHOSEN default false (no CONFIRMED construction value).
    bool& spotlightHeliLaserEnabled() { return spotlightHeliLaserEnabled_; }

    // roadblocks_ambient_traffic_enable (Sec22.7): CONFIRMED only written
    // "when a live session exists AND this machine is host" - reuses
    // coopLocalIsHost() directly (CONFIRMED false in single player project-
    // wide), so this is a real, confirmed no-op in single player; the field
    // exists for structural/test coverage (a test can flip coopLocalIsHost()'s
    // own backing state to exercise the write branch).
    bool& roadblocksAmbientTrafficEnabled() { return roadblocksAmbientTrafficEnabled_; }
    uint64_t& roadblocksAmbientTrafficNoSessionSkipCount() { return roadblocksAmbientTrafficNoSessionSkipCount_; }

    // notoriety_spawn_only_air (Sec22.13): CHOSEN ONE global bool standing in
    // for the real 9-separate-fixed-object write (no generic entity shape
    // invented) - default false (no CONFIRMED construction value).
    bool& notorietySpawnOnlyAir() { return notorietySpawnOnlyAir_; }

    // notoriety_restricted_zones_enable (Sec22.14): global enable flag plus
    // the small fixed 4-entry restricted-zone slot table (0x026ea7f8) this
    // native clears when genuinely disabling - no in-scope setter ever
    // populates a slot (OPEN, no in-scope writer), so the clear path is only
    // ever counted, never observed changing a real value.
    struct NotorietyRestrictedZones {
        bool enabled = true; // Sec22.14's own nil-gated-argument default
        std::array<int32_t, 4> slots{-1, -1, -1, -1};
        uint64_t setCount = 0;
        uint64_t clearedSlotCount = 0;
    };
    NotorietyRestrictedZones& notorietyRestrictedZones() { return notorietyRestrictedZones_; }

    // base_jumping_enable (Sec22.28): local-apply setter that ALWAYS ALSO
    // fires a notification record (unconditional, not host-gated - the
    // FIRST confirmed opcode-0x44 sighting going through the local-only
    // commit sibling, per Sec22.28's own text). Default true (Sec22.28's own
    // nil-gated-argument default).
    bool& baseJumpingEnabled() { return baseJumpingEnabled_; }
    uint64_t& baseJumpingReplicateCount() { return baseJumpingReplicateCount_; }

    // dlc_gis_film_grain_load (Sec22.20): the SAME shared "active" latch
    // global (0x02317598) dlc_gis_film_grain_unload (Sec17.29, a DIFFERENT,
    // not-yet-implemented batch) also targets - modelled here since THIS
    // native is the one in scope; a future implementation of the `_unload`
    // sibling should reuse this SAME field, not a second one.
    bool& dlcGisFilmGrainActive() { return dlcGisFilmGrainActive_; }
    uint64_t& dlcGisFilmGrainLoadLatchSetCount() { return dlcGisFilmGrainLoadLatchSetCount_; }
    uint64_t& dlcGisFilmGrainLoadNotifyCount() { return dlcGisFilmGrainLoadNotifyCount_; }

    // wind_override_set (Sec22.30) / wind_override_clear (Sec24.3): CONFIRMED
    // exact math (byte-confirmed constants) - this project CAN and does
    // reproduce the real normalize algorithm precisely, since it is pure
    // 3-float arithmetic with no world-coordinate dependency. Default (1,0,0)/
    // 0.0 matches the real degenerate-input fallback (Sec22.30's own byte-
    // confirmed X-axis default). `actionStopSyncedStateFlag` is the SAME
    // replication-mode flag (0x024d4461) both natives' own text cites, shared
    // with the not-yet-implemented action_stop_synced_state (Sec18.4) -
    // CHOSEN default false (no CONFIRMED construction value).
    struct WindOverride {
        double x = 1.0, y = 0.0, z = 0.0;
        double magnitude = 0.0;
        uint64_t setCount = 0;
        uint64_t clearCount = 0;
        uint64_t setNotifyCount = 0;
        uint64_t clearNotifyCount = 0;
    };
    WindOverride& windOverride() { return windOverride_; }
    bool& actionStopSyncedStateFlag() { return actionStopSyncedStateFlag_; }
    // CONFIRMED exact algorithm (Sec22.30): magnitude = sqrt(x^2+y^2+y^2); near-
    // zero (< 1e-7) or non-finite -> (1,0,0); else each component /= magnitude.
    static void normalizeWindVector(double x, double y, double z, double& outX, double& outY, double& outZ) {
        double mag = std::sqrt(x * x + y * y + z * z);
        if (!(mag >= 1.0E-7) || !std::isfinite(mag)) {
            outX = 1.0;
            outY = 0.0;
            outZ = 0.0;
            return;
        }
        outX = x / mag;
        outY = y / mag;
        outZ = z / mag;
    }

    // boss_battle_kb_enable_crowd_props (Sec22.27): CHOSEN minimal stand-in
    // for the real 6-named-marker/8-global-struct "crowd prop spawn point"
    // table (not previously catalogued, Sec22.27's own text) - only the
    // CONFIRMED top-level facts (enabled state, density argument) are
    // tracked, since no in-scope reader needs the marker table itself.
    struct BossBattleKbCrowdProps {
        bool enabled = false;
        double density = 1.0; // Sec22.27's own nil-gated-argument default
        uint64_t enableCount = 0;
        uint64_t disableCount = 0;
    };
    BossBattleKbCrowdProps& bossBattleKbCrowdProps() { return bossBattleKbCrowdProps_; }

    // boss_battle_matt_cheats_stop (Sec24.1): CHOSEN minimal stand-in for the
    // real multi-global "boss battle Matt" cheat/state-tracking cluster -
    // only whether the cluster was active (the real sentinel-not-already-
    // -1 gate) and a call count are tracked.
    bool& bossBattleMattCheatsActive() { return bossBattleMattCheatsActive_; }
    uint64_t& bossBattleMattCheatsStopCount() { return bossBattleMattCheatsStopCount_; }

    // trigger_type_enable (Sec24.13): CHOSEN simplification - a trigger TYPE
    // name is used directly as the map key rather than this project
    // reproducing the real hash+64-bit-bitmask-by-type-id mechanism (no
    // generic hash table modelled anywhere in this project). Missing/absent
    // = enabled (bit clear), matching the real all-zero-at-start bitmask.
    // Both directions ALSO fire the SAME local-only notification record
    // (identical opcode+sub-tag either way, Sec24.13's own confirmed
    // "identical record for both directions" idiom) - tracked as one shared
    // counter, since the record payload itself never distinguishes direction.
    std::unordered_map<std::string, bool>& triggerTypeDisabled() { return triggerTypeDisabled_; }
    uint64_t& triggerTypeEnableNotifyCount() { return triggerTypeEnableNotifyCount_; }

    // trigger_set_hold_to_repeat (Sec24.14): resolved trigger's own +0x7b bit
    // 0x10 - CHOSEN per-name map (missing = false, matching a zero-filled
    // byte's own default bit state).
    std::unordered_map<std::string, bool>& triggerHoldToRepeat() { return triggerHoldToRepeat_; }

    // skydive_set_elev_angles_deg (Sec24.15): CONFIRMED exact degrees-to-
    // radians conversion (byte-confirmed constant 0x012a30d8 = pi/180).
    double& skydiveElevAngleA() { return skydiveElevAngleA_; }
    double& skydiveElevAngleB() { return skydiveElevAngleB_; }

    // radio_newsbreak (Sec24.17): CHOSEN - the real hash/lookup helper
    // (0x00462960) is OPEN, so the raw newsbreak name itself is recorded,
    // honestly, standing in for "whatever resolved id results."
    std::string& currentRadioNewsbreak() { return currentRadioNewsbreak_; }

    // npc_drop_grenades (Sec24.28): a bare global boolean, no resolver, no
    // record (CONFIRMED, Sec24.28's own text) - CHOSEN default false.
    bool& npcDropGrenades() { return npcDropGrenades_; }

    // tutorial_unlock (Sec24.11): host-gated (coopLocalIsHost(), CONFIRMED
    // false in single player); 0x007161a0's own internal write is OPEN (not
    // independently decompiled) - this project does NOT invent which state
    // code it writes into EngineState::tutorialState() above, only that a
    // resolved call reached the setter is counted, per index, honestly (same
    // convention as tutorialAdvanceCount/recordTutorialAdvance above).
    int tutorialUnlockCount(int index) const {
        auto it = tutorialUnlockCounts_.find(index);
        return it != tutorialUnlockCounts_.end() ? it->second : 0;
    }
    void recordTutorialUnlock(int index) { ++tutorialUnlockCounts_[index]; }

    // message_remove_all (Sec25.5): genuinely zero-real-argument (Sec25.5's
    // own text) - no real message queue modelled anywhere in this project,
    // so only a call count stands in for "cleared every pending message."
    uint64_t& messageRemoveAllCount() { return messageRemoveAllCount_; }

    // mission_set_next_mission (Sec25.4) - CORRECTED by Sec60.3: in single
    // player this native takes its own no-session path and changes nothing
    // at all; the previously-cited gate constant (0x011239d0) is a true
    // compile-time constant, not a live "mission system active" flag. This
    // project therefore implements it as a genuine, confirmed no-op - no
    // EngineState value is ever written - with only a call count for test
    // observability (same "genuine no-op, call count only" convention as
    // bink_play, Sec24.2).
    uint64_t& missionSetNextMissionNoopCount() { return missionSetNextMissionNoopCount_; }

    // mission_store_purchased (Sec25.3, f752324 answer): a pure QUERY; the
    // boolean arg is a KIND selector (false=store, true=property), not a
    // value. Holds the sticky purchased flag of the first mission-tagged
    // record of each kind; nullopt = no such record loaded (query answers
    // true). Nothing in the host loads shop records yet, so nullopt default.
    std::optional<bool>& missionStoreRecordPurchased(bool isProperty) {
        return isProperty ? missionPropertyPurchased_ : missionStorePurchased_;
    }

    // hud_inventory_disable (Sec25.12): CONFIRMED "always both" category -
    // unconditionally local-applies AND fires a record, with the host-check
    // result forwarded as data (not a skip gate). Default false (no
    // CONFIRMED construction value).
    bool& hudInventoryDisabled() { return hudInventoryDisabled_; }
    uint64_t& hudInventoryDisableReplicateCount() { return hudInventoryDisableReplicateCount_; }
    uint64_t& hudInventoryDisableSecondaryCallCount() { return hudInventoryDisableSecondaryCallCount_; } // 0x007f01b0, role OPEN

    // hud_critical_timer_get_remainder (Sec25.13): a DIFFERENT global
    // ("critical" HUD countdown timer, 0x006cf080) from HudCriticalTimerState
    // above (hud_critical_timer_set's own, unrelated timer record) - CHOSEN
    // default 0.0 ("no time remaining"); 0x006cf080's own internal state
    // source is OPEN per Sec25.13's own text.
    double& hudCriticalTimerRemainder() { return hudCriticalTimerRemainder_; }

    // ingame_effect_remove_trigger (Sec22.17): per-trigger cached "current
    // effect index" (-1 = no effect, CONFIRMED sentinel reused from
    // vehicle_pathfind_check_done's own -1/"no active request" family,
    // Sec9.10/Sec22.3) - a plain per-name map (missing = -1, same default).
    int32_t ingameEffectIndex(const std::string& triggerName) const {
        auto it = ingameEffectIndices_.find(triggerName);
        return it != ingameEffectIndices_.end() ? it->second : -1;
    }
    void setIngameEffectIndex(const std::string& triggerName, int32_t v) { ingameEffectIndices_[triggerName] = v; }
    uint64_t& ingameEffectRemoveNotifyCount() { return ingameEffectRemoveNotifyCount_; }
    uint64_t& ingameEffectRemoveClearedCount() { return ingameEffectRemoveClearedCount_; }

    // continuous_spawn_set_orders (Sec22.23): resolves its group name via
    // the SAME "guardian angel"/script-group table group_destroy_do (Sec25.16)
    // and group_create_check_done (Sec22.18, a DIFFERENT batch) already share
    // (guardianAngelGroupKnown/guardianAngelGroupRegistered above) - reused
    // directly rather than a second, duplicate group table. The order-type
    // name is matched against the CONFIRMED 4-entry literal table ("attack",
    // "defend", "moveflee", "idle" - byte-confirmed, Sec22.23's own text);
    // anything else is index 4 (the real sentinel, not a 5th slot). The
    // anchor position is NOT modelled (no coordinate system) - only whether
    // an anchor name resolved is recorded, honestly.
    struct ContinuousSpawnOrder {
        int32_t orderTypeIndex = 4; // 4 = sentinel/no-match, matching the real table's own out-of-range behaviour
        std::string anchorName;      // "" = world-origin default (anchor absent or unresolved)
        double radius = 0.0;
        uint64_t setCount = 0;
    };
    ContinuousSpawnOrder& getOrCreateContinuousSpawnOrder(const std::string& groupName) {
        return continuousSpawnOrders_[groupName];
    }
    static int32_t continuousSpawnOrderTypeIndex(const std::string& orderTypeName) {
        if (orderTypeName == "attack") return 0;
        if (orderTypeName == "defend") return 1;
        if (orderTypeName == "moveflee") return 2;
        if (orderTypeName == "idle") return 3;
        return 4;
    }

    // is_rappelling (Sec22.16): HARDENING POLICY counter for the resolve-
    // failure path (see src/lua_spec_confirmed_stubs_b22_wave2_sec22.cpp's
    // own stub doc comment) - a genuine, real "near-null read, not
    // independently verified to fault" finding this host deliberately does
    // NOT reproduce as an unverified crash, answering `false` instead.
    uint64_t& isRappellingResolveFailureSafeCount() { return isRappellingResolveFailureSafeCount_; }

    // group_destroy_do (Sec25.16): reuses guardianAngelGroupKnown/
    // guardianAngelGroupRegistered above (Sec6.6/Sec6.8) directly - a
    // destroyed group is marked un-registered (CHOSEN: a destroyed group
    // should not go on resolving as alive) via setGuardianAngelGroupRegistered,
    // plus this call count.
    uint64_t& groupDestroyedCount() { return groupDestroyedCount_; }

    // Shared, uniform stand-in for this project's own named-hook-slot-array
    // family (0x005e4520/0x00a1fcc0/0x00a29920, Sec1.1 et al.) for every one
    // of this batch's own hook-registration natives - on_projectile_hit
    // (Sec22.10, GLOBAL slot 7), on_near_crash (Sec22.11, PER-OBJECT,
    // computed sub-record +0x1f00 slot 0xf), on_wieldable_prop_destroy
    // (Sec24.20, PER-OBJECT +0x1f00 slot 0xe), on_taunt (Sec24.21, dual-path:
    // PER-OBJECT +0x1f00 slot 0xc OR PER-CHARACTER +0xf8 slot 0x11),
    // on_notoriety_event (Sec24.23, PER-OBJECT +0x1f00 slot 2, default name
    // "#PLAYER1#"), on_hud_screen_exit (Sec24.24, PER-HUD-SCREEN-NAME,
    // dedicated registry singleton 0x012fced8, slot 0), on_character_grabbed
    // (Sec24.25, dual-path: PER-OBJECT +0x1f00 slot 0xd OR PER-CHARACTER
    // +0xf8 slot 0x19). This project does not model the real hook-slot-
    // array/sub-record internals (base address, slot index, ref-counting)
    // anywhere - only that a callback name was registered (or cleared, "" =
    // none) against a resolved key is tracked, honestly, per that native's
    // own "record the request" convention (same MinimapIconRecord/
    // ObjectIndicatorRecord-style precedent this header uses throughout).
    // Keyed by a per-native composite string ("<native>:<key>", or just
    // "<native>" for the one genuinely global slot) so two different
    // natives' own slots never collide even when they share a resolved
    // object name.
    std::string namedHookCallback(const std::string& compositeKey) const {
        auto it = namedHookCallbacks_.find(compositeKey);
        return it != namedHookCallbacks_.end() ? it->second : std::string();
    }
    void setNamedHookCallback(const std::string& compositeKey, const std::string& callbackName) {
        namedHookCallbacks_[compositeKey] = callbackName;
    }

    // on_hud_screen_exit (Sec24.24): the dedicated HUD-screen-name registry
    // singleton (0x012fced8) is a never-populated object space in this
    // project's scope - same "test-registrable" OpenValueMap<bool>
    // precedent as notorietySpawnGroupResolves/shops above (an unregistered
    // screen name is a real, CONFIRMED no-op per the shared hook-slot-array
    // family's own "on a match..." branch structure).
    OpenValueMap<bool>& hudScreenNameResolves() { return hudScreenNameResolves_; }

    // on_random_obj_killed_do (Sec24.22): CHOSEN minimal record (resolved
    // object-category name, callback name, the 4-way category selector) -
    // the 4 category-specific dispatch handlers' own internals are OPEN.
    struct RandomObjKilledHook {
        std::string objectCategoryName;
        std::string callbackName;
        int32_t category = 0;
    };
    std::vector<RandomObjKilledHook>& randomObjKilledHooks() { return randomObjKilledHooks_; }

    // =====================================================================
    // Wave 2, Batch W2-7 (2026-10-08): spec-lua-api-behaviour.md Sec38-60,
    // "gameplay A: vehicles/world/spawn/effects/audio/camera" (113 names).
    // See src/lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp
    // (vehicle/helicopter/airplane/traffic) and src/lua_spec_confirmed_
    // stubs_b27_wave2_spawn_world_audio.cpp (spawn/world/effects/audio/
    // camera/mission-specific) for the full per-native citations this
    // section's own doc comments abbreviate.
    // =====================================================================

    // --- roadblock_create / roadblock_destroy (Sec39.2) ------------------
    // CONFIRMED: "returns exactly 1 number: the new roadblock's 64-bit
    // handle converted to a double, or -1 on any failure." "The template
    // lookup keeps scanning after a match and returns the LAST one" - this
    // project has no real template table, so templateName is stored
    // verbatim (the "last match wins" behaviour is inherent: a second
    // createRoadblock call with the same template name is simply a new,
    // independent record - no lookup/collapsing happens here, matching
    // observable behaviour for the common case of distinct template
    // names). Placement success needs real road-network/geometry data this
    // project does not have (8 candidate spots within 7.0 units, a
    // per-template acceptance test) - placementResolves is test-settable
    // (OPEN until set); a real call against an unregistered placement
    // object name REFUSES (treated as placement failure -> -1), the
    // honest minimal default rather than fabricating road-network state.
    struct RoadblockRecord {
        std::string templateName;
        std::string placementObjectName;
    };
    int64_t createRoadblock(const std::string& templateName, const std::string& placementObjectName); // .cpp
    bool destroyRoadblock(int64_t handle); // .cpp: CONFIRMED requires the roadblock-kind descriptor bit (modelled as
                                            // simply "does this handle exist in the registry")
    bool roadblockExists(int64_t handle) const { return roadblocks_.count(handle) != 0; }
    size_t roadblockCount() const { return roadblocks_.size(); }
    OpenValueMap<bool>& roadblockPlacementResolves() { return roadblockPlacementResolves_; }

    // --- rc_set_max_signal_range (Sec39.2) --------------------------------
    // CONFIRMED: "returns the PREVIOUS range... swaps the new value into a
    // global float." "The global's own start-up default was not traced
    // (OPEN)" - the FIRST real call (no prior .set()) honestly throws
    // OpenStateError via OpenValue::get() below, per this project's
    // standing no-invented-defaults policy.
    OpenValue<double>& rcMaxSignalRange() { return rcMaxSignalRange_; }
    double setRcMaxSignalRangeReturningPrevious(double newRange) {
        double prev = rcMaxSignalRange_.get();
        rcMaxSignalRange_.set(newRange);
        return prev;
    }

    // --- spawning/world-state globals (Sec39.3) ---------------------------
    // spawning_allow_gang_ambient_spawns: "writes the SAME value to four
    // separate global bytes at once... four independent category gates
    // Lua can only ever set in lockstep" (HYPOTHESIS) - modelled as ONE
    // flag since nothing in scope reads the four bytes individually.
    // CHOSEN false initial (no CONFIRMED spawn-time default).
    bool& gangAmbientSpawnsAllowed() { return gangAmbientSpawnsAllowed_; }
    // spawn_region_enable_los: "a bare boolean (absent = disables)...
    // writes the INVERSE of the argument" - the stub applies the inversion;
    // this map holds the resolved "skip LOS test" bit per region name.
    std::unordered_map<std::string, bool>& spawnRegionLos() { return spawnRegionLos_; }
    // set_cops_shooting_from_vehicles: CONFIRMED "the engine's own reset
    // default is false... calling this setter with no argument at all sets
    // it true" - the FIELD default below is the engine's own CONFIRMED
    // reset value (false); the stub's own optional-bool-default-true
    // argument handling is what produces the documented mismatch.
    bool& copsShootingFromVehicles() { return copsShootingFromVehicles_; }
    // set_saints_hated / set_stag_notoriety_area_active: bare booleans,
    // session-synced-variable-registered bytes ("saints_hated"/
    // "stag_notoriety") - CHOSEN false initial (no CONFIRMED default).
    bool& saintsHated() { return saintsHated_; }
    bool& stagNotorietyAreaActive() { return stagNotorietyAreaActive_; }
    // "AND - only when this machine is host and the value actually
    // changed - runs a host-gated spawn/despawn step that releases every
    // tracked handle... when turning off" - this project has no tracked-
    // handle table to release, so only the gated call itself is counted
    // (same "record that a call happened" convention used throughout).
    uint64_t& stagNotorietyAreaHostStepCount() { return stagNotorietyAreaHostStepCount_; }

    // --- spotlight_heli_laser_size (Sec39.2) ------------------------------
    // CONFIRMED default 1.0; "no clamp, no replication, no host reset
    // found anywhere."
    double& spotlightHeliLaserSize() { return spotlightHeliLaserSize_; }

    // --- vehicle_disable_weapon_physics (Sec38.1) --------------------------
    // GLOBAL byte (no vehicle name argument at all, CONFIRMED): "no host
    // gate, no replication, and exactly ONE writer anywhere in the binary -
    // once a script disables it, it stays disabled for the rest of the
    // process, across missions and loads." The stub writes the LOGICAL
    // NOT of the (optional, default-true) argument (CONFIRMED) - modelled
    // here as an "enabled" sense field (so the CONFIRMED "no-argument call
    // disables the effect" case, NOT(true)=false, reads naturally as
    // "not enabled"). CHOSEN true initial (weapon physics enabled at boot).
    bool& weaponPhysicsEnabled() { return weaponPhysicsEnabled_; }

    // --- vehicle_forced_corpse_removal_enabled (Sec38.1) --------------------
    // GLOBAL byte, host+session-gated write. CONFIRMED: "reset to ON at
    // every world init" - CHOSEN true initial (matches that documented
    // reset value).
    bool& vehicleForcedCorpseRemovalEnabled() { return vehicleForcedCorpseRemovalEnabled_; }

    // --- vehicle_detonate_random_parked_car (Sec52.2) hardening counters --
    // See stub_vehicle_detonate_random_parked_car's own doc comment
    // (lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp) for the
    // full CHOSEN (hardened divergence) reasoning - two distinct named
    // counters, one per refusal shape, per this batch's hardening policy.
    uint64_t& vehicleDetonateRandomNoReferenceGuardCount() { return vehicleDetonateRandomNoReferenceGuardCount_; }
    uint64_t& vehicleDetonateRandomFailedReferenceGuardCount() { return vehicleDetonateRandomFailedReferenceGuardCount_; }
    // Third counter: reference name given AND resolves. Per spec Sec52.2,
    // the reference object is a location anchor for an angle/distance
    // window, not the detonation target itself - the default (all-zero)
    // window admits nothing, so this host correctly detonates NOTHING and
    // leaves the resolved reference object's own state untouched.
    uint64_t& vehicleDetonateRandomResolvedReferenceNoOpCount() { return vehicleDetonateRandomResolvedReferenceNoOpCount_; }

    // --- rappel_enter / rappel_exit (Sec39.1) -----------------------------
    // CONFIRMED: "a queued rappel request is stored in a fixed 2-slot
    // table (slot 0 = local player, slot 1 = every other character - two
    // different NPCs' queued rappels silently overwrite each other's
    // slot)." This project has no coordinate/position system to drive the
    // real landing logic (+0xcc8/+0xcce sub-states), so only the
    // CONFIRMED-structure queue-slot assignment and call counts are
    // tracked, honestly, not a simulated landing.
    struct RappelQueue {
        std::string slot0; // local player's own queued target name ("" = none)
        std::string slot1; // every OTHER character's queued target name (shared slot, CONFIRMED collision)
    };
    RappelQueue& rappelQueue() { return rappelQueue_; }
    uint64_t& rappelEnterQueuedCount() { return rappelEnterQueuedCount_; }
    uint64_t& rappelEnterImmediateCount() { return rappelEnterImmediateCount_; }
    uint64_t& rappelExitCount() { return rappelExitCount_; }

    // --- projectile_fire_from_navpoint (Sec40.3) --------------------------
    // "No crash shapes found - every resolution step is properly guarded."
    // No coordinate system to compute a real trajectory - only the gated
    // (both positions resolve, distinct, weapon name resolves, spread <
    // 90 deg) call is counted.
    uint64_t& projectileFireFromNavpointCount() { return projectileFireFromNavpointCount_; }

    // --- vtol_flyby_animation_for_m18 (Sec46.4) ---------------------------
    // "a one-off mission-18 helper (teleport + animation + an
    // unconditionally broadcast network record with no host or ownership
    // gate)" - VehicleState::vtolFlybyM18Played above tracks the per-
    // vehicle play flag; this counts the unconditional broadcast.
    uint64_t& vtolFlybyM18BroadcastCount() { return vtolFlybyM18BroadcastCount_; }

    // --- radio_get_station (Sec40.2) --------------------------------------
    // CONFIRMED: "-1 if unresolved, -1 if resolved but tuned to no
    // station, else the 1-based station index." Default (no entry) is "no
    // station tuned," a REAL confirmed answer, not OPEN - a plain map,
    // not an OpenValueMap.
    int32_t radioStationForVehicle(const std::string& name) const {
        auto it = radioStationByVehicle_.find(name);
        return it != radioStationByVehicle_.end() ? it->second : -1;
    }
    void setRadioStationForVehicle(const std::string& name, int32_t station) { radioStationByVehicle_[name] = station; }

    // --- radio_set_sing_along_allowed_during_missions (Sec40.2) -----------
    // Bare boolean, mission-scoped synced variable "mission_sing_alongs" -
    // CHOSEN false initial (no CONFIRMED default; mission-reset mechanism
    // out of this batch's scope).
    bool& singAlongAllowedDuringMissions() { return singAlongAllowedDuringMissions_; }

    // --- radio_set_sing_along_track / radio_clear_sing_along_track
    // (Sec51.3) - a real cross-machine set/clear divergence ------------
    // CONFIRMED: "song index 0 can be set... but can never be cleared from
    // script (the clear function explicitly rejects index 0)," and "the
    // clear also leaves a separate 'current sing-along index' global
    // stale rather than resetting it." currentIndex below models ONLY the
    // local-apply half (the spec's own explicit "the second [network]
    // argument is never actually read by the local-apply logic at all" -
    // the cross-machine divergence this tranche documents is a NETWORK-
    // layer effect this single-process host has no second machine to
    // exhibit, so it is not fabricated here, only the local half is).
    struct SingAlongTrack {
        int32_t currentIndex = -1; // -1 = none (CHOSEN initial; file-default not stated)
    };
    SingAlongTrack& singAlongTrack() { return singAlongTrack_; }
    // Song-name -> index resolver: this project has no real shipped radio
    // song table (same "no real data table" gap as elsewhere, e.g.
    // vcust's own customization-item tables) - OPEN until a test registers
    // a name; a real call with an unregistered song name honestly refuses
    // rather than fabricating an index.
    OpenValueMap<int32_t>& songNameToIndex() { return songNameToIndex_; }
    void setSingAlongTrack(int32_t songIndex) { singAlongTrack_.currentIndex = songIndex; }
    // CONFIRMED: explicitly rejects index 0; "leaves currentIndex stale"
    // on any OTHER index (i.e. this never resets currentIndex to -1).
    bool clearSingAlongTrack(int32_t songIndex) { // false = rejected (index 0), matching the real native's own refusal
        if (songIndex == 0) return false;
        // CONFIRMED: clearing does NOT reset currentIndex (left stale) -
        // deliberately a no-op on currentIndex itself; only a separate
        // "is a sing-along currently selected" bit (not read by any
        // in-scope native) would actually change, so nothing is written.
        return true;
    }

    // --- radio_unblock (Sec51.3) -------------------------------------------
    // Inverse of the already-registered radio_block (radioBlockedVehicles()
    // above, Sec12.20) - reused directly, no new state.
    void recordRadioUnblock(const std::string& vehicleName) { radioBlockedVehicles_.erase(vehicleName); }

    // --- parking_spot_set_max / parking_spot_reset_max (Sec42.6) ---------
    // CONFIRMED: "values above 18 or negative are silently ignored (only
    // 0-18 accepted)... not clamped, simply dropped." Reset writes the
    // hard-coded default 18 - CHOSEN as the initial value too (reset's own
    // CONFIRMED value is the only concrete number spec gives for this
    // field at all).
    int32_t& parkingSpotMax() { return parkingSpotMax_; }

    // --- path_name_is_path (Sec42.6) --------------------------------------
    // CONFIRMED: "true only for a live object of the specific 'path' kind."
    // This project has no object-kind table - OPEN until a test registers
    // a name's kind.
    OpenValueMap<bool>& pathObjectIsPath() { return pathObjectIsPath_; }

    // --- get_closest_npc_to_object / get_closest_object (Sec41.6/53.5) ---
    // Both need real spatial-query data this project has nowhere in its
    // state (no coordinate system at all, stated project-wide
    // simplification). CONFIRMED STRUCTURAL defaults when nothing
    // qualifies (NOT themselves OPEN, unlike the query result for a
    // specific real case): get_closest_npc_to_object -> (FLT_MAX, ""),
    // get_closest_object -> "" alone, never nil. A test overrides either
    // per reference-object name via these OpenValueMaps; a real call with
    // no override takes the CONFIRMED "nothing qualifies" default rather
    // than throwing (the default itself is a real, spec-given fact).
    struct ClosestNpcResult { double distance; std::string name; };
    OpenValueMap<ClosestNpcResult>& closestNpcOverride() { return closestNpcOverride_; }
    OpenValueMap<std::string>& closestObjectOverride() { return closestObjectOverride_; }

    // --- mesh_mover_apply_impulse / _ready / _set_damaged_by_player_only
    // (Sec41.1) / mesh_mover_reset_by_zone (Sec50.5) -----------------------
    struct MeshMoverRecord {
        bool damagedByPlayerOnly = false; // CONFIRMED bit this setter sets/clears; reader OPEN per spec
    };
    MeshMoverRecord& getOrCreateMeshMover(const std::string& name) { return meshMovers_[name]; }
    bool hasMeshMover(const std::string& name) const { return meshMovers_.count(name) != 0; }
    uint64_t& meshMoverApplyImpulseCount() { return meshMoverApplyImpulseCount_; }
    // mesh_mover_reset_by_zone: "resets every free-standing, currently-
    // attached mesh mover in a named loaded zone... no host gate at all."
    // No real per-zone mover list in this project - counted per zone name,
    // honestly, not simulated.
    std::unordered_map<std::string, uint64_t>& meshMoverResetByZoneCounts() { return meshMoverResetByZoneCounts_; }

    // --- fov_check_xz_plane (Sec44.5) --------------------------------------
    // "An XZ-flattened cone test; arg 3 is a half-angle in degrees
    // (default 30)." No coordinate system in this project - the boolean
    // test result is test-settable per (reference, target) pair; a real
    // call with no override is the honest OPEN refusal (no geometry here).
    // CONFIRMED regardless of geometry: "when the target is in view it
    // returns 2 Lua values (true, false) instead of 1" - this logic defect
    // is reproduced directly by the stub, independent of the override.
    OpenValueMap<bool>& fovCheckXzPlaneResult() { return fovCheckXzPlaneResult_; } // key: "ref|target"

    // --- audio_suppress_ambient_player_lines (Sec45.6) --------------------
    // Host-only write, session-synced variable "persona_supress", "no
    // direct engine reader found anywhere." CHOSEN false initial.
    bool& audioSuppressAmbientPlayerLines() { return audioSuppressAmbientPlayerLines_; }

    // --- boss_battle_kb_enable / boss_battle_mars_killbane_active
    // (Sec45.5) - a real logic defect, ONE shared flag ---------------------
    // CONFIRMED: "boss_battle_mars_killbane_active(false) clears the
    // Killbane QTE-active flag WITHOUT running the QTE-abort cleanup that
    // boss_battle_kb_enable(false) runs." ONE shared bool - both setters
    // write it, only kb_enable's own false-transition also runs cleanup.
    // "Both default to enabling when called with no argument" (the Lua
    // ARGUMENT default is true; the field's own CHOSEN initial value is
    // false - "no QTE in progress" is the natural boot state).
    bool& killbaneQteActive() { return killbaneQteActive_; }
    bool killbaneCleanupPerformed() const { return killbaneCleanupPerformed_; }
    void bossBattleKbEnable(bool enable) { // .cpp body kept here (trivial, no resolve/replication to model)
        if (!enable && killbaneQteActive_) killbaneCleanupPerformed_ = true; // CONFIRMED: only this setter's false-path cleans up
        killbaneQteActive_ = enable;
    }
    void bossBattleMarsKillbaneActive(bool active) {
        killbaneQteActive_ = active; // CONFIRMED: clears/sets with NO cleanup, regardless of previous value
    }

    // --- camera_script_enable / camera_script_disable (Sec45.2) -----------
    // "write byte 0x013c9382 ('script camera owns the view,' HYPOTHESIS)
    // and... UNCONDITIONALLY broadcast... with no host check - either
    // machine can send it." CHOSEN false initial.
    bool& scriptCameraOwnsView() { return scriptCameraOwnsView_; }
    uint64_t& cameraScriptBroadcastCount() { return cameraScriptBroadcastCount_; }

    // --- cutscene_was_skipped (Sec45.2) ------------------------------------
    // "reads bit 0x40 of the cutscene flag byte; stays true after the
    // skipped cutscene has ended, clearing only when the next cutscene
    // starts." CHOSEN false initial (no cutscene has been skipped yet in
    // a fresh host - a real fact independent of the real bit's own
    // start-up value, since nothing sets it true without an actual skip
    // action, out of this batch's own scope). cutscene_check_exiting
    // reuses the EXISTING cutsceneState()/screenFade().cutsceneState
    // accessor directly (Sec26.24/26.25) - no new field needed for it.
    bool& cutsceneWasSkippedFlag() { return cutsceneWasSkippedFlag_; }

    // --- debris_flow_set_inactive (Sec45.2) - CRASH, particle-effect
    // debris flow (NOT EngineState::DebrisFlowState above, which is the
    // UNRELATED world-debris-pile Sec14.20 `debris_flow_set_active`) -----
    // This batch owns only _set_inactive; a sibling wave-2 batch's own
    // debris_flow_create/_destroy/_add_object/_add_avoid_object (not yet
    // merged) is expected to populate particleDebrisFlowKnownIds below -
    // designed to be additive-compatible, not blocked on that merge.
    // HARDENED (host-safety): the real worker sets a stale/unresolvable
    // stored object handle to null and STILL calls a virtual method
    // through it (Sec45.2, "a null dereference reachable whenever a
    // debris-flow entry outlives its object") - this host refuses the
    // deactivate for any flow id not in the registry instead, counting it.
    // "Always replicates (opcode 0x45/0x25) with no host/existence check"
    // - the stub calls replicateStateChange() unconditionally regardless
    // of whether the guard fired.
    std::unordered_set<int64_t>& particleDebrisFlowKnownIds() { return particleDebrisFlowKnownIds_; }
    uint64_t& debrisFlowSetInactiveNullHandleGuardCount() { return debrisFlowSetInactiveNullHandleGuardCount_; }

    // --- force_throw_char_do (Sec55.4) - CRASH hardening counter -----------
    // CONFIRMED: "computes character + 0x40 as a position pointer for the
    // target resolver BEFORE the character is null-checked." HARDENED:
    // this host checks the character for null FIRST (the fix order its own
    // sibling force_throw_do already uses, per spec's own text) instead of
    // reproducing the address-0x40 read, counted here.
    uint64_t& forceThrowCharNullCharacterGuardCount() { return forceThrowCharNullCharacterGuardCount_; }

    // --- continuous_explosion_follow_target_remove (Sec25.23/57.4) --------
    // Shares the SAME <=2-entry global follow-target list a sibling wave-2
    // batch's own continuous_explosion_follow_target_add (not yet merged,
    // 0x005eb3e0) is expected to populate - this batch owns only _remove;
    // field naming ("continuousExplosionFollowTargets_", a plain name
    // vector) is chosen to be the obvious shared name a merge would
    // naturally coordinate on, per this task's own "coordinate field
    // naming if you can infer it" guidance.
    std::vector<std::string>& continuousExplosionFollowTargets() { return continuousExplosionFollowTargets_; }

    // --- is_a_vehicle / is_syn_tower_destroyed (Sec54.3) -------------------
    // is_a_vehicle: no new state - reuses hasVehicle() above directly.
    // is_syn_tower_destroyed: CONFIRMED "nothing more than the already-
    // documented city_zone_swap_is_active with a hardcoded zone name" -
    // reuses the EXISTING cityZoneSwapValue()/recordCityZoneSwap() map
    // directly (Sec28.18/Sec1.9) under the CHOSEN key "syn_tower" (the
    // real literal zone-name string is not given by spec text, only that
    // one is hardcoded - this project's own natural, clearly-labelled
    // stand-in key, not a fabricated gameplay fact).
    static constexpr const char* kSynTowerZoneSwapKey = "syn_tower"; // CHOSEN, see doc comment above

    // --- level_light_enable_all_do / light_group_attach_to_camera /
    // light_group_detach_from_camera (Sec54.2) -----------------------------
    // "a reference-counted 16-slot table (attach increments, detach
    // decrements, removal only at zero)... a 17th attach is silently
    // dropped... a light group that gets despawned while still attached
    // can never be detached through this binding." Modelled as ONE global
    // ref-count against the CONFIRMED 16-slot cap (this project tracks no
    // per-group identity beyond the name key, consistent with "no real
    // light-group table" gap elsewhere).
    std::unordered_map<std::string, int>& lightGroupAttachRefCounts() { return lightGroupAttachRefCounts_; }
    static constexpr int kLightGroupAttachSlots = 16;
    int& lightGroupAttachedSlotsUsed() { return lightGroupAttachedSlotsUsed_; } // total across all groups, capped at 16
    uint64_t& levelLightEnableAllCount() { return levelLightEnableAllCount_; } // "three lighting modes," structure only

    // --- los_check_do (Sec54.2) - CRASH, stuck-poll hazard -----------------
    // CONFIRMED: "returns -1 ('ask again'), then 1 or 0 once resolved...
    // an unresolvable object name returns 0." This project has no cached
    // request-pool mechanism to EVER exhaust (the real stuck-poll hazard's
    // own trigger condition), so the hazard structurally cannot fire here
    // - a real resolved-pair query needs real line-of-sight geometry this
    // project does not have, so it is test-settable (OPEN until set); only
    // the CONFIRMED "unresolvable name -> 0" default is a real fact,
    // applied without an override.
    OpenValueMap<int32_t>& losCheckResult() { return losCheckResult_; } // key: "a|b", values -1/0/1

    // --- guardian_angel_start_zone_pinning / _clear_zone_pinning /
    // _release_attack_groups (Sec53.3): see GuardianAngel::
    // currentZonePinSetName's own doc comment above (extended in place,
    // this batch's own additions to that already-existing struct).

    // --- helicopter_* cluster (Sec53.2): see VehicleState's own new
    // fields above (largeDspiral/dontMoveInCombat/dontExplodeInAir/
    // maxBankAngleOverrideEnabled/landDistanceGatePasses/landed/
    // dropoffOccupancyGatePasses/dropoffRequested) - "accepts any resolved
    // vehicle, not just a helicopter," so these live on VehicleState, not
    // a separate per-helicopter map.

    // --- dlc2_m02_clapboards_set (Sec55.5) - MEMSAFE, hardened AS ONE UNIT
    // WITH its already-registered siblings dlc2ClapboardsGet/_Reset
    // (wave-1 Batch 5, lua_spec_confirmed_stubs_b05_invuln_r15_23.cpp /
    // engine_state.h's own existing dlc2ClapboardCount_/dlc2ClapboardFlags_
    // /dlc2ClapboardsGetNegativeIndexGuardCount_ fields above) - SAME
    // signed-index bounding scheme: argument <= 0 is HOST-SAFETY-refused
    // (own named counter, NOT dlc2ClapboardsGetNegativeIndexGuardCount_ -
    // "count every hardened-path firing via a NAMED PER-SITE counter" per
    // this batch's own hardening policy); the upper bound (index >= count
    // or >= the 10-byte array) is the CONFIRMED real behaviour already
    // established by _get (there: "no value at all"; here: the write is
    // simply skipped, there is no return value to shape). See
    // EngineState::dlc2ClapboardsGet's own doc comment (above) for the
    // full CONFIRMED shape this reuses.
    void dlc2ClapboardsSet(int32_t clapboardNumber, bool value); // .cpp
    int dlc2ClapboardsSetNegativeIndexGuardCount() const { return dlc2ClapboardsSetNegativeIndexGuardCount_; }

    // --- dlc3_m03_punch_load / _unload (Sec55.5) ---------------------------
    // "load/unload a UI document... by a bitmask argument (bit 0 = local,
    // bit 1 = broadcast)." CONFIRMED logic defect: "_load overwrites its
    // stored document-id global without checking it was already set" -
    // modelled as a plain unconditional overwrite (the "leak" IS this
    // absence of a check, not a separate thing to simulate).
    struct Dlc3Punch {
        bool loaded = false;
        uint64_t loadCount = 0;
        uint64_t unloadCount = 0;
        uint64_t broadcastCount = 0; // bit 1 of either mask argument
    };
    Dlc3Punch& dlc3Punch() { return dlc3Punch_; }

    // --- flashlights_enable / flashlights_disable (Sec55.5) ----------------
    // CONFIRMED: global byte 0x0262e690, "a world/mission reset routine
    // clears the byte back to 'enabled' independently" - CHOSEN true
    // initial (matches the documented reset value; this project has no
    // separate mission-reset hook in scope to model as a distinct event).
    bool& flashlightsEnabledGlobal() { return flashlightsEnabledGlobal_; }

    // --- audio_play_do / audio_play_id_do / audio_play_id_for_navpoint_do
    // (event-based, 3 of 7) / audio_play_for_character_do / audio_play_
    // for_character_weapon_do / audio_play_for_mover_do / audio_play_id_
    // for_character_do (object-sound, 4 of 7) (Sec59.6) -------------------
    // CONFIRMED: "the other four... return a monotonically-incrementing
    // engine serial counter" - reuses the EXISTING nextAudioVoiceHandle()
    // shared counter directly (already reused by nextEffectPostHandle()
    // elsewhere), not a new counter. CONFIRMED: "three event-based
    // bindings... return a result that can be left uninitialized on the
    // stack if audio itself was never initialized." HARDENED (host-safety):
    // this host returns the documented failure value 0 instead (CHOSEN -
    // no specific numeric failure value is given for this event-based
    // trio by name; 0 reuses "the rest" convention the object-sound
    // bindings' own failure shape already establishes, Sec59.6/59.8) -
    // rather than reproducing an uninitialized stack read.
    bool& audioSystemInitialized() { return audioSystemInitialized_; } // CHOSEN true initial; a test flips it false
                                                                        // to exercise the hardened path above
    uint64_t& audioEventUninitializedGuardCount() { return audioEventUninitializedGuardCount_; }
    int64_t allocateAudioSerial() { return nextAudioVoiceHandle(); }

    // --- force_fire_do / force_fire_target_do / force_throw_do /
    // force_throw_char_do (Sec55.4): reuse CharacterState's own EXISTING
    // caeStagedActionIndex/caeStagedTargetName/caeCommittedActionIndex/
    // caeCommittedTargetName fields (Sec30.4, 70-entry CAE table) directly
    // - action 0 = fire (CONFIRMED literal), action 4 = throw grenade
    // (CONFIRMED literal, "same pattern as ai_suggest_action"). No new
    // EngineState/CharacterState fields needed beyond
    // CharacterState::hasThrowableItemEquipped (added above, Sec55.4).

    // --- airplane_takeoff_do / airplane_land_do (Sec59.7) /
    // airplane_fly_to_do (Sec58) ---------------------------------------
    // CONFIRMED: airplane_land_do "always pushes true once its two name
    // arguments resolve, regardless of what the underlying landing
    // routine itself reports" (a return-value trap, modelled directly by
    // the stub, not by this state). airplane_fly_to_do's navpoint form
    // "can never work" (CONFIRMED: always returns false) and "leaks a
    // request id... silently cancels [a] previously-started path flight"
    // - this project has no per-vehicle flight-request slot to actually
    // cancel, so only the leak/supersede FACT is counted, honestly, not a
    // simulated flight state.
    struct AirplaneRecord {
        bool takeoffRequested = false;
        bool pathFlightInProgress = false; // set by the (out-of-scope) path-target form; this batch only clears/
                                            // supersedes it via the leaked-request path, per Sec58's own text
    };
    std::unordered_map<std::string, AirplaneRecord>& airplanes() { return airplanes_; }
    uint64_t& airplaneFlyToLeakedRequestCount() { return airplaneFlyToLeakedRequestCount_; }
    // HARDENED (host-safety): the real engine measures the target-name
    // argument's strlen() BEFORE null-checking it (Sec58, "the identical
    // bug already documented in vehicle_pathfind_to_do") - this host
    // refuses (treats as empty/unresolved) instead of reading a non-string
    // Lua value as a C string, counted here.
    uint64_t& airplaneFlyToNullTargetGuardCount() { return airplaneFlyToNullTargetGuardCount_; }

    // --- camera_look_through_do / camera_script_is_finished (Sec59.4) -----
    // CONFIRMED: "the HUD-hide reference is taken BEFORE the target object
    // is resolved" - hudHideRefCount below is incremented unconditionally
    // by the stub, regardless of resolve outcome, matching that text
    // exactly (a real "effect taken before its precondition is checked"
    // logic defect, not a crash - reproduced faithfully, not hardened
    // away). camera_script_is_finished's own "divide-by-zero-shaped but
    // harmless NaN comparison for a zero duration" (CONFIRMED structure,
    // HYPOTHESIS exact comparison outcome) is modelled as CHOSEN
    // "immediately finished" for durationMs <= 0 - the simplest reading
    // consistent with "harmless" that does not fabricate a specific NaN
    // comparison result spec text does not give.
    struct CameraLookThrough {
        int hudHideRefCount = 0;
        bool active = false;
        double durationMs = 0.0;
        OpenValue<double> elapsedMs{"camera look-through elapsed time (no real per-frame clock tied to this call)",
                                    "spec-lua-api-behaviour.md Sec59.4"};
        uint64_t broadcastCount = 0;
    };
    CameraLookThrough& cameraLookThrough() { return cameraLookThrough_; }

    // --- boss_battle_matt_qte_begin / boss_battle_matt_set_phase
    // (Sec59.5): see BossBattleMatt's own new fields above (phase/
    // kPhaseTableSize/qteBeginOutOfRangePhaseGuardCount/
    // qteBeginMattNotLiveGuardCount/qteBeginUnresolvedPlayerGuardCount) -
    // CHOSEN literal object name "Matt" for the liveness check
    // (objectResolves()), matching the spec's own prose ("the boss
    // character 'Matt'").

    // --- spawn_global_override_set_category / _clear_category (Sec49.2) --
    // CONFIRMED: "the global override is not replicated at all mid-
    // session - it only reaches a newly-joining client via the one-time
    // join snapshot." A plain string value, "" = cleared/none.
    struct SpawnGlobalOverride { std::string category; };
    SpawnGlobalOverride& spawnGlobalOverride() { return spawnGlobalOverride_; }

    // --- spawn_override_clear_override_for_hood (Sec49.2, `0x00a5fb40`) ---
    // UPDATED 2026-10-09 (spec sync 3ecbf84, closing this entry's own
    // previously-AGG placeholder): CONFIRMED real body - resolves the hood
    // through the SAME chain as the SET sibling above, then
    // `AND [hood+0x64], 0xfb` (clears override-enabled bit 0x04) and
    // `MOV [hood+0x70], 0` (zeroes the stored category pointer) - "not a
    // 'remove from a list' or 'reset to a default category' - there is no
    // fallback category value involved; it simply resets both override
    // fields to unset." Modeled here by erasing the hood from BOTH of the
    // SET sibling's own real fields (SpawnOverride::perHoodCategory and
    // ::perHoodCategoryNullStored above) - the SAME struct the SET entry
    // point actually writes, NOT the disconnected placeholder map this
    // accessor used to expose (a real pre-existing bug this sync caught:
    // the old clear-for-hood erased from a map SET never wrote to, so
    // calling set-then-clear never actually cleared anything). On a host,
    // the real function also broadcasts the identical opcode `0x45`/
    // sub-type `0x29` record shape the SET native sends, with a "none"
    // sentinel hash in place of a resolved category hash, replicated to
    // remote machines the same way SET is - NOT modeled here, matching
    // this project's own existing scope line for this whole family (the
    // SET sibling immediately above does not model replication either;
    // adding it to CLEAR alone, when its own sibling lacks it, would be a
    // net-new, unasked-for mechanism, not a sibling-parity fix).
    void spawnOverrideClearOverrideForHood(const std::string& hood) {
        auto& so = spawnOverride();
        so.perHoodCategory.erase(hood);
        so.perHoodCategoryNullStored.erase(hood);
    }

    // --- spawn_region_min_spawn_dist / _reset (Sec32.1, `0x00a5dc40`/
    // `0x00a5dc70`) --------------------------------------------------------
    // UPDATED 2026-10-09 (spec sync 3ecbf84, closing this pair's own
    // previously-AGG "shape only, CHOSEN 0.0" placeholder): CONFIRMED real
    // body, the exact sibling pair of spawnRegionMaxSpawnDistSquared_
    // above, same mechanism, different global. Set: narrows the argument
    // to float and stores `d * d` (squared, exactly like the MAX
    // sibling's own setter - the stub was previously storing the RAW
    // value, a real bug this sync also fixes). Reset: copies back a
    // file-backed read-only constant `0x451c4000` (= 50.0f² = 2500.0f) -
    // the CONFIRMED compile-time default, replacing the old placeholder's
    // CHOSEN 0.0. Edges (CONFIRMED): a negative input squares to the same
    // positive value as its magnitude (no sign rejection); NaN makes the
    // later `<` comparison always false, silently disabling the floor (see
    // SpawnRegionAnnulusEligibleForPlayer below, which reproduces that
    // same "NaN never triggers the too-close rejection" behaviour by using
    // a plain `<` against the stored squared value, not a NaN-safe
    // comparison).
    float spawnRegionMinSpawnDistSquared() const { return spawnRegionMinSpawnDistSquared_; }
    void setSpawnRegionMinSpawnDistSquared(float v) { spawnRegionMinSpawnDistSquared_ = v; }
    void resetSpawnRegionMinSpawnDist() { spawnRegionMinSpawnDistSquared_ = 2500.0f; } // CONFIRMED 50.0f^2

    // --- trigger_allow_dead_guys (Sec52/T15) -------------------------------
    // No individual spec text beyond "resolved in full, no further crash
    // shapes" (§52.3's own one-line mention). CHOSEN minimal: trigger name
    // + optional boolean (default true, matching this document's dominant
    // nil-gated-optional convention elsewhere), local field write only -
    // no replication is claimed since none is documented.
    std::unordered_map<std::string, bool>& triggerAllowDeadGuys() { return triggerAllowDeadGuys_; }

    // --- get_stag_active (Sec53/T20) ---------------------------------------
    // "resolved in full, no crash shape" - no further detail. CHOSEN false
    // initial (an "active" flag's natural boot state).
    bool& stagActive() { return stagActive_; }

    // --- waypoint_is_placed (Sec46/T14, AGG) -------------------------------
    // No individual spec text (lumped into §46.5's "11 functions... vehicle
    // flags and queries" cluster with no per-function detail beyond the
    // ones the prose actually names). CHOSEN minimal: a GLOBAL query (no
    // vehicle argument fits the name), false initial, no in-scope setter
    // (the real waypoint_add/_remove family is out of this batch).
    bool& waypointPlaced() { return waypointPlaced_; }

    // --- world_despawn_all_vehicles / world_despawn_all_pedestrians
    // (Sec46.3) ---------------------------------------------------------
    // Walks vehicles()/characters() (this project's own stand-in for "the
    // dedicated ambient/traffic lists," same convention VehicleState's own
    // doc comment on vehicles() already establishes) and marks every
    // non-protected, non-player-occupied entry despawnedPendingDelete -
    // "deferring actual deletion through the engine's normal pending-
    // delete queue rather than freeing synchronously" is modelled as
    // "marked, not erased from the map" (consistent with this project's
    // general no-real-deletion convention elsewhere, e.g. garage/pool
    // records). Returns the count actually despawned.
    uint64_t despawnAllVehicles(); // .cpp
    uint64_t despawnAllPedestrians(); // .cpp

    // =====================================================================
    // Batch W2-5 (2026-10-08), wave 2 of the parallel fan-out - Customization
    // frontends: PCU/PCR/customization_* (src/lua_spec_confirmed_stubs_b24_
    // wave2_pcu_pcr.cpp) + vcust/store/garage/crib/gang (src/lua_spec_
    // confirmed_stubs_b25_wave2_vcust_store_garage.cpp). spec-lua-api-
    // behaviour.md Sec19/Sec23/Sec26/Sec38-Sec60 (various ranking tranches -
    // see each b24/b25 stub's own doc comment for its exact citation).
    //
    // HARDENING BATCH: this wave owns 28 crash-shaped findings (the most of
    // any wave-2 batch), 4 of them the brief's own NAMED memory-safety bugs
    // (gang_customization_confirm_gang_sign/_select_gang_sign/_select_
    // vehicle, vcust_purchase_component - the last one's own state lives on
    // VcustPreview above, extended in place rather than duplicated). EVERY
    // one of the 28 is bound-checked/refused here, never reproduced - see
    // each counter's own doc comment below for which stub increments it and
    // the spec line it diverges from. Reuses wave-1 state where the SAME
    // real mechanism is already modelled (VcustPreview::targetLive above,
    // PcuInventory/PcuCategoryTable/PcuCatalogOutfits/playerCash()/
    // purchaseLedger() below/above, PcrCustomizationState's existing 63-slot
    // list) rather than re-deriving a parallel model - see each struct's own
    // "reused from" note.
    // =====================================================================

    // customization_create_character / customization_creation_is_open /
    // customization_screen_is_ready (Sec60.1).
    struct CustomizationCreationState {
        // customization_create_character: CONFIRMED "runs the same 3-call
        // sequence the engine's own ... co-op character-creation flow runs,
        // ending in a step that reads the session pointer's +0x60 field
        // with no null test - any single-player call ... faults." HARDENED:
        // reuses the existing coopSession().present (CONFIRMED false by
        // default in single player, Sec8.27/Sec26.28 - applySpecInitialState
        // already seeds it) as the no-session refusal gate, rather than
        // inventing a separate flag.
        uint64_t noSessionRefusedCount = 0;
        uint64_t sessionPresentAttemptedCount = 0; // session-present path - deep internals OPEN, counted only
        // customization_creation_is_open: CONFIRMED "always returns true in
        // single player" - a real logic defect in the original (a polling
        // loop never exits) that this task's own brief states explicitly
        // IS the faithful behaviour here, not a bug to fix. No state
        // needed - the stub returns a literal true.
        uint64_t creationIsOpenQueryCount = 0;
        // customization_screen_is_ready: no body given in spec scope beyond
        // "resolves in full, no crash shape" (Sec60.1) - modelled as a
        // plain OPEN flag, not invented (same "mechanism confirmed, no body
        // given" convention as pcrPlasticSurgeonMode_ above).
        OpenValue<bool> screenIsReady{"customization_screen_is_ready backing flag", "spec-lua-api-behaviour.md Sec60.1"};
    };
    CustomizationCreationState& customizationCreation() { return customizationCreation_; }

    // PCR voice (Sec23.1 pcr_set_voice / Sec43.3 pcr_get_voice/pcr_cleanup_
    // player_voice): +0x2088+0x10 cached voice id, -1 = none (CHOSEN
    // sentinel). The 7-entry by-name resolver (0x009ed7a0) and the "Phone_
    // Call" cue handle are test-populated/counted (no real voice-cue table
    // loaded in scope).
    struct PcrVoiceState {
        int32_t cachedVoiceIndex = -1;
        bool haveCachedVoice = false;
        OpenValueMap<bool> voiceTagResolves{"pcr_set_voice's own by-name voice resolver (0x009ed7a0), indices 0-6",
                                            "spec-lua-api-behaviour.md Sec23.1"};
        uint64_t playCurrentCount = 0;
        uint64_t resolvedAndAppliedCount = 0;
        uint64_t unresolvedTagCount = 0;
        uint64_t phoneCallHandleOpenCount = 0; // CONFIRMED: "Phone_Call" handle opened unconditionally once resolved, Sec23.1
        uint64_t cleanupCount = 0; // pcr_cleanup_player_voice - clears cachedVoiceIndex, replicated when host (Sec43.3)
    };
    PcrVoiceState& pcrVoice() { return pcrVoice_; }

    // PCR triangle cursor (Sec19.3 pcr_set_triangle_cursor_pos / Sec43.3
    // pcr_get_triangle_cursor_pos): CONFIRMED (Sec43.3's own correction)
    // the setter TRUNCATES both values to an integer before storing -
    // fractional positions are lost on write, not just on read.
    struct PcrTriangleCursor {
        int32_t x = 0;
        int32_t y = 0;
        bool known = false;
    };
    PcrTriangleCursor& pcrTriangleCursor() { return pcrTriangleCursor_; }

    // PCR report family (Sec19.5 pcr_report_composite_items, Sec23.3 pcr_
    // report_composite_colors, Sec23.4 pcr_report_avail_morphs, Sec43.3
    // pcr_report_hair_colors/_hair_items/_anims/_eye_colors/_skin_colors):
    // every one dispatches a named Lua callback via the project's already-
    // established "spawns a script coroutine" mechanism (Sec43.1 - this
    // resolves the former "0x00e0xxxx event-report family" wording, Sec26.
    // 23). This project has no real Lua-coroutine-dispatch model, and no
    // in-scope producer for the underlying per-category/per-item records
    // either, so every report function here is COUNTED (attempted, plus
    // resolved/unresolved where the text gives a resolver), never actually
    // invoking a callback - same "mechanism confirmed, no in-scope
    // producer" convention as VintDataResponderRecord::dispatchAttempts/
    // QteSlotEntry above.
    struct PcrReportCounters {
        uint64_t compositeItemsReportCount = 0;
        uint64_t compositeColorsReportCount = 0;
        uint64_t availMorphsReportCount = 0;
        uint64_t hairColorsReportCount = 0;
        uint64_t hairItemsReportCount = 0;
        // pcr_report_hair_items (Sec43.3 CRASH): "Maps the category through
        // a 3-entry table with NO range check (unlike pcr_change_hair/pcr_
        // get_hair_info, which do)." HARDENED: bound-checked here (0-2),
        // matching its own sibling functions' existing convention.
        uint64_t hairItemsOobRefusedCount = 0;
        uint64_t animsReportCount = 0;
        uint64_t eyeColorsReportCount = 0;
        uint64_t skinColorsReportCount = 0;
        uint64_t reportCategoryUnresolvedCount = 0; // shared "category didn't resolve" counter across the whole family
    };
    PcrReportCounters& pcrReportCounters() { return pcrReportCounters_; }

    // pcr_set_random_composite (Sec19.4): per-category eligible-row count -
    // this project's own test-populated stand-in for the real per-category-
    // definition table scan (0x0045b3a0 validity predicate, OPEN).
    struct PcrRandomComposite {
        OpenValueMap<int32_t> eligibleRowCount{
            "pcr_set_random_composite's own per-category eligible-row scan (0x0045b3a0 validity predicate)",
            "spec-lua-api-behaviour.md Sec19.4"};
        uint64_t appliedCount = 0;
        uint64_t categoryUnresolvedCount = 0;
        uint64_t noEligibleRowsCount = 0; // CONFIRMED: an empty eligible set is a real, reachable no-op, not a crash
    };
    PcrRandomComposite& pcrRandomComposite() { return pcrRandomComposite_; }

    // pcr_apply_preset (Sec23.8) / pcr_apply_regional_preset (Sec43.3):
    // named-preset/region resolvers - test-populated (no real preset-slot
    // array or region table loaded in scope).
    struct PcrPresetState {
        OpenValueMap<bool> presetResolves{"pcr_apply_preset's own named preset-slot scan (0x02300858)",
                                          "spec-lua-api-behaviour.md Sec23.8"};
        OpenValueMap<bool> regionResolves{"pcr_apply_regional_preset's own region/preset name resolvers",
                                          "spec-lua-api-behaviour.md Sec43.3"};
        uint64_t appliedCount = 0;
        uint64_t regionalAppliedCount = 0;
        // pcr_apply_regional_preset (Sec43.3 CRASH): "Neither the preset-
        // apply step nor the morph-apply step null-checks the local player,
        // so a valid region/preset pair with no local player crashes."
        // HARDENED: reuses hasLocalPlayer() as the refusal gate.
        uint64_t regionalNoLocalPlayerRefusedCount = 0;
    };
    PcrPresetState& pcrPreset() { return pcrPreset_; }

    // pcr_purchase_tattoo (Sec43.3 CRASH - "No sign check, so a negative
    // price PAYS the player instead of charging them; no affordability
    // check; no player null check."). HARDENED: refuses a negative price
    // (the task brief's own explicit instruction) and reuses hasLocalPlayer()
    // for the null-player half; reuses playerCash()/purchaseLedger() rather
    // than a separate wallet (this is a cash-only transaction, Sec43.3).
    struct PcrTattooPurchase {
        uint64_t chargedCount = 0;
        uint64_t negativePriceRefusedCount = 0; // HARDENED divergence (named in the task brief)
        uint64_t noLocalPlayerRefusedCount = 0; // HARDENED (unchecked local player)
    };
    PcrTattooPurchase& pcrTattooPurchase() { return pcrTattooPurchase_; }

    // pcr_hair_randomize_all / pcr_lips_randomize (Sec43.3 CRASH, both
    // divide-by-zero on an empty palette) / pcr_randomize_eye_colors
    // (Sec43.3, data-gated 600-entry stack array, not a divide-by-zero).
    // Palette sizes are test-populated (no real palette data loaded in
    // scope) - HARDENED: an empty (or never-set) palette REFUSES the draw
    // instead of dividing by zero.
    struct PcrRandomizePalettes {
        OpenValue<int32_t> lipsColorPaletteSize{"pcr_lips_randomize's own lips-colour palette (PRNG divisor hi-lo+1)",
                                                "spec-lua-api-behaviour.md Sec43.3"};
        OpenValue<int32_t> hairColorPaletteSize{"pcr_hair_randomize_all's own hair-colour table (PRNG divisor hi-lo+1)",
                                               "spec-lua-api-behaviour.md Sec43.3"};
        uint64_t lipsAppliedCount = 0;
        uint64_t lipsEmptyPaletteRefusedCount = 0; // HARDENED divergence (real: divide-by-zero)
        uint64_t hairAppliedCount = 0;
        uint64_t hairEmptyPaletteRefusedCount = 0; // HARDENED divergence (real: divide-by-zero)
        uint64_t eyeColorsAppliedCount = 0;        // pcr_randomize_eye_colors - real selection internals OPEN, counted only
    };
    PcrRandomizePalettes& pcrRandomizePalettes() { return pcrRandomizePalettes_; }

    // pcr_change_skin_color (Sec19.15): CONFIRMED "bounds-checks the index
    // against a live count global (0x02300850, itself gated on a non-null
    // table pointer 0x0230084c)." Test-populated (no real skin-colour
    // table loaded in scope) - HARDENED: refuses an unpopulated, negative,
    // or out-of-range index. This is a DEDICATED field, not reused from
    // PcrRandomizePalettes above - CORRECTED (adversarial review): a prior
    // version of this stub's gate read pcrRandomizePalettes().
    // hairColorPaletteSize (a HAIR-family field, the wrong category
    // entirely, belonging to pcr_hair_randomize_all) and never actually
    // compared the index against any bound at all (only checked "known"
    // and "negative," no upper-bound comparison) - a real fidelity bug,
    // not a memory-safety one (every write still lands inside this
    // project's own fixed-size state).
    struct PcrSkinColorChange {
        OpenValue<int32_t> tableSize{"pcr_change_skin_color's own skin-colour table count (0x02300850, gated on non-null 0x0230084c)",
                                     "spec-lua-api-behaviour.md Sec19.15"};
        uint64_t appliedCount = 0;
        uint64_t oobRefusedCount = 0; // HARDENED (fidelity fix): refuses an unpopulated/negative/out-of-range index - the prior version never checked the upper bound at all
    };
    PcrSkinColorChange& pcrSkinColorChange() { return pcrSkinColorChange_; }

    // pcr_creation_complete (Sec43.3 CRASH - "if the [dialog] pool is
    // exhausted, the allocator returns null and the caller dereferences it
    // unchecked. In single player, 'ready' is always true (no remote player
    // exists)."). HARDENED: refuses to open the waiting dialog when the
    // (test-populated) pool reports exhausted.
    struct PcrCreationComplete {
        OpenValue<bool> dialogPoolExhausted{"pcr_creation_complete's own fixed dialog pool (allocator result)",
                                            "spec-lua-api-behaviour.md Sec43.3"};
        uint64_t readyCount = 0;              // CONFIRMED: single player is always ready (no remote player, Sec43.3)
        uint64_t poolExhaustedRefusedCount = 0; // HARDENED divergence counter
    };
    PcrCreationComplete& pcrCreationComplete() { return pcrCreationComplete_; }

    // pcr_add_default_clothing (Sec43.3): CONFIRMED a genuinely destructive
    // effect - "empties the player's entire owned-clothing inventory (sets
    // the owned count to 0) before re-adding only the default garments."
    // The task brief's own explicit instruction: preserve this FAITHFULLY
    // (it is intentional engine behaviour for new-character creation, NOT a
    // bug to harden away) - flagged clearly here so nobody "fixes" it later
    // by mistake. Reuses the existing PcuInventory::ownedItemCount below
    // (NOT PcuClothingState::slots, the WORN slots - a completely
    // different storage, Sec19.16's own "do not conflate the two" caution)
    // as the actual owned-item count this wipes to 0. CORRECTED
    // (adversarial review): a prior version of this stub's code wiped
    // PcuClothingState::slots (all 24 WORN slots) instead of
    // PcuInventory::ownedItemCount, landing this deliberately-preserved
    // bug on the wrong object entirely - the real "do not fix" wipe never
    // actually happened. Fixed to target the field this comment (and
    // PcuClothingState::addDefaultClothingSlot10RefreshCount's own doc
    // comment) already correctly named.
    uint64_t& pcrAddDefaultClothingWipeCount() { return pcrAddDefaultClothingWipeCount_; }

    // New-game default outfit (spec-frontend-menu-runtime.md Sec9 item 1,
    // CLOSED 2026-10-11): CONFIRMED the real script path is
    // store_character.lua's store_character_lineup_to_customization() -
    // the character-creation/preset-selection screen's own transition
    // handler - which applies the player's chosen creation preset via
    // pcr_apply_preset(preset_name, "figure", regional_preset_name, false)
    // and immediately calls pcr_add_default_clothing() with the script's
    // own comment: "We want the character to have clothes on at the top
    // menu, and we need to change to preset clothing." This is NOT a
    // separate xtbl-level "default outfit" field - no such field exists
    // anywhere in this project's unlockables/player_presets models either
    // (confirmed clean negative), so there is no competing path to remove
    // here. Team B's CHOSEN application point for "new game, no save
    // present" (same status as kNewGameStartingCashCents/
    // grantNewGameAutoUnlocks above): mirrors that exact script call pair
    // against the SAME pcr_apply_preset/pcr_add_default_clothing state the
    // stubs already use (lua_spec_confirmed_stubs_b24_wave2_pcu_pcr.cpp),
    // so a fresh game's outfit is always driven by whichever character-
    // creation preset the player picked, never an xtbl default.
    // Lower-folds presetName the SAME plain-ASCII way
    // stub_pcr_apply_preset's own toLowerAsciiB24 does (AsciiLowerFold,
    // zone_object_resolver.h, already included above), so this resolves
    // against the identical presetResolves() keys tests already populate.
    // The script calls pcr_add_default_clothing() unconditionally right
    // after pcr_apply_preset() - it never checks a return value (there
    // isn't one) - so the wipe/re-add step below is NOT gated on whether
    // the preset itself resolved; only the returned bool (for test/caller
    // visibility) reflects that.
    bool applyNewGameDefaultOutfit(const std::string& presetName) {
        const std::string key = AsciiLowerFold(presetName);
        const bool presetResolved = pcrPreset_.presetResolves.known(key) && pcrPreset_.presetResolves.get(key);
        if (presetResolved) ++pcrPreset_.appliedCount;
        ++pcuClothing_.addDefaultClothingSlot10RefreshCount; // CONFIRMED real effect ("refreshes worn slot 10")
        pcuInventory_.ownedItemCount = 0; // CONFIRMED-destructive wipe - FAITHFUL (Sec43.3), same target stub_pcr_add_default_clothing uses
        ++pcrAddDefaultClothingWipeCount_;
        return presetResolved;
    }

    // pcr_strip_clothing (Sec26.2) / pcr_restore_clothing (Sec23.2): plain
    // call counters - both functions' own deep internals (the "mark dirty
    // vs. clear" OPEN question, Sec26.2; the full re-sync chain, Sec23.2)
    // are OPEN past their own entry checks, so only the gated call itself
    // is counted, honestly, per this project's established convention.
    uint64_t& pcrStripClothingCount() { return pcrStripClothingCount_; }
    uint64_t& pcrRestoreClothingCount() { return pcrRestoreClothingCount_; }

    // ---- PCU (clothing store/wardrobe) family, Sec19/Sec26/Sec40/Sec43 ----
    // One worn-clothing slot, 0-23 (CONFIRMED range, Sec40.5) - the PCU-
    // family per-character clothing list (0x022d0920-family), a SEPARATE
    // storage from PcrCustomizationState's own 63-slot PCR composite-item
    // list (Sec19.16's own explicit "do not conflate the two" caution).
    struct PcuClothingSlot {
        int32_t itemId = -1; // -1 = empty (CHOSEN sentinel, same convention as PcrCustomizationItem)
        int32_t variantIndex = -1;
        int32_t colorIndex = -1;
        bool isNew = false; // owned "new" flag, byte +9 (pcu_clear_item_new, Sec43.2)
    };
    // pcu_category_nav_clear_slot (Sec43.2 CRASH): the 16-entry area
    // descriptor array (0x022ffcf0) - CONFIRMED count, same array pcu_set_
    // active_category/pcu_get_categories_in_area/pcu_get_areas (Sec19.1/
    // Sec26.14/Sec26.15) also read - test-populated by name (no real area/
    // category data loader in scope).
    struct PcuAreaDescriptor {
        std::string name;
        bool known = false;
    };
    struct PcuClothingState {
        static constexpr int kSlotCount = 24;           // 0-23, CONFIRMED (Sec40.5)
        static constexpr int kBackpackSlot = 0x17;       // 23 (Sec40.5)
        static constexpr int kUnderwearSlot = 0x13;      // 19 (Sec40.5)
        static constexpr int kAreaDescriptorCount = 16;  // CONFIRMED bound (Sec43.2) - the HARDENING target
        std::array<PcuClothingSlot, kSlotCount> slots{};
        std::array<PcuAreaDescriptor, kAreaDescriptorCount> areas{};

        int32_t previewedSlot = -1; // 0x01300c6c sentinel (Sec19.2/Sec19.12/Sec19.13)
        bool listChanged = false;   // "list changed" tracking, cleared by pcu_discard_slot (Sec26.12)
        std::string activeAreaName; // 0x022ffd4c cached area name (Sec19.1)
        bool activeAreaIsBackpacks = false;
        bool wardrobeMode = false;  // 0x022ffd44 (Sec26.15)

        // --- pcu_set_active_category (Sec19.1) / pcu_clear_preview_slot
        // (Sec19.2) / pcu_get_slot_info+pcu_set_slot_info (Sec19.12/19.13)
        // / pcu_discard_slot (Sec26.12) / pcu_wear_current_clothing (Sec26.
        // 11) call counters - deep per-field internals OPEN past entry/
        // resolve checks (see each stub's own doc comment). ---
        uint64_t setActiveCategoryCount = 0;
        uint64_t clearPreviewSlotCount = 0;
        uint64_t getSlotInfoFoundCount = 0;
        uint64_t getSlotInfoNotFoundCount = 0;
        uint64_t setSlotInfoAppliedCount = 0;
        uint64_t setSlotInfoOobRefusedCount = 0; // HARDENED: slot/category id bound (< 0x18, Sec19.12's own CONFIRMED check reproduced, not a new divergence)
        uint64_t discardSlotCount = 0;
        uint64_t wearCurrentClothingCount = 0;

        // --- HARDENING counters (pcu_category_nav_clear_slot / pcu_create_
        // outfit - 2 of this batch's 28 crash-shaped findings live here). ---
        uint64_t categoryNavOobRefusedCount = 0;  // pcu_category_nav_clear_slot (Sec43.2 CRASH: 16-bit index, no bound/null check)
        int32_t categoryNavBackupSlot5Item = -1;  // single-level backup (Sec43.2's own "loses the original slot-5 item" note)
        bool categoryNavBackupValid = false;
        uint64_t createOutfitPoolExhaustedRefusedCount = 0; // pcu_create_outfit (Sec43.2 CRASH: null write on an empty block/node pool)
        uint64_t createOutfitCapRefusedCount = 0;           // the 128-saved-outfit cap (Sec43.2: real engine "orphans the already-allocated node" instead)
        int32_t nextCreatedOutfitIndex = 0;
        uint64_t deleteOutfitCount = 0;

        // pcr_add_default_clothing (Sec43.3): "Refreshes worn slot 10"
        // first (CONFIRMED real, distinct effect) before its destructive
        // owned-inventory wipe (see pcrAddDefaultClothingWipeCount()'s own
        // doc comment, and PcuInventory::ownedItemCount below - the
        // ACTUAL wipe target). The slot-10 refresh's own real garment
        // content is OPEN (no default-garment table loaded in scope, same
        // "no in-scope producer" gap the wipe's own re-add step has) - so
        // only the real, test-observable attempt is counted here, not
        // invented content; `slots[10]` itself is left untouched rather
        // than guessed at.
        uint64_t addDefaultClothingSlot10RefreshCount = 0;
    };
    PcuClothingState& pcuClothing() { return pcuClothing_; }

    // pcu_get_num_items_owned / pcu_get_owned_item_info / pcu_is_current_
    // item_worn (Sec40.5/Sec40.6 - the 3 of the "15 pcu_* names" that ARE
    // individually spelled out in spec scope, so no AGG marker). pcu_get_
    // num_items_owned reuses the EXISTING pcuCatalogOutfits() (b04,
    // Sec40.5/40.6) "bit 0 only" numbering rather than a new catalog model
    // - CONFIRMED "numbers the identical catalog list by testing bit 0
    // only," the SAME filter pcu_purchase_outfit already applies there.
    struct PcuOwnedItemQueries {
        // pcu_get_owned_item_info (Sec40.5 CRASH): "dereferences a null item
        // record when the item index is out of range (once any item is
        // owned), and separately its variant search scans with <= instead
        // of < [a one-past-the-end read]." Test-populated per-item record
        // (no real owned-item array in scope) - HARDENED: an unresolved
        // index refuses (no push) rather than reading past the end.
        OpenValueMap<bool> ownedItemResolves{"pcu_get_owned_item_info's own per-item-index owned record",
                                             "spec-lua-api-behaviour.md Sec40.5"};
        uint64_t ownedInfoFoundCount = 0;
        uint64_t ownedInfoOobRefusedCount = 0; // HARDENED
        // pcu_is_current_item_worn (Sec40.6): "returns ZERO Lua values (not
        // a boolean) on one of its paths - a real return-arity irregularity
        // a caller must handle." Reproduced faithfully (not hardened away -
        // this is a return-shape quirk, not a memory-safety bug): the
        // "no item/slot resolved" path returns 0 values; a resolved slot
        // returns its worn boolean.
        uint64_t currentItemWornFoundCount = 0;
        uint64_t currentItemWornZeroValuesCount = 0; // CONFIRMED return-arity quirk, faithfully reproduced
    };
    PcuOwnedItemQueries& pcuOwnedItemQueries() { return pcuOwnedItemQueries_; }

    // The 7 AGG pcu_* names (Sec40.5/Sec40.6 - aggregate-only, no individual
    // per-function body given in spec scope): pcu_get_variants/pcu_get_
    // wear_options/pcu_report_outfits are the family's own "pure Lua-
    // callback iterators ... call back once per qualifying element and
    // return nothing themselves" (Sec40.6); pcu_is_item_needed_for_outfit/
    // pcu_remove_clothing/pcu_set_slots_empty/pcu_wear_outfit get only the
    // generic "the id argument means a different index space per function"
    // + "local player never null-checked" facts (Sec40.5). AGG discipline
    // (this batch's own explicit instruction): implement only what the
    // aggregate text supports - a thin, argument-consuming shell plus a
    // call counter each, no invented per-function logic or return shape
    // beyond what Sec40.5/40.6 actually states.
    struct PcuAggregateCounters {
        uint64_t getVariantsCallbackDispatchCount = 0;
        uint64_t getWearOptionsCallbackDispatchCount = 0;
        uint64_t reportOutfitsCallbackDispatchCount = 0;
        uint64_t isItemNeededForOutfitQueryCount = 0;
        uint64_t removeClothingCount = 0;
        uint64_t setSlotsEmptyCount = 0;
        uint64_t wearOutfitCount = 0;
    };
    PcuAggregateCounters& pcuAggregate() { return pcuAggregate_; }

    // pcu_bra_required / pcu_can_create_outfit / pcu_clear_item_new / pcu_
    // clear_obscured_slots / pcu_delete_outfit / pcu_get_item_defaults /
    // pcu_get_items_in_category / pcu_get_num_items_category (Sec43.2).
    struct PcuCreatorMisc {
        // pcu_bra_required (Sec43.2): "local player byte +0xa41 == 1 (the
        // gender byte, Sec16.8 - 'player is female'). No player null check
        // (crash shape)." HARDENED: reuses hasLocalPlayer(); reuses the
        // EXISTING PcrCustomizationState::genderByte (Sec16.8, already
        // modelled) rather than a parallel gender field.
        uint64_t braRequiredQueryCount = 0;
        // pcu_can_create_outfit (Sec43.2): "1 or 3 values (ok, or false+
        // title+body for 'naked'/empty block pool). Incomplete guard -
        // doesn't check the free-node pool or the 128-saved-outfit cap."
        // Reproduced faithfully (the incompleteness itself is CONFIRMED
        // real behaviour, not a bug this project hardens away - the actual
        // pcu_create_outfit call this "guard" precedes IS separately
        // hardened above).
        OpenValue<bool> blockPoolEmpty{"pcu_can_create_outfit's own free-block-pool-empty test",
                                       "spec-lua-api-behaviour.md Sec43.2"};
        uint64_t canCreateOutfitOkCount = 0;
        uint64_t canCreateOutfitEmptyPoolCount = 0;
        uint64_t clearItemNewCount = 0;
        uint64_t clearObscuredSlotsCount = 0;
        OpenValueMap<bool> jobQueueHasRoom{"pcu_clear_obscured_slots's own job-queue-full test (kinds 6/7)",
                                           "spec-lua-api-behaviour.md Sec43.2"};
        uint64_t clearObscuredSlotsQueueFullCount = 0; // CONFIRMED: a full queue silently drops the job (not a crash)
        uint64_t getItemDefaultsOkCount = 0;
        uint64_t getItemDefaultsOutOfRangeCount = 0; // CONFIRMED: -1 return on out-of-range (not a crash by itself; the variant-0 null read is data-gated, not reproduced)
        // pcu_get_items_in_category (Sec43.2 CRASH - real infinite-loop
        // hang in "owned instances" mode on a bad/missing callback name or
        // a full 256-record coroutine table). HARDENED: this project never
        // actually dispatches a real Lua coroutine (Sec43.1 - "mechanism
        // confirmed, no in-scope producer"), so the hang's own trigger
        // condition cannot occur here; the 3 reporting modes are counted,
        // never looped.
        uint64_t itemsInCategoryMode0Count = 0; // "Outfits" by name
        uint64_t itemsInCategoryMode1Count = 0; // "owned instances" (the real hang's own mode)
        uint64_t itemsInCategoryMode2Count = 0; // "variants"
        uint64_t numItemsCategoryOwnedCount = 0;    // arg2 == -1 branch
        uint64_t numItemsCategoryCatalogCount = 0;  // arg2 != -1 branch
    };
    PcuCreatorMisc& pcuCreatorMisc() { return pcuCreatorMisc_; }

    // ---- store_* family (Sec26/Sec38/Sec39/Sec49), b25 file ----
    struct StoreFrontendState {
        // store_dlc_make_purchase (Sec26.8): CONFIRMED hardcoded stub -
        // "always reports a purchase result of exactly 0, for any DLC id,
        // and never performs any actual purchase, lookup, or network
        // call." Reproduced faithfully (not a crash, not hardened).
        uint64_t dlcMakePurchaseCount = 0;
        // store_weapon_purchase_weapon (Sec26.7): reuses playerCash()/
        // purchaseLedger() for the grant path; the 9-entry category table
        // is CONFIRMED (Sec26.7's own literal table) and reproduced here.
        OpenValueMap<bool> weaponOwnedByCategoryIndex{
            "store_weapon_purchase_weapon's own per-(category,index) ownership predicate (0x005ee650)",
            "spec-lua-api-behaviour.md Sec26.7"};
        uint64_t weaponPurchaseGrantedCount = 0;
        uint64_t weaponPurchaseAchievementSweepCount = 0;
        uint64_t weaponPurchaseUnmatchedCategoryCount = 0;
        // store_weapon_change_weapon (Sec38.3 CRASH - "A category match
        // selects one of two fixed arrays by item index with no bounds
        // check at all - an arbitrary script-supplied index reads an
        // arbitrary nearby dword as an item pointer."). CHOSEN: reuses the
        // SAME 9-entry category table store_weapon_purchase_weapon
        // documents (both are weapon-store UI siblings; the real per-
        // category array sizes are not independently given in scope, so
        // this project test-populates "does (category,index) resolve" and
        // refuses an unresolved pair - HARDENING the unchecked index
        // without inventing a fixed capacity the spec doesn't give).
        OpenValueMap<bool> weaponChangeSlotResolves{"store_weapon_change_weapon's own per-(category,index) preview slot",
                                                     "spec-lua-api-behaviour.md Sec38.3"};
        uint64_t weaponChangeCount = 0;
        uint64_t weaponChangeOobRefusedCount = 0; // HARDENED
        bool postBgCovered = false; // store_weapon_process_post_bg_covered (Sec38.3) - Lua-side-only state, no engine consumer in scope

        // store_vehicle_* (Sec38.4).
        int32_t vehicleStoreMode = 0; // CONFIRMED: 0 = take-over-pending, any-other = store-current (sets mode to literal 1 on success, Sec38.4)
        uint64_t vehicleChangeModeCount = 0;
        uint64_t vehicleRetrieveCarAndExitCount = 0;
        OpenValue<bool> selectionLocksControls{"store_vehicle_selection_should_lock_controls backing query",
                                               "spec-lua-api-behaviour.md Sec38.4"};

        // Sec39.5 UI cluster.
        bool characterLineupLoaded = false; // store_character_lineup_loaded, 0x022ccc52
        OpenValue<int32_t> clothingStoreId{"store_clothing_get_store_id's own un-opened callee result (0x0080d810)",
                                           "spec-lua-api-behaviour.md Sec39.5"};
        uint64_t dlcIsOfferFreeCount = 0; // store_dlc_is_offer_free, NOOP (CONFIRMED always false)
        uint64_t dlcQueueIconCount = 0;   // store_dlc_queue_icon, NOOP (CONFIRMED no side effects, no return)

        // Sec52.1/T15 AGG.
        uint64_t vehicleNoVehicleExitCount = 0;
        uint64_t vehicleNotifyScreenCoveredCount = 0;

        // Sec49.1/T16.
        uint64_t cribInitCribGarageCount = 0;
        OpenValueMap<bool> galleryCharacterIndexResolves{"store_gallery_display_character's own bounds-checked list-index",
                                                         "spec-lua-api-behaviour.md Sec49.1"};
        uint64_t galleryDisplayCharacterAppliedCount = 0;
        uint64_t galleryDisplayCharacterOobRefusedCount = 0;
        uint64_t galleryDownloadShowListCount = 0;
        // store_gallery_download_show_list (Sec49.1 CRASH): "reuses the
        // exact null-dereference shape tranche 09 B.12 already found: the
        // dialog-pool allocator can return null on exhaustion." HARDENED:
        // same test-populated dialogPoolExhausted convention as
        // pcrCreationComplete() above.
        OpenValue<bool> downloadDialogPoolExhausted{"store_gallery_download_show_list's own dialog-pool allocator result",
                                                    "spec-lua-api-behaviour.md Sec49.1"};
        uint64_t galleryDownloadShowListPoolExhaustedRefusedCount = 0; // HARDENED
        uint64_t galleryUploadScheduledCount = 0; // store_gallery_upload_character - "a deferred-upload scheduler"
        uint64_t gangBeginExitCount = 0;
        uint64_t gangBgCoveredCount = 0;
        OpenValue<bool> gangUnlockedGate{"store_gang_is_unlocked's own player cash/unlock-bit gate", "spec-lua-api-behaviour.md Sec49.1"};
        uint64_t gangIsUnlockedQueryCount = 0;
        uint64_t strongholdUpgradeEndFlybyCount = 0;
        // store_stronghold_upgrade_end_flyby (Sec49.1 CRASH): "collects
        // every 'unlocked at this level' entry into a 256-slot stack array
        // with no cap on the match count." HARDENED: caps the collection at
        // 256 explicitly rather than reproducing the unbounded write.
        static constexpr int kUnlockedAtLevelCapacity = 256;
        uint64_t strongholdUpgradeEndFlybyOverflowRefusedCount = 0; // HARDENED
        OpenValueMap<bool> garageAllowDenylist{"store_vehicle_allow_garage's own 13-named-mission denylist",
                                               "spec-lua-api-behaviour.md Sec49.1"};
        uint64_t vehicleAllowGarageCount = 0;
        uint64_t vehicleDoReturnToCribCount = 0; // "screen-stack pop with a confirmed-dead secondary branch"
    };
    StoreFrontendState& storeFrontend() { return storeFrontend_; }

    // ---- garage_* family (Sec23.10/Sec41.7/Sec53.4), b25 file ----
    struct GarageEntry {
        bool present = false;
        int32_t garageType = 0;
    };
    struct GarageFrontendState {
        static constexpr int32_t kEntryCapacity = 168; // CONFIRMED 168-slot array (Sec53.4, correcting Sec27.9's "unbounded" note)

        // "Currently selected garage vehicle" - two-part identity pair
        // (0x014a1d28/0x014a1d2c, Sec23.10), DISTINCT from GaragePreview's
        // own 0x014a1d1c/0x014a1a80[index] above (an adjacent but separate
        // mechanism, Sec23.10's own "new global pair, not previously
        // catalogued") - test-populated (no real garage/vehicle-pool
        // loader in scope).
        bool hasSelectedVehicle = false;
        OpenValue<double> selectedVehicleRepairCost{"garage's own currently-selected-vehicle repair cost (0x00a9bbe0)",
                                                    "spec-lua-api-behaviour.md Sec23.10"};
        uint64_t repairVehicleChargedCount = 0;
        double repairVehicleTotalCharged = 0.0;
        // garage_repair_vehicle (Sec41.7 CRASH): "if that handle is unset,
        // stale, dead, or not actually a vehicle, the vehicle pointer is
        // left null - and the cost-calculation callee dereferences it
        // unconditionally." HARDENED: refuses (no charge, no repair) when
        // hasSelectedVehicle is false, matching the sibling garage handler
        // the spec itself cites as the correct fix template.
        uint64_t repairVehicleNoSelectionRefusedCount = 0; // HARDENED

        std::array<GarageEntry, kEntryCapacity> entries{};
        int32_t entryCount = 0;       // CONFIRMED "count" field garage_back_to_customize's own sibling bounds correctly (< count)
        int32_t selectedEntryIndex = -1;

        uint64_t clearNewCount = 0;
        // garage_clear_new (Sec53.4 CRASH): "the bound check accepts
        // index == count, not just indices strictly below it." HARDENED:
        // uses a strict < bound, matching the sibling garage_back_to_
        // customize's own correct form (the spec's own cited fix template).
        uint64_t clearNewOobRefusedCount = 0; // HARDENED
        uint64_t backToCustomizeCount = 0;
        uint64_t backToCustomizeOobRefusedCount = 0;
        OpenValueMap<bool> canCustomizeMissionDenylist{"garage_can_customize's own 12-mission denylist (0x005f87c0)",
                                                       "spec-lua-api-behaviour.md Sec53.4"};
        uint64_t canCustomizeQueryCount = 0;
        uint64_t canDeleteQueryCount = 0;
        uint64_t removeVehicleCount = 0;
        // garage_remove_vehicle (Sec53.4 CRASH - "a real double-release
        // logic defect ... never clears the 'selected entry' global ... A
        // second call before a fresh selection ... hands the same record
        // back to its free pool a second time."). HARDENED: refuses a
        // second call when selectedEntryIndex is already -1 (cleared after
        // the first successful removal, unlike the original).
        uint64_t removeVehicleDoubleReleaseRefusedCount = 0; // HARDENED
        uint64_t vehicleLoadPendingQueryCount = 0;
    };
    GarageFrontendState& garageFrontend() { return garageFrontend_; }

    // ---- crib_* family (Sec23.19/Sec57.3) + building_purchase_is_crib_
    // being_purchased (Sec59, AGG), b25 file ----
    struct CribFrontendState {
        // crib_has_garage_with_vehicles_of_type (Sec23.19): player's current
        // crib record (+0x209c, corrected per Sec23.19's own in-place
        // correction) - test-populated (no real crib/garage-vehicle-list
        // loader in scope).
        bool hasCribRecord = false;
        OpenValueMap<int32_t> garageVehicleCountByType{"crib garage-vehicle-list count by vehicle type (0x005facf0)",
                                                        "spec-lua-api-behaviour.md Sec23.19"};
        uint64_t hasGarageWithVehiclesTrueCount = 0;
        uint64_t hasGarageWithVehiclesFalseCount = 0;

        // crib_get_cash_stash (Sec57.3): naming trap - "returns no value at
        // all; it instead collects a stashed-cash global into the player's
        // cash balance and zeroes the stash" - reproduced faithfully
        // (0 Lua values, not the stash amount); reuses playerCash().
        double cashStash = 0.0;
        uint64_t cashStashCollectedCount = 0;
        uint64_t cashStashNoLocalPlayerForwardCount = 0; // CONFIRMED: forwarded (and discarded) when not locally applicable - not a crash

        OpenValue<bool> garageDisabled{"crib_is_garage_disabled backing flag", "spec-lua-api-behaviour.md Sec57.3"};
        OpenValue<bool> isStronghold{"crib_is_stronghold's own upgrade-level hash set (+0x148/+0x14c/+0x150)",
                                     "spec-lua-api-behaviour.md Sec57.3"};

        // crib_purchase_purchase_crib (Sec57.3 CRASH - "if nothing is
        // validly selected ... the crib pointer is left null and the VERY
        // NEXT INSTRUCTION reads price through it ... the player's cash is
        // ALWAYS DEDUCTED BEFORE the function checks whether the crib is
        // already owned."). Test-populated selection (no real crib-
        // selection/purchase-screen loader in scope). HARDENED: refuses
        // (no charge at all) when nothing is validly selected - the charge-
        // before-ownership-check quirk ITSELF is preserved faithfully for a
        // valid selection (it's a logic defect, not a memory-safety one;
        // the task's hardening mandate targets the null-pointer crash, not
        // this ordering).
        bool hasSelectedPurchaseCrib = false;
        OpenValueMap<double> cribPurchasePrice{"crib_purchase_purchase_crib's own selected-crib price field",
                                               "spec-lua-api-behaviour.md Sec57.3"};
        bool cribAlreadyOwned = false;
        uint64_t purchaseCribChargedCount = 0;
        double purchaseCribTotalCharged = 0.0;
        uint64_t purchaseCribNoSelectionRefusedCount = 0; // HARDENED (named CRASH)

        uint64_t purchasingUnlockCount = 0;          // crib_purchasing_unlock, AGG
        uint64_t isCribBeingPurchasedQueryCount = 0; // building_purchase_is_crib_being_purchased, AGG
    };
    CribFrontendState& cribFrontend() { return cribFrontend_; }

    // ---- gang_customization_* family (Sec53.1/Sec56.1), b25 file - 4 of
    // the batch's own 4 NAMED memory-safety findings live here. ----
    struct GangCustomizationState {
        // --- Vehicle family: 3-slot array, CONFIRMED bound (Sec53.1: "a
        // binding layer must reject any slot outside 0-2 for every
        // gang_customization_*_vehicle name - the single clearest 'accept
        // exactly this tiny range' fix this whole series has produced"). ---
        static constexpr int kVehicleSlotCount = 3;
        std::array<int32_t, kVehicleSlotCount> vehicleSlotItemIndex{{-1, -1, -1}}; // -1 = empty
        // Preview-release baseline. -1/unset + previewedVehicleBaselineSet
        // == false = "nothing previewed yet" (CONFIRMED zero-fill default -
        // this is the ONE case Sec56.1's 2nd crash fires on: revert_vehicle
        // running before the baseline is EVER set writes null into the slot
        // and hands it to a null-unsafe callee - see revert_vehicle's own
        // doc comment). previewedVehicleBaselineStale models a SEPARATE,
        // not-yet-hardened bug (Sec53.1's OWN 2nd bug, in select_vehicle's
        // preview-release path: "used to resolve, now doesn't" keeps using
        // a zeroed working pointer anyway and writes a flag bit through
        // null+0x16c0) - CONFIRMED a different function and a different
        // condition from revert_vehicle's null-baseline crash; revert_
        // vehicle must NOT key its own guard on this field (a prior version
        // of this stub did, incorrectly - see its doc comment).
        int32_t previewedVehicleBaselineIndex = -1;
        bool previewedVehicleBaselineSet = false;
        bool previewedVehicleBaselineStale = false; // reserved for the SEPARATE select_vehicle preview-release bug above - not yet hardened anywhere, not read by revert_vehicle
        uint64_t selectVehicleCount = 0;
        uint64_t selectVehicleOobRefusedCount = 0;          // HARDENED (named finding, Sec53.1)
        uint64_t confirmVehicleCount = 0;
        uint64_t confirmVehicleOobRefusedCount = 0;         // HARDENED (Sec53.1/Sec56.1)
        uint64_t revertVehicleCount = 0;                    // genuine, real reverts ONLY - never incremented on a refusal
        uint64_t revertVehicleOobRefusedCount = 0;          // HARDENED (Sec53.1/Sec56.1)
        uint64_t revertVehicleNullBaselineRefusedCount = 0; // HARDENED (THE NAMED FIX, Sec56.1's 2nd crash: baseline never set at all -> null write into the slot -> null-unsafe preview-vehicle-spawner callee at +0x24)

        // --- Gang sign family: position-to-record map, bound genuinely
        // OPEN (Sec56.1's own residual "the declared size of the sign
        // position-to-record map" item - NOT invented here) - test-
        // populated resolver, refused when a position does not resolve.
        // Mode 1 of select_gang_sign is a CONFIRMED DETERMINISTIC null-
        // vtable call in the original (not data-gated: "every mode-1 call
        // crashes, full stop") - refused unconditionally here (HARDENED,
        // named finding), never reproduced under any input. ---
        OpenValueMap<bool> signPositionResolves{"gang-sign position-to-record map (declared size OPEN, Sec56.1)",
                                                "spec-lua-api-behaviour.md Sec56.1"};
        int32_t confirmedSignPosition = -1;
        bool confirmedSignPositionSet = false;
        uint64_t selectGangSignMode0Count = 0;
        uint64_t selectGangSignMode0UnresolvedRefusedCount = 0; // HARDENED
        uint64_t selectGangSignMode1RefusedCount = 0;           // HARDENED (named finding - deterministic crash, never reproduced under any input)
        uint64_t selectGangSignUnhandledModeCount = 0;
        uint64_t confirmGangSignCount = 0;
        uint64_t confirmGangSignUnresolvedRefusedCount = 0; // HARDENED (named finding, Sec56.1: "a single out-of-range call can corrupt a save slot")
        uint64_t revertGangSignCount = 0;
        uint64_t revertGangSignNoPriorBuildRefusedCount = 0; // HARDENED (deferred null shape, Sec56.1)
    };
    GangCustomizationState& gangCustomization() { return gangCustomization_; }

    // =======================================================================
    // Wave 2 batch W2-6 (2026-10-08), spec-lua-api-behaviour.md UI-frontend
    // hardening batch - 143 names split across lua_spec_confirmed_stubs_b17_
    // wave2_ui_frontend_a.cpp (66 names: Sec19/Sec23/Sec26 + lobby/coop/
    // session/steam `game_*`) and _b18_wave2_ui_frontend_b.cpp (77 names:
    // menus/pause/dialog/cellphone/cell/save/machinima/completion). State
    // grouped by theme below; see each stub's own doc comment in those two
    // files for the per-name CONFIRMED/OPEN/HYPOTHESIS/CHOSEN citation.
    // Hardening counters (`...GuardCount`) are test-observable only, not
    // real engine fields - they count how often this host's own deliberate
    // "fail closed instead of reproducing the real engine's crash" branch
    // fired, per the orchestrator's standing hardening policy.
    // =======================================================================

    // --- Sec19.7/Sec23.14 game_interface_effect_begin/_end -----------------
    struct InterfaceEffectState {
        std::string activeName;
        double param1 = 0.0;
        double param2 = 0.0;
        bool forced = false;
        uint64_t beginCount = 0;
        uint64_t endCount = 0;
    };
    InterfaceEffectState& interfaceEffect() { return interfaceEffect_; }

    // --- Sec19.6 online_validator_set_checking ------------------------------
    bool& onlineValidatorChecking() { return onlineValidatorChecking_; }

    // --- Sec19.9/Sec44.3/Sec26.21 game_coop_get_starting_coop/_syslink,
    // game_is_syslink: all three CONFIRMED to read the SAME two bytes
    // game_coop_start_new_live/_syslink already write (coopLiveSessionActive/
    // coopSyslinkSessionActive, above) - no new state needed.

    // --- Sec26.22 cellphone_choose_vehicle ----------------------------------
    struct CellphoneVehicleDelivery {
        int32_t lastSelectedIndex = -1;
        uint64_t selectCount = 0;
        uint64_t announceCount = 0;
    };
    CellphoneVehicleDelivery& cellphoneVehicleDelivery() { return cellphoneVehicleDelivery_; }

    // --- Sec26.17 game_audio_play: a name/id "resolves" through the same
    // never-populated-registry convention this project already uses for
    // Wwise-backed names (ambientEmitterKnown) - a name this host has never
    // had registered honestly takes the real CONFIRMED "no match" path. A
    // raw numeric id always resolves (no hash lookup needed).
    bool audioEventKnown(const std::string& name) const { return audioEventRegistry_.count(name) != 0; }
    void registerAudioEventForTesting(const std::string& name) { audioEventRegistry_.insert(name); }

    // --- Sec26.4/Sec26.5 game_lobby_get_remote_player_name /
    // Completion_get_other_player_name: CONFIRMED same +0xc2 name field,
    // reached via two different context objects this project models as one.
    struct RemotePlayerInfo {
        bool present = false;
        std::string name;
    };
    RemotePlayerInfo& remotePlayer() { return remotePlayer_; }

    // --- Sec26.6 completion_screen_finished ---------------------------------
    bool& completionScreenFinishedFlag() { return completionScreenFinishedFlag_; }
    // (reuses the existing screenFadeRequest()/Sec26.24 mechanism for its own fade call)

    // --- Sec26.19 game_set_equipped_grenade: -1 = none; CONFIRMED internal
    // slots 0-3 map to Lua-facing ids 8-11.
    int32_t& equippedGrenadeSlot() { return equippedGrenadeSlot_; }

    // --- Sec26.18 game_use_radial_menu_item: slot 12 and any out-of-range
    // slot are CONFIRMED to always report 1.0; slots 0-7 dispatch through an
    // OPEN internal (0x00600720's own success/failure) - OpenValue until a
    // test sets it.
    OpenValue<bool>& radialMenuQuickItemSucceeds() { return radialMenuQuickItemSucceeds_; }
    uint64_t& radialMenuSlot12Count() { return radialMenuSlot12Count_; }

    // --- Sec26.20 game_vkeyboard_input ---------------------------------------
    struct VirtualKeyboardState {
        bool open = false;
        uint64_t requestCount = 0;
        std::string lastTitle;
    };
    VirtualKeyboardState& virtualKeyboard() { return virtualKeyboard_; }
    OpenValue<bool>& virtualKeyboardCloseResult() { return virtualKeyboardCloseResult_; }

    // --- Sec23.13/Sec44.2 game_is_connected_to_service/_internet: CONFIRMED
    // the SAME handler ("logged on to Steam"), distinct from the existing,
    // weaker game_is_signed_in ("interface exists").
    OpenValue<bool>& steamLoggedOn() { return steamLoggedOn_; }

    // --- Sec23.11/Sec41.2 game_stop_find_syslink_servers/_start_... --------
    struct SyslinkSearch {
        bool active = false;
        uint64_t startCount = 0;
        uint64_t stopCount = 0;
        uint64_t guardedNoopCount = 0;
    };
    SyslinkSearch& syslinkSearch() { return syslinkSearch_; }

    // --- Sec41.2/Sec56.3 Steam overlay/url openers (no validation, per spec) -
    struct SteamOverlayCounters {
        uint64_t friendsOverlayCount = 0;
        std::string lastOverlayArg;
        uint64_t urlOverlayCount = 0;
        std::string lastUrl;
        uint64_t dlcOverlayCount = 0;
    };
    SteamOverlayCounters& steamOverlay() { return steamOverlay_; }

    // --- Sec41.2/Sec44.5 get_current_difficulty/game_difficulty_select: CONFIRMED
    // file-default 1; the real setter only accepts an unsigned value < 3.
    int32_t& difficultyLevel() { return difficultyLevel_; }

    // --- spec-player-runtime.md Sec15.3a (wave 6, 2026-10-09): the damage
    // sink 0x0096fa20 applies "a difficulty-based scalar when the
    // instigator is player-class" on top of the caller's own base amount.
    // spec-tables-progression.md Sec14.7 gives the real, empirically-
    // decoded Damage_Dealt_Mult values for difficulty_levels.xtbl's FIRST
    // <Dynamic_Difficulty> block ("Set A"): L0/L1/L2 = 1.1/1.0/0.75 (easy
    // deals more damage, hard deals less - the expected shape). Set A is
    // used here, not Set B (whose own Damage_Dealt_Mult, 1.0/0.9/0.6, is
    // also harsher at L2), per this project's own standing "single player
    // only" simplification (controlLockGatePasses's own doc comment) -
    // Sec14.7 itself leaves the set-A-vs-set-B semantic split as HYPOTHESIS
    // only ("consistent with, but not confirmed as, a single-player-vs-
    // co-op split"), so this is a CHOSEN pick between two real, CONFIRMED-
    // decoded tables, not an invented value. difficultyLevel_ is already
    // kept in {0,1,2} by
    // game_difficulty_select's own setter (Sec44.5: "only accepts unsigned
    // values below 3"); the clamp below is a defensive no-op against a test
    // writing difficultyLevel() directly out of range.
    float damageDealtDifficultyMultiplier() const {
        static constexpr float kSetADamageDealtMult[3] = {1.1f, 1.0f, 0.75f};
        int32_t lvl = difficultyLevel_;
        if (lvl < 0 || lvl > 2) lvl = 1; // defensive only - see doc comment above
        return kSetADamageDealtMult[lvl];
    }

    // --- Sec41.6 get_get_key_names_for_axis_action: CONFIRMED the real
    // engine returns uninitialized stack memory for an unknown axis-action
    // name (this host has no 34-entry table to match against, so every call
    // takes that path) - CHOSEN (hardened divergence): two empty strings
    // instead of leftover stack content, counted.
    uint64_t& axisActionUnknownNameGuardCount() { return axisActionUnknownNameGuardCount_; }

    // --- Sec41.4 hud_diversion_remove_callback: CHOSEN simplification - the
    // real table only has 4 valid slot-occupancy counters for 4 specific
    // (unnamed by spec) diversion types; this project tracks one
    // floored-at-zero counter per type id actually used by a test/script,
    // which is behaviourally equivalent for every type this host is ever
    // given (an id this host was never told about was never incremented
    // either, so decrementing it floors at zero exactly as spec requires).
    std::unordered_map<int32_t, int32_t>& diversionSlotOccupancy() { return diversionSlotOccupancy_; }

    // --- Sec41.2 main_menu_new_game/_horde_start/_set_coop_menu_type -------
    struct MainMenuNewGame {
        std::string worldName = "sr3_city"; // CONFIRMED hardcoded literal
        bool hordeMode = false;
        uint64_t newGameCount = 0;
        uint64_t hordeStartCount = 0;
    };
    MainMenuNewGame& mainMenuNewGame() { return mainMenuNewGame_; }
    int32_t& coopMenuType() { return coopMenuType_; } // raw byte store; 2 further UI dispatch codes OPEN

    // --- Sec44.2/Sec56.6 coop kick/gamercard + the Sec44.2 project-wide
    // wide-sprintf-wrapper latent-overflow hardening note (this project
    // formats via std::string/snprintf, never a fixed 256-wide-char stack
    // buffer, so the real overflow cannot reproduce here by construction;
    // the counter below is test-observable evidence the bounded path ran).
    struct CoopKickState {
        uint64_t confirmDialogShownCount = 0;
        uint64_t kickedCount = 0;
        uint64_t callbackFiredCount = 0;
        bool lastCallbackArg = false;
        uint64_t gamercardOverlayCount = 0;
        uint64_t wideSprintfBoundGuardCount = 0;
    };
    CoopKickState& coopKick() { return coopKick_; }

    // --- Sec44.5 game_hud_hide: CONFIRMED reference-counted (clamped at 0);
    // calling with no argument counts as a show, not a no-op.
    int32_t& hudHideRefCount() { return hudHideRefCount_; }

    // --- Sec44.5 game_is_autosaving: CHOSEN simplification - one flag
    // standing in for the real 3-test combination (the real native itself
    // already omits one of the three, per spec; this project tracks the
    // net observable result directly rather than re-deriving all 3 tests).
    bool& autosaveInProgressSimplified() { return autosaveInProgressSimplified_; }

    // --- Sec44.2 game_is_joinable: CONFIRMED real logic quirk (not a crash) -
    // free-slot count tested "!= 0" rather than "> 0"; reproduced exactly,
    // not fixed, since it is not memory-unsafe.
    struct JoinableState {
        OpenValue<int32_t> capacity{"session capacity (joinable slot test)", "spec-lua-api-behaviour.md Sec44.2"};
        int32_t currentPlayerCount = 0;
        int32_t reservedSlots = 2; // CONFIRMED "two reserved-slot checks"
    };
    JoinableState& joinable() { return joinable_; }

    // --- Sec44.2 game_join_syslink_game: CONFIRMED 8-bit index compare -
    // index 257 silently matches the same record as index 1.
    uint64_t& syslinkJoinCount() { return syslinkJoinCount_; }
    int32_t& syslinkJoinLastIndex() { return syslinkJoinLastIndex_; }

    // --- Sec44.3 lobby map/char selection, horde data, ready state ---------
    struct HordeLobbyState {
        int32_t selectedMapIndex = -1;
        int32_t committedMapIndex = -1;
        bool continueFromSavedWave = false;
        std::unordered_map<int32_t, int32_t> savedProgressByMap; // OPEN data; test-populated
        uint64_t spuriousRestartWarningCount = 0;
        int32_t selectedCharIndex = -1;
        int32_t stagedCharIndexHost = -1;
        uint64_t readyStateCountdownStartedCount = 0;
        uint64_t readyStateCancelCount = 0;
        uint64_t finishedLoadingCount = 0;
    };
    HordeLobbyState& hordeLobby() { return hordeLobby_; }

    // --- Sec44.4/Sec56.5 machinima family (game_machinima_*, cinema_editor_
    // create_camera_zone/_save_clip, cell_machinima_is_recording): shared
    // path-builder defect - the real builder leaves its output buffer
    // unwritten on 3 failure exits and NO caller checks; this project's
    // hardened divergence is to fail closed instead (every caller checks
    // `ok` before using `path`). OPEN whether the environment (process-
    // token/folder lookup/creation) succeeds; CHOSEN default true so normal
    // operation proceeds, with a test-only toggle forcing the failure path.
    struct MachinimaPathResult {
        bool ok = false;
        std::string path;
    };
    OpenValue<bool>& machinimaPathBuilderSucceeds() { return machinimaPathBuilderSucceeds_; }
    MachinimaPathResult buildMachinimaClipPath(const std::string& clipName);
    struct MachinimaState {
        bool recording = false;
        bool playbackActive = false;
        uint64_t playbackEnterCount = 0;
        uint64_t recordCount = 0;
        uint64_t pathBuilderFailureGuardCount = 0;
        std::unordered_map<std::string, bool> clips; // name -> exists (in-memory stand-in for the clips folder)
    };
    MachinimaState& machinima() { return machinima_; }

    // --- Sec56.3 game_pause_map_filter --------------------------------------
    struct PauseMapFilterState {
        std::string currentFilterId = "PFT_FILTER_ALL"; // CHOSEN default name (one of 8 public ids)
        bool locked = false; // CONFIRMED: a mission-imposed lock (real id >= 0x80) the setter ignores
        uint64_t setCount = 0;
        uint64_t getCount = 0;
    };
    PauseMapFilterState& pauseMapFilter() { return pauseMapFilter_; }

    // --- Sec54.4/Sec56.4 game_proto_select: CRASH-shaped in the real engine
    // (an invalid argument corrupts the caller's own saved-register stack
    // spill). This host never touches raw registers, so the hardened
    // divergence is simply to refuse the invalid-argument case instead of
    // selecting anything, counted, while reproducing "lowest of 0/1/2 not
    // named by either argument" for valid 1-3 arguments.
    struct ProtoSelectState {
        int32_t selected = -1;
        uint64_t invalidArgGuardCount = 0;
    };
    ProtoSelectState& protoSelect() { return protoSelect_; }

    // --- Sec56.2 game_record_mode_can_encode/_enable/_show_hud/
    // game_record_set_quality_level: CONFIRMED the family of 4 is 3 no-op +
    // 1 constant-false, but NOT which of the 4 names is which - an
    // irreducible ambiguity this project refuses rather than guesses (see
    // each stub's own OPEN refusal). Counter is test-observable only.
    uint64_t& gameRecordAmbiguousRefusalCount() { return gameRecordAmbiguousRefusalCount_; }

    // --- Sec56.3 game_request_game_terminate: CONFIRMED once-only exit latch -
    bool& gameTerminateRequested() { return gameTerminateRequested_; }

    // --- Sec56.3 game_use_imperial_units: CONFIRMED to follow the real
    // Windows user locale (GetUserDefaultLCID/GetLocaleInfoW) with no in-
    // game option. Implemented via the real Win32 API on _WIN32 builds
    // (same real-header convention common.h already uses for
    // GetKeyNameTextW - a standard, publicly-documented OS function, not
    // reverse-engineered content); a test-only override takes precedence
    // when set, so tests do not depend on the CI machine's own locale.
    OpenValue<bool>& imperialUnitsOverrideForTesting() { return imperialUnitsOverrideForTesting_; }

    // --- Sec55.1 game_is_going_to_main_menu: a simplified game-state stack -
    struct GameStateStack {
        int32_t currentTop = 2;      // CHOSEN default (OPEN what 2/5/6 really name)
        int32_t projectedTop = -1;   // -1 = no projection queued
        std::vector<int32_t> queuedOps;
        uint64_t overflowGuardCount = 0; // hardening: the real 16-dword buffer is unbounded; this host bounds it
    };
    GameStateStack& gameStateStack() { return gameStateStack_; }

    // --- Sec55.1 game_get_game_play_mode/_pending_game_play_mode -----------
    struct GamePlayMode {
        std::string current = "Normal"; // CONFIRMED file-default
        std::string pending;            // empty => "None"
    };
    GamePlayMode& gamePlayMode() { return gamePlayMode_; }

    // --- Sec55.1 game_get_coop_ping -----------------------------------------
    OpenValue<uint32_t>& coopPing() { return coopPing_; }

    // --- Sec55.2 game_cancel_mission: 3 distinct crash paths in the real
    // engine (null mission pointer; dialog-pool exhaustion; unhandled type) -
    // all 3 hardened to a safe no-op here, each counted.
    struct MissionCancelState {
        OpenValue<int32_t> inProgressType{"active mission in-progress type", "spec-lua-api-behaviour.md Sec55.2"};
        bool missionPointerValid = false; // CHOSEN: whether a real mission object exists for non-diversion types
        uint64_t nullMissionGuardCount = 0;
        uint64_t dialogPoolExhaustedGuardCount = 0;
        uint64_t unhandledTypeGuardCount = 0;
        uint64_t diversionCancelCount = 0;
        uint64_t confirmDialogOpenCount = 0;
    };
    MissionCancelState& missionCancel() { return missionCancel_; }
    // Shared 4-slot dialog pool (Sec27.11 cross-reference) - also consumed by
    // main_menu_matchmaking and cell_cheats_activate_cheat below.
    int32_t& dialogPoolSlotsInUse() { return dialogPoolSlotsInUse_; }

    // --- Sec57.1 dialog_box_create_internal / dialog_box_set_result /
    // dialog_open_pause_display / dialog_close_finished /
    // dialog_option_accept_kbd_input/_end_kbd_input / dialog_pause_unpause -
    // dialog_box_create_internal is one of this batch's two OWN named
    // memory-safety findings (callback-name bound to 63 chars; nil title/
    // body/callback handled) - see the stub's own doc comment for the fix.
    struct DialogUiState {
        bool pauseDisplayOpen = false;
        uint64_t pauseDisplayOpenCount = 0;
        int32_t currentOptionIndex = 0;
        bool closeLatchSet = false;
        uint64_t closeFinishedCount = 0;
        bool kbdEditing = false;
        int32_t kbdCursorPos = 0;
        std::string kbdCommittedText;
        uint64_t kbdAcceptCount = 0;
        uint64_t kbdEndCount = 0;
        std::array<int32_t, 4> slotShowingId{{-1, -1, -1, -1}}; // test-only fixture
        OpenValue<bool> optionApplyResult{"dialog_box_set_result option-apply outcome (0x007c1c80)",
                                           "spec-lua-api-behaviour.md Sec23.18"};
        uint64_t setResultSelfConsistencyFailCount = 0;
        uint64_t pauseUnpauseCount = 0;
        uint64_t createCount = 0;
        uint64_t createNilArgGuardCount = 0;
        uint64_t createCallbackNameTruncatedCount = 0;
        std::string lastCreateCallbackName;
        std::string lastCreateTitle;
    };
    DialogUiState& dialogUi() { return dialogUi_; }
    void setDialogSlotShowingIdForTesting(int slot, int32_t id) {
        if (slot >= 0 && slot < 4) dialogUi_.slotShowingId[static_cast<size_t>(slot)] = id;
    }

    // --- Sec59.3/59.1 cell_* cheat menu/UI queries/music toggle -------------
    struct CellUiState {
        bool allowCamera = true; // CHOSEN default (AGG - collectively described only)
        int32_t pendingCheatIndex = -1;
        bool cheatNoConfirmNeeded = false;
        OpenValue<uint32_t> unlockedCheatCount{"cell cheat-menu unlocked-cheat count", "spec-lua-api-behaviour.md Sec59.3"};
        uint64_t activateNullRecordGuardCount = 0;
        uint64_t unlockCount = 0;
        bool phoneUiDocType1Open = false;
        bool phoneUiDocType2Open = false;
        bool mapDisabledExtraGate = false;
        std::unordered_map<int32_t, bool> stationDisabled; // 1-based station -> hidden/disabled
        int32_t listeningStation = -1;
        uint64_t toggleStationNoopCount = 0;
    };
    CellUiState& cellUi() { return cellUi_; }

    // --- Sec51.2/Sec51.3 playlist_play_track/_stop_track: CONFIRMED null-
    // read crash on an out-of-range/no-songs-loaded index - hardened here.
    struct PlaylistState {
        OpenValue<uint32_t> loadedSongCount{"playlist loaded-song count", "spec-lua-api-behaviour.md Sec51.2"};
        int32_t currentTrack = -1;
        bool playing = false;
        uint64_t nullSongGuardCount = 0;
        uint64_t stopCount = 0;
    };
    PlaylistState& playlist() { return playlist_; }

    // --- Sec51.2 save_game_wrap_up/save_system_change_device/_delete_game/
    // _set_operation (reuses the existing saveSystemUi() where relevant) ---
    struct SaveGameExtra {
        uint64_t wrapUpCount = 0;
        uint64_t changeDeviceCount = 0;
        bool jobInProgress = false; // gates delete
        OpenValue<bool> steamUserInterfaceAvailable{"save-system Steam user interface availability",
                                                     "spec-lua-api-behaviour.md Sec51.2"};
        uint64_t deleteDroppedCount = 0;
        uint64_t deleteAppliedCount = 0;
        int32_t lastOperation = 0; // CONFIRMED dead write (nothing reads it)
    };
    SaveGameExtra& saveGameExtra() { return saveGameExtra_; }

    // --- Sec51.2 sb_saintsbook_is_available/_select_chop_shop_vehicle/
    // _select_hitman_target -------------------------------------------------
    struct SaintsbookState {
        int32_t activeHitmanTarget = -1;
        int32_t activeChopShopVehicle = -1;
        uint64_t hitmanReselectNoopCount = 0;
        uint64_t chopShopDeselectCount = 0;
        OpenValue<bool> eligibilityTerm1{"sb_saintsbook_is_available eligibility term 1", "spec-lua-api-behaviour.md Sec51.2"};
        OpenValue<bool> eligibilityTerm2{"sb_saintsbook_is_available eligibility term 2", "spec-lua-api-behaviour.md Sec51.2"};
    };
    SaintsbookState& saintsbook() { return saintsbook_; }
    // CONFIRMED: the real 3rd eligibility term reads a global nothing in the
    // binary ever writes - permanently dead. Not an OpenValue (there is
    // nothing to ever set): always false.
    bool saintsbookThirdTermDeadGlobal() const { return false; }

    // --- Sec50.1 main_menu_restart_video/_video_has_stopped: CONFIRMED a
    // stale-flag-then-null-call crash (the "playing" flag is set even when
    // video creation failed) - hardened to only set it on success.
    struct MainMenuVideo {
        bool playingFlag = false;
        uint64_t restartCount = 0;
        uint64_t nullVideoGuardCount = 0;
        uint64_t stopHookCount = 0;
        uint64_t startHookCount = 0;
    };
    MainMenuVideo& mainMenuVideo() { return mainMenuVideo_; }
    OpenValue<bool>& mainMenuVideoCreateSucceeds() { return mainMenuVideoCreateSucceeds_; }

    // --- Sec50.4 msn_killbane_selected_option -------------------------------
    int32_t& killbaneSelectedOption() { return killbaneSelectedOption_; }
    uint64_t& killbaneCallbackFiredCount() { return killbaneCallbackFiredCount_; }

    // --- Sec50.2 object_indicator_init_lua ----------------------------------
    struct ObjectIndicatorInitLua {
        bool oneTimeSetupDone = false;
        uint64_t elementCacheCount = 0;
        OpenValue<bool> callbackPoolAvailable{"object_indicator_init_lua callback pool availability",
                                               "spec-lua-api-behaviour.md Sec50.2"};
        uint64_t poolExhaustedGuardCount = 0;
    };
    ObjectIndicatorInitLua& objectIndicatorInitLua() { return objectIndicatorInitLua_; }

    // --- Sec50.4 options_display_pc_set_options -----------------------------
    struct PcOptionsState {
        int32_t resolutionWidth = 0, resolutionHeight = 0;
        bool vsync = false, fullscreen = false;
        int32_t msaa = 0, anisotropic = 0, lightingDetail = 0, shadowDetail = 0, sceneDetail = 0;
        int32_t reflections = -1, postProcessing = -1, ambientOcclusion = -1; // CONFIRMED: out-of-range -> -1, no range check
        uint64_t applyCount = 0;
    };
    PcOptionsState& pcOptions() { return pcOptions_; }

    // --- Sec50.1 pause_menu_cant_retry_because_host_in_creation/
    // _cat_mouse_active/_common_top_finished_loading -------------------------
    bool& pauseMenuHostInCharacterCreation() { return pauseMenuHostInCharacterCreation_; }
    bool& catMouseMinigameActive() { return catMouseMinigameActive_; }
    uint64_t& pauseMenuCommonTopFinishedLoadingCount() { return pauseMenuCommonTopFinishedLoadingCount_; }

    // --- Sec54.1 main_menu_matchmaking/_continue: CONFIRMED null-dialog-pool
    // crash (reuses dialogPoolSlotsInUse() above), hardened.
    struct MainMenuMatchmaking {
        uint64_t startCount = 0;
        uint64_t nullDialogGuardCount = 0;
    };
    MainMenuMatchmaking& mainMenuMatchmaking() { return mainMenuMatchmaking_; }
    uint64_t& mainMenuContinueCount() { return mainMenuContinueCount_; }

    // --- Sec53.5 get_char_in_string/get_localized_string_for_tag/
    // get_world_income_dollars -----------------------------------------------
    uint64_t& getCharInStringNullGuardCount() { return getCharInStringNullGuardCount_; }
    OpenValueMap<std::string>& localizedStringForTag() { return localizedStringForTag_; }
    OpenValue<double>& worldIncomeDollars() { return worldIncomeDollars_; }

    // --- Sec46.6 vint_options_remap_reset_bindings --------------------------
    uint64_t& vintOptionsRemapResetCount() { return vintOptionsRemapResetCount_; }

    // --- Sec51.5 pause_menu_horde_mode_new/_retry/southpaw queries ---------
    struct PauseMenuHorde {
        uint64_t newCount = 0;
        uint64_t retryCount = 0;
    };
    PauseMenuHorde& pauseMenuHorde() { return pauseMenuHorde_; }
    bool& southpawControlScheme() { return southpawControlScheme_; }
    bool& vehicleSouthpawControlScheme() { return vehicleSouthpawControlScheme_; }

    // --- Sec42.1 pause_map_zoom/_add_bookmark/_drag_map/
    // pause_menu_change_control_scheme --------------------------------------
    struct PauseMapZoomState {
        double zoomFactor = 1.0;
        double zoomMin = 0.1;
        double zoomMax = 4.0;
        double panX = 0.0, panY = 0.0;
        uint64_t zoomCount = 0;
    };
    PauseMapZoomState& pauseMapZoomState() { return pauseMapZoomState_; }
    struct PauseMapBookmarks {
        int32_t selectedBookmark = -1; // -1 = none selected
        std::vector<double> bookmarkPositions; // up to 5
        uint64_t addCount = 0;
        uint64_t removeCount = 0;
        OpenValue<bool> markerCreateSucceeds{"pause_map_add_bookmark marker-object creation", "spec-lua-api-behaviour.md Sec42.1"};
        uint64_t createFailureGuardCount = 0;
    };
    PauseMapBookmarks& pauseMapBookmarks() { return pauseMapBookmarks_; }
    uint64_t& pauseMapDragMapCount() { return pauseMapDragMapCount_; }
    struct PauseMenuControlSchemeSets {
        OpenValue<int32_t> countA{"pause-menu control-scheme set A count", "spec-lua-api-behaviour.md Sec42.1"};
        OpenValue<int32_t> countB{"pause-menu control-scheme set B count", "spec-lua-api-behaviour.md Sec42.1"};
        int32_t appliedIndex = -1;
        uint64_t emptySetGuardCount = 0;
    };
    PauseMenuControlSchemeSets& pauseMenuControlSchemeSets() { return pauseMenuControlSchemeSets_; }

    // --- Sec57.3 crib-adjacent / Sec57.4 world/UI front-end -----------------
    bool& cityLoadImgComplete() { return cityLoadImgComplete_; }
    uint64_t& communityLoginOpenCount() { return communityLoginOpenCount_; }
    int32_t& completionCallbackCaseSelected() { return completionCallbackCaseSelected_; }

    // --- Sec57.4 cell_playlist_save_list: this batch's SECOND own named
    // memory-safety finding - drop out-of-range song ids, clamp a negative
    // table count to 0 before use (both §57's own fix recommendations).
    OpenValue<uint32_t>& radioSongTableCount() { return radioSongTableCount_; }
    uint64_t& playlistSaveOutOfRangeDroppedCount() { return playlistSaveOutOfRangeDroppedCount_; }
    uint64_t& playlistSaveNegativeCountClampedCount() { return playlistSaveNegativeCountClampedCount_; }
    // CHOSEN (hardening, adversarial-review follow-up, 2026-10-08): a very large
    // positive table count (e.g. 2^31-1) is no longer a memory-safety issue once
    // the negative-count underflow above is closed, but it is still a multi-
    // second hang/DoS shape - bounded at 65,535 (the real engine's own 16-bit
    // narrowing bug's accidental cap) in cell_playlist_save_list. Counted here.
    uint64_t& playlistSaveCountCappedCount() { return playlistSaveCountCappedCount_; }
    std::vector<std::vector<int32_t>>& savedPlaylists() { return savedPlaylists_; }

    // --- Sec45.2 cinema_editor_create_camera_zone/_save_clip ----------------
    struct CinemaEditorState {
        uint32_t nextZoneHandle = 1;
        std::unordered_map<uint32_t, uint32_t> zoneGeneration; // handle -> generation
        uint64_t oobTemplateGuardCount = 0;
        uint64_t createCount = 0;
        uint64_t saveClipPathGuardCount = 0;
    };
    CinemaEditorState& cinemaEditor() { return cinemaEditor_; }

    // --- Sec26.10 pause_menu_quit_game_internal -----------------------------
    bool& pauseMenuQuitRequested() { return pauseMenuQuitRequested_; }

    // --- Sec23.9/Sec23.20/Sec23.21/Sec23.22 completion/main-menu small flags -
    uint64_t& completionCameraShakeCount() { return completionCameraShakeCount_; }
    bool& completionCreditsWereActive() { return completionCreditsWereActive_; }
    bool& completionCutsceneWasSkipped() { return completionCutsceneWasSkipped_; }
    uint64_t& mainMenuRedeemCodeOpenCount() { return mainMenuRedeemCodeOpenCount_; }

    // --- Sec45.4 cellphone_dial ----------------------------------------------
    struct CellphoneDialState {
        OpenValue<uint32_t> phoneBookCount{"cellphone phone-book entry count", "spec-lua-api-behaviour.md Sec45.4"};
        uint64_t outOfRangeGuardCount = 0;
        uint64_t dialCount = 0;
    };
    CellphoneDialState& cellphoneDial() { return cellphoneDial_; }

    // --- Sec55.3 game_audio_set_rtpc ----------------------------------------
    uint64_t& audioRtpcAppliedCount() { return audioRtpcAppliedCount_; }

    // --- Sec59.2 cell_music_menu_toggle_station / Sec59.1 cell_is_map_disabled:
    // a hard-coded six-mission list this host tracks via a test-settable
    // "current mission stem" (no real mission-stack string anywhere else in
    // this project's public API) plus its own extra gating byte (cellUi_
    // above). Also: cell_cheats_unlock_cheat's content-id gate (a cheat
    // whose master record carries a non-default content id is never
    // unlocked through this path, CONFIRMED) - per-cheat content id, 0 =
    // default = always unlockable.
    std::string& currentMissionStemForTesting() { return currentMissionStem_; }
    std::unordered_map<int32_t, int32_t>& cheatContentId() { return cheatContentId_; }

    // --- Sec45.4/Sec56.1 autil_hud_mayhem_init/autil_mashing_minigame_
    // faded_out: the former has a documented shape (one-time native-callback
    // creation + per-call element lookup, null-read on an exhausted pool on
    // the first call); the latter has NO spec text anywhere in this project
    // (checked spec-lua-api-behaviour.md's full Sec59/Sec45 text and
    // spec-lua-bindings.md - genuinely absent), so it is refused OPEN rather
    // than guessed from its name.
    struct AutilHudMayhemInit {
        bool oneTimeSetupDone = false;
        OpenValue<bool> nativeCallbackPoolAvailable{"autil_hud_mayhem_init native-callback pool availability",
                                                     "spec-lua-api-behaviour.md Sec45.6"};
        uint64_t poolExhaustedGuardCount = 0;
        uint64_t subscribeCount = 0;
    };
    AutilHudMayhemInit& autilHudMayhemInit() { return autilHudMayhemInit_; }

    // =====================================================================
    // Batch W2-8 (2026-10-08), wave-2 fan-out: spec-lua-api-behaviour.md
    // §38-60 "gameplay B" (character/NPC/inventory/party/HUD/minimap/
    // mission-flow/hooks), 93 names split across
    // lua_spec_confirmed_stubs_b19_wave2_char_npc_inv.cpp (55 names) and
    // lua_spec_confirmed_stubs_b20_wave2_hud_minimap_hooks.cpp (38 names).
    // Both files are new translation units in the existing bNN split
    // (lua_spec_confirmed_stubs_registry.cpp's own top comment); their
    // shared EngineState additions live in this ONE block since
    // engine_state.h is a single header both files include. Every new
    // per-object field below is a NEW, STANDALONE map/struct (never an
    // insertion into CharacterState/VehicleState's own bodies) - this
    // project's own established convention for a later feature needing new
    // per-object state (cf. missionCellphoneAudio_/spawnRegionFilterBits_/
    // aiPersonalityProfileCodes_ above), chosen here specifically to avoid
    // a high-conflict-risk edit to either of those two very large structs.
    // =====================================================================

    // --- Shared on_* hook-name pool (0x00a1fcc0, spec-lua-api-behaviour.md
    // §42.3/§42.7/§47) - a ~350-slot fixed name->id registry with
    // insertion-on-miss, used by on_dismount/on_hit_ped/on_mount/
    // on_vehicle_debris_flow_recycled/on_m21_player_choice/
    // on_mission_item_drop (all 6 registered by THIS batch - this is the
    // first implementation of the pool, not a reuse of a prior one; the
    // spec's own "already documented" phrasing at §42.7/§47 means
    // documented IN THE SPEC TEXT, not already coded). Modelled the SAME
    // way this project's existing internHudSecondaryKey() (Sec12.1,
    // 0x00a1fcc0's OTHER already-coded call site, a narrower per-feature
    // version of the identical real mechanism) already chose: sequential
    // id assignment starting at 1 on first use, CHOSEN (the real pool's
    // own starting id/base is not given) - NOT a literal hash-collision
    // simulation (§42.7's "matches purely by name hash, two different
    // names with an equal hash could share a slot" quirk is a documented
    // but UNMODELED real-engine hazard, the same simplification already
    // made for internHudSecondaryKey; reproducing the real hash would
    // need the real 0x00a1fcc0 algorithm, which is OPEN). CORRECTED
    // 2026-10-08 (review of this batch): the real pool has TWO distinct,
    // separately-sentinelled failure modes, not one - §22.11's own
    // raw-disassembly re-confirmation of 0x00a1fcc0 is explicit that table
    // EXHAUSTION (no free slot left once kCapacity, 350, CONFIRMED real
    // pool size, §42.7/§47, names are already interned) "returns -1 on
    // exhaustion", while §42.7 separately documents that a STRING-
    // ALLOCATION FAILURE during registration (a transient allocator
    // failure while copying the name, unrelated to how many names are
    // already interned) "stores the sentinel id 0 rather than the
    // 'failed' sentinel -1." These are two genuinely different real
    // conditions the spec text itself distinguishes by sentinel value, not
    // one merged failure mode - a prior version of this comment/code
    // conflated them (claiming -1-shaped capacity exhaustion returns 0,
    // "not a CHOSEN hardening divergence"), which was simply wrong, not a
    // documented simplification. Modelled here: reaching kCapacity on a
    // new name is the EXHAUSTION case and returns -1 (matching §22.11's
    // own sentinel exactly - a real, testable capacity limit, not a
    // hardening divergence). The separate string-allocation-failure path
    // (sentinel 0) has no structural equivalent in this project - nothing
    // here ever models std::string/unordered_map allocation failing, and
    // no other OpenValue-style field in this codebase fabricates an
    // injectable allocation-failure hook - so it is simply not reachable,
    // not silently dropped (same "no invented fixes" discipline as every
    // other OPEN gap in this header).
    struct HookNamePool {
        static constexpr size_t kCapacity = 350;
        int32_t intern(const std::string& name) {
            auto it = ids.find(name);
            if (it != ids.end()) return it->second;
            if (ids.size() >= kCapacity) { ++exhaustionCount; return -1; } // §22.11: -1 on table exhaustion
            int32_t id = nextId++;
            ids[name] = id;
            return id;
        }
        std::unordered_map<std::string, int32_t> ids;
        int32_t nextId = 1;
        uint64_t exhaustionCount = 0; // §22.11 table-exhaustion sentinel (-1) - NOT §42.7's distinct
                                       // string-allocation-failure sentinel (0), which this project
                                       // doesn't model (see doc comment above)
    };
    HookNamePool& onHookNamePool() { return onHookNamePool_; }

    // --- Groups (§41.5 group_is_loaded_internal/group_show_do/
    // group_hide_do). "Loaded" is CHOSEN to reuse the EXISTING
    // groupAlreadyCreated()/markGroupCreated() existence registry
    // (Sec53.3's own group_create_do, already in this codebase) directly -
    // the only real "does this group exist" signal this project tracks;
    // §41.5's own "false on a failed lookup" wording is a natural fit for
    // "never created." group_show_do/group_hide_do's own documented crash
    // (a null member object reached while walking the group's member
    // list) has NO structural equivalent in this project: Sec53.3's own
    // groupCreateCheckDone() doc comment already establishes this project
    // deliberately tracks group EXISTENCE only, never a member roster - so
    // there is no member list to walk and nothing to null-dereference;
    // implemented instead as a safe existence-gated visibility flag (a
    // CHOSEN scope limitation, not a fabricated member list). CORRECTED
    // 2026-10-08 (review of this batch): this comment always claimed
    // "existence-gated," but a prior version of the two stubs never
    // actually called groupAlreadyCreated() before mutating
    // w28GroupVisibility_ - hiding/showing a group that was NEVER created
    // silently set a visibility flag anyway, contradicting this comment
    // and leaving group_is_loaded_internal('G1')==false inconsistent with
    // a group that nonetheless had real show/hide state. §41.5's own text
    // doesn't separately spell out what the real function does for an
    // unresolved/never-created group name (it only documents the member-
    // object null-deref crash on an ALREADY-resolved group), but every
    // other named-object resolve in this spec early-returns on a failed
    // lookup rather than fabricating state - consistent with, not
    // contradicted by, this project's own stated "existence-gated" design
    // - so the fix is to make the CODE match the comment's always-stated
    // intent (option (a): add the gate), not to weaken the comment.
    // group_show_do/group_hide_do now both refuse (no-op, counted below)
    // when groupAlreadyCreated(name) is false, exactly like
    // group_is_loaded_internal already reads that same registry.
    struct W28GroupVisibility {
        bool hidden = false;
        uint64_t showCount = 0;
        uint64_t hideCount = 0;
    };
    W28GroupVisibility& w28GroupVisibility(const std::string& name) { return w28GroupVisibility_[name]; }
    uint64_t groupShowOnUncreatedGuardCount() const { return groupShowOnUncreatedGuardCount_; }
    void recordGroupShowOnUncreatedGuard() { ++groupShowOnUncreatedGuardCount_; }
    uint64_t groupHideOnUncreatedGuardCount() const { return groupHideOnUncreatedGuardCount_; }
    void recordGroupHideOnUncreatedGuard() { ++groupHideOnUncreatedGuardCount_; }

    // --- player_override_* sprint family (§40.1, 6 names). CONFIRMED
    // GLOBAL (not per-player), file-backed -1 ("no override") statics,
    // mission-scoped (reset to -1 at mission start/restart and registered
    // as a session-synced variable, mechanism 0x0086d770) - but this
    // project has NO mission-start/restart hook and NO generic "session-
    // synced variable" class anywhere in ~7800 lines of existing state
    // (confirmed by direct search: every spec "session-synced variable"
    // write-up elsewhere in this codebase was implemented as a bare field,
    // e.g. allowWeaponAutoPickup() below) - so the mission-scoped reset is
    // OPEN/not wireable in this project as it stands, same documented gap
    // as every other mission-scoped variable here; seeded at the CHOSEN
    // real file-backed default (-1) directly instead.
    double& sprintUseOverride() { return sprintUseOverride_; }
    double& sprintRechargeOverride() { return sprintRechargeOverride_; }
    double& sprintDelayOverride() { return sprintDelayOverride_; }

    // --- players_naked (§51.4): a strip-and-resync routine covering both
    // local and remote co-op players - this project has no single
    // PlayerRig-level "naked" bool yet (PlayerRig above tracks gender/
    // presence only), so two plain bools stand in for "local"/"coop
    // partner," CHOSEN false at construction (no spec-given start value).
    bool& localPlayerNaked() { return localPlayerNaked_; }
    bool& coopPlayerNaked() { return coopPlayerNaked_; }

    // --- players_in_weird_camera_mode (§40.4): CONFIRMED the FIRST of its
    // two contributing globals is permanently dead code in the shipped
    // binary (a 9-site exhaustive scan found zero writers) - modelled as a
    // literal `false` constant in the stub body, no field needed. The
    // SECOND global (an "RC Gun" flag) is written only by two functions
    // explicitly OUT of this batch's own 93-name list (OPEN, not wired
    // here) - this project tracks it anyway as a settable field so a
    // future batch implementing those two setters can wire it without
    // another engine_state.h edit, but within THIS batch it can only ever
    // read false (CHOSEN, honestly reflects "no in-scope writer").
    bool& rcGunActiveFlag() { return rcGunActiveFlag_; }

    // --- Inventory (§41.3/§54.3, 6 names: inv_item_add_ammo/_add_do/
    // _get_weapon_upgrades/_dual_wield/_get_anim_group/_get_weapon_slot).
    // No InventoryState/weapon-table/ammo-pool model exists anywhere in
    // this project (confirmed by search) - new, minimal per-character/
    // per-item maps, CHOSEN plain-map convention (same shape as
    // spawnRegionFilterBits_ above). Sentinels CONFIRMED by spec: anim
    // group "Default" (§54.3), weapon slot 255 (§54.3), weapon-upgrade
    // lookup miss -1 alone (§41.3).
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>& inventoryAmmo() { return inventoryAmmo_; }
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>& inventoryItemCounts() { return inventoryItemCounts_; }
    std::unordered_map<std::string, bool>& dualWieldFlags() { return dualWieldFlags_; }
    std::unordered_map<std::string, std::string>& itemAnimGroups() { return itemAnimGroups_; }
    std::unordered_map<std::string, int32_t>& itemWeaponSlots() { return itemWeaponSlots_; }
    std::unordered_map<std::string, int32_t>& weaponUpgradeLevels() { return weaponUpgradeLevels_; }

    // --- notoriety_get (§50.5). Reuses the EXISTING
    // lookupNotorietyTrackCode()/kNotorietyTrackCodeNotFound resolver
    // directly (do not re-derive) - a resolved code's actual numeric LEVEL
    // has no existing backing store anywhere (confirmed by search), so a
    // new per-code map is added; an unresolved/unknown track name is
    // CONFIRMED to push zero Lua values (handled in the stub, not here).
    // HARDENED (CHOSEN divergence, §50/T18 CRASH): the real engine passes
    // a non-string argument straight into the CRT's __stricmp with no
    // null check, which can terminate the process - this project instead
    // folds a non-string argument into the SAME "unknown track" (zero
    // values) path every misspelt-but-string name already takes, counted
    // via the named guard below.
    std::unordered_map<int32_t, double>& notorietyTrackValues() { return notorietyTrackValues_; }
    uint64_t notorietyGetNonStringArgGuardCount() const { return notorietyGetNonStringArgGuardCount_; }
    void recordNotorietyGetNonStringArgGuard() { ++notorietyGetNonStringArgGuardCount_; }

    // --- ai_force_team_idle (§46.3). Reuses the EXISTING
    // resolveTeamId()/teamIdByName_ resolver (sentinel 9 on miss) exactly
    // as-is. CONFIRMED real, intentionally-replicated logic defect (not a
    // memory-safety bug, so NOT hardened away): an unknown/misspelled team
    // name resolves to the SAME sentinel 9 every downstream loop here
    // treats as "match every team," so this project idles all 8 tracked
    // teams in that case too, matching the real documented scope-widening
    // behavior exactly. HARDENED (CHOSEN divergence) only for the
    // SEPARATE, genuine memory-safety finding: a nil/non-string team name
    // reaches a null-pointer dereference in the real hash helper with no
    // guard - this project refuses (no-op) instead, counted below.
    std::array<bool, 8>& teamIdleFlags() { return teamIdleFlags_; }
    uint64_t aiForceTeamIdleNullNameGuardCount() const { return aiForceTeamIdleNullNameGuardCount_; }
    void recordAiForceTeamIdleNullNameGuard() { ++aiForceTeamIdleNullNameGuardCount_; }

    // --- action_sequence_setup/_is_playing/_advance (§46.2). A NEW struct
    // (NOT the existing `ActionSequence` struct above, which is scoped
    // only to action_sequence_end/Sec33.3 and has no vehicle pointer/
    // control-lock pool - reusing that name here would collide two
    // genuinely different real mechanisms under one C++ type). CONFIRMED
    // "action_sequence_setup with no vehicle name clears the sequence in
    // single player" (this project's whole scope) and "leaks one control-
    // lock slot per repeated call" (replicated via the monotonic counter
    // below, never decremented - no release function is in this batch).
    struct ActionSequenceScriptBlock {
        bool active = false;
        std::string vehicleName;
        uint64_t controlLockLeakCount = 0; // CONFIRMED: never released in this batch's own scope
        uint64_t advanceCount = 0;
    };
    ActionSequenceScriptBlock& actionSequenceScript() { return actionSequenceScript_; }
    // HARDENED (CHOSEN divergence, §46.2 CRASH): action_sequence_is_playing
    // always resets the local player's script mode as a side effect of the
    // query; the real engine's null-tolerant "is locally owned" helper lets
    // this proceed even with no local player, reaching a null+0x2888 write.
    // This project only performs the (test-observable, no-op-shaped) side
    // effect when hasLocalPlayer() is known true; otherwise refused, counted.
    uint64_t actionSequenceIsPlayingNullPlayerGuardCount() const { return actionSequenceIsPlayingNullPlayerGuardCount_; }
    void recordActionSequenceIsPlayingNullPlayerGuard() { ++actionSequenceIsPlayingNullPlayerGuardCount_; }

    // --- tutorial_get_case/_visible/_stop_current (§38.2/§52.3). Reuses
    // the EXISTING tutorialLookup()/210-entry table directly for name
    // resolution. tutorial_get_case's own "+0x10 field" (a per-entry case
    // number) has no spec-given data - OpenValueMap so a resolved-but-
    // unset entry refuses honestly (OpenStateError) rather than fabricate
    // a number, the SAME convention tutorialState() above already uses.
    // tutorial_visible's own boolean depends on "~11 further conditions,
    // not individually traced" (§38.2's own words, genuinely OPEN) - also
    // an OpenValue, refusing until a test sets it. tutorial_stop_current
    // has no tutorial-PROMPT-QUEUE model in this project (a different,
    // runtime-only mechanism from the 210-entry table) - call-count only,
    // same "deferred infra" convention as hudCriticalTimerStopCount_ above.
    OpenValueMap<double>& tutorialCaseNumber() { return tutorialCaseNumber_; }
    OpenValue<bool>& tutorialVisible() { return tutorialVisible_; }
    uint64_t tutorialStopCurrentCount() const { return tutorialStopCurrentCount_; }
    void recordTutorialStopCurrent() { ++tutorialStopCurrentCount_; }

    // --- perform_killswitch (§51.4 CRASH). CONFIRMED the victim argument
    // is used only for a type check; the real QTE always targets whatever
    // live object is registered under the LITERAL script name
    // "M21_Killbane" - reuses objectResolves() with that literal name, no
    // new per-object state. HARDENED (CHOSEN divergence): the real engine
    // zeroes the lookup result and makes a virtual call through it when no
    // such object exists; this project refuses (no-op) instead, counted.
    uint64_t performKillswitchMissingKillbaneGuardCount() const { return performKillswitchMissingKillbaneGuardCount_; }
    void recordPerformKillswitchMissingKillbaneGuard() { ++performKillswitchMissingKillbaneGuardCount_; }

    // --- player_action_is_pressed (§51.4): "a bounds-safe 166-row action-
    // name lookup" (aggregate text only, no per-action press-state system
    // exists in this project) - a settable per-action map stands in,
    // CHOSEN false ("not pressed") for any action never set by a test.
    std::unordered_map<std::string, bool>& actionPressedFlags() { return actionPressedFlags_; }

    // --- player_get_custom_voice (§51.4 CRASH). CONFIRMED the resolver
    // result is never checked before being dereferenced. HARDENED (CHOSEN
    // divergence): refuses (returns the CHOSEN "no value" sentinel -1,
    // this project's own pervasive "no value" convention, rather than
    // crash) on an unresolved player name, counted below; a resolved name
    // reads a settable per-player voice id, CHOSEN -1 default.
    std::unordered_map<std::string, int32_t>& customVoiceIds() { return customVoiceIds_; }
    uint64_t playerGetCustomVoiceUnresolvedGuardCount() const { return playerGetCustomVoiceUnresolvedGuardCount_; }
    void recordPlayerGetCustomVoiceUnresolvedGuard() { ++playerGetCustomVoiceUnresolvedGuardCount_; }

    // --- character_take_human_shield_do / player_take_human_shield_do
    // (§57.5/§51.4). Reuses the EXISTING CharacterState::
    // humanShieldHostageName field directly (already modelled for an
    // earlier character_has_specific_human_shield-family native) - no new
    // per-character state needed.

    // --- qte_start_m02_skyqte_01 (§51.1 CRASH). CONFIRMED the underlying
    // worker checks its optional partner argument on the remote
    // (forwarded) branch but NOT on the local branch - reachable only for
    // a locally-owned character, which this project's whole single-player
    // scope always is. §51.1's own text is explicit that "a nil,
    // misspelled, despawned, or dead partner argument crashes" - CORRECTED
    // 2026-10-08 (review of this batch): a prior version of this guard
    // only covered "present but fails to resolve" (misspelled/despawned),
    // silently falling through for an omitted/nil partner (the most
    // common real case - this native's own partner argument is optional)
    // and never checking a resolved-but-dead partner at all, missing 2 of
    // the 4 named crash shapes. HARDENED (CHOSEN divergence): both an
    // omitted/nil partner AND a partner that fails to resolve are refused
    // (no-op) by the SAME counter below - the real crash is the identical
    // null dereference either way, so this project doesn't split "nil" and
    // "unresolved name" into separate counters (matching this project's
    // own established convention of ONLY splitting counters when the
    // underlying code path is genuinely distinct, e.g. action_play_do's
    // separate resolve-failure vs. dead-not-forced sentinels/counters,
    // Sec22.29). A resolved-but-dead partner is a genuinely distinct code
    // path (reads CharacterState rather than just objectResolves()), so it
    // gets its OWN counter below, reusing CharacterState::
    // notDeadForActionPlay() - this project's existing general "is this
    // character dead" predicate (the SAME lifeState==5/killFinalizedFlagE3
    // pair character_is_dead's own stub reads directly, Sec3.5) - rather
    // than inventing a new liveness check or reusing the narrower,
    // separately-scoped isDeadHighConfidence field (tied specifically to
    // set_current_hit_points' own internal logic, Sec7.31 - see that
    // field's own doc comment above for why it is NOT a general-purpose
    // dead check).
    uint64_t qteSkyqte01NullPartnerGuardCount() const { return qteSkyqte01NullPartnerGuardCount_; }
    void recordQteSkyqte01NullPartnerGuard() { ++qteSkyqte01NullPartnerGuardCount_; }
    uint64_t qteSkyqte01DeadPartnerGuardCount() const { return qteSkyqte01DeadPartnerGuardCount_; }
    void recordQteSkyqte01DeadPartnerGuard() { ++qteSkyqte01DeadPartnerGuardCount_; }
    uint64_t qteSkyqte01StartCount() const { return qteSkyqte01StartCount_; }
    void recordQteSkyqte01Start() { ++qteSkyqte01StartCount_; }

    // --- message_remove (§50.2). CONFIRMED "fully bounds- and generation-
    // checked, no crash shape" - the already-registered message() (Sec15.11,
    // lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp) models its
    // own returned id as a bare monotonic handle with no backing slot
    // array, so there is nothing for this native to actually mutate;
    // implemented as a safe call counter, consistent with that existing
    // simplification (not a cross-batch state change to stub_message).
    uint64_t messageRemoveCount() const { return messageRemoveCount_; }
    void recordMessageRemove() { ++messageRemoveCount_; }

    // --- minimap_icon_add_radius_do (§50.2 CRASH) / _remove_radius_do
    // (§50 intro, a pure alias of the ALREADY-REGISTERED
    // minimap_icon_remove_do, lua_spec_confirmed_stubs_b04_r04_r06_08.cpp -
    // reproduced byte-for-byte in the new file rather than cross-TU-
    // aliased, since that existing stub has internal linkage; see the new
    // stub's own doc comment). Both reuse CharacterState::minimapIcons
    // directly - no new per-object state. HARDENED (CHOSEN divergence,
    // add_radius_do only): the real engine reads a handle through a null
    // object pointer when the name is unresolved, and indexes a 4-entry
    // function-pointer table with no range check on the selector argument
    // (only 1-3 safe) - both refused (no-op) here instead, one shared
    // counter (either guard can fire independently per call).
    uint64_t minimapIconAddRadiusGuardCount() const { return minimapIconAddRadiusGuardCount_; }
    void recordMinimapIconAddRadiusGuard() { ++minimapIconAddRadiusGuardCount_; }

    // --- open_vint_dialog_do (§50.2 CRASH) / open_vint_dialog_check_done
    // (§42.6, corrected §50.3). CONFIRMED the completion callback only
    // ever writes the literal -1 (never an option index), gated on its own
    // 2nd argument == -1; file-default -2 ("pending"). One shared global
    // models both ends of this mechanism.
    int32_t& vintDialogResult() { return vintDialogResult_; }
    // HARDENED (CHOSEN divergence): open_vint_dialog_do's own help-text
    // table pointer is never null-checked in the real engine; this project
    // has no help-text table at all to dereference, so the hazard has no
    // structural equivalent here (trivially satisfied, not a guessed fix).
    uint64_t openVintDialogDoCount() const { return openVintDialogDoCount_; }
    void recordOpenVintDialogDo() { ++openVintDialogDoCount_; }

    // --- mission_maybe_uncomplete_m22 (§50.6). CONFIRMED gated on an XOR
    // over two specific follow-on missions' own progress, which this
    // project has no mission-progression model for (OPEN) - call-count
    // only, same "deferred infra" convention as several fields above; the
    // CONFIRMED "broadcasts from every machine regardless of host status"
    // half is still reproduced via EngineState::replicateStateChange.
    uint64_t missionMaybeUncompleteM22Count() const { return missionMaybeUncompleteM22Count_; }
    void recordMissionMaybeUncompleteM22() { ++missionMaybeUncompleteM22Count_; }

    // --- homie_mission_lock/_unlock (§53/AGG, §54.4). The ONLY text
    // available for either (homie_mission_unlock's own aggregate mention,
    // §54.4): "an activity-record override... paired with its lock
    // sibling, has no way to restore the record's original default state
    // through either binding" - modelled as a simple per-id override flag
    // with deliberately NO reset/clear accessor (matching "no way to
    // restore" exactly): 0 = no override, 1 = locked, 2 = unlocked.
    std::unordered_map<std::string, int32_t>& homieMissionOverride() { return homieMissionOverride_; }

    // --- hud_get_text_adventure_current_screen_index (§41.4): CONFIRMED
    // consume-once (reads a global then immediately resets it to -1).
    int32_t& hudTextAdventureScreenIndex() { return hudTextAdventureScreenIndex_; }
    // --- hud_get_proto_selection (§54.4): "a sticky poll result" (does
    // NOT consume-once, unlike the field above) - CHOSEN default -1.
    int32_t& hudProtoSelection() { return hudProtoSelection_; }
    // --- hud_pop_screen (§54.4): CONFIRMED only pops when the named
    // screen is on top of the stack, but broadcasts the pop regardless.
    // Reuses the SAME hudScreenStack()/hudScreenStack_ hud_push_screen
    // (Sec21.6, above) already models as its one plain-vector stand-in for
    // the real HUD-screen stack - not a second, duplicate field (push side
    // is out of this batch's own 93 names, so only pop is exercised here;
    // a test can still seed it via the shared accessor).

    // --- hud_x_of_y_add/_update(§54.3 CRASH)/_remove(§41.4 CRASH). A new
    // fixed 4-slot array (CONFIRMED real bound, from hud_x_of_y_add's own
    // "correctly rejects indices outside 0-3"). HARDENED (CHOSEN
    // divergence) for update/remove: the real engine applies ZERO bound
    // checking to update's slot index (a worse, write-shaped sibling of
    // remove's own unbounded-index-into-a-wild-indirect-call hazard) -
    // both are bound-checked here to the SAME 0-3 range add already
    // enforces, refused+counted outside it.
    struct HudXOfYSlot {
        bool active = false;
        double current = 0.0;
        double total = 0.0;
    };
    static constexpr size_t kHudXOfYSlotCount = 4;
    std::array<HudXOfYSlot, kHudXOfYSlotCount>& hudXOfYSlots() { return hudXOfYSlots_; }
    uint64_t hudXOfYAddOobGuardCount() const { return hudXOfYAddOobGuardCount_; }
    void recordHudXOfYAddOobGuard() { ++hudXOfYAddOobGuardCount_; }
    uint64_t hudXOfYUpdateOobGuardCount() const { return hudXOfYUpdateOobGuardCount_; }
    void recordHudXOfYUpdateOobGuard() { ++hudXOfYUpdateOobGuardCount_; }
    uint64_t hudXOfYRemoveOobGuardCount() const { return hudXOfYRemoveOobGuardCount_; }
    void recordHudXOfYRemoveOobGuard() { ++hudXOfYRemoveOobGuardCount_; }

    // --- human_being_revived/_revived_by (§54.3), human_downed_set_mars_
    // kb_interactable (§54.3). CONFIRMED sentinels: revived-by's own
    // single-player literal is "#NETID#0" (no session ever exists in
    // single player, §8.27/§26.28) when revived by the local player, nil
    // otherwise (never revived, or revived by a non-player NPC - this
    // project cannot distinguish "non-player NPC revived them" from
    // "never revived" without an NPC-reviver identity model, OPEN, folded
    // into the same nil path). mars_kb's own documented "no repeat-call
    // guard... leaks" defect is reproduced via the counter below, which
    // increments whenever a call overwrites an ALREADY-set id.
    std::unordered_map<std::string, bool>& revivedFlags() { return revivedFlags_; }
    std::unordered_map<std::string, bool>& revivedByLocalPlayerFlags() { return revivedByLocalPlayerFlags_; }
    std::unordered_map<std::string, int64_t>& marsKbInteractableId() { return marsKbInteractableId_; }
    uint64_t marsKbInteractableLeakCount() const { return marsKbInteractableLeakCount_; }
    void recordMarsKbInteractableLeak() { ++marsKbInteractableLeakCount_; }

    // --- flashpoint_start_mission/_mission_status (§45.2/§55.5). No
    // world-flashpoint-list model exists in this project (OPEN) - the id
    // is stored raw rather than resolved against a real list (CHOSEN
    // simplification). flashpoint_mission_status's own "combine rule
    // favoring in-progress when the two sides disagree" (§45.2's own
    // words, the fullest reading this spec text supports) is implemented
    // literally: equal -> that value, disagreement -> 2 (started/in
    // progress); single player never has a peer (§8.27/§26.28), so
    // peerStatus is simply kept equal to localStatus here, CHOSEN.
    struct FlashpointState {
        int32_t localStatus = 1;  // 1=idle/reset (CONFIRMED file default), 2=started, 3=HYPOTHESIS failed/aborted, 4=complete
        int32_t peerStatus = 1;
        std::string activeId;
    };
    FlashpointState& flashpointState() { return flashpointState_; }

    // --- action_play_custom_do (§58 CRASH). CONFIRMED sentinels: -1 dead,
    // 0 play-failed, 1 play-succeeded (truthy even on failure - 0 is
    // truthy in Lua, per §58's own flagged trap). CONFIRMED a resolved,
    // live character ALWAYS reaches 1 regardless of animation-name
    // validity (the real slot player "claims a slot anyway" for an
    // unknown name) - this project has no animation-name table to
    // distinguish known/unknown either way, so this is not a modelling
    // shortfall, it is the documented real behavior. HARDENED (CHOSEN
    // divergence): the real engine skips its own dead-check for an
    // unresolved (null) character and crashes; this project refuses
    // instead, returning the CHOSEN closest documented failure value (0,
    // "play failed" - the real crash has no defined return value at all),
    // counted below.
    uint64_t actionPlayCustomDoUnresolvedGuardCount() const { return actionPlayCustomDoUnresolvedGuardCount_; }
    void recordActionPlayCustomDoUnresolvedGuard() { ++actionPlayCustomDoUnresolvedGuardCount_; }

    // --- A consolidated per-character "misc small flag" record for the
    // remaining §39.1/§41.5/§42.2/§49.3/§50.5/§57.5 natives this batch
    // implements whose exact backing byte/bit offset this spec text does
    // NOT give individually (npc_brute_always_throws, npc_detection_enable,
    // npc_weapon_pickup_override, set_attack_peds_flag,
    // npc_brute_set_no_downed_finisher, npc_dont_auto_equip_or_unequip_
    // weapons, npc_is_in_brute_grab_qte,
    // human_has_taken_non_melee_damage_from_player, character_is_combat_
    // ready, get_char_skydiving_state, set_cower_variant,
    // override_npc_run_and_cower_run, set_unrecruitable_flag) - CHOSEN:
    // one small per-character map of plain fields rather than 13 separate
    // single-field maps, purely to keep this already-large batch's own
    // engine_state.h footprint manageable; each field's own CONFIRMED
    // default (where the spec states one) is preserved exactly.
    struct W28CharacterMisc {
        bool bruteAlwaysThrows = false;
        bool npcDetectionEnabled = false;       // CONFIRMED default false (bare boolean)
        bool npcWeaponPickupOverrideArg = false; // raw Lua argument, CONFIRMED default false (bare boolean) - the real stored bit is this value's logical NOT (§42.2); no getter native is in this batch, so only the raw argument is kept, not the inverted bit
        bool attackPedsFlag = false;
        bool bruteNoDownedFinisher = false;
        bool dontAutoEquipUnequipWeapons = false; // CONFIRMED default false (unconditional read, §50.5 - the one exception to this family's usual default-true idiom)
        bool inBruteGrabQte = false;
        bool tookNonMeleeDamageFromPlayer = false; // §41.5: "who sets the bit is OPEN" - no setter in this batch's own 93 names, so this can only ever be false unless a test sets it
        bool combatReady = false; // CHOSEN default for a resolved character (§57.5 gives no real combat-readiness model; only the unresolved-name "no value" sentinel is CONFIRMED)
        int32_t skydivingState = 255; // CONFIRMED sentinel "found, not currently skydiving" (§41.5) - the CHOSEN default for any resolved character, since no sub-state model exists
        int32_t cowerVariant = -1; // CHOSEN "no variant set" default (§39.1 gives the write-side clamp/mapping, not a rest-state default)
        int32_t runAnimOverride = -1;      // override_npc_run_and_cower_run (§42.2): CONFIRMED -1 = "no override" for an unknown/nil name
        int32_t cowerRunAnimOverride = -1; // same native, second animation-set name
        uint32_t forceFlags1e02 = 0; // set_unrecruitable_flag (§39.1): CONFIRMED bit 0x10 of character byte +0x1e02 - an external stand-in (see this file's own "new feature -> external map" convention note above), not inserted into CharacterState itself
    };
    W28CharacterMisc& w28CharacterMisc(const std::string& name) { return w28CharacterMisc_[name]; }

    // --- set_cower_variant/set_unrecruitable_flag/set_attack_peds_flag/
    // the 3 npc_* setters above all follow this project's established
    // double-gate shape (c.forceFlagGatePasses.get() check against the
    // EXISTING per-character CharacterState field, then either write the
    // W28CharacterMisc field above or EngineState::replicateStateChange) -
    // implemented per-stub, same inlined shape as every other double-gate
    // setter in this codebase (no shared template exists for characters
    // either, confirmed by research - see stub_character_set_no_explosive_
    // panic, lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp, for the
    // exact pattern this batch's own new stubs copy).

    // --- party_use_melee_weapons_only (§42.2): CONFIRMED applies to the
    // local player AND, separately, the co-op partner if one exists - not
    // to any other party member. Two plain bools (no per-character map
    // needed, this is a whole-player-slot setting).
    bool& partyMeleeOnlyLocal() { return partyMeleeOnlyLocal_; }
    bool& partyMeleeOnlyCoop() { return partyMeleeOnlyCoop_; }

    // --- character_parachute_open (§57.5): CONFIRMED real resource-leak
    // defect - every call spawns a new parachute object with no reuse/
    // cleanup check. A single monotonic counter (not per-character) is
    // enough to make the leak shape test-observable.
    uint64_t parachuteOpenSpawnCount() const { return parachuteOpenSpawnCount_; }
    void recordParachuteOpenSpawn() { ++parachuteOpenSpawnCount_; }

    // --- on_dismount/on_hit_ped/on_mount/on_vehicle_debris_flow_recycled/
    // on_m21_player_choice/on_mission_item_drop (§42.3/§47) - all 6
    // register a callback NAME (interned via onHookNamePool() above, see
    // its own doc comment) into a hook slot; this project stores the raw
    // callback STRING directly per object (the SAME "single dedicated
    // field per hook kind" simplification CharacterState::onRevivedCallback/
    // onDeathCallback above already establish - not a generic slot-id
    // array, since this project only ever tracks the specific hook kinds
    // in scope). Empty/nil clears (CONFIRMED family-wide convention, same
    // as onRevivedCallback), EXCEPT on_mission_item_drop (see its own
    // field's doc comment below - CONFIRMED NOT reset on empty/nil).
    std::unordered_map<std::string, std::string>& onDismountCallbacks() { return onDismountCallbacks_; }
    std::unordered_map<std::string, std::string>& onHitPedCharacterCallbacks() { return onHitPedCharacterCallbacks_; }
    std::unordered_map<std::string, std::string>& onHitPedVehicleCallbacks() { return onHitPedVehicleCallbacks_; }
    std::unordered_map<std::string, std::string>& onVehicleDebrisFlowRecycledCallbacks() { return onVehicleDebrisFlowRecycledCallbacks_; }
    // on_mount (§42.3): CONFIRMED real inverted-probe quirk - checks the
    // GENERIC/"#PLAYER#" chain FIRST and registers the hook ONLY IF THAT
    // CHECK FAILS. CHOSEN (HIGH CONFIDENCE by direct structural analogy):
    // this project's own already-implemented on_ai_do_action_complete
    // (Sec14.7, lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp) is
    // CONFIRMED to share the IDENTICAL two-tier shape (playerClassChainResolves()
    // match -> no-op; else plain objectResolves() fallback -> register on
    // the character) - reused here rather than guessed fresh; the exact
    // real hook-slot NUMBER for on_mount is OPEN (not given anywhere in
    // this spec), so only the callback NAME is tracked, same "single
    // dedicated field" convention as every sibling above.
    std::unordered_map<std::string, std::string>& onMountCallbacks() { return onMountCallbacks_; }
    // on_m21_player_choice (§42.3): CONFIRMED a hand-inlined registration
    // against a GLOBAL hook record (not an object) - HIGH CONFIDENCE a
    // general player-level/UI hook slot despite the mission-21-sounding name.
    std::string& onM21PlayerChoiceCallback() { return onM21PlayerChoiceCallback_; }
    // on_mission_item_drop (§47): CONFIRMED a GLOBAL, non-object callback-
    // name hook, interned via the SAME onHookNamePool() the 5 on_* hooks
    // above share - CONFIRMED this persistent global is NOT reset when the
    // argument is nil/empty (a no-argument call simply reuses whatever
    // name the previous real call registered) - the stub must NOT clear
    // this field on an empty argument, unlike every sibling above.
    std::string& onMissionItemDropCallback() { return onMissionItemDropCallback_; }

    // --- Script-group HUD markers (§42.4): minimap_icon_add_script_group_do
    // CRASH / minimap_icon_remove_script_group_do CRASH (both share the
    // CONFIRMED unbounded 4-entry function-pointer-table-by-flags hazard;
    // add ALSO has a null icon-name dereference) / object_indicator_add_
    // script_group_do CRASH (CONFIRMED null-group dereference on a bad
    // group name) - this project has no group-membership roster to walk
    // (see w28GroupVisibility's own doc comment), so each is implemented
    // as a safe existence/argument-validated no-op; one named counter per
    // site, per this project's hardening policy.
    uint64_t minimapIconAddScriptGroupGuardCount() const { return minimapIconAddScriptGroupGuardCount_; }
    void recordMinimapIconAddScriptGroupGuard() { ++minimapIconAddScriptGroupGuardCount_; }
    uint64_t minimapIconRemoveScriptGroupGuardCount() const { return minimapIconRemoveScriptGroupGuardCount_; }
    void recordMinimapIconRemoveScriptGroupGuard() { ++minimapIconRemoveScriptGroupGuardCount_; }
    uint64_t objectIndicatorAddScriptGroupGuardCount() const { return objectIndicatorAddScriptGroupGuardCount_; }
    void recordObjectIndicatorAddScriptGroupGuard() { ++objectIndicatorAddScriptGroupGuardCount_; }

    // =====================================================================
    // Wave 2, Batch W2-2 (spec-lua-api-behaviour.md Sec17+Sec18, 2026-10-08)
    // - see src/lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp's own doc
    // comment above each stub_* function for the full citation/reasoning.
    // =====================================================================

    // set_stag_active (Sec17.19): a single global byte, no resolver, no
    // gate, no record - the simplest function in its batch. Reuses the
    // SAME stagActive()/stagActive_ get_stag_active (Sec53/T20, above)
    // already models - not a second, duplicate field (get/set on one real
    // global flag).

    // party_set_recruitable (Sec18.28): a single global byte.
    bool& partyRecruitable() { return partyRecruitable_; }

    // flashpoints_enable (Sec18.15): a single global byte. CHOSEN true
    // ("enabled" - matches optionalBoolDefault's own default=true reading,
    // same convention as roadblocksEnabled() above).
    bool& flashpointsEnabled() { return flashpointsEnabled_; }

    // notoriety_force_no_air_spawn (Sec18.12): SIX separate global objects'
    // own +0x455 byte, all written identically by the SAME one-line setter
    // every call - collapsed to one flag since nothing in scope
    // distinguishes the six objects from each other.
    bool& notorietyForceNoAirSpawn() { return notorietyForceNoAirSpawn_; }

    // radio_global_block (Sec18.24): a single global byte - distinct from
    // radio_block's own per-vehicle scoping (Sec12.20).
    bool& radioGlobalBlocked() { return radioGlobalBlocked_; }

    // boss_battle_matt_set_matt_flying (Sec18.2): a bare global "Matt is
    // flying" state flag - no per-object target, no replication.
    bool& bossBattleMattFlying() { return bossBattleMattFlying_; }

    // helicopters_set_jitter_override (Sec18.13): two adjacent globals
    // (min/max, HYPOTHESIS which is which), both defaulting to the SAME
    // unresolved fixed float constant (0x01117a4c) - CHOSEN placeholder
    // 0.0 (no in-scope reader depends on a specific default value).
    double& helicopterJitterMin() { return helicopterJitterMin_; }
    double& helicopterJitterMax() { return helicopterJitterMax_; }

    // player_grenades_enable (Sec18.25): genuinely zero-argument-sensitive -
    // unconditionally clears two globals to 0 every call, regardless of any
    // script-side argument.
    double& playerGrenadeGlobalA() { return playerGrenadeGlobalA_; }
    double& playerGrenadeGlobalB() { return playerGrenadeGlobalB_; }
    uint64_t& playerGrenadesEnableCallCount() { return playerGrenadesEnableCallCount_; }

    // player_can_sprint (Sec18.26): the global sprint-capability byte, plus
    // the disable-cascade's own further writes - no per-character target in
    // this project (the real "current player" accessor, 0x009da4e0, has no
    // clean per-name handle here; same project-wide "singular local player"
    // simplification several other natives already apply, e.g.
    // PlayerRig::localPlayer1Gender). sprintModeIsFive CHOSEN false (the
    // mode/difficulty code `5` branch is HYPOTHESIS and unlikely to be the
    // common case).
    bool& sprintEnabled() { return sprintEnabled_; }
    bool& sprintBit2Cleared() { return sprintBit2Cleared_; }
    // CHOSEN false - a plain bool, not OpenValue: player_can_sprint is a
    // real-hit (RT) target, and the mode `5` branch is HYPOTHESIS/unlikely
    // to be the common case, so an OPEN refusal here would needlessly block
    // a measurement run.
    bool& sprintModeIsFive() { return sprintModeIsFive_; }
    uint64_t& sprintDisableCount() { return sprintDisableCount_; }

    // parking_spot_disable_all (Sec18.29): CONFIRMED (raw-disassembly-
    // verified hidden-argument finding) that, despite its own name, this
    // native only ever touches ONE fixed slot (index 1), never "all" - the
    // Lua-facing boolean sense is "enable" (true -> clear slot 1) vs.
    // "disable" (false -> set slot 1), inverted from the name. CHOSEN true
    // ("enabled" - no stated construction default).
    bool& parkingSpotSlot1Enabled() { return parkingSpotSlot1Enabled_; }

    // difficulty_get_level (Sec17.30): CONFIRMED (0x00a47300) to be the
    // SAME real function get_current_difficulty (Sec41.2, above) already
    // models under its own alias name - reuses difficultyLevel()/
    // difficultyLevel_ directly, not a second, duplicate field with a
    // weaker (OPEN) default than the one already CONFIRMED there.

    // player_force_vehicle_seat (Sec17.6): the global "forced seat vehicle"
    // identity half - see CharacterState::forcedVehicleSeatIndex's own doc
    // comment for the full two-independent-globals reasoning. "" = the
    // fixed default struct (no vehicle name given).
    std::string& forcedSeatVehicleName() { return forcedSeatVehicleName_; }

    // on_purchase (Sec18.30): the named-hook-slot-array family's first
    // confirmed GLOBAL-anchored instance (base 0x026e9528, slot 4) -
    // alongside the already-documented per-object character/mover/vehicle
    // bases. Same empty-string-clears-slot convention as every other hook
    // field in this header.
    std::string& onPurchaseCallback() { return onPurchaseCallback_; }

    // on_human_killed (Sec17.7): a GLOBAL (not per-object) hook, slot 0xb
    // (11) of the SAME hook-slot array family. The optional filter string
    // resolves to a numeric type id via a fixed 350-entry name-interning
    // table (0x00a1fcc0) this project does not reproduce (same "no fixed
    // id table" class of gap as several other name-lookup natives) -
    // collapsed to one flat resolves-or-not predicate, test-registerable.
    std::string& onHumanKilledCallback() { return onHumanKilledCallback_; }
    std::string& onHumanKilledFilterName() { return onHumanKilledFilterName_; }
    void registerHumanKindForTesting(const std::string& kindName) { humanKindResolves_.insert(kindName); }
    bool humanKindResolves(const std::string& kindName) const { return humanKindResolves_.count(kindName) != 0; }

    // persona_set_global_situation_override (Sec17.21): the SAME global
    // 0x0150447c persona_clear_global_situation_override (Sec15.7, wave-1
    // Batch 5) already models as personaGlobalSituationOverride_ above -
    // reused directly, not duplicated (this is its own first real writer of
    // an arbitrary value; Sec15.7's own method only ever resets it to 0). A
    // 9-value enum [1,9], range-checked (values outside silently ignored),
    // network-replicated only inside a coordinated co-op session (reuses
    // coopSession().present directly - the SAME real singleton, 0x0087ba20 -
    // CHOSEN false by default per the project's single-player-only stance,
    // so this branch is dead unless a test installs a session).
    void setPersonaGlobalSituationOverride(int32_t v) { personaGlobalSituationOverride_ = v; }

    // persona_set_dlc_bundle (Sec17.22): only acts if the value differs
    // from the cached global - change-detection modeled directly via a
    // plain comparison at the call site. CHOSEN 0 = "never set".
    int32_t& personaDlcBundle() { return personaDlcBundle_; }

    // mission_autosave (Sec17.23, corrected per Sec60.3 - implemented from
    // the correction, NOT Sec17.23 alone): a silent-return-on-Steam-check-
    // failure path gates the WHOLE function (no record, no save attempt at
    // all) - CHOSEN true ("Steam available," this project models no real
    // Steam integration anywhere else either, and failing this gate closed
    // by default would silently block every autosave in every test). The
    // busy predicate's own polarity is CONFIRMED inverted from Sec17.23's
    // original text: the real save kickoff proceeds only when busy is
    // FALSE - CHOSEN false ("not busy" - the common fresh-run case). The
    // native explicitly skips game_autosave's own extra mission gates (so
    // it can save mid-mission) - this project has no mission-gate state to
    // skip in the first place, so that correction has no further code
    // effect here beyond NOT adding one.
    bool& missionAutosaveSteamCheckOk() { return missionAutosaveSteamCheckOk_; }
    bool& missionAutosaveBusy() { return missionAutosaveBusy_; }
    // CHOSEN true ("ready") - a plain bool, not OpenValue: mission_autosave
    // is a real-hit (RT) target, and an unresolved OPEN refusal on this
    // internal readiness chain would needlessly block a measurement run
    // over a detail nothing in scope can independently observe either way.
    bool& missionAutosaveChainReady() { return missionAutosaveChainReady_; }
    bool& missionAutosaveDeferredPending() { return missionAutosaveDeferredPending_; }
    uint64_t& missionAutosaveStubCallCount() { return missionAutosaveStubCallCount_; }
    uint64_t& missionAutosaveKickoffCount() { return missionAutosaveKickoffCount_; }

    // light_group_set_intensity (Sec17.24): no real per-kind resolver table
    // in this project (same "no fixed id table" class of gap) - collapsed
    // to one flat resolves-or-not predicate, test-registerable, plus a
    // per-group record of the members a light group "owns" (walked and
    // individually re-applied on every intensity change, CONFIRMED).
    struct LightGroup {
        double intensity = 0.0;
        bool flagBit2Set = false;
        std::vector<std::string> memberLightNames;
        uint64_t applyCount = 0;
        uint64_t replicateCount = 0; // coordinated-co-op-session-gated record, Sec17.24
    };
    OpenValueMap<bool>& lightGroupResolves() { return lightGroupResolves_; }
    // spec-lua-api-behaviour.md Sec4.13.0 (2026-10-12, FINAL): a light-group name resolves only if a live,
    // registered light group carries it; an unresolved group is a no-op, never an error. This host has no
    // light-group registry, so a name nobody registered is unresolved (false) rather than OPEN.
    bool lightGroupResolve(const std::string& name) const {
        return lightGroupResolves_.known(name) ? lightGroupResolves_.get(name) : false;
    }
    LightGroup& lightGroup(const std::string& name) { return lightGroups_[name]; }
    void registerLightGroupMemberForTesting(const std::string& groupName, const std::string& lightName) {
        lightGroups_[groupName].memberLightNames.push_back(lightName);
    }

    // inv_item_remove (Sec17.25): the item definition lookup (0x008dcb10 ->
    // 0x005feaa0) collapsed to one flat resolves-or-not predicate (same "no
    // fixed id table" class of gap).
    OpenValueMap<bool>& inventoryItemDefinitionResolves() { return inventoryItemDefinitionResolves_; }
    // spec-lua-api-behaviour.md Sec17.25 "Item lookup source" (2026-10-12, FINAL): the lookup scans the
    // items_inventory.xtbl rows; the placeholder "none" is not a row, so it resolves null and the removal
    // is skipped silently. Any other unregistered name stays OPEN (the 110 row names are not installed).
    bool inventoryItemDefinitionResolve(const std::string& name) const {
        if (inventoryItemDefinitionResolves_.known(name)) return inventoryItemDefinitionResolves_.get(name);
        if (inventoryItemTableComplete_) {
            // items_inventory.xtbl (+DLC) installed: _stricmp over the COMPLETE row set; no match -> null.
            const std::string f = AsciiLowerFold(name);
            return inventoryItemDefinitionResolves_.known(f) ? inventoryItemDefinitionResolves_.get(f) : false;
        }
        if (AsciiLowerFold(name) == "none") return false;
        return inventoryItemDefinitionResolves_.get(name); // throws OpenStateError
    }

    // Real items_inventory.xtbl (+ DLC) row names (spec-lua-api-behaviour.md Sec17.25 "Item lookup source",
    // spec-tables-vehicle-world.md Sec15): _stricmp resolver; the table is then COMPLETE, so any other name
    // resolves false (removal skipped silently). Names are case-folded.
    void installInventoryItemNames(const std::vector<std::string>& names) {
        for (const std::string& n : names) inventoryItemDefinitionResolves_.set(AsciiLowerFold(n), true);
        inventoryItemTableComplete_ = true;
    }
    bool inventoryItemTableComplete() const { return inventoryItemTableComplete_; }

    // character_add_prop (Sec17.13): the prop/customization-item lookup
    // (0x006cf0c0/0x00904c10) collapsed to one flat resolves-or-not
    // predicate.
    OpenValueMap<bool>& propDefinitionResolves() { return propDefinitionResolves_; }
    // f752324 (Sec17.13 answer): the prop name resolver is the items_3d.xtbl
    // catalogue; once its names are installed the table is COMPLETE, so any
    // other name resolves false (silent no-op). Case-folded.
    void installItems3dNames(const std::vector<std::string>& names) {
        for (const std::string& n : names) propDefinitionResolves_.set(AsciiLowerFold(n), true);
        items3dTableComplete_ = true;
    }
    bool items3dTableComplete() const { return items3dTableComplete_; }
    bool propNameResolves(const std::string& propName) {
        if (items3dTableComplete_) {
            const std::string k = AsciiLowerFold(propName);
            return propDefinitionResolves_.known(k) && propDefinitionResolves_.get(k);
        }
        return propDefinitionResolves_.get(propName); // OPEN until set
    }

    // set_civilians_flee (Sec17.4), the largest non-COMPLEX entry in Sec17-
    // 22: no generic world-object registry exists in this project (stated,
    // project-wide gap) - the global tracked-object array (0x03171a64) this
    // native walks is modeled as a never-populated-by-default test registry
    // (same convention as QteSlotEntry/registerActiveQteSlotForTesting),
    // so the mass-update loop is a safe no-op until a test registers at
    // least one "civilian" character name. Per-civilian AI-mode-skip gate
    // (0x00ad2f80, "skipped if the mode equals both 2 then 4 in sequence")
    // collapsed to one OPEN predicate per name. The arg-3 anchor resolve
    // (when given and arg 1/flee is true) is tracked as the last resolved
    // anchor NAME (no coordinate system, same project-wide gap). The
    // arg-2-forwarding two-global-write helper (0x009060e0, own role OPEN)
    // is tracked honestly as two flat globals plus a gated pointer-copy
    // counter - not a guess at 0x009060e0's real role.
    void registerCivilianForTesting(const std::string& name) { civilianNames_.push_back(name); }
    const std::vector<std::string>& civilianNames() const { return civilianNames_; }
    OpenValueMap<bool>& civilianFleeAiModeSkip() { return civilianFleeAiModeSkip_; }
    std::string& lastCivilianFleeAnchorName() { return lastCivilianFleeAnchorName_; }
    bool& civilianFleeHelperGlobalA() { return civilianFleeHelperGlobalA_; }
    bool& civilianFleeHelperGlobalB() { return civilianFleeHelperGlobalB_; }
    uint64_t& civilianFleeHelperPointerCopyCount() { return civilianFleeHelperPointerCopyCount_; }

    // group_members_alive (Sec18.14): a dedicated group-name resolver
    // (0x005eab60) and a circular sibling-linked member list - both
    // collapsed to flat, test-registerable state (same "never-populated
    // object space" precedent as the civilian registry above).
    OpenValueMap<bool>& groupResolves() { return groupResolves_; }
    OpenValueMap<bool>& groupHasMembersBit() { return groupHasMembersBit_; }
    void registerGroupMemberForTesting(const std::string& groupName, const std::string& memberName) {
        groupMemberNames_[groupName].push_back(memberName);
    }
    // Counts how many registered members are NOT dead (CharacterState::
    // isDeadHighConfidence, the SAME already-established "is dead"
    // predicate, reused directly rather than duplicated).
    int32_t groupMembersAliveCount(const std::string& groupName) {
        auto it = groupMemberNames_.find(groupName);
        if (it == groupMemberNames_.end()) return 0;
        int32_t count = 0;
        for (const auto& memberName : it->second) {
            if (!getOrCreateCharacter(memberName).isDeadHighConfidence) ++count;
        }
        return count;
    }

    // player_is_rc_driver (Sec18.10): collapses the full resolve-both-
    // sides/driver-seat-position-compare chain into one OPEN predicate per
    // character name (a pure query, same "the predicate IS the only Lua-
    // observable fact" convention as VehicleState::isTankClassPredicate).
    OpenValueMap<bool>& isRcDriverPredicate() { return isRcDriverPredicate_; }

    // character_is_player (Sec18.19): reuses objectResolves() directly -
    // the GENERIC resolver only (0x00a280c0, not the fuller #PLAYER#-then-
    // #FOLLOWER# chain), but this project's flat objectResolves() already
    // collapses every resolver variant into one shared map (Sec29's
    // standing convention) - no new state needed.

    // action_stop_synced_state (Sec18.4): the global replication-mode flag
    // (0x024d4461) - CHOSEN false (single-player-only project stance, so
    // the native always takes its documented "normal local path" branch).
    bool& globalReplicationModeFlag() { return globalReplicationModeFlag_; }

    // vehicle_set_team (Sec18.20): the six-team-id table's own real-world
    // name mapping is OPEN (Sec18.20's own flagged item) - CONFIRMED only
    // the default fallback value (9, "no match"), so this project models a
    // test-registerable name->id map with that one CONFIRMED default, not
    // an invented mapping.
    static constexpr int32_t kVehicleTeamIdDefault = 9;
    void registerVehicleTeamIdForTesting(const std::string& teamName, int32_t id) {
        vehicleTeamIdByName_[teamName] = id;
    }
    int32_t lookupVehicleTeamId(const std::string& teamName) const {
        auto it = vehicleTeamIdByName_.find(teamName);
        return it != vehicleTeamIdByName_.end() ? it->second : kVehicleTeamIdDefault;
    }

    // spawn_override_change_hood_alternate_spawning (Sec18.6): a dedicated
    // hood/zone resolver (0x00624610) collapsed to the shared objectResolves()
    // map (Sec29 convention) directly - no separate predicate needed, since
    // nothing else in scope needs to distinguish "hood" resolution from any
    // other named-object resolution. The per-hood toggle bit and the host-
    // only extra notify (reuses coopLocalIsHost() directly) are new.
    std::unordered_map<std::string, bool>& hoodAlternateSpawning() { return hoodAlternateSpawning_; }

    // dlc_gis_film_grain_unload (Sec17.29): the DLC-availability sentinel
    // (0x013010d8 == -1 means "not loaded") - CHOSEN false ("not loaded,"
    // this project models no real DLC content anywhere else either).
    OpenValue<bool>& dlcFilmGrainAvailable() { return dlcFilmGrainAvailable_; }
    bool& filmGrainFadeState() { return filmGrainFadeState_; }
    uint64_t& filmGrainLocalApplyCount() { return filmGrainLocalApplyCount_; }
    uint64_t& filmGrainReplicateCount() { return filmGrainReplicateCount_; }

    // reticle_override_clear (Sec18.23): the two reticle-override globals
    // cleared on bit 0x1 of the caller's flags byte.
    bool& reticleOverrideGlobalsCleared() { return reticleOverrideGlobalsCleared_; }
    uint64_t& reticleOverrideClearLocalCount() { return reticleOverrideClearLocalCount_; }
    uint64_t& reticleOverrideClearReplicateCount() { return reticleOverrideClearReplicateCount_; }

private:
    // The death-transition/ragdoll-or-finalize tail shared by
    // applyCharacterDamage() and applyGuaranteedLethalHit() above (spec-
    // player-runtime.md Sec15.3a) - called once a caller has already
    // established `c`'s health is at/below the zero threshold (or, for a
    // guaranteed lethal hit with no known health, unconditionally).
    void finalizeLethalCharacter(CharacterState& c, const std::string& targetName);

    std::unordered_map<std::string, CharacterState> characters_;
    std::unordered_map<std::string, VehicleState> vehicles_;
    // Mission-host Phase 2's own name->live-object resolver (see
    // liveObjectKind()/bridgeLive*Position() above) - deliberately separate
    // from characters_/vehicles_ above (which hold EVERY name any stub has
    // ever touched, test or real) and from objectResolves_ below (which
    // answers resolution, not liveness).
    std::unordered_map<std::string, LiveObjectKind> liveObjectKinds_;
    CoopSession coopSession_;
    sr3player::InputSystem inputSystem_;
    sr3player::ControlStateMachine controlStateMachine_;
    OpenValue<uint32_t> playerControlsLockHandle_{"player_controls_disable lock handle global 0x0263ae3c",
                                                  "spec-lua-api-behaviour.md Sec7.7/Sec7.22"};
    int64_t nextAudioVoiceHandle_ = 1;
    int64_t nextVisualEffectHandle_ = 1; // effect_play/explosion_create, Sec9.1/Sec9.3
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
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, VintStoredProperty>> vintProperties_;
    // Counted from const getters too (vint_get_property), hence mutable.
    mutable VintPropertyStats vintPropertyStats_;
    // Typed-path helpers (src/lua_engine_state.cpp).
    void seedVintElementDefaults(uint32_t handle, const std::string& typeName);
    // Numeric components of a stored typed value, or OpenStateError naming
    // what is missing.
    std::vector<double> vintNumbers(uint32_t handle, const char* propertyName, size_t count) const;
    std::vector<double> vintUnscaledSize(uint32_t handle) const;
    struct VintPosM { double px = 0, py = 0; double m[4] = {1, 0, 0, 1}; };
    VintPosM vintQueryPosM(uint32_t handle) const;
    // Sec5.4 auto_offset applier (vtable +0xc4, 0x00e27530) on the stored
    // auto_offset/unscaled_size - used by loadVintDocument's image path.
    void vintApplyAutoOffset(uint32_t handle);
    std::optional<uint8_t> vintTweenBorrowedCode(uint32_t tweenHandle) const;
    // Keyed by sr3save::nameHash of the data-responder name (Sec21's own
    // "hash the name with the engine's standard string-hash" text).
    std::unordered_map<uint32_t, VintDataResponderRecord> vintDataResponders_;

    // --- teleport_check_done/turn_to_check_done/move_to_check_done/
    // vehicle_pathfind_check_done (Sec35.2), see each public accessor's own
    // doc comment above for citation/reasoning. Keyed by (name, kind) - see
    // kScriptedRequestKind* above for the 3 real kind values. `bool` = done
    // (true) vs. pending (false); absent = "no matching request" (status 2).
    std::map<std::pair<std::string, int>, bool> scriptedRequestDone_;
    std::map<std::string, std::string> vehiclePathfindGoal_;   // Sec9.10a: vehicle -> goal object name
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
    TutorialStart tutorialStart_; // Sec6.19, batch 2026-10-08
    OpenValueMap<bool> objectResolves_{"named-object resolution", "spec-lua-api-behaviour.md Sec3.9/Sec10.6/Sec29"};
    // NEW 2026-10-08: the real name<->sr3zone::ObjectRecord link -
    // loadZoneObjectsSection()/resolveZoneObject()'s own backing store, see
    // those methods' own doc comments above.
    ZoneObjectResolver zoneObjects_;
    // installVehicleTypeClassTable()'s own backing store, see that method's
    // own doc comment above.
    std::unordered_map<std::string, int32_t> vehicleTypeClassByLowerName_;
    // inv_weapon_add_temporary/inv_weapon_remove_temporary (Sec4.10/
    // Sec4.11) - weaponDefinitionResolves()/weaponTemporaryInventory()'s
    // own backing store, see those methods' own doc comments above.
    OpenValueMap<bool> weaponDefinitionResolves_{"weapon-definition name resolution (0x028dc4dc, stride 0x7ec)",
                                                 "spec-lua-api-behaviour.md Sec4.10/Sec4.11"};
    bool weaponDefinitionTableComplete_ = false;
    std::unordered_map<std::string, int32_t> weaponClassByName_;
    std::unordered_map<std::string, std::unordered_map<std::string, WeaponTemporaryInventorySlot>> weaponTemporaryInventory_;
    // DIAGNOSTIC ONLY state (see enableUnresolvedNameProbe()/probedFieldOr()
    // above) - empty, and probedFieldReadCount_ zero, unless a caller
    // explicitly opts in; never populated by any normal code path or test.
    std::unordered_set<std::string> probedNames_;
    uint64_t probedFieldReadCount_ = 0;
    // DIAGNOSTIC ONLY (see enableWorldPositionProbe()/worldPositionOr()
    // above) - off unless a caller explicitly opts in; never enabled by any
    // normal code path or test.
    bool worldPositionProbeEnabled_ = false;
    uint64_t worldPositionProbeReadCount_ = 0;
    // REAL (not diagnostic) - see tryResolveWorldPositionFromZoneData()'s
    // own doc comment above. Always active, no opt-in flag.
    uint64_t zoneDataWorldPositionHitCount_ = 0;
    GetDistStallInfo getDistStall_; // feature/auto-objective-harness, 2026-10-12
    TriggerWaitStallInfo triggerWaitStall_;             // feature/auto-objective-trigger-vehicle-seat, 2026-10-12
    std::vector<std::string> triggerRegistrationOrder_; // ditto
    std::unordered_map<std::string, bool> triggerEverEntered_; // ditto
    std::vector<std::string> triggerPollQueries_;       // ditto (fix/auto-objective-driving)
    std::vector<std::string> vehicleWaitQueries_;       // ditto (feature/route-table-driving)

    // --- action_play_do (Sec22.29), see the public accessor doc comments
    // above for citation/reasoning. Test-observable only, not real engine
    // fields - same convention as vehicleExitGroupNullThisGuardCount_.
    int actionPlayDeadGuardCount_ = 0;
    int actionPlayLookupMissCount_ = 0;

    // --- zscene (Sec26.25/Sec14.23/Sec8.21), see the accessor doc comment above ---
    OpenValueMap<bool> zsceneLoadable_{"zscene table entry with kind 1 (0x00721be0 lookup, 0x00723d20 test)",
                                       "spec-lua-api-behaviour.md Sec26.25/Sec14.23"};
    bool zsceneTableInstalled_ = false;
    std::vector<ZsceneTableRow> zsceneTable_;
    // drawOrGetMaxHitPoints()'s own real-data source - see
    // installSpawnRankHitPoints()'s doc comment.
    std::vector<int32_t> spawnRankHitPoints_;
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
        // Added for the cutscene_play_do camera-script wiring task
        // (2026-10-08): known exactly when found && zsceneTableInstalled_ -
        // zsceneResolve() itself already throws (refuse()) for a found
        // entry whose kind is OPEN, so by the time this struct is returned
        // without throwing, a found/table-installed entry's kind is never
        // nullopt. nullopt in every other case (not found; no table
        // installed at all - the OPEN per-name bool map has no kind concept).
        std::optional<int> kind;
        // ZsceneTableRow::cameraScriptName of the found entry; nullopt under
        // the same conditions as kind above, OR when the row itself has no
        // CameraScript text recorded.
        std::optional<std::string> cameraScriptName;
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

    // cutscene_play_do's real camera-script step (public API doc comment
    // above cutsceneCameraScriptOutcome()/sampleCutsceneCamera()). Resolves
    // `name`'s kind, applies the 0x00721bc0 Designer-skip gate, and for
    // kind 1/2 calls the installed opener and parses the result with the
    // EXISTING sr3cutscene::camera_script reader. May throw OpenStateError
    // (propagated straight from zsceneResolve(), same pre-existing
    // behaviour cutscenePlayDo's own zscene-gate call already has).
    void cutsceneCameraScriptStep(const std::string& name);
    CameraScriptOpener cameraScriptOpener_;
    CutsceneCameraScriptOutcome cutsceneCameraScriptOutcome_ = CutsceneCameraScriptOutcome::NotAttempted;
    uint64_t cutsceneCameraScriptOpenerCalls_ = 0;
    std::optional<sr3cutscene::CameraScript> cutsceneCameraScript_;

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

    // --- game-pause depth / fade-teardown dialog predicates (Sec37.3; see
    // the accessor doc comments above for the full citation) ---
    GamePauseDepthState gamePauseDepth_;
    BareThreadTable* gamePauseThreads_ = nullptr;
    lua_State* gamePauseGameplayL_ = nullptr;
    FadeTeardownDialogState fadeTeardownDialogState_;
    uint64_t fadeTeardownSecondEffectCount_ = 0;

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
    // groupFirstNpcName()/groupNextNpcName() above (Sec28.5/Sec28.6) are
    // derived directly from groupMemberNames_ (declared alongside
    // groupResolves_/groupHasMembersBit_ below, Sec18.14) - no separate
    // backing field needed here.
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
    // New-game-with-no-save default (see kNewGameStartingCashCents's own
    // doc comment above, spec-frontend-menu-runtime.md Sec9 item 1): CHOSEN
    // application point, real write-site still OPEN.
    double playerCash_ = kNewGameStartingCashCents;
    std::vector<PurchaseRecord> purchaseLedger_;
    // grantNewGameAutoUnlocks()'s own backing store - see that method's doc
    // comment near kNewGameStartingCashCents above.
    std::unordered_set<std::string> newGamePurchasedUnlocks_;
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
    std::unordered_map<std::string, std::vector<std::string>> pendingGroupMembers_; // lower(group) -> member alias names (ce4556f Sec29)
    std::array<bool, 10> dlc2ClapboardFlags_{};        // Sec55.5, zero-fill (file-default)
    int32_t dlc2ClapboardCount_ = -1;                  // Sec55.5, CONFIRMED file-default -1
    int dlc2ClapboardsGetNegativeIndexGuardCount_ = 0; // HOST-SAFETY, test-observable only
    std::string cutscenePlayDestination1_;             // Sec57.2, "" = none/dropped
    std::string cutscenePlayDestination2_;
    bool cutscenePlayInProgress_ = false;              // host bookkeeping stand-in, not a direct spec field
    // CHOSEN (Sec57.2a's 2026-10-11 "new finding" note, see
    // cutscenePlayApplyEndPlacement()'s own doc comment above): true once
    // the current cutscene_play start's own destinations have actually been
    // applied to the player(s) - reset to false on every fresh destination
    // write (cutscenePlayDo(), even a refused one - same "written even when
    // refused" quirk the destination fields themselves already follow) so a
    // later cutscenePlayCheckDone() poll or an immediate skip-branch commit
    // each get exactly one placement per destination write, never a repeat.
    bool cutscenePlayPlacementApplied_ = false;

    // --- Native-implementation batch 2026-10-07 - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    OpenValueMap<bool> characterReadyBlocked_{"character_is_ready AI predicate (0x009a1dc0)",
                                              "spec-lua-api-behaviour.md Sec7.27"};
    OpenValueMap<bool> characterPlayingCombatMove_{"character_playing_combat_move predicate (0x009634f0)",
                                                   "spec-lua-api-behaviour.md Sec25.27"};
    std::unordered_map<std::string, PlacedObjectState> placedObjects_;
    std::unordered_map<std::string, bool> cityZoneSwaps_;
    std::unordered_map<std::string, bool> trafficLanesEnabled_;
    uint64_t partyDismissAllCount_ = 0;
    std::unordered_map<std::string, bool> sidewalkNodesDisabled_;
    bool mipStreamingPaused_ = false;

    // --- Section 6 batch (2026-10-07, full-document spec sweep) - see each
    // public accessor's own doc comment above for citation/reasoning. ---
    float pedDensity_ = 0.0f;                                   // Sec6.9, CHOSEN default
    float trafficDensity_ = 0.0f;                               // Sec6.14, CHOSEN default
    bool notorietyDecaySuppressed_ = false;                     // Sec6.18, CHOSEN default
    uint64_t waypointRemoveCount_ = 0;                          // Sec6.17
    int32_t lastWaypointRemoveMode_ = 0;                        // Sec6.17
    bool pedestrianSpawningEnabled_ = true;                     // Sec6.13, CHOSEN default
    bool vehicleSpawningEnabled_ = true;                        // Sec6.15, CHOSEN default
    uint32_t drawbridgeDisabledMask_ = 0;                       // Sec6.4
    std::unordered_map<std::string, bool> guardianAngelGroups_; // Sec6.6/Sec6.8

    // --- Section 8 batch (2026-10-07, full-document spec sweep) - see each
    // public accessor's own doc comment above for citation/reasoning. ---
    OpenValueMap<bool> personaLinePlaying_{"audio persona line currently playing (0x0094c9e0/0x0046afd0)",
                                            "spec-lua-api-behaviour.md Sec8.4"};
    std::unordered_map<int, bool> audioPlaying_; // Sec8.3/Sec8.9

    // --- Team-A-cleared batch, spec-lua-api-behaviour.md Sec1.1/1.2/1.4/
    // 1.5/1.6/2.1/2.5/2.6/3.2/3.3/3.5/3.6 - see each public accessor's own
    // doc comment above for citation/reasoning. ---
    OpenValueMap<bool> playerClassChainResolves_{"generic/#PLAYER# chain class-bit gate (0x00a27e20)",
                                                 "spec-lua-api-behaviour.md Sec3.3/Sec3.5"};
    MissionContext missionContext_;
    MissionCheckpoint missionCheckpoint_;
    MissionCancelWarpLocation missionCancelWarpLocation_;

    // spec-save-format.md Sec15 wiring (2026-10-08) - see each public
    // accessor's own doc comment above (next to missionCheckpoint()) for
    // citation/reasoning.
    SaveGameState saveGame_;
    std::string saveGameDirectory_; // set in the constructor below (defaultSaveGameDirectory())
    std::optional<std::vector<uint8_t>> lastCheckpointSnapshot_;
    uint64_t checkpointSnapshotBuildCount_ = 0;
    uint64_t autosaveFileWriteCount_ = 0;
    std::string lastAutosaveFilePath_;
    std::string lastAutosaveError_;
    struct TriggerEnableRecord {
        bool enabled = true;
        int64_t flags = 3;
    };
    std::unordered_map<std::string, TriggerEnableRecord> triggerEnable_;
    std::unordered_map<std::string, std::string> onTriggerCallbacks_;
    std::unordered_map<int64_t, uint64_t> objectiveTextClearCounts_;
    std::unordered_map<std::string, int32_t> notorietyTrackCodes_;
    std::unordered_map<uint32_t, bool> notorietyTrackFlags_;
    bool activeInputIsGamepad_ = false;

    // --- Batch 2026-10-08, wave 1 batch 1: spec-lua-api-behaviour.md §1-4
    // - see each public accessor's own doc comment above for citation. ---
    std::unordered_map<std::string, std::string> onTriggerExitCallbacks_;
    // Sec1.14 trigger-volume geometry/overlap state - see registerTriggerGeometry()/
    // triggerCurrentlyInside()'s own doc comments above.
    std::unordered_map<std::string, TriggerVolumeGeometry> triggerGeometry_;
    std::unordered_map<std::string, bool> triggerInsideState_;
    // Shop/crib walk-in press-E entry - see registerShopCribTrigger()'s own
    // doc comment above for the full reasoning (crib's own open-target/chain
    // is REAL confirmed as of commit b8c17ec; shop's remains CHOSEN).
    std::unordered_map<std::string, ShopCribTriggerKind> shopCribTrigger_;
    std::string activeShopCribTrigger_;
    uint64_t documentOpenCount_ = 0;
    std::string lastDocumentOpened_;
    std::string lastDocumentOpenTrigger_;
    // Weapon pickup (Sec5.1) - see the doc comment above
    // giveEquipWeapon()/registerWeaponDrop()/registerHeavyWeaponPickupTrigger()
    // for the full reasoning (shared give/equip writer is REAL confirmed;
    // the auto-pickup proximity radius and the press-E prompt text are both
    // CHOSEN stand-ins, same posture as the shop-entry trigger above).
    uint64_t weaponGiveEquipCount_ = 0;
    uint32_t lastWeaponGiveEquipId_ = 0;
    std::unordered_map<std::string, WeaponDropRegistration> weaponDrops_;
    uint64_t weaponDropAutoPickupCount_ = 0;
    std::unordered_map<std::string, HeavyWeaponPickupTrigger> heavyWeaponPickupTrigger_;
    std::string activeHeavyWeaponPickupTrigger_;
    uint64_t heavyWeaponPickupInteractCount_ = 0;
    std::unordered_map<std::string, bool> spawnRegionEnabled_;
    std::unordered_map<int64_t, ObjectiveTextSlot> objectiveTextSlots_;
    ConversationPlay conversationPlay_;
    OpenValueMap<bool> pegInLoadedState_{"PEG resource in a loaded state (2 or 3)", "spec-lua-api-behaviour.md Sec2.7"};
    std::unordered_map<std::string, uint64_t> pegUnloadCount_;
    int32_t uiScreenStackDepth_ = 0;
    uint64_t popScreenCount_ = 0;
    OpenValueMap<bool> triggerContainsObject_{"trigger/object containment test result (0x0093bfc0)",
                                               "spec-lua-api-behaviour.md Sec1.12"};
    std::unordered_map<std::string, TeleportRequest> teleportRequests_;
    uint64_t teleportScriptedRequestCount_ = 0;
    OpenValueMap<int32_t> releaseToWorldKind_{"release_to_world resolved-object kind (1=group/2=character/3=vehicle)",
                                               "spec-lua-api-behaviour.md Sec4.4"};
    std::unordered_map<std::string, std::vector<std::string>> releaseToWorldGroupMembers_;
    uint64_t releaseToWorldUnclassifiedCount_ = 0;
    std::unordered_map<std::string, uint64_t> releasedCount_;
    std::unordered_map<std::string, bool> continuousSpawnActive_;
    std::unordered_map<std::string, uint64_t> continuousSpawnStopCount_;
    std::unordered_map<std::string, bool> continuousSpawnLastStopBoolean_;
    std::unordered_map<std::string, ContinuousSpawnRegionRecord> continuousSpawnRegions_; // Sec4.8, batch 2026-10-08
    // continuous_spawn_start arg 4 default (type-name filter): CONFIRMED
    // "" per spec-lua-api-behaviour.md Sec4.8.
    std::string continuousSpawnDefaultTypeFilter_ = "";
    OpenValueMap<int32_t> onVehicleEnterResolverKind_{
        "on_vehicle_enter resolved-object kind (1=character bit 0x4@+0xb/2=vehicle bit 0x8@+0xb)",
        "spec-lua-api-behaviour.md Sec4.12"};

    // --- Batch 2026-10-08 (Batch 7, dedicated agent), spec-lua-api-
    // behaviour.md Sec30 ranking tranche 03, the 16 names not already
    // wired by b03 - see each public accessor's own doc comment above for
    // citation/reasoning. ------------------------------------------------
    std::unordered_set<std::string> ambientEmitterNames_;         // Sec30.3, test-populated only
    std::unordered_map<std::string, bool> ambientEmitterPaused_;  // Sec30.3
    std::map<std::pair<std::string, int>, int32_t> activeScriptRequestId_; // 0x006f62b0, Sec30.4/30.5/30.6
    int32_t nextScriptRequestSeq_ = 1;                            // wraps at 0xFFF, CONFIRMED 12-bit structure
    OpenValue<double> pathfindResult_{"0x00b2b140 return value (semantics unknown)",
                                      "spec-lua-api-behaviour.md Sec30.5/Sec30.7"};
    int vehiclePathfindNonStringArg2GuardCount_ = 0;              // HOST-SAFETY counter, Sec30.5/Sec30.7
    bool twoDTankControlsEnabled_ = false;                        // Sec30.5, global byte 0x0278cad8
    OpenValue<double> rcSignalRange_{"RC signal range global 0x0278cad4", "spec-lua-api-behaviour.md Sec30.5"};
    double localPlayerScreenDistortion1650_ = 0.0;                // Sec30.5
    bool malfunctionGlobalByte02625852_ = false;                  // Sec30.5
    bool cyberM16GlitchEffectActive_ = false;                     // Sec30.5
    int64_t nextMonotonicStamp_ = 1;                               // CHOSEN "now" stand-in, Sec30.1

    // --- Batch 2026-10-08 (Sec5-9 fan-out, Batch 2) - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    std::unordered_map<std::string, int32_t> animationStateIds_;
    std::unordered_map<std::string, int32_t> actionNameIndices_;
    std::array<HudBarSlot, kHudBarSlotCount> hudBarSlots_{};
    std::array<HudTimerSlot, kHudTimerSlotCount> hudTimerSlots_{};
    bool hudTimerGlobalActive_ = false;
    HudHardeningCounters hudHardeningCounters_;
    uint64_t hudPromptClearCount_ = 0;
    std::array<FakeNotorietySlot, 6> fakeNotorietySlots_{};

    // --- Batch 5 (ranks 251-350, spec-lua-api-behaviour.md Sec15/Sec16),
    // 2026-10-08 - see each public accessor's own doc comment above for
    // citation/reasoning. ---
    bool refractionGlobalOverrideCleared_ = false;       // Sec15.5
    uint64_t refractionClearLocalCount_ = 0;
    uint64_t refractionClearReplicateCount_ = 0;
    HudDisplayStatePool hudDisplayStatePool_;             // Sec15.6/15.12/15.24/15.25
    std::vector<QteSlotEntry> qteActiveSlots_;
    // Wave 2 batch W2-1 (b15) - see each accessor's own doc comment above.
    OpenValue<int32_t> moveToActionIdCounter_{"move_to_do action-id counter (0x0130cb94) initial value",
                                              "spec-lua-api-behaviour.md Sec21.22"};
    OpenValue<double> vehicleNavmeshDispatchResult_{"0x00b24fa0 navmesh-pathfind dispatcher raw result",
                                                    "spec-lua-api-behaviour.md Sec22.1"};
    uint64_t qteCleanupCount_ = 0;
    int32_t personaGlobalSituationOverride_ = 0;          // Sec15.7
    uint64_t personaGlobalSituationOverrideResetCount_ = 0;
    std::unordered_map<std::string, bool> missionUnlocked_;       // Sec15.9 - legacy facade, see writeMissionUnlockedFlagBit()
    std::unordered_map<std::string, bool> missionCompletedMap_;   // Sec15.10 - legacy facade, see writeMissionCompletionFlagBits()
    uint64_t missionSetCompletedCallCount_ = 0;
    // The REAL mission handle's own +0x88 packed multi-bit flags byte
    // (Sec21.27b) - see writeMissionCompletionFlagBits()/
    // writeMissionUnlockedFlagBit()/missionCompletionFlagBitSet() (this
    // header's Sec15.9/Sec15.10 section) for its only writers/reader.
    std::unordered_map<std::string, uint8_t> missionFlagBits_;
    // `0x006db7fa`'s own per-mission "silent"-class gate (Sec21.27b) - see
    // missionSilentClass()'s own doc comment (MissionContext's own section,
    // above) for why this is OPEN/test-settable with no real population.
    OpenValueMap<bool> missionSilentClass_{"mission class-descriptor bit 0x20 ('silent')",
                                            "spec-lua-api-behaviour.md Sec21.27b"};
    int64_t nextMessageHandle_ = 1;                       // Sec15.11
    double playerSkydiveFallSpeed_ = 0.0;                 // Sec15.18, CHOSEN placeholder (OPEN default literal, no in-scope reader)
    uint32_t gameChoiceBitmask_ = 0;                      // Sec15.26
    uint64_t gameChoiceSetCount_ = 0;
    uint64_t gameChoiceIndexOutOfRangeGuardCount_ = 0;    // HOST-SAFETY
    uint64_t gameChoiceIndex0NotifyCount_ = 0;
    uint64_t gameChoiceIndex1AchievementHookAttemptCount_ = 0;
    std::array<int32_t, kDlcAchievementCounterSlotCount> dlcAchievementCounters_{}; // Sec15.27
    uint64_t dlcAchievementCounterNegativeIndexGuardCount_ = 0; // CHOSEN (hardened divergence)
    int32_t currentLanguageIndex_ = -1;                   // Sec16.1
    OpenValue<double> respectMultiplier_{"game_award_respect multiplier global 0x012fd1b0", "spec-lua-api-behaviour.md Sec16.2"};
    uint64_t respectGrantCount_ = 0;
    uint64_t respectGrantTargetedCount_ = 0;
    uint64_t respectGrantBroadcastCount_ = 0;
    uint64_t respectGrantNotifyCount_ = 0;

    // Sec5.4 respect/progression live runtime (see the public API block
    // above, near recordRespectGrant, for every field's own doc comment).
    uint32_t respectTotal_ = 0;   // +0x1ed0
    uint32_t respectBanked_ = 0;  // +0x1ed4
    uint32_t respectLevel_ = 0;   // +0x1ed8, 0..kRespectLevelCap
    std::vector<sr3tables_progression::RespectLevel> respectLevelTable_;
    std::unordered_map<std::string, bool> respectUnlockablesFlag_;
    uint64_t respectHomieSlot1UnlockCount_ = 0;
    std::unordered_map<int32_t, uint64_t> statRecordOpenCounts_;
    RespectDecadeMilestone respectDecadeMilestone_;
    WorldPosition respectMilestonePlayerPosition_;
    uint64_t respectAwardNonPositiveGuardCount_ = 0;

    bool tutorialHudHelpClosing_ = false;                 // Sec16.3
    std::array<ColorGradingLutSlot, 4> colorGradingLutSlots_{}; // Sec16.4
    int64_t nextLutId_ = 1;
    bool completionCutsceneWasActive_ = false;            // Sec16.5
    PcrCustomizationState pcrCustomization_;              // Sec16.6/16.7/16.8/16.8a
    uint64_t dialogBoxDisconnectCount_ = 0;               // Sec16.9
    bool mainMenuProfileChangeSuppressed_ = false;        // Sec16.10
    int32_t pauseMenuControlScheme_ = -1;                 // Sec16.11, CHOSEN "none" default
    int32_t pauseMenuPresetScheme0_ = 0;
    int32_t pauseMenuPresetScheme1Or2_ = 0;
    std::map<std::pair<int32_t, int32_t>, PauseMenuOptionRequest> pauseMenuOptionRequests_; // Sec16.12
    std::unordered_map<int32_t, uint64_t> pauseMenuRestoreDefaultsCounts_;                  // Sec16.13
    bool storeCommonBgAnimComplete_ = false;              // Sec16.14
    bool storeWeaponPreviewActive_ = false;               // Sec16.15
    int32_t storeWeaponPreviewSlot_ = -1;
    uint64_t storeWeaponDetachCount_ = 0;
    uint64_t storeWeaponHideWeaponCallCount_ = 0;

    // --- Batch 4 (Sec13+Sec14, native-implementation batch 2026-10-08) -
    // see each public accessor's own doc comment above for citation/
    // reasoning. ---
    OpenValueMap<bool> notorietySpawnGroupResolves_{"notoriety spawn-group resolution (0x008ed350)",
                                                     "spec-lua-api-behaviour.md Sec13.2"};
    std::unordered_map<std::string, NotorietySpawnGroupState> notorietySpawnGroups_;

    // --- batch b29 (2026-10-08): the rest of Section 6 - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    std::unordered_map<std::string, int32_t> notorietyAliasTrackCodes_; // 0x00605160, seeded below
    std::unordered_map<int32_t, NotorietyAliasTrackState> notorietyAliasTrackStates_;
    uint64_t notorietyUiMeterResetCount_ = 0;
    OpenValueMap<bool> notorietyGroupTierInRangeResolves_{"notoriety group tier in-range (+0x3e0/+0x3e4, per-group not given)",
                                                           "spec-lua-api-behaviour.md Sec6.2"};
    std::vector<HudPromptRequest> hudPromptRequests_;
    uint64_t hudPromptDefaultResetCount_ = 0;
    bool hudPromptActive_ = false;            // 0x02703cac != 0, Sec13.3
    std::string hudPromptCallbackName_;       // slot 9 callback name, Sec13.3
    std::vector<CustomizationOutfitWearRequest> customizationOutfitWearRequests_;
    std::vector<CustomizationItemWearRequest> customizationItemWearRequests_;
    bool customizationRevertPending_ = false;
    uint64_t customizationItemRevertCount_ = 0;
    uint64_t customizationUiRefreshTailCount_ = 0;
    uint64_t ragdollLastResortPositionClearCount_ = 0;
    std::vector<RefractionSituationRequest> refractionSituationRequests_;
    std::unordered_set<std::string> knownAchievements_;
    uint64_t achievementToastCount_ = 0;
    uint64_t achievementReplicateCount_ = 0;
    uint64_t achievementCompletionCount_ = 0;
    // Renamed from hudTimerSlots_ during merge to resolve a duplicate-member
    // collision with Batch 2's own differently-typed hudTimerSlots_ field
    // above (std::array<HudTimerSlot, kHudTimerSlotCount>, Sec8.19/8.20) -
    // same name, unrelated real mechanisms. Public accessor name unchanged.
    HudTimerSlots hudTimerSlotsPool13_;
    HudCriticalTimerState hudCriticalTimer_;
    uint64_t hudCriticalTimerLogCount_ = 0;
    uint64_t hudCriticalTimerNotifyCount_ = 0;
    std::unordered_map<std::string, int32_t> hudCriticalTimerSecondaryKeyIds_;
    int32_t nextHudCriticalTimerSecondaryKeyId_ = 1;
    std::unordered_map<std::string, uint8_t> spawnRegionFilterBits_;
    std::unordered_map<std::string, int32_t> aiPersonalityProfileCodes_;
    std::unordered_map<std::string, int32_t> behaviorSetCodes_;
    OpenValueMap<bool> syncedActionAppliesLocally_{"action_play_synced_state's own per-actor local-authority gate",
                                                   "spec-lua-api-behaviour.md Sec14.1"};
    MissionExcludedRewardsState missionExcludedRewards_;
    DebrisFlowState debrisFlow_;
    CameraRestrictLimits cameraRestrictLimits_;
    OpenValue<std::string> localPlayerName_{"local player's own resolved name (0x009da4e0 stand-in)",
                                            "spec-lua-api-behaviour.md Sec14.21"};
    bool cameraRestrictionActive_ = false; // Sec8.12, batch b30
    OpenValueMap<bool> conversationSlotPlaying_{"audio_conversation_playing per-slot active flag (0x00877dc0)",
                                                "spec-lua-api-behaviour.md Sec14.22"};
    double drugEffectOverrideA_ = 0.0;
    double drugEffectOverrideB_ = 0.0;
    bool roadblocksEnabled_ = true;
    uint64_t roadblocksDisableCleanupCount_ = 0;
    bool cribInterfaceDisabled_ = false;
    bool actionNodesEnabled_ = false;
    CameraShakeState cameraShake_;
    uint64_t cameraEndLookThroughCount_ = 0;
    uint64_t cameraEndLookThroughSmoothCount_ = 0;

    // --- Batch 3 (2026-10-08), spec-lua-api-behaviour.md Sec11+Sec12 - see
    // each public accessor's own doc comment above for citation/reasoning. ---
    OpenValue<bool> pcrPlasticSurgeonMode_{"pcr_is_plastic_surgeon global byte", "spec-lua-api-behaviour.md Sec11.1"};
    bool pcrDirty_ = false;
    PcrMorphState pcrMorph_;
    PcuStreaming pcuStreaming_;
    OpenValue<bool> hordeModeActive_{"horde_mode_is_active global byte (0x006e44f0)", "spec-lua-api-behaviour.md Sec11.6"};
    uint64_t gameAudioStopCount_ = 0;
    StoreCameraPreset storeCameraPreset_;
    PauseMenuOptions pauseMenuOptions_;
    VintRemapStaging vintRemapStaging_;
    uint64_t hudCriticalTimerStopCount_ = 0;
    std::unordered_map<std::string, MissionCellphoneAudio> missionCellphoneAudio_;
    WeaponSlotState weaponSlots_;
    std::vector<OnVehicleExitEvent> onVehicleExitEvents_;
    uint64_t guardianAngelNodeProgressCount_ = 0;
    std::unordered_set<std::string> radioBlockedVehicles_;
    CameraRestrictTarget cameraRestrictTarget_;

    // =========================================================================
    // Wave 2, Batch W2-3 (2026-10-08), spec-lua-api-behaviour.md Sec20+Sec21 -
    // backing storage for the public accessors declared above.
    // =========================================================================
    uint64_t dlcAchievementCounterGetNegativeIndexGuardCount_ = 0;
    bool grenadesDisableFlagA_ = false;
    bool grenadesDisableFlagB_ = false;
    bool tutorialVehicleControlsEnabled_ = false;
    bool copAmbientSpawnsAllowed_ = true;
    int64_t personaSuppressAmbientLinesCounter_ = 0;
    OpenValueMap<std::string> teamNameByObject_{"per-object team-name reverse lookup (0x02623a80)",
                                                "spec-lua-api-behaviour.md Sec20.23"};
    std::unordered_map<std::string, NotorietyOverrideTimer> notorietyOverrideTimers_;
    OpenValueMap<bool> hudScreenResolves_{"HUD screen registry (0x012fced8) name resolution",
                                          "spec-lua-api-behaviour.md Sec21.6"};
    OpenValueMap<int32_t> hudScreenTypeField_{"HUD screen's own +0x30 field", "spec-lua-api-behaviour.md Sec21.6"};
    std::vector<std::string> hudScreenStack_;
    std::vector<std::string> continuousExplosionAvoidObjects_;
    std::unordered_set<std::string> charactersOnFire_;
    OpenValueMap<bool> actionNodeTypeNameValid_{"action-node type-name validity (0x008c8550/0x008c7ba0)",
                                                "spec-lua-api-behaviour.md Sec21.2"};
    ReticleOverride reticleOverride_;
    SkydiveCatchingShaundi skydiveCatchingShaundi_;
    OpenValueMap<std::string> bestWeaponSelectionByCode_{"inv_item_equip_slot's own best-weapon-selector resolution",
                                                         "spec-lua-api-behaviour.md Sec21.23"};
    std::unordered_map<std::string, int32_t> effectIdByName_;

    // --- Wave 2, Batch W2-4 (Sec22+Sec24+Sec25 gameplay residue, 78 names,
    // 2026-10-08) - see each public accessor's own doc comment above for
    // citation/reasoning. ---
    bool spotlightHeliLaserEnabled_ = false;
    bool roadblocksAmbientTrafficEnabled_ = false;
    uint64_t roadblocksAmbientTrafficNoSessionSkipCount_ = 0;
    bool notorietySpawnOnlyAir_ = false;
    NotorietyRestrictedZones notorietyRestrictedZones_;
    bool baseJumpingEnabled_ = true;
    uint64_t baseJumpingReplicateCount_ = 0;
    bool dlcGisFilmGrainActive_ = false;
    uint64_t dlcGisFilmGrainLoadLatchSetCount_ = 0;
    uint64_t dlcGisFilmGrainLoadNotifyCount_ = 0;
    WindOverride windOverride_;
    bool actionStopSyncedStateFlag_ = false;
    BossBattleKbCrowdProps bossBattleKbCrowdProps_;
    bool bossBattleMattCheatsActive_ = false;
    uint64_t bossBattleMattCheatsStopCount_ = 0;
    std::unordered_map<std::string, bool> triggerTypeDisabled_;
    uint64_t triggerTypeEnableNotifyCount_ = 0;
    std::unordered_map<std::string, bool> triggerHoldToRepeat_;
    double skydiveElevAngleA_ = 0.0;
    double skydiveElevAngleB_ = 0.0;
    std::string currentRadioNewsbreak_;
    bool npcDropGrenades_ = false;
    std::unordered_map<int, int> tutorialUnlockCounts_;
    uint64_t messageRemoveAllCount_ = 0;
    uint64_t missionSetNextMissionNoopCount_ = 0;
    std::optional<bool> missionStorePurchased_;
    std::optional<bool> missionPropertyPurchased_;
    bool hudInventoryDisabled_ = false;
    uint64_t hudInventoryDisableReplicateCount_ = 0;
    uint64_t hudInventoryDisableSecondaryCallCount_ = 0;
    double hudCriticalTimerRemainder_ = 0.0;
    std::unordered_map<std::string, int32_t> ingameEffectIndices_;
    uint64_t ingameEffectRemoveNotifyCount_ = 0;
    uint64_t ingameEffectRemoveClearedCount_ = 0;
    std::unordered_map<std::string, ContinuousSpawnOrder> continuousSpawnOrders_;
    uint64_t groupDestroyedCount_ = 0;
    uint64_t isRappellingResolveFailureSafeCount_ = 0;
    std::unordered_map<std::string, std::string> namedHookCallbacks_;
    OpenValueMap<bool> hudScreenNameResolves_{"HUD-screen-name registry (0x012fced8)",
                                              "spec-lua-api-behaviour.md Sec24.24"};
    std::vector<RandomObjKilledHook> randomObjKilledHooks_;

    // =====================================================================
    // Wave 2, Batch W2-7 (2026-10-08) private backing fields - see each
    // public accessor's own doc comment above for citation/reasoning.
    // =====================================================================
    std::unordered_map<int64_t, RoadblockRecord> roadblocks_;
    int64_t nextRoadblockHandle_ = 1;
    OpenValueMap<bool> roadblockPlacementResolves_{"roadblock placement-object resolve (no road-network data here)",
                                                   "spec-lua-api-behaviour.md Sec39.2"};
    // Start-up value 200.0: RESOLVED 2026-10-12 (Sec39.2), tweak_table.xtbl `RC_vehicle_gun_max_distance`
    // (seeded by applySpecInitialState()).
    OpenValue<double> rcMaxSignalRange_{"RC max signal range global float",
                                        "spec-lua-api-behaviour.md Sec39.2"};
    bool gangAmbientSpawnsAllowed_ = false;
    std::unordered_map<std::string, bool> spawnRegionLos_;
    bool copsShootingFromVehicles_ = false;
    bool saintsHated_ = false;
    bool stagNotorietyAreaActive_ = false;
    uint64_t stagNotorietyAreaHostStepCount_ = 0;
    double spotlightHeliLaserSize_ = 1.0;
    bool weaponPhysicsEnabled_ = true;
    bool vehicleForcedCorpseRemovalEnabled_ = true;
    uint64_t vehicleDetonateRandomNoReferenceGuardCount_ = 0;
    uint64_t vehicleDetonateRandomFailedReferenceGuardCount_ = 0;
    uint64_t vehicleDetonateRandomResolvedReferenceNoOpCount_ = 0;
    uint64_t forceThrowCharNullCharacterGuardCount_ = 0;
    RappelQueue rappelQueue_;
    uint64_t rappelEnterQueuedCount_ = 0;
    uint64_t rappelEnterImmediateCount_ = 0;
    uint64_t rappelExitCount_ = 0;
    uint64_t projectileFireFromNavpointCount_ = 0;
    uint64_t vtolFlybyM18BroadcastCount_ = 0;
    std::unordered_map<std::string, int32_t> radioStationByVehicle_;
    bool singAlongAllowedDuringMissions_ = false;
    SingAlongTrack singAlongTrack_;
    OpenValueMap<int32_t> songNameToIndex_{"radio song-name -> index resolver (no real song table here)",
                                           "spec-lua-api-behaviour.md Sec51.3"};
    int32_t parkingSpotMax_ = 18;
    OpenValueMap<bool> pathObjectIsPath_{"path-kind object resolver (no object-kind table here)",
                                        "spec-lua-api-behaviour.md Sec42.6"};
    OpenValueMap<ClosestNpcResult> closestNpcOverride_{"get_closest_npc_to_object query (no coordinate system here)",
                                                       "spec-lua-api-behaviour.md Sec41.6"};
    OpenValueMap<std::string> closestObjectOverride_{"get_closest_object query (no coordinate system here)",
                                                     "spec-lua-api-behaviour.md Sec53.5"};
    std::unordered_map<std::string, MeshMoverRecord> meshMovers_;
    uint64_t meshMoverApplyImpulseCount_ = 0;
    std::unordered_map<std::string, uint64_t> meshMoverResetByZoneCounts_;
    OpenValueMap<bool> fovCheckXzPlaneResult_{"fov_check_xz_plane XZ-cone test (no coordinate system here)",
                                              "spec-lua-api-behaviour.md Sec44.5"};
    bool audioSuppressAmbientPlayerLines_ = false;
    bool killbaneQteActive_ = false;
    bool killbaneCleanupPerformed_ = false;
    bool scriptCameraOwnsView_ = false;
    uint64_t cameraScriptBroadcastCount_ = 0;
    bool cutsceneWasSkippedFlag_ = false;
    std::unordered_set<int64_t> particleDebrisFlowKnownIds_;
    uint64_t debrisFlowSetInactiveNullHandleGuardCount_ = 0;
    std::vector<std::string> continuousExplosionFollowTargets_;
    std::unordered_map<std::string, int> lightGroupAttachRefCounts_;
    int lightGroupAttachedSlotsUsed_ = 0;
    uint64_t levelLightEnableAllCount_ = 0;
    OpenValueMap<int32_t> losCheckResult_{"los_check_do cached line-of-sight result (no geometry here)",
                                          "spec-lua-api-behaviour.md Sec54.2"};
    int dlc2ClapboardsSetNegativeIndexGuardCount_ = 0;
    Dlc3Punch dlc3Punch_;
    bool flashlightsEnabledGlobal_ = true;
    bool audioSystemInitialized_ = true;
    uint64_t audioEventUninitializedGuardCount_ = 0;
    std::unordered_map<std::string, AirplaneRecord> airplanes_;
    uint64_t airplaneFlyToLeakedRequestCount_ = 0;
    uint64_t airplaneFlyToNullTargetGuardCount_ = 0;
    CameraLookThrough cameraLookThrough_;
    SpawnGlobalOverride spawnGlobalOverride_;
    // spawnOverrideCategoryForHood_ (the old disconnected per-hood-clear-only
    // placeholder map) removed 2026-10-09 (spec sync 3ecbf84): SET and CLEAR
    // now both read/write the SAME real state, SpawnOverride::perHoodCategory/
    // ::perHoodCategoryNullStored above - see spawnOverrideClearOverrideForHood()'s
    // own doc comment for the bug this fixes.
    float spawnRegionMinSpawnDistSquared_ = 2500.0f; // CONFIRMED file-backed default, 50.0f^2 (spec sync 3ecbf84)
    std::unordered_map<std::string, bool> triggerAllowDeadGuys_;
    bool stagActive_ = false;
    bool waypointPlaced_ = false;

    // --- Batch W2-5 (2026-10-08), wave 2 fan-out - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    CustomizationCreationState customizationCreation_;
    PcrVoiceState pcrVoice_;
    PcrTriangleCursor pcrTriangleCursor_;
    PcrReportCounters pcrReportCounters_;
    PcrRandomComposite pcrRandomComposite_;
    PcrPresetState pcrPreset_;
    PcrTattooPurchase pcrTattooPurchase_;
    PcrRandomizePalettes pcrRandomizePalettes_;
    PcrSkinColorChange pcrSkinColorChange_;
    PcrCreationComplete pcrCreationComplete_;
    uint64_t pcrAddDefaultClothingWipeCount_ = 0;
    uint64_t pcrStripClothingCount_ = 0;
    uint64_t pcrRestoreClothingCount_ = 0;
    PcuClothingState pcuClothing_;
    PcuOwnedItemQueries pcuOwnedItemQueries_;
    PcuAggregateCounters pcuAggregate_;
    PcuCreatorMisc pcuCreatorMisc_;
    StoreFrontendState storeFrontend_;
    GarageFrontendState garageFrontend_;
    CribFrontendState cribFrontend_;
    GangCustomizationState gangCustomization_;

    // --- Wave 2 batch W2-6 (2026-10-08) - see each public accessor's own
    // doc comment above for citation/reasoning. ---
    InterfaceEffectState interfaceEffect_;
    bool onlineValidatorChecking_ = false;
    CellphoneVehicleDelivery cellphoneVehicleDelivery_;
    std::unordered_set<std::string> audioEventRegistry_;
    RemotePlayerInfo remotePlayer_;
    bool completionScreenFinishedFlag_ = false;
    int32_t equippedGrenadeSlot_ = -1;
    OpenValue<bool> radialMenuQuickItemSucceeds_{"radial-menu quick-item use outcome (0x00600720)",
                                                  "spec-lua-api-behaviour.md Sec26.18"};
    uint64_t radialMenuSlot12Count_ = 0;
    VirtualKeyboardState virtualKeyboard_;
    OpenValue<bool> virtualKeyboardCloseResult_{"game_vkeyboard_input synchronous close result (0x0059f8a0)",
                                                 "spec-lua-api-behaviour.md Sec26.20"};
    OpenValue<bool> steamLoggedOn_{"Steam logged-on state (ISteamUser::BLoggedOn)", "spec-lua-api-behaviour.md Sec23.13/Sec44.2"};
    SyslinkSearch syslinkSearch_;
    SteamOverlayCounters steamOverlay_;
    int32_t difficultyLevel_ = 1; // CONFIRMED file-default
    uint64_t axisActionUnknownNameGuardCount_ = 0;
    std::unordered_map<int32_t, int32_t> diversionSlotOccupancy_;
    MainMenuNewGame mainMenuNewGame_;
    int32_t coopMenuType_ = 0;
    CoopKickState coopKick_;
    int32_t hudHideRefCount_ = 0;
    bool autosaveInProgressSimplified_ = false;
    JoinableState joinable_;
    uint64_t syslinkJoinCount_ = 0;
    int32_t syslinkJoinLastIndex_ = -1;
    HordeLobbyState hordeLobby_;
    OpenValue<bool> machinimaPathBuilderSucceeds_{"machinima clip path-builder success (process-token/folder lookup/creation)",
                                                   "spec-lua-api-behaviour.md Sec44.4/Sec56.5"};
    MachinimaState machinima_;
    PauseMapFilterState pauseMapFilter_;
    ProtoSelectState protoSelect_;
    uint64_t gameRecordAmbiguousRefusalCount_ = 0;
    bool gameTerminateRequested_ = false;
    OpenValue<bool> imperialUnitsOverrideForTesting_{"game_use_imperial_units test override", "spec-lua-api-behaviour.md Sec56.3"};
    GameStateStack gameStateStack_;
    GamePlayMode gamePlayMode_;
    OpenValue<uint32_t> coopPing_{"co-op ping (first non-local session member +0xb200)", "spec-lua-api-behaviour.md Sec55.1"};
    MissionCancelState missionCancel_;
    int32_t dialogPoolSlotsInUse_ = 0;
    DialogUiState dialogUi_;
    CellUiState cellUi_;
    PlaylistState playlist_;
    SaveGameExtra saveGameExtra_;
    SaintsbookState saintsbook_;
    MainMenuVideo mainMenuVideo_;
    OpenValue<bool> mainMenuVideoCreateSucceeds_{"main_menu_restart_video video-object creation", "spec-lua-api-behaviour.md Sec50.1"};
    int32_t killbaneSelectedOption_ = -1;
    uint64_t killbaneCallbackFiredCount_ = 0;
    ObjectIndicatorInitLua objectIndicatorInitLua_;
    PcOptionsState pcOptions_;
    bool pauseMenuHostInCharacterCreation_ = false;
    bool catMouseMinigameActive_ = false;
    uint64_t pauseMenuCommonTopFinishedLoadingCount_ = 0;
    MainMenuMatchmaking mainMenuMatchmaking_;
    uint64_t mainMenuContinueCount_ = 0;
    uint64_t getCharInStringNullGuardCount_ = 0;
    OpenValueMap<std::string> localizedStringForTag_{"localized string for tag", "spec-lua-api-behaviour.md Sec53.5"};
    OpenValue<double> worldIncomeDollars_{"world income dollars", "spec-lua-api-behaviour.md Sec53.5"};
    uint64_t vintOptionsRemapResetCount_ = 0;
    PauseMenuHorde pauseMenuHorde_;
    bool southpawControlScheme_ = false;
    bool vehicleSouthpawControlScheme_ = false;
    PauseMapZoomState pauseMapZoomState_;
    PauseMapBookmarks pauseMapBookmarks_;
    uint64_t pauseMapDragMapCount_ = 0;
    PauseMenuControlSchemeSets pauseMenuControlSchemeSets_;
    bool cityLoadImgComplete_ = false;
    uint64_t communityLoginOpenCount_ = 0;
    int32_t completionCallbackCaseSelected_ = -1;
    OpenValue<uint32_t> radioSongTableCount_{"radio song table count", "spec-lua-api-behaviour.md Sec57.4"};
    uint64_t playlistSaveOutOfRangeDroppedCount_ = 0;
    uint64_t playlistSaveNegativeCountClampedCount_ = 0;
    uint64_t playlistSaveCountCappedCount_ = 0;
    std::vector<std::vector<int32_t>> savedPlaylists_;
    CinemaEditorState cinemaEditor_;
    AutilHudMayhemInit autilHudMayhemInit_;
    bool pauseMenuQuitRequested_ = false;
    uint64_t completionCameraShakeCount_ = 0;
    bool completionCreditsWereActive_ = false;
    bool completionCutsceneWasSkipped_ = false;
    uint64_t mainMenuRedeemCodeOpenCount_ = 0;
    CellphoneDialState cellphoneDial_;
    uint64_t audioRtpcAppliedCount_ = 0;
    std::string currentMissionStem_;
    std::unordered_map<int32_t, int32_t> cheatContentId_;

    // --- Batch W2-8 (2026-10-08), wave-2 §38-60 gameplay B - see each
    // public accessor's own doc comment above for citation/reasoning. ---
    HookNamePool onHookNamePool_;
    std::unordered_map<std::string, W28GroupVisibility> w28GroupVisibility_;
    uint64_t groupShowOnUncreatedGuardCount_ = 0;
    uint64_t groupHideOnUncreatedGuardCount_ = 0;
    double sprintUseOverride_ = -1.0;
    double sprintRechargeOverride_ = -1.0;
    double sprintDelayOverride_ = -1.0;
    bool localPlayerNaked_ = false;
    bool coopPlayerNaked_ = false;
    bool rcGunActiveFlag_ = false;
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> inventoryAmmo_;
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> inventoryItemCounts_;
    std::unordered_map<std::string, bool> dualWieldFlags_;
    std::unordered_map<std::string, std::string> itemAnimGroups_;
    std::unordered_map<std::string, int32_t> itemWeaponSlots_;
    std::unordered_map<std::string, int32_t> weaponUpgradeLevels_;
    std::unordered_map<int32_t, double> notorietyTrackValues_;
    uint64_t notorietyGetNonStringArgGuardCount_ = 0;
    std::array<bool, 8> teamIdleFlags_{};
    uint64_t aiForceTeamIdleNullNameGuardCount_ = 0;
    ActionSequenceScriptBlock actionSequenceScript_;
    uint64_t actionSequenceIsPlayingNullPlayerGuardCount_ = 0;
    OpenValueMap<double> tutorialCaseNumber_{"tutorial entry +0x10 field (tutorial_get_case's own case-number)",
                                              "spec-lua-api-behaviour.md Sec38.2"};
    OpenValue<bool> tutorialVisible_{"tutorial_visible's own ~11-condition visibility predicate",
                                     "spec-lua-api-behaviour.md Sec38.2"};
    uint64_t tutorialStopCurrentCount_ = 0;
    uint64_t performKillswitchMissingKillbaneGuardCount_ = 0;
    std::unordered_map<std::string, bool> actionPressedFlags_;
    std::unordered_map<std::string, int32_t> customVoiceIds_;
    uint64_t playerGetCustomVoiceUnresolvedGuardCount_ = 0;
    uint64_t qteSkyqte01NullPartnerGuardCount_ = 0;
    uint64_t qteSkyqte01DeadPartnerGuardCount_ = 0;
    uint64_t qteSkyqte01StartCount_ = 0;
    uint64_t messageRemoveCount_ = 0;
    uint64_t minimapIconAddRadiusGuardCount_ = 0;
    int32_t vintDialogResult_ = -2;
    uint64_t openVintDialogDoCount_ = 0;
    uint64_t missionMaybeUncompleteM22Count_ = 0;
    std::unordered_map<std::string, int32_t> homieMissionOverride_;
    int32_t hudTextAdventureScreenIndex_ = -1;
    int32_t hudProtoSelection_ = -1;
    std::array<HudXOfYSlot, kHudXOfYSlotCount> hudXOfYSlots_{};
    uint64_t hudXOfYAddOobGuardCount_ = 0;
    uint64_t hudXOfYUpdateOobGuardCount_ = 0;
    uint64_t hudXOfYRemoveOobGuardCount_ = 0;
    std::unordered_map<std::string, bool> revivedFlags_;
    std::unordered_map<std::string, bool> revivedByLocalPlayerFlags_;
    std::unordered_map<std::string, int64_t> marsKbInteractableId_;
    uint64_t marsKbInteractableLeakCount_ = 0;
    FlashpointState flashpointState_;
    uint64_t actionPlayCustomDoUnresolvedGuardCount_ = 0;
    std::unordered_map<std::string, W28CharacterMisc> w28CharacterMisc_;
    bool partyMeleeOnlyLocal_ = false;
    bool partyMeleeOnlyCoop_ = false;
    uint64_t parachuteOpenSpawnCount_ = 0;
    std::unordered_map<std::string, std::string> onDismountCallbacks_;
    std::unordered_map<std::string, std::string> onHitPedCharacterCallbacks_;
    std::unordered_map<std::string, std::string> onHitPedVehicleCallbacks_;
    std::unordered_map<std::string, std::string> onVehicleDebrisFlowRecycledCallbacks_;
    std::unordered_map<std::string, std::string> onMountCallbacks_;
    std::string onM21PlayerChoiceCallback_;
    std::string onMissionItemDropCallback_;
    uint64_t minimapIconAddScriptGroupGuardCount_ = 0;
    uint64_t minimapIconRemoveScriptGroupGuardCount_ = 0;
    uint64_t objectIndicatorAddScriptGroupGuardCount_ = 0;
    // --- Wave 2, Batch W2-2 (2026-10-08), spec-lua-api-behaviour.md
    // Sec17+Sec18 - see each public accessor's own doc comment above for
    // citation/reasoning. (stagActive_ itself is NOT re-declared here -
    // reuses the existing field above, see set_stag_active's own doc
    // comment on the public accessor side.) ---
    bool partyRecruitable_ = false;
    bool flashpointsEnabled_ = true;
    bool notorietyForceNoAirSpawn_ = false;
    bool radioGlobalBlocked_ = false;
    bool bossBattleMattFlying_ = false;
    double helicopterJitterMin_ = 0.0;
    double helicopterJitterMax_ = 0.0;
    double playerGrenadeGlobalA_ = 0.0;
    double playerGrenadeGlobalB_ = 0.0;
    uint64_t playerGrenadesEnableCallCount_ = 0;
    bool sprintEnabled_ = true;
    bool sprintBit2Cleared_ = false;
    bool sprintModeIsFive_ = false;
    uint64_t sprintDisableCount_ = 0;
    bool parkingSpotSlot1Enabled_ = true;
    std::string onPurchaseCallback_;
    std::string forcedSeatVehicleName_;
    std::string onHumanKilledCallback_;
    std::string onHumanKilledFilterName_;
    std::unordered_set<std::string> humanKindResolves_;
    int32_t personaDlcBundle_ = 0;
    bool missionAutosaveSteamCheckOk_ = true;
    bool missionAutosaveBusy_ = false;
    bool missionAutosaveChainReady_ = true;
    bool missionAutosaveDeferredPending_ = false;
    uint64_t missionAutosaveStubCallCount_ = 0;
    uint64_t missionAutosaveKickoffCount_ = 0;
    OpenValueMap<bool> lightGroupResolves_{"light-group name resolution (0x00739690)", "spec-lua-api-behaviour.md Sec17.24"};
    std::unordered_map<std::string, LightGroup> lightGroups_;
    bool inventoryItemTableComplete_ = false;
    bool items3dTableComplete_ = false;
    OpenValueMap<bool> inventoryItemDefinitionResolves_{"inventory item definition resolution (0x008dcb10/0x005feaa0)",
                                                         "spec-lua-api-behaviour.md Sec17.25"};
    OpenValueMap<bool> propDefinitionResolves_{"character prop/customization-item definition resolution",
                                               "spec-lua-api-behaviour.md Sec17.13"};
    std::vector<std::string> civilianNames_;
    OpenValueMap<bool> civilianFleeAiModeSkip_{"set_civilians_flee's own per-civilian AI-mode-skip gate (0x00ad2f80)",
                                               "spec-lua-api-behaviour.md Sec17.4"};
    std::string lastCivilianFleeAnchorName_;
    bool civilianFleeHelperGlobalA_ = false;
    bool civilianFleeHelperGlobalB_ = false;
    uint64_t civilianFleeHelperPointerCopyCount_ = 0;
    OpenValueMap<bool> groupResolves_{"group-name resolution (0x005eab60)", "spec-lua-api-behaviour.md Sec18.14"};
    OpenValueMap<bool> groupHasMembersBit_{"group 'has members' bit (0x2@+0x3a)", "spec-lua-api-behaviour.md Sec18.14"};
    std::unordered_map<std::string, std::vector<std::string>> groupMemberNames_;
    OpenValueMap<bool> isRcDriverPredicate_{"player_is_rc_driver's own combined resolve/position-compare predicate",
                                            "spec-lua-api-behaviour.md Sec18.10"};
    bool globalReplicationModeFlag_ = false;
    std::unordered_map<std::string, int32_t> vehicleTeamIdByName_;
    std::unordered_map<std::string, bool> hoodAlternateSpawning_;
    OpenValue<bool> dlcFilmGrainAvailable_{"DLC-availability sentinel for dlc_gis_film_grain_unload (0x013010d8 != -1)",
                                           "spec-lua-api-behaviour.md Sec17.29"};
    bool filmGrainFadeState_ = false;
    uint64_t filmGrainLocalApplyCount_ = 0;
    uint64_t filmGrainReplicateCount_ = 0;
    bool reticleOverrideGlobalsCleared_ = false;
    uint64_t reticleOverrideClearLocalCount_ = 0;
    uint64_t reticleOverrideClearReplicateCount_ = 0;
};

// Sets the initial values Team A's specs confirm (src/lua_spec_initial_state.cpp).
// Host's constructor calls it once; slots it does not set stay OPEN.
void applySpecInitialState(EngineState& es);

// ===========================================================================
// Sec32.1/Sec49.2 (spec sync 3ecbf84, 2026-10-09): the shared reader
// `0x00931980(local_player_role, flag_mask)` - the ambient spawn-region
// candidate chooser's own min/max ANNULUS filter, ported directly. NOT the
// near-field (fully-simulated) ambient ped/vehicle population spawners
// (spec-ai-runtime.md Sec17.4c/Sec20, sr3airruntime/near_field_*_
// population.h) - a deliberate scope call, not an oversight: a whole-project
// address/global cross-check (both directions) found ZERO overlap between
// this reader's own addresses/globals (0x00931980, 0x005ed7d0,
// 0x013092ec/0x013092f0) and either near-field spawner's own addresses
// (0x0090b0a0 ped creation; 0x0090f2c0/0x0090ee10 vehicle maintenance) -
// spec-ai-runtime.md never once mentions this reader or its globals, and
// spec-lua-api-behaviour.md's own text names this reader's caller
// (`0x005ed7d0`) an "ambient spawn-region candidate chooser" walking the
// active WORLD-OBJECT table, the SAME "spawn-region" terminology
// `continuous_spawn_start`/`continuous_spawn_stop` (Sec4.7/Sec4.8) already
// use for their own mission-script-registered region table - structurally
// the Lua-scriptable spawn-region family this project's own near-field-
// spawner task explicitly places OUT of scope ("mission-script-driven
// continuous_spawn_start... roadblocks, etc."), not the always-on near-field
// ambient population. This filter is therefore implemented here, in the Lua
// native/engine-state layer where its own sibling max-dist field already
// lives (spawnRegionMaxSpawnDistSquared_ above), not bolted onto either
// near-field spawner file. See scratchpad note left for this task's own
// report for the full cross-check.
// ===========================================================================

// Per-candidate flag-byte bits the min-dist floor's own escape hatch tests
// (Sec32.1: "unless the candidate's flag byte has bit 0x08 set and bit 0x10
// clear"). The MAX cap has no such escape - CONFIRMED, no bit tests it.
inline constexpr uint8_t kSpawnRegionMinDistEscapeBit = 0x08;
inline constexpr uint8_t kSpawnRegionMinDistEscapeSuppressBit = 0x10;

// CONFIRMED per-player test (Sec32.1): `dist² < minSq` -> too close,
// REJECTED, *unless* the escape bits above hold, in which case a secondary
// override check (`0x00da38e0`/`0x00a11e10`, "record-copy + predicate call,
// field-level semantics OPEN" per spec) decides instead; `maxSq < dist²` ->
// too far, REJECTED unconditionally, no escape of any kind.
// `secondaryOverrideAdmits`: the escape's own OPEN downstream predicate -
// CHOSEN default true (same "an OPEN gate defaults to passing" idiom this
// project uses throughout, e.g. sr3airruntime::NearFieldPedSpawnGates::
// difficultyGatePasses) - a caller that independently traces the real
// predicate's field-level meaning supplies it here instead.
inline bool SpawnRegionAnnulusEligibleForPlayer(
    double distSq, float minSpawnDistSquared, float maxSpawnDistSquared, uint8_t candidateFlags,
    const std::function<bool()>& secondaryOverrideAdmits = [] { return true; }) {
    if (distSq > static_cast<double>(maxSpawnDistSquared)) return false;  // too far - unconditional, no escape
    if (distSq < static_cast<double>(minSpawnDistSquared)) {
        const bool escapeHolds = (candidateFlags & kSpawnRegionMinDistEscapeBit) != 0 &&
                                  (candidateFlags & kSpawnRegionMinDistEscapeSuppressBit) == 0;
        if (!escapeHolds) return false;  // too close, no escape
        return secondaryOverrideAdmits();  // too close, but escape holds - secondary check decides
    }
    return true;  // inside the annulus (or NaN distSq - CONFIRMED edge: NaN fails both comparisons above, so it reaches here, matching the setter's own "NaN silently disables the floor" note)
}

// CONFIRMED (Sec32.1): "applying both tests against each" local AND remote
// co-op player, and "candidates passing both tests (against both players,
// when both exist) are collected" - a candidate must be eligible against
// EVERY resolved player position, not merely one.
inline bool SpawnRegionAnnulusEligible(
    const std::vector<double>& distSqToEachPlayer, float minSpawnDistSquared, float maxSpawnDistSquared,
    uint8_t candidateFlags, const std::function<bool()>& secondaryOverrideAdmits = [] { return true; }) {
    for (double distSq : distSqToEachPlayer) {
        if (!SpawnRegionAnnulusEligibleForPlayer(distSq, minSpawnDistSquared, maxSpawnDistSquared, candidateFlags,
                                                  secondaryOverrideAdmits))
            return false;
    }
    return true;
}

} // namespace sr3luahost
