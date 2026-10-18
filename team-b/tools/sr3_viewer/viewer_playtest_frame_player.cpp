// runPlaytest() split: frame-loop step - player/vehicle camera, pose, footsteps and weapon fire.
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

#include <cstdlib>  // getenv (SR3_TRACE_PLAYER_Y diagnostic)

namespace sr3viewer {

void stepPlayerView(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& noInput = sess.noInput;
    auto& farZ = sess.farZ;
    auto& scriptFire = sess.scriptFire;
    auto& mesh = *sess.mesh_;
    const auto& channelIndex = sess.channelIndex;
    const auto& drawGroup = sess.drawGroup;
    const auto& bones = *sess.bones_p;
    const auto& useBonePalette = sess.useBonePalette;
    auto& parentIdx = sess.parentIdx;
    auto& restRig = sess.restRig;
    auto& verts = sess.verts;
    auto& bindPositions = sess.bindPositions;
    auto& bradWeaponDef = *sess.bradWeaponDef_;
    auto& bradWeapon = sess.bradWeapon;
    auto& walkLeftFootFrames = sess.walkLeftFootFrames;
    auto& walkRightFootFrames = sess.walkRightFootFrames;
    auto& walkTriggerFramesFound = sess.walkTriggerFramesFound;
    auto& bridge = *sess.bridge_;
    auto& clipRegistry = *sess.clipRegistry_;
    auto& walkClipDurationUnits = sess.walkClipDurationUnits;
    auto& loopPhase = *sess.loopPhase_;
    const auto& realPoseEnabled = sess.realPoseEnabled;
    auto& prevWalkTriggerPhase = sess.prevWalkTriggerPhase;
    auto& rideSets = sess.rideSets;
    auto& demoVehicleInteractionInfo = *sess.demoVehicleInteractionInfo_;
    auto& demoVehicleInteractionInfoFound = sess.demoVehicleInteractionInfoFound;
    auto& rideClipRecords = sess.rideClipRecords;
    auto& seatedPoseApplier = *sess.seatedPoseApplier_;
    auto& seatedPoseClockSeconds = sess.seatedPoseClockSeconds;
    auto& moveSrc = sess.moveSrc;
    auto& pm = *sess.pm_;
    auto& heldFacing = sess.heldFacing;
    auto& followCam = *sess.followCam_;
    auto& device = *sess.device_;
    auto& audioDevice = *sess.audioDevice_;
    auto& audioReady = sess.audioReady;
    auto& footstepClip = *sess.footstepClip_;
    auto& framesSinceLastFootstep = sess.framesSinceLastFootstep;
    auto& footstepsTriggered = sess.footstepsTriggered;
    auto& swapChain = *sess.swapChain_;
    auto& characterRenderer = *sess.characterRenderer_;
    auto& dupPtr = sess.dupPtr;
    auto& hudEs = sess.hudEs;
    sr3luahost::EngineState& combatEs = *sess.combatEs_p;
    auto& bradDamageSink = *sess.bradDamageSink_;
    auto& pedAiPlayerFiring = sess.pedAiPlayerFiring;
    auto& nearFieldPedReaction = sess.nearFieldPedReaction;
    auto& nearFieldPedFleeDestination = sess.nearFieldPedFleeDestination;
    auto& prevFireDown = sess.prevFireDown;
    auto& finalGroundY = sess.finalGroundY;
    auto& collisionDriver = *sess.collisionDriver_;
    auto& sampleGroundHeight = sess.sampleGroundHeight;
    auto& playerGroundFollower = *sess.playerGroundFollower_;
    auto& vehicleGroundY = sess.vehicleGroundY;
    auto& vehicleRig = *sess.vehicleRig_;
    auto& realCameraCollision = *sess.realCameraCollision_;
    auto& inVehicle = sess.inVehicle;
    auto& fpCamCur = sess.fpCamCur;
    auto& fpCamPrev = sess.fpCamPrev;
    auto& fpCamFwd = sess.fpCamFwd;
    auto& fpCamSamples = sess.fpCamSamples;
    auto& nearFieldPedsLive = sess.nearFieldPedsLive;
    auto& nearFieldPedRng = *sess.nearFieldPedRng_;
    auto& vehicleHideOccupant = sess.vehicleHideOccupant;
    auto& frames = sess.frames;
    auto& nowMsAccum = sess.nowMsAccum;
    auto& dt = frm.dt;
    auto& fireKeyDownNow = frm.fireKeyDownNow;
    auto& viewProjection = frm.viewProjection;
    auto& headingNow = frm.headingNow;
    auto& charWorld = frm.charWorld;
    auto& poseResult = frm.poseResult;
    auto& diagEyeX = frm.diagEyeX;
    auto& diagEyeY = frm.diagEyeY;
    auto& diagEyeZ = frm.diagEyeZ;
        // viewProjection/headingNow/charWorld/poseResult/diagEye* are filled by
        // EXACTLY ONE of the two branches below (THIS TASK's own "character
        // pauses/hides, camera follows the vehicle" requirement) - walking
        // runs the REAL player<->animation<->camera pipeline unchanged
        // (SUBSTITUTION 2, below); driving runs PlaytestVehicleRig's real
        // physics step plus a CHOSEN simple chase-cam (same formula as
        // runDriveDemo()'s own simplified trailing camera - see this
        // function's own "VEHICLE ENTRY/DRIVING" doc comment for why no real
        // CameraSystem vehicle submode is used instead).
        // Set by the DRIVING branch below when a real seated pose was actually resolved+
        // composed this frame (sr3player::SeatedRidePoseApplier) - the render gate near the
        // bottom of this loop uses it to decide whether the occupant renders with that real
        // pose or (resolution failed - honest fallback, not fabricated) its own bind pose.
        bool seatedPoseResolvedThisFrame = false;
        if (!inVehicle) {
        sess.vehicleChaseCamWasDriving = false;
        sr3player::Vec3 facing = pm.st.facingA50;
        if (sr3player::IsZero7(facing)) facing = heldFacing; else heldFacing = facing;
        pm.world.orientation.set(sr3playeranim::OrientationFromFacing(facing));

        // SUBSTITUTION 2: the REAL Sec4.3 cadence - cameraHook() (reads real mouse
        // rotate()/elevate() via lookSlot()) -> preUpdate() (angles -> working rows) ->
        // movementRows() fed into THIS frame's animFrame.camera, so moveSlot's own
        // F/Rt basis (spec-player-runtime.md Sec3.5.5) is the REAL camera, not a fixed
        // world axis - then the move slot, then physics/collision, then update().
        followCam.cameraHook();
        followCam.preUpdate(dt);
        sr3player::MoveFrameInput animFrame;
        animFrame.nowMs = nowMsAccum;
        animFrame.dt = dt;
        animFrame.axisDt = dt;
        animFrame.camera = followCam.movementRows();

        // REAL per-frame heightmap ground height for the player (THIS
        // TASK) - see this function's own "REAL per-frame heightmap-based
        // ground following" doc comment above the collision-hook wiring
        // for the full citation. Only the TARGET collisionDriver falls/
        // snaps against is eased here; the player's own actual fall/snap
        // behaviour stays CharacterProxyDriver::step()'s existing,
        // spec-CONFIRMED gravity formula (spec-player-runtime.md Sec5.6.5),
        // now aimed at a ground height that itself never jumps underneath
        // an already-grounded player.
        const float playerRawH =
            sampleGroundHeight(pm.st.position.x, pm.st.position.z, finalGroundY);
        collisionDriver.setGroundY(playerGroundFollower.update(playerRawH, dt).groundY);
        // Opt-in diagnostic (2026-10-18 freeroam ground-height investigation; OFF by default, no behaviour change):
        // SR3_TRACE_PLAYER_Y=1 prints, every 30 frames, the player Y, the raw ground sample fed to the follower, and
        // which link of the chain (0x2237 mesh / 0x2244 heightmap / flat fallback) supplied it.
        {
            static const bool tracePlayerY = std::getenv("SR3_TRACE_PLAYER_Y") != nullptr;
            if (tracePlayerY && frames % 30 == 0) {
                const float hmOnly = sess.sampleRealGroundHeight(pm.st.position.x, pm.st.position.z, -1.0e30f);
                const char* src = (playerRawH == hmOnly) ? "heightmap" : "0x2237-mesh";
                double nodeY = -999.0;
                const bool haveNode = sess.nodeGround.Nearest(pm.st.position.x, pm.st.position.z, 200.0, 0.0, nodeY);
                if (hmOnly < -1.0e29f && haveNode && playerRawH == static_cast<float>(nodeY)) src = "road-node";
                else if (hmOnly < -1.0e29f && playerRawH == finalGroundY) src = "flat-fallback(finalGroundY)";
                std::printf("player-y-trace: frame %d pos (%.2f %.2f %.2f) rawGround=%.2f source=%s nearestNodeY(200m)=%.2f\n",
                            frames, pm.st.position.x, pm.st.position.y, pm.st.position.z, static_cast<double>(playerRawH),
                            src, nodeY);
            }
        }

        bridge.stepFrame(pm, animFrame);

        if (realPoseEnabled) {
            const float blendedDuration = sr3animpose::BlendedLoopingDurationUnits(bridge.lastOutput(), clipRegistry);
            loopPhase.advance(animFrame.dt, bridge.playbackRate(), blendedDuration);
            std::vector<sr3animpose::ActiveLayer> activeLayers =
                sr3animpose::BuildActiveLayers(bridge.lastOutput(), loopPhase, clipRegistry);
            poseResult = sr3animpose::ComposeClipPose(activeLayers, clipRegistry, bones.size());
            std::vector<sr3rig::Rotation3> rotsRig(bones.size(), sr3rig::identityRotation3());
            std::vector<std::array<float, 3>> deltasRig(bones.size(), std::array<float, 3>{0.0f, 0.0f, 0.0f});
            for (size_t b = 0; b < bones.size(); ++b) {
                const sr3anim::BoneSample& s = poseResult.bones[b];
                rotsRig[b] = sr3rig::quatToRotation3(s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w);
                deltasRig[b] = s.translationDelta;
            }
            std::vector<sr3rig::Mat3x4> skinRig = sr3rig::computeAnimatedSkinningMatrices(parentIdx, restRig, &rotsRig, &deltasRig);
            std::vector<sr3rig::Mat3x4> skinMesh(bones.size());
            for (size_t b = 0; b < bones.size(); ++b)
                skinMesh[b] = useBonePalette ? sr3rig::conjugateToMeshSpaceInverted(skinRig[b])
                                             : sr3rig::conjugateToMeshSpace(skinRig[b]);
            std::vector<std::array<float, 3>> posedPositions = sr3rig::skinVertices(verts, skinMesh, nullptr);
            std::string reuploadError;
            if (!characterRenderer.upload(device.device(), mesh, channelIndex, 0, reuploadError, drawGroup,
                                          &posedPositions, dupPtr)) {
                std::fprintf(stderr, "frame %d: posed re-upload failed: %s\n", frames, reuploadError.c_str());
            }
        }

        sr3playeranim::DriveCameraTarget(followCam, pm);
        followCam.update(sr3player::CameraFrameTimes{animFrame.dt, animFrame.dt, animFrame.dt});

        headingNow = sr3player::HeadingFromForward(pm.world.orientation.get().forward);
        sr3render::buildWorldMatrix(static_cast<float>(pm.st.position.x), static_cast<float>(pm.st.position.y),
                                    static_cast<float>(pm.st.position.z), static_cast<float>(headingNow), 1.0f,
                                    charWorld);

        const sr3player::CameraTransform& cam = followCam.committed();
        const float camEye[3] = {static_cast<float>(cam.position.x), static_cast<float>(cam.position.y),
                                 static_cast<float>(cam.position.z)};
        const float camRight[3] = {static_cast<float>(cam.right.x), static_cast<float>(cam.right.y),
                                   static_cast<float>(cam.right.z)};
        const float camUp[3] = {static_cast<float>(cam.up.x), static_cast<float>(cam.up.y),
                                static_cast<float>(cam.up.z)};
        const float camForward[3] = {static_cast<float>(cam.forward.x), static_cast<float>(cam.forward.y),
                                     static_cast<float>(cam.forward.z)};
        sess.minimapCamFwdX = cam.forward.x;  // minimap rotation source (spec-hud-runtime.md H.3 point 4)
        sess.minimapCamFwdZ = cam.forward.z;

        // ---- FOOTSTEP-CADENCE SFX TRIGGER (THIS TASK, audio-playback) ------------------
        // "Moving on foot" is read straight off moveSrc's own two walk axes - the SAME
        // live-WASD/--no-input-scripted values PlayerMovement itself already consumes
        // every frame (no separate "isWalking" flag exists anywhere in this project to
        // reuse instead, so this reads the one real input signal that IS already here).
        // Emitter = pm.st.position (spec-audio-runtime.md Sec11a.1's own emitter-position
        // producer, the real player-character position); listener = cam.position (the
        // follow camera's committed position - Sec11a.2's own listener producer is fed
        // from an unlocated camera/view source upstream in the real game, OPEN per that
        // section; this demo's own follow camera is the closest real stand-in this tool
        // has). See audio_output_device.h's own playOneShot3D() doc comment for exactly
        // why the computed distance is logged but NOT used to attenuate volume.
        if (audioReady && footstepClip.valid) {
            audioDevice.pump();  // reap finished one-shot voices before this frame's own submit attempt
            const bool movingOnFoot = (std::fabs(moveSrc.A) > 0.05f || std::fabs(moveSrc.B) > 0.05f);
            bool fireFootstepThisFrame = false;
            // REAL foot-plant-trigger-driven timing (THIS TASK, 2026-10-10, orchestrator
            // correction) - when the walk clip's own real left-foot/right-foot Trigger Frame
            // data was found (walkTriggerFramesFound, see the doc comment+setup above this
            // function's own graph-load block), fire exactly when loopPhase's real [0,1) phase
            // crosses one of those real Frame positions (mapped into the same phase space via
            // walkClipDurationUnits) - NOT a fixed-interval guess. prevWalkTriggerPhase is
            // updated every frame (not just while moving) so a crossing check after an idle
            // spell compares against the correct, un-stale previous phase.
            const float currWalkPhase = loopPhase.phaseUnit();
            if (realPoseEnabled && walkTriggerFramesFound && walkClipDurationUnits > 0) {
                if (movingOnFoot) {
                    auto crossedFrame = [&](int32_t triggerFrame) {
                        const float p = static_cast<float>(triggerFrame) / static_cast<float>(walkClipDurationUnits);
                        if (currWalkPhase >= prevWalkTriggerPhase) return p > prevWalkTriggerPhase && p <= currWalkPhase;
                        return p > prevWalkTriggerPhase || p <= currWalkPhase;  // wrapped past 1.0 this tick
                    };
                    for (int32_t f : walkLeftFootFrames) if (crossedFrame(f)) { fireFootstepThisFrame = true; break; }
                    if (!fireFootstepThisFrame)
                        for (int32_t f : walkRightFootFrames) if (crossedFrame(f)) { fireFootstepThisFrame = true; break; }
                }
                prevWalkTriggerPhase = currWalkPhase;
            } else if (movingOnFoot) {
                // FALLBACK (real trigger data not available this run - no real-pose compose
                // active, or no matching anim_files.xtbl row found): the original CHOSEN fixed-
                // cadence guess, kept honestly rather than silently dropped.
                ++framesSinceLastFootstep;
                if (framesSinceLastFootstep >= kFootstepCadenceFrames) {
                    framesSinceLastFootstep = 0;
                    fireFootstepThisFrame = true;
                }
            } else {
                framesSinceLastFootstep = 0;  // reset cadence on stop, same as a real walk-cycle would
            }
            if (fireFootstepThisFrame) {
                const float emitterPos[3] = {static_cast<float>(pm.st.position.x),
                                              static_cast<float>(pm.st.position.y),
                                              static_cast<float>(pm.st.position.z)};
                double distance = 0.0;
                std::string playError;
                if (audioDevice.playOneShot3D(footstepClip.decoded, /*volume=*/0.6f, emitterPos, camEye,
                                               playError, &distance)) {
                    ++footstepsTriggered;
                    if (footstepsTriggered <= 5 || footstepsTriggered % 20 == 0) {
                        std::printf(
                            "footstep-sfx: #%llu frame=%d emitter-listener distance=%.2fm timing=%s "
                            "(logged only, see playOneShot3D doc comment - NOT attenuating volume)\n",
                            static_cast<unsigned long long>(footstepsTriggered), frames, distance,
                            (realPoseEnabled && walkTriggerFramesFound && walkClipDurationUnits > 0)
                                ? "real-trigger" : "cadence-fallback");
                    }
                }  // a transient failure (e.g. voice pool momentarily full) is silently
                   // skipped for this one tick, same as a real missed footstep cue -
                   // not fatal, not worth spamming stderr every tick for.
            }
        }

        const float fovY = static_cast<float>(cam.fov * 3.14159265 / 180.0);
        const float aspect = static_cast<float>(swapChain.width()) / static_cast<float>(swapChain.height());
        buildViewProjectionFromCamera(camEye, camRight, camUp, camForward, fovY, aspect, 0.5f, farZ, viewProjection);
        // Free-roam population (RG-5/RG-6): remember this frame's camera for next frame's traffic tick.
        fpCamPrev = fpCamCur;
        fpCamCur = {static_cast<double>(camEye[0]), static_cast<double>(camEye[1]), static_cast<double>(camEye[2])};
        fpCamFwd = {static_cast<double>(camForward[0]), static_cast<double>(camForward[1]), static_cast<double>(camForward[2])};
        if (fpCamSamples < 2) ++fpCamSamples;
        std::copy(viewProjection, viewProjection + 16, g_frustumViewProj);
        g_haveFrustum = true;
        diagEyeX = static_cast<float>(cam.position.x);
        diagEyeY = static_cast<float>(cam.position.y);
        diagEyeZ = static_cast<float>(cam.position.z);

        // ===================== WEAPON FIRE (THIS TASK) =====================
        // Edge-triggered (fireEdge && !prevFireDown), same debounce shape as
        // vehEEdge/tEdge/roadblockEdge above - a held button fires exactly
        // once, not every frame. On-foot only (inVehicle's own branch, this
        // `if`): vehicle-mounted weapons never fire through this dispatch at
        // all anyway (sr3weapons::kVehicleWeaponClass's own CONFIRMED short-
        // circuit, weapon_fire.h), and the chase-cam ELSE branch below has no
        // equivalent eye/forward ray to aim from.
        //
        // THE REAL CALL CHAIN this fires (see this command's own top
        // "WEAPON FIRE" doc comment for the full citation): the live follow-
        // camera's own just-computed eye (camEye)/forward (camForward) -
        // ALREADY the real mouse-look/movement-row basis this frame's
        // animation/physics were just driven from above, not a separately
        // invented aim model - feeds sr3weapons::WeaponInstance::fire()
        // (already-merged feature/hitscan-fire-dispatch), which runs the
        // real CONFIRMED dispatch body (refire-delay/vehicle-class/refuse
        // gates, ammo decrement) and then the CHOSEN hitscan wiring
        // (CastHitscanRay() against the always-on near-field ambient-
        // pedestrian population, built fresh below from their own live
        // positions) - a hit calls bradDamageSink.applyLethalHit(), which
        // routes into the real EngineState::applyGuaranteedLethalHit()
        // (spec-player-runtime.md Sec15.3a: force-snaps health to 0 when
        // known, sets isDeadHighConfidence, then the real death-transition/
        // ragdoll-or-finalize tail, finalizeLethalCharacter()).
        const bool fireEdge = fireKeyDownNow && !prevFireDown;
        prevFireDown = fireKeyDownNow;
        if (fireEdge) {
            pedAiPlayerFiring = true;                           // civilians react to gunfire (NPC AI loop)
            if (hudEs) hudEs->npcAiPlayerFiringThisFrame = true;  // mission NPCs likewise
        }
        if (fireEdge && bradWeapon) {
            sr3player::Vec3 fireOrigin{static_cast<double>(camEye[0]), static_cast<double>(camEye[1]),
                                        static_cast<double>(camEye[2])};
            sr3player::Vec3 fireDir{static_cast<double>(camForward[0]), static_cast<double>(camForward[1]),
                                     static_cast<double>(camForward[2])};
            // --script-fire TEST-ONLY convenience: a scripted camera's own
            // forward ray has no guarantee of actually intersecting any
            // near-field ped's randomized live position, so under --no-input
            // the deterministic shot is aimed directly at the nearest live
            // ped instead - a test-harness aiming convenience, NOT part of
            // the real fire path (live LMB play above ALWAYS uses the real,
            // unmodified camera-forward ray).
            if (noInput && scriptFire && !nearFieldPedsLive.empty()) {
                const sr3airuntime::NearFieldPed* nearest = nullptr;
                double bestDistSq = 0.0;
                for (const auto& nfp : nearFieldPedsLive) {
                    const double dx = nfp.pos.x - fireOrigin.x, dz = nfp.pos.z - fireOrigin.z;
                    const double d2 = dx * dx + dz * dz;
                    if (!nearest || d2 < bestDistSq) { nearest = &nfp; bestDistSq = d2; }
                }
                fireDir = sr3player::Vec3{nearest->pos.x - fireOrigin.x, nearest->pos.y + 0.9 - fireOrigin.y,
                                           nearest->pos.z - fireOrigin.z};
            }

            // Build a fresh HitscanTarget per currently-live near-field ped
            // (THIS TASK's own CHOSEN per-ped box, weapon_fire.h's own
            // documented stand-in geometry - see HitscanTarget's own doc
            // comment for why: no `.clmesh_pc`/ragdoll-capsule shape is
            // ported here). +0.9m: CHOSEN rough torso height above
            // NearFieldPed::pos (the SAME ground-anchored position the near-
            // field ped DRAW loop just above this function's main loop
            // already passes to buildWorldMatrix() at y=finalGroundY,
            // unmodified) - not a measured real capsule offset.
            std::vector<sr3weapons::HitscanTarget> targets;
            std::unordered_map<std::string, bool> beforeDeadMap;
            targets.reserve(nearFieldPedsLive.size());
            for (const auto& nfp : nearFieldPedsLive) {
                sr3weapons::HitscanTarget t;
                t.name = "nearfieldped_" + std::to_string(nfp.id);
                t.center = sr3player::Vec3{nfp.pos.x, nfp.pos.y + 0.9, nfp.pos.z};
                targets.push_back(t);
                // "Before" snapshot (diagnostic only) - hasCharacter() does
                // NOT auto-create (unlike getOrCreateCharacter()), so a ped
                // never hit before stays correctly absent from combatEs.
                beforeDeadMap[t.name] =
                    combatEs.hasCharacter(t.name) && combatEs.characters().at(t.name).isDeadHighConfidence;
            }
            const double maxRange = bradWeaponDef.rangeMax.present ? static_cast<double>(bradWeaponDef.rangeMax.value)
                                                                    : 1000.0;  // CHOSEN fallback - see
                                                                               // WeaponInstance::fire()'s own doc
                                                                               // comment (ordinarily def.rangeMax)
            sr3weapons::WeaponInstance::FireOutcome outcome = bradWeapon->fire(
                static_cast<double>(nowMsAccum), fireOrigin, fireDir, maxRange, targets, &bradDamageSink);
            std::printf("[fire] frame %d: reachedItemUse=%d soundSelected=%d roundsAfter=%d refireGateBlocked=%d "
                        "hit=%d target='%s' damageApplied=%d\n",
                        frames, outcome.reachedItemUse, static_cast<int>(outcome.soundSelected),
                        outcome.roundsInClipAfter, outcome.refireGateBlocked, outcome.hitDetected,
                        outcome.hitTargetName.c_str(), outcome.damageApplied);
            if (outcome.soundSelected != sr3weapons::WeaponInstance::FireSoundSelection::kNone) {
                // T6 weapon fire sound. CHOSEN: event name -> media is unresolved from specced data (see session.h),
                // so only log the selection; plays through the footstep output path once a clip exists.
                const bool dry = outcome.soundSelected == sr3weapons::WeaponInstance::FireSoundSelection::kDryFire;
                if (audioReady && sess.weaponFireClip.valid) {
                    const float ep[3] = {static_cast<float>(fireOrigin.x), static_cast<float>(fireOrigin.y),
                                         static_cast<float>(fireOrigin.z)};
                    std::string playError;
                    if (audioDevice.playOneShot3D(sess.weaponFireClip.decoded, 1.0f, ep, camEye, playError, nullptr))
                        ++sess.weaponFireSoundsPlayed;
                } else {
                    ++sess.weaponFireSoundsUnresolved;
                    std::printf("[fire-sound] UNRESOLVED %s event for weapon '%s' (soundbank '%s'): event id/media not in "
                                "spec, not played (#%llu)\n",
                                dry ? "dry-fire" : "fire", bradWeaponDef.name.c_str(),
                                bradWeaponDef.audio.soundbankName ? bradWeaponDef.audio.soundbankName->c_str()
                                                                  : "(default Wep_<model>)",
                                static_cast<unsigned long long>(sess.weaponFireSoundsUnresolved));
                }
            }
            if (outcome.hitDetected) {
                const bool beforeDead =
                    beforeDeadMap.count(outcome.hitTargetName) ? beforeDeadMap[outcome.hitTargetName] : false;
                const bool afterDead = combatEs.hasCharacter(outcome.hitTargetName) &&
                                       combatEs.characters().at(outcome.hitTargetName).isDeadHighConfidence;
                std::printf("[fire] hit '%s' - isDeadHighConfidence before=%d after=%d\n",
                            outcome.hitTargetName.c_str(), beforeDead, afterDead);
                // T5 crime reports (spec-ai-runtime.md Sec19.8 G1). The activity names are confirmed (Sec6.1), the
                // records are CHOSEN (notoriety_escalation.h): shooting a civilian, then killing one.
                notorietyReportCrime(sess, sr3airuntime::kChosenCivilianShooting, "shot a civilian ped");
                if (afterDead && !beforeDead)
                    notorietyReportCrime(sess, sr3airuntime::kChosenCivilianKill, "killed a civilian ped");

                // The real gunfire-to-flee reaction chain (spec-ai-runtime.md
                // Sec20.4, sr3airuntime::EvaluateGunfireReaction/
                // ChooseFleeOrCower - already spec-confirmed/unit-tested,
                // reused as-is here, not reimplemented). team=
                // kCivilianTeamByte is a CHOSEN input FACT, not new flee
                // logic: NearFieldPed::team stays at its own documented -1
                // "never set" sentinel everywhere else in this codebase
                // (near_field_ped_population.h's own file header) - every
                // near-field ambient ped is already modelled, project-wide,
                // as a plain civilian (the draw loop's own comment: "every
                // ambient pedestrian currently looks like the same character
                // model"), so this is this task's own honest completion of
                // that one caller-supplied fact, not a fabricated team
                // assignment. isLethalOrAttacked=true: a hitscan hit is the
                // "attacked" case (Sec20.4 kind 12), not merely "heard
                // gunfire" (kind 76).
                for (const auto& nfp : nearFieldPedsLive) {
                    if ("nearfieldped_" + std::to_string(nfp.id) != outcome.hitTargetName) continue;
                    sr3airuntime::NearFieldPed facts = nfp;
                    facts.team = sr3airuntime::kCivilianTeamByte;
                    const sr3airuntime::GunfireReactionResult reaction =
                        sr3airuntime::EvaluateGunfireReaction(facts, /*isLethalOrAttacked=*/true);
                    nearFieldPedReaction[nfp.id] = reaction.mode;
                    std::printf("[fire] ped id=%llu reaction mode=%d (0=None,1=GunfireWary,2=Cower,3=Flee,4=Panic) "
                                "spokeLineOnly=%d pos=(%.2f,%.2f,%.2f)\n",
                                static_cast<unsigned long long>(nfp.id), static_cast<int>(reaction.mode),
                                reaction.spokeLineOnly, nfp.pos.x, nfp.pos.y, nfp.pos.z);

                    // THIS TASK (2026-10-10): close the "no movement/AI
                    // consumer" gap the weapon-fire-wiring task's own
                    // HANDOFF entry left open - when the reaction is
                    // genuinely Flee, actually set the ped moving, reusing
                    // the real sidewalk/wander segment-graph machinery
                    // (near_field_ped_population.h's own TickWander/
                    // WanderMove sub-state, Sec20.2) rather than inventing a
                    // new movement system. fireOrigin (the shooter/camera
                    // position for this shot) is the CHOSEN "threat
                    // position" input - Sec20.4 confirms the reaction chain
                    // resolves "the shooter" as a real entity but never
                    // gives a formula for what point a flee destination
                    // should be computed away FROM; the camera/shooter
                    // origin is the most direct available stand-in (not the
                    // hit point on the ped's own body, which would not
                    // meaningfully differ in direction for a single shot).
                    // See near_field_ped_population.h's own
                    // ChooseFleeDestination/kFleeSpeedMsChosen comments for
                    // the full CHOSEN-vs-OPEN accounting.
                    if (reaction.mode == sr3airuntime::PedReactionMode::Flee &&
                        !nearFieldPedFleeDestination.count(nfp.id)) {
                        const std::vector<sr3airuntime::SidewalkSegment> fleeCandidateSegments =
                            GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer(nfp.pos, 0.0, nearFieldPedRng);
                        // fireOrigin is sr3player::Vec3 (the shooter/camera
                        // position) - a distinct type from
                        // sr3airuntime::Vec3 (near_field_ped_population.h's
                        // own field header: a deliberately separate module,
                        // no cross-namespace Vec3 reuse) - converted here at
                        // this task's own call site, not by changing either
                        // library's own type.
                        const sr3airuntime::Vec3 threatPosAirRuntime{fireOrigin.x, fireOrigin.y, fireOrigin.z};
                        const std::optional<sr3airuntime::Vec3> fleeDest =
                            sr3airuntime::ChooseFleeDestination(nfp.pos, threatPosAirRuntime, fleeCandidateSegments);
                        if (fleeDest.has_value()) {
                            nearFieldPedFleeDestination[nfp.id] = *fleeDest;
                            for (auto& mutablePed : nearFieldPedsLive) {
                                if (mutablePed.id != nfp.id) continue;
                                // Enter the real WanderMove sub-state
                                // directly (Sec20.2's own "wander choose
                                // dest... transitioning to sub-state 34") -
                                // the destination is already chosen above,
                                // so this skips straight past WanderChooseDest
                                // the same way a caller that already has a
                                // destination would.
                                mutablePed.idleSubState = sr3airuntime::PedIdleSubState::WanderMove;
                                mutablePed.onSegment = false;
                                break;
                            }
                            std::printf("[flee] ped id=%llu entering Flee movement -> dest=(%.2f,%.2f,%.2f)\n",
                                        static_cast<unsigned long long>(nfp.id), fleeDest->x, fleeDest->y,
                                        fleeDest->z);
                        }
                    }
                    break;
                }
            }
        }
        } else {
        // ---- DRIVING: simple chase-cam (CHOSEN, same formula as
        // runDriveDemo()'s own simplified trailing camera) around the
        // vehicle's real physics state. ----
        const float vHeading = static_cast<float>(vehicleRig.sim->state_.heading);
        // Driver-seat occupant rendering (seated-driving-pose merge, 2026-10-10): combines
        // the vehicle-occupant-hide-flag task's gating (skip all pose work when the real
        // driver-seat `Hide Occupant` flag is set - spec-render-pipeline.md Sec23.14, true
        // only for the 2 real tank rows) with the real composed seated/driving ride pose this
        // task adds (spec-player-runtime.md Sec7.4b, sr3player::SeatedRidePoseApplier),
        // entirely REPLACING the old "stand at a CHOSEN offset, frozen in the bind pose"
        // placeholder this comment used to describe (that placeholder's own three constants
        // and reasoning are gone - see the now-superseded comment above this function's own
        // vehicle-seat-offset declaration block, just above kScriptVehicleEnterFrame).
        if (!vehicleHideOccupant) {
            // ---- seated/driving pose: resolve+compose the real driver-seat ride pose every
            // frame and place the character at the vehicle's own position/heading plus a seat
            // offset (see this block's own CHOSEN-offset note below), replacing the bind pose
            // this command previously left frozen (or hid outright) while seated. ----
            seatedPoseClockSeconds += dt;
            if (demoVehicleInteractionInfoFound && !rideSets.empty()) {
                sr3player::SeatedRidePoseApplier::Result seatPose = seatedPoseApplier.ComposeSeatedPose(
                    demoVehicleInteractionInfo, sr3player::kDriverSeatResolverIndex, rideSets, rideClipRecords,
                    clipRegistry, seatedPoseClockSeconds, bones.size());
                if (seatPose.resolved) {
                    poseResult = std::move(seatPose.pose);
                    std::vector<sr3rig::Rotation3> rotsRig(bones.size(), sr3rig::identityRotation3());
                    std::vector<std::array<float, 3>> deltasRig(bones.size(), std::array<float, 3>{0.0f, 0.0f, 0.0f});
                    for (size_t b = 0; b < bones.size(); ++b) {
                        const sr3anim::BoneSample& s = poseResult.bones[b];
                        rotsRig[b] = sr3rig::quatToRotation3(s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w);
                        if (frames == 30 && (std::fabs(s.translationDelta[0]) > 1e-4f || std::fabs(s.translationDelta[1]) > 1e-4f || std::fabs(s.translationDelta[2]) > 1e-4f))
                            std::printf("seatpose-dbg: bone %zu delta = (%.4f %.4f %.4f)\n", b,
                                        s.translationDelta[0], s.translationDelta[1], s.translationDelta[2]);
                        deltasRig[b] = s.translationDelta;
                    }
                    std::vector<sr3rig::Mat3x4> skinRig = sr3rig::computeAnimatedSkinningMatrices(parentIdx, restRig, &rotsRig, &deltasRig);
                    std::vector<sr3rig::Mat3x4> skinMesh(bones.size());
                    for (size_t b = 0; b < bones.size(); ++b)
                        skinMesh[b] = useBonePalette ? sr3rig::conjugateToMeshSpaceInverted(skinRig[b])
                                                     : sr3rig::conjugateToMeshSpace(skinRig[b]);
                    std::vector<std::array<float, 3>> seatedPositions = sr3rig::skinVertices(verts, skinMesh, nullptr);
                    std::string seatedUploadError;
                    if (characterRenderer.upload(device.device(), mesh, channelIndex, 0, seatedUploadError, drawGroup,
                                                 &seatedPositions, dupPtr)) {
                        seatedPoseResolvedThisFrame = true;
                    } else {
                        std::fprintf(stderr, "frame %d: seated-pose re-upload failed: %s\n", frames,
                                     seatedUploadError.c_str());
                    }
                } else if (frames == 0) {
                    std::printf("seatpose : ComposeSeatedPose() did not resolve this run (animation='%s' clip='%s') - "
                                "occupant stays on its bind pose\n",
                                seatPose.animationString.c_str(), seatPose.chosenClipName.c_str());
                }
            }
            if (!seatedPoseResolvedThisFrame) {
                // Honest fallback: re-upload the character's own real BIND pose (the SAME
                // bindPositions this function's setup already computed) rather than leaving
                // whatever pose it last walked in frozen on screen.
                std::string bindUploadError;
                if (!characterRenderer.upload(device.device(), mesh, channelIndex, 0, bindUploadError, drawGroup,
                                              &bindPositions, dupPtr)) {
                    std::fprintf(stderr, "frame %d: bind-pose re-upload failed: %s\n", frames, bindUploadError.c_str());
                }
            }
            // CHOSEN seat offset (not spec-confirmed geometry - this project has no real
            // Human_Seat_Offset data for this demo vehicle either, same gap the vehicle-
            // occupant-hide-flag task's own investigation already found for every real base-
            // game vehicle): forward 0, lateral toward the LEFT (driver) side using the SAME
            // right-vector convention this function's own vehicle-exit code already uses
            // (rightX = -sin(heading), rightZ = cos(heading)), height a bit above the chassis
            // local origin.
            //
            // BUG FOUND AND FIXED verifying this task's own captured before/after frames
            // (2026-10-09, two rounds): an earlier draft based seatY on `finalGroundY +
            // kVehPivotHeight` (the SAME "pivot" constant the CAMERA uses, just below here,
            // for `pivot[1]`/the chase-cam's own framing target) and claimed that was "tuned
            // against captured frames" - it was not actually checked against a captured
            // frame, and doing so just now showed the occupant floating ~1m above the car,
            // clearly outside the seat. ROUND 1 FIX: `vehWorld` (the vehicle MESH's own world
            // matrix, below) is built from the ground height ALONE, no `kVehPivotHeight` term -
            // that constant exists only to aim the chase camera at cabin height, not to
            // describe the mesh's local Y=0. Matching `vehWorld`'s own base improved it but
            // the occupant STILL rendered floating well above the seat. ROUND 2 (real
            // measurement, not a second guess): a one-off debug dump of real local-space
            // vertex Y bounds (temporarily instrumented, then removed) measured, at this
            // exact captured frame: bind (standing) pose local Y [-0.0131, 1.7217] (feet near
            // 0, head near the real 1.72m bind height - confirms the walking convention: mesh
            // local origin = feet/ground contact); the COMPOSED SEATED pose's own local Y
            // [1.5238, 2.6511] - i.e. ~1.53-1.55 HIGHER than the standing convention over the
            // same mesh, not just re-posed in place; and the real vehicle body mesh's own
            // local Y [-0.4408, 0.8303] (wheels near the bottom, roof near the top, origin
            // roughly mid-body). CHOSEN interpretation (the clip's own coordinate convention
            // was never traced - Sec7.4b's own open item, see this file's seated_ride_pose.h
            // banner): the composed seated pose's LOWEST point (1.5238) is the occupant's
            // feet (resting on the floorboard, knees bent - same role the bind pose's ~0
            // minimum plays for standing feet), so `kSeatHeightAbovePivot` is chosen to land
            // that floor-contact point near the vehicle's own local floor height (-0.25,
            // inside its measured [-0.44, 0.83] body range, below the seat cushion):
            // kSeatHeightAbovePivot = -0.25 - 1.5238 = -1.7738. Re-verified against a real
            // re-captured frame after this change (results/ cites the before/after this
            // task's own report describes) - the occupant now renders seated inside the
            // car's silhouette instead of floating above it; still CHOSEN, not spec-confirmed
            // geometry (no real Human_Seat_Offset data for this demo vehicle, same gap noted
            // above).
            //
            // SEATED-DRIVING-POSE MERGE NOTE (2026-10-10): `seatY` reads the SAME per-frame
            // `vehicleGroundY` the vehicle body's own `vehWorld` and the camera `pivot` below
            // now use (the real-ground-following task, landed on main AFTER this seat-offset
            // math was originally tuned against the then-current flat `finalGroundY`) - not
            // the flat fallback - so the seat stays anchored to the vehicle's own real
            // per-frame render height instead of silently drifting out of sync with it on
            // sloped ground. The -1.7738 offset itself is unchanged; only its ground-height
            // base was swapped to match vehWorld's current basis.
            constexpr float kSeatLateralOffset = 0.0f;
            constexpr float kSeatHeightAbovePivot = -1.7738f;
            const float seatRightX = -std::sin(vHeading), seatRightZ = std::cos(vHeading);
            const float seatX = static_cast<float>(vehicleRig.sim->state_.posX) + kSeatLateralOffset * seatRightX;
            const float seatZ = static_cast<float>(vehicleRig.sim->state_.posZ) + kSeatLateralOffset * seatRightZ;
            const float seatY = vehicleGroundY + kSeatHeightAbovePivot;
            sr3render::buildWorldMatrix(seatX, seatY, seatZ, vHeading, 1.0f, charWorld);
        }
        // Sec4.15 vehicle chase camera (FD 7.5 / FH 0.7 / Lookat_Offset.Y 1.15 / FOV 50-75 / follow swing /
        // pitch clamp), replacing the CHOSEN rigid 9 m / 4 m camera. The demo vehicle is genki (see
        // vehicleCmeshName), so its Sec4.15.4 row is used.
        // CHOSEN: the vehicle reference point is the ground height under the vehicle (OPEN, Sec4.15.5).
        // CHOSEN: speed is the planar body speed (no vertical velocity in this planar sim, spec speed is 3-D);
        // world velocity = forward*velForwardBody + right*velRightBody with the seat code's right vector
        // (-sin h, cos h).
        const auto& vst = vehicleRig.sim->state_;
        if (!sess.vehicleChaseCamWasDriving) sess.vehicleChaseCam.reset();  // camera cut on entering
        sess.vehicleChaseCamWasDriving = true;
        sr3player::VehicleCameraInput camIn;
        camIn.referencePoint = sr3player::Vec3{vst.posX, static_cast<double>(vehicleGroundY), vst.posZ};
        camIn.vehicleHeading = vst.heading;
        camIn.velX = vst.velForwardBody * std::cos(vst.heading) - vst.velRightBody * std::sin(vst.heading);
        camIn.velZ = vst.velForwardBody * std::sin(vst.heading) + vst.velRightBody * std::cos(vst.heading);
        camIn.speed = std::sqrt(vst.velForwardBody * vst.velForwardBody + vst.velRightBody * vst.velRightBody);
        camIn.step = dt;
        // Collision: CHOSEN reuse of the on-foot sweep (RealCameraCollision, radius 0.12), pivot -> eye.
        const sr3player::VehicleCameraOutput camOut = sess.vehicleChaseCam.update(camIn, &realCameraCollision);
        const float pivot[3] = {static_cast<float>(camOut.pivot.x), static_cast<float>(camOut.pivot.y),
                                static_cast<float>(camOut.pivot.z)};
        const float eye[3] = {static_cast<float>(camOut.eye.x), static_cast<float>(camOut.eye.y),
                              static_cast<float>(camOut.eye.z)};
        const float vehCamFovY = static_cast<float>(camOut.fov);  // axis OPEN (Sec4.15.5); used as vertical
        float camForward[3] = {pivot[0] - eye[0], pivot[1] - eye[1], pivot[2] - eye[2]};
        float flen = std::sqrt(camForward[0] * camForward[0] + camForward[1] * camForward[1] +
                               camForward[2] * camForward[2]);
        if (flen <= 0.0f) flen = 1.0f;
        for (float& c : camForward) c /= flen;
        sess.minimapCamFwdX = camForward[0];  // minimap rotation source (H.3 point 4)
        sess.minimapCamFwdZ = camForward[2];
        const float worldUp[3] = {0.0f, 1.0f, 0.0f};
        float camRight[3] = {camForward[1] * worldUp[2] - camForward[2] * worldUp[1],
                             camForward[2] * worldUp[0] - camForward[0] * worldUp[2],
                             camForward[0] * worldUp[1] - camForward[1] * worldUp[0]};
        float rlen = std::sqrt(camRight[0] * camRight[0] + camRight[1] * camRight[1] + camRight[2] * camRight[2]);
        if (rlen <= 0.0f) rlen = 1.0f;
        for (float& c : camRight) c /= rlen;
        const float camUp[3] = {camRight[1] * camForward[2] - camRight[2] * camForward[1],
                                camRight[2] * camForward[0] - camRight[0] * camForward[2],
                                camRight[0] * camForward[1] - camRight[1] * camForward[0]};
        const float aspect = static_cast<float>(swapChain.width()) / static_cast<float>(swapChain.height());
        buildViewProjectionFromCamera(eye, camRight, camUp, camForward, vehCamFovY, aspect, 0.3f, farZ,
                                      viewProjection);
        sess.vehicleChaseCamStats.add(camOut.distance, camOut.fov, camOut.collided);
        headingNow = static_cast<double>(vHeading);
        diagEyeX = eye[0]; diagEyeY = eye[1]; diagEyeZ = eye[2];
        }
}

} // namespace sr3viewer
