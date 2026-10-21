// runPlaytest() split: frame-loop render step and the end-of-run report.
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"
#include "sr3sky/sun.h"

namespace sr3viewer {

bool stepRender(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& maxFrames = sess.maxFrames;
    auto& capturePath = sess.capturePath;
    auto& captureFrame = sess.captureFrame;
    auto& captureFrames = sess.captureFrames;
    auto& hudEnabled = sess.hudEnabled;
    auto& vehicleEnabled = sess.vehicleEnabled;
    const auto& bones = *sess.bones_p;
    auto& todSegments = sess.todSegments;
    const auto& stateActions = *sess.stateActions_;
    auto& bridge = *sess.bridge_;
    const auto& realPoseEnabled = sess.realPoseEnabled;
    auto& pm = *sess.pm_;
    auto& error = sess.error;
    auto& device = *sess.device_;
    auto& swapChain = *sess.swapChain_;
    auto& focusBox = *sess.focusBox_;
    auto& sunShadowMap = *sess.sunShadowMap_;
    const auto& shadowMapReady = sess.shadowMapReady;
    auto& characterRenderer = *sess.characterRenderer_;
    auto& characterHeadRenderer = *sess.characterHeadRenderer_;
    auto& characterPerMaterialSrv = sess.characterPerMaterialSrv;
    auto& characterHeadPerMaterialSrv = sess.characterHeadPerMaterialSrv;
    auto& characterTextured = sess.characterTextured;
    auto& characterHeadTextured = sess.characterHeadTextured;
    auto& hudHost = sess.hudHost;
    auto& hudEs = sess.hudEs;
    auto& hudGp = sess.hudGp;
    auto& hudFont = *sess.hudFont_;
    auto& locLookup = *sess.locLookup_;
    auto& hudPipe = *sess.hudPipe_;
    auto& hudHealthH = sess.hudHealthH;
    auto& hudAmmoH = sess.hudAmmoH;
    auto& hudMoneyH = sess.hudMoneyH;
    auto& hudNotorietyH = sess.hudNotorietyH;
    auto& hudVehiclePromptH = sess.hudVehiclePromptH;
    auto& hudObjectiveH = sess.hudObjectiveH;
    auto& hudMaxHp = sess.hudMaxHp;
    auto& pauseMenuOpen = sess.pauseMenuOpen;
    auto& pauseTitleH = sess.pauseTitleH;
    auto& pauseResumeH = sess.pauseResumeH;
    auto& pauseQuitH = sess.pauseQuitH;
    auto& loaded = sess.loaded;
    auto& groundTiles = sess.groundTiles;
    auto& finalGroundY = sess.finalGroundY;
    auto& vehicleGroundY = sess.vehicleGroundY;
    auto& vehicleRig = *sess.vehicleRig_;
    auto& inVehicle = sess.inVehicle;
    auto& vehiclePromptShown = sess.vehiclePromptShown;
    auto& nearFieldLive = sess.nearFieldLive;
    auto& nearFieldVehicleHeading = sess.nearFieldVehicleHeading;
    auto& nearFieldPedsLive = sess.nearFieldPedsLive;
    auto& roadblockManager = *sess.roadblockManager_;
    auto& roadblockSlotVisual = sess.roadblockSlotVisual;
    auto& vehicleHideOccupant = sess.vehicleHideOccupant;
    auto& frames = sess.frames;
    auto& todFraction = sess.todFraction;
    const auto& focusX = frm.focusX;
    const auto& focusZ = frm.focusZ;
    auto& viewProjection = frm.viewProjection;
    auto& headingNow = frm.headingNow;
    auto& charWorld = frm.charWorld;
    auto& poseResult = frm.poseResult;
    auto& diagEyeX = frm.diagEyeX;
    auto& diagEyeY = frm.diagEyeY;
    auto& diagEyeZ = frm.diagEyeZ;
        // vehWorld (THIS TASK, sun-shadow-map): hoisted out of the final
        // vehicle-draw block further below so BOTH the new shadow depth-only
        // pass immediately below AND that later lit draw use the identical
        // matrix - the SAME buildWorldMatrix() call that block already made,
        // just computed once per frame instead of twice. Identity when the
        // vehicle is disabled (never read in that case either way).
        float vehWorld[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        if (vehicleEnabled) {
            // AW-15: craftWorldMatrix == the old buildWorldMatrix(...) for every non-craft car (same arguments).
            craftWorldMatrix(sess, finalGroundY, vehWorld);
        }

        // ===================== SUN/CASCADE SHADOW MAP (THIS TASK, 2026-10-09) =====================
        // Real-mechanism citation + CHOSEN-v1-simplification breakdown:
        // include/sr3render/sun_shadow_map.h's own banner (spec-render-
        // pipeline.md Sec23.6.4). Generates the depth-only shadow map BEFORE
        // the main lit pass below samples it - the real engine's own
        // confirmed sun/cascade generation function (`0x00491160`) likewise
        // runs at the very top of its per-frame lighting driver, before any
        // lit draw. `lightViewProjection` is recomputed every frame (the sun
        // moves - ComputeChosenSunDirection() is t-dependent) from the SAME
        // chosen sun direction formula the lit pass's own todLighting.sunDirection
        // will use moments below (ComputeTodLighting() calls that identical
        // function with this identical `todFraction` - verified by reading
        // src/tod_lighting.cpp, not assumed).
        float sunDirectionForShadow[3];
        {  // Team C: the real sun path (spec-tables-environment 19.2, differential D-23), not the CHOSEN arc
            const auto toward = sr3sky::SunTowardDirection(sr3sky::HourFromDayFraction(todFraction));
            for (int i = 0; i < 3; ++i) sunDirectionForShadow[i] = toward[i];
        }
        // CHOSEN v1 scene-bounds fit (see sun_shadow_map.h's own banner): a
        // single local bounding sphere centred on the camera's current
        // focus point (the same focusX/focusZ this frame's own
        // reconcile()/streaming call above already computed), NOT the real
        // engine's 4 data-driven cascade splits over the whole streamed
        // world - a fixed 40-unit radius comfortably covers the nearby
        // ground tiles/props/character/vehicle this pass also draws as
        // casters, immediately below.
        const float shadowFocusCentre[3] = {focusX, finalGroundY, focusZ};
        constexpr float kShadowRadius = 40.0f;
        float lightViewProjection[16];
        sunShadowMap.computeLightViewProjection(sunDirectionForShadow, shadowFocusCentre, kShadowRadius,
                                                 lightViewProjection);
        if (shadowMapReady) {
            sunShadowMap.beginDepthPass(device.context());
            // Casters mirror the lit pass's own object set below (ground
            // tiles, loaded city props, character when on foot, vehicle when
            // enabled) - same discipline MeshRenderer::drawDepthOnly()'s own
            // doc comment describes.
            for (auto& kv : groundTiles) {
                CityGroundTile& gt = *kv.second;
                for (auto& r : gt.renderers) r->drawDepthOnly(device.context(), lightViewProjection, nullptr);
            }
            for (auto& kv : loaded) {
                CityLoadedCell& lc = *kv.second;
                for (auto& p : lc.props)
                    for (auto& r : p.renderers) r->drawDepthOnly(device.context(), lightViewProjection, p.world);
            }
            if (!inVehicle) {
                characterRenderer.drawDepthOnly(device.context(), lightViewProjection, charWorld);
                characterHeadRenderer.drawDepthOnly(device.context(), lightViewProjection, charWorld);
            }
            if (vehicleEnabled)
                (sess.activeCarModel ? sess.activeCarModel->rig : vehicleRig)
                    .DrawDepthOnly(device.context(), lightViewProjection, vehWorld);
            sunShadowMap.endDepthPass(device.context(), swapChain.width(), swapChain.height());
        }

        swapChain.bind(device.context());
        ID3D11RenderTargetView* rtv = swapChain.renderTargetView();
        device.context()->OMSetRenderTargets(1, &rtv, focusBox.depthView());
        // SKY/CLEAR COLOUR + LIGHTING (this task, 2026-10-08, sky/lighting/TOD): REPLACES the
        // prior task's own fixed CHOSEN placeholders (flat skyblue clear colour; mesh_renderer's
        // fixed (0.3,0.5,-0.8) "headlight" term) with the real per-frame weather/TOD blend now
        // that it is spec-confirmed (spec-tables-environment.md Sec18) - see
        // sr3render/tod_lighting.h's own banner for the full CONFIRMED/CHOSEN breakdown:
        //   * The 2x2 bilinear TOD-segment x weather-stage corner blend formula IS spec-
        //     confirmed and implemented faithfully (BlendTodCorners()); the weather-STAGE axis
        //     is a CHOSEN constant (kWeatherStageForPlaytest above) because this project has no
        //     ported weather-stage SCHEDULE (a separate, much larger task).
        //   * Ambient_Color -> ambient term, Fog_Color -> sky/clear colour, TOD_Light_Color ->
        //     sun term is this consumer's OWN CHOSEN reading of Sec18's cross-reference
        //     paragraph, not a field-by-field-confirmed shader-constant mapping.
        //   * Sun DIRECTION has no spec-confirmed source anywhere (Sec18: substantiated
        //     negative) - ComputeChosenSunDirection()'s simple day-arc is entirely CHOSEN.
        // Lighting now varies PER CHANNEL (ambient + sun colour), not as one shared scalar -
        // this DOES change mesh_renderer.cpp's shared pixel shader (see that file's own
        // comment), but every pre-existing call site (none of which passes the new trailing
        // sunDirection/ambientColor/sunColor parameters) gets back exactly its old fixed values
        // by default, so every OTHER command's frozen golden baseline (city_corner, walk_demo*,
        // drive_demo_coverage*, golden_scene_check) renders byte-for-byte unchanged - verified,
        // not merely argued, by this task's own ctest run (see HANDOFF.md/this task's commit).
        // Gap #9: stage A/B + blend come from the weather scheduler (sess.weatherTick). Without weather.xtbl the
        // names are empty -> ComputeTodLightingBlended falls back to its old fixed defaults.
        sr3render::TodLightingResult todLighting = sr3render::ComputeTodLightingBlended(
            todSegments, static_cast<float>(todFraction), sess.weatherLightA, sess.weatherLightB, sess.weatherLightBlend);
        applySceneExposure(sess, todFraction, todLighting.ambientColor, todLighting.sunColor);  // Team C exposure
        for (int i = 0; i < 3; ++i) todLighting.sunDirection[i] = sunDirectionForShadow[i];  // Team C: 19.2 sun path
        swapChain.clear(device.context(), todLighting.skyClearColor[0], todLighting.skyClearColor[1],
                        todLighting.skyClearColor[2], 1.0f);
        focusBox.clearDepth(device.context());
        drawSkyGradient(sess, viewProjection, todFraction);  // Team C: TOD District_Skybox gradient

        // Real ground textures when resolveGroundTextureBindings() found them
        // (ground-tile texture fix merged into main after this command's own
        // first draft - picked up by merging main in, not re-derived here);
        // the same safe MaterialIdAsColour fallback as before when it did
        // not (gt.perMaterialSrv stays empty in that case - see
        // CityGroundTile's own field comment).
        // sunDirection/ambientColor/sunColor (this task): every draw call
        // below forwards the SAME per-frame todLighting values computed
        // above, so the whole scene is lit consistently - see
        // sr3render::MeshRenderer::draw()'s own comment for the byte-for-
        // byte-by-default contract this relies on.
        // shadowSrv/shadowSmp (THIS TASK, sun-shadow-map): null when the
        // shadow map failed to initialise (shadowMapReady == false), which
        // is exactly the existing byte-for-byte-by-default fallback every
        // draw()/drawTextured() call below already honours (see
        // mesh_renderer.h's own comment) - a shadow-map init failure
        // degrades gracefully to "no shadows", never a crash or a blocked
        // frame.
        ID3D11ShaderResourceView* shadowSrv = shadowMapReady ? sunShadowMap.shaderResourceView() : nullptr;
        ID3D11SamplerState* shadowSmp = shadowMapReady ? sunShadowMap.sampler() : nullptr;
        for (auto& kv : groundTiles) {
            CityGroundTile& gt = *kv.second;
            for (auto& r : gt.renderers) {
                if (!gt.perMaterialSrv.empty())
                    r->drawTextured(device.context(), viewProjection, gt.perMaterialSrv, nullptr,
                                    todLighting.sunDirection, todLighting.ambientColor, todLighting.sunColor,
                                    lightViewProjection, shadowSrv, shadowSmp);
                else
                    r->draw(device.context(), viewProjection, sr3render::MeshDrawMode::MaterialIdAsColour, nullptr,
                           nullptr, nullptr, todLighting.sunDirection, todLighting.ambientColor,
                           todLighting.sunColor, lightViewProjection, shadowSrv, shadowSmp);
            }
        }
        for (auto& kv : loaded) {
            CityLoadedCell& lc = *kv.second;
            for (auto& p : lc.props)
                for (auto& r : p.renderers)
                    r->drawTextured(device.context(), viewProjection, p.perMaterialSrv, p.world,
                                    todLighting.sunDirection, todLighting.ambientColor, todLighting.sunColor,
                                    lightViewProjection, shadowSrv, shadowSmp);
        }
        // Character rendering (combines the vehicle-occupant-hide-flag task's gating with
        // the seated-driving-pose task's "always show where the occupant actually is" fix,
        // 2026-10-10 merge): draws whenever NOT in a vehicle (unchanged - real walked pose),
        // OR in a vehicle whose own real driver-seat `Hide Occupant` flag is false
        // (spec-render-pipeline.md Sec23.14) - true for every ordinary vehicle, false
        // (hidden) only for the 2 real tank rows. charWorld and the uploaded vertex pose are
        // set to the correct seated values in the DRIVING branch above (a real resolved
        // seated pose when ComposeSeatedPose() resolved one this frame, else the honest
        // bind-pose fallback - spec-player-runtime.md Sec7.4b) under the SAME
        // `!vehicleHideOccupant` condition, so the two gates now agree on exactly when the
        // occupant is visible; no more blanket "always hide while driving" and no more
        // double-guessed CHOSEN stand-at-an-offset placeholder. Everything above this point
        // (ground/city props) still draws normally either way.
        if (!inVehicle || !(vehicleHideOccupant || sess.craft.rig.active)) {  // AW-15: no seated pose for craft classes
            if (characterTextured)
                characterRenderer.drawTextured(device.context(), viewProjection, characterPerMaterialSrv, charWorld,
                                               todLighting.sunDirection, todLighting.ambientColor,
                                               todLighting.sunColor, lightViewProjection, shadowSrv, shadowSmp);
            else
                characterRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker, nullptr,
                                       charWorld, nullptr, todLighting.sunDirection, todLighting.ambientColor,
                                       todLighting.sunColor, lightViewProjection, shadowSrv, shadowSmp);
            if (characterHeadTextured)
                characterHeadRenderer.drawTextured(device.context(), viewProjection, characterHeadPerMaterialSrv,
                                                   charWorld, todLighting.sunDirection, todLighting.ambientColor,
                                                   todLighting.sunColor, lightViewProjection, shadowSrv, shadowSmp);
            else
                characterHeadRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker,
                                           nullptr, charWorld, nullptr, todLighting.sunDirection,
                                           todLighting.ambientColor, todLighting.sunColor, lightViewProjection,
                                           shadowSrv, shadowSmp);
        }

