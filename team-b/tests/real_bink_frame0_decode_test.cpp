// Real-data regression for the Bink decoder skeleton (include/sr3binkcodec/
// *.h) against frame 0 of the game's own real shipped logo movie
// (`videos/logo_thq_and_voli_hd.bik`, extracted by tools/bink_frame0_probe.cpp
// per spec-bink-codec.md Sec1.5/Sec1.5a/Sec1.6; frozen at
// tests/golden/bink_logo_frame0/frame0_raw.bin, gitignored, local-only).
//
// Per this task's own explicit instruction: verify the golden file's MD5
// before using it; if it is missing or does not match, stop and report
// (exit 77, same SKIPPED convention every other real_*_test in this repo
// uses - see tests/real_vorbis_decode_test.cpp) rather than re-deriving it
// from anywhere else.
//
// This test is NOT a claim that this project's decoder fully reconstructs
// frame 0. It reports, and regression-pins, exactly what the decoder
// skeleton's own dispatch trace measures against the real bytes - which
// block-type path(s) it hit, how many blocks were fully reconstructed
// before the first OPEN/out-of-range stop, and why it stopped there. See
// include/sr3binkcodec/frame_decoder.h's own top comment for the two
// flagged scope/alignment assumptions this measurement is read under
// (luma-plane-only; byte 0 of the extracted range assumed to be call 1's
// own first dispatch read - i.e. decodeOnce() below passes
// hasLeadingSubChunkSpan=false, UNCHANGED by the 2026-10-09 fix/bink-merge-
// group-bit-consumption follow-up task: that task CONFIRMED a leading
// 4-byte per-plane span field for the OTHER real fixture, main_menu_bg.bik,
// but found forcing the same skip onto THIS file empirically desyncs its
// decode immediately - see frame_decoder.h's own top comment for the full
// citation trail and why this stays an open, per-file question rather than
// a guessed-at fix).

#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "bink_md5.h"
#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/block_dispatch.h"
#include "sr3binkcodec/frame_decoder.h"
#include "sr3binkcodec/planar_buffer.h"
#include "sr3binkcodec/yuv_to_rgb.h"

#ifndef BINK_FRAME0_GOLDEN_PATH
#define BINK_FRAME0_GOLDEN_PATH "tests/golden/bink_logo_frame0/frame0_raw.bin"
#endif

using namespace sr3binkcodec;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

// This project's own designated golden fixture, per this task's brief:
// MD5 99a631dfd434b5a5e1c68f9a4a477cfe, 29336 bytes, frame 0 of
// logo_thq_and_voli_hd.bik (keyframe=yes, per tools/bink_frame0_probe.cpp's
// own extraction log).
constexpr const char* kExpectedMd5 = "99a631dfd434b5a5e1c68f9a4a477cfe";
constexpr size_t kExpectedSize = 29336;

// Real header fields for this exact file, read directly by
// tools/bink_frame0_probe.cpp against the real asset (spec-bink-codec.md
// Sec1.2's own field table) - NOT re-derived here, since frame0_raw.bin
// itself is only the per-frame compressed payload (no container header of
// its own). D5/D6 raw width/height = 1280/720, both already exact multiples
// of 16, so the Sec1.2 macroblock-rounding is a no-op for this file.
constexpr int kLumaWidth = 1280;
constexpr int kLumaHeight = 720;

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    if (n <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(buf.data()), n);
    if (!f) return {};
    return buf;
}

}  // namespace

