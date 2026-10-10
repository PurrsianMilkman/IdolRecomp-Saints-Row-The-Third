// DIAGNOSTIC-ONLY tool (2026-10-08, vorb-chunk seek-table-relative-offset
// confirmation task, NOT a ctest - same convention as wwise_setup_budget_probe
// / wwise_vorb_offset_probe above). Population-wide test of the disassembly-
// CONFIRMED formula spec-audio-runtime.md Sec11.11.5 now states (commit
// 64f705a, "Spec sync from Team A (orchestrator): Wwise vorb: setup/
// first-audio offsets = vorb field + seek-table length (vorb[16:20]) + data
// start"), cherry-picked onto this branch as commit c1f586e - superseding
// every earlier placement this project tried against vorb bytes [4:8)/[8:12)
// taken literally (tools/wwise_vorb_offset_probe.cpp, 3 placements, 0/493
// each):
//
//   setupPacketStart      = dataChunkOffset + vorb[4:8)  + vorb[16:20)
//   firstAudioPacketStart = dataChunkOffset + vorb[8:12) + vorb[16:20)
//
// (the common/default "selector==0 or >1" path - Sec11.11.5's own text:
// "every real standalone WAV in this project should hit the common path" -
// this project's real records are each a complete, self-contained RIFF/WAVE
// blob per sr3audio::MediaBankRecord, i.e. exactly "standalone" in that
// sense; the rare selector==1 fallback needs a live decode-state field
// (`this+0xe0`) this project has no file-derived equivalent of and is out of
// scope here).
//
// RESULT: FORMULA REJECTED, population-wide, 0/493 plausible, as literally
// stated and under 2 motivated alternate readings (drop the vorb[4:8) term;
// subtract it instead of adding). One EXACT, zero-variance, 493/493
// falsification: firstAudioPacketStart computes to EXACTLY the end of
// "data" for every real record (dataEnd - firstAudioPacketStart == 0,
// min=max=mean=0) - zero bytes left for any audio packet, in a corpus
// where every record demonstrably holds hundreds of KB of real compressed
// audio. A +-64/+256-byte local-neighbourhood scan around the computed
// setupPacketStart found no nearby fix either - not a rounding error. The
// one sub-claim that IS corroborated: vorb[16:20) really is the leading
// seek table's exact byte length (seekTableLen%4==0 for 493/493; it lands
// exactly on the boundary the prior pass's own approximate cleanRunEnd
// heuristic was already circling). Full numbers and worked examples:
// results/probe_vorb_seektable_relative_offsets_20261008.md. Relayed to
// Team A as a spec-sync question (what this+0x48/this+0x4c are actually
// consumed FOR by the Seek call, not a disassembly correction - Team B does
// not disassemble), not attempted further here per "no invented fixes."
//
// vorb[16:20) is independently CONFIRMED (Sec11.11.2, 981/981 real records
// on Team A's own side) to be the byte-length of a leading per-packet
// seek/index table that precedes the setup+audio packet stream -
// entryCount = vorb[16:20)/4. This tool re-derives/cross-checks that
// divisibility fact on THIS project's own 493-record corpus as a cheap bonus
// check, then applies the real formula above and validates the result with
// the SAME already-validated sr3audioruntime::probeSetupPacketBitBudget()
// plausibility bar every prior pass used (sane books/floors/residues/maps/
// modes counts, zero out-of-range codebook indices, <=7 leftover bits) -
// population-wide, not a handful of examples.
//
// Also measures this project's own real "vorb" chunk SIZE directly off the
// file's own declared RIFF sub-chunk size field (via findRiffSubChunk's
// returned size, not assumed/hardcoded) - this task's own explicit "measure,
// don't assume" requirement - to confirm which of Wwise's known vorb-chunk
// size variants these real files actually use (expected: a constant 0x2a/42,
// per wwise_vorbis_setup.h's own prior 800-sample measurement and Sec11.11.2's
// now-added "Chunk-size/layout question, answered" note).
//
// Usage: wwise_vorb_seektable_offset_probe <path to cutscene_sounds.vpp_pc>

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
    std::map<uint32_t, long long> vorbChunkSizeHistogram;  // measured directly, not assumed

    long long seekTableLenDivBy4 = 0;
    long long offsetsWithinData = 0;        // both setupPacketStart/firstAudioPacketStart inside [dataOffset, dataOffset+dataSize]
    long long setupBeforeAudio = 0;         // setupPacketStart < firstAudioPacketStart
    long long lenPrefixInBounds = 0;        // the 16-bit length read at setupPacketStart doesn't run past firstAudioPacketStart's own 2-byte prefix start... actually: packet payload fits before firstAudioPacketStart
    long long invariantExact = 0;           // setupPacketStart + 2 + declaredLen == firstAudioPacketStart, EXACTLY
    long long plausibleParse = 0;           // probeSetupPacketBitBudget() plausibility bar, population-wide
    long long printedExamples = 0;

    // Extra, population-wide falsification checks added after the formula's
    // literal placement came back 0/493 on the first 8 worked examples -
    // testing SPECIFIC alternate readings rather than guessing blindly:
    long long audioRegionZero = 0;          // dataEnd - firstAudioPacketStart == 0 (no room for ANY audio)
    int64_t audioRegionMin = INT64_MAX, audioRegionMax = INT64_MIN;
    double audioRegionSum = 0.0;
    long long noFieldAPlausible = 0;        // setup = dataChunkOffset + seekTableLen (drop vorb[4:8) term)
    long long minusFieldAPlausible = 0;     // setup = dataChunkOffset + seekTableLen - vorb[4:8)

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
            // DIRECT measurement of this record's own "vorb" chunk size, off
            // the file's own declared RIFF sub-chunk size field - NOT
            // assumed/hardcoded. findRiffSubChunk's returned `.second` is
            // exactly that clamped declared size.
            ++vorbChunkSizeHistogram[static_cast<uint32_t>(vorbLoc->second)];

            sr3audioruntime::WwiseVorbChunk vorb = sr3audioruntime::parseVorbChunk(recBytes, rec.size, vorbLoc->first);
            if (!vorb.present) continue;

            auto chunks = sr3audioruntime::parseRiffWaveContainer(recBytes, rec.size);
            if (!chunks.valid || !chunks.hasFmt || !chunks.hasData) continue;
            sr3audioruntime::StandardWaveFmt fmt =
                sr3audioruntime::parseStandardWaveFmt(recBytes, chunks.fmtOffset, chunks.fmtSize);
            if (!fmt.present || fmt.channels == 0) continue;

            ++recordsWithVorb;

            const uint32_t fieldA = u32le(vorb.raw42, 4);    // vorb[4:8)
            const uint32_t fieldB = u32le(vorb.raw42, 8);    // vorb[8:12)
            const uint32_t seekTableLen = u32le(vorb.raw42, 16);  // vorb[16:20)

            if (seekTableLen % 4 == 0) ++seekTableLenDivBy4;

            const size_t dataEnd = chunks.dataOffset + chunks.dataSize;
            const size_t setupPacketStart = chunks.dataOffset + fieldA + seekTableLen;
            const size_t firstAudioPacketStart = chunks.dataOffset + fieldB + seekTableLen;

            const bool withinData = setupPacketStart >= chunks.dataOffset && setupPacketStart < dataEnd &&
                                     firstAudioPacketStart > chunks.dataOffset && firstAudioPacketStart <= dataEnd;
            if (withinData) ++offsetsWithinData;
            if (setupPacketStart < firstAudioPacketStart) ++setupBeforeAudio;

            // How much room is actually left for audio packets under the
            // CONFIRMED formula's own firstAudioPacketStart? (independent of
            // whether setupPacketStart itself parses - this checks the
            // OTHER half of the formula on its own terms.)
            {
                int64_t audioRegion = static_cast<int64_t>(dataEnd) - static_cast<int64_t>(firstAudioPacketStart);
                if (audioRegion == 0) ++audioRegionZero;
                audioRegionMin = std::min(audioRegionMin, audioRegion);
                audioRegionMax = std::max(audioRegionMax, audioRegion);
                audioRegionSum += static_cast<double>(audioRegion);
            }

            // Alternate readings of the setup-packet half, tested directly
            // against the SAME plausibility bar, population-wide - not a
            // blind guess, but the two most specific nearby alternatives:
            // (a) drop the vorb[4:8) term entirely (matches ww2ogg's own
            // simpler "setup_offset = start_offset + seek_size" and the
            // prior pass's own already-tried "LENPREFIX@trueTableEnd"
            // placement, now using the EXACT seekTableLen field instead of
            // that pass's own approximate cleanRunEnd landmark);
            // (b) subtract it instead of adding (a plausible transcription-
            // sign slip to rule in or out explicitly rather than assume).
            {
                auto tryPlace = [&](size_t off) -> bool {
                    if (off + 2 > dataEnd) return false;
                    uint16_t len = static_cast<uint16_t>(recBytes[off]) |
                                   static_cast<uint16_t>(static_cast<uint16_t>(recBytes[off + 1]) << 8);
                    if (off + 2 + len > dataEnd) return false;
                    auto p = sr3audioruntime::probeSetupPacketBitBudget(recBytes + off + 2, len, fmt.channels);
                    return p.ranToEnd && p.leftoverBits >= 0 && p.leftoverBits <= 7 && p.outOfRangeCount == 0 &&
                           p.books >= 1 && p.books <= 256 && p.floors >= 1 && p.floors <= 64 && p.residues >= 1 &&
                           p.residues <= 64 && p.maps >= 1 && p.maps <= 64 && p.modes >= 1 && p.modes <= 64;
                };
                if (tryPlace(chunks.dataOffset + seekTableLen)) ++noFieldAPlausible;
                if (seekTableLen >= fieldA && tryPlace(chunks.dataOffset + seekTableLen - fieldA)) ++minusFieldAPlausible;
            }

            if (!withinData || setupPacketStart >= firstAudioPacketStart) continue;

            // Sec11.11.1 CONFIRMED bare 16-bit length-prefix framing, applied
            // starting exactly at setupPacketStart instead of chunks.dataOffset
            // (the now-corrected starting point).
            if (setupPacketStart + 2 > dataEnd) continue;
            const uint16_t declaredLen = static_cast<uint16_t>(recBytes[setupPacketStart]) |
                                          static_cast<uint16_t>(static_cast<uint16_t>(recBytes[setupPacketStart + 1]) << 8);
            const size_t payloadOffset = setupPacketStart + 2;
            if (payloadOffset + declaredLen > dataEnd) continue;
            ++lenPrefixInBounds;

            // The free, falsifiable self-consistency check: does the setup
            // packet's own declared length land EXACTLY on the independently
            // computed firstAudioPacketStart (same invariant ww2ogg's own
            // parser enforces: "first audio packet doesn't follow setup
            // packet" is a hard error there)?
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

            if (printedExamples < 8) {
                ++printedExamples;
                std::cout << "[" << n << "] rec@" << rec.offset << " channels=" << fmt.channels
                          << " dataChunkSize=" << chunks.dataSize << " fieldA(vorb[4:8))=" << fieldA
                          << " fieldB(vorb[8:12))=" << fieldB << " seekTableLen(vorb[16:20))=" << seekTableLen
                          << " seekTableLen%4=" << (seekTableLen % 4) << "\n";
                std::cout << "    setupPacketStart(rel data)=" << (setupPacketStart - chunks.dataOffset)
                          << " firstAudioPacketStart(rel data)=" << (firstAudioPacketStart - chunks.dataOffset)
                          << " declaredLen=" << declaredLen
                          << " payloadEnd(rel data)=" << (payloadOffset + declaredLen - chunks.dataOffset)
                          << " INVARIANT_EXACT=" << exact << "\n";
                std::cout << "    setup packet bytes first 24: " << hexDump(recBytes + payloadOffset, std::min<size_t>(24, declaredLen)) << "\n";
                std::cout << "    PARSE: books=" << probe.books << " floors=" << probe.floors
                          << " residues=" << probe.residues << " maps=" << probe.maps << " modes=" << probe.modes
                          << " outOfRangeCount=" << probe.outOfRangeCount << " ranToEnd=" << probe.ranToEnd
                          << " leftoverBits=" << probe.leftoverBits << " PLAUSIBLE=" << plausible << "\n";

                // BONUS DIAGNOSTIC (not part of the formula under test): scan
                // a window of byte offsets around the computed
                // setupPacketStart for ANY position whose own 16-bit length
                // prefix + probeSetupPacketBitBudget() parse is PLAUSIBLE -
                // reveals whether a small, consistent delta from the formula's
                // own setupPacketStart would fix it (real signal, worth
                // relaying) versus no nearby fix existing at all (the formula
                // itself, or this project's own reading of which vorb dwords
                // feed it, would need to be revisited).
                for (long delta = -64; delta <= 256; ++delta) {
                    long long cand = static_cast<long long>(setupPacketStart) + delta;
                    if (cand < static_cast<long long>(chunks.dataOffset) || cand + 2 > static_cast<long long>(dataEnd)) continue;
                    size_t candOff = static_cast<size_t>(cand);
                    uint16_t candLen = static_cast<uint16_t>(recBytes[candOff]) |
                                        static_cast<uint16_t>(static_cast<uint16_t>(recBytes[candOff + 1]) << 8);
                    if (candOff + 2 + candLen > dataEnd || candLen < 32) continue;
                    auto candProbe = sr3audioruntime::probeSetupPacketBitBudget(recBytes + candOff + 2, candLen, fmt.channels);
                    bool candPlausible = candProbe.ranToEnd && candProbe.leftoverBits >= 0 && candProbe.leftoverBits <= 7 &&
                                          candProbe.outOfRangeCount == 0 && candProbe.books >= 1 && candProbe.books <= 256 &&
                                          candProbe.floors >= 1 && candProbe.floors <= 64 && candProbe.residues >= 1 &&
                                          candProbe.residues <= 64 && candProbe.maps >= 1 && candProbe.maps <= 64 &&
                                          candProbe.modes >= 1 && candProbe.modes <= 64;
                    if (candPlausible) {
                        std::cout << "    [SCAN-HIT] delta=" << delta << " candLen=" << candLen
                                  << " books=" << candProbe.books << " floors=" << candProbe.floors
                                  << " residues=" << candProbe.residues << " maps=" << candProbe.maps
                                  << " modes=" << candProbe.modes << " leftoverBits=" << candProbe.leftoverBits << "\n";
                    }
                }
            }
        }
    }

    std::cout << "\n=== POPULATION SUMMARY (" << recordsWithVorb << " real records) ===\n";
    std::cout << "measured 'vorb' chunk size (direct, off the file's own declared RIFF size field):\n";
    for (const auto& kv : vorbChunkSizeHistogram) {
        std::cout << "  size=" << kv.first << " (0x" << std::hex << kv.first << std::dec << "): " << kv.second << " records\n";
    }
    std::cout << "seekTableLen (vorb[16:20)) % 4 == 0                    : " << seekTableLenDivBy4 << "/" << recordsWithVorb << "\n";
    std::cout << "setupPacketStart < firstAudioPacketStart               : " << setupBeforeAudio << "/" << recordsWithVorb << "\n";
    std::cout << "both offsets within [dataOffset, dataOffset+dataSize]  : " << offsetsWithinData << "/" << recordsWithVorb << "\n";
    std::cout << "16-bit length prefix @ setupPacketStart in bounds      : " << lenPrefixInBounds << "/" << recordsWithVorb << "\n";
    std::cout << "INVARIANT (setupPacketStart+2+declaredLen == firstAudioPacketStart), EXACT: "
              << invariantExact << "/" << recordsWithVorb << "\n";
    std::cout << "PARSE([payloadOffset, payloadOffset+declaredLen)) PLAUSIBLE as a setup packet: "
              << plausibleParse << "/" << recordsWithVorb << "\n";

    std::cout << "\naudio region per the CONFIRMED formula's own firstAudioPacketStart (dataEnd - firstAudioPacketStart):\n";
    std::cout << "  == 0 (zero bytes left for ANY audio packet): " << audioRegionZero << "/" << recordsWithVorb << "\n";
    std::cout << "  min=" << audioRegionMin << " max=" << audioRegionMax
              << " mean=" << (recordsWithVorb ? audioRegionSum / recordsWithVorb : 0.0) << "\n";
    std::cout << "\nALTERNATE setup-packet placements (same plausibility bar, population-wide):\n";
    std::cout << "  (a) drop vorb[4:8) term - setup = dataChunkOffset + seekTableLen            : "
              << noFieldAPlausible << "/" << recordsWithVorb << "\n";
    std::cout << "  (b) subtract vorb[4:8) - setup = dataChunkOffset + seekTableLen - vorb[4:8)  : "
              << minusFieldAPlausible << "/" << recordsWithVorb << "\n";

    return 0;
}
