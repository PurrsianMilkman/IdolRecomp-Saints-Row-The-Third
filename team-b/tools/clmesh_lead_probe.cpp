// Throwaway investigation tool (per this project's own practice of keeping
// the tool that found a real result) - NOT a shipped viewer command, NOT
// part of the new fork. Verifies, against REAL bytes, the two leads an
// orchestrator task handed to the prototype_lit_clmesh_tower.cpp follow-on
// fork, before any rendering code is written:
//
//   Lead 1: does a BARE-stem `.fxo_pc` (no _v/_mv suffix) exist for the
//   tower's non-drawable stems, and does it structurally carry a real,
//   non-empty VS blob + PS blob pairing for role 4 and/or role 6 via the
//   SAME T8 role-pairing mechanism prototype_lit_clmesh_tower.cpp's own
//   pairVsPsForRole() already uses (copied verbatim below, not reinvented)?
//
//   Lead 2: does airport_controltower.clmesh_pc's own real MaterialRecord
//   B (constant-name-hash) / C (vec4-value) arrays exist with plausible
//   in-range values, laid out exactly as spec-foliage-format.md Sec6/
//   spec-physics-format.md Sec4.4.6(h) describe (A 12-byte texture bindings,
//   4-aligned, then B 4-byte name hashes, 4-aligned, then C 16-byte vec4s,
//   16-aligned) - and do the B hashes match any real CTAB constant name
//   (lower-cased CRC32, sr3fxo::hashLowerName) on the material's own
//   resolved role4/role6 shader?
//
// Usage: clmesh_lead_probe.exe
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3clmesh/level_mesh.h"
#include "sr3d3d9bc/ctab.h"
#include "sr3d3d9bc/disassembler.h"
#include "sr3d3d9bc/hlsl_translator.h"
#include "sr3fxo/wrapper_header.h"
#include "vpp/container.h"

namespace {
std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
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
bool findEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    return false;
}
std::string lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; }
bool endsWithStr(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

const std::vector<std::string> kStageSuffixes = {
    "_bms", "_bmc", "_bs", "_bc", "_ms", "_mc", "_mv", "_ts", "_fd", "_s", "_c", "_t", "_v",
};
std::string stripStageSuffix(const std::string& lowerNoExt) {
    for (const auto& suf : kStageSuffixes)
        if (endsWithStr(lowerNoExt, suf)) return lowerNoExt.substr(0, lowerNoExt.size() - suf.size());
    return lowerNoExt;
}
void collectFxoStems(const vpp::Container& c, std::map<std::string, std::vector<std::string>>& stemToFiles) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWithStr(lower(n), ".fxo_pc")) {
            std::string noExt = lower(n).substr(0, n.size() - 7);
            stemToFiles[stripStageSuffix(noExt)].push_back(n);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { collectFxoStems(c.openNested(i), stemToFiles); } catch (const std::exception&) {}
        }
    }
}

