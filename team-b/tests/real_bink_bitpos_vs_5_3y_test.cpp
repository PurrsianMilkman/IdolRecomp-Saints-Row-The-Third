// Diagnostic (NOT a fix) measurement for the diag/bink-bitpos-vs-5.3y task:
// finds the FIRST point where THIS codebase's own real end-to-end Bink
// decode (block_dispatch.h/frame_decoder.h, real production path - the same
// pipeline tools/bink_ffmpeg_pixel_compare.cpp exercises) diverges from
// spec-bink-codec.md Sec5.3y's own live-Unicorn-captured ground-truth bit
// positions, for main_menu_bg.bik frame 0's Y-plane row 0, dispatch idx
// 0/3/4 (the three secondary=2 DC+AC/IDCT blocks in that row - idx1/idx2 are
// secondary=3 flat-fill, no AC-tree call, already separately fixed/out of
// scope here).
//
// Sec5.3y's own table (live Unicorn capture against the unmodified DLL/
// .bik), 3 points per block - dispatch (block-type read), AC-tree entry,
// AC-tree exit, all measured on ONE shared, coherently-advancing bit-reader
// struct (the Y sub-chunk's own entropy pre-stage + per-block nibble read +
// AC-tree decoder, all the same context address):
//
//   idx0: dispatch=13619 entry=13619 exit=13636
//   idx3: dispatch=13640 entry=13640 exit=13723
//   idx4: dispatch=13727 entry=13727 exit=13863
//
// This test reuses block_dispatch.h's own real production decodeOneBlock()
// path (via frame_decoder.h's decodePlaneRasterOrder(), restricted to row 0
// only for speed) - NOT a hand-rolled reimplementation - with the same
// BlockRecord::dispatchBitPos/acEntryBitPos/acExitBitPos instrumentation this
// task added to block_dispatch.h (diagnostic-only fields, zero behavior
// change - see that header's own top comment on BlockRecord).
//
// Same gitignored/local-only fixture and SKIPPED (exit 77) convention as
// every other real_bink_*_test.cpp in this repo (tests/golden/
// bink_main_menu_bg/frame0_raw.bin) - never re-derived from anywhere else.
// Reports PASS/FAIL based on whether the FIRST divergence point (if any) is
// found, per this task's own explicit instruction - this is a measurement,
// not a correctness assertion about the decoder's own end-to-end accuracy
// (which is already known-wrong per block_dispatch.h/frame_decoder.h's own
// standing notes).

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "bink_sha256.h"
#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/block_dispatch.h"
#include "sr3binkcodec/entropy_prestage.h"
#include "sr3binkcodec/entropy_prestage_runtime.h"
#include "sr3binkcodec/frame_decoder.h"
#include "sr3binkcodec/planar_buffer.h"

#ifndef BINK_MMBG_GOLDEN_DIR
#define BINK_MMBG_GOLDEN_DIR "tests/golden/bink_main_menu_bg"
#endif

using namespace sr3binkcodec;

namespace {

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

// Reused verbatim from tests/real_bink_flat_fill_prestage_test.cpp /
// tests/real_bink_dc_prestage_gap_test.cpp - same fixture, same pin.
constexpr size_t kFrame0Size = 209288;
constexpr const char* kFrame0Sha = "613043f0f32ce10c0f058959bc0483a2f4fc0938c33bc971f0bb5825931e803c";
constexpr int kLumaWidthPixels = 1920;
constexpr int kLumaHeightPixels = 1080;

struct Point {
    const char* label;
    int blockCol;
    size_t theirsDispatch;
    size_t theirsEntry;
    size_t theirsExit;
};

// spec-bink-codec.md Sec5.3y's own table, row 0 only, idx0/3/4 (the three
// secondary=2 AC-tree blocks).
constexpr Point kGroundTruth[] = {
    {"idx0", 0, 13619, 13619, 13636},
    {"idx3", 3, 13640, 13640, 13723},
    {"idx4", 4, 13727, 13727, 13863},
};

bool reportPoint(const char* pointLabel, size_t ours, size_t theirs, bool& firstDivergenceFound,
                  std::string& firstDivergenceReport) {
    const bool match = (ours == theirs);
    std::cout << "  " << pointLabel << ": ours=" << ours << " spec=" << theirs << " -> "
              << (match ? "MATCH" : "DIVERGE") << "\n";
    if (!match && !firstDivergenceFound) {
        firstDivergenceFound = true;
        const long long delta = static_cast<long long>(ours) - static_cast<long long>(theirs);
        firstDivergenceReport = std::string(pointLabel) + ": ours=" + std::to_string(ours) +
                                 " spec=" + std::to_string(theirs) + " delta=" + std::to_string(delta) + " bits";
    }
    return match;
}

}  // namespace

