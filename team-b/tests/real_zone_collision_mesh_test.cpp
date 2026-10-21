// Real-data regression for sr3zone::LocateCollisionMeshes / BuildStripStandInTriangles against the tile-0915 test
// vectors of spec-zone-data-format.md Sec13.5.1(e). Reads `sr3_city~s0915.czn_pc` (and the `^megab1` variant) from
// CRREISH_GAME_CACHE_DIR/sr3_city_0.vpp_pc; exit 77 (SKIPPED) when absent.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3zone/zone_collision_mesh.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

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
bool find(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            if (find(c.openNested(i), name, out)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

// Highest triangle surface at or below y0 + 0.05 at (x,z) (the 0x0075dfe0 ground snap), over the strip stand-in.
bool groundHit(const std::vector<std::array<float, 3>>& tris, double x, double z, double y0, double& outY, double& outNy) {
    bool found = false;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const auto &a = tris[i], &b = tris[i + 1], &c = tris[i + 2];
        const double d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
        if (std::fabs(d) < 1e-12) continue;
        const double l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d;
        const double l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d;
        const double l3 = 1.0 - l1 - l2;
        if (l1 < 0 || l2 < 0 || l3 < 0) continue;
        const double y = l1 * a[1] + l2 * b[1] + l3 * c[1];
        if (y > y0 + 0.05) continue;
        if (!found || y > outY) {
            found = true;
            outY = y;
            const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
            const double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
            const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            outNy = std::fabs(ny) / std::sqrt(nx * nx + ny * ny + nz * nz);
        }
    }
    return found;
}
}  // namespace

