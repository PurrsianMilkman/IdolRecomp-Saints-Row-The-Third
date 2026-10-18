// runPlaytest() split: frame-loop steps - frame begin, input, vehicle controls, world update.
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

bool stepFrameBegin(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& noInput = sess.noInput;
    auto& error = sess.error;
    auto& window = *sess.window_;
    auto& device = *sess.device_;
    auto& swapChain = *sess.swapChain_;
    auto& focusBox = *sess.focusBox_;
    auto& depthWidth = sess.depthWidth;
    auto& depthHeight = sess.depthHeight;
    auto& freq = sess.freq;
    auto& prevTime = sess.prevTime;
    auto& dt = frm.dt;
        if (!swapChain.resize(window.width(), window.height(), error)) return true;
        if (swapChain.width() != depthWidth || swapChain.height() != depthHeight) {
            if (focusBox.createDepth(device.device(), swapChain.width(), swapChain.height(), error)) {
                depthWidth = swapChain.width();
                depthHeight = swapChain.height();
            }
        }
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        frm.dt = static_cast<double>(now.QuadPart - prevTime.QuadPart) / static_cast<double>(freq.QuadPart);
        prevTime = now;
        if (dt > 0.25 || dt < 0.0) dt = 0.0;
        if (noInput) dt = 1.0 / 30.0;  // deterministic fixed step, same convention runWalkDemo() uses under --no-input
    return false;
}