        // Vehicle (THIS TASK) - drawn every frame whenever enabled, parked or
        // driving, from the real sim's CURRENT state (so a parked vehicle
        // renders at its own last-driven resting spot, not just its original
        // spawn - see PlaytestVehicleRig's own doc comment). A same-named
        // vehWorld was ALSO computed earlier this frame (hoisted above, for
        // the shadow depth-only pass) - that is a deliberately separate,
        // coarser copy (see the comment inside this block for why) and is
        // NOT reused here.
        if (vehicleEnabled) {
            // vehWorld (THIS BLOCK) is deliberately recomputed here with
            // vehicleGroundY rather than reusing the outer hoisted vehWorld
            // declared above for the shadow depth-only pass: that hoisted
            // copy uses finalGroundY, an intentional CHOSEN-v1 approximation
            // good enough for a shadow caster (see its own comment), while
            // the visible lit draw below must keep using vehicleGroundY -
            // the per-frame GroundFollower-smoothed height this file's own
            // "render code reads this INSTEAD OF the flat finalGroundY from
            // here on" comment (near vehicleGroundY's declaration) already
            // establishes as the correct value for actually placing the
            // vehicle on screen. Shadowing the outer vehWorld name in this
            // nested scope is intentional and does not affect the earlier
            // depth-only pass, which already captured its own copy by value.
            float vehWorld[16];
            craftWorldMatrix(sess, vehicleGroundY, vehWorld);  // AW-15: == buildWorldMatrix(...) for every non-craft car
            (sess.activeCarModel ? sess.activeCarModel->rig : vehicleRig)
                .Draw(device.context(), viewProjection, vehWorld, todLighting.sunDirection,
                      todLighting.ambientColor, todLighting.sunColor, lightViewProjection, shadowSrv, shadowSmp);
        }

        // Near-field ambient vehicle traffic (THIS TASK) - drawn via the SAME
        // already-loaded vehicleRig body/wheel mesh, once per live near-field
        // vehicle, at that vehicle's own live population position (see this
        // function's own "NEAR-FIELD AMBIENT VEHICLE TRAFFIC" doc-comment
        // update for why this reuses vehicleRig rather than loading up to 48
        // separate vehicle meshes - every ambient vehicle currently looks
        // like the same car model, an honest, documented simplification).
        // Skipped entirely under --no-vehicle (nothing loaded to reuse) -
        // the population/despawn bookkeeping above still runs regardless.
        // T3 helper: per-actor ground Y, re-sampled only after the actor moved > 0.25 m since the last sample.
        // Peds also update heading = atan2(vx, vz) (same convention as the player, HeadingFromForward) from
        // frame-to-frame displacement; heading is kept while stationary. CHOSEN: snap turn (spec-player-runtime
        // 5.4.3 step 5 describes an NPC turn-rate-limited heading but gives no value used here).
        // Ground reference is the actor's OWN graph-node Y (nodeY = NearField*::pos.y, from the road/sidewalk
        // graph, spec-zone-data-format 13.2a), NOT the player's height. CHOSEN: a ground sample is accepted only
        // when within 3 m of that node Y; otherwise (heightmap fallback returned -5/40 m with no ground mesh
        // loaded - the heightmap read is itself a CHOSEN un-cross-checked completion) node Y is used.
        auto sampleActorGroundY = [&](auto& pl, double x, double z, double nodeY, bool isPed) -> float {
            auto doSample = [&]() {
                pl.sampX = x;
                pl.sampZ = z;
                // G1: walkable collision ground from the actor's own node (spec 13.5.1 item 4: start = node Y + 2 m,
                // CHOSEN for a port that has only (x, z)); no real hit (tile not resident / hole) -> its node Y.
                const auto g = sess.sampleGround(x, z, nodeY + 2.0, static_cast<float>(nodeY));
                const float s = g.real() ? g.y : static_cast<float>(nodeY);
                pl.sampledMinusNode = s - static_cast<float>(nodeY);
                pl.y = std::fabs(pl.sampledMinusNode) <= 3.0f ? s : static_cast<float>(nodeY);
                ++sess.actorGroundSamples;
            };
            if (!pl.valid) {
                pl.valid = true;
                pl.lastX = x;
                pl.lastZ = z;
                doSample();
                return pl.y;
            }
            if (isPed) {
                const double mx = x - pl.lastX, mz = z - pl.lastZ;
                if (mx * mx + mz * mz > 1.0e-6) pl.heading = std::atan2(mx, mz);
            }
            pl.lastX = x;
            pl.lastZ = z;
            const double sx = x - pl.sampX, sz = z - pl.sampZ;
            if (sx * sx + sz * sz > 0.0625) doSample();
            return pl.y;
        };
        // Ambient model variety (this task): per-actor model from the hood's spawn category (spec-ai-runtime.md
        // 20.6.1(e)/20.6.2(e), see viewer_playtest_ambient_models.h). An actor whose model could not be chosen or
        // loaded (or --no-ambient-models) keeps the old placeholder draw below.
        AmbientModelRegistry* ambient =
            (sess.ambientModels_ && sess.ambientModels_->ready()) ? &*sess.ambientModels_ : nullptr;
        const bool ambientNight = [&] {
            const double f = todFraction - std::floor(todFraction);
            const int mins = static_cast<int>(f * 1440.0 + 1e-6);
            const int hhmm = (mins / 60) * 100 + (mins % 60);
            return hhmm >= 2200 || hhmm < 630;  // 20.6.0(b): day_begin 630 / day_end 2200
        }();
        if (vehicleEnabled && (vehicleRig.ready || ambient)) {
            for (const auto& nfv : nearFieldLive) {
                AmbientVehicleModel* ambientModel = nullptr;
                if (ambient) {
                    auto mIt = sess.nearFieldVehicleModel.find(nfv.id);
                    if (mIt == sess.nearFieldVehicleModel.end()) {
                        std::vector<AmbientModelRegistry::LiveVeh> liveAssigned;
                        for (const auto& other : nearFieldLive) {
                            if (other.id == nfv.id) continue;
                            auto oIt = sess.nearFieldVehicleModel.find(other.id);
                            if (oIt != sess.nearFieldVehicleModel.end() && oIt->second)
                                liveAssigned.push_back({oIt->second, other.pos.x, other.pos.z});
                        }
                        ambientModel = ambient->chooseVehicle(nfv.id, nfv.pos.x, nfv.pos.y, nfv.pos.z, ambientNight, liveAssigned);
                        sess.nearFieldVehicleModel[nfv.id] = ambientModel;  // null cached too: no per-frame retry
                    } else {
                        ambientModel = mIt->second;
                    }
                }
                if (!ambientModel && !vehicleRig.ready) continue;
                // REAL heading (TRAFFIC-LANE-FOLLOW-REAL-MECHANISM TASK,
                // 2026-10-10) - driven every frame by the lane-following
                // tick above (nearFieldVehicleHeading), REPLACING the prior
                // cosmetic deterministic-per-id formula. Falls back to the
                // old cosmetic formula only in the practically-unreachable
                // case a vehicle is drawn the SAME frame it spawned before
                // the drive tick above has run for it yet (defensive, not
                // expected to trigger - the drive tick runs earlier this
                // same frame, before this draw loop, for every entry
                // currently in nearFieldLive).
                const auto headingIt = nearFieldVehicleHeading.find(nfv.id);
                const double nfHeading = (headingIt != nearFieldVehicleHeading.end())
                                             ? headingIt->second
                                             : std::fmod(static_cast<double>(nfv.id) * 2.39996323, 6.28318530717958648);
                // T3: the vehicle's OWN ground Y. Ride height: spec-physics-runtime 10.11.5 is PARTIAL (no usable
                // numeric), so CHOSEN: 0 offset, same as the player vehicle's vehicleGroundY placement.
                const float nfY = sampleActorGroundY(sess.nearFieldVehiclePlace[nfv.id], nfv.pos.x, nfv.pos.z, nfv.pos.y, false);
                float nfWorld[16];
                sr3render::buildWorldMatrixSimHeading(static_cast<float>(nfv.pos.x), nfY,
                                            static_cast<float>(nfv.pos.z), static_cast<float>(nfHeading), 1.0f,
                                            nfWorld);
                (ambientModel ? ambientModel->rig : vehicleRig)
                    .Draw(device.context(), viewProjection, nfWorld, todLighting.sunDirection,
                          todLighting.ambientColor, todLighting.sunColor);
            }
            // Cars the player parked (enter-ambient-cars): same single model at their stored pose.
            for (const auto& pk : sess.parkedVehicles) {
                float pkWorld[16];
                craftParkedWorldMatrix(sess, pk, pkWorld);  // AW-15: == buildWorldMatrix(...) for every non-craft car
                (pk.model ? pk.model->rig : vehicleRig)
                    .Draw(device.context(), viewProjection, pkWorld, todLighting.sunDirection,
                          todLighting.ambientColor, todLighting.sunColor);
            }
        }

