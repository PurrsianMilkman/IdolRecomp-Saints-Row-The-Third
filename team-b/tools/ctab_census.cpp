// CTAB constant census over every shipped D3D9 shader (cloud phase,
// 2026-09-30, manager request): for each .fxo_pc vertex/pixel blob, the
// constants its CTAB declares - name -> register set, index, count (sampler
// slots included) - as one TSV row per constant. Evidence for re-deriving
// render-pipeline register/constant claims from clean sources; the reader is
// sr3d3d9bc::readConstantTable (spec-d3d9-sm2-sm3-bytecode.md §13), already
// population-validated 7,276/7,276 (HANDOFF §9.98).
//
// Usage: ctab_census <out_dir> (--cache-dir <packfiles/pc/cache> | <archive.vpp_pc> ...)
// Blob location is the same as tools/validation/validate_d3d9bc_ctab_population
// .cpp (wrapper header first, sr3fxo::ShaderWrapper scan as fallback; the
// "middle" geometry-table blobs excluded, DX11 .fxo_pc_dx11 out of scope).
// Every archive and every entry is walked with a per-entry guard; failures
// are counted in the summary, never dropped silently.
//
// Outputs in <out_dir>:
//   ctab_census.tsv      archive, entry path, fxo name, blob index, stage,
//                        shader version, ctab status, constant name,
//                        register set (raw + label), register index, count,
//                        class, type, rows, columns, elements
//   ctab_census_blobs.tsv one row per blob: status and constant count, so a
//                        blob with no CTAB still appears
//   ctab_census_summary.txt totals and per-register-set counts
// The TSVs hold names the shaders declare (text describing game files); they
// go to the private bus and are not committed.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/wrapper_header.h"
#include "vpp/container.h"

namespace fs = std::filesystem;

