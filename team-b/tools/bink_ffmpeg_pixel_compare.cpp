// Bink decoder vs. independent FFmpeg ground-truth pixel comparison
// (fix/bink-dc-literal-descriptor-l task, 2026-10-11): decodes one real,
// already-extracted Bink frame's luma (Y) plane exactly like
// tools/bink_y_plane_oracle_dump.cpp does, then compares EVERY byte this
// project's decoder actually reconstructed (BlockOutcome::Reconstructed
// blocks only - never a guessed/OPEN-hook byte) against the Y plane of an
// independent FFmpeg `-pix_fmt yuv420p -f rawvideo` extraction of the same
// frame (NOT `-pix_fmt gray` - that one silently rescales tv-range to
// full-range and would mislead this comparison, per this project's own
// standing HANDOFF.md caveat).
//
// This tool adds NO new reverse-engineered facts - it is pure measurement
// plumbing around include/sr3binkcodec/*.h (already-cited spec sections),
// reused from this project's own existing real-data-check convention (see
// HANDOFF.md's "DONE THIS SESSION" Bink entries for the prior, ad hoc
// version of this exact check).
//
// Usage: bink_ffmpeg_pixel_compare <frameN_raw.bin> <ffmpeg_yuv420p.raw>
//        <lumaWidth> <lumaHeight> <numAudioTracks> <hasLeadingSubChunkSpan 0|1>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/block_dispatch.h"
#include "sr3binkcodec/frame_decoder.h"
#include "sr3binkcodec/planar_buffer.h"

