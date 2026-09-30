// Stage-3-adjacent, INDEPENDENT narrowing tool for the orchestrator's D3D9
// shader-identification plan. Not a known-answer exercise: ranks every real
// vertex shader (~3,731 blobs, see the run below for the exact count) by
// EXACT vertex-input-signature compatibility against a real vehicle-body
// mesh's channel layout (spec-vertex-format.md §5/§6/§6.4), to narrow which
// .fxo_pc a vehicle-body draw could plausibly use. Data-only: it never
// touches shaderHash/name-hash joins (that path is CLOSED NEGATIVE, see
// HANDOFF.md §9.101 / WALLS.md) - this is a completely separate, purely
// structural cross-check between two ALREADY-SOLVED readers (sr3d3d9bc's
// disassembler and sr3mesh's channel decoder), joined only by hand-built
// target signatures.
//
// PIPELINE (per real VS blob):
//   1. locate + disassemble (sr3d3d9bc::disassemble, spec-d3d9-sm2-sm3-
//      bytecode.md) - never touches PS blobs, per the task's VS-input-only
//      scope.
//   2. walk every DCL instruction (spec §8) whose destination register type
//      is D3DSPR_INPUT (RegisterType::Input == 1, register_type.h - the only
//      register-type value that unambiguously means "VS input register";
//      unlike values 3 and 6 it is not overloaded with another meaning per
//      spec §7, so no shader-version disambiguation is needed here) and
//      record (registerNumber, D3DDECLUSAGE usage, usageIndex) for each.
//   3. build two signatures from that set: an ORDERED one (usage/index
//      pairs sorted by ascending register number) and a SET one (the same
//      pairs, order-independent) - reported separately throughout, because
//      whether register order is guaranteed to track vertex-buffer field
//      order is not something this project has established.
//   4. compare against hand-built TARGET signatures for real vehicle-body
//      layouts (codes 100/101, spec §5/§6.4) and, as a control, real
//      character layouts (code 1/3, skinned) - counting SET matches and
//      ORDERED matches separately.
//
// TARGET SIGNATURES - the empirical inputs, not assumptions:
//   texcoord counts actually used by real vehicle-body (layout 100/101)
//   channels were measured directly off all 393 real .ccar_pc/.gcar_pc
//   pairs (sr3vehicle + sr3geometry + sr3mesh, the same chain
//   tools/validation/validate_vehicle.cpp already uses) via a throwaway
//   scratch probe, NOT assumed to be 1:
//     layout 100 (pos+normal+rigid-index):          texcoords in {1,2,3}
//       (channel counts: 1(x1158) 2(x288) 3(x45), total 1491 channels)
//     layout 101 (pos+normal+tangent+rigid-index):  texcoords in {1,2,3,4}
//       (channel counts: 1(x1161) 2(x530) 3(x832) 4(x50), total 2573
//       channels - confirms layout 101 is the dominant vehicle layout,
//       2573 > 1491, matching spec-vertex-format.md's "vehicles
//       dominantly use layout code 101")
//   Every one of those (layout, texcoordCount) pairs is tested as its own
//   target below - a parameter list built from real measurement, not a
//   hardcoded single guess.
//
// THE OPEN ASSUMPTION, tested both ways rather than asserted:
//   spec-vertex-format.md §6.4 says the rigid part index is "stored in the
//   same 4-byte slot shape the skinned path uses so one shader path serves
//   both" - which is genuinely ambiguous between two readings this tool
//   tests as SEPARATE target families:
//     (A) PRIMARY candidate - the task's own documented default: the slot
//         is declared BLENDINDICES only, no accompanying BLENDWEIGHT (a
//         rigid bind has no partition-of-unity weight to declare).
//     (B) ALTERNATE hypothesis - "one shader path serves both" read
//         literally: the SAME declaration as the skinned path, i.e. BOTH
//         BLENDWEIGHT and BLENDINDICES declared, and the vehicle vertex
//         data's absent weight field is simply not read by that shader
//         variant / fed a constant.
//   Neither is asserted as fact. The run below reports which one (if
//   either) actually narrows the population, which is the honest empirical
//   answer this task asked for.
//
// CONTROLS - obviously-wrong target signatures the same matching logic is
// run against, to prove it discriminates rather than accepting everything
// or nothing (spec §5's stride table: code 1 = position+normal+skinning,
// "characters (rare)"; code 3 = position+normal+tangent+skinning,
// "characters (dominant)"):
//   code 3, texcoords in {1,2} (both BLENDWEIGHT and BLENDINDICES present,
//   by construction - skinning is not in dispute for characters)
//   code 1, texcoords = 1
//
// Enumeration idiom (nested-container walk, entryBytes, WrapperHeader-first/
// ShaderWrapper-fallback blob location, .fxo_pc vs .fxo_pc_dx11 distinction)
// copied from tools/validation/validate_d3d9bc_population.cpp - the
// project's own existing pattern for finding every real D3D9 blob, reused
// verbatim rather than re-derived. Default archive dir when no argv is the
// same real cache directory that tool uses.
//
// Standalone diagnostic, deliberately NOT wired into CMakeLists.txt - build
// with the same manual cl.exe pattern every other harness in this directory
// uses (see build_one.bat's object list), linking sr3d3d9bc's own
// translation unit alongside the existing build_verify/*.obj set.
//
// Usage: rank_shaders_by_vertex_signature [archive.vpp_pc ...]
//   (default: every archive in the game's packfiles/pc/cache directory)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/errors.h"
#include "sr3d3d9bc/register_type.h"
#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/wrapper_header.h"
#include "vpp/container.h"

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------
// File/container plumbing - copied verbatim in spirit from
// tools/validation/validate_d3d9bc_population.cpp (same helper names, same
// shape), scoped down to what this tool needs.
// ---------------------------------------------------------------------

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
};

