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
#include "sr3mesh/mesh_block.h"
#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
#include "sr3zone/zone_objects.h"
#include "sr3zone/traffic_graph.h"
#include "sr3zone/hood_volumes.h"
#include "sr3zone/road_graph.h"
#include "sr3zone/zone_heightmap.h"
#include "sr3zone/zone_collision_mesh.h"
#include <cctype>
#include "vpp/container.h"

using namespace sr3viewer;

namespace {

// Recursive top-down build. `nodes` already holds a placeholder for this
// node (see BuildCollisionBvh() below for why the push happens before the
// recursive calls, not after) - this function fills it in and returns its
// own index, which is stable even though the vector keeps growing (later
// pushes may reallocate, but an `int` index survives that; only raw
// pointers/references into `nodes` taken before a later push would not).
int BuildCollisionBvhRange(std::vector<CollisionTriangle>& tris, std::vector<CollisionBvhNode>& nodes, int start,
                           int count) {
    const int myIndex = static_cast<int>(nodes.size());
    nodes.emplace_back();
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = start; i < start + count; ++i) {
        const CollisionTriangle& t = tris[static_cast<size_t>(i)];
        const float* verts[3] = {t.v0, t.v1, t.v2};
        for (const float* v : verts) {
            for (int c = 0; c < 3; ++c) {
                if (v[c] < mn[c]) mn[c] = v[c];
                if (v[c] > mx[c]) mx[c] = v[c];
            }
        }
    }
    for (int c = 0; c < 3; ++c) {
        nodes[static_cast<size_t>(myIndex)].min[c] = mn[c];
        nodes[static_cast<size_t>(myIndex)].max[c] = mx[c];
    }
    constexpr int kLeafSize = 4;
    if (count <= kLeafSize) {
        nodes[static_cast<size_t>(myIndex)].triStart = start;
        nodes[static_cast<size_t>(myIndex)].triCount = count;
        return myIndex;
    }
    int axis = 0;
    const float ext[3] = {mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]};
    if (ext[1] > ext[axis]) axis = 1;
    if (ext[2] > ext[axis]) axis = 2;
    std::sort(tris.begin() + start, tris.begin() + start + count,
              [axis](const CollisionTriangle& a, const CollisionTriangle& b) {
                  const float ca = (a.v0[axis] + a.v1[axis] + a.v2[axis]);
                  const float cb = (b.v0[axis] + b.v1[axis] + b.v2[axis]);
                  return ca < cb;
              });
    const int mid = start + count / 2;
    const int leftIdx = BuildCollisionBvhRange(tris, nodes, start, mid - start);
    const int rightIdx = BuildCollisionBvhRange(tris, nodes, mid, (start + count) - mid);
    nodes[static_cast<size_t>(myIndex)].left = leftIdx;
    nodes[static_cast<size_t>(myIndex)].right = rightIdx;
    return myIndex;
}

} // namespace

