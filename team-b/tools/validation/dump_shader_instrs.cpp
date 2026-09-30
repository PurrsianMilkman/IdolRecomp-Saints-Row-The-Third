// One-shot diagnostic (not part of the deliverable set): prints full
// per-instruction detail (dest/source register kind+number+mask/swizzle/
// modifier, DEF literal values) for one named real shader, so the numeric-
// verification sample's fixed inputs/expected outputs can be hand-computed
// against the REAL instruction stream rather than guessed.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/wrapper_header.h"
#include "vpp/container.h"

using namespace sr3d3d9bc;

namespace {
std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
std::string lower(std::string s) { for (auto& c : s) if (c>='A'&&c<='Z') c = char(c-'A'+'a'); return s; }

void printSwiz(const Swizzle& s) { printf(".%c%c%c%c", "xyzw"[s.x], "xyzw"[s.y], "xyzw"[s.z], "xyzw"[s.w]); }
void printMask(const WriteMask& m) {
    printf(".");
    if (m.x) printf("x"); if (m.y) printf("y"); if (m.z) printf("z"); if (m.w) printf("w");
}
const char* regKindName(uint32_t raw) {
    switch (raw) { case 0: return "r"; case 1: return "v"; case 2: return "c"; case 3: return "a/t";
        case 6: return "o"; case 7: return "i"; case 8: return "oC"; case 9: return "oDepth";
        case 10: return "s"; case 14: return "b"; default: return "?"; }
}
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: dump_shader_instrs <archive> <fxo-entry-name>\n"); return 1; }
    auto bytes = readFile(argv[1]);
    vpp::Container c{vpp::ByteView(bytes.data(), bytes.size())};
    std::string want = lower(argv[2]);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (lower(c.entries()[i].name) != want) continue;
        auto r = c.decompressEntry(i);
        std::vector<uint8_t> data = c.entries()[i].payload.kind == vpp::PayloadKind::Raw
                                         ? std::vector<uint8_t>(c.rawEntryBytes(i).data(), c.rawEntryBytes(i).data() + c.rawEntryBytes(i).size())
                                         : r.data;
        vpp::ByteView view(data.data(), data.size());
        sr3fxo::WrapperHeader h; std::string why;
        if (!sr3fxo::WrapperHeader::tryParse(view, h, why)) { printf("no header: %s\n", why.c_str()); return 1; }
        size_t end = 0;
        for (auto& b : h.layoutBlobs(end)) {
            if (b.length == 0 || b.stage == sr3fxo::Stage::Middle) continue;
            printf("\n=== %s stage, offset %zu, length %zu ===\n", b.stage == sr3fxo::Stage::Vertex ? "VERTEX" : "PIXEL",
                   b.offset, b.length);
            vpp::ByteView blob(data.data() + b.offset, b.length);
            DisassembledShader d = disassemble(blob);
            ConstantTable ct = readConstantTable(blob, d);
            printf("CTAB: status=%d constants=%zu\n", (int)ct.status, ct.constants.size());
            for (auto& cc : ct.constants)
                printf("  %s: set=%d index=%u count=%u type=%u rows=%u cols=%u\n", cc.name.c_str(),
                       (int)cc.registerSet, cc.registerIndex, cc.registerCount, cc.type.typeRaw, cc.type.rows,
                       cc.type.columns);
            for (size_t ii = 0; ii < d.instructions.size(); ++ii) {
                const auto& inst = d.instructions[ii];
                printf("[%2zu] %-10s ", ii, opcodeName(inst.opcodeRaw) ? opcodeName(inst.opcodeRaw) : "?");
                if (inst.dest) {
                    printf("dest=%s%u", regKindName(inst.dest->registerTypeRaw), inst.dest->registerNumber);
                    printMask(inst.dest->writeMask);
                    printf(" sat=%d ", inst.dest->resultModifier.saturate());
                }
                for (auto& s : inst.sources) {
                    printf(" src=%s%u", regKindName(s.registerTypeRaw), s.registerNumber);
                    printSwiz(s.swizzle);
                    printf("(mod=%d)", (int)s.modifier);
                }
                if (inst.opcode == Opcode::DEF || inst.opcode == Opcode::DEFI) {
                    size_t off = inst.tokenOffset + 8;
                    for (int k = 0; k < 4; ++k) {
                        uint32_t u = blob.readU32LE(off + 4 * (size_t)k);
                        float f; memcpy(&f, &u, 4);
                        printf(" lit%d=%g(0x%08X)", k, f, u);
                    }
                }
                if (inst.dcl) printf(" dclUsage=%u dclIdx=%u samplerType=%u", inst.dcl->usage, inst.dcl->usageIndex,
                                      inst.dcl->samplerTextureType);
                printf("\n");
            }
        }
    }
    return 0;
}
