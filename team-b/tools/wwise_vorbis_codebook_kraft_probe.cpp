// DIAGNOSTIC-ONLY tool (2026-10-09, 488/493 codebook-count investigation
// task), NOT a ctest, NOT wired into the real decode path - same "one-shot
// real-data probe" convention as every other tools/wwise_*_probe.cpp on
// this branch.
//
// Purpose: wwise_setup_rebuild.h's own "STILL genuinely OPEN" note already
// narrowed the 488/493 vorbis_synthesis_init() failure to
// vorbis_book_init_decode()'s own Kraft-inequality check
// (third_party/libvorbis/lib/sharedbook.c's `_make_words()`, "the lengths
// must specify an overpopulated tree") on every rebuilt.codebookCount==32
// (442 records) or ==34 (46 records) setup, but never actually located
// WHICH codebook-library slot(s) fail that check, or confirmed they are
// the same slots driving the real population's failures. This tool answers
// that directly, in two independent passes:
//
//   MODE "sweep": for all 599 real library slots (wwise_codebook_library.h),
//   parse the slot standalone (parseOneCodebookForCrossCheck(), this
//   project's own already-cross-checked transcode) and feed its resolved
//   dim/entries/lengths directly into the REAL, vendored
//   vorbis_book_init_decode() (third_party/libvorbis/lib/sharedbook.c,
//   linked, not reimplemented) with a minimal local `static_codebook`
//   (maptype=0 - the Kraft check only ever consumes dim/entries/
//   lengthlist, never the lookup-table fields). Reports every slot that
//   fails, by index, with its dim/entries/ordered/usedEntries - a
//   standalone, record-independent answer to "is this a library-DATA
//   problem" (the check is a pure function of one codebook's own lengths
//   list; if a slot fails here, it will fail in EVERY record that ever
//   selects it, unconditionally).
//
//   MODE "record <namefilter>": walks the real archive exactly like
//   tools/wwise_vorbis_packet_divergence_probe.cpp, for every matching
//   "_media.bnk_pc" record with a 'vorb' chunk: runs the exact same
//   id/comment/setup header-accept sequence attemptWwiseVorbisDecode()
//   uses, then (a) re-reads the stripped setup packet's own per-codebook
//   10-bit index list directly (duplicating rebuildVorbisSetupHeader()'s
//   own outer-loop read, not modifying it) to recover which LIBRARY SLOT
//   sits at each position, (b) reaches into the REAL, already-parsed
//   `vorbis_info::codec_setup` (populated by libvorbis's OWN
//   `_vorbis_unpack_books`, info.c, when vorbis_synthesis_headerin accepted
//   the rebuilt setup header - not this project's own transcode output) and
//   calls vorbis_book_init_decode() per position directly, isolating which
//   position(s) fail independent of vorbis_synthesis_init()'s own
//   all-or-nothing gate. Cross-references position -> library slot ->
//   sweep-mode pass/fail, and dumps the record's raw bytes (as a renamed
//   .wem) for independent oracle (vgmstream-cli) testing.
//
// Usage:
//   wwise_vorbis_codebook_kraft_probe sweep
//   wwise_vorbis_codebook_kraft_probe record <archive> <outDir> [nameFilter] [codebookCountFilter]
//
// `codebookCountFilter`, if given, probes (and dumps raw bytes for) EVERY
// matching record whose rebuilt.codebookCount equals that value (not just
// the first) - used both to pick one representative 32-book/34-book record
// for independent oracle testing, exactly as this task's own brief asks
// for, and to tally the per-position/per-slot failure pattern across the
// ENTIRE real population in each bucket (every record, not a sample).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

