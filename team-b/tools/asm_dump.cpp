// Small inspection CLI for .asm_pc manifest files (spec-asm-format.md
// sections 6-10, version 11). Rewritten 2026-09-20 with the sr3asm reader
// (HANDOFF §9.71).
//
// Usage: asm_dump <file.asm_pc> [--entries]
// Exit code: 0 = parsed, exactly consumed; 1 = error; 2 = parsed but bytes were left over.

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3asm/manifest.h"

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
        std::cerr << "usage: asm_dump <file.asm_pc> [--entries]\n";
        return 1;
    }
    const bool showEntries = argc > 2 && std::strcmp(argv[2], "--entries") == 0;

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        sr3asm::AsmManifest m =
            sr3asm::AsmManifest::parse(sr3asm::ByteView(bytes.data(), bytes.size()));

        std::cout << argv[1] << ": version=" << m.version() << " records=" << m.records().size()
                  << " entries=" << m.totalEntries() << " bytes=" << bytes.size()
                  << " consumed=" << m.bytesConsumed() << " trailing=" << m.trailingBytes() << "\n\n";

        static const char* kTableNames[3] = {"memory pools", "resource types", "container kinds"};
        for (size_t t = 0; t < m.fixedTables().size(); ++t) {
            const auto& table = m.fixedTables()[t];
            std::cout << "Table " << (t + 1) << " (" << kTableNames[t] << "): " << table.entries.size() << " entries\n";
            size_t shown = 0;
            for (const auto& e : table.entries) {
                if (shown >= 3) {
                    std::cout << "  ... (" << (table.entries.size() - shown) << " more)\n";
                    break;
                }
                std::cout << "  id=" << static_cast<int>(e.id) << " name='" << e.name << "'\n";
                ++shown;
            }
        }
        std::cout << "\n";

        for (const auto& r : m.records()) {
            const std::string* kind = m.kindTable().findName(r.containerKind);
            std::cout << "'" << r.name << "' kind=" << static_cast<int>(r.containerKind) << " ("
                      << (kind ? *kind : std::string("?")) << ") flags=0x" << std::hex << r.recordFlags << std::dec
                      << " entries=" << r.entries.size() << " headerRegion=" << r.headerRegionSize
                      << " payloadLength=" << r.payloadLength
                      << (r.sourceName.empty() ? "" : " source='" + r.sourceName + "'") << "\n";
            if (showEntries) {
                for (const auto& e : r.entries) {
                    std::cout << "    type=" << static_cast<int>(e.typeId) << " pool=" << static_cast<int>(e.poolId)
                              << " flags=0x" << std::hex << static_cast<int>(e.entryFlags) << std::dec
                              << " variant=" << static_cast<int>(e.variantSelect) << " primary=" << e.primarySize
                              << " secondary=" << e.secondarySize << " group=" << static_cast<int>(e.allocGroup)
                              << " '" << e.name << "'\n";
                }
            }
        }

        return m.trailingBytes() == 0 ? 0 : 2;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
