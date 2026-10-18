#pragma once

// Shared state for tools/prototype_lit_clmesh_tower2.cpp. main() used to be one ~1,700-line function;
// its locals that are shared between the sections now live in Tower2Session (members in the order the
// old locals were declared, same names/types/initialisers). Locals that needed a non-trivial
// constructor are std::optional<T> named `<name>_`; the one lambda shared between sections
// (rdocEndCapture) is a std::function member. Each section function binds `auto& name = session.name;`
// references at its top so the moved code reads exactly as it did inside main().

#include <functional>
#include <optional>

#include "prototype_lit_clmesh_tower2_helpers.h"

namespace tower2_proto {

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
    struct MaterialCbuffers {
        std::vector<uint8_t> gbVsCpu, gbPsCpu, litVsCpu, litPsCpu;
        ID3D11Buffer *cbGbVs = nullptr, *cbGbPs = nullptr, *cbLitVs = nullptr, *cbLitPs = nullptr;
    };

struct Tower2Session {
    const std::string kCacheDir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
    std::vector<ConstantFillLog> fillLog;
    const uint32_t kW = 800, kH = 600; // CHOSEN, same as every tool in this family
    const DXGI_FORMAT kGBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // CHOSEN, not confirmed real
    std::string outPath = "D:/Project Crreish/TEAM B/tools/prototype_lit_clmesh_tower2_output.png";
    const char* rdocCaptureRaw = std::getenv("SR3_RENDERDOC_CAPTURE");
    const bool kRenderDocCapture = (rdocCaptureRaw != nullptr) && (std::string(rdocCaptureRaw) == "1");
    const char* rdocAbsurdRaw = std::getenv("SR3_RENDERDOC_ABSURD_VALUES");
    const bool kRenderDocAbsurdValues = (rdocAbsurdRaw != nullptr) && (std::string(rdocAbsurdRaw) == "1");
    const char* testEnvRaw = std::getenv("SR3_TEST_OBJ_INSTANCE_PARAMS");
    const bool kTestObjInstanceParams = (testEnvRaw != nullptr) && (std::string(testEnvRaw) == "1");
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
    RenderDevice dev;
    std::vector<RangeResolution> resolutions;
    std::map<std::string, CompiledShader> gbufferShaders; // role 4
    std::map<std::string, CompiledShader> litShaders;     // role 6
    std::set<uint32_t> drawableMaterialIds;
    std::map<uint32_t, sr3render::UploadedTexture> materialNormalTex, materialDiffuseTex, materialSpecularTex;
    std::map<int, ChannelData> channels;
    float viewOnly[16];
    float viewProj[16];
    float invProj[16];
    float worldIdentity[16];
    std::map<uint32_t, std::string> materialIdToVsFileName;
    std::map<uint32_t, MaterialCbuffers> materialCbuffers;
    const char* dumpCbufRawRaw = std::getenv("SR3_DUMP_CBUFFER_RAW");
    const bool kDumpCbufferRaw = (dumpCbufRawRaw != nullptr) && (std::string(dumpCbufRawRaw) == "1");
    OffscreenTarget normalsRT, dsfDataRT, lightingParamRT, lightBufferRT;
    OffscreenDepth gbufferDepth;
    ID3D11DepthStencilState* depthState = nullptr;
    ID3D11DepthStencilState* noDepthState = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11ShaderResourceView* whitePlaceholderSrv = nullptr;
    ID3D11ShaderResourceView* blackPlaceholderSrv = nullptr;
    std::function<void()> rdocEndCapture;
};

bool tower2SetupDiagnostics(Tower2Session& session);
bool tower2SetupArchives(Tower2Session& session);
bool tower2SetupCensus(Tower2Session& session);
bool tower2SetupCensus2(Tower2Session& session);
bool tower2SetupEnvironment(Tower2Session& session);
bool tower2SetupClmesh(Tower2Session& session);
bool tower2ResolveRenderGroup(Tower2Session& session);
bool tower2ResolveShaders(Tower2Session& session);
bool tower2FinalizeShaders(Tower2Session& session);
bool tower2UploadTextures(Tower2Session& session);
bool tower2DecodeChannels(Tower2Session& session);
bool tower2SetupCamera(Tower2Session& session);
bool tower2FillTemplateBuffers(Tower2Session& session);
bool tower2FillMaterialBuffers(Tower2Session& session);
bool tower2DumpMaterials(Tower2Session& session);
bool tower2CreateChannelBuffers(Tower2Session& session);
bool tower2CreateTargets(Tower2Session& session);
bool tower2PassGBuffer(Tower2Session& session);
bool tower2PassLight(Tower2Session& session);
bool tower2PassMaterial(Tower2Session& session);
bool tower2WriteOutput(Tower2Session& session);

} // namespace tower2_proto