namespace sr3viewer {

// `localTris` is a flat local-space triangle soup (3 consecutive entries per
// triangle - exactly loadCityProp()'s own `outCollisionTris` shape);
// `tx,ty,tz` is the prop's own world translation (world[12]/[13]/[14] -
// pure translation, confirmed, see CityPropAabbCollisionWorld's own doc
// comment).
CollisionBvh BuildCollisionBvh(const std::vector<std::array<float, 3>>& localTris, float tx, float ty, float tz) {
    CollisionBvh bvh;
    const size_t triCount = localTris.size() / 3;
    bvh.tris.reserve(triCount);
    for (size_t i = 0; i < triCount; ++i) {
        CollisionTriangle t;
        const std::array<float, 3>& a = localTris[i * 3 + 0];
        const std::array<float, 3>& b = localTris[i * 3 + 1];
        const std::array<float, 3>& c = localTris[i * 3 + 2];
        t.v0[0] = a[0] + tx; t.v0[1] = a[1] + ty; t.v0[2] = a[2] + tz;
        t.v1[0] = b[0] + tx; t.v1[1] = b[1] + ty; t.v1[2] = b[2] + tz;
        t.v2[0] = c[0] + tx; t.v2[1] = c[1] + ty; t.v2[2] = c[2] + tz;
        bvh.tris.push_back(t);
    }
    if (!bvh.tris.empty()) bvh.root = BuildCollisionBvhRange(bvh.tris, bvh.nodes, 0, static_cast<int>(bvh.tris.size()));
    return bvh;
}


// Anchored prefix match (case-insensitive) for a tile's real zone/terrain
// `.czn_pc` entries - the SAME "sr3_city~f<CC><RR>" stem --lattice-check
// above already uses for the sibling `.czh_pc` (validated there to ~2m MAD
// against the real lattice). A prefix match (not exact-equals) so a named
// sub-area suffix - e.g. tests/golden/zone_tile's own
// "sr3_city~f0816~al.czn_pc" - is still found. Sets `cznName` to the chosen
// PRIMARY candidate (the tile's own "base" zone, if one exists anywhere in
// `container`'s tree - see DECISION below) and returns true iff one was
// found. When `candidates` is non-null, EVERY prefix match found anywhere
// in the tree - base included - is additionally appended there,
// classified, for the caller's own diagnostics.
//
// CORRECTED 2026-10-07 (ground/terrain follow-up task, same-day self-catch):
// this function originally scanned ONLY `container.entries()` at whatever
// level it was called with - i.e. assumed the entry is a literal TOP-LEVEL
// entry of the archive. That assumption was WRONG and is exactly why the
// first pass of this fix reported "no real ground data" for tiles that
// genuinely have it: runZoneTile()'s own usage text (a few hundred lines
// above this function) already says so explicitly - "<archive.vpp_pc> is
// whichever top-level archive holds the tile - findEntry() below already
// recurses into nested .str2_pc containers" - and findEntry() itself (this
// file's own shared helper, used by both --lattice-check for `.czh_pc` and
// by this function's own caller for the final `.czn_pc`/`.gzn_pc` byte read)
// recurses into every nested raw-payload sub-container, never just the
// top level. Direct empirical confirmation this pass: `--lattice-check`
// against sr3_city_0.vpp_pc alone finds real, populated `.czh_pc` headers
// for tiles 0916 (923 records), 1015 (1,246), 1016 (688) - the exact three
// tiles walk_demo's own city_corner focus wants - via THAT recursive search;
// this function's own original flat, non-recursive scan could not find
// their sibling `.czn_pc` for exactly that reason, not because the data is
// absent. Now mirrors findEntry()'s own recursion shape exactly (same
// `payload.kind != vpp::PayloadKind::Raw` guard, same `openNested()` +
// try/catch), so it can find a `.czn_pc` at any nesting depth a real
// shipped archive puts one at, matching what this project's own pre-
// existing, already-validated search already does for the sibling file.
//
// CORRECTED AGAIN 2026-10-08 (adversarial review follow-up): even after the
// fix above, this function still RETURNED on the first prefix match found
// anywhere in the tree - and a real tile's data carries MORE than one such
// match: the base zone (nested in that tile's own NNNN.str2_pc) and a
// separate "always loaded" `~al` variant (nested in awld_compact.str2_pc).
// Archive entry order always lands on the base container's DFS branch
// before awld_compact's, so the early return meant this function could
// NEVER even see an `~al` entry, for any tile - not that `~al` was
// considered and rejected, it was simply invisible. That made this
// project's own "ground genuinely absent" conclusion (this file's then-top
// comment, and tests/golden/walk_demo_v2/COMMAND.txt) an overstated claim:
// true that the BASE zone carries no real Mesh data for 0916/1015/1016
// (re-confirmed this pass - see loadCityGroundTile()'s own top comment),
// false that no real zone geometry exists there at all. Each of those 3
// tiles' `~al` zone DOES carry real, non-trivial Mesh sub-block data
// (re-derived this pass via this project's own already-validated
// sr3zone::ZoneGeometry::locate() + sr3mesh::MeshBlock pipeline, run
// directly against the real archives - not taken on anyone's word: 2 Mesh
// sub-blocks in each tile's `~al` zone; position-channel bounding boxes
// from a few metres up to (166 x 116 x 310)m / (326 x 81 x 299)m / (173 x
// 23 x 196)m, with vertical (Y) extents as large as 116m - far too TALL for
// a flat street-level ground surface, consistent with the "rooftops/
// window-grid/scattered-patch" world-level LOD-0 content this file's own
// pre-existing top comment already says is deliberately not streamed at
// this LOD, not a continuous walkable ground plane).
//
// DECISION (this pass, see the reviewer's own two suggested options - "list
// all prefix matches, or exclude ~al on purpose with a stated reason" - this
// takes BOTH, not one or the other): now ALWAYS recurses into every nested
// raw-payload container regardless of whether an earlier sibling already
// matched (the actual bug fix - see the loop below), so every real
// candidate is found and classified; but the chosen PRIMARY - the only
// thing `loadCityGroundTile()` ever renders as ground - stays the tile's
// own BASE zone, exactly as before this pass, NOT "whichever candidate
// carries the most Mesh data". Reasoning: `~al` is not a fallback copy of
// the SAME content, it is conceptually DIFFERENT per spec-zone-data-
// format.md Sec10.5 - a separately, always-resident world-level container,
// not this tile's own streamed-in terrain - and the measured SHAPE of its
// real geometry here (tall, building/rooftop-like, not flat) supports
// treating it as such rather than as "the tile's ground, just stored
// somewhere else". Silently promoting it to "ground" the moment the base
// zone is empty would draw a wrong-shaped surface with real-looking
// provenance - exactly the fabrication this project's "no invented fixes"
// discipline forbids, just moved one level down. So: `~al`/variant
// candidates are surfaced in the diagnostic text (loadCityGroundTile()'s
// own `why` message) so a reader is TOLD real data exists there instead of
// being told none does anywhere, and an explicit OPEN spec-sync question is
// left for Team A (see loadCityGroundTile()'s own top comment) rather than
// guessed at here - this project does not read the game executable to
// settle what `~al` geometry actually represents (standing rule).
//
// `~s` FAMILY (2026-10-08, Team A spec-sync relay, corroborated directly
// against this project's OWN already-committed spec docs this same pass -
// NOT re-derived from the executable here; this project never opens the
// real archive's raw bytes outside its own clean-room readers, standing
// rule): Team A opened sr3_city_1.vpp_pc directly and found that for
// coordinate 1312 the engine ships TWO SEPARATE top-level file families at
// the SAME tile coordinate, same nesting depth (inside that coordinate's
// own "1312.str2_pc", same pattern sr3_city_0.vpp_pc uses) -
// "sr3_city~f1312.czh_pc/.czn_pc" (`.czh_pc`+`.czn_pc` only, NO `.gzn_pc`
// companion at all) and "sr3_city~s1312.czh_pc/.czn_pc/.gzn_pc" (all three,
// CONFIRMED by Team A to carry real Mesh geometry). That single-coordinate
// relay turns out to match a MUCH larger already-confirmed population this
// project's own spec docs already carry, independent of this pass's own
// relay: `~f`/`~s` are the engine's own "fast"/"slow" zone-header split
// (spec-ctorless-types.md Sec8 item 3's name for it; CONFIRMED - disassembly
// re-derivation by the spec team, not this project - zone request type
// 44/29 for a `~f` name, 45/30 for a `~s` one). spec-world-streaming.md
// Sec10.7's own population count over ALL 1,265 real tile/sub-area/
// activity-zone headers in sr3_city_0/1 finds exactly 603 real `~s`
// ("slow") headers (vs. 248 `~f` ("fast") headers that carry placement
// records) - `~s` is not a one-off naming accident, it is the engine's
// OTHER, more numerous real header family. spec-zone-data-format.md's own
// Sec10.4 (`sr3_city~s0715`: a real 201,928-byte `.czn_pc` paired with a
// real, substantial 557,224-byte `.gzn_pc`) and Sec13.2 (the population-
// wide `0x2237` pavement survey's own two cited proof cases are BOTH `~s`
// tiles - `sr3_city~s1312` decodes to a real 31m x 140m x 0.55m flat
// horizontal slab at street height, `sr3_city~s1017` to a 31.5m x 3.6m x
// 23.3m block) independently show `~s` tiles carrying real, populated,
// flat/street-shaped Mesh geometry - consistent with, and considerably
// stronger than, Team A's own one-coordinate sample. This also lines up
// with what this project's OWN prior investigation already measured
// independently for 3 different tiles in sr3_city_0.vpp_pc (0916/1015/
// 1016, see loadCityGroundTile()'s own top comment and runWalkDemo()'s
// "CORRECTED 2026-10-08" paragraph): each tile's `~f` base `.czn_pc` is
// real and substantial (268-407 KB) but its `.gzn_pc` reads back 0 bytes -
// i.e. this project's own data already showed `~f` structurally lacking a
// real `.gzn_pc` companion, before this project had looked for an `~s`
// family of its own at all.
//
// So: `sr3_city~s<CC><RR>` is now searched here too, as a second anchored
// prefix alongside the pre-existing `~f` one, with the SAME suffix
// classification scheme (`~al`/`^variant`/other) applied independently -
// but every `~s`-family kind string is "s_"-prefixed (`s_base`/`s_al`/
// `s_variant`/`s_other(<suffix>)`) so it is NEVER confused with the
// pre-existing `~f`-family kind strings, and a tile with no `~s` match at
// all produces EXACTLY the same candidates/`cznName`/`foundBase` this
// function already produced before this pass (regression safety: `~al`/
// variant/other classification under the `~f` family is completely
// unaffected by this addition - verified directly, see this task's own
// report). `cznName`/`foundBase` (the single PRIMARY this function itself
// resolves) still refer ONLY to the `~f` family's own "base" (no-suffix)
// match, exactly as before - the `~s` family's own "s_base" candidate is
// surfaced through `candidates` like every other non-`~f`-base kind, and it
// is loadCityGroundTile() (NOT this function) that decides whether/when to
// actually try rendering it - see that function's own top comment for the
// CHOSEN fallback-priority decision and why it differs from the `~al`
// DECISION above.
bool findTileZoneEntryName(const vpp::Container& container, int col, int row, std::string& cznName,
                           std::vector<TileZoneCandidate>* candidates) {
    char prefixF[32], prefixS[32];
    std::snprintf(prefixF, sizeof(prefixF), "sr3_city~f%02d%02d", col, row);
    std::snprintf(prefixS, sizeof(prefixS), "sr3_city~s%02d%02d", col, row);
    const std::string prefixFLower = toLowerLocal(std::string(prefixF));
    const std::string prefixSLower = toLowerLocal(std::string(prefixS));
    bool foundBase = false;
    for (const auto& e : container.entries()) {
        if (!endsWithNoCase(e.name, ".czn_pc")) continue;
        const std::string nameLower = toLowerLocal(e.name);
        // Family dispatch - exactly one of the two anchored prefixes can
        // match (they differ at a fixed character position, 'f' vs 's'),
        // never both. +7 = ".czn_pc", avoids an overlap underflow below.
        bool isF = false, isS = false;
        if (nameLower.size() >= prefixFLower.size() + 7 &&
            nameLower.compare(0, prefixFLower.size(), prefixFLower) == 0) {
            isF = true;
        } else if (nameLower.size() >= prefixSLower.size() + 7 &&
                   nameLower.compare(0, prefixSLower.size(), prefixSLower) == 0) {
            isS = true;
        } else {
            continue;
        }
        const size_t prefixLen = isF ? prefixFLower.size() : prefixSLower.size();
        const std::string suffix = nameLower.substr(prefixLen, nameLower.size() - prefixLen - 7);
        std::string kind;
        if (isF) {
            kind = "base";
            if (suffix == "~al") kind = "~al";
            else if (!suffix.empty() && suffix[0] == '^') kind = "variant";
            else if (!suffix.empty()) kind = "other(" + suffix + ")";
        } else { // isS
            kind = "s_base";
            if (suffix == "~al") kind = "s_al";
            else if (!suffix.empty() && suffix[0] == '^') kind = "s_variant";
            else if (!suffix.empty()) kind = "s_other(" + suffix + ")";
        }
        if (candidates) candidates->push_back(TileZoneCandidate{e.name, kind});
        if (isF && kind == "base" && !foundBase) {
            cznName = e.name;
            foundBase = true;
        }
    }
    // Always recurse into EVERY nested raw-payload container - never
    // return/stop at the first success (THE fix: see this function's own
    // "CORRECTED AGAIN 2026-10-08" paragraph above) - so a sibling
    // container reached later in archive order (e.g. awld_compact.str2_pc,
    // after this tile's own NNNN.str2_pc) holding a DIFFERENT prefix match
    // is still found and classified, not silently skipped once the base
    // has already been seen.
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            vpp::Container nested = container.openNested(i);
            std::string nestedPrimary;
            if (findTileZoneEntryName(nested, col, row, nestedPrimary, candidates) && !foundBase) {
                cznName = nestedPrimary;
                foundBase = true;
            }
        } catch (const std::exception&) {
        }
    }
    return foundBase;
}

} // namespace sr3viewer

