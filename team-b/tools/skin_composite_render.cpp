// Character skin composite-bake renderer - builds the confirmed 2048x1024
// X8R8G8B8-equivalent sheet (spec-tables-customization.md Sec12.1e,
// CONFIRMED 2026-10-08, commit f502db5) from REAL `Composite_Layer` rows
// (customization_compositing.xtbl) and writes it to a PNG for visual
// inspection.
//
// WHAT THIS TOOL IS: wiring between three already-real, independently
// confirmed pieces of this project - nothing here is a new reader and
// nothing here is a new engine mechanism:
//   * sr3tables_customization::ParseCustomizationCompositingTable
//     (include/sr3tables_customization/materials.h,
//     src/tables_customization_materials.cpp) - the REAL, already-shipped
//     xtbl reader for Composite_Layer rows. This tool parses xtbl data
//     through it, never by hand.
//   * sr3render::CompositeSheetRenderer /
//     sr3render::BuildCharacterSkinComposite (include/sr3render/
//     composite_renderer.h) - this task's own new renderer, built on
//     sr3render::RenderDevice (device/context lifecycle) and
//     sr3render::writePng (readback-to-PNG), exactly like every other
//     sr3render milestone (tree_baseline_render.cpp,
//     prototype_lit_clmesh_tower2.cpp, ...).
//   * findAndUploadTexture()/TextureSearchCache, copied VERBATIM from
//     tools/prototype_lit_clmesh_tower2.cpp (itself copied verbatim from
//     tools/sr3_viewer.cpp - see that file's own "Real per-material
//     texture upload machinery" comment) - the project's existing by-name
//     PEG/texture-registry lookup, reused rather than reinvented, per
//     spec-tables-customization.md Sec12.1c/Sec12.1d point 1 ("each layer
//     independently resolves to a standalone registry texture").
//
// WHICH ROWS ARE "A CHARACTER'S LAYER SET": customization_compositing.xtbl
// has no field linking a Composite_Layer row to a specific character (see
// include/sr3tables_customization/materials.h's own CompositeLayer struct -
// confirmed exhaustive per spec-tables-customization.md Sec9.3). This tool
// therefore selects by the one CONFIRMED, already-parsed field that
// actually names a real-world scope: `Flags > Player_creation`
// (CompositeLayer::playerCreation) - i.e. "the layer set the live
// character-creation screen draws from" - directly relevant to what
// Sec12.1f (2026-10-08, now FULLY CLOSED) confirmed about this whole
// pipeline: it is a singleton scoped to PLAYER characters specifically (a
// 2-slot PCR pool, the engine's 2-player split-screen co-op cap - Q1), its
// composited result is NEVER bound to any character mesh (Q2 - the only
// consumer of the composite material handle is one internal draw call,
// confirmed via the generic save/override/draw/restore idiom traced from
// its scratch-global cache), and it is CONFIRMED UNRELATED to NPC skinning
// (Q3) - NPCs, Brad (`law_natguard_brad.str2_pc`) included, ship their own
// individually-named pre-baked textures via `.matlib_pc` instead, directly
// confirmed against the shipped `characters.vpp_pc` archive (no 2048x1024
// texture exists anywhere in it). See include/sr3render/
// composite_renderer.h's own top comment for the full citation chain. The
// `Player_creation` selection below is a selection by an EXISTING parsed
// field, not an invented character-to-layer mapping, and this whole tool
// is not, and cannot be, a path toward Brad's or any other NPC's face.
//
// Usage:
//   skin_composite_render <misc_tables.vpp_pc> <texture_archive.vpp_pc...> <out.png>
//
// The FIRST archive is searched (recursively, including nested Raw
// containers) for customization_compositing.xtbl; every archive
// (including the first) is searched for each layer's own diffuse texture,
// using the same findAndUploadTexture() recursive walk tools/sr3_viewer.cpp
// already uses for every other per-material texture lookup in this
// project.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <d3d11.h>

#include "sr3render/composite_renderer.h"
#include "sr3render/device.h"
#include "sr3render/png_writer.h"
#include "sr3render/texture_upload.h"
#include "sr3tables_customization/materials.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    if (n < 0) n = 0;
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

std::string baseName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    const bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                    r.status == vpp::DecodeStatus::ContentValidated ||
                    r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                    r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

bool findNamedFile(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name == name && entryBytes(c, i, out)) return true;
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                if (findNamedFile(c.openNested(i), name, out)) return true;
            } catch (const std::exception&) {
            }
        }
    }
    return false;
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
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

