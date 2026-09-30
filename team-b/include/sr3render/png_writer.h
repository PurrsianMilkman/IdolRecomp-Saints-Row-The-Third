// Minimal PNG writer, used as this engine's VERIFICATION BACKBONE rather
// than as a general image library.
//
// Why this exists at all: an engine claim like "it renders" is worth
// nothing if the only evidence is that no API call returned an error
// code. Every rendering milestone in this project is therefore verified
// by rendering to an offscreen target, reading the pixels back, and
// writing them to a PNG that a human (or an agent) can actually LOOK at.
// That is the "confirmed on-screen" confidence tier: a picture, not a
// return value.
//
// It also means the whole engine works headless - no window, no display,
// no GPU required (D3D11's WARP rasterizer covers the last case) - which
// matters in this sandbox.
//
// Implementation note: PNG needs DEFLATE and CRC-32, and this project
// already vendors zlib for the .vpp_pc container format, so both come
// free. No new dependency is introduced. The writer emits a single
// IHDR/IDAT/IEND 8-bit RGBA image with no interlacing, no palette and no
// ancillary chunks - the simplest thing that every viewer reads.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sr3render {

// Writes `width` x `height` 8-bit RGBA pixels (row-major, top row first,
// exactly width*height*4 bytes) to `path` as a PNG.
//
// Returns false and sets `error` on any failure - a bad size, a short
// buffer, a zlib failure, or a file that could not be opened/written.
// Deliberately does not throw: this is verification scaffolding and its
// callers are usually already in an error path.
bool writePng(const std::string& path, uint32_t width, uint32_t height,
              const std::vector<uint8_t>& rgba, std::string& error);

} // namespace sr3render
