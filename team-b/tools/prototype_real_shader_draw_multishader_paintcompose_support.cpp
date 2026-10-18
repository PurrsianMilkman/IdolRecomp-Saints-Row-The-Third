// Helper definitions split out of tools/prototype_real_shader_draw_multishader_paintcompose.cpp (see
// prototype_real_shader_draw_multishader_paintcompose_support.h).  Bodies moved verbatim; only the
// enclosing namespace changed (anonymous -> paintcompose).
#include "prototype_real_shader_draw_multishader_paintcompose_support.h"

namespace paintcompose {


// ===========================================================================
// Small generic helpers (identical to prototype_real_shader_draw.cpp's own)
// ===========================================================================

std::string hrToString(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
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
std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
bool containsCI(const std::string& haystack, const std::string& needleLower) {
    return lower(haystack).find(needleLower) != std::string::npos;
}
bool endsWithStr(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }


const char* usageName(uint8_t u) {
    switch (u) {
        case kPOSITION: return "POSITION";
        case kBLENDWEIGHT: return "BLENDWEIGHT";
        case kBLENDINDICES: return "BLENDINDICES";
        case kNORMAL: return "NORMAL";
        case kTEXCOORD: return "TEXCOORD";
        case kTANGENT: return "TANGENT";
        default: return "?";
    }
}

std::vector<VsInputField> extractVsInputSignature(const sr3d3d9bc::DisassembledShader& d) {
    std::vector<VsInputField> out;
    for (const auto& inst : d.instructions) {
        if (inst.opcode != sr3d3d9bc::Opcode::DCL) continue;
        if (!inst.dcl.has_value() || !inst.dest.has_value()) continue;
        if (inst.dest->registerTypeRaw != 1 /* D3DSPR_INPUT */) continue;
        out.push_back({inst.dest->registerNumber, inst.dcl->usage, inst.dcl->usageIndex});
    }
    std::stable_sort(out.begin(), out.end(), [](const VsInputField& a, const VsInputField& b) { return a.reg < b.reg; });
    return out;
}
std::string signatureToString(const std::vector<VsInputField>& sig) {
    std::string s;
    for (const auto& f : sig) { s += usageName(f.usage); s += std::to_string(f.usageIndex); s += " "; }
    return s;
}

void packColumn(const float M[16], int col, float out4[4]) {
    out4[0] = M[0 * 4 + col];
    out4[1] = M[1 * 4 + col];
    out4[2] = M[2 * 4 + col];
    out4[3] = M[3 * 4 + col];
}
void transformPointRowVector(const float p[3], const float M[16], float out[3]) {
    out[0] = p[0] * M[0] + p[1] * M[4] + p[2] * M[8] + M[12];
    out[1] = p[0] * M[1] + p[1] * M[5] + p[2] * M[9] + M[13];
    out[2] = p[0] * M[2] + p[1] * M[6] + p[2] * M[10] + M[14];
}
void computeOrbitViewMatrix(const float boundsMin[3], const float boundsMax[3], float yaw, float pitch,
                             float distanceScale, float outView[16]) {
    float centre[3]; float extent = 0.0f;
    for (int i = 0; i < 3; ++i) {
        centre[i] = (boundsMin[i] + boundsMax[i]) * 0.5f;
        float span = boundsMax[i] - boundsMin[i];
        if (span > extent) extent = span;
    }
    if (extent <= 0.0f) extent = 1.0f;
    const float distance = extent * distanceScale;
    float eye[3] = {centre[0] + distance * std::cos(pitch) * std::sin(yaw), centre[1] + distance * std::sin(pitch),
                     centre[2] + distance * std::cos(pitch) * std::cos(yaw)};
    float forward[3] = {centre[0] - eye[0], centre[1] - eye[1], centre[2] - eye[2]};
    float flen = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (flen <= 0.0f) flen = 1.0f;
    for (float& f : forward) f /= flen;
    const float worldUp[3] = {0, 1, 0};
    float right[3] = {forward[1] * worldUp[2] - forward[2] * worldUp[1], forward[2] * worldUp[0] - forward[0] * worldUp[2],
                       forward[0] * worldUp[1] - forward[1] * worldUp[0]};
    float rlen = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (rlen <= 0.0f) rlen = 1.0f;
    for (float& r : right) r /= rlen;
    float up[3] = {right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                   right[0] * forward[1] - right[1] * forward[0]};
    float view[16] = {right[0], up[0], forward[0], 0, right[1], up[1], forward[1], 0,
                       right[2], up[2], forward[2], 0,
                       -(right[0]*eye[0]+right[1]*eye[1]+right[2]*eye[2]),
                       -(up[0]*eye[0]+up[1]*eye[1]+up[2]*eye[2]),
                       -(forward[0]*eye[0]+forward[1]*eye[1]+forward[2]*eye[2]), 1};
    std::memcpy(outView, view, sizeof(view));
}

void fillFloat4Registers(std::vector<uint8_t>& cpuBuffer, uint16_t registerIndex, uint16_t registerCount,
                          const std::vector<std::array<float, 4>>& regs) {
    for (uint16_t r = 0; r < registerCount && r < regs.size(); ++r) {
        size_t byteOff = static_cast<size_t>(registerIndex + r) * 16;
        if (byteOff + 16 > cpuBuffer.size()) continue;
        std::memcpy(cpuBuffer.data() + byteOff, regs[r].data(), 16);
    }
}


// ===========================================================================
// STEP 1/2: real shaderHash -> real stem -> real vertex-stage .fxo_pc file.
// ===========================================================================

// spec-fxo-format.md §6.6's own 13 suffixes, longest-match-first (same
// table as the session scratchpad tool verify_stem_crc.cpp, independently
// re-derived from the spec's own text, 17/17-verified there).
const std::vector<std::string> kStageSuffixes = {
    "_bms", "_bmc", "_bs", "_bc", "_ms", "_mc", "_mv", "_ts",
    "_fd", "_s", "_c", "_t", "_v",
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
            std::string stem = stripStageSuffix(noExt);
            stemToFiles[stem].push_back(n);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { collectFxoStems(c.openNested(i), stemToFiles); } catch (const std::exception&) {}
        }
    }
}