// Vendored libvorbis's OWN private header for static_codebook/codebook and
// the extern vorbis_book_init_decode() - read/used directly for exactly the
// purpose this project's own existing comments already establish elsewhere
// (e.g. wwise_setup_rebuild.h's own header note): this is the real,
// open-source decoder library's own internal structure, not game-derived
// data, and not modified here, only read via extern linkage. codebook.h
// itself is safe to include from C++ (only depends on <ogg/ogg.h>) -
// codec_internal.h is NOT (see tools/wwise_vorbis_codec_setup_bridge.c's
// own header comment for why that one small reach-in is a separate,
// C-compiled translation unit instead). codebook.h has no `__cplusplus`
// guard of its own (it is a private header never meant to be included from
// C++ at all) - its extern function decls must be wrapped here, or the
// C++ compiler mangles their names and the real, C-linkage symbols compiled
// into vorbisstatic (sharedbook.c) fail to link.
extern "C" {
#include "codebook.h"  // static_codebook / codebook / vorbis_book_init_decode (extern)
}

#include "sr3audio/media_bank.h"
#include "sr3audioruntime/wwise_codebook_library.h"
#include "sr3audioruntime/wwise_setup_rebuild.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"
#include "vpp/container.h"

using namespace sr3audioruntime;

// Implemented in tools/wwise_vorbis_codec_setup_bridge.c (compiled as C, not
// C++ - see that file's own header comment). Reaches into a real,
// already-libvorbis-unpacked `vorbis_info::codec_setup` to isolate which
// codebook POSITION fails vorbis_book_init_decode, using libvorbis's own
// parsed static_codebook for that position.
extern "C" {
int sr3_probe_codec_setup_books(vorbis_info* vi);
int sr3_probe_book_init_decode(vorbis_info* vi, int index, long* dimOut, long* entriesOut, int* usedEntriesOut);
}

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

bool writeRawBlob(const std::string& path, const uint8_t* data, size_t size) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return f.good();
}

// Builds a minimal local static_codebook from already-parsed dim/entries/
// lengths and runs the REAL vorbis_book_init_decode() on it - standalone,
// no setup-header/record context needed. maptype is deliberately left 0
// (no lookup table): _make_words()/the Kraft check (sharedbook.c) consumes
// ONLY entries + lengthlist, never touches maptype/quantlist/q_min/q_delta,
// so this is a faithful, isolated test of exactly the one check in question.
bool kraftCheckOnly(long dim, long entries, const std::vector<long>& lengths, int* usedEntriesOut) {
    std::vector<char> lengthsChar(static_cast<size_t>(entries), 0);
    int used = 0;
    for (long i = 0; i < entries && i < static_cast<long>(lengths.size()); ++i) {
        long L = lengths[static_cast<size_t>(i)];
        lengthsChar[static_cast<size_t>(i)] = static_cast<char>(L);
        if (L > 0) ++used;
    }
    if (usedEntriesOut) *usedEntriesOut = used;

    static_codebook sc;
    memset(&sc, 0, sizeof(sc));
    sc.dim = dim;
    sc.entries = entries;
    sc.lengthlist = lengthsChar.data();
    sc.maptype = 0;
    sc.allocedp = 0;

    codebook c;
    memset(&c, 0, sizeof(c));
    int rc = vorbis_book_init_decode(&c, &sc);
    if (rc == 0) vorbis_book_clear(&c);
    return rc == 0;
}

