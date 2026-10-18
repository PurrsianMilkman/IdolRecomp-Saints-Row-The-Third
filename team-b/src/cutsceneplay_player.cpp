// In-game cutscene playback core - spec-cutscene-camera-format.md "In-game
// cutscene playback, end to end" B-E. See include/sr3cutsceneplay/playback.h.

#include "sr3cutsceneplay/playback.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace sr3cutsceneplay {

namespace {

constexpr double kSkipMinSinceStart = 2.0;        // > 2000 ms after playback start
constexpr double kSkipMinRemainingCompletion = 0.6;
constexpr double kZsceneEndFadeLead = 0.125;      // Zscene: <= 0.125 s left -> 125 ms fade
constexpr double kOtherEndFadeLead = 0.8;         // other kinds: <= 0.8 s left -> 0 ms request
constexpr double kLipsyncLead = 0.1;              // lipsync starts 0.1 s before the line
constexpr double kQuickFadeMs = 125.0;
constexpr double kNormalFadeInMs = 500.0;

bool NameIs(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return i == a.size() && b[i] == 0;
}

Event Ev(EventKind k, int shot = -1, int index = -1) {
    Event e{k};
    e.shot = shot;
    e.index = index;
    return e;
}

}  // namespace

CutscenePlayer::CutscenePlayer(Scene scene, PlaybackOptions options)
    : scene_(std::move(scene)), opt_(std::move(options)) {
    spans_ = ShotSpans(scene_);
    duration_ = SceneDuration(scene_);
}

void CutscenePlayer::fireOption(const SceneOption& o, int shot, std::vector<Event>& out) {
    Event e = Ev(shot < 0 ? EventKind::SceneOption : EventKind::ShotOption, shot);
    e.text = o.name;
    out.push_back(e);
    const bool hasMovie = scene_.binkMovieFilename.has_value();
    if (NameIs(o.name, "Show Completion Screen")) {
        scene_.completionScreen = o.enabled.value_or(true);
    } else if (NameIs(o.name, "Fullscreen Bink Play")) {
        if (!hasMovie) return;
        if (o.enabled.value_or(false)) {
            binkFullscreen_ = true;
            out.push_back(Ev(EventKind::BinkFullscreenPush, shot));
        } else {
            binkFullscreen_ = false;
            out.push_back(Ev(EventKind::BinkFullscreenPop, shot));
        }
    } else if (NameIs(o.name, "Bink Play")) {
        if (!hasMovie) return;
        const double f = o.number.value_or(0.0f);
        if (f < 0.0) {
            out.push_back(Ev(EventKind::BinkPause, shot));
        } else {
            Event p = Ev(EventKind::BinkPlay, shot);
            p.value = f;
            out.push_back(p);
        }
    } else if (NameIs(o.name, "Bink Stop")) {
        if (!hasMovie) return;
        out.push_back(Ev(binkFullscreen_ ? EventKind::BinkFullscreenPop : EventKind::BinkStop, shot));
        if (binkFullscreen_) out.push_back(Ev(EventKind::BinkStop, shot));
        binkFullscreen_ = false;
    }
}

std::vector<Event> CutscenePlayer::start() {
    std::vector<Event> out;
    if (started_) return out;
    started_ = true;
    // Load (state 7): soundtrack bank, movie opened paused, scene options.
    std::optional<SoundtrackSwitch> sw;
    if (scene_.soundtrack) sw = ParseSoundtrack(*scene_.soundtrack);
    if (sw) {
        Event b = Ev(EventKind::BankLoad);
        b.text = sw->bankFile;
        b.id0 = sw->valueId;
        out.push_back(b);
    }
    if (scene_.binkMovieFilename) {
        Event b = Ev(EventKind::BinkOpen);
        b.text = BinkMoviePath(*scene_.binkMovieFilename);
        out.push_back(b);
    }
    for (const SceneOption& o : scene_.options) fireOption(o, -1, out);

    // First playing frame.
    state_ = PlayState::Playing;
    if (sw) {
        Event s = Ev(EventKind::SoundtrackStart);
        s.id0 = sw->groupId;
        s.id1 = sw->valueId;
        s.id2 = opt_.hasPlayer ? opt_.playerPersonaId : 0u;
        s.flag = opt_.hasPlayer;
        out.push_back(s);
    }
    if (scene_.kind != SceneKind::Designer) out.push_back(Ev(EventKind::CameraEnter));
    if (!scene_.shots.empty()) {
        // Pre-fire options of the first shot fire at playback start.
        for (const SceneOption& o : scene_.shots[0].options)
            if (o.preFire) fireOption(o, 0, out);
        beginShot(0, out);
        shotUpdate(out);
    }
    return out;
}

