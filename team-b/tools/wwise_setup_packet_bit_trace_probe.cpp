// DIAGNOSTIC-ONLY tool (2026-10-08, vorb-chunk setup/audio-offset-pair
// re-hypothesis task follow-up, NOT a ctest - same "one-shot real-data
// probe" convention as every other tools/wwise_*_probe.cpp in this tree).
//
// Now that tools/wwise_vorb_setup_audio_dword_probe.cpp has established the
// NEW offset formula (setupPacketStart = dataChunkOffset + vorb[16:20),
// firstAudioPacketStart = dataChunkOffset + vorb[20:24)) holds EXACTLY,
// 493/493, including the parameter-free structural invariant
// (setupPacketStart + 2 + declaredLen == firstAudioPacketStart, EXACT for
// every record) and the complete elimination of the previously-reported
// out-of-range-codebook-index problem (0/493 now, was 488/493 before the
// offset fix) - the ONE remaining failure is uniform across the whole
// population: every record's setup-packet bitstream parse (via the
// already-existing sr3audioruntime::probeSetupPacketBitBudget()) hits EOF
// at the EXACT SAME place, "residue 0/2, EOF reading booklist entry".
//
// This tool re-walks ONE real record's setup-packet bitstream by hand,
// field by field, printing the bit position and value at every step, using
// the exact same field grammar wwise_setup_rebuild.cpp's own
// transcodeOneCodebook()/transcodeOneFloor()/transcodeOneResidue()
// implement (this project's OWN code, re-derived here only for verbose
// diagnostic visibility - not external disassembly, not a rewrite of the
// production logic) - to find exactly which section/field is consuming the
// wrong number of bits and shifting everything downstream.
//
// Usage: wwise_setup_packet_bit_trace_probe <path to cutscene_sounds.vpp_pc> [recordName] [recordIndex]

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <ogg/ogg.h>

#include "sr3audio/media_bank.h"
#include "sr3audioruntime/wwise_codebook_library.h"
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

long bitPos(const oggpack_buffer& b) { return static_cast<long>(b.endbyte) * 8 + static_cast<long>(b.endbit); }
long bitsLeft(const oggpack_buffer& b) { return static_cast<long>(b.storage) * 8 - bitPos(b); }

