// Real-data regression for the Bink decoder skeleton (include/sr3binkcodec/
// *.h) against frame 0 of TWO of the game's own real shipped D10>=1 logo
// movies (`videos/logo_thq_and_voli_hd.bik`, 1280x720; `videos/
// logo_amd_hd.bik`, 1920x1080), both extracted by tools/bink_frame0_probe.cpp
// per spec-bink-codec.md Sec1.5/Sec1.5a/Sec1.6; frozen at
// tests/golden/bink_logo_frame0/frame0_raw.bin and tests/golden/
// bink_logo_amd_hd_frame0/frame0_raw.bin respectively (both gitignored,
// local-only).
//
// Per this task's own explicit instruction: verify each golden file's MD5
// before using it; if either is missing or does not match, stop and report
// (exit 77, same SKIPPED convention every other real_*_test in this repo
// uses - see tests/real_vorbis_decode_test.cpp) rather than re-deriving it
// from anywhere else.
//
// This test is NOT a claim that this project's decoder fully reconstructs
// frame 0. It reports, and regression-pins, exactly what the decoder
// skeleton's own dispatch trace measures against the real bytes - which
// block-type path(s) it hit, how many blocks were fully reconstructed
// before the first OPEN/out-of-range stop, and why it stopped there. See
// include/sr3binkcodec/frame_decoder.h's own top comment for the full
// scope/alignment citation trail.
//
// FIX (2026-10-09, fix/bink-d10plus-audio-chunk-skip): both fixtures below
// are D10>=1 ("one or more audio tracks") files per spec-bink-codec.md
// Sec1.6 - each frame's blob leads with D10 per-track audio chunks (a
// 4-byte little-endian chunk-size field followed by that many payload
// bytes, repeated once per track) BEFORE the video chunk begins. The PRIOR
// pass here passed hasLeadingSubChunkSpan=false, treating blob byte 0 as
// the video chunk's own span field directly - wrong for a D10>=1 file, and
// the reason forcing hasLeadingSubChunkSpan=true unconditionally used to
// desync immediately. decodeOnce() below now calls
// sr3binkcodec::skipAudioChunks(br, numAudioTracks) first (both real
// fixtures here have exactly one audio track, D10=1), THEN passes
// hasLeadingSubChunkSpan=true - the video chunk's own leading span field is
// the same shape already confirmed for D10=0 files once the audio prefix is
// correctly skipped. See frame_decoder.h's own top comment for the full
// citation trail.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "bink_md5.h"
#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/block_dispatch.h"
#include "sr3binkcodec/entropy_prestage_runtime.h"
#include "sr3binkcodec/frame_decoder.h"
#include "sr3binkcodec/planar_buffer.h"
#include "sr3binkcodec/yuv_to_rgb.h"
#include "sr3runtimedata/runtime_data_loader.h"

#ifndef BINK_FRAME0_GOLDEN_PATH
#define BINK_FRAME0_GOLDEN_PATH "tests/golden/bink_logo_frame0/frame0_raw.bin"
#endif
#ifndef BINK_LOGO_AMD_HD_GOLDEN_PATH
#define BINK_LOGO_AMD_HD_GOLDEN_PATH "tests/golden/bink_logo_amd_hd_frame0/frame0_raw.bin"
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

