#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_common.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3texture/texture_pair.h"
#include "vpp/container.h"

using namespace sr3viewer;


int runClear(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: sr3_viewer clear <out.png> [width height]\n");
        return 1;
    }
    const std::string outPath = argv[2];
    uint32_t width = 640;
    uint32_t height = 360;
    if (argc >= 5) {
        width = static_cast<uint32_t>(std::atoi(argv[3]));
        height = static_cast<uint32_t>(std::atoi(argv[4]));
    }

    sr3render::RenderDevice device;
    std::string error;
    if (!device.initialise(width, height, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }

    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().empty() ? "unnamed adapter" : device.adapterName().c_str());
    std::printf("target   : %ux%u RGBA8 offscreen\n", device.width(), device.height());

    device.bindRenderTarget();

    // A deliberately distinctive colour. A wrong-channel-order bug (the
    // classic RGBA/BGRA mix-up) would show up as the wrong hue rather than
    // as a plausible-looking image, so this doubles as a channel check.
    const sr3render::Rgba8 clearColour{40, 90, 160, 255};
    device.clear(clearColour);

    std::vector<uint8_t> pixels;
    if (!device.readBack(pixels, error)) {
        std::fprintf(stderr, "readback failed: %s\n", error.c_str());
        return 1;
    }

    // Verify the readback actually contains what was asked for, before
    // trusting the file. Checking a pixel is the difference between "the
    // calls returned S_OK" and "the target really holds this colour".
    bool allMatch = true;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i + 0] != clearColour.r || pixels[i + 1] != clearColour.g ||
            pixels[i + 2] != clearColour.b || pixels[i + 3] != clearColour.a) {
            allMatch = false;
            std::fprintf(stderr,
                         "readback mismatch at pixel %zu: got (%u,%u,%u,%u), expected (%u,%u,%u,%u)\n",
                         i / 4, pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3],
                         clearColour.r, clearColour.g, clearColour.b, clearColour.a);
            break;
        }
    }
    std::printf("readback : %zu bytes, all pixels match clear colour: %s\n", pixels.size(),
                allMatch ? "YES" : "NO");

    if (!sr3render::writePng(outPath, device.width(), device.height(), pixels, error)) {
        std::fprintf(stderr, "png write failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("wrote    : %s\n", outPath.c_str());
    return allMatch ? 0 : 1;
}


namespace {


// Walks an archive listing .cpeg_pc/.cvbm_pc entries and their textures.
void listTextures(const vpp::Container& container, int& remaining, int depth) {
    for (size_t i = 0; i < container.entries().size() && remaining > 0; ++i) {
        const std::string& name = container.entries()[i].name;
        if (endsWithNoCase(name, ".cpeg_pc") || endsWithNoCase(name, ".cvbm_pc")) {
            std::vector<uint8_t> bytes;
            if (!entryBytes(container, i, bytes) || bytes.empty()) continue;
            try {
                sr3texture::TexturePair pair = sr3texture::TexturePair::parse(
                    sr3texture::ByteView(bytes.data(), bytes.size()));
                std::printf("%*s%s  (%zu texture(s))\n", depth * 2, "", name.c_str(),
                            pair.records().size());
                for (const auto& rec : pair.records()) {
                    std::printf("%*s    %-40s %5ux%-5u fmt=%-4u %8u bytes\n", depth * 2, "",
                                rec.name.c_str(), rec.width, rec.height, rec.pixelFormat,
                                rec.compressedSize);
                }
                --remaining;
            } catch (const std::exception&) {
            }
        }
        if (container.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = container.openNested(i);
                listTextures(nested, remaining, depth + 1);
            } catch (const std::exception&) {
            }
        }
    }
}

} // namespace


int runListTextures(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: sr3_viewer list-textures <archive.vpp_pc> [limit]\n");
        return 1;
    }
    int limit = argc >= 4 ? std::atoi(argv[3]) : 20;
    std::vector<uint8_t> archive = readFile(argv[2]);
    if (archive.empty()) {
        std::fprintf(stderr, "could not read %s\n", argv[2]);
        return 1;
    }
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        listTextures(container, limit, 0);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}