// WrapperHeader-first (table-driven, knows stage), ShaderWrapper-scan
// fallback - identical strategy to validate_d3d9bc_population.cpp's
// locateBlobs(), simplified: middle/geometry-table blobs are dropped
// outright (out of scope here - spec-fxo-format.md's own target population
// excludes them, and they are neither VS nor PS), and only the isVertex
// flag is kept, since PS blobs are never disassembled by this tool at all.
std::vector<LocatedBlob> locateBlobs(const std::vector<uint8_t>& data) {
    std::vector<LocatedBlob> out;
    vpp::ByteView view(data.data(), data.size());
    sr3fxo::WrapperHeader h;
    std::string why;
    if (sr3fxo::WrapperHeader::tryParse(view, h, why)) {
        size_t end = 0;
        auto blobs = h.layoutBlobs(end);
        for (const auto& b : blobs) {
            if (b.length == 0) continue;
            if (b.offset + b.length > data.size()) continue;
            if (b.stage == sr3fxo::Stage::Middle) continue;  // out of scope
            LocatedBlob lb;
            lb.offset = b.offset;
            lb.length = b.length;
            lb.isVertex = (b.stage == sr3fxo::Stage::Vertex);
            out.push_back(lb);
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
            out.push_back(lb);
        }
    } catch (const std::exception&) {
    }
    return out;
}

// ---------------------------------------------------------------------
// Vertex-input signature model.
// ---------------------------------------------------------------------

using UsagePair = std::pair<uint8_t, uint8_t>;  // (D3DDECLUSAGE usage, usageIndex)

// D3DDECLUSAGE values, spec-d3d9-sm2-sm3-bytecode.md §8.1.
constexpr uint8_t kPOSITION = 0;
constexpr uint8_t kBLENDWEIGHT = 1;
constexpr uint8_t kBLENDINDICES = 2;
constexpr uint8_t kNORMAL = 3;
constexpr uint8_t kTEXCOORD = 5;
constexpr uint8_t kTANGENT = 6;

const char* usageName(uint8_t u) {
    switch (u) {
        case kPOSITION: return "POSITION";
        case kBLENDWEIGHT: return "BLENDWEIGHT";
        case kBLENDINDICES: return "BLENDINDICES";
        case kNORMAL: return "NORMAL";
        case 4: return "PSIZE";
        case kTEXCOORD: return "TEXCOORD";
        case kTANGENT: return "TANGENT";
        case 7: return "BINORMAL";
        case 8: return "TESSFACTOR";
        case 9: return "POSITIONT";
        case 10: return "COLOR";
        case 11: return "FOG";
        case 12: return "DEPTH";
        case 13: return "SAMPLE";
        default: return "?";
    }
}

std::string sigToString(const std::vector<UsagePair>& ordered) {
    std::string s;
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (i) s += ",";
        s += usageName(ordered[i].first);
        s += std::to_string(ordered[i].second);
    }
    return s;
}