// spec-fxo-format.md §6.6: "_v"/"_mv" are BOTH class-5 ("vertex"), variants
// 0 and 2. Prefer "_v" (variant 0); fall back to "_mv" (variant 2) only if
// no "_v" file exists for this stem. If neither exists, reports plainly
// which OTHER real files this stem does have (a genuine possible outcome,
// not assumed impossible per the task's own explicit instruction).
std::string pickVertexFile(const std::vector<std::string>& files, std::string& reasonIfNone) {
    for (const auto& f : files) if (endsWithStr(lower(f), "_v.fxo_pc")) return f;
    for (const auto& f : files) if (endsWithStr(lower(f), "_mv.fxo_pc")) return f;
    reasonIfNone = "stem resolved but has no _v/_mv real file among:";
    for (const auto& f : files) reasonIfNone += " " + f;
    return "";
}

MaterialArrayBCResult extractMaterialArrayBC(vpp::ByteView content, size_t subheaderOffset,
                                              size_t meshSubBlockOffset, uint32_t meshCLength,
                                              uint16_t materialCount) {
    MaterialArrayBCResult result;
    if (materialCount == 0 || materialCount > 4096) { result.note = "implausible materialCount"; return result; }
    const uint16_t n2 = content.readU16LE(subheaderOffset + 0x0E);
    size_t cursor = static_cast<size_t>(meshSubBlockOffset) + meshCLength;
    if (n2 > 0) {
        cursor = alignUp(cursor, 8);
        cursor += static_cast<size_t>(n2) * 8 + static_cast<size_t>(n2) * 4;
    }
    cursor = alignUp(cursor, 8);
    cursor += static_cast<size_t>(materialCount) * 8;

    for (uint16_t i = 0; i < materialCount; ++i) {
        if (cursor + 4 > content.size()) { result.note = "ran out of bytes at material " + std::to_string(i); return result; }
        uint32_t declaredSize = content.readU32LE(cursor);
        size_t headerStart = alignUp(cursor + 4, 8);
        if (headerStart + 0x30 > content.size()) { result.note = "header overrun at material " + std::to_string(i); return result; }
        uint16_t aCount = content.readU16LE(headerStart + 0x0C);
        uint8_t bCount = content.at(headerStart + 0x0E);
        size_t arrayABase = headerStart + 0x30;
        size_t arrayBBase = alignUp(arrayABase + static_cast<size_t>(aCount) * 12, 4);
        size_t arrayCBase = alignUp(arrayBBase + static_cast<size_t>(bCount) * 4, 16);
        std::vector<MaterialConstant> consts;
        for (uint8_t k = 0; k < bCount; ++k) {
            size_t bOff = arrayBBase + static_cast<size_t>(k) * 4;
            size_t cOff = arrayCBase + static_cast<size_t>(k) * 16;
            if (bOff + 4 > content.size() || cOff + 16 > content.size()) break;
            MaterialConstant mc;
            mc.nameHash = content.readU32LE(bOff);
            for (int w = 0; w < 4; ++w) {
                uint32_t u = content.readU32LE(cOff + static_cast<size_t>(w) * 4);
                std::memcpy(&mc.value[static_cast<size_t>(w)], &u, 4);
            }
            consts.push_back(mc);
        }
        result.perMaterial.push_back(std::move(consts));
        if (declaredSize == 0 || declaredSize > 1000000) {
            result.note = "implausible declaredSize=" + std::to_string(declaredSize) + " at material " + std::to_string(i);
            return result;
        }
        cursor += declaredSize;
    }
    result.complete = true;
    return result;
}

