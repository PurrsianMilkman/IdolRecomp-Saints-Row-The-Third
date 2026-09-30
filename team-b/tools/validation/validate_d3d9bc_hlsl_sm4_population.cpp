// Real-data population gate for sr3d3d9bc::translateToHlsl() run in
// HlslTarget::SM4_5 mode (hlsl_translator.h) - the actual "usable in this
// renderer" gate this project's D3D11 viewer needs, as opposed to
// tools/validation/validate_d3d9bc_hlsl_population.cpp's SM3Legacy gate
// (kept working, unmodified, see that file - D3DCompile accepting vs_3_0/
// ps_3_0 HLSL just produces more SM1-3 bytecode, which a D3D11 device
// cannot load at all, HANDOFF.md §9.1).
//
// Identical archive-walking/blob-location structure to the SM3 gate
// (duplicated rather than shared, per tools/validation/README.md's own
// "every harness is its own standalone translation unit" convention) - the
// only real differences are:
//   1. translateToHlsl()'s 4th argument: sr3d3d9bc::HlslTarget::SM4_5.
//   2. D3DCompile() target profile: tr.targetProfile is now "vs_4_0"/
//      "ps_4_0" (the translator computes this itself once given SM4_5).
//   3. WARNINGS are captured and classified even on a SUCCESSFUL compile -
//      D3DCompile() populates the error/warning blob on S_OK too whenever
//      there is warning text, and this task's own bar is "zero warnings,
//      or every distinct warning CLASS explained", not just a pass/fail
//      count. Each warning line is parsed for its "warning X####:" (or
//      "warning X####-####:") diagnostic code, grouped by that code (the
//      STABLE part of the message - the surrounding shader-specific detail
//      is not), and a representative sample kept per class.
//
// Usage: validate_d3d9bc_hlsl_sm4_population [--limit N] [--dump-dir DIR] [--sm5] [archive.vpp_pc ...]
//   --limit N     stop after N blobs attempted (for a quick smoke run)
//   --dump-dir D  writes every FAILED translation's HLSL text to D\<n>.hlsl
//   --sm5         compile against vs_5_0/ps_5_0 instead of vs_4_0/ps_4_0
//                 (the translator's own emitted HLSL is target-syntax-
//                 identical either way; only the D3DCompile() profile
//                 string passed here changes - useful to check whether any
//                 real failure is an actual SM4-vs-SM5 gap rather than a
//                 translator bug)
//   (default archive set: every archive in the game's packfiles/pc/cache directory)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <d3dcompiler.h>

#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/errors.h"
#include "sr3d3d9bc/hlsl_translator.h"
#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/wrapper_header.h"
#include "vpp/container.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Archive walking / blob location - duplicated from
// tools/validation/validate_d3d9bc_hlsl_population.cpp (unchanged in
// substance; see that file for the more heavily commented original).
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
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated || r.status == vpp::DecodeStatus::RecoveredSharedStream ||
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

