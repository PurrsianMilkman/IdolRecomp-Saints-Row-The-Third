#pragma once

// sr3audioruntime - the audio bring-up task's own "last mile": a minimal
// Windows audio OUTPUT device that actually sends already-decoded PCM to a
// speaker. Everything upstream of this file (real_vorbis_decode.h's Vorbis
// decode, wav_writer.h's file sink) already produces/consumes real
// interleaved 16-bit PCM; nothing before this file has ever opened an actual
// playback device. This file closes exactly that gap and nothing more.
//
// ---------------------------------------------------------------------
// WHY XAudio2, not raw WASAPI
// ---------------------------------------------------------------------
// XAudio2 (the version that ships inline in the Windows 10+ SDK's
// <xaudio2.h>, CLSID-activated via CoCreateInstance - no import .lib needed
// beyond ole32, same COM footing this project's D3D11 device already uses
// for DXGI) gives one-call "submit a PCM buffer, it plays" semantics with a
// built-in small-voice-pool model, instead of this project having to hand-
// roll WASAPI's own ring-buffer/event-callback pump loop for a first cut.
// This project already links against Windows platform libs for D3D11
// (d3d11/dxgi/d3dcompiler/user32/gdi32, see CMakeLists.txt's sr3render
// target) - XAudio2 adds only `ole32` (CoInitializeEx/CoCreateInstance), no
// new SDK surface.
//
// ---------------------------------------------------------------------
// SCOPE, stated plainly (first cut, NOT a full mixer)
// ---------------------------------------------------------------------
// - A SMALL FIXED POOL of concurrent one-shot source voices (kMaxVoices
//   below). Once that many are simultaneously playing, a new playOneShot()
//   call fails (returns false, does not block, does not evict/steal an
//   active voice) until pump() reaps a finished one. This project's own
//   object-sound-core (spec-audio-runtime.md Sec11a.4) runs a real 32-slot
//   pool with steal-oldest-finished eviction; this file's pool is
//   DELIBERATELY simpler (no stealing) since a handful of simultaneous
//   voices is enough for a first audible cut (footsteps/one engine loop),
//   and inventing an eviction policy this project's own spec never
//   describes for THIS layer would be exactly the kind of guess the
//   no-invented-fixes rule forbids.
// - Each playOneShot() call creates its own IXAudio2SourceVoice (one voice
//   per concurrently-playing sound, not a shared streaming voice) and COPIES
//   the caller's PCM into voice-owned storage (XAudio2 reads directly from
//   the buffer pointer it's given for as long as the voice plays - the
//   caller's own buffer, e.g. a local DecodeAttemptResult::pcm, routinely
//   does not outlive the call). This is a real, deliberate memory copy, not
//   a shortcut; for the short one-shot clips this first cut targets
//   (footsteps, UI pings) the cost is negligible. A looping/streamed voice
//   (e.g. a continuous car-engine loop) would want a different, queued-
//   buffer design - NOT built here, see the "still open" list below.
// - NO distance-attenuation CURVE is applied (see playOneShot3D()'s own doc
//   comment for exactly why - spec-audio-runtime.md Sec11a.5's own "No -
//   entirely inside Wwise's own statically-linked code" verdict, a
//   permanent, confirmed gap, not a time-crunch shortcut).
// - NO reverb/aux-send, NO occlusion/obstruction (both already documented as
//   not reaching Wwise's own API surface at all - Sec2.2/Sec11a.3/Sec11a.5),
//   NO bank-driven bus routing. This device plays flat interleaved PCM to
//   the one default output device's mastering voice, nothing more.
//
// ---------------------------------------------------------------------
// Still OPEN / explicitly NOT attempted here
// ---------------------------------------------------------------------
// - Picking a non-default output device (this always opens
//   AudioEndpoint "default" via XAudio2's own CreateMasteringVoice(..., NULL
//   device id)).
// - Looping/streamed voices (continuous engine/ambient loops) - this file's
//   API is one-shot-submit-and-forget only; a caller wanting a loop must
//   resubmit, which will audibly click at the loop seam (no crossfade).
// - Per-voice real-time pitch/doppler.
// - Any multi-channel speaker layout beyond whatever the PCM's own
//   `channels` field already says (mono/stereo passthrough only).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sr3audioruntime/real_vorbis_decode.h"  // reuses DecodeAttemptResult's own pcm/channels/sampleRate shape - no new PCM struct invented

namespace sr3audioruntime {

// Fixed, small concurrent-voice cap for this first cut - see this file's own
// top comment for why no stealing/eviction policy is implemented.
constexpr size_t kAudioOutputMaxConcurrentVoices = 8;

class AudioOutputDevice {
public:
    AudioOutputDevice();
    ~AudioOutputDevice();

    AudioOutputDevice(const AudioOutputDevice&) = delete;
    AudioOutputDevice& operator=(const AudioOutputDevice&) = delete;

    // Opens the default output device (XAudio2 engine + one mastering
    // voice). Safe to call at most once per instance; returns false (with a
    // human-readable reason in `error`) on any COM/XAudio2 failure - never
    // throws, never crashes a caller that simply doesn't have a speaker
    // (e.g. a CI build agent with no audio hardware: XAudio2Create/
    // CreateMasteringVoice failing there is EXPECTED and handled as an
    // ordinary false return, not a special case).
    bool initialise(std::string& error);

