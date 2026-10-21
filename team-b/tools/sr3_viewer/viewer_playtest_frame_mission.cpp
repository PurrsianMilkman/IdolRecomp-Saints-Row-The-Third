// runPlaytest() split: frame-loop steps - mission live bridge/auto-objective and mission drive.
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

void stepMissionBridge(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& autoObjective = sess.autoObjective;
    auto& autoCombat = sess.autoCombat;
    auto& autoObjectiveFrames = sess.autoObjectiveFrames;
    auto& missionEnabled = sess.missionEnabled;
    auto& bradWeaponDef = *sess.bradWeaponDef_;
    auto& bradWeapon = sess.bradWeapon;
    auto& pm = *sess.pm_;
    auto& hudHost = sess.hudHost;
    auto& hudEs = sess.hudEs;
    auto& hudGp = sess.hudGp;
    auto& acPlayerShots = sess.acPlayerShots;
    auto& acPlayerHits = sess.acPlayerHits;
    auto& acTeleports = sess.acTeleports;
    auto& acHeals = sess.acHeals;
    auto& acKillsDamage = sess.acKillsDamage;
    auto& acKillsFallback = sess.acKillsFallback;
    auto& acHostileOrders = sess.acHostileOrders;
    auto& finalGroundY = sess.finalGroundY;
    auto& inVehicle = sess.inVehicle;
    auto& frames = sess.frames;
    auto& nowMsAccum = sess.nowMsAccum;
    auto& autoObjectiveFired = sess.autoObjectiveFired;
    auto& autoObjectiveVisitedTriggers = sess.autoObjectiveVisitedTriggers;
    auto& autoRoute = sess.autoRoute;
    auto& autoRouteHoldFramesLeft = sess.autoRouteHoldFramesLeft;
    auto& autoRouteHoldAge = sess.autoRouteHoldAge;
    auto& autoRouteHoldPos = sess.autoRouteHoldPos;
    auto& autoVehicleWaitFrames = sess.autoVehicleWaitFrames;
    auto& autoEnterHookWaitFrames = sess.autoEnterHookWaitFrames;
    auto& autoKillPollFrames = sess.autoKillPollFrames;
    auto& autoKilledNames = sess.autoKilledNames;
    auto& autoKillIdleFrames = sess.autoKillIdleFrames;
    auto& autoCombatCfg = *sess.autoCombatCfg_;
    auto& autoCombatState = *sess.autoCombatState_;
    auto& autoCombatSeen = sess.autoCombatSeen;
    auto& autoCombatCounted = sess.autoCombatCounted;
    auto& autoCombatWeaponReady = sess.autoCombatWeaponReady;
    auto& autoCombatSink = *sess.autoCombatSink_;
    const auto& autoKillIntervalFrames = sess.autoKillIntervalFrames;
    auto& autoCallAgeFrames = sess.autoCallAgeFrames;
    auto& autoShieldPollFrames = sess.autoShieldPollFrames;
    auto& autoRcPollFrames = sess.autoRcPollFrames;
    auto& autoShieldDone = sess.autoShieldDone;
    auto& autoRcDone = sess.autoRcDone;
    auto& autoHudScreenWaitFrames = sess.autoHudScreenWaitFrames;
    auto& autoHudScreenPopped = sess.autoHudScreenPopped;
    const auto& autoCallTalkFrames = sess.autoCallTalkFrames;
    auto& lowerStr = sess.lowerStr;
    auto& autoObjectiveNpcMoved = sess.autoObjectiveNpcMoved;
    const auto& focusX = frm.focusX;
    const auto& focusZ = frm.focusZ;
        // MISSION-HOST PHASE 2 (this task) - live position bridge
        // (results/design_mission_host_in_engine_loop_20261009.md §3(c)):
        // writes THIS frame's REAL simulated player position - on foot,
        // pm.st.position; driving the one demo vehicle,
        // vehicleRig.sim->state_'s own position via the SAME focusX/focusZ
        // the camera/streaming-reconcile code just above already computed,
        // at the SAME vehicle pivot height (finalGroundY + kVehPivotHeight)
        // used for rendering/the chase cam - into EngineState's existing
        // "#PLAYER1#" CharacterState::worldPosition, the SAME field
        // get_dist and the other position-shaped natives already read
        // (character_state.h's own doc comment on `worldPosition`) - every
        // frame mission mode is enabled, strictly BEFORE the per-frame
        // scheduler pass below, so a coroutine resumed this frame observes
        // this frame's position, never last frame's.
        //
        // "#PLAYER1#", never the HUD-only lowercase "player" literal the
        // --hud block above hand-seeds for its own two-native probe:
        // Sec29.4's own CONFIRMED pre-resolved local-player name, already
        // true in objectResolves() via applySpecInitialState() (Host's own
        // constructor, before this loop ever runs) - this bridge writes
        // real per-frame data into an ALREADY-real name, it does not invent
        // one. UPDATED 2026-10-09 (spec-zone-data-format.md §11.10 item 4 /
        // spec-lua-api-behaviour.md §4.13, Team A's named-object-resolution
        // synthesis, landed on main after this task started and merged in):
        // "#PLAYER1#" is one of exactly 8 CLOSED, special-cased sentinels
        // the REAL generic resolver (`0x00a280c0`) short-circuits via a
        // literal `.rdata` string compare, resolving straight to "the
        // current player object" WITHOUT ever touching the real engine's
        // own name map - i.e. in the real engine "#PLAYER1#" is always
        // resolvable whenever a local player exists, by construction. That
        // independently confirms this is the right - and in this minimal
        // playtest, the ONLY available - name to bridge live data onto.
        //
        // Deliberately the ONLY live object bridged here. The one demo
        // PlaytestVehicleRig car (vehicleRig, further up this function) has
        // no established Lua-visible identity anywhere in this project -
        // its cmesh filename (vehicleCmeshName) is a CHOSEN display
        // argument for manual on-screen testing, not a mission-script name
        // any real .lua ever references. §11.10 item 2's own closed
        // 4-producer list for how the real engine's name map gets a live
        // entry (zone placement, vehicle cover-node batches, in-memory
        // engine records including group members, co-op-replication
        // replay) does not include "a manually-spawned debug/demo
        // vehicle" either - independently confirming that registering this
        // demo car under any invented name would not just be unconfirmed,
        // it would be modelling a producer the real engine does not have.
        // liveObjectKind()/isLiveObject() on any OTHER name (including any
        // vehicle name a mission script might reference) therefore
        // correctly, honestly returns kNone in this minimal playtest - a
        // real "not live" answer, not a crash and not a fabricated one.
        // See this task's own report for this stated plainly as a current
        // OPEN boundary, not an oversight.
        if (missionEnabled) {
            const double liveY = inVehicle
                                      ? static_cast<double>(finalGroundY) + static_cast<double>(kVehPivotHeight)
                                      : pm.st.position.y;
            hudEs->bridgeLiveCharacterPosition(
                "#PLAYER1#",
                sr3luahost::WorldPosition{static_cast<double>(focusX), liveY, static_cast<double>(focusZ)});
            // Printed once (frame 0 only) - a real, checkable stdout proof that this
            // bridge is actually wired into the live --mission path, for
            // tests/real_playtest_mission_test.cpp's own real-cache coverage (the real
            // mission scripts tried so far all hit an UNRELATED, earlier OPEN gap
            // before ever calling a position-shaped native, per this task's own
            // report - this line is this frame-loop's own direct proof instead of an
            // inferred one).
            if (frames == 0) {
                std::printf("mission  : live position bridge active for '#PLAYER1#' (isLiveObject=%d, pos=%.2f "
                            "%.2f %.2f) - written every frame before the scheduler pass\n",
                            hudEs->isLiveObject("#PLAYER1#") ? 1 : 0, static_cast<double>(focusX), liveY,
                            static_cast<double>(focusZ));
            }

            // --auto-objective (feature/auto-objective-harness, 2026-10-12):
            // one-shot teleport once the SAME get_dist(a,b) pair has been
            // queried for >= autoObjectiveFrames consecutive calls - see this
            // flag's own doc comment above its CLI declaration and
            // EngineState::recordGetDistCall()'s doc comment for the full
            // reasoning (generalizes the m07 spawn-at-barge diagnostic).
            // Placed BEFORE the in-world-trigger overlap check just below so
            // a teleport that lands inside a waited-on trigger volume this
            // SAME frame is picked up by this frame's own overlap test,
            // exactly like a real player who had just walked there.
            if (autoObjective && !autoObjectiveFired) {
                const sr3luahost::EngineState::GetDistStallInfo& stall = hudEs->getDistStallInfo();
                if (stall.repeatCount >= static_cast<uint64_t>(autoObjectiveFrames)) {
                    autoObjectiveFired = true; // fires at most once per run regardless of outcome below
                    const bool aIsPlayer = (stall.nameA == "#PLAYER1#");
                    const bool bIsPlayer = (stall.nameB == "#PLAYER1#");
                    if (!aIsPlayer && !bIsPlayer && stall.knownA && stall.knownB &&
                        hudEs->characters().count(stall.nameA) &&
                        !autoObjectiveNpcMoved.count(stall.nameA + "|" + stall.nameB)) {
                        // fix/auto-objective-driving-2 (b): an NPC-follows-navpoint wait (dlc2_m02's
                        // `while get_dist(homie, nav) > 3 do thread_yield() end` after ai_do_scripted_move).
                        // CHOSEN harness stand-in: write the character's worldPosition (the SAME field get_dist
                        // reads) at the navpoint, through the existing bridge - no AI/navigation behaviour is
                        // invented or changed, the NPC is just placed where the move order points. One-shot per
                        // (npc, target) pair.
                        autoObjectiveNpcMoved.insert(stall.nameA + "|" + stall.nameB);
                        autoObjectiveFired = false; // an NPC placement must not consume the player-teleport one-shot
                        hudEs->bridgeLiveCharacterPosition(stall.nameA, stall.posB);
                        std::printf("auto-objective: stalled %llu calls on get_dist('%s','%s') - placed NPC '%s' at "
                                    "'%s' (%.2f, %.2f, %.2f) [CHOSEN test-harness stand-in, not real AI navigation]\n",
                                    static_cast<unsigned long long>(stall.repeatCount), stall.nameA.c_str(),
                                    stall.nameB.c_str(), stall.nameA.c_str(), stall.nameB.c_str(), stall.posB.x,
                                    stall.posB.y, stall.posB.z);
                    } else if (aIsPlayer == bIsPlayer) {
                        // Neither name is the player sentinel, or (degenerate) both are -
                        // no trackable target; honestly do nothing rather than guessing.
                        std::printf("auto-objective: stalled %llu calls on get_dist('%s','%s') but neither name is "
                                    "'#PLAYER1#' - no trackable target, not teleporting\n",
                                    static_cast<unsigned long long>(stall.repeatCount), stall.nameA.c_str(), stall.nameB.c_str());
                    } else {
                        const std::string& targetName = aIsPlayer ? stall.nameB : stall.nameA;
                        const sr3luahost::WorldPosition& targetPos = aIsPlayer ? stall.posB : stall.posA;
                        const bool targetKnown = aIsPlayer ? stall.knownB : stall.knownA;
                        if (!targetKnown) {
                            std::printf("auto-objective: stalled %llu calls on get_dist('%s','%s') but target '%s' has "
                                        "no resolved position (tryResolveWorldPositionFromZoneData() returned false) - "
                                        "not inventing a fallback, not teleporting\n",
                                        static_cast<unsigned long long>(stall.repeatCount), stall.nameA.c_str(),
                                        stall.nameB.c_str(), targetName.c_str());
                        } else {
                            pm.warp(sr3player::Vec3{targetPos.x, targetPos.y, targetPos.z});
                            autoRouteHoldFramesLeft = 0; // a get_dist teleport supersedes any trigger hold
                            hudEs->bridgeLiveCharacterPosition("#PLAYER1#", targetPos);
                            std::printf("auto-objective: stalled %llu calls on get_dist('%s','%s') - teleported player "
                                        "to '%s' at (%.2f, %.2f, %.2f) [CHOSEN test-harness stand-in, not real "
                                        "navigation - see --auto-objective's own doc comment]\n",
                                        static_cast<unsigned long long>(stall.repeatCount), stall.nameA.c_str(),
                                        stall.nameB.c_str(), targetName.c_str(), targetPos.x, targetPos.y, targetPos.z);
                        }
                    }
                }
            }

            // IN-WORLD TRIGGERS (this task, 2026-10-10): the real per-frame
            // overlap test (spec-lua-api-behaviour.md Sec1.14) against the
            // live player's own just-updated position, firing registered
            // on_trigger/on_trigger_exit callbacks via BareThreadTable::
            // allocate() - the SAME coroutine-spawn primitive commit
            // 0b6c934 already wired for tools/lua_host_run.cpp's own
            // --probe-autopilot path (spawn-only, no immediate run(); the
            // scheduler pass just below this block is what actually resumes
            // it, matching the real spawn-now/run-later dispatch timing).
            //
            // Auto-register geometry, lazily, for every trigger name this
            // session has seen an on_trigger()/on_trigger_exit() call for so
            // far but has no geometry for yet: resolve its real position
            // from the zone data just loaded above (sr3zone::ObjectPosition())
            // AND, as of this task (2026-10-10), its REAL on-disk shape/
            // extent/radius too (spec-lua-api-behaviour.md Sec1.14's own
            // "On-disk gaps - RESOLVED 2026-10-10" paragraph) via sr3zone's
            // TriggerShapeType()/TriggerShapeParam1()/TriggerShapeParam2()/
            // TriggerShapeCodeForTypeString() - see those functions' own doc
            // comments (include/sr3zone/zone_objects.h) for the exact
            // on-disk encoding and the real population-scan-grounded string
            // mapping this uses. The CHOSEN sphere/radius=3.0 stand-in this
            // block used before this task is now only a FALLBACK, for a
            // trigger whose record has no decodable shape (no
            // trigger_shape_type property, an unmapped shape string, or a
            // missing/wrong-width param - never silently invented beyond
            // that same narrow fallback this project already used). A
            // trigger whose name does not resolve against real zone data at
            // all still gets no geometry and is simply never tested -
            // reported once, not silently.
            {
                auto tryAutoRegister = [&](const std::string& triggerName) {
                    if (hudEs->triggerGeometry(triggerName)) return; // already registered
                    std::string note;
                    if (!hudEs->registerTriggerGeometryFromZoneData(triggerName, &note)) {
                        // Re-tried next frame (cheap: one map lookup); a miss can never silently become a hit.
                        std::printf("mission  : in-world trigger '%s' has a registered callback but no real "
                                    "zone-data position was found - not tested for overlap this session\n",
                                    triggerName.c_str());
                        return;
                    }
                    std::printf("mission  : %s\n", note.c_str());
                };
                for (const auto& kv : hudEs->onTriggerCallbacksMap()) tryAutoRegister(kv.first);
                for (const auto& kv : hudEs->onTriggerExitCallbacksMap()) tryAutoRegister(kv.first);

                for (const auto& kv : hudEs->triggerGeometryMap()) {
                    const std::string& triggerName = kv.first;
                    const sr3luahost::EngineState::TriggerVolumeGeometry& g = kv.second;
                    const bool nowInside = sr3luahost::EngineState::TriggerVolumeContainsPoint(
                        g, static_cast<double>(focusX), liveY, static_cast<double>(focusZ));
                    bool& wasInside = hudEs->triggerCurrentlyInside(triggerName);
                    // spec-lua-api-behaviour.md V.4 (FINAL, bfe0542): the overlap phantom exists only while the
                    // trigger is enabled, so edges are detected only then; disabling already emitted EXIT for a
                    // human inside (EngineState::applyTriggerEnableTransition, from trigger_enable).
                    if (hudEs->triggerEnableKnown(triggerName) && !hudEs->triggerEnableValue(triggerName)) continue;
                    // Rule (a): registering on_trigger while already inside an ENABLED trigger does not fire - the
                    // phantom already tracked that human. A fresh phantom (enable of a disabled trigger) clears
                    // this baseline in applyTriggerEnableTransition, so rule (b) still fires on the next pass.
                    bool& overlapBaselined = hudEs->triggerOverlapBaselined(triggerName);
                    if (!overlapBaselined) {
                        overlapBaselined = true;
                        wasInside = nowInside;
                        if (nowInside) hudEs->triggerEverEntered(triggerName) = true;
                        continue;
                    }
                    if (nowInside == wasInside) continue; // no transition - the real poll's own "only on a transition" rule
                    wasInside = nowInside;
                    if (nowInside) hudEs->triggerEverEntered(triggerName) = true; // feature/auto-objective-trigger-vehicle-seat
                    const std::string& cbName = nowInside ? hudEs->onTriggerCallbackValue(triggerName)
                                                           : hudEs->onTriggerExitCallbackValue(triggerName);
                    const char* edge = nowInside ? "ENTER" : "EXIT";
                    if (cbName.empty()) {
                        std::printf("mission  : in-world trigger '%s' %s transition, but no %s callback is "
                                    "registered - nothing to dispatch\n",
                                    triggerName.c_str(), edge, nowInside ? "on_trigger" : "on_trigger_exit");
                        continue;
                    }
                    // 2026-10-11 fix (m09/m13/m05 trigger-stall task): every real
                    // on_trigger/on_trigger_exit callback this project's own mission
                    // scripts register (m09_player_on_boat_enter_cb, m13_trigger_cback,
                    // m05's own land-trigger callback, etc.) declares a first parameter
                    // ("human") and either compares it against LOCAL_PLAYER/REMOTE_PLAYER
                    // (an identity gate) or, for m05, ignores it and sets its flag
                    // unconditionally. Previously this call always spawned with
                    // nargs=0, so `human` was unconditionally nil inside every one of
                    // these callbacks - a reimplementation-side bug independent of and
                    // prior to either of spec-lua-api-behaviour.md Sec1.14's two
                    // hypotheses (identity-mismatch / dispatch-chain-interception): the
                    // callback was reached (this project's own dispatch has no 6-way
                    // type-tester chain to misroute through - CHOSEN, not modeled, see
                    // Sec1.14), but its own first argument was always missing. Fixed by
                    // pushing this project's one real player-identity sentinel
                    // ("#PLAYER1#", see EngineState's own existing "#PLAYER1#"/"#PLAYER2#"
                    // convention) as that argument - the playtest harness only ever
                    // drives the LOCAL player's own position, so every trigger
                    // enter/exit transition detected here is the local player's own.
                    // LOCAL_PLAYER/REMOTE_PLAYER are registered as the matching Lua
                    // globals in the gameplay state (src/lua_host.cpp) so the identity
                    // comparison in m09/m13's callbacks has a real, non-nil value on
                    // both sides.
                    // Arg 1 "#PLAYER1#" (== LOCAL_PLAYER), arg 2 the trigger's name string exactly as passed to
                    // on_trigger (spec V.4 FINAL: exactly 2 string arguments). Fired through the engine callback
                    // spawner: a new script thread, a missing global or a full table silently loses the event.
                    const bool spawnedOk = hudEs->fireEngineCallback(
                        cbName,
                        {sr3luahost::CallbackArg::str("#PLAYER1#"), sr3luahost::CallbackArg::str(triggerName)},
                        nowInside ? "on_trigger" : "on_trigger_exit");
                    if (!spawnedOk) {
                        std::printf("mission  : in-world trigger '%s' %s transition - coroutine for '%s' NOT "
                                    "spawned (no such global function, or script-thread table full - silently lost per spec)\n",
                                    triggerName.c_str(), edge, cbName.c_str());
                    } else {
                        std::printf("mission  : in-world trigger '%s' %s transition - spawned coroutine for '%s', "
                                    "resumed by this frame's own scheduler pass below\n",
                                    triggerName.c_str(), edge, cbName.c_str());
                    }
                }

                // --auto-objective (feature/auto-objective-trigger-vehicle-seat,
                // 2026-10-12): the on_trigger analogue of the get_dist stall
                // teleport above - a mission script waiting on a trigger
                // ENTER that never fires (m13.lua's own m13_goto_a_trigger,
                // results/verify_combined_with_seatedpose_20261010's own
                // stall-point dump) makes no get_dist call at all, so it is
                // invisible to that tracker. Reuses the SAME autoObjective
                // flag/autoObjectiveFrames threshold (this task's own CHOSEN
                // "keep it simple" call, per the brief) but fires
                // INDEPENDENTLY (autoObjectiveTriggerFired, its own one-shot
                // latch) - a single run could conceivably stall on BOTH
                // shapes at different points, and each should get its own
                // one-shot teleport. Candidate selection: the EARLIEST-
                // registered trigger that has geometry but has never
                // produced an ENTER yet (triggerRegistrationOrder()/
                // triggerEverEntered()) - the trigger this harness has been
                // "waiting on" the longest, matching a real player's own
                // likely next destination better than an arbitrary pending
                // trigger.
                // fix/auto-objective-driving (2026-10-13): REPEATING and target-aware (was a one-shot on the
                // earliest never-entered trigger, which for m19 was the wrong trigger and for dlc2_m02-style
                // object_is_in_trigger polling was never the polled one). CHOSEN harness heuristics, in order:
                //  (1) a trigger the script POLLED for the live player via object_is_in_trigger this frame
                //      (the latest such query wins - that is what the script is waiting on right now);
                //  (2) else the EARLIEST-registered trigger with a non-empty on_trigger callback, not disabled by
                //      trigger_enable(false), not currently inside, and not already auto-visited.
                // The target must stay the same for autoObjectiveFrames consecutive frames before the teleport
                // (recordTriggerWaitFrame resets on a name change); each trigger is visited at most once, so a
                // mission with several sequential trigger waits is driven through them one after another.
                if (autoObjective && autoRouteHoldFramesLeft > 0) {
                    // HOLD in the trigger volume (m22: exit callback / flag reset must not see the player leave).
                    --autoRouteHoldFramesLeft;
                    ++autoRouteHoldAge;
                    // spec-mission-packages.md step-kinds table: "Entry is edge-triggered: if the player is already
                    // inside when the script resets its flag (m22 line 1591), step out and back in." CHOSEN: one
                    // out-and-back pulse (3 frames out) per hold, 240 frames after the teleport.
                    sr3luahost::WorldPosition holdAt = autoRouteHoldPos;
                    if (autoRouteHoldAge >= 240 && autoRouteHoldAge < 243) holdAt.x += 300.0;
                    pm.warp(sr3player::Vec3{holdAt.x, holdAt.y, holdAt.z});
                    hudEs->bridgeLiveCharacterPosition("#PLAYER1#", holdAt);
                }
                if (autoObjective) {
                    // feature/route-table-driving: enter-vehicle waits. A script polling
                    // character_is_in_vehicle(#PLAYER1#[, V]) negatively for autoObjectiveFrames consecutive
                    // frames gets the player seated via the REAL vehicle_enter_do native (instant), trying the vehicle
                    // the script named, then the route table's named vehicles, then any vehicle object known to the
                    // host. CHOSEN candidate order; the on_vehicle_enter callback is fired by the engine's own seating
                    // commit inside vehicle_enter_do (spec V.1), not by this block.
                    const std::vector<std::string> vq = hudEs->drainVehicleWaitQueries();
                    if (vq.empty()) autoVehicleWaitFrames = 0; else ++autoVehicleWaitFrames;
                    if (autoVehicleWaitFrames >= autoObjectiveFrames) {
                        autoVehicleWaitFrames = 0;
                        std::vector<std::string> cands;
                        for (const auto& q : vq) if (!q.empty()) cands.push_back(q);
                        for (const auto& v : autoRoute.vehicles) cands.push_back(v);
                        for (const auto& kv : hudEs->vehicles()) cands.push_back(kv.first);
                        bool seated = false;
                        std::string seatedName;
                        for (const std::string& vn : cands) {
                            std::string esc;
                            for (char ch : vn) { if (ch == '\'' || ch == '\\') esc += '\\'; esc += ch; }
                            hudHost->runChunk(hudGp, "vehicle_enter_do(LOCAL_PLAYER, '" + esc + "', true)", "playtest_auto_route_seat.lua");
                            if (hudEs->getOrCreateCharacter("#PLAYER1#").stateEnum.get() == 3) { seated = true; seatedName = vn; break; }
                        }
                        std::string candList;
                        for (const auto& c : cands) candList += (candList.empty() ? "" : ", ") + c;
                        std::printf("auto-objective: stalled on character_is_in_vehicle(#PLAYER1#) - %s%s (tried %zu candidates: %s) "
                                    "[CHOSEN harness stand-in via real vehicle_enter_do]\n",
                                    seated ? "seated in " : "could not seat any vehicle", seatedName.c_str(), cands.size(),
                                    candList.c_str());
                    }
                }
                if (autoObjective) {
                    // ===== engine callback waits (spec-lua-api-behaviour.md "Engine callback dispatch", 2026-10-14) =====
                    // The engine now FIRES on_vehicle_enter / on_vehicle_exit / on_death / on_human_killed /
                    // on_vehicle_destroyed / cellphone cb1+cb2 through the real paths (the seating commit in
                    // vehicle_enter_do, the death finalizer after character_kill, ...). What the harness still has to
                    // supply is the PLAYER ACTION a real player would perform. Each assist below drives a REAL native
                    // (vehicle_enter_do, character_kill) or a real engine event hook; all are CHOSEN harness stand-ins
                    // behind --auto-objective, never engine behaviour.
                    {   // (1) on_vehicle_enter wait: a hook is registered but the player is not seated.
                        sr3luahost::CharacterState& pc = hudEs->getOrCreateCharacter("#PLAYER1#");
                        const bool playerSeated = pc.stateEnum.known() && pc.stateEnum.get() == 3;
                        std::vector<std::string> hookVehicles;
                        for (const auto& kv : hudEs->vehicles())
                            if (!kv.second.onVehicleEnterCallback.empty() && kv.second.seatOccupants.empty())
                                hookVehicles.push_back(kv.first);
                        std::sort(hookVehicles.begin(), hookVehicles.end());
                        bool playerHook = !pc.onVehicleEnterGenericSlotCallback.empty();
                        if (!playerHook && hudEs->hasCharacter("#PLAYER#"))
                            playerHook = !hudEs->getOrCreateCharacter("#PLAYER#").onVehicleEnterGenericSlotCallback.empty();
                        if (!playerSeated && (!hookVehicles.empty() || playerHook)) ++autoEnterHookWaitFrames;
                        else autoEnterHookWaitFrames = 0;
                        if (autoEnterHookWaitFrames >= autoObjectiveFrames) {
                            autoEnterHookWaitFrames = 0;
                            std::vector<std::string> cands = hookVehicles;
                            if (playerHook) {
                                for (const auto& v : autoRoute.vehicles) cands.push_back(v);
                                std::vector<std::string> known;
                                for (const auto& kv : hudEs->vehicles()) known.push_back(kv.first);
                                std::sort(known.begin(), known.end());
                                for (const auto& v : known) cands.push_back(v);
                            }
                            bool seated = false;
                            std::string seatedName;
                            for (const std::string& vn : cands) {
                                std::string esc;
                                for (char ch : vn) { if (ch == '\'' || ch == '\\') esc += '\\'; esc += ch; }
                                hudHost->runChunk(hudGp, "vehicle_enter_do(LOCAL_PLAYER, '" + esc + "', true)", "playtest_auto_enter_hook_seat.lua");
                                if (pc.stateEnum.known() && pc.stateEnum.get() == 3) { seated = true; seatedName = vn; break; }
                            }
                            std::printf("auto-objective: on_vehicle_enter registered but player not seated - %s%s (%zu candidates) "
                                        "[CHOSEN harness player action via real vehicle_enter_do; the enter callback is then fired by the "
                                        "engine's seating commit]\n",
                                        seated ? "seated in " : "could not seat any vehicle", seatedName.c_str(), cands.size());
                        }
                    }
                    {   // (2) kill waits: character_kill (the real native) on a hostile mission target.
                        const std::vector<std::string> dq = hudEs->drainDeathPollQueries();
                        std::unordered_set<std::string> polledNow(dq.begin(), dq.end());
                        for (auto it = autoKillPollFrames.begin(); it != autoKillPollFrames.end();) {
                            if (!polledNow.count(it->first)) it = autoKillPollFrames.erase(it); else ++it;
                        }
                        for (const std::string& n : polledNow) ++autoKillPollFrames[n];
                        // CHOSEN: "hostile" = handed an ai_add_enemy_target order toward the player, or an authored
                        // team other than the player's own side / civilians. Allies and unknown-team characters are
                        // never auto-killed (a script may FAIL on their death).
                        auto hostile = [&](const std::string& n) {
                            auto cit = hudEs->characters().find(n);
                            if (cit == hudEs->characters().end()) return false;
                            const sr3luahost::CharacterState& cs = cit->second;
                            if (sr3luahost::EngineState::characterReadsDead(cs)) return false;
                            if (n.rfind("#PLAYER", 0) == 0) return false;
                            if (cs.hostileTargetOrderSeen) return true;
                            if (cs.teamName.empty()) return false;
                            const std::string t = lowerStr(cs.teamName);
                            for (const char* allied : {"saint", "player", "playa", "friend", "ally", "civil"})
                                if (t.find(allied) != std::string::npos) return false;
                            return true;
                        };
                        if (autoCombat) {
                            // ===== --auto-combat (CHOSEN test-harness assist; see sr3luahost/auto_combat.h) =====
                            // Awaited targets = the same hostile on_death / on_human_killed / polled-dead set the instant
                            // kill uses, but they are engaged instead of killed.
                            std::vector<sr3luahost::autocombat::Target> acTargets;
                            for (const std::string& n : hudEs->characterCreationOrder()) {
                                if (autoKilledNames.count(n) || !hostile(n)) continue;
                                auto cit = hudEs->characters().find(n);
                                if (cit == hudEs->characters().end()) continue;
                                const bool polled = autoKillPollFrames.count(n) && autoKillPollFrames[n] >= autoObjectiveFrames;
                                if (cit->second.onDeathCallback.empty() && hudEs->onHumanKilledCallback().empty() && !polled) continue;
                                if (!hudEs->tryResolveWorldPositionFromZoneData(n, hudEs->getOrCreateCharacter(n).worldPosition)) continue;
                                const sr3luahost::WorldPosition wp = hudEs->getOrCreateCharacter(n).worldPosition.get();
                                acTargets.push_back({n, wp.x, wp.z});
                                autoCombatSeen.insert(n);
                            }
                            // kills through the normal death path: a once-targeted character now reads dead, not via fallback.
                            for (const std::string& n : autoCombatSeen) {
                                if (autoCombatCounted.count(n)) continue;
                                auto cit = hudEs->characters().find(n);
                                if (cit != hudEs->characters().end() && sr3luahost::EngineState::characterReadsDead(cit->second)) {
                                    autoCombatCounted.insert(n);
                                    if (!autoKilledNames.count(n)) ++acKillsDamage;
                                }
                            }
                            const sr3luahost::WorldPosition ppos = hudEs->getOrCreateCharacter("#PLAYER1#").worldPosition.known()
                                ? hudEs->getOrCreateCharacter("#PLAYER1#").worldPosition.get() : sr3luahost::WorldPosition{};
                            const sr3luahost::autocombat::Decision dec = sr3luahost::autocombat::decide(
                                autoCombatState, autoCombatCfg, ppos.x, ppos.z, acTargets,
                                static_cast<int>(acKillsDamage + acKillsFallback));
                            // CHOSEN: an awaited target the player is in combat range of, whose host-side facts do not already
                            // make it hostile (team relation matrix / enemy-target order), is given the enemy-target order
                            // against the player that a hostile mission script would (EngineState enemyTargets record, the
                            // state ai_add_enemy_target writes). Without it the real NPC AI never fights targets the host has
                            // no hostility fact for (m06/m08/m21 targets read non-hostile to tickNpcAi).
                            for (const auto& t : acTargets) {
                                const double dx = t.x - ppos.x, dz = t.z - ppos.z;
                                if (dx * dx + dz * dz > autoCombatCfg.combatRange * autoCombatCfg.combatRange) continue;
                                sr3luahost::CharacterState& tcs = hudEs->getOrCreateCharacter(t.name);
                                if (tcs.enemyTargets.count("#PLAYER1#")) continue;
                                tcs.enemyTargets["#PLAYER1#"] = sr3luahost::EnemyTargetRecord{};
                                ++acHostileOrders;
                            }
                            if (dec.action == sr3luahost::autocombat::Action::Teleport) {
                                ++acTeleports;
                                double ty = ppos.y;
                                for (const auto& t : acTargets) if (t.name == dec.target) ty = hudEs->getOrCreateCharacter(t.name).worldPosition.get().y;
                                const sr3luahost::WorldPosition dst{dec.tx, ty, dec.tz};
                                pm.warp(sr3player::Vec3{dst.x, dst.y, dst.z});
                                autoRouteHoldFramesLeft = 0;
                                hudEs->bridgeLiveCharacterPosition("#PLAYER1#", dst);
                                std::printf("auto-combat: teleported player to (%.1f, %.1f, %.1f), %.0f m standoff from target '%s' "
                                            "[CHOSEN test-harness stand-in for walking there]\n",
                                            dst.x, dst.y, dst.z, autoCombatCfg.standoff, dec.target.c_str());
                            } else if (dec.action == sr3luahost::autocombat::Action::Fire && bradWeapon) {
                                if (!autoCombatWeaponReady) {
                                    autoCombatWeaponReady = true;
                                    bradWeapon->setInfiniteAmmo(true);  // CHOSEN: the harness never reloads
                                    std::printf("auto-combat: player weapon '%s' (real weapons.xtbl row), damage per hit %.1f (CHOSEN: "
                                                "Damage_Max NPC value when authored), infinite ammo [CHOSEN]\n",
                                                bradWeaponDef.name.c_str(), autoCombatSink.amount());
                                }
                                std::vector<sr3weapons::HitscanTarget> ht;
                                for (const auto& t : acTargets) {
                                    const sr3luahost::WorldPosition wp = hudEs->getOrCreateCharacter(t.name).worldPosition.get();
                                    sr3weapons::HitscanTarget h;
                                    h.name = t.name;
                                    h.center = sr3player::Vec3{wp.x, wp.y + 0.9, wp.z};
                                    ht.push_back(h);
                                }
                                // CHOSEN aim: perfect aim at the chosen target's torso box, eye 1.6 m above the player; no occlusion test.
                                const sr3player::Vec3 org{ppos.x, ppos.y + 1.6, ppos.z};
                                sr3player::Vec3 dir{0, 0, 1};
                                for (const auto& h : ht)
                                    if (h.name == dec.target) dir = sr3player::Vec3{h.center.x - org.x, h.center.y - org.y, h.center.z - org.z};
                                const double maxRange = bradWeaponDef.rangeMax.present ? static_cast<double>(bradWeaponDef.rangeMax.value) : 1000.0;
                                const auto outcome = bradWeapon->fire(static_cast<double>(nowMsAccum), org, dir, maxRange, ht, &autoCombatSink);
                                if (outcome.reachedItemUse) {
                                    ++acPlayerShots;
                                    hudEs->npcAiPlayerFiringThisFrame = true;  // NPCs notice gunfire, same as the live LMB path
                                }
                                if (outcome.hitDetected) ++acPlayerHits;
                                if (acPlayerShots <= 3 || acPlayerShots % 25 == 0)
                                    std::printf("auto-combat: shot #%llu at '%s' reached=%d hit=%d hitTarget='%s' (sink: applied=%llu dmg=%.0f killsByHit=%llu refused=%llu)\n",
                                                static_cast<unsigned long long>(acPlayerShots), dec.target.c_str(), outcome.reachedItemUse,
                                                outcome.hitDetected, outcome.hitTargetName.c_str(),
                                                static_cast<unsigned long long>(autoCombatSink.applied), autoCombatSink.damageDealt,
                                                static_cast<unsigned long long>(autoCombatSink.killsByHit),
                                                static_cast<unsigned long long>(autoCombatSink.refused));
                            } else if (dec.action == sr3luahost::autocombat::Action::Fallback) {
                                autoKilledNames.insert(dec.target);
                                ++acKillsFallback;
                                std::string esc;
                                for (char ch : dec.target) { if (ch == '\'' || ch == '\\') esc += '\\'; esc += ch; }
                                hudHost->runChunk(hudGp, "character_kill('" + esc + "')", "playtest_auto_kill.lua");
                                std::printf("auto-combat: FALLBACK character_kill('%s') after %d frames with no progress [CHOSEN timeout]\n",
                                            dec.target.c_str(), autoCombatCfg.fallbackFrames);
                            }
                            // CHOSEN harness god mode: keep the player's hit points at max (only when the host already holds
                            // a value, so nothing is invented) so NPC fire cannot fail the mission.
                            if (!acTargets.empty() || acPlayerShots > 0) {
                                sr3luahost::CharacterState& pcs = hudEs->getOrCreateCharacter("#PLAYER1#");
                                if (pcs.currentHitPoints.known() && pcs.maxHitPoints.known() && pcs.maxHitPoints.get() > 0 &&
                                    pcs.currentHitPoints.get() < pcs.maxHitPoints.get()) {
                                    pcs.currentHitPoints.set(pcs.maxHitPoints.get());
                                    ++acHeals;
                                }
                            }
                        }
                        std::string killTarget;
                        std::string killWhy;
                        // (a) polled with character_is_dead for >= N consecutive frames.
                        for (const std::string& n : dq) {
                            if (autoCombat) break;  // --auto-combat engages targets instead (block above)
                            if (autoKilledNames.count(n)) continue;
                            auto pit = autoKillPollFrames.find(n);
                            if (pit != autoKillPollFrames.end() && pit->second >= autoObjectiveFrames && hostile(n)) {
                                killTarget = n;
                                killWhy = "polled character_is_dead";
                                break;
                            }
                        }
                        // (b) a hostile character with an on_death / on_human_killed registration outstanding.
                        bool outstanding = false;
                        if (killTarget.empty() && !autoCombat) {
                            for (const std::string& n : hudEs->characterCreationOrder()) {
                                if (autoKilledNames.count(n)) continue;
                                auto cit = hudEs->characters().find(n);
                                if (cit == hudEs->characters().end()) continue;
                                if (cit->second.onDeathCallback.empty() && hudEs->onHumanKilledCallback().empty()) continue;
                                if (!hostile(n)) continue;
                                outstanding = true;
                                if (autoKillIdleFrames >= autoKillIntervalFrames) {
                                    killTarget = n;
                                    killWhy = cit->second.onDeathCallback.empty() ? "on_human_killed registered" : "on_death registered";
                                }
                                break;
                            }
                        }
                        if (outstanding) ++autoKillIdleFrames; else autoKillIdleFrames = 0;
                        if (!killTarget.empty()) {
                            autoKilledNames.insert(killTarget);
                            autoKillIdleFrames = autoKillIntervalFrames - 60; // CHOSEN: next kill 60 frames later
                            std::string esc;
                            for (char ch : killTarget) { if (ch == '\'' || ch == '\\') esc += '\\'; esc += ch; }
                            hudHost->runChunk(hudGp, "character_kill('" + esc + "')", "playtest_auto_kill.lua");
                            killWhy += " team='" + hudEs->getOrCreateCharacter(killTarget).teamName + "' hostileOrder=" +
                                       std::to_string(hudEs->getOrCreateCharacter(killTarget).hostileTargetOrderSeen ? 1 : 0);
                            std::printf("auto-objective: kill wait - character_kill('%s') (%s) [CHOSEN harness assist via real "
                                        "character_kill; on_human_killed/on_death are then fired by the engine's death "
                                        "finalizer at the next per-human update]\n",
                                        killTarget.c_str(), killWhy.c_str());
                        }
                    }
                    {   // (2b) player-action events a native waits on that the harness cannot perform: the spec says
                        // each needs the player to do something ("A harness that wants to progress must simulate ...").
                        // human shield (spec Sec13.14): "player grabs X" = beginHumanShield (0x009acb20), holder 2 -> 1.
                        std::unordered_set<std::string> shieldNow;
                        for (const auto& pq : hudEs->drainShieldPollQueries()) shieldNow.insert(pq.first + "|" + pq.second);
                        for (auto it = autoShieldPollFrames.begin(); it != autoShieldPollFrames.end();) {
                            if (!shieldNow.count(it->first)) it = autoShieldPollFrames.erase(it); else ++it;
                        }
                        for (const auto& key : shieldNow) {
                            if (++autoShieldPollFrames[key] < autoObjectiveFrames || autoShieldDone.count(key)) continue;
                            autoShieldDone.insert(key);
                            const size_t bar = key.find('|');
                            const std::string holder = key.substr(0, bar), hostage = key.substr(bar + 1);
                            hudEs->beginHumanShield(holder, hostage);
                            hudEs->getOrCreateCharacter(holder).humanShieldStateByte18d4 = 1;
                            std::printf("auto-objective: character_has_specific_human_shield('%s','%s') polled false for %d frames - "
                                        "simulated 'player grabs it as a human shield' (beginHumanShield, state 2 -> 1) [CHOSEN harness "
                                        "player action; the native's value then follows the engine's gate]\n",
                                        holder.c_str(), hostage.c_str(), autoObjectiveFrames);
                        }
                        // RC possession (spec Sec18.10): "the harness must simulate the possession event".
                        std::unordered_set<std::string> rcNow;
                        for (const auto& pq : hudEs->drainRcDriverPollQueries()) if (!pq.second.empty()) rcNow.insert(pq.first + "|" + pq.second);
                        for (auto it = autoRcPollFrames.begin(); it != autoRcPollFrames.end();) {
                            if (!rcNow.count(it->first)) it = autoRcPollFrames.erase(it); else ++it;
                        }
                        for (const auto& key : rcNow) {
                            if (++autoRcPollFrames[key] < autoObjectiveFrames || autoRcDone.count(key)) continue;
                            autoRcDone.insert(key);
                            const size_t bar = key.find('|');
                            const std::string chr = key.substr(0, bar), veh = key.substr(bar + 1);
                            const bool began = hudEs->beginRcControl(veh, chr);
                            std::printf("auto-objective: player_is_rc_driver('%s','%s') polled false for %d frames - simulated RC "
                                        "possession (beginRcControl -> %s) [CHOSEN harness player action]\n",
                                        chr.c_str(), veh.c_str(), autoObjectiveFrames, began ? "controller set" : "refused");
                        }
                        // HUD screen pop (spec Sec24.24): a registered msn_text_adventure exit hook needs the player to
                        // finish or exit the screen; the harness pops it (the engine then calls the stored callback).
                        if (!hudEs->namedHookCallback("on_hud_screen_exit:msn_text_adventure").empty()) {
                            if (++autoHudScreenWaitFrames >= autoObjectiveFrames && !autoHudScreenPopped) {
                                autoHudScreenPopped = true;
                                hudEs->popHudScreen("msn_text_adventure", "#PLAYER1#");
                                std::printf("auto-objective: on_hud_screen_exit('msn_text_adventure') registered for %d frames - popped "
                                            "the screen (exit hook called with (\"msn_text_adventure\", \"#PLAYER1#\")) [CHOSEN harness "
                                            "player action]\n", autoObjectiveFrames);
                            }
                        } else {
                            autoHudScreenWaitFrames = 0;
                            autoHudScreenPopped = false;
                        }
                    }
                    {   // (3) mission cellphone calls: answer, then end after a fixed talk time (spec V.3: the dialogue
                        // duration source is OPEN, so the call end is a harness event, never decided by the engine here).
                        std::vector<std::string> ids;
                        for (const auto& kv : hudEs->missionCellphoneAudioMap()) if (kv.second.live) ids.push_back(kv.first);
                        std::sort(ids.begin(), ids.end());
                        for (const std::string& id : ids) {
                            int& age = autoCallAgeFrames[id];
                            ++age;
                            const auto& rec = hudEs->missionCellphoneAudioMap().at(id);
                            if (!rec.answered && age >= autoObjectiveFrames) {
                                hudEs->answerMissionCellphoneCall(id);
                                std::printf("auto-objective: mission cellphone call '%s' answered (cb1 '%s') [CHOSEN harness event]\n",
                                            id.c_str(), rec.hookSlot1.c_str());
                            } else if (rec.answered && age >= autoObjectiveFrames + autoCallTalkFrames) {
                                std::string cb2 = rec.hookSlot2;
                                hudEs->endMissionCellphoneCall(id);
                                std::printf("auto-objective: mission cellphone call '%s' ended after %d frames of dialogue (cb2 '%s') "
                                            "[CHOSEN harness event; real dialogue length is OPEN in the spec]\n",
                                            id.c_str(), autoCallTalkFrames, cb2.c_str());
                            }
                        }
                    }
                }
                if (autoObjective) {
                    std::string pendingTrigger;
                    const std::vector<std::string> polled = hudEs->drainTriggerPollQueries();
                    for (auto it = polled.rbegin(); it != polled.rend(); ++it) {
                        if (autoObjectiveVisitedTriggers.count(*it)) continue;
                        if (autoRoute.hazards.count(lowerStr(*it))) continue; // spec hazard trigger - never enter
                        { auto ab = autoRoute.altBlockedUntilFrame.find(lowerStr(*it));
                          if (ab != autoRoute.altBlockedUntilFrame.end() && frames < ab->second) continue; }
                        if (!hudEs->triggerGeometry(*it)) hudEs->registerTriggerGeometryFromZoneData(*it);
                        if (hudEs->triggerGeometry(*it) && !hudEs->triggerCurrentlyInside(*it)) { pendingTrigger = *it; break; }
                    }
                    if (pendingTrigger.empty()) {
                        // feature/route-table-driving: among the eligible registered triggers prefer the one that
                        // comes EARLIEST in the mission's route table; spec hazard triggers are never eligible.
                        // Triggers not in the route fall back to registration order (legacy heuristic).
                        size_t bestRouteIdx = static_cast<size_t>(-1);
                        std::string firstNonRoute;
                        for (const std::string& name : hudEs->triggerRegistrationOrder()) {
                            if (autoObjectiveVisitedTriggers.count(name)) continue;
                            if (autoRoute.hazards.count(lowerStr(name))) continue;
                            if (hudEs->onTriggerCallbackValue(name).empty()) continue;
                            if (hudEs->triggerEnableKnown(name) && !hudEs->triggerEnableValue(name)) continue;
                            if (hudEs->triggerCurrentlyInside(name)) continue;
                            const std::string ln = lowerStr(name);
                            { auto ab = autoRoute.altBlockedUntilFrame.find(ln);
                              if (ab != autoRoute.altBlockedUntilFrame.end() && frames < ab->second) continue; }
                            size_t idx = static_cast<size_t>(-1);
                            for (size_t ri = 0; ri < autoRoute.triggers.size(); ++ri)
                                if (autoRoute.triggers[ri] == ln) { idx = ri; break; }
                            if (idx != static_cast<size_t>(-1)) {
                                if (bestRouteIdx == static_cast<size_t>(-1) || idx < bestRouteIdx) { bestRouteIdx = idx; pendingTrigger = name; }
                            } else if (firstNonRoute.empty()) {
                                firstNonRoute = name;
                            }
                        }
                        if (pendingTrigger.empty()) pendingTrigger = firstNonRoute;
                    }
                    if (!pendingTrigger.empty()) {
                        hudEs->recordTriggerWaitFrame(pendingTrigger);
                        const sr3luahost::EngineState::TriggerWaitStallInfo& tstall = hudEs->triggerWaitStallInfo();
                        if (tstall.framesWaiting >= static_cast<uint64_t>(autoObjectiveFrames)) {
                            autoObjectiveVisitedTriggers.insert(pendingTrigger);
                            { // alternatives in the same route row ("{A, B, C}": the script waits on ANY) are not
                              // visited for 600 frames (CHOSEN) so the one we entered can be served first.
                                const std::string vl = lowerStr(pendingTrigger);
                                for (size_t ri = 0; ri < autoRoute.triggers.size(); ++ri) {
                                    if (autoRoute.triggers[ri] != vl) continue;
                                    for (size_t rj = 0; rj < autoRoute.triggers.size(); ++rj)
                                        if (autoRoute.triggerRow[rj] == autoRoute.triggerRow[ri] && rj != ri)
                                            autoRoute.altBlockedUntilFrame[autoRoute.triggers[rj]] = frames + 600;
                                    break;
                                }
                            }
                            const sr3luahost::EngineState::TriggerVolumeGeometry* g = hudEs->triggerGeometry(pendingTrigger);
                            if (!g || !g->positionKnown) {
                                std::printf("auto-objective: stalled %llu frames waiting on trigger '%s' but its "
                                            "geometry has no known real position - not teleporting\n",
                                            static_cast<unsigned long long>(tstall.framesWaiting), pendingTrigger.c_str());
                            } else {
                                const sr3luahost::WorldPosition rep =
                                    sr3luahost::EngineState::TriggerVolumeRepresentativePoint(*g);
                                pm.warp(sr3player::Vec3{rep.x, rep.y, rep.z});
                                hudEs->bridgeLiveCharacterPosition("#PLAYER1#", rep);
                                { // drive-to: a seated player takes the vehicle along (route-table "drive-to" rule)
                                    const std::string& cv = hudEs->getOrCreateCharacter("#PLAYER1#").currentVehicleName.get();
                                    if (!cv.empty()) hudEs->bridgeLiveVehiclePosition(cv, rep);
                                }
                                autoRouteHoldPos = rep;
                                // CHOSEN: hold until the NEXT route teleport (trigger or get_dist) replaces it, so exit
                                // callbacks (m22_zone_exited_cb) never see the player leave while the script waits.
                                autoRouteHoldFramesLeft = 1 << 30;
                                autoRouteHoldAge = 0;
                                std::printf("auto-objective: stalled %llu frames waiting on trigger '%s' - teleported "
                                            "player INTO its volume at (%.2f, %.2f, %.2f) [CHOSEN test-harness "
                                            "stand-in, not real navigation - see --auto-objective's own doc comment]\n",
                                            static_cast<unsigned long long>(tstall.framesWaiting), pendingTrigger.c_str(),
                                            rep.x, rep.y, rep.z);
                            }
                        }
                    }
                }
            }
        }
}