// ===========================================================================
// Real per-material texture upload machinery - copied from
// tools/prototype_lit_clmesh_tower2.cpp (itself copied verbatim from
// tools/sr3_viewer.cpp's TextureSearchCache/buildTextureSearchCache/
// findAndUploadTexture) - this project's existing by-name PEG/texture-
// registry lookup, reused rather than reinvented (see this file's own top
// comment). The CACHED lookup path (the only one this tool's call sites
// use) is unchanged; the original's separate UNCACHED recursive-fallback
// overload (for a caller with no TextureSearchCache at all) was omitted -
// this tool always builds one, so that branch would be dead code here.
// ===========================================================================
struct TextureSearchCache {
    bool built = false;
    struct Located {
        std::shared_ptr<std::vector<uint8_t>> cpegBytes;
        std::shared_ptr<std::vector<uint8_t>> gpegBytes;
        sr3texture::TexturePair pair;
        size_t recordIndex = 0;
        std::string recordNameLower;
        std::string foundIn;
    };
    std::vector<Located> all;
    std::map<std::string, sr3render::UploadedTexture> uploadedByWantedName;
};

void buildTextureSearchCache(const vpp::Container& container, TextureSearchCache& cache) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& name = container.entries()[i].name;
        if (endsWithNoCase(name, ".cpeg_pc") || endsWithNoCase(name, ".cvbm_pc")) {
            std::vector<uint8_t> cbytes;
            if (entryBytes(container, i, cbytes) && !cbytes.empty()) {
                try {
                    auto cbytesPtr = std::make_shared<std::vector<uint8_t>>(std::move(cbytes));
                    sr3texture::TexturePair pair =
                        sr3texture::TexturePair::parse(sr3texture::ByteView(cbytesPtr->data(), cbytesPtr->size()));
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

bool findAndUploadTexture(const vpp::Container& container, const std::string& tgaName, ID3D11Device* device,
                          sr3render::UploadedTexture& out, std::string& foundIn, TextureSearchCache* cache) {
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
            if (endsWithNoCase(loc.recordNameLower, wantedLower)) {
                foundIn = loc.foundIn;
                break;
            }
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <misc_tables.vpp_pc> <texture_archive.vpp_pc...> <out.png>\n", argv[0]);
        return 1;
    }
    const std::string tablesArchivePath = argv[1];
    const std::string outPng = argv[argc - 1];
    std::vector<std::string> textureArchivePaths;
    for (int a = 2; a < argc - 1; ++a) textureArchivePaths.push_back(argv[a]);

    std::vector<uint8_t> tablesBytes = readFile(tablesArchivePath);
    if (tablesBytes.empty()) {
        std::printf("FATAL: could not read '%s'\n", tablesArchivePath.c_str());
        return 1;
    }
    std::vector<uint8_t> xtblBytes;
    try {
        vpp::Container root(vpp::ByteView(tablesBytes.data(), tablesBytes.size()));
        if (!findNamedFile(root, "customization_compositing.xtbl", xtblBytes)) {
            std::printf("FATAL: customization_compositing.xtbl not found in '%s'\n",
                       baseName(tablesArchivePath).c_str());
            return 1;
        }
    } catch (const std::exception& e) {
        std::printf("FATAL: could not open '%s': %s\n", tablesArchivePath.c_str(), e.what());
        return 1;
    }

    sr3xtbl::Document doc = sr3xtbl::ParseDocument(xtblBytes.data(), xtblBytes.size());
    const std::vector<sr3tables_customization::CompositeLayer> allLayers =
        sr3tables_customization::ParseCustomizationCompositingTable(doc);
    std::printf("customization_compositing.xtbl: %zu total Composite_Layer row(s)\n", allLayers.size());
    {
        size_t withDiffuse = 0, withDiffuseAndPlayerCreation = 0;
        for (const auto& l : allLayers) {
            if (l.diffuseTexture.has_value()) {
                ++withDiffuse;
                if (l.playerCreation) ++withDiffuseAndPlayerCreation;
            }
        }
        std::printf("  of which have a diffuse texture at all: %zu (of which also Player_creation-flagged: %zu)\n",
                   withDiffuse, withDiffuseAndPlayerCreation);
    }

    // Selection: Flags > Player_creation AND a diffuse texture present - see
    // this file's own top comment for why Player_creation, not an invented
    // character mapping; the diffuseTexture check is added because real data
    // shows several Player_creation rows carry no diffuse reference at all
    // (e.g. normal-map-only decal layers) - nothing for THIS renderer (which
    // only ever draws a resolved diffuse quad, see composite_renderer.h) to
    // draw for those rows regardless of which texture archive is searched.
    std::vector<const sr3tables_customization::CompositeLayer*> selected;
    for (const auto& l : allLayers)
        if (l.playerCreation && l.diffuseTexture.has_value()) selected.push_back(&l);
    std::printf("  of which Player_creation-flagged AND have a diffuse texture: %zu\n", selected.size());
    if (selected.empty()) {
        std::printf("FATAL: no Player_creation-flagged Composite_Layer rows with a diffuse texture found - nothing to composite\n");
        return 1;
    }

    std::vector<uint8_t> archiveBytesHolder0 = std::move(tablesBytes); // keep alive for nested container aliasing
    std::vector<std::unique_ptr<vpp::Container>> textureRoots;
    std::vector<std::vector<uint8_t>> textureArchiveBytes;
    std::vector<TextureSearchCache> textureCaches;
    textureArchiveBytes.reserve(textureArchivePaths.size() + 1);
    // The tables archive itself is also searched for textures (some
    // archives ship both xtbl data and textures together) - same
    // convention tools/sr3_viewer.cpp uses for its own --textures list.
    textureArchiveBytes.push_back(archiveBytesHolder0);
    for (const auto& p : textureArchivePaths) textureArchiveBytes.push_back(readFile(p));
    for (size_t i = 0; i < textureArchiveBytes.size(); ++i) {
        if (textureArchiveBytes[i].empty()) {
            std::printf("FATAL: could not read texture archive #%zu\n", i);
            return 1;
        }
        textureRoots.push_back(std::make_unique<vpp::Container>(
            vpp::ByteView(textureArchiveBytes[i].data(), textureArchiveBytes[i].size())));
        textureCaches.emplace_back();
    }

    std::string error;
    sr3render::RenderDevice dev;
    if (!dev.initialise(64, 64, error)) { // this device's OWN built-in target is unused - see composite_renderer.h
        std::printf("FATAL: RenderDevice::initialise failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device: %s (%s)\n", dev.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP",
               dev.adapterName().empty() ? "unnamed" : dev.adapterName().c_str());

    std::vector<sr3render::CompositeLayerPlacement> placements;
    size_t resolvedCount = 0, unresolvedCount = 0;
    for (const auto* l : selected) {
        if (!l->diffuseTexture.has_value()) {
            ++unresolvedCount;
            continue;
        }
        sr3render::UploadedTexture tex;
        std::string foundIn;
        bool found = false;
        // l->diffuseTexture is the row's own <Filename> text, which ALREADY
        // carries the ".tga" extension in real data (e.g. "makeup_face_
        // white01.tga") - confirmed directly against misc_tables.vpp_pc's
        // own shipped customization_compositing.xtbl, 2026-10-08. An earlier
        // version of this loop appended a SECOND ".tga" here
        // (`*l->diffuseTexture + ".tga"`), which could never match any real
        // texture-registry record name and silently left every real-data row
        // "unresolved" - never caught earlier because the (separate, now also
        // fixed) diffuse/normal reader bug meant diffuseTexture was always
        // nullopt beforehand, so this line had never actually run against
        // real data until this task.
        for (size_t i = 0; i < textureRoots.size() && !found; ++i) {
            if (findAndUploadTexture(*textureRoots[i], *l->diffuseTexture, dev.device(), tex, foundIn,
                                     &textureCaches[i])) {
                found = true;
            }
        }
        if (!found) {
            std::printf("  [unresolved] %-24s diffuse=%s\n", l->name.value_or("?").c_str(),
                       l->diffuseTexture->c_str());
            ++unresolvedCount;
            continue;
        }
        sr3render::CompositeLayerPlacement p;
        p.name = l->name.value_or("");
        p.layer = l->layer.value;
        p.x = static_cast<float>(l->xCoord.value) * sr3render::kOpenPlacementScale;
        p.y = static_cast<float>(l->yCoord.value) * sr3render::kOpenPlacementScale;
        p.width = static_cast<float>(tex.width);
        p.height = static_cast<float>(tex.height);
        p.alpha = l->alpha.value;
        p.srv = tex.srv;
        placements.push_back(p);
        ++resolvedCount;
        std::printf("  [resolved]   %-24s diffuse=%-24s %ux%u @ (%.0f,%.0f) alpha=%.3f layer=%d (found in %s)\n",
                   p.name.c_str(), l->diffuseTexture->c_str(), tex.width, tex.height, p.x, p.y, p.alpha, p.layer,
                   foundIn.c_str());
    }
    std::printf("resolved=%zu unresolved=%zu (of %zu Player_creation rows)\n", resolvedCount, unresolvedCount,
               selected.size());
    if (placements.empty()) {
        std::printf("FATAL: zero layers resolved a texture - nothing to composite\n");
        return 1;
    }

    sr3render::CompositeSheetRenderer sheet;
    size_t skipped = 0;
    if (!sr3render::BuildCharacterSkinComposite(dev.device(), dev.context(), placements, sheet, error, &skipped)) {
        std::printf("FATAL: BuildCharacterSkinComposite failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("composite sheet: %ux%u, %zu layer(s) drawn (painter's-algorithm order, lowest Layer first)\n",
               sheet.width(), sheet.height(), placements.size() - skipped);

    std::vector<uint8_t> rgba;
    if (!sheet.readBack(dev.context(), rgba, error)) {
        std::printf("FATAL: readBack failed: %s\n", error.c_str());
        return 1;
    }
    if (!sr3render::writePng(outPng, sheet.width(), sheet.height(), rgba, error)) {
        std::printf("FATAL: writePng failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("wrote: %s\n", baseName(outPng).c_str());
    return 0;
}
