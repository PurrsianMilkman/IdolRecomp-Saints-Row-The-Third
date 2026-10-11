// See include/sr3audioruntime/audio_output_device.h for the full scope
// statement (what this first cut does and deliberately does not do).

#include "sr3audioruntime/audio_output_device.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <xaudio2.h>
#pragma comment(lib, "ole32.lib")
#endif

namespace sr3audioruntime {

#if defined(_WIN32)

namespace {

// Marks `*finished` true from XAudio2's own audio-processing thread when a
// one-shot buffer completes. DestroyVoice() is documented as unsafe to call
// from inside an XAudio2 callback, so the callback does nothing but flip a
// flag - AudioOutputDevice::pump() (caller's thread) does the real cleanup.
class OneShotVoiceCallback : public IXAudio2VoiceCallback {
public:
    explicit OneShotVoiceCallback(std::shared_ptr<std::atomic<bool>> finished) : finished_(std::move(finished)) {}

    void OnStreamEnd() noexcept override {
        if (finished_) finished_->store(true, std::memory_order_release);
    }
    void OnVoiceProcessingPassStart(UINT32) noexcept override {}
    void OnVoiceProcessingPassEnd() noexcept override {}
    void OnBufferStart(void*) noexcept override {}
    void OnBufferEnd(void*) noexcept override {
        // Belt-and-suspenders: a one-shot XAUDIO2_END_OF_STREAM buffer
        // should always also fire OnStreamEnd, but mark finished here too in
        // case a future change submits buffers without that flag set.
        if (finished_) finished_->store(true, std::memory_order_release);
    }
    void OnLoopEnd(void*) noexcept override {}
    void OnVoiceError(void*, HRESULT) noexcept override {
        if (finished_) finished_->store(true, std::memory_order_release);
    }

private:
    std::shared_ptr<std::atomic<bool>> finished_;
};

}  // namespace

struct AudioOutputDevice::VoiceSlot {
    IXAudio2SourceVoice* voice = nullptr;
    std::unique_ptr<OneShotVoiceCallback> callback;
    std::shared_ptr<std::atomic<bool>> finished;
    std::vector<int16_t> ownedPcm;  // the real copy - see this file's header's own top comment
};

AudioOutputDevice::AudioOutputDevice() = default;

AudioOutputDevice::~AudioOutputDevice() { shutdown(); }

bool AudioOutputDevice::initialise(std::string& error) {
    if (xaudio2_ != nullptr) {
        error = "AudioOutputDevice::initialise() called twice on the same instance";
        return false;
    }

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (comHr == S_OK) {
        comInitialisedByUs_ = true;
    } else if (comHr != S_FALSE && comHr != RPC_E_CHANGED_MODE) {
        // RPC_E_CHANGED_MODE: COM already initialised on this thread with a
        // different concurrency model by an earlier, unrelated caller (e.g.
        // the window/device setup code elsewhere in this process) - not our
        // failure to report, and not ours to uninitialise either.
        error = "CoInitializeEx failed, hr=0x" + std::to_string(static_cast<unsigned long>(comHr));
        return false;
    }

    IXAudio2* engine = nullptr;
    HRESULT hr = XAudio2Create(&engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || engine == nullptr) {
        error = "XAudio2Create failed, hr=0x" + std::to_string(static_cast<unsigned long>(hr)) +
                " (no audio device/driver available is an expected, non-fatal outcome on a headless build agent)";
        if (comInitialisedByUs_) {
            CoUninitialize();
            comInitialisedByUs_ = false;
        }
        return false;
    }

    IXAudio2MasteringVoice* mastering = nullptr;
    hr = engine->CreateMasteringVoice(&mastering);
    if (FAILED(hr) || mastering == nullptr) {
        error = "IXAudio2::CreateMasteringVoice failed, hr=0x" + std::to_string(static_cast<unsigned long>(hr));
        engine->Release();
        if (comInitialisedByUs_) {
            CoUninitialize();
            comInitialisedByUs_ = false;
        }
        return false;
    }

    xaudio2_ = engine;
    masteringVoice_ = mastering;
    return true;
}

void AudioOutputDevice::shutdown() {
    for (auto& slot : activeVoices_) {
        if (slot->voice != nullptr) {
            slot->voice->Stop(0);
            slot->voice->DestroyVoice();
            slot->voice = nullptr;
        }
    }
    activeVoices_.clear();

    if (masteringVoice_ != nullptr) {
        static_cast<IXAudio2MasteringVoice*>(masteringVoice_)->DestroyVoice();
        masteringVoice_ = nullptr;
    }
    if (xaudio2_ != nullptr) {
        static_cast<IXAudio2*>(xaudio2_)->Release();
        xaudio2_ = nullptr;
    }
    if (comInitialisedByUs_) {
        CoUninitialize();
        comInitialisedByUs_ = false;
    }
}

void AudioOutputDevice::pump() {
    activeVoices_.erase(
        std::remove_if(activeVoices_.begin(), activeVoices_.end(),
                        [](const std::unique_ptr<VoiceSlot>& slot) {
                            const bool done = slot->finished && slot->finished->load(std::memory_order_acquire);
                            if (done && slot->voice != nullptr) {
                                slot->voice->DestroyVoice();
                            }
                            return done;
                        }),
        activeVoices_.end());
}

bool AudioOutputDevice::playOneShot(const int16_t* interleavedPcm, size_t sampleCount, long sampleRate,
                                     int channels, float volume, std::string& error) {
    if (xaudio2_ == nullptr) {
        error = "AudioOutputDevice not initialised";
        return false;
    }
    if (interleavedPcm == nullptr || sampleCount == 0) {
        error = "empty PCM buffer";
        return false;
    }
    if (channels != 1 && channels != 2) {
        error = "unsupported channel count (only mono/stereo passthrough, see this file's own scope note)";
        return false;
    }
    if (sampleRate <= 0) {
        error = "invalid sample rate";
        return false;
    }

    pump();  // reap finished voices before checking capacity
    if (activeVoices_.size() >= kAudioOutputMaxConcurrentVoices) {
        error = "concurrent-voice pool full (kAudioOutputMaxConcurrentVoices=" +
                std::to_string(kAudioOutputMaxConcurrentVoices) +
                ") - this first cut does not steal/evict, see this file's own scope note";
        return false;
    }

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = static_cast<WORD>(channels);
    wfx.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = static_cast<WORD>(channels * 2);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    auto slot = std::make_unique<VoiceSlot>();
    slot->finished = std::make_shared<std::atomic<bool>>(false);
    slot->callback = std::make_unique<OneShotVoiceCallback>(slot->finished);
    slot->ownedPcm.assign(interleavedPcm, interleavedPcm + sampleCount);

    IXAudio2SourceVoice* voice = nullptr;
    HRESULT hr = static_cast<IXAudio2*>(xaudio2_)->CreateSourceVoice(&voice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
                                                                      slot->callback.get());
    if (FAILED(hr) || voice == nullptr) {
        error = "IXAudio2::CreateSourceVoice failed, hr=0x" + std::to_string(static_cast<unsigned long>(hr));
        return false;
    }
    slot->voice = voice;

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(slot->ownedPcm.size() * sizeof(int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(slot->ownedPcm.data());
    buffer.Flags = XAUDIO2_END_OF_STREAM;

    voice->SetVolume(std::max(0.0f, volume));
    hr = voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        error = "IXAudio2SourceVoice::SubmitSourceBuffer failed, hr=0x" + std::to_string(static_cast<unsigned long>(hr));
        voice->DestroyVoice();
        return false;
    }
    hr = voice->Start(0, 0);
    if (FAILED(hr)) {
        error = "IXAudio2SourceVoice::Start failed, hr=0x" + std::to_string(static_cast<unsigned long>(hr));
        voice->DestroyVoice();
        return false;
    }

    buffersSubmitted_++;
    bytesSubmitted_ += buffer.AudioBytes;
    activeVoices_.push_back(std::move(slot));
    return true;
}

bool AudioOutputDevice::playOneShot(const DecodeAttemptResult& decoded, float volume, std::string& error) {
    if (!decoded.ok) {
        error = "DecodeAttemptResult::ok is false - nothing to play";
        return false;
    }
    return playOneShot(decoded.pcm.data(), decoded.pcm.size(), decoded.sampleRate, decoded.channels, volume, error);
}

namespace {
// Plain 3D Euclidean distance - the degenerate (single-position-sample)
// case of spec-audio-runtime.md Sec11a.3's own closest-of-N-samples
// mechanism (`0x004685c0`): with exactly one sample, "closest of N" is just
// the one distance. Generic public geometry, not a clean-room concern.
double euclideanDistance3(const float a[3], const float b[3]) {
    const double dx = static_cast<double>(a[0]) - static_cast<double>(b[0]);
    const double dy = static_cast<double>(a[1]) - static_cast<double>(b[1]);
    const double dz = static_cast<double>(a[2]) - static_cast<double>(b[2]);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
}  // namespace

bool AudioOutputDevice::playOneShot3D(const int16_t* interleavedPcm, size_t sampleCount, long sampleRate,
                                       int channels, float volume, const float emitterPos[3],
                                       const float listenerPos[3], std::string& error, double* outDistance) {
    if (outDistance != nullptr) *outDistance = euclideanDistance3(emitterPos, listenerPos);
    // Deliberately flat volume - see this file's header's own doc comment on
    // playOneShot3D() for exactly why no curve is applied.
    return playOneShot(interleavedPcm, sampleCount, sampleRate, channels, volume, error);
}

bool AudioOutputDevice::playOneShot3D(const DecodeAttemptResult& decoded, float volume, const float emitterPos[3],
                                       const float listenerPos[3], std::string& error, double* outDistance) {
    if (outDistance != nullptr) *outDistance = euclideanDistance3(emitterPos, listenerPos);
    return playOneShot(decoded, volume, error);
}

#else  // !_WIN32

// This project is Windows-only end to end (D3D11 rendering, see
// include/sr3render/device.h) - this file has never needed a non-Windows
// fallback for anything else, so none is invented here either. A build on
// another platform gets a clear link/runtime failure rather than a silent
// no-op stub pretending to work.
#error "sr3audioruntime::AudioOutputDevice is Windows-only (XAudio2) - see this file's own top comment"

#endif  // _WIN32

}  // namespace sr3audioruntime