void modeSweep() {
    std::cout << "=== MODE sweep: all " << kWwiseCodebookLibrarySize
              << " real codebook-library slots, standalone Kraft-inequality check ===\n";
    if (!verifyWwiseCodebookLibraryBlobSha256()) {
        std::cout << "FATAL: codebook library blob SHA-256 mismatch - refusing to probe possibly-corrupted data\n";
        return;
    }
    int passCount = 0, failCount = 0, parseFailCount = 0;
    std::vector<int> failingSlots;
    for (int slot = 0; slot < kWwiseCodebookLibrarySize; ++slot) {
        const WwiseCodebookLibraryEntry& e = kWwiseCodebookLibraryIndex[slot];
        CodebookParseInfo info;
        std::string err;
        bool ok = parseOneCodebookForCrossCheck(kWwiseCodebookLibraryBlob + e.blobOffset, e.byteLen, info, err);
        if (!ok) {
            ++parseFailCount;
            std::cout << "slot " << slot << ": PARSE FAILED (" << err << ") - cannot run Kraft check\n";
            continue;
        }
        int usedEntries = 0;
        bool kraftOk = kraftCheckOnly(info.dim, info.entries, info.lengths, &usedEntries);
        if (kraftOk) {
            ++passCount;
        } else {
            ++failCount;
            failingSlots.push_back(slot);
            double kraftSum = 0.0;
            for (long L : info.lengths)
                if (L > 0) kraftSum += std::pow(2.0, -static_cast<double>(L));
            std::cout << std::setprecision(12);
            std::cout << "slot " << slot << ": KRAFT-INEQUALITY FAIL (vorbis_book_init_decode rc!=0) dim="
                      << info.dim << " entries=" << info.entries << " ordered=" << info.ordered
                      << " usedEntries=" << usedEntries << " maptype=" << info.maptype << " kraftSum=" << kraftSum
                      << " (>1.0 = overpopulated/invalid prefix code, <1.0 with >1 used entries = underpopulated)"
                      << " lengths=[";
            for (size_t li = 0; li < info.lengths.size(); ++li) std::cout << info.lengths[li] << (li + 1 < info.lengths.size() ? "," : "");
            std::cout << "]\n";
        }
    }
    std::cout << "SWEEP SUMMARY: " << passCount << "/" << kWwiseCodebookLibrarySize
              << " slots pass vorbis_book_init_decode standalone, " << failCount << " FAIL, " << parseFailCount
              << " could not even be parsed.\n";
    std::cout << "Failing slot list (" << failingSlots.size() << "): ";
    for (int s : failingSlots) std::cout << s << " ";
    std::cout << "\n";
}

struct RecordProbeOutcome {
    bool reached = false;
    int codebookCount = 0;
    std::vector<int> librarySlotsByPosition;  // position i -> library slot index
    std::vector<bool> positionPassedInitDecode;
    int firstFailPosition = -1;
};

