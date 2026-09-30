// One-shot empirical probe (not part of the build): confirms the exact
// SM4/5 (vs_4_0/ps_4_0) syntax facts the SM4/5-retargeting follow-up needs,
// the same discipline d3dctest_semantics.cpp used for the SM3 path -
// nothing here is trusted from memorized D3D9-vs-D3D10/11 knowledge without
// a real D3DCompile() call confirming it.
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

static int g_fails = 0;

static bool tryCompile(const char* label, const char* src, const char* profile, bool expectOk = true) {
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), label, nullptr, nullptr, "main", profile, 0, 0, &code, &err);
    bool ok = SUCCEEDED(hr);
    if (ok != expectOk) {
        printf("[UNEXPECTED] %s (%s): hr=0x%08lX (expected %s)\n%s\n", label, profile, (unsigned long)hr,
               expectOk ? "SUCCESS" : "FAILURE", err ? (const char*)err->GetBufferPointer() : "(no error blob)");
        ++g_fails;
    } else {
        printf("[ %s as expected ] %s (%s)%s\n", ok ? "OK" : "FAIL", label, profile,
               (err && err->GetBufferSize() > 0) ? " -- with warnings/errors text below" : "");
        if (err && err->GetBufferSize() > 0) printf("    %s\n", (const char*)err->GetBufferPointer());
    }
    if (code) code->Release();
    if (err) err->Release();
    return ok == expectOk;
}