struct Signature {
    std::vector<UsagePair> ordered;  // by ascending VS input register number
    std::set<UsagePair> asSet;
};

Signature buildTarget(const std::vector<UsagePair>& baseFields, int texcoordCount) {
    Signature sig;
    sig.ordered = baseFields;
    for (int t = 0; t < texcoordCount; ++t) sig.ordered.push_back({kTEXCOORD, static_cast<uint8_t>(t)});
    for (const auto& p : sig.ordered) sig.asSet.insert(p);
    return sig;
}

// One real VS blob's location plus its extracted input signature.
struct Candidate {
    std::string archive, path, entryName;
    size_t blobOffset = 0;
    Signature sig;
};

std::string locStr(const Candidate& c) {
    return c.archive + " :: " + (c.path.empty() ? "" : c.path + "/") + c.entryName +
           " (blob offset " + std::to_string(c.blobOffset) + ")";
}

// ---------------------------------------------------------------------
// Population walk.
// ---------------------------------------------------------------------

struct Stats {
    size_t fxoPcEntries = 0;
    size_t vsBlobsFound = 0;
    size_t psBlobsFound = 0;
    size_t vsPreconditionFailed = 0;
    size_t vsWalkedCleanly = 0;      // WalkStatus::Ok
    size_t vsWalkedNotClean = 0;     // some other WalkStatus, instructions still usable
    size_t vsWithNoInputDcl = 0;     // walked, but zero D3DSPR_INPUT DCLs found
    size_t vsBlendIndicesNoWeight = 0;   // population-wide plausibility check
    size_t vsBlendIndicesWithWeight = 0;
};

Stats g_stats;
std::vector<Candidate> g_candidates;  // every VS blob with >= 1 input DCL

void disassembleVsBlob(const std::string& archive, const std::string& path, const std::string& name,
                        const std::vector<uint8_t>& data, const LocatedBlob& b) {
    ++g_stats.vsBlobsFound;
    vpp::ByteView blobView(data.data() + b.offset, b.length);
    sr3d3d9bc::DisassembledShader d;
    try {
        d = sr3d3d9bc::disassemble(blobView);
    } catch (const sr3d3d9bc::FormatError&) {
        ++g_stats.vsPreconditionFailed;
        return;
    }
    if (d.status == sr3d3d9bc::WalkStatus::Ok)
        ++g_stats.vsWalkedCleanly;
    else
        ++g_stats.vsWalkedNotClean;

    // Collect (registerNumber, usage, usageIndex) for every DCL whose
    // destination register type is D3DSPR_INPUT (RegisterType::Input == 1 -
    // the one unambiguous value per spec §7; unlike 3/6 it has no second
    // meaning, so no shader-version check is needed to trust it here).
    struct RegUsage {
        uint16_t reg;
        UsagePair usage;
    };
    std::vector<RegUsage> regs;
    bool sawBlendWeight = false, sawBlendIndices = false;
    for (const auto& inst : d.instructions) {
        if (inst.opcode != sr3d3d9bc::Opcode::DCL) continue;
        if (!inst.dcl.has_value() || !inst.dest.has_value()) continue;
        if (inst.dest->registerTypeRaw != static_cast<uint32_t>(sr3d3d9bc::RegisterType::Input)) continue;
        regs.push_back({inst.dest->registerNumber, {inst.dcl->usage, inst.dcl->usageIndex}});
        if (inst.dcl->usage == kBLENDWEIGHT) sawBlendWeight = true;
        if (inst.dcl->usage == kBLENDINDICES) sawBlendIndices = true;
    }
    if (regs.empty()) {
        ++g_stats.vsWithNoInputDcl;
        return;
    }
    if (sawBlendIndices) {
        if (sawBlendWeight)
            ++g_stats.vsBlendIndicesWithWeight;
        else
            ++g_stats.vsBlendIndicesNoWeight;
    }

    std::stable_sort(regs.begin(), regs.end(),
                      [](const RegUsage& a, const RegUsage& b) { return a.reg < b.reg; });

    Candidate c;
    c.archive = archive;
    c.path = path;
    c.entryName = name;
    c.blobOffset = b.offset;
    for (const auto& r : regs) {
        c.sig.ordered.push_back(r.usage);
        c.sig.asSet.insert(r.usage);
    }
    g_candidates.push_back(std::move(c));
}

