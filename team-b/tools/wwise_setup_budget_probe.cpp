// DIAGNOSTIC-ONLY tool (2026-10-08, out-of-range-codebook-index bring-up
// task) - not part of the real decode path, not a ctest. Investigates
// whether wwise_setup_rebuild.cpp's OUTER per-codebook 10-bit-index bit
// grammar is itself correct, against the real 493-record population
// tests/real_vorbis_decode_test.cpp already exercises (cutscene_sounds.vpp_pc).
//
// See sr3audioruntime::probeSetupPacketBitBudget()'s own header comment
// (include/sr3audioruntime/wwise_setup_rebuild.h) for the full rationale:
// a codebook's BODY never touches the main packet's own bitstream cursor
// regardless of whether its 10-bit index resolves in range, so skipping an
// out-of-range index and continuing through floors/residues/mappings/modes
// costs the main cursor nothing extra. If the rest of the packet then
// closes its own declared byte length cleanly for the bulk of the real
// population, that is real-data evidence the outer grammar is right and
// this is a pure codebook-library-data completeness gap; if not, that is
// evidence of a still-unfound bit-grammar bug.
//
// Usage: wwise_setup_budget_probe <path to cutscene_sounds.vpp_pc>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "sr3audio/media_bank.h"
#include "sr3audioruntime/wav_writer.h"
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

}  // namespace

namespace {

// Tests the "leading seek/size TABLE of (sampleCount_u16, byteSize_u16)
// pairs, followed by N packet payloads back-to-back" hypothesis: scans
// increasing table lengths k and checks whether 4*k (the table's own size)
// plus the sum of the first k byteSize fields lands EXACTLY on
// dataChunkSize - the same "does the file's own declared oracle land
// exactly" falsifiable check this project already uses elsewhere
// (walkFramedPackets' own `consistent` flag, MediaBank::walk()).
// Returns the matching k (table entry count) if found in [1,4096], or -1.
long findSeekTableLength(const uint8_t* data, size_t dataChunkSize, uint64_t& outSumByteSize) {
    uint64_t sum = 0;
    for (long k = 1; k <= 4096; ++k) {
        const size_t off = static_cast<size_t>(k - 1) * 4 + 2;  // byteSize field of entry k-1
        if (off + 2 > dataChunkSize) break;
        const uint16_t byteSize = static_cast<uint16_t>(data[off]) | (static_cast<uint16_t>(data[off + 1]) << 8);
        sum += byteSize;
        const uint64_t total = static_cast<uint64_t>(k) * 4 + sum;
        if (total == dataChunkSize) {
            outSumByteSize = sum;
            return k;
        }
        if (total > dataChunkSize) break;
    }
    outSumByteSize = sum;
    return -1;
}

// Profiles how much of `data[0..dataChunkSize)` "looks like" a table of
// near-integer/near-round float32 values (offset+2, stride 4 - the pattern
// this task's own investigation found at the START of every real record's
// "data" chunk) versus high-entropy noise (what a real compressed Vorbis
// bitstream should look like). Reports, per 2048-byte block, the fraction
// of float32 samples within 1% of an integer - a purely mechanical,
// falsifiable measurement to locate where the "clean" region ends, without
// assuming or inventing what it actually is.
void profileCleanliness(const uint8_t* data, size_t dataChunkSize, size_t blockBytes, size_t maxBlocks) {
    for (size_t blockStart = 0; blockStart < dataChunkSize && (blockStart / blockBytes) < maxBlocks; blockStart += blockBytes) {
        size_t blockEnd = std::min(blockStart + blockBytes, dataChunkSize);
        int total = 0, clean = 0;
        for (size_t off = blockStart + 2; off + 4 <= blockEnd; off += 4) {
            float f;
            std::memcpy(&f, data + off, 4);
            ++total;
            if (std::isfinite(f) && std::fabs(f) > 0.05f && std::fabs(f) < 1.0e6f) {
                float r = std::round(f);
                if (std::fabs(f - r) / std::max(std::fabs(f), 1.0f) < 0.01f) ++clean;
            }
        }
        if (total > 0) {
            std::cout << "      block@" << blockStart << " clean=" << clean << "/" << total << " ("
                      << (100.0 * clean / total) << "%)\n";
        }
    }
}

// Lists every top-level RIFF chunk tag/size in a RIFF/WAVE blob, in file
// order - a generic walk (not coupled to any specific tag this project
// already recognises) to check for any chunk the existing, tag-specific
// readers (fmt /vorb/data) might be skipping over unnoticed.
std::string listRiffChunks(const uint8_t* data, size_t size) {
    std::string out;
    if (size < 12) return out;
    auto tag4 = [&](size_t off) -> std::string {
        char buf[5] = {0};
        for (int i = 0; i < 4; ++i) buf[i] = static_cast<char>(data[off + i]);
        return std::string(buf);
    };
    auto u32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(data[off]) | (static_cast<uint32_t>(data[off + 1]) << 8) |
               (static_cast<uint32_t>(data[off + 2]) << 16) | (static_cast<uint32_t>(data[off + 3]) << 24);
    };
    size_t offset = 12;
    while (offset + 8 <= size) {
        std::string t = tag4(offset);
        uint32_t sz = u32(offset + 4);
        out += "[" + t + " @" + std::to_string(offset) + " size=" + std::to_string(sz) + "] ";
        size_t advance = static_cast<size_t>(sz) + (sz & 1u);
        if (offset + 8 + advance <= offset) break;
        offset = offset + 8 + advance;
        if (advance == 0 && sz == 0) break;
    }
    return out;
}

