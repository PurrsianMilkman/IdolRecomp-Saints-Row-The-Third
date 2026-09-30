// Gate: the bit-packed slot summary of savedir.sr3d_pc (spec-save-format.md Sec8)
// decoded by this project's own reader (src/save_directory.cpp) and compared,
// field by field, with what the same spec says the game DERIVES from the matching
// sr3save_NN.sr3s_pc (src/save_snapshot.cpp, deriveDirectorySummary).
//
//   spec claim (Sec8.3): 171 comparisons (= 19 fields x 9 occupied slots) over
//   3 real directories, 0 mismatches. This harness counts over every distinct real
//   directory on the machine (the spec's Sec6 lists a 4th) and prints matched/total
//   per field.
//
// The decode uses the spec table's explicit bit positions; the encoder (used
// only to build synthetic fixtures) uses the writer RULE of Sec8.1. The two
// derivations of the bit layout are cross-checked here (22/22 fields).
//
// Controls that CAN fail:
//   A. a naive contiguous LSB-first packing (ignoring the "whole bytes are
//      byte-aligned" rule)             -> must collapse
//   B. summary of slot i compared with the snapshot of a different slot
//   C. play time by ROUNDING instead of truncation (the spec's own near-miss)
//   D. the summary shifted by one bit
//
// STRICTLY READ-ONLY on the save files.
//
// usage: validate_save_summary [folder ...]

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3save/save_crc.h"
#include "sr3save/save_directory.h"
#include "sr3save/save_snapshot.h"

namespace fs = std::filesystem;
using sr3save::SlotSummary;

