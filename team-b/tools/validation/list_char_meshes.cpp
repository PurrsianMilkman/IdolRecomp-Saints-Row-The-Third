// Lists every .ccmesh_pc in characters.vpp_pc (name, size), optionally
// filtered by a name substring, so a mesh/rig pairing distinct from
// `brad.ccmesh_pc` can be picked by hand (HANDOFF.md Sec9.56.1's open
// question: does a different mesh/clip pairing render cleanly?).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "vpp/container.h"

namespace {
std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) { f.seekg(0); f.read(reinterpret_cast<char*>(b.data()), n); }
    return b;
}
bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string g_filter;
void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        if (endsWith(e.name, ".ccmesh_pc") || endsWith(e.name, ".rig_pc")) {
            if (g_filter.empty() || lower(e.name).find(g_filter) != std::string::npos) {
                std::printf("  %s\n", e.name.c_str());
            }
        }
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: list_char_meshes <characters.vpp_pc> [nameSubstringFilter]\n");
        return 1;
    }
    if (argc >= 3) g_filter = lower(argv[2]);
    std::vector<uint8_t> b = readFile(argv[1]);
    if (b.empty()) { std::fprintf(stderr, "could not read %s\n", argv[1]); return 1; }
    vpp::Container c(vpp::ByteView(b.data(), b.size()));
    walk(c);
    return 0;
}