std::vector<LocatedBlob> locateBlobs(const std::vector<uint8_t>& data, bool& usedHeader, bool& crossCheckMismatch,
                                      size_t& headerCount, size_t& scanCount, std::vector<LocatedBlob>& outMiddle) {
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
            lb.stageLabel = b.stage == sr3fxo::Stage::Vertex ? "vertex" : b.stage == sr3fxo::Stage::Pixel ? "pixel" : "middle";
            if (b.stage == sr3fxo::Stage::Middle)
                outMiddle.push_back(lb);
            else
                out.push_back(lb);
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
// Stats
// ---------------------------------------------------------------------------

struct FailSample {
    std::string label;
    std::string profile;
    std::string errorText;
};

struct WarnClass {
    long long count = 0;
    std::string sampleLabel;
    std::string sampleText;
};

struct Stats {
    size_t blobsAttempted = 0;
    size_t disassemblePreconditionFailed = 0;
    size_t ctabNotWellFormed = 0;

    size_t translateComplete = 0;
    size_t translatePartial = 0;
    size_t compileOk = 0;
    size_t compileOkDespitePartial = 0;
    size_t compileFailed = 0;
    size_t compileOkWithWarnings = 0; // successful compiles whose warning text was non-empty

    long long vsAttempted = 0, psAttempted = 0;
    long long vsCompileOk = 0, psCompileOk = 0;

    std::vector<FailSample> failSamples;
    std::map<std::string, long long> unsupportedHist;
    std::map<std::string, long long> warningHist; // translator's own TranslationResult::warnings (target-independent judgment calls)
    std::map<std::string, WarnClass> compilerWarningClasses; // real D3DCompile warning codes (e.g. "X3206")

    void noteUnsupported(const std::string& msg) {
        size_t cut = msg.find(" at instruction");
        if (cut == std::string::npos) cut = msg.find(" (");
        std::string key = cut == std::string::npos ? msg.substr(0, 80) : msg.substr(0, cut);
        ++unsupportedHist[key];
    }
    void noteWarning(const std::string& msg) {
        size_t cut = msg.find(" at instruction");
        if (cut == std::string::npos) cut = msg.find(" - ");
        std::string key = cut == std::string::npos ? msg.substr(0, 80) : msg.substr(0, cut);
        ++warningHist[key];
    }
    void noteCompilerWarning(const std::string& code, const std::string& label, const std::string& line) {
        auto& wc = compilerWarningClasses[code];
        ++wc.count;
        if (wc.sampleText.empty()) {
            wc.sampleLabel = label;
            wc.sampleText = line;
        }
    }
};

Stats g_stats;
std::string g_dumpDir;
long long g_dumpCount = 0;
std::string g_vsProfile = "vs_4_0";
std::string g_psProfile = "ps_4_0";

// Splits D3DCompile's combined error/warning text blob into lines and
// classifies each "warning X####:" line by its diagnostic code, leaving
// "error X####:" lines alone (those are handled via the pass/fail path,
// not this function - this is ONLY called for text attached to an
// otherwise-successful compile, though it is written to tolerate a mixed
// blob defensively).
void classifyWarnings(const std::string& label, const std::string& text) {
    static const std::regex warnRe(R"(warning\s+(X\d{3,5}(?:-\d+)?)\s*:)");
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        std::smatch m;
        if (std::regex_search(line, m, warnRe)) {
            g_stats.noteCompilerWarning(m[1].str(), label, line);
        } else if (line.find("warning") != std::string::npos) {
            // A warning line that doesn't match the expected "X####" code
            // shape - keep it under its own bucket rather than silently
            // drop it, since the task's bar is "every distinct warning
            // CLASS explained", not "every warning matching one assumed
            // regex".
            g_stats.noteCompilerWarning("(uncoded warning text)", label, line);
        }
    }
}

void processBlob(const std::string& archive, const std::string& path, const std::string& name, const char* stage,
                  const std::vector<uint8_t>& data, size_t blobOffset, size_t blobLength) {
    ++g_stats.blobsAttempted;
    const bool isVertex = std::strcmp(stage, "vertex") == 0;
    if (isVertex) ++g_stats.vsAttempted; else ++g_stats.psAttempted;

    vpp::ByteView blobView(data.data() + blobOffset, blobLength);
    const std::string label =
        archive + " :: " + path + (path.empty() ? "" : "/") + name + " (" + stage + " stage, blob offset " +
        std::to_string(blobOffset) + ")";

    sr3d3d9bc::DisassembledShader d;
    try {
        d = sr3d3d9bc::disassemble(blobView);
    } catch (const sr3d3d9bc::FormatError&) {
        ++g_stats.disassemblePreconditionFailed;
        return;
    }

    sr3d3d9bc::ConstantTable ct = sr3d3d9bc::readConstantTable(blobView, d);
    if (ct.status != sr3d3d9bc::CtabStatus::WellFormed) {
        ++g_stats.ctabNotWellFormed;
    }

    sr3d3d9bc::TranslationResult tr = sr3d3d9bc::translateToHlsl(d, ct, blobView, sr3d3d9bc::HlslTarget::SM4_5);
    for (const auto& u : tr.unsupported) g_stats.noteUnsupported(u);
    for (const auto& w : tr.warnings) g_stats.noteWarning(w);
    if (tr.complete)
        ++g_stats.translateComplete;
    else
        ++g_stats.translatePartial;

    const std::string profile = isVertex ? g_vsProfile : g_psProfile;

    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(tr.hlsl.c_str(), tr.hlsl.size(), label.c_str(), nullptr, nullptr, tr.entryPoint.c_str(),
                             profile.c_str(), D3DCOMPILE_SKIP_OPTIMIZATION, 0, &code, &err);
    std::string errText = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "";
    if (SUCCEEDED(hr)) {
        ++g_stats.compileOk;
        if (isVertex) ++g_stats.vsCompileOk; else ++g_stats.psCompileOk;
        if (!tr.complete) ++g_stats.compileOkDespitePartial;
        if (!errText.empty()) {
            ++g_stats.compileOkWithWarnings;
            classifyWarnings(label, errText);
        }
    } else {
        ++g_stats.compileFailed;
        if (g_stats.failSamples.size() < 60) g_stats.failSamples.push_back({label, profile, errText.empty() ? "(no error blob)" : errText});
        if (!g_dumpDir.empty()) {
            std::ofstream f(g_dumpDir + "/" + std::to_string(g_dumpCount++) + ".hlsl");
            f << "// " << label << "\n// profile: " << profile << "\n// error:\n";
            std::istringstream iss(errText);
            std::string ln;
            while (std::getline(iss, ln)) f << "// " << ln << "\n";
            f << tr.hlsl;
        }
    }
    if (code) code->Release();
    if (err) err->Release();
}

