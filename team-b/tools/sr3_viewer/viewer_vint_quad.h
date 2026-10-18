#pragma once

#include "viewer_platform.h"

namespace sr3viewer {

// One bitmap-resource pool lookup (Sec12.1) for an image name.
struct VintImageEntry {
    bool sized = false;   // the pool lookup succeeded (sheet sub-image or standalone texture found)
    int mode = -1;        // 1 = sheet sub-image, 0 = standalone image
    std::string sheet;
    int32_t pageX = 0, pageY = 0; // p (mode 1)
    double imgW = 0, imgH = 0;    // the image's own size
    ID3D11ShaderResourceView* srv = nullptr; // null = texture handle -1 -> no draw (Sec12.1 0x00e30470 step 1)
    uint32_t texW = 0, texH = 0;  // T
    std::string textureName, foundIn;
};


// A minimal textured-quad pipeline for the slot +0x20 contract (Sec12.4):
// pixel-space positions, UVs and a per-vertex RGBA8 colour, 4-vertex strip.
//
// UPDATED 2026-10-03 (spec-vint-render.md Sec15, UI draw unit U6 - the
// exact D3D9 state rl_draw itself sets, closing U4's own remaining open
// item): was one fixed "filter"-only blend state and no masking at all
// (every element inside a mask subtree was simply not drawn, "mask pass
// not modelled"). Now implements all 5 real render_mode blend codes
// (Sec15.4), the 3 real stencil states the 2 real mask modes need
// (Sec15.6 - mode 0 off / 1 test / 2 write, this pipeline never uses the
// other 3 jump-table entries since the UI itself never does), wrap
// addressing (was CLAMP) and the real half-texel UV offset plus the
// matching -0.5px vertex shift (Sec15.7 - CORRECTING U3/U4's own prior
// "no half-pixel compensation" CHOSEN stance, which U6 found to be wrong:
// the offset is real, just applied in the pixel shader's UV rather than
// the vertex position, with a D3D11 equivalent that shifts geometry
// instead since D3D11's pixel-centre convention is already offset from
// D3D9's). Submission shape (one DrawPrimitiveUP-equivalent Draw() call
// per primitive, never batched across elements, Sec15.2) was already
// correct and needs no change - confirmed, not re-derived.
class VintQuadPipeline {
public:
    ~VintQuadPipeline() { release(); }
    bool initialise(ID3D11Device* device, uint32_t width, uint32_t height, std::string& error) {
        static const char* kSrc = R"(
cbuffer C : register(b0) { float2 toClip; float srgbTexture; float maskWrite; float2 halfTexel; float2 pad2; };
struct VI { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
VO vsMain(VI i) {
    VO o;
    // Sec15.7: a D3D11 port shifts every UI vertex by (-0.5,-0.5) px (D3D9's
    // pixel-centre convention vs D3D11's) to reproduce the real half-texel
    // sampling alongside the pixel shader's own UV offset below.
    float2 p = i.pos - float2(0.5, 0.5);
    o.pos = float4(p.x * toClip.x - 1.0, 1.0 - p.y * toClip.y, 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}
Texture2D tex : register(t0);
SamplerState smp : register(s0);
float3 srgbToLinear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return lerp(hi, lo, step(c, 0.04045));
}
float4 psMain(VO i) : SV_Target {
    // Sec15.7: the real pixel shader samples at uv + (0.5/texW, 0.5/texH) -
    // this project's own D3D11 equivalent of the engine's own half-texel.
    float4 t = tex.Sample(smp, i.uv + halfTexel);
    if (srgbTexture > 0.5) t.rgb = srgbToLinear(t.rgb);
    float4 result = t * i.col;
    // Sec15.6 mode 2 (write the mask): D3D11 has no fixed-function alpha
    // test, so the real "ALPHAFUNC GREATER, ALPHAREF 0" gate (stencil gets
    // written only where final alpha is above 0) is reproduced with an
    // explicit discard - the stencil write never happens for a discarded
    // pixel, matching "every pixel... with final alpha above 0 gets
    // stencil 1" exactly. Colour write is separately off via the blend
    // state's write mask (Sec15.6), so this discard's only real effect is
    // on the stencil result.
    if (maskWrite > 0.5 && result.a <= 0.0) discard;
    return result;
}
)";
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* err = nullptr;
        if (FAILED(D3DCompile(kSrc, std::strlen(kSrc), "vint_quad", nullptr, nullptr, "vsMain", "vs_4_0", 0, 0, &vsBlob,
                              &err))) {
            error = std::string("vint VS compile: ") + (err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
            if (err) err->Release();
            return false;
        }
        if (FAILED(D3DCompile(kSrc, std::strlen(kSrc), "vint_quad", nullptr, nullptr, "psMain", "ps_4_0", 0, 0, &psBlob,
                              &err))) {
            error = std::string("vint PS compile: ") + (err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
            if (err) err->Release();
            vsBlob->Release();
            return false;
        }
        bool ok = SUCCEEDED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_)) &&
                  SUCCEEDED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_));
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        ok = ok && SUCCEEDED(device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout_));
        vsBlob->Release();
        psBlob->Release();
        if (!ok) { error = "vint shader/input-layout creation failed"; return false; }

        D3D11_BUFFER_DESC vb{};
        vb.ByteWidth = sizeof(Vertex) * 4;
        vb.Usage = D3D11_USAGE_DYNAMIC;
        vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 32; // 8 floats: toClip(2)/srgbTexture(1)/maskWrite(1)/halfTexel(2)/pad(2)
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        // Sec15.4: all 5 real render_mode blend codes. "The same factors
        // apply to the target's alpha channel (separate alpha blending is
        // never turned on by the UI)" is the real D3D9 engine's own
        // behaviour - D3D9 permits a colour-only factor (SRCCOLOR/
        // DESTCOLOR) in the alpha slot too, so one literal state covers
        // both channels there. D3D11's CreateBlendState validation rejects
        // SRC_COLOR/DEST_COLOR for the alpha slot outright (found
        // empirically building this), so `multiply` uses DESTALPHA/ZERO/ADD
        // for its own alpha channel instead of DESTCOLOR/ZERO/ADD. This is
        // an EXACT equivalence, not an approximation: a blend equation's
        // alpha channel only ever reads the ALPHA component of whichever
        // factor is named, regardless of the factor's own symbolic name -
        // DEST_COLOR's alpha component IS dst.a, the identical value
        // DEST_ALPHA supplies, by the blend factor's own definition (every
        // blend factor is a 4-component RGBA vector; the alpha output lane
        // only ever multiplies against that vector's own A component). So
        // DESTALPHA in the alpha slot produces the numerically identical
        // result the real engine's own literal DESTCOLOR-in-both-slots
        // state would have, not a forced platform workaround.
        auto makeBlend = [&](D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND_OP op, D3D11_BLEND srcA, D3D11_BLEND dstA,
                              ID3D11BlendState** out) {
            D3D11_BLEND_DESC bd{};
            bd.RenderTarget[0].BlendEnable = TRUE;
            bd.RenderTarget[0].SrcBlend = src;
            bd.RenderTarget[0].DestBlend = dst;
            bd.RenderTarget[0].BlendOp = op;
            bd.RenderTarget[0].SrcBlendAlpha = srcA;
            bd.RenderTarget[0].DestBlendAlpha = dstA;
            bd.RenderTarget[0].BlendOpAlpha = op;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            return SUCCEEDED(device->CreateBlendState(&bd, out));
        };
        bool ok2 = makeBlend(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA,
                              D3D11_BLEND_INV_SRC_ALPHA, &blendFilter_) &&
                   makeBlend(D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE,
                             &blendAdditive_) &&
                   makeBlend(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA,
                             D3D11_BLEND_ONE, &blendAdditiveAlpha_) &&
                   makeBlend(D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_REV_SUBTRACT, D3D11_BLEND_ONE,
                             D3D11_BLEND_ONE, &blendSubtractive_) &&
                   makeBlend(D3D11_BLEND_DEST_COLOR, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_BLEND_DEST_ALPHA,
                             D3D11_BLEND_ZERO, &blendMultiply_);
        {
            // Sec15.4 blend code 0 (mask-write mode, forced regardless of
            // the element's own render_mode): ALPHABLENDENABLE FALSE, and
            // Sec15.6's own "COLORWRITEENABLE 0" for the same mode.
            D3D11_BLEND_DESC bd{};
            bd.RenderTarget[0].BlendEnable = FALSE;
            bd.RenderTarget[0].RenderTargetWriteMask = 0;
            ok2 = ok2 && SUCCEEDED(device->CreateBlendState(&bd, &blendMaskWrite_));
        }

        // Sec15.6: the 3 real stencil states the 2 real mask modes need
        // (mode 0 off, 1 test, 2 write - the other 3 jump-table nibbles are
        // confirmed unused by the UI, Sec15.6's own table). Depth stays
        // disabled in all 3 (the UI's normal depth mode 0, Sec15.5/15.9.1 -
        // `background`'s own depth mode 1 is a separate, not-yet-built
        // item). BackFace mirrors FrontFace throughout: the UI never turns
        // on TWOSIDEDSTENCILMODE (Sec15.3's own "never set" list), and a
        // 2-D quad can present either winding under CullMode NONE, so both
        // faces need identical behaviour to match "front-face settings
        // apply regardless of which way the triangle faces" faithfully.
        auto makeDepthStencil = [&](bool stencilEnable, D3D11_COMPARISON_FUNC func, D3D11_STENCIL_OP passOp,
                                     ID3D11DepthStencilState** out) {
            D3D11_DEPTH_STENCIL_DESC dd{};
            dd.DepthEnable = FALSE;
            dd.StencilEnable = stencilEnable ? TRUE : FALSE;
            dd.StencilReadMask = 0xff;  // Sec15.3: STENCILMASK never set by the UI - inherited default 0xff
            dd.StencilWriteMask = 0xff; // Sec15.3: same for STENCILWRITEMASK's mask value
            D3D11_DEPTH_STENCILOP_DESC face{};
            face.StencilFailOp = D3D11_STENCIL_OP_KEEP;
            face.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            face.StencilPassOp = passOp;
            face.StencilFunc = func;
            dd.FrontFace = face;
            dd.BackFace = face;
            return SUCCEEDED(device->CreateDepthStencilState(&dd, out));
        };
        ok2 = ok2 && makeDepthStencil(false, D3D11_COMPARISON_ALWAYS, D3D11_STENCIL_OP_KEEP, &dsOff_) &&
               makeDepthStencil(true, D3D11_COMPARISON_EQUAL, D3D11_STENCIL_OP_KEEP, &dsTest_) &&
               makeDepthStencil(true, D3D11_COMPARISON_ALWAYS, D3D11_STENCIL_OP_REPLACE, &dsWrite_);

        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; // Sec15.7: linear mag, bilinear-equivalent min (MAXANISOTROPY=1), linear mip
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; // Sec15.7: WRAP, was CLAMP
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        // 1x1 opaque white, for the untextured text placeholder.
        const uint32_t white = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = 1;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{&white, 4, 4};
        ID3D11Texture2D* whiteTex = nullptr;
        bool ok3 = SUCCEEDED(device->CreateBuffer(&vb, nullptr, &vb_)) && SUCCEEDED(device->CreateBuffer(&cb, nullptr, &cb_)) &&
             SUCCEEDED(device->CreateSamplerState(&sd, &sampler_)) &&
             SUCCEEDED(device->CreateRasterizerState(&rd, &raster_)) &&
             SUCCEEDED(device->CreateTexture2D(&td, &init, &whiteTex)) &&
             SUCCEEDED(device->CreateShaderResourceView(whiteTex, nullptr, &white_));
        if (whiteTex) whiteTex->Release();

        // A real stencil-capable depth buffer, needed only for masking -
        // self-contained rather than shared with any other scene's own
        // depth view, since no other pipeline in this file needs stencil.
        D3D11_TEXTURE2D_DESC dtd{};
        dtd.Width = width;
        dtd.Height = height;
        dtd.MipLevels = dtd.ArraySize = 1;
        dtd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dtd.SampleDesc.Count = 1;
        dtd.Usage = D3D11_USAGE_DEFAULT;
        dtd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ID3D11Texture2D* depthTex = nullptr;
        bool ok4 = SUCCEEDED(device->CreateTexture2D(&dtd, nullptr, &depthTex));
        if (ok4) {
            ok4 = SUCCEEDED(device->CreateDepthStencilView(depthTex, nullptr, &depthView_));
            depthTex->Release();
        }
        const bool shaderOk = ok;
        ok = ok && ok2 && ok3 && ok4;
        if (!ok) {
            error = std::string("vint pipeline state creation failed (shader/layout=") + (shaderOk ? "ok" : "FAIL") +
                    " blend/stencil=" + (ok2 ? "ok" : "FAIL") + " buf/sampler/raster/white=" + (ok3 ? "ok" : "FAIL") +
                    " depth=" + (ok4 ? "ok" : "FAIL") + ")";
        }
        return ok;
    }
    void begin(ID3D11DeviceContext* ctx, uint32_t width, uint32_t height, bool srgbTexture) {
        toClipX_ = 2.0f / static_cast<float>(width);
        toClipY_ = 2.0f / static_cast<float>(height);
        srgbTexture_ = srgbTexture ? 1.0f : 0.0f;
        D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(raster_);
        ctx->IASetInputLayout(layout_);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        const UINT stride = sizeof(Vertex), offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vb_, &stride, &offset);
        ctx->VSSetShader(vs_, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &cb_);
        ctx->PSSetShader(ps_, nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, &cb_);
        ctx->PSSetSamplers(0, 1, &sampler_);
        setMaskMode(ctx, 0);
    }
    ID3D11DepthStencilView* depthView() const { return depthView_; }
    // Sec15.6 "mask begin": the stencil buffer is cleared to 0 - called
    // once per fresh mask-write run (mode 0 -> 2 transition), never per
    // element, matching the real engine's own "fires only on an actual
    // mode change" callback contract (Sec15.1's own +0x08 note).
    void clearMaskStencil(ID3D11DeviceContext* ctx) {
        if (depthView_) ctx->ClearDepthStencilView(depthView_, D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
    // Binds the depth-stencil state for mask mode 0 (off) / 1 (test,
    // StencilRef irrelevant to EQUAL-vs-1 since the ref itself IS 1) / 2
    // (write). Tracked so quad() can also force the mask-write blend state
    // and the shader's discard flag regardless of the element's own
    // render_mode (Sec15.6's own "forced blend code 0" for mode 2).
    void setMaskMode(ID3D11DeviceContext* ctx, int mode) {
        currentMaskMode_ = mode;
        ID3D11DepthStencilState* st = mode == 2 ? dsWrite_ : (mode == 1 ? dsTest_ : dsOff_);
        ctx->OMSetDepthStencilState(st, 1);
    }
    // pos/uv in strip order (u0,v0),(u1,v0),(u0,v1),(u1,v1); rgba packed
    // r,g,b,a. renderMode is the Vint render_mode value (1-5, spec-vint-
    // render.md Sec13.5); texW/texH size the pixel shader's half-texel
    // offset (Sec15.7) and are ignored (no visible effect on a solid
    // colour) for the untextured 1x1 white texture rect() uses.
    void quad(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* srv, const double pos[4][2], const double uv[4][2],
              const uint8_t rgba[4], int64_t renderMode, uint32_t texW, uint32_t texH) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        Vertex* v = static_cast<Vertex*>(m.pData);
        for (int i = 0; i < 4; ++i) {
            v[i].x = static_cast<float>(pos[i][0]);
            v[i].y = static_cast<float>(pos[i][1]);
            v[i].u = static_cast<float>(uv[i][0]);
            v[i].v = static_cast<float>(uv[i][1]);
            std::memcpy(v[i].rgba, rgba, 4);
        }
        ctx->Unmap(vb_, 0);
        D3D11_MAPPED_SUBRESOURCE cm{};
        if (SUCCEEDED(ctx->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &cm))) {
            float* f = static_cast<float*>(cm.pData);
            f[0] = toClipX_;
            f[1] = toClipY_;
            f[2] = srgbTexture_;
            f[3] = currentMaskMode_ == 2 ? 1.0f : 0.0f;
            f[4] = texW ? 0.5f / static_cast<float>(texW) : 0.0f;
            f[5] = texH ? 0.5f / static_cast<float>(texH) : 0.0f;
            f[6] = f[7] = 0.0f;
            ctx->Unmap(cb_, 0);
        }
        ID3D11ShaderResourceView* s = srv ? srv : white_;
        ctx->PSSetShaderResources(0, 1, &s);
        ID3D11BlendState* bs = currentMaskMode_ == 2 ? blendMaskWrite_ : blendForRenderMode(renderMode);
        const float bf[4] = {0, 0, 0, 0};
        ctx->OMSetBlendState(bs, bf, 0xFFFFFFFFu);
        ctx->Draw(4, 0);
    }
    // An axis-aligned solid rectangle in device pixels (placeholder
    // outlines) - always drawn "filter"-blended regardless of the current
    // element's own render_mode, a debug marker rather than real spec'd
    // content, but still respects the current mask mode like any draw.
    void rect(ID3D11DeviceContext* ctx, double x0, double y0, double x1, double y1, const uint8_t rgba[4]) {
        const double pos[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
        const double uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        quad(ctx, nullptr, pos, uv, rgba, 1, 1, 1);
    }

private:
    struct Vertex { float x, y, u, v; uint8_t rgba[4]; };
    // Sec13.5: render_mode values 1-5 map to 5 real, distinct blend codes
    // (filter/additive/additive_alpha/subtractive/multiply, Sec15.4). "A
    // value above 5 leaves the previous blend code in place rather than
    // erroring" - not reproduced (would need a sticky "last code" rather
    // than a pure function of renderMode); CHOSEN to fall back to filter
    // instead, since no shipped content this project has exercised reaches
    // that path (Sec13.5's own wording - "above 5", never observed).
    ID3D11BlendState* blendForRenderMode(int64_t renderMode) const {
        switch (renderMode) {
            case 2: return blendAdditive_;
            case 3: return blendAdditiveAlpha_;
            case 4: return blendSubtractive_;
            case 5: return blendMultiply_;
            default: return blendFilter_;
        }
    }
    void release() {
        auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
        rel(vs_); rel(ps_); rel(layout_); rel(vb_); rel(cb_); rel(sampler_); rel(raster_); rel(white_);
        rel(blendFilter_); rel(blendAdditive_); rel(blendAdditiveAlpha_); rel(blendSubtractive_); rel(blendMultiply_);
        rel(blendMaskWrite_); rel(dsOff_); rel(dsTest_); rel(dsWrite_); rel(depthView_);
    }
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* vb_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
    ID3D11BlendState* blendFilter_ = nullptr;
    ID3D11BlendState* blendAdditive_ = nullptr;
    ID3D11BlendState* blendAdditiveAlpha_ = nullptr;
    ID3D11BlendState* blendSubtractive_ = nullptr;
    ID3D11BlendState* blendMultiply_ = nullptr;
    ID3D11BlendState* blendMaskWrite_ = nullptr;
    ID3D11DepthStencilState* dsOff_ = nullptr;
    ID3D11DepthStencilState* dsTest_ = nullptr;
    ID3D11DepthStencilState* dsWrite_ = nullptr;
    ID3D11DepthStencilView* depthView_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11RasterizerState* raster_ = nullptr;
    ID3D11ShaderResourceView* white_ = nullptr;
    int currentMaskMode_ = 0;
    float toClipX_ = 0, toClipY_ = 0, srgbTexture_ = 0;
};

} // namespace sr3viewer
