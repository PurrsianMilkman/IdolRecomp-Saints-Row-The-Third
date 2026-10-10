// Dedicated coverage for include/sr3binkcodec/frame_decoder.h's plane
// sub-chunk span-field navigation (PlaneSubChunkSpan / readPlaneSubChunkSpan()
// / nextPlaneSubChunkPosition()) - the fix/bink-merge-group-bit-consumption
// follow-up task's own deliverable (spec-bink-codec.md Sec5.3g's "The
// absolute number" paragraph): each plane's own sub-chunk is preceded by a
// 4-byte little-endian span field; a plane's own real entropy-data start is
// that field's position + 4, and the NEXT plane's own span-field position is
// (this field's position + 4 + payloadSpan), algebraically identical to
// (this field's position + rawFieldValue).
//
// Two independent checks, same split as this project's other real_bink_*
// tests:
//   1. A SYNTHETIC, deterministic check (no real game data needed) - proves
//      the arithmetic and the bitstream-cursor side effect (readBits(32)
//      consumes exactly 4 bytes, leaving the reader at the plane's own data
//      start) against a hand-built buffer, independent of any real file.
//   2. A REAL-DATA check against tests/golden/bink_main_menu_bg/frame0_raw.bin
//      (same gitignored/local-only fixture, same (size, sha256) pin, as
//      tests/real_bink_test_vector_validation_test.cpp and tests/real_bink_
//      merge_group_trace_test.cpp already use) - confirms this helper's
//      output against spec-bink-codec.md Sec5.3g's own CONFIRMED numbers:
//      Y's own raw span-field value is 135028 (bytes `74 0f 02 00` at blob
//      offset 0), and chromaA's own real dispatch position - independently
//      live-measured TWICE (Sec5.3e's CTX_ECX capture, Sec5.3g's own second
//      triangulation) - is EXACTLY blob offset 135028 = 0 + 135028, with NO
//      further 4-byte skip on top. SKIPPED (exit 77) if the fixture is
//      absent or does not match its designated hash, same convention as
//      every other real_*_test in this repo.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "bink_sha256.h"
#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/frame_decoder.h"

#ifndef BINK_MMBG_GOLDEN_DIR
#define BINK_MMBG_GOLDEN_DIR "tests/golden/bink_main_menu_bg"
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

// Reused verbatim from tests/real_bink_test_vector_validation_test.cpp's own
// kFrame0Size/kFrame0Sha (same fixture, same pin) - not re-derived here, per
// this project's own convention for a shared gitignored fixture.
constexpr size_t kFrame0Size = 209288;
constexpr const char* kFrame0Sha = "613043f0f32ce10c0f058959bc0483a2f4fc0938c33bc971f0bb5825931e803c";

}  // namespace

