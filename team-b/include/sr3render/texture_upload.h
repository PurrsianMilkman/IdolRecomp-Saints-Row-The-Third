// Uploads a real SR3 texture (a .cpeg_pc/.gpeg_pc pair, read by
// sr3texture) into a D3D11 shader resource.
//
// FORMAT MAPPING - REWRITTEN against spec-texture-format.md Sec11. An older
// revision of this module treated codes 400/402 as a byte-ratio HYPOTHESIS
// and refused 403 ("several 16-bit layouts fit; do not guess"). The spec now
// closes the whole table: the pixel-format code names the Direct3D 9 format
// the game itself requests (Sec11.1, read from the game's format switch),
// and Sec11.2 settles 403 by both the switch and a decode of a real texture.
// Evidence tier for what is mapped here:
//
//     400 D3DFMT_DXT1     -> BC1_UNORM       direct upload   (spec: switch + decode + size gate)
//     402 D3DFMT_DXT5     -> BC3_UNORM       direct upload   (spec: switch + decode + size gate)
//     701 D3DFMT_DXT5     -> BC3_UNORM       direct upload   (spec: accepted by the game, never shipped)
//     403 D3DFMT_R5G6B5   -> R8G8B8A8_UNORM  CPU-expanded    (spec Sec11.2: switch + decode + size gate)
//     401 D3DFMT_DXT3     -> BC2_UNORM       direct upload   (spec: switch + size gate; never decoded to pixels)
//     407 D3DFMT_A8R8G8B8 -> B8G8R8A8_UNORM  direct upload   (spec: switch + size gate; never decoded to pixels)
//     410 D3DFMT_A8       -> A8_UNORM        direct upload   (spec: switch + size gate; never decoded to pixels)
//
// 403 is expanded on the CPU (sr3texture::expandR5G6B5ToRgba8) rather than
// handed to DXGI_FORMAT_B5G6R5_UNORM: 16-bit-per-pixel formats are an
// OPTIONAL capability on some D3D11 hardware, while an RGBA8 copy works on
// every device and its bit layout comes straight from the spec's own text
// (R = bits 15-11, G = 10-5, B = 4-0).
//
// Codes 404-406 and 408-409 are accepted by the game but appear in zero
// shipped records (spec Sec11.1); they are reported as unsupported rather
// than mapped without any data to check the mapping against.
//
// The level chain comes from sr3texture::computeLevelLayout (spec Sec10.5)
// driven by the record's explicit level count (+0x23) - NOT from "consume
// levels until the declared size is used up" as the older code did.

#pragma once

#include <cstdint>
#include <string>

#include "sr3texture/texture_pair.h"

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace sr3render {

// How a pixel-format code was turned into a D3D11 resource, kept distinct
// from whether the upload succeeded.
enum class FormatMapping {
    Direct,      // the code's D3D9 format has an identical DXGI format; the game's bytes are handed to D3D11 unchanged
    CpuExpanded, // the bytes are widened to RGBA8 on the CPU first (403)
    Unsupported, // a code accepted by the game but with no shipped data (404-406, 408-409), or not a code at all
};

struct UploadedTexture {
    ID3D11ShaderResourceView* srv = nullptr; // caller owns; Release() when done
    uint32_t width = 0;
    uint32_t height = 0;    // a cube record reports its stacked 6 x edge height
    uint32_t mipLevels = 0;
    uint32_t pixelFormatCode = 0;
    FormatMapping mapping = FormatMapping::Unsupported;
    std::string dxgiFormatName; // e.g. "BC1_UNORM", for logging

    // Total bytes the level chain is COMPUTED to occupy (spec Sec10.5),
    // alongside the record's own declared size (+0x24). Reported separately
    // and compared by the caller rather than silently trusted: the spec has
    // them equal on 76,650/76,650 paired shipped records.
    size_t computedMipChainBytes = 0;
    size_t declaredBytes = 0;
};

// Uploads texture `index` of `pair` from the paired .gpeg_pc pixel data.
// Returns false with `error` set if the index is out of range, the pixel
// format is not one this module maps, the pixel data would run past the end
// of `gpegBytes`, the record is a multi-level cube map (a shape no shipped
// file has, spec Sec10.5), or D3D11 rejects the resource. A single-level
// cube record is uploaded as one 2D texture of edge x 6*edge texels - the
// six faces stacked, exactly as they lie in the file.
bool uploadTexture(ID3D11Device* device, const sr3texture::TexturePair& pair, size_t index,
                   sr3texture::ByteView gpegBytes, UploadedTexture& out, std::string& error);

// Releases the SRV held by `texture` and nulls it. Provided so callers can
// clean up without including d3d11.h - this header deliberately only
// forward-declares the D3D types so that consumers of the engine's texture
// API don't drag the whole Direct3D header in. Safe to call on an
// already-released or never-populated UploadedTexture.
void releaseTexture(UploadedTexture& texture);

} // namespace sr3render
