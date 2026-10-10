// Bink arbitrary-frame raw-byte extractor, built on tools/bink_frame0_probe.cpp's
// own clean-room header-parse + frame-offset-table-location machinery (same
// two spec files, same citations - this file adds no new reverse-engineered
// facts, it only generalizes frame0_probe's own "frame 0" extraction to an
// arbitrary frame index N so this task's FFmpeg black-box validation step
// can compare frame 60 (not just frame 0) against an independent oracle.
//
// Usage: bink_frameN_extract <input.bik> <frameIndex> <output_raw.bin>
//
// spec-bink-codec.md Sec1.6: frame N's compressed byte range is
// [table[N] masked, table[N+1] masked) - read directly as stored, same as
// frame0_probe's own extractFrame0Range(), generalized to any valid N.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

struct BinkHeader {
    char signature[5] = {0, 0, 0, 0, 0};
    uint32_t totalSizeField = 0;
    uint32_t numFrames = 0;
    uint32_t d3SizeShaped = 0;
    uint32_t d4BoundedCount = 0;
    uint32_t rawWidth = 0;
    uint32_t rawHeight = 0;
    uint32_t frameRateNum = 0;
    uint32_t frameRateDen = 0;
    uint32_t attributes = 0;
    uint32_t d10SmallCount = 0;
};

bool readHeader(const std::string& path, BinkHeader& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "could not open '" + path + "'";
        return false;
    }
    uint8_t raw[44];
    f.read(reinterpret_cast<char*>(raw), sizeof(raw));
    if (!f || f.gcount() != static_cast<std::streamsize>(sizeof(raw))) {
        error = "Error reading Bink header.";
        return false;
    }
    std::memcpy(out.signature, raw, 4);
    out.signature[4] = '\0';
    const bool sigOk = (std::memcmp(raw, "BIKf", 4) == 0) || (std::memcmp(raw, "BIKg", 4) == 0) ||
                       (std::memcmp(raw, "BIKh", 4) == 0) || (std::memcmp(raw, "BIKi", 4) == 0);
    if (!sigOk) {
        error = "Not a Bink file.";
        return false;
    }
    out.totalSizeField = readU32LE(raw + 0x04);
    out.numFrames = readU32LE(raw + 0x08);
    out.d3SizeShaped = readU32LE(raw + 0x0c);
    out.d4BoundedCount = readU32LE(raw + 0x10);
    out.rawWidth = readU32LE(raw + 0x14);
    out.rawHeight = readU32LE(raw + 0x18);
    out.frameRateNum = readU32LE(raw + 0x1c);
    out.frameRateDen = readU32LE(raw + 0x20);
    out.attributes = readU32LE(raw + 0x24);
    out.d10SmallCount = readU32LE(raw + 0x28);
    if (out.numFrames == 0) {
        error = "The file doesn't contain any compressed frames yet.";
        return false;
    }
    return true;
}

struct FrameOffsetTable {
    bool found = false;
    uint32_t start = 0;
    uint32_t count = 0;
    std::vector<uint32_t> raw;
};