int main() {
    std::cout << "bink_plane_subchunk_span_test: synthetic deterministic check\n";

    // Hand-built buffer: a 4-byte LE span field with raw value 20 (so
    // payloadSpan = 16), followed by 4 marker bytes a real decode would read
    // next (0xAA,0xBB,0xCC,0xDD) - picked arbitrarily, just far enough past
    // the field to prove the cursor landed exactly 4 bytes in, not short or
    // long by even one byte.
    {
        const uint8_t buf[8] = {20, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD};
        BitstreamReader br(buf, sizeof(buf));
        CHECK(br.bitsConsumedSoFar() == 0);

        const PlaneSubChunkSpan span = readPlaneSubChunkSpan(br);
        CHECK(span.rawFieldValue == 20);
        CHECK(span.payloadSpan == 16);
        CHECK(br.bitsConsumedSoFar() == 32);  // exactly 4 bytes consumed, nothing more/less

        // Cursor must now sit at this plane's own real data start (field
        // position 0 + 4) - the very next byte-aligned read must see 0xAA,
        // not a stale/shifted byte.
        const uint8_t nextByte = br.readAlignedByte();
        CHECK(nextByte == 0xAA);

        // nextPlaneSubChunkPosition(): from a span-field position of 0, the
        // NEXT plane's own span-field position must be 0 + 4 + 16 == 20 -
        // i.e. exactly the raw field value (4 + payloadSpan == rawFieldValue
        // by construction: 4 + 16 == 20), matching this file's own doc
        // comment's algebraic identity.
        const uint64_t nextPos = nextPlaneSubChunkPosition(0, span);
        CHECK(nextPos == 20);
        CHECK(nextPos == span.rawFieldValue);

        std::cout << "  rawFieldValue=" << span.rawFieldValue << " payloadSpan=" << span.payloadSpan
                  << " nextPlaneSubChunkPosition=" << nextPos << " (expected 20/16/20) - "
                  << ((span.rawFieldValue == 20 && span.payloadSpan == 16 && nextPos == 20) ? "OK" : "MISMATCH")
                  << "\n";
    }

    // A second synthetic case starting from a nonzero base position (10),
    // the way a real multi-plane blob would chain: proves the helper adds
    // onto an arbitrary starting position correctly, not just from 0.
    {
        const uint8_t buf[4] = {100, 0, 0, 0};  // rawFieldValue=100, payloadSpan=96
        BitstreamReader br(buf, sizeof(buf));
        const PlaneSubChunkSpan span = readPlaneSubChunkSpan(br);
        CHECK(span.rawFieldValue == 100);
        CHECK(span.payloadSpan == 96);
        const uint64_t nextPos = nextPlaneSubChunkPosition(10, span);
        CHECK(nextPos == 110);  // 10 + 4 + 96
        std::cout << "  (base=10) nextPlaneSubChunkPosition=" << nextPos << " (expected 110) - "
                  << (nextPos == 110 ? "OK" : "MISMATCH") << "\n";
    }

    std::cout << "\nbink_plane_subchunk_span_test: real-data check against main_menu_bg.bik frame 0\n";
    const std::string path = std::string(BINK_MMBG_GOLDEN_DIR) + "/frame0_raw.bin";
    const std::vector<uint8_t> frame0Raw = readFile(path);
    if (frame0Raw.empty()) {
        std::cout << "  SKIPPED (real-data part only) - golden fixture not found at '" << path
                   << "' (gitignored, local-only; same convention as every other real_*_test in this repo). "
                      "Synthetic checks above still ran and are reported via this test's own exit code.\n";
        if (g_failures != 0) {
            std::cerr << "\n" << g_failures << " check(s) failed (synthetic part)\n";
            return 1;
        }
        return 77;
    }
    const std::string actualSha = bink_test_sha256::computeSha256Hex(frame0Raw.data(), frame0Raw.size());
    if (frame0Raw.size() != kFrame0Size || actualSha != kFrame0Sha) {
        std::cout << "  SKIPPED (real-data part only) - golden fixture present but does not match its designated "
                      "hash (expected "
                   << kFrame0Size << " bytes / sha256 " << kFrame0Sha << ", got " << frame0Raw.size()
                   << " bytes / sha256 " << actualSha << "). Stopping rather than re-deriving it.\n";
        if (g_failures != 0) {
            std::cerr << "\n" << g_failures << " check(s) failed (synthetic part)\n";
            return 1;
        }
        return 77;
    }
    std::cout << "  golden fixture verified (" << frame0Raw.size() << " bytes, sha256 " << actualSha << ")\n";

    // Y's own span field sits at blob offset 0 (frame0Raw's own byte 0) -
    // spec-bink-codec.md Sec5.3g's own "The absolute number" paragraph.
    BitstreamReader brY(frame0Raw.data(), frame0Raw.size());
    const PlaneSubChunkSpan ySpan = readPlaneSubChunkSpan(brY);
    std::cout << "  Y's own span field: rawFieldValue=" << ySpan.rawFieldValue
              << " payloadSpan=" << ySpan.payloadSpan << " (Sec5.3g: bytes `74 0f 02 00` == 135028 decimal)\n";
    CHECK(ySpan.rawFieldValue == 135028);
    CHECK(ySpan.payloadSpan == 135024);

    const uint64_t chromaAPos = nextPlaneSubChunkPosition(0, ySpan);
    std::cout << "  computed chromaA span-field position=" << chromaAPos
              << " (Sec5.3g/Sec5.3e CONFIRMED real dispatch position, triangulated twice: 135028)\n";
    CHECK(chromaAPos == 135028);

    if (g_failures != 0) {
        std::cerr << "\n" << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nbink_plane_subchunk_span_test: all checks passed\n";
    return 0;
}