void processFxoEntry(const std::string& archive, const std::string& path, const std::string& name,
                      const std::vector<uint8_t>& data) {
    if (data.empty()) return;
    bool usedHeader = false, crossCheckMismatch = false;
    size_t headerCount = 0, scanCount = 0;
    std::vector<LocatedBlob> middleBlobs;
    std::vector<LocatedBlob> blobs = locateBlobs(data, usedHeader, crossCheckMismatch, headerCount, scanCount, middleBlobs);
    (void)crossCheckMismatch;
    for (const auto& b : blobs) processBlob(archive, path, name, b.stageLabel, data, b.offset, b.length);
}

void walk(const vpp::Container& c, const std::string& archive, const std::string& path, size_t limit) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (limit && g_stats.blobsAttempted >= limit) return;
        const auto& e = c.entries()[i];
        const std::string ln = lower(e.name);
        const bool isFxoPc = endsWith(ln, ".fxo_pc");
        const bool isFxoPcDx11 = endsWith(ln, ".fxo_pc_dx11");
        if (isFxoPc) {
            std::vector<uint8_t> data;
            if (entryBytes(c, i, data)) processFxoEntry(archive, path, e.name, data);
            continue;
        }
        if (isFxoPcDx11) continue;
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container n = c.openNested(i);
                walk(n, archive, path + "/" + e.name, limit);
            } catch (const std::exception&) {
            }
        } else if (endsWith(ln, ".str2_pc") || endsWith(ln, ".vpp_pc")) {
            std::vector<uint8_t> data;
            if (entryBytes(c, i, data)) {
                try {
                    vpp::Container n{vpp::ByteView(data.data(), data.size())};
                    walk(n, archive, path + "/" + e.name, limit);
                } catch (const std::exception&) {
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    size_t limit = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--limit" && i + 1 < argc) {
            limit = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (a == "--dump-dir" && i + 1 < argc) {
            g_dumpDir = argv[++i];
            fs::create_directories(g_dumpDir);
        } else if (a == "--sm5") {
            g_vsProfile = "vs_5_0";
            g_psProfile = "ps_5_0";
        } else {
            archives.push_back(a);
        }
    }
    if (archives.empty()) {
        const fs::path dir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
        for (const auto& de : fs::directory_iterator(dir))
            if (de.path().extension() == ".vpp_pc") archives.push_back(de.path().string());
        std::sort(archives.begin(), archives.end());
    }

    printf("D3DCompile target profiles: vs=%s ps=%s\n", g_vsProfile.c_str(), g_psProfile.c_str());

    size_t scanned = 0;
    for (const std::string& a : archives) {
        if (limit && g_stats.blobsAttempted >= limit) break;
        std::vector<uint8_t> bytes = readFile(a);
        if (bytes.empty()) continue;
        const std::string base = fs::path(a).filename().string();
        try {
            vpp::Container c{vpp::ByteView(bytes.data(), bytes.size())};
            walk(c, base, "", limit);
            ++scanned;
        } catch (const std::exception& ex) {
            printf("skip %s: %s\n", base.c_str(), ex.what());
        }
        printf("scanned %s (blobs attempted so far: %zu, compileOk: %zu, compileFailed: %zu, withWarnings: %zu)\n",
               base.c_str(), g_stats.blobsAttempted, g_stats.compileOk, g_stats.compileFailed,
               g_stats.compileOkWithWarnings);
        fflush(stdout);
    }

    printf("\narchives scanned: %zu / %zu\n", scanned, archives.size());
    printf("\n=== SM4/5 HLSL translator population gate (D3DCompile against %s/%s) ===\n", g_vsProfile.c_str(),
           g_psProfile.c_str());
    printf("blobs attempted                          : %zu\n", g_stats.blobsAttempted);
    printf("  disassemble() precondition failed (not expected)      : %zu\n", g_stats.disassemblePreconditionFailed);
    printf("  CTAB not well-formed (not expected, spec §13.4)       : %zu\n", g_stats.ctabNotWellFormed);
    printf("vs attempted %lld (compileOk %lld) | ps attempted %lld (compileOk %lld)\n", g_stats.vsAttempted,
           g_stats.vsCompileOk, g_stats.psAttempted, g_stats.psCompileOk);
    printf("translation complete (no flagged gap)    : %zu / %zu\n", g_stats.translateComplete, g_stats.blobsAttempted);
    printf("translation partial (gap flagged, best-effort HLSL emitted) : %zu\n", g_stats.translatePartial);
    printf("D3DCompile SUCCEEDED                     : %zu / %zu (%.4f%%)\n", g_stats.compileOk, g_stats.blobsAttempted,
           g_stats.blobsAttempted ? 100.0 * static_cast<double>(g_stats.compileOk) / static_cast<double>(g_stats.blobsAttempted)
                                   : 0.0);
    printf("  of which had a flagged translation gap anyway         : %zu\n", g_stats.compileOkDespitePartial);
    printf("  of which produced non-empty WARNING text              : %zu\n", g_stats.compileOkWithWarnings);
    printf("D3DCompile FAILED                        : %zu\n", g_stats.compileFailed);

    printf("\n--- TranslationResult::unsupported histogram (translator-level, target-independent judgment calls) ---\n");
    for (const auto& kv : g_stats.unsupportedHist) printf("  x%-6lld %s\n", kv.second, kv.first.c_str());
    if (g_stats.unsupportedHist.empty()) printf("  (none - every real instruction translated with no flagged gap)\n");

    printf("\n--- TranslationResult::warnings histogram (translator-level, target-independent judgment calls) ---\n");
    for (const auto& kv : g_stats.warningHist) printf("  x%-6lld %s\n", kv.second, kv.first.c_str());
    if (g_stats.warningHist.empty()) printf("  (none)\n");

    printf("\n--- Real D3DCompile WARNING classes on successful compiles (grouped by diagnostic code) ---\n");
    for (const auto& kv : g_stats.compilerWarningClasses) {
        printf("  x%-6lld %s\n      sample [%s]:\n      %s\n", kv.second.count, kv.first.c_str(),
               kv.second.sampleLabel.c_str(), kv.second.sampleText.c_str());
    }
    if (g_stats.compilerWarningClasses.empty()) printf("  (none - zero warning text on any successful compile)\n");

    printf("\n--- D3DCompile failure samples (capped at %zu, real compiler text) ---\n", g_stats.failSamples.size());
    for (const auto& s : g_stats.failSamples) {
        printf("\n[%s] profile=%s\n%s\n", s.label.c_str(), s.profile.c_str(),
               s.errorText.size() > 2000 ? (s.errorText.substr(0, 2000) + "...(truncated)").c_str() : s.errorText.c_str());
    }

    return 0;
}
