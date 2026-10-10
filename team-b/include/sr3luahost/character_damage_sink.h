#pragma once

// sr3luahost/character_damage_sink.h - a concrete adapter for
// sr3weapons::IWeaponDamageSink (feature/hitscan-fire-dispatch,
// include/sr3weapons/weapon_fire.h), wired against the now-confirmed real
// damage/death mechanism (spec-player-runtime.md Sec15.3a, wave 6
// 2026-10-09; see EngineState::applyCharacterDamage()/
// applyGuaranteedLethalHit(), engine_state.h, and CharacterState's own
// "Wave 6" field block, character_state.h, for the full per-field
// citation).
//
// WHY THIS HEADER EXISTS INSTEAD OF A DERIVED sr3weapons::IWeaponDamageSink
// CLASS: feature/hitscan-fire-dispatch (commit 1e58667, "Implement
// equip-time hitscan fire dispatch") is NOT present in this worktree/branch
// (feature/character-damage-death) - `include/sr3weapons/weapon_fire.h`
// does not exist here, and this branch must not merge main or any sibling
// feature branch in to get it. This task's own brief anticipated exactly
// this ("If include/sr3weapons/weapon_fire.h is not present in your
// worktree... implement the adapter against whatever damage/death API you
// build in step 1 anyway, in a location that makes sense... don't invent or
// guess at that interface's exact shape if you can't see it").
//
// This project's own git history already holds that interface, even though
// it is not merged to main or present in this worktree's files - `git show
// feature/hitscan-fire-dispatch:include/sr3weapons/weapon_fire.h` (a
// read-only look at this SAME project's own sibling in-flight branch, not
// an external/forbidden source) was used to confirm its EXACT shape rather
// than guess it, without checking that branch out or merging anything from
// it. Quoting the one relevant line precisely (weapon_fire.h's own doc
// comment above it, abridged): the seam is
//
//   class IWeaponDamageSink {
//   public:
//       virtual ~IWeaponDamageSink() = default;
//       virtual void applyLethalHit(const std::string& targetName) = 0;
//   };
//
// - "[a model of] the SAME deterministic lethal-hit shape this project's
// own existing damage-application entry point already uses (spec-lua-api-
// behaviour.md Sec3.15's character_kill: a fixed -1.0 lethal constant
// through the shared damage routine... 'guaranteed death via the normal
// pipeline, not a possibly-survivable hit')... not a partial/variable-
// amount damage call." That file's own doc comment explicitly calls out
// stub_character_damage's (then-OPEN) authoritative branch as "the
// authoritative variable-amount branch" and flags it as the natural target
// for a future real adapter - this header, plus this task's own wave-6
// work resolving that branch (src/lua_spec_confirmed_stubs_b15_wave2_core_
// dispatchers.cpp), is that follow-up.
//
// EngineStateLethalDamageSink below matches IWeaponDamageSink's exact
// method name and signature (applyLethalHit(const std::string&)) but does
// NOT derive from it (the base class is not visible to this branch) - once
// feature/hitscan-fire-dispatch merges, changing
// `class EngineStateLethalDamageSink` to
// `class EngineStateLethalDamageSink : public sr3weapons::IWeaponDamageSink`
// and adding `#include "sr3weapons/weapon_fire.h"` is the entire remaining
// wiring step (a one-line change, by construction - this is the whole
// point of matching the real signature now rather than guessing a
// different shape). The hitscan dispatch chain's own CHOSEN wiring
// (CastHitscanRay()/the fire() entry point calling this sink at all) is
// unchanged by this header - this project still has not found the real
// "hitscan fire calls the damage sink" call site (spec-weapons-runtime.md
// Sec2.2c's own "genuinely OPEN" chain, re-confirmed not-closed by Sec15.3a
// "The hitscan gap - still open, now more precisely bounded") - this
// header only makes the SINK's own internals real, per this task's own
// explicit scope ("keep your CHOSEN wiring but point it at this sink's
// behaviour").
//
// UPDATE 2026-10-09 (spec-weapons-runtime.md Sec4.4, Team A spec sync
// c9ab790): the "hitscan fire calls the damage sink" call site is no
// longer merely OPEN pending further tracing - the damage sink's own
// CALLER census is now STATICALLY EXHAUSTED (75 call sites across 58
// functions, no vtable installs), and none of them is hitscan-shaped. The
// real path is MOST LIKELY an async Havok projectile/ray contact callback -
// invisible to static tracing by nature, which is why the STATIC search
// (not the question itself) is exhausted. Team A is preparing an optional,
// read-only live capture that could still find the real call site - this
// is NOT closed permanently. The hitscan module's own wiring is CHOSEN,
// with the static search exhausted (Sec4.4); the real path is likely a
// physics contact callback, pending a possible live capture.
#include <string>

#include "sr3luahost/engine_state.h"

namespace sr3luahost {

// A concrete sink that routes a "lethal hit" into this project's own real
// damage/death mechanism via EngineState::applyGuaranteedLethalHit() - the
// SAME deterministic, guaranteed-death shape character_kill already uses
// (health force-snapped to 0 when known, then the real death-transition/
// ragdoll-or-finalize hand-off, EngineState::finalizeLethalCharacter()).
// `source` is this hit's resolved instigator name, if any (empty = none) -
// not yet consumed by applyGuaranteedLethalHit() itself (see that method's
// own doc comment), carried here so a future pass that DOES attribute a
// guaranteed kill to an instigator (e.g. for on_death callbacks/kill
// credit) has it ready without a second interface change.
class EngineStateLethalDamageSink {
public:
    explicit EngineStateLethalDamageSink(EngineState& state, std::string source = {})
        : state_(state), source_(std::move(source)) {}

    // Matches sr3weapons::IWeaponDamageSink::applyLethalHit's exact name
    // and signature (see this header's own top doc comment) - NOT marked
    // `override` since the base class is not visible to this branch.
    void applyLethalHit(const std::string& targetName) {
        state_.applyGuaranteedLethalHit(targetName, source_);
        ++callCount;
        lastTarget = targetName;
    }

    // Test/diagnostic observability, same convention as sr3weapons'
    // own RecordingDamageSink (weapon_fire.h).
    uint64_t callCount = 0;
    std::string lastTarget;

private:
    EngineState& state_;
    std::string source_;
};

}  // namespace sr3luahost
