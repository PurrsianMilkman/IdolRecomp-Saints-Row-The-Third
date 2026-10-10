// Synthetic tests for the Team-A-cleared batch (commit 97a9b8d):
// spec-lua-api-behaviour.md Sec1.1 trigger_enable, Sec1.2 on_trigger,
// Sec1.4 objective_text_clear, Sec1.5 mission_end_failure, Sec1.6
// mission_set_checkpoint, Sec2.1 game_get_platform, Sec2.5
// audio_play_persona_line, Sec2.6 game_is_active_input_gamepad, Sec3.2
// on_death, Sec3.3 on_vehicle_destroyed, Sec3.5 character_is_dead, Sec3.6
// notoriety_force_no_spawn. Same fixture convention as
// tests/synthetic_luahost_native_batch1007_test.cpp; real cluster tags
// verified directly against tools/lua_all_registered_1490_tagged.txt
// (grepped, not assumed): all 12 are "gameplay" EXCEPT game_get_platform
// ("ui"); game_is_active_input_gamepad is ALSO dual-registered into the UI
// state (src/lua_host.cpp's own specUiNames) - not exercised via this
// fixture Host (which only registers the names it's given), see this
// file's own registration-check block for how that's covered instead.
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "trigger_mission_cluster_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code`, expecting a Lua error whose message contains `needle`.
void refuses(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "trigger_mission_cluster_test.lua");
    if (!r.loadOk || r.pcallOk || r.pcallError.find(needle) == std::string::npos) {
        std::cerr << "expected refusal containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("success") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

std::vector<RegisteredName> fixtureNames() {
    return {
        {"trigger_enable", "gameplay"},
        {"on_trigger", "gameplay"},
        {"objective_text_clear", "gameplay"},
        {"mission_end_failure", "gameplay"},
        {"mission_set_checkpoint", "gameplay"},
        {"game_get_platform", "ui"},
        {"audio_play_persona_line", "gameplay"},
        {"game_is_active_input_gamepad", "gameplay"},
        {"on_death", "gameplay"},
        {"on_vehicle_destroyed", "gameplay"},
        {"character_is_dead", "gameplay"},
        {"notoriety_force_no_spawn", "gameplay"},
    };
}

} // namespace

