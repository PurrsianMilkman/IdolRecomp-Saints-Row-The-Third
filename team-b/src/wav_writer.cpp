#include "sr3audioruntime/wav_writer.h"

#include <cstdio>
#include <cstring>
#include <fstream>

#include "sr3io/output_path_guard.h"

namespace sr3audioruntime {
namespace {

void putU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
void putU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
uint32_t getU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t getU16LE(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8); }

}  // namespace

bool writeWavPcm16(const std::string& path, const std::vector<int16_t>& pcmInterleaved, uint32_t sampleRate,
                    uint16_t channels) {
    std::string error;
    bool ok = writeWavPcm16(path, pcmInterleaved, sampleRate, channels, error);
    if (!ok && !error.empty()) std::fprintf(stderr, "writeWavPcm16: %s\n", error.c_str());
    return ok;
}

bool writeWavPcm16(const std::string& path, const std::vector<int16_t>& pcmInterleaved, uint32_t sampleRate,
                    uint16_t channels, std::string& error) {
    if (channels == 0) {
        error = "zero channels";
        return false;
    }

    // Output-path guard (include/sr3io/output_path_guard.h) - same check
    // src/png_writer.cpp's writePng() runs, added after the 2026-10-08
    // incident in which a capture tool's raw argv output path overwrote the
    // live sr3_city_0.vpp_pc archive. Runs before the std::ofstream below,
    // whose ios::trunc would otherwise destroy the target on open.
    if (!sr3io::isOutputPathSafe(path, ".wav", sr3io::kMaxOverwriteBytesWav, error)) return false;
    const uint16_t bitsPerSample = 16;
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataBytes = static_cast<uint32_t>(pcmInterleaved.size() * sizeof(int16_t));

    std::vector<uint8_t> header;
    header.reserve(44);
    header.insert(header.end(), {'R', 'I', 'F', 'F'});
    putU32LE(header, 36 + dataBytes);  // RIFF chunk size (everything after this field)
    header.insert(header.end(), {'W', 'A', 'V', 'E'});

    header.insert(header.end(), {'f', 'm', 't', ' '});
    putU32LE(header, 16);           // fmt sub-chunk size
    putU16LE(header, 1);            // PCM format tag
    putU16LE(header, channels);
    putU32LE(header, sampleRate);
    putU32LE(header, byteRate);
    putU16LE(header, blockAlign);
    putU16LE(header, bitsPerSample);

    header.insert(header.end(), {'d', 'a', 't', 'a'});
    putU32LE(header, dataBytes);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "could not open '" + path + "' for writing";
        return false;
    }
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!pcmInterleaved.empty()) {
        out.write(reinterpret_cast<const char*>(pcmInterleaved.data()), static_cast<std::streamsize>(dataBytes));
    }
    if (!out) {
        error = "write to '" + path + "' failed";
        return false;
    }
    return true;
}

double wavDurationSeconds(const std::string& path, bool& ok) {
    ok = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0.0;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (buf.size() < 44) return 0.0;
    if (std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0) return 0.0;

    // Walk sub-chunks from offset 12 looking for "fmt " and "data" - does
    // not assume they are the first two sub-chunks at fixed offsets, even
    // though this project's own writeWavPcm16() always emits them that way,
    // so a byte-identical re-read is never accidentally order-dependent.
    size_t offset = 12;
    uint32_t sampleRate = 0;
    uint16_t channels = 0, blockAlign = 0;
    uint32_t dataBytes = 0;
    bool haveFmt = false, haveData = false;
    while (offset + 8 <= buf.size()) {
        char tag[5] = {0};
        std::memcpy(tag, buf.data() + offset, 4);
        uint32_t chunkSize = getU32LE(buf.data() + offset + 4);
        size_t chunkDataOffset = offset + 8;
        if (chunkDataOffset + chunkSize > buf.size()) break;
        if (std::memcmp(tag, "fmt ", 4) == 0 && chunkSize >= 16) {
            channels = getU16LE(buf.data() + chunkDataOffset + 2);
            sampleRate = getU32LE(buf.data() + chunkDataOffset + 4);
            blockAlign = getU16LE(buf.data() + chunkDataOffset + 12);
            haveFmt = true;
        } else if (std::memcmp(tag, "data", 4) == 0) {
            dataBytes = chunkSize;
            haveData = true;
        }
        offset = chunkDataOffset + chunkSize + (chunkSize & 1u);
    }
    if (!haveFmt || !haveData || sampleRate == 0 || blockAlign == 0 || channels == 0) return 0.0;
    ok = true;
    return static_cast<double>(dataBytes) / static_cast<double>(blockAlign) / static_cast<double>(sampleRate);
}

}  // namespace sr3audioruntime