void CutscenePlayer::releaseShotClips(int k, std::vector<Event>& out) {
    if (k < 0 || static_cast<size_t>(k) >= scene_.shots.size()) return;
    const SceneShot& s = scene_.shots[static_cast<size_t>(k)];
    for (size_t a = 0; a < s.actors.size() && a < clipState_.size(); ++a)
        for (size_t c = 0; c < s.actors[a].clips.size(); ++c)
            if (clipState_[a][c] == 1) {
                clipState_[a][c] = 2;
                Event e = Ev(EventKind::ClipRelease, k, static_cast<int>(a));
                e.text = ResolveClipName(s.actors[a].clips[c].filename, opt_.femalePlayer, opt_.clipExists);
                out.push_back(e);
            }
}

void CutscenePlayer::beginShot(int k, std::vector<Event>& out) {
    releaseShotClips(shot_, out);
    shot_ = k;
    const SceneShot& s = scene_.shots[static_cast<size_t>(k)];
    out.push_back(Ev(EventKind::ShotBegin, k));
    for (const SceneOption& o : s.options)
        if (!o.preFire) fireOption(o, k, out);
    // Pre-fire options of the next shot fire one shot ahead.
    if (static_cast<size_t>(k) + 1 < scene_.shots.size())
        for (const SceneOption& o : scene_.shots[static_cast<size_t>(k) + 1].options)
            if (o.preFire) fireOption(o, k + 1, out);
    clipState_.assign(s.actors.size(), {});
    for (size_t a = 0; a < s.actors.size(); ++a) {
        clipState_[a].assign(s.actors[a].clips.size(), 0);
        Event e = Ev(EventKind::ActorSetup, k, static_cast<int>(a));
        e.text = ShotActorObjectName(s.actors[a].name);
        e.flag = s.actors[a].hideChar;
        out.push_back(e);
    }
    lipsyncFired_.assign(s.audio.size(), 0);
    subtitleFired_.assign(s.audio.size(), 0);
}

void CutscenePlayer::shotUpdate(std::vector<Event>& out) {
    if (shot_ < 0) return;
    const SceneShot& s = scene_.shots[static_cast<size_t>(shot_)];
    const double elapsed = elapsedInShot();
    const int frame = static_cast<int>(elapsed * kFramesPerSecond);
    for (size_t a = 0; a < s.actors.size(); ++a) {
        for (size_t c = 0; c < s.actors[a].clips.size(); ++c) {
            const ClipRow& r = s.actors[a].clips[c];
            uint8_t& st = clipState_[a][c];
            if (st == 0 && frame >= r.startFrame) {
                st = 1;
                Event e = Ev(EventKind::ClipStart, shot_, static_cast<int>(a));
                e.text = ResolveClipName(r.filename, opt_.femalePlayer, opt_.clipExists);
                e.value = r.maxWeight;
                out.push_back(e);
            }
            // Released at the end of its window (CHOSEN: the first frame past
            // EndFrame; the shot change releases whatever is still playing).
            if (st == 1 && frame > r.endFrame) {
                st = 2;
                Event e = Ev(EventKind::ClipRelease, shot_, static_cast<int>(a));
                e.text = ResolveClipName(r.filename, opt_.femalePlayer, opt_.clipExists);
                out.push_back(e);
            }
        }
    }
    for (size_t i = 0; i < s.audio.size(); ++i) {
        const AudioLine& l = s.audio[i];
        const double at = l.delayBeforeFrames / kFramesPerSecond;
        if (!lipsyncFired_[i] && !l.lipsyncClip.empty() && elapsed >= at - kLipsyncLead) {
            lipsyncFired_[i] = 1;
            Event e = Ev(EventKind::LipsyncStart, shot_, static_cast<int>(i));
            e.text = l.lipsyncClip;
            e.id0 = static_cast<uint32_t>(l.lipsyncCharIndex);
            out.push_back(e);
        }
        if (!subtitleFired_[i] && elapsed >= at) {
            subtitleFired_[i] = 1;
            Event e = Ev(EventKind::SubtitleShow, shot_, static_cast<int>(i));
            e.text = l.subtitleKey;
            e.value = l.subtitleDurationFrames / kFramesPerSecond;
            out.push_back(e);
        }
    }
}

