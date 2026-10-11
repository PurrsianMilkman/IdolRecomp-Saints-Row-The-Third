#pragma once

// sr3binkcodec - the shared 8-point IDCT butterfly network, spec-bink-codec.md
// Sec5.3j Part 1 (2026-10-10), run twice per 8x8 block (column pass with
// dequantize, row pass with final pixel descale).
//
// CONFIRMED per Sec5.3j (bit-exact-verified against a real, independently
// FFmpeg-matched block, `postidct_block_frame0_row0_col80_VERIFIED.bin`):
//   - All 25 `>>0xb` (/2048) shifts in the function (13 column-pass, 12
//     row-pass: 8 per-row dequant + 4 rotation each pass, minus the DC
//     fast-path's own single extra shift counted separately) are PLAIN
//     truncating `sar` with ZERO rounding bias - no `add`/rounding constant
//     precedes any of them.
//   - The ONLY rounding-biased shift anywhere in the function is the final
//     per-pixel descale: `(sum + 0x7f) >> 8`, NOT `+0x80` - an exact tie
//     (sum == N*256+128) rounds DOWN, confirmed by literal immediate
//     inspection at all 8 descale sites (every one uses 0x7f, never 0x80).
//   - `0x10024b50` (inter/residual) and `0x100247d0` (sub=2's direct-store
//     sibling) use the IDENTICAL rounding rule - same four rotation
//     constants, same "plain sar,0xb" pattern, same final +0x7f/>>8 pixel
//     descale. The only differences are downstream of this shared network:
//     0x10024b50 adds a motion-compensated reference byte AFTER the
//     descale (not fused into it, no clamp); 0x100247d0 packs 4 descaled
//     bytes into one dword before the store (a packing-only difference).
//     This header models the shared math once; callers that need the
//     reference-add or dword-packing behaviour apply it on top of this
//     header's own plain `uint8_t` pixel output.
//
// HIGH CONFIDENCE, not independently re-verified this pass (per Sec5.3j):
// the inter/residual reference-add path (0x10024b50) is address-confirmed
// identical for the rounding/shift portion, but was not run through a
// second live P-frame worked example this pass - callers using this header
// for an inter block should not claim bit-exact P-frame verification the
// way the intra path has.

#include <array>
#include <cstdint>