int main() {
    // Registration: all 12 land in gameplay, EXCEPT game_get_platform
    // (ui-only per its own tagged cluster). Host's own constructor
    // (src/lua_host.cpp) is the SAME single function production code and
    // every test fixture both go through - its own hardcoded
    // `specUiNames.push_back("game_is_active_input_gamepad")` (the real
    // dual-registration fix, same pattern as its neighbouring
    // `specUiNames.push_back("coop_is_active")`) runs UNCONDITIONALLY,
    // regardless of this fixture's own tag choice, so
    // game_is_active_input_gamepad is expected in BOTH states here too.
    {
        Host host(fixtureNames());
        for (const auto& rn : fixtureNames()) {
            bool expectGameplay = rn.name != "game_get_platform";
            bool expectUi = rn.name == "game_get_platform" || rn.name == "game_is_active_input_gamepad";
            CHECK(hasGlobal(host.gameplayState(), rn.name.c_str()) == expectGameplay);
            CHECK(hasGlobal(host.uiState(), rn.name.c_str()) == expectUi);
        }
    }

    // trigger_enable (Sec1.1): unresolved refuses; resolved records the
    // raw (enabled, flags) pair, defaults true/3.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "trigger_enable('NoTrigger')", "named-object resolution");
        es.objectResolves().set("KnownMissingTrigger", false);
        ok(host, gp, "trigger_enable('KnownMissingTrigger')"); // known not to resolve: silent no-op, no crash
        CHECK(!es.triggerEnableKnown("KnownMissingTrigger"));
        es.objectResolves().set("Trig1", true);
        ok(host, gp, "trigger_enable('Trig1')"); // defaults: enable=true, flags=3
        CHECK(es.triggerEnableKnown("Trig1"));
        CHECK(es.triggerEnableValue("Trig1") == true);
        CHECK(es.triggerEnableFlagsValue("Trig1") == 3);
        ok(host, gp, "trigger_enable('Trig1', false, 7)");
        CHECK(es.triggerEnableValue("Trig1") == false);
        CHECK(es.triggerEnableFlagsValue("Trig1") == 7);
    }

    // on_trigger (Sec1.2): unresolved refuses; resolved registers the
    // enter-mode callback (the +0xa8 gate always reads unset in this
    // closed world, see the stub's own doc comment, so registration always
    // proceeds once the trigger resolves).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "on_trigger('cb1', 'NoTrigger')", "named-object resolution");
        es.objectResolves().set("Trig2", true);
        ok(host, gp, "on_trigger('on_enter_cb', 'Trig2')");
        CHECK(es.onTriggerCallbackKnown("Trig2"));
        CHECK(es.onTriggerCallbackValue("Trig2") == "on_enter_cb");
        // Empty callback name clears the slot (plain assignment).
        ok(host, gp, "on_trigger('', 'Trig2')");
        CHECK(es.onTriggerCallbackValue("Trig2") == "");
    }

    // objective_text_clear (Sec1.4): no resolve gate at all; records a
    // per-slot call count. The slot index is read UNCONDITIONALLY (no
    // nil-gate) - calling with no argument reads as slot 0.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        CHECK(es.objectiveTextClearCount(3) == 0);
        ok(host, gp, "objective_text_clear(3)");
        ok(host, gp, "objective_text_clear(3)");
        ok(host, gp, "objective_text_clear(4)");
        CHECK(es.objectiveTextClearCount(3) == 2);
        CHECK(es.objectiveTextClearCount(4) == 1);
        ok(host, gp, "objective_text_clear()"); // unconditional read: absent -> 0
        CHECK(es.objectiveTextClearCount(0) == 1);
    }

    // mission_end_failure (Sec1.5): gate now KNOWN false by default (fixed
    // 2026-10-08, spec-lua-api-behaviour.md Sec21.27a - a fresh EngineState
    // models the real engine's own idle start-up phase, 0x014c7a14 == 0,
    // not an unmodelled value) -> silent no-op; name mismatch short-circuits
    // before the ending-bit read (no throw even though currentMissionEnding
    // stays OPEN); full pass arms the shared mission-end-transition timer.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "mission_end_failure('m1', 'reason')"); // gate KNOWN false by default: silent no-op
        CHECK(es.missionContext().missionEndTimer1Armed == false);
        es.missionContext().activeAndRunning.set(false); // redundant with the real default - kept explicit for clarity
        ok(host, gp, "mission_end_failure('m1', 'reason')"); // gate false: silent no-op
        CHECK(es.missionContext().missionEndTimer1Armed == false);
        es.missionContext().activeAndRunning.set(true);
        es.missionContext().currentMissionName.set("other_mission");
        ok(host, gp, "mission_end_failure('m1', 'reason')"); // name mismatch short-circuits before reading currentMissionEnding
        CHECK(es.missionContext().missionEndTimer1Armed == false);
        es.missionContext().currentMissionName.set("m1");
        es.missionContext().currentMissionEnding.set(true);
        ok(host, gp, "mission_end_failure('m1', 'reason')");
        CHECK(es.missionContext().missionEndTimer1Armed == true);
        CHECK(es.missionContext().missionEndFailureDispatchCount == 1);
    }

    // mission_set_checkpoint (Sec1.6): CONFIRMED "return 0" unconditionally
    // - the function itself never raises a Lua error from the gate. Fixed
    // 2026-10-08 (Sec21.27a): activeAndRunning is now KNOWN false by
    // default (the real engine's own idle start-up phase), not OPEN, so
    // this is a plain silent no-op from the very first call, no throw.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "mission_set_checkpoint('cp1')"); // gate KNOWN false by default: silent no-op
        CHECK(es.missionCheckpoint().setCount == 0);
        es.missionContext().activeAndRunning.set(false); // redundant with the real default - kept explicit for clarity
        ok(host, gp, "mission_set_checkpoint('cp1')"); // gate false: silent no-op
        CHECK(es.missionCheckpoint().setCount == 0);
        es.missionContext().activeAndRunning.set(true);
        es.missionContext().missionEndTimer1Armed = true;
        ok(host, gp, "mission_set_checkpoint('cp1')"); // debounce: timer 1 armed, silent no-op
        CHECK(es.missionCheckpoint().setCount == 0);
        es.missionContext().missionEndTimer1Armed = false;
        es.missionContext().missionEndTimer2Armed = true;
        ok(host, gp, "mission_set_checkpoint('cp1')"); // debounce: timer 2 armed, silent no-op
        CHECK(es.missionCheckpoint().setCount == 0);
        es.missionContext().missionEndTimer2Armed = false;
        // arg 2 = true: always the player's-own-position branch, never
        // consults usePlayerVehicleTransform (still OPEN here - no throw).
        ok(host, gp, "mission_set_checkpoint('cp2', true, true)");
        CHECK(es.missionCheckpoint().lastName == "cp2");
        CHECK(es.missionCheckpoint().setCount == 1);
        CHECK(es.missionCheckpoint().lastUsedVehicleTransform == false);
        CHECK(es.missionCheckpoint().lastLoadoutSlotsZeroed == true);
        // arg 2 = false (default): now usePlayerVehicleTransform IS consulted.
        es.missionCheckpoint().usePlayerVehicleTransform.set(true);
        ok(host, gp, "mission_set_checkpoint('cp3')");
        CHECK(es.missionCheckpoint().lastName == "cp3");
        CHECK(es.missionCheckpoint().lastUsedVehicleTransform == true);
        CHECK(es.missionCheckpoint().lastLoadoutSlotsZeroed == false);
        es.missionCheckpoint().usePlayerVehicleTransform.set(false);
        ok(host, gp, "mission_set_checkpoint('cp4')");
        CHECK(es.missionCheckpoint().lastUsedVehicleTransform == false);
    }

    // game_get_platform (Sec2.1): pure constant, no state, no arguments used.
    {
        Host host(fixtureNames());
        lua_State* ui = host.uiState();
        ok(host, ui, "assert(game_get_platform() == 'PC')");
        ok(host, ui, "assert(game_get_platform(1, 2, 3) == 'PC')"); // arguments silently ignored
    }

    // audio_play_persona_line (Sec2.5): persona OPEN refuses; persona
    // KNOWN false -> fallback 255.0, no throw; persona resolves but line
    // name absent (real NULL, not "") -> also 255.0; persona resolves AND
    // line name present (even "") -> a real, increasing handle.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "audio_play_persona_line('NoPersona', 'line1')", "named-object resolution");
        es.objectResolves().set("KnownMissingPersona", false);
        ok(host, gp, "assert(audio_play_persona_line('KnownMissingPersona', 'line1') == 255.0)");
        es.objectResolves().set("Persona1", true);
        ok(host, gp, "assert(audio_play_persona_line('Persona1') == 255.0)"); // arg 2 absent: real NULL pointer, 0x0070a2f0's own null-check fails
        ok(host, gp, "local h1 = audio_play_persona_line('Persona1', '')\n"   // arg 2 present but empty: succeeds
                     "assert(h1 ~= 255.0 and h1 > 0)");
        ok(host, gp, "local h1 = audio_play_persona_line('Persona1', 'line1')\n"
                     "local h2 = audio_play_persona_line('Persona1', 'line2', 42)\n" // arg 3 read then discarded
                     "assert(h1 > 0 and h2 == h1 + 1)");
    }

    // game_is_active_input_gamepad (Sec2.6): pure read, CONFIRMED
    // zero-filled .data construction default (false).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        ok(host, gp, "assert(game_is_active_input_gamepad() == false)");
        es.activeInputIsGamepad() = true;
        ok(host, gp, "assert(game_is_active_input_gamepad() == true)");
    }

    // on_death (Sec3.2): unresolved refuses; resolved registers the
    // callback into hook slot 0; empty callback clears it.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        refuses(host, gp, "on_death('cb', 'NoCharacter')", "named-object resolution");
        es.objectResolves().set("Vic1", true);
        ok(host, gp, "on_death('die_cb', 'Vic1')");
        CHECK(es.getOrCreateCharacter("Vic1").onDeathCallback == "die_cb");
        ok(host, gp, "on_death('', 'Vic1')");
        CHECK(es.getOrCreateCharacter("Vic1").onDeathCallback == "");
    }

    // on_vehicle_destroyed (Sec3.3): player-class-gated generic path ->
    // character hook slot 18; fallback vehicle-name path -> the vehicle's
    // own hook slot; neither resolving -> no-op, no throw once both are
    // KNOWN false. playerClassChainResolves(name) (the 1-arg overload every
    // consumer now calls) carries a CHOSEN (not spec-confirmed) default for
    // any name no test .set() an explicit value for - see that overload's
    // own doc comment, engine_state.h - so this block no longer needs a
    // manual .set() to avoid the old "class-bit gate... is OPEN" refusal.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        // CHOSEN default, no .set() at all: "Never" is not vehicle-shaped,
        // so it resolves player-class TRUE by default and lands on the
        // primary/character path.
        ok(host, gp, "on_vehicle_destroyed('on_veh_cb', 'Never')");
        CHECK(es.getOrCreateCharacter("Never").onVehicleDestroyedPlayerSlotCallback == "on_veh_cb");
        CHECK(!es.hasVehicle("Never"));
        // CHOSEN default, no .set() on playerClassChainResolves() at all:
        // "debris_vehicle 001" matches this project's own vehicle-naming
        // convention (substring "vehicle" - the same name this gate actually
        // blocked in the mission-drive probe, m02), so it resolves FALSE by
        // default and falls through to the vehicle fallback path (still
        // needs objectResolves() known for liveness - a SEPARATE, unrelated
        // gate, Sec3.9/Sec10.6/Sec29).
        es.objectResolves().set("debris_vehicle 001", true);
        ok(host, gp, "on_vehicle_destroyed('veh_cb', 'debris_vehicle 001')");
        CHECK(es.getOrCreateVehicle("debris_vehicle 001").onVehicleDestroyedCallback == "veh_cb");
        CHECK(!es.hasCharacter("debris_vehicle 001") ||
              es.getOrCreateCharacter("debris_vehicle 001").onVehicleDestroyedPlayerSlotCallback.empty());

        es.playerClassChainResolves().set("PlayerSlot1", true);
        ok(host, gp, "on_vehicle_destroyed('on_veh_cb', 'PlayerSlot1')");
        CHECK(es.getOrCreateCharacter("PlayerSlot1").onVehicleDestroyedPlayerSlotCallback == "on_veh_cb");
        CHECK(!es.hasVehicle("PlayerSlot1"));
        es.playerClassChainResolves().set("Bootlegger1", false);
        es.objectResolves().set("Bootlegger1", true);
        ok(host, gp, "on_vehicle_destroyed('veh_cb', 'Bootlegger1')");
        CHECK(es.getOrCreateVehicle("Bootlegger1").onVehicleDestroyedCallback == "veh_cb");
        CHECK(!es.hasCharacter("Bootlegger1") || es.getOrCreateCharacter("Bootlegger1").onVehicleDestroyedPlayerSlotCallback.empty());
        es.playerClassChainResolves().set("Neither1", false);
        es.objectResolves().set("Neither1", false);
        ok(host, gp, "on_vehicle_destroyed('cb', 'Neither1')"); // both known false: no-op, no throw
        CHECK(!es.hasVehicle("Neither1"));
    }

    // character_is_dead (Sec3.5): both paths unresolved -> dead (true),
    // the CONFIRMED null-pointer branch; resolved+alive -> false; the
    // shared mission-end-transition-timer override applies ONLY on the
    // primary (player-class) branch. playerClassChainResolves(name)'s own
    // CHOSEN default (engine_state.h) means an untouched, non-vehicle-shaped
    // name takes the primary path with no .set() call needed.
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();
        // CHOSEN default: "Never" is not vehicle-shaped -> primary/player-
        // class path by default. A freshly-created character seeds
        // lifeState=0/killFinalizedFlagE3=false (both "seeded at
        // construction, not OPEN by default" - getOrCreateCharacter), so
        // this resolves to false end-to-end with no .set() at all.
        ok(host, gp, "assert(character_is_dead('Never') == false)");
        // CHOSEN default, the other direction: a vehicle-shaped name
        // defaults to FALSE on the primary gate and falls to the fallback
        // chain instead (still needs objectResolves() known - a SEPARATE
        // gate); the mission-end-timer override must NOT apply there even
        // armed, matching the existing NpcY case below (fallback-only).
        es.objectResolves().set("Veh CP SJ Spotlight Heli", true);
        es.getOrCreateCharacter("Veh CP SJ Spotlight Heli").lifeState.set(0);
        es.getOrCreateCharacter("Veh CP SJ Spotlight Heli").killFinalizedFlagE3.set(false);
        es.missionContext().missionEndTimer1Armed = true;
        ok(host, gp, "assert(character_is_dead('Veh CP SJ Spotlight Heli') == false)");
        es.missionContext().missionEndTimer1Armed = false;

        es.playerClassChainResolves().set("Ghost1", false);
        es.objectResolves().set("Ghost1", false);
        ok(host, gp, "assert(character_is_dead('Ghost1') == true)"); // neither resolves: null-pointer branch

        es.playerClassChainResolves().set("PlayerX", true);
        es.getOrCreateCharacter("PlayerX").lifeState.set(0);
        es.getOrCreateCharacter("PlayerX").killFinalizedFlagE3.set(false);
        ok(host, gp, "assert(character_is_dead('PlayerX') == false)");
        // Mission-end-transition-timer override - primary branch only.
        es.missionContext().missionEndTimer1Armed = true;
        ok(host, gp, "assert(character_is_dead('PlayerX') == true)");
        es.missionContext().missionEndTimer1Armed = false;
        ok(host, gp, "assert(character_is_dead('PlayerX') == false)");

        // Fallback-only branch: lifeState == 5 reports dead directly;
        // the timer override must NOT apply here even if armed.
        es.playerClassChainResolves().set("NpcY", false);
        es.objectResolves().set("NpcY", true);
        es.getOrCreateCharacter("NpcY").lifeState.set(5);
        es.missionContext().missionEndTimer1Armed = true;
        ok(host, gp, "assert(character_is_dead('NpcY') == true)"); // lifeState==5 alone already true
        es.getOrCreateCharacter("NpcY").lifeState.set(0);
        es.getOrCreateCharacter("NpcY").killFinalizedFlagE3.set(false);
        ok(host, gp, "assert(character_is_dead('NpcY') == false)"); // fallback branch: timer armed but NOT applied
        es.missionContext().missionEndTimer1Armed = false;
    }

    // notoriety_force_no_spawn (Sec3.6): arg 1 names a track via a
    // DISTINCT lookup (never objectResolves()); unregistered -> code 9,
    // silent no-op; each reachable code sets its own fixed base
    // address(es) directly to arg 2 (no nil-gate).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        ok(host, gp, "notoriety_force_no_spawn('UnknownTrack', true)"); // code 9 (not found): no-op
        CHECK(!es.notorietyTrackFlagKnown(0x025e9000));

        es.registerNotorietyTrackCodeForTesting("Deckers", 1);
        ok(host, gp, "notoriety_force_no_spawn('Deckers', true)");
        CHECK(es.notorietyTrackFlagKnown(0x025e9000) && es.notorietyTrackFlagValue(0x025e9000) == true);

        es.registerNotorietyTrackCodeForTesting("Morningstar", 2);
        ok(host, gp, "notoriety_force_no_spawn('Morningstar', true)");
        CHECK(es.notorietyTrackFlagValue(0x025e9460) == true);

        es.registerNotorietyTrackCodeForTesting("Police", 3);
        ok(host, gp, "notoriety_force_no_spawn('Police', true)");
        CHECK(es.notorietyTrackFlagValue(0x025e98c0) == true);
        CHECK(es.notorietyTrackFlagValue(0x025e9d20) == true);

        es.registerNotorietyTrackCodeForTesting("DeadCode", 4);
        ok(host, gp, "notoriety_force_no_spawn('DeadCode', true)"); // CONFIRMED hard no-op
        CHECK(!es.notorietyTrackFlagKnown(0x025e9000 + 1)); // nothing new touched

        es.registerNotorietyTrackCodeForTesting("Luchadores", 5);
        ok(host, gp, "notoriety_force_no_spawn('Luchadores', true)");
        static const uint32_t kCode5Bases[10] = {0x025e8740, 0x025e8ba0, 0x025eaee0, 0x025eb340, 0x025ea190,
                                                  0x025ea600, 0x025eaa70, 0x025eb7a0, 0x025ebc10, 0x025ec080};
        for (uint32_t base : kCode5Bases) CHECK(es.notorietyTrackFlagValue(base) == true);

        es.registerNotorietyTrackCodeForTesting("STAG", 6);
        ok(host, gp, "notoriety_force_no_spawn('STAG', true)");
        static const uint32_t kCode6Bases[5] = {0x025ec4e0, 0x025ec940, 0x025ecdb0, 0x025ed220, 0x025ed690};
        for (uint32_t base : kCode6Bases) CHECK(es.notorietyTrackFlagValue(base) == true);

        // No nil/absence gate on arg 2: omitted reads as false (the real
        // luaO_nilobject sentinel path), not an error.
        ok(host, gp, "notoriety_force_no_spawn('Deckers')");
        CHECK(es.notorietyTrackFlagValue(0x025e9000) == false);
    }

    // Sec21.27a/Sec21.27b upgrade (this task, 2026-10-08): the REAL integer
    // mission-phase machine (0-8) and the REAL end-of-mission dispatcher/
    // completion-flag writer, additive to the boolean-only activeAndRunning
    // facade this file's own earlier blocks already exercise directly
    // above (left completely unchanged by this task). Exercised at the
    // C++ level (not via Lua) since bindMissionPhaseMachine()/
    // missionEndDispatch() are new EngineState methods, not new Lua-visible
    // natives - the existing mission_unlock/mission_set_completed native
    // wiring (tested elsewhere, tests/synthetic_luahost_batch5_hud_
    // customization_test.cpp) is unchanged by this task except that it now
    // reaches the SAME shared +0x88 field these methods also write.
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();

        // bindMissionPhaseMachine() via beginMissionPhaseTracking(): CHOSEN
        // default type 0 settles at phase 5 (the types-0/3 self-loop
        // target, Sec21.27a) and arms the marker; the three legacy
        // boolean facades are still flipped true (purely additive).
        es.beginMissionPhaseTracking("m01");
        CHECK(es.missionContext().phase == 5);
        CHECK(es.missionContext().missionBound == true);
        CHECK(es.missionContext().boundMissionName == "m01");
        CHECK(es.missionContext().missionStartMarkerArmed == true);
        CHECK(es.missionContext().activeAndRunning.get() == true);

        // A type-2 mission self-loops to phase 3 instead (Sec21.27a's own
        // case-1 jump table) - the SAME global phase slot is reused for
        // the next bind, matching the real engine's own single-slot
        // mission context.
        es.beginMissionPhaseTracking("m02", /*type=*/2);
        CHECK(es.missionContext().phase == 3);

        // missionEndDispatch() (0x006d9f70, Sec21.27b): wrong-name/unbound
        // refuses; type 1 success fires BOTH the completion writer and
        // mission_unlock's own setter on the SAME +0x88 byte (Sec21.27b's
        // own cross-validation), landing on every legacy per-native facade
        // at once - not three independent, never-cross-wired maps anymore.
        CHECK(es.missionEndDispatch("not_the_bound_mission", true) == false);
        es.beginMissionPhaseTracking("m03", /*type=*/1);
        CHECK(es.missionEndDispatch("m03", true) == true);
        CHECK(es.missionContext().phase == 6); // shared "ending" tail every type falls through to
        CHECK(es.missionCompletionFlagBitSet("m03") == true);
        CHECK(es.missionCompletedValue("m03") == true); // legacy facade, Sec15.10
        CHECK(es.missionUnlockedValue("m03") == true);  // legacy facade, Sec15.9
        CHECK(es.missionContext().missionEndType1UnlockAndCompleteCount == 1);
        // cell_is_mission_complete/mission_is_complete's own shared read
        // (Sec27.2/Sec20.14, missionComplete()) now sees it WITHOUT a test
        // needing to .set() it directly - the real production writer
        // reaches the SAME field (this is exactly the "don't invent a
        // parallel field" fix this task was asked to make).
        CHECK(es.missionComplete().known("m03") && es.missionComplete().get("m03") == true);

        // Type 2 success: fires the completion writer again, counts the
        // still-OPEN 0x006d9470 stub, never invents its body.
        es.beginMissionPhaseTracking("m04", /*type=*/2);
        CHECK(es.missionEndDispatch("m04", true) == true);
        CHECK(es.missionCompletionFlagBitSet("m04") == true);
        CHECK(es.missionContext().missionEndType2CompleteAndOpenStubCount == 1);

        // Types 0/3 success: Sec21.27b's own text gives this branch a
        // +0x1f8 script-hook ATTEMPT, never a completion-flag write - an
        // honest (not invented) negative result, same as a genuine mission
        // failure (any type, successFlag false) never writing anything.
        es.beginMissionPhaseTracking("m05", /*type=*/0);
        CHECK(es.missionEndDispatch("m05", true) == true);
        CHECK(es.missionCompletionFlagBitSet("m05") == false);
        CHECK(es.missionContext().missionEndType0Or3ScriptHookAttemptCount == 1);
        es.beginMissionPhaseTracking("m06", /*type=*/1);
        CHECK(es.missionEndDispatch("m06", false) == true); // failure - type 1's own completion write is gated on successFlag
        CHECK(es.missionCompletionFlagBitSet("m06") == false);

        // recordMissionSetCompleted()/recordMissionUnlock() (the existing
        // mission_set_completed/mission_unlock native wiring, unchanged by
        // this task) ALSO land on the SAME unified +0x88 field now.
        es.recordMissionSetCompleted("m07", /*resolved=*/true);
        CHECK(es.missionCompletionFlagBitSet("m07") == true);
        CHECK(es.missionComplete().known("m07") && es.missionComplete().get("m07") == true);

        // missionSilentClass() (0x006db7fa, Sec21.27b's organic-trigger
        // table): OPEN by default - a documented no-op for a mission never
        // flagged (no real population table for any of the 49 real
        // missions, see that accessor's own doc comment). Once a test
        // flags it, bindMissionPhaseMachine() fires the organic auto-
        // success trigger itself, immediately after settling at phase 3/5.
        es.beginMissionPhaseTracking("m08", /*type=*/0);
        CHECK(es.missionCompletionFlagBitSet("m08") == false); // not silent-class: documented no-op
        es.missionSilentClass().set("m09", true);
        es.beginMissionPhaseTracking("m09", /*type=*/0);
        CHECK(es.missionContext().phase == 6); // the silent trigger drove it straight to "ending"
        CHECK(es.missionContext().missionEndSilentAutoSuccessCount == 1);
        CHECK(es.missionCompletionFlagBitSet("m09") == false); // type 0's own success branch never writes the bit (see m05)
        es.missionSilentClass().set("m10", true);
        es.beginMissionPhaseTracking("m10", /*type=*/1);
        CHECK(es.missionCompletionFlagBitSet("m10") == true); // silent trigger + type 1 DOES write it
    }

    // mission-context-name task (2026-10-08): the real bind call site
    // (bindMissionPhaseMachine(), via beginMissionPhaseTracking()) now also
    // populates MissionContext::currentMissionName/currentMissionEnding -
    // the second-order blocker 9 of the 49 real probed missions hit right
    // after the Sec21.27a phase-8 fix above (first_error was literally
    // "current mission name ... is OPEN", results/probe_missionphase_
    // completion_20261008.md). Before this fix, EVERY call below would have
    // thrown OpenStateError instead of running to completion; this proves
    // both the no-throw fix AND that mission_end_failure's full gate
    // semantics (name match AND the "already tearing down" precondition,
    // NEITHER alone sufficient) hold when driven through the real
    // production bind path, not just by a test calling .set() directly
    // (that case was already covered by the mission_end_failure block near
    // the top of this file).
    {
        Host host(fixtureNames());
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        es.beginMissionPhaseTracking("mA");
        CHECK(es.missionContext().currentMissionName.known());
        CHECK(es.missionContext().currentMissionName.get() == "mA");
        CHECK(es.missionContext().currentMissionEnding.known());
        CHECK(es.missionContext().currentMissionEnding.get() == false); // freshly bound: alive, not tearing down

        // Freshly bound and still alive: real gate fails CLOSED (silent
        // no-op, Sec1.5's own "still fully alive does NOT pass" text) - no
        // throw, no dispatch.
        ok(host, gp, "mission_end_failure('mA', 'reason')");
        CHECK(es.missionContext().missionEndTimer1Armed == false);
        CHECK(es.missionContext().missionEndFailureDispatchCount == 0);

        // Mission starts its own ending/destroy transition (the real
        // liveness-bit flip Sec1.5 requires - no in-scope production writer
        // sets this automatically, per that field's own doc comment, so a
        // test drives it directly, same as the manual-.set() block above).
        // Name now matches the REAL bound mission (set by the bind call
        // above, not by this test) -> full pass.
        es.missionContext().currentMissionEnding.set(true);
        ok(host, gp, "mission_end_failure('mA', 'reason')");
        CHECK(es.missionContext().missionEndTimer1Armed == true);
        CHECK(es.missionContext().missionEndFailureDispatchCount == 1);

        // Re-bind: the real engine's single global slot is overwritten by
        // the next bind (Sec21.27a: "set non-null by exactly one
        // function... at the exact instant phase goes to 8"). The OLD
        // mission's own name no longer matches -> gate fails on identity
        // alone, even though currentMissionEnding was left true from above.
        es.beginMissionPhaseTracking("mB");
        CHECK(es.missionContext().currentMissionName.get() == "mB");
        CHECK(es.missionContext().currentMissionEnding.get() == false); // reset to "alive" on the new bind
        ok(host, gp, "mission_end_failure('mA', 'reason')"); // stale name: no longer the bound mission
        CHECK(es.missionContext().missionEndFailureDispatchCount == 1); // unchanged - no new dispatch
    }

    // playerClassChainResolves(name)'s own CHOSEN default heuristic
    // (engine_state.h doc comment, player-class-chain-gate fix): direct
    // unit coverage of the classifier itself, independent of any one
    // consuming native, calibrated against the exact names this project's
    // own mission-drive probe hit this gate for (results/verify_probe_
    // blockers_ranked_20261008_post_missioncontextname).
    {
        Host host(fixtureNames());
        EngineState& es = host.engineState();

        // Real probe names, character side (all 7 of them) - default TRUE.
        for (const char* name : {"NPC Pierce", "npc_zimos", "npc_pierce", "char_stag_main_stage 005",
                                  "char_stag_grunt_dead 001", "Pierce", "Angel"}) {
            CHECK(es.playerClassChainResolves(name) == true);
        }
        // Real probe names, vehicle side (both of them) - default FALSE.
        CHECK(es.playerClassChainResolves("debris_vehicle 001") == false);
        CHECK(es.playerClassChainResolves("Veh CP SJ Spotlight Heli") == false);
        // Additional calibration from this project's own release_to_world-
        // adjacent vehicle names (Sec4.4, a DIFFERENT real mechanism, same
        // naming convention) - default FALSE.
        CHECK(es.playerClassChainResolves("Starting_Vehicles") == false);
        CHECK(es.playerClassChainResolves("Script_Vehicle 001") == false);
        // Exact token "veh" (no separator follows - nothing TO follow) -
        // still classified as vehicle-shaped (the size==3 case).
        CHECK(es.playerClassChainResolves("veh") == false);
        // False-positive guard: a name that merely STARTS WITH the letters
        // "veh" without the dedicated separator this heuristic requires
        // (i.e. not the short "Veh <rest>" token convention) is NOT
        // misclassified as a vehicle - it keeps the character default.
        CHECK(es.playerClassChainResolves("Vehemently") == true);

        // An explicit test .set() always wins over the default, in EITHER
        // direction - the default only applies when truly unset.
        es.playerClassChainResolves().set("debris_vehicle 001", true); // override a vehicle-shaped name to true
        CHECK(es.playerClassChainResolves("debris_vehicle 001") == true);
        es.playerClassChainResolves().set("Pierce", false); // override a character-shaped name to false
        CHECK(es.playerClassChainResolves("Pierce") == false);
        // .forget() reverts to the default heuristic, not to some other
        // fixed state.
        es.playerClassChainResolves().forget("debris_vehicle 001");
        CHECK(es.playerClassChainResolves("debris_vehicle 001") == false);
        es.playerClassChainResolves().forget("Pierce");
        CHECK(es.playerClassChainResolves("Pierce") == true);
    }

    // Sec21.27c upgrade (this task, 2026-10-09): "0x006da400 opened in
    // full" - the real fail/success presentation split. Three things this
    // task's own brief calls out as THE deliverable: (a) an ordinary
    // mission's failure call produces failure-shaped state (fail-text
    // global write, debounce timer 1 at 2000ms/4000ms-on-overlap, HUD
    // teardown-all when the gate bit reads true); (b) a type-2 mission's
    // failure call is silently coerced into hardcoded success instead,
    // never entering dispatchMissionEndPresentation() at all; (c) an
    // ordinary success call does NOT trigger HUD teardown even when the
    // SAME gate bit a failure call would read is true - the confirmed
    // asymmetry - while arming its own separate timer 2 and writing the
    // separate success-announcement global.
    {
        // mission_end_success is not in fixtureNames() above (that list is
        // this file's own pre-existing 12-name cluster, unchanged) - a
        // separate local list adds it for this block's own Host only, same
        // "gameplay" tag the real tagged-registration file gives it
        // (src/lua_spec_confirmed_stubs_b29_sec6_notoriety_mission.cpp).
        std::vector<RegisteredName> names = fixtureNames();
        names.push_back({"mission_end_success", "gameplay"});
        Host host(names);
        lua_State* gp = host.gameplayState();
        EngineState& es = host.engineState();

        // (a) Ordinary (type 0) mission, failure call, gate bit true.
        es.beginMissionPhaseTracking("mFailOrdinary", /*type=*/0);
        es.missionContext().currentMissionEnding.set(true); // real liveness-transition bit, no in-scope auto-writer (same as the block near the top of this file)
        es.missionContext().hudTeardownGateBit.set(true);    // CONFIRMED mechanism/OPEN value flags byte 0x014c848c bit 0x8 - a test sets it
        ok(host, gp, "mission_end_failure('mFailOrdinary', 'you died')");
        CHECK(es.missionContext().missionEndFailureDispatchCount == 1);
        CHECK(es.missionContext().missionEndFailureCoercedToSuccessCount == 0); // NOT type 2: no coercion
        CHECK(es.missionContext().lastFailReasonText == "you died");            // 0x014c7a08
        CHECK(es.missionContext().missionEndTimer1Armed == true);
        CHECK(es.missionContext().missionEndTimer1LastArmedDurationMs == 2000); // first arm: 2000ms
        CHECK(es.missionContext().lastDispatchTornDownHud == true);            // gate bit true -> teardown-all fired
        CHECK(es.missionContext().hudTeardownCount == 1);
        CHECK(es.missionContext().missionEndTimer2Armed == false); // failure never touches success's own timer

        // A second mission-end while timer 1 is still armed from the call
        // above doubles to 4000ms (Sec21.27c's own "4000ms if a second
        // mission-end fires while the first is cooling down").
        ok(host, gp, "mission_end_failure('mFailOrdinary', 'again')");
        CHECK(es.missionContext().missionEndFailureDispatchCount == 2);
        CHECK(es.missionContext().missionEndTimer1LastArmedDurationMs == 4000);
        CHECK(es.missionContext().lastFailReasonText == "again");
        CHECK(es.missionContext().hudTeardownCount == 2); // gate bit still true: tears down again

        // (b) Type-2 mission: mission_end_failure is silently coerced into
        // hardcoded success - dispatchMissionEndPresentation() is never
        // entered, so NONE of its effects (fail-text write, timer 1/2 arm,
        // HUD teardown) fire, even though the gate bit is still true.
        es.beginMissionPhaseTracking("mFailType2", /*type=*/2);
        CHECK(es.missionContext().phase == 3); // type-2 self-loop target, Sec21.27a
        es.missionContext().currentMissionEnding.set(true); // same real liveness-transition bit the gate requires, even on the coercion path
        ok(host, gp, "mission_end_failure('mFailType2', 'should be swallowed')");
        CHECK(es.missionContext().missionEndFailureCoercedToSuccessCount == 1);
        CHECK(es.missionContext().missionEndFailureDispatchCount == 2); // unchanged - non-coerced branch never entered
        CHECK(es.missionContext().lastFailReasonText == "again");       // unchanged - no new write
        CHECK(es.missionContext().hudTeardownCount == 2);               // unchanged - no new teardown
        // Coercion routes through the SAME missionEndDispatch()/0x006d9f70
        // super-dispatcher Sec21.27b already models, with successFlag=true.
        CHECK(es.missionContext().missionEndDispatchCount == 1);
        CHECK(es.missionContext().lastMissionEndDispatchSuccessFlag == true);
        CHECK(es.missionContext().phase == 6); // shared "ending" tail every missionEndDispatch() type falls through to
        CHECK(es.missionCompletionFlagBitSet("mFailType2") == true); // type-2 success branch's own completion write (Sec21.27b, m04 case above)
        CHECK(es.missionContext().missionEndType2CompleteAndOpenStubCount == 1);

        // (c) Ordinary (type 0) mission, SUCCESS call, SAME gate bit still
        // true. Confirmed asymmetry: success never reaches the HUD-teardown
        // primitive regardless of that bit, unlike failure's (a) above.
        es.beginMissionPhaseTracking("mSuccessOrdinary", /*type=*/0);
        es.missionContext().currentMissionEnding.set(true);
        CHECK(es.missionContext().hudTeardownGateBit.get() == true); // still true from above - not reset by the rebind
        ok(host, gp, "mission_end_success('mSuccessOrdinary', 'You win')");
        CHECK(es.missionContext().missionEndSuccessDispatchCount == 1);
        CHECK(es.missionContext().lastSuccessAnnouncementText == "You win"); // 0x014c846c
        CHECK(es.missionContext().missionEndTimer2Armed == true);           // success's own separate timer
        CHECK(es.missionContext().missionEndTimer1Armed == true);           // unchanged leftover from (a) - success never touches timer 1
        CHECK(es.missionContext().lastDispatchTornDownHud == false);        // THE asymmetry: never true for success
        CHECK(es.missionContext().hudTeardownCount == 2);                   // unchanged - no new teardown despite the gate bit

        // Direct contrast within the same gate-bit-true state: a fresh
        // ordinary failure call right after the success call above DOES
        // tear down, proving the asymmetry is about which branch ran, not
        // about the gate bit's value.
        es.beginMissionPhaseTracking("mFailAfterSuccess", /*type=*/0);
        es.missionContext().currentMissionEnding.set(true);
        ok(host, gp, "mission_end_failure('mFailAfterSuccess', 'reason')");
        CHECK(es.missionContext().lastDispatchTornDownHud == true);
        CHECK(es.missionContext().hudTeardownCount == 3);
    }

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_trigger_mission_cluster_test: all checks passed\n";
    return 0;
}