namespace {

// Diagnostic-only (never affects what is rendered): re-derives how many
// real Mesh sub-blocks a NON-chosen candidate entry carries, via the SAME
// findEntry()+ZoneGeometry::locate() pipeline this function already uses
// for its own chosen entry, so loadCityGroundTile()'s own `why` message can
// honestly say what exists there instead of staying silent about it. Empty
// `outWhy` on success; on any read/parse failure, describes that failure
// instead of a block count, rather than fabricating a number.
void describeZoneCandidateBlockCount(const vpp::Container& top, const std::string& entryName, size_t& outBlockCount,
                                     std::string& outWhy) {
    std::string gznName = entryName;
    {
        const size_t dot = gznName.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < gznName.size()) gznName[dot + 1] = 'g';
    }
    std::vector<uint8_t> cznBytes, gznBytes;
    if (!findEntry(top, entryName, cznBytes, gznBytes, gznName) || cznBytes.empty()) {
        outWhy = "could not read czn/gzn bytes";
        return;
    }
    try {
        outBlockCount = sr3zone::ZoneGeometry::locate(vpp::ByteView(cznBytes.data(), cznBytes.size()),
                                                      vpp::ByteView(gznBytes.data(), gznBytes.size()))
                            .size();
    } catch (const std::exception& ex) {
        outWhy = std::string("zone geometry locate failed: ") + ex.what();
    }
}

} // namespace