    // Releases the mastering voice, every still-active source voice, the
    // XAudio2 engine, and (if this instance itself called CoInitializeEx)
    // uninitialises COM. Safe to call even if initialise() was never called
    // or failed. Called automatically by the destructor.
    void shutdown();

    bool isInitialised() const { return xaudio2_ != nullptr; }

    // Reaps finished voices (XAudio2's OnStreamEnd callback only sets a
    // flag - DestroyVoice() must not be called from inside that callback
    // per XAudio2's own documented restriction, so this does the actual
    // cleanup on the caller's thread). Call once per frame/tick from the
    // same thread that calls playOneShot(); playOneShot() also calls this
    // internally before checking the pool for a free slot, so an explicit
    // per-frame call is a convenience (keeps the active-voice count
    // accurate for callers inspecting it), not a correctness requirement.
    void pump();

    // Submits raw interleaved 16-bit PCM for one-shot playback at `volume`
    // (XAudio2 SetVolume units - 1.0 = unattenuated, matching this project's
    // own existing [0,1] volume convention, nothing else).  Returns false
    // (sets `error`, submits nothing) if: not initialised; `pcm`/`sampleCount`
    // is empty; `channels` is not 1 or 2; or the concurrent-voice pool
    // (kAudioOutputMaxConcurrentVoices) is full. The PCM is COPIED - see this
    // file's own top comment.
    bool playOneShot(const int16_t* interleavedPcm, size_t sampleCount, long sampleRate, int channels,
                      float volume, std::string& error);

    // Convenience overload: plays a real_vorbis_decode.h DecodeAttemptResult
    // directly (the actual decode-pipeline output shape this task's own
    // brief says to reuse, not reinvent) - caller must have already checked
    // `decoded.ok`.
    bool playOneShot(const DecodeAttemptResult& decoded, float volume, std::string& error);

    // Like playOneShot() but reports a stable voice id (never 0) so a caller
    // can stop it early (vehicle radio: leaving the car cuts the song).
    // `outVoiceId` is set only on success.
    bool playTracked(const int16_t* interleavedPcm, size_t sampleCount, long sampleRate, int channels,
                      float volume, std::string& error, uint64_t* outVoiceId);
    // Stops and destroys one tracked voice (no-op if unknown/finished).
    void stopVoice(uint64_t id);
    // True while the tracked voice has not finished playing.
    bool isVoiceActive(uint64_t id);

    // THE 3D CALL. Computes the real, spec-CONFIRMED straight-line distance
    // (spec-audio-runtime.md Sec11a.3's own `0x004685c0` mechanism,
    // single-position-sample case - no SetMultiplePositions equivalent here)
    // between `emitterPos` and `listenerPos` and logs it (via `outDistance`,
    // optional) for verification/diagnostic purposes, but - DELIBERATELY,
    // see this file's own top comment and spec-audio-runtime.md Sec11a.5's
    // own attenuation-verdict table - does NOT turn that distance into a
    // volume/LPF rolloff: "Distance/cone attenuation curve evaluation...
    // entirely inside Wwise's own statically-linked code" is Sec11a.5's own
    // CONFIRMED-negative verdict, a permanent architectural fact about this
    // binary, not an unopened lead this pass ran out of time for. Inventing
    // a rolloff curve/exponent/reference-distance number here (something
    // like "1/distance^2 past 5m") would be fabricating a value this
    // project's own spec never supplies, exactly what the no-invented-fixes
    // rule forbids. Plays at flat `volume` every time; `outDistance`, when
    // given, lets a caller log/verify the real positioning math ran even
    // though it does not (yet, pending a real curve source) affect what's
    // heard.
    bool playOneShot3D(const int16_t* interleavedPcm, size_t sampleCount, long sampleRate, int channels,
                        float volume, const float emitterPos[3], const float listenerPos[3],
                        std::string& error, double* outDistance = nullptr);
    bool playOneShot3D(const DecodeAttemptResult& decoded, float volume, const float emitterPos[3],
                        const float listenerPos[3], std::string& error, double* outDistance = nullptr);

    // Verification counters (THIS task's own "you cannot literally hear it,
    // verify what you can" ask) - monotonically increasing for the lifetime
    // of this instance, never reset by pump()/shutdown().
    uint64_t buffersSubmittedTotal() const { return buffersSubmitted_; }
    uint64_t bytesSubmittedTotal() const { return bytesSubmitted_; }
    size_t activeVoiceCount() const { return activeVoices_.size(); }

private:
    struct VoiceSlot;  // defined in the .cpp (hides the IXAudio2 callback type from this header)

    bool comInitialisedByUs_ = false;
    void* xaudio2_ = nullptr;       // IXAudio2* (void* here so this header never needs <xaudio2.h>)
    void* masteringVoice_ = nullptr;  // IXAudio2MasteringVoice*
    std::vector<std::unique_ptr<VoiceSlot>> activeVoices_;

    uint64_t nextVoiceId_ = 1;
    uint64_t buffersSubmitted_ = 0;
    uint64_t bytesSubmitted_ = 0;
};

}  // namespace sr3audioruntime
