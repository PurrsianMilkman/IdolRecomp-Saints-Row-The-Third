#include "sr3render/mesh_renderer.h"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstring>

namespace sr3render {

namespace {

std::string hrToString(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

template <typename T>
void safeRelease(T*& p) {
    if (p != nullptr) { p->Release(); p = nullptr; }
}

struct GpuVertex {
    float position[3];
    float uv[2];
    float normal[3];
};

struct Constants {
    float viewProjection[16];
    int mode;
    float pad[3];
    float tint[4]; // per-draw colour, used by mode 4 (material id)
};

const char kShaderSource[] = R"HLSL(
cbuffer Constants : register(b0) {
    row_major float4x4 viewProjection;
    int  mode;
    float3 pad;
    float4 tint;
};

struct VSIn {
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
    float3 normal   : NORMAL;
};

struct VSOut {
    float4 pos    : SV_Position;
    float2 uv     : TEXCOORD0;
    float3 normal : NORMAL;
};

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos    = mul(float4(i.position, 1.0), viewProjection);
    o.uv     = i.uv;
    o.normal = i.normal;
    return o;
}

Texture2D    tex : register(t0);
SamplerState smp : register(s0);

float4 ps_main(VSOut i) : SV_Target {
    // A fixed headlight term, only so silhouettes and surface curvature
    // are readable. Not a lighting model and not pretending to be one.
    float lambert = saturate(dot(normalize(i.normal), normalize(float3(0.3, 0.5, -0.8))));
    float shade = 0.35 + 0.65 * lambert;

    if (mode == 0) {
        // Raw UV. A wrong 16-bit interpretation shows up here as noise
        // rather than as smooth gradients following the surface.
        return float4(frac(i.uv.x), frac(i.uv.y), 0.0, 1.0);
    }
    if (mode == 1) {
        // UV-space checker: makes scale and wrapping errors obvious in a
        // way a smooth gradient does not.
        float2 c = floor(i.uv * 16.0);
        float checker = frac((c.x + c.y) * 0.5) * 2.0;
        return float4(float3(0.15, 0.15, 0.18) + checker * float3(0.8, 0.8, 0.85) * shade, 1.0);
    }
    if (mode == 3) {
        return float4(i.normal * 0.5 + 0.5, 1.0);
    }
    if (mode == 4) {
        // Flat colour per draw range's material id. Deliberately NOT a
        // texture: the materialId -> texture mapping is OPEN (HANDOFF
        // Sec9.18), and binding a guessed texture would render something
        // plausible and wrong. A colour asserts only what is CONFIRMED -
        // that the index buffer is partitioned into ranges carrying
        // material ids - and makes the partition visible.
        return float4(tint.rgb * shade, 1.0);
    }
    if (mode == 5) {
        // FlatTint: ONE caller-supplied colour for the whole draw, ignoring
        // material id - see MeshDrawMode::FlatTint's own comment (added for
        // the zone-composite investigation command's channel colouring).
        return float4(tint.rgb * shade, 1.0);
    }
    return float4(tex.Sample(smp, i.uv).rgb * shade, 1.0);
}
)HLSL";

bool compileShader(const char* entry, const char* target, ID3DBlob** blob, std::string& error) {
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "sr3render_mesh", nullptr,
                            nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob,
                            &errors);
    if (FAILED(hr)) {
        error = std::string("D3DCompile(") + entry + ") failed: " + hrToString(hr);
        if (errors != nullptr) {
            error += " - ";
            error.append(static_cast<const char*>(errors->GetBufferPointer()),
                         errors->GetBufferSize());
            errors->Release();
        }
        return false;
    }
    if (errors != nullptr) errors->Release();
    return true;
}

} // namespace

MeshRenderer::~MeshRenderer() { release(); }

void MeshRenderer::release() {
    safeRelease(skeletonBuffer_);
    safeRelease(depthView_);
    safeRelease(rasterState_);
    safeRelease(depthState_);
    safeRelease(sampler_);
    safeRelease(constantBuffer_);
    safeRelease(indexBuffer_);
    safeRelease(vertexBuffer_);
    safeRelease(inputLayout_);
    safeRelease(ps_);
    safeRelease(vs_);
}

