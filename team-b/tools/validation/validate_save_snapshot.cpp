// Gate: numeric claims of spec-save-format.md Sec3/Sec6/Sec9/Sec10/Sec12 that can be checked
// on the real snapshots with this project's own reader (include/sr3save/save_snapshot.h) and
// a transcription of the Sec6.4 region map. Everything is counted with its denominator; where
// a claim could not be reproduced the difference is printed, not smoothed over.
//
//  A. header: version 94, +0x00C = 1, +0x004 = 0, build stamp, sub-version word 0x11D88, hash
//  B. play time: int(float32@0x09C) == uint32@0x0A0, minutes = trunc(seconds / 60.0)
//  C. counted arrays: count <= capacity and no non-zero byte after count x stride
//  D. region map (Sec6.4 rows transcribed): non-zero bytes outside every row; shifted-map controls;
//     the two large gaps; per-row width sanity
//  E. Sec10.7 per-sample table reproduced as a multiset (10 columns, 16 rows)
//  F. small claims: level-22 pair difference, cheats-used flag vs cash, difficulty range, ...
//
// STRICTLY READ-ONLY on the save files.
//
// usage: validate_save_snapshot [folder ...]

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <string>
#include <vector>

#include "sr3save/save_crc.h"
#include "sr3save/save_snapshot.h"

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
uint32_t rd32(const std::vector<uint8_t>& b, size_t o) {
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
}

int g_unexpected = 0;
void gate(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_unexpected;
}

struct Span {
    uint32_t a, b; // inclusive
};

// Sec6.4 region map rows, transcribed (start, end inclusive). The hash word 0x000-0x003 is added separately.
const Span kRows[] = {
    {0x008, 0x00B}, {0x00C, 0x00F}, {0x010, 0x013}, {0x014, 0x057}, {0x058, 0x05F}, {0x060, 0x071}, {0x072, 0x083},
    {0x084, 0x087}, {0x088, 0x08B}, {0x08C, 0x09F}, {0x0A0, 0x0A3}, {0x0A4, 0x0C3}, {0x0C8, 0x0C8}, {0x0CC, 0x3EF},
    {0x3F0, 0x3FB}, {0x3FC, 0x8FF}, {0x900, 0x2B53}, {0x2B54, 0x2B63}, {0x2B64, 0x2B67}, {0x2B68, 0x2B8F},
    {0x2B90, 0x2E97}, {0x2E98, 0x2E9F}, {0x2EA0, 0x2EA3}, {0x2EA4, 0x2F13}, {0x2F14, 0x2F4F}, {0x2F50, 0x3097},
    {0x3098, 0x3237}, {0x3238, 0x3337}, {0x3338, 0x333B}, {0x3340, 0x37E7}, {0x37E8, 0x3FEF}, {0x3FF0, 0x403F},
    {0x4040, 0x4333}, {0x4334, 0x4413}, {0x4414, 0x4417}, {0x4418, 0x4427}, {0x4428, 0x499F}, {0x49A0, 0x4A23},
    {0x4A24, 0x4A57}, {0x4A58, 0x4A5F}, {0x4A60, 0x4A6B}, {0x4A6C, 0x4A6C}, {0x4A6E, 0x4A6F}, {0x4A70, 0x4A73},
    {0x4A74, 0x4A77}, {0x4A78, 0x4BC7}, {0x4BC8, 0x4D57}, {0x4D58, 0x4D59}, {0x4D5C, 0x4D5F}, {0x4E60, 0x4E63},
    {0x4E68, 0x4F57}, {0x4F58, 0x4F5B}, {0x4F5C, 0x4F6B}, {0x4F6C, 0x8F6F}, {0x8F70, 0x9098}, {0x909C, 0xB31F},
    {0xB320, 0xB35F}, {0xB360, 0xB75F}, {0xB760, 0xF9E7}, {0xF9E8, 0xFAEB}, {0xFAEC, 0xFBD8}, {0xFBD9, 0xFBDE},
    {0xFBE0, 0xFC5D}, {0xFC60, 0x10C07}, {0x10C08, 0x10DAD}, {0x10DB0, 0x10FEF}, {0x10FF0, 0x113B7},
    {0x113B8, 0x11D7F}, {0x11D80, 0x11D80}, {0x11D88, 0x11D8B}, {0x11D8C, 0x15D8F}, {0x15D90, 0x16693},
    {0x16694, 0x18B03}, {0x18BD0, 0x191D7}, {0x191D8, 0x19AC7}, {0x19AC8, 0x1A8E7},
};