RecordProbeOutcome probeOneRecord(const uint8_t* recBytes, size_t recSize) {
    RecordProbeOutcome out;
    VorbisContainerChunks chunks = parseRiffWaveContainer(recBytes, recSize);
    if (!chunks.valid || !chunks.hasFmt || !chunks.hasData) return out;
    StandardWaveFmt fmt = parseStandardWaveFmt(recBytes, chunks.fmtOffset, chunks.fmtSize);
    auto vorbLoc = findRiffSubChunk(recBytes, recSize, kFourCcVorb);
    if (!vorbLoc) return out;
    WwiseVorbChunk vorb = parseVorbChunk(recBytes, recSize, vorbLoc->first);
    if (!vorb.present) return out;

    const uint32_t seekTableLenField =
        static_cast<uint32_t>(vorb.raw42[16]) | (static_cast<uint32_t>(vorb.raw42[17]) << 8) |
        (static_cast<uint32_t>(vorb.raw42[18]) << 16) | (static_cast<uint32_t>(vorb.raw42[19]) << 24);
    const size_t setupPacketStart = chunks.dataOffset + seekTableLenField;
    if (setupPacketStart >= chunks.dataOffset + chunks.dataSize) return out;
    const size_t walkSize = (chunks.dataOffset + chunks.dataSize) - setupPacketStart;
    FramedPacketWalkResult framed = walkFramedPackets(recBytes, setupPacketStart, walkSize);
    if (framed.packets.empty()) return out;

    // (a) Duplicate rebuildVorbisSetupHeader()'s own outer per-codebook
    // 10-bit-index read, READ-ONLY, to recover which library slot sits at
    // each position - not a modification of that function, a separate walk
    // of the same bytes using the same CONFIRMED field widths (8-bit
    // books-count, +1 bias; then `books` 10-bit indices).
    {
        oggpack_buffer idxIn;
        oggpack_readinit(&idxIn, const_cast<unsigned char*>(recBytes + framed.packets[0].offset),
                          static_cast<int>(framed.packets[0].length));
        long booksRaw = oggpack_read(&idxIn, 8);
        if (booksRaw >= 0) {
            long books = booksRaw + 1;
            for (long i = 0; i < books; ++i) {
                long idx = oggpack_read(&idxIn, 10);
                out.librarySlotsByPosition.push_back(static_cast<int>(idx));
            }
        }
    }

    vorbis_info vi;
    vorbis_info_init(&vi);
    vorbis_comment vc;
    vorbis_comment_init(&vc);

    auto writeString = [](oggpack_buffer& b, const char* s, int len) {
        for (int i = 0; i < len; ++i) oggpack_write(&b, static_cast<unsigned long>(static_cast<unsigned char>(s[i])), 8);
    };
    auto finishPacket = [](oggpack_buffer& b) {
        std::vector<uint8_t> o(oggpack_get_buffer(&b), oggpack_get_buffer(&b) + oggpack_bytes(&b));
        oggpack_writeclear(&b);
        return o;
    };
    auto makePacket = [](std::vector<uint8_t>& storage, bool bos, int64_t packetno) {
        ogg_packet op{};
        op.packet = storage.data();
        op.bytes = static_cast<long>(storage.size());
        op.b_o_s = bos ? 1 : 0;
        op.e_o_s = 0;
        op.granulepos = 0;
        op.packetno = packetno;
        return op;
    };

    oggpack_buffer bId;
    oggpack_writeinit(&bId);
    oggpack_write(&bId, 0x01, 8);
    writeString(bId, "vorbis", 6);
    oggpack_write(&bId, 0, 32);
    oggpack_write(&bId, static_cast<unsigned long>(fmt.channels), 8);
    oggpack_write(&bId, fmt.samplesPerSec, 32);
    oggpack_write(&bId, 0, 32);
    oggpack_write(&bId, 0, 32);
    oggpack_write(&bId, 0, 32);
    oggpack_write(&bId, vorb.blockSizeExponent0, 4);
    oggpack_write(&bId, vorb.blockSizeExponent1, 4);
    oggpack_write(&bId, 1, 1);
    std::vector<uint8_t> idPkt = finishPacket(bId);
    ogg_packet opId = makePacket(idPkt, true, 0);
    if (vorbis_synthesis_headerin(&vi, &vc, &opId) != 0) {
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return out;
    }

    oggpack_buffer bC;
    oggpack_writeinit(&bC);
    oggpack_write(&bC, 0x03, 8);
    writeString(bC, "vorbis", 6);
    oggpack_write(&bC, 0, 32);
    oggpack_write(&bC, 0, 32);
    oggpack_write(&bC, 1, 1);
    std::vector<uint8_t> commentPkt = finishPacket(bC);
    ogg_packet opComment = makePacket(commentPkt, false, 1);
    if (vorbis_synthesis_headerin(&vi, &vc, &opComment) != 0) {
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return out;
    }

    SetupRebuildResult rebuilt =
        rebuildVorbisSetupHeader(recBytes + framed.packets[0].offset, framed.packets[0].length, fmt.channels);
    if (!rebuilt.ok) {
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return out;
    }
    out.codebookCount = rebuilt.codebookCount;

    std::vector<uint8_t> setupPkt = rebuilt.standardSetupPacket;
    ogg_packet opSetup = makePacket(setupPkt, false, 2);
    if (vorbis_synthesis_headerin(&vi, &vc, &opSetup) != 0) {
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return out;
    }

    // All 3 header packets accepted - vi.codec_setup now holds the REAL
    // libvorbis-unpacked codec_setup_info (info.c's own
    // `_vorbis_unpack_books`), including book_param[0..books-1], each a
    // fully-parsed `static_codebook*` from libvorbis's OWN reader (not this
    // project's transcode) of the rebuilt standard setup packet. Reach in
    // (via the C bridge, see wwise_vorbis_codec_setup_bridge.c) and call
    // vorbis_book_init_decode() per codebook DIRECTLY, isolating exactly
    // which position fails, instead of only learning "it failed somewhere"
    // from vorbis_synthesis_init()'s own all-or-nothing gate.
    out.reached = true;
    int books = sr3_probe_codec_setup_books(&vi);
    for (int i = 0; i < books; ++i) {
        long dim = 0, entries = 0;
        int used = 0;
        int rc = sr3_probe_book_init_decode(&vi, i, &dim, &entries, &used);
        bool passed = (rc == 0);
        out.positionPassedInitDecode.push_back(passed);
        if (!passed && out.firstFailPosition < 0) out.firstFailPosition = i;
    }

    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    return out;
}

