// Real-data population gate for sr3d3d9bc::readConstantTable() (ctab.h,
// spec-d3d9-sm2-sm3-bytecode.md §13). Walks the shipped game archives,
// locates every real D3D9 shader blob the same way
// tools/validation/validate_d3d9bc_population.cpp does (that file's own
// `locateBlobs`/`entryBytes`/`walk` idiom, duplicated here rather than
// shared since every tools/validation/*.cpp harness is its own standalone
// translation unit - see tools/validation/README.md), runs
// sr3d3d9bc::disassemble() first (already proven 7,276/7,276 clean per
// HANDOFF.md §9.97), then this new CTAB reader on the SAME blob.
//
// POPULATION GATE (spec §13.4): every one of the 7,276 real .fxo_pc VS+PS
// blobs (excluding the "middle"/geometry-table blobs spec-fxo-format.md
// assumes don't exist for D3D9, same exclusion validate_d3d9bc_population.cpp
// applies) must land in EXACTLY ONE of two buckets - a well-formed CTAB, or
// explicitly none. A third "Malformed" bucket exists in CtabStatus for
// defensive/diagnostic purposes (a CTAB-FourCC-tagged comment that fails a
// structural check) but is not expected to be hit by any real
// compiler-emitted blob; if it ever is, that's reported explicitly as a
// genuine finding, never silently folded into either of the two expected
// buckets.
//
// Also reports: the RegisterSet/Class/Type distributions seen across every
// well-formed table's constants, every genuine D3DXPC_STRUCT-classed
// constant found (full detail, since spec §13's own struct-layout
// resolution was never exercised against real data before this run), and a
// cross-check of this reader's name/RegisterSet/RegisterIndex/RegisterCount
// output against the existing minimal sr3fxo::inspectD3d9Blob reader
// (include/sr3fxo/d3d9_blob.h) over every blob where either reader found a
// CTAB.
//
// Standalone diagnostic, deliberately NOT wired into CMakeLists.txt (see
// tools/validation/README.md).
//
// Usage: validate_d3d9bc_ctab_population [archive.vpp_pc ...]
//   (default: every archive in the game's packfiles/pc/cache directory)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/errors.h"
#include "sr3fxo/d3d9_blob.h"
#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/wrapper_header.h"
#include "vpp/container.h"

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Archive walking / blob location - duplicated from
// tools/validation/validate_d3d9bc_population.cpp (see that file for the
// original, more heavily commented version); unchanged in substance.
// ---------------------------------------------------------------------------

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
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
    bool ok = r.status == vpp::DecodeStatus::Ok ||
              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

struct LocatedBlob {
    size_t offset = 0, length = 0;
    bool isVertex = false;
    const char* stageLabel = "?";
};

std::vector<LocatedBlob> locateBlobs(const std::vector<uint8_t>& data, bool& usedHeader,
                                      bool& crossCheckMismatch, size_t& headerCount, size_t& scanCount,
                                      std::vector<LocatedBlob>& outMiddle) {
    std::vector<LocatedBlob> out;
    vpp::ByteView view(data.data(), data.size());
    sr3fxo::WrapperHeader h;
    std::string why;
    usedHeader = sr3fxo::WrapperHeader::tryParse(view, h, why);
    if (usedHeader) {
        size_t end = 0;
        auto blobs = h.layoutBlobs(end);
        for (const auto& b : blobs) {
            if (b.length == 0) continue;
            if (b.offset + b.length > data.size()) continue;
            LocatedBlob lb;
            lb.offset = b.offset;
            lb.length = b.length;
            lb.isVertex = (b.stage == sr3fxo::Stage::Vertex);
            lb.stageLabel = b.stage == sr3fxo::Stage::Vertex ? "vertex"
                             : b.stage == sr3fxo::Stage::Pixel ? "pixel"
                                                                : "middle";
            if (b.stage == sr3fxo::Stage::Middle) {
                outMiddle.push_back(lb);
            } else {
                out.push_back(lb);
            }
        }
        headerCount = out.size();
        try {
            sr3fxo::ShaderWrapper sw = sr3fxo::ShaderWrapper::parse(view);
            scanCount = sw.shaders().size();
            if (scanCount != headerCount) crossCheckMismatch = true;
        } catch (const std::exception&) {
            scanCount = 0;
            crossCheckMismatch = true;
        }
        return out;
    }
    try {
        sr3fxo::ShaderWrapper sw = sr3fxo::ShaderWrapper::parse(view);
        for (const auto& es : sw.shaders()) {
            LocatedBlob lb;
            lb.offset = es.offset;
            lb.length = es.length;
            lb.isVertex = es.isVertexShader;
            lb.stageLabel = lb.isVertex ? "vertex" : "pixel";
            out.push_back(lb);
        }
    } catch (const std::exception&) {
    }
    return out;
}

