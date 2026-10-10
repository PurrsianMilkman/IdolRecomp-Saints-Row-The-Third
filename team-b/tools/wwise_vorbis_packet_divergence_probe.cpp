// DIAGNOSTIC-ONLY tool (2026-10-08, independent-oracle verification task,
// NOT a ctest, NOT wired into the real decode path - same "one-shot
// real-data probe" convention as every other tools/wwise_*_probe.cpp on
// this branch). Purpose: the orchestrator's own oracle comparison
// (vgmstream-cli, run as an independent black-box decoder - see
// tools/wwise_vorbis_oracle_extract.cpp) found ZERO correlation between
// this project's own "5/493 real decodes" and the real audio content, at
// every lag, for every one of the 5 records. This tool duplicates (not
// modifies) src/real_vorbis_decode.cpp's own per-audio-packet
// vorbis_synthesis() loop with per-packet pass/fail logging, to locate
// PRECISELY where and how often the real decode diverges from a clean
// packet-by-packet decode - the "find the first divergent packet" diagnostic
// the oracle-comparison task calls for.
//
// Usage: wwise_vorbis_packet_divergence_probe <path to cutscene_sounds.vpp_pc> <record name substring>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "sr3audio/media_bank.h"
#include "sr3audioruntime/wwise_setup_rebuild.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"
#include "vpp/container.h"

using namespace sr3audioruntime;

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

// Verbatim duplicates of real_vorbis_decode.cpp's own anonymous-namespace
// packet-builder helpers (this project's own code, not third-party source) -
// needed here only because that file does not export them; kept
// byte-for-byte identical in read order/semantics, not re-derived.
void writeString(oggpack_buffer& b, const char* s, int len) {
    for (int i = 0; i < len; ++i) oggpack_write(&b, static_cast<unsigned long>(static_cast<unsigned char>(s[i])), 8);
}

std::vector<uint8_t> finishPacket(oggpack_buffer& b) {
    std::vector<uint8_t> out(oggpack_get_buffer(&b), oggpack_get_buffer(&b) + oggpack_bytes(&b));
    oggpack_writeclear(&b);
    return out;
}

std::vector<uint8_t> buildIdHeaderPacket(int channels, uint32_t sampleRate, uint8_t bs0exp, uint8_t bs1exp) {
    oggpack_buffer b;
    oggpack_writeinit(&b);
    oggpack_write(&b, 0x01, 8);
    writeString(b, "vorbis", 6);
    oggpack_write(&b, 0, 32);
    oggpack_write(&b, static_cast<unsigned long>(channels), 8);
    oggpack_write(&b, sampleRate, 32);
    oggpack_write(&b, 0, 32);
    oggpack_write(&b, 0, 32);
    oggpack_write(&b, 0, 32);
    oggpack_write(&b, bs0exp, 4);
    oggpack_write(&b, bs1exp, 4);
    oggpack_write(&b, 1, 1);
    return finishPacket(b);
}

std::vector<uint8_t> buildCommentHeaderPacket() {
    oggpack_buffer b;
    oggpack_writeinit(&b);
    oggpack_write(&b, 0x03, 8);
    writeString(b, "vorbis", 6);
    oggpack_write(&b, 0, 32);
    oggpack_write(&b, 0, 32);
    oggpack_write(&b, 1, 1);
    return finishPacket(b);
}

ogg_packet makePacket(std::vector<uint8_t>& storage, bool bos, int64_t packetno) {
    ogg_packet op{};
    op.packet = storage.data();
    op.bytes = static_cast<long>(storage.size());
    op.b_o_s = bos ? 1 : 0;
    op.e_o_s = 0;
    op.granulepos = 0;
    op.packetno = packetno;
    return op;
}