// Identical mechanism to prototype_lit_clmesh_tower.cpp's own pairVsPsForRole.
struct BlobPairResult {
    bool ok = false; std::string reason;
    size_t vsOffset = 0, vsLength = 0, psOffset = 0, psLength = 0; int passIndex = -1;
};
BlobPairResult pairVsPsForRole(const std::vector<uint8_t>& fxoBytes, size_t role) {
    BlobPairResult r;
    sr3fxo::WrapperHeader wh; std::string why;
    vpp::ByteView view(fxoBytes.data(), fxoBytes.size());
    if (!sr3fxo::WrapperHeader::tryParse(view, wh, why)) { r.reason = "tryParse failed: " + why; return r; }
    if (!wh.rolePresent(role)) { r.reason = "role " + std::to_string(role) + " not present (flags=0x" + std::to_string(wh.flags()) + ")"; return r; }
    int8_t passIdx = wh.roleIndexByte(role);
    if (passIdx < 0 || static_cast<size_t>(passIdx) >= wh.passes().size()) { r.reason = "roleIndexByte out of range"; return r; }
    r.passIndex = passIdx;
    const sr3fxo::PassEntry& p = wh.passes()[static_cast<size_t>(passIdx)];
    size_t endOfBlobs = 0;
    std::vector<sr3fxo::WrapperBlob> blobs = wh.layoutBlobs(endOfBlobs);
    const sr3fxo::WrapperBlob* vsBlob = nullptr; const sr3fxo::WrapperBlob* psBlob = nullptr;
    for (const auto& b : blobs) {
        if (b.stage == sr3fxo::Stage::Vertex && static_cast<int16_t>(b.tableIndex) == p.vertexIndex) vsBlob = &b;
        if (b.stage == sr3fxo::Stage::Pixel && static_cast<int16_t>(b.tableIndex) == p.pixelIndex) psBlob = &b;
    }
    if (!vsBlob) { r.reason = "no non-empty VS blob for this pass's vertexIndex"; return r; }
    if (!psBlob) { r.reason = "no non-empty PS blob for this pass's pixelIndex"; return r; }
    r.vsOffset = vsBlob->offset; r.vsLength = vsBlob->length;
    r.psOffset = psBlob->offset; r.psLength = psBlob->length;
    r.ok = true;
    return r;
}
} // namespace

