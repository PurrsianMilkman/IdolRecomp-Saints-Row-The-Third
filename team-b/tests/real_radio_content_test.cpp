// Real-data test: per-station cue tables (misc_tables.vpp_pc) -> RadioSystem content table, plus the spec's
// GENX queue vector (spec-audio-runtime.md Sec14.6.4 item 2 / 14.6.6) and cue->media resolution vectors
// (Sec14.6.4 item 3) when sounds_common.vpp_pc is present. Exits 77 (SKIP) when the game cache is absent.
#include <cstdio>
#include <filesystem>

#include "sr3radio/radio_content.h"
#include "sr3radio/radio_media.h"

namespace fs = std::filesystem;
using namespace sr3radio;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    const fs::path cache = CRREISH_GAME_CACHE_DIR;
    const fs::path misc = cache / "misc_tables.vpp_pc";
    if (!fs::exists(misc)) { std::printf("SKIP: %s not present\n", misc.string().c_str()); return 77; }

    XtblArchive arch; std::string err;
    CHECK(arch.open(misc.string(), err));
    RadioSystem sys; RadioLoadReport rep;
    const bool loaded = loadRadioContent(arch.provider(), sys, rep, err);
    if (!loaded) std::printf("load error: %s\n", err.c_str());
    CHECK(loaded);
    if (!loaded) return 1;
    for (const auto& w : rep.warnings) std::printf("warning: %s\n", w.c_str());

    std::printf("content entries=%zu station files=%zu song records=%zu\n", rep.contentEntries, rep.stationFilesLoaded,
                rep.songRecords);
    CHECK(rep.contentEntries == 915);          // spec 14.6.2 "Retail total: 915 entries"
    CHECK(rep.stationFilesLoaded == 11);       // Off..Credits = 11 rows; Mix Tape is code-built
    CHECK(sys.stations.size() == 12);
    CHECK(rep.songRecords <= 299);

    // Roster counts (spec 14.6.1 table): songs/ads/sweep/intro/outro per station.
    struct Row { int st; size_t songs, ads, sweep; size_t pat; int8_t ip, op; };
    const Row rows[] = {{3, 18, 81, 2, 10, 40, 40}, {4, 20, 81, 2, 10, 40, 40}, {5, 17, 81, 2, 10, 40, 40},
                        {6, 17, 81, 2, 10, 40, 40}, {7, 12, 11, 4, 7, 50, 50},  {8, 16, 81, 2, 1, 40, 40},
                        {9, 20, 81, 2, 10, 40, 40}, {10, 18, 81, 2, 10, 40, 40}};
    for (const Row& r : rows) {
        const Station& s = sys.stations[(size_t)r.st - 1];
        if (s.songs.idx.size() != r.songs || s.commercials.idx.size() != r.ads || s.sweepers.idx.size() != r.sweep ||
            s.pattern.size() != r.pat || s.introPct != r.ip || s.outroPct != r.op)
            std::printf("station %d mismatch: songs=%zu ads=%zu sweep=%zu pat=%zu i%%=%d o%%=%d\n", r.st, s.songs.idx.size(),
                        s.commercials.idx.size(), s.sweepers.idx.size(), s.pattern.size(), s.introPct, s.outroPct);
        CHECK(s.songs.idx.size() == r.songs);
        CHECK(s.commercials.idx.size() == r.ads);
        CHECK(s.sweepers.idx.size() == r.sweep);
        CHECK(s.pattern.size() == r.pat);
        CHECK(s.introPct == r.ip && s.outroPct == r.op);
    }
    CHECK(sys.stations[1].pattern.empty());                               // Off: empty playlist
    CHECK(sys.stations[1].wwiseId == 0x37798A64u);                        // FNV-1("OFF")
    CHECK(sys.stations[2].wwiseId == 0x48E832A3u);                        // GENX
    CHECK(sys.stations[3].wwiseId == 0xD11F2385u);                        // KRHYME
    CHECK(sys.stations[2].name == "LOGO_GENX" && sys.stations[2].hudImage == "ui_radio_89_gen_x");
    CHECK(sys.stations[10].newsStation && !sys.stations[10].songs.idx.empty());
    CHECK(sys.stations[10].pattern.empty());
    CHECK(sys.stations[11].pattern.size() == 13 && sys.stations[11].songs.idx.size() == 2);

    // GENX content indices (spec 14.6.6): sweepers 0,1; songs 173..190; file-order first song.
    const auto& e = sys.content.entries;
    CHECK(e.size() > 190);
    CHECK(e[0].cueId == 0xEE15F037u && e[0].lengthMs == 9962);           // GENX_SWEEPER_01
    CHECK(e[173].cueId == 0x34908852u && e[173].lengthMs == 210895 && e[173].type == ContentType::Song);
    CHECK(e[175].cueId == 0x5423413Bu);
    CHECK(e[125].cueId == 0x4B375387u && e[125].type == ContentType::Intro && e[125].lengthMs == 8271 && e[125].delayMs == 3518);
    CHECK(e[154].cueId == 0x10EC4CCCu && e[154].type == ContentType::Outro && e[154].delayMs == 297);
    CHECK(e[7].cueId == 0x1DEEBA84u && e[7].type == ContentType::Commercial && e[7].lengthMs == 65175);

    // The queue vector: ring seeded 5489, GENX built first, all used flags 0, position 0.
    sys.rng.reseed(5489);
    Station& genx = sys.stations[2];
    sys.buildQueue(genx);
    static const uint16_t expect[24] = {0, 175, 125, 178, 7, 190, 154, 179, 168, 0, 85, 188,
                                        127, 189, 174, 143, 45, 189, 164, 0, 186, 26, 187, 173};
    CHECK(genx.queue.size() == 512);
    int firstBad = -1;
    for (int i = 0; i < 24 && i < (int)genx.queue.size(); ++i)
        if (genx.queue[(size_t)i] != expect[i]) { firstBad = i; break; }
    if (firstBad >= 0) {
        std::printf("queue differs at %d: got", firstBad);
        for (int i = 0; i < 24 && i < (int)genx.queue.size(); ++i) std::printf(" %u", (unsigned)genx.queue[(size_t)i]);
        std::printf("\n");
    }
    CHECK(firstBad < 0);
    std::printf("GENX queue: %zu entries, %zu ring draws, saved pattern position %d\n", genx.queue.size(), sys.rng.draws(),
                genx.patternPos);
    CHECK(sys.rng.draws() == 946);
    CHECK(genx.patternPos == 2);

    // Cue -> media resolution vectors (need sounds_common.vpp_pc).
    const fs::path snd = cache / "sounds_common.vpp_pc";
    if (fs::exists(snd)) {
        RadioMedia media;
        CHECK(media.open(snd.string(), err));
        if (media.isOpen()) {
            struct V { uint32_t cue; ContentType t; uint32_t sound, media; };
            const V vs[] = {{0x34908852u, ContentType::Song, 0x1330703Bu, 0x1771F271u},
                            {0x7AF2299Cu, ContentType::Song, 0x203B816Fu, 0x3CE73F94u},
                            {0xEE15F037u, ContentType::Sweeper, 0x322402CFu, 0x1382C1E3u},
                            {0x8C71978Eu, ContentType::Commercial, 0x26C5C013u, 0x019B2805u}};
            for (const V& v : vs) {
                MediaLoc loc; std::string why;
                const bool ok = media.resolve(v.cue, v.t, loc, why);
                if (!ok) std::printf("resolve 0x%08X failed: %s\n", v.cue, why.c_str());
                CHECK(ok);
                if (ok) {
                    if (loc.soundId != v.sound || loc.mediaId != v.media)
                        std::printf("resolve 0x%08X -> sound 0x%08X media 0x%08X (want 0x%08X / 0x%08X)\n", v.cue, loc.soundId,
                                    loc.mediaId, v.sound, v.media);
                    CHECK(loc.soundId == v.sound);
                    CHECK(loc.mediaId == v.media);
                }
            }
            MediaLoc loc; std::string why;
            CHECK(media.resolve(0x34908852u, ContentType::Song, loc, why) && loc.size == 2711096);   // spec 14.6.4 item 3
            // Every song of every selectable station should resolve. Measured exceptions: 8 songs (one in each of
            // 8 stations; e.g. the data names
            // GENX_SLEIGH_BELLS___RIOT_RHYTHM whose media id 0x003DDEE0) is in none of the radio_*_media banks
            // of sounds_common.vpp_pc (soundboot.vpp_pc only holds 316-byte prefetch stubs), so it can never
            // play; tolerated explicitly rather than hidden.
            size_t bad = 0, total = 0;
            for (size_t si = 2; si < 10; ++si)
                for (uint16_t ci : sys.stations[si].songs.idx) {
                    ++total;
                    if (!media.resolve(e[ci].cueId, e[ci].type, loc, why)) {
                        ++bad;
                        std::printf("unresolved song %s (0x%08X): %s\n", e[ci].cue.c_str(), e[ci].cueId, why.c_str());
                        CHECK(why.find("not in any radio media bank") != std::string::npos);   // only "media absent" is tolerated
                    }
                }
            std::printf("songs resolved: %zu of %zu\n", total - bad, total);
            CHECK(bad <= 8);   // measured: exactly 8 of 138 (one per station, see above)
            std::printf("media index: %zu records\n", media.mediaRecordCount());
        }
    } else std::printf("note: sounds_common.vpp_pc absent, media vectors skipped\n");

    std::printf(g_fail ? "FAILED (%d)\n" : "ok\n", g_fail);
    return g_fail ? 1 : 0;
}