namespace sr3binkcodec {

// Four rotation constants, spec-bink-codec.md Sec5.3j (CONFIRMED, same
// values at the same relative code positions in both the column and row
// pass, and in all three sibling IDCT entry points):
//   0xb50  = 2896  (sqrt2 fixed-point)
//   0xec8  = 3784
//   0xffffeb18 = -5352 (two's-complement 32-bit)
//   0x8a9  = 2217
constexpr int32_t kIdctRotSqrt2 = 0xb50;   // 2896
constexpr int32_t kIdctRot3784 = 0xec8;    // 3784
constexpr int32_t kIdctRotNeg5352 = -5352;  // 0xffffeb18 as signed
constexpr int32_t kIdctRot2217 = 0x8a9;    // 2217

// Plain truncating `sar reg, 0xb` (/2048, floor toward -infinity for
// negative values, matching x86 `sar` exactly) - every one of the 25
// intermediate shifts in the butterfly uses this, per Sec5.3j's own
// exhaustive address enumeration. A signed right-shift is
// implementation-defined pre-C++20 but is arithmetic (matches `sar`) on
// every compiler this project targets (MSVC/GCC/Clang, x86) - the same
// assumption the rest of this codebase's fixed-point code already makes.
inline int32_t sar11(int64_t v) { return static_cast<int32_t>(v >> 11); }

// One shared 8-point butterfly stage, spec-bink-codec.md Sec5.3j: takes 8
// already-dequantized (column pass) or already-column-pass-output (row
// pass) values `t[0..7]` and returns the 8 combined outputs in the EXACT
// order the spec's own final pixel-descale sites consume them:
//   out[0]=U+A  out[1]=F3+C  out[2]=I2+D  out[3]=B-G3
//   out[4]=G3+B out[5]=D-I2  out[6]=C-F3  out[7]=A-U
// (pixel[k] = sar(out[k] + 0x7f, 8) in the row pass; out[k] IS the column
// pass's own 8 dword outputs directly, no further transform, per Sec5.3j's
// row-pass citation: "Identical network, run on the column pass's own 8
// dword outputs directly... NO further table/quant multiply here").
inline std::array<int32_t, 8> idctButterfly8(const int32_t t[8]) {
    const int32_t b0 = t[0] + t[4];
    const int32_t d0 = t[0] - t[4];
    const int32_t s1 = t[2] + t[6];
    const int32_t d1 = t[2] - t[6];
    const int32_t e0 = sar11(static_cast<int64_t>(d1) * kIdctRotSqrt2) - s1;
    const int32_t A = s1 + b0;
    const int32_t B = b0 - s1;
    const int32_t C = e0 + d0;
    const int32_t D = d0 - e0;

    const int32_t P = t[3] + t[5];
    const int32_t M = t[5] - t[3];
    const int32_t Q = t[1] + t[7];
    const int32_t R = t[1] - t[7];
    const int32_t U = P + Q;
    const int32_t V = Q - P;
    const int32_t E2 = R + M;
    const int32_t H = sar11(static_cast<int64_t>(E2) * kIdctRot3784);
    const int32_t F = sar11(static_cast<int64_t>(M) * kIdctRotNeg5352);
    const int32_t F2 = F - U;
    const int32_t F3 = F2 + H;
    const int32_t G = sar11(static_cast<int64_t>(R) * kIdctRot2217);
    const int32_t G2 = G - H;
    const int32_t I = sar11(static_cast<int64_t>(V) * kIdctRotSqrt2);
    const int32_t I2 = I - F3;
    const int32_t G3 = G2 + I2;

    return {U + A, F3 + C, I2 + D, B - G3, G3 + B, D - I2, C - F3, A - U};
}

// Column pass (dequantize + first butterfly), spec-bink-codec.md Sec5.3j:
// one call per 8x8 block. `coeffs[64]` is the row-major (struct_byte_offset
// (k)=2k, k=r*8+c) signed-16-bit coefficient array (DC at k=0, AC at
// k=1..63 - zero for any AC position the tree decoder didn't resolve).
// `quantRow[64]` is one row of the intra/inter quantization matrix
// (kIntraQuantCompiled/kInterQuantCompiled, Sec5.3b), already unpacked to
// int32, row-major, quant_byte_offset(k)=4k - same k indexing as `coeffs`.
// `out[64]` receives the column pass's 8 dword outputs per column, same
// row-major k indexing, ready for idctRowPassDescale() below.
//
// AC-nonzero test (CONFIRMED, Sec5.3j): if every AC coefficient (k with
// r=k/8 != 0, i.e. rows 1..7) across the WHOLE block is zero, the DC-only
// fast path runs instead: `dc = sar(row0_raw * quant[r=0], 0xb)` (PLAIN
// sar, no rounding), broadcast to all 8 rows of that column.
inline void idctColumnPass(const int16_t coeffs[64], const int32_t quantRow[64], int32_t out[64]) {
    bool acNonZero = false;
    for (int k = 0; k < 64 && !acNonZero; ++k) {
        if ((k / 8) != 0 && coeffs[k] != 0) acNonZero = true;
    }

    for (int c = 0; c < 8; ++c) {
        if (!acNonZero) {
            const int32_t dc = sar11(static_cast<int64_t>(coeffs[c]) * quantRow[c]);
            for (int r = 0; r < 8; ++r) out[r * 8 + c] = dc;
            continue;
        }
        int32_t t[8];
        for (int r = 0; r < 8; ++r) {
            const int k = r * 8 + c;
            t[r] = sar11(static_cast<int64_t>(coeffs[k]) * quantRow[k]);
        }
        const std::array<int32_t, 8> o = idctButterfly8(t);
        for (int r = 0; r < 8; ++r) out[r * 8 + c] = o[r];
    }
}

// Row pass (final butterfly + descale to 8-bit pixels), spec-bink-codec.md
// Sec5.3j: runs the SAME network on the column pass's own 8 outputs per
// row (no further quant multiply), then applies the ONE rounding-biased
// shift in the whole function: `pixel = sar(sum + 0x7f, 8)`, per pixel (64
// total). Truncation to `uint8_t` matches the real code's own behaviour
// exactly (store low byte of a 32-bit register - "silent wraparound on
// overflow, no clamp instruction present", Sec5.3j).
inline void idctRowPassDescale(const int32_t colOut[64], uint8_t pixelsOut[64]) {
    for (int r = 0; r < 8; ++r) {
        int32_t t[8];
        for (int c = 0; c < 8; ++c) t[c] = colOut[r * 8 + c];
        const std::array<int32_t, 8> o = idctButterfly8(t);
        for (int c = 0; c < 8; ++c) {
            const int32_t px = (o[c] + 0x7f) >> 8;  // THE only rounding-biased shift - CONFIRMED +0x7f, not +0x80
            pixelsOut[r * 8 + c] = static_cast<uint8_t>(px);
        }
    }
}

// Convenience: full 8x8 intra IDCT (column pass + row pass) in one call.
inline void idctBlock8x8(const int16_t coeffs[64], const int32_t quantRow[64], uint8_t pixelsOut[64]) {
    int32_t colOut[64];
    idctColumnPass(coeffs, quantRow, colOut);
    idctRowPassDescale(colOut, pixelsOut);
}

}  // namespace sr3binkcodec