namespace sr3viewer {

// Loads ONE tile's real ground/terrain geometry - reuses runZoneTile()'s own
// already-proven pipeline (see this section's own top comment): locate() the
// real Mesh sub-block(s), auto-select the first channel whose layout code is
// POSITION-DECODABLE (0, 2 or 4 - see below), then union that channel's own
// draw ranges across EVERY located LOD group (not just group 0 -
// runZoneTile()'s own doc comment measured that a channel's ranges can be
// split, gap-free, across more than one group).
//
// CORRECTED 2026-10-08 (spec-zone-data-format.md Sec13.2 spec-sync task):
// this used to accept ONLY layoutCode == 0 - a real bug, not merely a
// narrow choice. Sec13.2's population-wide decode of the real `0x2237`
// ("WE geom") road/sidewalk/pavement records that DO carry real per-channel
// vertex data (359/374 road-textured samples read as flat horizontal slabs)
// finds that population living on **layout codes 2 and 4** - never code 0 -
// so a reader that only ever looks for code 0 silently treats every real
// code-2/4 ground channel exactly like "no ground here", which is a false
// negative, not a true absence. Codes 0, 2 and 4 are the three layouts
// spec-vertex-format.md Sec5/Sec6.1 CONFIRM carry a plain decodable
// `float3` position at channel offset 0 (code 0: position+normal; code 2:
// +tangent; code 4: position only) - sr3mesh::MeshBlock::decodeChannel()
// already decodes all three correctly (position is read from the channel's
// own per-vertex array, never from the index buffer - see mesh_block.h's
// own file-level comment on the index buffer being a SEPARATE structure;
// no "primary buffer is actually the index buffer" confusion exists in this
// reader, that bug was specific to the spec team's own one-off survey
// tooling, not this shared, independently-validated reader). Preference
// order below is 0 or 2 FIRST, 4 only as a fallback: codes 0/2 carry a real
// decoded normal (confirmed unit-length, spec-vertex-format.md Sec6.2),
// while code 4 is position-only (no normal field at all - Sec6's own field
// table), and this file's shared pixel shader computes
// `normalize(i.normal)` for every lit draw mode; feeding it a code-4
// channel's structurally-absent (all-zero) normal would compute
// normalize(0,0,0), a genuine new degenerate-shading risk this fix must not
// introduce while fixing the false-negative above. A code-4-only match is
// therefore reported via `why` (so it is never silently dropped, same
// honest-diagnostic treatment the pre-existing `~al`/variant candidates
// already get below) rather than rendered, pending a normal-safe shading
// path for position-only channels - see the `resolvedBlock < 0` branch
// below for the exact message.
//
// Searches every given archive in order (a tile's czn/gzn need not sit in
// the same archive as its hN cells - the same multi-archive fallback
// loadCityProp() already uses for textures). Returns false with `why` set
// when there is genuinely no real zone file for this tile in any given
// archive, or any other real parse/upload failure - never fabricates a
// stand-in plane.
//
// RENDERS THE TILE'S OWN "base" (`~f`) ZONE WHEN IT HAS REAL, RENDERABLE
// DATA, ELSE FALLS BACK TO THE "s_base" (`~s`) ZONE (see
// findTileZoneEntryName()'s own "DECISION" paragraph for why an `~al`/
// variant candidate is STILL deliberately never promoted to ground here,
// even when it carries real Mesh data and the base zone does not - that
// reasoning is UNCHANGED by this pass). CHOSEN 2026-10-08 (Team A spec-sync
// relay, see findTileZoneEntryName()'s own "~s FAMILY" doc-comment
// paragraph for the confirmed coordinate this is based on): unlike `~al`,
// the `~s` family is treated as a second, PARALLEL per-coordinate "base"
// zone - same engine tier as `~f`, not a conceptually different
// always-loaded/world-LOD container - because Team A's relayed finding
// (and this project's own independently-measured 0-byte `~f` `.gzn_pc`
// reads for 0916/1015/1016, see this function's own "CORRECTED 2026-10-08"
// paragraph above) point to `~s` being the SAME tile's real street-level
// terrain, just shipped under a naming convention this loader previously
// never searched - not a different-shaped world-LOD container the way
// `~al`'s own measured (tall, building-like) geometry is. So: `~f` is
// still tried FIRST, in the same order as before this pass (a tile whose
// `~f` zone already renders real ground is completely unaffected); `~s`
// is tried ONLY when `~f` produces nothing renderable (absent entirely, 0
// Mesh sub-blocks located, or no layout-code-0/2/4 channel) - see
// tryRenderZoneEntry() below, shared by both attempts. This priority rule
// is CHOSEN (a judgement call this task made, not itself something the
// engine states), but it rests on more than ONE sample: besides Team A's
// own checked coordinate (1312), this project's own already-committed spec
// docs independently confirm `~f`/`~s` are the engine's real "fast"/"slow"
// zone-header split (spec-ctorless-types.md Sec8 item 3, spec-world-
// streaming.md Sec10.7 - 603 real `~s` headers vs. 248 `~f` ones with
// placement records, over all 1,265 tile/sub-area/activity-zone headers in
// sr3_city_0/1) and separately show real, populated, flat/street-shaped
// Mesh geometry specifically on `~s` tiles (spec-zone-data-format.md
// Sec10.4's `sr3_city~s0715`, Sec13.2's `sr3_city~s1312`/`sr3_city~s1017`
// proof cases) - population-wide corroboration, not just the one
// coordinate. Still: if a future tile is found where `~f` AND `~s` coexist
// with real, DIFFERENT populated Mesh data (not simply "`~f` empty, `~s`
// populated"), that would contradict the simple relationship assumed here
// and should be reported honestly rather than silently papered over; this
// pass's own verification sweep (see this task's own report) did not find
// such a tile, but only sampled what the given archives/golden scenes
// reach. Always asks findTileZoneEntryName() for the FULL candidate list
// too, so a failure's own `why` message can honestly report what else this
// tile's data carries instead of implying nothing does.
// Attempts to load+render ONE already-identified real zone entry's ground
// geometry: read its czn/gzn bytes, locate() Mesh sub-block(s), auto-select
// a position-decodable channel (code 0/2 preferred, code 4 detected but
// deliberately never rendered - see loadCityGroundTile()'s own top comment
// for the full reasoning), then union that channel's draw ranges across
// every located LOD group and upload them. Factored out (2026-10-08, `~s`
// family addition) so loadCityGroundTile() can run the EXACT SAME pipeline
// against either the tile's `~f` base entry or, as a fallback, its `~s`
// base entry, without duplicating ~150 lines of locate/channel-select/
// render logic twice - this is a mechanical extraction of what was
// previously loadCityGroundTile()'s own inline per-archive body, not a
// behaviour change for the `~f`-only path (verified directly via the
// existing golden-scene byte-exact stdout/PNG comparisons, which would
// catch any accidental difference).
//
// On success: sets tile.cznName/tile.renderers/tile.rangesRendered/
// tile.triangles and returns true (NOT tile.archiveIndex - the caller,
// which knows the archive index `ai`, sets that itself). On any real
// failure: sets `why` to a detailed, honest reason (never touches `tile`)
// and returns false - never fabricates a stand-in plane. `outBytesUnreadable`,
// when given, is set true ONLY for the "could not read czn/gzn bytes"
// failure (distinct from every other failure reason below) - purely so
// loadCityGroundTile() can reproduce its OWN pre-existing (pre-`~s`)
// per-archive control flow exactly for a tile with no `~s` candidate: that
// one specific failure used to mean "try the next given archive", every
// other failure reason here was already terminal for the tile. See
// loadCityGroundTile()'s own call site for why that distinction still
// matters post-`~s`.
//
// ---------------------------------------------------------------------------
// GROUND TEXTURE RESOLUTION (2026-10-08 follow-up task - "real road/sidewalk
// textures instead of the MaterialIdAsColour debug ribbon/slabs"). ROOT
// CAUSE of the prior dead end: this function's own doc comment chain above
// (runZoneTile()'s "MATERIAL/TEXTURE BINDING - investigated, genuine dead
// end" paragraph) checked exactly TWO candidate texture-binding mechanisms
// for `.czn_pc` - the vehicle-style `0x424BD00D` GeometryBlock sub-header
// (material_binding.h, 0/2,971 real files) and `.czh_pc`'s OWN shared
// MaterialBlock name table (sr3zone::ZoneHeader::materialBlock(), 71/71
// non-texture names in the file checked) - and both are genuine, measured
// negatives. Neither check ever looked at `.czn_pc`'s OWN body, where
// spec-zone-data-format.md Sec13.2 separately found (population-wide,
// 374/2,862 real files) that a real `.tga` texture-name pool lives inside
// the `0x2237` ("WE geom") content-kind record itself.
//
// EMPIRICALLY LOCATED THIS SESSION (tools/zone_texture_probe.cpp, a
// throwaway investigation tool kept in-tree per this project's own
// practice - see its own top comment for the full method): opening
// `sr3_city~s0916~al.czn_pc` and `sr3_city~s0916.czn_pc` directly (via this
// project's own vpp::Container, NOT the executable) shows the real on-disk
// shape - a `{id=0x2237, length}` chain record (high bit set on id, the
// SAME top-level `.czn_pc` record convention sr3zone::zone_header.h's own
// file comment already names and flags as unread anywhere in this project
// - still not generally decoded here, only this one id's own body is used
// empirically) whose body is, in order: a small fixed head (a mesh-ref
// count that exactly matches sr3zone::ZoneGeometry::locate()'s own located
// block count - 1 for the `~al` file, 172 for the base file - confirming
// this IS the same record those Mesh sub-blocks belong to), a shared ASCII
// name blob (mesh-ref/region object names like "AL mesh 1/1"/"Street
// 010<001>" immediately followed, in the `~al` file, by real `.tga` names -
// "roads_combined_detail_n.tga", "sidewalk_01_d_co.tga", ... - 75 real
// names recovered from the base file's own larger blob too), THEN the
// `0x70`-byte mesh-ref array (spec-zone-data-format.md Sec13.1), THEN the
// embedded Mesh sub-block(s) this function already locates. Measured
// directly: in both real files checked, EVERY `.tga` name found lands at a
// strictly lower byte offset than the first located Mesh sub-block - so
// scanning `[0, firstMeshBlockOffset)` for NUL-terminated, printable-ASCII
// runs ending in `.tga` (case-insensitive - findTgaNamesBefore() below)
// recovers the same real name pool Sec13.2's own population survey already
// found and validated (374/374 real files decoded correct, geometrically
// real road/sidewalk slabs from the SAME kind of scan), not a fresh guess.
//
// WHAT IS NOT DECODED, STATED HONESTLY: the `0x70`-byte mesh-ref record's
// own per-record field layout - which would give an EXACT materialId ->
// name-pool-offset binding, the same shape sr3clmesh::LevelMesh's
// materialTextureBinding() / sr3tree::Tree's Material::textureBindings
// already decode for THEIR OWN carriers - is OPEN. spec-zone-data-
// format.md Sec13.1 confirms only WHICH five low-level primitives build it
// (`0xe718b0` mesh-ref, `0xe401d0` index-list, `0xe71090` material - the
// SAME shared per-material-record parser clmesh/tree/vehicle/character/
// foliage already use - `0xe5e240`/`0xe5e220` material-allocate,
// `0xe3fcb0`), and spec-terrain-format.md's own review status flags the
// exact callers/class as "NEEDS-EXE" - genuinely untraced, not a Team-B
// clean-room gap closeable by reading further committed spec text (Team B
// never disassembles the executable - see this task's own report).
//
// Absent that exact binding, resolveGroundTextureBindings() below applies
// a CHOSEN, clearly-labelled heuristic instead of inventing a fake decode:
// every pool name that does NOT look like a known non-diffuse map by
// filename suffix (`_n`/`_r`/`_sp`/`_ref`/`_sb` - the SAME normal/
// roughness/specular/reflection/subsurface suffix family this project's
// own material_binding.h already confirms BY PARAMETER HASH for other
// carriers; read here by filename instead, since no parameter hash exists
// for this carrier) becomes a "diffuse candidate", in file order, and
// candidates are assigned POSITIONALLY to the resolved ground channel's
// own ascending distinct materialId values (rank 0 -> the lowest real
// materialId, etc.). HIGH CONFIDENCE for the common case Sec13.2's own
// population finding describes (a flat slab with few real materials - the
// `~al` file above is the clean, single-mesh-ref limit of this); markedly
// less certain for a tile whose pool mixes many unrelated mesh-refs' own
// textures (e.g. the 172-mesh-ref base file) - not hidden, labelled here.
// A materialId with no candidate left for its rank, or whose candidate
// name fails to resolve through findAndUploadTexture() (the SAME archive-
// walking mechanism loadCityProp()/runClmesh() already use successfully -
// reused, not reinvented), simply stays unresolved: drawTextured()'s own
// pre-existing per-material fallback (a flat materialId colour, NEVER a
// neighbour's or a fabricated texture) covers it, byte-identical to the
// old MaterialIdAsColour-only behaviour for that one material slot.
//
// REGRESSION SAFETY: `texCache` defaults to nullptr, so every pre-existing
// caller of tryRenderZoneEntry() (there are none outside this file) is
// unaffected; loadCityGroundTile() below is the only caller and always
// passes its own archive's cache. When nothing resolves for a tile (no
// `.tga` names found, or none upload), tile.perMaterialSrv stays EMPTY and
// both ground draw call sites (runCity()/runWalkDemo()) keep calling the
// untouched `draw(..., MaterialIdAsColour, nullptr)` path for that tile -
// not drawTextured() with an empty vector - so a tile this resolves
// nothing new for renders byte-identical pixels to before this task.
// SR3_GROUND_TEX_DEBUG=1 (env var, unset/0 = silent, default): shared by
// every ground-texture diagnostic print in this file (same convention as
// SR3_ANIM_DEBUG_LOG elsewhere) - never stdout, so it cannot affect any
// golden scene's stdout comparison. A function-local static so the env var
// is read once regardless of how many call sites check it.
bool groundTexDebugLog() {
    static const bool v = []() {
        const char* e = std::getenv("SR3_GROUND_TEX_DEBUG");
        return e != nullptr && e[0] != '0' && e[0] != '\0';
    }();
    return v;
}

} // namespace sr3viewer

