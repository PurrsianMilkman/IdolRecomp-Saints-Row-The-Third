// Named placed-object (door / mesh-mover) engine state: PlacedObjectState.
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

// door_close/door_lock/mesh_mover_hide/mesh_mover_show/mesh_mover_reset/
// mesh_mover_set_invulnerable (spec-lua-api-behaviour.md Sec9.12/9.14/9.15/
// 9.16/9.17/14.9) - a single flags082 byte stands in for each named
// object's own real +0x82 flags byte (doors: bit 0x4 lock state, bits
// 0x10/0x20 close-transient state, Sec9.14/9.15; mesh-movers: bit 0x1
// hidden-toggle state, bit 0x8 invulnerability state, Sec9.16/9.17/14.9).
// A given name is used by real scripts as either a door OR a mesh-mover,
// never both, so one shared byte per name does not collide in practice -
// see EngineState::getOrCreatePlacedObject()'s own doc comment for the
// shared-resolver reasoning.
struct PlacedObjectState {
    uint8_t flags082 = 0;
    static constexpr uint8_t kDoorLockBit = 0x04;
    static constexpr uint8_t kDoorCloseTransientBits = 0x30; // bits 0x10|0x20
    static constexpr uint8_t kMeshMoverHiddenBit = 0x01;
    static constexpr uint8_t kMeshMoverInvulnerableBit = 0x08;

    // on_mover_destroyed (Sec7.16): resolves via the unidentified-4th-kind
    // resolver (0x006434a0) DIRECTLY, later individually resolved as this
    // SAME named-placed-object resolver (Sec9.21/Sec14.6 cross-reference,
    // the spec text's own "[Resolved: ...]" annotation) - registered
    // unconditionally once resolved, same single-dedicated-field-per-hook-
    // kind simplification as CharacterState::onTakeDamageCallback.
    std::string onMoverDestroyedCallback;

    // --- door_open (Sec1.10), batch 2026-10-08 (wave 1 batch 1, §1-4) ---
    //
    // CONFIRMED: the door-handle field at offset +200 (+0xc8) is a pointer
    // to a distinct, door-owned interaction-anchor sub-object (NOT a
    // boolean "has lock" flag, per Sec1.10's own corrected reading) that
    // gates the whole arg-4-present rich path. No stated construction
    // default anywhere in Sec1.10's own text - OPEN until a test sets it,
    // same discipline as every other previously-unmodeled field in this
    // header.
    OpenValue<bool> hasInteractionAnchor{"door +0xc8 interaction-anchor sub-object present",
                                          "spec-lua-api-behaviour.md Sec1.10"};
    // Raw rich-path request, recorded on the last successful call: this
    // project has no animation-state name->id table (0x004bf810, the same
    // table character_kill's own arg 3 reads, Sec3.15) and no vtable
    // dispatch modelled anywhere (same "record the raw request, not the
    // real internals" convention as HelicopterFlyTo::Request/
    // MinimapIconRecord elsewhere in this header) - CHOSEN, test-
    // observable only.
    std::string lastInteractionCharacterName; // resolved arg 4 (via the shared objectResolves() stand-in)
    std::string lastInteractionAnimStateName; // raw arg 5 (name->id resolution not modelled)
    std::string lastInteractionTargetName;    // raw arg 6, empty if absent or unresolved
    uint64_t richPathAppliedCount = 0;        // times the rich (arg-4-present) path actually applied (pushed 1)
    uint64_t simpleOpenCount = 0;             // times the simple (arg-4-absent) open call (0x00a31920) ran

    // on_mover_enter_water (Sec14.6): mover hook slot 3 (+0x20/+0x24 sub-
    // record - this RESOLVES this document's own previously-flagged OPEN
    // "3rd unidentified kind" hook-slot array, per Sec14.6's own text).
    // Same empty-string-clears-slot convention as onMoverDestroyedCallback
    // above (same underlying setter shape, 0x00a56d00).
    std::string onMoverEnterWaterCallback;

