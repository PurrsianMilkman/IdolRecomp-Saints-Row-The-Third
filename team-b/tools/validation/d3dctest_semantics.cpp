// One-shot empirical probe (not part of the build): confirms the exact
// spelling/casing of every D3DDECLUSAGE-derived HLSL semantic name the
// translator emits, compiled with the SAME D3DCompile() entry point and
// SAME vs_3_0/ps_3_0 legacy target the real population gate uses - per the
// task's own instruction to confirm semantic spelling against a real
// D3DCompile rather than trust the public enum names alone.
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

static bool tryCompile(const char* label, const char* src, const char* profile) {
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), label, nullptr, nullptr, "main", profile, 0, 0, &code, &err);
    if (FAILED(hr)) {
        printf("[FAIL] %s (%s): hr=0x%08lX\n%s\n", label, profile, (unsigned long)hr,
               err ? (const char*)err->GetBufferPointer() : "(no error blob)");
        if (err) err->Release();
        return false;
    }
    printf("[ OK ] %s (%s)\n", label, profile);
    if (code) code->Release();
    if (err) err->Release();
    return true;
}

int main() {
    int fails = 0;

    // Every D3DDECLUSAGE base name at index 0, as a VS input.
    const char* usages[] = {"POSITION0", "BLENDWEIGHT0", "BLENDINDICES0", "NORMAL0",   "PSIZE0",
                             "TEXCOORD0", "TANGENT0",     "BINORMAL0",     "TESSFACTOR0", "POSITIONT0",
                             "COLOR0",    "FOG0",         "DEPTH0",        "SAMPLE0"};
    for (const char* u : usages) {
        char src[256];
        snprintf(src, sizeof(src),
                 "struct VSI { float4 v0 : %s; };\n"
                 "struct VSO { float4 o0 : POSITION0; };\n"
                 "VSO main(VSI input) { VSO o = (VSO)0; o.o0 = input.v0; return o; }\n",
                 u);
        if (!tryCompile(u, src, "vs_3_0")) ++fails;
    }

    // TEXCOORD up to index 7 (real DCL usage indices this game's shaders
    // can plausibly use).
    for (int i = 0; i <= 7; ++i) {
        char sem[32];
        snprintf(sem, sizeof(sem), "TEXCOORD%d", i);
        char src[256];
        snprintf(src, sizeof(src),
                 "struct VSI { float4 v0 : %s; };\n"
                 "struct VSO { float4 o0 : POSITION0; };\n"
                 "VSO main(VSI input) { VSO o = (VSO)0; o.o0 = input.v0; return o; }\n",
                 sem);
        if (!tryCompile(sem, src, "vs_3_0")) ++fails;
    }

    // PS color outputs COLOR0..3 and DEPTH0, at ps_3_0.
    for (int i = 0; i <= 3; ++i) {
        char sem[32];
        snprintf(sem, sizeof(sem), "COLOR%d", i);
        char src[256];
        snprintf(src, sizeof(src), "struct PSO { float4 %s : %s; };\nPSO main() { PSO o = (PSO)0; return o; }\n",
                 "oC", sem);
        if (!tryCompile(sem, src, "ps_3_0")) ++fails;
    }
    {
        const char* src = "struct PSO { float4 oC0 : COLOR0; float oD : DEPTH0; };\n"
                           "PSO main() { PSO o = (PSO)0; return o; }\n";
        if (!tryCompile("DEPTH0", src, "ps_3_0")) ++fails;
    }

    // Legacy sampler objects at ps_3_0.
    {
        const char* src = "sampler2D DiffuseMap : register(s0);\n"
                           "float4 main(float4 uv : TEXCOORD0) : COLOR0 { return tex2D(DiffuseMap, uv.xy); }\n";
        if (!tryCompile("sampler2D/tex2D", src, "ps_3_0")) ++fails;
    }
    {
        const char* src = "samplerCUBE EnvMap : register(s0);\n"
                           "float4 main(float4 dir : TEXCOORD0) : COLOR0 { return texCUBE(EnvMap, dir.xyz); }\n";
        if (!tryCompile("samplerCUBE/texCUBE", src, "ps_3_0")) ++fails;
    }
    {
        const char* src = "sampler2D DiffuseMap : register(s0);\n"
                           "float4 main(float4 uv : TEXCOORD0) : COLOR0 { return tex2Dlod(DiffuseMap, uv); }\n";
        if (!tryCompile("tex2Dlod", src, "ps_3_0")) ++fails;
    }
    {
        const char* src = "sampler2D DiffuseMap : register(s0);\n"
                           "float4 main(float4 uv : TEXCOORD0) : POSITION0 { return tex2Dlod(DiffuseMap, uv); }\n";
        if (!tryCompile("tex2Dlod (vs_3_0)", src, "vs_3_0")) ++fails;
    }

    // Relative-addressed constant array + sincos + saturate + int4 array -
    // the other idioms the real translator emits.
    {
        const char* src = "float4 c[64] : register(c0);\n"
                           "int4 a0reg : register(c63);\n" // just to prove arrays+scalars coexist; not used by translator this way
                           "float4 main(float4 pos : POSITION0) : POSITION0 {\n"
                           "  int a0 = 3;\n"
                           "  return c[a0 + 2];\n"
                           "}\n";
        if (!tryCompile("relative-addressed c[] array", src, "vs_3_0")) ++fails;
    }
    {
        const char* src = "float4 main(float4 pos : POSITION0) : POSITION0 {\n"
                           "  float s, c; sincos(pos.x, s, c);\n"
                           "  return float4(s, c, 0, 1);\n"
                           "}\n";
        if (!tryCompile("sincos()", src, "vs_3_0")) ++fails;
    }
    {
        const char* src = "bool b[4] : register(b0);\n"
                           "float4 main(float4 pos : POSITION0) : POSITION0 {\n"
                           "  if (b[0]) return pos; else return -pos;\n"
                           "}\n";
        if (!tryCompile("bool[] array + if/else", src, "vs_3_0")) ++fails;
    }

    printf("\n%d probe(s) failed\n", fails);
    return fails == 0 ? 0 : 1;
}
