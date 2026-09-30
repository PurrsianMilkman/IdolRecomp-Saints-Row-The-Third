// Draws a single texture across the whole render target.
//
// Minimal on purpose: this exists to get real SR3 pixel data onto a
// surface that can be read back and looked at, which is what turns a
// texture-format hypothesis into a confirmed one (see texture_upload.h).
// It is not a material system and has no business becoming one.
//
// Uses the standard three-vertex fullscreen-triangle trick driven entirely
// off SV_VertexID, so there is no vertex buffer, no input layout and no
// index buffer to get wrong - fewer moving parts between the texture bytes
// and the picture.
//
// Shaders are compiled at runtime with D3DCompile. The game's own .fxo_pc
// shaders are D3D9 SM1-3 bytecode and cannot be used here (see device.h).

#pragma once

#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;

namespace sr3render {

class QuadRenderer {
public:
    QuadRenderer() = default;
    ~QuadRenderer();
    QuadRenderer(const QuadRenderer&) = delete;
    QuadRenderer& operator=(const QuadRenderer&) = delete;

    // Compiles the shaders and creates the sampler. Returns false with
    // `error` set on failure (including shader compiler diagnostics, which
    // are passed through verbatim rather than summarised).
    bool initialise(ID3D11Device* device, std::string& error);

    // Draws `srv` over the whole currently-bound render target.
    void draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* srv);

private:
    void release();

    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
};

} // namespace sr3render
