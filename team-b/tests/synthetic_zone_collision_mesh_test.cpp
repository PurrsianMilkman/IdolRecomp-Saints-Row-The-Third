// Pure decode tests for sr3zone/zone_collision_mesh.h (spec-zone-data-format.md Sec13.5.1(c)): shape-key triangle
// decode, surface-id accessor, locator on a synthetic tail-mesh layout, strip stand-in triangle list.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "sr3zone/zone_collision_mesh.h"

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4);
    std::memcpy(&b[at], &v, 4);
}
void putf(std::vector<uint8_t>& b, size_t at, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(b, at, u);
}
size_t al(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

// Builds `pad` junk bytes + the tail layout described in the header; n1 == n2 == n.
std::vector<uint8_t> build(size_t pad, const std::vector<std::array<float, 3>>& v, const std::vector<uint16_t>& s,
                           uint32_t blobLen) {
    std::vector<uint8_t> b(al(pad, 16), 0xCD);
    size_t at = b.size();
    const size_t n = v.size();
    put32(b, at, static_cast<uint32_t>(n));
    at += 4;
    for (size_t i = 0; i < n; ++i)
        for (size_t k = 0; k < 3; ++k) putf(b, at + i * 12 + 4 * k, v[i][k]);
    at += n * 12;
    at = al(at, 4);
    b.resize(at, 0);
    put32(b, at, static_cast<uint32_t>(n));
    at += 4;
    b.resize(at + 2 * n, 0);
    for (size_t i = 0; i < n; ++i) std::memcpy(&b[at + 2 * i], &s[i], 2);
    at += 2 * n;
    at = al(at, 16);
    b.resize(at, 0);
    put32(b, at, blobLen);
    at += 4;
    at = al(at, 16);
    b.resize(at, 0);
    put32(b, at, sr3zone::kMoppMagic);
    b.resize(at + blobLen, 0x11);
    put32(b, at, sr3zone::kMoppMagic);
    at = al(at + blobLen, 4);
    b.resize(at, 0);
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& p : v)
        for (size_t k = 0; k < 3; ++k) { lo[k] = std::fmin(lo[k], p[k]); hi[k] = std::fmax(hi[k], p[k]); }
    for (size_t k = 0; k < 3; ++k) { putf(b, at + 4 * k, lo[k]); putf(b, at + 12 + 4 * k, hi[k]); }
    return b;
}
}  // namespace

int main() {
    using namespace sr3zone;
    // key decode (FINAL): v0 = key&0x3ffff, v1 = v0 + ((key>>18)&0x7f), v2 = v1 + ((key>>25)&0x3f)
    {
        const uint32_t key = 100u | (3u << 18) | (5u << 25);
        const ShapeKeyTriangle t = DecodeShapeKey(key);
        CHECK(t.v0 == 100 && t.v1 == 103 && t.v2 == 108);
        const ShapeKeyTriangle z = DecodeShapeKey(0x3ffffu);
        CHECK(z.v0 == 0x3ffff && z.v1 == 0x3ffff && z.v2 == 0x3ffff);
    }
    // synthetic tail mesh, several pads / n values (exercises the alignment arithmetic)
    for (size_t pad : {16u, 37u, 100u}) {
        for (size_t n : {3u, 4u, 5u, 6u, 7u, 8u, 9u}) {
            std::vector<std::array<float, 3>> v;
            std::vector<uint16_t> s;
            for (size_t i = 0; i < n; ++i) {
                v.push_back({static_cast<float>(i) * 2.0f - 5.0f, 30.0f + 0.1f * static_cast<float>(i), static_cast<float>(i % 3) * 3.0f});
                s.push_back(static_cast<uint16_t>(i + 10));
            }
            s[n - 2] = kNoTriangleSurfaceId;
            s[n - 1] = kNoTriangleSurfaceId;
            const std::vector<uint8_t> buf = build(pad, v, s, 24);
            std::vector<CollisionMesh> out;
            CHECK(LocateCollisionMeshes(vpp::ByteView(buf.data(), buf.size()), out));
            CHECK(out.size() == 1);
            if (out.size() != 1) continue;
            CHECK(out[0].v.size() == n && out[0].s.size() == n);
            CHECK(out[0].v[n - 1][0] == v[n - 1][0]);
            CHECK(out[0].s[0] == 10);
            CHECK(out[0].bmax[1] > out[0].bmin[1]);
            uint16_t sid = 0;
            CHECK(out[0].SurfaceIdForKey(0, sid) && sid == 10);
            CHECK(!out[0].SurfaceIdForKey(static_cast<uint32_t>(n), sid));
            // strip stand-in: triangles (v, v+1, v+2) for every v with s[v] != 0x38ff -> n-2 candidates minus excluded
            const auto tris = BuildStripStandInTriangles(out[0]);
            CHECK(tris.size() == 3 * (n - 2));
            if (tris.size() >= 3) CHECK(tris[1] == v[1] && tris[2] == v[2]);
        }
    }
    // bounds mismatch (corrupt AABB) must not be accepted
    {
        std::vector<std::array<float, 3>> v = {{0, 1, 0}, {1, 1, 0}, {0, 1, 1}, {1, 2, 1}};
        std::vector<uint16_t> s = {1, 1, kNoTriangleSurfaceId, kNoTriangleSurfaceId};
        std::vector<uint8_t> buf = build(16, v, s, 16);
        putf(buf, buf.size() - 24, -100.0f);
        std::vector<CollisionMesh> out;
        CHECK(!LocateCollisionMeshes(vpp::ByteView(buf.data(), buf.size()), out));
    }
    // junk buffer
    {
        std::vector<uint8_t> junk(1024, 0x5a);
        std::vector<CollisionMesh> out;
        CHECK(!LocateCollisionMeshes(vpp::ByteView(junk.data(), junk.size()), out));
    }
    if (g_failures == 0) std::cout << "synthetic_zone_collision_mesh_test: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
