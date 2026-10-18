#pragma once

#include "viewer_platform.h"
#include "viewer_city_support.h"
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
#include "vpp/container.h"

namespace sr3viewer {

// ---- real `.clmesh_pc` collision triangles + per-prop BVH (2026-10-08 real-
// collision follow-up task) - see ClmeshTriangleCollisionWorld's own doc
// comment, below runWalkDemo()'s doc block, for the full citation. One
// CollisionTriangle is WORLD-space (post the SAME pure-translation
// transform loadCell()'s own buildWorldMatrix call already applies to
// render bounds - see CityPropAabbCollisionWorld's own doc comment for why
// that transform is exactly a translation, confirmed at its call site).
struct CollisionTriangle {
    float v0[3], v1[3], v2[3];
};


// A minimal array-based BVH (CHOSEN acceleration structure - this task's own
// choice, explicitly NOT the real Havok MOPP tree the game itself builds
// over `.clmesh_pc`'s hkpExtendedMeshShape - per the orchestrator's own
// instruction, that binary tree format is out of scope; only the real
// triangle DATA has to be real). One BVH per prop, built once at load time
// over that prop's own real collision triangles (empty unless --collision -
// see loadCityProp()'s own `outCollisionTris` parameter). Binary, median-
// split over triangle centroids on the longest axis of each node's own AABB,
// leaf size 4 - an ordinary, well-understood construction, not a novel one.
struct CollisionBvhNode {
    float min[3] = {1e30f, 1e30f, 1e30f};
    float max[3] = {-1e30f, -1e30f, -1e30f};
    int left = -1, right = -1;        // -1/-1 = leaf
    int triStart = 0, triCount = 0;   // valid only at a leaf (left < 0)
};

struct CollisionBvh {
    std::vector<CollisionTriangle> tris;  // WORLD-space, reordered during the build below
    std::vector<CollisionBvhNode> nodes;
    int root = -1;
    bool empty() const { return tris.empty() || root < 0; }
};

CollisionBvh BuildCollisionBvh(const std::vector<std::array<float, 3>>& localTris, float tx, float ty, float tz);


// ---------------------------------------------------------------------------
// GROUND/TERRAIN (2026-10-07 follow-up task). ROOT CAUSE, confirmed against
// the real spec, NOT the previously-assumed "city_corner has no ground-plane
// mesh" (that claim was wrong, not just untested): spec-world-streaming.md
// Sec10.6(a) - the hN "Zone (High LOD)" fine-cell containers this file's
// `city`/`walk` streaming already loads (CityCell/loadCell above) hold ONLY
// `Level Mesh` entries (41,689/41,689, 0 exceptions - discrete props,
// `.clmesh_pc`) - never any terrain/ground entry. The REAL ground geometry
// for a tile lives one level UP, in the tile's own "Zone" container (engine
// level 3, the tile bundle this file's own pre-existing top comment already
// named as deliberately deferred: "levels 0 / 3 ... tile bundle ~L1 - only
// level 4 is streamed") - that same spec section/table: a tile record's
// entries additionally include "zone/zone-header/buffer entries" alongside
// its own Level Mesh (~L1) set - i.e. exactly the real `.czh_pc`/`.czn_pc`/
// `.gzn_pc` terrain trio (spec-terrain-format.md Sec1-4). So the ground was
// never "absent from the data" - this command simply never opened the one
// container kind that carries it. That container's own real geometry IS
// already decodable in this project: `sr3zone::ZoneGeometry::locate()` +
// `sr3mesh::MeshBlock` (used standalone by this file's own `zone` command,
// runZoneTile() above) is a real, tested, population-validated pipeline
// (tests/golden/zone_tile; spec-terrain-format.md's own citations of this
// project's "two independently-built tools" against 2,971 real files) - NOT
// a documented-but-unimplemented format. This section wires that SAME
// pipeline (the same union-ranges-across-every-group logic runZoneTile()
// already uses, not a new mechanism) into the city/walk streaming loop
// instead of its own standalone viewer. CORRECTED 2026-10-08: no longer
// "byte-for-byte the same auto-select-layout-code-0" as runZoneTile() -
// loadCityGroundTile()'s own channel auto-select now also accepts layout
// code 2 (spec-zone-data-format.md Sec13.2's real road-textured `0x2237`
// population; see loadCityGroundTile()'s own top comment for the full
// reasoning and why runZoneTile() itself is deliberately left unchanged).
//
// MaterialIdAsColour, not textured: runZoneTile()'s own doc comment records
// a genuine, checked dead end - the vehicle-style 0x424BD00D GeometryBlock
// texture-binding sub-header never occurs in any real `.czn_pc` (0/2,971
// files) - so zone/terrain geometry has no resolvable per-material texture
// in this project, same honest "flat colour, never invented" fallback used
// elsewhere. A flat-coloured REAL ground surface is still a real fix for the
// black/missing ground this task was about - inventing a textured ground
// plane when no real texture binding exists would be exactly the kind of
// fabrication this project's "no invented fixes" discipline forbids.
//
// Reconciled at TILE granularity (one entry per unique (col,row) among the
// currently-wanted hN cells - see reconcile()'s own ground section below),
// not per-cell: a tile's ground geometry is shared by all 4 of its hN cells.

// One real zone/terrain `.czn_pc` entry whose name prefix-matches one tile
// (see findTileZoneEntryName()'s own doc comment below for why a tile can
// legitimately have more than one of these, and what this project does
// about it).
struct TileZoneCandidate {
    std::string entryName;
    // "base"    - the tile's own per-tile Zone container (engine level 3),
    //             nested in that tile's own NNNN.str2_pc - the PRIMARY kind
    //             this file renders as ground, with "s_base" (below) as a
    //             CHOSEN fallback when it has nothing renderable (see
    //             DECISION below and loadCityGroundTile()'s own top
    //             comment).
    // "~al"     - the "always loaded" zone variant (nested in
    //             awld_compact.str2_pc, spec-zone-data-format.md Sec10.5).
    // "variant" - a named `^name` sub-area/state variant (the same `^name`
    //             convention hN cells use - see
    //             looksLikeHnVariantEntryName() above).
    // "other(<suffix>)" - a prefix match with a suffix shape not covered
    //             by the three cases above - recorded rather than
    //             silently dropped or guessed at.
    // "s_base"/"s_al"/"s_variant"/"s_other(<suffix>)" - the SAME four cases
    //             above, but matched against the separate "sr3_city~s<CC><RR>"
    //             prefix (2026-10-08, Team A spec-sync relay - see
    //             findTileZoneEntryName()'s own "~s FAMILY" doc-comment
    //             paragraph below for the real coordinate this was confirmed
    //             against and why it is tracked as its own family rather
    //             than folded into the four cases above).
    std::string kind;
};

bool findTileZoneEntryName(const vpp::Container& container, int col, int row, std::string& cznName,
                           std::vector<TileZoneCandidate>* candidates = nullptr);


struct CityGroundTile {
    int col = 0, row = 0;
    std::string cznName;   // empty if no real zone file was found for this tile
    size_t archiveIndex = 0;
    std::vector<std::unique_ptr<sr3render::MeshRenderer>> renderers; // empty = nothing real to draw
    size_t rangesRendered = 0;
    uint32_t triangles = 0;
    // Real per-material textures (2026-10-08 follow-up task - see
    // resolveGroundTextureBindings()'s own doc comment, above
    // tryRenderZoneEntry(), for the full citation and honest limitations).
    // Indexed by materialId, same convention as CityProp::perMaterialSrv;
    // EMPTY (never partially-sized-but-all-null) whenever nothing real
    // resolved for this tile - both ground draw call sites check `.empty()`
    // and keep calling the untouched MaterialIdAsColour draw() path in
    // that case, never drawTextured() with an empty vector, so a tile this
    // resolves nothing for stays byte-identical to before this task.
    std::vector<ID3D11ShaderResourceView*> perMaterialSrv;
    // Real `0x2237` ground-pavement collision triangles (street-collision-
    // 0x2237-mopp follow-up task, 2026-10-11 - spec-player-runtime.md
    // Sec5.6.5's "RESOLVED, 2026-10-11" finding / spec-zone-data-format.md
    // Sec13.6): the SAME world-space vertex positions/triangles the render
    // path above already decodes for this tile (tryRenderZoneEntry()'s own
    // `outCollisionTris` parameter, below) - the real engine's own
    // `0x00864c60` handler builds its Havok hkpExtendedMeshShape from
    // exactly this same per-record vertex/u16-index data, independent of
    // any separate MOPP-blob byte parse (see this project's own citation at
    // SampleGroundMeshHeight()'s doc comment for the full chain). Built
    // ONLY when loadCityGroundTile() is called with `buildCollision=true`
    // (runWalkDemo()'s --collision path, runPlaytest() unconditionally -
    // see those call sites); empty/unbuilt otherwise, so every
    // pre-existing caller (plain `city`/`walk` without --collision) is
    // completely unaffected, same "additive, zero-cost unless opted in"
    // discipline ClmeshTriangleCollisionWorld's own CityProp::collisionBvh
    // already uses for props.
    CollisionBvh collisionBvh;
};

bool groundTexDebugLog();

bool loadCityGroundTile(int col, int row, std::vector<std::unique_ptr<CityArchive>>& archives, ID3D11Device* device,
                        CityGroundTile& tile, std::string& why, bool buildCollision = false);

} // namespace sr3viewer