namespace {

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

struct FieldDef {
    const char* name;
    bool derived; // compared against the snapshot
};
constexpr int kNumCmp = 19;
const char* kCmpNames[kNumCmp] = {"#1 reserved(0)",       "#3 flagged region records", "#5 play minutes",
                                  "#6 difficulty",        "#7 cheats-used bit",        "#9 completion %",
                                  "#10 format version",   "#11 label id",              "#12 reserved(0)",
                                  "#13 month",            "#14 day",                   "#15 year2",
                                  "#16 hour",             "#17 minute",                "#18 second",
                                  "#19 cash/100",         "#20 level",                 "#21 region count",
                                  "#22 points in level"};

void compare(const SlotSummary& d, const SlotSummary& e, bool out[kNumCmp]) {
    out[0] = d.reserved1 == e.reserved1;
    out[1] = d.flaggedRecordCount == e.flaggedRecordCount;
    out[2] = d.playMinutes == e.playMinutes;
    out[3] = d.difficulty == e.difficulty;
    out[4] = d.cheatsUsed == e.cheatsUsed;
    out[5] = d.completionPercent == e.completionPercent;
    out[6] = d.formatVersion == e.formatVersion;
    out[7] = d.locationLabelId == e.locationLabelId;
    out[8] = d.reserved12 == e.reserved12;
    out[9] = d.month == e.month;
    out[10] = d.day == e.day;
    out[11] = d.year2 == e.year2;
    out[12] = d.hour == e.hour;
    out[13] = d.minute == e.minute;
    out[14] = d.second == e.second;
    out[15] = d.cashDiv100 == e.cashDiv100;
    out[16] = d.level == e.level;
    out[17] = d.regionRecordCount == e.regionRecordCount;
    out[18] = d.pointsInLevel == e.pointsInLevel;
}

// Control A: decode with NAIVE contiguous LSB-first packing (fields simply laid end to end).
SlotSummary decodeNaive(const uint8_t* b) {
    static const int w[22] = {6, 7, 9, 5, 17, 2, 1, 1, 7, 8, 7, 5, 4, 5, 7, 5, 6, 6, 32, 6, 9, 32};
    uint32_t v[22];
    int pos = 0;
    for (int f = 0; f < 22; ++f) {
        uint64_t x = 0;
        for (int i = 0; i < w[f]; ++i, ++pos) {
            if (pos >= 24 * 8) break;
            x |= static_cast<uint64_t>((b[pos / 8] >> (pos % 8)) & 1u) << i;
        }
        v[f] = static_cast<uint32_t>(x);
    }
    SlotSummary s;
    s.reserved1 = v[0]; s.liveCountA_Open = v[1]; s.flaggedRecordCount = v[2]; s.liveCounterB_Open = v[3];
    s.playMinutes = v[4]; s.difficulty = v[5]; s.cheatsUsed = v[6]; s.autosave = v[7]; s.completionPercent = v[8];
    s.formatVersion = v[9]; s.locationLabelId = v[10]; s.reserved12 = v[11]; s.month = v[12]; s.day = v[13];
    s.year2 = v[14]; s.hour = v[15]; s.minute = v[16]; s.second = v[17]; s.cashDiv100 = static_cast<int32_t>(v[18]);
    s.level = v[19]; s.regionRecordCount = v[20]; s.pointsInLevel = v[21];
    return s;
}

// Control D: shift the 24 bytes left by one bit (as a 192-bit little-endian integer).
void shiftOneBit(const uint8_t* in, uint8_t* out) {
    for (int i = 0; i < 24; ++i) {
        unsigned hi = (i + 1 < 24) ? in[i + 1] : 0u;
        out[i] = static_cast<uint8_t>((in[i] >> 1) | ((hi & 1u) << 7));
    }
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
            {"CloudSyncBackup", "D:/Project Crreish/Saints Row 3 CRREISH/!Downloads/!Cloud Sync Backup/saves"},
            {"CloudSyncBackup(RTX)", "D:/SR3RTXREMIXCOMP/Saints Row 3/!Downloads/!Cloud Sync Backup/saves"},
            {"TEAM-B fixtures", "D:/Project Crreish/TEAM B/test-fixtures/saves"},
        };
    }

    int unexpected = 0;

    // ---- 0. the two derivations of the bit layout agree ----------------------
    std::printf("== 0. layout: spec table positions vs writer-rule simulation ==\n");
    {
        const auto& t = sr3save::slotSummaryBitPositionsFromSpecTable();
        auto w = sr3save::slotSummaryBitPositionsFromWriterRule();
        int agree = 0, total = 0;
        size_t totalBits = 0;
        for (size_t f = 0; f < t.size(); ++f) {
            ++total;
            if (t[f] == w[f]) ++agree;
            else std::printf("  field #%zu differs\n", f + 1);
            totalBits += t[f].size();
        }
        std::printf("  fields whose positions agree: %d/%d;  total bits %zu (spec: 155 bit-packed + 32 raw = 187)\n", agree,
                    total, totalBits);
        if (agree != total || totalBits != 187) ++unexpected;
        // 155 bit-packed bits leave the cursor 3 bits into byte 18: max used bit of byte 18 is 2.
        int maxBit18 = -1;
        for (const auto& f : t) for (const auto& p : f) if (p.byte == 18 && p.bit > maxBit18) maxBit18 = p.bit;
        std::printf("  highest used bit in byte 18: %d (spec: cursor 3 bits into byte 18 -> bits 0..2)\n", maxBit18);
        if (maxBit18 != 2) ++unexpected;
        // no bit used twice
        std::map<std::pair<int, int>, int> seen;
        int dup = 0;
        for (const auto& f : t) for (const auto& p : f) if (seen[{p.byte, p.bit}]++) ++dup;
        std::printf("  bits claimed by two fields: %d (must be 0)\n", dup);
        if (dup) ++unexpected;
    }

    // ---- collect distinct directories -----------------------------------------
    struct Dir {
        std::string label;
        fs::path folder;
        std::vector<uint8_t> bytes;
    };
    std::vector<Dir> dirs;
    std::map<std::vector<uint8_t>, size_t> seen;
    for (const auto& f : folders) {
        fs::path p = f.second / "savedir.sr3d_pc";
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) continue;
        std::vector<uint8_t> b = readFile(p);
        if (seen.count(b)) continue;
        seen[b] = dirs.size();
        dirs.push_back({f.first, f.second, b});
    }
    std::printf("\nfound %zu distinct real director%s\n", dirs.size(), dirs.size() == 1 ? "y" : "ies");
    if (dirs.empty()) {
        std::printf("NO REAL DIRECTORY FOUND\n");
        return 2;
    }

    int cmpMatch[kNumCmp] = {0}, cmpTotal[kNumCmp] = {0};
    int ctrlA[kNumCmp] = {0}, ctrlB[kNumCmp] = {0}, ctrlD[kNumCmp] = {0};
    int ctrlATotal = 0, ctrlBTotal = 0, ctrlDTotal = 0;
    int roundMismatch = 0, roundTotal = 0, anyCheats = 0;
    int slotsTotal = 0, roundtripOk = 0, inactiveZero = 0, inactiveTotal = 0, autosaveSlot0 = 0, autosaveOthers = 0,
        othersTotal = 0, versionOk = 0;
    std::vector<std::string> mismatches;

    for (const auto& d : dirs) {
        sr3save::SaveDirectory dir =
            sr3save::SaveDirectory::parse(sr3save::ByteView(d.bytes.data(), d.bytes.size()));
        std::printf("\n== directory from %s: hashValid=%d count=%u ==\n", d.label.c_str(), dir.hashValid(),
                    dir.activeSlotCount());
        if (!dir.hashValid()) ++unexpected;

        // load the snapshots this directory names
        std::map<int, sr3save::SaveSnapshot> snaps;
        for (int s : dir.occupiedSlotIndices()) {
            char fn[32];
            std::snprintf(fn, sizeof fn, "sr3save_%02d.sr3s_pc", s);
            fs::path sp = d.folder / fn;
            std::error_code ec;
            if (!fs::is_regular_file(sp, ec)) {
                std::printf("  slot %d: snapshot %s MISSING\n", s, fn);
                ++unexpected;
                continue;
            }
            std::vector<uint8_t> sb = readFile(sp);
            snaps.emplace(s, sr3save::SaveSnapshot::parse(sr3save::ByteView(sb.data(), sb.size())));
        }

        // inactive slots must be zeroed whole (the save normalises them, Sec6.1)
        for (size_t s = 0; s < sr3save::kSlotCount; ++s) {
            if (dir.slots()[s].active) continue;
            ++inactiveTotal;
            size_t off = sr3save::kSlotTableOffset + s * sr3save::kSlotRecordStride;
            bool zero = true;
            for (size_t k = 0; k < sr3save::kSlotRecordStride; ++k) zero = zero && d.bytes[off + k] == 0;
            if (zero) ++inactiveZero;
        }

        std::vector<int> occ = dir.occupiedSlotIndices();
        for (size_t oi = 0; oi < occ.size(); ++oi) {
            int s = occ[oi];
            auto it = snaps.find(s);
            if (it == snaps.end()) continue;
            const auto& rec = dir.slots()[static_cast<size_t>(s)];
            const SlotSummary& ds = rec.summary;
            ++slotsTotal;

            // round trip through the writer-rule encoder
            auto enc = sr3save::encodeSlotSummary(ds);
            if (enc == rec.summaryRaw) ++roundtripOk;
            else std::printf("  slot %d: encode(decode(raw)) != raw\n", s);

            SlotSummary ex = it->second.deriveDirectorySummary(ds.autosave);
            if (ex.cheatsUsed) ++anyCheats;
            bool r[kNumCmp];
            compare(ds, ex, r);
            int bad = 0;
            for (int k = 0; k < kNumCmp; ++k) {
                ++cmpTotal[k];
                if (r[k]) ++cmpMatch[k];
                else {
                    ++bad;
                    char buf[200];
                    std::snprintf(buf, sizeof buf, "dir(%s) slot %d field %s", d.label.c_str(), s, kCmpNames[k]);
                    mismatches.push_back(buf);
                }
            }
            if (ds.formatVersion == 94) ++versionOk;
            if (s == 0) autosaveSlot0 += ds.autosave ? 1 : 0;
            else { ++othersTotal; autosaveOthers += ds.autosave ? 1 : 0; }
            std::printf("  slot %d %s: %02u.%02u.%02u %02u:%02u:%02u  min=%u  diff=%u cheat=%d compl=%u%% label=%u cash/100=%d lvl=%u regions=%u(flagged %u) pts=%u autosave=%d | live#2=%u live#4=%u | %d/%d derived fields match\n",
                        s, it->second.levelName().c_str(), ds.month, ds.day, ds.year2, ds.hour, ds.minute, ds.second,
                        ds.playMinutes, ds.difficulty, ds.cheatsUsed, ds.completionPercent, ds.locationLabelId,
                        ds.cashDiv100, ds.level, ds.regionRecordCount, ds.flaggedRecordCount, ds.pointsInLevel,
                        ds.autosave, ds.liveCountA_Open, ds.liveCounterB_Open, kNumCmp - bad, kNumCmp);

            // control A: naive contiguous packing
            {
                SlotSummary nv = decodeNaive(rec.summaryRaw.data());
                bool rr[kNumCmp];
                compare(nv, ex, rr);
                ++ctrlATotal;
                for (int k = 0; k < kNumCmp; ++k) ctrlA[k] += rr[k] ? 1 : 0;
            }
            // control B: a different slot's snapshot
            if (occ.size() > 1) {
                int other = occ[(oi + 1) % occ.size()];
                auto it2 = snaps.find(other);
                if (it2 != snaps.end()) {
                    SlotSummary exo = it2->second.deriveDirectorySummary(ds.autosave);
                    bool rr[kNumCmp];
                    compare(ds, exo, rr);
                    ++ctrlBTotal;
                    for (int k = 0; k < kNumCmp; ++k) ctrlB[k] += rr[k] ? 1 : 0;
                }
            }
            // control D: one-bit shift
            {
                uint8_t sh[24];
                shiftOneBit(rec.summaryRaw.data(), sh);
                SlotSummary sd = sr3save::decodeSlotSummary(sh);
                bool rr[kNumCmp];
                compare(sd, ex, rr);
                ++ctrlDTotal;
                for (int k = 0; k < kNumCmp; ++k) ctrlD[k] += rr[k] ? 1 : 0;
            }
            // control C: rounding instead of truncation for the play time
            {
                double secs = static_cast<double>(it->second.playTimeSeconds());
                uint32_t rounded = static_cast<uint32_t>(secs / 60.0 + 0.5);
                ++roundTotal;
                if (rounded != ds.playMinutes) ++roundMismatch;
            }
        }
    }

    // ---- results -------------------------------------------------------------
    std::printf("\n== RESULTS: directory summary vs derivation from the matching snapshot ==\n");
    int totM = 0, totT = 0;
    for (int k = 0; k < kNumCmp; ++k) {
        std::printf("  %-28s %3d/%-3d   | control A(naive pack) %3d/%-3d  control B(other slot) %3d/%-3d  control D(1-bit shift) %3d/%-3d\n",
                    kCmpNames[k], cmpMatch[k], cmpTotal[k], ctrlA[k], ctrlATotal, ctrlB[k], ctrlBTotal, ctrlD[k], ctrlDTotal);
        totM += cmpMatch[k];
        totT += cmpTotal[k];
        if (cmpMatch[k] != cmpTotal[k]) ++unexpected;
    }
    int cA = 0, cB = 0, cD = 0;
    for (int k = 0; k < kNumCmp; ++k) { cA += ctrlA[k]; cB += ctrlB[k]; cD += ctrlD[k]; }
    std::printf("\n  TOTAL derived-field comparisons: %d/%d matched over %d occupied slots (spec's 3 directories: 171/171)\n", totM,
                totT, slotsTotal);
    std::printf("  control A (naive contiguous packing): %d/%d comparisons match\n", cA, ctrlATotal * kNumCmp);
    std::printf("  control B (other slot's snapshot):   %d/%d comparisons match (chance/constant-field agreement only)\n", cB,
                ctrlBTotal * kNumCmp);
    std::printf("  control D (summary shifted 1 bit):   %d/%d comparisons match\n", cD, ctrlDTotal * kNumCmp);
    std::printf("  control C (play time ROUNDED not truncated): differs from the stored value in %d/%d slots (spec: 4 mismatches in its run)\n",
                roundMismatch, roundTotal);
    std::printf("  encode(decode(raw)) == raw (all 24 bytes incl. unused bits): %d/%d slots\n", roundtripOk, slotsTotal);
    std::printf("  inactive slots zeroed whole (25 bytes): %d/%d\n", inactiveZero, inactiveTotal);
    std::printf("  autosave marker: set in %d/%zu directories' slot 0; set in %d/%d other slots (spec: 1 in slot 0 of all directories, 0 elsewhere)\n",
                autosaveSlot0, dirs.size(), autosaveOthers, othersTotal);
    std::printf("  summary format version == 94: %d/%d\n", versionOk, slotsTotal);
    if (!mismatches.empty()) {
        std::printf("\nMISMATCHES:\n");
        for (auto& m : mismatches) std::printf("  %s\n", m.c_str());
    }
    if (totM != totT) ++unexpected;
    {
        // Control A can only fail on fields whose position differs between the naive contiguous
        // packing and the writer rule (elsewhere the two layouts coincide and the control has no
        // power). Require it to fail, in most slots, on every discriminating field.
        const int fieldOfCmp[kNumCmp] = {0, 2, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
        static const int w[22] = {6, 7, 9, 5, 17, 2, 1, 1, 7, 8, 7, 5, 4, 5, 7, 5, 6, 6, 32, 6, 9, 32};
        const auto rule = sr3save::slotSummaryBitPositionsFromWriterRule();
        int naiveStart[22];
        int acc = 0;
        for (int f = 0; f < 22; ++f) { naiveStart[f] = acc; acc += w[f]; }
        int discriminating = 0, nonDiscriminating = 0, collapsed = 0;
        std::printf("  control A power: fields whose naive position differs from the writer rule:");
        for (int k = 0; k < kNumCmp; ++k) {
            int f = fieldOfCmp[k];
            bool same = true;
            for (int i = 0; i < w[f]; ++i) {
                sr3save::BitPos n{(naiveStart[f] + i) / 8, (naiveStart[f] + i) % 8};
                if (!(rule[static_cast<size_t>(f)][static_cast<size_t>(i)] == n)) same = false;
            }
            if (same) { ++nonDiscriminating; continue; }
            if (k == 4 && anyCheats == 0) { std::printf(" [%s: no power, the flag is 0 in every real slot]", kCmpNames[k]); ++nonDiscriminating; continue; }
            ++discriminating;
            std::printf(" %s", kCmpNames[k]);
            if (ctrlA[k] * 2 <= ctrlATotal) ++collapsed;
        }
        std::printf("\n  -> %d fields discriminating (control A fails on %d of them in >=50%% of slots), %d non-discriminating\n",
                    discriminating, collapsed, nonDiscriminating);
        if (collapsed != discriminating) ++unexpected;
        if (cD * 10 >= totM * 2) ++unexpected; // a 1-bit shift must destroy the decode (<20% agreement)
    }
    if (roundMismatch == 0) ++unexpected;               // the rounding control must be able to fail
    if (roundtripOk != slotsTotal || inactiveZero != inactiveTotal) ++unexpected;

    std::printf("\nRESULT: %s (%d unexpected)\n", unexpected == 0 ? "ALL GATES AS EXPECTED" : "DISAGREEMENT", unexpected);
    return unexpected == 0 ? 0 : 1;
}
