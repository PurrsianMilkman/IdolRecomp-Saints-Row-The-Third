// Gate: sr3def_profile (spec-save-format.md Sec7) parsed by this project's own
// reader (include/sr3save/save_profile.h, written from the spec text) over every
// real profile found on this machine.
//
//   spec claims (Sec7.8): size 16,968; the length-prefixed stat stream in chunk 0
//   consumes exactly used0 = 891 bytes = 179 records, chunk 1 is empty (0 of 0);
//   the 179 equals the number of serialisable ids (217 - 38); every record is
//   4 bytes except id 45 which is empty; table A/B record i has index i
//   (164/164, 34/34); tables end exactly at 0x4248.
//
// A pass here is only evidence if the same reader FAILS on inputs that are
// wrong: this harness therefore also runs mutation controls (fixed 5-byte
// stride, a flipped length byte, used0 +-1, wrong framing, truncation, an
// index swap in table A) and prints what each one does.
//
// The stat payloads are then decoded with NO free parameters (record k = the
// k-th serialisable id) and checked against the semantic facts the spec quotes
// for one file (time played = time played female, shots hit <= shots fired ...).
//
// STRICTLY READ-ONLY on the profile files.
//
// usage: validate_profile [file ...]   (default: the known real profile locations)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3save/save_profile.h"

namespace fs = std::filesystem;
using sr3save::ByteView;

namespace {

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

int g_unexpected = 0;
int g_specDiff = 0;
void specNote(bool cond, const char* what) {
    std::printf("    [%s] %s\n", cond ? "spec" : "SPEC-DIFF", what);
    if (!cond) ++g_specDiff;
}
void expect(bool cond, const char* what) {
    std::printf("    [%s] %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++g_unexpected;
}

// Fixed 5-byte stride hypothesis (the struck-through Sec4 reading):
// [u8 tag == 4][4 bytes]. Returns the number of records it can follow from
// offset 0 and the offset where it first fails.
size_t fixedStrideRecords(const std::vector<uint8_t>& b, size_t& failOffset) {
    size_t n = 0, pos = 0;
    while (pos + 5 <= 891 + 0x200 && b[pos] == 4) {
        ++n;
        pos += 5;
    }
    failOffset = pos;
    return n;
}

// Wrong framing hypothesis: [u8 L][L+1 payload bytes] (off by one).
size_t offByOneRecords(const std::vector<uint8_t>& b, uint32_t used, size_t& consumed) {
    size_t n = 0, pos = 0;
    while (pos < used) {
        size_t step = 1 + static_cast<size_t>(b[pos]) + 1;
        if (pos + step > used) break;
        pos += step;
        ++n;
    }
    consumed = pos;
    return n;
}

// The full stat gate: exact consumption in both chunks AND record count == the 179
// serialisable ids AND every record length matches its stat's serialiser.
// Exact consumption alone is weak: a stray zero byte parses as a zero-length
// record and a phase-shifted walk often re-synchronises on the many payload
// bytes that are 0x00 - the mutation sweep below measures exactly that.
bool fullGate(const sr3save::SaveProfile& q) {
    return q.chunk0Walk().exact && q.chunk1Walk().exact && q.recordCountMatchesIds() && q.statsConsistent();
}

// Names the spec itself quotes (Sec7.4, Sec12.6.1); for display only.
const char* nameOf(uint32_t id) {
    switch (id) {
    case 0: return "time played";
    case 1: return "highest rank achieved";
    case 4: return "hospital bills";
    case 42: return "shot hit pct (numerator)";
    case 44: return "shots fired";
    case 45: return "unique vehicles owned (empty record)";
    case 56: return "time played male";
    case 57: return "time played female";
    default: return "";
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::pair<std::string, fs::path>> files;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) files.push_back({argv[i], argv[i]});
    } else {
        files = {
            {"Documents", "C:/Users/Purrsian/Documents/Saints Row The Third/sr3def_profile"},
            {"LocalAppData", "C:/Users/Purrsian/AppData/Local/Saints Row The Third/sr3def_profile"},
            {"CloudSyncBackup(CRREISH)",
             "D:/Project Crreish/Saints Row 3 CRREISH/!Downloads/!Cloud Sync Backup/saves/sr3def_profile"},
            {"CloudSyncBackup(RTXREMIX)",
             "D:/SR3RTXREMIXCOMP/Saints Row 3/!Downloads/!Cloud Sync Backup/saves/sr3def_profile"},
        };
    }

    // Collect distinct contents.
    struct P {
        std::string where;
        std::vector<uint8_t> bytes;
        std::vector<std::string> also;
    };
    std::vector<P> profiles;
    std::map<std::vector<uint8_t>, size_t> idx;
    int found = 0;
    for (const auto& f : files) {
        std::error_code ec;
        if (!fs::is_regular_file(f.second, ec)) {
            std::printf("not found (skipped): %s\n", f.second.string().c_str());
            continue;
        }
        ++found;
        std::vector<uint8_t> b = readFile(f.second);
        auto it = idx.find(b);
        if (it == idx.end()) {
            idx[b] = profiles.size();
            profiles.push_back({f.first, b, {}});
        } else {
            profiles[it->second].also.push_back(f.first);
        }
    }
    std::printf("found %d profile file(s) -> %zu distinct\n", found, profiles.size());
    if (profiles.empty()) {
        std::printf("NO REAL PROFILE FOUND\n");
        return 2;
    }

    // Spec-derived constants, checked before anything else.
    {
        auto ids = sr3save::profileSerialisedStatIds();
        std::printf("serialisable ids from the spec's 38-id omission list: %zu (spec: 179 = 217 - 38)\n", ids.size());
        expect(ids.size() == 179, "179 serialisable ids");
        size_t omitted = 0;
        for (uint32_t i = 0; i < 217; ++i) omitted += sr3save::profileStatHasRecord(i) ? 0 : 1;
        expect(omitted == 38, "38 omitted ids");
        // position of id 45 among serialisable ids: record index 20 (0-based) -> offset 0x64 if the first 20 are 5 bytes
        size_t pos45 = 0;
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == 45) pos45 = i;
        expect(pos45 == 20, "id 45 is the 21st serialisable id (record index 20)");
    }

