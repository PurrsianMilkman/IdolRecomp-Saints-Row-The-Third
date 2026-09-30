// Throwaway investigation tool: lists every real entry name in a given
// archive (recursively, into nested Raw containers) whose name contains a
// given substring (case-insensitive) - used here to find the exact real
// names of tile 1018's own files (.czh_pc/.czn_pc/.gzn_pc/.asm_pc/.str2_pc)
// before writing anything that assumes a naming convention.
//
// Usage: tile1018_list <archive.vpp_pc> <substring>

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> buf;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return buf;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0) {
        buf.resize(static_cast<size_t>(size));
        if (fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear();
    }
    fclose(f);
    return buf;
}

std::string lowerV(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

void walk(const vpp::Container& c, const std::string& needle, const std::string& path, int depth) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        if (lowerV(e.name).find(needle) != std::string::npos) {
            const char* kind = e.payload.kind == vpp::PayloadKind::Raw ? "RAW" : "COMPRESSED";
            std::printf("%*s%s%s  [%s, len=%zu]\n", depth * 2, "", path.c_str(), e.name.c_str(), kind,
                        static_cast<size_t>(e.payload.length));
        }
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = c.openNested(i);
                walk(nested, needle, path + e.name + "::", depth + 1);
            } catch (const std::exception&) {
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tile1018_list <archive.vpp_pc> <substring>\n");
        return 1;
    }
    std::vector<uint8_t> archive = readFile(argv[1]);
    if (archive.empty()) {
        std::fprintf(stderr, "could not read %s\n", argv[1]);
        return 1;
    }
    std::string needle = lowerV(argv[2]);
    try {
        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        walk(c, needle, "", 0);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
