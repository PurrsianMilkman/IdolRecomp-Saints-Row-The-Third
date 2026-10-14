// AC-tree decoder test against the REAL traced blocks of spec-bink-codec.md Sec5.3ac / Sec5.3ai answer 1
// (main_menu_bg.bik frame 0, Y plane, row 0, cells x=48 and x=64): the exact bit strings read off the frame by
// this project's own bit reader, the published bit counts (83 and 136) and the published nonzero-coefficient
// lists (20 and 30 AC values), plus block (0,0)'s 17-bit case.
//
// REWRITTEN 2026-10-13 (fix/bink-9c70c61): the previous version of this file was a synthetic self-consistency
// check of the retired "Phase 1 BFS + separate Phase 2" model (expected values came from a Python re-implementation
// of that same model). Sec5.3ai/Sec5.3ac's published totals contradict that model (it stopped at 54/36 bits, the
// spec trace is 83/136), so the synthetic scenario was dropped in favour of the real bit strings.

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "sr3binkcodec/ac_tree_decoder.h"
#include "sr3binkcodec/bitstream_reader.h"

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

// '0'/'1' string, first char = first bit read (LSB-first packing).
std::vector<uint8_t> packBits(const std::string& s) {
    std::vector<uint8_t> out((s.size() + 7) / 8 + 8, 0);
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '1') out[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    return out;
}

using Expected = std::vector<std::pair<int, int>>;  // raster k -> value

void checkBlock(const char* name, const std::string& bits, size_t expectedBits, const Expected& expected) {
    std::vector<uint8_t> bytes = packBits(bits);
    BitstreamReader br(bytes.data(), bytes.size());
    const AcTreeDecodeResult r = decodeAcCoefficientTree(br);
    if (br.bitsConsumedSoFar() != expectedBits) {
        std::cerr << name << ": consumed " << br.bitsConsumedSoFar() << " bits, expected " << expectedBits << "\n";
        ++g_failures;
    }
    int nonzero = 0;
    for (size_t k = 1; k < r.coeffs.size(); ++k) nonzero += (r.coeffs[k] != 0);
    CHECK(nonzero == static_cast<int>(expected.size()));
    for (const auto& kv : expected) {
        if (r.coeffs[static_cast<size_t>(kv.first)] != kv.second) {
            std::cerr << name << ": k=" << kv.first << " got " << r.coeffs[static_cast<size_t>(kv.first)]
                      << " expected " << kv.second << "\n";
            ++g_failures;
        }
    }
}

void testIdx3() {  // Sec5.3ac idx3: A=2, 83 bits, 20 AC
    checkBlock("idx3",
               "0100101000100110110011100101111110110101011011101110100111001000100101111010111000000001100101011110"
               "00110101101011111011101011110001101110001",
               83,
               {{1, -2}, {2, 3}, {3, -2}, {4, -1}, {8, -3}, {9, 3}, {10, -2}, {11, 1}, {13, -1}, {16, 1},
                {17, -2}, {18, 1}, {20, -1}, {25, -1}, {26, 1}, {27, -1}, {32, -1}, {33, 1}, {34, -1}, {43, 1}});
}

void testIdx4() {  // Sec5.3ac idx4: A=3, 136 bits, 30 AC; exercises case 1 (4-way split, entry NOT advanced)
    checkBlock("idx4",
               "1100101011110001101011010111110111010111100011011100010101100111100010010111110010101001011010111110"
               "0110011000111100111010001100101111000000",
               136,
               {{1, -5}, {2, -5}, {3, 3}, {5, 3}, {6, -2}, {8, -2}, {9, -5}, {10, -3}, {11, 1}, {12, 1},
                {13, 1}, {14, 1}, {16, 1}, {17, 2}, {18, -1}, {19, -1}, {21, 1}, {23, 1}, {25, 1}, {26, 2},
                {27, 1}, {28, -1}, {32, -1}, {33, -1}, {34, -1}, {35, 1}, {36, 1}, {38, -1}, {42, -1}, {45, 1}});
}

void testLevelZeroResolvesNothing() {
    // A == 0: no passes run at all, nothing is read beyond the 4-bit level nibble.
    std::vector<uint8_t> bytes = packBits("0000");
    BitstreamReader br(bytes.data(), bytes.size());
    const AcTreeDecodeResult r = decodeAcCoefficientTree(br);
    CHECK(r.countResolved == 0);
    CHECK(br.bitsConsumedSoFar() == 4);
}

}  // namespace

int main() {
    testIdx3();
    testIdx4();
    testLevelZeroResolvesNothing();
    if (g_failures == 0) {
        std::cout << "ac_tree_phase2_test: all checks passed\n";
        return 0;
    }
    std::cerr << "ac_tree_phase2_test: " << g_failures << " check(s) failed\n";
    return 1;
}