namespace {

struct Totals {
    long archives = 0, archivesFailed = 0, fxoEntries = 0, fxoDecodeFailed = 0, blobs = 0;
    long disassembleFailed = 0, wellFormed = 0, notPresent = 0, malformed = 0, constants = 0;
    std::map<std::string, long> byRegisterSet;
    // Vertex-shader float4 constants starting at c28 / c48, by name and count
    // (the render-pipeline projTM / world2view claims, Sec20.12.5/Sec20.12.9).
    std::map<std::string, long> vsC28, vsC48;
};
Totals g;
std::ofstream g_rows, g_blobs;

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
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
    if (r.status != vpp::DecodeStatus::Ok) return false;
    out = std::move(r.data);
    return true;
}
std::string tsv(std::string s) {
    for (auto& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}
const char* registerSetLabel(uint16_t raw) {
    switch (raw) {
        case 0: return "bool";
        case 1: return "int4";
        case 2: return "float4";
        case 3: return "sampler";
        default: return "?";
    }
}

struct Blob {
    size_t offset = 0, length = 0;
    const char* stage = "?";
};

std::vector<Blob> locateBlobs(const std::vector<uint8_t>& data) {
    std::vector<Blob> out;
    vpp::ByteView view(data.data(), data.size());
    sr3fxo::WrapperHeader h;
    std::string why;
    if (sr3fxo::WrapperHeader::tryParse(view, h, why)) {
        size_t end = 0;
        for (const auto& b : h.layoutBlobs(end)) {
            if (b.length == 0 || b.offset + b.length > data.size()) continue;
            if (b.stage == sr3fxo::Stage::Middle) continue; // geometry-table blobs excluded (spec-fxo-format)
            out.push_back({b.offset, b.length, b.stage == sr3fxo::Stage::Vertex ? "vertex" : "pixel"});
        }
        return out;
    }
    try {
        sr3fxo::ShaderWrapper sw = sr3fxo::ShaderWrapper::parse(view);
        for (const auto& es : sw.shaders()) out.push_back({es.offset, es.length, es.isVertexShader ? "vertex" : "pixel"});
    } catch (const std::exception&) {
    }
    return out;
}

void processFxo(const std::string& archive, const std::string& path, const std::string& name,
                const std::vector<uint8_t>& data) {
    std::vector<Blob> blobs = locateBlobs(data);
    for (size_t bi = 0; bi < blobs.size(); ++bi) {
        const Blob& b = blobs[bi];
        ++g.blobs;
        vpp::ByteView blob(data.data() + b.offset, b.length);
        std::string version = "?", status;
        size_t count = 0;
        try {
            sr3d3d9bc::DisassembledShader d = sr3d3d9bc::disassemble(blob);
            char v[32];
            std::snprintf(v, sizeof v, "%s_%u_%u", d.version.isVertexShader ? "vs" : "ps", static_cast<unsigned>(d.version.major), static_cast<unsigned>(d.version.minor));
            version = v;
            sr3d3d9bc::ConstantTable ct = sr3d3d9bc::readConstantTable(blob, d);
            if (ct.status == sr3d3d9bc::CtabStatus::WellFormed) {
                status = "well_formed";
                ++g.wellFormed;
            } else if (ct.status == sr3d3d9bc::CtabStatus::NotPresent) {
                status = "not_present";
                ++g.notPresent;
            } else {
                status = "malformed:" + tsv(ct.malformedReason);
                ++g.malformed;
            }
            for (const auto& c : ct.constants) {
                ++count;
                ++g.constants;
                ++g.byRegisterSet[registerSetLabel(c.registerSetRaw)];
                if (std::string(b.stage) == "vertex" && c.registerSetRaw == 2) {
                    std::string key = c.name + " x" + std::to_string(c.registerCount);
                    if (c.registerIndex == 28) ++g.vsC28[key];
                    if (c.registerIndex == 48) ++g.vsC48[key];
                }
                g_rows << tsv(archive) << "\t" << tsv(path) << "\t" << tsv(name) << "\t" << bi << "\t" << b.stage
                       << "\t" << version << "\t" << status << "\t" << tsv(c.name) << "\t" << c.registerSetRaw << "\t"
                       << registerSetLabel(c.registerSetRaw) << "\t" << c.registerIndex << "\t" << c.registerCount
                       << "\t" << c.type.classRaw << "\t" << c.type.typeRaw << "\t" << c.type.rows << "\t"
                       << c.type.columns << "\t" << c.type.elements << "\n";
            }
        } catch (const std::exception& ex) {
            status = std::string("disassemble_failed:") + tsv(ex.what());
            ++g.disassembleFailed;
        }
        g_blobs << tsv(archive) << "\t" << tsv(path) << "\t" << tsv(name) << "\t" << bi << "\t" << b.stage << "\t"
                << version << "\t" << status << "\t" << count << "\n";
    }
}

void walk(const vpp::Container& c, const std::string& archive, const std::string& path) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        const std::string ln = lower(e.name);
        try { // per-entry guard (HANDOFF rule 6)
            if (endsWith(ln, ".fxo_pc")) {
                ++g.fxoEntries;
                std::vector<uint8_t> data;
                if (entryBytes(c, i, data)) processFxo(archive, path, e.name, data);
                else ++g.fxoDecodeFailed;
                continue;
            }
            if (endsWith(ln, ".fxo_pc_dx11")) continue;
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                try {
                    vpp::Container n = c.openNested(i);
                    walk(n, archive, path + "/" + e.name);
                } catch (const std::exception&) {
                }
            } else if (endsWith(ln, ".str2_pc") || endsWith(ln, ".vpp_pc")) {
                std::vector<uint8_t> data;
                if (entryBytes(c, i, data)) {
                    vpp::Container n{vpp::ByteView(data.data(), data.size())};
                    walk(n, archive, path + "/" + e.name);
                }
            }
        } catch (const std::exception&) {
            if (endsWith(ln, ".fxo_pc")) ++g.fxoDecodeFailed;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ctab_census <out_dir> (--cache-dir <dir> | <archive.vpp_pc> ...)\n");
        return 1;
    }
    fs::path out = argv[1];
    fs::create_directories(out);
    std::vector<std::string> archives;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--cache-dir" && i + 1 < argc) {
            for (const auto& de : fs::directory_iterator(argv[++i]))
                if (de.path().extension() == ".vpp_pc") archives.push_back(de.path().string());
        } else {
            archives.push_back(a);
        }
    }
    std::sort(archives.begin(), archives.end());
    g_rows.open(out / "ctab_census.tsv");
    g_rows << "archive\tentry_path\tfxo\tblob_index\tstage\tversion\tctab_status\tconstant\tregister_set_raw\t"
              "register_set\tregister_index\tregister_count\tclass_raw\ttype_raw\trows\tcolumns\telements\n";
    g_blobs.open(out / "ctab_census_blobs.tsv");
    g_blobs << "archive\tentry_path\tfxo\tblob_index\tstage\tversion\tctab_status\tconstant_count\n";
    for (const auto& a : archives) {
        std::vector<uint8_t> bytes = readFile(a);
        std::string base = fs::path(a).filename().string();
        ++g.archives;
        if (bytes.empty()) {
            ++g.archivesFailed;
            std::printf("unreadable: %s\n", base.c_str());
            continue;
        }
        try {
            vpp::Container c{vpp::ByteView(bytes.data(), bytes.size())};
            walk(c, base, "");
        } catch (const std::exception& ex) {
            ++g.archivesFailed;
            std::printf("open failed %s: %s\n", base.c_str(), ex.what());
        }
    }
    std::ofstream sum(out / "ctab_census_summary.txt");
    auto line = [&](const std::string& s) { sum << s << "\n"; std::printf("%s\n", s.c_str()); };
    line("archives=" + std::to_string(g.archives) + " (failed " + std::to_string(g.archivesFailed) + ")");
    line("fxo_entries=" + std::to_string(g.fxoEntries) + " (decode failed " + std::to_string(g.fxoDecodeFailed) + ")");
    line("blobs=" + std::to_string(g.blobs) + " (HANDOFF Sec9.98 population: 7276)");
    line("disassemble_failed=" + std::to_string(g.disassembleFailed));
    line("ctab_well_formed=" + std::to_string(g.wellFormed) + " not_present=" + std::to_string(g.notPresent) +
         " malformed=" + std::to_string(g.malformed));
    line("constants=" + std::to_string(g.constants));
    for (auto& kv : g.byRegisterSet) line("register_set_" + kv.first + "=" + std::to_string(kv.second));
    // Which names sit at VS c28 / c48 (name xregisterCount -> blob count).
    for (auto& kv : g.vsC28) line("vs_c28 " + kv.first + "=" + std::to_string(kv.second));
    for (auto& kv : g.vsC48) line("vs_c48 " + kv.first + "=" + std::to_string(kv.second));
    return 0;
}