int main() {
    if (!caseSelectorTablesAvailable()) {
        std::cout << "real_bink_bitpos_vs_5_3y_test: SKIPPED - Bink case-selector static tables not "
                      "available (no compiled-in data, and no runtime loader data).\n";
        return 77;
    }

    const std::string path = std::string(BINK_MMBG_GOLDEN_DIR) + "/frame0_raw.bin";
    const std::vector<uint8_t> frame0Raw = readFile(path);
    if (frame0Raw.empty()) {
        std::cout << "real_bink_bitpos_vs_5_3y_test: SKIPPED - golden fixture not found at '" << path
                   << "' (gitignored, local-only; same convention as every other real_*_test in this repo).\n";
        return 77;
    }
    const std::string actualSha = bink_test_sha256::computeSha256Hex(frame0Raw.data(), frame0Raw.size());
    if (frame0Raw.size() != kFrame0Size || actualSha != kFrame0Sha) {
        std::cout << "real_bink_bitpos_vs_5_3y_test: SKIPPED - golden fixture present but does not match its "
                      "designated hash (expected "
                   << kFrame0Size << " bytes / sha256 " << kFrame0Sha << ", got " << frame0Raw.size()
                   << " bytes / sha256 " << actualSha << ").\n";
        return 77;
    }
    std::cout << "real_bink_bitpos_vs_5_3y_test: golden fixture verified (" << frame0Raw.size() << " bytes, sha256 "
              << actualSha << ")\n";
    if (frame0Raw.size() < 4) {
        std::cout << "real_bink_bitpos_vs_5_3y_test: SKIPPED - fixture too small.\n";
        return 77;
    }

    // Cold BitstreamReader at Y-plane's own sub-chunk DATA start (frame0_raw.bin
    // byte offset 4 - skipping the 4-byte sub-chunk span field directly rather
    // than through br, so br.bitsConsumedSoFar()==0 at the Y sub-chunk's own
    // data start) - matching spec-bink-codec.md Sec5.3y's own stated reference
    // point ("measured from the Y sub-chunk's own data start") and the SAME
    // convention tests/real_bink_flat_fill_prestage_test.cpp/real_bink_dc_
    // prestage_gap_test.cpp already use for this exact fixture.
    BitstreamReader br(frame0Raw.data() + 4, frame0Raw.size() - 4);

    FrameDimensions dims = FrameDimensions::fromRoundedLuma(kLumaWidthPixels, kLumaHeightPixels);
    PlanarYuvFrame frame;
    frame.allocate(dims);
    PlaneView dest = frame.luma().view();
    const int cols = blockGridCols(dims.lumaWidth);

    CaseSelectorScratchSource caseSelectorA(br, dims.lumaWidth);
    CaseSelectorScratchSource caseSelectorB(br, dims.lumaWidth, caseSelectorLengthFieldBitWidthB,
                                             kDescriptorBSiblingTableIndex);

    // Real production path, row 0 only (frame_decoder.h's
    // decodePlaneRasterOrder(), the SAME function decodeLumaPlane() calls for
    // every row of a real end-to-end decode) - not a hand-rolled
    // reimplementation of the row-0 sequence.
    DecodeTrace trace = decodePlaneRasterOrder(br, dest, /*prev=*/nullptr, cols, /*blockRows=*/1, &caseSelectorA,
                                                &caseSelectorB, /*decodeDcArray=*/true);

    std::cout << "\n--- row 0 trace: " << trace.blocks.size() << " block(s) attempted, stop reason: "
              << trace.stopReasonText() << " ---\n";

    bool firstDivergenceFound = false;
    std::string firstDivergenceReport;
    int pointsChecked = 0;

    for (const auto& gt : kGroundTruth) {
        const BlockRecord* rec = nullptr;
        for (const auto& b : trace.blocks) {
            if (b.blockRow == 0 && b.blockCol == gt.blockCol) {
                rec = &b;
                break;
            }
        }
        std::cout << "\n" << gt.label << " (blockCol=" << gt.blockCol << "):\n";
        if (rec == nullptr) {
            std::cout << "  NOT REACHED by this decode (trace stopped before this block was attempted) - "
                      << "this IS itself the first divergence: the real decode's own row-0 trace never got "
                      << "this far.\n";
            if (!firstDivergenceFound) {
                firstDivergenceFound = true;
                firstDivergenceReport = std::string(gt.label) + ": block not reached at all (trace stopped earlier)";
            }
            break;
        }
        if (!rec->dispatchBitPos) {
            std::cout << "  dispatchBitPos not recorded (unexpected - should always be set)\n";
            break;
        }
        ++pointsChecked;
        bool ok =
            reportPoint("dispatch", *rec->dispatchBitPos, gt.theirsDispatch, firstDivergenceFound, firstDivergenceReport);
        if (!ok) break;

        if (!rec->acEntryBitPos || !rec->acExitBitPos) {
            std::cout << "  AC-entry/AC-exit not recorded (block did not reach the AC-tree call - outcome="
                       << static_cast<int>(rec->outcome) << ", primary=" << static_cast<int>(rec->primary);
            if (rec->sub) std::cout << " sub=" << static_cast<int>(*rec->sub);
            std::cout << ") - this IS itself the first divergence from the spec's expectation that this block "
                      << "reaches the AC-tree.\n";
            if (!firstDivergenceFound) {
                firstDivergenceFound = true;
                firstDivergenceReport = std::string(gt.label) + ": block did not reach the AC-tree call at all";
            }
            break;
        }
        ++pointsChecked;
        ok = reportPoint("AC-entry", *rec->acEntryBitPos, gt.theirsEntry, firstDivergenceFound, firstDivergenceReport);
        if (!ok) break;

        ++pointsChecked;
        ok = reportPoint("AC-exit", *rec->acExitBitPos, gt.theirsExit, firstDivergenceFound, firstDivergenceReport);
        if (!ok) break;
    }

    std::cout << "\n=== RESULT (" << pointsChecked << "/9 points checked before stopping) ===\n";
    if (firstDivergenceFound) {
        std::cout << "FIRST DIVERGENCE: " << firstDivergenceReport << "\n";
    } else if (pointsChecked == 9) {
        std::cout << "NO DIVERGENCE: all 9 points (idx0/3/4 dispatch/AC-entry/AC-exit) match spec-bink-codec.md "
                      "Sec5.3y exactly. The bit-position tracking is NOT the bug - any remaining error must be in "
                      "per-symbol VALUE decode (magnitude/sign) or a later stage.\n";
    } else {
        std::cout << "INCONCLUSIVE: stopped before reaching 9 points but reported no numeric mismatch above (see "
                      "the per-block NOT REACHED/not-recorded notes above for why).\n";
    }

    // This is a diagnostic/measurement task, not a correctness assertion -
    // always exit 0 (when the fixture itself verified) so ctest records this
    // as a clean run whose own stdout IS the deliverable, per this task's
    // explicit "do not attempt to fix anything" + "report precisely" scope.
    return 0;
}