// ---------------------------------------------------------------------------
// CTAB-specific stats.
// ---------------------------------------------------------------------------

struct CtabStats {
    size_t blobsAttempted = 0;
    size_t disassemblePreconditionFailed = 0;  // FormatError from disassemble() - not expected (spec §2)
    size_t readerThrew = 0;                    // defensive: readConstantTable() should never throw - not expected

    size_t wellFormed = 0;
    size_t notPresent = 0;
    size_t malformed = 0;  // spec §13.4's population gate expects this to be 0
    std::vector<std::string> malformedSamples;

    long long totalConstants = 0;
    std::map<uint16_t, long long> registerSetHist;  // raw RegisterSet -> constant count
    std::map<uint16_t, long long> classHist;         // raw Class -> constant count
    std::map<uint16_t, long long> typeHist;           // raw Type -> constant count

    long long structConstantsFound = 0;
    std::vector<std::string> structSamples;  // full detail on every genuine D3DXPC_STRUCT constant

    size_t crossCheckedBlobs = 0;
    size_t crossCheckDisagreements = 0;
    std::vector<std::string> crossCheckSamples;
};

CtabStats g_stats;

const char* registerSetName(uint16_t raw) {
    switch (raw) {
        case 0: return "Bool";
        case 1: return "Int4";
        case 2: return "Float4";
        case 3: return "Sampler";
        default: return "?";
    }
}
const char* classNameOf(uint16_t raw) {
    switch (raw) {
        case 0: return "Scalar";
        case 1: return "Vector";
        case 2: return "MatrixRows";
        case 3: return "MatrixColumns";
        case 4: return "Object";
        case 5: return "Struct";
        default: return "?";
    }
}
const char* typeNameOf(uint16_t raw) {
    switch (raw) {
        case 0: return "Void";
        case 1: return "Bool";
        case 2: return "Int";
        case 3: return "Float";
        case 4: return "String";
        case 5: return "Texture";
        case 6: return "Texture1D";
        case 7: return "Texture2D";
        case 8: return "Texture3D";
        case 9: return "TextureCube";
        case 10: return "Sampler";
        case 11: return "Sampler1D";
        case 12: return "Sampler2D";
        case 13: return "Sampler3D";
        case 14: return "SamplerCube";
        default: return "?";
    }
}

// Recursively tallies one TypeInfo (and, for a struct, its members) into
// the class/type histograms and captures a full-detail sample for any
// genuine D3DXPC_STRUCT hit.
void tallyTypeInfo(const sr3d3d9bc::TypeInfo& ti, const std::string& ownerLabel, const std::string& fieldName) {
    ++g_stats.classHist[ti.classRaw];
    ++g_stats.typeHist[ti.typeRaw];
    if (ti.classRaw == static_cast<uint16_t>(sr3d3d9bc::ParameterClass::Struct)) {
        ++g_stats.structConstantsFound;
        if (g_stats.structSamples.size() < 50) {
            std::string s = ownerLabel + " field \"" + fieldName + "\": Class=Struct Type=" +
                             std::to_string(ti.typeRaw) + " Rows=" + std::to_string(ti.rows) +
                             " Columns=" + std::to_string(ti.columns) + " Elements=" + std::to_string(ti.elements) +
                             " StructMembers=" + std::to_string(ti.structMembers) + " (parsed " +
                             std::to_string(ti.structMemberInfo.size()) + " member entries: ";
            for (size_t i = 0; i < ti.structMemberInfo.size(); ++i) {
                if (i) s += ", ";
                s += "\"" + ti.structMemberInfo[i].name + "\" class=" +
                     classNameOf(ti.structMemberInfo[i].typeInfo.classRaw) +
                     " type=" + typeNameOf(ti.structMemberInfo[i].typeInfo.typeRaw);
            }
            s += ")";
            g_stats.structSamples.push_back(s);
        }
    }
    for (const auto& m : ti.structMemberInfo) tallyTypeInfo(m.typeInfo, ownerLabel, fieldName + "." + m.name);
}

