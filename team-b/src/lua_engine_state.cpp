#include "sr3luahost/engine_state.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

#include "sr3luahost/save_game_bridge.h"
#include "sr3player/vitals.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3save/save_crc.h"
#include "sr3vintdoc/vint_doc.h"

namespace sr3luahost {

namespace {

// Character spawn-state's own "round(M * Hit_Points)" (spec-lua-api-
// behaviour.md Sec34.2) does not name a distinct rounding routine, so this
// reuses this project's one CONFIRMED rounding convention already
// established for a bare-global `round()` call (Sec26.27,
// lua_bare_globals.cpp's own bare_round): narrow to float first, then half
// away from zero. Not independently re-derived for this specific call site -
// HIGH CONFIDENCE by convention, not claimed CONFIRMED on its own.
int32_t RoundHalfAwayFromZero(double x) {
    double t = (x >= 0.0) ? std::trunc(x + 0.5) : std::trunc(x - 0.5);
    if (t < -2147483648.0) return INT32_MIN;
    if (t > 2147483647.0) return INT32_MAX;
    return static_cast<int32_t>(t);
}

// FIX 2026-10-08 (same investigation as the Sec34.2/script_npc_hp fix just
// above - a sibling defect found in the identical code path, EngineState::
// loadZoneObjectsSection() below, for the ignore-AI override instead of
// max HP): spec-lua-api-behaviour.md Sec34.3's own CONFIRMED mechanism is
// "the flag-name table (0x00a33ff0, built once at startup) hashes
// 'ignore_ai' to mask 0x00001000" against the record's own space-separated
// `script_npc_flags` string (Sec11.7's own real example, "never_pickup_
// weapons ignore_ai should_not_be_grabbed dont_drop_cash use_default_
// loadout") - i.e. the real engine tokenizes on whitespace and matches
// each whole token against its flag-name table, not a raw substring probe
// over the whole string. Implemented here as the real, confirmed
// tokenization rather than find()/substring (Sec11.7's own "425 of 6,702
// literally contain the string 'ignore_ai'" population figure is this
// project's and Team A's shared PROXY measurement for that scan, not a
// claim that substring matching is the real mechanism).
bool ScriptNpcFlagsHasIgnoreAiToken(const std::string& flags) {
    size_t pos = 0;
    while (pos < flags.size()) {
        size_t start = flags.find_first_not_of(" \t\r\n", pos);
        if (start == std::string::npos) break;
        size_t end = flags.find_first_of(" \t\r\n", start);
        if (end == std::string::npos) end = flags.size();
        if (flags.compare(start, end - start, "ignore_ai") == 0) return true;
        pos = end;
    }
    return false;
}

} // namespace

EngineState::EngineState() {
    // spec-lua-api-behaviour.md Sec24.24 blocker m16 (FINAL): `msn_text_adventure` is a static HUD screen object
    // appended to the screen-name registry (0x012fced8) unconditionally at start-up, so on_hud_screen_exit
    // resolves it from boot onward in every game mode. Keyed lower-case (the registry hashes the lower-cased name).
    hudScreenNameResolves_.set("msn_text_adventure", true);
    // spec-player-runtime.md Sec2.7: the rows-0/1 enter 0x005aa5f0 begins
    // with "clear latched" 0x005bbf60 (Sec1.11.1) on the control context.
    controlStateMachine_.setClearLatched([this] { inputSystem_.clearLatched(); });
    // Sec1.4 step 5: the latch reads the current control state (0x005aa630).
    inputSystem_.setControlStateGetter([this] { return controlStateMachine_.currentState(); });

    // CONFIRMED (spec-lua-api-behaviour.md Sec21.27a, 2026-10-08): the real
    // mission-phase machine (0x014c7a14) starts at phase 0 ("idle - no
    // mission bound") with the context pointer (0x014c8460) null, per the
    // engine's own start-up initialiser 0x006d6010 - so every "context
    // non-null AND phase != 8" gate this project tracks is a KNOWN false
    // before any mission is ever bound, not an unmodelled value. See
    // EngineState::beginMissionPhaseTracking()'s own doc comment (this
    // header's MissionContext section) for the companion "mission started"
    // transition and the full root-cause history (probe_blockers_ranked_
    // 20261008_post_resolver_merge.md item 2, 12/49 missions).
    missionContext_.activeAndRunning.set(false);
    inProgressType_.activeMissionPresent.set(false);
    missionExcludedRewards_.activeAndRunning.set(false);

    // spec-save-format.md Sec15 wiring (2026-10-08): a clearly-labelled
    // scratch default (see save_game_bridge.h's defaultSaveGameDirectory()
    // for why), never a real game save-data location.
    saveGameDirectory_ = defaultSaveGameDirectory();

    // 2026-10-10: the SAME one literal-name seed getOrCreateVehicle() below
    // applies to VehicleState::flyingType, applied HERE too for
    // helicopterFlyTo_.qualifies specifically, because that map is its own
    // independent EngineState-level field (Sec28.2), NOT nested inside
    // VehicleState - a caller can reach `helicopterFlyTo().qualifies.get(
    // name)` directly (e.g. stub_helicopter_fly_to_set_goal_direction)
    // without ever calling getOrCreateVehicle() first, so seeding only
    // there would leave this copy of the SAME real field OPEN depending on
    // call order. See getOrCreateVehicle()'s own doc comment for the full
    // citation (Team A spec sync, HANDOFF.md commit 849a88e; m05.lua's own
    // "-- Spawn Pierce's helicopter" comment) - kept in exact sync with
    // that seed, same single literal name, nothing generalized.
    helicopterFlyTo_.qualifies.set("m05_heli_pierce", true);

    // CONFIRMED (spec-lua-api-behaviour.md Sec6.19, 2026-10-10 Team A spec
    // sync, routed mission-blocking dlc2_m03): tutorial_start's own
    // dispatcher step 5 reads 0x00706ab0, a top-of-mode-stack accessor whose
    // backing index (0x012f4a80) is file-backed, initialized to -1 - the
    // mode stack starts EMPTY on a fresh game, so this accessor's spawn
    // default is 0, not 6 (6 = the key-bindings config sub-screen, the only
    // value that stops this step). See TutorialStart::gate5Value00706ab0's
    // own doc comment for the full citation. A test exercising the
    // key-bindings-screen-open case still overrides this via .set()/
    // .forget(), exactly as before this pre-seed (same convention as
    // CharacterState::ragdollStateEnumCd2 above).
    tutorialStart_.gate5Value00706ab0.set(0);
}

// Design notes moved verbatim from EngineState::getOrCreateCharacter's declaration in engine_state.h
// (refactor step 3a):
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
CharacterState& EngineState::getOrCreateCharacter(const std::string& name) {
    auto it = characters_.find(name);
    if (it != characters_.end()) return it->second;
    CharacterState& c = characters_[name]; // default-constructs on first reference
    characterOrder_.push_back(name);        // creation order = the manager index list (engine callback death pass)
    // CONFIRMED (Sec34.1/Sec34.3): ignore-AI off at construction, re-cleared
    // by the no-bound-script-NPC default path - see CharacterState::ignoreAI's
    // own doc comment. markScriptNpcBoundForTesting() forgets this back to
    // OPEN for the one case (a bound script NPC) where the real answer is
    // zone data this project doesn't have.
    c.ignoreAI.set(false);
    // CONFIRMED (Sec3.7's own single-player resolution, 2026-10-03): the
    // force-flag double-gate always resolves true with no session installed
    // - see CharacterState::forceFlagGatePasses's own doc comment.
    c.forceFlagGatePasses.set(true);
    // CONFIRMED (Sec34.5, 2026-10-03): +0xcc8 is zeroed by the constructor
    // and +0xe3 bit 0x01 is clear from a zero-filled allocation - see
    // CharacterState::lifeState/killFinalizedFlagE3's own doc comments.
    c.lifeState.set(0);
    c.killFinalizedFlagE3.set(false);
    // CONFIRMED (2026-10-09, Sec3.4's own in-place correction): +0x18f0/
    // +0x18f4 is zeroed unconditionally at construction (0x00954340 ->
    // 0x00942920) - see CharacterState::reviverHandle's own doc comment.
    c.reviverHandle.set(0);
    // CONFIRMED (Team A sync 9c70c61, Sec30.5 note): +0x16c0/+0x16c4 is the
    // current-vehicle handle, null until the vehicle-entry sequence starts;
    // "" is this project's null.
    c.currentVehicleName.set(std::string());
    // CHOSEN (not CONFIRMED): the 0x008addb0 single gate of the
    // player_controls_*/player_movement_* setters takes the direct-apply
    // branch - see CharacterState::controlLockGatePasses's own doc comment.
    c.controlLockGatePasses.set(true);
    // HIGH CONFIDENCE (spec-player-runtime.md Sec2.10, 2026-10-03): +0x1d99
    // is all-clear (enabled) at spawn, a zero-filled-allocation default - see
    // CharacterState::controlsDisabled/movementDisabled's own doc comments.
    c.controlsDisabled.set(false);
    c.movementDisabled.set(false);
    // HIGH CONFIDENCE (spec-player-runtime.md Sec2.10 "Question B",
    // bit-sense polarity resolved 2026-10-08): the third bit at the SAME
    // +0x1d99 byte, actionsEnableBit20_1d99 (despite its own "Enable" name,
    // a disabled-sense bit - see its own doc comment), is likewise all-clear
    // (enabled) at spawn - Sec2.10 names and seeds all three bits together
    // ("controls/movement/actions disabled ... all clear (0) - enabled").
    c.actionsEnableBit20_1d99.set(false);
    // HIGH CONFIDENCE (batch 3, 2026-10-08): gate 0x008addb0 for
    // player_parachute_wear_backpack (Sec12.28) is the SAME address
    // controlLockGatePasses above already resolves true for - see
    // CharacterState::parachuteBackpackGatePasses's own doc comment.
    c.parachuteBackpackGatePasses.set(true);
    // CONFIRMED (spec-player-runtime.md, 2026-10-08): constructor 0x00954340
    // zeroes +0x16d4 unconditionally for every newly-constructed character
    // (0x00954be8, edi traced back to a single xor edi,edi with no
    // redefinition before the write) - the object's real default/initial
    // value, not a fifth vehicle-occupancy transition state. Closes
    // CharacterState::stateEnum's own previously-OPEN spawn-default question
    // (see that field's own doc comment for the full history: the m05/m21/
    // m09 +0x16d4 bucket this settles).
    c.stateEnum.set(0);
    // CONFIRMED (spec-player-runtime.md Sec5.4.2/Sec5.4.4, cross-confirmed by
    // Sec7.4a's vehicle-enter eligibility gate step 4 and Sec15.3a's ragdoll
    // hand-off trace, 2026-10-09): +0xcd2 is the human's physics-
    // representation byte and 0 = "character proxy," the ordinary state
    // every freshly constructed, not-yet-ragdolled character holds until an
    // explicit ragdoll/death event moves it away - see
    // CharacterState::ragdollStateEnumCd2's own doc comment. A test
    // exercising a ragdolling/recovering character still overrides this via
    // .set()/.forget(), exactly as before this pre-seed.
    c.ragdollStateEnumCd2.set(0);
    // CONFIRMED (spec-lua-api-behaviour.md Sec22.3, 2026-10-10 Team A spec
    // sync): the character base constructor 0x00954340 (via its embedded
    // AI sub-object at +0x2b0, the same base spec-ai-runtime.md Sec2.4
    // already uses for +0x508/+0x50c) zeroes the AI mode byte +0x501
    // unconditionally before the object is ever live - mode 0 = Idle in
    // the 44-value mode enum (spec-ai-runtime.md Sec3.2). Exactly 3
    // writers exist project-wide: this construction (one-time, pre-live),
    // the main gameplay transition primitive 0x004dd380 (spec-ai-runtime.md
    // Sec3.3, every voluntary AI mode change), and the proxy-only network
    // read 0x004ea530 (non-owned/proxy human only, spec-ai-runtime.md
    // Sec2.4 point 3) - see CharacterState::stateCodeByte501's own doc
    // comment. Closes this field's previously-OPEN spawn-default question
    // (the m07 mission-drive blocker this seed was written to clear).
    c.stateCodeByte501.set(0);
    return c;
}

bool EngineState::hasCharacter(const std::string& name) const {
    return characters_.count(name) != 0;
}

// Design notes moved verbatim from EngineState::getOrCreateVehicle's declaration in engine_state.h
// (refactor step 3a):
// Same convention as getOrCreateCharacter/hasCharacter/characterCount
// above, for VehicleState (spec-lua-api-behaviour.md Sec30.5/Sec33.1).
// Exposed by mutable reference so vehicle_clear_all_radio_locks can walk
// every vehicle this host currently knows about (this project's own
// stand-in for "every vehicle in the world object manager" - see
// VehicleState's own doc comment) and so tests can read/write state
// directly.
VehicleState& EngineState::getOrCreateVehicle(const std::string& name) {
    auto it = vehicles_.find(name);
    if (it != vehicles_.end()) return it->second;
    VehicleState& v = vehicles_[name]; // default-constructs on first reference
    // CONFIRMED (Sec3.7's own single-player resolution, cross-ref Sec30.5,
    // 2026-10-03): the force-flag double-gate always resolves true with no
    // session installed - see VehicleState::forceFlagGatePasses's own doc
    // comment.
    v.forceFlagGatePasses.set(true);
    // Sec46.1 (78fb40a, FINAL): +0x1d7a is 0 at spawn (both bits); the zone's script_vehicle_flags 'invulnerable'
    // token sets bit 0x01 afterwards (loadZoneObjectsSection).
    v.forceFlags1d7a.setBits(VehicleState::kInvulnerableBit1d7a | VehicleState::kAlwaysApplyPlayerDamageBit1d7a, 0u);

    // REMOVED 2026-10-10 (vehicle-class xtbl wiring task): this method used
    // to carry a single hardcoded "m05_heli_pierce" name special case here
    // (seeding flyingType=3/qualifies=true for that one literal string only)
    // because this project had no general mechanism joining a zone-placed
    // vehicle instance's name back to its real `<name>_veh.xtbl` Vehicle_Type
    // row - sr3vehicleinfo::ParseAllVehicles existed but was not wired into
    // this lua host's EngineState at all. That general mechanism now exists:
    // EngineState::loadZoneObjectsSection() (this file, below) resolves a
    // zone-placed `Script_Vehicle` record's own `script_vehicle_type_name`
    // property (sr3zone::ScriptVehicleTypeName(), include/sr3zone/
    // zone_objects.h) against installVehicleTypeClassTable()'s real
    // xtbl-sourced class data and populates flyingType/qualifies from THAT,
    // for every real zone-placed vehicle instance including m05's own pierce
    // helicopter - not just one hardcoded literal name. A bare
    // getOrCreateVehicle() call with no backing zone data (e.g. a synthetic
    // unit-test fixture) leaves flyingType/qualifies exactly as OPEN as
    // before this removal - nothing here fabricates a value for a name this
    // project has no real data for.
    return v;
}

bool EngineState::hasVehicle(const std::string& name) const {
    return vehicles_.count(name) != 0;
}

// Design notes moved verbatim from EngineState::applyCharacterSpawnDefaults's declaration in engine_state.h
// (refactor step 3a):
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
void EngineState::applyCharacterSpawnDefaults(const std::string& name, uint32_t hitPoints, double multiplier) {
    CharacterState& c = getOrCreateCharacter(name);
    int32_t maxHp = RoundHalfAwayFromZero(multiplier * static_cast<double>(hitPoints));
    c.maxHitPoints.set(maxHp);
    c.currentHitPoints.set(maxHp); // Sec34.1 step 2: spawns at full health
    c.ignoreAI.set(false);          // Sec34.1 step 3 / Sec34.3: re-cleared, redundant with getOrCreateCharacter's own default but explicit here too
}

// spec-player-runtime.md Sec18.3 item 4 / Sec18.8 (PV-6, FINAL): the damage-received multiplier is applied INSIDE the
// damage sink, for a player victim or a follower of the local player, when dmg > 0. The record is the single-player
// difficulty record (picker 0x005f4a50, level = difficultyLevel_): Damage_Received_Mult 0.3/0.75/1.5 for easy/
// normal/hard (Sec18.2). Co-op (second record) is not modelled: this host has no session members. Sec18.3 item 1
// (wait re-arm after every damaging hit) and item 2 (the 1/30 s tick) are armHealthRegenWait/tickHealthRegen below.
float EngineState::damageReceivedDifficultyMultiplier() const {
    static const bool diagOff = std::getenv("SR3_DIAG_NO_RECEIVED_MULT") != nullptr;  // DIAGNOSTIC ONLY (before/after runs)
    if (diagOff) return 1.0f;
    return sr3player::SpDifficultyVitals(difficultyLevel_).damageReceivedMult;
}

// CHOSEN: the local player is the character named "player" (free-roam playtest) or "#PLAYER..." (mission scripts).
bool EngineState::isLocalPlayerName(const std::string& name) {
    return name == "player" || name.rfind("#PLAYER", 0) == 0;
}

bool EngineState::isFollowerOfLocalPlayer(const std::string& name) const {
    for (const auto& kv : party_.membersByLeader) {
        if (!isLocalPlayerName(kv.first)) continue;
        for (const auto& m : kv.second)
            if (m == name) return true;
    }
    return false;
}

void EngineState::armHealthRegenWait(CharacterState& c) {
    const bool dont = c.forceFlags1c98.known(CharacterState::kDontRegenHealthBit1c98) &&
                      c.forceFlags1c98.get(CharacterState::kDontRegenHealthBit1c98) != 0;
    // reduction: cheat word 0x014F71E8 bit 0x10000 (3000) is not modelled (no cheat state here), else +0x276C == 0.
    const int64_t waitMs = sr3player::RegenWaitMs(0.0, sr3player::SpDifficultyVitals(difficultyLevel_).healthRegenWaitMult,
                                                  !c.humanShieldHostageName.empty(), dont);
    if (waitMs < 0) return;
    c.healthRegenReadyAtSeconds = vitalsClockSeconds_ + static_cast<double>(waitMs) / 1000.0;
}

// Sec18.3 item 2: per host frame, fixed 1/30 s steps. Only player characters regenerate; health runs to full.
void EngineState::tickHealthRegen(double dtSeconds) {
    static const bool diagOff = std::getenv("SR3_DIAG_NO_REGEN") != nullptr;  // DIAGNOSTIC ONLY (before/after runs)
    if (diagOff) return;
    if (!(dtSeconds > 0.0)) return;
    vitalsClockSeconds_ += dtSeconds;
    if (playerFullRestoreUntilSeconds_ >= 0.0 && vitalsClockSeconds_ >= playerFullRestoreUntilSeconds_) {
        playerFullRestoreUntilSeconds_ = -1.0;  // Sec18.13: on expiry HP := maxHP (0x009a0860)
        for (const auto& nm : characterOrder_) {
            if (!isLocalPlayerName(nm)) continue;
            auto ci = characters_.find(nm);
            if (ci != characters_.end() && ci->second.currentHitPoints.known() && ci->second.maxHitPoints.known() &&
                !characterReadsDead(ci->second))
                ci->second.currentHitPoints.set(ci->second.maxHitPoints.get());
        }
    }
    const size_t n = characterOrder_.size();
    for (size_t i = 0; i < n; ++i) {
        const std::string name = characterOrder_[i];
        if (!isLocalPlayerName(name)) continue;
        auto it = characters_.find(name);
        if (it == characters_.end()) continue;
        CharacterState& c = it->second;
        if (characterReadsDead(c) || !c.currentHitPoints.known() || !c.maxHitPoints.known()) continue;
        if (c.lifeState.known() && c.lifeState.get() == 6) continue;  // downed
        if (c.forceFlags1c98.known(CharacterState::kDontRegenHealthBit1c98) &&
            c.forceFlags1c98.get(CharacterState::kDontRegenHealthBit1c98) != 0)
            continue;
        const int32_t hp = c.currentHitPoints.get();
        const int32_t maxHp = c.maxHitPoints.get();
        if (c.healthRegenExactHp < 0.0 || std::lround(c.healthRegenExactHp) != hp) c.healthRegenExactHp = hp;
        const bool ready = vitalsClockSeconds_ >= c.healthRegenReadyAtSeconds;
        const double rate = sr3player::RegenRatePerSec(sr3player::SpDifficultyVitals(difficultyLevel_).healthRegenSpeedMult,
                                                       healthRegenUnlockFactor, !c.humanShieldHostageName.empty());
        sr3player::HealthRegen acc;
        acc.carrySeconds = c.healthRegenCarrySeconds;
        const double before = c.healthRegenExactHp;
        c.healthRegenExactHp = acc.step(c.healthRegenExactHp, static_cast<double>(maxHp), ready, rate, dtSeconds);
        c.healthRegenCarrySeconds = acc.carrySeconds;
        if (c.healthRegenExactHp > before) {
            ++healthRegenTickCount;
            const int32_t newHp = std::min<int32_t>(maxHp, static_cast<int32_t>(std::lround(c.healthRegenExactHp)));
            if (newHp != hp) c.currentHitPoints.set(newHp);
        }
    }
}

// Sec18.13: the ambulance marker arms the timer; tickHealthRegen applies the expiry.
void EngineState::armPlayerFullRestore() { playerFullRestoreUntilSeconds_ = vitalsClockSeconds_ + kAmbulanceFullRestoreSeconds; }
bool EngineState::playerFullRestoreArmed(const std::string& name) const {
    return isLocalPlayerName(name) && playerFullRestoreUntilSeconds_ >= 0.0 && vitalsClockSeconds_ < playerFullRestoreUntilSeconds_;
}

// Sec18.14: base = max - bonus; bonus' = trunc(base * (M - 1.0)) in double math; max += bonus' - bonus; HP clamped.
int32_t EngineState::applyPlayerHealthBonus(double modifier, const std::string& playerName) {
    CharacterState& c = getOrCreateCharacter(playerName);
    const int32_t maxHp = drawOrGetMaxHitPoints(playerName);
    const int32_t base = maxHp - playerMaxHpBonus_;
    const int32_t bonus = static_cast<int32_t>(static_cast<double>(base) * (modifier - 1.0));  // trunc toward 0
    const int32_t newMax = maxHp + (bonus - playerMaxHpBonus_);
    playerMaxHpBonus_ = bonus;
    c.maxHitPoints.set(newMax);
    if (c.currentHitPoints.known() && c.currentHitPoints.get() > newMax) c.currentHitPoints.set(newMax);
    return newMax;
}

bool EngineState::fireOnRespawn(const std::string& playerScriptName) {
    if (playerOnRespawnCallback_.empty()) return false;
    return fireEngineCallback(playerOnRespawnCallback_, {CallbackArg::str(playerScriptName)}, "on_respawn");
}

// The damage-apply sink's authoritative-branch core (spec-player-runtime.md
// Sec15.3a) - see this method's own declaration (engine_state.h) for why it
// is factored out of stub_character_damage. `targetName` must already be a
// resolved, created character (the caller's own job); `sourceName` empty
// means "no resolved instigator."
// Design notes moved verbatim from EngineState::applyCharacterDamage's declaration in engine_state.h
// (refactor step 3a):
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
// `flags` is the F word of the hit (bit 0x200 suppresses on_take_damage, spec F; character_damage passes
// 0x100, plus 0x200 for its default third argument true).
void EngineState::applyCharacterDamage(const std::string& targetName, const std::string& sourceName, float amount,
                                       uint32_t flags) {
    if (playerFullRestoreArmed(targetName)) return;  // Sec18.13 / Sec4.5.4 step 10d: no damage while +0x19c4 is armed
    CharacterState& c = getOrCreateCharacter(targetName);
    recordDamageAttacker(targetName, sourceName); // spec B: rewritten on every application, nil when attacker-less
    int32_t resistancePercent = 100; // CHOSEN neutral default - no instigator, or instigator's own scale unset
    if (!sourceName.empty()) resistancePercent = getOrCreateCharacter(sourceName).instigatorDamageScalePercent;
    double scaled = static_cast<double>(amount) * (static_cast<double>(resistancePercent) / 100.0);
    bool instigatorIsPlayerClass = !sourceName.empty() && playerClassChainResolves(sourceName);
    if (instigatorIsPlayerClass) scaled *= static_cast<double>(damageDealtDifficultyMultiplier());
    // Sec18.3 item 4 (PV-6): Damage_Received_Mult for the local player and its followers (after the dealt scalar,
    // before the downed absorb, in the Sec4.5.4 order).
    const bool receivesPlayerScale = isLocalPlayerName(targetName) || isFollowerOfLocalPlayer(targetName);
    if (receivesPlayerScale && scaled > 0.0) {
        scaled *= static_cast<double>(damageReceivedDifficultyMultiplier());
        ++damageReceivedScaledCount;
    }
    if (absorbDamageWhileDowned(c, targetName, scaled)) return;  // Sec21.10.1: a downed human does not take the normal HP path

    // CORRECTED (this pass): maxHitPoints is resolved through
    // drawOrGetMaxHitPoints(), NOT a direct c.maxHitPoints.get() - the exact
    // same Sec60.4 lazy-draw convention get_max_hit_points and
    // set_current_hit_points already use for this field (see that method's
    // own doc comment: "this cap is resolved the same way regardless of
    // which of the two natives a script calls first"). character_damage is
    // a third such native; calling .get() directly here would reintroduce
    // the exact OpenStateError-for-an-ordinary-character regression Sec60.4
    // already fixed (probe_blockers_ranked_20261007.md item 2, 26/49
    // missions) for any character damaged before its cap was ever queried
    // or explicitly set. currentHitPoints has NO equivalent lazy-draw
    // anywhere in this project (get_current_hit_points doesn't do it either)
    // - staying OPEN until set is the established, uniform behavior for
    // that field, not a gap introduced here.
    int32_t maxHp = drawOrGetMaxHitPoints(targetName);
    int32_t oldHp = drawOrGetCurrentHitPoints(targetName);  // Sec34.2: starts at the table/max value
    // 0x0094d520(delta) -> 0x0094cfc0(value), clamped [0, maxHitPoints]. The
    // real engine's own separate force-snap-to-0.0 pre-write on "a clearly
    // lethal hit" is a float-precision-only safety measure that collapses
    // into this exact clamped result once health is int32 (see this
    // method's own declaration doc comment) - not reproduced as a literal
    // second write.
    double newHpD = std::clamp(static_cast<double>(oldHp) - scaled, 0.0, static_cast<double>(maxHp));
    int32_t newHp = static_cast<int32_t>(std::lround(newHpD));
    c.currentHitPoints.set(newHp);
    c.lastDamageMagnitude = std::max(static_cast<double>(oldHp - newHp), 0.0); // +0x1584: max(-delta, 0)
    if (isLocalPlayerName(targetName) && scaled > 0.0) armHealthRegenWait(c);  // Sec18.3 item 1

    // on_take_damage / on_damage fire inside the sink, before the death chain (spec F): the lethal hit still
    // produces them. The death callbacks follow from the per-human update.
    // Review N-9: fires for every non-0x200 hit, also when the max reads 0 (fraction 0 then, as the lethal sink does).
    const auto hpFraction = [maxHp](int32_t hp) {
        return maxHp > 0 ? static_cast<double>(hp) / maxHp : 0.0;
    };
    fireCharacterHitCallbacks(targetName, sourceName, hpFraction(oldHp), hpFraction(newHp), flags);
    if (newHp <= 0 && !tryEnterDownedOnLethalHit(c, targetName, static_cast<double>(oldHp) - scaled))
        finalizeLethalCharacter(c, targetName);
}

// Sec21.10.1 entry: the lethal-hit path asks the selector 0x009735d0 first; a refusal (-1, the type-1 owner predicate, or an
// unknown Bleed_Out_Hit_Points) falls through to the normal death. Durations are the FINAL constants: type 0 30000 ms x the
// difficulty scale, type 1 HD_brute_down_time_ms 7000, type 2 10000, type 3 8000, type 4 10000.
bool EngineState::tryEnterDownedOnLethalHit(CharacterState& c, const std::string& targetName, double hpAfter) {
    if (characterReadsDead(c) || (c.lifeState.known() && c.lifeState.get() == 6)) return false;
    // AI-11: the one selector input this host can derive from live data is "a non-player follower" (0x0097e280 non-null:
    // the human sits on a leader's party list, Sec32.4 party_add_do). The player is never a follower. The other inputs
    // (brute, gang member, Killbane records, avatar) have no data source yet and stay whatever the host/test set.
    sr3airuntime::combat::DownedSelectorInputs facts = c.downedFacts;
    if (!(targetName.rfind("#PLAYER", 0) == 0 || targetName == "player")) {
        for (const auto& kv : party_.membersByLeader)
            for (const auto& m : kv.second)
                if (m == targetName) facts.isNonPlayerFollower = true;
    }
    {
        // Team A 2026-10-20 (Sec21.10.1 note, FINAL): gang member (0x00943190) = team byte <= 4 AND matrix(team, 0) <= 1.
        const int team = teamIdOfCharacter(c);
        if (team >= 0 && team <= 4) {
            const int rel = teamRelationForTesting(team, 0);
            if (rel >= 0 && rel <= 1) facts.isGangMember = true;
        }
    }
    const int type = sr3airuntime::combat::SelectDownedType(facts);
    if (type < 0) return false;
    double durationMs = 0.0;
    switch (type) {
        case 0: durationMs = static_cast<double>(sr3airuntime::combat::DownedDeadlineMs(0, downedTimerScale_, false)); break;
        case 1: {
            if (!c.bleedOutHitPoints) return false;  // record +0x0C source not wired: OPEN, nothing invented
            sr3airuntime::combat::Type1Inputs in;
            in.attackerIsLiveBruteWithBit7 = c.downedAttackerBruteBit7;
            in.hpAfterKillingHit = static_cast<int>(hpAfter);
            in.threshold = *c.bleedOutHitPoints;
            in.burning = c.downedBurning;
            if (!sr3airuntime::combat::Type1Predicate(in)) return false;
            c.downedBuffer = sr3airuntime::combat::Type1EnterBuffer(in.hpAfterKillingHit, in.threshold);
            c.downedBufferKnown = true;
            durationMs = static_cast<double>(sr3airuntime::combat::kBruteDownTimeMs);
            break;
        }
        case 2: durationMs = 10000.0; break;  // HD_killbane_down_time_ms
        case 3: durationMs = 8000.0; break;   // HD_mars_killbane_down_time_ms
        default: durationMs = 10000.0; break; // HD_avatar_down_time_ms (the stun-count-5 "no timer" case is OPEN)
    }
    c.downedType = type;
    c.currentHitPoints.set(0);
    c.lifeState.set(6);
    c.downedDeadlineSeconds = syncedActionNowSeconds() + durationMs / 1000.0;
    ++c.enterDownedAppliedCount;
    ++npcAiStats_.downedEntered;
    return true;
}

// Damage while downed: the HP setter cannot kill a downed human, so types 0/2/3/4 ignore the hit. Type 1 runs the
// overkill buffer (0x00972bf0): dies when the buffer runs out, else the timer is extended by 50 ms per point (cap 7000).
bool EngineState::absorbDamageWhileDowned(CharacterState& c, const std::string& targetName, double damage) {
    if (characterReadsDead(c) || !c.lifeState.known() || c.lifeState.get() != 6) return false;
    if (c.downedType == 1 && c.downedBufferKnown && c.downedDeadlineSeconds >= 0.0 && damage > 0.0) {
        const double now = syncedActionNowSeconds();
        const int64_t remainingMs = static_cast<int64_t>(std::llround((c.downedDeadlineSeconds - now) * 1000.0));
        const auto r = sr3airuntime::combat::Type1ApplyDamage(c.downedBuffer, remainingMs, damage, false);
        if (r.dies) {
            killDownedCharacter(c, targetName);
        } else {
            c.downedBuffer = r.buffer;
            c.downedDeadlineSeconds = now + static_cast<double>(r.remainingMs) / 1000.0;
        }
    }
    return true;
}

// 0x009a7610 on a downed human: HP 0, partner/marker released, then the death finalizer 0x00952760 (stance 5).
void EngineState::killDownedCharacter(CharacterState& c, const std::string& targetName) {
    c.currentHitPoints.set(0);
    c.downedDeadlineSeconds = -1.0;
    c.lifeState.set(5);
    c.killFinalizedFlagE3.set(true);
    c.reviverHandle.set(0);
    ++c.characterKillFinalizerCount;
    armPlayerDeathTimer(targetName);
}

// Shared death-transition/ragdoll-or-finalize tail - see this method's own
// declaration (engine_state.h, private) for why it is factored out.
void EngineState::finalizeLethalCharacter(CharacterState& c, const std::string& targetName) {
    c.isDeadHighConfidence = true; // this project's own pre-existing "HP reached zero" stand-in (Sec7.31)
    armPlayerDeathTimer(targetName); // review N-7: the 4 s on_death timer starts at the finalizer
    if (!c.lethalHitKillEligible) return; // CHOSEN pass-through - 0x0096f990's own real trigger condition is OPEN
    ++c.deathTransitionCount;              // 0x00970e70's real death side-effects - host-side bookkeeping only, see field's own doc comment
    bool victimIsPlayerClass = playerClassChainResolves(targetName);
    if (c.ragdollActivationEligible) {
        c.ragdollStateEnumCd2.set(1); // CHOSEN sentinel (!= 0 && != 6) - see field's own doc comment
        ++c.deathRagdollActivateCount;
    } else if (victimIsPlayerClass) {
        // non-ragdoll fallback (locally-owned player-class only, Sec15.3a): the SAME finalizer
        // character_kill's own arg4=false branch already models (stub_character_kill, b30) - reused, not duplicated.
        c.lifeState.set(5);
        c.killFinalizedFlagE3.set(true);
        ++c.characterKillFinalizerCount;
    }
    // else: non-player-class, ragdoll-ineligible - genuinely unspecified by spec (ragdollActivationEligible's own doc comment).
}

// A guaranteed lethal hit (character_kill's own "-1.0 constant... guaranteed
// death" shape) - see this method's own declaration (engine_state.h) for
// the full citation and why it does not require health to already be known.
// Design notes moved verbatim from EngineState::applyGuaranteedLethalHit's declaration in engine_state.h
// (refactor step 3a):
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
void EngineState::applyGuaranteedLethalHit(const std::string& targetName, const std::string& sourceName) {
    CharacterState& c = getOrCreateCharacter(targetName);
    recordDamageAttacker(targetName, sourceName); // spec B: the killer is the attacker of the killing blow (nil when none)
    if (c.maxHitPoints.known()) {
        int32_t oldHp = c.currentHitPoints.known() ? c.currentHitPoints.get() : c.maxHitPoints.get();
        c.currentHitPoints.set(0); // force-snap-to-zero, the SAME "clearly lethal hit" shape applyCharacterDamage() models
        c.lastDamageMagnitude = std::max(static_cast<double>(oldHp), 0.0);
        // Review N-9: also fires when the max reads 0 (old fraction 0 then).
        fireCharacterHitCallbacks(targetName, sourceName,
                                  c.maxHitPoints.get() > 0 ? static_cast<double>(oldHp) / c.maxHitPoints.get() : 0.0, 0.0, 0x100u);
    } else {
        fireCharacterHitCallbacks(targetName, sourceName, 1.0, 0.0, 0x100u);  // health not modelled: lethal, fraction 0
    }
    finalizeLethalCharacter(c, targetName);
}

// Design notes moved verbatim from EngineState::markScriptNpcBoundForTesting's declaration in engine_state.h
// (refactor step 3a):
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
void EngineState::markScriptNpcBoundForTesting(const std::string& name) {
    CharacterState& c = getOrCreateCharacter(name);
    // Sec34.4: the override question (script_npc_hp / script_npc_flags
    // "ignore_ai") is zone data this project does not have - forget the
    // CONFIRMED defaults back to OPEN rather than guess either way.
    c.ignoreAI.forget();
    c.maxHitPoints.forget();
    c.currentHitPoints.forget();
    c.scriptNpcBound = true; // see CharacterState::scriptNpcBound's own doc comment
    c.scriptNpcHp.forget();  // see CharacterState::scriptNpcHp's own doc comment
}

// Design notes moved verbatim from EngineState::applyScriptNpcHitPointsForTesting's declaration in engine_state.h
// (refactor step 3a):
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
void EngineState::applyScriptNpcHitPointsForTesting(const std::string& name, int32_t scriptNpcHp) {
    CharacterState& c = getOrCreateCharacter(name);
    // Same "forget the CONFIRMED defaults, draw lazily" shape as
    // markScriptNpcBoundForTesting() - see that method's own body and
    // CharacterState::scriptNpcHp's doc comment for the full rationale.
    c.ignoreAI.forget();
    c.maxHitPoints.forget();
    c.currentHitPoints.forget();
    c.scriptNpcBound = true;
    c.scriptNpcHp.set(scriptNpcHp);
}

// Design notes moved verbatim from EngineState::loadZoneObjectsSection's declaration in engine_state.h
// (refactor step 3a):
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
void EngineState::loadZoneObjectsSection(const sr3zone::ObjectsSection& section) {
    zoneObjects_.ingest(section);
    {
        // AI-7: collect every cover_node placement (named or not) as a positional source for the NPC cover search
        // (spec-ai-runtime Sec21.3 gather input). Only the position is decoded; see NpcAiRuntime::searchCover.
        static const uint32_t kCoverNodeClassHash = sr3save::nameHash("cover_node");
        bool any = false;
        for (const sr3zone::ObjectRecord& rec : section.records) {
            if (rec.classHash != kCoverNodeClassHash) continue;
            const std::optional<sr3zone::Position3f> p = sr3zone::ObjectPosition(rec);
            if (!p) continue;
            npcAiCoverNodes_.push_back({static_cast<double>(p->x), static_cast<double>(p->y), static_cast<double>(p->z)});
            any = true;
        }
        if (any) pushNpcAiCoverNodes();
    }
    std::unordered_set<std::string> lowerNames;
    for (const sr3zone::ObjectRecord& rec : section.records)
        if (rec.hasName && rec.nameText.has_value() && !rec.nameText->empty()) lowerNames.insert(AsciiLowerFold(*rec.nameText));
    for (const sr3zone::ObjectRecord& rec : section.records) {
        if (!rec.hasName || !rec.nameText.has_value() || rec.nameText->empty()) continue;
        const std::string& name = *rec.nameText;

        // Step 1: real zone data wins over the flat test-settable map - see
        // this method's own declaration (engine_state.h) for the full
        // rationale (exact-case, deliberately not folded - objectResolves()
        // is the existing shared simplification, left otherwise untouched).
        objectResolves_.set(name, true);

        // Step 2: script_npc_hp precedence (Sec34.2/Sec34.4), routed for
        // real - same mechanism bc7ff16's applyScriptNpcHitPointsForTesting()
        // already modelled as test-only, now driven by real ingested zone
        // data. Only for records whose class resolves to "script_npc" -
        // spec-zone-data-format.md's own real Killbane resolution uses this
        // exact class (crc32l('script_npc') = 0xa518e725, 6,788-object
        // population match) - a non-script_npc named object (a prop, a
        // navpoint, a trigger volume, etc.) has no script_npc_hp precedence
        // rule to apply and is left alone beyond step 1 above.
        std::optional<std::string> className = sr3zone::ClassNameForHash(rec.classHash);

        // Step 2a: NEW 2026-10-10 (vehicle-class xtbl wiring task) - the
        // vehicle-equivalent of step 2's script_npc precedence. A real
        // zone-placed vehicle instance's own class resolves to the literal
        // string "Script_Vehicle" (spec-zone-data-format.md's own "Resolved
        // classes include" list). If it also carries a decodable
        // `script_vehicle_type_name` property (sr3zone::ScriptVehicleTypeName(),
        // Sec11.7's own spec-cited name for the rest of the script_vehicle_*
        // family) that matches - case-folded, the SAME AsciiLowerFold()
        // convention this file's own ZoneObjectResolver already established -
        // an entry of installVehicleTypeClassTable() (the real xtbl-sourced
        // class data sr3vehicleinfo::ParseAllVehicles already computes
        // correctly, wired in by tools/lua_host_run.cpp's real-cache install
        // step), populates that vehicle instance's REAL class: flyingType
        // directly, and - only for class 3 (Helicopter) or 4 (Vtol),
        // HelicopterFlyTo::qualifies' own CONFIRMED gate (Sec28.2: "class in
        // {3,4}") - qualifies too. This is the real, general, data-driven
        // replacement for the single hardcoded "m05_heli_pierce" name
        // special case getOrCreateVehicle() used to carry (removed this same
        // task - see that method's own doc comment). A script_vehicle record
        // with no script_vehicle_type_name property, or one whose type is
        // not in the installed table (e.g. the table was never installed -
        // every synthetic unit test), is left exactly as OPEN as before -
        // never guessed at. Never touches getOrCreateCharacter() - a vehicle
        // is not a character, matching step 2's own "non-script_npc named
        // object... left alone beyond step 1" precedent for the reverse case.
        if (className.has_value() && *className == "Script_Vehicle") {
            // The generic player-class resolver masks every hit against the object's own player-class
            // descriptor bit, so a vehicle's name can NEVER resolve there (spec-lua-api-behaviour.md Sec3.3,
            // 2026-10-07). Zone data names the vehicles, so record that instead of the name-shape heuristic:
            // names like "start_truck" or "v_sb1_vtol 001" must register on_vehicle_enter/on_vehicle_destroyed
            // into the VEHICLE record, not a character slot.
            playerClassChainResolves_.set(name, false);
            playerClassChainResolves_.set(AsciiLowerFold(name), false);
            getOrCreateVehicle(name); // spawn defaults (+0x1d7a = 0)
            if (std::optional<std::string> flags = sr3zone::ScriptVehicleFlags(rec)) {
                // Space-separated tokens (0x00a3a7b0); 'invulnerable' (record bit 0x40) -> +0x1d7a bit 0x01 (0x00a39611).
                std::string lf = AsciiLowerFold(*flags);
                size_t pos = 0;
                while (pos < lf.size()) {
                    size_t e = lf.find_first_of(" \t,;", pos);
                    if (e == std::string::npos) e = lf.size();
                    if (lf.compare(pos, e - pos, "invulnerable") == 0)
                        getOrCreateVehicle(name).forceFlags1d7a.setBits(VehicleState::kInvulnerableBit1d7a,
                                                                        VehicleState::kInvulnerableBit1d7a);
                    pos = e + 1;
                }
            }
            std::optional<std::string> typeName = sr3zone::ScriptVehicleTypeName(rec);            if (typeName.has_value()) {
                auto it = vehicleTypeClassByLowerName_.find(AsciiLowerFold(*typeName));
                if (it != vehicleTypeClassByLowerName_.end()) {
                    VehicleState& v = getOrCreateVehicle(name);
                    v.flyingType.set(it->second);
                    if (it->second == 3 || it->second == 4) {
                        helicopterFlyTo_.qualifies.set(name, true);
                    }
                }
                // Special_Vehicle_Type (xtbl +0x4C4, spec Sec33.1 blocker m10): same data join, own table.
                auto sp = vehicleSpecialTypeByLowerName_.find(AsciiLowerFold(*typeName));
                if (sp != vehicleSpecialTypeByLowerName_.end()) getOrCreateVehicle(name).vehicleClass.set(sp->second);
            }
            continue; // no script_npc precedence rule applies to a vehicle
        }

        if (!className.has_value() || *className != "script_npc") continue;

        // ce4556f Sec29 (m19): group-member NPC, authored "Chem_02_brute", scripted
        // as "chem_02_brute". The alias is unresolvable (false) until its
        // "<name>_group" is created via group_create.
        {
            std::string lower = AsciiLowerFold(name);
            std::string groupLower = lower + "_group";
            if (lower != name && lowerNames.count(groupLower) && !groupsCreated_.count(groupLower) &&
                !objectResolves_.known(lower)) {
                objectResolves_.set(lower, false);
                pendingGroupMembers_[groupLower].push_back(lower);
            }
        }

        CharacterState& c = getOrCreateCharacter(name);
        // Same "forget the CONFIRMED defaults, draw lazily" shape as
        // markScriptNpcBoundForTesting()/applyScriptNpcHitPointsForTesting()
        // above - see those methods' own bodies and CharacterState::
        // scriptNpcHp's doc comment for the full rationale. Re-ingesting the
        // SAME record twice (e.g. two loadZoneObjectsSection() calls over
        // the same file) is idempotent: forgetting+re-setting the identical
        // value changes nothing observable.
        c.maxHitPoints.forget();
        c.currentHitPoints.forget();
        c.scriptNpcBound = true;
        // The authored team name (zone `script_npc_team`): on_human_killed's first argument. The engine maps the
        // team byte to a name through a team table this project does not have (OPEN), so the authored string
        // stands in; absent -> stays "" and the callback gets nil.
        if (std::optional<std::string> team = sr3zone::ScriptNpcTeam(rec); team.has_value()) c.teamName = *team;

        // FIX 2026-10-08 (sibling defect to the script_npc_hp fix below,
        // found while re-measuring this task's probe after that fix: ignore-
        // AI became the NEW dominant blocker, 31/49 missions, for the exact
        // same reason). spec-lua-api-behaviour.md Sec34.3's own CONFIRMED
        // text: ignore-AI's real default is OFF, overridden to ON only when
        // the resolved record's own `script_npc_flags` property contains the
        // literal token `ignore_ai` (Sec11.7: true for only 425 of 6,702
        // script_npc_flags-bearing records, and of course never true for a
        // script_npc record with no script_npc_flags property at all - 86 of
        // 6,788). Once this record is resolved (which it now is), that is a
        // CONFIRMED, fully-decidable fact either way, not an open question -
        // forgetting it here unconditionally (as the previous code did)
        // misapplied the SAME "known: no override, use the default" vs
        // "unknown" conflation Sec34.2's own script_npc_hp fix below
        // corrects, just for this sibling field.
        std::optional<std::string> flags = sr3zone::ScriptNpcFlags(rec);
        c.ignoreAI.set(flags.has_value() && ScriptNpcFlagsHasIgnoreAiToken(*flags));

        std::optional<uint64_t> hp = sr3zone::ScriptNpcHp(rec);
        if (hp.has_value() && *hp <= static_cast<uint64_t>(INT32_MAX)) {
            c.scriptNpcHp.set(static_cast<int32_t>(*hp));
        } else if (!hp.has_value()) {
            // FIX 2026-10-08 (mission-blockers investigation,
            // probe_blockers_ranked_20261008_post_resolver_merge.md item 1 -
            // "character max hit points (+0x1cac) OPEN", 26/49 missions, by
            // far this project's largest single blocker at the time).
            //
            // ROOT CAUSE: no script_npc_hp property on THIS RESOLVED record
            // is the documented common case (Sec11.7: only 1,028 of 6,788
            // script_npc objects carry one - the other ~5,760 do not), and
            // spec-lua-api-behaviour.md Sec34.2 is explicit and CONFIRMED
            // (disassembly) that the absence has a real, known meaning:
            // "A script NPC's own constructor defaults that field to -1
            // ('use the table'); commando_spawn-produced script NPCs ...
            // never set script_npc_hp, so they always fall back to the
            // table." A placement record that never authors the property
            // simply leaves that constructor default untouched - this is a
            // CONFIRMED FACT about this specific, already-resolved record,
            // not the separate "which record does an arbitrary Lua name
            // even map to" gap Sec34.4 flags as needing the (now-lifted)
            // `.czn_pc` zone-placement interior. The previous code called
            // c.scriptNpcHp.forget() here, conflating the two: it treated
            // "known: no override, use the table" the same as "unknown,"
            // which left drawOrGetMaxHitPoints() (lua_engine_state.cpp)
            // refusing (OpenStateError) forever for every script_npc
            // character that lacks an authored override - the large
            // majority - even though the real engine's own answer for that
            // exact case is simply "use spawn_info_ranks.xtbl like an
            // ordinary character." That misclassification, not a genuine
            // zone-data gap, is what the 26-mission blocker traced back to:
            // get_max_hit_points/set_current_hit_points (Sec7.12/Sec7.31)
            // are called 3,133 times across the real mission population,
            // and every script_npc-bound character without an authored
            // script_npc_hp hit this exact refusal.
            //
            // FIX: set the real, CONFIRMED default (-1) instead of
            // forgetting it. drawOrGetMaxHitPoints()'s own already-correct
            // precedence rule (">= 1 wins verbatim over the table; a known
            // value < 1 falls through to the ordinary table draw, same as a
            // non-scriptNpcBound character") then takes the table path for
            // real, exactly as Sec34.2 describes - closing this field for
            // every script_npc character whose REAL placement record (now
            // resolved, not guessed) simply has no override. A character
            // whose name never resolves to any zone record at all never
            // reaches this method in the first place (scriptNpcBound stays
            // false, the ordinary non-script-NPC draw path already handled
            // it) - this fix changes nothing for that case.
            c.scriptNpcHp.set(-1);
        } else {
            // A value this project's own int32_t field genuinely cannot
            // represent (never observed in the real population, Sec11.7's
            // own "up to 100,000" figure) - stays OPEN, never guessed at.
            c.scriptNpcHp.forget();
        }
    }

    // Case-insensitive name lookup (spec-lua-api-behaviour.md, 2026-10-14 blocker m01 `gat_captive_nav`, FINAL:
    // the global name lookup 0x004588f0 is case-insensitive, so a script's lowercase `gat_captive_nav` resolves
    // the object authored `Gat_captive_nav`; "register every object in the loaded mission zone by name when the
    // zone loads"). Done as a second pass so the group-member aliases above (resolvable only after group_create)
    // are never overridden.
    for (const sr3zone::ObjectRecord& rec : section.records) {
        if (!rec.hasName || !rec.nameText.has_value() || rec.nameText->empty()) continue;
        const std::string lower = AsciiLowerFold(*rec.nameText);
        if (lower != *rec.nameText && !objectResolves_.known(lower)) objectResolves_.set(lower, true);
    }
}

int32_t EngineState::drawOrGetCurrentHitPoints(const std::string& name) {
    // Sec34.2: "Current HP (+0x1cb8) starts equal to the table value (step 2 of Sec34.1)". Whether it is refilled
    // to match a script_npc_hp override differing from the table is OPEN - and drawOrGetMaxHitPoints() throws
    // OpenStateError for exactly that state (script-NPC-bound, override unknown), so it stays OPEN here too.
    CharacterState& c = getOrCreateCharacter(name);
    if (!c.currentHitPoints.known()) c.currentHitPoints.set(drawOrGetMaxHitPoints(name));
    return c.currentHitPoints.get();
}

// Design notes moved verbatim from EngineState::drawOrGetMaxHitPoints's declaration in engine_state.h
// (refactor step 3a):
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
int32_t EngineState::drawOrGetMaxHitPoints(const std::string& name) {
    // See CharacterState::maxHitPoints's "UPDATE 2026-10-07" doc comment and
    // this method's own declaration (engine_state.h) for the full rationale.
    CharacterState& c = getOrCreateCharacter(name);
    if (!c.maxHitPoints.known()) {
        // Sec34.2's own CONFIRMED override, reachable only when a caller
        // already supplied scriptNpcHp (applyScriptNpcHitPointsForTesting() -
        // see CharacterState::scriptNpcHp's own doc comment for why this
        // project cannot resolve it on its own): >= 1 wins verbatim over the
        // table; a known value < 1 is the script NPC constructor's own -1
        // "use the table" default and falls through to the ordinary draw
        // below, same as a non-scriptNpcBound character. Genuinely unknown
        // (scriptNpcBound with no scriptNpcHp ever supplied - the only state
        // any real production path in this project reaches today) stays
        // OPEN exactly as before this update.
        if (c.scriptNpcBound && c.scriptNpcHp.known() && c.scriptNpcHp.get() >= 1) {
            c.maxHitPoints.set(c.scriptNpcHp.get());
        } else if (!c.scriptNpcBound || c.scriptNpcHp.known()) {
            int32_t drawn = 100; // CHOSEN diagnostic fallback - same constant this field's own callers already used
            if (!spawnRankHitPoints_.empty()) {
                // splitmix64 (public-domain reference algorithm), same shape as
                // BareRandomSource::hostStep (bare_globals.h) - a second,
                // independent HOST SUBSTITUTE instance, seeded from the name
                // alone (via the already-established CRC-32 name hash,
                // sr3save::nameHash) so a given name always draws the same
                // value. Not the real engine's own level-draw generator (OPEN,
                // Sec34.2's own "0x0096e830" note) - a stated stand-in.
                uint64_t state = static_cast<uint64_t>(sr3save::nameHash(name)) ^ 0x9E3779B97F4A7C15ull;
                state += 0x9E3779B97F4A7C15ull;
                uint64_t z = state;
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                z ^= z >> 31;
                drawn = spawnRankHitPoints_[static_cast<size_t>(z % spawnRankHitPoints_.size())];
            }
            c.maxHitPoints.set(drawn);
        }
        // else: scriptNpcBound with scriptNpcHp genuinely unknown - stays OPEN (Sec34.4).
    }
    return c.maxHitPoints.get(); // still throws OpenStateError for a scriptNpcBound character with no known override - CONFIRMED-correct
}

void EngineState::registerSyncedActionForTesting(const std::string& name, int32_t index) {
    syncedActionIndexByName_[name] = index;
}

int32_t EngineState::lookupSyncedActionIndex(const std::string& name) const {
    auto it = syncedActionIndexByName_.find(name);
    return it != syncedActionIndexByName_.end() ? it->second : kSyncedActionNotFound;
}

void EngineState::actionSequenceEnd() {
    // action_sequence_end (Sec33.3): clears the shared sequence object and
    // per-player scripted-camera/target fields; releases two tracked
    // handles (this project's own combined counter, see ActionSequence's
    // own doc comment); host-only broadcasts an opcode-0x49 record.
    actionSequence_.active = false;
    actionSequence_.localScriptedCameraTarget.clear();
    actionSequence_.remoteScriptedCameraTarget.clear();
    actionSequence_.trackedHandleReleaseCount += 2;
    if (coopLocalIsHost()) ++actionSequence_.hostBroadcastCount;
}

// Design notes moved verbatim from EngineState::replicateStateChange's declaration in engine_state.h
// (refactor step 3a):
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
void EngineState::replicateStateChange(const std::string& fieldTag, const std::string& objectName) {
    // Stated, explicit no-op (see engine_state.h's own doc comment on this
    // function): a real coop/network-sync commit would happen here on the
    // real engine. This project builds no networking layer at all, so
    // every real call site that would open one of these records instead
    // calls this function - purely so those call sites are visibly marked
    // in the source, not silently dropped - and does nothing further.
    // Parameters intentionally unused beyond documenting the call site.
    (void)fieldTag;
    (void)objectName;
}

// Design notes moved verbatim from EngineState::registerVdoObjectForTesting's declaration in engine_state.h
// (refactor step 3a):
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
uint32_t EngineState::registerVdoObjectForTesting(const std::string& name, uint32_t parentHandle, uint32_t docHandle) {
    uint32_t handle = nextVdoObjectHandle_++;
    VdoObject obj;
    obj.name = name;
    obj.nameHash = sr3save::nameHash(name); // 0x00d9e740 stand-in - see engine_state.h's own doc comment
    obj.parentHandle = parentHandle;
    obj.docHandle = docHandle;
    vdoObjects_[handle] = obj;
    return handle;
}

// Design notes moved verbatim from EngineState::vdoObjectForTesting's declaration in engine_state.h
// (refactor step 3a):
// Test-only direct read of a registered VDO object's own fields - no
// Lua-visible getter exists for parentHandle/docHandle among this
// document's 8-name Sec18-Sec21 scope (vint_object_parent/
// vint_object_get_doc are both out of scope), so cloneVdoObject's own
// parent/document assignment (Sec18.2) can only be checked this way.
// Returns nullptr for an unregistered handle.
const VdoObject* EngineState::vdoObjectForTesting(uint32_t handle) const {
    auto it = vdoObjects_.find(handle);
    return it == vdoObjects_.end() ? nullptr : &it->second;
}

// Design notes moved verbatim from EngineState::findVdoObject's declaration in engine_state.h
// (refactor step 3a):
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
uint32_t EngineState::findVdoObject(const std::string& name, bool hasParent, uint32_t parentHandle,
                                     bool hasDoc, uint32_t docHandle) const {
    uint32_t targetHash = sr3save::nameHash(name);
    // CONFIRMED (Sec15): "parent_handle, if given, resolves through
    // 0x00e28850 ... then the hashed name is looked up among that
    // resolved parent's children, OR document-wide if no parent was
    // resolved" - "no parent was resolved" covers BOTH an absent/wrong-
    // type argument AND a given-but-unresolvable (stale/bad) handle
    // number; either way this falls through to the document-wide search
    // below, it does NOT short-circuit to "not found" on a bad handle
    // number alone.
    bool parentResolved = hasParent && (vdoObjects_.find(parentHandle) != vdoObjects_.end());
    if (parentResolved) {
        for (const auto& [h, obj] : vdoObjects_) {
            if (obj.parentHandle == parentHandle && obj.nameHash == targetHash) return h;
        }
        return 0; // resolved parent, no matching child - real "not found" path
    }
    // The current default document is OPEN state; read it only when the
    // answer depends on it. With no object registered under this name at
    // all, "not found" holds for every document, so it is not read.
    bool anyWithName = false;
    for (const auto& [h, obj] : vdoObjects_) {
        if (obj.nameHash == targetHash) { anyWithName = true; break; }
    }
    if (!anyWithName) return 0;
    uint32_t targetDoc = hasDoc ? docHandle : currentDefaultDocHandle_.get();
    for (const auto& [h, obj] : vdoObjects_) {
        if (obj.docHandle == targetDoc && obj.nameHash == targetHash) return h;
    }
    return 0;
}

void EngineState::setVdoObjectFirstChildForTesting(uint32_t parentHandle, uint32_t childHandle) {
    auto it = vdoObjects_.find(parentHandle);
    if (it == vdoObjects_.end()) return; // test/setup-only - no-op on an unregistered parent, nothing to attach to
    it->second.firstChildHandle = childHandle;
}

// Design notes moved verbatim from EngineState::vdoObjectFirstChild's declaration in engine_state.h
// (refactor step 3a):
// vint_object_first_child's own CONFIRMED mechanism (Sec18.1): resolve
// `handle` (the shared resolver, same as findVdoObject), read its
// firstChildHandle. Returns 0 for EITHER a bad handle OR a resolved
// object with no first child - both real, CONFIRMED paths that push zero
// Lua values (never 0.0), which the caller (the Lua stub) implements by
// simply not pushing anything when this returns 0 (handle 0 is never a
// real child handle - same sentinel reasoning as VdoObject::parentHandle).
uint32_t EngineState::vdoObjectFirstChild(uint32_t handle) const {
    auto it = vdoObjects_.find(handle);
    if (it == vdoObjects_.end()) return 0; // CONFIRMED (Sec18.1): bad handle -> zero Lua values
    return it->second.firstChildHandle; // 0 either way is "no children" - CONFIRMED, zero Lua values
}

// Design notes moved verbatim from EngineState::cloneVdoObject's declaration in engine_state.h
// (refactor step 3a):
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
uint32_t EngineState::cloneVdoObject(uint32_t origHandle, bool hasParentArg, uint32_t parentHandleArg) {
    // Step 1 (CONFIRMED order, Sec18.2): the current document is resolved
    // BEFORE touching either handle - an OPEN read here refuses before any
    // other work happens, same "every read before any write" convention
    // already established elsewhere in this file (e.g. zscenePrep).
    uint32_t doc = currentDefaultDocHandle_.get();

    // Step 2: resolve the source object.
    auto origIt = vdoObjects_.find(origHandle);
    if (origIt == vdoObjects_.end()) return 0; // CONFIRMED: bad orig_handle -> 0.0 (the real engine also logs a debug string here, not Lua-visible)
    const VdoObject& orig = origIt->second;

    // Step 3: resolve the requested parent, falling back to the original's
    // own parent (CONFIRMED, Sec18.2).
    uint32_t parent = (hasParentArg && vdoObjects_.count(parentHandleArg)) ? parentHandleArg : orig.parentHandle;

    // Step 4: the real virtual clone call - this project's own minimal
    // stand-in (see cloneVdoObject's own header doc comment for why the
    // new object's content is not copied). No further failure condition is
    // modeled, so this always succeeds once the above resolves.
    uint32_t handle = nextVdoObjectHandle_++;
    VdoObject clone;
    clone.parentHandle = parent;
    clone.docHandle = doc;
    vdoObjects_[handle] = clone;
    return handle;
}

} // namespace sr3luahost