bool stepInput(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& noInput = sess.noInput;
    auto& scriptForwardFrames = sess.scriptForwardFrames;
    auto& scriptTurnFrames = sess.scriptTurnFrames;
    auto& vehicleEnabled = sess.vehicleEnabled;
    auto& scriptVehicle = sess.scriptVehicle;
    auto& scriptRoadblock = sess.scriptRoadblock;
    auto& scriptFire = sess.scriptFire;
    const auto& visible = sess.visible;
    auto& moveSrc = sess.moveSrc;
    auto& lookSrc = sess.lookSrc;
    auto& hudHost = sess.hudHost;
    auto& hudEs = sess.hudEs;
    auto& hudGp = sess.hudGp;
    auto& pauseMenuOpen = sess.pauseMenuOpen;
    auto& prevEscDown = sess.prevEscDown;
    auto& prevF10Down = sess.prevF10Down;
    auto& pauseDocLoadAttempted = sess.pauseDocLoadAttempted;
    auto& hwnd = sess.hwnd;
    auto& cursorHidden = sess.cursorHidden;
    const auto& kMouseRadPerCount = sess.kMouseRadPerCount;
    const auto& kScriptVehicleExitFrame = sess.kScriptVehicleExitFrame;
    auto& frames = sess.frames;
    auto& nowMsAccum = sess.nowMsAccum;
    auto& todFraction = sess.todFraction;
    auto& tryLoadRealPauseDoc = sess.tryLoadRealPauseDoc;
    auto& dt = frm.dt;
    auto& vehInputLive = frm.vehInputLive;
    auto& vehEDownNow = frm.vehEDownNow;
    auto& tDownNow = frm.tDownNow;
    auto& radioPrevDownNow = frm.radioPrevDownNow;
    auto& radioNextDownNow = frm.radioNextDownNow;
    auto& roadblockKeyDownNow = frm.roadblockKeyDownNow;
    auto& fireKeyDownNow = frm.fireKeyDownNow;
        // ===================== PAUSE MENU (THIS TASK) ==========================
        // Live-only (same convention as the T/R keys below) - under --no-input there is no
        // real OS keyboard to poll, so this never fires in the ctest smoke test. See this
        // function's own top "PAUSE MENU" doc comment for the full real mechanism and the
        // honest caveats on what is/isn't reproduced.
        if (!noInput) {
            const bool escDownNow = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            const bool escEdge = escDownNow && !prevEscDown;
            prevEscDown = escDownNow;
            if (escEdge) {
                if (!pauseMenuOpen) {
                    // GATE: the real engine's 3 "already in blocking UI/cutscene" checks
                    // (0x007e1f40/0x007aaf00/0x007afb90) have no modelled equivalent here (see
                    // this function's own top doc comment) - the one condition both modelled
                    // and genuinely applicable to a single-dialog harness is "not already open",
                    // which this very `if` already is.
                    pauseMenuOpen = true;
                    if (!pauseDocLoadAttempted) tryLoadRealPauseDoc();
                    if (hudEs) {
                        hudEs->pushGamePauseReason(4);  // 0x007074a0(4, 1) - reason 4 only, see top doc comment
                    }
                    if (hudGp && hudHost) {
                        sr3luahost::LuaCoroutineLaunchResult dr =
                            sr3luahost::LaunchLuaCoroutine(hudGp, hudHost->bareGlobals().threads(),
                                                           "dialog_pause_show", 0, nullptr);
                        std::printf("pause    : OPENED (Esc) - pushGamePauseReason(4) -> depth=%d; "
                                    "\"dialog_pause_show\" hook %s\n",
                                    hudEs->gamePauseDepth().depth,
                                    dr.existedAsFunction ? "fired" : "not found in loaded Lua (no-op, same "
                                    "bookkeeping-only behaviour this project's other menu-runtime hooks already have)");
                    } else {
                        std::printf("pause    : OPENED (Esc) - no --hud/--mission EngineState this run, so the "
                                    "real pushGamePauseReason()/\"dialog_pause_show\" hook are not exercised; "
                                    "sim time is still frozen (dt forced to 0 below) and the window stays open\n");
                    }
                } else {
                    pauseMenuOpen = false;
                    if (hudEs) {
                        hudEs->popGamePauseReason(4);
                    }
                    if (hudGp && hudHost) {
                        sr3luahost::LaunchLuaCoroutine(hudGp, hudHost->bareGlobals().threads(), "dialog_pause_hide",
                                                       0, nullptr);
                    }
                    std::printf("pause    : CLOSED (Esc) - resuming%s\n",
                                hudEs ? "" : " (no EngineState this run)");
                }
            }
            // F10: the replacement quit key (Esc is repurposed above - Window::create's own
            // escQuits=false for this command). Edge-triggered so holding it doesn't matter;
            // breaks the main loop immediately, same as the window's own title-bar/X button.
            const bool f10DownNow = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
            const bool f10Edge = f10DownNow && !prevF10Down;
            prevF10Down = f10DownNow;
            if (f10Edge) {
                std::printf("quit     : F10 pressed - exiting\n");
                return true;
            }
        }
        // Freeze sim/frame time while the pause menu is open: this is the one choke point
        // nearly every per-frame system below reads dt (or nowMsAccum/todFraction, both derived
        // from it) from - see this function's own top "PAUSE MENU" doc comment for the full
        // list. Deliberately AFTER the escEdge handling above, so the SAME frame the menu opens
        // on already freezes (matching the real engine: the gate and the push happen in the
        // same per-frame query, not one frame later).
        if (pauseMenuOpen) dt = 0.0;
        nowMsAccum += static_cast<int64_t>(dt * 1000.0 + 0.5);
        // Advance the CHOSEN TOD clock (see its own declaration above) by one frame -
        // sr3render::AdvanceTodFraction() (tod_clock.h), not inlined here, so
        // tests/synthetic_tod_clock_test.cpp can exercise the real formula directly.
        todFraction = sr3render::AdvanceTodFraction(todFraction, dt, sr3render::kTodSecondsPerDay);

        // vehInputLive/vehEDownNow (THIS TASK): populated by both branches below,
        // consumed only if/when `inVehicle` is true (see the "VEHICLE ENTRY/
        // EXIT" block right after this if/else) - harmless to compute
        // unconditionally even while on foot, same spirit as this file's own
        // existing "compute both, use one" idioms.
        // tDownNow (v0.3 polish task) - the new TOD time-skip key's own raw
        // per-frame key state, live-input only (stays false under
        // --no-input; see the main loop's own "T key" block below for why no
        // scripted press is needed for this manual convenience control).
        // roadblockKeyDownNow (THIS TASK): same "populated by both branches,
        // edge-triggered after the if/else" shape as vehEDownNow/vehEEdge
        // just above - see the "POLICE ROADBLOCK ARM/DISARM" block right
        // after the vehicle entry/exit block below.
        // fireKeyDownNow (THIS TASK): same "populated by both branches,
        // edge-triggered after the if/else" shape as vehEDownNow/
        // roadblockKeyDownNow above - see the "WEAPON FIRE" block, further
        // below, right after the follow-camera's real eye/forward is
        // computed for this frame.
        if (!noInput) {
            // SUBSTITUTION 1 (live): real WASD + Shift, same GetAsyncKeyState polling shape
            // as runScene()'s own fly camera, applied to the move-input seam instead.
            const bool kW = (GetAsyncKeyState('W') & 0x8000) != 0, kS = (GetAsyncKeyState('S') & 0x8000) != 0;
            const bool kA = (GetAsyncKeyState('A') & 0x8000) != 0, kD = (GetAsyncKeyState('D') & 0x8000) != 0;
            moveSrc.B = (kW ? 1.0f : 0.0f) - (kS ? 1.0f : 0.0f);
            moveSrc.A = (kD ? 1.0f : 0.0f) - (kA ? 1.0f : 0.0f);
            moveSrc.sprintHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            // Vehicle controls (THIS TASK) - the SAME W/A/S/D keys, repurposed
            // while driving: W/S throttle/brake, A/D steer.
            vehInputLive.throttle = kW ? 1.0 : 0.0;
            vehInputLive.brake = kS ? 1.0 : 0.0;
            vehInputLive.steer = (kD ? 1.0 : 0.0) - (kA ? 1.0 : 0.0);
            vehEDownNow = vehicleEnabled && (GetAsyncKeyState('E') & 0x8000) != 0;
            tDownNow = (GetAsyncKeyState('T') & 0x8000) != 0;
            radioPrevDownNow = (GetAsyncKeyState(VK_OEM_COMMA) & 0x8000) != 0;
            radioNextDownNow = (GetAsyncKeyState(VK_OEM_PERIOD) & 0x8000) != 0;
            roadblockKeyDownNow = (GetAsyncKeyState('R') & 0x8000) != 0;
            // Left mouse button fires Brad's weapon (THIS TASK) - unused by
            // anything else in this file's playtest command (mouse motion
            // already drives look via the recentre block just below; no
            // other code here reads VK_LBUTTON).
            fireKeyDownNow = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            // T2: Space = jump (edge), C = crouch (held; keyboard hold mode, spec-player-runtime.md Sec3.6.3).
            // The spec gives no default key for CBA_OFC_JUMP_CLIMB / CBA_OFC_CROUCH (control_schemes row text is not
            // published; spec-tables-ui-controls.md only lists the key vocabulary) -> Space/C are the task's choice.
            const bool spaceDownNow = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            if (spaceDownNow && !sess.prevSpaceDown && !sess.inVehicle && sess.jump_) sess.jump_->requestJump();
            sess.prevSpaceDown = spaceDownNow;
            moveSrc.crouchHeld = (GetAsyncKeyState('C') & 0x8000) != 0;

            // Real mouse deltas, poll-and-recentre - only while the window actually has
            // focus (never yanks the user's cursor around if they alt-tab away).
            const bool focused = visible && (GetForegroundWindow() == hwnd);
            if (focused) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                POINT centreClient{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
                POINT centreScreen = centreClient;
                ClientToScreen(hwnd, &centreScreen);
                POINT cur{};
                GetCursorPos(&cur);
                const float dxPix = static_cast<float>(cur.x - centreScreen.x);
                const float dyPix = static_cast<float>(cur.y - centreScreen.y);
                lookSrc.rotateValue = dxPix * kMouseRadPerCount;
                lookSrc.elevateValue = -dyPix * kMouseRadPerCount;  // mouse up -> look up
                SetCursorPos(centreScreen.x, centreScreen.y);
                if (!cursorHidden) { ShowCursor(FALSE); cursorHidden = true; }
            } else {
                lookSrc.rotateValue = lookSrc.elevateValue = 0.0f;
                if (cursorHidden) { ShowCursor(TRUE); cursorHidden = false; }
            }
        } else {
            // SUBSTITUTION 1 (scripted fallback, --no-input): a deterministic
            // forward/turn/stop schedule - the SAME shape runWalkDemo()'s own scripted
            // sequence uses - plus a small scripted look pan during the turn phase, so
            // the mouse-look camera path is exercised deterministically too (labelled
            // here plainly: this is a scripted STAND-IN for real OS mouse input, not a
            // second real input source).
            if (frames < scriptForwardFrames) { moveSrc.A = 0.0f; moveSrc.B = 1.0f; }
            else if (frames < scriptForwardFrames + scriptTurnFrames) { moveSrc.A = 0.6f; moveSrc.B = 0.0f; }
            else { moveSrc.A = 0.0f; moveSrc.B = 0.0f; }
            moveSrc.sprintHeld = false;
            // T2 scripted jump/crouch (--script-jump-at=FRAME, --script-crouch-frames=A-B).
            moveSrc.crouchHeld = sess.scriptCrouchA >= 0 && frames >= sess.scriptCrouchA && frames <= sess.scriptCrouchB;
            for (int jf : sess.scriptJumpAt)
                if (frames == jf && !sess.inVehicle && sess.jump_) sess.jump_->requestJump();
            lookSrc.rotateValue = (frames >= scriptForwardFrames && frames < scriptForwardFrames + scriptTurnFrames)
                                      ? 0.02f
                                      : 0.0f;
            lookSrc.elevateValue = 0.0f;
            // --script-vehicle (THIS TASK): deterministic enter/drive/exit -
            // see this function's own "VEHICLE ENTRY/DRIVING" doc comment and
            // the kScriptVehicle* constants above the main loop for the
            // exact frame schedule.
            vehEDownNow = vehicleEnabled && scriptVehicle &&
                          (frames == kScriptVehicleEnterFrame || frames == kScriptVehicleExitFrame);
            // --script-enter-ambient-at=FRAME: the scripted stand-in for "walk up to an ambient car and press E":
            // stepVehicleControls warps the player beside the nearest ambient car at FRAME, E enters at FRAME+1,
            // E exits again scriptEnterAmbientDriveFrames later (same throttle script as --script-vehicle).
            if (vehicleEnabled && sess.scriptEnterAmbientAt >= 0) {
                // Cycle k (0..count-1) starts at at + k*cycle: warp at the start, E on +1, E again after the drive.
                // --script-enter-ambient-reenter adds, after the last cycle, a re-enter of the car just left (the
                // player is still beside it) and a second exit 30 frames later.
                const int cycle = sess.scriptEnterAmbientDriveFrames + 40;
                const int rel = frames - sess.scriptEnterAmbientAt;
                if (rel >= 0 && rel < cycle * sess.scriptEnterAmbientCount) {
                    const int inCycle = rel % cycle;
                    vehEDownNow = vehEDownNow || inCycle == 1 || inCycle == 1 + sess.scriptEnterAmbientDriveFrames;
                }
                if (sess.scriptEnterAmbientReenter) {
                    const int rbase = sess.scriptEnterAmbientAt + cycle * sess.scriptEnterAmbientCount;
                    vehEDownNow = vehEDownNow || frames == rbase || frames == rbase + 30;
                }
            }
            vehInputLive = sr3physics::DriverInput{0.6, 0.0, 0.08}; // mild constant turn, so the test also exercises steering
            // --script-roadblock (THIS TASK): deterministic arm-then-disarm
            // 'R' press pair - see the kScriptRoadblockArmFrame/
            // kScriptRoadblockDisarmFrame declarations above the main loop.
            roadblockKeyDownNow =
                scriptRoadblock && (frames == kScriptRoadblockArmFrame || frames == kScriptRoadblockDisarmFrame);
            // --script-fire (THIS TASK): a single deterministic left-click -
            // see kScriptFireFrame's own declaration (near kScriptRoadblock*
            // above the main loop) for the exact frame.
            fireKeyDownNow = scriptFire && frames == kScriptFireFrame;
        }
    return false;
}