void stepMissionDrive(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& autoSeatMissionVehicle = sess.autoSeatMissionVehicle;
    auto& autoSkipCutscenes = sess.autoSkipCutscenes;
    auto& missionStem = sess.missionStem;
    auto& missionTimeoutSeconds = sess.missionTimeoutSeconds;
    auto& scriptHudPromptResponse = sess.scriptHudPromptResponse;
    auto& autoAnswerHudPrompts = sess.autoAnswerHudPrompts;
    auto& hudPromptAutoAnswerDelayFrames = sess.hudPromptAutoAnswerDelayFrames;
    auto& hudHost = sess.hudHost;
    auto& hudEs = sess.hudEs;
    auto& hudGp = sess.hudGp;
    auto& missionDriveActive = sess.missionDriveActive;
    auto& missionStartRecordId = sess.missionStartRecordId;
    auto& missionStartTracked = sess.missionStartTracked;
    auto& missionScriptEntryName = sess.missionScriptEntryName;
    auto& missionElapsedSeconds = sess.missionElapsedSeconds;
    auto& hudPromptRequestsRespondedCount = sess.hudPromptRequestsRespondedCount;
    auto& hudPromptFirstSeenFrame = sess.hudPromptFirstSeenFrame;
    auto& frames = sess.frames;
    auto& missionTimedOutDump = sess.missionTimedOutDump;
    auto& autoSkipCutscenesApplied = sess.autoSkipCutscenesApplied;
    auto& autoSeatMissionVehicleFired = sess.autoSeatMissionVehicleFired;
    auto& dt = frm.dt;
        // MISSION-HOST PHASE 1 (this task) - per-frame mission drive, AFTER
        // this frame's own player/vehicle position update (mirroring spec-
        // render-pipeline.md §6.6's own real order: Lua coroutine resume
        // runs as part of "each active game state's own per-frame update",
        // i.e. after the step that produces that frame's positions) and
        // BEFORE rendering begins. See this function's own doc comment
        // (above runPlaytest()) for the full rationale/citation.
        if (missionDriveActive) {
            // --auto-seat-mission-vehicle (THIS TASK, 2026-10-12): see this
            // flag's own doc comment above its CLI declaration for the full
            // "why m05 specifically, why no generic detection" account.
            // Fired once, on the very first mission-drive tick, BEFORE the
            // scheduler pass below gets a chance to resume m05_jump_local
            // into its own `vehicle_exit(LOCAL_PLAYER)` call - calling the
            // REAL registered vehicle_enter_do native exactly as a mission
            // script's own vehicle_enter_teleport wrapper would.
            if (autoSkipCutscenes && !autoSkipCutscenesApplied) {
                autoSkipCutscenesApplied = true;
                hudEs->zsceneSkipAllCutscenes().set(true);
                std::printf("auto-skip: --auto-skip-cutscenes set the real skip_all_cutscenes option [CHOSEN harness "
                            "option value; engine skip branches unchanged]\n");
            }
            if (autoSeatMissionVehicle && !autoSeatMissionVehicleFired && missionStem == "m05") {
                autoSeatMissionVehicleFired = true;
                auto seatResult = hudHost->runChunk(
                    hudGp, "vehicle_enter_do(LOCAL_PLAYER, 'm05_heli_pierce', true)",
                    "playtest_auto_seat_mission_vehicle.lua");
                std::printf("auto-seat: --auto-seat-mission-vehicle seated #PLAYER1# in 'm05_heli_pierce' via the "
                            "real vehicle_enter_do native (loadOk=%d pcallOk=%d) [CHOSEN, m05-specific stand-in - "
                            "see --auto-seat-mission-vehicle's own doc comment]\n",
                            seatResult.loadOk ? 1 : 0, seatResult.pcallOk ? 1 : 0);
            }

            // Wall-clock timeout (NEW this task, design-doc §6's own flagged
            // risk): accumulated against this loop's own real per-frame
            // `dt` (real wall-clock time in normal operation; the SAME
            // deterministic 1/30s --no-input substitute every other
            // per-frame value here already uses - see this function's own
            // doc comment for why that is a faithful, reproducible choice,
            // not a second clock). Checked BEFORE driving another tick, so
            // a mission that is already over budget never gets one more
            // resume.
            missionElapsedSeconds += dt;
            if (missionTimeoutSeconds > 0.0 && missionElapsedSeconds > missionTimeoutSeconds) {
                missionDriveActive = false;
                missionTimedOutDump = true;
                std::printf("mission  : --mission=%s stopped - wall-clock timeout (%.1fs) exceeded while still "
                            "suspended; this frame's own rendering/input loop is UNAFFECTED (see this "
                            "function's own doc comment)\n",
                            missionStem.c_str(), missionTimeoutSeconds);
            } else {
                // Tick cadence - EXACT same CHOSEN order tools/lua_host_run.cpp's
                // own mission-drive loop uses (screenFadeHostFrame ->
                // cutsceneHostFrame -> gameClockAdvanceFrame -> ONE
                // BareThreadTable scheduler pass -> fireConfirmedHooks), now
                // driven by this loop's own REAL per-frame dt instead of that
                // file's own fixed 333ms/tick substitute.
                const int64_t dtMs = static_cast<int64_t>(dt * 1000.0 + 0.5);
                hudEs->screenFadeHostFrame(dtMs);
                hudEs->cutsceneHostFrame();
                hudEs->gameClockAdvanceFrame(dt);
                // Engine callback dispatch (spec-lua-api-behaviour.md "Engine callback dispatch: death and
                // destruction" B): the per-human update finalizes deaths BEFORE the script pump, so a thread's
                // character_kill is finalized (on_human_killed, then on_death queued) at the next frame's world
                // tick and the callback runs in that frame's pump, after every older thread.
                hudEs->tickHealthRegen(dt);  // spec-player-runtime.md Sec18.3 regen tick (player victims only)
                hudEs->tickCharacterDeaths();
                hudHost->bareGlobals().setFrameTime(static_cast<float>(dt));
                lua_State* currentResumeCo = nullptr;
                hudHost->runScriptThreadSchedulerPass(
                    [&](const sr3luahost::BareThreadTable::Record& rec) {
                        currentResumeCo = rec.co;
                        lua_sethook(rec.co, missionWatchdogHook, LUA_MASKCOUNT, kMissionWatchdogInstructionBudget);
                    },
                    [&](const sr3luahost::BareThreadTable::PassEvent& ev) {
                        if (currentResumeCo) lua_sethook(currentResumeCo, nullptr, 0, 0);
                        static const bool traceThreadEnds = std::getenv("SR3_TRACE_STALL_LOCALS") != nullptr;
                        if (traceThreadEnds && !ev.aliveAfter)  // DIAGNOSTIC ONLY (opt-in)
                            std::printf("[thread-end] frame %d thread '%s' (id %u) ended %s\n", frames, ev.name.c_str(),
                                        static_cast<unsigned>(ev.id), ev.error.empty() ? "cleanly" : ev.error.c_str());
                        if (!ev.error.empty()) {
                            std::printf("mission  : --mission=%s thread '%s' errored on resume: %s\n",
                                        missionStem.c_str(), ev.name.c_str(), ev.error.c_str());
                        }
                        if (missionStartTracked && ev.id == missionStartRecordId && !ev.aliveAfter) {
                            missionStartTracked = false;
                            missionDriveActive = false;
                            std::printf("mission  : --mission=%s's own %s_start record finished %s after "
                                        "%.1fs - nothing further to drive this session\n",
                                        missionStem.c_str(), missionStem.c_str(),
                                        ev.error.empty() ? "cleanly" : "with an error", missionElapsedSeconds);
                        }
                    });
                {   // A thread started by thread_new that raises before its first yield (or any later error recorded
                    // by the thread table outside the scheduler pass events above) was previously silent.
                    static size_t threadErrorsPrinted = 0;
                    const auto& terrs = hudHost->bareGlobals().threads().errors();
                    for (; threadErrorsPrinted < terrs.size(); ++threadErrorsPrinted)
                        std::printf("mission  : --mission=%s thread error recorded by the thread table: %s\n",
                                    missionStem.c_str(), terrs[threadErrorsPrinted].c_str());
                }
                auto tickHooks =
                    hudHost->fireConfirmedHooks(hudGp, sr3luahost::confirmedHooks(), missionScriptEntryName);
                for (auto& hr : tickHooks) {
                    if (hr.attemptedCall && !hr.callOk) {
                        std::printf("mission  : --mission=%s hook '%s' errored: %s\n", missionStem.c_str(),
                                    hr.hookName.c_str(), hr.callError.c_str());
                    }
                }

                // ===================== SCRIPT-HUD-PROMPT-RESPONSE (THIS TASK) =====================
                // See --script-hud-prompt-response's/--auto-answer-hud-prompts's
                // own doc comments (flag declarations, above) for the full
                // citation. Any hud_prompt request recorded THIS tick (or any
                // prior tick not yet answered - hudPromptRequestsRespondedCount
                // tracks the boundary) is answered once it has been showing for
                // at least hudPromptAutoAnswerDelayFrames frames (0 for plain
                // --script-hud-prompt-response - answers the very tick it first
                // appears, same as before this task; the CHOSEN default 15 for
                // --auto-answer-hud-prompts - waits, so a scripted/automated
                // --no-input run's auto-answer looks like a delayed reaction
                // rather than an instant one): spawn a coroutine for its own
                // registered callback name via the SAME BareThreadTable
                // mechanism the mission-start call above and lua_host_run's own
                // --probe-autopilot already use. Spawn only, exactly like that
                // precedent - the callback body does NOT run synchronously
                // here; it runs on the NEXT tick's runScriptThreadSchedulerPass
                // above, same decoupled-in-time shape Sec1.14 confirms for the
                // real dispatcher. Generic over ALL currently showing prompts,
                // not hardcoded to any one promptId/callbackName (M05_JUMP
                // included) - whichever prompt(s) are live get answered the
                // same way.
                if (scriptHudPromptResponse) {
                    auto& hudPromptReqs = hudEs->hudPromptRequests();
                    // Record the first-seen frame for any newly-appended
                    // request before checking delays below, so a request that
                    // appears and is due to be answered in the SAME tick (e.g.
                    // delay 0) is still handled this tick.
                    while (hudPromptFirstSeenFrame.size() < hudPromptReqs.size()) {
                        hudPromptFirstSeenFrame.push_back(frames);
                    }
                    while (hudPromptRequestsRespondedCount < hudPromptReqs.size()) {
                        const auto& req = hudPromptReqs[hudPromptRequestsRespondedCount];
                        const int firstSeenFrame = hudPromptFirstSeenFrame[hudPromptRequestsRespondedCount];
                        if (frames - firstSeenFrame < hudPromptAutoAnswerDelayFrames) {
                            // This request (and, since requests are answered in
                            // the order they were recorded, every later one too)
                            // isn't due yet - stop and re-check next tick.
                            break;
                        }
                        ++hudPromptRequestsRespondedCount;
                        if (req.arg3.empty()) continue; // no callback name to respond with
                        // CHOSEN stand-in (hud-prompt-direct-callback-standin task, 2026-10-12):
                        // the real dispatch path that resolves hud_prompt's arg3 to a callback is
                        // not yet traced (spec-lua-api-behaviour.md Sec13.3, 2026-10-12 update -
                        // confirmed arg3 genuinely IS the callback name the engine calls, but via
                        // a mechanism NOT inside hud_prompt's own native call chain). Measured
                        // (results/verify_m05_hud_prompt_autoanswer_20261012.md): spawning a
                        // coroutine for arg3 via BareThreadTable::allocate() did NOT advance m05
                        // past m05.lua:1141. This directly calls the named global Lua function by
                        // string lookup (Host::callGlobalFunctionByName()) instead - NOT how the
                        // real engine does it, but produces the same observable effect (the
                        // callback body actually running) for any script that only cares about the
                        // callback having run.
                        std::printf("mission  : %s: hud_prompt(\"%s\", \"%s\", \"%s\") seen %d frame(s) ago - "
                                    "CHOSEN stand-in: calling global '%s' directly\n",
                                    autoAnswerHudPrompts ? "--auto-answer-hud-prompts" : "--script-hud-prompt-response",
                                    req.target.c_str(), req.arg2.c_str(), req.arg3.c_str(),
                                    frames - firstSeenFrame, req.arg3.c_str());
                        std::string callErr;
                        bool called = hudHost->callGlobalFunctionByName(hudGp, req.arg3, callErr);
                        if (!called) {
                            std::printf("mission  : --script-hud-prompt-response: callback '%s' not called: %s\n",
                                        req.arg3.c_str(), callErr.empty() ? "no such global function" : callErr.c_str());
                        }
                    }
                }
            }
        }
}

} // namespace sr3viewer