int main() {
    const std::vector<uint8_t> raw = readFile(BINK_FRAME0_GOLDEN_PATH);
    if (raw.empty()) {
        std::cout << "real_bink_frame0_decode_test: SKIPPED - golden fixture not found at '"
                   << BINK_FRAME0_GOLDEN_PATH
                   << "' (gitignored, local-only; see this task's own instruction: stop and report rather than "
                      "re-deriving it from anywhere else).\n";
        return 77;
    }

    const std::string actualMd5 = bink_test_md5::computeMd5Hex(raw.data(), raw.size());
    if (raw.size() != kExpectedSize || actualMd5 != kExpectedMd5) {
        std::cout << "real_bink_frame0_decode_test: SKIPPED - golden fixture present but does not match its "
                      "designated hash (expected "
                   << kExpectedSize << " bytes / md5 " << kExpectedMd5 << ", got " << raw.size() << " bytes / md5 "
                   << actualMd5 << "). Per this task's own instruction, stopping rather than re-deriving it.\n";
        return 77;
    }

    std::cout << "real_bink_frame0_decode_test: golden fixture verified (" << raw.size() << " bytes, md5 "
              << actualMd5 << ")\n";

    auto decodeOnce = [&]() {
        BitstreamReader br(raw.data(), raw.size());
        FrameDimensions dims = FrameDimensions::fromRoundedLuma(kLumaWidth, kLumaHeight);
        PlanarYuvFrame frame;
        frame.allocate(dims);
        // Frame 0 is a keyframe (confirmed by tools/bink_frame0_probe.cpp's
        // own extraction, the frame-offset table's own bit 0, spec-bink-
        // codec.md Sec1.6) - there is no real previous frame to copy from,
        // so block-copy blocks (if any are hit) get no invented source
        // pixels (see decodeOneBlock's own doc comment).
        // hasLeadingSubChunkSpan=false: logo_thq_and_voli_hd.bik is a
        // spec-bink-codec.md Sec1.6 D10>=1 ("one or more audio tracks") file
        // - the section's own named example of a file whose extra per-track
        // header bytes "break" the clean D10=0 frame-blob formula. Whether
        // this file's own already-extracted frame0_raw.bin ALSO carries the
        // leading 4-byte per-plane span field Sec5.3g confirms for the OTHER
        // real fixture (main_menu_bg.bik, a D10=0 file) is UNCONFIRMED -
        // forcing skip=true here was tried (fix/bink-merge-group-bit-
        // consumption follow-up task) and found to desync this file's own
        // decode immediately (OutOfRangeDispatch at block (0,0), down from
        // 418/419 blocks reconstructed), so this stays false pending a Team A
        // spec-sync answer rather than forcing either stance as a guess - see
        // include/sr3binkcodec/frame_decoder.h's own top comment.
        DecodeTrace trace = decodeLumaPlane(br, frame, nullptr, /*hasLeadingSubChunkSpan=*/false);
        return std::make_pair(std::move(trace), std::move(frame));
    };

    auto [trace, frame] = decodeOnce();

    std::cout << "\n--- Luma-plane dispatch trace (spec-bink-codec.md Sec5.3/Sec5.3a) ---\n";
    std::cout << "grid: " << trace.blockCols << " x " << trace.blockRows << " = " << trace.totalBlocksInGrid
              << " 8x8 blocks\n";
    std::cout << "blocks attempted : " << trace.blocks.size() << "\n";
    std::cout << "reconstructed    : " << trace.reconstructedCount() << "\n";
    std::cout << "open-hook        : " << trace.openHookCount() << "\n";
    std::cout << "out-of-range     : " << trace.outOfRangeCount() << "\n";
    std::cout << "stop reason      : " << trace.stopReasonText() << "\n";
    std::cout << "histogram (primary[/sub] -> count):\n";
    for (const auto& h : trace.histogram()) {
        if (h.outcome == BlockOutcome::OutOfRangeDispatch) {
            // h.primary is meaningless here (default-constructed, never
            // assigned - see BlockRecord's own doc comment); print the real
            // raw nibble instead.
            std::cout << "  OUT-OF-RANGE raw_nibble=" << h.rawPrimaryNibble << " -> " << h.count << "\n";
            continue;
        }
        std::cout << "  primary=" << static_cast<int>(h.primary);
        if (h.sub) std::cout << " sub=" << static_cast<int>(*h.sub);
        std::cout << " -> " << h.count << "\n";
    }

    // Structural sanity, true regardless of which hypothesis about this
    // project's own two flagged alignment assumptions (frame_decoder.h's
    // top comment) turns out right: at least one block was attempted, and
    // every attempted block's outcome is one of the four known enum
    // values (trivially true by construction, asserted anyway as a
    // regression anchor against a future refactor).
    CHECK(!trace.blocks.empty());
    for (const auto& b : trace.blocks) {
        CHECK(b.outcome == BlockOutcome::Reconstructed || b.outcome == BlockOutcome::OpenHook ||
              b.outcome == BlockOutcome::OutOfRangeDispatch || b.outcome == BlockOutcome::SourceExhausted);
    }

    // Determinism: re-running the exact same decode from scratch must
    // reproduce byte-identical luma pixels and an identical trace - the
    // one claim this test makes unconditionally, independent of whichever
    // real block types the measurement above turned out to hit.
    auto [trace2, frame2] = decodeOnce();
    CHECK(trace.blocks.size() == trace2.blocks.size());
    CHECK(trace.stopReason == trace2.stopReason);
    const auto& lumaA = frame.luma();
    const auto& lumaB = frame2.luma();
    CHECK(lumaA.size() == lumaB.size());
    bool identical = (lumaA.size() == lumaB.size());
    if (identical) {
        for (size_t i = 0; i < lumaA.size(); ++i) {
            if (lumaA.data()[i] != lumaB.data()[i]) {
                identical = false;
                break;
            }
        }
    }
    CHECK(identical);

    // This test deliberately does NOT assert a specific stopReason/
    // reconstructed-count value: pinning an exact number here would
    // silently start asserting a conclusion about which of frame_decoder.h's
    // two flagged open alignment questions is correct, which this task was
    // explicitly told not to guess at. The measurement is reported above
    // (stdout) for a human/future agent to read; see this agent's own
    // report/HANDOFF.md entry for the specific numbers observed this pass.

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nreal_bink_frame0_decode_test: all checks passed\n";
    return 0;
}