void stepVehicleControls(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& noInput = sess.noInput;
    auto& vehicleEnabled = sess.vehicleEnabled;
    auto& scriptVehicle = sess.scriptVehicle;
    auto& pm = *sess.pm_;
    auto& heldFacing = sess.heldFacing;
    auto& followCam = *sess.followCam_;
    auto& radioPlayer = sess.radioPlayer;
    auto& radioLastStation = sess.radioLastStation;
    auto& finalGroundY = sess.finalGroundY;
    auto& vehicleGroundY = sess.vehicleGroundY;
    auto& vehicleRig = *sess.vehicleRig_;
    auto& inVehicle = sess.inVehicle;
    auto& prevEDown = sess.prevEDown;
    auto& prevTDown = sess.prevTDown;
    auto& prevRadioPrevDown = sess.prevRadioPrevDown;
    auto& prevRadioNextDown = sess.prevRadioNextDown;
    auto& vehiclePromptShown = sess.vehiclePromptShown;
    auto& roadblockDriver = *sess.roadblockDriver_;
    auto& prevRDown = sess.prevRDown;
    auto& frames = sess.frames;
    auto& todFraction = sess.todFraction;
    auto& dt = frm.dt;
    auto& vehEDownNow = frm.vehEDownNow;
    auto& tDownNow = frm.tDownNow;
    auto& radioPrevDownNow = frm.radioPrevDownNow;
    auto& radioNextDownNow = frm.radioNextDownNow;
    auto& roadblockKeyDownNow = frm.roadblockKeyDownNow;
        // ===================== VEHICLE ENTRY/EXIT (THIS TASK) =====================
        // Edge-triggered (vehEDownNow && !prevEDown) so holding E does not
        // toggle every frame - same debounce shape this task's own brief asks
        // for, applied to a key this file had not previously used.
        const bool vehEEdge = vehEDownNow && !prevEDown;
        prevEDown = vehEDownNow;

        // ===================== T KEY: TOD TIME-SKIP (v0.3 polish task) ============
        // Same edge-triggered debounce shape as vehEEdge/prevEDown just above
        // (holding T does not re-skip every frame) - CHOSEN manual fast-
        // forward so a human playtester can see a lighting change on demand
        // now that kTodSecondsPerDay (tod_clock.h) is slow enough that
        // waiting it out in real time is no longer practical during a short
        // play session. sr3render::JumpTodFractionHours()/
        // TodFractionToHourMinute() (tod_clock.h) - not inlined here, so
        // tests/synthetic_tod_clock_test.cpp can exercise the real formula.
        const bool tEdge = tDownNow && !prevTDown;
        prevTDown = tDownNow;
        if (tEdge) {
            todFraction = sr3render::JumpTodFractionHours(todFraction, sr3render::kTodHoursPerTKeyPress);
            int tHour = 0, tMinute = 0;
            sr3render::TodFractionToHourMinute(todFraction, tHour, tMinute);
            std::printf("[time] T pressed - skipped %.0f game hour(s) forward, time now %02d:%02d "
                        "(todFraction %.4f)\n",
                        sr3render::kTodHoursPerTKeyPress, tHour, tMinute, todFraction);
        }

        if (vehicleEnabled) {
            if (inVehicle) {
                // Distance accounting for the ambient-entry trace (ground-plane metres while driving).
                sess.ambientEntryStats.AddDrive(vehicleRig.sim->state_.posX - sess.ambientPrevX,
                                                vehicleRig.sim->state_.posZ - sess.ambientPrevZ);
                sess.ambientPrevX = vehicleRig.sim->state_.posX;
                sess.ambientPrevZ = vehicleRig.sim->state_.posZ;
            }
            if (!inVehicle) {
                // ---- Entry target selection (free-roam enter-ambient-cars task; spec-player-runtime.md Sec13.3). ----
                // Every car the player could climb into: the rig's own parked car (genki or the last car driven),
                // every live near-field ambient car (occupied = a carjack), and every car parked by an earlier swap.
                // Proximity alone picks the vehicle (Sec13.3: no angle rule); nearest-two + validate.
                auto& st = vehicleRig.sim->state_;
                const int ambCycle = sess.scriptEnterAmbientDriveFrames + 40;
                const int ambRel = frames - sess.scriptEnterAmbientAt;
                if (noInput && sess.scriptEnterAmbientAt >= 0 && ambRel >= 0 && ambRel % ambCycle == 0 &&
                    ambRel / ambCycle < sess.scriptEnterAmbientCount) {
                    // Scripted stand-in for "walk to the nearest ambient car": warp the player 2 m beside it.
                    const sr3airuntime::NearFieldVehicle* best = nullptr;
                    double bestD2 = 160.0 * 160.0;  // the spawn ring is 130 m (Sec20.6.1(c))
                    for (const auto& nfv : sess.nearFieldLive) {
                        const double ddx = nfv.pos.x - pm.st.position.x, ddz = nfv.pos.z - pm.st.position.z;
                        if (ddx * ddx + ddz * ddz < bestD2) { bestD2 = ddx * ddx + ddz * ddz; best = &nfv; }
                    }
                    if (best) {
                        const auto hIt = sess.nearFieldVehicleHeading.find(best->id);
                        const double h = hIt != sess.nearFieldVehicleHeading.end() ? hIt->second : 0.0;
                        pm.warp(sr3player::Vec3{best->pos.x - std::sin(h) * 2.0, best->pos.y, best->pos.z + std::cos(h) * 2.0});
                        std::printf("[ambient-entry] script: frame %d warped player beside ambient car %llu at (%.1f %.1f)\n",
                                    frames, static_cast<unsigned long long>(best->id), best->pos.x, best->pos.z);
                    } else {
                        std::printf("[ambient-entry] script: frame %d no ambient car within 160 m to walk to\n", frames);
                    }
                }
                std::vector<sr3player::AmbientEntryCandidate> cands;
                cands.reserve(1 + sess.nearFieldLive.size() + sess.parkedVehicles.size());
                {
                    sr3player::AmbientEntryCandidate c;
                    c.id = kAmbientRigCarId; c.x = st.posX; c.z = st.posZ;
                    cands.push_back(c);
                }
                for (const auto& nfv : sess.nearFieldLive) {
                    sr3player::AmbientEntryCandidate c;
                    c.id = nfv.id; c.x = nfv.pos.x; c.z = nfv.pos.z;
                    c.occupied = true;  // CHOSEN: every ambient car has a driver (no occupant model in the port)
                    // CHOSEN reading of the Sec13.4 occupant gate: a scripted "important" occupant makes the car
                    // un-enterable; ordinary ambient drivers are carjackable.
                    c.eligible = !sr3airuntime::AnySeatHasImportantOccupant(nfv.seatHasImportantOccupant);
                    cands.push_back(c);
                }
                for (const auto& pk : sess.parkedVehicles) {
                    sr3player::AmbientEntryCandidate c;
                    c.id = pk.id; c.x = pk.x; c.z = pk.z;
                    cands.push_back(c);
                }
                const sr3player::AmbientEntryPick pick = sr3player::PickAmbientEntryTarget(
                    pm.st.position.x, pm.st.position.z, cands, kAmbientEntryRange);
                const bool inRange = pick.index >= 0;
                if (inRange != vehiclePromptShown) {
                    std::printf(inRange ? "[vehicle] in range (%.1fm) - press E to enter%s\n"
                                        : "[vehicle] out of range%s\n",
                                static_cast<double>(pick.dist),
                                inRange && pick.carjack ? " (carjack)" : "");
                    vehiclePromptShown = inRange;
                }
                if (vehEEdge && inRange) {
                    const uint64_t id = cands[static_cast<size_t>(pick.index)].id;
                    bool carjack = false, reentry = false, rigCar = (id == kAmbientRigCarId);
                    if (!rigCar) {
                        // Resolve the target's pose/velocity first (before the rig's own car is filed away).
                        double tx = 0, tz = 0, th = 0, tSpeed = 0;
                        float ty = vehicleGroundY;
                        bool tGenki = false;
                        bool found = false;
                        std::vector<PlaytestSession::ParkedVehicle> keepParked = sess.parkedVehicles;
                        if (id >= kParkedIdBase) {
                            for (size_t i = 0; i < keepParked.size(); ++i) {
                                if (keepParked[i].id != id) continue;
                                const auto& pk = keepParked[i];
                                tx = pk.x; tz = pk.z; th = pk.heading; ty = pk.y; tGenki = pk.isGenki;
                                reentry = true; found = true;
                                keepParked.erase(keepParked.begin() + static_cast<std::ptrdiff_t>(i));
                                break;
                            }
                        } else {
                            for (size_t i = 0; i < sess.nearFieldLive.size(); ++i) {
                                const auto& nfv = sess.nearFieldLive[i];
                                if (nfv.id != id) continue;
                                tx = nfv.pos.x; tz = nfv.pos.z;
                                const auto hIt = sess.nearFieldVehicleHeading.find(id);
                                th = hIt != sess.nearFieldVehicleHeading.end() ? hIt->second : 0.0;
                                const auto sIt = sess.nearFieldVehicleSpeed.find(id);
                                tSpeed = sIt != sess.nearFieldVehicleSpeed.end() ? sIt->second : 0.0;
                                const auto pIt = sess.nearFieldVehiclePlace.find(id);
                                ty = (pIt != sess.nearFieldVehiclePlace.end() && pIt->second.valid)
                                         ? pIt->second.y : static_cast<float>(nfv.pos.y);
                                // Carjack: CHOSEN - the ambient driver is simply removed (no ped is modelled inside
                                // ambient cars, so there is nobody to exit/flee). The car leaves the traffic list so it
                                // is neither simulated nor drawn twice; per-id side maps are pruned by the next
                                // maintenance tick.
                                carjack = true; found = true;
                                sess.nearFieldLive.erase(sess.nearFieldLive.begin() + static_cast<std::ptrdiff_t>(i));
                                break;
                            }
                        }
                        if (found) {
                            // File the rig's current car away as a parked car (same model at its stored pose).
                            PlaytestSession::ParkedVehicle old;
                            old.id = kParkedIdBase + sess.nextParkedId++;
                            old.x = st.posX; old.z = st.posZ; old.heading = st.heading; old.y = vehicleGroundY;
                            old.isGenki = sess.rigCarIsGenki;
                            keepParked.push_back(old);
                            sess.parkedVehicles = std::move(keepParked);
                            // Load the target into the rig (position/heading/velocity carry over; the physics row stays
                            // the rig's own - there is only one chassis/handling row, see report).
                            st.posX = tx; st.posZ = tz; st.heading = th;
                            st.velForwardBody = tSpeed; st.velRightBody = 0.0; st.yawRate = 0.0;
                            st.steeringAngleCurrentRad = 0.0; st.steeringAngleTargetRad = 0.0;
                            sess.rigCarIsGenki = tGenki;
                            sess.vehicleGroundFollower_.emplace(static_cast<double>(ty));
                            vehicleGroundY = ty;
                        } else {
                            rigCar = true;  // target vanished between selection and use: fall back to the rig car
                        }
                    }
                    if (rigCar && !sess.rigCarIsGenki) reentry = true;  // the car the player last drove, still parked
                    auto& as = sess.ambientEntryStats;
                    if (!rigCar || !sess.rigCarIsGenki) {
                        ++as.entries;
                        if (carjack) ++as.carjacks;
                        if (reentry) ++as.reentries;
                    } else {
                        ++as.genkiEntries;
                    }
                    sess.ambientPrevX = st.posX; sess.ambientPrevZ = st.posZ;
                    inVehicle = true;
                    std::printf("[vehicle] entered %s%s - W/S throttle/brake, A/D steer, E to exit, ,/. radio prev/next\n",
                                carjack ? "ambient car (carjack, driver removed)"
                                        : (reentry ? "previously parked car" : (sess.rigCarIsGenki ? "parked car" : "car")),
                                (!rigCar && st.velForwardBody > 0.5) ? " (moving)" : "");
                    if (radioPlayer) radioLastStation = radioPlayer->enterVehicle(1, radioLastStation);
                }
            } else if (vehEEdge) {
                inVehicle = false;
                ++sess.ambientEntryStats.exits;
                if (radioPlayer) { radioLastStation = radioPlayer->station() > 0 ? radioPlayer->station() : 0xFF; radioPlayer->exitVehicle(); }
                // Exit position: a lateral offset from the vehicle's CURRENT
                // (not spawn) position/heading - see this function's own
                // "VEHICLE ENTRY/DRIVING" doc comment for why this is a
                // CHOSEN offset, not a real door-hinge position.
                const double exHeading = vehicleRig.sim->state_.heading;
                const double rightX = -std::sin(exHeading), rightZ = std::cos(exHeading);
                const double exitX = vehicleRig.sim->state_.posX + rightX * static_cast<double>(kVehicleExitOffset);
                const double exitZ = vehicleRig.sim->state_.posZ + rightZ * static_cast<double>(kVehicleExitOffset);
                // (y: the car's own ground height, not the spawn-area finalGroundY - the car may be far away now)
                pm.warp(sr3player::Vec3{exitX, static_cast<double>(sess.vehicleGroundY), exitZ});
                pm.world.orientation.set(sr3playeranim::OrientationFromFacing(heldFacing));
                sr3playeranim::DriveCameraTarget(followCam, pm);
                followCam.reset(); // immediate snap to the new (post-exit) position, not a long fly-over
                vehiclePromptShown = false;
                std::printf("[vehicle] exited at (%.2f %.2f)\n", exitX, exitZ);
            }
        }

        // ===================== VEHICLE RADIO (spec-audio-runtime.md Sec14.6) =====================
        // ',' / '.' = previous / next station while driving (CHOSEN D-pad stand-ins; edge-triggered).
        // --script-vehicle drives the same path deterministically: next at enter+30, previous at enter+60.
        if (radioPlayer) {
            bool radioPrevEdge = radioPrevDownNow && !prevRadioPrevDown;
            bool radioNextEdge = radioNextDownNow && !prevRadioNextDown;
            prevRadioPrevDown = radioPrevDownNow; prevRadioNextDown = radioNextDownNow;
            if (noInput && scriptVehicle && vehicleEnabled) {
                radioNextEdge = radioNextEdge || frames == kScriptVehicleEnterFrame + 30;
                radioPrevEdge = radioPrevEdge || frames == kScriptVehicleEnterFrame + 60;
            }
            if (inVehicle && radioNextEdge) radioPlayer->cycle(+1);
            if (inVehicle && radioPrevEdge) radioPlayer->cycle(-1);
            radioPlayer->update((uint32_t)(dt * 1000.0 + 0.5));
        }

        // ===================== POLICE ROADBLOCK ARM/DISARM (THIS TASK) =====================
        // Edge-triggered (roadblockKeyDownNow && !prevRDown), same debounce
        // shape as vehEEdge above - a held key toggles exactly once, not
        // every frame.
        const bool roadblockEdge = roadblockKeyDownNow && !prevRDown;
        prevRDown = roadblockKeyDownNow;
        if (roadblockEdge) {
            roadblockDriver.armed = !roadblockDriver.armed;
            std::printf("[roadblock] %s\n", roadblockDriver.armed ? "armed" : "disarmed");
        }
}

