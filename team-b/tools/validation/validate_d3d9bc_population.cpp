// Real-data population gate for sr3d3d9bc (spec-d3d9-sm2-sm3-bytecode.md),
// stage 1 of the orchestrator's 3-stage D3D9 shader plan. Walks the shipped
// game archives, locates every real D3D9 shader blob sr3fxo already knows
// how to find, and disassembles each one - never inventing anything the
// disassembler itself doesn't already implement from the spec.
//
// The idiom (recurse into nested vpp/str2 containers, decompress leaves,
// examine content) follows tools/validation/validate_vehicle.cpp exactly;
// see that file for the same `entryBytes`/`walk` shape.
//
// POPULATION GATE (spec §10): for every instruction token this decoder
// walks, classify its opcode against spec §9's bands. "Zero unknown
// opcodes" means zero hits outside bands 1/2/the three sentinels/
// RESERVED0 - RESERVED0 itself is a real, expected, under-specified value,
// not a bug (spec §10's own wording). Every genuine unknown is reported
// with its exact source file and byte offset, never just a count.
//
// SCOPE NOTE: `.fxo_pc_dx11` entries share the same wrapper (spec-fxo-
// format.md §8.2) but carry DXBC (Direct3D 10/11) bytecode, a completely
// different format this SM2/3 spec does not cover. They are still located
// and handed to the disassembler (so the precondition check - spec §2's
// version-token high-bits rule - gets exercised against real non-SM2/3
// content), but reported in a SEPARATE table: a FormatError there is the
// EXPECTED, correct outcome (this decoder correctly refusing to treat DXBC
// as SM2/3 tokens), not a defect.
//
// Standalone diagnostic, deliberately NOT wired into CMakeLists.txt (see
// tools/validation/README.md) - build with the same manual cl.exe pattern
// every other harness there uses, linking sr3d3d9bc's own translation unit
// alongside the existing build_verify/*.obj set (byte_view, container,
// format, hash, payload_locator, shader_wrapper, zlib).
//
// Usage: validate_d3d9bc_population [archive.vpp_pc ...]
//   (default: every archive in the game's packfiles/pc/cache directory)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
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

// One located D3D9 blob's byte range plus where it came from, independent
// of which sr3fxo reader found it.
struct LocatedBlob {
    size_t offset = 0, length = 0;
    bool isVertex = false;
    const char* stageLabel = "?";
};