// Runs the full frame-0 luma decode + regression checks against one real
// D10>=1 fixture, returning true if the fixture was present/verified and
// actually decoded (false = SKIPPED for THIS fixture specifically - the
// caller decides what that means for the test's own overall exit code).
// NOTE: the compiled-tables-available gate (2026-10-10, runtime data loader
// task) is checked ONCE in main() below, before either fixture runs here -
// not per-fixture, since it is a single global prerequisite (the runtime
// loader either populated the shared case-selector tables or it didn't).
bool runFixture(const std::string& label, const std::string& path, const char* expectedMd5, size_t expectedSize,
                int lumaWidth, int lumaHeight, int numAudioTracks) {
    const std::vector<uint8_t> raw = readFile(path);
    if (raw.empty()) {
        std::cout << "real_bink_frame0_decode_test: SKIPPED (" << label << ") - golden fixture not found at '"
                   << path
                   << "' (gitignored, local-only; see this task's own instruction: stop and report rather than "
                      "re-deriving it from anywhere else).\n";
        return false;
    }

    const std::string actualMd5 = bink_test_md5::computeMd5Hex(raw.data(), raw.size());
    if (raw.size() != expectedSize || actualMd5 != expectedMd5) {
        std::cout << "real_bink_frame0_decode_test: SKIPPED (" << label
                   << ") - golden fixture present but does not match its designated hash (expected " << expectedSize
                   << " bytes / md5 " << expectedMd5 << ", got " << raw.size() << " bytes / md5 " << actualMd5
                   << "). Per this task's own instruction, stopping rather than re-deriving it.\n";
        return false;
    }

    std::cout << "real_bink_frame0_decode_test (" << label << "): golden fixture verified (" << raw.size()
              << " bytes, md5 " << actualMd5 << ")\n";

    auto decodeOnce = [&]() {
        BitstreamReader br(raw.data(), raw.size());
        FrameDimensions dims = FrameDimensions::fromRoundedLuma(lumaWidth, lumaHeight);
        PlanarYuvFrame frame;
        frame.allocate(dims);
        // Frame 0 is a keyframe (confirmed by tools/bink_frame0_probe.cpp's
        // own extraction, the frame-offset table's own bit 0, spec-bink-
        // codec.md Sec1.6) - there is no real previous frame to copy from,
        // so block-copy blocks (if any are hit) get no invented source
        // pixels (see decodeOneBlock's own doc comment).
        //
        // FIX (2026-10-09, fix/bink-d10plus-audio-chunk-skip): this fixture
        // is a spec-bink-codec.md Sec1.6 D10>=1 ("one or more audio tracks")
        // file - its blob leads with `numAudioTracks` per-track audio
        // chunks before the video chunk begins. skipAudioChunks() walks
        // past exactly those chunks; only THEN is `br` positioned at the
        // video chunk's own leading span field, so
        // hasLeadingSubChunkSpan=true from here on - see
        // include/sr3binkcodec/frame_decoder.h's own top comment for the
        // full citation trail (this replaces the prior pass's
        // hasLeadingSubChunkSpan=false, which treated blob byte 0 as the
        // video chunk directly and was wrong for a D10>=1 file).
        skipAudioChunks(br, numAudioTracks);
        DecodeTrace trace = decodeLumaPlane(br, frame, nullptr, /*hasLeadingSubChunkSpan=*/true);
        return std::make_pair(std::move(trace), std::move(frame));
    };

    auto [trace, frame] = decodeOnce();

    std::cout << "\n--- Luma-plane dispatch trace (spec-bink-codec.md Sec5.3/Sec5.3a) [" << label << "] ---\n";
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

    // Structural sanity, true regardless of exactly which block type the
    // real decode happens to hit first: at least one block was attempted,
    // and every attempted block's outcome is one of the known enum values
    // (trivially true by construction, asserted anyway as a regression
    // anchor against a future refactor).
    //
    // UPDATED 2026-10-11 (fix/bink-dc-array-indexing task): added
    // BlockOutcome::CaseSelectorPoisoned and BlockOutcome::PreStageOpen to
    // this allow-list. Both are pre-existing, already-modelled real decode
    // outcomes (entropy_prestage.h's CaseSelectorStatus::Poisoned is a
    // CONFIRMED permanent decode state per spec-bink-codec.md Sec5.3g, and
    // tests/real_bink_test_vector_validation_test.cpp already treats it as
    // an ordinary possible trace stop) - this test's own allow-list had
    // simply never needed them because neither real fixture here had ever
    // produced them before. This task's own per-row DC delta-array decode
    // (dc_delta_array.h) now consumes real bits from `br` on EVERY row for
    // EVERY file decoded via decodeLumaPlane() (not just main_menu_bg.bik -
    // Sec5.3p's own finding is that `0x1001a160` genuinely does this in the
    // real binkw32.dll too), which shifts this fixture's (logo_amd_hd.bik)
    // own per-row bit alignment enough that its trace now legitimately hits
    // CaseSelectorPoisoned instead of the pre-fix run's OpenHook stop - a
    // real behaviour change from a real production-path fix, not a new
    // failure mode this project invented, so the allow-list is widened to
    // match rather than the test weakened to ignore it.
    CHECK(!trace.blocks.empty());
    for (const auto& b : trace.blocks) {
        CHECK(b.outcome == BlockOutcome::Reconstructed || b.outcome == BlockOutcome::OpenHook ||
              b.outcome == BlockOutcome::OutOfRangeDispatch || b.outcome == BlockOutcome::SourceExhausted ||
              b.outcome == BlockOutcome::CaseSelectorPoisoned || b.outcome == BlockOutcome::PreStageOpen);
    }

    // NOT asserted, reported only (same "no invented fixes" convention the
    // pre-fix version of this test already used for stopReason/reconstructed-
    // count): whether block (0,0) itself got past Reconstructed. Measured
    // result this pass, AFTER the audio-chunk-skip fix: both real fixtures
    // still stop, but for two DIFFERENT, non-fix reasons, not because the
    // fix is wrong.
    //
    // CORRECTED 2026-10-09 (fix/bink-pixel-accuracy-oracle task): the
    // PRIOR version of this comment claimed logo_amd_hd.bik's block (0,0)
    // dispatches to primary=1/sub=5 (TwoLiteralPattern) - this is WRONG on
    // two independent counts, now reconciled: (1) spec-bink-codec.md Sec5.3's
    // own extended TwoLiteralPattern paragraph (2026-10-09 live-execution
    // trace) explicitly states that specific claim "does not hold under live
    // execution - that exact block dispatches into secondary=3 (Flat/DC-fill)
    // instead, and a full-frame live scan found zero secondary=5 occurrences
    // anywhere in that frame's 3 planes"; (2) this project's OWN decoder
    // (entropy_prestage.h's case-selector simulation, not a raw-nibble read)
    // independently reproduces that same finding on real bytes: this exact
    // fixture now measures primary=1/sub=3 (FlatDcFill) x 600 blocks, then
    // stops at block 601 on primary=3 (Giant1FullResTwin, an
    // ALREADY-DOCUMENTED, pre-existing OPEN gap - the giant run-length engine's
    // own bit-level token format, nothing to do with D10/audio chunks or
    // TwoLiteralPattern). TwoLiteralPattern (sub=5) is now itself CONFIRMED
    // and Reconstructed (block_dispatch.h's twoLiteralPattern16x16(), same
    // task) but is simply never dispatched anywhere in THIS file's frame 0 -
    // see spec-bink-codec.md Sec5.3 for the real worked example instead
    // (main_menu_bg.bik, 183 real secondary=5 occurrences in its own Y-plane).
    // logo_thq_and_voli_hd.bik's block (0,0) gets a valid primary=1 too, but
    // then fails the SEPARATE sub-nibble range check (read directly off `br`,
    // not through the case-selector pre-stage) - plausibly an
    // entropy_prestage.h edge case for this file's own unusually tiny Y-plane
    // payload (216 bytes, Sec1.6's own `220` span-field value minus 4 - far
    // smaller than main_menu_bg.bik's 135024-byte Y payload every prior
    // entropy-pre-stage validation pass used), not something this task's own
    // D10 audio-chunk-skip fix caused or is scoped to chase further
    // (entropy_prestage.h's own algorithm predates this task and is out of
    // scope here). Both are genuinely separate, already-named gaps, not
    // silently swept under this fix - see this task's own report for the
    // full account.
    std::cout << "got past block (0,0) (Reconstructed)? "
              << ((trace.blocks.size() > 1 || trace.blocks.front().outcome == BlockOutcome::Reconstructed) ? "yes"
                                                                                                             : "no")
              << "\n";

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
    // reconstructed-count value beyond "got past block (0,0)": pinning an
    // exact number here would silently start asserting a conclusion about
    // downstream gaps (ChromaA/ChromaB chaining, the DC "+128" bug, etc.)
    // this task was explicitly told not to chase. The measurement is
    // reported above (stdout) for a human/future agent to read.
    return true;
}

}  // namespace

