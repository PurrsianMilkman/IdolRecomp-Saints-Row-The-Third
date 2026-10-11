// Bink pixel-accuracy oracle helper (fix/bink-pixel-accuracy-oracle task):
// decodes one real, already-extracted Bink frame's luma (Y) plane exactly
// like tools/bink_frame0_decode.cpp does, but writes the RAW Y bytes
// directly as a grayscale PNG (R=G=B=Y, A=255) - NOT through
// yuv_to_rgb.h's limitedRangeYuvToRgb() (which rescales limited-range Y into
// full-range RGB and would confound a per-pixel Y-value comparison against
// an external oracle) and NOT with the magenta-tint "unreconstructed" visual
// convention bink_frame0_decode.cpp uses for human viewing (an unreconstructed
// byte here is written as its own literal sentinel value,
// PlanarYuvFrame::kUndecodedSentinel == 0x55, so a PSNR/max-diff comparison
// against this tool's own output is comparing REAL Y bytes end to end, with
// large, honest error reported wherever this project's decoder has not
// reconstructed a pixel - exactly the "measure accurately, don't force the
// numbers to look good" instruction this task was given).
//
// This tool adds NO new reverse-engineered facts - it is pure plumbing
// around include/sr3binkcodec/*.h (already-cited spec sections) and this
// project's own existing sr3render::writePng.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/block_dispatch.h"
#include "sr3binkcodec/frame_decoder.h"
#include "sr3binkcodec/planar_buffer.h"
#include "sr3render/png_writer.h"

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
            "usage: bink_y_plane_oracle_dump <raw_frame0.bin> <out.png> <lumaWidth> <lumaHeight> "
            "<numAudioTracks> <hasLeadingSubChunkSpan 0|1>\n");
        return 1;
    }
    const std::string inPath = argv[1];
    const std::string outPng = argv[2];
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
    std::printf("read %zu raw compressed bytes from '%s'\n", raw.size(), inPath.c_str());

    using namespace sr3binkcodec;

    BitstreamReader br(raw.data(), raw.size());
    FrameDimensions dims = FrameDimensions::fromRoundedLuma(lumaWidth, lumaHeight);
    PlanarYuvFrame frame;
    frame.allocate(dims);

    skipAudioChunks(br, numAudioTracks);
    DecodeTrace trace = decodeLumaPlane(br, frame, nullptr /* keyframe, no previous frame */, hasLeadingSubChunkSpan);

    std::printf("\n--- Dispatch trace ---\n");
    std::printf("grid             : %d x %d = %d 8x8 blocks\n", trace.blockCols, trace.blockRows,
                trace.totalBlocksInGrid);
    std::printf("blocks attempted : %zu\n", trace.blocks.size());
    std::printf("reconstructed    : %llu\n", static_cast<unsigned long long>(trace.reconstructedCount()));
    std::printf("stop reason      : %s\n", trace.stopReasonText().c_str());
    std::printf("histogram (primary[/sub] -> count):\n");
    for (const auto& h : trace.histogram()) {
        if (h.outcome == BlockOutcome::OutOfRangeDispatch) {
            std::printf("  OUT-OF-RANGE raw_nibble=%u -> %llu\n", h.rawPrimaryNibble,
                        static_cast<unsigned long long>(h.count));
        } else if (h.sub) {
            std::printf("  primary=%d sub=%d (%s) -> %llu\n", static_cast<int>(h.primary), static_cast<int>(*h.sub),
                        h.outcome == BlockOutcome::Reconstructed ? "Reconstructed" : "OpenHook",
                        static_cast<unsigned long long>(h.count));
        } else {
            std::printf("  primary=%d (%s) -> %llu\n", static_cast<int>(h.primary),
                        h.outcome == BlockOutcome::Reconstructed ? "Reconstructed" : "OpenHook",
                        static_cast<unsigned long long>(h.count));
        }
    }

    // Raw Y bytes -> RGBA grayscale, NO colorspace rescale, NO magenta tint -
    // a literal byte-for-byte view of this plane's own storage (including
    // PlanarYuvFrame::kUndecodedSentinel wherever nothing was reconstructed).
    const uint8_t* yPlane = frame.luma().data();
    const size_t w = static_cast<size_t>(lumaWidth);
    const size_t h = static_cast<size_t>(lumaHeight);
    std::vector<uint8_t> rgba(w * h * 4);
    for (size_t i = 0; i < w * h; ++i) {
        const uint8_t y = yPlane[i];
        rgba[i * 4 + 0] = y;
        rgba[i * 4 + 1] = y;
        rgba[i * 4 + 2] = y;
        rgba[i * 4 + 3] = 255;
    }

    std::string pngError;
    if (!sr3render::writePng(outPng, static_cast<uint32_t>(lumaWidth), static_cast<uint32_t>(lumaHeight), rgba,
                              pngError)) {
        std::printf("\nFATAL: writePng failed: %s\n", pngError.c_str());
        return 1;
    }
    std::printf("\nWrote raw-Y grayscale PNG (literal plane bytes, sentinel 0x55 where unreconstructed) to '%s'\n",
                outPng.c_str());
    return 0;
}
