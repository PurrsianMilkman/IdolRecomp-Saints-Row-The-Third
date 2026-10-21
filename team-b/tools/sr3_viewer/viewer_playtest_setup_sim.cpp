// runPlaytest() split: NPC AI, vehicle, traffic, roadblock and harness-state setup.
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"
#include "sr3vehicleinfo/vehicle_entry.h"

namespace sr3viewer {

int setupNpcAiState(PlaytestSession& sess) {
    auto& debugNotoriety = sess.debugNotoriety;
    auto& npcAiTables = sess.npcAiTables;
    auto& hudEs = sess.hudEs;
    auto& pedAi = sess.pedAi;
    // ---- WEAPON FIRE: damage sink (THIS TASK) --------------------------------
    // A dedicated EngineState for the real damage/death sink
    // (sr3luahost::EngineStateLethalDamageSink -> EngineState::
    // applyGuaranteedLethalHit(), spec-player-runtime.md Sec15.3a) -
    // ALWAYS constructed (not gated behind --hud/--mission) so the fire key
    // works in every invocation, the same "the feature is live
    // unconditionally" precedent the always-on near-field traffic/ped
    // population already sets elsewhere in this function. When --hud/
    // --mission ALSO built a Host (hudEs non-null just above), that SAME
    // EngineState is reused instead of a second, disconnected one - one
    // world state, not two.
    sess.standaloneCombatEs_.emplace();
    auto& standaloneCombatEs = *sess.standaloneCombatEs_;
    sess.combatEs_p = &(hudEs ? *hudEs : standaloneCombatEs);
    sr3luahost::EngineState& combatEs = *sess.combatEs_p;
    sess.bradDamageSink_.emplace(combatEs, "player");
    // ---- NPC AI loop state for the near-field ambient peds (NPC AI loop task) -----------------------------------
    // The live notoriety state (EngineState alias tracks: 0 Luchadores, 1 Deckers, 2 Morningstar, 3 Police) replaces the
    // old hardcoded 0 of the population density inputs. --debug-notoriety=N writes the police track (a labelled DEBUG
    // input: no crime source is wired into this viewer, so without it the state stays at what scripts set).
    if (debugNotoriety >= 0) {
        combatEs.notorietyAliasTrackStates()[3].currentValue = static_cast<double>(debugNotoriety);
        std::printf("npc-ai   : --debug-notoriety=%d written to the police track (DEBUG input)\n", debugNotoriety);
    }
    sess.liveNotorietyLevel = [&combatEs]() {
        int level = 0;
        for (int code = 0; code < 4; ++code) {
            auto it = combatEs.notorietyAliasTrackStates().find(code);
            if (it != combatEs.notorietyAliasTrackStates().end())
                level = std::max(level, std::min(5, static_cast<int>(std::floor(it->second.currentValue))));
        }
        return level;
    };
    // T5: the session notoriety state (Sec19.8). Level changes are written to the alias tracks (the single source the
    // HUD, populations and mission spawn categories already read) unless --debug-notoriety=N is active (override wins).
    sess.notoriety_.emplace();
    {
        auto* sp = &sess;
        sess.notoriety_->onLevelChange = [sp, &combatEs](const sr3airuntime::NotorietyLevelEvent& ev) {
            ++sp->notorietyLevelChanges;
            std::printf("[notoriety] frame %d track=%d level %d -> %d (t=%lldms)%s\n", sp->frames,
                        static_cast<int>(ev.track), ev.oldLevel, ev.newLevel, static_cast<long long>(ev.atMs),
                        sp->debugNotoriety >= 0 ? " [alias NOT written: --debug-notoriety override wins]" : "");
            if (sp->debugNotoriety >= 0) return;
            const int code = static_cast<int>(ev.track);
            combatEs.notorietyAliasTrackStates()[code].currentValue = static_cast<double>(ev.newLevel);
            sp->notorietyAliasLastWritten[static_cast<size_t>(code)] = static_cast<double>(ev.newLevel);
        };
    }
    if (npcAiTables) {
        sr3airuntime::NpcAiConfig pedCfg;
        pedAi = std::make_unique<sr3airuntime::NpcAiRuntime>(npcAiTables, pedCfg, /*seed=*/0x7065);
        std::printf("npc-ai   : near-field ped AI loop ON (civilians: flee/cower when the player fires nearby; CHOSEN stand-ins "
                    "in sr3airuntime/npc_ai_runtime.h)\n");
    }
    // Per-ped last-known gunfire reaction (THIS TASK) - sr3airuntime::
    // PedReactionMode has no existing storage field on NearFieldPed itself
    // (near_field_ped_population.h's own struct carries only the CALLER-
    // supplied facts EvaluateGunfireReaction() reads, not an output slot -
    // see that header's own Sec20.4 block) and nothing in this codebase
    // currently consumes a computed PedReactionMode into movement/AI state
    // either (confirmed: no other call site references PedReactionMode
    // outside near_field_ped_population.h's own tests) - this map is this
    // task's OWN observability-only side table, not a new modelled engine
    // field, so the real decision (ChooseFleeOrCower/EvaluateGunfireReaction,
    // both already spec-confirmed and unit-tested) is exercised for real
    // without this task inventing a flee-movement system to consume it (see
    // this function's own "WEAPON FIRE" doc comment above runPlaytest() for
    // the honest statement of this exact gap).
    // THIS TASK (2026-10-10): the one new piece of per-ped state Flee
    // actually needs - the real near_field_ped_population.h NearFieldPed
    // struct itself has no flee-destination field (same scope-split every
    // other sr3airuntime struct in this file uses: caller-owned facts, not
    // a new engine field) - a ped's presence as a key here means "currently
    // fleeing toward this point," reusing the real WanderMove/
    // WanderToSidewalk sub-state machine (near_field_ped_population.h's own
    // TickWander/TickIdleSubBehaviour, Sec20.2) to drive it, not a parallel
    // movement system. See the per-frame mover block below (after the
    // near-field ped spawn/draw section) for the actual step, and that
    // header's own ChooseFleeDestination/kFleeSpeedMsChosen for the CHOSEN
    // destination rule and speed stand-in this reuses.
    // GENERAL-PED-IDLE-TICK TASK (2026-10-10): the wider pre-existing gap
    // the flee task's own HANDOFF entry flagged but did not take on -
    // "ordinary idle peds never actually sidewalk-walk or wander either."
    // These two maps are this task's own per-ped state, same scope-split
    // discipline as nearFieldPedFleeDestination just above (caller-owned
    // facts, not new NearFieldPed fields):
    //  - nearFieldPedWanderDestination: a non-fleeing ped's own chosen
    //    wander-move target (near_field_ped_population.h's own WanderMove
    //    sub-state, Sec20.2), persisted across frames the same way Flee's
    //    own destination map already is, so TickWander's own
    //    hasReachedDestination check is against one stable point rather
    //    than a re-rolled one every frame.
    //  - nearFieldPedSidewalkSegGeo: the real stand-in SidewalkSegment
    //    geometry (start/end) a ped most recently snapped onto (sub-state
    //    13's own "snap onto the graph" gate, 0x0051bb70) - captured here
    //    because TickSidewalk itself only advances the abstract 0..1
    //    segmentProgress scalar (near_field_ped_population.h's own file
    //    header: no C++ Sidewalks graph reader exists, caller resolves
    //    geometry) and does not touch NearFieldPed::pos at all; this map
    //    lets the per-frame mover below translate progress into an actual
    //    world position via SidewalkSegment::PointAt (the SAME helper
    //    SelectNearFieldPedSpawn already uses for an identical progress->
    //    point mapping, not a new formula).
    return -1;
}

int setupVehicle(PlaytestSession& sess) {
    const auto& charactersPath = sess.charactersPath;
    auto& vehicleEnabled = sess.vehicleEnabled;
    auto& vehicleArchiveOverride = sess.vehicleArchiveOverride;
    auto& vehicleWheelsArchiveOverride = sess.vehicleWheelsArchiveOverride;
    auto& vehicleCmeshName = sess.vehicleCmeshName;
    auto& vehicleSpawnX = sess.vehicleSpawnX;
    auto& vehicleSpawnZ = sess.vehicleSpawnZ;
    auto& scriptVehicleDriveFramesOverride = sess.scriptVehicleDriveFramesOverride;
    auto& device = *sess.device_;
    auto& finalGroundY = sess.finalGroundY;
    // ===================== VEHICLE ENTRY/DRIVING (THIS TASK) ==================
    // Loads ONE real vehicle (PlaytestVehicleRig, defined above runDriveDemo()
    // in this file - see that struct's own doc comment for the full REAL/
    // CHOSEN breakdown) and parks it at (vehicleSpawnX, finalGroundY,
    // vehicleSpawnZ). A load failure disables the feature entirely rather
    // than rendering something broken (this project's own "honestly omit
    // rather than ship half-working" rule) - playtest still runs exactly as
    // before, on foot only, with a printed reason why.
    sess.vehicleRig_.emplace();
    auto& vehicleRig = *sess.vehicleRig_;
    // T key (v0.3 polish task, 2026-10-09) - same edge-triggered debounce
    // shape as prevEDown just above, applied to the new manual TOD time-
    // skip key (see the main loop's own "T key" block below).
    // CHOSEN proximity/placement constants (this task) - 3m entry radius
    // comfortably inside walking range of the car's own ~4.5m body length;
    // 2.5m lateral exit offset clears the body without needing a real door-
    // hinge position (spec-vehicle-geometry.md §7.2/§12.2's own open
    // "door panel" questions - see PlaytestVehicleRig's own doc comment).
    // Simplified trailing chase-cam while driving - IDENTICAL CHOSEN
    // constants/formula to runDriveDemo()'s own simplified camera (see that
    // function's own doc comment for the citation); reused rather than
    // adapting the real CameraSystem, which has no vehicle submode any spec
    // confirms (see this function's own "UPDATED 2026-10-08" doc comment).
    // Driver-seat local offset: SUPERSEDED (seated-driving-pose merge, 2026-10-10) - the
    // vehicle-occupant-hide-flag task's own CHOSEN "stand at an offset" placeholder (three
    // constants, no real pose) that used to live here is replaced by the real seated/driving
    // pose composed every frame in the DRIVING branch below (spec-player-runtime.md Sec7.4b,
    // sr3player::SeatedRidePoseApplier) - see that block's own doc comment for the full
    // CHOSEN/CONFIRMED breakdown of the seat-offset and ground-height reasoning that now
    // applies instead.
    // --script-vehicle (--no-input only): a deterministic enter/drive/exit
    // schedule for the headless smoke test - see
    // tests/real_playtest_vehicle_smoke_test.cpp. Frame 10 (well inside the
    // 300-frame --no-input default): press E (vehicle is in range from
    // frame 0 when the test's own --spawn places the player a couple
    // metres from --vehicle-spawn); drive straight for 120 frames; press E
    // again to exit; walk for whatever frames remain.
    // --script-vehicle-drive-frames overrides this (THIS TASK, world-streaming-reconcile) -
    // default stays 120, byte-for-byte the real_playtest_vehicle_smoke_test.cpp baseline.
    // UPDATED (vehicle-world-collision follow-up task): the "long override is safe because
    // the vehicle rig has no world collision to get stuck on" rationale this comment
    // originally gave no longer holds - the vehicle now has real world collision (see
    // PlaytestVehicleRig::Advance()'s own doc comment) and a long straight-line script CAN
    // now stop early against real geometry along its path, correctly - that is this
    // follow-up task's entire point, not a bug in this override.
    const int kScriptVehicleDriveFrames =
        scriptVehicleDriveFramesOverride >= 0 ? scriptVehicleDriveFramesOverride : 120;
    sess.kScriptVehicleExitFrame = sess.scriptVehicleEnterFrame + kScriptVehicleDriveFrames;
    if (vehicleEnabled) {
        std::string vehDir;
        {
            const size_t slash = charactersPath.find_last_of("/\\");
            vehDir = (slash == std::string::npos) ? std::string() : charactersPath.substr(0, slash + 1);
        }
        std::string vehArchivePath = vehicleArchiveOverride.empty() ? vehDir + "dlc1.vpp_pc" : vehicleArchiveOverride;
        std::string wheelsArchivePath =
            vehicleWheelsArchiveOverride.empty() ? vehDir + "vehicles_preload.vpp_pc" : vehicleWheelsArchiveOverride;
        for (auto& ch : vehArchivePath) if (ch == '\\') ch = '/';
        for (auto& ch : wheelsArchivePath) if (ch == '\\') ch = '/';
        std::string vehDiag;
        if (!vehicleRig.Load(device.device(), vehArchivePath, vehicleCmeshName, wheelsArchivePath, vehDiag)) {
            std::printf("vehicle  : DISABLED - load failed (%s): %s - playtest continues on foot only\n",
                        vehArchivePath.c_str(), vehDiag.c_str());
            vehicleEnabled = false;
        } else {
            vehicleRig.sim->state_.posX = static_cast<double>(vehicleSpawnX);
            vehicleRig.sim->state_.posZ = static_cast<double>(vehicleSpawnZ);
            vehicleRig.sim->state_.heading = 0.0; // facing +X, same convention buildWheelDeltaTransformV() documents
            vehicleRig.PoseAndUpload(device.device(), 0.0); // static resting pose before ever driven
            std::printf("vehicle  : %s :: %s parked at (%.1f %.1f %.1f) - walk within %.1fm and press E to enter\n",
                        vehArchivePath.c_str(), vehicleCmeshName.c_str(), static_cast<double>(vehicleSpawnX),
                        static_cast<double>(finalGroundY), static_cast<double>(vehicleSpawnZ),
                        static_cast<double>(kVehicleEnterRadius));
        }
    } else {
        std::printf("vehicle  : --no-vehicle given - disabled\n");
    }
    // Ambient model variety (spec-ai-runtime.md 20.6.1(e)/20.6.2(e)): per-category model registry. Needs the
    // character archive (mapped by setupBrad), the vehicles archive next to it and the spawn/character tables.
    // Any failure leaves ambientModels_ not ready and the old placeholder draw path untouched.
    if (!sess.noAmbientModels && sess.charArchiveBytes_ && sess.docOf) {
        std::string dir;
        {
            const size_t slash = charactersPath.find_last_of("/\\");
            dir = (slash == std::string::npos) ? std::string() : charactersPath.substr(0, slash + 1);
        }
        std::string vehiclesPath = dir + "vehicles.vpp_pc";
        std::string wheelsPath = vehicleWheelsArchiveOverride.empty() ? dir + "vehicles_preload.vpp_pc" : vehicleWheelsArchiveOverride;
        for (auto& ch : vehiclesPath) if (ch == '\\') ch = '/';
        for (auto& ch : wheelsPath) if (ch == '\\') ch = '/';
        sess.ambientModels_.emplace();
        sess.ambientModels_->setNoWalkAnim(sess.noPedAnim);
        std::string animPath = sess.animArchiveOverride.empty() ? dir + "preload_anim.vpp_pc" : sess.animArchiveOverride;
        for (auto& ch : animPath) if (ch == '\\') ch = '/';
        PlaytestSession* sp = &sess;
        // Per-model AI_Max_Braking_Decel (+0x770) from the model's own `<name>_veh.xtbl` row (misc_tables), used per car
        // by the traffic tick (feature/ambient-per-car-braking). Missing file/row/tag -> nullopt (CHOSEN fallback there).
        sess.ambientModels_->setBrakingDecelLookup([sp](const std::string& name) -> std::optional<double> {
            const sr3xtbl::Document doc = sp->docOf(name + "_veh.xtbl");  // throws when absent; the registry catches
            for (const auto& row : sr3vehicleinfo::ParseAllVehicles(doc)) {
                if (!row.name.has_value()) continue;
                std::string rn = *row.name;
                for (auto& ch : rn) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (rn != name) continue;
                if (!row.aiMaxBrakingDecel.present) return std::nullopt;
                return static_cast<double>(row.aiMaxBrakingDecel.value);
            }
            return std::nullopt;
        });
        const bool ok = sess.ambientModels_->init(
            device.device(), vehiclesPath, wheelsPath, &*sess.charArchiveBytes_, sess.docOf,
            [sp](double x, double y, double z) -> std::string {
                // Same hood BSP lookup as sess.currentSpawnCategory (20.6.8), without its trace-string side effects.
                if (const sr3zone::HoodVolume* h = sr3zone::FindHood(sp->playtestHoods, x, y, z)) return h->spawnCategory;
                return "Default Spawn";
            },
            animPath);
        if (!ok) sess.ambientModels_.reset();
    }
    // AW-15: the craft rows (Vehicle_Type 2..5) are only read when a craft is requested (reads misc_tables + dlc1..3).
    if (sess.craft.registryWanted) {
        std::string dir;
        {
            const size_t slash = charactersPath.find_last_of("/\\");
            dir = (slash == std::string::npos) ? std::string() : charactersPath.substr(0, slash + 1);
        }
        std::string diag;
        missionzoneobj::loadCraftTypeRegistry(dir, diag);
        sess.craft.registryLoaded = true;
        std::printf("craft    : %s\n", diag.c_str());
        if (sess.craft.spawnName == "list") {
            for (const auto& kv : sr3viewer_craft::CraftRegistry())
                std::printf("craft    : row %s class %d mass %.0f maxSpeed %.1f buoy %.2f dataBlocks heli=%d plane=%d boat=%d\n",
                            kv.first.c_str(), kv.second.cls, kv.second.mass, kv.second.maxSpeed, kv.second.buoyancy,
                            kv.second.dataHeli ? 1 : 0, kv.second.dataPlane ? 1 : 0, kv.second.dataBoat ? 1 : 0);
            if (sess.ambientModels_ && sess.ambientModels_->ready())
                for (const auto& n : sess.ambientModels_->vehicleNameList()) std::printf("craft    : mesh %s\n", n.c_str());
        }
    }
    return -1;
}

int setupTraffic(PlaytestSession& sess) {
    auto& vehicleSpawnX = sess.vehicleSpawnX;
    auto& vehicleSpawnZ = sess.vehicleSpawnZ;
    auto& playtestRoadGraph = sess.playtestRoadGraph;
    auto& trafficLaneGraphBuf = sess.trafficLaneGraphBuf;
    auto& trafficLaneGraphOwner = sess.trafficLaneGraphOwner;
    const auto& kNearFieldPedLocalCap = sess.kNearFieldPedLocalCap;
    // ===================== NEAR-FIELD AMBIENT VEHICLE TRAFFIC (THIS TASK) =====================
    // Wires include/sr3airuntime/near_field_vehicle_population.h's own
    // NearFieldVehicleDriver/RunNearFieldVehicleMaintenancePass (spec-ai-
    // runtime.md Sec17.4c), reused completely unmodified, into the LIVE
    // per-frame loop below - see this function's own 2026-10-09 doc-comment
    // update (above) for the full CHOSEN breakdown. ALWAYS ON.
    sess.nearFieldDriver_.emplace();
    sess.nearFieldToggles_.emplace();
    sess.nearFieldRng_.emplace(20261009u);
    auto& nearFieldRng = *sess.nearFieldRng_;
    sess.nearFieldPickUniform = sr3airuntime::MakePickUniform(nearFieldRng);
    // player_vehicle_despawn_distance=100m, squared (spec-tables-traffic-
    // ai.md Sec26.3's own real sample - the SAME constant near_field_vehicle_
    // demo.cpp's own Scenario 2/3 uses).
    // Camera state of the PREVIOUS two frames (Sec20.6.1(b)/(d), RG-6): the eye is built later in the frame than the
    // traffic tick, so the tick sees last frame's camera and the displacement between the last two frames.
    std::printf("traffic  : near-field ambient vehicle population ALWAYS ON (cap %d, spec-ai-runtime.md "
                "Sec17.4c)\n",
                sr3airuntime::kNearFieldVehicleCap);

    // ===================== NEAR-FIELD TRAFFIC LANE-FOLLOWING (TRAFFIC-LANE-
    // FOLLOW-REAL-MECHANISM TASK, 2026-10-10) =====================
    // REPLACES the prior "position-only, cosmetic deterministic-per-id
    // heading" rendering (Sec17.4c itself never traces a heading/driving
    // field for this tier - see NearFieldVehicle's own file comment) with
    // the now-CONFIRMED lane-following drive loop (spec-ai-runtime.md
    // Sec18.1/18.2/18.5/18.6): sr3airuntime::TrafficLaneGraph (the REAL
    // 0x2236 Traffic record reader, sr3zone/traffic_graph.h) over a
    // SYNTHETIC-DATA/REAL-FORMAT fixture (BuildPlaytestTrafficLaneGraphFixture()
    // above - this playtest's own `city` loader has no real Traffic
    // `.czn_pc` content-kind reader, same honest gap
    // GatherNearFieldSpawnCandidatesAroundPlayer()'s own doc comment already
    // states for the distant-tier synthetic road grid), anchored at the
    // vehicle spawn point so it is reachable on foot/by car from the
    // player's own start position.
    //
    // Per-vehicle driving state (LaneFollowState + a live heading double) is
    // kept in two side-car maps, keyed by NearFieldVehicle::id - the SAME
    // "caller-owned side-car map, not a field added to the population
    // struct itself" convention nearFieldPedWanderDestination/
    // nearFieldPedFleeDestination already establish for the near-field PED
    // population just below.
    // feature/road-graph: with the real city road graph loaded the lane graph IS that graph (polylines =
    // decoded segment points, adjacency = the on-disk successor lists); the synthetic closed-loop
    // fixture is used only when no archive carried the graph.
    if (playtestRoadGraph) {
        trafficLaneGraphOwner = std::make_unique<sr3airuntime::TrafficLaneGraph>(*playtestRoadGraph);
        std::printf("traffic  : lane graph = REAL road graph (%zu lanes, on-disk successor links)\n",
                    trafficLaneGraphOwner->segmentCount());
    } else {
        sr3zone::TrafficRecord trafficLaneRecord =
            BuildPlaytestTrafficLaneGraphFixture(vehicleSpawnX, vehicleSpawnZ, trafficLaneGraphBuf);
        trafficLaneGraphOwner = std::make_unique<sr3airuntime::TrafficLaneGraph>(trafficLaneRecord);
    }
    sess.trafficLaneGraph_p = &(*trafficLaneGraphOwner);
    // Sec18.6a overtake-gate task (2026-10-10): this vehicle's own last-
    // computed speed (world units/sec), fed back into StepLaneFollow()'s
    // own closing-speed gate test next tick (file header item 5a of
    // sr3airuntime/traffic_lane_follower.h - CHOSEN one-body simplification,
    // no 2nd-vehicle velocity is tracked). Same side-car-map convention as
    // nearFieldVehicleHeading just above.
    // overtake-gate-18.6b task (2026-10-11) diagnostic-only side-car maps -
    // NOT a gameplay field, exists purely so --headless runs can print a
    // real per-vehicle net-displacement/gate-fire summary at the end of the
    // run (same measurement this task's own live 900-frame re-verification
    // needs), without adding a permanent CLI flag for something only this
    // kind of offline verification run needs.
    // CHOSEN (not spec-stated - Sec18 describes steady-state following
    // only, never a kinematic model for ambient traffic that has no own
    // per-instance Havok body): near-field ambient vehicles do not each get
    // a full sr3physics::VehicleSubstepSimulator instance (48 live Havok-
    // shaped sims every frame is out of this task's own scope and no
    // per-ambient-vehicle physics object exists anywhere else in this
    // codebase either - near-field vehicles are, and remain, "position +
    // id only" per NearFieldVehicle's own struct). Instead, the REAL
    // LaneFollowCommand{steer,throttle,brake} this task's own confirmed
    // algorithm computes is integrated through a simple CHOSEN kinematic
    // bicycle-style update (heading rate from steer, speed from throttle) -
    // the steering/throttle DECISION is the real, newly-confirmed mechanism
    // under test; the vehicle BODY that decision drives is the same
    // honest "every ambient vehicle currently looks like the same car
    // model" simplification tier this file's own near-field rendering
    // comment already documents, extended from "position" to "position +
    // heading, driven by a real decision".
    // Real-graph path (RG-12/RG-13): CHOSEN kinematic constants. 3 m/s^2 is the acceleration Sec20.6.7(c) uses to
    // predict conflict windows; the rest stand in for per-vehicle data the project lacks (vehicle info
    // AI_Max_Braking_Decel is not wired to ambient cars, box extents are not modelled).
    // follower braking memory: own id -> (remembered target id, seconds left of the 300 ms memory)

    // Sec20.6.13 traffic signals (real road graph only): the controller (init at world start, per-frame cursor
    // tick), the per-car stop-line memory (bit-7 lanes) and the diagnostics the headless run prints.
    // CHOSEN stand-ins: every near-field car is an ambient mode-8 car with the obey bit set (no chase/flee modes
    // exist in this population); "light zone resident" = junction within kSignalResidentRadius of the player
    // (the light record carries only a zone id, filled by the 0x2241 tile loader that this viewer does not run).
    sess.signalController_.emplace();
    auto& signalController = *sess.signalController_;
    // Ambient-ped crossing gate (spec-ai-runtime.md Sec20.6.9(c) / Sec20.6.13(h)): per-ped navigator bits 3/4/5.
    // Ambient-ped navigator (spec-ai-runtime.md Sec20.6.9, include/sr3airuntime/ped_navigator.h): per-ped route + counters.
    if (playtestRoadGraph) {
        signalController.Init(*playtestRoadGraph, 0);
        signalController.SetRecordEvents(true);
        std::printf("signals  : %zu junctions, %zu lights, %zu phases initialised (Sec20.6.13(e))\n",
                    playtestRoadGraph->junctions.size(), playtestRoadGraph->lights.size(),
                    playtestRoadGraph->phases.size());
    }

    // ===================== NEAR-FIELD AMBIENT PEDESTRIAN POPULATION (THIS TASK) =====================
    // Wires include/sr3airuntime/near_field_ped_population.h's own
    // NearFieldPedDriver/RunNearFieldPedSpawnPass (spec-ai-runtime.md
    // Sec20), reused completely unmodified, into the LIVE per-frame loop
    // below - following the near-field vehicle traffic wiring just above as
    // its exact precedent (same per-frame cadence source, same reference
    // position/heading, same ALWAYS-ON convention, same "cap reused, not
    // re-literalled" discipline) - see this function's own 2026-10-10
    // doc-comment update (above) for the full CHOSEN breakdown. ALWAYS ON.
    sess.nearFieldPedDriver_.emplace();
    sess.nearFieldPedRng_.emplace(20261010u);
    auto& nearFieldPedRng = *sess.nearFieldPedRng_;
    sess.nearFieldPedPickUniform = sr3airuntime::MakePickUniform(nearFieldPedRng);
    // UPDATED 2026-10-10 (ped-spawn-distance-spec-tuning task): isVisibleToCamera
    // is now wired to near_field_ped_population.h's own ApproxVisibleToCamera
    // helper (reassigned fresh each tick, just below its own call site, from
    // the SAME live trafficReferencePos/trafficReferenceForward the vehicle
    // side already uses - no separate camera/frustum state owned here,
    // consistent with every other "no real X in this build" gap this file's
    // own doc comment already lists). difficultyGatePasses/isGroundValid/
    // sidewaysOffsetDistance remain at their own CHOSEN defaults (still no
    // difficulty/physics state this harness owns to inject) -
    // cameraFarRejectDist remains disabled (0.0): Sec20.1's own "a second
    // distance threshold" has no CONFIRMED literal to enforce, see
    // near_field_ped_population.h's own field comment.
    sess.nearFieldPedGates_.emplace();
    // near_field_ped_population.h's own file header (item 4): no CONFIRMED
    // numeric cadence exists for the ped tick's own throttle gate (unlike
    // the vehicle sibling's CONFIRMED 200-300ms/400-600ms literals) - this
    // driver models ONE randomized gate with CALLER-SUPPLIED bounds. CHOSEN
    // here: reuse the vehicle sibling's own 200-300ms range (the only
    // known near-field-population cadence precedent in this codebase), an
    // honestly-labelled stand-in, not a second confirmed literal.
    // near_field_ped_population.h's own file note: "local headroom" has no
    // CONFIRMED numeric literal for peds (unlike the vehicle tier's
    // CONFIRMED 48) and is CALLER-SUPPLIED. CHOSEN here: reuse
    // kNearFieldVehicleCap (48), the one known near-field-population cap
    // precedent - see this function's own doc-comment update (above) for
    // the full citation.
    sess.kNearFieldPedLocalCap = static_cast<size_t>(sr3airuntime::kNearFieldVehicleCap);
    std::printf("peds     : near-field ambient pedestrian population ALWAYS ON (cap %zu, spec-ai-runtime.md "
                "Sec20)\n",
                kNearFieldPedLocalCap);
    return -1;
}

int setupRoadblockOccupant(PlaytestSession& sess) {
    auto& vehicleEnabled = sess.vehicleEnabled;
    auto& vehicleInteractionNameOverride = sess.vehicleInteractionNameOverride;
    auto& docOf = sess.docOf;
    auto& roadblockSlots = sess.roadblockSlots;
    auto& vehicleHideOccupant = sess.vehicleHideOccupant;
    // ===================== POLICE ROADBLOCK SPAWN (THIS TASK) =====================
    // Wires include/sr3airuntime/police_roadblock_spawn.h's own
    // RoadblockPopulationDriver/RoadblockSpawnManager (spec-ai-runtime.md
    // Sec19.7), reused completely unmodified, behind a new debug key ('R') -
    // see this function's own 2026-10-09 doc-comment update for the full
    // CHOSEN breakdown, including THE HONEST NOTORIETY GATE note (armed is
    // NOT tied to notoriety - that link is spec-confirmed OPEN). Slot table
    // (CHOSEN, not spec-stated - Sec19.5's own "who writes a roadblock-class
    // instance's own slot table" remains OPEN): 4 ped slots (throttle cap
    // 2/tick) + 4 vehicle slots (1 named "Spike_strip", 3 generic; throttle
    // cap 4/tick) - no coop slots (kCoopPlayerSlotA/B are SKIPPED, no-op
    // hand-offs per that header's own file note; omitting them here lets
    // the anchor actually recenter once the table fills, same reasoning
    // police_roadblock_demo.cpp's own Section 2 documents).
    {
        sr3airuntime::RoadblockSlot s;
        s.category = sr3airuntime::RoadblockCategory::kPed;
        for (int i = 0; i < 4; ++i) roadblockSlots.push_back(s);
        s.category = sr3airuntime::RoadblockCategory::kVehicle;
        s.vehicleDefinitionName = sr3airuntime::kChosenSpikeStripName;
        roadblockSlots.push_back(s);
        s.vehicleDefinitionName = sr3airuntime::kChosenGenericVehicleName;
        for (int i = 0; i < 3; ++i) roadblockSlots.push_back(s);
    }
    sess.roadblockManager_.emplace(roadblockSlots);
    sess.roadblockDriver_.emplace();
    // Per-slot cached visual (THIS TASK's own rendering-glue state, NOT part
    // of the header's own public API - RoadblockSlot itself does not store
    // a resolved creation request). Populated/cleared each tick by diffing
    // RoadblockSpawnManager::slots()'s own `filled` flag before/after
    // RunTick() against the tick's own non-coop dispatches, in slot order -
    // see the main loop's own "POLICE ROADBLOCK TICK" block for exactly how.
    // ONE DOCUMENTED EDGE CASE this diffing does not perfectly cover: a slot
    // that both frees (its 45s occupant lifetime, kChosenOccupantLifetimeSeconds,
    // expires) AND is immediately refilled within the SAME tick (possible -
    // RunTick() runs the despawn pass then the spawn-attempt walk in one
    // call) keeps its OLD cached visual for that one frame, stale. Cosmetic
    // only - the real slot/throttle/anchor state inside RoadblockSpawnManager
    // itself is entirely unaffected, this cache is draw-only.
    sess.roadblockSlotVisual = std::vector<std::optional<sr3airuntime::RoadblockDispatchResult>>(roadblockSlots.size());
    std::printf("roadblock: police roadblock spawn OFF by default (%zu slots: 4 ped + 4 vehicle [1 Spike_strip "
                "+ 3 generic]) - press B to arm (spec-ai-runtime.md Sec19.7; debug gate, not tied to "
                "notoriety)\n",
                roadblockSlots.size());
    // --script-roadblock (--no-input only): a deterministic arm-then-disarm
    // 'R' press pair, same frame-scheduled idiom --script-vehicle's own
    // kScriptVehicleEnterFrame/kScriptVehicleExitFrame already establishes.
    // Arm early enough, disarm late enough, that a 300-frame default run
    // (the --no-input default below) gives the roadblock chain many ticks
    // to actually populate its own slot table before the scripted disarm.
    // --script-fire (THIS TASK, playtest-weapon-fire-wiring): a single
    // deterministic left-click, scheduled late enough that the always-on
    // near-field ambient-pedestrian spawner (kNearFieldPedTickMinMs/MaxMs,
    // 200-300ms real-time ticks) has had many ticks to populate at least
    // one live ped before the shot fires.

    // ===================== VEHICLE OCCUPANT VISIBILITY (THIS TASK, 2026-10-09) =====================
    // Replaces the prior task's own blanket "character pauses/hides while in a vehicle"
    // rule with the REAL per-vehicle, per-seat `Hide Occupant` flag (spec-render-pipeline.md
    // Sec23.14, citing spec-tables-vehicle-world.md Sec3.2's `+0x09` flags-byte-2 bit6):
    // read directly from the shipped `vehicle_interaction_info.xtbl`, `Hide Occupant` is set
    // on a DRIVER seat for only 2 of 131 real rows (`Tank NatGrd`/`Tank STAG`, both tanks,
    // whose own gunner/turret seat does NOT set it) - every ordinary car/motorcycle/
    // helicopter/boat/VTOL driver seat has it absent. Hiding the character for every vehicle
    // (the old behaviour) reproduced the tank-only special case universally, not retail.
    //
    // `vehicle_interaction_info.xtbl` is already a real, wired reader (src/
    // tables_vehicle_world.cpp's parseSeatInfo(), confirmed by this task - not a stub): it is
    // the SAME archive (misc_tables.vpp_pc) and the SAME docOf()/sr3xtbl::ParseDocument
    // helper already used above for camera_free.xtbl/control_parameters.xtbl/etc., so loading
    // it here adds no new archive-reading plumbing, only this one table's own already-
    // existing parser.
    //
    // Which REAL row applies to the one vehicle this command can load is a SEPARATE, unbuilt
    // cross-reference this task does not add (see --vehicle-interaction-name's own doc
    // comment above, by vehicleCmeshName's declaration, for the full account of why: it runs
    // through a per-vehicle `<name>_veh.xtbl` this tool never loads for its hardcoded demo
    // mesh) - so vehicleHideOccupant below is driven by the CLI override, defaulting to
    // "false" (shown), which is the REAL, empirically-confirmed behaviour for the default
    // ordinary sedan this command actually drives.
    // ResolveDriverHideOccupant()/FindSeatByResolverIndex() (include/sr3tables_vehicle_world/
    // tables.h) are the SAME shared, unit-tested helper tests/synthetic_tables_vehicle_world_
    // test.cpp and tests/real_vehicle_interaction_hide_occupant_test.cpp exercise directly -
    // one implementation of "which seat is the driver, does it hide its occupant", not
    // re-derived here.
    if (vehicleEnabled) {
        try {
            const sr3xtbl::Document viiDoc = docOf("vehicle_interaction_info.xtbl");
            const std::vector<sr3tables_vehicle_world::VehicleInteractionInfo> viiRows =
                sr3tables_vehicle_world::ParseVehicleInteractionInfoTable(viiDoc);
            if (vehicleInteractionNameOverride.empty()) {
                std::printf("vehicle  : no --vehicle-interaction-name given - occupant hide stays at its real-"
                            "data default (shown; correct for the ordinary default vehicle, spec-render-"
                            "pipeline.md Sec23.14)\n");
            } else {
                vehicleHideOccupant =
                    sr3tables_vehicle_world::ResolveDriverHideOccupant(viiRows, vehicleInteractionNameOverride);
                std::printf("vehicle  : '%s' driver seat Hide Occupant = %s (vehicle_interaction_info.xtbl, %zu "
                            "real row(s) loaded, spec-render-pipeline.md Sec23.14)\n",
                            vehicleInteractionNameOverride.c_str(), vehicleHideOccupant ? "true" : "false",
                            viiRows.size());
            }
        } catch (const std::exception& ex) {
            std::printf("vehicle  : vehicle_interaction_info.xtbl not available (%s) - occupant hide stays at its "
                        "default (shown)\n", ex.what());
        }
    }
    return -1;
}

int setupLoopState(PlaytestSession& sess) {
    const auto& charactersPath = sess.charactersPath;
    auto& spawnX = sess.spawnX;
    auto& spawnZ = sess.spawnZ;
    auto& autoObjective = sess.autoObjective;
    auto& autoObjectiveFrames = sess.autoObjectiveFrames;
    auto& missionEnabled = sess.missionEnabled;
    auto& missionStem = sess.missionStem;
    auto& interfaceArchiveOverride = sess.interfaceArchiveOverride;
    auto& bradWeaponDef = *sess.bradWeaponDef_;
    auto& bradWeaponDefFound = sess.bradWeaponDefFound;
    auto& hudEs = sess.hudEs;
    auto& pauseDocLoadAttempted = sess.pauseDocLoadAttempted;
    auto& pauseDocLoaded = sess.pauseDocLoaded;
    auto& pauseDocHandle = sess.pauseDocHandle;
    sr3luahost::EngineState& combatEs = *sess.combatEs_p;
    auto& finalGroundY = sess.finalGroundY;
    auto& freq = sess.freq;
    auto& prevTime = sess.prevTime;
    auto& autoRoute = sess.autoRoute;
    auto& lowerStr = sess.lowerStr;
    std::printf("spawn    : (%.1f %.1f %.1f)\n", static_cast<double>(spawnX), static_cast<double>(finalGroundY),
                static_cast<double>(spawnZ));
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prevTime);
    // feature/route-table-driving: per-mission objective route from tools/mission_routes.tsv (generated by
    // tools/gen_mission_routes.ps1 from spec-mission-packages.md "Per-mission objective routes", spec sync 40feafc).
    // Used ONLY to order/filter the harness's own teleport targets (route triggers first, in route order; spec-listed
    // hazard triggers never) and to name vehicles to seat. It adds no engine behaviour.
    // Hold: after a route teleport the player is re-warped every frame for autoRouteHoldFramesLeft frames so that
    // zone exit callbacks (m22) / a script resetting its flag cannot see the player outside the volume. CHOSEN.
    bool autoVehicleSeatedOnce = false;
    // Engine-callback wait assists (--auto-objective; see the "engine callback waits" block in the frame loop).
    // --auto-combat state (see the "--auto-combat" block in the kill-wait assist).
    sess.autoCombatCfg_.emplace();
    sess.autoCombatState_.emplace();
    // CHOSEN damage per player hit: the equipped weapon's authored Damage_Max NPC value when present, else 25.
    sess.autoCombatSink_.emplace(combatEs, (bradWeaponDefFound && bradWeaponDef.damageMaxNpc.present &&
                                                   bradWeaponDef.damageMaxNpc.value > 0.0f) ? bradWeaponDef.damageMaxNpc.value : 25.0f);
    // CHOSEN: 2 x the stall threshold before the first kill assist, then one every 60 frames.
    sess.autoKillIntervalFrames = autoObjectiveFrames * 2;
    // CHOSEN talk time for a mission cellphone call (the real dialogue length is OPEN in the spec): 5 s at the
    // harness's 30 Hz step.
    sess.lowerStr = [](std::string s) { for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return s; };
    if (autoObjective && missionEnabled) {
        const char* routeCandidates[] = {"tools/mission_routes.tsv", "../../tools/mission_routes.tsv", "../tools/mission_routes.tsv"};
        for (const char* rp : routeCandidates) {
            std::ifstream rf(rp);
            if (!rf) continue;
            std::string ln;
            while (std::getline(rf, ln)) {
                std::vector<std::string> col;
                size_t p = 0;
                while (true) {
                    size_t q = ln.find('\t', p);
                    col.push_back(ln.substr(p, q == std::string::npos ? std::string::npos : q - p));
                    if (q == std::string::npos) break;
                    p = q + 1;
                }
                if (col.size() < 4 || col[0] != missionStem) continue;
                autoRoute.loaded = true;
                std::vector<std::string> names;
                { size_t a = 0; const std::string& nm = col[3];
                  while (a <= nm.size() && !nm.empty()) { size_t b = nm.find('|', a);
                    names.push_back(nm.substr(a, b == std::string::npos ? std::string::npos : b - a));
                    if (b == std::string::npos) break; a = b + 1; } }
                if (col[2] == "hazard") { for (auto& n : names) autoRoute.hazards.insert(lowerStr(n)); }
                else if (col[2] == "go-to-trigger" || col[2] == "drive-to") { for (auto& n : names) { autoRoute.triggers.push_back(lowerStr(n)); autoRoute.triggerRow.push_back(std::atoi(col[1].c_str())); } }
                else if (col[2] == "enter-vehicle") { for (auto& n : names) autoRoute.vehicles.push_back(n); }
            }
            std::printf("auto-objective: route table '%s' for '%s': %s (%zu trigger steps, %zu hazards, %zu named vehicles)\n",
                        rp, missionStem.c_str(), autoRoute.loaded ? "loaded" : "no rows", autoRoute.triggers.size(),
                        autoRoute.hazards.size(), autoRoute.vehicles.size());
            break;
        }
        if (!autoRoute.loaded) std::printf("auto-objective: no route rows for '%s' - legacy trigger heuristics only\n", missionStem.c_str());
    }
    // TOD clock (originally 2026-10-08, sky/lighting/TOD task; slowed down
    // 2026-10-09, v0.3 polish task): a CHOSEN, slowly-advancing fraction-of-
    // day in [0,1), starting at 0.5 (noon) - the original task brief's own
    // suggested default, and visually the closest continuation of the OLD
    // fixed skyblue/headlight placeholders that task replaced.
    // sr3render::kTodSecondsPerDay (sr3render/tod_clock.h; CHOSEN, NOT spec-
    // derived - spec-tables-environment.md Sec18 traces the real TOD-
    // fraction advance/crossfade mechanism in full but never binds it to a
    // real-world-seconds-per-day constant, re-checked directly before
    // choosing this value) was ORIGINALLY 20.0 (a full in-game day every 20
    // REAL seconds) so a short --frames run - including the 300-frame
    // real_playtest_smoke_test.cpp - would actually show the sky/ambient/sun
    // colour and direction vary within a short automated run. The
    // orchestrator flagged that as far too fast for actual human play after
    // reviewing the vehicle-world-collision playtest build, so THIS task
    // changes it to 1440.0 (24 REAL minutes per in-game day - "roughly one
    // game hour per real minute", the orchestrator's own framing) - see
    // tod_clock.h's own top comment for the full citation. The automated
    // smoke test still passes unaffected (it only checks exit code/capture-
    // file existence/the "300 presented" stdout line, never a colour delta -
    // tests/real_playtest_smoke_test.cpp, re-checked directly, not assumed).
    // The new `T` key below (same task) gives a human playtester a way to
    // see a lighting change on demand without waiting out the slower cycle.

