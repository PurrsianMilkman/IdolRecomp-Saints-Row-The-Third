// Small inspection CLI for .anim_pc animation files (spec-anim-format.md).
// Shows the confirmed header: version, flags, the root-motion transform,
// and the optional section at +0x40. Does not touch the keyframe payload -
// see animation.h for why that is deliberately out of scope.
//
// Usage: anim_dump <file.anim_pc>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"

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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: anim_dump <file.anim_pc>\n";
        return 1;
    }

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        sr3anim::Animation a =
            sr3anim::Animation::parse(sr3anim::ByteView(bytes.data(), bytes.size()));

        std::cout << argv[1] << ": " << bytes.size() << " bytes\n";
        std::cout << "version=" << static_cast<int>(a.version()) << " flags=0x" << std::hex
                  << static_cast<int>(a.flags()) << std::dec;
        std::cout << (a.hasOptionalSection() ? " [optional section present]" : "") << "\n";

        std::cout << std::fixed << std::setprecision(6);
        const auto& q = a.rootRotation();
        std::cout << "root rotation (quaternion): x=" << q.x << " y=" << q.y << " z=" << q.z
                  << " w=" << q.w << "\n";
        float norm2 = a.rootRotationNormSquared();
        std::cout << "  norm^2=" << norm2
                  << (std::fabs(norm2 - 1.0f) <= 1e-4f ? "  (unit quaternion - as expected)"
                                                        : "  (NOT unit - unexpected, see spec Sec4)")
                  << "\n";

        const auto& t = a.rootTranslation();
        std::cout << "root translation: x=" << t.x << " y=" << t.y << " z=" << t.z;
        float mag = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        std::cout << "  |t|=" << mag << (mag == 0.0f ? "  (in-place animation)" : "") << "\n";

        std::cout << std::setprecision(0);
        if (a.hasTrailingOffset()) {
            std::cout << "+0x30 in-file offset: " << a.trailingOffset()
                      << (a.trailingOffset() <= bytes.size() ? " (within file)"
                                                              : " (OUT OF RANGE - unexpected)")
                      << " - target unidentified (spec Sec8 item 3)\n";
        }
        if (a.hasOptionalSectionOffset()) {
            if (a.optionalSectionIsNull()) {
                std::cout << "+0x40 optional section: null (-1)\n";
            } else {
                std::cout << "+0x40 optional section offset: " << a.optionalSectionOffset() << "\n";
            }
        }

        std::cout << "uninterpreted counts: +0x06=" << a.field_0x06_rawCount()
                  << " +0x08=" << static_cast<int>(a.field_0x08_rawCount())
                  << " +0x09=" << static_cast<int>(a.field_0x09_rawCount())
                  << " +0x0A=" << static_cast<int>(a.field_0x0A_rawCount())
                  << " +0x0B=" << static_cast<int>(a.field_0x0B_rawCount()) << "\n";
        std::cout << "(keyframe/track payload deliberately not parsed - spec Sec6)\n";

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