void crossCheckAgainstSr3fxo(vpp::ByteView blobView, const sr3d3d9bc::ConstantTable& ct, const std::string& label) {
    sr3fxo::D3d9BlobInfo old = sr3fxo::inspectD3d9Blob(blobView);
    const bool oldFound = old.ctabFound;
    const bool newFound = ct.status == sr3d3d9bc::CtabStatus::WellFormed;
    if (!oldFound && !newFound) return;  // nothing to cross-check for this blob
    ++g_stats.crossCheckedBlobs;

    bool disagree = false;
    std::string why;
    if (oldFound != newFound) {
        disagree = true;
        why = "ctabFound old=" + std::to_string(oldFound) + " new(WellFormed)=" + std::to_string(newFound);
    } else if (old.constants.size() != ct.constants.size()) {
        disagree = true;
        why = "constant count old=" + std::to_string(old.constants.size()) +
              " new=" + std::to_string(ct.constants.size());
    } else {
        for (size_t i = 0; i < old.constants.size(); ++i) {
            const auto& o = old.constants[i];
            const auto& n = ct.constants[i];
            if (o.name != n.name || o.registerSet != n.registerSetRaw || o.registerIndex != n.registerIndex ||
                o.registerCount != n.registerCount) {
                disagree = true;
                why = "constant[" + std::to_string(i) + "] old={" + o.name + "," + std::to_string(o.registerSet) +
                      "," + std::to_string(o.registerIndex) + "," + std::to_string(o.registerCount) + "} new={" +
                      n.name + "," + std::to_string(n.registerSetRaw) + "," + std::to_string(n.registerIndex) + "," +
                      std::to_string(n.registerCount) + "}";
                break;
            }
        }
    }
    if (disagree) {
        ++g_stats.crossCheckDisagreements;
        if (g_stats.crossCheckSamples.size() < 25) g_stats.crossCheckSamples.push_back(label + ": " + why);
    }
}

void processBlob(const std::string& archive, const std::string& path, const std::string& name, const char* stage,
                  const std::vector<uint8_t>& data, size_t blobOffset, size_t blobLength) {
    ++g_stats.blobsAttempted;
    vpp::ByteView blobView(data.data() + blobOffset, blobLength);
    const std::string label = archive + " :: " + path + (path.empty() ? "" : "/") + name + " (" + stage +
                               " stage, blob offset " + std::to_string(blobOffset) + ")";

    sr3d3d9bc::DisassembledShader d;
    try {
        d = sr3d3d9bc::disassemble(blobView);
    } catch (const sr3d3d9bc::FormatError&) {
        ++g_stats.disassemblePreconditionFailed;
        return;
    }

    sr3d3d9bc::ConstantTable ct;
    try {
        ct = sr3d3d9bc::readConstantTable(blobView, d);
    } catch (const std::exception&) {
        ++g_stats.readerThrew;
        return;
    }

    switch (ct.status) {
        case sr3d3d9bc::CtabStatus::WellFormed: {
            ++g_stats.wellFormed;
            g_stats.totalConstants += static_cast<long long>(ct.constants.size());
            for (const auto& c : ct.constants) {
                ++g_stats.registerSetHist[c.registerSetRaw];
                tallyTypeInfo(c.type, label + " constant \"" + c.name + "\"", c.name);
            }
            break;
        }
        case sr3d3d9bc::CtabStatus::NotPresent:
            ++g_stats.notPresent;
            break;
        case sr3d3d9bc::CtabStatus::Malformed:
            ++g_stats.malformed;
            if (g_stats.malformedSamples.size() < 50)
                g_stats.malformedSamples.push_back(label + ": " + ct.malformedReason);
            break;
    }

    crossCheckAgainstSr3fxo(blobView, ct, label);
}

void processFxoEntry(const std::string& archive, const std::string& path, const std::string& name,
                      const std::vector<uint8_t>& data) {
    if (data.empty()) return;
    bool usedHeader = false, crossCheckMismatch = false;
    size_t headerCount = 0, scanCount = 0;
    std::vector<LocatedBlob> middleBlobs;
    std::vector<LocatedBlob> blobs =
        locateBlobs(data, usedHeader, crossCheckMismatch, headerCount, scanCount, middleBlobs);
    (void)crossCheckMismatch;  // already covered by validate_d3d9bc_population.cpp; not re-reported here

    for (const auto& b : blobs) processBlob(archive, path, name, b.stageLabel, data, b.offset, b.length);
    // Middle/geometry-table blobs are intentionally excluded from the
    // primary 7,276-blob population count, same exclusion
    // validate_d3d9bc_population.cpp applies (spec-fxo-format.md assumes
    // "no D3D9 geometry stage exists" for that table).
}