namespace {

std::vector<std::string> findTgaNamesBefore(const std::vector<uint8_t>& buf, size_t limit) {
    std::vector<std::string> out;
    const size_t scanLimit = (std::min)(limit, buf.size());
    size_t runStart = std::string::npos;
    for (size_t i = 0; i <= scanLimit; ++i) {
        const bool printable = i < scanLimit && buf[i] >= 0x20 && buf[i] < 0x7f;
        if (printable) {
            if (runStart == std::string::npos) runStart = i;
        } else {
            if (runStart != std::string::npos) {
                const size_t len = i - runStart;
                if (len >= 5) {
                    std::string s(reinterpret_cast<const char*>(buf.data() + runStart), len);
                    if (endsWithNoCase(s, ".tga")) out.push_back(std::move(s));
                }
                runStart = std::string::npos;
            }
        }
    }
    return out;
}


// Known non-diffuse map suffixes (normal/roughness/specular/reflection/
// subsurface), by filename - see this section's own top comment for why
// filename, not parameter hash, is the only option available for this
// carrier. `nameLower` must already be lower-cased and end in ".tga".
bool looksLikeNonDiffuseMapName(const std::string& nameLower) {
    if (nameLower.size() < 4) return false;
    const std::string stem = nameLower.substr(0, nameLower.size() - 4); // strip ".tga"
    static const char* const kNonDiffuseTails[] = {"_n", "_r", "_sp", "_ref", "_sb"};
    for (const char* tail : kNonDiffuseTails) {
        const size_t tailLen = std::strlen(tail);
        if (stem.size() > tailLen && stem.compare(stem.size() - tailLen, tailLen, tail) == 0) return true;
    }
    return false;
}


// Builds tile.perMaterialSrv for the resolved ground channel, per this
// section's own top comment - never throws, leaves tile.perMaterialSrv
// untouched (caller sees it stay empty) on any failure to find/resolve
// anything. `blocks` is tryRenderZoneEntry()'s own located block list
// (used only to find the lowest real Mesh-sub-block offset, the pool's own
// upper bound); `mesh`/`channelIndex` identify the resolved ground
// channel's own draw ranges, whose distinct materialIds this assigns
// candidates to.
void resolveGroundTextureBindings(const vpp::Container& top, const std::vector<uint8_t>& cznBytes,
                                  const std::vector<sr3zone::ZoneMeshBlockEntry>& blocks,
                                  const std::vector<std::pair<const sr3mesh::MeshBlock*, size_t>>& meshes,
                                  ID3D11Device* device, TextureSearchCache* texCache, CityGroundTile& tile) {
    if (texCache == nullptr || meshes.empty()) return;
    size_t firstBlockOffset = cznBytes.size();
    for (const auto& b : blocks) firstBlockOffset = (std::min)(firstBlockOffset, b.cznOffset);
    const std::vector<std::string> pool = findTgaNamesBefore(cznBytes, firstBlockOffset);
    std::vector<std::string> diffuseCandidates;
    for (const auto& n : pool) {
        if (!looksLikeNonDiffuseMapName(toLowerLocal(n))) diffuseCandidates.push_back(n);
    }
    if (diffuseCandidates.empty()) return;

    std::set<uint32_t> materialIds;
    for (const auto& mc : meshes) {
        if (!mc.first->drawGroupsLocated()) continue;
        for (const auto& group : mc.first->drawGroups())
            for (const auto& dr : group)
                if (dr.submeshIndex == mc.second) materialIds.insert(dr.materialId);
    }
    if (materialIds.empty()) return;

    // SR3_GROUND_TEX_DEBUG=1: dumps the exact rank->materialId->name->
    // resolved mapping to stderr - see groundTexDebugLog()'s own comment.
    const bool debugLog = groundTexDebugLog();

    std::vector<ID3D11ShaderResourceView*> perMaterial(static_cast<size_t>(*materialIds.rbegin()) + 1, nullptr);
    size_t rank = 0, resolvedCount = 0;
    for (uint32_t materialId : materialIds) {
        if (rank < diffuseCandidates.size()) {
            sr3render::UploadedTexture ut;
            std::string foundIn;
            const bool found = findAndUploadTexture(top, diffuseCandidates[rank], device, ut, foundIn, texCache);
            if (found) {
                perMaterial[materialId] = ut.srv;
                ++resolvedCount;
            }
            if (debugLog) {
                std::fprintf(stderr, "[groundtex] materialId=%u rank=%zu candidate=\"%s\" %s\n", materialId, rank,
                            diffuseCandidates[rank].c_str(), found ? ("FOUND in " + foundIn).c_str() : "NOT FOUND");
            }
        } else if (debugLog) {
            std::fprintf(stderr, "[groundtex] materialId=%u rank=%zu: no candidate left (pool has %zu)\n",
                        materialId, rank, diffuseCandidates.size());
        }
        ++rank;
    }
    if (resolvedCount > 0) tile.perMaterialSrv = std::move(perMaterial);
}


// `outCollisionTris` (street-collision-0x2237-mopp follow-up task,
// 2026-10-11): when non-null, ALSO decodes every uploaded channel's real
// WORLD-space vertex positions (3 consecutive entries per triangle - the
// SAME `outCollisionTris` shape loadCityProp() already uses for
// `.clmesh_pc`, just with no local->world translation needed here - this
// content kind's own decoded positions are already world-space, see this
// function's own `tile.renderers.push_back` loop above's "world Y 18-31"
// doc-comment note confirming that directly) via decodeChannel()/
// triangleListForRange() - the identical data MeshRenderer::upload() just
// sent to the GPU for rendering, read a second time into plain CPU
// triangles, exactly mirroring loadCityProp()'s own established pattern.
// Defaults to nullptr, which is every pre-existing caller, so this costs
// nothing and changes nothing for them.
bool tryRenderZoneEntry(const vpp::Container& top, const std::string& cznName, ID3D11Device* device,
                        CityGroundTile& tile, std::string& why, bool* outBytesUnreadable = nullptr,
                        TextureSearchCache* texCache = nullptr,
                        std::vector<std::array<float, 3>>* outCollisionTris = nullptr, bool allBlocks = false) {
    if (outBytesUnreadable) *outBytesUnreadable = false;
    std::string gznName = cznName;
    {
        const size_t dot = gznName.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < gznName.size()) gznName[dot + 1] = 'g';
    }
    std::vector<uint8_t> cznBytes, gznBytes;
    if (!findEntry(top, cznName, cznBytes, gznBytes, gznName) || cznBytes.empty()) {
        why = cznName + ": could not read czn/gzn bytes";
        if (outBytesUnreadable) *outBytesUnreadable = true;
        return false;
    }
    std::vector<sr3zone::ZoneMeshBlockEntry> blocks;
    try {
        blocks = sr3zone::ZoneGeometry::locate(vpp::ByteView(cznBytes.data(), cznBytes.size()),
                                               vpp::ByteView(gznBytes.data(), gznBytes.size()));
    } catch (const std::exception& ex) {
        why = cznName + ": zone geometry locate failed: " + std::string(ex.what());
        return false;
    }
    // CORRECTED 2026-10-08 (spec-zone-data-format.md Sec13.2 spec-sync
    // task - see loadCityGroundTile()'s own top comment for the full
    // account): two passes, not one. Pass 1 looks for layout code 0 or 2 -
    // both CONFIRMED (spec-vertex-format.md Sec6.2) to carry a real decoded
    // normal, so they are safe for this file's shared lit shader. Pass
    // 2, only if pass 1 found nothing, looks for layout code 4
    // (position-only, Sec5/Sec6.1) PURELY so a code-4-only tile is
    // reported accurately in `why` below instead of being folded into
    // the generic "no block here at all"/"none carries ..." messages -
    // it is deliberately never assigned to resolvedBlock/resolvedChannel
    // (never rendered), since this shader's `normalize(i.normal)` would
    // be fed a structurally-absent (all-zero) normal for a code-4
    // channel otherwise - a new degenerate-shading bug this fix must
    // not trade the false-negative for.
    int resolvedBlock = -1, resolvedChannel = -1;
    int code4Block = -1, code4Channel = -1;
    for (size_t bi = 0; bi < blocks.size() && resolvedBlock < 0; ++bi) {
        const auto& channels = blocks[bi].block.channels();
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            if (channels[ci].layoutCode == 0 || channels[ci].layoutCode == 2) {
                resolvedBlock = static_cast<int>(bi);
                resolvedChannel = static_cast<int>(ci);
                break;
            }
        }
    }
    if (resolvedBlock < 0) {
        for (size_t bi = 0; bi < blocks.size() && code4Block < 0; ++bi) {
            const auto& channels = blocks[bi].block.channels();
            for (size_t ci = 0; ci < channels.size(); ++ci) {
                if (channels[ci].layoutCode == 4) {
                    code4Block = static_cast<int>(bi);
                    code4Channel = static_cast<int>(ci);
                    break;
                }
            }
        }
    }
    if (resolvedBlock < 0) {
        // CORRECTED 2026-10-08 (adversarial review follow-up): this
        // used to say "no block carries a vertex-layout-code-0
        // channel" UNCONDITIONALLY, including when `blocks` is empty -
        // i.e. 0 blocks located at all, a different and more basic
        // condition than "blocks were found but none carry that
        // channel". Say which one actually happened.
        if (blocks.empty()) {
            why = cznName + ": 0 Mesh sub-block(s) located (not merely \"no layout-code-0/2 channel\" - "
                             "there is no block here at all)";
        } else if (code4Block >= 0) {
            // A real, position-decodable (layout code 4) ground channel
            // DOES exist here - honestly say so, rather than reporting
            // it exactly like a true absence (see this function's own
            // top comment for why it is not rendered as-is).
            why = cznName + ": " + std::to_string(blocks.size()) +
                  " Mesh sub-block(s) located; block " + std::to_string(code4Block) + " channel " +
                  std::to_string(code4Channel) + " is a real layout-code-4 (position-only, no normal) "
                  "ground channel - NOT rendered (would need a normal-safe shading path this task "
                  "deliberately does not add - see loadCityGroundTile()'s own top comment)";
        } else {
            why = cznName + ": " + std::to_string(blocks.size()) +
                  " Mesh sub-block(s) located but none carries a vertex-layout-code-0/2/4 channel";
        }
        return false;
    }
    const sr3mesh::MeshBlock& mesh = blocks[static_cast<size_t>(resolvedBlock)].block;
    const size_t channelIndex = static_cast<size_t>(resolvedChannel);

    // Real texture resolution (see this function's own "GROUND TEXTURE
    // RESOLUTION" doc comment above for the full citation) - purely
    // additive: on any failure to find/resolve a real name,
    // tile.perMaterialSrv stays empty and nothing below is affected.
    // allBlocks (Team C, playtest / sr3.exe) - spec-zone-data-format.md 13.5.2 port recipe: every Mesh
    // sub-block and every layout-0/2 channel a draw range uses is ground, drawn at identity; near-origin
    // local-space blocks are skipped until their placement is specified (spec CHOSEN rule: bounds within
    // +-50 m of the origin while the tile is elsewhere) - which includes the first block the original
    // first-block path draws (in ~s0915 block #0 is such a template). Material ids share the one rank
    // mapping above. Off for every golden-scene command (unchanged).
    auto nearOrigin = [](const sr3mesh::MeshBlock& m, size_t ch) {
        try {
            for (const auto& v : m.decodeChannel(ch))
                if (std::fabs(v.position[0]) > 50.0f || std::fabs(v.position[2]) > 50.0f) return false;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    std::vector<std::pair<const sr3mesh::MeshBlock*, size_t>> drawn = {{&mesh, channelIndex}};
    bool skipPrimary = false;
    if (allBlocks) {
        skipPrimary = nearOrigin(mesh, channelIndex);
        if (skipPrimary) drawn.clear();
        for (size_t bi = 0; bi < blocks.size(); ++bi) {
            const sr3mesh::MeshBlock& m = blocks[bi].block;
            const auto& channels = m.channels();
            std::set<size_t> used;
            if (m.drawGroupsLocated()) {
                for (const auto& g : m.drawGroups())
                    for (const auto& r : g) used.insert(r.submeshIndex);
            } else {
                for (size_t ci = 0; ci < channels.size(); ++ci) used.insert(ci);
            }
            for (size_t ci : used) {
                if (ci >= channels.size() || (channels[ci].layoutCode != 0 && channels[ci].layoutCode != 2)) continue;
                if (static_cast<int>(bi) == resolvedBlock && ci == channelIndex) continue;  // the primary, above
                if (nearOrigin(m, ci)) continue;
                drawn.push_back({&m, ci});
            }
        }
    }
    resolveGroundTextureBindings(top, cznBytes, blocks, drawn, device, texCache, tile);

    struct GroupTarget {
        size_t groupIndex = 0;
        std::vector<bool> rangeMask;
        size_t rangesForThisChannel = 0;
    };
    std::vector<GroupTarget> targets;
    if (mesh.drawGroupsLocated()) {
        for (size_t gi = 0; gi < mesh.drawGroups().size(); ++gi) {
            const auto& group = mesh.drawGroups()[gi];
            GroupTarget t;
            t.groupIndex = gi;
            t.rangeMask.assign(group.size(), false);
            for (size_t r = 0; r < group.size(); ++r) {
                if (group[r].submeshIndex != channelIndex) continue;
                t.rangeMask[r] = true;
                ++t.rangesForThisChannel;
            }
            if (t.rangesForThisChannel > 0) targets.push_back(std::move(t));
        }
        if (targets.empty()) {
            why = cznName + ": channel has no draw ranges in any located group";
            return false;
        }
    } else {
        targets.push_back(GroupTarget{});
    }

    // Collision triangle capture (street-collision-0x2237-mopp follow-up
    // task) - decoded ONCE here, outside the render loop below, since every
    // target shares the same resolved channel; "skip, don't fabricate" on
    // any decode failure, same discipline loadCityProp()'s own
    // `outCollisionTris` block already uses.
    std::vector<sr3mesh::Vertex> collisionVerts;
    bool haveCollisionVerts = false;
    if (outCollisionTris != nullptr) {
        try {
            collisionVerts = mesh.decodeChannel(channelIndex);
            haveCollisionVerts = true;
        } catch (const std::exception&) {
            haveCollisionVerts = false;
        }
    }

    std::string error;
    for (const auto& t : targets) {
        auto r = std::make_unique<sr3render::MeshRenderer>();
        if (!r->initialise(device, error)) {
            why = cznName + ": renderer init failed: " + error;
            return false;
        }
        const std::vector<bool>* maskPtr = t.rangeMask.empty() ? nullptr : &t.rangeMask;
        if (!r->upload(device, mesh, channelIndex, 0, error, t.groupIndex, nullptr, nullptr, maskPtr)) {
            why = cznName + ": upload failed: " + error;
            return false;
        }
        tile.rangesRendered += t.rangesForThisChannel;
        tile.triangles += r->trianglesEmitted();
        // SR3_GROUND_TEX_DEBUG=1: this renderer's own real decoded world
        // bounds - added after a post-merge review caught a misleading
        // demonstration frame (a nearby, already-textured PROP was mistaken
        // for this ground renderer at a camera angle where the two happen
        // to overlap). Printing the real bounds is what found the actual
        // cause: this content kind's own geometry is not always ground-
        // level - e.g. sr3_city~s0916's resolved channel sits at world Y
        // 18-31, not Y~0, so it can sit entirely inside/behind a taller
        // coincident prop's own silhouette from most camera angles, purely
        // a depth-occlusion/visibility question, unrelated to whether a
        // real texture resolved. Left in permanently - same diagnostic
        // value for any future tile/camera combination.
        if (groundTexDebugLog()) {
            std::fprintf(stderr, "[groundtex] %s world bounds: min(%.2f %.2f %.2f) max(%.2f %.2f %.2f)\n",
                        cznName.c_str(), r->boundsMin()[0], r->boundsMin()[1], r->boundsMin()[2],
                        r->boundsMax()[0], r->boundsMax()[1], r->boundsMax()[2]);
        }
        tile.renderers.push_back(std::move(r));
        if (haveCollisionVerts && outCollisionTris != nullptr) {
            // Real triangles for THIS target's own draw ranges, in the
            // SAME world-space positions just uploaded for rendering -
            // see triangleListForRange()'s own "strip-to-list, degenerates
            // already dropped" contract (ClmeshTriangleCollisionWorld's
            // doc comment above already cites this for `.clmesh_pc`; the
            // SAME sr3mesh::MeshBlock method, reused unmodified here).
            std::vector<uint32_t> triIdx;
            if (mesh.drawGroupsLocated() && !t.rangeMask.empty()) {
                const auto& group = mesh.drawGroups()[t.groupIndex];
                for (size_t rr = 0; rr < group.size(); ++rr) {
                    if (!t.rangeMask[rr]) continue;
                    std::vector<uint32_t> one = mesh.triangleListForRange(group[rr]);
                    triIdx.insert(triIdx.end(), one.begin(), one.end());
                }
            } else {
                // No located draw groups at all (the `else` branch above,
                // one empty GroupTarget) - fall back to the whole mesh's
                // own triangle list, same shape MeshRenderer::upload()
                // itself falls back to for rendering when given no mask.
                triIdx = mesh.triangleListIndices();
            }
            for (size_t i = 0; i + 2 < triIdx.size(); i += 3) {
                const uint32_t ia = triIdx[i], ib = triIdx[i + 1], ic = triIdx[i + 2];
                if (ia >= collisionVerts.size() || ib >= collisionVerts.size() || ic >= collisionVerts.size())
                    continue;
                outCollisionTris->push_back(collisionVerts[ia].position);
                outCollisionTris->push_back(collisionVerts[ib].position);
                outCollisionTris->push_back(collisionVerts[ic].position);
            }
        }
    }
    // A near-origin primary block (allBlocks) is dropped again: its renderers were the first ones added.
    if (skipPrimary) {
        tile.renderers.clear();
        tile.rangesRendered = 0;
        tile.triangles = 0;
    }
    // The other blocks (allBlocks): same per-draw-group upload, no collision (that comes from the 0x2237 path).
    for (size_t di = skipPrimary ? 0 : 1; di < drawn.size(); ++di) {
        const sr3mesh::MeshBlock& m = *drawn[di].first;
        const size_t ch = drawn[di].second;
        std::vector<std::pair<size_t, std::vector<bool>>> groups;
        if (m.drawGroupsLocated()) {
            for (size_t gi = 0; gi < m.drawGroups().size(); ++gi) {
                std::vector<bool> mask(m.drawGroups()[gi].size(), false);
                size_t n = 0;
                for (size_t r = 0; r < mask.size(); ++r)
                    if (m.drawGroups()[gi][r].submeshIndex == ch) {
                        mask[r] = true;
                        ++n;
                    }
                if (n > 0) groups.push_back({gi, std::move(mask)});
            }
        } else {
            groups.push_back({0, {}});
        }
        for (const auto& g : groups) {
            auto r = std::make_unique<sr3render::MeshRenderer>();
            std::string err;
            if (!r->initialise(device, err)) continue;
            if (!r->upload(device, m, ch, 0, err, g.first, nullptr, nullptr, g.second.empty() ? nullptr : &g.second))
                continue;  // a block that does not upload is skipped, the rest still draw
            for (bool b : g.second) tile.rangesRendered += b ? 1 : 0;
            tile.triangles += r->trianglesEmitted();
            tile.renderers.push_back(std::move(r));
        }
    }
    tile.cznName = cznName;
    return true;
}

} // namespace