void modeRecord(const std::string& archivePath, const std::string& outDir, const std::string& filter,
                 int codebookCountFilter) {
    std::error_code mkec;
    std::filesystem::create_directories(outDir, mkec);

    std::vector<uint8_t> archiveBytes = readFile(archivePath);
    if (archiveBytes.empty()) {
        std::cerr << "could not read " << archivePath << "\n";
        return;
    }
    vpp::Container c(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));

    // Build the sweep's own failing-slot set first (silently) so each
    // record's report can cross-reference it inline.
    std::set<int> failingSlots;
    for (int slot = 0; slot < kWwiseCodebookLibrarySize; ++slot) {
        const WwiseCodebookLibraryEntry& e = kWwiseCodebookLibraryIndex[slot];
        CodebookParseInfo info;
        std::string err;
        if (!parseOneCodebookForCrossCheck(kWwiseCodebookLibraryBlob + e.blobOffset, e.byteLen, info, err)) continue;
        if (!kraftCheckOnly(info.dim, info.entries, info.lengths, nullptr)) failingSlots.insert(slot);
    }
    std::cout << "(sweep cross-reference: " << failingSlots.size() << " failing library slot(s))\n";

    int matchesDumped = 0;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (!endsWith(n, "_media.bnk_pc")) continue;
        if (!filter.empty() && n.find(filter) == std::string::npos) continue;

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
        int recIdx = -1;
        for (const auto& rec : records) {
            ++recIdx;
            if (rec.size < 128) continue;
            const uint8_t* recBytes = bytes.data() + rec.offset;
            auto vorbLoc = findRiffSubChunk(recBytes, rec.size, kFourCcVorb);
            if (!vorbLoc) continue;

            RecordProbeOutcome res = probeOneRecord(recBytes, rec.size);
            if (!res.reached) continue;
            if (codebookCountFilter >= 0 && res.codebookCount != codebookCountFilter) continue;

            std::cout << "=== " << n << " (bank record #" << recIdx << ") codebookCount=" << res.codebookCount
                      << " ===\n";
            std::cout << "  position -> librarySlot -> initDecode : ";
            int failCount = 0;
            for (size_t p = 0; p < res.librarySlotsByPosition.size(); ++p) {
                int slot = res.librarySlotsByPosition[p];
                bool passed = p < res.positionPassedInitDecode.size() ? res.positionPassedInitDecode[p] : true;
                bool inSweepFailSet = failingSlots.count(slot) > 0;
                if (!passed) ++failCount;
                if (!passed || inSweepFailSet) {
                    std::cout << "[pos=" << p << " slot=" << slot << " initDecodeOk=" << passed
                              << " inSweepFailSet=" << inSweepFailSet << "] ";
                }
            }
            std::cout << "\n  firstFailPosition=" << res.firstFailPosition << " totalFailingPositions=" << failCount
                       << " / " << res.positionPassedInitDecode.size() << " books\n";

            std::string base = n;
            const std::string suffix = ".bnk_pc";
            if (endsWith(base, suffix)) base = base.substr(0, base.size() - suffix.size());
            std::string tag = recIdx == 0 ? base : base + "_rec" + std::to_string(recIdx);
            std::string rawPath =
                outDir + "/raw_cbcount" + std::to_string(res.codebookCount) + "_" + tag + ".wem";
            bool rawOk = writeRawBlob(rawPath, recBytes, rec.size);
            std::cout << "  raw bytes -> '" << rawPath << "' (wrote=" << rawOk << ", size=" << rec.size << ")\n";

            ++matchesDumped;
        }
    }
    std::cout << "modeRecord: " << matchesDumped << " record(s) matched filter/codebookCountFilter and were probed.\n";
}