// Finds the first 4-byte-entry index (stride 4, testing the offset+2
// float32) where the "clean near-round-number" run (see profileCleanliness)
// breaks down - i.e. the apparent end of the leading table-like region.
size_t findCleanRunEnd(const uint8_t* data, size_t dataChunkSize) {
    size_t entry = 0;
    for (; ; ++entry) {
        size_t off = entry * 4 + 2;
        if (off + 4 > dataChunkSize) break;
        float f;
        std::memcpy(&f, data + off, 4);
        bool clean = false;
        if (std::isfinite(f) && std::fabs(f) > 0.05f && std::fabs(f) < 1.0e6f) {
            float r = std::round(f);
            if (std::fabs(f - r) / std::max(std::fabs(f), 1.0f) < 0.01f) clean = true;
        }
        if (!clean) break;
    }
    return entry * 4;  // byte offset where the clean run ends
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
    const std::string archivePath = argc > 1 ? argv[1] : "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/cutscene_sounds.vpp_pc";

    std::vector<uint8_t> archiveBytes = readFile(archivePath);
    if (archiveBytes.empty()) {
        std::cerr << "could not read " << archivePath << "\n";
        return 1;
    }

    vpp::Container c(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));

    long long recordsWithVorb = 0;
    long long ranToEndCount = 0;
    long long ranToEndCleanCount = 0;   // ranToEnd && leftoverBits in [0,7] (byte-aligned slack only)
    long long didNotRunToEndCount = 0;
    long long zeroOutOfRangeCount = 0;  // records where every index was already in range
    long long outOfRangeButCleanCount = 0;  // out-of-range present, but STILL closes cleanly once skipped

    std::map<int, long long> leftoverBitsHistogramInRange;    // for zeroOutOfRangeCount records
    std::map<int, long long> leftoverBitsHistogramOutOfRange;  // for records with >=1 out-of-range index
    std::map<int, long long> outOfRangeValueHistogram;
    std::map<int, long long> outOfRangePositionHistogram;
    long long totalOutOfRangeHits = 0;

    // Population-wide validation of the "leading per-packet (sampleCount,
    // byteSize) table" finding: for every real record, sum the FIRST u16
    // field across the detected clean-run table entries and compare it to
    // the vorb chunk's own INDEPENDENT sample-count field. A ratio close to
    // 1.0 across the whole population (not just a couple of hand-picked
    // examples) is the real, falsifiable population-level confirmation.
    long long ratioRecordCount = 0;
    double ratioSum = 0.0, ratioMin = 1e18, ratioMax = -1e18;
    int ratioHistogram[11] = {0};  // buckets: <0.5,0.5-0.6,...,1.4-1.5,>1.5 (11 buckets, 0.1 wide from 0.5 to 1.5)

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
            sr3audioruntime::StandardWaveFmt fmt = sr3audioruntime::parseStandardWaveFmt(recBytes, chunks.fmtOffset, chunks.fmtSize);
            if (!fmt.present || fmt.channels == 0) continue;

            auto framed = sr3audioruntime::walkFramedPackets(recBytes, chunks.dataOffset, chunks.dataSize);
            if (framed.packets.empty()) continue;

            ++recordsWithVorb;

            // Population-wide check of the leading-table finding (see
            // ratioHistogram's own comment above): sum the table's first
            // u16 field over its detected clean run and compare to vorb's
            // own independent sample-count field, for THIS record.
            {
                size_t cleanEndPop = findCleanRunEnd(recBytes + chunks.dataOffset, chunks.dataSize);
                if (cleanEndPop >= 16 && vorb.sampleCountFallbackField > 0) {
                    uint64_t sumFirst = 0;
                    for (size_t e = 0; e * 4 < cleanEndPop; ++e) {
                        size_t off = e * 4;
                        uint16_t v0 = static_cast<uint16_t>(recBytes[chunks.dataOffset + off]) |
                                      (static_cast<uint16_t>(recBytes[chunks.dataOffset + off + 1]) << 8);
                        sumFirst += v0;
                    }
                    double ratio = static_cast<double>(sumFirst) / static_cast<double>(vorb.sampleCountFallbackField);
                    ++ratioRecordCount;
                    ratioSum += ratio;
                    ratioMin = std::min(ratioMin, ratio);
                    ratioMax = std::max(ratioMax, ratio);
                    int bucket = static_cast<int>((ratio - 0.5) / 0.1);
                    if (bucket < 0) bucket = 0;
                    if (bucket > 10) bucket = 10;
                    ++ratioHistogram[bucket];
                }
            }

            sr3audioruntime::SetupBudgetProbeResult probe = sr3audioruntime::probeSetupPacketBitBudget(
                recBytes + framed.packets[0].offset, framed.packets[0].length, fmt.channels);

            totalOutOfRangeHits += probe.outOfRangeCount;
            for (int v : probe.outOfRangeIndices) ++outOfRangeValueHistogram[v];
            for (int p : probe.outOfRangePositions) ++outOfRangePositionHistogram[p];

            if (!probe.ranToEnd) {
                ++didNotRunToEndCount;
                continue;
            }
            ++ranToEndCount;
            const int leftover = static_cast<int>(probe.leftoverBits);
            if (leftover >= 0 && leftover <= 7) ++ranToEndCleanCount;

            if (probe.outOfRangeCount == 0) {
                ++zeroOutOfRangeCount;
                ++leftoverBitsHistogramInRange[leftover];
                if (zeroOutOfRangeCount <= 2) {
                    std::cout << "[ZERO-OOR] " << n << " rec@" << rec.offset
                              << " fmt.formatTag=" << fmt.formatTag << " fmt.channels=" << fmt.channels
                              << " fmt.samplesPerSec=" << fmt.samplesPerSec << " fmt.avgBytesPerSec=" << fmt.avgBytesPerSec
                              << " fmt.blockAlign=" << fmt.blockAlign << " fmt.bitsPerSample=" << fmt.bitsPerSample
                              << " fmtSize=" << chunks.fmtSize
                              << " recSize=" << rec.size
                              << " dataChunkSize=" << chunks.dataSize << " stoppedAt=" << framed.stoppedAtOffset
                              << " setupPacketBytes=" << framed.packets[0].length << " framingConsistent=" << framed.consistent
                              << " totalFramedPackets=" << framed.packets.size() << " books=" << probe.books
                              << " floors=" << probe.floors << " residues=" << probe.residues
                              << " maps=" << probe.maps << " modes=" << probe.modes
                              << " leftoverBits=" << probe.leftoverBits
                              << " consumedBits=" << (framed.packets[0].length * 8 - probe.leftoverBits) << "\n";
                    std::cout << "    dataChunk first 24 bytes: " << hexDump(recBytes + chunks.dataOffset, 24) << "\n";
                    std::cout << "    dataChunk first 512 bytes: " << hexDump(recBytes + chunks.dataOffset, std::min<size_t>(512, chunks.dataSize)) << "\n";
                    uint64_t sumByteSize = 0;
                    long tableLen = findSeekTableLength(recBytes + chunks.dataOffset, chunks.dataSize, sumByteSize);
                    std::cout << "    seek-table-hypothesis: tableLen=" << tableLen << " sumByteSize=" << sumByteSize
                              << " vorb.sampleCountFallback=" << vorb.sampleCountFallbackField << "\n";
                    std::cout << "    cleanliness profile (32-byte blocks, first 2048 bytes):\n";
                    profileCleanliness(recBytes + chunks.dataOffset, std::min<size_t>(2048, chunks.dataSize), 32, 64);
                    size_t cleanEnd = findCleanRunEnd(recBytes + chunks.dataOffset, chunks.dataSize);
                    std::cout << "    clean run ends at byte offset " << cleanEnd << " within the data chunk ("
                              << (cleanEnd / 4) << " entries)\n";
                    std::cout << "    all RIFF chunks: " << listRiffChunks(recBytes, rec.size) << "\n";
                    if (cleanEnd + 16 < chunks.dataSize) {
                        std::cout << "    bytes around the clean-run end: " << hexDump(recBytes + chunks.dataOffset + cleanEnd - 8, 32) << "\n";
                        uint64_t sumSecondField = 0, sumFirstField = 0;
                        for (size_t e = 0; e * 4 < cleanEnd; ++e) {
                            size_t off = e * 4;
                            uint16_t v0 = static_cast<uint16_t>(recBytes[chunks.dataOffset + off]) |
                                          (static_cast<uint16_t>(recBytes[chunks.dataOffset + off + 1]) << 8);
                            uint16_t v1 = static_cast<uint16_t>(recBytes[chunks.dataOffset + off + 2]) |
                                          (static_cast<uint16_t>(recBytes[chunks.dataOffset + off + 3]) << 8);
                            sumFirstField += v0;
                            sumSecondField += v1;
                        }
                        std::cout << "    sum of 1st-u16 fields=" << sumFirstField << " sum of 2nd-u16 fields=" << sumSecondField
                                  << " remaining-after-table=" << (chunks.dataSize - cleanEnd)
                                  << " vorb.sampleCountFallback=" << vorb.sampleCountFallbackField << "\n";
                        auto afterTable = sr3audioruntime::probeSetupPacketBitBudget(
                            recBytes + chunks.dataOffset + cleanEnd, chunks.dataSize - cleanEnd, fmt.channels);
                        std::cout << "    PARSE-AFTER-TABLE: books=" << afterTable.books << " floors=" << afterTable.floors
                                  << " residues=" << afterTable.residues << " maps=" << afterTable.maps
                                  << " modes=" << afterTable.modes << " outOfRangeCount=" << afterTable.outOfRangeCount
                                  << " ranToEnd=" << afterTable.ranToEnd << " leftoverBits=" << afterTable.leftoverBits << "\n";

                        // Hypothesis: the real setup packet is NOT at the
                        // front - it is a trailing region of
                        // (remaining-after-table - sumSecondField) bytes at
                        // the very END of "data", separate from the regular
                        // per-packet payload pool the table describes.
                        if (sumSecondField < chunks.dataSize - cleanEnd) {
                            size_t trailingLen = (chunks.dataSize - cleanEnd) - sumSecondField;
                            size_t trailingStart = chunks.dataOffset + chunks.dataSize - trailingLen;
                            std::cout << "    trailing-setup-packet-hypothesis: trailingLen=" << trailingLen
                                      << " trailingStart(rel data)=" << (trailingStart - chunks.dataOffset) << "\n";
                            std::cout << "    trailing bytes first 24: " << hexDump(recBytes + trailingStart, 24) << "\n";
                            auto trailing = sr3audioruntime::probeSetupPacketBitBudget(
                                recBytes + trailingStart, trailingLen, fmt.channels);
                            std::cout << "    PARSE-TRAILING: books=" << trailing.books << " floors=" << trailing.floors
                                      << " residues=" << trailing.residues << " maps=" << trailing.maps
                                      << " modes=" << trailing.modes << " outOfRangeCount=" << trailing.outOfRangeCount
                                      << " ranToEnd=" << trailing.ranToEnd << " leftoverBits=" << trailing.leftoverBits << "\n";
                        }
                    }
                }
            } else {
                ++leftoverBitsHistogramOutOfRange[leftover];
                if (leftover >= 0 && leftover <= 7) ++outOfRangeButCleanCount;
                static long long oorPrinted = 0;
                if (oorPrinted < 2) {
                    ++oorPrinted;
                    std::cout << "[OOR] " << n << " rec@" << rec.offset
                              << " fmt.formatTag=" << fmt.formatTag << " fmt.channels=" << fmt.channels
                              << " fmt.samplesPerSec=" << fmt.samplesPerSec << " fmt.avgBytesPerSec=" << fmt.avgBytesPerSec
                              << " fmt.blockAlign=" << fmt.blockAlign << " fmt.bitsPerSample=" << fmt.bitsPerSample
                              << " fmtSize=" << chunks.fmtSize
                              << " recSize=" << rec.size
                              << " dataChunkSize=" << chunks.dataSize << " stoppedAt=" << framed.stoppedAtOffset
                              << " setupPacketBytes=" << framed.packets[0].length << " framingConsistent=" << framed.consistent
                              << " totalFramedPackets=" << framed.packets.size() << " books=" << probe.books
                              << " floors=" << probe.floors << " residues=" << probe.residues
                              << " maps=" << probe.maps << " modes=" << probe.modes
                              << " oorCount=" << probe.outOfRangeCount << " firstOorPos=" << probe.outOfRangePositions[0]
                              << " firstOorVal=" << probe.outOfRangeIndices[0]
                              << " leftoverBits=" << probe.leftoverBits
                              << " consumedBits=" << (framed.packets[0].length * 8 - probe.leftoverBits) << "\n";
                    std::cout << "    dataChunk first 24 bytes: " << hexDump(recBytes + chunks.dataOffset, 24) << "\n";
                    std::cout << "    dataChunk first 512 bytes: " << hexDump(recBytes + chunks.dataOffset, std::min<size_t>(512, chunks.dataSize)) << "\n";
                    uint64_t sumByteSize = 0;
                    long tableLen = findSeekTableLength(recBytes + chunks.dataOffset, chunks.dataSize, sumByteSize);
                    std::cout << "    seek-table-hypothesis: tableLen=" << tableLen << " sumByteSize=" << sumByteSize
                              << " vorb.sampleCountFallback=" << vorb.sampleCountFallbackField << "\n";
                    std::cout << "    cleanliness profile (32-byte blocks, first 2048 bytes):\n";
                    profileCleanliness(recBytes + chunks.dataOffset, std::min<size_t>(2048, chunks.dataSize), 32, 64);
                    size_t cleanEnd = findCleanRunEnd(recBytes + chunks.dataOffset, chunks.dataSize);
                    std::cout << "    clean run ends at byte offset " << cleanEnd << " within the data chunk ("
                              << (cleanEnd / 4) << " entries)\n";
                    if (cleanEnd + 16 < chunks.dataSize) {
                        uint64_t sumSecondField = 0, sumFirstField = 0;
                        for (size_t e = 0; e * 4 < cleanEnd; ++e) {
                            size_t off = e * 4;
                            uint16_t v0 = static_cast<uint16_t>(recBytes[chunks.dataOffset + off]) |
                                          (static_cast<uint16_t>(recBytes[chunks.dataOffset + off + 1]) << 8);
                            uint16_t v1 = static_cast<uint16_t>(recBytes[chunks.dataOffset + off + 2]) |
                                          (static_cast<uint16_t>(recBytes[chunks.dataOffset + off + 3]) << 8);
                            sumFirstField += v0;
                            sumSecondField += v1;
                        }
                        std::cout << "    sum of 1st-u16 fields=" << sumFirstField << " sum of 2nd-u16 fields=" << sumSecondField
                                  << " remaining-after-table=" << (chunks.dataSize - cleanEnd)
                                  << " vorb.sampleCountFallback=" << vorb.sampleCountFallbackField << "\n";
                        if (sumSecondField < chunks.dataSize - cleanEnd) {
                            size_t trailingLen = (chunks.dataSize - cleanEnd) - sumSecondField;
                            size_t trailingStart = chunks.dataOffset + chunks.dataSize - trailingLen;
                            std::cout << "    trailing-setup-packet-hypothesis: trailingLen=" << trailingLen
                                      << " trailingStart(rel data)=" << (trailingStart - chunks.dataOffset) << "\n";
                            auto trailing = sr3audioruntime::probeSetupPacketBitBudget(
                                recBytes + trailingStart, trailingLen, fmt.channels);
                            std::cout << "    PARSE-TRAILING: books=" << trailing.books << " floors=" << trailing.floors
                                      << " residues=" << trailing.residues << " maps=" << trailing.maps
                                      << " modes=" << trailing.modes << " outOfRangeCount=" << trailing.outOfRangeCount
                                      << " ranToEnd=" << trailing.ranToEnd << " leftoverBits=" << trailing.leftoverBits << "\n";
                        }
                    }
                }
            }
        }
    }

    std::cout << "records with a 'vorb' chunk                           : " << recordsWithVorb << "\n";
    std::cout << "ran all the way to the end of the packet (no EOF)     : " << ranToEndCount << "\n";
    std::cout << "  of those, closed with <=7 leftover bits (clean)     : " << ranToEndCleanCount << "\n";
    std::cout << "did NOT reach the end (genuine EOF somewhere)         : " << didNotRunToEndCount << "\n";
    std::cout << "records with ZERO out-of-range codebook indices       : " << zeroOutOfRangeCount << "\n";
    std::cout << "records with >=1 out-of-range index, but STILL closed cleanly (<=7 leftover bits): "
              << outOfRangeButCleanCount << "\n";
    std::cout << "total out-of-range index occurrences across population: " << totalOutOfRangeHits << "\n";

    std::cout << "\nleftover-bits histogram, records with ZERO out-of-range indices:\n";
    for (const auto& kv : leftoverBitsHistogramInRange) std::cout << "  leftover=" << kv.first << " bits: " << kv.second << " records\n";

    std::cout << "\nleftover-bits histogram, records with >=1 out-of-range index (skipped+continued):\n";
    for (const auto& kv : leftoverBitsHistogramOutOfRange) std::cout << "  leftover=" << kv.first << " bits: " << kv.second << " records\n";

    std::cout << "\ntop out-of-range VALUES (value: occurrence count):\n";
    {
        std::vector<std::pair<int, long long>> sorted(outOfRangeValueHistogram.begin(), outOfRangeValueHistogram.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < sorted.size() && i < 25; ++i) std::cout << "  " << sorted[i].first << ": " << sorted[i].second << "\n";
    }

    std::cout << "\nPOPULATION-WIDE check of the leading per-packet-table finding (sum of table's first u16\n";
    std::cout << "field over its detected clean run, divided by vorb's own independent sample-count field):\n";
    std::cout << "  records with a usable clean run: " << ratioRecordCount << "/" << recordsWithVorb << "\n";
    if (ratioRecordCount > 0) {
        std::cout << "  mean ratio=" << (ratioSum / ratioRecordCount) << " min=" << ratioMin << " max=" << ratioMax << "\n";
    }
    for (int i = 0; i < 11; ++i) {
        std::cout << "  ratio in [" << (0.5 + i * 0.1) << "," << (0.5 + (i + 1) * 0.1) << "): " << ratioHistogram[i] << "\n";
    }

    std::cout << "\nout-of-range POSITION histogram (0-based codebook-slot position : occurrence count):\n";
    {
        std::vector<std::pair<int, long long>> sorted(outOfRangePositionHistogram.begin(), outOfRangePositionHistogram.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < sorted.size() && i < 25; ++i) std::cout << "  pos " << sorted[i].first << ": " << sorted[i].second << "\n";
    }

    return 0;
}
