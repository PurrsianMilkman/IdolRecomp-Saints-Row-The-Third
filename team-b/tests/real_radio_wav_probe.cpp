// CLI + ctest: decodes the first N seconds (default 5) of one radio station's first song to a WAV in the
// system temp dir and reports length and peak level. Real data; exits 77 (SKIP) if the cache is absent.
//   radio_wav_probe [--station N (1-based, default 3 = GenX)] [--seconds S] [--seek-ms M] [--out file.wav]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#define NOMINMAX
#include <windows.h>
#include "sr3audioruntime/audio_output_device.h"
#include "sr3audioruntime/wav_writer.h"
#include "sr3radio/radio_media.h"

namespace fs = std::filesystem;
using namespace sr3radio;

int main(int argc, char** argv) {
    int station = 3; double seconds = 5.0; double seekMs = 0; std::string out;
    bool play = false;
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--play")) play = true;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--station")) station = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--seconds")) seconds = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--seek-ms")) seekMs = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--out")) out = argv[i + 1];
    }
    const fs::path cache = CRREISH_GAME_CACHE_DIR;
    if (!fs::exists(cache / "misc_tables.vpp_pc") || !fs::exists(cache / "sounds_common.vpp_pc")) {
        std::printf("SKIP: game cache not present\n");
        return 77;
    }
    if (out.empty()) out = (fs::temp_directory_path() / ("sr3_radio_station" + std::to_string(station) + ".wav")).string();

    std::string err;
    XtblArchive arch;
    if (!arch.open((cache / "misc_tables.vpp_pc").string(), err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    RadioSystem sys; RadioLoadReport rep;
    if (!loadRadioContent(arch.provider(), sys, rep, err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    if (station < 1 || (size_t)station > sys.stations.size() || sys.stations[(size_t)station - 1].songs.idx.empty()) {
        std::printf("FAIL: station %d has no songs\n", station); return 1;
    }
    const ContentEntry& e = sys.content.entries[sys.stations[(size_t)station - 1].songs.idx[0]];
    RadioMedia media;
    if (!media.open((cache / "sounds_common.vpp_pc").string(), err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    MediaLoc loc; std::string why;
    if (!media.resolve(e.cueId, e.type, loc, why)) { std::printf("FAIL: resolve %s: %s\n", e.cue.c_str(), why.c_str()); return 1; }
    sr3audioruntime::DecodeAttemptResult d;
    if (!media.decode(loc, d, why)) { std::printf("FAIL: %s\n", why.c_str()); return 1; }

    std::printf("record %u bytes, packets %llu ok %llu, avg %.0f kbps over decoded, %.0f kbps over xtbl Length\n", loc.size,
                (unsigned long long)d.audioPacketsTotal, (unsigned long long)d.audioPacketsDecodedOk,
                loc.size * 8.0 / ((double)d.totalSampleFrames / (double)d.sampleRate) / 1000.0, loc.size * 8.0 / (e.lengthMs / 1000.0) / 1000.0);
    std::printf("xtbl Length %u ms vs decoded %.0f ms\n", e.lengthMs, 1000.0 * (double)d.totalSampleFrames / (double)d.sampleRate);
    const size_t ch = (size_t)d.channels;
    const uint64_t start = std::min<uint64_t>(d.totalSampleFrames, (uint64_t)(seekMs * d.sampleRate / 1000.0));
    const uint64_t want = (uint64_t)(seconds * d.sampleRate);
    const uint64_t frames = std::min<uint64_t>(want, d.totalSampleFrames - start);
    std::vector<int16_t> pcm(d.pcm.begin() + (long long)(start * ch), d.pcm.begin() + (long long)((start + frames) * ch));
    int peak = 0; for (int16_t s : pcm) peak = std::max(peak, std::abs((int)s));
    if (!sr3audioruntime::writeWavPcm16(out, pcm, (uint32_t)d.sampleRate, (uint16_t)ch, err)) {
        std::printf("FAIL: wav write: %s\n", err.c_str()); return 1;
    }
    const double len = (double)frames / (double)d.sampleRate;
    std::printf("station %d song %s (full %.1f s, %ld Hz, %d ch) -> %s\n", station, e.cue.c_str(),
                (double)d.totalSampleFrames / (double)d.sampleRate, d.sampleRate, d.channels, out.c_str());
    std::printf("WAV: %.3f s (%llu frames), peak %d / 32767 (%.1f%% FS)\n", len, (unsigned long long)frames, peak,
                100.0 * peak / 32767.0);
    if (peak < 1000 || len < seconds - 0.01) { std::printf("FAIL: silent or short\n"); return 1; }
    if (play) {   // opt-in: audible check + tracked-voice stop through the real XAudio2 device
        sr3audioruntime::AudioOutputDevice dev; std::string derr; uint64_t id = 0;
        if (!dev.initialise(derr)) {
            std::printf("play: no audio device (%s)\n", derr.c_str());
        } else if (!dev.playTracked(pcm.data(), pcm.size(), d.sampleRate, d.channels, 0.5f, derr, &id)) {
            std::printf("FAIL: play: %s\n", derr.c_str());
            return 1;
        } else {
            Sleep(1500);
            const bool active = dev.isVoiceActive(id);
            dev.stopVoice(id);
            const bool after = dev.isVoiceActive(id);
            std::printf("play: voice active after 1.5 s=%d, after stopVoice=%d\n", (int)active, (int)after);
            if (!active || after) { std::printf("FAIL: tracked voice semantics\n"); return 1; }
        }
    }
    std::printf("ok\n");
    return 0;
}