MaterialShaderHashResult extractMaterialShaderHashes(vpp::ByteView content, size_t subheaderOffset,
                                                       size_t meshSubBlockOffset, uint32_t meshCLength,
                                                       uint16_t materialCount) {
    MaterialShaderHashResult result;
    if (materialCount == 0 || materialCount > 4096) { result.note = "implausible materialCount"; return result; }
    const uint16_t n2 = content.readU16LE(subheaderOffset + 0x0E);
    size_t cursor = static_cast<size_t>(meshSubBlockOffset) + meshCLength;
    if (n2 > 0) {
        cursor = alignUp(cursor, 8);
        cursor += static_cast<size_t>(n2) * 8 + static_cast<size_t>(n2) * 4; // array_A, array_B
    }
    cursor = alignUp(cursor, 8);
    cursor += static_cast<size_t>(materialCount) * 8; // array_C

    for (uint16_t i = 0; i < materialCount; ++i) {
        if (cursor + 4 > content.size()) { result.note = "ran out of bytes at material " + std::to_string(i); return result; }
        uint32_t declaredSize = content.readU32LE(cursor);
        size_t headerStart = alignUp(cursor + 4, 8);
        if (headerStart + 0x30 > content.size()) { result.note = "header overrun at material " + std::to_string(i); return result; }
        uint32_t shaderHash = content.readU32LE(headerStart + 0x00);
        result.hashes.push_back(shaderHash);
        if (declaredSize == 0 || declaredSize > 1000000) {
            result.note = "implausible declaredSize=" + std::to_string(declaredSize) + " at material " + std::to_string(i);
            return result;
        }
        cursor += declaredSize;
    }
    result.complete = true;
    return result;
}