        // Near-field ambient pedestrian population (THIS TASK) - drawn via
        // the SAME already-loaded/posed brad character mesh (characterRenderer/
        // characterHeadRenderer) the police-roadblock ped category already
        // draws with (just below), once per live near-field ped, at that
        // ped's own live population position - following the near-field
        // vehicle traffic draw loop just above as its exact precedent: every
        // ambient pedestrian currently looks like the same character model,
        // the identical honest, stated simplification. Always drawn (no
        // --no-vehicle-style opt-out exists for the character mesh anywhere
        // else in this file either).
        for (const auto& nfp : nearFieldPedsLive) {
            auto& pedPl = sess.nearFieldPedPlace[nfp.id];
            const float nfpY = sampleActorGroundY(pedPl, nfp.pos.x, nfp.pos.z, nfp.pos.y, true);
            float nfPedWorld[16];
            sr3render::buildWorldMatrix(static_cast<float>(nfp.pos.x), nfpY, static_cast<float>(nfp.pos.z),
                                        static_cast<float>(pedPl.heading), 1.0f, nfPedWorld);
            AmbientPedModel* ambientPed = nullptr;
            if (ambient) {
                auto pIt = sess.nearFieldPedModel.find(nfp.id);
                if (pIt == sess.nearFieldPedModel.end()) {
                    ambientPed = ambient->choosePed(nfp.id, nfp.pos.x, nfp.pos.y, nfp.pos.z, ambientNight);
                    sess.nearFieldPedModel[nfp.id] = ambientPed;
                } else {
                    ambientPed = pIt->second;
                }
            }
            if (ambientPed) {
                // Own per-model skinned buffers, re-posed per ped with the specced walk clip (spec-ai-runtime 20.6.9(a); see
                // viewer_playtest_ambient_models.h poseWalk). Phase advances with the distance the ped actually moved.
                auto& pedAnim = sess.nearFieldPedAnim[nfp.id];
                double moved = 0.0;
                if (pedAnim.valid) moved = std::hypot(nfp.pos.x - pedAnim.lastX, nfp.pos.z - pedAnim.lastZ);
                pedAnim.lastX = nfp.pos.x;
                pedAnim.lastZ = nfp.pos.z;
                pedAnim.valid = true;
                if (sess.noPedAnim || !ambient->clipsReady()) {
                    ++ambient->mutableStats().pedDrawsStill;
                } else {
                    const double pedSpeed = sess.nearFieldPedWalkSpeed.count(nfp.id) ? sess.nearFieldPedWalkSpeed[nfp.id]
                                                                                     : sr3airuntime::kWanderWalkSpeedMsChosen;
                    const AmbientModelRegistry::WalkChoice wc = ambient->walkChoiceFor(*ambientPed, pedSpeed);
                    if (wc.record >= 0 && wc.strideM > 0.0f && wc.durationUnits > 0.0f) {
                        // Moving = displacement rate above 0.05 m/s; a jump over 3 m in one frame is a re-placement.
                        const bool moving = frm.dt > 0.0 && moved / frm.dt > 0.05 && moved < 3.0;
                        if (moving) {
                            pedAnim.cycles += moved / static_cast<double>(wc.strideM);
                            pedAnim.cycles -= std::floor(pedAnim.cycles);
                        } else {
                            pedAnim.cycles = 0.0;  // stationary: hold walk frame 0 (CHOSEN, no ped idle clip specced)
                        }
                        int tick = static_cast<int>(pedAnim.cycles * static_cast<double>(wc.durationUnits));
                        if (tick >= static_cast<int>(wc.durationUnits)) tick = 0;
                        const double cdx = nfp.pos.x - static_cast<double>(diagEyeX), cdz = nfp.pos.z - static_cast<double>(diagEyeZ);
                        size_t rank = 0;
                        for (const auto& o : nearFieldPedsLive) {
                            const double ox = o.pos.x - static_cast<double>(diagEyeX), oz = o.pos.z - static_cast<double>(diagEyeZ);
                            if (ox * ox + oz * oz < cdx * cdx + cdz * cdz) ++rank;
                        }
                        const bool allowPose =
                            cdx * cdx + cdz * cdz <= AmbientModelRegistry::kAnimRadiusM * AmbientModelRegistry::kAnimRadiusM &&
                            rank < static_cast<size_t>(AmbientModelRegistry::kMaxPoseUpdatesPerFrame);
                        bool uploaded = false;
                        const bool posed = ambient->poseWalk(*ambientPed, device.context(), wc, tick, allowPose, uploaded);
                        auto& animStats = ambient->mutableStats();
                        if (!posed) ++animStats.pedDrawsStale;
                        else if (moving) ++animStats.pedDrawsAnimated;
                        else ++animStats.pedDrawsStill;
                    }
                }
                // Same sun-shadow receive parameters the player's own character draw passes (parity with the brad path).
                AmbientModelRegistry::DrawPed(*ambientPed, device.context(), viewProjection, nfPedWorld,
                                              todLighting.sunDirection, todLighting.ambientColor, todLighting.sunColor,
                                              lightViewProjection, shadowSrv, shadowSmp);
                continue;
            }
            if (characterTextured)
                characterRenderer.drawTextured(device.context(), viewProjection, characterPerMaterialSrv, nfPedWorld,
                                               todLighting.sunDirection, todLighting.ambientColor,
                                               todLighting.sunColor);
            else
                characterRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker, nullptr,
                                       nfPedWorld, nullptr, todLighting.sunDirection, todLighting.ambientColor,
                                       todLighting.sunColor);
            if (characterHeadTextured)
                characterHeadRenderer.drawTextured(device.context(), viewProjection, characterHeadPerMaterialSrv,
                                                   nfPedWorld, todLighting.sunDirection, todLighting.ambientColor,
                                                   todLighting.sunColor);
            else
                characterHeadRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker,
                                           nullptr, nfPedWorld, nullptr, todLighting.sunDirection,
                                           todLighting.ambientColor, todLighting.sunColor);
        }

        // Police response units (feature/police-response, stepPolice): chase cars and on-foot cops, drawn with the
        // police models the registry resolved at spawn (placeholder: the player demo vehicle / brad character mesh).
        for (auto& pu : sess.police.units) {
            if (pu.dead) continue;
            if (pu.car) {
                AmbientVehicleModel* pm2 = pu.vmodel;
                if (!pm2 && !(vehicleEnabled && vehicleRig.ready)) continue;
                float puWorld[16];
                sr3render::buildWorldMatrixSimHeading(static_cast<float>(pu.pos.x), static_cast<float>(pu.pos.y),
                                            static_cast<float>(pu.pos.z), static_cast<float>(pu.heading), 1.0f, puWorld);
                (pm2 ? pm2->rig : vehicleRig)
                    .Draw(device.context(), viewProjection, puWorld, todLighting.sunDirection, todLighting.ambientColor,
                          todLighting.sunColor);
                continue;
            }
            const float puY = sampleActorGroundY(pu.place, pu.pos.x, pu.pos.z, pu.pos.y, true);
            float puPed[16];
            sr3render::buildWorldMatrix(static_cast<float>(pu.pos.x), puY, static_cast<float>(pu.pos.z),
                                        static_cast<float>(pu.place.heading), 1.0f, puPed);
            if (pu.pmodel) {
                AmbientModelRegistry::DrawPed(*pu.pmodel, device.context(), viewProjection, puPed, todLighting.sunDirection,
                                              todLighting.ambientColor, todLighting.sunColor, lightViewProjection, shadowSrv,
                                              shadowSmp);
            } else if (characterTextured) {
                characterRenderer.drawTextured(device.context(), viewProjection, characterPerMaterialSrv, puPed,
                                               todLighting.sunDirection, todLighting.ambientColor, todLighting.sunColor);
            } else {
                characterRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker, nullptr, puPed,
                                       nullptr, todLighting.sunDirection, todLighting.ambientColor, todLighting.sunColor);
            }
        }

        // Homies (feature/homies, stepHomies): the drive-up car, parked homie cars, and the followers on foot (a member
        // inside a car, boarding-hidden or seated is not drawn).
        {
            auto& hs = sess.homies;
            auto drawCar = [&](AmbientVehicleModel* m, const sr3airuntime::Vec3& p, double heading) {
                if (!m && !(vehicleEnabled && vehicleRig.ready)) return;
                float w[16];
                sr3render::buildWorldMatrixSimHeading(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z),
                                            static_cast<float>(heading), 1.0f, w);
                (m ? m->rig : vehicleRig).Draw(device.context(), viewProjection, w, todLighting.sunDirection,
                                               todLighting.ambientColor, todLighting.sunColor);
            };
            for (const auto& pc : hs.parkedCars) drawCar(pc.model, pc.pos, pc.heading);
            if (hs.callPhase >= 3) drawCar(hs.carModel, hs.carPos, hs.carHeading);
            for (auto& hm : hs.members) {
                if (hm.dead || hm.phase == PlaytestSession::HomieMember::Phase::InCar ||
                    hm.phase == PlaytestSession::HomieMember::Phase::Seated)
                    continue;
                const float hy = sampleActorGroundY(hm.place, hm.pos.x, hm.pos.z, hm.pos.y, true);
                float hw[16];
                sr3render::buildWorldMatrix(static_cast<float>(hm.pos.x), hy, static_cast<float>(hm.pos.z),
                                            static_cast<float>(hm.place.heading), 1.0f, hw);
                if (hm.pmodel) {
                    AmbientModelRegistry::DrawPed(*hm.pmodel, device.context(), viewProjection, hw, todLighting.sunDirection,
                                                  todLighting.ambientColor, todLighting.sunColor, lightViewProjection, shadowSrv,
                                                  shadowSmp);
                } else if (characterTextured) {
                    characterRenderer.drawTextured(device.context(), viewProjection, characterPerMaterialSrv, hw,
                                                   todLighting.sunDirection, todLighting.ambientColor, todLighting.sunColor);
                } else {
                    characterRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker, nullptr, hw,
                                           nullptr, todLighting.sunDirection, todLighting.ambientColor, todLighting.sunColor);
                }
            }
        }

        // T3: prune placement caches of despawned ids (same lazy size-threshold style as the other near-field maps).
        if (sess.nearFieldVehiclePlace.size() > nearFieldLive.size() + 64) {
            std::unordered_set<uint64_t> live;
            for (const auto& v : nearFieldLive) live.insert(v.id);
            for (auto it = sess.nearFieldVehiclePlace.begin(); it != sess.nearFieldVehiclePlace.end();)
                it = live.count(it->first) ? std::next(it) : sess.nearFieldVehiclePlace.erase(it);
            for (auto it = sess.nearFieldVehicleModel.begin(); it != sess.nearFieldVehicleModel.end();)
                it = live.count(it->first) ? std::next(it) : sess.nearFieldVehicleModel.erase(it);
        }
        if (sess.nearFieldPedPlace.size() > nearFieldPedsLive.size() + 64) {
            std::unordered_set<uint64_t> live;
            for (const auto& p : nearFieldPedsLive) live.insert(p.id);
            for (auto it = sess.nearFieldPedPlace.begin(); it != sess.nearFieldPedPlace.end();)
                it = live.count(it->first) ? std::next(it) : sess.nearFieldPedPlace.erase(it);
            for (auto it = sess.nearFieldPedModel.begin(); it != sess.nearFieldPedModel.end();)
                it = live.count(it->first) ? std::next(it) : sess.nearFieldPedModel.erase(it);
            for (auto it = sess.nearFieldPedAnim.begin(); it != sess.nearFieldPedAnim.end();)
                it = live.count(it->first) ? std::next(it) : sess.nearFieldPedAnim.erase(it);
        }

        // Police roadblock spawn visuals (THIS TASK) - one draw per
        // currently-filled slot with a cached visual (roadblockSlotVisual[],
        // maintained by the "POLICE ROADBLOCK TICK" block above). Peds reuse
        // the SAME already-loaded/posed brad character mesh (characterRenderer/
        // characterHeadRenderer) at a different world position, drawn
        // regardless of inVehicle (a roadblock ped standing in the street
        // stays visible whether the player is walking or driving past);
        // vehicles reuse vehicleRig, same reasoning as near-field traffic
        // above. Case 4 (ThirdCategoryCreationRequest - real-world identity
        // OPEN per that header's own CHOSEN note) has no mesh wired up here -
        // position bookkeeping only, honestly not drawn rather than guessing
        // a shape for it.
        for (size_t i = 0; i < roadblockSlotVisual.size(); ++i) {
            if (!roadblockManager.slots()[i].filled || !roadblockSlotVisual[i]) continue;
            const sr3airuntime::RoadblockDispatchResult& vis = *roadblockSlotVisual[i];
            if (vis.ped) {
                float pedWorld[16];
                sr3render::buildWorldMatrix(static_cast<float>(vis.ped->position.x), finalGroundY,
                                            static_cast<float>(vis.ped->position.z), 0.0f, 1.0f, pedWorld);
                if (characterTextured)
                    characterRenderer.drawTextured(device.context(), viewProjection, characterPerMaterialSrv,
                                                   pedWorld, todLighting.sunDirection, todLighting.ambientColor,
                                                   todLighting.sunColor);
                else
                    characterRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker,
                                           nullptr, pedWorld, nullptr, todLighting.sunDirection,
                                           todLighting.ambientColor, todLighting.sunColor);
                if (characterHeadTextured)
                    characterHeadRenderer.drawTextured(device.context(), viewProjection,
                                                       characterHeadPerMaterialSrv, pedWorld,
                                                       todLighting.sunDirection, todLighting.ambientColor,
                                                       todLighting.sunColor);
                else
                    characterHeadRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker,
                                               nullptr, pedWorld, nullptr, todLighting.sunDirection,
                                               todLighting.ambientColor, todLighting.sunColor);
            } else if (vis.vehicle && vehicleEnabled && vehicleRig.ready) {
                float vehWorldRb[16];
                sr3render::buildWorldMatrix(static_cast<float>(vis.vehicle->position.x), finalGroundY,
                                            static_cast<float>(vis.vehicle->position.z), 0.0f, 1.0f, vehWorldRb);
                vehicleRig.Draw(device.context(), viewProjection, vehWorldRb, todLighting.sunDirection,
                                todLighting.ambientColor, todLighting.sunColor);
            }
        }

        // --hud: composited LAST, on top of the 3D scene just drawn above -
        // VintQuadPipeline::begin()/quad()/rect() never call
        // OMSetRenderTargets (confirmed by reading the class), so this
        // draws straight onto the SAME swapchain RTV already bound, no
        // separate target or extra present needed. Polled every frame
        // (CHOSEN cadence, same as runHudLiveDemo()'s own health/money/
        // notoriety poll) - see this function's own doc comment for the
        // honest caveat that nothing in v0 currently changes these values.
        if (hudEnabled) {
            hudHost->runChunk(hudGp, "g_hp = get_current_hit_points('player')", "playtest_hud.lua");
            const double hudHp = readLuaGlobalNumber(hudGp, "g_hp");
            const int64_t hudAmmo = hudEs->weaponTemporaryInventory()["player"]["pistol"].ammoCount;
            // Gap #17: when the player has a weapon inventory, feed its live clip/reserve (spec-hud-runtime.md
            // Sec 717 ammo string form "clip/reserve"; infinite weapons show the clip only).
            std::string hudAmmoText = std::to_string(static_cast<long long>(hudAmmo));
            if (sess.weaponInv && sess.weaponInv->active()) {
                const auto* aw = sess.weaponInv->active();
                hudAmmoText = aw->inst.infiniteAmmo() ? std::string("INF")
                                                      : std::to_string(aw->inst.roundsInClip()) + "/" + std::to_string(sess.weaponInv->reserve(sess.weaponInv->activeIndex()));
                hudAmmoText += "  [" + aw->def.name + (sess.weaponInv->reloading() ? " RELOADING" : "") + "]";
            }
            const double hudCashDollars = hudEs->playerCash() / 100.0;
            auto hudSetLabel = [&](uint32_t h, const std::string& text) {
                sr3luahost::VintTaggedValue v;
                v.kind = sr3luahost::VintTaggedValue::Kind::String;
                v.text = text;
                hudEs->setVintProperty(h, "text_tag", {v});
            };
            // sr3hud wiring (Team C, HANDOFF-C.md section 6 step 2): every value goes through the H.1/H.2
            // producers (change-detected dataitems) and the HUD scripts' display model.
            sr3hud::PlayerStatusInput hudStatusIn;
            hudStatusIn.hp = static_cast<float>(hudHp);
            hudStatusIn.maxHp = static_cast<int32_t>(hudMaxHp);
            sr3hud::PushPlayerStatus(sess.hudItems.status, hudStatusIn);
            const float hudHealthPct = sess.hudItems.status.getFloat(sr3hud::status::HealthPct);
            sess.hudHealthDisplay.update(hudHealthPct, 0);  // health upgrade level: not modelled (CHOSEN 0)
            sr3hud::PlayerStatusInfrequentInput hudInfIn;
            hudInfIn.cashCents = std::llround(hudCashDollars * 100.0);
            sr3hud::PushPlayerStatusInfrequent(sess.hudItems.infrequent, hudInfIn);
            sess.hudCashDisplay.setCash(sess.hudItems.infrequent.getFloat(sr3hud::infrequent::Cash), false);
            sess.hudCashDisplay.tick();  // the hud_process count-up, once per frame
            // EngineState keeps one combined level per alias track (codes 0-3: the three gangs, then
            // police), so the display model is fed that level directly (CHOSEN: the {level, fraction}
            // record of spec-ai-runtime.md 19.1 is not what EngineState stores).
            std::array<float, 4> hudNotoLevels{};
            for (int i = 0; i < 4; ++i)
                hudNotoLevels[static_cast<size_t>(i)] = static_cast<float>(
                    (std::max)(0.0, (std::min)(5.0, hudEs->notorietyAliasTrackStates()[i].currentValue)));
            const sr3hud::NotorietyDisplayUpdate hudNoto = sess.hudNotorietyDisplay.update(hudNotoLevels);
            sr3hud::RespectInput hudRespectIn;
            hudRespectIn.totalRespect = static_cast<int32_t>(hudEs->respectTotal());
            hudRespectIn.level = static_cast<int32_t>(hudEs->respectLevel());
            hudRespectIn.progress = static_cast<double>(hudEs->respectBanked());
            hudRespectIn.need = static_cast<int32_t>(hudEs->respectLevelCost(hudEs->respectLevel()));
            sess.hudRespectProducer.push(sess.hudItems.respect, hudRespectIn, 0);
            const sr3hud::RespectMeterUpdate hudRespect = sess.hudRespectDisplay.update(
                hudRespectIn.totalRespect, sess.hudItems.respect.getFloat(sr3hud::respect::RespectPct),
                hudRespectIn.level);
            if (hudRespect.gained) sess.hudRespectGainText = "  +" + std::to_string(*hudRespect.gained);

            hudSetLabel(hudHealthH, "HEALTH " + std::to_string(static_cast<int>(hudHp)) + "/" +
                                        std::to_string(static_cast<int>(hudMaxHp)));
            hudSetLabel(hudAmmoH, "AMMO " + hudAmmoText);
            hudSetLabel(hudMoneyH, "MONEY " + sess.hudCashDisplay.text());
            hudSetLabel(hudNotorietyH, "NOTORIETY " + std::to_string(hudNoto.police.lit) +
                                           (hudNoto.gang.lit > 0 ? "  GANG " + std::to_string(hudNoto.gang.lit)
                                                                 : std::string()));
            hudSetLabel(sess.hudRespectH,
                        "RESPECT LEVEL " + hudRespect.levelText + "  " +
                            std::to_string(static_cast<long long>(hudRespectIn.progress)) + "/" +
                            std::to_string(hudRespectIn.need) + sess.hudRespectGainText);
            // Vehicle entry prompt (THIS TASK) - trivial on-screen mirror of
            // the console prompt printed by the "VEHICLE ENTRY/EXIT" block
            // above; empty string draws nothing (drawHudLabel's own existing
            // behaviour for an empty text_tag).
            hudSetLabel(hudVehiclePromptH,
                       (!inVehicle && vehiclePromptShown) ? "PRESS E TO ENTER VEHICLE" : std::string());

            // Objective-text overlay (THIS TASK, spec-hud-runtime.md §6.5) -
            // reads EngineState::objectiveTextSlots() directly (no new
            // native/dispatch logic - objective_text/objective_text_clear
            // already exist, see lua_spec_confirmed_stubs_b08_batch1_core.cpp/
            // b07_sec8_9_teamA.cpp). A slot is "active" exactly when it is
            // PRESENT in that map - objective_text_clear now erases the entry
            // on clear (engine_state.h's own recordObjectiveTextClear(), fixed
            // as part of THIS task so the overlay has a correct, live notion
            // of "cleared" to read, not a stale forever-shown line). Empty
            // string for an inactive slot draws nothing (drawHudLabel's own
            // existing empty-text_tag behaviour), so up to 4 lines, in slot
            // order, only the active ones ever appear. rawTextKey (UPDATED,
            // objective-text-localization task) is now resolved through the
            // SAME sr3locstr::LeStringsLookup mechanism runVintDoc()'s own
            // drawText() uses for a STRING text_tag (spec-localization-
            // format.md Sec6.1/Sec7 item 2: "tried as a localisation key
            // first"), falling back to the raw key string itself on a
            // lookup miss - the EXACT same miss behaviour drawText() already
            // has (its own decodeRawTextTag(rawTag, ...) fallback, which for
            // a plain ASCII key just redraws the key text verbatim - see
            // this file's narrowLocText() comment above drawHudLabel()).
            // combinedAuxText (arg3/arg4's own CONFIRMED combine rule) is
            // shown alongside it, not instead of it, since which of the two
            // a real player would actually see is OPEN (neither is
            // independently traced to a render-side consumer, spec-hud-
            // runtime.md §6.5's own "HUD-side readers OPEN" heading).
            for (size_t i = 0; i < hudObjectiveH.size(); ++i) {
                auto it = hudEs->objectiveTextSlots().find(static_cast<int64_t>(i));
                if (it == hudEs->objectiveTextSlots().end()) {
                    hudSetLabel(hudObjectiveH[i], std::string());
                    continue;
                }
                const sr3luahost::ObjectiveTextSlot& s = it->second;
                std::string label = (i == 0) ? "OBJECTIVE: " : ("OBJECTIVE " + std::to_string(i + 1) + ": ");
                if (std::optional<std::u16string> resolved = locLookup.lookupByKey(s.rawTextKey))
                    label += narrowLocText(*resolved);
                else
                    label += s.rawTextKey; // miss -> raw key string, same fallback drawText() already has
                if (!s.combinedAuxText.empty()) label += " (" + s.combinedAuxText + ")";
                hudSetLabel(hudObjectiveH[i], label);
            }

            hudPipe.begin(device.context(), swapChain.width(), swapChain.height(), false);
            const double barX0 = 40, barX1 = 340, barY0 = 56, barY1 = 84;
            const uint8_t barBg[4] = {60, 20, 20, 255};
            hudPipe.rect(device.context(), barX0, barY0, barX1, barY1, barBg);
            if (hudHealthPct > 0.0f) {
                const double clamped = (std::min)(static_cast<double>(hudHealthPct), 1.0);  // health_pct (H.2)
                const uint8_t fg[4] = {static_cast<uint8_t>(220 - 140 * clamped), static_cast<uint8_t>(60 + 140 * clamped),
                                       40, 255};
                drawHudBarFill(sess, barX0, barY0, barX0 + (barX1 - barX0) * clamped, barY1, fg);  // Team C: real HUD art
            }
            const uint8_t hudWhite[4] = {255, 255, 255, 255};
            drawHudLabel(*hudEs, hudHealthH, hudFont, hudPipe, device.context(), hudWhite);
            drawHudLabel(*hudEs, hudAmmoH, hudFont, hudPipe, device.context(), hudWhite);
            drawHudLabel(*hudEs, hudMoneyH, hudFont, hudPipe, device.context(), hudWhite);
            drawHudLabel(*hudEs, hudNotorietyH, hudFont, hudPipe, device.context(), hudWhite);
            drawHudLabel(*hudEs, sess.hudRespectH, hudFont, hudPipe, device.context(), hudWhite);
            drawActivityHud(sess, *hudEs, hudFont, hudPipe, device.context());  // Team C activities
            drawHudLabel(*hudEs, hudVehiclePromptH, hudFont, hudPipe, device.context(), hudWhite);
            for (uint32_t h : hudObjectiveH) drawHudLabel(*hudEs, h, hudFont, hudPipe, device.context(), hudWhite);

            // Pause overlay (THIS TASK) - drawn only while pauseMenuOpen (set by the Esc
            // edge-trigger in the main loop's own "PAUSE MENU" block below); a dim full-
            // screen rect behind the SAME real text-label mechanism every other HUD line
            // above already uses. Resume/quit TEXT is CHOSEN (labelled honestly in this
            // function's own top "PAUSE MENU" doc comment: full rendering of the real,
            // separately-loaded pause_menu_common.vint_doc element tree is not wired this
            // pass) - empty strings when closed draw nothing, same convention
            // hudVehiclePromptH already relies on.
            if (pauseMenuOpen) {
                const uint8_t dim[4] = {0, 0, 0, 160};
                hudPipe.rect(device.context(), 0, 0, static_cast<double>(swapChain.width()),
                            static_cast<double>(swapChain.height()), dim);
            }
            // sr3.exe: the front end's own pause menu replaces the three default labels below.
            const bool pauseHookDraws = pauseMenuOpen && static_cast<bool>(sess.hooks.pauseDraw);
            hudSetLabel(pauseTitleH, pauseMenuOpen && !pauseHookDraws ? "PAUSED" : std::string());
            hudSetLabel(pauseResumeH, pauseMenuOpen && !pauseHookDraws ? "Resume: Esc" : std::string());
            hudSetLabel(pauseQuitH, pauseMenuOpen && !pauseHookDraws ? "Quit: F10" : std::string());
            if (pauseHookDraws) sess.hooks.pauseDraw(sess);
            drawHudLabel(*hudEs, pauseTitleH, hudFont, hudPipe, device.context(), hudWhite);
            drawHudLabel(*hudEs, pauseResumeH, hudFont, hudPipe, device.context(), hudWhite);
            drawHudLabel(*hudEs, pauseQuitH, hudFont, hudPipe, device.context(), hudWhite);

            // ===================== MINIMAP OVERLAY ====================
            // Replaced 2026-10-17 (Team C): the former 200x200 top-right box (minimap_live_transform.h,
            // flat tile colour) is now the spec-hud-runtime.md H.3 corner minimap below.
            // sr3hud minimap (Team C, HANDOFF-C.md section 6 step 3; spec-hud-runtime.md H.3): centre C at
            // (191,566) reference px (x UI scale = height/720), speed zoom, camera-heading rotation,
            // the real minimap_CCRR texture registered at 1 m per texel, player glyph at C, north
            // marker on the R67 ring. CHOSEN until the hud.vint_doc map is rendered: a square window of
            // the ring's size instead of the ui_hud_map_map_mask stencil, a 10 px white square for
            // map_blip, no blips, rotating mode only (no static-minimap profile option wired).
            {
                const double ui = static_cast<double>(swapChain.height()) / 720.0;
                const sr3hud::Vec2 mmC{sr3hud::kMinimapCentreRef.x * ui, sr3hud::kMinimapCentreRef.y * ui};
                sr3hud::MinimapCentreInput mmCi;
                mmCi.playerX = pm.st.position.x;
                mmCi.playerZ = pm.st.position.z;
                mmCi.inVehicle = inVehicle;
                double mmSpeed = 0.0;
                if (inVehicle) {
                    mmCi.vehicleX = vehicleRig.sim->state_.posX;
                    mmCi.vehicleZ = vehicleRig.sim->state_.posZ;
                    const double vf = vehicleRig.sim->state_.velForwardBody, vr = vehicleRig.sim->state_.velRightBody;
                    mmSpeed = sr3hud::MinimapZoomSourceSpeed(std::sqrt(vf * vf + vr * vr), std::nullopt);
                }
                const sr3hud::Vec2 mmP = sr3hud::MinimapCentre(mmCi);
                sess.minimapZoom.update(mmSpeed, frm.dt);
                const double mmK = sr3hud::MinimapScale(sr3hud::MinimapZoom(sess.minimapZoom.level()), false, ui);
                const sr3hud::MinimapRotation mmRot = sr3hud::ComputeMinimapRotation(
                    false, sr3hud::HeadingFromForward(sess.minimapCamFwdX, sess.minimapCamFwdZ), 0.0);

                const sr3hud::TileCoord mmTile = sr3hud::CityTileAt(mmP.x, mmP.y);
                ID3D11ShaderResourceView* mmSrv = nullptr;
                if (sr3hud::InCityGrid(mmTile)) {
                    const auto key = std::make_pair(mmTile.col, mmTile.row);
                    if (sess.minimapTileTried.insert(key).second) {
                        const std::string name = "minimap_" + sr3hud::CityTileName(mmTile) + ".tga";  // record name (H.3 point 6)
                        for (const auto& a : sess.archives) {
                            sr3render::UploadedTexture ut;
                            std::string foundIn;
                            if (findAndUploadTexture(*a->top, name, device.device(), ut, foundIn, &a->texCache)) {
                                sess.minimapTileTex[key] = ut;
                                break;
                            }
                        }
                    }
                    auto it = sess.minimapTileTex.find(key);
                    if (it != sess.minimapTileTex.end()) mmSrv = it->second.srv;
                }
                const sr3hud::WorldBox mmBounds = sr3hud::MinimapPaddedBounds(sr3hud::CityTileBox(mmTile));

                // The visible window: the ring's square around C; each corner's texel follows the
                // inverse of screen = C + R(theta_m) k (-dX, +dZ).
                const double half = sr3hud::kMinimapRingDiameterRef * 0.5 * ui;
                const double mmCos = std::cos(-mmRot.mapTheta), mmSin = std::sin(-mmRot.mapTheta);
                double mmPos[4][2], mmUv[4][2];
                const double corner[4][2] = {{-half, -half}, {half, -half}, {-half, half}, {half, half}};
                for (int i = 0; i < 4; ++i) {
                    mmPos[i][0] = mmC.x + corner[i][0];
                    mmPos[i][1] = mmC.y + corner[i][1];
                    const double wx = (mmCos * corner[i][0] - mmSin * corner[i][1]) / mmK;
                    const double wy = (mmSin * corner[i][0] + mmCos * corner[i][1]) / mmK;
                    const sr3hud::Vec2 uv = sr3hud::MinimapTextureUV(mmBounds, mmP.x - wx, mmP.y + wy);
                    mmUv[i][0] = uv.x;
                    mmUv[i][1] = uv.y;
                }
                const uint8_t mmBlack[4] = {0, 0, 0, 255};
                hudPipe.rect(device.context(), mmC.x - half, mmC.y - half, mmC.x + half, mmC.y + half, mmBlack);
                if (mmSrv) {
                    const uint8_t mmTint[4] = {255, 255, 255, static_cast<uint8_t>(255 * sr3hud::kMinimapBaseMapAlpha)};
                    hudPipe.quad(device.context(), mmSrv, mmPos, mmUv, mmTint, 1, 640, 512);
                } else {
                    uint8_t mmFallback[4];
                    tileFallbackColour(mmTile.col, mmTile.row, mmFallback);
                    hudPipe.quad(device.context(), nullptr, mmPos, mmUv, mmFallback, 1, 1, 1);
                }
                const uint8_t mmWhite[4] = {255, 255, 255, 255};
                const double g = 5.0 * ui;
                hudPipe.rect(device.context(), mmC.x - g, mmC.y - g, mmC.x + g, mmC.y + g, mmWhite);
                const sr3hud::Vec2 mmN = sr3hud::MinimapNorthMarker(mmC, mmRot.mapTheta, ui);
                const uint8_t mmNorth[4] = {230, 60, 60, 255};
                hudPipe.rect(device.context(), mmN.x - g, mmN.y - g, mmN.x + g, mmN.y + g, mmNorth);
                drawHudNotorietyIcons(sess, mmC.x, mmC.y, ui, hudNoto.police.lit);  // Team C: real HUD art
                drawHudRespectMeter(sess, ui, sess.hudItems.respect.getFloat(sr3hud::respect::RespectPct));
            }
        }

        const bool wantCapture = !capturePath.empty() &&
            (!captureFrames.empty() ? std::find(captureFrames.begin(), captureFrames.end(), frames) != captureFrames.end()
                                     : frames == captureFrame);
        if (wantCapture) {
            if (!inVehicle) {
                LogWalkDemoAnimDiagnostics(frames, bridge, stateActions, realPoseEnabled ? &poseResult : nullptr,
                                           realPoseEnabled ? &bones : nullptr);
            }
            const std::string path = captureFrames.empty() ? capturePath
                                                            : capturePath + "_frame" + std::to_string(frames) + ".png";
            std::vector<uint8_t> pixels;
            if (swapChain.capture(device.context(), pixels, error) &&
                sr3render::writePng(path, swapChain.width(), swapChain.height(), pixels, error)) {
                if (!inVehicle) {
                    std::printf("captured : %s (frame %d, player pos (%.2f %.2f %.2f) heading %.1f deg, locomotion "
                                "%d, camera eye (%.2f %.2f %.2f))\n",
                                path.c_str(), frames, pm.st.position.x, pm.st.position.y, pm.st.position.z,
                                headingNow * 180.0 / 3.14159265, pm.st.locomotionState,
                                static_cast<double>(diagEyeX), static_cast<double>(diagEyeY),
                                static_cast<double>(diagEyeZ));
                } else {
                    // vehicleGroundY added to this diagnostic line (street-
                    // collision-0x2237-mopp follow-up task, 2026-10-11) -
                    // the vehicle's own render Y (this header's own top
                    // comment: "has no vertical physics model of any kind
                    // ... GroundFollower IS the entire vertical model"),
                    // now fed by SampleGroundMeshHeight() in preference to
                    // the heightmap/flat-plane chain - printed so a reader
                    // can directly see it tracking the real 0x2237 mesh
                    // height as the vehicle drives, not a fixed value.
                    std::printf("captured : %s (frame %d, DRIVING - vehicle pos (%.2f %.2f %.2f) heading %.1f deg, "
                                "speed %.2f m/s, camera eye (%.2f %.2f %.2f))\n",
                                path.c_str(), frames, vehicleRig.sim->state_.posX, static_cast<double>(vehicleGroundY),
                                vehicleRig.sim->state_.posZ, headingNow * 180.0 / 3.14159265,
                                vehicleRig.sim->state_.velForwardBody, static_cast<double>(diagEyeX),
                                static_cast<double>(diagEyeY), static_cast<double>(diagEyeZ));
                }
            } else {
                std::fprintf(stderr, "capture failed: %s\n", error.c_str());
            }
            // Shadow-map depth dump (DEBUG ONLY, this task, 2026-10-09,
            // sun-shadow-map out-of-frustum bug follow-up): reuses the SAME
            // CRREISH_SUN_SHADOW_DEBUG_FRUSTUM env var mesh_renderer.cpp's
            // own ps_main debug tint already reads - one switch turns on
            // BOTH halves of this diagnosis (what the shadow map itself
            // recorded, and what each sample's own occlusion test decided).
            // SunShadowMap::dumpDepthToPng()'s own comment explains why this
            // exists alongside the pixel-shader tint rather than instead of
            // it. Written next to the normal capture, same naming
            // convention (`<path>_shadowmap.png`), never on the default
            // (non-debug) capture path.
            if (shadowMapReady) {
                const char* dbgEnv = std::getenv("CRREISH_SUN_SHADOW_DEBUG_FRUSTUM");
                if (dbgEnv != nullptr && dbgEnv[0] != '0' && dbgEnv[0] != '\0') {
                    std::string shadowDumpError;
                    const std::string shadowDumpPath = path + "_shadowmap.png";
                    if (!sunShadowMap.dumpDepthToPng(device.device(), device.context(), shadowDumpPath,
                                                     shadowDumpError)) {
                        std::fprintf(stderr, "shadow map dump failed: %s\n", shadowDumpError.c_str());
                    } else {
                        std::printf("shadow dbg: %s\n", shadowDumpPath.c_str());
                    }
                }
            }
        }

        swapChain.present(true);
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames) {
            // gap #17: final weapon-state trace (only if the weapon inventory saw any activity).
            if (sess.weaponInv && (sess.scriptWeapons || sess.weaponInv->totalSwitches() > 0 || sess.weaponInv->totalReloads() > 0 || (sess.weaponInv->active() && sess.weaponInv->active()->shots > 0)))
                std::printf("%s\n", sess.weaponInv->traceLine().c_str());
            return true;
        }
    return false;
}

