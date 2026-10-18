// In-game cutscene playback core - spec-cutscene-camera-format.md "In-game
// cutscene playback, end to end" B-E (the H wiring list, steps 3-9). One
// clock drives the camera time, shot begins, clip starts, lipsync and
// subtitles, the shot options (Bink and completion screen), the
// soundtrack, skip and end. The player emits events; the host executes them
// (spawn/animate/teleport, Wwise calls, Bink, fades, camera sampling at
// time()).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sr3cutsceneplay/scene.h"

namespace sr3cutsceneplay {

enum class EventKind {
    // Load (state 7) and playback start.
    BankLoad,          // text = bank file, id0 = bank id
    BinkOpen,          // text = "videos\<movie>", opened paused
    SceneOption,       // a scene-level option dispatched at load (text = name)
    SoundtrackStart,   // SetSwitch(id0 group, id1 value); id2 = persona switch value (0 = no player)
                       // then PostEvent Cinematic_CTE on the 2D object "Cutscene"
    CameraEnter,       // camera mode 7 (not for Designer scenes)
    // Per shot.
    ShotBegin,         // shot
    ShotOption,        // shot, text = option name (the option's own events follow)
    ActorSetup,        // shot, index = actor row, text = object name, flag = HideChar
    ClipStart,         // shot, index = actor row, text = clip name, value = MaxWeight
    ClipRelease,       // shot, index = actor row, text = clip name
    LipsyncStart,      // shot, index = audio line, text = lipsync clip, id0 = 1-based actor index
    SubtitleShow,      // shot, index = audio line, text = subtitle key, value = duration (s)
    // Options with an engine effect.
    BinkPlay,          // in-scene play from frame `value`
    BinkPause,
    BinkFullscreenPush,  // push the full-screen movie layer (the cutscene keeps ticking)
    BinkFullscreenPop,   // pause and pop
    BinkStop,
    // Clock jumps and pause.
    SoundtrackSeek,    // value = milliseconds
    SoundtrackPause,
    SoundtrackResume,
    // End.
    FadeOut,           // value = ms (0 = the "0 ms" request); flag = duration not stated
    PlaybackStopped,   // flag = skipped (cutscene_was_skipped)
    SoundtrackStop,    // stop the play id, unload the bank, post Stop_Cinematics
    ZsceneUnmute,      // post UnMute_After_Zscene
    CameraExit,
    RestoreOptionsAndVisibility,
    ChainNextScene,    // text = NextScene
    FadeIn,            // value = ms (500, or 125 after a quick fade)
    Finished,
};

struct Event {
    EventKind kind;
    int shot = -1;
    int index = -1;
    std::string text;
    uint32_t id0 = 0, id1 = 0, id2 = 0;
    double value = 0.0;
    bool flag = false;
};

struct PlaybackOptions {
    bool hasPlayer = true;
    uint32_t playerPersonaId = 0;  // the Cinematic_Persona switch value
    bool femalePlayer = false;     // f_ clips first
    std::function<bool(const std::string&)> clipExists;  // for the f_ fallback; empty = plain names
};

// States used by the skip gate: 10-12 playing, 11 skip requested, 13 stopped.
enum class PlayState : int {
    Loaded = 7,
    Playing = 10,
    SkipRequested = 11,
    Playing12 = 12,
    Stopped = 13,
    Done = 0x13,
};

class CutscenePlayer {
public:
    CutscenePlayer(Scene scene, PlaybackOptions options);

    // Load: bank, movie, scene options. Then the first playing frame:
    // soundtrack start, camera mode 7, shot 0 begin.
    std::vector<Event> start();
    // Advance the clock by the real frame time.
    std::vector<Event> step(double dt);

    // CBA_SKIP_MODAL_SEQUENCE. Accepted only while playing (10-13), when
    // neither blocking flag is set, more than 2000 ms after playback start,
    // and - with the completion screen - with more than 0.6 s left.
    bool requestSkip(bool blockingFlagSet = false);
    // The host reports a requested fade-out has fully faded (skip of a scene
    // with a tail hold or completion screen waits for it).
    void notifyFadedOut() { fadedOut_ = true; }
    // With the completion screen, the end waits until HUD screens 0x35-0x39
    // are gone.
    void setCompletionHudGone(bool gone) { completionHudGone_ = gone; }

    std::vector<Event> pause();
    std::vector<Event> resume();

    double time() const { return t_; }  // sample the camera here
    double duration() const { return duration_; }
    int currentShot() const { return shot_; }
    double elapsedInShot() const { return shot_ < 0 ? 0.0 : t_ - spans_[static_cast<size_t>(shot_)].start; }
    PlayState state() const { return state_; }
    bool skipped() const { return skipped_; }
    const Scene& scene() const { return scene_; }

private:
    void beginShot(int k, std::vector<Event>& out);
    void shotUpdate(std::vector<Event>& out);
    void fireOption(const SceneOption& o, int shot, std::vector<Event>& out);
    void releaseShotClips(int k, std::vector<Event>& out);
    void seekTo(double t, std::vector<Event>& out);
    void finish(std::vector<Event>& out);

    Scene scene_;
    PlaybackOptions opt_;
    std::vector<ShotSpan> spans_;
    double duration_ = 0.0;
    double t_ = 0.0;
    double sincePlaybackStart_ = 0.0;
    int shot_ = -1;
    PlayState state_ = PlayState::Loaded;
    bool started_ = false;
    bool skipped_ = false;
    bool paused_ = false;
    bool fadeRequested_ = false;
    bool quickFade_ = false;
    bool fadedOut_ = false;
    bool completionHudGone_ = true;
    bool tailSkipPending_ = false;
    bool binkFullscreen_ = false;
    // Per current shot: clip started/released, lipsync/subtitle fired.
    std::vector<std::vector<uint8_t>> clipState_;
    std::vector<uint8_t> lipsyncFired_;
    std::vector<uint8_t> subtitleFired_;
};

}  // namespace sr3cutsceneplay