    // Real pause_menu_common.vint_doc best-effort load (THIS TASK, see this function's own
    // "PAUSE MENU" doc comment above) - parses the REAL on-disk document through the exact
    // same sr3vintdoc::parseDocument/EngineState::loadVintDocument path `sr3_viewer vint-doc`
    // already uses (tests/real_vintdoc_document_test.cpp exercises the identical pair), purely
    // to exercise the project's existing real-interface/dialog-loading mechanism against this
    // specific real document - its own element/animation counts are logged, but this overlay's
    // own VISIBLE resume/quit text is still the synthetic HudDemoFont/drawHudLabel labels set up
    // above (full bitmap/text-tree rendering of the loaded tree is NOT wired this pass - flagged
    // honestly rather than invented). Tried at most once (pauseDocLoadAttempted), lazily, the
    // first time the menu is actually opened - never on a run where Esc is never pressed.
    sess.tryLoadRealPauseDoc = [&]() {
        pauseDocLoadAttempted = true;
        std::string ifaceDir;
        {
            const size_t slash = charactersPath.find_last_of("/\\");
            ifaceDir = (slash == std::string::npos) ? std::string() : charactersPath.substr(0, slash + 1);
        }
        std::string ifacePath = interfaceArchiveOverride.empty() ? ifaceDir + "interface.vpp_pc" : interfaceArchiveOverride;
        for (auto& ch : ifacePath) if (ch == '\\') ch = '/';
        std::vector<uint8_t> ifaceBytes = readFile(ifacePath);
        if (ifaceBytes.empty()) {
            std::printf("pause    : real pause_menu_common.vint_doc NOT loaded (could not read %s) - "
                        "overlay text is the synthetic label fallback only\n", ifacePath.c_str());
            return;
        }
        try {
            vpp::Container top(vpp::ByteView(ifaceBytes.data(), ifaceBytes.size()));
            for (size_t i = 0; i < top.entries().size(); ++i) {
                if (top.entries()[i].name != "pause_menu_common.str2_pc") continue;
                vpp::Container inner = top.openNested(i);
                for (size_t j = 0; j < inner.entries().size(); ++j) {
                    if (inner.entries()[j].name != "pause_menu_common.vint_doc") continue;
                    vpp::DecompressResult r = inner.decompressEntry(j);
                    if (r.status != vpp::DecodeStatus::Ok) break;
                    sr3vintdoc::Document doc = sr3vintdoc::parseDocument(vpp::ByteView(r.data.data(), r.data.size()));
                    if (hudEs) pauseDocHandle = hudEs->loadVintDocument("pause_menu_common.vint_doc", doc);
                    pauseDocLoaded = true;
                    std::printf("pause    : loaded REAL pause_menu_common.vint_doc from %s (%zu top-level "
                                "elements, %zu animations, %zu metadata entries) via sr3vintdoc::parseDocument"
                                "%s\n",
                                ifacePath.c_str(), doc.elements.size(), doc.animations.size(), doc.metadata.size(),
                                hudEs ? " + EngineState::loadVintDocument()" : " (no --hud/--mission EngineState "
                                "to load into - parsed only)");
                    return;
                }
            }
            std::printf("pause    : pause_menu_common.{str2_pc,vint_doc} not found inside %s - overlay text is "
                        "the synthetic label fallback only\n", ifacePath.c_str());
        } catch (const std::exception& ex) {
            std::printf("pause    : real pause_menu_common.vint_doc load error: %s - overlay text is the "
                        "synthetic label fallback only\n", ex.what());
        }
    };
    return -1;
}

} // namespace sr3viewer
