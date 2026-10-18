#include "viewer_platform.h"
#include "viewer_city_support.h"
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
#include "sr3geometry/character_matlib.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3geometry/material_binding.h"
#include "sr3clmesh/level_mesh.h"
#include "sr3mesh/mesh_block.h"
#include "vpp/container.h"

using namespace sr3viewer;

namespace {
// spec-world-streaming.md Sec10.6(g) (VALIDATED-BY-DATA) and Sec10.3 level 4.
constexpr float kCityTilePitchX = 320.0f;

constexpr float kCityTilePitchZ = 280.0f;

constexpr float kCityOriginX = -4000.0f;

constexpr float kCityOriginZ = 3640.0f;

constexpr float kCityRowStagger = 160.0f;

} // namespace

namespace sr3viewer {

void cityTileCentre(int col, int row, float& x, float& z) {
    x = kCityTilePitchX * static_cast<float>(col) + kCityOriginX - kCityRowStagger * static_cast<float>(row % 2);
    z = kCityOriginZ - kCityTilePitchZ * static_cast<float>(row);
}


// N -> (column offset N & 1, row offset N >> 1), row 0 = higher z (Sec10.1,
// Sec10.6(f)/(g)).
void cityFineCellCentre(int col, int row, int n, float& x, float& z) {
    cityTileCentre(col, row, x, z);
    x += (n & 1) ? kFineCellHalfX : -kFineCellHalfX;
    z += (n >> 1) ? -kFineCellHalfZ : kFineCellHalfZ;
}


// The exact algebraic inverse of cityTileCentre() above (minimap live-
// transform task, 2026-10-08) - same pitch/origin/stagger constants, not a
// new invented mechanism: row falls straight out of the z equation (the
// stagger term never touches z), then col falls out of the x equation once
// row (and so the stagger term) is known. Used by runMinimapDemo() (see its
// own doc comment) to pick the `minimap_CCRR` tile under a live world
// position - spec-world-streaming.md Sec5's own "same CCRR coordinate grid
// as the rest of the open world" is the OPEN gap this closes with a CHOSEN,
// but non-invented, resolution: the SAME grid this file already uses for
// ground-tile/prop streaming, read backwards.
void cityTileColRowForWorld(float x, float z, int& col, int& row) {
    row = static_cast<int>(std::lround((kCityOriginZ - z) / kCityTilePitchZ));
    const float stagger = kCityRowStagger * static_cast<float>(((row % 2) + 2) % 2);
    col = static_cast<int>(std::lround((x - kCityOriginX + stagger) / kCityTilePitchX));
}


// Parses EXACTLY "<4 digits>h<digit>.str2_pc" (case-insensitive 'h' and
// extension) - the default variant of a fine cell. `^name` variants and
// anything else return false.
bool parseDefaultHnEntryName(const std::string& entryName, int& col, int& row, int& n) {
    if (!endsWithNoCase(entryName, ".str2_pc")) return false;
    const std::string stem = entryName.substr(0, entryName.size() - 8);
    if (stem.size() != 6) return false;
    for (int i = 0; i < 4; ++i)
        if (stem[static_cast<size_t>(i)] < '0' || stem[static_cast<size_t>(i)] > '9') return false;
    if (stem[4] != 'h' && stem[4] != 'H') return false;
    if (stem[5] < '0' || stem[5] > '3') return false;
    col = (stem[0] - '0') * 10 + (stem[1] - '0');
    row = (stem[2] - '0') * 10 + (stem[3] - '0');
    n = stem[5] - '0';
    return true;
}


double cityNowMs() {
    LARGE_INTEGER f{}, t{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}


// Loads ONE real `.clmesh_pc`/`.glmesh_pc` pair - mirrors runClmeshTile()'s
// per-prop block step by step (see the top comment). Returns false with
// `why` set on any failure; never fabricates a stand-in.
//
// `outCollisionTris`, 2026-10-08 real-collision follow-up task: when
// non-null, ALSO decodes render-group-0's real LOCAL-space triangle soup
// (position only, 3 consecutive entries per triangle) for every channel
// this same call already uploads for rendering - the identical
// decodeChannel()/triangleListForRange() data MeshRenderer::upload() sends
// to the GPU, read a second time here into plain CPU triangles. Defaults to
// nullptr, which is every PRE-EXISTING caller (`city`, `clmesh-tile`), so
// this costs nothing and changes nothing for them.
bool loadCityProp(const vpp::Container& str2, const std::string& clName,
                  std::vector<std::unique_ptr<CityArchive>>& archives, size_t ownArchive, ID3D11Device* device,
                  bool textures, CityProp& prop, std::string& why,
                  std::vector<std::array<float, 3>>* outCollisionTris) {
    prop.name = clName;
    std::string glName = clName;
    {
        const size_t dot = glName.find_last_of('.');
        if (dot != std::string::npos) glName[dot + 1] = 'g';
    }
    std::vector<uint8_t> clBytes, glBytes;
    if (!findEntry(str2, clName, clBytes, glBytes, glName) || clBytes.empty() || glBytes.empty()) {
        why = "could not find paired .clmesh_pc/.glmesh_pc bytes";
        return false;
    }
    vpp::ByteView clView(clBytes.data(), clBytes.size());
    vpp::ByteView glView(glBytes.data(), glBytes.size());
    sr3clmesh::LevelMesh lm;
    try {
        lm = sr3clmesh::LevelMesh::parse(clView);
    } catch (const std::exception& ex) {
        why = std::string("LevelMesh parse failed: ") + ex.what();
        return false;
    }
    if (!lm.walkComplete() || lm.middle().renderGroupCount == 0) {
        why = "walk incomplete or no render groups";
        return false;
    }
    const auto& mid = lm.middle();

    std::vector<sr3clmesh::MaterialRecord> matRecs = lm.materialRecords(clView);
    prop.perMaterialSrv.assign(matRecs.size(), nullptr);
    if (textures) {
        // Search the cell's OWN archive first, then (on a miss) every other
        // archive given on the command line, in order - each through its own
        // lazily-built TextureSearchCache.
        std::vector<size_t> order{ownArchive};
        for (size_t ai = 0; ai < archives.size(); ++ai)
            if (ai != ownArchive) order.push_back(ai);
        for (size_t mi = 0; mi < matRecs.size(); ++mi) {
            sr3geometry::MaterialBinding mb = lm.materialTextureBinding(mi, clView);
            const std::string* diffuse = mb.diffuse();
            if (diffuse == nullptr && !mb.textures.empty()) diffuse = &mb.textures.front().name;
            if (diffuse == nullptr) continue;
            ++prop.texturesWanted;
            for (size_t ai : order) {
                CityArchive& archive = *archives[ai];
                sr3render::UploadedTexture ut;
                std::string foundIn;
                if (!archive.texCache.built) {
                    std::printf("[tex] building TextureSearchCache for %s (one-time, shared by every cell)...\n",
                                archive.label.c_str());
                    std::fflush(stdout);
                }
                const bool wasBuilt = archive.texCache.built;
                const double t0 = cityNowMs();
                const bool found =
                    findAndUploadTexture(*archive.top, *diffuse, device, ut, foundIn, &archive.texCache);
                if (!wasBuilt && archive.texCache.built) {
                    archive.texCacheBuildMs = cityNowMs() - t0;
                    std::printf("[tex] TextureSearchCache for %s built: %zu texture records indexed in %.1f s\n",
                                archive.label.c_str(), archive.texCache.all.size(),
                                archive.texCacheBuildMs / 1000.0);
                }
                if (found) {
                    prop.perMaterialSrv[mi] = ut.srv;
                    ++prop.texturesFound;
                    if (ai != ownArchive) ++prop.texturesFoundElsewhere;
                    break;
                }
            }
        }
    }

    // gCursorAfterHeadChain() rounds the HEAD-chain sum up to 16 at this
    // HEAD -> MIDDLE handoff (level_mesh.h note 9) - a bare sum of
    // headBlocks' gLength() is wrong whenever that sum is not already
    // 16-aligned.
    std::vector<sr3mesh::MeshBlock> headBlocks = lm.resolveReferencedMeshes(clView, glView);
    size_t gCursor = sr3clmesh::LevelMesh::gCursorAfterHeadChain(headBlocks);
    const auto& rg = mid.renderGroups[0];
    const size_t headerDisplacement = ((rg.mesh.offset + 16 + 7) / 8 * 8) - rg.mesh.offset;
    sr3mesh::MeshBlock chosenMesh;
    try {
        chosenMesh = sr3mesh::MeshBlock::parse(clView, rg.mesh.offset, glView, gCursor, headerDisplacement);
    } catch (const std::exception& ex) {
        why = std::string("renderGroup[0] Mesh sub-block parse failed: ") + ex.what();
        return false;
    }
    if (!chosenMesh.drawGroupsLocated() || chosenMesh.drawGroups().empty()) {
        why = "renderGroup[0] has no located draw groups";
        return false;
    }
    const auto& drawGroup0 = chosenMesh.drawGroups()[0];
    std::map<size_t, std::vector<bool>> masksByChannel;
    for (size_t r = 0; r < drawGroup0.size(); ++r) {
        const size_t ch = drawGroup0[r].submeshIndex;
        auto it = masksByChannel.find(ch);
        if (it == masksByChannel.end()) it = masksByChannel.emplace(ch, std::vector<bool>(drawGroup0.size(), false)).first;
        it->second[r] = true;
    }
    std::string error;
    for (const auto& kv : masksByChannel) {
        if (kv.first >= chosenMesh.channels().size()) continue;
        auto r = std::make_unique<sr3render::MeshRenderer>();
        if (!r->initialise(device, error)) continue;
        if (!r->upload(device, chosenMesh, kv.first, 0, error, 0, nullptr, nullptr, &kv.second)) continue;
        const float* rmn = r->boundsMin();
        const float* rmx = r->boundsMax();
        for (int c = 0; c < 3; ++c) {
            if (rmn[c] < prop.localMin[c]) prop.localMin[c] = rmn[c];
            if (rmx[c] > prop.localMax[c]) prop.localMax[c] = rmx[c];
        }
        prop.renderers.push_back(std::move(r));
    }
    if (prop.renderers.empty()) {
        why = "no channel target could be uploaded" + (error.empty() ? std::string() : (": " + error));
        return false;
    }
    if (outCollisionTris != nullptr) {
        // Same channels/ranges the render loop just uploaded, decoded a
        // second time into plain CPU triangles (local space - the caller
        // translates into world space once the prop's own world matrix is
        // known, see BuildCollisionBvh() above). "Skip, don't fabricate" on
        // any individual failure, same discipline the render path already
        // uses just above.
        for (const auto& kv : masksByChannel) {
            if (kv.first >= chosenMesh.channels().size()) continue;
            std::vector<sr3mesh::Vertex> verts;
            try {
                verts = chosenMesh.decodeChannel(kv.first);
            } catch (const std::exception&) {
                continue;
            }
            for (size_t r = 0; r < drawGroup0.size(); ++r) {
                if (!kv.second[r]) continue;
                const std::vector<uint32_t> triList = chosenMesh.triangleListForRange(drawGroup0[r]);
                for (size_t i = 0; i + 2 < triList.size(); i += 3) {
                    const uint32_t ia = triList[i], ib = triList[i + 1], ic = triList[i + 2];
                    if (ia >= verts.size() || ib >= verts.size() || ic >= verts.size()) continue;
                    outCollisionTris->push_back(verts[ia].position);
                    outCollisionTris->push_back(verts[ib].position);
                    outCollisionTris->push_back(verts[ic].position);
                }
            }
        }
    }
    return true;
}


// A closed rectangle (plus two orientation ticks from its centre: a long one
// toward +x, a short one toward +z) as a MeshRenderer line list via
// uploadSkeleton() - parent chain p0 <- p1 <- ... so each consecutive pair
// becomes one segment. White lines (drawSkeleton()'s own fixed colour).
bool uploadCityOutline(sr3render::MeshRenderer& r, ID3D11Device* device, float cx, float cz, float halfX,
                       float halfZ, bool ticks, std::string& error) {
    std::vector<std::array<float, 3>> pos;
    std::vector<uint32_t> parents;
    const float y = 0.0f;
    const std::array<float, 3> corners[5] = {{cx - halfX, y, cz - halfZ},
                                             {cx + halfX, y, cz - halfZ},
                                             {cx + halfX, y, cz + halfZ},
                                             {cx - halfX, y, cz + halfZ},
                                             {cx - halfX, y, cz - halfZ}};
    for (int i = 0; i < 5; ++i) {
        pos.push_back(corners[i]);
        parents.push_back(i == 0 ? 0xFFFFFFFFu : static_cast<uint32_t>(pos.size() - 2));
    }
    if (ticks) {
        const uint32_t centre = static_cast<uint32_t>(pos.size());
        pos.push_back({cx, y, cz});
        parents.push_back(0xFFFFFFFFu);
        pos.push_back({cx + halfX * 0.6f, y, cz}); // long tick: +x
        parents.push_back(centre);
        pos.push_back({cx, y, cz + halfZ * 0.3f}); // short tick: +z
        parents.push_back(centre);
    }
    if (!r.initialise(device, error)) return false;
    return r.uploadSkeleton(device, pos, parents, error);
}


// Sec10.4 steps 1-3 for level 4: every registered cell whose 160 x 140 m box
// overlaps the focus box (focus +- window/2). Strict inequality: touching
// does not count.
// `windowScale` != 1 is the `--window-scale` DEBUG override (NOT the
// engine's rule) used only for wide overview captures.
std::vector<size_t> cityWantedCells(const std::vector<CityCell>& cells, float fx, float fz, float windowScale) {
    const float hx = kLevel4WindowX * 0.5f * windowScale;
    const float hz = kLevel4WindowZ * 0.5f * windowScale;
    std::vector<size_t> out;
    for (size_t i = 0; i < cells.size(); ++i) {
        const CityCell& c = cells[i];
        const bool ox = (fx - hx) < (c.centreX + kFineCellHalfX) && (fx + hx) > (c.centreX - kFineCellHalfX);
        const bool oz = (fz - hz) < (c.centreZ + kFineCellHalfZ) && (fz + hz) > (c.centreZ - kFineCellHalfZ);
        if (ox && oz) out.push_back(i);
    }
    return out;
}

} // namespace sr3viewer