    // mesh_mover_kill (Sec14.10): which of the two "extended" sub-objects
    // (+0xc0 scripted, +0xdc plain) this mover has - this project has no
    // real sub-object graph (same "no generic entity shape invented"
    // boundary this header's own top note states elsewhere) - test-
    // settable booleans only, CHOSEN false ("neither populated") by
    // default.
    bool hasScriptedKillSubObjectC0 = false;
    bool hasPlainKillSubObjectDc = false;
    // The +0xdc sub-object's own health-like field (its own +0x30 sub-
    // object's +0x138, Sec14.10's own text) - CHOSEN 0 ("never set").
    int32_t killSubObjectHealth = 0;
    // The THIRD "should this replicate" gate (0x027038c0 == 0, Sec14.9/
    // Sec14.10/Sec14.31) mesh_mover_kill's own non-scripted-path helper
    // (0x00a315b0) tests, shared with mesh_mover_set_invulnerable's own
    // helper (0x00a31760, NOT this batch's scope) - OPEN until a test sets
    // it (no spec gives either native's own real single-player resolution,
    // unlike forceFlagGatePasses/controlLockGatePasses in character_state.h).
    OpenValue<bool> replicateGate027038c0Zero{"shared 'should replicate' gate 0x027038c0 == 0",
                                              "spec-lua-api-behaviour.md Sec14.9/Sec14.10/Sec14.31"};
    uint64_t scriptedKillCount = 0;      // the +0xc0 vtable+0x80 path, count only (internals OPEN)
    uint64_t plainKillFinalizeCount = 0; // the +0xdc path reached a destroy finalize (health already <= 0)

    // =====================================================================
    // Wave 2, Batch W2-3 (2026-10-08), spec-lua-api-behaviour.md Sec20+Sec21
    // - see lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp for full
    // per-native citations. Every field below is keyed through the SAME
    // EngineState::getOrCreatePlacedObject() shared-resolver registry this
    // struct's own top comment already establishes (Sec29's "one shared
    // global name map" finding) - no separate per-kind resolver map
    // invented for trigger/light-group/interior/item/door names.
    // =====================================================================

    // on_door_opened (Sec21.19): registers hook code 4 via the THIRD
    // registration-helper family (0x00a56d00, already cross-referenced at
    // Sec3.13/Sec14.6) at this door's own +0x90 hook-slot sub-record. Same
    // empty-string-clears-slot convention as onMoverDestroyedCallback above.
    std::string onDoorOpenedCallback;

    // on_pickup (Sec20.12): registers hook slot 0 at this item's own +0xc0
    // sub-record, via the SAME named-hook-slot-array family
    // (0x005e4520/0x00a1fcc0/0x00a29920).
    std::string onPickupCallback;

    // on_collision (Sec21.1) / on_weapon_equip (Sec21.18): these are
    // CHARACTER-keyed (the +0x1f00 hook-slot sub-record lives on
    // CharacterState::onCollisionCallback/onWeaponEquipCallback above, NOT
    // here - this project treats every resolved name uniformly through the
    // one shared objectResolves() registry, same "no name-to-type
    // registry" simplification get_dist/teleport_vehicle already establish
    // project-wide, so the spec's own SEPARATE vehicle-fallback branch for
    // on_collision, +0xb0 sub-record hook code 4, is NOT separately
    // modeled - a deliberate, labeled CHOSEN simplification). Listed here
    // only to flag that these two do NOT belong on PlacedObjectState,
    // despite on_door_opened/on_pickup (immediately below/above) sharing
    // the same "hook registration" shape.

    // trigger_get_radius (Sec21.13): the real 3-way shape-kind branch (a
    // corner-distance formula for kind 0, two further fixed per-kind
    // floats for kinds 1/2) has no other in-scope reader of any
    // intermediate field - abstracted to ONE final radius value, OPEN
    // until a test sets it, the same "abstract an unconfirmed multi-branch
    // computation to one flag" convention used throughout this header. An
    // UNRESOLVED name has no explicitly-described failure path in the spec
    // text itself (the hidden `this` is the resolved object directly, with
    // no stated pre-dispatch resolve check) - CHOSEN (not a spec fact): this
    // host has no real pointer to dereference for an unresolved name, so it
    // returns 0.0 (the SAME value the spec's own "any other [shape] kind
    // defaults to 0.0" fallback already uses) rather than crash or guess a
    // different number.
    OpenValue<double> triggerRadius{"trigger_get_radius's own final per-shape-kind radius value",
                                    "spec-lua-api-behaviour.md Sec21.13"};

    // ingame_effect_add_trigger (Sec21.5): the resolved object's own +0xb4
    // field (an effect id). -1 ("no effect") CHOSEN default - the SAME
    // sentinel this native's own effect-name-lookup failure (0x005c50b0)
    // independently returns, so it is a reasonable, labeled starting value,
    // not an invented one. The 3-deep replication gate (0x008addb0 then
    // 0x008add90 then 0x00889d10) has no in-scope reader beyond "does the
    // replicate happen" - CHOSEN (project-wide single-player simplification,
    // same as every other co-op-only gate in this header): always passes.
    int32_t effectId = -1;
    uint64_t effectApplyLocalCount = 0;  // mask bit 0, only when the id actually changed
    uint64_t effectReplicateCount = 0;   // mask bit 1

