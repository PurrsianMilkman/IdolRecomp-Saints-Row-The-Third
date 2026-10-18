// Function bodies for prototype_lit_clmesh_tower_helpers.h (moved verbatim from the former
// anonymous namespace of prototype_lit_clmesh_tower.cpp).

#include "prototype_lit_clmesh_tower_helpers.h"

namespace tower_proto {

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
bool endsWithNoCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}
size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

// NEW in this fork: a diagnostic readback of an intermediate off-screen
// target (G-buffer normals, light-accumulation buffer, ...), printed as the
// SAME per-channel min/max/mean stats the final composited image gets -
// concrete, measured evidence (not just an assertion) that the pipeline's
// upstream stages are genuinely non-trivial even where the FINAL per-
// material colour comes out flat black (see this file's own top comment).
void printTargetStats(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* tex, uint32_t w, uint32_t h,
                       const char* label) {
    D3D11_TEXTURE2D_DESC td{}; tex->GetDesc(&td);
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device->CreateTexture2D(&sd, nullptr, &staging))) { printf("  [%s] staging texture creation failed\n", label); return; }
    context->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &m))) { printf("  [%s] Map failed\n", label); staging->Release(); return; }
    uint64_t sum[4] = {0,0,0,0}; uint8_t minV[4] = {255,255,255,255}, maxV[4] = {0,0,0,0};
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch;
        for (uint32_t x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c) { uint8_t v = row[x*4+c]; sum[c]+=v; if (v<minV[c]) minV[c]=v; if (v>maxV[c]) maxV[c]=v; }
    }
    context->Unmap(staging, 0);
    staging->Release();
    size_t n = static_cast<size_t>(w) * h;
    printf("  [%s] R:min=%u,max=%u,mean=%.2f  G:min=%u,max=%u,mean=%.2f  B:min=%u,max=%u,mean=%.2f  A:min=%u,max=%u,mean=%.2f\n",
           label, minV[0], maxV[0], n ? (double)sum[0]/n : 0.0, minV[1], maxV[1], n ? (double)sum[1]/n : 0.0,
           minV[2], maxV[2], n ? (double)sum[2]/n : 0.0, minV[3], maxV[3], n ? (double)sum[3]/n : 0.0);
}
const char* usageName(uint8_t u) {
    switch (u) {
        case kPOSITION: return "POSITION"; case kBLENDWEIGHT: return "BLENDWEIGHT";
        case kBLENDINDICES: return "BLENDINDICES"; case kNORMAL: return "NORMAL";
        case kTEXCOORD: return "TEXCOORD"; case kTANGENT: return "TANGENT"; default: return "?";
    }
}
std::vector<VsInputField> extractVsInputSignature(const sr3d3d9bc::DisassembledShader& d) {
    std::vector<VsInputField> out;
    for (const auto& inst : d.instructions) {
        if (inst.opcode != sr3d3d9bc::Opcode::DCL) continue;
        if (!inst.dcl.has_value() || !inst.dest.has_value()) continue;
        if (inst.dest->registerTypeRaw != 1) continue;
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

// ===========================================================================
// Matrix helpers - identical to prototype_lit_car.cpp's own.
// ===========================================================================
void packColumn(const float M[16], int col, float out4[4]) {
    out4[0] = M[0 * 4 + col]; out4[1] = M[1 * 4 + col]; out4[2] = M[2 * 4 + col]; out4[3] = M[3 * 4 + col];
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
void buildPerspectiveLH(float fovYRadians, float aspect, float nearZ, float farZ, float out[16]) {
    float yScale = 1.0f / std::tan(fovYRadians * 0.5f);
    float xScale = yScale / aspect;
    float M[16] = {
        xScale, 0, 0, 0,
        0, yScale, 0, 0,
        0, 0, farZ / (farZ - nearZ), 1,
        0, 0, -nearZ * farZ / (farZ - nearZ), 0,
    };
    std::memcpy(out, M, sizeof(M));
}
bool invert4x4(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::fabs(det) < 1e-12f) {
        for (int i = 0; i < 16; ++i) out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        return false;
    }
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * det;
    return true;
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
// real shaderHash -> real stem -> real vertex-stage .fxo_pc file (identical
// to prototype_lit_car.cpp's own - see spec-fxo-format.md Sec6.6 for the
// real 13-suffix stripping rule this implements).
// ===========================================================================
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
std::string pickVertexFile(const std::vector<std::string>& files, std::string& reasonIfNone) {
    for (const auto& f : files) if (endsWithStr(lower(f), "_v.fxo_pc")) return f;
    for (const auto& f : files) if (endsWithStr(lower(f), "_mv.fxo_pc")) return f;
    reasonIfNone = "stem resolved but has no _v/_mv real file among:";
    for (const auto& f : files) reasonIfNone += " " + f;
    return "";
}

// ===========================================================================
// Real T8 VS<->PS pairing BY REAL ROLE NUMBER - identical to
// prototype_lit_car.cpp's own (already generalized to "whatever pass index
// this file's own header ACTUALLY declares for role R", not hardcoded to
// any one shader family).
// ===========================================================================
struct BlobPairResult {
    bool ok = false;
    std::string reason;
    size_t vsOffset = 0, vsLength = 0, psOffset = 0, psLength = 0;
    int passIndex = -1;
};
BlobPairResult pairVsPsForRole(const std::vector<uint8_t>& fxoBytes, size_t role) {
    BlobPairResult r;
    sr3fxo::WrapperHeader wh;
    std::string why;
    vpp::ByteView view(fxoBytes.data(), fxoBytes.size());
    if (!sr3fxo::WrapperHeader::tryParse(view, wh, why)) { r.reason = "WrapperHeader::tryParse failed: " + why; return r; }
    if (!wh.rolePresent(role)) { r.reason = "real role table: role " + std::to_string(role) + " not present (flags=0x" +
                                             hrToString(wh.flags()) + ")"; return r; }
    int8_t passIdx = wh.roleIndexByte(role);
    if (passIdx < 0 || static_cast<size_t>(passIdx) >= wh.passes().size()) {
        r.reason = "real role table: role " + std::to_string(role) + " roleIndexByte=" + std::to_string(passIdx) +
                   " out of range (passCount=" + std::to_string(wh.passes().size()) + ")";
        return r;
    }
    r.passIndex = passIdx;
    const sr3fxo::PassEntry& p = wh.passes()[static_cast<size_t>(passIdx)];
    size_t endOfBlobs = 0;
    std::vector<sr3fxo::WrapperBlob> blobs = wh.layoutBlobs(endOfBlobs);
    const sr3fxo::WrapperBlob* vsBlob = nullptr;
    const sr3fxo::WrapperBlob* psBlob = nullptr;
    for (const auto& b : blobs) {
        if (b.stage == sr3fxo::Stage::Vertex && static_cast<int16_t>(b.tableIndex) == p.vertexIndex) vsBlob = &b;
        if (b.stage == sr3fxo::Stage::Pixel && static_cast<int16_t>(b.tableIndex) == p.pixelIndex) psBlob = &b;
    }
    if (!vsBlob) { r.reason = "pass " + std::to_string(passIdx) + " (role " + std::to_string(role) + ")'s vertexIndex has no real non-empty VS blob"; return r; }
    if (!psBlob) { r.reason = "pass " + std::to_string(passIdx) + " (role " + std::to_string(role) + ")'s pixelIndex has no real non-empty PS blob"; return r; }
    r.vsOffset = vsBlob->offset; r.vsLength = vsBlob->length;
    r.psOffset = psBlob->offset; r.psLength = psBlob->length;
    r.ok = true;
    return r;
}

CompiledShader compileOnePass(ID3D11Device* device, const std::vector<uint8_t>& fxoBytes, const std::string& vsFileName,
                               const std::string& stem, size_t role) {
    CompiledShader cs;
    cs.vsFileName = vsFileName; cs.stem = stem; cs.roleUsed = static_cast<int>(role);
    BlobPairResult pair = pairVsPsForRole(fxoBytes, role);
    if (!pair.ok) { cs.failReason = "role " + std::to_string(role) + " pairing failed: " + pair.reason; return cs; }
    cs.passIndexUsed = pair.passIndex;

    vpp::ByteView vsBlobView(fxoBytes.data() + pair.vsOffset, pair.vsLength);
    vpp::ByteView psBlobView(fxoBytes.data() + pair.psOffset, pair.psLength);
    cs.vsDis = sr3d3d9bc::disassemble(vsBlobView);
    cs.psDis = sr3d3d9bc::disassemble(psBlobView);
    cs.vsCtab = sr3d3d9bc::readConstantTable(vsBlobView, cs.vsDis);
    cs.psCtab = sr3d3d9bc::readConstantTable(psBlobView, cs.psDis);
    cs.vsTr = sr3d3d9bc::translateToHlsl(cs.vsDis, cs.vsCtab, vsBlobView, sr3d3d9bc::HlslTarget::SM4_5);
    cs.psTr = sr3d3d9bc::translateToHlsl(cs.psDis, cs.psCtab, psBlobView, sr3d3d9bc::HlslTarget::SM4_5);
    cs.vsInputs = extractVsInputSignature(cs.vsDis);

    for (const auto& c : cs.psCtab.constants) {
        if (c.registerSet != sr3d3d9bc::RegisterSet::Sampler) continue;
        if (containsCI(c.name, "ir_lbuffersampler")) cs.lBufferSamplerReg = c.registerIndex;
        if (containsCI(c.name, "ir_gbuffer_normalssampler")) cs.normalsSamplerReg = c.registerIndex;
        if (containsCI(c.name, "ir_gbuffer_lightingsampler")) cs.lightingSamplerReg = c.registerIndex;
        // NEW real sampler-name matches this building-shader family declares
        // (car paint's role4/role6 never did - see top comment).
        if (containsCI(c.name, "normal_mapsampler")) cs.normalMapSamplerReg = c.registerIndex;
        if (containsCI(c.name, "diffuse_mapsampler")) cs.diffuseMapSamplerReg = c.registerIndex;
        if (containsCI(c.name, "specular_mapsampler")) cs.specularMapSamplerReg = c.registerIndex;
    }

    ID3DBlob *vsErr = nullptr, *psErr = nullptr;
    HRESULT hr = D3DCompile(cs.vsTr.hlsl.c_str(), cs.vsTr.hlsl.size(), vsFileName.c_str(), nullptr, nullptr,
                             cs.vsTr.entryPoint.c_str(), cs.vsTr.targetProfile.c_str(), D3DCOMPILE_SKIP_OPTIMIZATION, 0,
                             &cs.vsCode, &vsErr);
    if (FAILED(hr)) {
        cs.failReason = "D3DCompile(VS) failed: " + hrToString(hr);
        if (vsErr) cs.failReason += std::string(" - ") + static_cast<const char*>(vsErr->GetBufferPointer());
        safeRelease(vsErr);
        return cs;
    }
    safeRelease(vsErr);
    hr = D3DCompile(cs.psTr.hlsl.c_str(), cs.psTr.hlsl.size(), (vsFileName + "_ps" + std::to_string(role)).c_str(),
                     nullptr, nullptr, cs.psTr.entryPoint.c_str(), cs.psTr.targetProfile.c_str(),
                     D3DCOMPILE_SKIP_OPTIMIZATION, 0, &cs.psCode, &psErr);
    if (FAILED(hr)) {
        cs.failReason = "D3DCompile(PS) failed: " + hrToString(hr);
        if (psErr) cs.failReason += std::string(" - ") + static_cast<const char*>(psErr->GetBufferPointer());
        safeRelease(psErr);
        return cs;
    }
    safeRelease(psErr);

    D3DReflect(cs.vsCode->GetBufferPointer(), cs.vsCode->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
               reinterpret_cast<void**>(&cs.vsRefl));
    D3DReflect(cs.psCode->GetBufferPointer(), cs.psCode->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
               reinterpret_cast<void**>(&cs.psRefl));
    if (!cs.vsRefl || !cs.psRefl) { cs.failReason = "D3DReflect failed"; return cs; }
    { ID3D11ShaderReflectionConstantBuffer* cb = cs.vsRefl->GetConstantBufferByIndex(0); D3D11_SHADER_BUFFER_DESC bd{};
      if (cb) cb->GetDesc(&bd); cs.vsCbSize = bd.Size; }
    { ID3D11ShaderReflectionConstantBuffer* cb = cs.psRefl->GetConstantBufferByIndex(0); D3D11_SHADER_BUFFER_DESC bd{};
      if (cb) cb->GetDesc(&bd); cs.psCbSize = bd.Size; }
    D3D11_SHADER_DESC psDesc{};
    cs.psRefl->GetDesc(&psDesc);
    cs.psOutputCount = psDesc.OutputParameters;
    for (UINT r = 0; r < psDesc.BoundResources; ++r) {
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        cs.psRefl->GetResourceBindingDesc(r, &bind);
        if (bind.Type == D3D_SIT_TEXTURE) cs.allTexBindPoints.push_back(bind.BindPoint);
        else if (bind.Type == D3D_SIT_SAMPLER) cs.allSamplerBindPoints.push_back(bind.BindPoint);
    }

    // Subset check identical to prototype_lit_car.cpp's own: any subset of
    // {POSITION0,NORMAL0,TANGENT0,BLENDINDICES0,TEXCOORD0}, OR the real
    // light-shader signature {POSITION0,TEXCOORD0}. Measured this session:
    // ir_bbsimple1's real role4/role6 VS input DCLs are subsets of the SAME
    // known set (register ORDER differs from car paint's, but D3D11
    // CreateInputLayout matches by semantic name+index, not register order,
    // so kFullLayout below needs no change for that).
    auto hasUsage = [&](uint8_t usage, uint8_t idx) {
        for (const auto& f : cs.vsInputs) if (f.usage == usage && f.usageIndex == idx) return true;
        return false;
    };
    bool isQuadShape = cs.vsInputs.size() == 2 && hasUsage(kPOSITION, 0) && hasUsage(kTEXCOORD, 0);
    bool knownSubset = hasUsage(kPOSITION, 0);
    for (const auto& f : cs.vsInputs) {
        bool known = (f.usage == kPOSITION && f.usageIndex == 0) || (f.usage == kNORMAL && f.usageIndex == 0) ||
                     (f.usage == kTANGENT && f.usageIndex == 0) || (f.usage == kBLENDINDICES && f.usageIndex == 0) ||
                     (f.usage == kTEXCOORD && f.usageIndex == 0);
        if (!known) knownSubset = false;
    }
    if (!isQuadShape && !knownSubset) {
        cs.failReason = "real VS input signature (" + signatureToString(cs.vsInputs) + ") is neither the known "
                         "subset nor the known {POSITION0,TEXCOORD0} light-quad signature - not forced to fit";
        return cs;
    }

    cs.ok = true;
    return cs;
}

void buildTextureSearchCache(const vpp::Container& container, TextureSearchCache& cache) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& name = container.entries()[i].name;
        if (endsWithNoCase(name, ".cpeg_pc") || endsWithNoCase(name, ".cvbm_pc")) {
            std::vector<uint8_t> cbytes;
            if (entryBytes(container, i, cbytes) && !cbytes.empty()) {
                try {
                    auto cbytesPtr = std::make_shared<std::vector<uint8_t>>(std::move(cbytes));
                    sr3texture::TexturePair pair = sr3texture::TexturePair::parse(
                        sr3texture::ByteView(cbytesPtr->data(), cbytesPtr->size()));
                    std::string gname = name;
                    size_t dot = gname.find_last_of('.');
                    if (dot != std::string::npos) gname[dot + 1] = 'g';
                    std::vector<uint8_t> gbytes;
                    for (size_t j = 0; j < container.entries().size(); ++j) {
                        if (container.entries()[j].name == gname) {
                            entryBytes(container, j, gbytes);
                            break;
                        }
                    }
                    if (!gbytes.empty()) {
                        auto gbytesPtr = std::make_shared<std::vector<uint8_t>>(std::move(gbytes));
                        for (size_t r = 0; r < pair.records().size(); ++r) {
                            TextureSearchCache::Located loc;
                            loc.cpegBytes = cbytesPtr;
                            loc.gpegBytes = gbytesPtr;
                            loc.pair = pair;
                            loc.recordIndex = r;
                            loc.recordNameLower = lower(pair.records()[r].name);
                            loc.foundIn = name;
                            cache.all.push_back(std::move(loc));
                        }
                    }
                } catch (const std::exception&) {
                }
            }
        }
        if (container.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = container.openNested(i);
                buildTextureSearchCache(nested, cache);
            } catch (const std::exception&) {
            }
        }
    }
}
bool findAndUploadTexture(const vpp::Container& container, const std::string& tgaName,
                          ID3D11Device* device, sr3render::UploadedTexture& out,
                          std::string& foundIn, TextureSearchCache* cache) {
    if (cache != nullptr) {
        if (!cache->built) {
            buildTextureSearchCache(container, *cache);
            cache->built = true;
        }
        const std::string wantedLower = lower(tgaName);
        auto memoIt = cache->uploadedByWantedName.find(wantedLower);
        if (memoIt != cache->uploadedByWantedName.end()) {
            out = memoIt->second;
            if (out.srv == nullptr) return false;
            for (const auto& loc : cache->all) {
                if (endsWithNoCase(loc.recordNameLower, wantedLower)) { foundIn = loc.foundIn; break; }
            }
            return true;
        }
        for (const auto& loc : cache->all) {
            if (!endsWithNoCase(loc.recordNameLower, wantedLower)) continue;
            std::string error;
            if (sr3render::uploadTexture(device, loc.pair, loc.recordIndex,
                                         sr3texture::ByteView(loc.gpegBytes->data(), loc.gpegBytes->size()), out,
                                         error)) {
                foundIn = loc.foundIn;
                cache->uploadedByWantedName[wantedLower] = out;
                return true;
            }
        }
        sr3render::UploadedTexture notFound;
        cache->uploadedByWantedName[wantedLower] = notFound;
        return false;
    }
    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& name = container.entries()[i].name;
        if (endsWithNoCase(name, ".cpeg_pc") || endsWithNoCase(name, ".cvbm_pc")) {
            std::vector<uint8_t> cbytes;
            if (!entryBytes(container, i, cbytes) || cbytes.empty()) continue;
            try {
                sr3texture::TexturePair pair = sr3texture::TexturePair::parse(
                    sr3texture::ByteView(cbytes.data(), cbytes.size()));
                for (size_t r = 0; r < pair.records().size(); ++r) {
                    if (!endsWithNoCase(pair.records()[r].name, tgaName)) continue;
                    std::string gname = name;
                    size_t dot = gname.find_last_of('.');
                    if (dot != std::string::npos) gname[dot + 1] = 'g';
                    std::vector<uint8_t> gbytes;
                    for (size_t j = 0; j < container.entries().size(); ++j) {
                        if (container.entries()[j].name == gname) {
                            entryBytes(container, j, gbytes);
                            break;
                        }
                    }
                    if (gbytes.empty()) continue;
                    std::string error;
                    if (sr3render::uploadTexture(device, pair, r,
                                                 sr3texture::ByteView(gbytes.data(), gbytes.size()),
                                                 out, error)) {
                        foundIn = name;
                        return true;
                    }
                }
            } catch (const std::exception&) {
            }
        }
        if (container.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = container.openNested(i);
                if (findAndUploadTexture(nested, tgaName, device, out, foundIn)) return true;
            } catch (const std::exception&) {
            }
        }
    }
    return false;
}

} // namespace tower_proto
