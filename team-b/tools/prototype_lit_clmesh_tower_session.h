#pragma once

// Shared state for tools/prototype_lit_clmesh_tower.cpp. main() used to be one ~1,000-line function;
// its locals that are shared between the sections now live in TowerSession (members in the order the
// old locals were declared, same names/types/initialisers). Locals that needed a non-trivial
// constructor are std::optional<T> named `<name>_`; each section function binds `auto& name = s.name;`
// references at its top so the moved code reads exactly as it did inside main().

#include <optional>

#include "prototype_lit_clmesh_tower_helpers.h"

namespace tower_proto {

    struct RangeResolution {
        size_t rangeIdx = 0; uint32_t materialId = 0, submeshIndex = 0, shaderHash = 0;
        bool ok = false; std::string reason, stem, vsFileName;
    };
    struct ChannelData {
        std::vector<sr3mesh::Vertex> verts;
        std::vector<GpuVertexReal> gpuVerts;
        std::vector<uint32_t> gpuIndices;
        std::map<size_t, std::pair<size_t, size_t>> rangeIndexSpan;
        ID3D11Buffer* vb = nullptr; ID3D11Buffer* ib = nullptr;
        sr3mesh::LayoutInfo layoutInfo;
    };

struct TowerSession {
    const std::string kCacheDir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
    std::vector<ConstantFillLog> fillLog;
    const uint32_t kW = 800, kH = 600; // CHOSEN, same as every tool in this family
    const DXGI_FORMAT kGBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // CHOSEN, not confirmed real
    std::string outPath = "D:/Project Crreish/TEAM B/tools/prototype_lit_clmesh_tower_output.png";
    std::vector<uint8_t> shadersArchive;
    std::optional<vpp::Container> shadersContainer_;
    std::map<std::string, std::vector<std::string>> stemToFiles;
    std::map<uint32_t, std::string> stemCrcToStem;
    std::vector<uint8_t> cityArchive;
    std::optional<vpp::Container> cityContainer_;
    std::optional<vpp::Container> h0Container_;
    bool realLightRowFound = false;
    float realTodColor[3] = {0, 0, 0}, realTodIntensity = 0.0f;
    float realAmbientColor[3] = {0, 0, 0}, realAmbientIntensity = 0.0f;
    float realBackAmbientColor[3] = {0, 0, 0}, realBackAmbientIntensity = 0.0f;
    float realExposureMin = 0.0f;
    std::vector<uint8_t> clBytes, glBytes;
    vpp::ByteView clView;
    vpp::ByteView glView;
    sr3clmesh::LevelMesh lm;
    std::vector<sr3clmesh::MaterialRecord> matRecs;
    std::vector<sr3geometry::MaterialBinding> matBindings;
    sr3mesh::MeshBlock chosenMesh;
    std::vector<RangeResolution> resolutions;
    std::map<std::string, std::vector<size_t>> byVsFile;
    RenderDevice dev;
    std::map<std::string, CompiledShader> gbufferShaders; // role 4
    std::map<std::string, CompiledShader> litShaders;     // role 6
    std::set<uint32_t> drawableMaterialIds;
    std::map<uint32_t, sr3render::UploadedTexture> materialNormalTex, materialDiffuseTex, materialSpecularTex;
    std::map<int, ChannelData> channels;
    float viewOnly[16];
    float viewProj[16];
    float invProj[16];
    float worldIdentity[16];
    OffscreenTarget normalsRT, depthParamRT, lightingParamRT, lightBufferRT;
    OffscreenDepth gbufferDepth;
    ID3D11DepthStencilState* depthState = nullptr;
    ID3D11DepthStencilState* noDepthState = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11ShaderResourceView* whitePlaceholderSrv = nullptr;
    ID3D11ShaderResourceView* blackPlaceholderSrv = nullptr;
};

bool towerSetupArchives(TowerSession& s);
bool towerSetupCensus(TowerSession& s);
bool towerSetupEnvironment(TowerSession& s);
bool towerSetupClmesh(TowerSession& s);
bool towerResolveRenderGroup(TowerSession& s);
bool towerCompileShaders(TowerSession& s);
bool towerUploadTextures(TowerSession& s);
bool towerDecodeChannels(TowerSession& s);
bool towerSetupCamera(TowerSession& s);
bool towerFillConstantBuffers(TowerSession& s);
bool towerCreateTargets(TowerSession& s);
bool towerPassGBuffer(TowerSession& s);
bool towerPassLight(TowerSession& s);
bool towerPassMaterial(TowerSession& s);
bool towerWriteOutput(TowerSession& s);

} // namespace tower_proto