void modeDumpSlot(int slot) {
    if (slot < 0 || slot >= kWwiseCodebookLibrarySize) {
        std::cerr << "slot out of range\n";
        return;
    }
    const WwiseCodebookLibraryEntry& e = kWwiseCodebookLibraryIndex[slot];
    std::cout << "slot " << slot << ": blobOffset=" << e.blobOffset << " byteLen=" << e.byteLen
              << " dim=" << e.dim << " entries=" << e.entries << " ordered=" << e.ordered
              << " hasLookup=" << e.hasLookup << " valueBitsIfLookup=" << e.valueBitsIfLookup
              << " quantvalsIfLookup=" << e.quantvalsIfLookup << "\n";
    std::cout << "  raw bytes (hex): ";
    for (uint32_t i = 0; i < e.byteLen; ++i) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02x ", kWwiseCodebookLibraryBlob[e.blobOffset + i]);
        std::cout << buf;
    }
    std::cout << "\n  raw bytes (binary, LSb-first per byte, matching oggpack's own bit order): ";
    for (uint32_t i = 0; i < e.byteLen; ++i) {
        uint8_t b = kWwiseCodebookLibraryBlob[e.blobOffset + i];
        for (int bit = 0; bit < 8; ++bit) std::cout << ((b >> bit) & 1);
        std::cout << " ";
    }
    std::cout << "\n";
    CodebookParseInfo info;
    std::string err;
    bool ok = parseOneCodebookForCrossCheck(kWwiseCodebookLibraryBlob + e.blobOffset, e.byteLen, info, err);
    std::cout << "  parse ok=" << ok << " err='" << err << "' dim=" << info.dim << " entries=" << info.entries
              << " ordered=" << info.ordered << " maptype=" << info.maptype << " lengths=[";
    for (size_t i = 0; i < info.lengths.size(); ++i) std::cout << info.lengths[i] << (i + 1 < info.lengths.size() ? "," : "");
    std::cout << "]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: wwise_vorbis_codebook_kraft_probe sweep\n"
                      "       wwise_vorbis_codebook_kraft_probe record <archive> <outDir> [nameFilter] "
                      "[codebookCountFilter]\n"
                      "       wwise_vorbis_codebook_kraft_probe dumpslot <slot>\n";
        return 1;
    }
    std::string mode = argv[1];
    if (mode == "sweep") {
        modeSweep();
        return 0;
    }
    if (mode == "dumpslot") {
        modeDumpSlot(argc > 2 ? std::atoi(argv[2]) : -1);
        return 0;
    }
    if (mode == "record") {
        std::string archivePath = argc > 2 ? argv[2]
                                            : "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/"
                                              "cutscene_sounds.vpp_pc";
        std::string outDir = argc > 3 ? argv[3] : ".";
        std::string filter = argc > 4 ? argv[4] : "";
        int codebookCountFilter = argc > 5 ? std::atoi(argv[5]) : -1;
        modeRecord(archivePath, outDir, filter, codebookCountFilter);
        return 0;
    }
    std::cerr << "unknown mode '" << mode << "'\n";
    return 1;
}