void CutscenePlayer::seekTo(double t, std::vector<Event>& out) {
    t_ = std::clamp(t, 0.0, duration_);
    Event e = Ev(EventKind::SoundtrackSeek);
    e.value = t_ * 1000.0;
    out.push_back(e);
    int k = shot_;
    for (size_t i = 0; i < spans_.size(); ++i)
        if (t_ >= spans_[i].start) k = static_cast<int>(i);
    if (k != shot_ && k >= 0) beginShot(k, out);
}

void CutscenePlayer::finish(std::vector<Event>& out) {
    state_ = PlayState::Stopped;
    Event stop = Ev(EventKind::PlaybackStopped);
    stop.flag = skipped_;
    out.push_back(stop);
    releaseShotClips(shot_, out);
    if (scene_.soundtrack && ParseSoundtrack(*scene_.soundtrack)) out.push_back(Ev(EventKind::SoundtrackStop));
    if (scene_.kind == SceneKind::Zscene) out.push_back(Ev(EventKind::ZsceneUnmute));
    if (scene_.binkMovieFilename) {
        if (binkFullscreen_) out.push_back(Ev(EventKind::BinkFullscreenPop));
        out.push_back(Ev(EventKind::BinkStop));
        binkFullscreen_ = false;
    }
    if (scene_.kind != SceneKind::Designer) out.push_back(Ev(EventKind::CameraExit));
    out.push_back(Ev(EventKind::RestoreOptionsAndVisibility));
    if (scene_.nextScene && !scene_.nextScene->empty()) {
        Event c = Ev(EventKind::ChainNextScene);
        c.text = *scene_.nextScene;
        out.push_back(c);
    } else if (fadeRequested_) {
        Event f = Ev(EventKind::FadeIn);
        f.value = quickFade_ ? kQuickFadeMs : kNormalFadeInMs;
        out.push_back(f);
    }
    state_ = PlayState::Done;
    out.push_back(Ev(EventKind::Finished));
}

bool CutscenePlayer::requestSkip(bool blockingFlagSet) {
    const int s = static_cast<int>(state_);
    if (s < 10 || s > 13 || blockingFlagSet) return false;
    if (!(sincePlaybackStart_ > kSkipMinSinceStart)) return false;
    if (scene_.completionScreen && !(duration_ - t_ > kSkipMinRemainingCompletion)) return false;
    state_ = PlayState::SkipRequested;
    return true;
}

std::vector<Event> CutscenePlayer::pause() {
    std::vector<Event> out;
    if (!paused_ && (state_ == PlayState::Playing || state_ == PlayState::SkipRequested)) {
        paused_ = true;
        out.push_back(Ev(EventKind::SoundtrackPause));
    }
    return out;
}

