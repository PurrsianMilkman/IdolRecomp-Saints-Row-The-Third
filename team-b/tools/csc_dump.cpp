// Small inspection CLI for .csc_pc cutscene camera scripts
// (spec-cutscene-camera-format.md). Shows the header, every shot's time
// span, and each shot's eight animation channels with their key counts -
// decoding the channels whose element width is confirmed. Channel 1 is a
// unit quaternion, 4 x s16 (x,y,z,w) x 16385/2^28 (spec Sec10, HANDOFF
// Sec9.73); this tool still prints it as raw bytes - the decoded values and
// the sampler live in the sr3cutscene library.
//
// Usage: csc_dump <file.csc_pc> [--keys]
//   --keys  also print individual keyframe times/values (verbose)

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "sr3cutscene/camera_script.h"

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("could not open file: " + path);
    }
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size)) {
        throw std::runtime_error("failed reading file: " + path);
    }
    return buf;
}

const char* channelLabel(size_t index) {
    switch (index) {
        case sr3cutscene::kChannelPosition:    return "position (vec3) [HIGH CONFIDENCE]";
        case sr3cutscene::kChannel1:           return "channel 1 (8 B) [quaternion, 4 x s16; raw]";
        case sr3cutscene::kChannelFieldOfView: return "field of view (rad) [HIGH CONFIDENCE]";
        default:                               return "optional group member [meaning HYPOTHESIS]";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: csc_dump <file.csc_pc> [--keys]\n";
        return 1;
    }
    bool showKeys = (argc > 2 && std::string(argv[2]) == "--keys");

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        sr3cutscene::ByteView content(bytes.data(), bytes.size());
        sr3cutscene::CameraScript s = sr3cutscene::CameraScript::parse(content);

        std::cout << argv[1] << ": " << bytes.size() << " bytes\n";
        std::cout << "version: " << s.version() << "\n";
        std::cout << "shot-record array @" << s.recordArrayOffset() << " (ends exactly at EOF)\n";
        std::cout << "header +0x08 raw: " << s.field_0x08_raw()
                  << " (padding; never read by the engine's parser)\n";
        std::cout << "shots: " << s.shots().size() << ", duration " << std::fixed
                  << std::setprecision(3) << s.duration() << " s\n";

        for (size_t i = 0; i < s.shots().size(); ++i) {
            const auto& shot = s.shots()[i];
            std::cout << "  shot " << std::setw(3) << i << " @" << shot.offset << "  ["
                      << std::setw(9) << shot.startTime << " .. " << std::setw(9) << shot.endTime
                      << "]";
            if (i > 0 && !shot.isContiguousWith(s.shots()[i - 1])) {
                std::cout << "  [NOT contiguous with the previous shot - unusual: this held "
                             "1,309/1,309 in shipped files]";
            }
            std::cout << "\n";

            for (size_t ch = 0; ch < sr3cutscene::kChannelCount; ++ch) {
                const auto& channel = shot.channels[ch];
                if (channel.keyCount == 0) continue;
                std::cout << "      ch" << ch << ": " << std::setw(4) << channel.keyCount
                          << " key(s) x " << channel.valueWidth << " B  " << channelLabel(ch)
                          << "\n";
                if (!showKeys) continue;

                if (channel.valueWidth == 12) {
                    auto values = sr3cutscene::decodeVec3(channel);
                    for (size_t k = 0; k < values.size(); ++k) {
                        std::cout << "         t=" << std::setw(8)
                                  << (k < channel.times.size() ? channel.times[k] : 0.0f) << "  ("
                                  << values[k][0] << ", " << values[k][1] << ", " << values[k][2]
                                  << ")\n";
                    }
                } else if (channel.valueWidth == 4) {
                    auto values = sr3cutscene::decodeFloats(channel);
                    for (size_t k = 0; k < values.size(); ++k) {
                        std::cout << "         t=" << std::setw(8)
                                  << (k < channel.times.size() ? channel.times[k] : 0.0f)
                                  << "  " << values[k] << "\n";
                    }
                } else {
                    // Channel 1: a unit quaternion (spec Sec10); dumped as
                    // raw bytes here, decoded by the library.
                    for (uint32_t k = 0; k < channel.keyCount; ++k) {
                        std::cout << "         t=" << std::setw(8)
                                  << (k < channel.times.size() ? channel.times[k] : 0.0f)
                                  << "  raw:";
                        size_t at = static_cast<size_t>(k) * channel.valueWidth;
                        for (size_t b = 0; b < channel.valueWidth && at + b < channel.valueBytes.size();
                             ++b) {
                            std::cout << " " << std::hex << std::setw(2) << std::setfill('0')
                                      << static_cast<int>(channel.valueBytes[at + b]) << std::dec
                                      << std::setfill(' ');
                        }
                        std::cout << "\n";
                    }
                }
            }
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
