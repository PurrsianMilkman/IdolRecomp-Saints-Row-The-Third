#include "sr3render/png_writer.h"

#include <cstdio>
#include <cstring>

#include "zlib.h"

namespace sr3render {

namespace {

void appendU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

// PNG chunk: length, type, data, CRC-32 over (type + data). zlib's crc32()
// is the same polynomial PNG specifies, so it is reused directly.
void appendChunk(std::vector<uint8_t>& out, const char type[4],
                 const std::vector<uint8_t>& data) {
    appendU32BE(out, static_cast<uint32_t>(data.size()));
    size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out.data() + crcStart, static_cast<uInt>(4 + data.size()));
    appendU32BE(out, static_cast<uint32_t>(crc));
}

} // namespace

bool writePng(const std::string& path, uint32_t width, uint32_t height,
              const std::vector<uint8_t>& rgba, std::string& error) {
    if (width == 0 || height == 0) {
        error = "zero width or height";
        return false;
    }
    size_t expected = static_cast<size_t>(width) * height * 4;
    if (rgba.size() != expected) {
        error = "pixel buffer is " + std::to_string(rgba.size()) + " bytes, expected " +
                std::to_string(expected) + " (" + std::to_string(width) + "x" +
                std::to_string(height) + " RGBA)";
        return false;
    }

    // Raw PNG image data: each scanline prefixed with a filter-type byte.
    // Filter 0 (None) throughout - simplest correct choice; DEFLATE still
    // does the compression work.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 4));
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgba.data() + static_cast<size_t>(y) * width * 4;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4);
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressedSize);
    int zr = compress2(compressed.data(), &compressedSize, raw.data(),
                       static_cast<uLong>(raw.size()), Z_BEST_SPEED);
    if (zr != Z_OK) {
        error = "zlib compress2 failed with code " + std::to_string(zr);
        return false;
    }
    compressed.resize(compressedSize);

    std::vector<uint8_t> png;
    const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), signature, signature + 8);

    std::vector<uint8_t> ihdr;
    appendU32BE(ihdr, width);
    appendU32BE(ihdr, height);
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // colour type 6 = truecolour with alpha (RGBA)
    ihdr.push_back(0); // compression method (DEFLATE)
    ihdr.push_back(0); // filter method
    ihdr.push_back(0); // interlace: none
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", compressed);
    appendChunk(png, "IEND", {});

    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || f == nullptr) {
        error = "could not open '" + path + "' for writing";
        return false;
    }
    size_t written = fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    if (written != png.size()) {
        error = "short write to '" + path + "' (" + std::to_string(written) + " of " +
                std::to_string(png.size()) + " bytes)";
        return false;
    }
    return true;
}

} // namespace sr3render