long rd(oggpack_buffer& b, int bits, const char* label) {
    long before = bitPos(b);
    long v = oggpack_read(&b, bits);
    std::cout << "  [bit " << before << "..+" << bits << "] " << label << " = " << v
              << "  (bitsLeft=" << bitsLeft(b) << ")\n";
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (!sr3audioruntime::isWwiseCodebookLibraryDataAvailable()) {
        // Public build without the excluded compiled-in codebook data (see
        // include/sr3audioruntime/wwise_codebook_library.h) - nothing
        // meaningful to trace against a zeroed placeholder library.
        std::cerr << "wwise codebook library data not loaded (public build without the excluded "
                     "compiled-in codebook data) - nothing to trace\n";
        return 1;
    }
    const std::string archivePath = argc > 1 ? argv[1]
        : "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/cutscene_sounds.vpp_pc";
    const std::string wantName = argc > 2 ? argv[2] : "ct_angel_01_media.bnk_pc";
    const int wantIndex = argc > 3 ? std::atoi(argv[3]) : 0;

    std::vector<uint8_t> archiveBytes = readFile(archivePath);
    if (archiveBytes.empty()) {
        std::cerr << "could not read " << archivePath << "\n";
        return 1;
    }
    vpp::Container c(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));

    int seen = 0;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (!endsWith(n, wantName)) continue;

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
        } catch (const std::exception& e) {
            std::cerr << "MediaBank::parse failed: " << e.what() << "\n";
            continue;
        }
        const size_t declaredSize = c.entries()[i].payload.decompressedLength;
        std::vector<sr3audio::MediaBankRecord> records;
        try {
            records = mb.walk(declaredSize);
        } catch (const std::exception& e) {
            std::cerr << "MediaBank::walk failed: " << e.what() << "\n";
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

            if (seen != wantIndex) { ++seen; continue; }
            ++seen;

            const uint32_t seekTableLen = u32le(vorb.raw42, 16);
            const uint32_t audioOffsetField = u32le(vorb.raw42, 20);
            const size_t setupPacketStart = chunks.dataOffset + seekTableLen;
            std::cout << "(firstAudioPacketStart(rel data)=" << audioOffsetField << ", informational only)\n";
            const uint16_t declaredLen = static_cast<uint16_t>(recBytes[setupPacketStart]) |
                                          static_cast<uint16_t>(static_cast<uint16_t>(recBytes[setupPacketStart + 1]) << 8);
            const size_t payloadOffset = setupPacketStart + 2;

            std::cout << "=== " << n << " rec@" << rec.offset << " channels=" << fmt.channels
                      << " declaredLen=" << declaredLen << " (" << (declaredLen * 8) << " bits) ===\n";

            oggpack_buffer in;
            oggpack_readinit(&in, const_cast<unsigned char*>(recBytes + payloadOffset), declaredLen);

            long booksRaw = rd(in, 8, "booksRaw");
            long books = booksRaw + 1;
            std::cout << "books = " << books << "\n";
            for (long b = 0; b < books; ++b) {
                long idx = rd(in, 10, "codebook_index");
                if (idx < 0 || idx >= sr3audioruntime::kWwiseCodebookLibrarySize) {
                    std::cout << "  ** OUT OF RANGE codebook index " << idx << " at slot " << b << " **\n";
                    continue;
                }
                const auto& e = sr3audioruntime::kWwiseCodebookLibraryIndex[idx];
                std::cout << "    -> library[" << idx << "]: dim=" << e.dim << " entries=" << e.entries
                          << " ordered=" << e.ordered << " hasLookup=" << e.hasLookup << "\n";
            }

            std::cout << "-- time-domain-transform placeholder: NOT read from input (synthesized) --\n";

            long floorsRaw = rd(in, 6, "floorsRaw");
            long floors = floorsRaw + 1;
            std::cout << "floors = " << floors << "\n";
            for (long fl = 0; fl < floors; ++fl) {
                std::cout << " floor " << fl << ":\n";
                long partitions = rd(in, 5, "  partitions");
                std::vector<long> partitionClass(static_cast<size_t>(partitions));
                int maxClass = -1;
                for (long j = 0; j < partitions; ++j) {
                    long pc = rd(in, 4, "  partition_class");
                    partitionClass[static_cast<size_t>(j)] = pc;
                    if (pc > maxClass) maxClass = static_cast<int>(pc);
                }
                std::vector<long> classDim(static_cast<size_t>(maxClass < 0 ? 0 : maxClass + 1), 0);
                for (int j = 0; j <= maxClass; ++j) {
                    long cdimRaw = rd(in, 3, "  class_dim_raw");
                    classDim[static_cast<size_t>(j)] = cdimRaw + 1;
                    long csub = rd(in, 2, "  class_subclasses");
                    if (csub != 0) {
                        rd(in, 8, "  class_book");
                    }
                    long subCount = 1L << csub;
                    for (long k = 0; k < subCount; ++k) rd(in, 8, "  class_subbook");
                }
                long mult = rd(in, 2, "  mult");
                long rangebits = rd(in, 4, "  rangebits");
                long k = 0, countAcc = 0;
                for (long j = 0; j < partitions; ++j) {
                    countAcc += classDim[static_cast<size_t>(partitionClass[static_cast<size_t>(j)])];
                    for (; k < countAcc; ++k) rd(in, static_cast<int>(rangebits), "  postlist_entry");
                }
                (void)mult;
            }

            long residuesRaw = rd(in, 6, "residuesRaw");
            long residues = residuesRaw + 1;
            std::cout << "residues = " << residues << "\n";
            for (long r = 0; r < residues; ++r) {
                std::cout << " residue " << r << ":\n";
                rd(in, 2, "  residue_type (FIXED - vgmstream agrees-with-public-format)");
                rd(in, 24, "  begin");
                rd(in, 24, "  end");
                rd(in, 24, "  grouping");
                long partitionsRaw = rd(in, 6, "  partitionsRaw");
                long partitions = partitionsRaw + 1;
                rd(in, 8, "  groupbook");
                std::vector<long> secondstages(static_cast<size_t>(partitions));
                long acc = 0;
                for (long j = 0; j < partitions; ++j) {
                    long cascadeLow = rd(in, 3, "  cascade_low");
                    long cflag = rd(in, 1, "  cascade_flag");
                    long cascadeHigh = 0;
                    if (cflag) cascadeHigh = rd(in, 5, "  cascade_high");
                    long cascade = cascadeLow | (cflag ? (cascadeHigh << 3) : 0);
                    secondstages[static_cast<size_t>(j)] = cascade;
                    int ic = 0;
                    unsigned int cc = static_cast<unsigned int>(cascade);
                    while (cc) { ic += (cc & 1u); cc >>= 1; }
                    acc += ic;
                    std::cout << "    partition " << j << ": cascade=" << cascade << " icount=" << ic
                              << " runningBooklistTotal=" << acc << "\n";
                }
                std::cout << "  total booklist entries needed = " << acc << " (" << (acc * 8) << " bits), bitsLeft="
                          << bitsLeft(in) << "\n";
                for (long j = 0; j < acc; ++j) {
                    if (bitsLeft(in) < 8) {
                        std::cout << "  ** EOF at booklist entry " << j << "/" << acc << " (bitsLeft=" << bitsLeft(in) << ") **\n";
                        break;
                    }
                    rd(in, 8, "  booklist_entry");
                }
            }

            long mapsRaw = rd(in, 6, "mapsRaw");
            long maps = mapsRaw + 1;
            std::cout << "maps = " << maps << "\n";
            for (long m = 0; m < maps; ++m) {
                std::cout << " mapping " << m << ":\n";
                long hasSubmaps = rd(in, 1, "  hasSubmaps");
                long submaps = 1;
                if (hasSubmaps) {
                    long submapsRaw = rd(in, 4, "  submapsRaw");
                    submaps = submapsRaw + 1;
                }
                std::cout << "  submaps = " << submaps << "\n";
                long hasCoupling = rd(in, 1, "  hasCoupling");
                if (hasCoupling) {
                    long couplingRaw = rd(in, 8, "  couplingRaw");
                    long couplingSteps = couplingRaw + 1;
                    int cbits = 0;
                    { uint32_t v = static_cast<uint32_t>(fmt.channels > 0 ? fmt.channels - 1 : 0); while (v) { ++cbits; v >>= 1; } }
                    for (long cs = 0; cs < couplingSteps; ++cs) {
                        if (cbits > 0) rd(in, cbits, "  coupling_mag");
                        if (cbits > 0) rd(in, cbits, "  coupling_ang");
                    }
                }
                rd(in, 2, "  reserved");
                if (submaps > 1) {
                    for (int ch = 0; ch < fmt.channels; ++ch) rd(in, 4, "  chmux");
                }
                for (long sm = 0; sm < submaps; ++sm) {
                    std::cout << "  submap " << sm << " (bit " << bitPos(in) << "):\n";
                    rd(in, 8, "    time_config (NEW - vgmstream: 'passed through as unused placeholder')");
                    long floorsub = rd(in, 8, "    floorsub");
                    long residuesub = rd(in, 8, "    residuesub");
                    std::cout << "    floorsub valid(<floors=" << floors << ")? " << (floorsub < floors)
                              << "  residuesub valid(<residues=" << residues << ")? " << (residuesub < residues) << "\n";
                }
            }

            long modesRaw = rd(in, 6, "modesRaw");
            long modes = modesRaw + 1;
            std::cout << "modes = " << modes << "\n";
            for (long md = 0; md < modes; ++md) {
                rd(in, 1, " blockflag");
                long modeMapping = rd(in, 8, " mode_mapping");
                std::cout << " mode_mapping valid(<maps=" << maps << ")? " << (modeMapping < maps) << "\n";
            }
            std::cout << "=== END OF SECTIONS: bitPos=" << bitPos(in) << " declaredLenBits=" << (declaredLen * 8)
                      << " leftoverBits=" << bitsLeft(in) << " ===\n";
            return 0;
        }
    }
    std::cerr << "record not found (wantName=" << wantName << " wantIndex=" << wantIndex << ", saw " << seen << ")\n";
    return 1;
}
