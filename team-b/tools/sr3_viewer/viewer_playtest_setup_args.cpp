// runPlaytest() split: command-line parsing and the banner (setupArgs).
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

int setupArgs(PlaytestSession& sess, int argc, char** argv) {
    auto& cityArchivePaths = sess.cityArchivePaths;
    auto& headless = sess.headless;
    auto& noInput = sess.noInput;
    auto& maxFrames = sess.maxFrames;
    auto& capturePath = sess.capturePath;
    auto& captureFrame = sess.captureFrame;
    auto& captureFrames = sess.captureFrames;
    auto& winWidth = sess.winWidth;
    auto& winHeight = sess.winHeight;
    auto& spawnX = sess.spawnX;
    auto& spawnZ = sess.spawnZ;
    auto& spawnYOverrideGiven = sess.spawnYOverrideGiven;
    auto& spawnYOverride = sess.spawnYOverride;
    auto& autoObjective = sess.autoObjective;
    auto& autoCombat = sess.autoCombat;
    auto& autoObjectiveFrames = sess.autoObjectiveFrames;
    auto& autoSeatMissionVehicle = sess.autoSeatMissionVehicle;
    auto& autoSkipCutscenes = sess.autoSkipCutscenes;
    auto& farZ = sess.farZ;
    auto& animArchiveOverride = sess.animArchiveOverride;
    auto& mouseSensitivity = sess.mouseSensitivity;
    auto& scriptForwardFrames = sess.scriptForwardFrames;
    auto& scriptTurnFrames = sess.scriptTurnFrames;
    auto& hudEnabled = sess.hudEnabled;
    auto& hudRegistrationListPath = sess.hudRegistrationListPath;
    auto& hudFontArchivePath = sess.hudFontArchivePath;
    auto& missionEnabled = sess.missionEnabled;
    auto& missionStem = sess.missionStem;
    auto& missionRegistrationListPath = sess.missionRegistrationListPath;
    auto& missionPackageTsvPathOverride = sess.missionPackageTsvPathOverride;
    auto& missionTimeoutSeconds = sess.missionTimeoutSeconds;
    auto& vehicleEnabled = sess.vehicleEnabled;
    auto& vehicleArchiveOverride = sess.vehicleArchiveOverride;
    auto& vehicleWheelsArchiveOverride = sess.vehicleWheelsArchiveOverride;
    auto& interfaceArchiveOverride = sess.interfaceArchiveOverride;
    auto& vehicleCmeshName = sess.vehicleCmeshName;
    auto& vehicleInteractionNameOverride = sess.vehicleInteractionNameOverride;
    auto& vehicleSpawnX = sess.vehicleSpawnX;
    auto& vehicleSpawnZ = sess.vehicleSpawnZ;
    auto& scriptVehicle = sess.scriptVehicle;
    auto& scriptVehicleDriveFramesOverride = sess.scriptVehicleDriveFramesOverride;
    auto& noCollision = sess.noCollision;
    auto& noNpcAi = sess.noNpcAi;
    auto& npcAiNoFire = sess.npcAiNoFire;
    auto& debugNotoriety = sess.debugNotoriety;
    auto& scriptRoadblock = sess.scriptRoadblock;
    auto& scriptFire = sess.scriptFire;
    auto& scriptHudPromptResponse = sess.scriptHudPromptResponse;
    auto& autoAnswerHudPrompts = sess.autoAnswerHudPrompts;
    auto& hudPromptAutoAnswerDelayFrames = sess.hudPromptAutoAnswerDelayFrames;
    auto& noAudio = sess.noAudio;
    auto& sfxArchiveOverride = sess.sfxArchiveOverride;
    sess.charactersPath = argv[2];
    sess.miscTablesPath = argv[3];
    int argi = 4;
    while (argi < argc && std::strncmp(argv[argi], "--", 2) != 0) cityArchivePaths.push_back(argv[argi++]);
    if (cityArchivePaths.empty()) {
        std::fprintf(stderr, "need at least one city archive (sr3_city_0.vpp_pc)\n");
        return 2;
    }
    // THIS TASK (fix/m05-zscene-m13-systemlib, 2026-10-12): scan the cache
    // directory (same directory as the first archive the caller named) for
    // cutscene_tables.vpp_pc/patch_compressed.vpp_pc/misc_tables.vpp_pc if
    // not already named explicitly, so --mission's own zscene-table install
    // and system_lib.lua/game_lib.lua preload below can find them without
    // the caller having to know or guess the right archive - see
    // expandArchivePathsWithCacheDirScan()'s own doc comment above. Kept as
    // a SEPARATE list from `cityArchivePaths` itself (used below for the
    // CityArchive/CityCell world-streaming population): adding
    // patch_compressed.vpp_pc there would additionally feed it into that
    // unrelated container-open code path, which this task's brief flags as
    // having its own separate, pre-existing "bad allocation" warning when
    // handed that specific archive - out of scope here, and this mission/
    // Lua-preload lookup does not need the world-streaming side effect.
    sess.luaPreloadArchivePaths = expandArchivePathsWithCacheDirScan(cityArchivePaths);

    bool framesExplicit = false;
    // RELOCATED (this task, 2026-10-08, playtest-visuals follow-up): the previous default
    // (-960,-700) - the city_corner golden's own corner - resolves REAL ground (tile 0916's
    // own `~s` fallback, sr3_city~s0916.czn_pc, 1 range, 237 triangles - see tests/golden/
    // walk_demo_v2/baseline_stdout.txt), but that ground is ELEVATED: measured Y bounds
    // [18.15, 31.20] here, a rooftop/ledge-shaped `~s` zone, not street pavement - confirmed
    // directly (not assumed) via this exact command's own GROUND-Y MEASUREMENT block below,
    // the project's real loadCityGroundTile()/findTileZoneEntryName() path, no stand-in. Per
    // this task's own brief, every reachable tile of sr3_city_0.vpp_pc's resident ~29-tile
    // neighbourhood (cols 7-10, rows 9-19 - the only region this archive covers, surveyed via
    // `sr3_viewer city <archive> --ground --eye <tileCentreX> 60 <tileCentreZ> --no-input
    // --frames 1`, one real ground-tile load per tile, same reconcile()/loadCityGroundTile()
    // call this command's own GROUND-Y MEASUREMENT below uses) was measured this same way.
    // Most resolve elevated (range 2.9m to 41.4m); tile 0915 (one tile north of the old
    // corner, same col 9) measured Y=[-0.21, 0.21] - genuinely flat, street-level, and the
    // ONLY tile the streaming window loads at its own centre (confirmed: the survey's own
    // [ground] stdout showed exactly one LOAD line there, no neighbour pulled in) - tile 0914
    // (col 9, one row further) measured Y=[-0.50, 0.50], same story, a second good option.
    // CHOSEN: tile 0915's own centre (cityTileCentre(9,15) = (-1280,-560), spec-world-
    // streaming.md Sec10.6(g)'s own formula) as the new default spawn - real street-level
    // ground, not a rooftop, still in the same resident neighbourhood as before. --spawn
    // overrides this as always.
    //
    // CORRECTION (orchestrator follow-up, 2026-10-08, after a reported "void below the
    // character's feet" at this spawn was investigated): the "Y=[-0.21,0.21], flat street-
    // level" measurement above is accurate about what loadCityGroundTile() returned, but
    // NOT trustworthy evidence this location is real street-level ground - tile 0915 (and
    // its neighbours 0914/0715/0716) was separately found to decode its ground mesh at
    // world-origin-centred coordinates, >1000m from its own nominal tile centre, unlike
    // every other surveyed tile (0916/0818/0912/1015/1016), which all resolve within a
    // normal ~30-150m "slop" of their own nominal centre. HYPOTHESIS, not yet confirmed:
    // this may be the same still-OPEN spec-terrain-format.md `.czn_pc` per-region-origin
    // gap - relayed to Team A as a spec-sync question. Independently verified via direct
    // on/off toggling of ground-tile vs. prop rendering at this spawn: the ground tile
    // contributes ZERO visible pixels here - the entire visible street (walls, doors,
    // sidewalk, traffic light) comes from props (.clmesh_pc), a separate, unaffected code
    // path. So "not a rooftop" still holds (confirmed via props, human-scale street
    // geometry, correctly placed), but it is NOT confirmed "at" the coordinates this
    // comment block claims, and a real ground mesh is NOT known to be present here - the
    // near-field void below the character is real and pre-existing (reproducible with the
    // OLD dark clear colour too, just camouflaged there), not introduced by this change.
    // Full investigation: results/verify_playtest_visuals_20261008/gap_investigation/.
    // --auto-objective[=N] (feature/auto-objective-harness, 2026-10-12):
    // generalizes the manually-proven m07 spawn-at-barge technique (results/
    // verify_m07_spawn_at_barge_20261012/: spawn the scripted player directly
    // at a get_dist target's own real, zone-data-resolved position) into an
    // automatic in-run teleport. After N consecutive get_dist(a,b) calls on
    // the SAME unordered name pair (EngineState::recordGetDistCall(), set by
    // every stub_get_dist call regardless of this flag), with --mission
    // active, teleports the player (via the SAME pm.warp() already used for
    // --spawn placement) to whichever of a/b is NOT "#PLAYER1#", using its
    // resolved worldPosition - the SAME tryResolveWorldPositionFromZoneData()
    // position stub_get_dist's own real-fallback chain already uses, never a
    // fabricated one. Does nothing (logs why) if no get_dist stall has been
    // recorded yet, if neither name is the player sentinel, or if the
    // target's position never resolved known() - no invented fallback
    // position, per this project's standing no-invented-fixes rule. Fires
    // AT MOST ONCE per run (a one-shot stand-in, not a repeating homing
    // assist). Default N=80: CHOSEN - roughly 2.7s at this harness's fixed
    // 1/30s --no-input step, long enough that a single transient get_dist
    // call (most calls are NOT part of a wait loop) won't trip it, short
    // enough to resolve well inside the 300-frame runs this project's own
    // mission-drive verification tasks already use (m07's own confirmed
    // stall held for the full 300 frames; m09/m13 unconfirmed but presumed
    // similar order of magnitude).
    // --auto-combat (feature/harness-combat): opt-in; replaces the --auto-objective instant character_kill on hostile
    // on_death targets with teleport-near -> let the NPC AI engage -> player fires through the real hitscan +
    // damage-model path -> character_kill only after a no-progress timeout. Needs --auto-objective.
    // --auto-seat-mission-vehicle (THIS TASK, auto-objective-trigger-vehicle-
    // seat, 2026-10-12): m05's own heli stall (results/verify_m05_direct_
    // callback_stand_in_20261012.md - m05_jump_local's last line,
    // `vehicle_exit(LOCAL_PLAYER)`, never returns because this synthetic
    // mission-drive run never put the player in a vehicle, so stub_vehicle_
    // exit_do's own seated test, CharacterState::stateEnum == 3, never
    // passes) has NO generic detection this host can run - unlike the
    // get_dist/on_trigger stalls above, nothing observable distinguishes "a
    // script expects the player already seated in THIS vehicle" from any
    // other yield. CHOSEN, scoped narrowly to m05 by name, exactly as this
    // task's own brief allows: when this run drives --mission=m05, calls the
    // REAL vehicle_enter_do Lua native (the SAME registered global
    // stub_vehicle_enter_do - lua_spec_confirmed_stubs_b15_wave2_core_
    // dispatchers.cpp - a mission script's own vehicle_enter_teleport wrapper
    // calls) with LOCAL_PLAYER and "m05_heli_pierce" (the mission's own real
    // vehicle name, confirmed via results/baseline_2026-10-11_tick3000's own
    // verdict dump), instant=true - the exact code path that already writes
    // CharacterState::stateEnum = 3 ("seated") synchronously on an accepted
    // instant placement (see that stub's own 2026-10-09 FIXED note). No new
    // state-writing mechanism: this just drives the existing native through
    // the host exactly as a real script would, once, before the mission's
    // own scheduler pass gets a chance to reach vehicle_exit.
    // --auto-skip-cutscenes (fix/auto-objective-driving-2): CHOSEN harness opt-in that sets the REAL game option
    // `skip_all_cutscenes` (byte 0x0153b556, spec-lua-api-behaviour.md Sec14.23/Sec26.25) via the existing
    // EngineState::zsceneSkipAllCutscenes() before the mission starts. The engine's own skip branches
    // (zscene_prep/zscene_is_loaded/cutscene_play) then run unchanged; this flag only picks the option value.
    // --hud <registration_list.txt> <misc.vpp_pc>: OFF by default (opt-in
    // stretch goal, see this function's own "UPDATED 2026-10-08" doc
    // comment above for the full honest-caveat citation).
    // --mission=<stem> (Phase 1 mission-host debug flag, THIS TASK, 2026-10-09):
    // OFF unless given - see this function's own doc comment (above) for the
    // full rationale. kMissionDefaultTimeoutSeconds is this task's own CHOSEN
    // wall-clock safety default (no existing timeout convention found
    // elsewhere in this project to reuse instead - see that doc comment).
    constexpr double kMissionDefaultTimeoutSeconds = 30.0;
    sess.missionTimeoutSeconds = kMissionDefaultTimeoutSeconds;
    // ---- vehicle entry/driving (THIS TASK, 2026-10-08) - ON by default,
    // --no-vehicle opts out. Archive/name defaults mirror this function's
    // own --anim-archive convention just below: derived from
    // <characters.vpp_pc>'s own directory rather than hardcoded, since that
    // directory already holds every other real cache archive this command
    // reads (dlc1.vpp_pc / vehicles_preload.vpp_pc confirmed present
    // alongside characters.vpp_pc/misc_tables.vpp_pc/sr3_city_0.vpp_pc in
    // the real game cache this project already uses elsewhere). ----
    // --interface-archive (THIS TASK, pause-menu-open binding): where the real
    // pause_menu_common.vint_doc (interface.vpp_pc::pause_menu_common.str2_pc, confirmed present
    // via vpp_dump against the real packfiles\pc\cache - exact byte-for-byte names this spec's
    // own "push_screen('pause_menu_common')" grep-for-call-sites text already cites) is read from
    // for the pause overlay's own best-effort real-document load - see the main loop's own
    // "PAUSE MENU" block. CHOSEN default: derived from <characters.vpp_pc>'s own directory, same
    // convention --vehicle-archive's own default already uses, below.
    // --vehicle-interaction-name (THIS TASK, 2026-10-09, spec-render-pipeline.md Sec23.14):
    // selects which real vehicle_interaction_info.xtbl row's DRIVER seat "Hide Occupant"
    // flag governs this run's in-vehicle character visibility (see the "VEHICLE OCCUPANT
    // VISIBILITY" block below, right before the main loop, for the full account). CHOSEN,
    // NOT auto-resolved from the loaded .ccar_pc: Sec23.14's own row Name
    // (e.g. "high car 4dr", "Tank NatGrd") is a vehicle INTERACTION CATEGORY, cross-
    // referenced from a per-vehicle `<name>_veh.xtbl`'s own Vehicle_Interaction_Info field
    // (include/sr3vehicleinfo/vehicle_entry.h's VehicleEntry::vehicleInteractionInfo, +0xA38)
    // - and this project parses no such file for car_4dr_genki_0.ccar_pc (a DLC dlc1.vpp_pc
    // mesh with no matching entry in misc_tables.vpp_pc's own vehicles.xtbl manifest, checked
    // empirically this task), so automatically deriving the category for an arbitrary loaded
    // mesh is a separate, unbuilt cross-reference pipeline, out of this task's "consume the
    // already-parsed flag" scope. Left empty, this run's own hide decision defaults to
    // "false" (shown) - the REAL, empirically-verified behaviour for every ordinary vehicle
    // (129/131 real rows, Sec23.14) - and the default demo vehicle is an ordinary 4-door
    // sedan, not a tank, so that default is correct for it specifically, not just a vacuous
    // fallback. Pass e.g. --vehicle-interaction-name "Tank NatGrd" (a real row, empirically
    // confirmed this task against the shipped misc_tables.vpp_pc) to exercise/demonstrate the
    // tank-hide path instead.
    bool vehicleSpawnExplicit = false;
    // --script-vehicle-drive-frames N (THIS TASK, world-streaming-reconcile, 2026-10-08):
    // overrides the scripted drive phase's own duration (kScriptVehicleDriveFrames below,
    // default unchanged at 120 = real_playtest_vehicle_smoke_test.cpp's own baseline) so a
    // long deterministic drive - far enough to cross several level-4 fine cells/tiles
    // (spec-world-streaming.md Sec10.3/Sec10.4) - can be scripted for streaming-proof
    // captures (results/) without touching that existing smoke test's own behaviour.
    // --no-collision (THIS TASK): leaves pm.collisionHook UNSET (PlayerMovement's own
    // "if (collisionHook)" gate, src/player_locomotion.cpp line ~155 - already an existing,
    // intentional no-op path for a caller that never installs one) instead of installing
    // the real ClmeshTriangleCollisionWorld hook below. CHOSEN test-only accommodation, OFF
    // by default (every existing golden/smoke test keeps real collision, byte-for-byte
    // unaffected): a scripted long-distance WALK proof needs to cross tile boundaries far
    // from spawn, and the real city's own building collision geometry (confirmed empirically
    // this task: the default spawn's own +Z street is blocked by a wall ~36m out) makes that
    // impractical to script deterministically without either a hand-picked clear corridor
    // (fragile against future spawn-point changes) or disabling collision for this one
    // diagnostic path. This is a streaming/focus-tracking test concern, not a collision
    // regression test - real_playtest_smoke_test.cpp's own default run keeps collision ON
    // and is untouched by this flag.
    // --no-npc-ai / --npc-ai-no-fire / --debug-notoriety=N (NPC AI loop task): turn the per-NPC AI loop off, keep it but
    // stop NPCs firing at the player, or write the police notoriety track for a live check (a labelled DEBUG input: no
    // crime source is wired, so notoriety otherwise stays at whatever the scripts set).
    // --script-roadblock (THIS TASK, playtest-nearfield-traffic-roadblock):
    // a deterministic arm-then-disarm 'R' press pair under --no-input, same
    // scripted-fallback idiom --script-vehicle already establishes for 'E' -
    // see kScriptRoadblockArmFrame/kScriptRoadblockDisarmFrame below (near
    // the main loop) for the exact frame schedule.
    // --script-fire (THIS TASK, playtest-weapon-fire-wiring): a single
    // deterministic fire-key press under --no-input, same scripted-fallback
    // idiom --script-vehicle/--script-roadblock already establish for 'E'/
    // 'R' - see kScriptFireFrame below (near the main loop) for the exact
    // frame.
    // --script-hud-prompt-response (THIS TASK, m05-jump-hud-prompt-test):
    // a genuine player-INPUT wait (e.g. m05.lua:1141's `while not M05_jumped
    // do thread_yield() end`, unblocked only by the registered callback a
    // live `hud_prompt(target, promptId, callbackName)` call names, Sec13.3)
    // has no existing scripted-key idiom to reuse - --script-vehicle/
    // --script-roadblock both script a KEY PRESS, not a response to a
    // specific prompt's own registered callback. Reuses, instead, the ONE
    // real "fire a registered callback" mechanism this project already has
    // confirmed end-to-end (spec-lua-api-behaviour.md Sec1.14's "GAP
    // CLOSED" finding: the real dispatcher spawns a new Lua coroutine for
    // the resolved callback name via BareThreadTable, never a synchronous
    // call) - the EXACT SAME allocate()-then-let-the-next-scheduler-pass-
    // resume-it idiom tools/lua_host_run.cpp's own --probe-autopilot
    // dispatch block (see that file's 2026-10-09 doc comment) already
    // established for the on_trigger/on_death/on_vehicle_enter callback-
    // registration family - applied here to a DIFFERENT real native
    // (hud_prompt) that stores a callback name the identical way, not a
    // second/parallel mechanism. See the main loop's own
    // "SCRIPT-HUD-PROMPT-RESPONSE" block for the exact per-tick trigger.
    // --auto-answer-hud-prompts [N] (THIS TASK, hud-prompt-autoanswer-harness):
    // generalizes --script-hud-prompt-response above for scripted/automated
    // --no-input runs (mission-drive verification harnesses) that need to get
    // PAST a real player-input-gated HUD-prompt wait (e.g. m05.lua:1141's
    // M05_JUMP) without a real button press, so the run can surface the
    // mission's own NEXT real stall point instead of tick-budget-stalling on
    // the prompt forever. Reuses the exact same dispatch mechanism (spawn a
    // coroutine for the prompt's own registered callbackName via
    // BareThreadTable::allocate(), the same idiom Sec1.14's confirmed
    // on_trigger/on_death/etc dispatch and --script-hud-prompt-response both
    // already use - no parallel path invented), generically for ANY currently
    // showing hud_prompt, not hardcoded to M05_JUMP. The only real difference
    // from --script-hud-prompt-response is the per-request delay below
    // (hudPromptAutoAnswerDelayFrames): --script-hud-prompt-response answers
    // on the very tick a request first appears (delay 0); this flag instead
    // waits N frames of that SAME request showing first, matching a real
    // player's own reaction time rather than an instant answer. If both flags
    // are given, --auto-answer-hud-prompts's own delay wins (scriptHudPromptResponse
    // stays true either way - same bool drives both).
    // --no-audio (THIS TASK, audio-playback): opts OUT of opening a real
    // speaker device even in an otherwise-interactive run. Independent of
    // this flag, --headless and --no-input NEVER open a device regardless -
    // see the "AUDIO OUTPUT DEVICE" init block below, right before the main
    // loop, for the exact gating and why (ctest's own smoke-test runs use
    // both flags and must stay genuinely silent in CI, never just quiet by
    // convention).
    // --sfx-archive <path>: overrides which real Wwise-bank-bearing
    // `.vpp_pc` the footstep-cadence demo clip is resolved from (CHOSEN
    // default: sounds.vpp_pc, same "resolved relative to <characters.vpp_pc>'s
    // own directory" convention --vehicle-archive's own default uses - see
    // the "AUDIO OUTPUT DEVICE" init block below, right before the main
    // loop, for exactly why sounds.vpp_pc and not cutscene_sounds.vpp_pc,
    // the first CHOSEN default this task tried: that archive is pure
    // cutscene dialogue, and dialogue is CONFIRMED (spec-audio-runtime.md
    // Sec9.8) to never resolve through this embedded-descriptor mechanism).
    for (; argi < argc; ++argi) {
        const std::string arg = argv[argi];
        if (arg == "--headless") headless = true;
        else if (arg == "--no-audio") noAudio = true;
        else if (arg == "--sfx-archive" && argi + 1 < argc) sfxArchiveOverride = argv[++argi];
        else if (arg == "--hud" && argi + 2 < argc) {
            hudEnabled = true;
            hudRegistrationListPath = argv[++argi];
            hudFontArchivePath = argv[++argi];
        }
        else if (arg == "--visible") { /* no-op: this command's own default is already visible=true, unlike
                                          every other command in this file - accepted anyway so a habit
                                          typed from those commands' own docs does not hard-error here. */ }
        else if (arg == "--no-input") noInput = true;
        else if (arg.rfind("--weather=", 0) == 0) sess.kWeatherStageForPedDensity = arg.substr(10);
        else if (arg == "--frames" && argi + 1 < argc) { maxFrames = std::atoi(argv[++argi]); framesExplicit = true; }
        else if (arg == "--capture" && argi + 1 < argc) capturePath = argv[++argi];
        else if (arg == "--capture-frame" && argi + 1 < argc) captureFrame = std::atoi(argv[++argi]);
        else if (arg == "--capture-frames" && argi + 1 < argc) {
            const std::string csv = argv[++argi];
            size_t pos = 0;
            while (pos < csv.size()) {
                size_t comma = csv.find(',', pos);
                const std::string tok = csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if (!tok.empty()) captureFrames.push_back(std::atoi(tok.c_str()));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        } else if (arg == "--size" && argi + 2 < argc) {
            winWidth = static_cast<uint32_t>(std::atoi(argv[++argi]));
            winHeight = static_cast<uint32_t>(std::atoi(argv[++argi]));
        } else if (arg == "--spawn" && argi + 2 < argc) {
            spawnX = static_cast<float>(std::atof(argv[++argi]));
            spawnZ = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--load-save" && argi + 1 < argc) {
            sess.loadSavePath = argv[++argi];  // spec-save-format.md A.1 (viewer_playtest_saveflow.cpp)
        } else if (arg == "--save-at-exit" && argi + 1 < argc) {
            sess.saveAtExitPath = argv[++argi];  // spec-save-format.md A.2
        } else if (arg == "--spawn-y" && argi + 1 < argc) {
            // TEST-ONLY override (this task, screen-fade/m09-progress verification): the
            // real GROUND-Y MEASUREMENT/heightmap-follow logic below has no data at all
            // over water/dock areas (confirmed live: 0 ground tiles resolved at m09's own
            // on_boat_trigger position), so there is no real way to stand a scripted
            // --no-input player inside an elevated real in-world trigger box there without
            // this. Bypasses both the measured flat-plane fallback AND the per-frame
            // heightmap follower's own fallback target (same value fed to both
            // GroundFollower instances below) - CHOSEN test scaffolding, not game logic;
            // does nothing unless passed.
            spawnYOverrideGiven = true;
            spawnYOverride = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--far" && argi + 1 < argc) {
            farZ = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--anim-archive" && argi + 1 < argc) {
            animArchiveOverride = argv[++argi];
        } else if (arg == "--mouse-sensitivity" && argi + 1 < argc) {
            mouseSensitivity = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--script-forward-frames" && argi + 1 < argc) {
            scriptForwardFrames = std::atoi(argv[++argi]);
        } else if (arg == "--script-turn-frames" && argi + 1 < argc) {
            scriptTurnFrames = std::atoi(argv[++argi]);
        } else if (arg == "--no-ambient-models") {
            sess.noAmbientModels = true;  // keep the pre-variety placeholder models (before/after measurement)
        } else if (arg == "--no-vehicle") {
            vehicleEnabled = false;
        } else if (arg == "--vehicle-archive" && argi + 1 < argc) {
            vehicleArchiveOverride = argv[++argi];
        } else if (arg == "--vehicle-name" && argi + 1 < argc) {
            vehicleCmeshName = argv[++argi];
        } else if (arg == "--vehicle-interaction-name" && argi + 1 < argc) {
            vehicleInteractionNameOverride = argv[++argi];
        } else if (arg == "--wheels-archive" && argi + 1 < argc) {
            vehicleWheelsArchiveOverride = argv[++argi];
        } else if (arg == "--interface-archive" && argi + 1 < argc) {
            interfaceArchiveOverride = argv[++argi];
        } else if (arg == "--vehicle-spawn" && argi + 2 < argc) {
            vehicleSpawnX = static_cast<float>(std::atof(argv[++argi]));
            vehicleSpawnZ = static_cast<float>(std::atof(argv[++argi]));
            vehicleSpawnExplicit = true;
        } else if (arg == "--script-vehicle") {
            scriptVehicle = true;
        } else if (arg.rfind("--script-enter-ambient-at=", 0) == 0) {
            sess.scriptEnterAmbientAt = std::atoi(arg.c_str() + 26);
        } else if (arg.rfind("--script-enter-ambient-count=", 0) == 0) {
            sess.scriptEnterAmbientCount = std::atoi(arg.c_str() + 29);
        } else if (arg == "--script-enter-ambient-reenter") {
            sess.scriptEnterAmbientReenter = true;
        } else if (arg.rfind("--script-enter-ambient-drive-frames=", 0) == 0) {
            sess.scriptEnterAmbientDriveFrames = std::atoi(arg.c_str() + 36);
        } else if (arg == "--script-vehicle-drive-frames" && argi + 1 < argc) {
            scriptVehicleDriveFramesOverride = std::atoi(argv[++argi]);
        } else if (arg == "--no-npc-ai") {
            noNpcAi = true;
        } else if (arg == "--npc-ai-no-fire") {
            npcAiNoFire = true;
        } else if (arg.rfind("--debug-notoriety-points=", 0) == 0) {
            // T5 DEBUG input: one injected police crime of N points at frame 1 (goes through the Sec19.8 recipe, so
            // the level/fraction/decay are the real rules; unlike --debug-notoriety=N it does not override them).
            sess.debugNotorietyPoints = std::atoi(arg.substr(25).c_str());  // strlen("--debug-notoriety-points=") == 25
        } else if (arg.rfind("--debug-damage-player=", 0) == 0) {
            sess.debugDamagePlayer = std::atoi(arg.substr(22).c_str());  // strlen("--debug-damage-player=") == 22
        } else if (arg.rfind("--debug-notoriety=", 0) == 0) {
            debugNotoriety = std::atoi(arg.substr(18).c_str());  // strlen("--debug-notoriety=") == 18
        } else if (arg == "--no-node-ground") {
            sess.noNodeGround = true;  // CHOSEN ground stand-in OFF (see results/freeroam_ground_height_20261018.md)
        } else if (arg == "--no-collision") {
            noCollision = true;
        } else if (arg == "--script-roadblock") {
            scriptRoadblock = true;
        } else if (arg.rfind("--script-jump-at=", 0) == 0) {
            sess.scriptJumpAt.push_back(std::atoi(arg.substr(17).c_str()));  // strlen("--script-jump-at=") == 17
        } else if (arg.rfind("--script-crouch-frames=", 0) == 0) {
            const std::string v = arg.substr(23);  // strlen("--script-crouch-frames=") == 23
            const size_t dash = v.find('-');
            sess.scriptCrouchA = std::atoi(v.c_str());
            sess.scriptCrouchB = dash == std::string::npos ? sess.scriptCrouchA : std::atoi(v.c_str() + dash + 1);
        } else if (arg.rfind("--jump-height=", 0) == 0) {
            sess.jumpHeight = std::atof(arg.substr(14).c_str());  // strlen("--jump-height=") == 14
        } else if (arg == "--script-fire") {
            scriptFire = true;
        } else if (arg == "--script-hud-prompt-response") {
            scriptHudPromptResponse = true;
        } else if (arg == "--auto-answer-hud-prompts") {
            scriptHudPromptResponse = true;
            autoAnswerHudPrompts = true;
            // Optional trailing N (frame-delay override) - only consume the
            // next token if it looks like a plain non-negative integer, so a
            // following positional/flag argument is never swallowed by mistake.
            if (argi + 1 < argc) {
                const std::string next = argv[argi + 1];
                if (!next.empty() && next.rfind("--", 0) != 0 &&
                    std::all_of(next.begin(), next.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    hudPromptAutoAnswerDelayFrames = std::atoi(argv[++argi]);
                }
            }
        } else if (arg.rfind("--auto-objective=", 0) == 0) {
            autoObjective = true;
            autoObjectiveFrames = std::atoi(arg.substr(17).c_str()); // strlen("--auto-objective=") == 17
        } else if (arg == "--auto-objective") {
            autoObjective = true;
            // Optional trailing N (frame-threshold override) - same "only
            // consume the next token if it's a plain non-negative integer"
            // convention --auto-answer-hud-prompts uses above, so a
            // following positional/flag argument is never swallowed.
            if (argi + 1 < argc) {
                const std::string next = argv[argi + 1];
                if (!next.empty() && next.rfind("--", 0) != 0 &&
                    std::all_of(next.begin(), next.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    autoObjectiveFrames = std::atoi(argv[++argi]);
                }
            }
        } else if (arg == "--auto-combat") {
            autoCombat = true;
        } else if (arg == "--auto-seat-mission-vehicle") {
            autoSeatMissionVehicle = true;
        } else if (arg == "--auto-skip-cutscenes") {
            autoSkipCutscenes = true;
        } else if (arg.rfind("--mission=", 0) == 0) {
            missionEnabled = true;
            missionStem = arg.substr(10);
        } else if (arg == "--mission-registration-list" && argi + 1 < argc) {
            missionRegistrationListPath = argv[++argi];
        } else if (arg == "--mission-package-tsv" && argi + 1 < argc) {
            missionPackageTsvPathOverride = argv[++argi];
        } else if (arg.rfind("--mission-timeout=", 0) == 0) {
            const std::string v = arg.substr(18);
            char* end = nullptr;
            const double parsed = std::strtod(v.c_str(), &end);
            if (v.empty() || end == v.c_str() || *end != '\0' || parsed < 0.0) {
                std::fprintf(stderr, "bad --mission-timeout value (want a non-negative number of seconds): %s\n",
                             v.c_str());
                return 2;
            }
            missionTimeoutSeconds = parsed;
        } else {
            std::fprintf(stderr, "unrecognised argument '%s'\n", arg.c_str());
            return 2;
        }
    }
    if (missionEnabled && missionStem.empty()) {
        std::fprintf(stderr, "--mission=<stem>: <stem> must not be empty\n");
        return 2;
    }
    if (missionEnabled && !hudEnabled && missionRegistrationListPath.empty()) {
        std::fprintf(stderr,
                     "--mission=<stem> needs a registration list: pass --mission-registration-list <path>, "
                     "or also pass --hud <registration_list.txt> <misc.vpp_pc> to share its list (ONE shared "
                     "Host/EngineState either way - see this function's own doc comment).\n");
        return 2;
    }
    if (noInput && !framesExplicit) maxFrames = 300;  // the smoke test's own documented frame count
    // fix/auto-objective-driving (2026-10-13): a headless --mission --auto-objective run is a mission-SWEEP
    // run, whose own --mission-timeout (default 180 s) is the intended budget - the 300-frame smoke cap above
    // ended every such run after ~10 s of sim time, long before a multi-stage mission could be driven. CHOSEN
    // harness heuristic: when no explicit --frames was given, size the cap to the timeout at the harness's
    // fixed 1/30 s --no-input step (the timeout itself still stops the mission drive).
    if (noInput && !framesExplicit && missionEnabled && autoObjective && missionTimeoutSeconds > 0.0)
        maxFrames = static_cast<int>(missionTimeoutSeconds * 30.0) + 30;
    // CHOSEN default vehicle parking spot: 6m along +Z from --spawn (the
    // player's own forward axis at the default yaw=0 heading - see
    // sim.state_.heading's own "facing +X" comment on the VEHICLE side,
    // a DIFFERENT convention from the player's forward = (cos(pitch)*sin(yaw),
    // sin(pitch), cos(pitch)*cos(yaw)), which is +Z at yaw=0, confirmed via
    // this command's own measured camera-eye-vs-player-Z offset). CORRECTED
    // 2026-10-08 (orchestrator follow-up): the original +X offset placed the
    // car 90 degrees off the camera's own boresight - confirmed empirically
    // (results/verify_default_bat_launch_20261008/bare_default.png) the car
    // was only barely visible, clipped at the extreme frame edge, not the
    // intended "visible in the first frame" result. +Z puts it directly
    // ahead of both the player and the trailing follow camera instead.
    if (!vehicleSpawnExplicit) {
        vehicleSpawnX = spawnX;
        vehicleSpawnZ = spawnZ + 6.0f;
    }
    // Capture is strictly OPT-IN (capturePath stays empty, same convention
    // every other command in this file uses) - UNLIKE an earlier draft of
    // this function, which forced a "_work/playtest.png" default here. A
    // plain interactive launch (run_playtest.bat, no --capture) has no
    // reason to attempt a frame-0 disk write the player never asked for.
    if (captureFrame < 0 && captureFrames.empty() && !capturePath.empty())
        captureFrame = (maxFrames > 0 ? maxFrames - 1 : 0);
    sess.visible = !headless;

    std::printf("=== playtest v0: interactive player<->animation<->camera<->collision demo ===\n");
    std::printf("CONTROLS : W/A/S/D move (real moveSlot/composeVelocity, camera-relative via REAL\n"
                "           CameraSystem::movementRows()); hold Left Shift to sprint; mouse look\n"
                "           (real CameraSystem rotate()/elevate()->cameraHook->preUpdate->yawStep/\n"
                "           pitchStep seam); walk up to the parked car and press E to enter/exit it\n"
                "           (W/S throttle/brake, A/D steer while driving - unless --no-vehicle);\n"
                "           T skips the TOD clock forward %.0f game hour(s) per press (CHOSEN manual\n"
                "           fast-forward, v0.3 polish task - a full in-game day now takes %.0f REAL\n"
                "           minutes, too slow to just wait out during a play session); press R to\n"
                "           arm/disarm the police roadblock spawn (debug gate, OFF by default - see\n"
                "           this command's own doc comment's \"THE HONEST NOTORIETY GATE\" note); Esc\n"
                "           or close the window to quit.\n",
                sr3render::kTodHoursPerTKeyPress, sr3render::kTodSecondsPerDay / 60.0);
    std::printf("NOT IN V0: jump, crouch, gamepad/XInput (spec-tables-ui-controls.md documents the\n"
                "           real gamepad binding layer; not wired here - keyboard+mouse only, said\n"
                "           honestly rather than faking a controller path), ambient PEDESTRIAN\n"
                "           population (the distant/LOD human tier - no spec-confirmed pass wiring\n"
                "           it into playtest yet) - see this command's own doc comment above\n"
                "           runPlaytest() for the full breakdown. Vehicle entry/driving IS wired (ON\n"
                "           by default, --no-vehicle opts out), WITH real world collision (spec-\n"
                "           physics-runtime.md Sec10a, vehicle-world-collision follow-up task -\n"
                "           superseding this line's own former \"no world collision\" wording) - see\n"
                "           that same doc comment's own honest scope note (a CHOSEN simple chase\n"
                "           camera, one PLAYER vehicle only, no vehicle-vs-vehicle collision/damage).\n"
                "           Near-field AMBIENT VEHICLE TRAFFIC IS wired (ALWAYS on, no flag) and the\n"
                "           POLICE ROADBLOCK spawn IS wired (debug key 'R', OFF by default) - see\n"
                "           this command's own doc comment's 2026-10-09 update for the full CHOSEN\n"
                "           breakdown of both. The health/ammo/money/notoriety HUD overlay IS wired\n"
                "           (--hud, opt-in, off unless passed) - see that flag's own honest caveat in\n"
                "           this command's doc comment. The top-right corner minimap (v0.3 polish\n"
                "           task) is now part of that SAME --hud overlay - real live player/vehicle\n"
                "           position and heading feed updateMinimapLiveTransform(), flat per-tile\n"
                "           fallback colour only (no real minimap_CCRR texture lookup wired into this\n"
                "           command).\n");
    if (!noInput) {
        std::printf("MOUSE-LOOK SIGN CONVENTION: chosen by reading lookSlot()/yawStep()/pitchStep()'s\n"
                    "           real angle-integration code (src/player_follow_camera.cpp), not by a\n"
                    "           human trying it - mouse right turns right, mouse up looks up. If that\n"
                    "           reads backwards once a human actually plays it, flip the sign of dx/dy\n"
                    "           in PlaytestLookSource below (one line each).\n");
    }
    return -1;
}

} // namespace sr3viewer
