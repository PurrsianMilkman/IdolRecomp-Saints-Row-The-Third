// Small inspection CLI for the .cpeg_pc / .gpeg_pc paired texture container
// format (spec-texture-format.md). The paired .gpeg_pc is optional - without
// it, only the .cpeg_pc's own header/record/filename-table structure is
// shown; with it, offsets are additionally cross-validated against the
// paired file's real size.
//
// Usage: texture_dump <file.cpeg_pc> [file.gpeg_pc]

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3texture/texture_pair.h"

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
        std::cerr << "usage: texture_dump <file.cpeg_pc> [file.gpeg_pc]\n";
        return 1;
    }

    try {
        std::vector<uint8_t> cpegBytes = readFile(argv[1]);
        sr3texture::TexturePair t =
            sr3texture::TexturePair::parse(sr3texture::ByteView(cpegBytes.data(), cpegBytes.size()));

        std::cout << argv[1] << ": " << cpegBytes.size() << " bytes, version=" << t.version()
                  << ", pairedSize=" << t.pairedSize() << ", " << t.records().size()
                  << " texture(s)\n";

        std::vector<uint8_t> gpegBytes;
        bool haveGpeg = false;
        if (argc >= 3) {
            gpegBytes = readFile(argv[2]);
            t.validateAgainstGpeg(sr3texture::ByteView(gpegBytes.data(), gpegBytes.size()));
            haveGpeg = true;
            std::cout << argv[2] << ": " << gpegBytes.size()
                      << " bytes, validated against " << argv[1] << "\n";
        }

        for (size_t i = 0; i < t.records().size(); ++i) {
            const auto& r = t.records()[i];
            std::cout << "  [" << i << "] " << r.name << ": " << r.width << "x" << r.height
                      << " format=" << r.pixelFormat << " gpegOffset=" << r.gpegOffset
                      << " compressedSize=" << r.compressedSize;
            if (haveGpeg) {
                std::cout << " (verified in range)";
            }
            std::cout << "\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
