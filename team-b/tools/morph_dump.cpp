// Small inspection CLI for .cmorph_pc morph-target files
// (spec-morph-format.md). Walks the container and reports the target
// directory, descriptors and bulk runs. Does not decode the quantised
// components - see morph_file.h for why that is deliberately out of scope.
//
// Usage: morph_dump <file.cmorph_pc> [--elements N]

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "sr3morph/morph_file.h"

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
        std::cerr << "usage: morph_dump <file.cmorph_pc> [--elements N]\n";
        return 1;
    }
    size_t showElements = 0;
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--elements") {
            showElements = static_cast<size_t>(std::stoul(argv[i + 1]));
        }
    }

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        sr3morph::ByteView content(bytes.data(), bytes.size());
        sr3morph::MorphFile m = sr3morph::MorphFile::parse(content);

        std::cout << argv[1] << ": " << bytes.size() << " bytes\n";
        std::cout << "mode=" << m.mode() << " targets=" << m.targets().size()
                  << " descriptors=" << m.descriptors().size()
                  << " total elements=" << m.totalElementCount() << "\n";
        std::cout << "structural walk landed exactly on end of file, trailing 0x0BADBEEF sentinel at "
                  << m.trailerOffset() << " (spec Sec4 exact-size replay)\n";

        std::cout << std::fixed << std::setprecision(4);
        size_t shown = 0;
        for (size_t i = 0; i < m.descriptors().size(); ++i) {
            const auto& d = m.descriptors()[i];
            std::cout << "  descriptor " << i << " (target " << d.targetIndex << ", id=0x"
                      << std::hex << m.targets()[d.targetIndex].id << std::dec << "): N="
                      << d.affectedVertexCount << " maxVertexIndex=" << d.maxVertexIndex
                      << " bulk@" << d.bulkOffset << " (" << d.bulkByteLength << " bytes)\n";
            std::cout << "      paramsA=(" << d.paramsA[0] << ", " << d.paramsA[1] << ", "
                      << d.paramsA[2] << ")  paramsB=(" << d.paramsB[0] << ", " << d.paramsB[1]
                      << ", " << d.paramsB[2] << ")  [dequantisation params - formula OPEN, spec Sec7]\n";

            for (size_t e = 0; e < d.affectedVertexCount && shown < showElements; ++e, ++shown) {
                sr3morph::Element el = m.elementAt(i, e, content);
                std::cout << "        element " << e << ": vertexIndex=" << el.vertexIndex
                          << "  quantised=(" << el.component0 << ", " << el.component1 << ", "
                          << el.component2 << ")  +8=" << el.field_8 << " +10=" << el.field_10
                          << "\n";
            }
        }
        if (showElements > 0) {
            std::cout << "(quantised components shown raw - not decoded, spec Sec7)\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