int main() {
    printf("=== 1) VS input semantics: ordinary D3DDECLUSAGE names at vs_4_0 (expect ALL ok) ===\n");
    const char* usages[] = {"POSITION0", "BLENDWEIGHT0", "BLENDINDICES0", "NORMAL0", "PSIZE0",
                             "TEXCOORD0", "TANGENT0",     "BINORMAL0",     "COLOR0", "FOG0", "SAMPLE0"};
    for (const char* u : usages) {
        char src[256];
        snprintf(src, sizeof(src),
                 "struct VSI { float4 v0 : %s; };\n"
                 "float4 main(VSI input) : SV_Position { return input.v0; }\n",
                 u);
        tryCompile(u, src, "vs_4_0");
    }

    printf("\n=== 2) VS output: legacy POSITION0 on the rasterizer-fed output MUST FAIL at vs_4_0 ===\n");
    {
        const char* src = "struct VSO { float4 o0 : POSITION0; };\n"
                           "VSO main(float4 pos : POSITION0) { VSO o = (VSO)0; o.o0 = pos; return o; }\n";
        tryCompile("VS output POSITION0 (legacy)", src, "vs_4_0", /*expectOk=*/false);
    }
    printf("\n=== 3) VS output: SV_Position (no index) MUST SUCCEED at vs_4_0 ===\n");
    {
        const char* src = "struct VSO { float4 o0 : SV_Position; };\n"
                           "VSO main(float4 pos : POSITION0) { VSO o = (VSO)0; o.o0 = pos; return o; }\n";
        tryCompile("VS output SV_Position", src, "vs_4_0");
    }
    printf("\n=== 3b) SV_Position WITH a numeric index (SV_Position0) - does the compiler accept or reject the index? ===\n");
    {
        const char* src = "struct VSO { float4 o0 : SV_Position0; };\n"
                           "VSO main(float4 pos : POSITION0) { VSO o = (VSO)0; o.o0 = pos; return o; }\n";
        tryCompile("VS output SV_Position0 (indexed)", src, "vs_4_0", /*expectOk=*/false);
    }

    printf("\n=== 4) VS output: ordinary (non-SV) varying semantics alongside SV_Position (expect ok) ===\n");
    {
        const char* src = "struct VSO { float4 pos : SV_Position; float4 o1 : TEXCOORD0; float4 o2 : COLOR0; };\n"
                           "VSO main(float4 v0 : POSITION0) { VSO o = (VSO)0; o.pos = v0; o.o1 = v0; o.o2 = v0; return o; }\n";
        tryCompile("VS output TEXCOORD0/COLOR0 varyings", src, "vs_4_0");
    }

    printf("\n=== 5) PS output: legacy COLOR0 MUST FAIL at ps_4_0 ===\n");
    {
        const char* src = "struct PSO { float4 oC0 : COLOR0; };\n"
                           "PSO main() { PSO o = (PSO)0; return o; }\n";
        tryCompile("PS output COLOR0 (legacy)", src, "ps_4_0", /*expectOk=*/false);
    }
    printf("\n=== 6) PS output: SV_Target0 MUST SUCCEED at ps_4_0 ===\n");
    {
        const char* src = "struct PSO { float4 oC0 : SV_Target0; };\n"
                           "PSO main() { PSO o = (PSO)0; return o; }\n";
        tryCompile("PS output SV_Target0", src, "ps_4_0");
    }
    printf("\n=== 6b) PS output: bare SV_Target (no index) - does it work? ===\n");
    {
        const char* src = "float4 main() : SV_Target { return float4(0,0,0,0); }\n";
        tryCompile("PS output SV_Target (no index)", src, "ps_4_0");
    }
    printf("\n=== 7) PS output: SV_Target0..3 contiguity requirement - SV_Target0+SV_Target2 with NO SV_Target1 ===\n");
    {
        const char* src = "struct PSO { float4 oC0 : SV_Target0; float4 oC2 : SV_Target2; };\n"
                           "PSO main() { PSO o = (PSO)0; return o; }\n";
        tryCompile("PS output SV_Target0+SV_Target2 gap (expect FAIL like SM3's X4538)", src, "ps_4_0", /*expectOk=*/false);
    }
    printf("\n=== 7b) PS output: SV_Target0 declared but body never writes it - required or not? ===\n");
    {
        const char* src = "struct PSO { float4 oC0 : SV_Target0; };\n"
                           "PSO main() { PSO o = (PSO)0; /* never assign oC0 */ return o; }\n";
        tryCompile("PS output SV_Target0 unwritten (zero-initialized via (PSO)0)", src, "ps_4_0");
    }

    printf("\n=== 8) PS output: SV_Depth (scalar) ===\n");
    {
        const char* src = "struct PSO { float4 oC0 : SV_Target0; float oD : SV_Depth; };\n"
                           "PSO main() { PSO o = (PSO)0; return o; }\n";
        tryCompile("PS output SV_Depth", src, "ps_4_0");
    }

    printf("\n=== 9) PS input: ordinary D3DDECLUSAGE-shaped names (POSITION0/TEXCOORD0/COLOR0/NORMAL0) at ps_4_0 (expect ok) ===\n");
    const char* psInUsages[] = {"POSITION0", "TEXCOORD0", "COLOR0", "NORMAL0"};
    for (const char* u : psInUsages) {
        char src[256];
        snprintf(src, sizeof(src),
                 "struct PSI { float4 v0 : %s; };\n"
                 "float4 main(PSI input) : SV_Target0 { return input.v0; }\n",
                 u);
        tryCompile(u, src, "ps_4_0");
    }

    printf("\n=== 10) cbuffer with float4[]/int4[]/bool[] arrays, register-indexed relative addressing ===\n");
    {
        const char* src = "cbuffer Constants : register(b0) {\n"
                           "  float4 c[64];\n"
                           "  int4 iarr[8];\n"
                           "  bool barr[4];\n"
                           "};\n"
                           "float4 main(float4 pos : POSITION0) : SV_Position {\n"
                           "  int a0 = 3;\n"
                           "  float4 v = c[a0 + 2];\n"
                           "  if (barr[0]) v += iarr[0];\n"
                           "  return v;\n"
                           "}\n";
        tryCompile("cbuffer float4[]/int4[]/bool[] + relative index", src, "vs_4_0");
    }

    printf("\n=== 11) Texture2D<float4> + SamplerState + Sample()/SampleLevel() at ps_4_0 ===\n");
    {
        const char* src = "Texture2D<float4> DiffuseMap : register(t0);\n"
                           "SamplerState DiffuseMapSampler : register(s0);\n"
                           "float4 main(float4 uv : TEXCOORD0) : SV_Target0 {\n"
                           "  return DiffuseMap.Sample(DiffuseMapSampler, uv.xy);\n"
                           "}\n";
        tryCompile("Texture2D.Sample at ps_4_0", src, "ps_4_0");
    }
    {
        const char* src = "Texture2D<float4> DiffuseMap : register(t0);\n"
                           "SamplerState DiffuseMapSampler : register(s0);\n"
                           "float4 main(float4 uv : TEXCOORD0) : SV_Target0 {\n"
                           "  return DiffuseMap.SampleLevel(DiffuseMapSampler, uv.xy, uv.w);\n"
                           "}\n";
        tryCompile("Texture2D.SampleLevel at ps_4_0", src, "ps_4_0");
    }
    printf("\n=== 12) Texture2D.Sample (implicit LOD/derivatives) at vs_4_0 - expect FAIL (no derivatives in VS) ===\n");
    {
        const char* src = "Texture2D<float4> HeightMap : register(t0);\n"
                           "SamplerState HeightMapSampler : register(s0);\n"
                           "float4 main(float4 uv : TEXCOORD0) : SV_Position {\n"
                           "  return HeightMap.Sample(HeightMapSampler, uv.xy);\n"
                           "}\n";
        tryCompile("Texture2D.Sample at vs_4_0 (expect FAIL)", src, "vs_4_0", /*expectOk=*/false);
    }
    printf("\n=== 12b) Texture2D.SampleLevel (explicit LOD) at vs_4_0 - expect OK ===\n");
    {
        const char* src = "Texture2D<float4> HeightMap : register(t0);\n"
                           "SamplerState HeightMapSampler : register(s0);\n"
                           "float4 main(float4 uv : TEXCOORD0) : SV_Position {\n"
                           "  return HeightMap.SampleLevel(HeightMapSampler, uv.xy, uv.w);\n"
                           "}\n";
        tryCompile("Texture2D.SampleLevel at vs_4_0", src, "vs_4_0");
    }

    printf("\n=== 13) TextureCube<float4> + Sample()/SampleLevel() at ps_4_0/vs_4_0 ===\n");
    {
        const char* src = "TextureCube<float4> EnvMap : register(t0);\n"
                           "SamplerState EnvMapSampler : register(s0);\n"
                           "float4 main(float4 dir : TEXCOORD0) : SV_Target0 {\n"
                           "  return EnvMap.Sample(EnvMapSampler, dir.xyz);\n"
                           "}\n";
        tryCompile("TextureCube.Sample at ps_4_0", src, "ps_4_0");
    }
    {
        const char* src = "TextureCube<float4> EnvMap : register(t0);\n"
                           "SamplerState EnvMapSampler : register(s0);\n"
                           "float4 main(float4 dir : TEXCOORD0) : SV_Position {\n"
                           "  return EnvMap.SampleLevel(EnvMapSampler, dir.xyz, dir.w);\n"
                           "}\n";
        tryCompile("TextureCube.SampleLevel at vs_4_0", src, "vs_4_0");
    }

    printf("\n=== 14) sincos()/saturate()/mad()/lerp()/clip()/frac()/rsqrt()/rcp() at vs_4_0/ps_4_0 (should be unchanged from SM3) ===\n");
    {
        const char* src = "float4 main(float4 pos : POSITION0) : SV_Position {\n"
                           "  float s, c; sincos(pos.x, s, c);\n"
                           "  float4 v = mad(pos, pos, pos);\n"
                           "  v = lerp(v, pos, 0.5);\n"
                           "  v = saturate(v);\n"
                           "  return v + float4(s, c, frac(pos.z), rsqrt(abs(pos.w)));\n"
                           "}\n";
        tryCompile("intrinsics", src, "vs_4_0");
    }
    {
        const char* src = "float4 main(float4 pos : TEXCOORD0) : SV_Target0 { clip(pos.xyz); return pos; }\n";
        tryCompile("clip() at ps_4_0", src, "ps_4_0");
    }

    printf("\n=== 15) VS_5_0/PS_5_0 sanity (in case 4_0 is ever insufficient) ===\n");
    {
        const char* src = "cbuffer C : register(b0) { float4 c[16]; };\n"
                           "float4 main(float4 pos : POSITION0) : SV_Position { return c[0] + pos; }\n";
        tryCompile("trivial vs_5_0", src, "vs_5_0");
    }

    printf("\n%d probe(s) unexpected\n", g_fails);
    return g_fails == 0 ? 0 : 1;
}