namespace sr3viewer {

// `buildCollision` (street-collision-0x2237-mopp follow-up task,
// 2026-10-11): when true, ALSO captures this tile's real `0x2237`
// collision triangles (tryRenderZoneEntry()'s own `outCollisionTris`,
// above) and builds `tile.collisionBvh` from them - the SAME
// CollisionBvh/BuildCollisionBvh() machinery ClmeshTriangleCollisionWorld
// already uses for `.clmesh_pc` props, just with a zero translation (this
// content kind's own decoded positions are already world-space, unlike a
// prop's local-space triangles). Defaults to false, which is every
// pre-existing caller (plain `city` without --collision), so this costs
// nothing and changes nothing for them.
static bool loadCityGroundTileRender(int col, int row, std::vector<std::unique_ptr<CityArchive>>& archives,
                                     ID3D11Device* device, CityGroundTile& tile, std::string& why,
                                     bool buildCollision, bool allBlocks) {
    tile.col = col;
    tile.row = row;
    bool foundAnyEntry = false;
    std::vector<std::array<float, 3>> localCollisionTris;
    auto* collisionOut = buildCollision ? &localCollisionTris : nullptr;
    // Every prefix-matching candidate seen across every given archive (base
    // zone included) - diagnostic only, see describeZoneCandidateBlockCount()
    // and the `why`-message building below.
    std::vector<TileZoneCandidate> allCandidates;
    for (size_t ai = 0; ai < archives.size(); ++ai) {
        CityArchive& a = *archives[ai];
        std::string cznName;
        // Collected per-archive (not straight into allCandidates) so the
        // `~s` fallback below can find the "s_base" entry THIS archive's
        // own call produced, before it gets merged into the global dump
        // list - see this function's own top comment's CHOSEN fallback
        // paragraph for why `~s` is tried at all.
        std::vector<TileZoneCandidate> archiveCandidates;
        const bool haveBase = findTileZoneEntryName(*a.top, col, row, cznName, &archiveCandidates);
        allCandidates.insert(allCandidates.end(), archiveCandidates.begin(), archiveCandidates.end());
        std::string sBaseName;
        for (const auto& c : archiveCandidates) {
            if (c.kind == "s_base") {
                sBaseName = c.entryName;
                break;
            }
        }
        if (!haveBase && sBaseName.empty()) continue;
        foundAnyEntry = true;

        // Built fresh for THIS archive only - never inherits a previous
        // archive's own failure message (regression-safety: the original,
        // pre-`~s` code always fully OVERWROTE `why` on every failure,
        // never accumulated it across archives; only committed to the
        // real `why` output parameter below, once this archive's own
        // outcome - primary attempt, `~s` fallback if any, candidate dump -
        // is fully known).
        std::string archiveWhy;
        if (haveBase) {
            bool bytesUnreadable = false;
            if (tryRenderZoneEntry(*a.top, cznName, device, tile, archiveWhy, &bytesUnreadable, &a.texCache,
                                    collisionOut, allBlocks)) {
                tile.archiveIndex = ai;
                if (collisionOut != nullptr && !localCollisionTris.empty())
                    tile.collisionBvh = BuildCollisionBvh(localCollisionTris, 0.0f, 0.0f, 0.0f);
                return true;
            }
            // Regression-safety: with no `~s` candidate in this archive at
            // all, reproduce the EXACT pre-`~s` control flow - "could not
            // read czn/gzn bytes" tried the next given archive, every other
            // failure reason was already terminal for this tile (see
            // tryRenderZoneEntry()'s own `outBytesUnreadable` doc comment).
            if (sBaseName.empty() && bytesUnreadable) {
                why = archiveWhy;
                continue;
            }
        }
        if (!sBaseName.empty()) {
            // `~s` FALLBACK (2026-10-08, Team A spec-sync relay - CHOSEN,
            // see this function's own top comment): only reached because
            // the `~f` base above either does not exist in this archive or
            // produced nothing renderable - a `~f` base that already
            // rendered already returned true above, so this never
            // overrides real `~f` ground data.
            std::string sWhy;
            if (tryRenderZoneEntry(*a.top, sBaseName, device, tile, sWhy, nullptr, &a.texCache, collisionOut, allBlocks)) {
                tile.archiveIndex = ai;
                if (collisionOut != nullptr && !localCollisionTris.empty())
                    tile.collisionBvh = BuildCollisionBvh(localCollisionTris, 0.0f, 0.0f, 0.0f);
                return true;
            }
            archiveWhy += archiveWhy.empty() ? sWhy : ("; ~s fallback " + sWhy);
        }
        // Honest reporting of what this tile's OTHER real candidates carry
        // (see findTileZoneEntryName()'s own "DECISION" paragraph for why
        // they are reported here but never rendered as ground): an `~al`/
        // variant/`~s`-non-base candidate with real Mesh data used to be
        // invisible to this diagnostic entirely.
        for (const auto& c : allCandidates) {
            if (c.entryName == cznName || c.entryName == sBaseName) continue; // already described above
            size_t otherBlocks = 0;
            std::string otherWhy;
            describeZoneCandidateBlockCount(*a.top, c.entryName, otherBlocks, otherWhy);
            archiveWhy += "; " + c.kind + " candidate " + c.entryName + " ";
            if (!otherWhy.empty()) {
                archiveWhy += "(" + otherWhy + ")";
            } else {
                archiveWhy += "carries " + std::to_string(otherBlocks) +
                       " Mesh sub-block(s) (not rendered as ground - OPEN spec-sync question for "
                       "Team A: does awld_compact's `~al` zone geometry represent this tile's "
                       "street-level ground? see findTileZoneEntryName()'s own top comment)";
            }
        }
        why = archiveWhy;
        return false;
    }
    if (!foundAnyEntry) {
        char bufF[64], bufS[64];
        std::snprintf(bufF, sizeof(bufF), "sr3_city~f%02d%02d*.czn_pc", col, row);
        std::snprintf(bufS, sizeof(bufS), "sr3_city~s%02d%02d*.czn_pc", col, row);
        why = std::string("no real \"base\" ") + bufF + " or \"s_base\" " + bufS + " found in any given archive";
        // No BASE zone anywhere, but a non-base (`~al`/variant/`~s`)
        // candidate can still exist (findTileZoneEntryName() returns false
        // - no `~f` base - while still appending any other matches it
        // passed to `candidates`) - report that honestly too, same
        // reasoning as the resolvedBlock<0 branch above.
        for (const auto& c : allCandidates) {
            why += "; " + c.kind + " candidate " + c.entryName + " exists (not rendered as ground - "
                   "see findTileZoneEntryName()'s own top comment)";
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// WALKABLE GROUND SOURCE (TEAM B task G1, spec-zone-data-format.md Sec13.5.1 port recipe, steps 1-2).
// Loads the world-space `0x2237` TAIL COLLISION MESH of this tile's `sr3_city~s<CCRR>.czn_pc` (no transform; the `^name`
// variant file when that variant is active; `~f` is never a source) and builds tile.collisionBvh from it. This REPLACES
// the earlier render-mesh-derived collision triangles (which were the near-origin render content, not the walkable
// surface - results/freeroam_ground_height_20261018.md). The true triangle list needs a MOPP walker the spec does not
// give, so the spec's GUESS strip stand-in is used (CHOSEN, labelled): (v, v+1, v+2) for every v with s[v] != 0x38ff.
// Placed `.clmesh_pc` objects of <CCRR>h0..h3 (recipe step 1, second source) are NOT added here: they are buildings/props,
// "none of them is a road or terrain surface" (13.5.1(b)2), and their real object placement (Sec11 object -> clmesh
// binding) is not decoded by this project (CityLoadedCell lays props on a stand-in grid) - see the final report.
namespace {
std::string g_activeZoneVariant;  // "" = none ("megab1" selects the `^megab1` file)

bool collisionEntryMatchesVariant(const std::string& entryName, const std::string& variant) {
    std::string low = entryName;
    for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::string want = "^" + variant;
    for (char& ch : want) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return low.find(want + ".") != std::string::npos;
}
}  // namespace


void setActiveZoneVariant(const std::string& variant) { g_activeZoneVariant = variant; }
const std::string& activeZoneVariant() { return g_activeZoneVariant; }

bool loadCityGroundTileCollision(int col, int row, std::vector<std::unique_ptr<CityArchive>>& archives,
                                 CityGroundTile& tile, std::string& why) {
    tile.collisionLoaded = false;
    tile.collisionBvh = CollisionBvh();
    tile.collisionVertexCount = 0;
    tile.collisionTriangleCount = 0;
    tile.collisionSource.clear();
    bool sawSEntry = false;
    for (size_t ai = 0; ai < archives.size(); ++ai) {
        CityArchive& a = *archives[ai];
        std::string unusedBase;
        std::vector<TileZoneCandidate> cands;
        findTileZoneEntryName(*a.top, col, row, unusedBase, &cands);
        std::string chosen;
        const std::string& variant = g_activeZoneVariant;
        if (!variant.empty()) {
            for (const auto& c : cands)
                if (c.kind == "s_variant" && collisionEntryMatchesVariant(c.entryName, variant)) { chosen = c.entryName; break; }
        }
        if (chosen.empty()) {
            for (const auto& c : cands)
                if (c.kind == "s_base") { chosen = c.entryName; break; }
        }
        if (chosen.empty()) continue;
        sawSEntry = true;
        std::vector<uint8_t> cznBytes, unusedSibling;
        if (!findEntry(*a.top, chosen, cznBytes, unusedSibling, "") || cznBytes.empty()) {
            why = chosen + ": could not read czn bytes";
            continue;
        }
        std::vector<sr3zone::CollisionMesh> meshes;
        if (!sr3zone::LocateCollisionMeshes(vpp::ByteView(cznBytes.data(), cznBytes.size()), meshes)) {
            why = chosen + ": no 0x2237 tail collision mesh located";
            continue;
        }
        std::vector<std::array<float, 3>> tris;
        for (const auto& m : meshes) {
            tile.collisionVertexCount += m.v.size();
            auto t = sr3zone::BuildKeyTriangles(m);
            if (t.empty()) t = sr3zone::BuildStripStandInTriangles(m);  // LABELLED FALLBACK (wrong; only if MOPP walk failed)
            tris.insert(tris.end(), t.begin(), t.end());
        }
        if (tris.empty()) {
            why = chosen + ": collision mesh decoded but the strip stand-in produced no triangles";
            continue;
        }
        tile.collisionBvh = BuildCollisionBvh(tris, 0.0f, 0.0f, 0.0f);  // world-space, identity placement (13.5.1(b)1)
        tile.collisionTriangleCount = tile.collisionBvh.tris.size();
        tile.collisionSource = chosen;
        tile.collisionLoaded = !tile.collisionBvh.empty();
        return tile.collisionLoaded;
    }
    if (!sawSEntry) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "no sr3_city~s%02d%02d*.czn_pc in any archive", col, row);
        why = buf;
    }
    return false;
}

bool loadCityGroundTile(int col, int row, std::vector<std::unique_ptr<CityArchive>>& archives, ID3D11Device* device,
                        CityGroundTile& tile, std::string& why, bool buildCollision, bool allBlocks) {
    // Render path: unchanged, but no longer the source of collision triangles (see the section comment above).
    const bool ok = loadCityGroundTileRender(col, row, archives, device, tile, why, false, allBlocks);
    if (buildCollision) {
        std::string collWhy;
        loadCityGroundTileCollision(col, row, archives, tile, collWhy);
        tile.collisionWhy = collWhy;
    }
    return ok;
}

} // namespace sr3viewer