int main() {
#ifndef CRREISH_GAME_CACHE_DIR
    std::cout << "CRREISH_GAME_CACHE_DIR not configured - SKIPPED\n";
    return 77;
#else
    const std::string path = std::string(CRREISH_GAME_CACHE_DIR) + "/sr3_city_0.vpp_pc";
    if (!std::ifstream(path, std::ios::binary).good()) {
        std::cout << "archive not found (" << path << ") - SKIPPED\n";
        return 77;
    }
    std::vector<uint8_t> s0915, f0915, variant;
    try {
        auto opened = vppcache::openArchive(path);
        if (!find(opened->container, "sr3_city~s0915.czn_pc", s0915)) {
            std::cout << "sr3_city~s0915.czn_pc not found\n";
            return 1;
        }
        find(opened->container, "sr3_city~f0915.czn_pc", f0915);
        find(opened->container, "sr3_city~s0915^megab1.czn_pc", variant);
    } catch (const std::exception& ex) {
        std::cout << "open failed: " << ex.what() << "\n";
        return 77;
    }
    std::vector<sr3zone::CollisionMesh> meshes;
    CHECK(sr3zone::LocateCollisionMeshes(vpp::ByteView(s0915.data(), s0915.size()), meshes));
    CHECK(meshes.size() == 1);
    if (meshes.size() != 1) return 1;
    const sr3zone::CollisionMesh& m = meshes[0];
    // Sec13.5.1(e): n1 at file 0x114a0, 9,421 vertices, n2 = 9,421, MOPP version-301 blob of 192,741 bytes.
    CHECK(m.n1Offset == 0x114a0);
    CHECK(m.v.size() == 9421);
    CHECK(m.s.size() == 9421);
    CHECK(m.blobLength == 192741);
    // AABB x[-1461.61,-1094.52] y[3.77,42.56] z[-722.72,-379.97]
    CHECK(std::fabs(m.bmin[0] + 1461.61f) < 0.01f && std::fabs(m.bmax[0] + 1094.52f) < 0.01f);
    CHECK(std::fabs(m.bmin[1] - 3.77f) < 0.01f && std::fabs(m.bmax[1] - 42.56f) < 0.01f);
    CHECK(std::fabs(m.bmin[2] + 722.72f) < 0.01f && std::fabs(m.bmax[2] + 379.97f) < 0.01f);
    // s[n1-2] == s[n1-1] == 0x38ff (18/18 records)
    CHECK(m.s[m.s.size() - 2] == sr3zone::kNoTriangleSurfaceId && m.s[m.s.size() - 1] == sr3zone::kNoTriangleSurfaceId);
    // `~f0915` has no mesh (92-byte stub)
    if (!f0915.empty()) {
        std::vector<sr3zone::CollisionMesh> fm;
        CHECK(!sr3zone::LocateCollisionMeshes(vpp::ByteView(f0915.data(), f0915.size()), fm));
    }
    // variant: its own 9,495-vertex mesh, same AABB
    if (!variant.empty()) {
        std::vector<sr3zone::CollisionMesh> vm;
        CHECK(sr3zone::LocateCollisionMeshes(vpp::ByteView(variant.data(), variant.size()), vm));
        if (!vm.empty()) CHECK(vm[0].v.size() == 9495);
    } else {
        std::cout << "note: ^megab1 variant entry not found by that name (variant check skipped)\n";
    }

    // strip stand-in triangle count: 6513 in ~s0915
    CHECK(m.keys.size() == 13503);  // spec 13.5.3: MOPP terminal keys
    CHECK(m.keys.size() >= 10 && m.keys[0] == 0x02040000u && m.keys[1] == 0x82080001u && m.keys[2] == 0x04080001u &&
          m.keys[3] == 0x82080003u && m.keys[4] == 0x02040005u);
    CHECK(m.keys.size() >= 3 && m.keys[m.keys.size() - 3] == 0x0C040C3Eu && m.keys[m.keys.size() - 2] == 0x82040C3Bu &&
          m.keys.back() == 0x82080C3Bu);
    const auto tris = sr3zone::BuildKeyTriangles(m);
    std::cout << "key triangles: " << tris.size() / 3 << " (spec: 13503)\n";
    CHECK(tris.size() / 3 == 13503);
    CHECK(sr3zone::BuildStripStandInTriangles(m).size() / 3 == 6513);  // labelled fallback

    // Test vectors: (x, z, nodeY or NaN, expected hit Y or NaN = miss, expected vertex n within 4 m XZ)
    struct Vec { double x, z, nodeY, hitY; int nVerts; double vmin, vmed, vmax; };
    const Vec vecs[] = {
        {-1280.0, -560.0, NAN, 32.676, 1, 32.39, 32.39, 32.39},
        {-1272.0, -584.4, 32.15, 32.150, 5, 32.00, 32.15, 32.15},
        {-1266.9, -584.4, 32.13, 32.156, 4, 32.00, 32.08, 32.15},
        {-1282.8, -478.6, 36.06, 36.058, 4, 35.93, 36.13, 36.18},
        {-1318.6, -681.3, 23.82, 23.795, 2, 23.69, 23.77, 23.84},
        {-1311.1, -544.6, 28.75, 28.749, 3, 28.60, 28.75, 28.75},
    };
    int hits = 0, misses = 0;
    for (const Vec& t : vecs) {
        std::vector<double> ys;
        for (const auto& v : m.v)
            if (std::hypot(v[0] - t.x, v[2] - t.z) <= 4.0) ys.push_back(v[1]);
        std::sort(ys.begin(), ys.end());
        std::cout << "point (" << t.x << ", " << t.z << "): " << ys.size() << " collision vertices within 4 m";
        CHECK(static_cast<int>(ys.size()) == t.nVerts);
        if (!ys.empty()) {
            std::cout << " min/med/max " << ys.front() << "/" << ys[ys.size() / 2] << "/" << ys.back();
            CHECK(std::fabs(ys.front() - t.vmin) < 0.02 && std::fabs(ys.back() - t.vmax) < 0.02);
            if (!std::isnan(t.nodeY))
                for (double y : ys) CHECK(std::fabs(y - t.nodeY) <= 0.15 + 1e-6);  // "within 0.15 m of node Y"
        }
        // spec item 4: start height = nearest node Y + 2 m (spawn has no node: AABB max Y + 1)
        const double y0 = std::isnan(t.nodeY) ? static_cast<double>(m.bmax[1]) + 1.0 : t.nodeY + 2.0;
        double hy = 0, ny = 0;
        const bool hit = groundHit(tris, t.x, t.z, y0, hy, ny);
        std::cout << " | ray hit " << (hit ? std::to_string(hy) : std::string("miss")) << "\n";
        if (std::isnan(t.hitY)) {
            CHECK(!hit);
            ++misses;
        } else {
            CHECK(hit);
            if (hit) {
                ++hits;
                CHECK(std::fabs(hy - t.hitY) < 0.03);
                CHECK(ny >= 0.7);
            }
        }
    }
    std::cout << "stand-in on the 6 spec vectors: " << hits << " hits, " << misses << " misses (spec 13.5.3: 6/6 hit)\n";
    // Spawn ground is about 32.4 to 32.7 m (spec).
    double sy = 0, sny = 0;
    CHECK(groundHit(tris, -1280.0, -560.0, static_cast<double>(m.bmax[1]) + 1.0, sy, sny));
    CHECK(sy >= 32.3 && sy <= 32.8);

    if (g_failures == 0) std::cout << "real_zone_collision_mesh_test: all passed\n";
    return g_failures == 0 ? 0 : 1;
#endif
}