void probeOneRecord(const std::string& bankName, int recIdx, const uint8_t* recBytes, size_t recSize) {
    VorbisContainerChunks chunks = parseRiffWaveContainer(recBytes, recSize);
    if (!chunks.valid || !chunks.hasFmt || !chunks.hasData) {
        std::cout << "  [container parse failed]\n";
        return;
    }
    StandardWaveFmt fmt = parseStandardWaveFmt(recBytes, chunks.fmtOffset, chunks.fmtSize);
    auto vorbLoc = findRiffSubChunk(recBytes, recSize, kFourCcVorb);
    if (!vorbLoc) return;
    WwiseVorbChunk vorb = parseVorbChunk(recBytes, recSize, vorbLoc->first);
    if (!vorb.present) return;

    const uint32_t seekTableLenField =
        static_cast<uint32_t>(vorb.raw42[16]) | (static_cast<uint32_t>(vorb.raw42[17]) << 8) |
        (static_cast<uint32_t>(vorb.raw42[18]) << 16) | (static_cast<uint32_t>(vorb.raw42[19]) << 24);
    const size_t setupPacketStart = chunks.dataOffset + seekTableLenField;
    if (setupPacketStart >= chunks.dataOffset + chunks.dataSize) {
        std::cout << "  [setupPacketStart out of range]\n";
        return;
    }
    const size_t walkSize = (chunks.dataOffset + chunks.dataSize) - setupPacketStart;
    FramedPacketWalkResult framed = walkFramedPackets(recBytes, setupPacketStart, walkSize);
    std::cout << "  framed.packets.size()=" << framed.packets.size() << " framed.consistent=" << framed.consistent
              << " framed.stoppedAtOffset=" << framed.stoppedAtOffset << " walkSize=" << walkSize << "\n";
    if (framed.packets.empty()) return;

    vorbis_info vi;
    vorbis_info_init(&vi);
    vorbis_comment vc;
    vorbis_comment_init(&vc);

    std::vector<uint8_t> idPkt =
        buildIdHeaderPacket(fmt.channels, fmt.samplesPerSec, vorb.blockSizeExponent0, vorb.blockSizeExponent1);
    ogg_packet opId = makePacket(idPkt, true, 0);
    if (vorbis_synthesis_headerin(&vi, &vc, &opId) != 0) {
        std::cout << "  [id header rejected]\n";
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return;
    }
    std::vector<uint8_t> commentPkt = buildCommentHeaderPacket();
    ogg_packet opComment = makePacket(commentPkt, false, 1);
    if (vorbis_synthesis_headerin(&vi, &vc, &opComment) != 0) {
        std::cout << "  [comment header rejected]\n";
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return;
    }
    SetupRebuildResult rebuilt =
        rebuildVorbisSetupHeader(recBytes + framed.packets[0].offset, framed.packets[0].length, fmt.channels);
    if (!rebuilt.ok) {
        std::cout << "  [setup rebuild failed: " << rebuilt.diagnostic << "]\n";
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return;
    }
    std::vector<uint8_t> setupPkt = rebuilt.standardSetupPacket;
    ogg_packet opSetup = makePacket(setupPkt, false, 2);
    if (vorbis_synthesis_headerin(&vi, &vc, &opSetup) != 0) {
        std::cout << "  [setup header rejected by libvorbis]\n";
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return;
    }
    vorbis_dsp_state vd;
    if (vorbis_synthesis_init(&vd, &vi) != 0) {
        std::cout << "  [vorbis_synthesis_init failed, codebookCount=" << rebuilt.codebookCount << "]\n";
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        return;
    }
    vorbis_block vb;
    vorbis_block_init(&vd, &vb);

    std::cout << "  per-packet vorbis_synthesis() results (index: rc, bytes[, samplesThisPacket]), first "
                 "divergence and run-length pattern:\n";
    int firstFailIdx = -1;
    long long attempted = 0, okCount = 0;
    long long totalSamplesOut = 0;
    // Run-length-encode the pass/fail sequence to characterize the pattern
    // without printing thousands of lines.
    std::vector<std::pair<bool, long long>> runs;  // (ok?, run length)
    bool printedDetailHead = false;
    int detailLines = 0;
    for (size_t i = 1; i < framed.packets.size(); ++i) {
        std::vector<uint8_t> audioBytes(recBytes + framed.packets[i].offset,
                                         recBytes + framed.packets[i].offset + framed.packets[i].length);
        ogg_packet opAudio = makePacket(audioBytes, false, static_cast<int64_t>(2 + i));
        ++attempted;
        int rc = vorbis_synthesis(&vb, &opAudio);
        bool ok = (rc == 0);
        long long samplesThis = 0;
        if (ok) {
            vorbis_synthesis_blockin(&vd, &vb);
            ++okCount;
            float** pcm = nullptr;
            int samples;
            while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
                samplesThis += samples;
                totalSamplesOut += samples;
                vorbis_synthesis_read(&vd, samples);
            }
        }
        if (!ok && firstFailIdx < 0) firstFailIdx = static_cast<int>(i);
        if (detailLines < 25) {
            std::cout << "    pkt[" << i << "] offset=" << framed.packets[i].offset
                       << " len=" << framed.packets[i].length << " rc=" << rc << " samples=" << samplesThis << "\n";
            ++detailLines;
        }
        if (!runs.empty() && runs.back().first == ok) {
            ++runs.back().second;
        } else {
            runs.push_back({ok, 1});
        }
    }
    std::cout << "  SUMMARY: attempted=" << attempted << " okCount=" << okCount << " (" << (100.0 * okCount / attempted)
              << "%) firstFailIdx=" << firstFailIdx << " totalSamplesOut=" << totalSamplesOut << "\n";
    std::cout << "  run-length pattern (first 40 runs, ok/fail alternating, length each): ";
    for (size_t r = 0; r < runs.size() && r < 40; ++r) {
        std::cout << (runs[r].first ? "OK" : "FAIL") << "x" << runs[r].second << " ";
    }
    std::cout << "\n  total run count=" << runs.size() << "\n";

    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string archivePath =
        argc > 1 ? argv[1] : "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/cutscene_sounds.vpp_pc";
    const std::string filter = argc > 2 ? argv[2] : "";

    std::vector<uint8_t> archiveBytes = readFile(archivePath);
    if (archiveBytes.empty()) {
        std::cerr << "could not read " << archivePath << "\n";
        return 1;
    }
    vpp::Container c(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));

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
            std::cout << "=== " << n << " (bank record #" << recIdx << ") ===\n";
            probeOneRecord(n, recIdx, recBytes, rec.size);
        }
    }
    return 0;
}