int main() {
    // 2026-10-10 (runtime data loader task): load the Bink static tables
    // from the player's own game install, independently SHA-256-verified,
    // if they aren't already compiled in - see include/sr3runtimedata/
    // runtime_data_loader.h. A cheap no-op in the ordinary dev build. Gates
    // BOTH fixtures below (a single shared prerequisite, not per-fixture).
    sr3runtimedata::ensureRuntimeDataLoaded();
    if (!caseSelectorTablesAvailable()) {
        std::cout << "real_bink_frame0_decode_test: SKIPPED - Bink case-selector static tables not "
                      "available (no compiled-in data, and the runtime loader found no verified "
                      "data at the configured game install path).\n";
        return 77;
    }

    // Both fixtures are D10=1 (one audio track) real shipped logo-splash
    // assets, per spec-bink-codec.md Sec1.6's own two worked examples.
    const bool ranThq = runFixture("logo_thq_and_voli_hd.bik frame0", BINK_FRAME0_GOLDEN_PATH,
                                    "99a631dfd434b5a5e1c68f9a4a477cfe", 29336, 1280, 720, /*numAudioTracks=*/1);
    const bool ranAmd = runFixture("logo_amd_hd.bik frame0", BINK_LOGO_AMD_HD_GOLDEN_PATH,
                                    "92516b68ed75eabdeab9aaa90d50d07a", 27420, 1920, 1080, /*numAudioTracks=*/1);

    if (!ranThq && !ranAmd) {
        std::cout << "real_bink_frame0_decode_test: SKIPPED - neither golden fixture is present/verified, see "
                      "individual SKIPPED lines above.\n";
        return 77;
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nreal_bink_frame0_decode_test: all checks passed\n";
    return 0;
}