// Locates blobs with sr3fxo::WrapperHeader (primary: table-driven, knows
// stage) and cross-checks the count against sr3fxo::ShaderWrapper (scan-
// based) when both apply - per the task's "use both and cross-check"
// option. Falls back to the scanner alone when the header formula isn't
// applicable to this file.
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
            if (b.offset + b.length > data.size()) continue;  // truncated/malformed table entry
            LocatedBlob lb;
            lb.offset = b.offset;
            lb.length = b.length;
            lb.isVertex = (b.stage == sr3fxo::Stage::Vertex);
            lb.stageLabel = b.stage == sr3fxo::Stage::Vertex ? "vertex"
                             : b.stage == sr3fxo::Stage::Pixel ? "pixel"
                                                                : "middle";
            if (b.stage == sr3fxo::Stage::Middle) {
                // spec-fxo-format.md's own population figures assume "no
                // D3D9 geometry stage exists" for this table (matches
                // tools/validation/validate_fxo_header.cpp's identical
                // exclusion) - so these are kept OUT of the primary VS+PS
                // population-gate count and reported as their own
                // category instead of silently dropped (the cross-check
                // against sr3fxo::ShaderWrapper below is what surfaced
                // that this assumption is not always true - see the
                // report).
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

struct UnknownHit {
    std::string archive, path, entryName, stage;
    size_t blobOffset = 0, tokenOffsetInBlob = 0;
    uint32_t opcodeRaw = 0;
};

struct OpcodeHistEntry {
    long long count = 0;
    sr3d3d9bc::OpcodeClass cls = sr3d3d9bc::OpcodeClass::UnknownOther;
};

struct Stats {
    size_t entriesFound = 0;
    size_t entriesBytesObtained = 0;
    size_t blobsAttempted = 0;
    size_t blobsWalkedCleanly = 0;      // WalkStatus::Ok - end token, no overrun, no trailing bytes
    size_t blobsTrailingBytes = 0;
    size_t blobsUnexpectedEnd = 0;
    size_t blobsNoEndToken = 0;
    size_t blobsPreconditionFailed = 0;  // FormatError - not even a valid version token
    size_t crossCheckMismatches = 0;
    std::vector<std::string> crossCheckSamples;  // "name: header=N scan=M"
    long long instructionsDecoded = 0;
    std::map<uint32_t, OpcodeHistEntry> histogram;  // key = raw 16-bit opcode
    std::vector<UnknownHit> unknownHits;
};

void recordInstructions(Stats& st, const sr3d3d9bc::DisassembledShader& d, const std::string& archive,
                         const std::string& path, const std::string& entryName, const char* stage,
                         size_t blobOffset) {
    for (const auto& inst : d.instructions) {
        ++st.instructionsDecoded;
        auto& e = st.histogram[inst.opcodeRaw];
        ++e.count;
        e.cls = inst.opcodeClass;
        if (!sr3d3d9bc::isKnownForPopulationGate(inst.opcodeClass)) {
            st.unknownHits.push_back({archive, path, entryName, stage, blobOffset, inst.tokenOffset, inst.opcodeRaw});
        }
    }
}

void disassembleBlobs(Stats& st, const std::string& archive, const std::string& path, const std::string& name,
                       const std::vector<uint8_t>& data, const std::vector<LocatedBlob>& blobs) {
    for (const auto& b : blobs) {
        ++st.blobsAttempted;
        vpp::ByteView blobView(data.data() + b.offset, b.length);
        try {
            sr3d3d9bc::DisassembledShader d = sr3d3d9bc::disassemble(blobView);
            recordInstructions(st, d, archive, path, name, b.stageLabel, b.offset);
            switch (d.status) {
                case sr3d3d9bc::WalkStatus::Ok: ++st.blobsWalkedCleanly; break;
                case sr3d3d9bc::WalkStatus::TrailingBytes: ++st.blobsTrailingBytes; break;
                case sr3d3d9bc::WalkStatus::UnexpectedEnd: ++st.blobsUnexpectedEnd; break;
                case sr3d3d9bc::WalkStatus::NoEndToken: ++st.blobsNoEndToken; break;
            }
        } catch (const sr3d3d9bc::FormatError&) {
            ++st.blobsPreconditionFailed;
        }
    }
}

void processFxoEntry(Stats& st, Stats& stMiddle, const std::string& archive, const std::string& path,
                      const std::string& name, const std::vector<uint8_t>& data) {
    ++st.entriesFound;
    if (data.empty()) return;
    ++st.entriesBytesObtained;

    bool usedHeader = false, crossCheckMismatch = false;
    size_t headerCount = 0, scanCount = 0;
    std::vector<LocatedBlob> middleBlobs;
    std::vector<LocatedBlob> blobs =
        locateBlobs(data, usedHeader, crossCheckMismatch, headerCount, scanCount, middleBlobs);
    if (crossCheckMismatch) {
        ++st.crossCheckMismatches;
        if (st.crossCheckSamples.size() < 10) {
            st.crossCheckSamples.push_back(name + ": header(VS+PS, excl. middle)=" + std::to_string(headerCount) +
                                            " scan(ShaderWrapper)=" + std::to_string(scanCount) +
                                            " (middle-table blobs this entry has: " +
                                            std::to_string(middleBlobs.size()) + ")");
        }
    }

    disassembleBlobs(st, archive, path, name, data, blobs);
    if (!middleBlobs.empty()) {
        ++stMiddle.entriesFound;
        ++stMiddle.entriesBytesObtained;
        disassembleBlobs(stMiddle, archive, path, name, data, middleBlobs);
    }
}

Stats g_fxoPc, g_fxoPcMiddle, g_fxoPcDx11, g_fxoPcDx11Middle;

void walk(const vpp::Container& c, const std::string& archive, const std::string& path) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        const std::string ln = lower(e.name);
        const bool isFxoPc = endsWith(ln, ".fxo_pc");
        const bool isFxoPcDx11 = endsWith(ln, ".fxo_pc_dx11");
        if (isFxoPc || isFxoPcDx11) {
            std::vector<uint8_t> data;
            if (entryBytes(c, i, data)) {
                if (isFxoPcDx11)
                    processFxoEntry(g_fxoPcDx11, g_fxoPcDx11Middle, archive, path, e.name, data);
                else
                    processFxoEntry(g_fxoPc, g_fxoPcMiddle, archive, path, e.name, data);
            }
            continue;
        }
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container n = c.openNested(i);
                walk(n, archive, path + "/" + e.name);
            } catch (const std::exception&) {
                // Leaf raw content that is not itself a container - fine.
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

void printReport(const char* title, const Stats& st, bool expectClean) {
    printf("\n=== %s ===\n", title);
    printf("entries found (directory)            : %zu\n", st.entriesFound);
    printf("entries whose bytes were obtained     : %zu\n", st.entriesBytesObtained);
    printf("WrapperHeader/ShaderWrapper mismatches on blob count : %zu\n", st.crossCheckMismatches);
    for (const auto& s : st.crossCheckSamples) printf("    sample: %s\n", s.c_str());
    printf("blobs attempted                       : %zu\n", st.blobsAttempted);
    printf("blobs walked cleanly (end token, no overrun/underrun, no trailing bytes) : %zu / %zu\n",
           st.blobsWalkedCleanly, st.blobsAttempted);
    printf("  end token found but trailing bytes remain : %zu\n", st.blobsTrailingBytes);
    printf("  a token/param overran the buffer          : %zu\n", st.blobsUnexpectedEnd);
    printf("  buffer exhausted, end token never seen     : %zu\n", st.blobsNoEndToken);
    printf("  precondition failed (no valid version token - %s) : %zu\n",
           expectClean ? "should be 0 for real D3D9 SM2/3 blobs" : "EXPECTED for DXBC content, not a defect",
           st.blobsPreconditionFailed);
    printf("instructions decoded                  : %lld\n", st.instructionsDecoded);

    long long band1 = 0, band2 = 0, reserved0 = 0, phase = 0, comment = 0, end = 0, gap = 0, other = 0;
    for (const auto& kv : st.histogram) {
        switch (kv.second.cls) {
            case sr3d3d9bc::OpcodeClass::Band1: band1 += kv.second.count; break;
            case sr3d3d9bc::OpcodeClass::Band2: band2 += kv.second.count; break;
            case sr3d3d9bc::OpcodeClass::Reserved0: reserved0 += kv.second.count; break;
            case sr3d3d9bc::OpcodeClass::SentinelPhase: phase += kv.second.count; break;
            case sr3d3d9bc::OpcodeClass::SentinelComment: comment += kv.second.count; break;
            case sr3d3d9bc::OpcodeClass::SentinelEnd: end += kv.second.count; break;
            case sr3d3d9bc::OpcodeClass::UnknownGap: gap += kv.second.count; break;
            case sr3d3d9bc::OpcodeClass::UnknownOther: other += kv.second.count; break;
        }
    }
    printf("  by class: Band1 %lld, Band2 %lld, RESERVED0 %lld, PHASE %lld, COMMENT(as-opcode) %lld, END(as-opcode) %lld, UnknownGap(49-63) %lld, UnknownOther %lld\n",
           band1, band2, reserved0, phase, comment, end, gap, other);

    printf("\nopcode histogram (name, raw value, count):\n");
    for (const auto& kv : st.histogram) {
        const char* nm = sr3d3d9bc::opcodeName(kv.first);
        printf("  %-14s 0x%04X  x%lld\n", nm ? nm : "(UNKNOWN)", kv.first, kv.second.count);
    }

    printf("\nPOPULATION GATE (spec §10): instruction tokens outside bands 1/2/the sentinels/RESERVED0 : %zu\n",
           st.unknownHits.size());
    if (!st.unknownHits.empty()) {
        printf("  every occurrence:\n");
        for (const auto& u : st.unknownHits) {
            printf("    opcode 0x%04X at %s :: %s%s%s (blob offset %zu, %s stage) token offset in blob %zu\n",
                   u.opcodeRaw, u.archive.c_str(), u.path.c_str(), u.path.empty() ? "" : "/", u.entryName.c_str(),
                   u.blobOffset, u.stage.c_str(), u.tokenOffsetInBlob);
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
        printf("scanned %s (fxo_pc entries so far: %zu, fxo_pc_dx11: %zu)\n", base.c_str(), g_fxoPc.entriesFound,
               g_fxoPcDx11.entriesFound);
        fflush(stdout);
    }
    printf("\narchives scanned: %zu / %zu\n", scanned, archives.size());

    printReport("D3D9 SM2/3 population: .fxo_pc (spec-d3d9-sm2-sm3-bytecode.md's own target population)", g_fxoPc,
                true);
    printReport(
        "SECONDARY: .fxo_pc middle/geometry-table blobs (excluded from the primary count above because "
        "spec-fxo-format.md assumes \"no D3D9 geometry stage exists\" there - the cross-check against "
        "sr3fxo::ShaderWrapper's independent scan found this table IS sometimes populated with a real, "
        "well-formed D3D9 blob, so it is measured here rather than silently dropped)",
        g_fxoPcMiddle, true);
    printReport(
        "OUT-OF-SCOPE control: .fxo_pc_dx11 (DXBC payload, not SM2/3 - precondition failures here are EXPECTED)",
        g_fxoPcDx11, false);
    printReport("OUT-OF-SCOPE control: .fxo_pc_dx11 middle-table blobs", g_fxoPcDx11Middle, false);
    return 0;
}
