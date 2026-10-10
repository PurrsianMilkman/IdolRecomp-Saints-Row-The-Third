#pragma once

// sr3binkcodec - limited-range YUV -> RGB conversion, FOR VIEWING ONLY.
//
// spec-bink-codec.md Sec5.6 is explicit and CONFIRMED: "No YUV->RGB
// colorspace conversion was found anywhere in this DLL's traced call
// chain" - if a conversion exists at all, "it happens entirely game-side
// (most plausibly a pixel shader at upload time)", which spec-bink-
// boundary.md's own Sec5.5 independently names as its single most
// load-bearing OPEN item (the "GPU-upload gap"). This file is NOT that
// game-side step, does not claim to model it, and is not a codec fact of
// any kind - it exists purely so a human (or an agent) can look at this
// project's own decoded planes as a picture, the same "confirmed on-screen"
// verification convention include/sr3render/png_writer.h's own doc comment
// describes for the rest of this engine.
//
// The matrix used is the standard limited-range ("TV range", CONFIRMED by
// spec-bink-codec.md Sec5.6's own ffprobe cross-check: color_range=tv,
// black/neutral point Y=16, U=V=128) BT.601 YCbCr->RGB conversion spec-
// bink-codec.md Sec5.6 itself cites the shape of: "RGB = matrix x
// (Y-16, U-128, V-128)". BT.601 (rather than BT.709) is used because this
// is a standard-definition-era codec (Bink 1.x) and spec-bink-codec.md
// names no other matrix; this is public colorimetry convention, not a
// reverse-engineered fact, exactly as the spec's own citation treats it.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "sr3binkcodec/planar_buffer.h"

namespace sr3binkcodec {

inline uint8_t clampToByte(int v) { return static_cast<uint8_t>(std::min(255, std::max(0, v))); }

// One limited-range YCbCr pixel -> RGB, BT.601, integer fixed-point
// (matching the "RGB = matrix x (Y-16,U-128,V-128)" shape spec-bink-
// codec.md Sec5.6 cites, scaled by 256 and rounded).
inline void limitedRangeYuvToRgb(uint8_t y, uint8_t u, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
    const int c = static_cast<int>(y) - 16;
    const int d = static_cast<int>(u) - 128;
    const int e = static_cast<int>(v) - 128;
    r = clampToByte((298 * c + 409 * e + 128) >> 8);
    g = clampToByte((298 * c - 100 * d - 208 * e + 128) >> 8);
    b = clampToByte((298 * c + 516 * d + 128) >> 8);
}

// Renders a full PlanarYuvFrame to an RGBA buffer at the LUMA plane's own
// resolution (nearest-neighbour chroma upsampling - 4:2:0, so each 2x2
// luma block shares one chroma sample, standard planar-YUV viewing
// practice, not a codec fact). `writtenMask`, if non-null, must be
// lumaWidth*lumaHeight bytes and nonzero wherever the LUMA plane holds a
// real reconstructed byte (not PlanarYuvFrame::kUndecodedSentinel) -
// unwritten pixels are rendered in a flat, clearly-synthetic magenta tint
// instead of a real (but fake) colour, so a viewer never mistakes "this
// project's own decoder hasn't reconstructed this part yet" for a real
// decoded result.
inline std::vector<uint8_t> renderFrameToRgba(const PlanarYuvFrame& frame, const std::vector<uint8_t>* writtenMask) {
    const int w = frame.dims().lumaWidth;
    const int h = frame.dims().lumaHeight;
    const int cw = frame.dims().chromaWidth();
    const int ch = frame.dims().chromaHeight();
    (void)ch;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);

    const uint8_t* yPlane = frame.luma().data();
    const uint8_t* uPlane = frame.chromaA().data();
    const uint8_t* vPlane = frame.chromaB().data();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t yi = static_cast<size_t>(y) * w + x;
            const bool written = !writtenMask || (*writtenMask)[yi] != 0;
            uint8_t r, g, b;
            if (written) {
                const int cx = std::min(x / 2, cw - 1);
                const int cy = std::min(y / 2, ch - 1);
                const uint8_t yy = yPlane[yi];
                const uint8_t uu = uPlane[static_cast<size_t>(cy) * cw + cx];
                const uint8_t vv = vPlane[static_cast<size_t>(cy) * cw + cx];
                limitedRangeYuvToRgb(yy, uu, vv, r, g, b);
            } else {
                // Deliberately synthetic/unrealistic "not decoded" tint -
                // never a real codec colour.
                r = 255;
                g = 0;
                b = 255;
            }
            const size_t o = yi * 4;
            rgba[o + 0] = r;
            rgba[o + 1] = g;
            rgba[o + 2] = b;
            rgba[o + 3] = 255;
        }
    }
    return rgba;
}

}  // namespace sr3binkcodec