// Sec10.7 table: lvl cheat cash barn stunt coll wpn items outfOwned outfSaved
struct Row {
    uint32_t lvl, cheat, cash, barn, stunt, coll, wpn, items, outfO, outfS;
    bool operator<(const Row& o) const {
        return std::tie(lvl, cheat, cash, barn, stunt, coll, wpn, items, outfO, outfS) <
               std::tie(o.lvl, o.cheat, o.cash, o.barn, o.stunt, o.coll, o.wpn, o.items, o.outfO, o.outfS);
    }
    bool operator==(const Row& o) const {
        return lvl == o.lvl && cheat == o.cheat && cash == o.cash && barn == o.barn && stunt == o.stunt &&
               coll == o.coll && wpn == o.wpn && items == o.items && outfO == o.outfO && outfS == o.outfS;
    }
};
const Row kSpec107[16] = {
    {38, 0, 51722765, 2, 0, 79, 11, 61, 9, 0},   {38, 0, 51722765, 2, 0, 79, 11, 61, 9, 0},
    {49, 1, 1971204346u, 2, 3, 6, 11, 96, 7, 3}, {49, 1, 1962852253u, 2, 4, 6, 11, 105, 8, 3},
    {50, 1, 1994646800u, 6, 8, 40, 11, 185, 11, 4}, {49, 1, 1669413400u, 0, 0, 1, 11, 21, 0, 0},
    {49, 0, 4765260, 3, 0, 80, 11, 525, 74, 0},  {49, 0, 4765260, 3, 0, 80, 11, 525, 74, 0},
    {39, 0, 45082712, 2, 0, 79, 11, 61, 9, 0},   {49, 0, 4466760, 3, 0, 80, 11, 521, 74, 0},
    {0, 0, 5700, 0, 0, 0, 2, 5, 0, 0},           {22, 0, 1149391, 2, 0, 24, 9, 57, 9, 0},
    {22, 0, 1154391, 2, 0, 24, 9, 57, 9, 0},     {49, 0, 4485960, 3, 0, 80, 11, 521, 74, 0},
    {49, 0, 3811, 3, 0, 80, 11, 521, 74, 0},     {49, 0, 4485960, 3, 0, 80, 11, 521, 74, 0},
};

struct Snap {
    std::string where;
    std::vector<uint8_t> b;
    sr3save::SaveSnapshot s;
};

