#include "sr3render/texture_upload.h"

#include <d3d11.h>

#include <cstdio>
#include <vector>

namespace sr3render {

namespace {

std::string hrToString(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

struct FormatChoice {
    DXGI_FORMAT dxgi = DXGI_FORMAT_UNKNOWN;
    FormatMapping mapping = FormatMapping::Unsupported;
    const char* name = "";
    bool cpuExpandR5G6B5 = false;
};

// Spec Sec11.1's code -> D3D9 format table, mapped to the DXGI format with
// the identical memory layout (see texture_upload.h for the evidence tier
// of each row). Returns false for a code with no mapping.
bool formatFor(uint32_t code, FormatChoice& out) {
    switch (code) {
        case sr3texture::kFormatDxt1:
            out = {DXGI_FORMAT_BC1_UNORM, FormatMapping::Direct, "BC1_UNORM", false};
            return true;
        case sr3texture::kFormatDxt3:
            out = {DXGI_FORMAT_BC2_UNORM, FormatMapping::Direct, "BC2_UNORM", false};
            return true;
        case sr3texture::kFormatDxt5:
        case sr3texture::kFormatDxt5Alt:
            out = {DXGI_FORMAT_BC3_UNORM, FormatMapping::Direct, "BC3_UNORM", false};
            return true;
        case sr3texture::kFormatR5G6B5:
            out = {DXGI_FORMAT_R8G8B8A8_UNORM, FormatMapping::CpuExpanded, "R5G6B5 -> R8G8B8A8_UNORM (CPU)", true};
            return true;
        case sr3texture::kFormatA8R8G8B8:
            out = {DXGI_FORMAT_B8G8R8A8_UNORM, FormatMapping::Direct, "B8G8R8A8_UNORM", false};
            return true;
        case sr3texture::kFormatA8:
            out = {DXGI_FORMAT_A8_UNORM, FormatMapping::Direct, "A8_UNORM", false};
            return true;
        default:
            return false;
    }
}

} // namespace

bool uploadTexture(ID3D11Device* device, const sr3texture::TexturePair& pair, size_t index,
                   sr3texture::ByteView gpegBytes, UploadedTexture& out, std::string& error) {
    if (device == nullptr) {
        error = "null device";
        return false;
    }
    if (index >= pair.records().size()) {
        error = "texture index " + std::to_string(index) + " out of range (" +
                std::to_string(pair.records().size()) + " records)";
        return false;
    }

    const auto& record = pair.records()[index];
    out.width = record.width;
    out.height = record.height;
    out.pixelFormatCode = record.pixelFormat;
    out.declaredBytes = record.compressedSize;

    FormatChoice fmt;
    if (!formatFor(record.pixelFormat, fmt)) {
        out.mapping = FormatMapping::Unsupported;
        sr3texture::PixelFormatInfo info;
        if (sr3texture::lookupPixelFormat(record.pixelFormat, info)) {
            error = "pixel format code " + std::to_string(record.pixelFormat) + " (" + info.d3d9Name +
                    ") is accepted by the game but occurs in no shipped record (spec-texture-format.md "
                    "Sec11.1), so there is no data to check a mapping against; not mapped";
        } else {
            error = "pixel format code " + std::to_string(record.pixelFormat) +
                    " is not in the spec's Sec11.1 table (the game's upload path rejects it too)";
        }
        return false;
    }
    out.mapping = fmt.mapping;
    out.dxgiFormatName = fmt.name;

    if (record.width == 0 || record.height == 0) {
        error = "texture has a zero dimension";
        return false;
    }
    if (!record.hasData()) {
        error = "record has the no-data offset sentinel 0xFFFFFFFF (spec Sec10.2)";
        return false;
    }
    if (record.levelCount == 0) {
        error = "record declares zero mip levels (+0x23)";
        return false;
    }

    // The level chain, by spec Sec10.5, from the explicit level count.
    sr3texture::LevelLayout layout;
    std::string why;
    if (!sr3texture::computeLevelLayout(record.pixelFormat, record.width, record.height,
                                        record.levelCount, record.isCubeMap(), layout, why)) {
        error = why;
        return false;
    }
    out.computedMipChainBytes = static_cast<size_t>(layout.totalBytes);

    // Levels actually handed to D3D11: (width, height, row pitch, source
    // pointer). A single-level cube record is one stacked 2D image; the data
    // is contiguous (6 faces x faceBytes) and the row pitch is unchanged.
    struct UploadLevel {
        uint32_t width, height, pitch;
        uint64_t offset, bytes;
    };
    std::vector<UploadLevel> levels;
    if (layout.faceCount == 1) {
        for (const auto& lv : layout.levels) levels.push_back({lv.width, lv.height, lv.rowBytes, lv.offset, lv.bytes});
    } else if (layout.levels.size() == 1) {
        const auto& lv = layout.levels[0];
        levels.push_back({lv.width, static_cast<uint32_t>(record.height), lv.rowBytes, 0, layout.totalBytes});
    } else {
        error = "multi-level cube map: the spec's face-major/level-minor walk (Sec10.5) is code-derived only and no "
                "shipped record exercises it, so it is not uploaded";
        return false;
    }

    // The whole region must lie inside the g file.
    const uint64_t regionEnd = static_cast<uint64_t>(record.gpegOffset) + layout.totalBytes;
    if (regionEnd > gpegBytes.size()) {
        error = "level chain [" + std::to_string(record.gpegOffset) + ", " + std::to_string(regionEnd) +
                ") runs past the end of the .gpeg_pc (" + std::to_string(gpegBytes.size()) + " bytes)";
        return false;
    }

    std::vector<std::vector<uint8_t>> expanded; // owns CPU-expanded levels for the duration of the call
    std::vector<D3D11_SUBRESOURCE_DATA> subresources;
    subresources.reserve(levels.size());
    if (fmt.cpuExpandR5G6B5) expanded.resize(levels.size());
    for (size_t k = 0; k < levels.size(); ++k) {
        const UploadLevel& lv = levels[k];
        const uint8_t* src = gpegBytes.data() + record.gpegOffset + lv.offset;
        D3D11_SUBRESOURCE_DATA sub{};
        sub.SysMemSlicePitch = 0;
        if (fmt.cpuExpandR5G6B5) {
            const size_t pixels = static_cast<size_t>(lv.width) * lv.height;
            expanded[k].resize(pixels * 4);
            sr3texture::expandR5G6B5ToRgba8(src, pixels, expanded[k].data());
            sub.pSysMem = expanded[k].data();
            sub.SysMemPitch = lv.width * 4;
        } else {
            sub.pSysMem = src;
            sub.SysMemPitch = lv.pitch;
        }
        subresources.push_back(sub);
    }
    out.mipLevels = static_cast<uint32_t>(levels.size());

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = record.width;
    desc.Height = record.height;
    desc.MipLevels = out.mipLevels;
    desc.ArraySize = 1;
    desc.Format = fmt.dxgi;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, subresources.data(), &texture);
    if (FAILED(hr)) {
        error = "CreateTexture2D failed for " + std::string(fmt.name) + " " +
                std::to_string(record.width) + "x" + std::to_string(record.height) + ": " +
                hrToString(hr);
        return false;
    }

    hr = device->CreateShaderResourceView(texture, nullptr, &out.srv);
    texture->Release(); // the SRV holds its own reference
    if (FAILED(hr)) {
        error = "CreateShaderResourceView failed: " + hrToString(hr);
        return false;
    }

    return true;
}

void releaseTexture(UploadedTexture& texture) {
    if (texture.srv != nullptr) {
        texture.srv->Release();
        texture.srv = nullptr;
    }
}

} // namespace sr3render