bool MeshRenderer::initialise(ID3D11Device* device, std::string& error) {
    if (device == nullptr) { error = "null device"; return false; }

    ID3DBlob* vsBlob = nullptr;
    if (!compileShader("vs_main", "vs_5_0", &vsBlob, error)) return false;
    HRESULT hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                            nullptr, &vs_);
    if (FAILED(hr)) {
        vsBlob->Release();
        error = "CreateVertexShader failed: " + hrToString(hr);
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                   &inputLayout_);
    vsBlob->Release();
    if (FAILED(hr)) {
        error = "CreateInputLayout failed: " + hrToString(hr);
        release();
        return false;
    }

    ID3DBlob* psBlob = nullptr;
    if (!compileShader("ps_main", "ps_5_0", &psBlob, error)) { release(); return false; }
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                                   &ps_);
    psBlob->Release();
    if (FAILED(hr)) {
        error = "CreatePixelShader failed: " + hrToString(hr);
        release();
        return false;
    }

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(Constants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cbDesc, nullptr, &constantBuffer_);
    if (FAILED(hr)) {
        error = "CreateBuffer (constants) failed: " + hrToString(hr);
        release();
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP; // wrap, so UVs outside [0,1] tile visibly
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sd, &sampler_);
    if (FAILED(hr)) {
        error = "CreateSamplerState failed: " + hrToString(hr);
        release();
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    hr = device->CreateDepthStencilState(&dsDesc, &depthState_);
    if (FAILED(hr)) {
        error = "CreateDepthStencilState failed: " + hrToString(hr);
        release();
        return false;
    }

    // Culling disabled on purpose for a first render: winding order is not
    // established anywhere in the spec, and culling the wrong way makes a
    // correct mesh look hollow or missing. Better to see all the geometry
    // and judge winding separately than to debug a self-inflicted hole.
    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&rsDesc, &rasterState_);
    if (FAILED(hr)) {
        error = "CreateRasterizerState failed: " + hrToString(hr);
        release();
        return false;
    }

    return true;
}

bool MeshRenderer::createDepth(ID3D11Device* device, uint32_t width, uint32_t height,
                               std::string& error) {
    safeRelease(depthView_);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depthTexture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &depthTexture);
    if (FAILED(hr)) {
        error = "CreateTexture2D (depth) failed: " + hrToString(hr);
        return false;
    }
    hr = device->CreateDepthStencilView(depthTexture, nullptr, &depthView_);
    depthTexture->Release();
    if (FAILED(hr)) {
        error = "CreateDepthStencilView failed: " + hrToString(hr);
        return false;
    }
    return true;
}

void MeshRenderer::clearDepth(ID3D11DeviceContext* context) {
    if (context != nullptr && depthView_ != nullptr) {
        context->ClearDepthStencilView(depthView_, D3D11_CLEAR_DEPTH, 1.0f, 0);
    }
}