BlobPairResult pairVsPs(const std::vector<uint8_t>& fxoBytes) {
    BlobPairResult r;
    sr3fxo::WrapperHeader wh;
    std::string why;
    vpp::ByteView view(fxoBytes.data(), fxoBytes.size());
    if (!sr3fxo::WrapperHeader::tryParse(view, wh, why)) { r.reason = "WrapperHeader::tryParse failed: " + why; return r; }
    size_t endOfBlobs = 0;
    std::vector<sr3fxo::WrapperBlob> blobs = wh.layoutBlobs(endOfBlobs);
    const sr3fxo::WrapperBlob* vsBlob = nullptr;
    for (const auto& b : blobs) {
        if (b.stage == sr3fxo::Stage::Vertex) { vsBlob = &b; break; } // first real (non-zero-length) VS entry
    }
    if (!vsBlob) { r.reason = "file has no non-empty vertex-stage blob"; return r; }
    r.vsOffset = vsBlob->offset; r.vsLength = vsBlob->length; r.vsTableIndex = vsBlob->tableIndex;
    bool psFound = false;
    for (const auto& p : wh.passes()) {
        if (p.vertexIndex != static_cast<int16_t>(vsBlob->tableIndex)) continue;
        for (const auto& pb : blobs) {
            if (pb.stage == sr3fxo::Stage::Pixel && static_cast<int16_t>(pb.tableIndex) == p.pixelIndex) {
                r.psOffset = pb.offset; r.psLength = pb.length; psFound = true;
            }
        }
    }
    if (!psFound) { r.reason = "real T8 pass table has no PS paired with VS tableIndex " + std::to_string(vsBlob->tableIndex); return r; }
    r.ok = true;
    return r;
}

// NEW in this fork: pairs the VS/PS blobs for one EXPLICIT T8 pass index
// (rather than pairVsPs()'s "first non-empty VS blob", which always lands
// on pass 0 for ir_sr3carpaint_gr_v.fxo_pc, since every one of its 5 real
// passes has its own distinct VS table entry - table index 0..4, one per
// pass, not shared). Used ONLY for the ir_sr3carpaint_gr stem, to draw with
// pass 2 (the real forward/composite pass, see this file's own top
// comment) instead of pass 0 (the real deferred G-buffer pass).
BlobPairResult pairVsPsForPass(const std::vector<uint8_t>& fxoBytes, size_t wantPassIndex) {
    BlobPairResult r;
    sr3fxo::WrapperHeader wh;
    std::string why;
    vpp::ByteView view(fxoBytes.data(), fxoBytes.size());
    if (!sr3fxo::WrapperHeader::tryParse(view, wh, why)) { r.reason = "WrapperHeader::tryParse failed: " + why; return r; }
    if (wantPassIndex >= wh.passes().size()) {
        r.reason = "requested pass index " + std::to_string(wantPassIndex) + " >= real passCount " +
                   std::to_string(wh.passes().size());
        return r;
    }
    const sr3fxo::PassEntry& p = wh.passes()[wantPassIndex];
    size_t endOfBlobs = 0;
    std::vector<sr3fxo::WrapperBlob> blobs = wh.layoutBlobs(endOfBlobs);
    const sr3fxo::WrapperBlob* vsBlob = nullptr;
    const sr3fxo::WrapperBlob* psBlob = nullptr;
    for (const auto& b : blobs) {
        if (b.stage == sr3fxo::Stage::Vertex && static_cast<int16_t>(b.tableIndex) == p.vertexIndex) vsBlob = &b;
        if (b.stage == sr3fxo::Stage::Pixel && static_cast<int16_t>(b.tableIndex) == p.pixelIndex) psBlob = &b;
    }
    if (!vsBlob) { r.reason = "pass " + std::to_string(wantPassIndex) + "'s own vertexIndex has no real non-empty VS blob"; return r; }
    if (!psBlob) { r.reason = "pass " + std::to_string(wantPassIndex) + "'s own pixelIndex has no real non-empty PS blob"; return r; }
    r.vsOffset = vsBlob->offset; r.vsLength = vsBlob->length; r.vsTableIndex = vsBlob->tableIndex;
    r.psOffset = psBlob->offset; r.psLength = psBlob->length;
    r.ok = true;
    return r;
}

}  // namespace paintcompose