void walk(const vpp::Container& c, const std::string& archive, const std::string& path) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        const std::string ln = lower(e.name);
        const bool isFxoPc = endsWith(ln, ".fxo_pc");
        const bool isFxoPcDx11 = endsWith(ln, ".fxo_pc_dx11");
        if (isFxoPc) {  // DX11 (.fxo_pc_dx11) is DXBC, not this spec's SM2/3 format - out of scope here, same as validate_d3d9bc_population.cpp's own control table
            std::vector<uint8_t> data;
            if (entryBytes(c, i, data)) processFxoEntry(archive, path, e.name, data);
            continue;
        }
        if (isFxoPcDx11) continue;
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container n = c.openNested(i);
                walk(n, archive, path + "/" + e.name);
            } catch (const std::exception&) {
            }
        } else if (endsWith(ln, ".str2_pc") || endsWith(ln, ".vpp_pc")) {
            std::vector<uint8_t> data;
            if (entryBytes(c, i, data)) {
                try {
                    vpp::Container n{vpp::ByteView(data.data(), data.size())};
                    walk(n, archive, path + "/" + e.name);
                } catch (const std::exception&) {
                }
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        const fs::path dir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
        for (const auto& de : fs::directory_iterator(dir))
            if (de.path().extension() == ".vpp_pc") archives.push_back(de.path().string());
        std::sort(archives.begin(), archives.end());
    }
    size_t scanned = 0;
    for (const std::string& a : archives) {
        std::vector<uint8_t> bytes = readFile(a);
        if (bytes.empty()) continue;
        const std::string base = fs::path(a).filename().string();
        try {
            vpp::Container c{vpp::ByteView(bytes.data(), bytes.size())};
            walk(c, base, "");
            ++scanned;
        } catch (const std::exception& ex) {
            printf("skip %s: %s\n", base.c_str(), ex.what());
        }
        printf("scanned %s (blobs attempted so far: %zu)\n", base.c_str(), g_stats.blobsAttempted);
        fflush(stdout);
    }
    printf("\narchives scanned: %zu / %zu\n", scanned, archives.size());

    printf("\n=== CTAB population gate (spec-d3d9-sm2-sm3-bytecode.md §13.4) ===\n");
    printf("blobs attempted                              : %zu\n", g_stats.blobsAttempted);
    printf("  disassemble() precondition failed (not expected, spec §2) : %zu\n",
           g_stats.disassemblePreconditionFailed);
    printf("  readConstantTable() threw (not expected - contract is never-throw) : %zu\n", g_stats.readerThrew);
    printf("well-formed CTAB                              : %zu\n", g_stats.wellFormed);
    printf("explicitly none (NotPresent)                  : %zu\n", g_stats.notPresent);
    printf("Malformed (CTAB fourcc present but structurally broken - NOT expected) : %zu\n", g_stats.malformed);
    const size_t accountedFor = g_stats.wellFormed + g_stats.notPresent + g_stats.malformed +
                                 g_stats.disassemblePreconditionFailed + g_stats.readerThrew;
    printf("sum of all buckets                            : %zu (blobs attempted: %zu, unaccounted-for: %zd)\n",
           accountedFor, g_stats.blobsAttempted,
           static_cast<ptrdiff_t>(g_stats.blobsAttempted) - static_cast<ptrdiff_t>(accountedFor));
    if (!g_stats.malformedSamples.empty()) {
        printf("  Malformed samples:\n");
        for (const auto& s : g_stats.malformedSamples) printf("    %s\n", s.c_str());
    }

    printf("\ntotal constants parsed (well-formed tables only) : %lld\n", g_stats.totalConstants);
    printf("RegisterSet distribution:\n");
    for (const auto& kv : g_stats.registerSetHist)
        printf("  %-8s (raw %u) : %lld\n", registerSetName(kv.first), kv.first, kv.second);
    printf("Class distribution (top-level constants + recursively any struct members):\n");
    for (const auto& kv : g_stats.classHist)
        printf("  %-14s (raw %u) : %lld\n", classNameOf(kv.first), kv.first, kv.second);
    printf("Type distribution (top-level constants + recursively any struct members):\n");
    for (const auto& kv : g_stats.typeHist)
        printf("  %-12s (raw %u) : %lld\n", typeNameOf(kv.first), kv.first, kv.second);

    printf("\nD3DXPC_STRUCT-classed entries found (top-level constants or nested struct members) : %lld\n",
           g_stats.structConstantsFound);
    if (!g_stats.structSamples.empty()) {
        printf("  every one found (capped at 50):\n");
        for (const auto& s : g_stats.structSamples) printf("    %s\n", s.c_str());
    } else {
        printf("  none found in this population - spec §13's struct-layout resolution (see src/d3d9bc_ctab.cpp's\n"
               "  top comment) rests on the spec's own explicit wording alone, not on having been exercised\n"
               "  against a real struct-typed constant.\n");
    }

    printf(
        "\n=== Cross-check vs sr3fxo::inspectD3d9Blob (include/sr3fxo/d3d9_blob.h) - existing minimal reader ===\n");
    printf("blobs where either reader found a CTAB (checked) : %zu\n", g_stats.crossCheckedBlobs);
    printf("disagreements                                     : %zu\n", g_stats.crossCheckDisagreements);
    if (!g_stats.crossCheckSamples.empty()) {
        printf("  every disagreement (capped at 25):\n");
        for (const auto& s : g_stats.crossCheckSamples) printf("    %s\n", s.c_str());
    }

    return 0;
}
