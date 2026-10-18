// sr3radio player implementation. See include/sr3radio/radio_player.h.
#include "sr3radio/radio_player.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include "sr3audioruntime/audio_output_device.h"

namespace sr3radio {

struct JobResult {
    bool ok = false;
    std::string why;
    sr3audioruntime::DecodeAttemptResult pcm;
};

RadioPlayer::RadioPlayer(RadioSystem& sys, const RadioMedia* media, sr3audioruntime::AudioOutputDevice* device,
                         bool decodeWithoutDevice, bool verbose)
    : sys_(sys), media_(media), dev_(device), decodeDry_(decodeWithoutDevice), verbose_(verbose) {}

RadioPlayer::~RadioPlayer() {
    stopAllVoices();
    for (auto& j : jobs_) if (j.fut.valid()) j.fut.wait();
}

void RadioPlayer::log(const char* fmt, ...) const {
    if (!verbose_) return;
    std::va_list ap; va_start(ap, fmt);
    std::fputs("[radio] ", stdout); std::vfprintf(stdout, fmt, ap); std::fputc('\n', stdout);
    va_end(ap);
}

void RadioPlayer::stopAllVoices() {
    if (dev_) for (uint64_t v : voices_) dev_->stopVoice(v);
    voices_.clear();
}

int RadioPlayer::enterVehicle(int vehicle, int lastStation) {
    if (vehicle_ != 0) exitVehicle();
    vehicle_ = vehicle;
    sys_.posted.clear();
    int st = sys_.defaultStation(lastStation, false);
    if (st > 0 && !sys_.tune(vehicle, st, true)) st = 0;
    station_ = st;
    sys_.playerBus(true, st);   // bus events recorded in sys_.busEvents (bus ids undecoded: no audible effect)
    if (st > 0) log("enter vehicle %d -> station %d (%s)", vehicle, st, sys_.stations[(size_t)st - 1].name.c_str());
    else log("enter vehicle %d -> no station", vehicle);
    // tune() -> becomeAudible() queued the join item (with seek).
    update(0);
    return station_;
}

void RadioPlayer::exitVehicle() {
    if (vehicle_ == 0) return;
    sys_.unregister(vehicle_);
    sys_.playerBus(false, 0);
    ++generation_;         // drop in-flight decodes
    stopAllVoices();
    log("exit vehicle %d: station %d stopped (timeline keeps running)", vehicle_, station_);
    vehicle_ = 0; station_ = 0;
    sys_.posted.clear();
}

int RadioPlayer::cycle(int dir) {
    if (vehicle_ == 0) return 0;
    const int before = station_;
    int now = sys_.cycle(vehicle_, dir, true);
    station_ = now;
    if (now != before) {
        sys_.playerBus(true, now);
        ++generation_;
        stopAllVoices();    // the new station starts through its own post below
        log("station %d -> %d (%s)", before, now,
            now > 0 ? sys_.stations[(size_t)now - 1].name.c_str() : "none");
    }
    update(0);
    return station_;
}

void RadioPlayer::startItem(const PostedItem& p) {
    if (p.station != station_) return;                      // nobody listening to that one
    if (p.content >= sys_.content.entries.size()) return;
    const ContentEntry& e = sys_.content.entries[p.content];
    ++itemsPosted_;
    log("post station %d item %u type=%d cue=%s length=%ums seek=%ums", p.station, (unsigned)p.content,
        (int)e.type, e.cue.c_str(), e.lengthMs, p.seekMs);
    if (!media_ || !media_->isOpen()) return;
    MediaLoc loc; std::string why;
    if (!media_->resolve(e.cueId, e.type, loc, why)) {
        ++itemsFailed_;
        log("  resolve FAILED (%s)", why.c_str());
        return;
    }
    log("  resolved sound 0x%08X media 0x%08X (%u bytes)", loc.soundId, loc.mediaId, loc.size);
    if (!dev_ && !decodeDry_) return;
    Job job;
    job.res = std::make_shared<JobResult>();
    job.posted = std::chrono::steady_clock::now();
    job.seekMs = p.seekMs; job.generation = generation_; job.cue = e.cue;
    const RadioMedia* media = media_;
    std::shared_ptr<JobResult> res = job.res;
    job.fut = std::async(std::launch::async, [media, loc, res]() {
        res->ok = media->decode(loc, res->pcm, res->why);
    });
    jobs_.push_back(std::move(job));
}

void RadioPlayer::reap() {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
        it->fut.get();
        if (it->generation != generation_) { it = jobs_.erase(it); continue; }
        const auto& r = *it->res;
        if (!r.ok) {
            ++itemsFailed_; log("  decode FAILED for %s (%s)", it->cue.c_str(), r.why.c_str());
        } else {
            const double latencyMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - it->posted).count();
            const size_t ch = (size_t)std::max(1, r.pcm.channels);
            const uint64_t skipFrames = (uint64_t)((double)(it->seekMs + (dev_ ? latencyMs : 0.0)) *
                                                   (double)r.pcm.sampleRate / 1000.0);
            if (skipFrames >= r.pcm.totalSampleFrames) {
                log("  %s already past its end (seek %.0f ms) - skipped", it->cue.c_str(), (double)it->seekMs);
            } else if (dev_) {
                std::string err; uint64_t id = 0;
                if (dev_->playTracked(r.pcm.pcm.data() + skipFrames * ch, (size_t)(r.pcm.pcm.size() - skipFrames * ch),
                                      r.pcm.sampleRate, r.pcm.channels, kRadioVolume, err, &id)) {
                    ++itemsStarted_;
                    voices_.push_back(id);
                    while (voices_.size() > 3) { dev_->stopVoice(voices_.front()); voices_.pop_front(); }  // 2 items back
                    log("  playing %s (%ld Hz x%d, %llu frames, skipped %llu, decode+queue %.0f ms)", it->cue.c_str(),
                        r.pcm.sampleRate, r.pcm.channels, (unsigned long long)r.pcm.totalSampleFrames,
                        (unsigned long long)skipFrames, latencyMs);
                } else { ++itemsFailed_; log("  submit FAILED (%s)", err.c_str()); }
            } else {
                ++itemsStarted_;
                log("  decoded %s (%ld Hz x%d, %llu frames) [no audio device: not played]", it->cue.c_str(),
                    r.pcm.sampleRate, r.pcm.channels, (unsigned long long)r.pcm.totalSampleFrames);
            }
        }
        it = jobs_.erase(it);
    }
}

void RadioPlayer::update(uint32_t dtMs) {
    if (vehicle_ != 0) {
        // The timeline always runs (every station, in the engine); only the audible one matters here.
        sys_.tick(dtMs);
        // Only the newest post of a batch is still current (a long frame can expire several items).
        const PostedItem* last = nullptr;
        for (const auto& p : sys_.posted) if (p.station == station_) last = &p;
        if (last) startItem(*last);
    } else {
        sys_.tick(dtMs);
    }
    sys_.posted.clear();
    if (dev_) dev_->pump();
    reap();
}

}  // namespace sr3radio
