// DIAGNOSTIC-ONLY tool (2026-10-08, vorb-chunk setup/audio-offset-pair
// re-hypothesis task, NOT a ctest - same "one-shot real-data probe, not a
// ctest" convention as tools/wwise_vorb_seektable_offset_probe.cpp above,
// whose own result this tool follows up on directly).
//
// NEW HYPOTHESIS (orchestrator-dispatched, following the REJECTED
// disassembly-relayed formula results/probe_vorb_seektable_relative_
// offsets_20261008.md found - 0/493, firstAudioPacketStart landing exactly
// on dataEnd for every record): since vorb[16:20) is independently CONFIRMED
// (three ways: Team A's disassembly, this project's own direct measurement,
// and public-tool corroboration) to be the leading seek table's own exact
// byte length, maybe it ALSO doubles as the setup packet's own byte offset
// (no separate offset field needed - the seek table's own end IS the setup
// packet's start), and the REAL first-audio-packet offset field is the NEXT
// dword, vorb[20:24) - NOT vorb[8:12) (the field the REJECTED formula used,
// which this project's own prior pass showed algebraically satisfies
// `vorb[8:12) + vorb[16:20) == dataChunkSize` for every record, i.e. an
// end-of-data-shaped field, not a packet-start-shaped one):
//
//   setupPacketStart      = dataChunkOffset + vorb[16:20)
//   firstAudioPacketStart = dataChunkOffset + vorb[20:24)
//
// PUBLIC-SOURCE CORROBORATION (explicitly authorized this round - read for
// FORMAT UNDERSTANDING ONLY, not transcribed into this file; cited here,
// not copied): vgmstream's own real source (`src/meta/wwise.c`, ISC-style
// license), read via WebFetch against the live GitHub raw file, independently
// confirms EXACTLY this shape for the `case 0x2a:` vorb-chunk-size variant
// vgmstream's own comment names "Saints Row the 3rd (X360)-v53-most" (the
// same variant this project's own direct measurement already pinned at
// 493/493 - see wwise_vorb_seektable_offset_probe's own result) -
// `data_offsets = 0x10` for that case, and:
//   seek_size    = read_u32(extra_offset + data_offsets + 0x00)   = vorb[0x10:0x14) = vorb[16:20)
//   audio_offset = read_u32(extra_offset + data_offsets + 0x04)   = vorb[0x14:0x18) = vorb[20:24)
//   setup_offset = start_offset(dataChunkOffset) + seek_size                        = dataChunkOffset + vorb[16:20)
//   (first audio) start_offset += audio_offset                                     = dataChunkOffset + vorb[20:24)
// i.e. the exact formula pair above, field-for-field and arithmetic-op-for-
// arithmetic-op. A second file read the same way (vgmstream's
// `src/coding/vorbis_custom_utils_wwise.c`) separately confirms this
// project's EXISTING packet-header-shape assumption (a bare 2-byte/u16
// little-endian length prefix, no granule field) is ALSO correct for this
// exact version: `WWVORBIS_V53` (the version `case 0x2a:` selects) maps to
// `header_type == WWV_TYPE_2` ("2-byte header"), used identically for the
// setup packet (`is_setup=1` takes the exact same `read_packet()` path/
// header_type as audio packets) and every audio packet - so a packet-
// header-SHAPE mismatch does NOT explain this branch's own prior 19-24-byte
// short-read finding; only the STARTING OFFSET was ever wrong. Per this
// project's own labelling convention (spec-audio-runtime.md's own "Team B
// never disassembles" footing), this public-source match is cited as
// `agrees-with-public-format`, NOT `[CONFIRMED]` (reserved for direct
// disassembly) - Team A's own disassembly answer for `this+0x48`/`this+0x4c`
// has not landed as of this writing.
//
// Usage: wwise_vorb_setup_audio_dword_probe <path to cutscene_sounds.vpp_pc>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "sr3audio/media_bank.h"
#include "sr3audioruntime/wwise_setup_rebuild.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    if (n < 0) n = 0;
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

bool endsWith(const std::string& n, const std::string& s) {
    return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0;
}

