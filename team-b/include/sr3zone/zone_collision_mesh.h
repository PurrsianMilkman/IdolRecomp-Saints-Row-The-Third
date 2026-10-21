// sr3zone/zone_collision_mesh.h - the walkable-ground source: the world-space TAIL COLLISION MESH of the `0x2237`
// record in a tile's `sr3_city~s<CCRR>.czn_pc` (spec-zone-data-format.md Sec13.5.1 "Walkable ground source",
// 2026-10-18; byte layout = the `.clmesh_pc` hull layout of spec-physics-format.md Sec4.4.6).
//
// FINAL (spec): layout `u32 n1; float3 v[n1]; align4; u32 n2; u16 s[n2]; align16; u32 blobLen; align16; blob ('MOPP');
// align4; float3 min; float3 max`; n2 == n1; the vertices are world-space metres (placed with identity); `s[v]` is the
// surface id of the triangle whose FIRST vertex is v (value 0x38ff = GUESS "no triangle starts here"); a triangle is
// defined by its shape key alone: v0 = key & 0x3ffff, v1 = v0 + ((key >> 18) & 0x7f), v2 = v1 + ((key >> 25) & 0x3f).
//
// NOT specified: the MOPP bytecode (the true key set), and where the record's tail mesh begins relative to the record
// head. CHOSEN locator: the record head layout ahead of the tail is not specified, so the mesh is located by its own
// invariants (MOPP magic 0x50504f4d at the blob, blobLen, n1 == n2, trailing AABB == vertex bounds within 0.02 m: MEASURED, the stored AABB is the vertex bounds padded by ~0.005 m, not exactly equal) - see
// LocateCollisionMeshes(). CHOSEN triangle list (the spec's "GUESS strip stand-in", labelled): (v, v+1, v+2) for every
// v with s[v] != 0x38ff.
// Pure (no I/O), unit-tested by tests/synthetic_zone_collision_mesh_test.cpp and tests/real_zone_collision_mesh_test.cpp.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "vpp/byte_view.h"

namespace sr3zone {

constexpr uint16_t kNoTriangleSurfaceId = 0x38ff;   // GUESS (spec 13.5.1(c)): "no triangle starts at this vertex"
constexpr uint32_t kMoppMagic = 0x50504f4d;         // 'MOPP' (spec-physics-format.md Sec4.4.6(g))

struct ShapeKeyTriangle {
    uint32_t v0 = 0, v1 = 0, v2 = 0;
};
// FINAL (spec 13.5.1(c), child-shape getter 0x00d55110).
inline ShapeKeyTriangle DecodeShapeKey(uint32_t key) {
    ShapeKeyTriangle t;
    t.v0 = key & 0x3ffffu;
    t.v1 = t.v0 + ((key >> 18) & 0x7fu);
    t.v2 = t.v1 + ((key >> 25) & 0x3fu);
    return t;
}

struct CollisionMesh {
    std::vector<std::array<float, 3>> v;   // world-space metres
    std::vector<uint16_t> s;               // surface id of the triangle starting at each vertex (n2 == n1)
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};  // the trailing AABB
    size_t n1Offset = 0;                   // byte offset of `u32 n1` within the buffer passed to the locator
    uint32_t blobLength = 0;
    size_t blobOffset = 0;                 // byte offset of the MOPP blob in the buffer
    std::vector<uint32_t> keys;            // FINAL (spec 13.5.3): shape keys at the MOPP terminals, VM walk order
    // FINAL accessor 0x00d54600: s[key & 0x3ffff] when that index < n2, else fails.
    bool SurfaceIdForKey(uint32_t key, uint16_t& out) const {
        const uint32_t i = key & 0x3ffffu;
        if (i >= s.size()) return false;
        out = s[i];
        return true;
    }
};

// Locates every collision mesh in a `.czn_pc` buffer (a `~s` file carries one, in its 0x2237 record; `~f` carries none).
bool LocateCollisionMeshes(vpp::ByteView czn, std::vector<CollisionMesh>& out);

// FINAL (spec 13.5.3): left-first DFS over the MOPP bytecode; false on invalid opcode / bad range.
bool EnumerateMoppKeys(vpp::ByteView czn, size_t blobOffset, std::vector<uint32_t>& keys);
// FINAL (spec 13.5.3): true triangle list (flat, 3 verts/tri); key bit 31 swaps v1/v2.
std::vector<std::array<float, 3>> BuildKeyTriangles(const CollisionMesh& mesh);

// LABELLED FALLBACK ONLY (wrong per 13.5.3(d)): CHOSEN strip stand-in (spec 13.5.1(c) GUESS): flat list, 3 vertices per triangle.
std::vector<std::array<float, 3>> BuildStripStandInTriangles(const CollisionMesh& mesh);

}  // namespace sr3zone