namespace {

std::vector<uint8_t> readFile(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        ok = false;
        return {};
    }
    std::streamsize n = f.tellg();
    if (n <= 0) {
        ok = false;
        return {};
    }
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(buf.data()), n);
    ok = static_cast<bool>(f);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 7) {
        std::printf(
            "usage: bink_ffmpeg_pixel_compare <raw_frameN.bin> <ffmpeg_yuv420p.raw> <lumaWidth> <lumaHeight> "
            "<numAudioTracks> <hasLeadingSubChunkSpan 0|1>\n");
        return 1;
    }
    const std::string inPath = argv[1];
    const std::string oraclePath = argv[2];
    const int lumaWidth = std::atoi(argv[3]);
    const int lumaHeight = std::atoi(argv[4]);
    const int numAudioTracks = std::atoi(argv[5]);
    const bool hasLeadingSubChunkSpan = std::atoi(argv[6]) != 0;

    bool ok = false;
    std::vector<uint8_t> raw = readFile(inPath, ok);
    if (!ok || raw.empty()) {
        std::printf("FATAL: could not read '%s'\n", inPath.c_str());
        return 1;
    }
    bool ok2 = false;
    std::vector<uint8_t> oracle = readFile(oraclePath, ok2);
    if (!ok2 || oracle.size() < static_cast<size_t>(lumaWidth) * static_cast<size_t>(lumaHeight)) {
        std::printf("FATAL: could not read (or too small) '%s' (need >= %d Y-plane bytes, got %zu)\n",
                    oraclePath.c_str(), lumaWidth * lumaHeight, oracle.size());
        return 1;
    }

    using namespace sr3binkcodec;

    BitstreamReader br(raw.data(), raw.size());
    FrameDimensions dims = FrameDimensions::fromRoundedLuma(lumaWidth, lumaHeight);
    PlanarYuvFrame frame;
    frame.allocate(dims);

    skipAudioChunks(br, numAudioTracks);
    DecodeTrace trace = decodeLumaPlane(br, frame, nullptr, hasLeadingSubChunkSpan);

    std::printf("--- Dispatch trace ---\n");
    std::printf("blocks attempted : %zu, reconstructed: %llu\n", trace.blocks.size(),
                static_cast<unsigned long long>(trace.reconstructedCount()));

    const uint8_t* ours = frame.luma().data();
    const uint8_t* theirs = oracle.data();  // ffmpeg yuv420p raw: Y plane is the first width*height bytes
    const int stride = dims.lumaWidth;      // PlanarBuffer's own rounded luma width (Sec1.2 macroblock-16 rounding)

    size_t blocksExact = 0;
    size_t blocksChecked = 0;
    size_t pixelsChecked = 0;
    size_t pixelsMatch = 0;
    size_t perPrimaryChecked[10] = {};
    size_t perPrimaryExact[10] = {};
    int firstBadRow = -1, firstBadCol = -1;

    for (const auto& b : trace.blocks) {
        if (b.outcome != BlockOutcome::Reconstructed) continue;
        // Skip blocks whose footprint falls outside ffmpeg's own real
        // (unrounded) frame dimensions - this project's own plane buffer
        // may be padded up to a macroblock-16 boundary (Sec1.2), ffmpeg's
        // raw dump is not.
        const int x0 = b.blockCol * kBlockSize;
        const int y0 = b.blockRow * kBlockSize;
        if (x0 + kBlockSize > lumaWidth || y0 + kBlockSize > lumaHeight) continue;

        ++blocksChecked;
        bool exact = true;
        for (int y = 0; y < kBlockSize; ++y) {
            for (int x = 0; x < kBlockSize; ++x) {
                const uint8_t o = ours[(y0 + y) * stride + (x0 + x)];
                const uint8_t t = theirs[(y0 + y) * lumaWidth + (x0 + x)];
                ++pixelsChecked;
                if (o == t) {
                    ++pixelsMatch;
                } else {
                    exact = false;
                }
                if (x == 0 && y == 0) {
                    std::printf("block (row=%d,col=%d) primary=%d sub=%d pixel(0,0): ours=%u ffmpeg=%u %s\n",
                                b.blockRow, b.blockCol, static_cast<int>(b.primary),
                                b.sub ? static_cast<int>(*b.sub) : -1, o, t, (o == t) ? "MATCH" : "MISMATCH");
                    if ((o != t || std::getenv("BINK_DIAG_ALL")) && b.dcLiteral)
                        std::printf("    diag: dc=%u acEntryBit=%zu acExitBit=%zu\n", static_cast<unsigned>(*b.dcLiteral),
                                    b.acEntryBitPos ? *b.acEntryBitPos : size_t(0),
                                    b.acExitBitPos ? *b.acExitBitPos : size_t(0));
                }
            }
        }
        if (exact) ++blocksExact;
        const int pi = static_cast<int>(b.primary);
        if (pi >= 0 && pi < 10) {
            ++perPrimaryChecked[pi];
            if (exact) ++perPrimaryExact[pi];
        }
        if (!exact && firstBadRow < 0) {
            firstBadRow = b.blockRow;
            firstBadCol = b.blockCol;
        }
    }
    std::printf("\nper-primary exact/checked:");
    for (int i = 0; i < 10; ++i)
        if (perPrimaryChecked[i]) std::printf(" p%d=%zu/%zu", i, perPrimaryExact[i], perPrimaryChecked[i]);
    std::printf("\nfirst mismatching block: row=%d col=%d\n", firstBadRow, firstBadCol);
    if (!trace.blocks.empty()) {
        const auto& lb = trace.blocks.back();
        std::printf("last block: row=%d col=%d primary=%d sub=%d outcome=%d rawPrimaryNibble=%d\n", lb.blockRow, lb.blockCol, static_cast<int>(lb.primary), lb.sub ? static_cast<int>(*lb.sub) : -1, static_cast<int>(lb.outcome), static_cast<int>(lb.rawPrimaryNibble));
    }
    std::printf("stop reason: %s\n", trace.stopReasonText().c_str());

    std::printf("\n=== RESULT ===\n");
    std::printf("blocks reconstructed (in-bounds) : %zu\n", blocksChecked);
    std::printf("blocks exact match               : %zu / %zu\n", blocksExact, blocksChecked);
    std::printf("pixels match                     : %zu / %zu (%.4f%%)\n", pixelsMatch, pixelsChecked,
                pixelsChecked ? (100.0 * static_cast<double>(pixelsMatch) / static_cast<double>(pixelsChecked))
                               : 0.0);
    return 0;
}
