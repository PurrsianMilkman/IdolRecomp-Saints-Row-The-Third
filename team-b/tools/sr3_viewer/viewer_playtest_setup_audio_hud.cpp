// runPlaytest() split: audio and --hud/--mission/pause-menu setup.
// Code moved verbatim out of the former single-function runPlaytest() (see viewer_playtest_session.h).
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

namespace sr3viewer {

int setupAudio(PlaytestSession& sess) {
    const auto& charactersPath = sess.charactersPath;
    const auto& miscTablesPath = sess.miscTablesPath;
    auto& headless = sess.headless;
    auto& noInput = sess.noInput;
    auto& noAudio = sess.noAudio;
    auto& sfxArchiveOverride = sess.sfxArchiveOverride;
    auto& audioReady = sess.audioReady;
    auto& radioPlayer = sess.radioPlayer;
    // ---- AUDIO OUTPUT DEVICE (THIS TASK, audio-playback, 2026-10-10) -------------------
    // Opens a real XAudio2 device ONLY for a genuinely interactive run: `headless` and
    // `noInput` are the two existing ctest-smoke-test gates (see this function's own
    // --headless/--no-input doc comment above) and BOTH independently veto audio,
    // regardless of --no-audio - the critical requirement this task's own brief states
    // ("the existing --headless/--no-input ctest smoke-test paths never open an audio
    // device at all, regardless of this flag"). --no-audio is the third, explicit opt-out
    // for an otherwise-interactive run. sr3_viewer's OTHER commands (runCity/runWalkDemo/
    // etc.) never call AudioOutputDevice at all - this is the one, deliberately narrow,
    // first wiring point this task's brief asks for.
    const bool audioWanted = !headless && !noInput && !noAudio;
    sess.audioDevice_.emplace();
    auto& audioDevice = *sess.audioDevice_;
    if (audioWanted) {
        std::string audioError;
        audioReady = audioDevice.initialise(audioError);
        std::printf("audio    : %s%s\n", audioReady ? "XAudio2 device opened" : "NOT opened",
                    audioReady ? "" : (" (" + audioError + ")").c_str());
    } else {
        std::printf("audio    : disabled (%s)\n",
                    headless ? "--headless" : noInput ? "--no-input" : "--no-audio");
    }
    // Free-roam radio (spec-audio-runtime.md Sec14.6): the 12 per-station cue tables from misc_tables are
    // loaded into the content table, every station's queue is built at world start, and entering a vehicle
    // selects the default station and plays the current queue item through the XAudio2 device (seeking into
    // it per the continuous timeline). Headless/--no-input/--no-audio never open a device: the player then
    // only runs the logic and resolves cues (SR3_RADIO_DRY_DECODE=1 additionally decodes, no output).
    // Keys (CHOSEN, D-pad stand-ins): ',' previous station, '.' next station, while driving.
    sess.radio_.emplace();
    auto& radio = *sess.radio_;
    sess.radioXtbl_.emplace();
    auto& radioXtbl = *sess.radioXtbl_;
    sess.radioMedia_.emplace();
    auto& radioMedia = *sess.radioMedia_;
    {
        std::string rerr;
        sr3radio::RadioLoadReport rrep;
        const bool loaded = radioXtbl.open(miscTablesPath, rerr) &&
                            sr3radio::loadRadioContent(radioXtbl.provider(), radio, rrep, rerr);
        if (!loaded) {
            std::printf("radio    : content load FAILED (%s) - retail roster with empty playlists\n", rerr.c_str());
            radio.stations = sr3radio::retailRoster();
        }
        // The engine seeds the ring from the performance counter (time-based); fixed seed for the
        // deterministic smoke paths.
        radio.rng.reseed((headless || noInput) ? 5489u : (uint32_t)GetTickCount64() * 2654435761u);
        radio.worldStart();
        bool mediaOk = false;
        if (loaded) {
            const std::string sndPath = (std::filesystem::path(miscTablesPath).parent_path() / "sounds_common.vpp_pc").string();
            std::string merr;
            mediaOk = std::filesystem::exists(sndPath) && radioMedia.open(sndPath, merr);
            std::printf("radio    : %zu content entries, %zu song records, %zu stations; media %s (%zu records)%s%s\n",
                        rrep.contentEntries, rrep.songRecords, radio.stations.size(), mediaOk ? "indexed" : "UNAVAILABLE",
                        radioMedia.mediaRecordCount(), mediaOk ? "" : " - ", mediaOk ? "" : merr.c_str());
            for (const auto& w : rrep.warnings) std::printf("radio    : warning: %s\n", w.c_str());
            // Startup self-check (resolve only, no decode): first queued item of every selectable station.
            for (size_t i = 0; i < radio.stations.size(); ++i) {
                const auto& st = radio.stations[i];
                const sr3radio::ContentEntry* ce = radio.current(st);
                if (!ce) continue;
                sr3radio::MediaLoc loc; std::string why;
                const bool ok = mediaOk && radioMedia.resolve(ce->cueId, ce->type, loc, why);
                std::printf("radio    : station %zu %-16s queue=%zu first=%s -> %s\n", i + 1, st.name.c_str(), st.queue.size(),
                            ce->cue.c_str(), ok ? "media resolved" : ("unresolved: " + why).c_str());
            }
        }
        const char* dry = std::getenv("SR3_RADIO_DRY_DECODE");
        radioPlayer = std::make_unique<sr3radio::RadioPlayer>(radio, mediaOk ? &radioMedia : nullptr,
                                                              audioReady ? &audioDevice : nullptr,
                                                              dry != nullptr && dry[0] == '1');
    }
    // The footstep-cadence demo clip (see findFirstPlayableSfxClip()'s own doc comment,
    // above runPlaytest() in this file, for exactly what this is and is NOT) - resolved
    // once, up front, only when a device is actually open (no point walking a real archive
    // and decoding Vorbis for a run that will never submit a buffer).
    sess.footstepClip_.emplace();
    auto& footstepClip = *sess.footstepClip_;
    if (audioReady) {
        std::string sfxArchivePath = sfxArchiveOverride;
        if (sfxArchivePath.empty()) {
            const size_t slash = charactersPath.find_last_of("/\\");
            const std::string dir = slash == std::string::npos ? std::string() : charactersPath.substr(0, slash + 1);
            // CORRECTED default (this task, empirically, 2026-10-10): cutscene_sounds.vpp_pc
            // (this project's own independently oracle-verified 493-record Vorbis-decode
            // corpus, HANDOFF.md) was the first CHOSEN default, but every one of its own
            // kind-2 Sound HIRC items is genuinely UNRESOLVABLE through this lookup -
            // empirically confirmed, not a bug: that archive is entirely cutscene DIALOGUE
            // (`ct_*`/`story_*`/`zscene_*` bank stems), and spec-audio-runtime.md Sec9.8's
            // own "Convergence with the voice-line external-source mechanism" paragraph
            // explicitly says voice lines "never touch a Sound HIRC item's own embedded
            // descriptor" - so resolveSoundIdToMedia() correctly finds nothing there, every
            // time, for every one of its ~90 bank pairs (measured this task). sounds.vpp_pc's
            // own "animation.bnk_pc" bank DOES resolve+decode a real clip (soundObjectId
            // 0x1f6e26c3, mono/44100Hz/8154 frames, measured this task against the real
            // shipped archive) - an animation-notify-driven SFX bank is a plausible real
            // home for foley/footstep-shaped one-shots, though NOT confirmed to be
            // specifically a footstep sample (see findFirstPlayableSfxClip()'s own doc
            // comment for why no such confirmed identity is claimed).
            sfxArchivePath = dir + "sounds.vpp_pc";
        }
        std::string sfxDiag;
        footstepClip = findFirstPlayableSfxClip(sfxArchivePath, sfxDiag);
        std::fputs(sfxDiag.c_str(), stdout);
        std::printf("audio sfx: %s (%s)\n",
                    footstepClip.valid ? "real SFX clip resolved+decoded, footstep cadence armed"
                                       : "NONE resolved this run - device stays open, nothing to play",
                    sfxArchivePath.c_str());
        // SR3_AUDIO_DUMP_WAV=<path> (THIS TASK, opt-in debug escape hatch, same convention as
        // CRREISH_DISABLE_SUN_SHADOW/SR3_GROUND_TEX_DEBUG elsewhere in this file): writes the
        // resolved clip's own decoded PCM to a real .wav file ONCE, via this project's
        // existing wav_writer.h (not a new writer) - the "capture the submitted PCM to a WAV
        // file as a sanity check it's not silence/garbage" verification this task's own brief
        // asks for, since nobody reviewing a build log can literally hear the speaker output.
        if (footstepClip.valid) {
            const char* dumpWavPath = std::getenv("SR3_AUDIO_DUMP_WAV");
            if (dumpWavPath != nullptr && dumpWavPath[0] != '\0') {
                std::string wavError;
                const bool wrote = sr3audioruntime::writeWavPcm16(
                    dumpWavPath, footstepClip.decoded.pcm, static_cast<uint32_t>(footstepClip.decoded.sampleRate),
                    static_cast<uint16_t>(footstepClip.decoded.channels), wavError);
                std::printf("audio sfx: SR3_AUDIO_DUMP_WAV %s -> %s%s\n", wrote ? "wrote" : "FAILED to write",
                            dumpWavPath, wrote ? "" : (" (" + wavError + ")").c_str());
            }
        }
    }
    // Footstep-cadence FALLBACK bookkeeping (THIS TASK, footstep-real-event, downgraded from the
    // PRIMARY mechanism to a fallback): used only when walkTriggerFramesFound is false (no real
    // anim_files.xtbl left-foot/right-foot Trigger Frame data located for the walk clip actually
    // being played this run - see the setup+doc comment above this function's graph-load block).
    // kFootstepCadenceFrames remains a CHOSEN (not spec-derived) walk-cycle-shaped interval at
    // this run's fixed 30Hz tick rate - a plain, stated guess at a PRESENTATION interval, kept
    // as an honest degrade rather than removed outright.
    return -1;
}

int setupHudAndMission(PlaytestSession& sess) {
    auto& cityArchivePaths = sess.cityArchivePaths;
    auto& luaPreloadArchivePaths = sess.luaPreloadArchivePaths;
    auto& winWidth = sess.winWidth;
    auto& winHeight = sess.winHeight;
    auto& hudEnabled = sess.hudEnabled;
    auto& hudRegistrationListPath = sess.hudRegistrationListPath;
    auto& hudFontArchivePath = sess.hudFontArchivePath;
    auto& missionEnabled = sess.missionEnabled;
    auto& missionStem = sess.missionStem;
    auto& missionRegistrationListPath = sess.missionRegistrationListPath;
    auto& missionPackageTsvPathOverride = sess.missionPackageTsvPathOverride;
    auto& missionTimeoutSeconds = sess.missionTimeoutSeconds;
    auto& npcAiNoFire = sess.npcAiNoFire;
    auto& docOf = sess.docOf;
    auto& inventoryItemNames = sess.inventoryItemNames;
    auto& items3dNames = sess.items3dNames;
    auto& weaponDefNames = sess.weaponDefNames;
    auto& weaponDefClasses = sess.weaponDefClasses;
    auto& npcAiTables = sess.npcAiTables;
    auto& error = sess.error;
    auto& device = *sess.device_;
    auto& hudHost = sess.hudHost;
    auto& hudEs = sess.hudEs;
    auto& hudGp = sess.hudGp;
    auto& hudHealthH = sess.hudHealthH;
    auto& hudAmmoH = sess.hudAmmoH;
    auto& hudMoneyH = sess.hudMoneyH;
    auto& hudNotorietyH = sess.hudNotorietyH;
    auto& hudVehiclePromptH = sess.hudVehiclePromptH;
    auto& hudObjectiveH = sess.hudObjectiveH;
    auto& hudMaxHp = sess.hudMaxHp;
    auto& hudFontArchivesOwned = sess.hudFontArchivesOwned;
    auto& missionDriveActive = sess.missionDriveActive;
    auto& missionStartRecordId = sess.missionStartRecordId;
    auto& missionStartTracked = sess.missionStartTracked;
    auto& missionScriptEntryName = sess.missionScriptEntryName;
    auto& pauseTitleH = sess.pauseTitleH;
    auto& pauseResumeH = sess.pauseResumeH;
    auto& pauseQuitH = sess.pauseQuitH;
    auto& acNpcToAttack = sess.acNpcToAttack;
    auto& acNpcToCover = sess.acNpcToCover;
    // ---- --hud (opt-in, OFF by default - see this function's own "UPDATED
    // 2026-10-08" doc comment for the full citation and honest caveat): the
    // SAME real health/ammo/money/notoriety overlay runHudLiveDemo() builds
    // (real sr3luahost::Host/EngineState, real get_current_hit_points/
    // get_max_hit_points/set_current_hit_points natives, real VINT text via
    // HudDemoFont/drawHudLabel - all reused verbatim, not duplicated, since
    // this function is now defined textually AFTER them in this file),
    // composited onto THIS command's own live swapchain render target every
    // frame via VintQuadPipeline - that class never touches
    // OMSetRenderTargets itself (confirmed by reading it), so its quad()/
    // rect() calls draw straight on top of whatever this function's own 3D
    // pass already bound, with no separate offscreen target needed.
    //
    // EXTENDED (mission-hud-objectives task, spec-hud-runtime.md §6.5,
    // 2026-10-09): a 4-slot objective-text overlay, same drawHudLabel()
    // mechanism, added alongside the 4 stats above. Reads
    // EngineState::objectiveTextSlots() each frame (a slot's presence in
    // that map is "active" - see that accessor's own updated doc comment)
    // - the real objective_text/objective_text_clear natives already
    // existed (lua_spec_confirmed_stubs_b08_batch1_core.cpp/
    // b07_sec8_9_teamA.cpp); this task only adds the render-side consumer,
    // which spec-hud-runtime.md §6.5 itself flags as never having been
    // traced in the original game ("the render-side consumer... remains
    // unidentified") - this overlay is this project's OWN clean-room HUD
    // rendering, not a reimplementation of a found original routine.
    // objective_text_clear's own dispatch previously only incremented a
    // counter (written before objective_text itself was implemented, so
    // there was no slot state yet to clear) - completed as part of THIS
    // task to actually erase the slot (engine_state.h's own
    // recordObjectiveTextClear(), matching Sec1.4's own CONFIRMED "clears
    // the slot" text), so the overlay has a correct notion of "cleared".
    // GPS route-line/minimap waypoint-marker rendering stays explicitly
    // OPEN per spec-hud-runtime.md §6.5's own text - not touched here. -------
    //
    // --hud objective-text overlay demo frames (THIS TASK) - declared here,
    // ahead of the vehicle-script constants further below (which this
    // function declares textually later, right before `if (vehicleEnabled)`),
    // since this block's own printf (just below) and the main loop's
    // scripted-trigger block (near the vehicle entry/exit code) both need
    // them. Same "scripted stand-in exercising the real native path" shape
    // as the vehicle entry/exit frames, since v0 has no real mission content
    // wired into this command (same honest caveat the health/money/
    // notoriety overlay already documents). Frame 2: the real
    // `objective_text` native (Sec1.3) fires for slots 0 AND 1, proving the
    // overlay's own multi-slot (up to 4) rendering, not just a single line -
    // using the REAL keys m01.lua's own script passes to this native
    // (objective-text-localization task; extracted per spec-vpp-container.md
    // §7.6's recipe from `sr3_city_0.vpp_pc` -> `m01_modal.str2_pc` ->
    // `m01.lua`, 182,249 bytes, byte count matches §7.6's own verified
    // figure): slot 0 "m01_kill_lobby_guards" (m01.lua line 1068, inside
    // `m01_run()`; also line 1516 inside `m01_go_to_vault()`) and slot 1
    // "m01_go_to_vault" (m01.lua line 1074, also inside `m01_run()`; also
    // line 1541 inside `m01_go_to_vault()` itself) - not this file's own
    // former placeholder
    // "MAIN_OBJECTIVE_KEY"/"SIDE_OBJECTIVE_KEY" strings, which never were a
    // real key any mission script passes. Both resolve through
    // `mission_us.le_strings` (loaded above) to real English text; see this
    // block's own runChunk() call just below for the literal script text.
    // Frame 150 (well inside the 300-frame --no-input default, clear of the
    // vehicle script's own 10/130 frames): the real `objective_text_clear`
    // native (Sec1.4) fires for slot 1 ONLY, proving both that a cleared
    // slot actually stops drawing (engine_state.h's own recordObjective
    // TextClear() now erases the slot - see that method's own doc comment)
    // and that clearing one slot leaves the others (slot 0) untouched.
    sess.hudFont_.emplace();
    auto& hudFont = *sess.hudFont_;
    // Objective-text key resolution (objective-text-localization task,
    // spec-localization-format.md) - same sr3locstr::LeStringsLookup class
    // runVintDoc()'s own drawText() already uses for `.vint_doc` text_tag
    // keys (declared/loaded there at this file's "localisation strings"
    // comment); populated below, inside `if (hudEnabled)`, from the SAME
    // --hud archive already opened for the glyph font/texture (misc.vpp_pc
    // in every real invocation - it ships both the fonts and 10 of the 11
    // US tables, spec-localization-format.md §1's own table). Declared out
    // here (not inside the `if`) so it is in scope for the per-frame
    // objective-text update further below.
    sess.locLookup_.emplace();
    auto& locLookup = *sess.locLookup_;
    sess.hudPipe_.emplace();
    auto& hudPipe = *sess.hudPipe_;
    // Objective-text overlay (THIS TASK, spec-hud-runtime.md §6.5) - one
    // label handle per real engine slot (0..3), slot order == array order.
    // MISSION-HOST PHASE 1 (this task): ONE shared Host/EngineState for
    // --hud AND --mission - the outer condition below now covers either
    // flag (previously --hud only), and --mission's own registration list
    // is used when --hud was not also given (validated above: one of the
    // two is always available here whenever missionEnabled is true).
                                            // function's own doc comment for why that IS a real wall clock
    // --script-hud-prompt-response (THIS TASK): how many of EngineState::
    // hudPromptRequests()'s own entries have already been answered (that
    // vector only ever grows - see HudPromptRequest's own doc comment) -
    // see the main loop's "SCRIPT-HUD-PROMPT-RESPONSE" block.
    // --auto-answer-hud-prompts (THIS TASK): parallel to hudPromptReqs
    // (EngineState::hudPromptRequests() only ever grows, same convention as
    // above) - the `frames` value at the instant each request was FIRST
    // observed here, so the delay below is measured from "first seen showing"
    // rather than from tool startup. Grown lazily, one entry per newly-seen
    // request, in the same per-tick block that drains hudPromptReqs.

    // ===================== PAUSE MENU (THIS TASK) ==============================
    // spec-frontend-menu-runtime.md Sec7/Sec9 item 2 (HIGH CONFIDENCE, landed
    // 2026-10-10): the real engine's pause-menu-open binding is a per-frame
    // query of action CBA_GAC_PAUSE_MENU (0x55), gated by three "already in a
    // blocking UI/cutscene state" checks (0x007e1f40/0x007aaf00/0x007afb90)
    // plus two further one-shot flag bytes both clear, then the shared
    // game-pause push `0x007074a0(4, 1)` (reason 4 - the previously
    // hypothesis-only game_mode::kNewGameTransitionRequestedWeakHypothesis
    // sentinel, menu_runtime.h, getting its first concrete call site here -
    // modelled via the ALREADY-REAL EngineState::pushGamePauseReason(), the
    // same primitive the fade-completion teardown's reason-0xd push already
    // exercises, Sec4/Sec37.3) and a UI dialog literally named
    // "dialog_pause_show" in .rdata - fired via LaunchLuaCoroutine(), the
    // SAME already-real Lua-hook-dispatch primitive this project's own
    // InputLockDispatcher uses for "main_menu_new_game_block_input"/
    // "save_load_lock_input" (Sec6) and spec-lua-api-behaviour.md Sec26
    // documents for the sibling "dialog_pause_hide" hook - no fake menu
    // runtime invented, the exact mechanism already on file elsewhere in
    // this project is reused as-is.
    //
    // The native's own 3 gate checks have no modelled equivalent in this
    // simplified playtest harness (no other blocking-UI/cutscene state is
    // tracked here besides this very menu) - OPEN, not reproduced
    // individually; the one gate condition that both applies and IS modelled
    // is "the pause menu is not already open" (prevents a double-push of the
    // shared depth counter), which is also the one substantive guard a
    // single-dialog harness like this actually needs. The real primitive's
    // second argument (the literal `1` in `(4, 1)`) is likewise uncharacterized
    // in the spec text (the only other known call site passes a literal `0`
    // instead, for a different reason code) - EngineState::pushGamePauseReason()
    // only ever modelled the reason code, matching its one prior real call
    // site, so this task does not invent a meaning for it either.
    //
    // Esc is repurposed for this (Window::create's own `escQuits=false` for
    // this command, above) - F10 quits instead (edge-triggered, breaks the
    // main loop below). dt is forced to 0 for the duration of a frame the
    // menu is open (right after the existing dt clamps, below) - the single
    // choke point nearly every per-frame system in this loop already reads
    // from (TOD clock, vehicle physics, ground-follow, roadblock AI, the
    // follow camera, animation playback, the mission host's own elapsed-time/
    // frame-time/game-clock advances) - so "sim/frame time stops advancing"
    // falls out of one edit rather than threading a pause check through every
    // individual subsystem.

    // --auto-combat run summary counters (npcModeAttack/Cover are fed by the NPC AI log sink below).
    if (hudEnabled || missionEnabled) {
        std::vector<sr3luahost::RegisteredName> hudNames =
            sr3luahost::loadTaggedRegistrationList(hudEnabled ? hudRegistrationListPath : missionRegistrationListPath);
        hudHost = std::make_unique<sr3luahost::Host>(hudNames);
        hudEs = &hudHost->engineState();
        hudGp = hudHost->gameplayState();
        if (missionEnabled && npcAiTables) {
            // Per-NPC AI loop for mission NPCs (EngineState::tickNpcAi, run from the per-human update).
            sr3airuntime::NpcAiConfig aiCfg;
            aiCfg.fireEnabled = !npcAiNoFire;
            // AI-3: line of sight from the real prop collision (BVH rays over the loaded `.clmesh_pc` props). CHOSEN ray
            // heights: 1.6 m standing eye height for NPC/player, 0.9 m crouch height for the cover shield test.
            aiCfg.losEyeHeight = 1.6;
            aiCfg.coverShieldHeight = 0.9;
            sess.npcLosWorld_.emplace(sess.loaded);
            hudEs->npcAiLineOfSight = [&sess](double x0, double y0, double z0, double x1, double y1, double z1) {
                return sess.npcLosWorld_->clear(x0, y0, z0, x1, y1, z1);
            };
            hudEs->enableNpcAi(npcAiTables, aiCfg);
            hudEs->npcAiLogSink = [&acNpcToAttack, &acNpcToCover](const std::string& line) {
                std::printf("%s\n", line.c_str());
                // --auto-combat summary: count NPC mode transitions into attack-ish / cover modes
                // (sr3airuntime::mode: Aim 1, CombatMovement 5, FireSpray 12, GrenadeThrow 15, Melee 23, BruteMelee 3 | CoverPopout 7, CoverStay 8).
                const size_t mp = line.find(" mode ");
                int from = 0, to = 0;
                if (mp != std::string::npos && std::sscanf(line.c_str() + mp, " mode %d->%d", &from, &to) == 2 && from != to) {
                    if (to == 1 || to == 3 || to == 5 || to == 12 || to == 15 || to == 23) ++acNpcToAttack;
                    else if (to == 7 || to == 8) ++acNpcToCover;
                }
            };
            std::printf("npc-ai   : mission NPC AI loop ON (fire %s; hostility = team relation / attack_player_on_sight / "
                        "enemy target; CHOSEN stand-ins listed in sr3airuntime/npc_ai_runtime.h)\n",
                        aiCfg.fireEnabled ? "on" : "off");
        }
        // CHOSEN test setup, same precedent as runHudLiveDemo()'s own
        // identical block (see its doc comment) - a single resolved
        // "player"/"pistol". Applied the same way whether this Host exists
        // for --hud, --mission, or both - a mission script run against this
        // same minimal state is this task's own CHOSEN starting point
        // (Phase 2's still-OPEN live position bridge/name-resolver would
        // widen it; not attempted here).
        hudEs->objectResolves().set("player", true);
        if (!weaponDefNames.empty()) hudEs->installWeaponDefinitionNames(weaponDefNames);
        else hudEs->weaponDefinitionResolves().set("pistol", true);
        if (!inventoryItemNames.empty()) hudEs->installInventoryItemNames(inventoryItemNames);
        // The real respect_levels.xtbl (spec-tables-progression.md B.3 wiring item 1): routes
        // awardRespect / applyRespectLevelUp through sr3progression. Optional, like the other
        // misc tables: without it the legacy path stays in place.
        try {
            const std::vector<sr3tables_progression::RespectLevel> rows =
                sr3tables_progression::ParseRespectLevelsTable(sess.docOf("respect_levels.xtbl"));
            if (!rows.empty()) {
                hudEs->setRealRespectTable(rows);
                std::printf("respect  : respect_levels.xtbl %zu levels, cap %d\n", rows.size(),
                            static_cast<int>(sr3progression::RespectTable::FromParsed(rows).cap()));
            }
        } catch (const std::exception& e) {
            std::printf("respect  : respect_levels.xtbl not loaded (%s)\n", e.what());
        }
        if (!items3dNames.empty()) hudEs->installItems3dNames(items3dNames);
        for (const auto& wc : weaponDefClasses) hudEs->setWeaponDefinitionClass(wc.first, wc.second);
        {
            // continuous_explosions.xtbl (spec-tables-weapons-combat.md Sec10.2): the rows continuous_explosion_start resolves
            // names against (Sec28.16 2026-10-12: unknown name = silent no-op). Optional, like the other tables here.
            try {
                std::vector<std::string> ceNames;
                for (const auto& row : sr3tables_weapons::ParseAllContinuousExplosions(docOf("continuous_explosions.xtbl"))) ceNames.push_back(row.name);
                hudEs->continuousExplosion().installNames(ceNames);
                std::printf("weapons  : continuous_explosions.xtbl - %zu row name(s) installed\n", ceNames.size());
            } catch (const std::exception& ex) {
                std::printf("weapons  : continuous_explosions.xtbl not available (%s) - continuous_explosion_start names stay OPEN\n", ex.what());
            }
        }
        hudEs->hasLocalPlayer().set(true);
        // RECONCILED (this task, merging feature/inworld-mission-host-
        // triggers-v2 + feature/m05-jump-hud-prompt-test): this block used
        // to force coopSession().present/localIsHost true here, "needed only
        // so notoriety_set's host-gate takes its local-apply branch" - a
        // CHOSEN workaround from BEFORE spec-lua-api-behaviour.md Sec8.27's
        // own `game_get_is_host` single-player resolution was closed
        // ("`game_get_is_host` implements false in single player, closed" -
        // Sec8.27's own 2026-10-01 review-status entry, re-confirmed by the
        // independent `isHost` single-player trace at the `skip_all_
        // cutscenes` registration call, tranche-18 notes). Host's own
        // constructor already seeds present=false via applySpecInitialState
        // (the CONFIRMED real default, lua_spec_initial_state.cpp) - this
        // override was both unnecessary and ACTIVELY HARMFUL for --mission:
        // forcing present=true made EngineState::coopIsActive() (Sec3.1)
        // fall through past its own "no session" short-circuit and into
        // reading coopSession().memberCount, an OpenValue no test here ever
        // seeds - any real mission script calling a coop_is_active()-
        // consuming native (m05's own m05_start does, early) threw "session
        // member count (+0x60) is OPEN" before ever reaching its own real
        // first blocker. The sibling branch's own fix instead seeded
        // memberCount to 1 to dodge that same throw while KEEPING present/
        // localIsHost force-true; removed here instead, in favour of simply
        // never forcing present/localIsHost away from their CONFIRMED false
        // single-player default - memberCount now correctly stays a
        // genuinely OPEN value, unreached, since coopIsActive()'s own first
        // condition (!present -> false) short-circuits before memberCount is
        // ever read (confirmed by inspection of EngineState::coopIsActive(),
        // src/lua_engine_state.cpp).
        //
        // On re-pointing notoriety_set's own host-gate to a separate
        // "local authority unconditionally true in single player" concept
        // (the reconciliation this merge was asked to apply): VERIFIED
        // AGAINST SPEC TEXT, and found NOT to apply to this family.
        // spec-lua-api-behaviour.md Sec6.22 is explicit and re-confirmed in
        // multiple independent passes: the five notoriety track-setters
        // (notoriety_set/_max/_min/_min_and_max, Sec6.3/6.5/6.7/6.11) gate
        // their host/local-apply branch on the EXACT SAME accessor
        // `0x0087ba20`/`+0x5c`==`+0x58` that `game_get_is_host` (Sec8.27)
        // and `coop_is_active` (Sec3.1) use - NOT the separate per-object
        // network-record-ownership "local authority" gate
        // (`0x008ae480`/`0x008837a0`, Sec3.7/Sec4.13/Sec6.22's own cross-
        // reference) that genuinely IS unconditionally true in single
        // player and that this project already models elsewhere by that
        // exact name (CharacterState::humanShieldReleaseIsLocalAuthority,
        // Sec17/18 stubs). Those are two different, independently-confirmed
        // real gates in two different subsystems, not the same boolean
        // under two names - Sec6.22's own text flags this explicitly as "a
        // genuine, checked difference between subsystems in how the same
        // underlying 'am I authoritative' decision is reached". And
        // `game_get_is_host`'s single-player answer is not merely assumed:
        // it is CLOSED at Sec8.27/Sec26.28 (exhaustive reference check: the
        // session installer is never invoked in the static binary, single-
        // player or otherwise - "implement false... with the same
        // confidence as any other statically-unreachable code path"),
        // independently re-confirmed by the `skip_all_cutscenes`
        // registration-call trace ("isHost ... evaluated as 0 whenever
        // there is no session, i.e. always in single player"). The ONLY
        // citation offered for "local authority unconditionally true in
        // single player" that would contradict this, address `0x00bc5610`,
        // was ITSELF corrected on 2026-10-10 (same day as this task) at
        // Sec3.7/Sec6.19/Sec35.3-35.5/Sec16730-16740: `0x00bc5610` is not a
        // connection/session/authority test at all - it is the machinima
        // recorder's own stopping/finalizing predicate
        // (`0x029443bc==2||3`), mislabeled at 6+ citation sites before this
        // correction, now fixed everywhere in spec-lua-api-behaviour.md.
        // tests/synthetic_luahost_b29_sec6_notoriety_mission_test.cpp
        // already encodes the correct, spec-matching design independently
        // of this task: it toggles coopSession().present/localIsHost
        // per-test to exercise BOTH notoriety_set's host branch (direct
        // write) and its non-host/replicate branch, treating that accessor
        // as the real, intentional gate for this family - re-pointing it to
        // an always-true "local authority" field would silently defeat that
        // test's own non-host coverage and contradict the spec text above.
        // CONCLUSION: no new field introduced, and notoriety_set's host-
        // gate (EngineState::coopLocalIsHost(), reading coopSession().
        // present/localIsHost) is left exactly as-is - it already reads the
        // CORRECT, spec-confirmed real gate for this family. The actual,
        // narrower bug this reconciliation fixes is simply this test
        // harness forcing present/localIsHost away from their real default
        // for a DIFFERENT reason (dodging coopIsActive()'s OPEN-memberCount
        // throw) - removing that force-set is sufficient; no gate re-point
        // is warranted. Flagged back to the orchestrator/Team A as a relay
        // discrepancy, not silently overridden.
        hudEs->registerNotorietyAliasTrackCodeForTesting("Police", 3);

      if (hudEnabled) {
        auto fa = std::make_unique<CityArchive>();
        fa->path = hudFontArchivePath;
        const size_t hudSlash = hudFontArchivePath.find_last_of("/\\");
        fa->label = hudSlash == std::string::npos ? hudFontArchivePath : hudFontArchivePath.substr(hudSlash + 1);
        fa->bytes = mapFileLargeC(hudFontArchivePath);
        if (fa->bytes.empty()) {
            std::fprintf(stderr, "--hud: could not read %s\n", hudFontArchivePath.c_str());
            return 1;
        }
        fa->top = std::make_unique<vpp::Container>(vpp::ByteView(fa->bytes.data(), fa->bytes.size()));
        hudFontArchivesOwned.push_back(std::move(fa));
        CityArchive& hudArchive = *hudFontArchivesOwned[0];

        const std::string hudFontFile = std::string(sr3font::gameFontFileStem(0)) + ".vf3_pc";
        std::vector<uint8_t> hudFontBytes, hudFontSib;
        if (!findEntry(*hudArchive.top, hudFontFile, hudFontBytes, hudFontSib, std::string()) || hudFontBytes.empty()) {
            std::fprintf(stderr, "--hud: font %s not found in %s\n", hudFontFile.c_str(), hudFontArchivePath.c_str());
            return 1;
        }
        try {
            hudFont.font = sr3font::parseFont(hudFontBytes);
            hudFont.loaded = true;
        } catch (const sr3font::FontFormatError& ex) {
            std::fprintf(stderr, "--hud: font parse failed: %s\n", ex.what());
            return 1;
        }
        sr3render::UploadedTexture hudFontTex;
        std::string hudFoundIn;
        if (!findAndUploadTexture(*hudArchive.top, hudFont.font.textureName, device.device(), hudFontTex, hudFoundIn,
                                  &hudArchive.texCache)) {
            std::fprintf(stderr, "--hud: glyph texture %s not found in %s\n", hudFont.font.textureName.c_str(),
                         hudFontArchivePath.c_str());
            return 1;
        }
        hudFont.tex.srv = hudFontTex.srv;
        hudFont.tex.texW = hudFontTex.width;
        hudFont.tex.texH = hudFontTex.height;

        // ---- localisation strings (objective-text-localization task,
        // spec-localization-format.md) - EXACT same table list, load order
        // and per-table "first archive that has it, else skip, not fatal"
        // search runVintDoc()'s own "localisation strings" block already
        // uses (this file's earlier comment there: Sec5.2 step 4's load
        // order static/hud/activity/diversion/mission/menu/multiplayer/
        // customize/subtitle/platform_PC/patch0). Only one archive is ever
        // searched here (--hud only ever takes one path, unlike --textures'
        // list) - every real invocation points --hud at misc.vpp_pc, which
        // alone carries 10 of these 11 US tables (patch0 needs
        // patch_compressed.vpp_pc too, §1's own table; a missing table is
        // not fatal, Sec5.1 step 2's "a missing file... returns false").
        // `mission_us.le_strings` specifically is what resolves a real
        // mission script's own objective_text key.
        {
            static const char* const kHudTableFiles[] = {
                "static_us.le_strings",      "hud_us.le_strings",       "activity_us.le_strings",
                "diversion_us.le_strings",   "mission_us.le_strings",   "menu_us.le_strings",
                "multiplayer_us.le_strings", "customize_us.le_strings", "subtitle_us.le_strings",
                "platform_pc_us.le_strings", "patch0_us.le_strings",
            };
            size_t hudTablesFound = 0, hudRecordsTotal = 0;
            for (const char* file : kHudTableFiles) {
                std::vector<uint8_t> bytes, sib;
                if (!findEntry(*hudArchive.top, file, bytes, sib, std::string()) || bytes.empty()) continue;
                try {
                    sr3locstr::LeStringsTable t = sr3locstr::parseLeStrings(bytes);
                    hudRecordsTotal += t.recordCount;
                    locLookup.addTable(std::move(t));
                    ++hudTablesFound;
                } catch (const sr3locstr::LeStringsFormatError& ex) {
                    std::printf("  [loc] %-26s PARSE ERROR (%s) -> table not loaded\n", file, ex.what());
                }
            }
            std::printf("  [loc] %zu/%zu US .le_strings tables loaded from %s (%zu records total) -> objective-text "
                        "keys resolved through them, raw key string on a miss (spec-localization-format.md "
                        "Sec6.1/Sec7 item 2)\n",
                        hudTablesFound, sizeof(kHudTableFiles) / sizeof(kHudTableFiles[0]), hudFontArchivePath.c_str(),
                        hudRecordsTotal);
        }

        if (!hudPipe.initialise(device.device(), winWidth, winHeight, error)) {
            std::fprintf(stderr, "--hud: VintQuadPipeline init failed: %s\n", error.c_str());
            return 1;
        }

        auto hudSetOffset = [&](uint32_t h, double x, double y) {
            sr3luahost::VintTaggedValue vx, vy;
            vx.kind = sr3luahost::VintTaggedValue::Kind::Number;
            vx.number = x;
            vy.kind = sr3luahost::VintTaggedValue::Kind::Number;
            vy.number = y;
            hudEs->setVintProperty(h, "offset", {vx, vy});
        };
        hudHealthH = hudEs->registerTypedVdoObjectForTesting("hud_health_text", "text", 0, 0);
        hudAmmoH = hudEs->registerTypedVdoObjectForTesting("hud_ammo_text", "text", 0, 0);
        hudMoneyH = hudEs->registerTypedVdoObjectForTesting("hud_money_text", "text", 0, 0);
        hudNotorietyH = hudEs->registerTypedVdoObjectForTesting("hud_notoriety_text", "text", 0, 0);
        sess.hudRespectH = hudEs->registerTypedVdoObjectForTesting("hud_respect_text", "text", 0, 0);
        // Vehicle entry prompt (THIS TASK) - trivial reuse of this same text-
        // label mechanism, see this function's own "VEHICLE ENTRY/DRIVING"
        // doc comment; text is set to empty/non-empty per frame below based
        // on vehiclePromptShown/inVehicle, same as every other HUD label's
        // own per-frame text update just below in the main loop.
        hudVehiclePromptH = hudEs->registerTypedVdoObjectForTesting("hud_vehicle_prompt_text", "text", 0, 0);
        // Objective-text overlay (THIS TASK, spec-hud-runtime.md §6.5) - 4
        // slots, same fixed-array shape as the real engine's own CONFIRMED
        // 4-slot objective-text record array (Sec1.3), one label handle per
        // slot in slot order. Reuses the SAME registerTypedVdoObjectForTesting/
        // hudSetOffset/drawHudLabel mechanism every other HUD line above
        // already uses - no new rendering convention invented. Slot 0 is the
        // main objective (so it sits visually first); slots 1-3 stack below
        // it, each only ever drawn when EngineState::objectiveTextSlots()
        // actually has that slot's entry (see the per-frame update below).
        for (uint32_t& h : hudObjectiveH) h = hudEs->registerTypedVdoObjectForTesting("hud_objective_text", "text", 0, 0);
        hudSetOffset(hudHealthH, 40, 24);
        hudSetOffset(hudAmmoH, 40, 108);
        hudSetOffset(hudMoneyH, 40, 148);
        hudSetOffset(hudNotorietyH, 40, 188);
        // Top right, near the real respect meter's anchor (spec-hud-runtime.md H.2: (1135,87)
        // at 1280x720); CHOSEN until the hud.vint_doc HUD itself is rendered.
        hudSetOffset(sess.hudRespectH, 900, 24);
        hudSetOffset(hudVehiclePromptH, 40, 228);
        // 40px vertical steps, continuing the SAME spacing every label above
        // the vehicle prompt already uses, starting right after it.
        for (size_t i = 0; i < hudObjectiveH.size(); ++i)
            hudSetOffset(hudObjectiveH[i], 40, 268 + 40 * static_cast<double>(i));

        // Pause-menu overlay labels (THIS TASK) - same registerTypedVdoObjectForTesting/
        // drawHudLabel convention as every other label above, just the text/offsets differ;
        // see this function's own "PAUSE MENU" doc comment (above the main loop) for the full
        // real mechanism this overlay is a visible stand-in for.
        pauseTitleH = hudEs->registerTypedVdoObjectForTesting("pause_overlay_title_text", "text", 0, 0);
        pauseResumeH = hudEs->registerTypedVdoObjectForTesting("pause_overlay_resume_text", "text", 0, 0);
        pauseQuitH = hudEs->registerTypedVdoObjectForTesting("pause_overlay_quit_text", "text", 0, 0);
        hudSetOffset(pauseTitleH, 40, 320);
        hudSetOffset(pauseResumeH, 40, 360);
        hudSetOffset(pauseQuitH, 40, 392);

        // Health (Sec2): get_max_hit_points draws this project's own
        // random-once spawn max; set_current_hit_points then applies the
        // CONFIRMED "starts equal to the newly-computed max" spawn rule -
        // same two real natives runHudLiveDemo()'s own frame-0 setup calls.
        hudHost->runChunk(hudGp, "g_maxhp = get_max_hit_points('player')", "playtest_hud.lua");
        hudMaxHp = readLuaGlobalNumber(hudGp, "g_maxhp");
        hudHost->runChunk(hudGp, "set_current_hit_points('player', g_maxhp)", "playtest_hud.lua");
        std::printf("hud      : enabled (--hud) - real health/ammo/money/notoriety + objective-text "
                    "overlay via sr3luahost::Host (spec-hud-runtime.md §2-6.5); max hp=%.0f. "
                    "Objective slots 0/1 are set via the real objective_text native at frame %d, "
                    "slot 1 cleared via objective_text_clear at frame %d (scripted stand-in, no real "
                    "mission content wired into this command). HONEST CAVEAT: v0 has no\n"
                    "           combat/economy/notoriety gameplay wired, so those 4 values will not\n"
                    "           actually change during play (see this function's own doc comment)\n",
                    hudMaxHp, kScriptObjectiveSetFrame, kScriptObjectiveClearFrame);
      } // if (hudEnabled) - font/pipeline/HUD-vdo setup, unchanged from before this task

      // MISSION-HOST PHASE 1 (this task): resolve <stem> against the real
      // archives this command already has open, load its chunk, call its
      // own `<stem>_start(checkpoint, is_restart)` - EXACTLY once, here, at
      // bind time (not per-frame) - and start tracking its suspended record
      // (if any) for the per-frame drive below. A resolution/load/call
      // failure is a hard error (return 1) - the SAME convention every
      // --hud resource-load failure above already uses, not a new one.
      if (missionEnabled) {
        const std::string missionPackageTsvPath =
            !missionPackageTsvPathOverride.empty()
                ? missionPackageTsvPathOverride
                // CHOSEN default: next to the registration list actually used to build this
                // Host - the SAME directory convention tools/lua_host_run.cpp's own toolsDir
                // uses (fs::path(registrationListPath).parent_path()), not a new one.
                : [&]() {
                      const std::string& regList = hudEnabled ? hudRegistrationListPath : missionRegistrationListPath;
                      const size_t slash = regList.find_last_of("/\\");
                      const std::string dir = slash == std::string::npos ? std::string() : regList.substr(0, slash);
                      return (dir.empty() ? std::string() : dir + "/") + "mission_package_per_container.tsv";
                  }();
        MissionResolution resolved = resolveMissionStemScript(cityArchivePaths, missionPackageTsvPath, missionStem);
        if (!resolved.ok) {
            std::fprintf(stderr, "--mission=%s: %s\n", missionStem.c_str(), resolved.error.c_str());
            return 1;
        }
        missionScriptEntryName = resolved.entryName;
        std::printf("mission  : resolved --mission=%s -> entry '%s' (container '%s', mission_package_tsv=%s)\n",
                    missionStem.c_str(), resolved.entryName.c_str(), resolved.containerName.c_str(),
                    missionPackageTsvPath.c_str());

        // IN-WORLD TRIGGERS (this task, 2026-10-10): load real zone-object
        // data (position only - see missionzoneobj::installZoneObjectsFromArchives's
        // own doc comment for the honest shape/extent gap, Sec1.14) over the
        // SAME `cityArchivePaths` the mission script itself was just resolved
        // against, so the per-frame overlap test below can look up a real
        // trigger's real position by name.
        {
            std::printf("mission  : %s\n",
                        missionzoneobj::installVehicleTypeClassTableFromArchives(cityArchivePaths, *hudEs).c_str());
            std::string zoneSummary = missionzoneobj::installZoneObjectsFromArchives(cityArchivePaths, *hudEs);
            std::printf("mission  : real zone-object load for in-world triggers: %s\n", zoneSummary.c_str());
        }

        // ZSCENE TABLE FIX (coordinator-routed, 2026-10-10): install the REAL
        // cutscene.xtbl/<name>.cte_xtbl-derived scene table (spec-lua-api-
        // behaviour.md Sec26.25) over the same `cityArchivePaths`, so
        // EngineState::zsceneIsLoaded()'s own already-correct Sec14.23
        // kind-gate (src/lua_cutscene.cpp: `if (!r.loadable) return true;`)
        // has real data to answer from instead of refusing OPEN on this
        // command's very first `zscene_is_loaded("<stem>_in")` call - see
        // missionzoneobj::installZsceneTableFromArchives()'s own doc comment
        // for the full root-cause citation.
        {
            // Uses luaPreloadArchivePaths (cache-directory-scan-expanded), not
            // cityArchivePaths directly - see that variable's own doc comment
            // above (fix/m05-zscene-m13-systemlib, Task A).
            std::string zsceneSummary = missionzoneobj::installZsceneTableFromArchives(luaPreloadArchivePaths, *hudEs);
            std::printf("mission  : real zscene/cutscene table load: %s\n", zsceneSummary.c_str());
        }

        // Real gameplay-state preload chain (CONFIRMED, spec-lua-bindings.md
        // Sec13.2 Item 2/Sec16.2: "the game play state registers its
        // roster, loads system_lib.lua, THEN game_lib.lua, before any
        // mission script runs against it") - the SAME two names tools/
        // lua_host_run.cpp's own main() preloads into gameplayState() before
        // its own mission-drive loop, in the SAME order, via the SAME
        // first-found-by-entry-name rule (that file's own preloadNamed()/
        // game_lib.lua block) - without this, a real mission script's first
        // call into a game_lib.lua helper (e.g. mission_start_fade_out,
        // observed directly: m01_start's own first real call) fails with
        // "attempt to call global '<name>' (a nil value)", a wrong-
        // initialization artifact, not a real engine blocker. Found over
        // the SAME `cityArchivePaths` the mission script itself resolves
        // against (one combined walk, findScriptsByName above) - missing
        // either is reported (same "BLOCKING FINDING" framing tools/
        // lua_host_run.cpp's own preloadNamed() uses) but NOT fatal here:
        // a mission that does not need them may still run.
        {
            // Uses luaPreloadArchivePaths (cache-directory-scan-expanded), not
            // cityArchivePaths directly - Task B (system_lib.lua archive-
            // location gap), fix/m05-zscene-m13-systemlib.
            auto preloads = findScriptsByName(luaPreloadArchivePaths, {"system_lib.lua", "game_lib.lua"});
            for (const char* name : {"system_lib.lua", "game_lib.lua"}) {
                auto pit = preloads.find(name);
                if (pit == preloads.end()) {
                    std::printf("mission  : BLOCKING FINDING - no entry named %s found in the given city "
                                "archives (spec-lua-bindings.md Sec16.2's own gameplay-state preload, not "
                                "loaded for this session - a mission needing its helpers will fail on its "
                                "first call into one)\n",
                                name);
                    continue;
                }
                sr3luahost::Host::RunResult pr = hudHost->runChunk(hudGp, pit->second.content, pit->second.entryName);
                std::string loadPart = pr.loadOk ? "OK" : ("FAILED: " + pr.loadError);
                std::string pcallPart = !pr.loadOk ? "" : (pr.pcallOk ? ", lua_pcall=OK" : (", lua_pcall=FAILED: " + pr.pcallError));
                std::printf("mission  : preloaded %s -> gameplay state: luaL_loadbuffer=%s%s\n", name,
                            loadPart.c_str(), pcallPart.c_str());
            }
            // Engine bootstrap equivalent (spec-lua-bindings.md Sec14, see Host::applyDynamicGlobalsBootstrap): thread_new's
            // `_GetAnyGlobalSilent` finds the mission's own global functions (previously every mission thread_new
            // returned 65535 and no mission worker thread ever started). See Host::applyDynamicGlobalsBootstrap.
            if (preloads.find("system_lib.lua") != preloads.end()) {
                sr3luahost::Host::RunResult br = hudHost->applyDynamicGlobalsBootstrap(hudGp);
                std::printf("mission  : dynamic-globals bootstrap (_GetAnyGlobalSilent also finds raw globals) -> %s\n",
                            br.loadOk && br.pcallOk ? "OK" : "FAILED");
            }
        }

        sr3luahost::Host::RunResult loadResult =
            missionRunChunkWithWatchdog(*hudHost, hudGp, resolved.content, resolved.entryName);
        if (!loadResult.loadOk) {
            std::fprintf(stderr, "--mission=%s: script failed to load: %s\n", missionStem.c_str(),
                         loadResult.loadError.c_str());
            return 1;
        }
        if (loadResult.ranPcall && !loadResult.pcallOk) {
            std::fprintf(stderr, "--mission=%s: script's own top-level code errored: %s\n", missionStem.c_str(),
                         loadResult.pcallError.c_str());
            return 1;
        }
        // MISSION-HOST PHASE 3 (this task): the REAL bind/marker-trigger
        // sequence (spec-lua-api-behaviour.md Sec21.27a + Sec36.2/36.4,
        // sr3luahost::MissionPhaseMachine, include/sr3luahost/menu_runtime.h)
        // - NOT EngineState::beginMissionPhaseTracking()'s one-shot collapse
        // any more (that method is still real and still used, but now only
        // by tools/lua_host_run.cpp's own standalone headless mission-drive
        // loop - a legitimate separate caller, not touched by this task).
        // bind(isRestart=false, hasName=true) is this call site's own fresh-
        // start convention, unchanged from Phase 1 - it is CONFIRMED (bind()'s
        // own doc comment) to always arm the marker on those exact arguments,
        // so triggerMarker() below is always the reachable next call here,
        // never relaunchDirect(). Constructed fresh per bind (not kept across
        // frames): nothing downstream of this block ever calls back into it -
        // the per-frame drive below resumes the suspended coroutine directly
        // through the SAME generic BareThreadTable scheduler pass Phase 1
        // already established, keyed by missionStartRecordId, exactly as
        // before this switch.
        sr3luahost::MissionPhaseMachine missionPhases(*hudEs, hudGp, hudHost->bareGlobals().threads());
        missionPhases.bind(missionStem, /*isRestart=*/false, /*hasName=*/true);
        const std::string funcName = missionStem + "_start";
        if (!missionPhases.marker().armed) {
            // Unreachable on this call site's own fixed (isRestart=false,
            // hasName=true) arguments per bind()'s own CONFIRMED body - kept
            // only as a defensive, honestly-reported guard, not a modelled
            // real branch this call site ever takes today.
            std::fprintf(stderr,
                         "--mission=%s: internal: MissionPhaseMachine::bind() did not arm the marker "
                         "(unexpected for a fresh, named start)\n",
                         missionStem.c_str());
            return 1;
        }
        sr3luahost::LuaCoroutineLaunchResult startResult = missionPhases.triggerMarker();
        if (!startResult.existedAsFunction) {
            std::fprintf(stderr,
                         "--mission=%s: %s not defined in the loaded script (real check, spec-lua-bindings.md "
                         "Sec14.10's own confirmed call shape not reachable here) - nothing to drive\n",
                         missionStem.c_str(), funcName.c_str());
            return 1;
        }
        if (!startResult.callOk) {
            std::fprintf(stderr, "--mission=%s: %s errored on its own first call: %s\n", missionStem.c_str(),
                         funcName.c_str(), startResult.callError.c_str());
            return 1;
        }
        if (startResult.suspended) {
            missionStartRecordId = startResult.recordId;
            missionStartTracked = true;
            missionDriveActive = true;
            // suspendedAt (file:line) is a diagnostic-only nice-to-have Phase 1's
            // own missionCallStart() used to compute inline - recomputed here off
            // the SAME real record id instead, now that LaunchLuaCoroutine's own
            // result shape (menu_runtime.h) doesn't carry it.
            std::string suspendedAt = "?";
            if (const sr3luahost::BareThreadTable::Record* rec =
                    hudHost->bareGlobals().threads().findAlive(startResult.recordId)) {
                lua_Debug ar{};
                if (lua_getstack(rec->co, 1, &ar) && lua_getinfo(rec->co, "Sln", &ar)) {
                    suspendedAt = (ar.short_src[0] ? ar.short_src : "?") + std::string(":") +
                                  std::to_string(ar.currentline) +
                                  (ar.name ? (std::string(" (in ") + ar.name + ")") : "");
                }
            }
            std::printf("mission  : %s() called, suspended (waiting at %s) - driving one scheduler pass per "
                        "rendered frame from here (real --mission-timeout=%.1fs wall-clock safety cutoff, "
                        "%d-instruction watchdog per resume)\n",
                        funcName.c_str(), suspendedAt.c_str(), missionTimeoutSeconds,
                        kMissionWatchdogInstructionBudget);
        } else {
            // Finished on its very first call, no error - a real, honest outcome for a
            // short script (same "low stub-hit count is not a bug" framing tools/
            // lua_host_run.cpp's own top comment gives for a hook-only script) - nothing
            // further to drive.
            missionDriveActive = false;
            std::printf("mission  : %s() called and finished immediately (no error, nothing suspended) - "
                        "nothing further to drive this session\n",
                        funcName.c_str());
        }
      }
    }
    return -1;
}

} // namespace sr3viewer