void reportPlaytest(PlaytestSession& sess) {
    auto& headless = sess.headless;
    auto& autoCombat = sess.autoCombat;
    auto& missionEnabled = sess.missionEnabled;
    auto& missionStem = sess.missionStem;
    auto& pm = *sess.pm_;
    auto& playtestRoadGraph = sess.playtestRoadGraph;
    auto& audioDevice = *sess.audioDevice_;
    auto& audioReady = sess.audioReady;
    auto& footstepsTriggered = sess.footstepsTriggered;
    auto& hudHost = sess.hudHost;
    auto& hudEs = sess.hudEs;
    auto& hudGp = sess.hudGp;
    auto& missionDriveActive = sess.missionDriveActive;
    auto& acNpcToAttack = sess.acNpcToAttack;
    auto& acNpcToCover = sess.acNpcToCover;
    auto& acPlayerShots = sess.acPlayerShots;
    auto& acPlayerHits = sess.acPlayerHits;
    auto& acTeleports = sess.acTeleports;
    auto& acHeals = sess.acHeals;
    auto& acKillsDamage = sess.acKillsDamage;
    auto& acKillsFallback = sess.acKillsFallback;
    auto& acHostileOrders = sess.acHostileOrders;
    auto& pedAi = sess.pedAi;
    auto& pedAiFleeCount = sess.pedAiFleeCount;
    auto& pedAiCowerCount = sess.pedAiCowerCount;
    auto& loaded = sess.loaded;
    auto& groundTiles = sess.groundTiles;
    auto& totalLoads = sess.totalLoads;
    auto& totalEvicts = sess.totalEvicts;
    auto& cursorHidden = sess.cursorHidden;
    auto& vehicleRig = *sess.vehicleRig_;
    auto& inVehicle = sess.inVehicle;
    auto& nearFieldLive = sess.nearFieldLive;
    auto& nearFieldVehicleStartPosForTrace = sess.nearFieldVehicleStartPosForTrace;
    auto& overtakeGateFireCountForTrace = sess.overtakeGateFireCountForTrace;
    auto& signalSwitchCount = sess.signalSwitchCount;
    auto& signalAmberCount = sess.signalAmberCount;
    auto& signalCarStops = sess.signalCarStops;
    auto& signalCarReleases = sess.signalCarReleases;
    auto& stopLineCarStops = sess.stopLineCarStops;
    auto& stopLineCarReleases = sess.stopLineCarReleases;
    auto& signalCarFramesOnControlled = sess.signalCarFramesOnControlled;
    auto& signalCarFramesOnRedAmber = sess.signalCarFramesOnRedAmber;
    auto& laneRequeryCount = sess.laneRequeryCount;
    auto& obstacleProbeRuns = sess.obstacleProbeRuns;
    auto& obstacleProbeReuses = sess.obstacleProbeReuses;
    auto& yieldBrakeFrames = sess.yieldBrakeFrames;
    auto& yieldStops = sess.yieldStops;
    auto& yieldRuns = sess.yieldRuns;
    auto& yieldConflictsFound = sess.yieldConflictsFound;
    auto& pedCrossWaits = sess.pedCrossWaits;
    auto& pedCrossCrosses = sess.pedCrossCrosses;
    auto& pedCrossAfterWait = sess.pedCrossAfterWait;
    auto& pedCrossUnsignalled = sess.pedCrossUnsignalled;
    auto& pedNavSearches = sess.pedNavSearches;
    auto& pedNavSearchFails = sess.pedNavSearchFails;
    auto& pedNavArrivals = sess.pedNavArrivals;
    auto& pedNavEdgesWalked = sess.pedNavEdgesWalked;
    auto& pedNavEvent8 = sess.pedNavEvent8;
    auto& nearFieldVehicleHeading = sess.nearFieldVehicleHeading;
    auto& nearFieldPedsLive = sess.nearFieldPedsLive;
    auto& finalGroundY = sess.finalGroundY;
    auto& roadblockSlots = sess.roadblockSlots;
    auto& roadblockManager = *sess.roadblockManager_;
    auto& roadblockDriver = *sess.roadblockDriver_;
    auto& frames = sess.frames;
    auto& missionTimedOutDump = sess.missionTimedOutDump;
    auto& autoCombatSink = *sess.autoCombatSink_;
    // overtake-gate-18.6b task diagnostic (2026-10-11), --headless only -
    // real per-vehicle net-displacement/gate-fire numbers for this task's
    // own live 900-frame re-verification requirement (spec-ai-runtime.md
    // Sec18.6b), not a permanent gameplay feature.
    if (headless) {
        double minDisp = -1.0, maxDisp = -1.0;
        for (const auto& nfv : nearFieldLive) {
            auto it = nearFieldVehicleStartPosForTrace.find(nfv.id);
            if (it == nearFieldVehicleStartPosForTrace.end()) continue;
            const double ddx = nfv.pos.x - it->second.x;
            const double ddz = nfv.pos.z - it->second.z;
            const double disp = std::sqrt(ddx * ddx + ddz * ddz);
            if (minDisp < 0.0 || disp < minDisp) minDisp = disp;
            if (maxDisp < 0.0 || disp > maxDisp) maxDisp = disp;
        }
        {
            // T3 actor-placement trace (live actors at end of run; vehicles + peds).
            size_t n = 0, nDiffY = 0, nHead = 0;
            double sumDy = 0.0;
            size_t nNodeFar = 0;
            double maxNodeDelta = 0.0;
            std::vector<double> dys;
            auto acc = [&](const auto& p, double heading) {
                if (!p.valid) return;
                ++n;
                const double dy = std::fabs(static_cast<double>(p.y) - static_cast<double>(finalGroundY));
                if (dy > 1.0e-4) ++nDiffY;
                sumDy += dy;
                dys.push_back(dy);
                const double nd = std::fabs(static_cast<double>(p.sampledMinusNode));
                if (nd > 1.0) ++nNodeFar;
                maxNodeDelta = std::max(maxNodeDelta, nd);
                if (heading != 0.0) ++nHead;
            };
            for (const auto& nfv : nearFieldLive) {
                auto it = sess.nearFieldVehiclePlace.find(nfv.id);
                if (it == sess.nearFieldVehiclePlace.end()) continue;
                const auto hIt = nearFieldVehicleHeading.find(nfv.id);  // vehicle heading = lane follower's
                acc(it->second, hIt != nearFieldVehicleHeading.end() ? hIt->second : 0.0);
            }
            for (const auto& nfp : nearFieldPedsLive) {
                auto it = sess.nearFieldPedPlace.find(nfp.id);
                if (it != sess.nearFieldPedPlace.end()) acc(it->second, it->second.heading);
            }
            std::sort(dys.begin(), dys.end());
            std::printf("actor-placement-trace: actors=%zu y_differs_from_player_ground=%zu mean_abs_dy=%.3f median_abs_dy=%.3f max_abs_dy=%.3f "
                        "sample_vs_node_gt1m=%zu max_sample_vs_node=%.3f heading_nonzero=%zu ground_samples=%llu frames=%d\n",
                        n, nDiffY, n ? sumDy / static_cast<double>(n) : 0.0, dys.empty() ? 0.0 : dys[dys.size() / 2],
                        dys.empty() ? 0.0 : dys.back(), nNodeFar, maxNodeDelta, nHead,
                        static_cast<unsigned long long>(sess.actorGroundSamples), frames);
        }
        std::printf(
            "traffic-overtake-gate-trace: frames=%d live_near_field_vehicles=%zu overtake_gate_fire_count=%llu "
            "net_displacement_range=[%.2f,%.2f]\n",
            frames, nearFieldLive.size(), static_cast<unsigned long long>(overtakeGateFireCountForTrace), minDisp,
            maxDisp);
        if (playtestRoadGraph) {
            std::printf(
                "traffic-signals-trace: switches=%llu amber_onsets=%llu car_signal_stops=%llu car_signal_releases=%llu "
                "car_stopline_stops=%llu car_stopline_releases=%llu car_frames_on_controlled_lane=%llu "
                "car_frames_on_red_or_amber_lane=%llu\n",
                static_cast<unsigned long long>(signalSwitchCount), static_cast<unsigned long long>(signalAmberCount),
                static_cast<unsigned long long>(signalCarStops), static_cast<unsigned long long>(signalCarReleases),
                static_cast<unsigned long long>(stopLineCarStops), static_cast<unsigned long long>(stopLineCarReleases),
                static_cast<unsigned long long>(signalCarFramesOnControlled),
                static_cast<unsigned long long>(signalCarFramesOnRedAmber));
            std::printf("traffic-review-trace: lane_requeries=%llu obstacle_probe_runs=%llu obstacle_probe_reuses=%llu yield_brake_frames=%llu yield_stops=%llu yield_runs=%llu yield_conflicts_found=%llu\n",
                        static_cast<unsigned long long>(laneRequeryCount), static_cast<unsigned long long>(obstacleProbeRuns),
                        static_cast<unsigned long long>(obstacleProbeReuses), static_cast<unsigned long long>(yieldBrakeFrames),
                        static_cast<unsigned long long>(yieldStops), static_cast<unsigned long long>(yieldRuns),
                        static_cast<unsigned long long>(yieldConflictsFound));
            std::printf("traffic-braking-trace: car_frames=%llu row_frames=%llu fallback_frames=%llu cars_with_row=%zu cars_ever_fallback=%zu aB_min=%.3f aB_max=%.3f (fallback AI_Max_Braking_Decel %.2f CHOSEN, condition 0 CHOSEN)\n",
                        static_cast<unsigned long long>(sess.aBCarFrames), static_cast<unsigned long long>(sess.aBRowFrames),
                        static_cast<unsigned long long>(sess.aBCarFrames - sess.aBRowFrames), sess.aBRowCars.size(),
                        sess.aBFallbackCars.size(), sess.aBCarFrames ? sess.aBMin : 0.0, sess.aBCarFrames ? sess.aBMax : 0.0,
                        kNearFieldVehicleAiMaxBrakingDecel);
            std::printf("ped-crossings-trace: waits=%llu crosses=%llu crosses_after_wait=%llu crosses_unsignalled=%llu\n",
                        static_cast<unsigned long long>(pedCrossWaits), static_cast<unsigned long long>(pedCrossCrosses),
                        static_cast<unsigned long long>(pedCrossAfterWait), static_cast<unsigned long long>(pedCrossUnsignalled));
            std::printf("ped-navigator-trace: path_searches=%llu search_fails=%llu route_nodes_reached=%llu arrivals=%llu event8_posted=%llu\n",
                        static_cast<unsigned long long>(pedNavSearches), static_cast<unsigned long long>(pedNavSearchFails),
                        static_cast<unsigned long long>(pedNavEdgesWalked), static_cast<unsigned long long>(pedNavArrivals),
                        static_cast<unsigned long long>(pedNavEvent8));
        }
    }
    if (sess.ambientModels_) {
        sess.ambientModels_->printTrace();
    } else {
        std::printf("ambient-models-trace: disabled (%s)\n", sess.noAmbientModels ? "--no-ambient-models" : "not initialised");
    }
    if (sess.jump_) {
        const sr3player::JumpStats& js = sess.jump_->stats();
        std::printf("jump-crouch-trace: jumps=%d landings=%d peak_above_takeoff=%.3f last_airtime_ms=%.0f air_frames=%d "
                    "crouch_frames=%d crouch_max_ground_speed=%.2f stand_max_ground_speed=%.2f airborne_at_exit=%d "
                    "jump_height_arg=%.2f final_y=%.3f\n",
                    js.jumps, js.landings, js.peakAboveTakeoff, js.lastAirtimeMs, js.airFrames, js.crouchFrames,
                    js.crouchMaxSpeed, js.standMaxSpeed, sess.jump_->airborne() ? 1 : 0, sess.jumpHeight,
                    sess.pm_ ? sess.pm_->st.position.y : 0.0);
    }
    printVehicleCollisionTrace(sess);
    printPoliceTrace(sess);
    printGpsTrace(sess);
    printCraftSummary(sess);  // AW-15 craft-trace / freefall-trace SUMMARY
    printHomieTrace(sess);
    if (sess.vitals_) {
        const sr3player::PlayerVitals& vv = *sess.vitals_;
        std::printf("player-vitals-trace: damage_events=%llu total_damage=%.0f deaths=%llu respawns=%llu min_health=%d max_health=%d "
                    "final_health=%d dead_at_exit=%d respawn_delay_ms=%lld\n",
                    static_cast<unsigned long long>(vv.stats().damageEvents), vv.stats().totalDamage,
                    static_cast<unsigned long long>(vv.stats().deaths), static_cast<unsigned long long>(vv.stats().respawns),
                    vv.stats().minHealth, vv.maxHealth(), vv.health(), vv.dead() ? 1 : 0,
                    static_cast<long long>(vv.respawnDelayMs()));
        std::printf("%s", sess.vitalsTrace.empty() ? "" : ("player-vitals-trace: events\n" + sess.vitalsTrace).c_str());
    } else {
        std::printf("player-vitals-trace: inactive (%s)\n", sess.missionEnabled ? "mission mode: mission death path owns the player" : "never initialised");
    }
    if (cursorHidden) ShowCursor(TRUE);
    // MISSION-HOST (this task, screen-fade-mainloop-tick/m09-progress): if the mission is
    // STILL suspended when this session ends (frame budget exhausted or window closed, not
    // finished/errored/timed out - those paths already print their own line above), say
    // precisely where every still-live mission script thread is waiting instead of silently
    // ending the run. "How far did the mission get" should always be a measured fact from
    // this real per-thread Lua debug info, never an inference from the absence of a print.
    if (missionEnabled && (missionDriveActive || missionTimedOutDump)) {
        for (uint16_t id : hudHost->bareGlobals().threads().liveIds()) {
            const sr3luahost::BareThreadTable::Record* rec = hudHost->bareGlobals().threads().findAlive(id);
            if (!rec) continue;
            // Level 0 is the C builtin the coroutine is parked inside (thread_yield, almost
            // always); level 1 is its Lua caller's own file:line - the one worth reporting.
            std::string at = "?";
            lua_Debug ar{};
            if (lua_getstack(rec->co, 1, &ar) && lua_getinfo(rec->co, "Sln", &ar)) {
                at = (ar.short_src[0] ? ar.short_src : "?") + std::string(":") + std::to_string(ar.currentline) +
                     (ar.name ? (std::string(" (in ") + ar.name + ")") : "");
            }
            std::printf("mission  : --mission=%s script thread '%s' (id %u) still suspended at %s when this "
                        "session ended\n",
                        missionStem.c_str(), rec->name.c_str(), static_cast<unsigned>(id), at.c_str());
            if (const char* evalCode = std::getenv("SR3_STALL_EVAL")) {  // DIAGNOSTIC ONLY (opt-in): run a chunk at stall time
                auto er = hudHost->runChunk(hudGp, evalCode, "stall_eval.lua");
                std::printf("[stall-eval] loadOk=%d pcallOk=%d %s\n", er.loadOk ? 1 : 0, er.pcallOk ? 1 : 0,
                            er.loadOk ? er.pcallError.c_str() : er.loadError.c_str());
            }
            if (std::getenv("SR3_TRACE_STALL_LOCALS")) {  // DIAGNOSTIC ONLY (opt-in): scalar locals of every Lua frame
                for (int lvl = 1; lvl < 6; ++lvl) {
                    lua_Debug fr{};
                    if (!lua_getstack(rec->co, lvl, &fr) || !lua_getinfo(rec->co, "Sln", &fr)) break;
                    std::printf("  [stall-frame %d] %s:%d (%s)", lvl, fr.short_src, fr.currentline, fr.name ? fr.name : "?");
                    for (int li = 1; li < 40; ++li) {
                        const char* ln = lua_getlocal(rec->co, &fr, li);
                        if (!ln) break;
                        const int ty = lua_type(rec->co, -1);
                        if (ty == LUA_TNUMBER) std::printf(" %s=%g", ln, lua_tonumber(rec->co, -1));
                        else if (ty == LUA_TSTRING) std::printf(" %s='%s'", ln, lua_tostring(rec->co, -1));
                        else if (ty == LUA_TBOOLEAN) std::printf(" %s=%s", ln, lua_toboolean(rec->co, -1) ? "true" : "false");
                        else std::printf(" %s=<%s>", ln, lua_typename(rec->co, ty));
                        lua_pop(rec->co, 1);
                    }
                    std::printf("\n");
                }
                {   // scalar globals carrying the mission's own prefix (e.g. SH01_HACK_PROGRESS)
                    std::string pfx;
                    for (char ch : missionStem) pfx += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                    pfx += "_";
                    lua_State* GL = hudGp;
                    lua_pushnil(GL);
                    while (lua_next(GL, LUA_GLOBALSINDEX)) {
                        if (lua_type(GL, -2) == LUA_TSTRING && std::string(lua_tostring(GL, -2)).rfind(pfx, 0) == 0) {
                            const int ty = lua_type(GL, -1);
                            if (ty == LUA_TNUMBER) std::printf("  [stall-global] %s=%g\n", lua_tostring(GL, -2), lua_tonumber(GL, -1));
                            else if (ty == LUA_TBOOLEAN) std::printf("  [stall-global] %s=%s\n", lua_tostring(GL, -2), lua_toboolean(GL, -1) ? "true" : "false");
                            else if (ty == LUA_TSTRING) std::printf("  [stall-global] %s='%s'\n", lua_tostring(GL, -2), lua_tostring(GL, -1));
                        }
                        lua_pop(GL, 1);
                    }
                }
            }
        }
    }
    // THIS TASK (world-streaming-reconcile): appends the real streaming totals (cell
    // loads/evictions and tiles still resident) to the end of this existing summary line -
    // the "frames   : %d presented" PREFIX real_playtest_smoke_test.cpp's/
    // real_playtest_vehicle_smoke_test.cpp's own `line.rfind(..., 0) == 0` checks stays
    // byte-for-byte unchanged (only text AFTER "presented" grows), so neither existing ctest
    // is affected - same parity reasoning as the EVICT logging added to reconcile() above.
    // FURTHER APPENDED (playtest-nearfield-traffic-roadblock task): near-field live
    // vehicle count and roadblock armed/filled state - again strictly AFTER the
    // existing "%zu cell evictions" field real_playtest_streaming_longwalk_test.cpp's
    // own sscanf() reads up through, so that test's own parse is unaffected too.
    // NPC AI loop summary (mission NPCs via EngineState::tickNpcAi, near-field peds via the viewer-owned runtime).
    if (hudEs && hudEs->npcAiEnabled()) {
        const auto& st = hudEs->npcAiStats();
        std::printf("npc-ai   : mission loop summary: ticks=%llu npc_ticks=%llu tracked=%zu shots=%llu hits=%llu "
                    "damage_to_player=%.1f no_player_position_ticks=%llu open_tick_skips=%llu\n",
                    static_cast<unsigned long long>(st.ticks), static_cast<unsigned long long>(st.npcsTicked),
                    hudEs->npcAi()->count(), static_cast<unsigned long long>(st.shotsFired),
                    static_cast<unsigned long long>(st.shotsHit), st.damageToPlayer,
                    static_cast<unsigned long long>(st.skippedNoPlayerPosition),
                    static_cast<unsigned long long>(hudEs->npcAi()->openErrorCount()));
        std::printf("npc-ai   :   hostile_npc_ticks=%llu (live, AI-driven; dead/ignore_ai suppressed=%llu) min_hostile_distance=%.1f; npc ticks by team (0 Playas 1 Luchadores 2 Deckers 3 Morningstar 4 Neutral "
                    "Gang 5 Police 6 STAG 7 Civilian 9 none):",
                    static_cast<unsigned long long>(st.hostileNpcTicks), static_cast<unsigned long long>(st.hostileSuppressedTicks), st.minHostileDistance);
        for (size_t t = 0; t < st.teamNpcTicks.size(); ++t)
            if (st.teamNpcTicks[t]) std::printf(" [%zu]=%llu", t, static_cast<unsigned long long>(st.teamNpcTicks[t]));
        std::printf("\n");
        std::printf("npc-ai   :   los_checks=%llu los_blocked=%llu cover_nodes=%llu cover_searches=%llu cover_found=%llu "
                    "cover_no_node=%llu cover_in_box=%llu cover_shield_rays=%llu downed_entered=%llu\n",
                    static_cast<unsigned long long>(st.losChecks), static_cast<unsigned long long>(st.losBlocked),
                    static_cast<unsigned long long>(st.coverNodes), static_cast<unsigned long long>(st.coverSearches),
                    static_cast<unsigned long long>(st.coverFound), static_cast<unsigned long long>(st.coverNoNode),
                    static_cast<unsigned long long>(st.coverNodesInBox), static_cast<unsigned long long>(st.coverShieldTests),
                    static_cast<unsigned long long>(st.downedEntered));
        if (st.coverSearches)
            std::printf("npc-ai   :   cover diagnostic: nearest pool node to a searching NPC = %.1f m (dy %.1f m)\n",
                        st.coverNearestNodeXZ, st.coverNearestNodeDy);
        const auto& ref = hudEs->npcAi()->openActionRefusals();
        for (size_t a = 0; a < ref.size(); ++a)
            if (ref[a]) std::printf("npc-ai   :   OPEN action routine %zu ('%s') refused %llu time(s) (not implemented in the port)\n",
                                    a, sr3airuntime::kActionTable[a].name, static_cast<unsigned long long>(ref[a]));
    }
    if (autoCombat && hudEs) {
        const auto& st = hudEs->npcAiStats();
        std::printf("auto-combat: summary: player_shots=%llu player_hits=%llu (damage-model applied=%llu, dmg=%.0f, refused=%llu) "
                    "npc_shots=%llu npc_hits=%llu damage_to_player=%.1f npc_mode_to_attack=%llu npc_mode_to_cover=%llu "
                    "teleports=%llu god_mode_heals=%llu hostile_orders=%llu kills_by_damage=%llu kills_by_fallback=%llu\n",
                    static_cast<unsigned long long>(acPlayerShots), static_cast<unsigned long long>(acPlayerHits),
                    static_cast<unsigned long long>(autoCombatSink.applied), autoCombatSink.damageDealt,
                    static_cast<unsigned long long>(autoCombatSink.refused), static_cast<unsigned long long>(st.shotsFired),
                    static_cast<unsigned long long>(st.shotsHit), st.damageToPlayer,
                    static_cast<unsigned long long>(acNpcToAttack), static_cast<unsigned long long>(acNpcToCover),
                    static_cast<unsigned long long>(acTeleports), static_cast<unsigned long long>(acHeals),
                    static_cast<unsigned long long>(acHostileOrders),
                    static_cast<unsigned long long>(acKillsDamage), static_cast<unsigned long long>(acKillsFallback));
    }
    if (pedAi) {
        std::printf("npc-ai   : near-field ped loop summary: ped_ticks=%llu tracked_now=%zu flee_entries=%llu cower_entries=%llu "
                    "open_tick_skips=%llu\n",
                    static_cast<unsigned long long>(pedAi->decisionStepCount()), pedAi->count(),
                    static_cast<unsigned long long>(pedAiFleeCount), static_cast<unsigned long long>(pedAiCowerCount),
                    static_cast<unsigned long long>(pedAi->openErrorCount()));
        const auto& ref = pedAi->openActionRefusals();
        for (size_t a = 0; a < ref.size(); ++a)
            if (ref[a]) std::printf("npc-ai   :   OPEN action routine %zu ('%s') refused %llu time(s) (not implemented in the port)\n",
                                    a, sr3airuntime::kActionTable[a].name, static_cast<unsigned long long>(ref[a]));
    }
    size_t roadblockFilledFinal = 0;
    for (const auto& s : roadblockManager.slots())
        if (s.filled) ++roadblockFilledFinal;
    {
        const auto& as = sess.ambientEntryStats;
        if (as.entries + as.genkiEntries + as.exits > 0)
            std::printf("ambient-entry-trace: entries=%d (ambient/parked cars; carjacks=%d reentries=%d) "
                        "original_car_entries=%d exits=%d distance_driven=%.1f m parked_cars=%zu rig_is_original=%d "
                        "driving_now=%d\n",
                        as.entries, as.carjacks, as.reentries, as.genkiEntries, as.exits, as.distanceDriven,
                        sess.parkedVehicles.size(), sess.rigCarIsGenki ? 1 : 0, sess.inVehicle ? 1 : 0);
        if (as.entries + as.genkiEntries + as.exits > 0) {
            std::string parked;
            for (const auto& pk : sess.parkedVehicles)
                parked += (parked.empty() ? "" : ",") + std::string(pk.model ? pk.model->key : (pk.isGenki ? "original(genki)" : "placeholder"));
            std::printf("ambient-entry-trace: active_car_model=%s parked_car_models=[%s] (own meshes: entered ambient cars keep nearFieldVehicleModel[id])\n",
                        sess.activeCarModel ? sess.activeCarModel->key.c_str() : "original(genki)", parked.c_str());
        }
    }
    if (sess.vehicleChaseCamStats.frames > 0) {
        const auto& vs = sess.vehicleChaseCamStats;
        std::printf("vehicle-camera-trace: driving_frames=%ld eye_to_pivot mean=%.4f min=%.4f max=%.4f m, "
                    "fov min=%.3f max=%.3f deg, collided_frames=%ld\n",
                    vs.frames, vs.distSum / static_cast<double>(vs.frames), vs.distMin, vs.distMax,
                    vs.fovMin * 180.0 / 3.14159265358979, vs.fovMax * 180.0 / 3.14159265358979, vs.collided);
    }
    if (inVehicle) {
        std::printf("frames   : %d presented (%zu cell loads, %zu cell evictions, %zu cells/%zu ground tiles "
                    "resident); final state DRIVING - vehicle pos (%.2f %.2f), speed %.2f m/s; near-field "
                    "traffic live=%zu/%d, roadblock armed=%d filled=%zu/%zu\n",
                    frames, totalLoads, totalEvicts, loaded.size(), groundTiles.size(), vehicleRig.sim->state_.posX,
                    vehicleRig.sim->state_.posZ, vehicleRig.sim->state_.velForwardBody, nearFieldLive.size(),
                    sr3airuntime::kNearFieldVehicleCap, roadblockDriver.armed ? 1 : 0, roadblockFilledFinal,
                    roadblockSlots.size());
    } else {
        std::printf("frames   : %d presented (%zu cell loads, %zu cell evictions, %zu cells/%zu ground tiles "
                    "resident); final player pos (%.2f %.2f %.2f), locomotion state %d; near-field traffic "
                    "live=%zu/%d, roadblock armed=%d filled=%zu/%zu\n",
                    frames, totalLoads, totalEvicts, loaded.size(), groundTiles.size(), pm.st.position.x,
                    pm.st.position.y, pm.st.position.z, pm.st.locomotionState, nearFieldLive.size(),
                    sr3airuntime::kNearFieldVehicleCap, roadblockDriver.armed ? 1 : 0, roadblockFilledFinal,
                    roadblockSlots.size());
    }
    // AUDIO verification summary (THIS TASK) - the "you can't literally hear it, verify
    // what you can" ask: real buffers actually submitted, bytes, and how many footstep-
    // cadence triggers fired vs. how many frames the player spent moving. audioDevice's
    // own destructor (end of this function's scope) does the real device/voice teardown -
    // nothing further to do here.
    if (audioReady) {
        std::printf("audio    : %llu buffers submitted (%llu bytes), %llu footstep-cadence triggers, "
                    "%zu voice(s) still active at exit\n",
                    static_cast<unsigned long long>(audioDevice.buffersSubmittedTotal()),
                    static_cast<unsigned long long>(audioDevice.bytesSubmittedTotal()),
                    static_cast<unsigned long long>(footstepsTriggered), audioDevice.activeVoiceCount());
    }
}

} // namespace sr3viewer