void stepWorld(PlaytestSession& sess, PlaytestFrame& frm) {
    auto& hudEnabled = sess.hudEnabled;
    auto& vehicleEnabled = sess.vehicleEnabled;
    auto& pm = *sess.pm_;
    auto& device = *sess.device_;
    auto& hudHost = sess.hudHost;
    auto& hudGp = sess.hudGp;
    auto& realCameraCollision = *sess.realCameraCollision_;
    auto& reconcile = sess.reconcile;
    auto& finalGroundY = sess.finalGroundY;
    auto& sampleGroundHeight = sess.sampleGroundHeight;
    auto& vehicleGroundFollower = *sess.vehicleGroundFollower_;
    auto& vehicleGroundY = sess.vehicleGroundY;
    auto& vehicleRig = *sess.vehicleRig_;
    auto& inVehicle = sess.inVehicle;
    auto& frames = sess.frames;
    auto& dt = frm.dt;
    auto& vehInputLive = frm.vehInputLive;
    const auto& focusX = frm.focusX;
    const auto& focusZ = frm.focusZ;
        // ================= OBJECTIVE-TEXT OVERLAY DEMO (THIS TASK) =================
        // Scripted stand-in for real mission content (v0 has none wired into
        // this command - see kScriptObjectiveSetFrame/kScriptObjectiveClearFrame's
        // own doc comment above): fires the REAL objective_text/
        // objective_text_clear Lua natives through hudHost, exactly as a real
        // mission script would, so the per-frame overlay update just below
        // reads genuine engine state, not a value poked in directly.
        //
        // UPDATED (objective-text-localization task): the two calls below are
        // copied VERBATIM (arg 1/2/3/4 - slot, key, arg3, arg4) from real
        // `m01.lua` text, extracted per spec-vpp-container.md §7.6's recipe
        // (see this file's own comment above kScriptObjectiveSetFrame) - line
        // 1068 (`objective_text( 0, "m01_kill_lobby_guards", "", "", ... )`)
        // and line 1074 (`objective_text( 0, "m01_go_to_vault", "", "", ... )`
        // - moved to slot 1 here only so both slots render at once, exactly
        // as the former placeholder demo already did). `SYNC_ALL`/
        // `OI_ASSET_KILL`/`OI_ASSET_LOCATION` (arg 5/6 in the real call) are
        // not registered Lua globals in this host, so they read as `nil`
        // here and fall through to stub_objective_text's own optional
        // defaults (3/0) - arg 5/6 are consumed but never read back by this
        // overlay (see stub_objective_text's own doc comment,
        // lua_spec_confirmed_stubs_b08_batch1_core.cpp), so this has no
        // effect on what renders. Both keys resolve through
        // `mission_us.le_strings` (loaded above) to real English text -
        // replacing this file's former "MAIN_OBJECTIVE_KEY"/
        // "SIDE_OBJECTIVE_KEY" invented placeholders, which were never a key
        // any real mission script passes.
        if (hudEnabled) {
            if (frames == kScriptObjectiveSetFrame) {
                hudHost->runChunk(hudGp,
                                   "objective_text(0, 'm01_kill_lobby_guards', '', '', SYNC_ALL, OI_ASSET_KILL)\n"
                                   "objective_text(1, 'm01_go_to_vault', '', '', SYNC_ALL, OI_ASSET_LOCATION)",
                                   "playtest_hud_objective.lua");
                std::printf("[objective] slot 0/1 set via objective_text (frame %d)\n", frames);
            } else if (frames == kScriptObjectiveClearFrame) {
                hudHost->runChunk(hudGp, "objective_text_clear(1)", "playtest_hud_objective.lua");
                std::printf("[objective] slot 1 cleared via objective_text_clear (frame %d)\n", frames);
            }
        }

        // THIS TASK (2026-10-08, spec-physics-runtime.md §10a): the vehicle now
        // tests against the SAME real `.clmesh_pc` collision geometry
        // realCameraCollision already queries for the follow camera (and
        // collisionWorld, further above, already queries for the player's
        // own on-foot CharacterProxyDriver) - see Advance()/
        // ApplyWorldCollisionResponse()'s own doc comments (PlaytestVehicleRig,
        // above runDriveDemo() in this file) for the full account. worldY is
        // the SAME vehicle-pivot height used for rendering/the chase cam just
        // below (finalGroundY + kVehPivotHeight) - this model is planar, so
        // there is only ever one world Y to sweep at.
        if (inVehicle)
            vehicleRig.Advance(device.device(), dt, vehInputLive, &realCameraCollision,
                                static_cast<double>(finalGroundY) + static_cast<double>(kVehPivotHeight));

        // REAL per-frame heightmap ground height for the vehicle (THIS
        // TASK) - see this function's own "REAL per-frame heightmap-based
        // ground following" doc comment above the collision-hook wiring
        // for the full citation. GroundFollower IS the vehicle's entire
        // vertical model (PlaytestVehicleRig has none of its own) - eased,
        // never a hard per-frame teleport, even though in practice a
        // PARKED car's own (x,z) rarely changes tile.
        if (vehicleEnabled) {
            const float vehRawH = sampleGroundHeight(vehicleRig.sim->state_.posX, vehicleRig.sim->state_.posZ,
                                                      finalGroundY);
            vehicleGroundY = static_cast<float>(vehicleGroundFollower.update(vehRawH, dt).groundY);
        }

        frm.focusX = inVehicle ? static_cast<float>(vehicleRig.sim->state_.posX)
                                       : static_cast<float>(pm.st.position.x);
        frm.focusZ = inVehicle ? static_cast<float>(vehicleRig.sim->state_.posZ)
                                       : static_cast<float>(pm.st.position.z);
        reconcile(frames, focusX, focusZ);
}

} // namespace sr3viewer