std::vector<Event> CutscenePlayer::resume() {
    std::vector<Event> out;
    if (paused_) {
        paused_ = false;
        out.push_back(Ev(EventKind::SoundtrackResume));
    }
    return out;
}

std::vector<Event> CutscenePlayer::step(double dt) {
    std::vector<Event> out;
    if (!started_ || paused_ || state_ == PlayState::Done) return out;

    // A requested skip (state 11).
    if (state_ == PlayState::SkipRequested) {
        const bool tailOrCompletion = scene_.tailHoldSeconds > 0.0 || scene_.completionScreen ||
                                      NameIs(scene_.name, "17_out");
        if (NameIs(scene_.name, "01_in") && shot_ == 0) {
            // 01_in on its first shot: state 12 and the skip timer re-stamped.
            state_ = PlayState::Playing12;
            sincePlaybackStart_ = 0.0;
        } else if (!tailOrCompletion) {
            skipped_ = true;
            if (!fadeRequested_) {
                Event f = Ev(EventKind::FadeOut);
                f.value = scene_.kind == SceneKind::Zscene ? kQuickFadeMs : 0.0;
                out.push_back(f);
                fadeRequested_ = true;
                quickFade_ = scene_.kind == SceneKind::Zscene;
            }
            finish(out);
            return out;
        } else if (!tailSkipPending_) {
            tailSkipPending_ = true;
            if (NameIs(scene_.name, "17_out")) scene_.completionScreen = true;
            Event f = Ev(EventKind::FadeOut);
            f.flag = true;  // the fade duration is not stated
            out.push_back(f);
            fadeRequested_ = true;
            fadedOut_ = false;
            return out;
        } else if (fadedOut_) {
            // Faded: jump to the last shot leaving only the hold, stop the
            // movie, stop the soundtrack, then play the tail (state 10).
            const double hold = scene_.tailHoldSeconds > 0.0 ? scene_.tailHoldSeconds
                                                             : kSkipMinRemainingCompletion;
            if (!spans_.empty()) {
                if (shot_ != static_cast<int>(spans_.size()) - 1) seekTo(spans_.back().start, out);
                seekTo(std::max(spans_.back().start, duration_ - hold), out);
            }
            if (scene_.binkMovieFilename) {
                if (binkFullscreen_) out.push_back(Ev(EventKind::BinkFullscreenPop));
                out.push_back(Ev(EventKind::BinkStop));
                binkFullscreen_ = false;
            }
            Event s = Ev(EventKind::SoundtrackStop);
            s.flag = true;  // Stop_Cinematics only; the bank stays until teardown
            out.push_back(s);
            skipped_ = true;
            tailSkipPending_ = false;
            state_ = PlayState::Playing;
            return out;
        } else {
            return out;  // waiting for the fade
        }
    }

    sincePlaybackStart_ += dt;
    t_ = std::min(t_ + dt, duration_);

    // Shot boundaries crossed this step, in order.
    while (shot_ + 1 < static_cast<int>(spans_.size()) && t_ >= spans_[static_cast<size_t>(shot_) + 1].start &&
           t_ < duration_) {
        beginShot(shot_ + 1, out);
    }
    shotUpdate(out);

    // Fade before the end.
    const double left = duration_ - t_;
    if (!fadeRequested_) {
        if (scene_.kind == SceneKind::Zscene && left <= kZsceneEndFadeLead) {
            Event f = Ev(EventKind::FadeOut);
            f.value = kQuickFadeMs;
            out.push_back(f);
            fadeRequested_ = true;
            quickFade_ = true;
        } else if (scene_.kind != SceneKind::Zscene && left <= kOtherEndFadeLead) {
            Event f = Ev(EventKind::FadeOut);
            f.value = 0.0;
            out.push_back(f);
            fadeRequested_ = true;
        }
    }

    // End of the clock; with the completion screen, wait for its HUD.
    if (t_ >= duration_ && (!scene_.completionScreen || completionHudGone_)) finish(out);
    return out;
}

}  // namespace sr3cutsceneplay