bool MeshRenderer::upload(ID3D11Device* device, const sr3mesh::MeshBlock& mesh,
                          size_t channelIndex, size_t texcoordSet, std::string& error,
                          size_t drawGroup, const std::vector<std::array<float, 3>>* overridePositions,
                          const VertexDuplication* duplication,
                          const std::vector<bool>* rangeMask) {
    safeRelease(vertexBuffer_);
    safeRelease(indexBuffer_);

    std::vector<sr3mesh::Vertex> vertices;
    try {
        vertices = mesh.decodeChannel(channelIndex);
    } catch (const std::exception& ex) {
        error = std::string("decodeChannel failed: ") + ex.what();
        return false;
    }
    if (vertices.empty()) {
        error = "channel has no vertices";
        return false;
    }
    const size_t extraCount = duplication != nullptr ? duplication->sourceIndex.size() : 0;
    const size_t totalVertexCount = vertices.size() + extraCount;
    if (duplication != nullptr) {
        for (uint32_t src : duplication->sourceIndex) {
            if (src >= vertices.size()) {
                error = "VertexDuplication::sourceIndex names original vertex " + std::to_string(src) +
                        ", out of range for a " + std::to_string(vertices.size()) + "-vertex decode";
                return false;
            }
        }
    }
    if (overridePositions != nullptr && overridePositions->size() != totalVertexCount) {
        error = "overridePositions size (" + std::to_string(overridePositions->size()) +
                ") does not match the expected vertex count (" + std::to_string(totalVertexCount) +
                (duplication != nullptr
                     ? " = decodeChannel()'s " + std::to_string(vertices.size()) +
                           " + duplication's " + std::to_string(extraCount)
                     : "") +
                ")";
        return false;
    }

    std::vector<GpuVertex> gpu;
    gpu.reserve(totalVertexCount);
    boundsMin_[0] = boundsMin_[1] = boundsMin_[2] = 1e30f;
    boundsMax_[0] = boundsMax_[1] = boundsMax_[2] = -1e30f;

    for (size_t vi = 0; vi < totalVertexCount; ++vi) {
        // For an extra (duplicate) slot, every attribute but position comes
        // from the ORIGINAL vertex it duplicates - it is the same vertex,
        // just reachable from a second, independently-skinned index.
        const size_t sourceVi = vi < vertices.size() ? vi : duplication->sourceIndex[vi - vertices.size()];
        const sr3mesh::Vertex& v = vertices[sourceVi];
        const std::array<float, 3>& pos =
            overridePositions != nullptr ? (*overridePositions)[vi] : v.position;
        GpuVertex g{};
        for (int c = 0; c < 3; ++c) {
            g.position[c] = pos[static_cast<size_t>(c)];
            if (g.position[c] < boundsMin_[c]) boundsMin_[c] = g.position[c];
            if (g.position[c] > boundsMax_[c]) boundsMax_[c] = g.position[c];
            g.normal[c] = v.normal[static_cast<size_t>(c)];
        }
        if (texcoordSet < v.texcoords.size()) {
            g.uv[0] = v.texcoords[texcoordSet][0];
            g.uv[1] = v.texcoords[texcoordSet][1];
        }
        gpu.push_back(g);
    }

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = static_cast<UINT>(gpu.size() * sizeof(GpuVertex));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData{};
    vbData.pSysMem = gpu.data();
    HRESULT hr = device->CreateBuffer(&vbDesc, &vbData, &vertexBuffer_);
    if (FAILED(hr)) {
        error = "CreateBuffer (vertices) failed: " + hrToString(hr);
        return false;
    }

    // Expand the STRIP into a list, dropping stitch degenerates (spec
    // Sec8.1). 32-bit indices because the expansion can exceed 65535
    // entries even though the source indices are 16-bit.
    // Draw ranges first: group 0 (highest LOD), each range restarted.
    // Falls back to the whole-buffer strip only if the groups could not be
    // located, and says so rather than silently producing bridges.
    // Build the group-0 list range by range, recording where each range
    // lands so draw() can issue one DrawIndexed per range - spec Sec8.2's
    // "issue one indexed draw per range in it". Concatenating first and
    // slicing later would work too, but recording the spans here keeps the
    // range identity that the material id belongs to.
    std::vector<uint32_t> indices;
    subDraws_.clear();
    if (mesh.drawGroupsLocated() && drawGroup < mesh.drawGroups().size()) {
        const auto& group = mesh.drawGroups()[drawGroup];
        if (rangeMask != nullptr && rangeMask->size() != group.size()) {
            error = "rangeMask size (" + std::to_string(rangeMask->size()) +
                    ") does not match draw group " + std::to_string(drawGroup) +
                    "'s range count (" + std::to_string(group.size()) + ")";
            return false;
        }
        for (size_t r = 0; r < group.size(); ++r) {
            if (rangeMask != nullptr && !(*rangeMask)[r]) continue;
            std::vector<uint32_t> part = mesh.triangleListForRange(group[r]);
            if (part.empty()) continue;
            // HANDOFF Sec9.68: redirect specific vertex REFERENCES within
            // THIS range only - a duplicate exists exactly because a
            // different range wants the un-redirected original.
            if (duplication != nullptr && r < duplication->redirectPerRange.size() &&
                !duplication->redirectPerRange[r].empty()) {
                const auto& redirect = duplication->redirectPerRange[r];
                for (uint32_t& idx : part) {
                    auto it = redirect.find(idx);
                    if (it != redirect.end()) idx = it->second;
                }
            }
            SubDraw sd;
            sd.startIndex = static_cast<uint32_t>(indices.size());
            sd.indexCount = static_cast<uint32_t>(part.size());
            sd.materialId = group[r].materialId;
            subDraws_.push_back(sd);
            indices.insert(indices.end(), part.begin(), part.end());
        }
    } else {
        indices = mesh.triangleListIndices();
    }
    if (indices.empty()) {
        error = "mesh produced no non-degenerate triangles";
        return false;
    }
    indexCount_ = static_cast<uint32_t>(indices.size());
    trianglesEmitted_ = indexCount_ / 3;

    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData{};
    ibData.pSysMem = indices.data();
    hr = device->CreateBuffer(&ibDesc, &ibData, &indexBuffer_);
    if (FAILED(hr)) {
        error = "CreateBuffer (indices) failed: " + hrToString(hr);
        return false;
    }
    return true;
}

