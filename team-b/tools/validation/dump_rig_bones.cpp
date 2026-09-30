// Quick bone dump for a named .rig_pc inside an archive - which bones are
// roots, their names, parents, rest positions. Ad hoc diagnostic for
// stage-2 animpose wiring (checking why certain high-index tracks carry
// world-scale translation deltas - hypothesis: they map to SECONDARY
// ROOTS in a multi-root rig, e.g. prop/weapon attachment points, which
// the bone-length-preservation check correctly excludes since a root has
// no parent to measure a pair distance against).
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "sr3rig/rig.h"
#include "vpp/container.h"

namespace {
std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) { f.seekg(0); f.read(reinterpret_cast<char*>(b.data()), n); }
    return b;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated &&
        r.status != vpp::DecodeStatus::RecoveredSharedStream &&
        r.status != vpp::DecodeStatus::RecoveredSharedStreamLongChain) return false;
    out = std::move(r.data);
    return true;
}
bool findRig(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { if (findRig(c.openNested(i), name, out)) return true; } catch (const std::exception&) {}
        }
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_rig_bones <characters.vpp_pc> <name.rig_pc>\n");
        return 1;
    }
    std::vector<uint8_t> archive = readFile(argv[1]);
    std::vector<uint8_t> rb;
    {
        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        if (!findRig(c, argv[2], rb)) { std::fprintf(stderr, "rig not found\n"); return 1; }
    }
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size()));
    const auto& bones = rig.bones();
    std::printf("%zu bones\n", bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        const auto& b = bones[i];
        std::printf("  [%2zu] %-24s parent=%3d %s rest=(%.3f,%.3f,%.3f)\n", i, b.name.c_str(),
                    b.isRoot() ? -1 : static_cast<int>(b.parentIndex), b.isRoot() ? "ROOT" : "    ",
                    static_cast<double>(b.restPosition[0]), static_cast<double>(b.restPosition[1]),
                    static_cast<double>(b.restPosition[2]));
    }
    return 0;
}