    // light_group_use_intensity (Sec20.15): +0x40 bit 0x2 ("use custom
    // intensity"). The child-propagation walk (0x00a2ebf0 per linked child)
    // has no modeled child-list data anywhere in this project (no linked-
    // list/group-membership registry) - not modeled, no counter (nothing to
    // count without a real child roster).
    bool useCustomIntensity = false;

    // interior_disable_exterior (Sec20.19): +0x7b bit 0x10 ("exterior
    // disabled") plus a "dirty" bit (+0x7d bit 0x2) set only on a real
    // value change (CONFIRMED). The replication condition reuses
    // EngineState::replicationModeFlag0x024d4461 below (a global, not
    // per-object, flag - shared with any FUTURE consumer of that same real
    // global, e.g. action_stop_synced_state, out of this batch's scope).
    bool exteriorDisabled = false;
    bool exteriorDisabledDirtyBit = false;

    // continuous_explosion_avoid_object_add (Sec21.7): this project has no
    // real position/coordinate data for the resolved record's own linked-
    // object chain (the two-hop vtable+0x70 indirection, CONFIRMED via raw
    // disassembly but not independently reproducible without real geometry)
    // - tracked as a per-NAME slot in the shared EngineState-level 8-slot
    // list instead (EngineState::continuousExplosionAvoidObjects() below),
    // not here (the fixed array is a global, not per-object, resource).

    // npc_use_closest_action_node_of_type (Sec21.2): this is CHARACTER-keyed
    // (the resolved arg 1 IS a character, via the character resolver method
    // on singleton 0x02442750 - this project's flat objectResolves()
    // stands in for it) - see CharacterState's own doc comment, not here;
    // listed in this struct's own top comment only to flag that this is the
    // ONE Sec21.2 function that does NOT belong on PlacedObjectState,
    // despite living in the same batch as several that do.

    // mesh_mover_update_control (Sec25.6, wave 2 batch W2-4): a SECOND gate
    // alongside hasInteractionAnchor above (+0xc8, which must be CLEAR/false
    // here - the real engine shares one resolver/handle between doors and
    // movers, mutually exclusive by which of +0xc4/+0xc8 is populated) - a
    // NEW field, +0xc4, the mover's own "control sub-record" pointer. No
    // CONFIRMED construction default stated - OPEN until a test sets it,
    // same discipline as hasInteractionAnchor.
    OpenValue<bool> hasMoverControlSubRecord{"mover +0xc4 control sub-record present",
                                              "spec-lua-api-behaviour.md Sec25.6"};
    // Per-control-name last-written value, split by the real type-dispatch
    // (boolean vs. float, with a float "truncate to integer" sub-mode) -
    // 0x0064a1f0/0x008cb380/0x008cb400's own internal field writes are OPEN
    // (not independently decompiled), so only the raw request is recorded,
    // honestly (same MinimapIconRecord-style convention used throughout this
    // header).
    std::unordered_map<std::string, bool> moverBoolControls;
    std::unordered_map<std::string, double> moverNumericControls;
    uint64_t moverControlUpdateCount = 0;

    // mesh_mover_destroyed (Sec17.8), wave 2 batch W2-2 (2026-10-08): reuses
    // the EXISTING hasScriptedKillSubObjectC0/hasPlainKillSubObjectDc pair
    // (Sec14.10) directly - the SAME +0xc0/+0xdc sub-object presence facts,
    // not a separate model. The nested "+0xc0 non-null -> +0x138 off that"
    // step is a genuinely new, one-level-deeper fact this project did not
    // previously need - OPEN until a test sets it.
    OpenValue<bool> scriptedSubObjectField138NonNull{
        "mesh mover +0xc0 sub-object's own +0x138 pointer non-null",
        "spec-lua-api-behaviour.md Sec17.8"};

    // door_is_open (Sec17.28): reuses the EXISTING hasInteractionAnchor
    // field (Sec1.10, door +0xc8) directly for the outer null-test; the
    // inner state byte (+0x7a bit 0x40 of that SAME sub-object) is a new,
    // independently OPEN fact (no spec-given construction default, HIGH
    // CONFIDENCE "open" meaning per Sec17.28's own text, not independently
    // proven).
    OpenValue<bool> doorOpenBit40At7a{"door interaction-anchor sub-object byte +0x7a bit 0x40 (door open)",
                                      "spec-lua-api-behaviour.md Sec17.28"};
};

} // namespace sr3luahost