    int passFiles = 0;
    for (const auto& p : profiles) {
        std::printf("\n=== profile from %s%s ===\n", p.where.c_str(), p.also.empty() ? "" : " (+ identical copies elsewhere)");
        ByteView bv(p.bytes.data(), p.bytes.size());
        sr3save::SaveProfile prof = sr3save::SaveProfile::parse(bv);
        const auto& h = prof.header();
        int before = g_unexpected;

        std::printf("  size=%zu used0=%u (0x%X) used1=%u settingsSize=0x%X valid=%u dirty=%u rebuild=%u tblVer=%u A=%u B=%u ts=%llu selfptr(ignored)=0x%08X\n",
                    p.bytes.size(), h.used0, h.used0, h.used1, h.settingsBlockSize, h.profileValid, h.dirty,
                    h.rebuildSettings, h.tableVersion, h.tableACount, h.tableBCount,
                    static_cast<unsigned long long>(h.timeStamp), h.settingsPointerRaw);
        expect(p.bytes.size() == 16968, "size == 16,968");
        expect(h.settingsBlockSize == 0x400 && h.profileValid == 1 && h.tableVersion == 5 && h.tableACount == 0xA4 &&
                   h.tableBCount == 0x22,
               "header: settings size 0x400, valid 1, table version 5, counts 164/34");
        expect(prof.settingsMagicOk() && prof.optionsApplied(), "settings magic 0x10 and options version 5");
        expect(prof.tablesApplied(), "tables would be applied");
        expect(prof.reservedRegionAllZero(), "reserved 0x2400..0x2FFF all zero");

        std::printf("  chunk0: %zu records consume %zu of %u bytes; chunk1: %zu records consume %zu of %u bytes\n",
                    prof.chunk0Walk().spans.size(), prof.chunk0Walk().consumed, h.used0, prof.chunk1Walk().spans.size(),
                    prof.chunk1Walk().consumed, h.used1);
        expect(prof.chunk0Walk().exact, "chunk 0 records consume exactly used0");
        expect(prof.chunk1Walk().exact, "chunk 1 records consume exactly used1");
        expect(h.used0 == 891 && prof.chunk0Walk().spans.size() == 179, "used0 == 891 and 179 records");
        expect(prof.recordCountMatchesIds(), "record count == serialisable ids (179 == 179)");
        expect(prof.statsConsistent(), "every record length matches its stat's serialiser");
        expect(fullGate(prof), "FULL stat gate (exact consumption + 179 count + per-id lengths)");

        std::map<int, int> lenHist;
        for (const auto& r : prof.statRecords()) lenHist[r.length]++;
        std::printf("  record length histogram:");
        for (auto& kv : lenHist) std::printf(" {%d: %d}", kv.first, kv.second);
        std::printf("\n");
        expect(lenHist.size() == 2 && lenHist[4] == 178 && lenHist[0] == 1, "lengths are {4: 178, 0: 1}");
        const auto* r45 = prof.findStat(45);
        expect(r45 && r45->length == 0 && r45->fileOffset == 0x64, "the one zero-length record is id 45 at file offset 0x64");

        std::printf("  table A: index==position %u/164, loader ranges ok %u/164; table B: index==position %u/34, ranges ok %u/34\n",
                    prof.tableAIndexMatches(), prof.tableALoaderAccepts(), prof.tableBIndexMatches(), prof.tableBRangesOk());
        expect(prof.tableAIndexMatches() == 164 && prof.tableALoaderAccepts() == 164, "table A 164/164 and 164/164");
        expect(prof.tableBIndexMatches() == 34 && prof.tableBRangesOk() == 34, "table B 34/34 and 34/34");
        // Tiling: tables end exactly at the file end.
        expect(sr3save::kProfileTableBOffset + sr3save::kProfileTableBCount * sr3save::kProfileTableBRecordSize == 0x4248 &&
                   sr3save::kProfileTableAOffset + sr3save::kProfileTableACount * sr3save::kProfileTableARecordSize ==
                       sr3save::kProfileTableBOffset &&
                   sr3save::kProfileHeaderOffset + sr3save::kProfileHeaderSize == sr3save::kProfileTableAOffset,
               "header/tableA/tableB tile up to 0x4248 exactly");

        // ---- semantic check, no free parameters -------------------------------
        std::printf("  -- decoded stat values (record k = k-th serialisable id) --\n");
        auto f = [&](uint32_t id) { const auto* r = prof.findStat(id); return r ? r->asFloat() : NAN; };
        auto i32 = [&](uint32_t id) { const auto* r = prof.findStat(id); return r ? r->asInt() : -1; };
        std::printf("  id 0 time played=%.1f  id 56=%.1f  id 57=%.1f  id 1 rank=%d  id 4 hospital=%.1f  id 44 shots fired=%d  id 42 hit numerator=%d\n",
                    f(0), f(56), f(57), i32(1), f(4), i32(44), i32(42));
        {
            float t0 = f(0), t56 = f(56), t57 = f(57);
            bool plausible = std::isfinite(t0) && t0 >= 0 && t0 < 3.0e7f;
            expect(plausible, "id 0 decodes as a finite non-negative float < 3e7 s");
            // "time played" is the sum of the male and female times (one of which is 0 in the spec's file)
            expect(std::fabs(t0 - (t56 + t57)) <= 1e-3f * std::fmax(1.0f, t0), "id 0 == id 56 + id 57 (float, 0.1% tol)");
            expect(i32(42) >= 0 && i32(44) >= 0 && i32(42) <= i32(44), "shot-hit numerator (42) <= shots fired (44)");
            expect(i32(1) >= 0 && i32(1) <= 1000, "id 1 (highest rank achieved) is a small non-negative integer");
        }
        // ids 13..17 are floats (Sec7.4)
        {
            bool ok = true;
            for (uint32_t id = 13; id <= 17; ++id) {
                float v = f(id);
                ok = ok && std::isfinite(v) && v >= 0.0f && v < 3.0e7f;
            }
            expect(ok, "ids 13..17 (time at max notoriety) decode as finite non-negative floats");
        }

        std::printf("  -- options / settings block --\n");
        const auto& o = prof.options();
        std::printf("  flagsA=0x%02X (invertY=%d invertRot=%d vib=%d crouch=%d) sens H/V=%.3f/%.3f gamma=%.3f bright=%.3f vsync(game)=%u cutsceneByte=0x%02X mouseSens=%.4f/%.4f\n",
                    o.flagsA, o.invertLookY(), o.invertRotation(), o.vibration(), o.crouchSetting(), o.lookSensitivityH,
                    o.lookSensitivityV, o.gamma, o.brightness, o.gameplayVsync, o.cutsceneVsyncByte, o.mouseSensitivityX,
                    o.mouseSensitivityY);
        std::printf("  audio vol:");
        for (float v : o.audioVolume) std::printf(" %.2f", v);
        std::printf("  session(raw)=%u friendlyFire=%u\n", prof.sessionSettingRaw(), prof.friendlyFire());
        {
            bool inRange = o.gamma >= 0 && o.gamma <= 1 && o.lookSensitivityH >= 0 && o.lookSensitivityH <= 2 &&
                           o.lookSensitivityV >= 0 && o.lookSensitivityV <= 2 && o.brightness >= 0 && o.brightness <= 2;
            for (float v : o.audioVolume) inRange = inRange && v >= 0 && v <= 2;
            expect(inRange, "float options are within [0,2] (gamma within [0,1])");
            expect((o.flagsA & 0xC0) == 0, "flagsA uses only the named low bits (bits 6,7 clear)");
        }
        {
            // Values the spec quotes for the three real files (Sec7.5 "Real values" column). A difference is a
            // SPEC-vs-BYTES disagreement, reported separately: it is not a reader failure.
            uint32_t rawX, rawY;
            std::memcpy(&rawX, &o.mouseSensitivityX, 4);
            std::memcpy(&rawY, &o.mouseSensitivityY, 4);
            std::printf("  unlabelled/other options: +0x08=%d radio=%d +0x48=%d mouseFlags=0x%02X +0x58=%d +0x5C=%d vehicleFlags=0x%02X mouseSens bits=0x%08X/0x%08X\n",
                        o.item08Open, o.radio, o.audioItem7Open, o.flagsMouse, o.mouseItem5Open, o.item5COpen,
                        o.flagsVehicle, rawX, rawY);
            specNote(o.item08Open == 3, "options +0x08 == 3 (spec Sec7.5)");
            specNote(o.radio == 0 && o.audioItem7Open == 1, "radio == 0 and audio item 7 == 1 (spec Sec7.5)");
            specNote(o.flagsMouse == 1 && o.mouseItem5Open == 2 && o.item5COpen == 1, "mouse flags 1, +0x58 == 2, +0x5C == 1 (spec Sec7.5)");
            specNote(std::fabs(o.mouseSensitivityX - 0.2397f) < 1e-5f && std::fabs(o.mouseSensitivityY - 0.2397f) < 1e-5f,
                     "mouse sensitivity X/Y == 0.2397 (spec Sec7.5); the file bytes are 0x3E75C28F = 0.24");
            specNote(o.lookSensitivityH == 1.0f && o.lookSensitivityV == 1.0f && o.gamma == 0.5f, "look sensitivity 1.0/1.0 and gamma 0.5 (spec Sec7.5)");
        }

        // ---- controls: the same reader must FAIL on wrong inputs -----------------
        std::printf("  -- controls (must all FAIL the gate they target) --\n");
        {
            size_t failOff = 0;
            size_t n = fixedStrideRecords(p.bytes, failOff);
            std::printf("    control 1: fixed 5-byte stride follows %zu records and stops at file offset 0x%zX (spec: 20 records, 0x64)\n",
                        n, failOff);
            expect(n == 20 && failOff == 0x64 && n != 179, "fixed 5-byte stride collapses at record 20 / offset 0x64");
        }
        {
            size_t consumed = 0;
            size_t n = offByOneRecords(p.bytes, h.used0, consumed);
            std::printf("    control 2: wrong framing [L][L+1 bytes] yields %zu records, consumed %zu of %u\n", n, consumed, h.used0);
            expect(!(n == 179 && consumed == h.used0), "off-by-one record size does not consume the chunk exactly");
        }
        {
            std::vector<uint8_t> m = p.bytes;
            m[0x64] = 1; // the zero-length record's length byte 0 -> 1
            sr3save::SaveProfile q = sr3save::SaveProfile::parse(ByteView(m.data(), m.size()));
            std::printf("    control 3: record #20 length 0->1: chunk0 exact=%d (weak gate) records=%zu (need 179) consumed=%zu\n",
                        q.chunk0Walk().exact, q.chunk0Walk().spans.size(), q.chunk0Walk().consumed);
            expect(!fullGate(q), "flipping the zero-length record fails the full gate");
        }
        for (int delta : {+1, -1}) {
            std::vector<uint8_t> m = p.bytes;
            put32(m, 0x3000, h.used0 + static_cast<uint32_t>(delta));
            sr3save::SaveProfile q = sr3save::SaveProfile::parse(ByteView(m.data(), m.size()));
            std::printf("    control %s: used0 %+d: chunk0 exact=%d (weak gate) records=%zu (need 179)\n", delta > 0 ? "4" : "5",
                        delta, q.chunk0Walk().exact, q.chunk0Walk().spans.size());
            expect(!fullGate(q), delta > 0 ? "used0 + 1 fails the full gate" : "used0 - 1 fails the full gate");
        }
        {
            // How strong is each gate on its own? Mutate every record's length byte (L -> L+1)
            // and count how many mutants each gate still lets through.
            int mutants = 0, weakPass = 0, fullPass = 0;
            for (const auto& r : prof.statRecords()) {
                std::vector<uint8_t> m = p.bytes;
                m[r.fileOffset] = static_cast<uint8_t>(r.length + 1);
                sr3save::SaveProfile q = sr3save::SaveProfile::parse(ByteView(m.data(), m.size()));
                ++mutants;
                if (q.chunk0Walk().exact) ++weakPass;
                if (fullGate(q)) ++fullPass;
            }
            std::printf("    control 9 (sweep): %d single-byte length mutants: pass exact-consumption alone %d/%d; pass full gate %d/%d\n",
                        mutants, weakPass, mutants, fullPass, mutants);
            expect(mutants == 179 && fullPass == 0, "no length mutant passes the full gate (0/179)");
        }
        {
            std::vector<uint8_t> m(p.bytes.begin(), p.bytes.end() - 1);
            bool threw = false;
            try {
                sr3save::SaveProfile::parse(ByteView(m.data(), m.size()));
            } catch (const sr3save::FormatError&) {
                threw = true;
            }
            expect(threw, "control 6: file truncated by one byte is rejected");
        }
        {
            // mid-stream length change: record #5 (a 4-byte record) length 4 -> 3
            std::vector<uint8_t> m = p.bytes;
            size_t off5 = prof.statRecords()[5].fileOffset;
            m[off5] = 3;
            sr3save::SaveProfile q = sr3save::SaveProfile::parse(ByteView(m.data(), m.size()));
            std::printf("    control 7: record #5 length 4->3: exact=%d records=%zu statsConsistent=%d\n", q.chunk0Walk().exact,
                        q.chunk0Walk().spans.size(), q.statsConsistent());
            expect(!q.statsConsistent(), "a wrong record length makes the stat set inconsistent");
        }
        {
            // swap two table-A records: index==position must drop
            std::vector<uint8_t> m = p.bytes;
            for (size_t k = 0; k < 20; ++k) std::swap(m[0x3028 + k], m[0x3028 + 20 * 5 + k]);
            sr3save::SaveProfile q = sr3save::SaveProfile::parse(ByteView(m.data(), m.size()));
            std::printf("    control 8: table A records 0 and 5 swapped: index==position %u/164\n", q.tableAIndexMatches());
            expect(q.tableAIndexMatches() == 162, "table-A index==position falls from 164 to 162");
        }

        if (g_unexpected == before) ++passFiles;
    }

    std::printf("\nRESULT: %d/%zu distinct profile files pass every gate and every control behaves as required; %d unexpected outcome(s); %d spec-quoted value(s) differ from the file bytes (informational)\n",
                passFiles, profiles.size(), g_unexpected, g_specDiff);
    return g_unexpected == 0 ? 0 : 1;
}