int runTexture(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: sr3_viewer texture <archive.vpp_pc> <name.cpeg_pc> <out.png> [index]\n");
        return 1;
    }
    const std::string archivePath = argv[2];
    const std::string cpegName = argv[3];
    const std::string outPath = argv[4];
    const size_t textureIndex = argc >= 6 ? static_cast<size_t>(std::atoi(argv[5])) : 0;

    // The paired GPU-side file: same stem, c -> g on the extension
    // (spec-texture-format.md Sec1/Sec7).
    std::string gpegName = cpegName;
    size_t dot = gpegName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gpegName.size()) gpegName[dot + 1] = 'g';

    std::vector<uint8_t> archive = readFile(archivePath);
    if (archive.empty()) {
        std::fprintf(stderr, "could not read %s\n", archivePath.c_str());
        return 1;
    }

    std::vector<uint8_t> cpegBytes, gpegBytes;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, cpegName, cpegBytes, gpegBytes, gpegName)) {
            std::fprintf(stderr, "could not find '%s' in %s\n", cpegName.c_str(),
                         archivePath.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what());
        return 1;
    }
    if (gpegBytes.empty()) {
        std::fprintf(stderr, "found '%s' but not its paired '%s'\n", cpegName.c_str(),
                     gpegName.c_str());
        return 1;
    }
    std::printf("archive  : %s\n", archivePath.c_str());
    std::printf("cpeg     : %s (%zu bytes)\n", cpegName.c_str(), cpegBytes.size());
    std::printf("gpeg     : %s (%zu bytes)\n", gpegName.c_str(), gpegBytes.size());

    sr3texture::TexturePair pair =
        sr3texture::TexturePair::parse(sr3texture::ByteView(cpegBytes.data(), cpegBytes.size()));
    if (textureIndex >= pair.records().size()) {
        std::fprintf(stderr, "texture index %zu out of range (%zu records)\n", textureIndex,
                     pair.records().size());
        return 1;
    }
    const auto& record = pair.records()[textureIndex];
    std::printf("texture  : [%zu] %s  %ux%u  format code %u  %u bytes @%u\n", textureIndex,
                record.name.c_str(), record.width, record.height, record.pixelFormat,
                record.compressedSize, record.gpegOffset);

    // Render at the texture's own resolution so what lands in the PNG is
    // the decoded texels, not a resampling of them.
    sr3render::RenderDevice device;
    std::string error;
    if (!device.initialise(record.width, record.height, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());

    sr3render::UploadedTexture uploaded;
    if (!sr3render::uploadTexture(device.device(), pair, textureIndex,
                                  sr3texture::ByteView(gpegBytes.data(), gpegBytes.size()),
                                  uploaded, error)) {
        std::fprintf(stderr, "texture upload failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("mapped   : format code %u -> %s (HYPOTHESIS under test, spec Sec6 item 1)\n",
                uploaded.pixelFormatCode, uploaded.dxgiFormatName.c_str());
    std::printf("mips     : %u levels, computed chain %zu bytes vs declared %zu bytes -> %s\n",
                uploaded.mipLevels, uploaded.computedMipChainBytes, uploaded.declaredBytes,
                uploaded.computedMipChainBytes == uploaded.declaredBytes
                    ? "EXACT MATCH"
                    : "differ (see spec Sec4 on block-size rounding at the smallest mips)");

    sr3render::QuadRenderer quad;
    if (!quad.initialise(device.device(), error)) {
        std::fprintf(stderr, "quad renderer init failed: %s\n", error.c_str());
        sr3render::releaseTexture(uploaded);
        return 1;
    }

    device.bindRenderTarget();
    // Magenta clear: if the draw silently did nothing, the output would be
    // an unmistakable flat magenta rather than something that might pass
    // for a dark texture.
    device.clear({255, 0, 255, 255});
    quad.draw(device.context(), uploaded.srv);

    std::vector<uint8_t> pixels;
    if (!device.readBack(pixels, error)) {
        std::fprintf(stderr, "readback failed: %s\n", error.c_str());
        sr3render::releaseTexture(uploaded);
        return 1;
    }
    sr3render::releaseTexture(uploaded);

    // Per-channel summary of the decoded texels. Small images are hard to
    // judge by eye, and some questions are numeric anyway - e.g. whether a
    // channel is flat-zero, which is what distinguishes a two-channel
    // normal map (X,Y stored, Z reconstructed at runtime) from an ordinary
    // colour texture. Reported, not interpreted.
    {
        unsigned mins[4] = {255, 255, 255, 255};
        unsigned maxs[4] = {0, 0, 0, 0};
        unsigned long long sums[4] = {0, 0, 0, 0};
        const size_t pixelCount = pixels.size() / 4;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            for (int c = 0; c < 4; ++c) {
                unsigned v = pixels[i + static_cast<size_t>(c)];
                if (v < mins[c]) mins[c] = v;
                if (v > maxs[c]) maxs[c] = v;
                sums[c] += v;
            }
        }
        const char* names[4] = {"R", "G", "B", "A"};
        std::printf("channels : ");
        for (int c = 0; c < 4; ++c) {
            std::printf("%s[min %3u max %3u mean %3llu]%s", names[c], mins[c], maxs[c],
                        pixelCount ? sums[c] / pixelCount : 0ULL, c < 3 ? "  " : "\n");
        }
    }

    if (!sr3render::writePng(outPath, device.width(), device.height(), pixels, error)) {
        std::fprintf(stderr, "png write failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("wrote    : %s\n", outPath.c_str());
    return 0;
}


// Interactive windowed viewing. Keeps the same texture pipeline as
// runTexture() but presents to a swap chain instead of an offscreen
// target.
//
// --frames N and --capture <png> exist so this path is verifiable without
// a human watching: run a bounded number of frames, write out what was
// actually presented, exit. A window nobody can check is weaker evidence
// than this project accepts elsewhere.
int runView(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: sr3_viewer view <archive.vpp_pc> <name.cpeg_pc> [index]\n"
                     "                       [--frames N] [--capture <out.png>] [--size W H]\n"
                     "                       [--visible]\n");
        return 1;
    }
    const std::string archivePath = argv[2];
    const std::string cpegName = argv[3];
    size_t textureIndex = 0;
    int maxFrames = -1; // -1 = run until closed
    std::string capturePath;
    uint32_t winWidth = 800, winHeight = 600;
    bool visible = false; // off by default - see sr3render/window.h's own doc comment

    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            maxFrames = std::atoi(argv[++i]);
        } else if (arg == "--capture" && i + 1 < argc) {
            capturePath = argv[++i];
        } else if (arg == "--size" && i + 2 < argc) {
            winWidth = static_cast<uint32_t>(std::atoi(argv[++i]));
            winHeight = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--visible") {
            visible = true;
        } else if (!arg.empty() && arg[0] != '-') {
            // Note the bound: `arg.size() > 1` here would silently ignore
            // every single-digit index, which is most of them.
            textureIndex = static_cast<size_t>(std::atoi(arg.c_str()));
        }
    }

    std::string gpegName = cpegName;
    size_t dot = gpegName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gpegName.size()) gpegName[dot + 1] = 'g';

    std::vector<uint8_t> archive = readFile(archivePath);
    if (archive.empty()) {
        std::fprintf(stderr, "could not read %s\n", archivePath.c_str());
        return 1;
    }
    std::vector<uint8_t> cpegBytes, gpegBytes;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, cpegName, cpegBytes, gpegBytes, gpegName)) {
            std::fprintf(stderr, "could not find '%s'\n", cpegName.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what());
        return 1;
    }
    if (gpegBytes.empty()) {
        std::fprintf(stderr, "found '%s' but not its paired '%s'\n", cpegName.c_str(),
                     gpegName.c_str());
        return 1;
    }

    sr3texture::TexturePair pair =
        sr3texture::TexturePair::parse(sr3texture::ByteView(cpegBytes.data(), cpegBytes.size()));
    if (textureIndex >= pair.records().size()) {
        std::fprintf(stderr, "texture index %zu out of range (%zu records)\n", textureIndex,
                     pair.records().size());
        return 1;
    }
    std::printf("texture  : [%zu] %s\n", textureIndex,
                pair.records()[textureIndex].name.c_str());

    std::string error;
    sr3render::Window window;
    if (!window.create("SR3 Viewer - " + pair.records()[textureIndex].name, winWidth, winHeight,
                       error, visible)) {
        std::fprintf(stderr, "window creation failed: %s\n", error.c_str());
        return 1;
    }

    // The device's own offscreen target is unused here; a small one keeps
    // initialisation cheap since the swap chain provides the real target.
    sr3render::RenderDevice device;
    if (!device.initialise(16, 16, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());

    sr3render::SwapChain swapChain;
    if (!swapChain.create(device.device(), window.nativeHandle(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "swap chain creation failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("swapchain: %ux%u\n", swapChain.width(), swapChain.height());

    sr3render::UploadedTexture uploaded;
    if (!sr3render::uploadTexture(device.device(), pair, textureIndex,
                                  sr3texture::ByteView(gpegBytes.data(), gpegBytes.size()),
                                  uploaded, error)) {
        std::fprintf(stderr, "texture upload failed: %s\n", error.c_str());
        return 1;
    }

    sr3render::QuadRenderer quad;
    if (!quad.initialise(device.device(), error)) {
        std::fprintf(stderr, "quad renderer init failed: %s\n", error.c_str());
        sr3render::releaseTexture(uploaded);
        return 1;
    }

    int frames = 0;
    while (window.pumpMessages()) {
        if (!swapChain.resize(window.width(), window.height(), error)) {
            std::fprintf(stderr, "swap chain resize failed: %s\n", error.c_str());
            break;
        }
        swapChain.bind(device.context());
        swapChain.clear(device.context(), 0.05f, 0.05f, 0.08f, 1.0f);
        quad.draw(device.context(), uploaded.srv);

        // Capture BEFORE Present: with DXGI_SWAP_EFFECT_DISCARD the back
        // buffer's contents are undefined afterwards, so reading it after
        // presenting would capture garbage on some drivers.
        if (!capturePath.empty() && frames == 0) {
            std::vector<uint8_t> pixels;
            if (swapChain.capture(device.context(), pixels, error)) {
                if (sr3render::writePng(capturePath, swapChain.width(), swapChain.height(),
                                        pixels, error)) {
                    std::printf("captured : %s\n", capturePath.c_str());
                } else {
                    std::fprintf(stderr, "capture png failed: %s\n", error.c_str());
                }
            } else {
                std::fprintf(stderr, "capture failed: %s\n", error.c_str());
            }
        }

        swapChain.present(true);
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames) break;
    }

    std::printf("frames   : %d presented\n", frames);
    sr3render::releaseTexture(uploaded);
    return 0;
}