// Same empirical search as bink_frame0_probe.cpp's locateFrameOffsetTable()
// (see that file's own top comment for the full methodology citation) -
// duplicated here rather than shared via a header, matching this project's
// existing convention of small standalone probe tools.
bool locateFrameOffsetTable(const std::vector<uint8_t>& file, uint32_t numFrames, uint32_t boundedCount,
                            FrameOffsetTable& out) {
    const uint32_t fileSize = static_cast<uint32_t>(file.size());
    constexpr uint32_t kHeaderEnd = 0x2c;
    constexpr uint32_t kMaxDescriptorScan = 256;

    std::vector<uint32_t> counts;
    for (uint32_t c : {numFrames, boundedCount, numFrames + 1, boundedCount + 1}) {
        if (c >= 2 && std::find(counts.begin(), counts.end(), c) == counts.end()) {
            counts.push_back(c);
        }
    }

    bool haveBest = false;
    struct Cand {
        uint32_t start = 0, count = 0;
        bool touchesTableEnd = false;
    };
    Cand best;

    for (uint32_t start = kHeaderEnd; start <= kHeaderEnd + kMaxDescriptorScan; start += 4) {
        for (uint32_t count : counts) {
            const uint64_t tableBytes = static_cast<uint64_t>(count) * 4ull;
            const uint64_t tableEnd = static_cast<uint64_t>(start) + tableBytes;
            if (tableEnd > fileSize) continue;

            std::vector<uint32_t> entries(count);
            for (uint32_t i = 0; i < count; ++i) {
                entries[i] = readU32LE(&file[start + static_cast<size_t>(i) * 4]);
            }
            bool monotonic = true, inRange = true;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t masked = entries[i] & ~1u;
                if (masked == 0 || masked > fileSize) inRange = false;
                if (i > 0) {
                    const uint32_t prevMasked = entries[i - 1] & ~1u;
                    if (!(masked > prevMasked)) monotonic = false;
                }
            }
            if (!(monotonic && inRange)) continue;

            Cand cand;
            cand.start = start;
            cand.count = count;
            cand.touchesTableEnd = ((entries[0] & ~1u) == tableEnd);
            const bool better =
                !haveBest || (cand.touchesTableEnd && !best.touchesTableEnd) ||
                (cand.touchesTableEnd == best.touchesTableEnd && cand.start < best.start);
            if (better) {
                best = cand;
                haveBest = true;
                out.found = true;
                out.start = start;
                out.count = count;
                out.raw = std::move(entries);
            }
        }
    }
    return out.found;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: bink_frameN_extract <input.bik> <frameIndex> <output_raw.bin>\n");
        return 1;
    }
    const std::string path = argv[1];
    const uint32_t frameIndex = static_cast<uint32_t>(std::stoul(argv[2]));
    const std::string outPath = argv[3];

    BinkHeader hdr;
    std::string error;
    if (!readHeader(path, hdr, error)) {
        std::printf("HEADER PARSE FAILED: %s\n", error.c_str());
        return 1;
    }
    std::printf("input: %s (numFrames=%u, %ux%u)\n", path.c_str(), hdr.numFrames, hdr.rawWidth, hdr.rawHeight);
    if (frameIndex >= hdr.numFrames) {
        std::printf("FATAL: frame index %u out of range (numFrames=%u)\n", frameIndex, hdr.numFrames);
        return 1;
    }

    std::vector<uint8_t> fileBytes;
    {
        std::ifstream wf(path, std::ios::binary);
        if (!wf) {
            std::printf("FATAL: could not reopen '%s'\n", path.c_str());
            return 1;
        }
        wf.seekg(0, std::ios::end);
        const std::streamoff sz = wf.tellg();
        wf.seekg(0, std::ios::beg);
        fileBytes.resize(static_cast<size_t>(sz));
        wf.read(reinterpret_cast<char*>(fileBytes.data()), sz);
    }

    FrameOffsetTable table;
    if (!locateFrameOffsetTable(fileBytes, hdr.numFrames, hdr.d4BoundedCount, table)) {
        std::printf("FATAL: could not locate frame-offset table\n");
        return 1;
    }
    std::printf("frame-offset table: start=0x%x count=%u\n", table.start, table.count);
    if (frameIndex + 1 >= table.count) {
        std::printf("FATAL: table has only %u entries, cannot bound frame %u\n", table.count, frameIndex);
        return 1;
    }

    const uint32_t e0 = table.raw[frameIndex];
    const uint32_t e1 = table.raw[frameIndex + 1];
    const bool keyframe = (e0 & 1u) != 0;
    const uint32_t startOffset = e0 & ~1u;
    const uint32_t endOffsetExclusive = e1 & ~1u;
    if (!(endOffsetExclusive > startOffset)) {
        std::printf("FATAL: frame %u's masked byte range is not positive\n", frameIndex);
        return 1;
    }
    const uint32_t sizeBytes = endOffsetExclusive - startOffset;
    if (endOffsetExclusive > fileBytes.size()) {
        std::printf("FATAL: frame %u's end offset exceeds the real file's size\n", frameIndex);
        return 1;
    }

    std::printf("frame %u: offset=0x%08x size=%u keyframe=%s\n", frameIndex, startOffset, sizeBytes,
                keyframe ? "yes" : "no");

    std::ofstream of(outPath, std::ios::binary);
    if (!of) {
        std::printf("FATAL: could not open '%s' for writing\n", outPath.c_str());
        return 1;
    }
    of.write(reinterpret_cast<const char*>(fileBytes.data() + startOffset), sizeBytes);
    if (!of) {
        std::printf("FATAL: write to '%s' did not complete\n", outPath.c_str());
        return 1;
    }
    std::printf("wrote %u bytes to '%s'\n", sizeBytes, outPath.c_str());
    return 0;
}