int main() {
    const std::string kCacheDir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";

    printf("=== LEAD 1: bare-stem .fxo_pc structural role4/role6 VS+PS check ===\n\n");
    std::vector<uint8_t> shadersArchive = readFile(kCacheDir + "/shaders.vpp_pc");
    if (shadersArchive.empty()) { printf("FATAL: shaders.vpp_pc\n"); return 1; }
    vpp::Container shadersContainer{vpp::ByteView(shadersArchive.data(), shadersArchive.size())};
    std::map<std::string, std::vector<std::string>> stemToFiles;
    collectFxoStems(shadersContainer, stemToFiles);
    printf("[shaderHash join] real .fxo_pc distinct stems: %zu\n\n", stemToFiles.size());

    std::vector<uint8_t> cityArchive = readFile(kCacheDir + "/sr3_city_0.vpp_pc");
    if (cityArchive.empty()) { printf("FATAL: sr3_city_0.vpp_pc\n"); return 1; }
    vpp::Container cityContainer{vpp::ByteView(cityArchive.data(), cityArchive.size())};
    size_t h0Index = SIZE_MAX;
    for (size_t i = 0; i < cityContainer.entries().size(); ++i)
        if (lower(cityContainer.entries()[i].name) == "1018h0.str2_pc") { h0Index = i; break; }
    if (h0Index == SIZE_MAX) { printf("FATAL: 1018h0.str2_pc not found\n"); return 1; }
    vpp::Container h0Container = cityContainer.openNested(h0Index);

    std::vector<uint8_t> clBytes;
    {
        size_t clIdx = SIZE_MAX;
        for (size_t i = 0; i < h0Container.entries().size(); ++i)
            if (lower(h0Container.entries()[i].name) == "airport_controltower.clmesh_pc") clIdx = i;
        if (clIdx == SIZE_MAX || !entryBytes(h0Container, clIdx, clBytes)) { printf("FATAL: tower clmesh\n"); return 1; }
    }
    vpp::ByteView clView(clBytes.data(), clBytes.size());
    sr3clmesh::LevelMesh lm;
    try { lm = sr3clmesh::LevelMesh::parse(clView); } catch (const std::exception& ex) { printf("FATAL parse: %s\n", ex.what()); return 1; }
    if (!lm.walkComplete()) { printf("FATAL: walk incomplete\n"); return 1; }
    std::vector<sr3clmesh::MaterialRecord> matRecs = lm.materialRecords(clView);
    printf("[tower] %zu material records\n\n", matRecs.size());

    std::map<uint32_t, std::string> crcToStem;
    for (const auto& kv : stemToFiles) {
        uint32_t crc = sr3fxo::crc32Raw(reinterpret_cast<const uint8_t*>(kv.first.data()), kv.first.size());
        crcToStem[crc] = kv.first;
    }

    std::set<std::string> testedStems;
    size_t bareExists = 0, bareRole4Ok = 0, bareRole6Ok = 0, bareBothOk = 0;
    for (size_t mi = 0; mi < matRecs.size(); ++mi) {
        auto it = crcToStem.find(matRecs[mi].hash0);
        if (it == crcToStem.end()) { printf("  material[%zu]: hash0 MISS\n", mi); continue; }
        const std::string& stem = it->second;
        // already-drawable via existing _v/_mv convention?
        bool hasSuffixedVs = false;
        for (const auto& f : stemToFiles[stem]) if (endsWithStr(lower(f), "_v.fxo_pc") || endsWithStr(lower(f), "_mv.fxo_pc")) hasSuffixedVs = true;
        if (hasSuffixedVs) { printf("  material[%zu]: stem=%-32s already drawable via _v/_mv - skip\n", mi, stem.c_str()); continue; }
        if (testedStems.count(stem)) continue;
        testedStems.insert(stem);
        std::string bareName = stem + ".fxo_pc";
        bool found = false;
        for (const auto& f : stemToFiles[stem]) if (lower(f) == bareName) found = true;
        printf("  stem=%-32s candidateFiles=[", stem.c_str());
        for (const auto& f : stemToFiles[stem]) printf(" %s", f.c_str());
        printf(" ]\n");
        if (!found) { printf("    bare '%s' NOT present among real files\n", bareName.c_str()); }
        else { ++bareExists; }

        // THOROUGH check (not just the bare-stem hypothesis): open EVERY
        // real candidate file for this stem - suffixed or bare - and test
        // its REAL header bytes (not its file name) for a structurally
        // valid role4/role6 T8 VS+PS pairing, via the exact same
        // pairVsPsForRole() mechanism prototype_lit_clmesh_tower.cpp's own
        // pipeline already uses. This directly tests the lead's own
        // framing ("a name with no matching suffix is class 0, variant 0...
        // It is NOT a structural requirement") against EVERY real file this
        // stem actually has, not just the bare one.
        bool anyRole4Ok = false, anyRole6Ok = false, anyBothOk = false;
        for (const auto& f : stemToFiles[stem]) {
            std::vector<uint8_t> fxoBytes;
            if (!findEntry(shadersContainer, f, fxoBytes)) { printf("    %-40s findEntry FAILED\n", f.c_str()); continue; }
            sr3fxo::WrapperHeader wh; std::string why;
            vpp::ByteView view(fxoBytes.data(), fxoBytes.size());
            bool hdrOk = sr3fxo::WrapperHeader::tryParse(view, wh, why);
            if (!hdrOk) { printf("    %-40s header parse FAILED: %s\n", f.c_str(), why.c_str()); continue; }
            BlobPairResult r4 = pairVsPsForRole(fxoBytes, 4);
            BlobPairResult r6 = pairVsPsForRole(fxoBytes, 6);
            std::string r4msg = r4.ok ? ("OK(pass " + std::to_string(r4.passIndex) + " vsLen=" + std::to_string(r4.vsLength) + " psLen=" + std::to_string(r4.psLength) + ")") : ("fail:" + r4.reason);
            std::string r6msg = r6.ok ? ("OK(pass " + std::to_string(r6.passIndex) + " vsLen=" + std::to_string(r6.vsLength) + " psLen=" + std::to_string(r6.psLength) + ")") : ("fail:" + r6.reason);
            printf("    %-40s nVS=%d nMid=%d nPS=%d passCount=%u flags=0x%X  role4=%s  role6=%s\n",
                   f.c_str(), wh.vertexCount(), wh.middleCount(), wh.pixelCount(), wh.passCount(), wh.flags(), r4msg.c_str(), r6msg.c_str());
            if (r4.ok) anyRole4Ok = true;
            if (r6.ok) anyRole6Ok = true;
            if (r4.ok && r6.ok) anyBothOk = true;
        }
        if (anyRole4Ok) ++bareRole4Ok;
        if (anyRole6Ok) ++bareRole6Ok;
        if (anyBothOk) ++bareBothOk;
    }
    printf("\n[LEAD1 SUMMARY] distinct non-drawable stems tested=%zu bareFileExists=%zu anyFileRole4Ok=%zu anyFileRole6Ok=%zu anyFileBothOk=%zu\n\n",
           testedStems.size(), bareExists, bareRole4Ok, bareRole6Ok, bareBothOk);

    printf("=== LEAD 2: real B (constant-name-hash) / C (vec4) material-record arrays ===\n\n");
    // Re-derive rec.offset (header_start) the SAME way materialRecords() does,
    // but via the public offsets already exposed (rec.offset IS header_start,
    // per level_mesh.h's own doc comment on MaterialRecord/materialRecords()).
    for (size_t mi = 0; mi < matRecs.size(); ++mi) {
        const auto& rec = matRecs[mi];
        const size_t headerStart = rec.offset; // already align8(recordOffset+4), see level_mesh.cpp materialRecords()
        const size_t aCount = rec.textureBindingCount;
        const size_t bCount = rec.constantNameCount;
        const size_t cCount = rec.vec4ConstantCount;
        const size_t runStart = headerStart + 0x30; // kMatRecordHeaderSize
        const size_t aEnd = runStart + aCount * 12;
        const size_t bStart = alignUp(aEnd, 4);
        const size_t bEnd = bStart + bCount * 4;
        const size_t cStart = alignUp(bEnd, 16);
        const size_t cEnd = cStart + cCount * 16;
        printf("  material[%zu]: header@0x%zx A(tex)=%zu B(constName)=%zu C(vec4)=%zu  bStart=0x%zx cStart=0x%zx cEnd=0x%zx fileSize=0x%zx\n",
               mi, headerStart, aCount, bCount, cCount, bStart, cStart, cEnd, clBytes.size());
        if (cEnd > clBytes.size()) { printf("      OUT OF RANGE - cannot read\n"); continue; }
        for (size_t bi = 0; bi < bCount; ++bi) {
            uint32_t h = clView.readU32LE(bStart + bi * 4);
            printf("      B[%zu] nameHash=0x%08X\n", bi, h);
        }
        for (size_t ci = 0; ci < cCount; ++ci) {
            size_t at = cStart + ci * 16;
            float v[4];
            for (int k = 0; k < 4; ++k) {
                uint32_t u = clView.readU32LE(at + k * 4);
                std::memcpy(&v[k], &u, 4);
            }
            printf("      C[%zu] vec4=(%.6f, %.6f, %.6f, %.6f)\n", ci, v[0], v[1], v[2], v[3]);
        }
    }

    printf("\n=== LEAD 2b: real CTAB constant names, hashed, cross-checked against B arrays ===\n\n");
    // For a representative resolved file per stem family, dump every real
    // CTAB constant name (role4 VS, role6 VS, role6 PS) and its
    // sr3fxo::hashLowerName() - the SAME lower-cased-name CRC32 the B array
    // is hypothesised (spec-foliage-format.md Sec6.2) to be keyed by - and
    // report which B hashes (any material, any stem) match a real name.
    auto dumpCtabForFile = [&](const std::string& fileName, size_t role, const char* roleLabel) {
        std::vector<uint8_t> fxoBytes;
        if (!findEntry(shadersContainer, fileName, fxoBytes)) { printf("  %s: findEntry failed\n", fileName.c_str()); return; }
        BlobPairResult r = pairVsPsForRole(fxoBytes, role);
        if (!r.ok) { printf("  %s role%zu: pairing failed: %s\n", fileName.c_str(), role, r.reason.c_str()); return; }
        vpp::ByteView vsBlobView(fxoBytes.data() + r.vsOffset, r.vsLength);
        vpp::ByteView psBlobView(fxoBytes.data() + r.psOffset, r.psLength);
        sr3d3d9bc::DisassembledShader vsDis = sr3d3d9bc::disassemble(vsBlobView);
        sr3d3d9bc::DisassembledShader psDis = sr3d3d9bc::disassemble(psBlobView);
        sr3d3d9bc::ConstantTable vsCtab = sr3d3d9bc::readConstantTable(vsBlobView, vsDis);
        sr3d3d9bc::ConstantTable psCtab = sr3d3d9bc::readConstantTable(psBlobView, psDis);
        printf("  %s %s (pass %d): VS CTAB status=%d constants=%u  PS CTAB status=%d constants=%u\n",
               fileName.c_str(), roleLabel, r.passIndex, (int)vsCtab.status, vsCtab.constantsCount,
               (int)psCtab.status, psCtab.constantsCount);
        for (const auto& c : vsCtab.constants) {
            uint32_t h = sr3fxo::hashLowerName(c.name);
            printf("    VS const '%s' reg=%u count=%u hash(lower)=0x%08X\n", c.name.c_str(), c.registerIndex, c.registerCount, h);
        }
        for (const auto& c : psCtab.constants) {
            uint32_t h = sr3fxo::hashLowerName(c.name);
            printf("    PS const '%s' reg=%u count=%u hash(lower)=0x%08X\n", c.name.c_str(), c.registerIndex, c.registerCount, h);
        }
    };
    auto realCasedName = [&](const std::string& stem, const std::string& suffixLower) -> std::string {
        auto it = stemToFiles.find(stem);
        if (it == stemToFiles.end()) return "";
        for (const auto& f : it->second) if (lower(f) == stem + suffixLower) return f;
        return "";
    };
    std::string bbsimple1V = realCasedName("ir_bbsimple1", "_v.fxo_pc");
    printf("-- %s (material[7]/[11]'s own real shader, existing drawable stem) --\n", bbsimple1V.c_str());
    dumpCtabForFile(bbsimple1V, 4, "role4");
    dumpCtabForFile(bbsimple1V, 6, "role6");
    std::string twoToneBs = realCasedName("ir_srtwotonediffuse_no_spec", "_bs.fxo_pc");
    printf("\n-- %s (material[2..6,9,10,14,15]'s stem, Lead1 candidate) --\n", twoToneBs.c_str());
    dumpCtabForFile(twoToneBs, 4, "role4");
    dumpCtabForFile(twoToneBs, 6, "role6");

    // ===========================================================================
    // LEAD 3 follow-up (coordinator diagnostic, 2026-09-30): the re-frozen
    // lit-tower render shows a smooth position-correlated colour gradient
    // (not texture detail, not view-angle-dependent rim lighting - uniform
    // across faces of the tower cylinder). Coordinator's hypothesis: some
    // shading term computes `viewDir = worldPos - eyePos`, and this fork's
    // own `eyePos` CTAB constant (role6 VS, real per prior LEAD 2b dump)
    // still fills PLACEHOLDER-zero (no B/C entry supplies it, and
    // prototype_lit_car.cpp has NO PRECEDENT for it - car paint's own role6
    // VS CTAB never declares an `eyePos` constant at all, confirmed by
    // grep: 0 hits in that file). Dump the REAL translated HLSL for
    // ir_bbsimple1's own role6 VS (and role4 VS, for comparison) and print
    // every line mentioning `eyePos` with context, to see EXACTLY how it is
    // consumed - not guessed at from the name alone.
    // ===========================================================================
    printf("\n=== LEAD 3: real translated HLSL around 'eyePos' usage (role6 VS, ir_bbsimple1) ===\n\n");
    auto dumpHlslEyePosContext = [&](const std::string& fileName, size_t role, const char* roleLabel) {
        std::vector<uint8_t> fxoBytes;
        if (!findEntry(shadersContainer, fileName, fxoBytes)) { printf("  %s: findEntry failed\n", fileName.c_str()); return; }
        BlobPairResult r = pairVsPsForRole(fxoBytes, role);
        if (!r.ok) { printf("  %s role%zu: pairing failed: %s\n", fileName.c_str(), role, r.reason.c_str()); return; }
        vpp::ByteView vsBlobView(fxoBytes.data() + r.vsOffset, r.vsLength);
        sr3d3d9bc::DisassembledShader vsDis = sr3d3d9bc::disassemble(vsBlobView);
        sr3d3d9bc::ConstantTable vsCtab = sr3d3d9bc::readConstantTable(vsBlobView, vsDis);
        sr3d3d9bc::TranslationResult vsTr = sr3d3d9bc::translateToHlsl(vsDis, vsCtab, vsBlobView, sr3d3d9bc::HlslTarget::SM4_5);
        printf("  --- %s %s VS, FULL translated HLSL ---\n", fileName.c_str(), roleLabel);
        printf("%s\n", vsTr.hlsl.c_str());
        printf("  --- %s %s VS, lines mentioning 'eyePos' (case-sensitive, as CTAB names it) ---\n", fileName.c_str(), roleLabel);
        std::string hay = vsTr.hlsl;
        size_t lineStart = 0;
        while (lineStart < hay.size()) {
            size_t lineEnd = hay.find('\n', lineStart);
            if (lineEnd == std::string::npos) lineEnd = hay.size();
            std::string line = hay.substr(lineStart, lineEnd - lineStart);
            if (line.find("eyePos") != std::string::npos) printf("    %s\n", line.c_str());
            lineStart = lineEnd + 1;
        }
        printf("\n");
    };
    dumpHlslEyePosContext(bbsimple1V, 4, "role4");
    dumpHlslEyePosContext(bbsimple1V, 6, "role6");

    // Companion PS dump: role6's PS is what actually CONSUMES the VS's
    // fog-factor output (TEXCOORD2.w, traced above) together with
    // Fog_color - need the real PS body to see whether it's a brightness
    // LERP (would explain a smooth intensity gradient) or something that
    // could also shift HUE (hypothesis: no, but check directly rather than
    // assume).
    auto dumpPsFull = [&](const std::string& fileName, size_t role, const char* roleLabel) {
        std::vector<uint8_t> fxoBytes;
        if (!findEntry(shadersContainer, fileName, fxoBytes)) { printf("  %s: findEntry failed\n", fileName.c_str()); return; }
        BlobPairResult r = pairVsPsForRole(fxoBytes, role);
        if (!r.ok) { printf("  %s role%zu: pairing failed: %s\n", fileName.c_str(), role, r.reason.c_str()); return; }
        vpp::ByteView psBlobView(fxoBytes.data() + r.psOffset, r.psLength);
        sr3d3d9bc::DisassembledShader psDis = sr3d3d9bc::disassemble(psBlobView);
        sr3d3d9bc::ConstantTable psCtab = sr3d3d9bc::readConstantTable(psBlobView, psDis);
        sr3d3d9bc::TranslationResult psTr = sr3d3d9bc::translateToHlsl(psDis, psCtab, psBlobView, sr3d3d9bc::HlslTarget::SM4_5);
        printf("  --- %s %s PS, FULL translated HLSL ---\n%s\n", fileName.c_str(), roleLabel, psTr.hlsl.c_str());
    };
    printf("\n=== LEAD 3b: real translated HLSL, role6 PS (consumes the VS's fog-factor output) ===\n\n");
    dumpPsFull(bbsimple1V, 6, "role6");

    // LEAD 3c: eyePos diagnostic came back NEGATIVE (empirically, via the
    // full pipeline re-run with a real eyePos - byte-identical pixel stats,
    // Fog_dist=0 already zeroes the eyePos-dependent terms in
    // ir_bbsimple1's own role6 VS). ir_bbsimple1 covers only 2 of the
    // tower's 19 materials (small support/door details) - re-check the
    // DOMINANT stem instead: ir_srtwotonediffuse_no_spec (9/19 materials,
    // most of the visible wall/roof area), whose own name ("two-tone
    // diffuse") suggests a real, INTENTIONAL two-colour blend (Diffuse_
    // Color + Diffuse_Color_2) that may legitimately use a position/height-
    // like factor - dump its full role6 VS+PS HLSL to see the real blend
    // mechanism directly, not guessed at by analogy to ir_bbsimple1.
    std::string twoToneSWins = realCasedName("ir_srtwotonediffuse_no_spec", "_s.fxo_pc"); // the ACTUAL candidate tower2.cpp's try-loop selects (not _bs - re-checked against the real resolution log)
    printf("\n=== LEAD 3c: real translated HLSL, %s role6 VS+PS (the REAL winning candidate, dominant stem, 9/19 materials) ===\n\n", twoToneSWins.c_str());
    dumpHlslEyePosContext(twoToneSWins, 4, "role4");
    dumpHlslEyePosContext(twoToneSWins, 6, "role6");
    dumpPsFull(twoToneSWins, 6, "role6");

    std::string reflectMaskS = realCasedName("ir_window_reflectmask", "_s.fxo_pc");
    printf("\n=== LEAD 3d: real translated HLSL, %s role6 VS+PS (materialId 0, name suggests reflection/gradient) ===\n\n", reflectMaskS.c_str());
    dumpHlslEyePosContext(reflectMaskS, 4, "role4");
    dumpHlslEyePosContext(reflectMaskS, 6, "role6");
    dumpPsFull(reflectMaskS, 6, "role6");

    // LEAD 3e: rigorous VS-output <-> PS-input register cross-reference by
    // REAL semantic (usage+usageIndex) from the raw DCL tokens - NOT by
    // assuming struct-field position order (the translator's PS_INPUT
    // v0..v5 numbering is the PS's OWN declared DCL order, which is not
    // guaranteed to line up positionally with the VS's o1..o6 struct - this
    // needs checking directly, not assumed, before trusting which PS
    // register actually receives the eyePos-derived fade term).
    printf("\n=== LEAD 3e: real DCL usage/usageIndex/register cross-reference, %s role6 ===\n\n", reflectMaskS.c_str());
    {
        std::vector<uint8_t> fxoBytes;
        if (findEntry(shadersContainer, reflectMaskS, fxoBytes)) {
            BlobPairResult r = pairVsPsForRole(fxoBytes, 6);
            if (r.ok) {
                vpp::ByteView vsBlobView(fxoBytes.data() + r.vsOffset, r.vsLength);
                vpp::ByteView psBlobView(fxoBytes.data() + r.psOffset, r.psLength);
                sr3d3d9bc::DisassembledShader vsDis = sr3d3d9bc::disassemble(vsBlobView);
                sr3d3d9bc::DisassembledShader psDis = sr3d3d9bc::disassemble(psBlobView);
                printf("  -- VS real DCL tokens (dest register, usage, usageIndex) --\n");
                for (const auto& inst : vsDis.instructions) {
                    if (inst.opcode != sr3d3d9bc::Opcode::DCL || !inst.dcl.has_value() || !inst.dest.has_value()) continue;
                    printf("    dest regType=%u regNum=%u  usage=%u usageIndex=%u\n",
                           inst.dest->registerTypeRaw, inst.dest->registerNumber, inst.dcl->usage, inst.dcl->usageIndex);
                }
                printf("  -- PS real DCL tokens (dest register, usage, usageIndex) --\n");
                for (const auto& inst : psDis.instructions) {
                    if (inst.opcode != sr3d3d9bc::Opcode::DCL || !inst.dcl.has_value() || !inst.dest.has_value()) continue;
                    printf("    dest regType=%u regNum=%u  usage=%u usageIndex=%u\n",
                           inst.dest->registerTypeRaw, inst.dest->registerNumber, inst.dcl->usage, inst.dcl->usageIndex);
                }
            } else {
                printf("  pairing failed: %s\n", r.reason.c_str());
            }
        }
    }

    printf("\n=== done ===\n");
    return 0;
}
