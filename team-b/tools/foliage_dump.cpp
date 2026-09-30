// Small inspection CLI for .cfmesh_pc foliage meshes
// (spec-foliage-format.md). Shows the material block, outer block, the
// embedded Mesh sub-block's location, the inline material definitions, the
// texture-name table and the trailing LOD/fade table.
//
// Usage: foliage_dump <file.cfmesh_pc>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "sr3foliage/foliage_mesh.h"

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
        std::cerr << "usage: foliage_dump <file.cfmesh_pc>\n";
        return 1;
    }

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        sr3foliage::ByteView content(bytes.data(), bytes.size());
        sr3foliage::FoliageMesh f = sr3foliage::FoliageMesh::parse(content);

        std::cout << argv[1] << ": " << bytes.size() << " bytes\n";
        std::cout << "material block: " << f.materialBlock().textureSlotCount
                  << " texture(s), size=" << f.materialBlock().totalSize << "\n";
        for (const auto& n : f.materialBlock().textureNames) std::cout << "  - " << n << "\n";

        std::cout << "outer block @" << f.outerBlockOffset() << " version=" << f.version()
                  << " materials=" << f.materialCount() << "\n";
        if (f.hasMeshSubBlock()) {
            std::cout << "embedded Mesh sub-block @" << f.meshSubBlockOffset()
                      << " (version 9 confirmed; internals deliberately not parsed)\n";
        }

        std::cout << std::fixed << std::setprecision(3);
        for (size_t i = 0; i < f.materials().size(); ++i) {
            const auto& m = f.materials()[i];
            std::cout << "  material " << i << " @" << m.offset << " size=" << m.declaredSize
                      << " shaderHash=0x" << std::hex << m.shaderHash << " variantHash=0x"
                      << m.variantHash << " flags=0x" << m.flags << std::dec << "\n";
            std::cout << "    shape (bindings, constants, vec4s) = (" << m.textureBindings.size()
                      << ", " << m.constantNameHashes.size() << ", " << m.constantValues.size()
                      << ")\n";
            for (const auto& b : m.textureBindings) {
                std::cout << "      texture: " << (b.name.empty() ? "<unresolved>" : b.name)
                          << "  slotHash=0x" << std::hex << b.slotHash << std::dec
                          << " flags=0x" << std::hex << b.flags << std::dec << "\n";
            }
            for (const auto& v : m.constantValues) {
                std::cout << "      vec4: (" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3]
                          << ")\n";
            }
        }

        std::cout << "texture-name table: " << f.textureNames().size() << " name(s)\n";
        for (const auto& n : f.textureNames()) std::cout << "  - " << n << "\n";

        std::cout << "LOD/fade table: " << f.lodRecords().size()
                  << " record(s) (spec Sec11.2, CONFIRMED)\n";
        for (const auto& r : f.lodRecords()) {
            std::cout << "  fadeIn " << r.fadeInStart << ".." << r.fadeInEnd << "  fadeOut "
                      << r.fadeOutStart << ".." << r.fadeOutEnd << "  drawGroup "
                      << r.drawGroupIndex << "  billboard " << r.billboardFlag << "\n";
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