void MeshRenderer::draw(ID3D11DeviceContext* context, const float viewProjection[16],
                        MeshDrawMode mode, ID3D11ShaderResourceView* texture,
                        const float world[16], const float tint[4]) {
    if (context == nullptr || vertexBuffer_ == nullptr || indexBuffer_ == nullptr) return;

    // world == nullptr (every pre-existing call site) takes wvp == the
    // caller's own viewProjection pointer, unmodified - byte-for-byte the
    // old behaviour, not merely numerically close to it.
    float combinedWvp[16];
    const float* wvp = viewProjection;
    if (world != nullptr) {
        multiplyMatrix4x4(world, viewProjection, combinedWvp);
        wvp = combinedWvp;
    }

    auto writeConstants = [&](const float tint[4]) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(context->Map(constantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        Constants constants{};
        std::memcpy(constants.viewProjection, wvp, sizeof(constants.viewProjection));
        constants.mode = static_cast<int>(mode);
        if (tint != nullptr) std::memcpy(constants.tint, tint, sizeof(constants.tint));
        std::memcpy(m.pData, &constants, sizeof(constants));
        context->Unmap(constantBuffer_, 0);
    };
    // FlatTint (mode 5) is the ONE call site that needs the caller's tint
    // uploaded up front - every other mode passes nullptr here exactly as
    // before (tint defaults to nullptr at every pre-existing call site, and
    // mode is never FlatTint there), so this is provably a no-op for them.
    writeConstants(mode == MeshDrawMode::FlatTint ? tint : nullptr);

    UINT stride = sizeof(GpuVertex);
    UINT offset = 0;
    context->IASetInputLayout(inputLayout_);
    context->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
    context->IASetIndexBuffer(indexBuffer_, DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &constantBuffer_);
    context->PSSetShader(ps_, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constantBuffer_);
    context->PSSetShaderResources(0, 1, &texture);
    context->PSSetSamplers(0, 1, &sampler_);
    context->OMSetDepthStencilState(depthState_, 0);
    context->RSSetState(rasterState_);

    // One indexed draw per range when the caller asked to see the material
    // partition. Every other mode draws the group in one call - separate
    // draws would be identical pixel for pixel, since the ranges are
    // already restarted during expansion (Sec9.10) and nothing per-range
    // is bound. Issuing them anyway would look like progress and be none.
    if (mode == MeshDrawMode::MaterialIdAsColour && !subDraws_.empty()) {
        for (const SubDraw& sd : subDraws_) {
            float tint[4];
            materialIdColour(sd.materialId, tint);
            writeConstants(tint);
            context->DrawIndexed(sd.indexCount, sd.startIndex, 0);
        }
        return;
    }
    context->DrawIndexed(indexCount_, 0, 0);
}

void MeshRenderer::drawTextured(ID3D11DeviceContext* context, const float viewProjection[16],
                                const std::vector<ID3D11ShaderResourceView*>& perMaterial,
                                const float world[16]) {
    if (context == nullptr || vertexBuffer_ == nullptr || indexBuffer_ == nullptr) return;
    if (subDraws_.empty()) return;

    float combinedWvp[16];
    const float* wvp = viewProjection;
    if (world != nullptr) {
        multiplyMatrix4x4(world, viewProjection, combinedWvp);
        wvp = combinedWvp;
    }

    UINT stride = sizeof(GpuVertex);
    UINT offset = 0;
    context->IASetInputLayout(inputLayout_);
    context->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
    context->IASetIndexBuffer(indexBuffer_, DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &constantBuffer_);
    context->PSSetShader(ps_, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constantBuffer_);
    context->PSSetSamplers(0, 1, &sampler_);
    context->OMSetDepthStencilState(depthState_, 0);
    context->RSSetState(rasterState_);

    for (const SubDraw& sd : subDraws_) {
        ID3D11ShaderResourceView* srv =
            sd.materialId < perMaterial.size() ? perMaterial[sd.materialId] : nullptr;

        // A material whose texture did not resolve falls back to its FLAT
        // COLOUR, never to another material's texture. The wrong texture
        // would look like a finished render; a coloured patch says plainly
        // that something is missing.
        Constants constants{};
        std::memcpy(constants.viewProjection, wvp, sizeof(constants.viewProjection));
        constants.mode = srv != nullptr ? static_cast<int>(MeshDrawMode::Textured)
                                        : static_cast<int>(MeshDrawMode::MaterialIdAsColour);
        if (srv == nullptr) materialIdColour(sd.materialId, constants.tint);

        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(context->Map(constantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            std::memcpy(m.pData, &constants, sizeof(constants));
            context->Unmap(constantBuffer_, 0);
        }
        context->PSSetShaderResources(0, 1, &srv);
        context->DrawIndexed(sd.indexCount, sd.startIndex, 0);
    }
}

// A stable, well-separated colour per material id. Golden-ratio hue
// stepping so adjacent ids never land on near-identical colours - the
// whole point is telling neighbouring ranges apart.
void MeshRenderer::materialIdColour(uint32_t materialId, float out[4]) {
    const float hue = std::fmod(static_cast<float>(materialId) * 0.6180339887f, 1.0f);
    const float h = hue * 6.0f;
    const int sector = static_cast<int>(h) % 6;
    const float f = h - std::floor(h);
    const float v = 0.95f, p = 0.25f, q = 0.95f * (1.0f - 0.72f * f),
                t = 0.25f + 0.70f * f;
    switch (sector) {
        case 0: out[0] = v; out[1] = t; out[2] = p; break;
        case 1: out[0] = q; out[1] = v; out[2] = p; break;
        case 2: out[0] = p; out[1] = v; out[2] = t; break;
        case 3: out[0] = p; out[1] = q; out[2] = v; break;
        case 4: out[0] = t; out[1] = p; out[2] = v; break;
        default: out[0] = v; out[1] = p; out[2] = q; break;
    }
    out[3] = 1.0f;
}

bool MeshRenderer::uploadSkeleton(ID3D11Device* device,
                                  const std::vector<std::array<float, 3>>& positions,
                                  const std::vector<uint32_t>& parents, std::string& error) {
    safeRelease(skeletonBuffer_);
    skeletonVertexCount_ = 0;
    if (positions.size() != parents.size() || positions.empty()) {
        error = "skeleton positions/parents mismatch or empty";
        return false;
    }

    std::vector<GpuVertex> lines;
    for (size_t i = 0; i < positions.size(); ++i) {
        uint32_t parent = parents[i];
        if (parent == 0xFFFFFFFFu || parent >= positions.size()) continue; // root: nothing to draw to
        GpuVertex a{}, b{};
        for (int c = 0; c < 3; ++c) {
            a.position[c] = positions[parent][static_cast<size_t>(c)];
            b.position[c] = positions[i][static_cast<size_t>(c)];
            a.normal[c] = 1.0f;
            b.normal[c] = 1.0f;
        }
        lines.push_back(a);
        lines.push_back(b);
    }
    if (lines.empty()) {
        error = "skeleton has no parented bones to draw";
        return false;
    }
    skeletonVertexCount_ = static_cast<uint32_t>(lines.size());

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(lines.size() * sizeof(GpuVertex));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = lines.data();
    HRESULT hr = device->CreateBuffer(&desc, &data, &skeletonBuffer_);
    if (FAILED(hr)) {
        error = "CreateBuffer (skeleton) failed: " + hrToString(hr);
        return false;
    }
    return true;
}

void MeshRenderer::drawSkeleton(ID3D11DeviceContext* context, const float viewProjection[16],
                                const float world[16]) {
    if (context == nullptr || skeletonBuffer_ == nullptr || skeletonVertexCount_ == 0) return;

    float combinedWvp[16];
    const float* wvp = viewProjection;
    if (world != nullptr) {
        multiplyMatrix4x4(world, viewProjection, combinedWvp);
        wvp = combinedWvp;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(constantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        Constants constants{};
        std::memcpy(constants.viewProjection, wvp, sizeof(constants.viewProjection));
        constants.mode = 3; // normal-as-colour: normals are set to 1, giving solid white lines
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(constantBuffer_, 0);
    }

    UINT stride = sizeof(GpuVertex);
    UINT offset = 0;
    context->IASetInputLayout(inputLayout_);
    context->IASetVertexBuffers(0, 1, &skeletonBuffer_, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->VSSetShader(vs_, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &constantBuffer_);
    context->PSSetShader(ps_, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constantBuffer_);
    context->RSSetState(rasterState_);
    context->Draw(skeletonVertexCount_, 0);
}

void buildOrbitViewProjection(const float boundsMin[3], const float boundsMax[3],
                              float yawRadians, float pitchRadians, float distanceScale,
                              float aspect, float outMatrix[16]) {
    float centre[3];
    float extent = 0.0f;
    for (int i = 0; i < 3; ++i) {
        centre[i] = (boundsMin[i] + boundsMax[i]) * 0.5f;
        float span = boundsMax[i] - boundsMin[i];
        if (span > extent) extent = span;
    }
    if (extent <= 0.0f) extent = 1.0f;
    const float distance = extent * distanceScale;

    // Orbit around the bounding box centre.
    float eye[3];
    eye[0] = centre[0] + distance * std::cos(pitchRadians) * std::sin(yawRadians);
    eye[1] = centre[1] + distance * std::sin(pitchRadians);
    eye[2] = centre[2] + distance * std::cos(pitchRadians) * std::cos(yawRadians);

    // Right-handed look-at.
    float forward[3] = {centre[0] - eye[0], centre[1] - eye[1], centre[2] - eye[2]};
    float flen = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] +
                           forward[2] * forward[2]);
    if (flen <= 0.0f) flen = 1.0f;
    for (int i = 0; i < 3; ++i) forward[i] /= flen;

    const float worldUp[3] = {0.0f, 1.0f, 0.0f};
    float right[3] = {forward[1] * worldUp[2] - forward[2] * worldUp[1],
                      forward[2] * worldUp[0] - forward[0] * worldUp[2],
                      forward[0] * worldUp[1] - forward[1] * worldUp[0]};
    float rlen = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (rlen <= 0.0f) rlen = 1.0f;
    for (int i = 0; i < 3; ++i) right[i] /= rlen;

    float up[3] = {right[1] * forward[2] - right[2] * forward[1],
                   right[2] * forward[0] - right[0] * forward[2],
                   right[0] * forward[1] - right[1] * forward[0]};

    // View matrix (row-major, row-vector convention to match the shader's
    // mul(v, M) and row_major declaration).
    float view[16] = {
        right[0], up[0], forward[0], 0.0f,
        right[1], up[1], forward[1], 0.0f,
        right[2], up[2], forward[2], 0.0f,
        -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]),
        -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]),
        -(forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2]),
        1.0f,
    };

    const float fovY = 0.9f;
    const float nearZ = extent * 0.01f;
    const float farZ = extent * 20.0f;
    const float h = 1.0f / std::tan(fovY * 0.5f);
    const float w = h / aspect;
    const float q = farZ / (farZ - nearZ);
    float proj[16] = {
        w, 0, 0, 0,
        0, h, 0, 0,
        0, 0, q, 1,
        0, 0, -nearZ * q, 0,
    };

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += view[r * 4 + k] * proj[k * 4 + c];
            outMatrix[r * 4 + c] = sum;
        }
    }
}