void processFxoEntry(const std::string& archive, const std::string& path, const std::string& name,
                      const std::vector<uint8_t>& data) {
    ++g_stats.fxoPcEntries;
    if (data.empty()) return;
    std::vector<LocatedBlob> blobs = locateBlobs(data);
    for (const auto& b : blobs) {
        if (b.length == 0) continue;
        if (b.isVertex)
            disassembleVsBlob(archive, path, name, data, b);
        else
            ++g_stats.psBlobsFound;  // counted, never disassembled - VS-input-only scope
    }
}

void walk(const vpp::Container& c, const std::string& archive, const std::string& path) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        const std::string ln = lower(e.name);
        if (endsWith(ln, ".fxo_pc")) {
            std::vector<uint8_t> data;
            if (entryBytes(c, i, data)) processFxoEntry(archive, path, e.name, data);
            continue;
        }
        if (endsWith(ln, ".fxo_pc_dx11")) continue;  // DXBC, out of scope entirely
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

// ---------------------------------------------------------------------
// Matching + reporting.
// ---------------------------------------------------------------------

struct Target {
    std::string label;
    Signature sig;
    bool isControl = false;
};

void reportTarget(const Target& t) {
    printf("\n--- TARGET: %s ---\n", t.label.c_str());
    printf("  expected ordered signature (%zu declared inputs): %s\n", t.sig.ordered.size(),
           sigToString(t.sig.ordered).c_str());

    std::vector<const Candidate*> setMatches, orderedMatches;
    std::map<size_t, long long> dclCountHist;  // among SET matches only
    for (const auto& c : g_candidates) {
        if (c.sig.asSet == t.sig.asSet) {
            setMatches.push_back(&c);
            ++dclCountHist[c.sig.ordered.size()];
        }
        if (c.sig.ordered == t.sig.ordered) orderedMatches.push_back(&c);
    }

    printf("  exact-SET matches (same usage/index set, any register order)     : %zu / %zu real VS blobs\n",
           setMatches.size(), g_candidates.size());
    printf("  exact-ORDERED matches (same set AND same register-number order) : %zu / %zu real VS blobs\n",
           orderedMatches.size(), g_candidates.size());

    if (!setMatches.empty()) {
        printf("  SET-match DCL-count histogram (sanity check - all should read %zu):\n", t.sig.ordered.size());
        for (const auto& kv : dclCountHist) printf("    %zu declared inputs : x%lld\n", kv.first, kv.second);
        printf("  SET-match filenames:\n");
        for (const auto* c : setMatches) {
            bool alsoOrdered = (c->sig.ordered == t.sig.ordered);
            printf("    %s%s  [%s]\n", locStr(*c).c_str(), alsoOrdered ? "  (also ORDERED match)" : "",
                   sigToString(c->sig.ordered).c_str());
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
    }
    printf("archives scanned: %zu / %zu\n", scanned, archives.size());

    printf("\n=== POPULATION GATE (full real population, not a sample) ===\n");
    printf(".fxo_pc entries found              : %zu\n", g_stats.fxoPcEntries);
    printf("VS blobs found                     : %zu\n", g_stats.vsBlobsFound);
    printf("PS blobs found (counted, never disassembled - out of scope) : %zu\n", g_stats.psBlobsFound);
    printf("VS precondition failed (not a valid version token)          : %zu\n", g_stats.vsPreconditionFailed);
    printf("VS walked cleanly (WalkStatus::Ok)                          : %zu\n", g_stats.vsWalkedCleanly);
    printf("VS walked, but not cleanly (still usable instructions)      : %zu\n", g_stats.vsWalkedNotClean);
    printf("VS walked with ZERO D3DSPR_INPUT DCLs (e.g. VS3.0 output-only decls or truncated)  : %zu\n",
           g_stats.vsWithNoInputDcl);
    printf("VS candidates carrying an extracted input signature         : %zu\n", g_candidates.size());

    printf("\n=== PLAUSIBILITY CHECK for the rigid-index=BLENDINDICES-without-BLENDWEIGHT assumption ===\n");
    printf("Population-wide, among VS blobs that declare BLENDINDICES at all:\n");
    printf("  BLENDINDICES declared WITHOUT a BLENDWEIGHT DCL : %zu\n", g_stats.vsBlendIndicesNoWeight);
    printf("  BLENDINDICES declared WITH a BLENDWEIGHT DCL    : %zu\n", g_stats.vsBlendIndicesWithWeight);
    printf("(if the without-weight count is 0, real shaders never declare BLENDINDICES alone - the\n"
           " project's documented candidate reading would have no real precedent in this population;\n"
           " if it is nonzero, that is a structurally plausible, though not yet confirmed, precedent.)\n");

    // -------------------------------------------------------------
    // TARGET SIGNATURES - texcoord counts measured off all 393 real
    // .ccar_pc/.gcar_pc pairs (see the file header comment for the exact
    // per-count channel tallies), not assumed.
    // -------------------------------------------------------------
    std::vector<Target> targets;

    const std::vector<int> tex101 = {1, 2, 3, 4};
    const std::vector<int> tex100 = {1, 2, 3};

    // Primary candidate (A): rigid index = BLENDINDICES only, no BLENDWEIGHT.
    for (int n : tex101) {
        Target t;
        t.label = "VEHICLE code=101 (pos,normal,tangent,rigid-index=BLENDINDICES-only) texcoords=" +
                  std::to_string(n) + "  [PRIMARY candidate reading]";
        t.sig = buildTarget({{kPOSITION, 0}, {kNORMAL, 0}, {kTANGENT, 0}, {kBLENDINDICES, 0}}, n);
        targets.push_back(t);
    }
    for (int n : tex100) {
        Target t;
        t.label = "VEHICLE code=100 (pos,normal,rigid-index=BLENDINDICES-only) texcoords=" + std::to_string(n) +
                   "  [PRIMARY candidate reading]";
        t.sig = buildTarget({{kPOSITION, 0}, {kNORMAL, 0}, {kBLENDINDICES, 0}}, n);
        targets.push_back(t);
    }

    // Alternate hypothesis (B): rigid index reuses the skinned path's full
    // declaration (BLENDWEIGHT + BLENDINDICES both present).
    for (int n : tex101) {
        Target t;
        t.label = "VEHICLE code=101 (pos,normal,tangent,BLENDWEIGHT+BLENDINDICES 'shared shader path') texcoords=" +
                  std::to_string(n) + "  [ALTERNATE hypothesis]";
        t.sig = buildTarget({{kPOSITION, 0}, {kNORMAL, 0}, {kTANGENT, 0}, {kBLENDWEIGHT, 0}, {kBLENDINDICES, 0}}, n);
        targets.push_back(t);
    }
    for (int n : tex100) {
        Target t;
        t.label = "VEHICLE code=100 (pos,normal,BLENDWEIGHT+BLENDINDICES 'shared shader path') texcoords=" +
                  std::to_string(n) + "  [ALTERNATE hypothesis]";
        t.sig = buildTarget({{kPOSITION, 0}, {kNORMAL, 0}, {kBLENDWEIGHT, 0}, {kBLENDINDICES, 0}}, n);
        targets.push_back(t);
    }

    // CONTROLS - obviously-wrong targets (real character layouts, spec §5:
    // code 3 = position+normal+tangent+skinning, "characters (dominant)";
    // code 1 = position+normal+skinning, "characters (rare)").
    for (int n : {1, 2}) {
        Target t;
        t.label = "CONTROL character code=3 (pos,normal,tangent,BLENDWEIGHT,BLENDINDICES) texcoords=" +
                  std::to_string(n) + "  [obviously-wrong target: real skinned character layout]";
        t.sig = buildTarget({{kPOSITION, 0}, {kNORMAL, 0}, {kTANGENT, 0}, {kBLENDWEIGHT, 0}, {kBLENDINDICES, 0}}, n);
        t.isControl = true;
        targets.push_back(t);
    }
    {
        Target t;
        t.label =
            "CONTROL character code=1 (pos,normal,BLENDWEIGHT,BLENDINDICES) texcoords=1  [obviously-wrong "
            "target: real skinned character layout, no tangent]";
        t.sig = buildTarget({{kPOSITION, 0}, {kNORMAL, 0}, {kBLENDWEIGHT, 0}, {kBLENDINDICES, 0}}, 1);
        t.isControl = true;
        targets.push_back(t);
    }

    printf("\n=== VEHICLE-BODY TARGET RESULTS (primary + alternate readings) ===\n");
    for (const auto& t : targets)
        if (!t.isControl) reportTarget(t);

    printf("\n=== CONTROL RESULTS (obviously-wrong targets - must discriminate differently) ===\n");
    for (const auto& t : targets)
        if (t.isControl) reportTarget(t);

    return 0;
}