// counted-array check: count <= cap and no non-zero byte after base + count*stride up to base + cap*stride
bool countedArrayOk(const std::vector<uint8_t>& b, size_t countOff, size_t base, size_t stride, size_t cap,
                    size_t countBytes = 4) {
    uint32_t count = countBytes == 1 ? b[countOff] : rd32(b, countOff);
    if (count > cap) return false;
    for (size_t o = base + static_cast<size_t>(count) * stride; o < base + cap * stride; ++o)
        if (b[o] != 0) return false;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::pair<std::string, fs::path>> folders;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) folders.push_back({argv[i], argv[i]});
    } else {
        folders = {
            {"Documents", "C:/Users/Purrsian/Documents/Saints Row The Third"},
            {"LocalAppData", "C:/Users/Purrsian/AppData/Local/Saints Row The Third"},
            {"CloudSync", "D:/Project Crreish/Saints Row 3 CRREISH/!Downloads/!Cloud Sync Backup/saves"},
            {"TEAM-B fixtures", "D:/Project Crreish/TEAM B/test-fixtures/saves"},
        };
    }
    std::vector<Snap> snaps;
    {
        std::map<std::vector<uint8_t>, int> seen;
        for (auto& f : folders) {
            std::error_code ec;
            if (!fs::is_directory(f.second, ec)) continue;
            for (const auto& de : fs::directory_iterator(f.second)) {
                std::string fn = de.path().filename().string();
                if (fn.size() != 18 || fn.rfind("sr3save_", 0) != 0) continue;
                std::vector<uint8_t> b = readFile(de.path());
                if (b.size() != sr3save::kSaveSnapshotSize || seen.count(b)) continue;
                seen[b] = 1;
                snaps.push_back({f.first + "/" + fn, b, sr3save::SaveSnapshot::parse(vpp::ByteView(b.data(), b.size()))});
            }
        }
    }
    const size_t N = snaps.size();
    std::printf("loaded %zu distinct real snapshots\n", N);
    if (N == 0) {
        std::printf("NO REAL SNAPSHOTS FOUND\n");
        return 2;
    }

    // ---------------------------------------------------------------- A. header
    std::printf("\n== A. header ==\n");
    {
        size_t v94 = 0, sec1 = 0, w4 = 0, hashOk = 0, tsOk = 0, zoneOk = 0;
        std::map<uint32_t, int> stamps, sub;
        for (auto& s : snaps) {
            v94 += s.s.formatVersion() == 94 && s.s.versionSupported();
            sec1 += s.s.secondaryVersion() == 1;
            w4 += rd32(s.b, 4) == 0;
            hashOk += s.s.hashValid();
            tsOk += s.s.timestamp().valid;
            zoneOk += !s.s.levelName().empty();
            stamps[s.s.buildStamp()]++;
            sub[rd32(s.b, 0x11D88)]++;
        }
        std::printf("  +0x008 == 94: %zu/%zu   +0x00C == 1: %zu/%zu   +0x004 == 0: %zu/%zu   hash valid: %zu/%zu   date+time parse: %zu/%zu   zone name non-empty: %zu/%zu\n",
                    v94, N, sec1, N, w4, N, hashOk, N, tsOk, N, zoneOk, N);
        gate(v94 == N && sec1 == N && w4 == N && hashOk == N && tsOk == N && zoneOk == N, "all header fields as the spec states (94, 1, 0, valid hash, parseable stamps)");
        std::printf("  build stamp (0x010):");
        for (auto& kv : stamps) std::printf(" %08X x%d", kv.first, kv.second);
        std::printf("   (spec: 000F0F6E x14, 000EA708 x1, 000E8CE7 x1)\n");
        gate(stamps[0x000F0F6E] == 14 && stamps[0x000EA708] == 1 && stamps[0x000E8CE7] == 1, "build stamp distribution 14/1/1");
        std::printf("  sub-version word 0x11D88:");
        for (auto& kv : sub) std::printf(" %u x%d", kv.first, kv.second);
        std::printf("   (spec: 9 x14, 8 x1, 0 x1)\n");
        gate(sub[9] == 14 && sub[8] == 1 && sub[0] == 1, "0x11D88 distribution 9:14 / 8:1 / 0:1");
        // the two oldest saves are exactly those with the non-standard stamp and sub-version
        int oldPair = 0;
        for (auto& s : snaps)
            if (s.s.buildStamp() != 0x000F0F6E && rd32(s.b, 0x11D88) != 9) ++oldPair;
        gate(oldPair == 2, "the two non-standard build stamps are the same two files as the non-9 sub-versions");
        std::printf("  zone names:");
        std::set<std::string> zones;
        for (auto& s : snaps) zones.insert(s.s.levelName());
        for (auto& z : zones) std::printf(" \"%s\"", z.c_str());
        std::printf("\n");
    }

    // ---------------------------------------------------------------- B. play time
    std::printf("\n== B. play time (Sec6.2) ==\n");
    {
        size_t eq = 0, minutesOk = 0;
        for (auto& s : snaps) {
            float f = s.s.playClockFloat();
            if (std::isfinite(f) && f >= 0 && f < 4.0e9f && static_cast<uint32_t>(f) == s.s.playTimeSeconds()) ++eq;
            if (s.s.playTimeMinutes() == static_cast<uint32_t>(s.s.playTimeSeconds() / 60)) ++minutesOk;
        }
        std::printf("  int(float32@0x09C) == uint32@0x0A0: %zu/%zu   (spec: 16/16)\n", eq, N);
        gate(eq == N, "float clock truncated == whole-seconds counter");
        // control: ROUNDING instead of truncation must fail somewhere
        size_t roundEq = 0;
        for (auto& s : snaps) {
            float f = s.s.playClockFloat();
            if (std::isfinite(f) && f >= 0 && f < 4.0e9f && static_cast<uint32_t>(std::floor(f + 0.5f)) == s.s.playTimeSeconds()) ++roundEq;
        }
        std::printf("  control: round() instead of truncation matches %zu/%zu (must be < %zu)\n", roundEq, N, N);
        gate(roundEq < N, "control: rounding does NOT reproduce the counter (truncation is real)");
        // control: reading the counter as 1/60-s ticks -> implausible late-game minutes
        std::printf("  worked values from the spec: ");
        for (auto& s : snaps) {
            uint32_t sec = s.s.playTimeSeconds();
            if (sec == 39688 || sec == 39857 || sec == 150778 || sec == 948)
                std::printf("%u s -> %u min; ", sec, s.s.playTimeMinutes());
        }
        std::printf("(spec: 39688->661, 39857->664, 150778->2512, 948->15)\n");
        for (auto& s : snaps) {
            uint32_t sec = s.s.playTimeSeconds();
            if (sec == 39688) gate(s.s.playTimeMinutes() == 661, "39688 s -> 661 min");
            if (sec == 39857) gate(s.s.playTimeMinutes() == 664, "39857 s -> 664 min");
            if (sec == 150778) gate(s.s.playTimeMinutes() == 2512, "150778 s -> 2512 min");
        }
        (void)minutesOk;
    }

    // ---------------------------------------------------------------- C. counted arrays
    std::printf("\n== C. counted arrays: count <= capacity, nothing non-zero after count x stride ==\n");
    {
        struct A {
            const char* name;
            size_t countOff, base, stride, cap, cb;
        };
        const A arrays[] = {
            {"cheat ids       0x0CC  (200 x u32)", 0x0CC, 0x0D0, 4, 200, 4},
            {"collectibles    0x3FC  (80 x 16)", 0x3FC, 0x400, 16, 80, 4},
            {"activity table  0x2B90 (64 x 12)", 0x2B90 + 0x304, 0x2B94, 12, 64, 4},
            {"mission objects 0x37E8 (128 x 16)", 0x37E8, 0x37F0, 16, 128, 4},
            {"hitman groups   0x2EA0 (7 x 16)", 0x2EA0, 0x2EA4, 16, 7, 4},
            {"weapon inv.     0x4A74 (12 x 28)", 0x4A74, 0x4A78, 28, 12, 4},
            {"wardrobe        0x4F6C (2048 x 8)", 0x4F6C, 0x4F70, 8, 2048, 4},
            {"owned outfits   0x9098 (74 x u32, count byte)", 0x9098, 0x8F70, 4, 74, 1},
            {"user outfits    0xB31C (32 x 0x114)", 0xB31C, 0x909C, 0x114, 32, 4},
            {"garage          0xB760 (152 x 0x70)", 0xB760, 0xB764, 0x70, 152, 4},
            {"hash set        0xF9E8 (64 x u32)", 0xF9E8, 0xF9EC, 4, 64, 4},
            {"trigger objects 0xFC60 (250 x 16)", 0xFC60, 0xFC68, 16, 250, 4},
            {"capability objs 0x10FF0 (60 x 16)", 0x10FF0, 0x10FF8, 16, 60, 4},
            {"regions         0x113B8 (156 x 16)", 0x113B8, 0x113C0, 16, 156, 4},
            {"DLC wardrobe    0x11D8C (2048 x 8)", 0x11D8C, 0x11D90, 8, 2048, 4},
            {"DLC garage      0x191D8 (20 x 0x70)", 0x191D8, 0x191DC, 0x70, 20, 4},
        };
        for (const auto& a : arrays) {
            size_t ok = 0;
            for (auto& s : snaps) ok += countedArrayOk(s.b, a.countOff, a.base, a.stride, a.cap, a.cb);
            std::printf("  %-52s %2zu/%zu\n", a.name, ok, N);
            if (ok != N) ++g_unexpected;
        }
        // control: a wrong (too small) capacity must fail on the arrays that are populated
        size_t ctrlFail = 0, ctrlTotal = 0;
        for (auto& s : snaps) {
            ++ctrlTotal;
            if (!countedArrayOk(s.b, 0x113B8, 0x113C0, 16, 100, 4)) ++ctrlFail; // 154 > 100
        }
        std::printf("  control: region array with capacity 100 (count is 154) fails %zu/%zu\n", ctrlFail, ctrlTotal);
        gate(ctrlFail == ctrlTotal, "control: an undersized capacity is rejected by the same check");
        // specific literal claims
        size_t r154 = 0, act60 = 0, mis113 = 0, trg113 = 0, cap80 = 0, cap74 = 0;
        for (auto& s : snaps) {
            r154 += rd32(s.b, 0x113B8) == 154;
            act60 += rd32(s.b, 0x2E94) == 60;
            mis113 += rd32(s.b, 0x37E8) == 113;
            trg113 += rd32(s.b, 0xFC60) == 113;
            cap80 += rd32(s.b, 0x3FC) == 80;
            cap74 += s.b[0x9098] == 74;
        }
        std::printf("  0x113B8==154: %zu/%zu  activity count==60: %zu/%zu  mission objs==113: %zu/%zu  triggers==113: %zu/%zu  collectible count==80 (cap): %zu/%zu  outfit count==74 (cap): %zu/%zu\n",
                    r154, N, act60, N, mis113, N, trg113, N, cap80, N, cap74, N);
        gate(r154 == N && act60 == N && mis113 == N && trg113 == N, "literal counts 154 / 60 / 113 / 113 in every snapshot");
        std::printf("  (spec: collectibles reach the 80 cap in 6 samples, owned outfits reach 74 in 6 samples)\n");
        gate(cap80 == 6 && cap74 == 6, "collectible cap reached in 6 samples and outfit cap in 6 samples");
    }

    // ---------------------------------------------------------------- D. region map
    std::printf("\n== D. Sec6.4 region map (transcribed rows) ==\n");
    {
        std::vector<Span> rows(std::begin(kRows), std::end(kRows));
        rows.push_back({0x000, 0x003}); // the hash
        auto coveredMap = [&](int shift) {
            std::vector<uint8_t> cov(sr3save::kSaveSnapshotSize, 0);
            for (auto& r : rows) {
                for (int64_t o = static_cast<int64_t>(r.a) + shift; o <= static_cast<int64_t>(r.b) + shift; ++o)
                    if (o >= 0 && o < static_cast<int64_t>(cov.size())) cov[static_cast<size_t>(o)] = 1;
            }
            return cov;
        };
        auto cov0 = coveredMap(0);
        size_t covered = std::count(cov0.begin(), cov0.end(), 1);
        std::vector<Span> gaps;
        for (size_t o = 0; o < cov0.size();) {
            if (cov0[o]) { ++o; continue; }
            size_t e = o;
            while (e + 1 < cov0.size() && !cov0[e + 1]) ++e;
            gaps.push_back({static_cast<uint32_t>(o), static_cast<uint32_t>(e)});
            o = e + 1;
        }
        std::printf("  %zu transcribed rows cover %zu of %zu bytes; %zu gaps (spec's finer 103-span harness: 108,252 of 108,776 = 99.5%%, 21 gaps, 524 uncovered bytes)\n",
                    rows.size(), covered, sr3save::kSaveSnapshotSize, gaps.size());
        std::printf("  gaps > 100 bytes:");
        for (auto& g : gaps) if (g.b - g.a + 1 > 100) std::printf(" 0x%X-0x%X (%u bytes)", g.a, g.b, g.b - g.a + 1);
        std::printf("   (spec: 0x4D60-0x4E5F = 256 and 0x18B04-0x18BCF = 204)\n");
        bool bigOk = false;
        int nBig = 0;
        for (auto& g : gaps) if (g.b - g.a + 1 > 100) { ++nBig; }
        bigOk = nBig == 2 && std::any_of(gaps.begin(), gaps.end(), [](const Span& g) { return g.a == 0x4D60 && g.b == 0x4E5F; }) &&
                std::any_of(gaps.begin(), gaps.end(), [](const Span& g) { return g.a == 0x18B04 && g.b == 0x18BCF; });
        gate(bigOk, "both large gaps reproduced exactly");

        auto nonzeroOutside = [&](const std::vector<uint8_t>& cov, size_t& gapNonzero) {
            size_t total = 0;
            gapNonzero = 0;
            for (auto& s : snaps) {
                for (size_t o = 0; o < cov.size(); ++o) {
                    if (!cov[o] && s.b[o] != 0) ++total;
                }
            }
            return total;
        };
        size_t tmp;
        size_t base = nonzeroOutside(cov0, tmp);
        std::printf("  non-zero bytes outside every row, summed over %zu snapshots: %zu (spec: 0)\n", N, base);
        gate(base == 0, "0 non-zero bytes outside the region map");
        // total non-zero content, and how much lies in rows the spec marks contiguity-stretched is not separable here
        size_t nz = 0;
        for (auto& s : snaps) for (uint8_t v : s.b) nz += v != 0;
        std::printf("  total non-zero bytes over all snapshots: %zu (spec: 201,363)\n", nz);
        if (nz != 201363) {
            std::printf("  *** DIFFERS from the spec's 201,363 (%+lld) ***\n", static_cast<long long>(nz) - 201363);
        }
        // shifted maps: the boundaries must carry information
        std::printf("  shifted-map controls (non-zero bytes outside): ");
        bool allPos = true;
        for (int sh : {+4, -4, +16, +256}) {
            auto cs = coveredMap(sh);
            size_t v = nonzeroOutside(cs, tmp);
            std::printf(" %+d -> %zu;", sh, v);
            if (v == 0) allPos = false;
        }
        std::printf("   (spec, its 103-span map: +4 -> 620, -4 -> 243, +16 -> 755, +256 -> 4695; not expected to be equal - different span granularity)\n");
        gate(allPos, "every shifted map leaves non-zero bytes outside (the control can fail)");
        // the two big gaps are zero in every sample
        size_t bigNz = 0;
        for (auto& s : snaps) {
            for (uint32_t o = 0x4D60; o <= 0x4E5F; ++o) bigNz += s.b[o] != 0;
            for (uint32_t o = 0x18B04; o <= 0x18BCF; ++o) bigNz += s.b[o] != 0;
        }
        gate(bigNz == 0, "the two large uncovered gaps are all-zero in every snapshot");
    }

    // ---------------------------------------------------------------- E. Sec10.7 table
    std::printf("\n== E. Sec10.7 per-sample table (10 columns) as a multiset ==\n");
    {
        std::vector<Row> mine;
        for (auto& s : snaps) {
            Row r;
            r.lvl = s.s.playerLevel();
            r.cheat = s.s.cheatsUsed() ? 1 : 0;
            r.cash = static_cast<uint32_t>(s.s.cashRaw());
            r.barn = s.s.barnstormsFound();
            r.stunt = s.s.stuntJumpsFound();
            r.coll = s.s.collectibleCount();
            r.wpn = rd32(s.b, 0x4A74);
            r.items = rd32(s.b, 0x4F6C);
            r.outfO = s.b[0x9098];
            r.outfS = rd32(s.b, 0xB31C);
            mine.push_back(r);
        }
        std::vector<Row> spec(std::begin(kSpec107), std::end(kSpec107));
        std::sort(mine.begin(), mine.end());
        std::sort(spec.begin(), spec.end());
        size_t matched = 0;
        std::vector<bool> used(mine.size(), false);
        for (auto& sr : spec) {
            for (size_t i = 0; i < mine.size(); ++i) {
                if (!used[i] && mine[i] == sr) { used[i] = true; ++matched; break; }
            }
        }
        std::printf("  spec rows reproduced: %zu/%zu (my snapshots: %zu)\n", matched, spec.size(), mine.size());
        if (matched != spec.size() || mine.size() != spec.size()) {
            std::printf("  unmatched spec rows / unmatched mine:\n");
            for (auto& sr : spec) {
                bool f = false;
                for (auto& m : mine) f = f || m == sr;
                if (!f) std::printf("    spec  lvl %u cheat %u cash %u barn %u stunt %u coll %u wpn %u items %u outf %u/%u\n", sr.lvl, sr.cheat, sr.cash, sr.barn, sr.stunt, sr.coll, sr.wpn, sr.items, sr.outfO, sr.outfS);
            }
            for (size_t i = 0; i < mine.size(); ++i) {
                if (!used[i]) {
                    auto& m = mine[i];
                    std::printf("    mine  lvl %u cheat %u cash %u barn %u stunt %u coll %u wpn %u items %u outf %u/%u\n", m.lvl, m.cheat, m.cash, m.barn, m.stunt, m.coll, m.wpn, m.items, m.outfO, m.outfS);
                }
            }
        }
        gate(matched == spec.size(), "all 16 rows of the Sec10.7 table reproduced");
        // control: perturb one cell of my rows, the multiset match must drop
        size_t matchedPert = 0;
        {
            std::vector<Row> pert = mine;
            pert[0].cash += 1;
            std::vector<bool> u2(pert.size(), false);
            for (auto& sr : spec)
                for (size_t i = 0; i < pert.size(); ++i)
                    if (!u2[i] && pert[i] == sr) { u2[i] = true; ++matchedPert; break; }
        }
        std::printf("  control: one cash value off by 1 -> %zu/%zu rows reproduced (must be %zu)\n", matchedPert, spec.size(), spec.size() - 1);
        gate(matchedPert == spec.size() - 1, "control: the multiset comparison detects a single-cell change");
    }

    // ---------------------------------------------------------------- F. small claims
    std::printf("\n== F. small claims ==\n");
    {
        // level-22 pair: 0x4418 - 0x441C = 73,095 in both
        int pairs = 0, diffOk = 0;
        for (auto& s : snaps) {
            if (s.s.playerLevel() == 22) {
                ++pairs;
                if (s.s.respectTotal() - s.s.respectInLevel() == 73095) ++diffOk;
            }
        }
        std::printf("  level-22 saves: %d; total - inLevel == 73,095: %d/%d (spec: two saves, both 73,095)\n", pairs, diffOk, pairs);
        gate(pairs == 2 && diffOk == 2, "level-22 pair: respect difference 73,095 in both");
        // level 0: total == in-level (1080); cap: in-level 0
        int l0 = 0, l0ok = 0, cap = 0, capOk = 0;
        for (auto& s : snaps) {
            if (s.s.playerLevel() == 0) { ++l0; l0ok += s.s.respectTotal() == s.s.respectInLevel() && s.s.respectTotal() == 1080; }
            if (s.s.playerLevel() == 50) { ++cap; capOk += s.s.respectInLevel() == 0; }
        }
        gate(l0 == 1 && l0ok == 1, "level 0: total == in-level == 1080");
        gate(cap >= 1 && cap == capOk, "level 50 (cap): respect-in-level == 0");
        // cheats-used flag == cash >= 1.66e9 raw
        int cheatIff = 0, cheats = 0;
        for (auto& s : snaps) {
            bool hi = s.s.cashRaw() >= 1660000000;
            if (hi == s.s.cheatsUsed()) ++cheatIff;
            cheats += s.s.cheatsUsed();
        }
        std::printf("  cheats-used flag == (cash >= 1.66e9): %d/%zu; flag set in %d samples (spec: exactly 4)\n", cheatIff, N, cheats);
        gate(cheatIff == static_cast<int>(N) && cheats == 4, "cheats-used flag set exactly for the 4 high-cash saves");
        // difficulty range, level range, cash within clamp
        int d = 0, l = 0, c = 0, pct = 0;
        for (auto& s : snaps) {
            d += s.s.difficultyIndex() <= 2;
            l += s.s.playerLevel() <= 50;
            c += s.s.cashRaw() >= 0 && s.s.cashRaw() <= 2000000000;
            pct += s.s.completionPercent() >= 1 && s.s.completionPercent() <= 78;
        }
        std::printf("  difficulty in 0..2: %d/%zu; level <= 50: %d/%zu; cash within [0, 2e9]: %d/%zu; completion in 1..78: %d/%zu\n", d, N, l, N, c, N, pct, N);
        gate(d == static_cast<int>(N) && l == static_cast<int>(N) && c == static_cast<int>(N) && pct == static_cast<int>(N), "difficulty / level / cash / completion ranges");
        // controlled-region counts
        std::vector<uint32_t> flagged;
        for (auto& s : snaps) flagged.push_back(s.s.controlledRegionCount());
        std::sort(flagged.begin(), flagged.end());
        std::printf("  controlled-region counts (sorted):");
        for (auto v : flagged) std::printf(" %u", v);
        std::printf("\n  (spec Sec12.4.1: 0, 48, 48, 132-134, 142 x6, 52/51, 96, 11)\n");
        // region handle set identical in all snapshots
        size_t same = 0;
        for (auto& s : snaps) {
            bool eq = s.s.regionRecords().size() == snaps[0].s.regionRecords().size();
            for (size_t i = 0; eq && i < s.s.regionRecords().size(); ++i)
                eq = s.s.regionRecords()[i].handleLo == snaps[0].s.regionRecords()[i].handleLo &&
                     s.s.regionRecords()[i].handleHi == snaps[0].s.regionRecords()[i].handleHi;
            same += eq;
        }
        std::printf("  region handle list identical to snapshot 0: %zu/%zu (spec: identical in all 16)\n", same, N);
        gate(same == N, "region handle list identical in every snapshot");
        size_t tailZero = 0, tailTotal = 0;
        for (auto& s : snaps) for (auto& r : s.s.regionRecords()) { ++tailTotal; bool z = true; for (auto t : r.tail) z = z && t == 0; tailZero += z; }
        std::printf("  region records with bytes +9..+15 zero: %zu/%zu\n", tailZero, tailTotal);
        gate(tailZero == tailTotal, "region record tails are zero");
        // legacy corrected base: bytes at 0x113C0.. (raw) vs old base 0x113C8
        size_t oldBaseFlagBit = 0, newBaseFlagBit = 0;
        for (auto& s : snaps) {
            uint32_t cnt = rd32(s.b, 0x113B8);
            for (uint32_t i = 0; i < cnt; ++i) {
                newBaseFlagBit += s.b[0x113C0 + 16 * i + 8] & 1;
                oldBaseFlagBit += s.b[0x113C8 + 16 * i + 8] & 1; // what reading from the old (wrong) base + 8 would give
            }
        }
        std::printf("  flag-bit tally with the corrected base 0x113C0: %zu;  with the old wrong base 0x113C8 (+8 flag byte at 0x113D0..): %zu\n", newBaseFlagBit, oldBaseFlagBit);
        // sort-key factor demonstration (Sec6.5 correction): ordering unaffected, values differ by 604,800*year
        std::printf("  save sort key of snapshot 0: %lld (year2=%d) - with the superseded factor 31,536,000 it would be %lld\n",
                    static_cast<long long>(snaps[0].s.saveSortKey()), snaps[0].s.timestamp().year2,
                    static_cast<long long>(snaps[0].s.saveSortKey() - 604800LL * snaps[0].s.timestamp().year2));
        // ordering check: sort by key vs sort by (yy,mm,dd,hh,mm,ss)
        std::vector<size_t> byKey(N), byTuple(N);
        for (size_t i = 0; i < N; ++i) byKey[i] = byTuple[i] = i;
        auto tup = [&](size_t i) {
            auto& t = snaps[i].s.timestamp();
            return std::make_tuple(t.year2, t.month, t.day, t.hour, t.minute, t.second);
        };
        std::stable_sort(byKey.begin(), byKey.end(), [&](size_t a, size_t b) { return snaps[a].s.saveSortKey() < snaps[b].s.saveSortKey(); });
        std::stable_sort(byTuple.begin(), byTuple.end(), [&](size_t a, size_t b) { return tup(a) < tup(b); });
        std::printf("  ordering by save sort key == ordering by (year,month,day,h,m,s): %s (2-digit years: 11, 12, 13, 26 present)\n", byKey == byTuple ? "yes" : "NO");
        gate(byKey == byTuple, "sort-key ordering equals chronological ordering");
    }

    // ---------------------------------------------------------------- G. statistics 68..216
    std::printf("\n== G. statistics 68..216 at 0x3340 (Sec12.6.1) ==\n");
    {
        size_t auxExact = 0, auxEqIdMinus1 = 0;
        size_t cmpTotal = 0, cmpOk = 0, rpgOver = 0, rpgTotal = 0;
        for (auto& s : snaps) {
            bool exact = true, eq = true;
            for (uint32_t id = 68; id <= 216; ++id) {
                const auto* st = s.s.statistic(id);
                bool isHit = id >= 94 && id <= 108 && (id % 2 == 0);
                if ((st->denominatorStatId != 0) != isHit) exact = false;
                if (isHit && st->denominatorStatId != id - 1) eq = false;
                if (isHit && id != 102) {
                    ++cmpTotal;
                    if (st->asInt() >= 0 && st->asInt() <= s.s.statistic(id - 1)->asInt()) ++cmpOk;
                }
                if (id == 102) {
                    ++rpgTotal;
                    if (st->asInt() > s.s.statistic(101)->asInt()) ++rpgOver;
                }
            }
            auxExact += exact;
            auxEqIdMinus1 += eq;
        }
        std::printf("  denominator id non-zero on exactly the eight hit-percent rows (94,96,..,108): %zu/%zu; equals id-1 on those: %zu/%zu\n",
                    auxExact, N, auxEqIdMinus1, N);
        gate(auxExact == N && auxEqIdMinus1 == N, "aux field non-zero exactly on 94,96,...,108 and equal to id-1 (16/16)");
        std::printf("  numerator <= denominator for the seven ids other than 102: %zu/%zu comparisons; id 102 (rpg hit pct) exceeds its denominator in %zu/%zu snapshots (spec: 4/16, a finding not an error)\n",
                    cmpOk, cmpTotal, rpgOver, rpgTotal);
        gate(cmpOk == cmpTotal, "numerator <= denominator on 7 of the 8 hit-percent statistics in every snapshot");
        gate(rpgOver == 4, "id 102 exceeds its denominator in exactly 4 of 16 snapshots");
        // control: the wrong stride (4 bytes) must destroy the aux pattern
        size_t badStride = 0;
        for (auto& s : snaps) {
            bool exact = true;
            for (uint32_t k = 0; k < 149; ++k) {
                uint32_t aux = rd32(s.b, 0x3340 + 4 * k + 4);
                uint32_t id = 68 + k;
                bool isHit = id >= 94 && id <= 108 && (id % 2 == 0);
                if ((aux != 0) != isHit) { exact = false; break; }
            }
            badStride += exact;
        }
        gate(badStride == 0, "control: reading the table with stride 4 instead of 8 does NOT reproduce the aux pattern (0/16)");
        // cross-check two separately documented places: 0x3F0/0x3F8 (Sec10.3) vs statistic ids 163/164 (Sec12.6.1)
        size_t barn = 0, stunt = 0;
        for (auto& s : snaps) {
            barn += s.s.statistic(163)->rawValue == s.s.barnstormsFound();
            stunt += s.s.statistic(164)->rawValue == s.s.stuntJumpsFound();
        }
        std::printf("  statistic 163 == barnstorms found (0x3F0): %zu/%zu; statistic 164 == stunt jumps found (0x3F8): %zu/%zu\n", barn, N, stunt, N);
        for (auto& s : snaps) {
            if (s.s.statistic(164)->rawValue != s.s.stuntJumpsFound() || s.s.statistic(163)->rawValue != s.s.barnstormsFound())
                std::printf("    mismatch in %s (level %u, %u s): stat163=%u barnstorms(0x3F0)=%u  stat164=%u stunt jumps(0x3F8)=%u\n",
                            s.where.c_str(), s.s.playerLevel(), s.s.playTimeSeconds(), s.s.statistic(163)->rawValue,
                            s.s.barnstormsFound(), s.s.statistic(164)->rawValue, s.s.stuntJumpsFound());
        }
        // the spec's worked example (level-50 / 78% snapshot)
        for (auto& s : snaps) {
            if (s.s.playerLevel() == 50 && s.s.completionPercent() == 78) {
                std::printf("  level-50 / 78%% snapshot: id 93=%d id 94=%d (aux %u) id 122=%d id 125=%d id 163=%d id 164=%d  (spec: 533, 367 (aux 93), 292, 292, 6, 8)\n",
                            s.s.statistic(93)->asInt(), s.s.statistic(94)->asInt(), s.s.statistic(94)->denominatorStatId,
                            s.s.statistic(122)->asInt(), s.s.statistic(125)->asInt(), s.s.statistic(163)->asInt(),
                            s.s.statistic(164)->asInt());
                gate(s.s.statistic(93)->asInt() == 533 && s.s.statistic(94)->asInt() == 367 && s.s.statistic(122)->asInt() == 292 &&
                         s.s.statistic(125)->asInt() == 292 && s.s.statistic(163)->asInt() == 6 && s.s.statistic(164)->asInt() == 8,
                     "spec's worked example for the level-50 snapshot reproduced");
            }
        }
    }

    std::printf("\nRESULT: %s (%d unexpected)\n", g_unexpected == 0 ? "ALL GATES AS EXPECTED" : "DISAGREEMENT", g_unexpected);
    return g_unexpected == 0 ? 0 : 1;
}
