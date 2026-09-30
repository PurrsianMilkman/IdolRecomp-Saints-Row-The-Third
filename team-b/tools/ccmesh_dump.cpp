// Small inspection CLI for .ccmesh_pc/.csmesh_pc files: the shared
// material block (spec-geometry-format.md Sec3.1) plus the geometry
// block's six raw arrays (Sec4.1) and Mesh-sub-block anchor (Sec4.1.1).
//
// Usage: ccmesh_dump <file.ccmesh_pc>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"

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
        std::cerr << "usage: ccmesh_dump <file.ccmesh_pc>\n";
        return 1;
    }

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        sr3geometry::ByteView content(bytes.data(), bytes.size());

        sr3geometry::MaterialBlock material = sr3geometry::MaterialBlock::parse(content);
        std::cout << argv[1] << ": " << bytes.size() << " bytes\n";
        std::cout << "material block: " << material.textureSlotCount << " texture(s), size="
                  << material.totalSize << "\n";
        for (const auto& name : material.textureNames) {
            std::cout << "  - " << name << "\n";
        }

        sr3geometry::GeometryBlock geom = sr3geometry::GeometryBlock::parse(content, material);
        std::cout << "geometry block: offset=" << geom.offset() << " version=" << geom.version()
                  << " flags=" << geom.flags() << "\n";
        for (size_t i = 0; i < geom.arrays().size(); ++i) {
            const auto& a = geom.arrays()[i];
            std::cout << "  array " << (i + 1) << ": count=" << a.count << " stride=" << a.stride
                      << " offset=" << a.offset << " bytes=" << a.byteLength() << "\n";
        }
        if (geom.hasMeshSubBlock()) {
            std::cout << "Mesh sub-block found at offset " << geom.meshSubBlockOffset()
                      << " (version==9 confirmed)\n";
        } else {
            std::cout << "no Mesh sub-block found at the predicted offset (" << geom.meshSubBlockOffset()
                      << ")\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
