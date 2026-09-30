// Small inspection CLI for .fxo_pc compiled-shader wrapper files
// (spec-fxo-format.md). Uses the scan-based extraction recipe only.
//
// Usage: fxo_dump <file.fxo_pc>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3fxo/shader_wrapper.h"

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
        std::cerr << "usage: fxo_dump <file.fxo_pc>\n";
        return 1;
    }

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        sr3fxo::ShaderWrapper w =
            sr3fxo::ShaderWrapper::parse(sr3fxo::ByteView(bytes.data(), bytes.size()));

        std::cout << argv[1] << ": " << bytes.size() << " bytes, "
                  << w.shaders().size() << " embedded shader(s) found\n";

        for (size_t i = 0; i < w.shaders().size(); ++i) {
            const auto& s = w.shaders()[i];
            std::cout << "  shader " << i << ": " << (s.isVertexShader ? "vertex" : "pixel")
                      << " shader model " << static_cast<int>(s.versionMajor) << "."
                      << static_cast<int>(s.versionMinor) << ", offset=" << s.offset
                      << " length=" << s.length;
            if (i + 1 == w.shaders().size() && s.offset + s.length == bytes.size()) {
                std::cout << " (ends exactly at EOF)";
            }
            std::cout << "\n";
        }

        return w.shaders().empty() ? 2 : 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