void multiplyMatrix4x4(const float a[16], const float b[16], float outMatrix[16]) {
    float result[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[r * 4 + k] * b[k * 4 + c];
            result[r * 4 + c] = sum;
        }
    }
    // Copied out through a local temporary so outMatrix may alias a or b.
    std::memcpy(outMatrix, result, sizeof(result));
}

void buildWorldMatrix(float x, float y, float z, float yawRadians, float scale,
                      float outMatrix[16]) {
    const float c = std::cos(yawRadians) * scale;
    const float s = std::sin(yawRadians) * scale;
    // Row-major, row-vector convention (matches the view/projection
    // matrices above): rows 0-2 are the scaled, yaw-rotated images of the
    // local X/Y/Z basis vectors in world space; row 3 is the translation.
    // This is the standard row-vector affine layout - translating after
    // the linear part is exactly "put the translation in the last row,
    // zero in the last column of the first three rows" - the same
    // convention buildOrbitViewProjection's view matrix already uses.
    const float m[16] = {
        c,    0.0f,  -s,   0.0f,
        0.0f, scale, 0.0f, 0.0f,
        s,    0.0f,  c,    0.0f,
        x,    y,     z,    1.0f,
    };
    std::memcpy(outMatrix, m, sizeof(m));
}

void buildFreeViewProjection(const float eye[3], float yawRadians, float pitchRadians,
                             float aspect, float nearZ, float farZ, float outMatrix[16]) {
    // Same spherical parameterisation buildOrbitViewProjection uses for its
    // eye-from-centre offset, pointed the other way: yaw 0 / pitch 0 looks
    // straight down +Z.
    float forward[3] = {
        std::cos(pitchRadians) * std::sin(yawRadians),
        std::sin(pitchRadians),
        std::cos(pitchRadians) * std::cos(yawRadians),
    };
    float flen = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] +
                           forward[2] * forward[2]);
    if (flen <= 0.0f) flen = 1.0f;
    for (int i = 0; i < 3; ++i) forward[i] /= flen;

    const float worldUp[3] = {0.0f, 1.0f, 0.0f};
    float right[3] = {forward[1] * worldUp[2] - forward[2] * worldUp[1],
                      forward[2] * worldUp[0] - forward[0] * worldUp[2],
                      forward[0] * worldUp[1] - forward[1] * worldUp[0]};
    float rlen = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (rlen <= 0.0f) rlen = 1.0f;
    for (int i = 0; i < 3; ++i) right[i] /= rlen;

    float up[3] = {right[1] * forward[2] - right[2] * forward[1],
                   right[2] * forward[0] - right[0] * forward[2],
                   right[0] * forward[1] - right[1] * forward[0]};

    // Right-handed look-at, identical construction to
    // buildOrbitViewProjection's view matrix, just with an explicit eye
    // instead of one derived from orbiting a bounding box.
    float view[16] = {
        right[0], up[0], forward[0], 0.0f,
        right[1], up[1], forward[1], 0.0f,
        right[2], up[2], forward[2], 0.0f,
        -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]),
        -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]),
        -(forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2]),
        1.0f,
    };

    const float fovY = 0.9f;
    const float h = 1.0f / std::tan(fovY * 0.5f);
    const float w = h / aspect;
    const float q = farZ / (farZ - nearZ);
    float proj[16] = {
        w, 0, 0, 0,
        0, h, 0, 0,
        0, 0, q, 1,
        0, 0, -nearZ * q, 0,
    };

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += view[r * 4 + k] * proj[k * 4 + c];
            outMatrix[r * 4 + c] = sum;
        }
    }
}

} // namespace sr3render
