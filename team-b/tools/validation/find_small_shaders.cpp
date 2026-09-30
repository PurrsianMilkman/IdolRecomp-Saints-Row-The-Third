// One-shot scan (not part of the deliverable set) to find small real
// shaders exercising specific opcodes, as candidates for the numeric-
// verification sample. Prints name/stage/instruction-count/opcode-list for
// every blob under --max-instrs whose opcode set intersects the requested
// set, sorted by instruction count ascending.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/errors.h"
#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/wrapper_header.h"
#include "vpp/container.h"

namespace fs = std::filesystem;
namespace {
std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated || r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}
struct Cand { std::string name; const char* stage; size_t nInstr; std::string opcodes; };
std::vector<Cand> g_cands;
std::set<sr3d3d9bc::Opcode> g_want;
size_t g_maxInstr = 40;

void processBlob(const std::string& name, const char* stage, const std::vector<uint8_t>& data, size_t off, size_t len) {
    vpp::ByteView v(data.data() + off, len);
    sr3d3d9bc::DisassembledShader d;
    try { d = sr3d3d9bc::disassemble(v); } catch (const sr3d3d9bc::FormatError&) { return; }
    if (d.instructions.size() > g_maxInstr) return;
    bool hit = false;
    std::set<std::string> ops;
    for (auto& i : d.instructions) {
        ops.insert(sr3d3d9bc::opcodeName(i.opcodeRaw) ? sr3d3d9bc::opcodeName(i.opcodeRaw) : "?");
        if (g_want.count(i.opcode)) hit = true;
    }
    if (!hit) return;
    std::string joined;
    for (auto& o : ops) joined += o + " ";
    g_cands.push_back({name, stage, d.instructions.size(), joined});
}

void walk(const vpp::Container& c, const std::string& path) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        std::string ln = lower(e.name);
        if (endsWith(ln, ".fxo_pc")) {
            std::vector<uint8_t> data;
            if (!entryBytes(c, i, data) || data.empty()) continue;
            vpp::ByteView view(data.data(), data.size());
            sr3fxo::WrapperHeader h; std::string why;
            if (sr3fxo::WrapperHeader::tryParse(view, h, why)) {
                size_t end = 0;
                for (auto& b : h.layoutBlobs(end)) {
                    if (b.length == 0 || b.offset + b.length > data.size()) continue;
                    if (b.stage == sr3fxo::Stage::Middle) continue;
                    processBlob(path + "/" + e.name, b.stage == sr3fxo::Stage::Vertex ? "vertex" : "pixel", data, b.offset, b.length);
                }
            }
            continue;
        }
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i), path + "/" + e.name); } catch (...) {}
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    g_want = {sr3d3d9bc::Opcode::MAD, sr3d3d9bc::Opcode::DP3, sr3d3d9bc::Opcode::DP4,
              sr3d3d9bc::Opcode::LRP, sr3d3d9bc::Opcode::SINCOS, sr3d3d9bc::Opcode::CMP};
    std::string archive = argc > 1 ? argv[1] : "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/shaders.vpp_pc";
    auto bytes = readFile(archive);
    vpp::Container c{vpp::ByteView(bytes.data(), bytes.size())};
    walk(c, fs::path(archive).filename().string());
    std::sort(g_cands.begin(), g_cands.end(), [](const Cand& a, const Cand& b) { return a.nInstr < b.nInstr; });
    for (auto& cd : g_cands) printf("%4zu instr | %-6s | %-50s | %s\n", cd.nInstr, cd.stage, cd.name.c_str(), cd.opcodes.c_str());
    printf("\n%zu candidates\n", g_cands.size());
    return 0;
}
