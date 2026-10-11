// Bink decoder-skeleton driver tool: runs include/sr3binkcodec/*.h's
// per-block dispatch against one real, already-extracted Bink frame (by
// default, frame 0 of this game's own logo_thq_and_voli_hd.bik - see
// tools/bink_frame0_probe.cpp for how that raw byte range was located and
// frozen at tests/golden/bink_logo_frame0/frame0_raw.bin), reports the
// dispatch trace, and writes a viewing-only PNG of whatever the luma plane
// actually reconstructed (sr3render::writePng - this project's own existing
// PNG writer, reused rather than adding a new image dependency).
//
// This is a decoder-SKELETON tool, not a working Bink player: see
// include/sr3binkcodec/frame_decoder.h's own top comment for the exact
// scope (luma-plane-only) and the two flagged open alignment questions
// this measurement is read under, and block_dispatch.h for exactly which
// block-type cases are fully reconstructed vs. represented by an explicit
// OPEN hook. Nothing here invents a pixel value, a Huffman table, or a
// transform coefficient no spec text confirms.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/block_dispatch.h"
#include "sr3binkcodec/frame_decoder.h"
#include "sr3binkcodec/planar_buffer.h"
#include "sr3binkcodec/yuv_to_rgb.h"
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
    const std::string defaultPath = "tests/golden/bink_logo_frame0/frame0_raw.bin";
    const std::string inPath = (argc > 1) ? argv[1] : defaultPath;
    const std::string outPng = (argc > 2) ? argv[2] : "bink_frame0_decode_output.png";

    // Real header fields for the default fixture (frame 0 of
    // logo_thq_and_voli_hd.bik), read directly by tools/bink_frame0_probe.cpp
    // against the real asset (spec-bink-codec.md Sec1.2): rounded luma
    // 1280x720. A caller pointing this tool at some OTHER extracted frame
    // must pass matching dimensions via argv[3]/argv[4] - not re-derived
    // here, since a raw per-frame byte range carries no header of its own.
    int lumaWidth = 1280;
    int lumaHeight = 720;
    if (argc > 4) {
        lumaWidth = std::atoi(argv[3]);
        lumaHeight = std::atoi(argv[4]);
    }
    // D10 (audio-track count) for the default fixture is 1 (spec-bink-
    // codec.md Sec1.6: logo_thq_and_voli_hd.bik is the section's own named
    // D10>=1 example). A caller pointing this tool at a different extracted
    // frame may pass its own real D10 value via argv[5] (0 for a D10=0
    // file such as main_menu_bg.bik). SUPERSEDES this tool's own prior
    // "hasLeadingSubChunkSpan override" argv[5] convention (merge/bink-
    // descriptor-b-case1-selector, 2026-10-10): that pre-audio-chunk-skip
    // version hardcoded hasLeadingSubChunkSpan=false for this fixture
    // because forcing it true WITHOUT first skipping the per-track audio
    // chunks reads audio bytes as video data and desyncs immediately -
    // fix/bink-d10plus-audio-chunk-skip's own skipAudioChunks() (below)
    // fixes the real root cause, after which hasLeadingSubChunkSpan is
    // unconditionally true for every real file (D10=0 directly, D10>=1
    // after the skip) - see frame_decoder.h's own top comment.
    int numAudioTracks = 1;
    if (argc > 5) {
        numAudioTracks = std::atoi(argv[5]);
    }

    std::printf("bink_frame0_decode: decoder-skeleton driver (sr3binkcodec)\n");
    std::printf("input: %s (luma %dx%d, assumed per spec-bink-codec.md Sec1.2's own macroblock rounding)\n",
                inPath.c_str(), lumaWidth, lumaHeight);

    bool ok = false;
    std::vector<uint8_t> raw = readFile(inPath, ok);
    if (!ok || raw.empty()) {
        std::printf("FATAL: could not read '%s'\n", inPath.c_str());
        return 1;
    }
    std::printf("read %zu raw compressed bytes\n", raw.size());

    using namespace sr3binkcodec;

    BitstreamReader br(raw.data(), raw.size());
    FrameDimensions dims = FrameDimensions::fromRoundedLuma(lumaWidth, lumaHeight);
    PlanarYuvFrame frame;
    frame.allocate(dims);
    // Chroma is out of scope for this skeleton (frame_decoder.h's own top
    // comment: call 2's role is OPEN, so where chroma-A's own pass begins
    // in the byte stream is unknown) - forced to neutral 128 purely for
    // this tool's own PNG visualization, so the rendered picture reflects
    // only the luma information this skeleton actually reconstructs rather
    // than a false colour cast from the generic undecoded-sentinel bytes.
    frame.chromaA().fillFlat(128);
    frame.chromaB().fillFlat(128);

    // FIX (2026-10-09, fix/bink-d10plus-audio-chunk-skip, spec-bink-codec.md
    // Sec1.6's "Frame N's own blob, for D10>=1..." paragraph): this tool's
    // default target (logo_thq_and_voli_hd.bik) is a D10>=1 ("audio
    // tracks") file - its blob leads with `numAudioTracks` per-track audio
    // chunks (each a 4-byte LE size field + that many payload bytes) before
    // the video chunk begins. skipAudioChunks() walks past exactly those
    // chunks; once past them, the video chunk's own leading span field is
    // the SAME shape already confirmed for D10=0 files, so
    // hasLeadingSubChunkSpan=true universally from here - see
    // include/sr3binkcodec/frame_decoder.h's own top comment.
    skipAudioChunks(br, numAudioTracks);
    DecodeTrace trace = decodeLumaPlane(br, frame, nullptr /* no previous frame: a keyframe test */,
                                         /*hasLeadingSubChunkSpan=*/true);

    std::printf("\n--- Dispatch trace (spec-bink-codec.md Sec5.3/Sec5.3a) ---\n");
    std::printf("grid                : %d x %d = %d 8x8 blocks\n", trace.blockCols, trace.blockRows,
                trace.totalBlocksInGrid);
    std::printf("blocks attempted    : %zu\n", trace.blocks.size());
    std::printf("  reconstructed     : %llu\n", static_cast<unsigned long long>(trace.reconstructedCount()));
    std::printf("  open-hook         : %llu\n", static_cast<unsigned long long>(trace.openHookCount()));
    std::printf("  out-of-range      : %llu\n", static_cast<unsigned long long>(trace.outOfRangeCount()));
    std::printf("stop reason         : %s\n", trace.stopReasonText().c_str());
    std::printf("histogram (primary[/sub] -> count):\n");
    for (const auto& h : trace.histogram()) {
        if (h.outcome == sr3binkcodec::BlockOutcome::OutOfRangeDispatch) {
            // h.primary is meaningless here (default-constructed, never
            // assigned - see BlockRecord's own doc comment); print the real
            // raw nibble instead so distinct overflow values aren't
            // misreported as "primary=0".
            std::printf("  OUT-OF-RANGE raw_nibble=%u -> %llu\n", h.rawPrimaryNibble,
                        static_cast<unsigned long long>(h.count));
        } else if (h.sub) {
            std::printf("  primary=%d sub=%d -> %llu\n", static_cast<int>(h.primary), static_cast<int>(*h.sub),
                        static_cast<unsigned long long>(h.count));
        } else {
            std::printf("  primary=%d -> %llu\n", static_cast<int>(h.primary),
                        static_cast<unsigned long long>(h.count));
        }
    }

    std::vector<uint8_t> writtenMask = buildWrittenMask(trace, lumaWidth, lumaHeight);
    std::vector<uint8_t> rgba = renderFrameToRgba(frame, &writtenMask);

    std::string pngError;
    if (!sr3render::writePng(outPng, static_cast<uint32_t>(lumaWidth), static_cast<uint32_t>(lumaHeight), rgba,
                              pngError)) {
        std::printf("\nWARNING: writePng failed: %s\n", pngError.c_str());
    } else {
        std::printf("\nWrote viewing-only PNG to '%s' (reconstructed luma blocks in grayscale, chroma forced to "
                    "neutral 128 - chroma decode is out of scope for this skeleton; any unreconstructed region is "
                    "a flat synthetic magenta tint, never a guessed pixel).\n",
                    outPng.c_str());
    }

    std::printf(
        "\nThis is a decoder SKELETON result, not a claim of full visual correctness - see "
        "include/sr3binkcodec/frame_decoder.h's own top comment for the two flagged scope/alignment "
        "assumptions this trace is read under, and spec-bink-codec.md Sec8 for the still-OPEN entropy/"
        "transform gaps behind every OpenHook block above.\n");

    return 0;
}
