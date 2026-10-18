// sr3radio player: drives RadioSystem (logic) + RadioMedia (cue -> PCM) + AudioOutputDevice (XAudio2).
// Spec-audio-runtime.md Sec14.6.2 (timeline, join-mid-song seek), 14.6.3 (item posting), 14.6.5.
//  - enterVehicle(): default station (14.6.1) -> tune -> becomeAudible posts the current item with seek.
//  - update(): ticks the timeline; every posted item is decoded on a worker thread (one song of PCM,
//    never a whole bank) and submitted once ready, skipping the seek offset plus decode latency so the
//    audible position matches the station timeline.
//  - The previous item keeps playing under the next one; the voice from two items back is stopped.
// CHOSEN (spec OPEN: Wwise attenuation/ducking in init.bnk_pc): flat 2D playback at kRadioVolume, no
// distance model (the player's own car is the only listener), no on/off bus fades (ids 0xC4F14EEA /
// 0x01DF15F0 are undecoded buses).
#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "sr3radio/radio_media.h"
#include "sr3radio/radio_sim.h"

namespace sr3audioruntime { class AudioOutputDevice; }

namespace sr3radio {

constexpr float kRadioVolume = 0.8f;   // CHOSEN

class RadioPlayer {
public:
    // `device` may be null (headless): logic and cue resolution still run; with decodeWithoutDevice the
    // worker also decodes (for verification) but nothing is submitted.
    RadioPlayer(RadioSystem& sys, const RadioMedia* media, sr3audioruntime::AudioOutputDevice* device,
                bool decodeWithoutDevice = false, bool verbose = true);
    ~RadioPlayer();

    // Vehicle id is any stable handle. lastStation = per-vehicle +0x1630 (0xFF = none).
    // Returns the tuned station number (0 = none).
    int enterVehicle(int vehicle, int lastStation = 0xFF);
    void exitVehicle();
    // D-pad equivalent: +1 next, -1 previous. Returns the new station number.
    int cycle(int dir);
    // dt in ms of game time. Call every frame.
    void update(uint32_t dtMs);

    int station() const { return station_; }
    bool inVehicle() const { return vehicle_ != 0; }
    int lastStationOfVehicle() const { return station_; }
    // counters for logs / tests
    uint32_t itemsPosted() const { return itemsPosted_; }
    uint32_t itemsStarted() const { return itemsStarted_; }
    uint32_t itemsFailed() const { return itemsFailed_; }

private:
    struct Job {
        std::future<void> fut;
        std::shared_ptr<struct JobResult> res;
        std::chrono::steady_clock::time_point posted;
        uint32_t seekMs = 0;
        uint64_t generation = 0;
        std::string cue;
    };
    void startItem(const PostedItem& p);
    void stopAllVoices();
    void reap();
    void log(const char* fmt, ...) const;

    RadioSystem& sys_;
    const RadioMedia* media_;
    sr3audioruntime::AudioOutputDevice* dev_;
    bool decodeDry_, verbose_;
    int vehicle_ = 0;
    int station_ = 0;
    uint64_t generation_ = 1;
    std::deque<Job> jobs_;
    std::deque<uint64_t> voices_;
    uint32_t itemsPosted_ = 0, itemsStarted_ = 0, itemsFailed_ = 0;
};

}  // namespace sr3radio