uint32_t u32le(const std::vector<uint8_t>& raw42, size_t off) {
    return static_cast<uint32_t>(raw42[off]) | (static_cast<uint32_t>(raw42[off + 1]) << 8) |
           (static_cast<uint32_t>(raw42[off + 2]) << 16) | (static_cast<uint32_t>(raw42[off + 3]) << 24);
}

std::string hexDump(const uint8_t* p, size_t n) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(hex[(p[i] >> 4) & 0xf]);
        s.push_back(hex[p[i] & 0xf]);
        s.push_back(' ');
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string archivePath = argc > 1 ? argv[1]
        : "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/cutscene_sounds.vpp_pc";

    std::vector<uint8_t> archiveBytes = readFile(archivePath);
    if (archiveBytes.empty()) {
        std::cerr << "could not read " << archivePath << "\n";
        return 1;
    }

    vpp::Container c(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));

    long long recordsWithVorb = 0;
    long long identityChecked = 0, identityHolds = 0;  // vorb[20:24) == vorb[4:8) + vorb[16:20), per commit 8871fb6

    long long setupBeforeAudio = 0;
    long long offsetsWithinData = 0;
    long long lenPrefixInBounds = 0;
    long long invariantExact = 0;       // setupPacketStart + 2 + declaredLen == firstAudioPacketStart
    long long plausibleParse = 0;       // probeSetupPacketBitBudget() plausibility bar, population-wide

    long long audioRegionPositive = 0;  // dataEnd - firstAudioPacketStart > 0 (room for real audio)
    int64_t audioRegionMin = INT64_MAX, audioRegionMax = INT64_MIN;
    double audioRegionSum = 0.0;

    long long printedExamples = 0;

    long long ranToEndCount = 0;
    long long outOfRangeZero = 0;
    long long outOfRangeTotalSum = 0;
    std::map<std::string, long long> failureStageHistogram;  // keyed by the giveUp() "where" text
    std::map<int, long long> booksHistogram;
    std::map<int, long long> leftoverBitsHistogram;  // only when ranToEnd

    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (!endsWith(n, "_media.bnk_pc")) continue;

        std::vector<uint8_t> storage;
        vpp::ByteView bytes;
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            bytes = c.rawEntryBytes(i);
        } else {
            auto r = c.decompressEntry(i);
            if (r.data.empty()) continue;
            storage = std::move(r.data);
            bytes = vpp::ByteView(storage.data(), storage.size());
        }

        sr3audio::MediaBank mb;
        try {
            mb = sr3audio::MediaBank::parse(bytes);
        } catch (const std::exception&) {
            continue;
        }
        const size_t declaredSize = c.entries()[i].payload.decompressedLength;
        std::vector<sr3audio::MediaBankRecord> records;
        try {
            records = mb.walk(declaredSize);
        } catch (const std::exception&) {
            continue;
        }

        for (const auto& rec : records) {
            if (rec.size < 128) continue;
            const uint8_t* recBytes = bytes.data() + rec.offset;

            auto vorbLoc = sr3audioruntime::findRiffSubChunk(recBytes, rec.size, sr3audioruntime::kFourCcVorb);
            if (!vorbLoc) continue;

            sr3audioruntime::WwiseVorbChunk vorb = sr3audioruntime::parseVorbChunk(recBytes, rec.size, vorbLoc->first);
            if (!vorb.present) continue;

            auto chunks = sr3audioruntime::parseRiffWaveContainer(recBytes, rec.size);
            if (!chunks.valid || !chunks.hasFmt || !chunks.hasData) continue;
            sr3audioruntime::StandardWaveFmt fmt =
                sr3audioruntime::parseStandardWaveFmt(recBytes, chunks.fmtOffset, chunks.fmtSize);
            if (!fmt.present || fmt.channels == 0) continue;

            ++recordsWithVorb;

            const uint32_t seekTableLen = u32le(vorb.raw42, 16);   // vorb[16:20)
            const uint32_t audioOffsetField = u32le(vorb.raw42, 20);  // vorb[20:24) - the NEW field under test
            const uint32_t fieldA = u32le(vorb.raw42, 4);  // vorb[4:8) - Team A's "this+0x48" term per commit 8871fb6
            if (audioOffsetField == fieldA + seekTableLen) ++identityHolds;
            ++identityChecked;

            const size_t dataEnd = chunks.dataOffset + chunks.dataSize;
            const size_t setupPacketStart = chunks.dataOffset + seekTableLen;
            const size_t firstAudioPacketStart = chunks.dataOffset + audioOffsetField;

            const bool withinData = setupPacketStart >= chunks.dataOffset && setupPacketStart < dataEnd &&
                                     firstAudioPacketStart > chunks.dataOffset && firstAudioPacketStart <= dataEnd;
            if (withinData) ++offsetsWithinData;
            if (setupPacketStart < firstAudioPacketStart) ++setupBeforeAudio;

            {
                int64_t audioRegion = static_cast<int64_t>(dataEnd) - static_cast<int64_t>(firstAudioPacketStart);
                if (audioRegion > 0) ++audioRegionPositive;
                audioRegionMin = std::min(audioRegionMin, audioRegion);
                audioRegionMax = std::max(audioRegionMax, audioRegion);
                audioRegionSum += static_cast<double>(audioRegion);
            }

            if (!withinData || setupPacketStart >= firstAudioPacketStart) continue;

            if (setupPacketStart + 2 > dataEnd) continue;
            const uint16_t declaredLen = static_cast<uint16_t>(recBytes[setupPacketStart]) |
                                          static_cast<uint16_t>(static_cast<uint16_t>(recBytes[setupPacketStart + 1]) << 8);
            const size_t payloadOffset = setupPacketStart + 2;
            if (payloadOffset + declaredLen > dataEnd) continue;
            ++lenPrefixInBounds;

            const bool exact = (payloadOffset + declaredLen == firstAudioPacketStart);
            if (exact) ++invariantExact;

            sr3audioruntime::SetupBudgetProbeResult probe =
                sr3audioruntime::probeSetupPacketBitBudget(recBytes + payloadOffset, declaredLen, fmt.channels);
            const bool plausible = probe.ranToEnd && probe.leftoverBits >= 0 && probe.leftoverBits <= 7 &&
                                    probe.outOfRangeCount == 0 && probe.books >= 1 && probe.books <= 256 &&
                                    probe.floors >= 1 && probe.floors <= 64 && probe.residues >= 1 &&
                                    probe.residues <= 64 && probe.maps >= 1 && probe.maps <= 64 &&
                                    probe.modes >= 1 && probe.modes <= 64;
            if (plausible) ++plausibleParse;

            if (probe.ranToEnd) {
                ++ranToEndCount;
                ++leftoverBitsHistogram[probe.leftoverBits];
            } else {
                // Classify by the "at: <X> (<reason>)" prefix up to the first
                // "/" digit run, coarse-grained to the SECTION name
                // (e.g. "residue", "floor", "codebook", "mappings count")
                // rather than the exact index, so the histogram stays small.
                std::string d = probe.diagnostic;
                size_t atPos = d.find("at: ");
                std::string section = atPos != std::string::npos ? d.substr(atPos + 4) : d;
                // Strip a trailing " N/M (...)" style index if present, keep
                // just the leading word(s).
                size_t paren = section.find(" (");
                std::string reason = paren != std::string::npos ? section.substr(paren + 2) : "";
                if (!reason.empty() && reason.back() == ')') reason.pop_back();
                size_t firstDigit = section.find_first_of("0123456789");
                std::string stage = firstDigit != std::string::npos ? section.substr(0, firstDigit) : section;
                failureStageHistogram[stage + "| " + reason] += 1;
            }
            ++booksHistogram[probe.books];
            outOfRangeTotalSum += probe.outOfRangeCount;
            if (probe.outOfRangeCount == 0) ++outOfRangeZero;

            if (printedExamples < 10) {
                ++printedExamples;
                std::cout << "[" << n << "] rec@" << rec.offset << " channels=" << fmt.channels
                          << " dataChunkSize=" << chunks.dataSize << " seekTableLen(vorb[16:20))=" << seekTableLen
                          << " audioOffsetField(vorb[20:24))=" << audioOffsetField << "\n";
                std::cout << "    setupPacketStart(rel data)=" << (setupPacketStart - chunks.dataOffset)
                          << " firstAudioPacketStart(rel data)=" << (firstAudioPacketStart - chunks.dataOffset)
                          << " declaredLen=" << declaredLen
                          << " payloadEnd(rel data)=" << (payloadOffset + declaredLen - chunks.dataOffset)
                          << " INVARIANT_EXACT=" << exact
                          << " audioRegionBytes=" << (static_cast<int64_t>(dataEnd) - static_cast<int64_t>(firstAudioPacketStart)) << "\n";
                std::cout << "    setup packet bytes first 24: " << hexDump(recBytes + payloadOffset, std::min<size_t>(24, declaredLen)) << "\n";
                std::cout << "    PARSE: books=" << probe.books << " floors=" << probe.floors
                          << " residues=" << probe.residues << " maps=" << probe.maps << " modes=" << probe.modes
                          << " outOfRangeCount=" << probe.outOfRangeCount << " ranToEnd=" << probe.ranToEnd
                          << " leftoverBits=" << probe.leftoverBits << " PLAUSIBLE=" << plausible
                          << " diag=" << probe.diagnostic << "\n";
                if (probe.outOfRangeCount > 0) {
                    std::cout << "    first out-of-range index=" << probe.outOfRangeIndices[0]
                              << " at codebook position " << probe.outOfRangePositions[0] << "/" << probe.books << "\n";
                }
            }
        }
    }

    std::cout << "\nIDENTITY CHECK (commit 8871fb6): vorb[20:24) == vorb[4:8) + vorb[16:20): "
              << identityHolds << "/" << identityChecked << "\n";

    std::cout << "\n=== POPULATION SUMMARY (" << recordsWithVorb << " real records) ===\n";
    std::cout << "setupPacketStart < firstAudioPacketStart               : " << setupBeforeAudio << "/" << recordsWithVorb << "\n";
    std::cout << "both offsets within [dataOffset, dataOffset+dataSize]  : " << offsetsWithinData << "/" << recordsWithVorb << "\n";
    std::cout << "16-bit length prefix @ setupPacketStart in bounds      : " << lenPrefixInBounds << "/" << recordsWithVorb << "\n";
    std::cout << "INVARIANT (setupPacketStart+2+declaredLen == firstAudioPacketStart), EXACT: "
              << invariantExact << "/" << recordsWithVorb << "\n";
    std::cout << "PARSE([payloadOffset, payloadOffset+declaredLen)) PLAUSIBLE as a setup packet: "
              << plausibleParse << "/" << recordsWithVorb << "\n";

    std::cout << "\naudio region per the NEW formula's own firstAudioPacketStart (dataEnd - firstAudioPacketStart):\n";
    std::cout << "  > 0 (real room left for audio packets): " << audioRegionPositive << "/" << recordsWithVorb << "\n";
    std::cout << "  min=" << audioRegionMin << " max=" << audioRegionMax
              << " mean=" << (recordsWithVorb ? audioRegionSum / recordsWithVorb : 0.0) << "\n";

    std::cout << "\n=== EXTRA DIAGNOSTICS (ALL records with a valid 16-bit length prefix in bounds, "
              << lenPrefixInBounds << "/" << recordsWithVorb << ") ===\n";
    std::cout << "ranToEnd (walked the ENTIRE declared packet with no EOF, regardless of plausibility gate): "
              << ranToEndCount << "/" << lenPrefixInBounds << "\n";
    std::cout << "outOfRangeCount==0 (every codebook library index in [0,599)): " << outOfRangeZero << "/" << lenPrefixInBounds << "\n";
    std::cout << "sum of outOfRangeCount across ALL records: " << outOfRangeTotalSum << "\n";
    std::cout << "\nbooks histogram (probe.books value -> record count):\n";
    for (const auto& kv : booksHistogram) std::cout << "  books=" << kv.first << ": " << kv.second << "\n";
    std::cout << "\nleftoverBits histogram (only for ranToEnd records):\n";
    for (const auto& kv : leftoverBitsHistogram) std::cout << "  leftoverBits=" << kv.first << ": " << kv.second << "\n";
    std::cout << "\nfailure-stage histogram (section| reason -> count), for NON-ranToEnd records:\n";
    for (const auto& kv : failureStageHistogram) std::cout << "  [" << kv.first << "]: " << kv.second << "\n";

    return 0;
}
