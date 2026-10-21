// Unit tests for sr3zone placement decode, against the spec-world-streaming.md Sec10.7.1 test vectors
// (tile 1018 and tile 0915 records, positions to 1e-4 m).
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3zone/zone_placement.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "CHECK FAILED: " #c " at line " << __LINE__ << "\n"; ++g_fail; } } while (0)

static sr3zone::ZoneHeaderRecord rec(int16_t x, int16_t y, int16_t z, int16_t a, int16_t b, int16_t c, uint16_t name) {
    sr3zone::ZoneHeaderRecord r{};
    auto put = [&](size_t o, uint16_t v) { r[o] = static_cast<uint8_t>(v & 0xFF); r[o + 1] = static_cast<uint8_t>(v >> 8); };
    put(0, static_cast<uint16_t>(x)); put(2, static_cast<uint16_t>(y)); put(4, static_cast<uint16_t>(z));
    put(6, static_cast<uint16_t>(a)); put(8, static_cast<uint16_t>(b)); put(10, static_cast<uint16_t>(c));
    put(12, name);
    return r;
}

int main() {
    // Spec pool: lite_fixh.lmeshx @0 (17 bytes with NUL), lamppoststrbse_a.lmeshx @17 (24), lamppoststr_b.lmeshx @41.
    std::vector<uint8_t> pool;
    for (const char* s : {"lite_fixh.lmeshx", "lamppoststrbse_a.lmeshx", "lamppoststr_b.lmeshx"}) {
        pool.insert(pool.end(), s, s + std::strlen(s));
        pool.push_back(0);
    }
    const std::array<float, 3> o1018 = {-814.5098877f, 8.2553046f, -1391.8225098f};
    struct V { sr3zone::ZoneHeaderRecord r; const char* mesh; float px, py, pz, aa, bb, cc; };
    const V v1018[] = {
        {rec(-5718, 847, 3124, 0, 0, 0, 0), "lite_fixh", -903.8536f, 21.4897f, -1343.0100f, 0, 0, 0},
        {rec(1839, -358, 982, 0, 0, 11606, 17), "lamppoststrbse_a", -785.7755f, 2.6616f, -1376.4788f, 0, 0, 2.83350f},
        {rec(1839, -338, 982, 0, 0, 11606, 41), "lamppoststr_b", -785.7755f, 2.9741f, -1376.4788f, 0, 0, 2.83350f},
    };
    for (const auto& v : v1018) {
        auto o = sr3zone::DecodePlacement(v.r, o1018, pool.data(), pool.size());
        CHECK(o.bound);
        CHECK(o.meshName == v.mesh);
        CHECK(std::fabs(o.position[0] - v.px) < 1e-3f);
        CHECK(std::fabs(o.position[1] - v.py) < 1e-3f);
        CHECK(std::fabs(o.position[2] - v.pz) < 1e-3f);
        CHECK(std::fabs(o.angles[0] - v.aa) < 1e-4f);
        CHECK(std::fabs(o.angles[1] - v.bb) < 1e-4f);
        CHECK(std::fabs(o.angles[2] - v.cc) < 1e-4f);
    }
    // Tile 0915 #3 (sh_megabrothel) / #4 (lightinseta): position and angles only (their pool is not reproduced here).
    const std::array<float, 3> o0915 = {-1279.5549316f, 40.2830467f, -557.1138916f};
    {
        auto o = sr3zone::DecodePlacement(rec(7718, -509, -1193, 0, 0, -6433, 76), o0915, nullptr, 0);
        CHECK(!o.bound);
        CHECK(std::fabs(o.position[0] - -1158.9612f) < 1e-3f);
        CHECK(std::fabs(o.position[1] - 32.3299f) < 1e-3f);
        CHECK(std::fabs(o.position[2] - -575.7545f) < 1e-3f);
        CHECK(std::fabs(o.angles[2] - -1.57056f) < 1e-4f);
        auto p = sr3zone::DecodePlacement(rec(8928, -115, 4240, -6391, 12867, 12867, 98), o0915, nullptr, 0);
        CHECK(std::fabs(p.position[0] - -1140.0549f) < 1e-3f);
        CHECK(std::fabs(p.position[1] - 38.4862f) < 1e-3f);
        CHECK(std::fabs(p.position[2] - -490.8639f) < 1e-3f);
        CHECK(std::fabs(p.angles[0] - -1.56030f) < 1e-4f);
        CHECK(std::fabs(p.angles[1] - 3.14136f) < 1e-4f);
        CHECK(std::fabs(p.angles[2] - 3.14136f) < 1e-4f);
    }
    // Rotation: all-zero = identity; yaw-only = rotation about +Y: x' = x cos c + z sin c, z' = -x sin c + z cos c.
    {
        auto I = sr3zone::PlacementRotationRows(0, 0, 0);
        const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        for (int i = 0; i < 9; ++i) CHECK(std::fabs(I[i] - id[i]) < 1e-6f);
        const float c = 0.7f;
        sr3zone::PlacedObject po;
        po.rows = sr3zone::PlacementRotationRows(0, 0, c);
        po.position = {10, 20, 30};
        auto q = sr3zone::PlacementTransformPoint(po, {2, 5, 3});
        CHECK(std::fabs(q[0] - (10 + 2 * std::cos(c) + 3 * std::sin(c))) < 1e-5f);
        CHECK(std::fabs(q[1] - 25) < 1e-5f);
        CHECK(std::fabs(q[2] - (30 - 2 * std::sin(c) + 3 * std::cos(c))) < 1e-5f);
        // Orthonormal for a general triple.
        auto R = sr3zone::PlacementRotationRows(-0.06274f, -0.21338f, -1.42480f);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                float d = R[i * 3] * R[j * 3] + R[i * 3 + 1] * R[j * 3 + 1] + R[i * 3 + 2] * R[j * 3 + 2];
                CHECK(std::fabs(d - (i == j ? 1.0f : 0.0f)) < 1e-5f);
            }
    }
    // Binding rejections: offset in the middle of a string, past the pool, non-.lmeshx string.
    {
        std::string m;
        CHECK(!sr3zone::BindPlacementName(pool.data(), pool.size(), 5, m));
        CHECK(!sr3zone::BindPlacementName(pool.data(), pool.size(), 5000, m));
        const char other[] = "tex.tga\0";
        CHECK(!sr3zone::BindPlacementName(reinterpret_cast<const uint8_t*>(other), sizeof(other) - 1, 0, m));
    }
    if (g_fail) { std::cerr << g_fail << " failure(s)\n"; return 1; }
    std::cout << "zone_placement_test OK\n";
    return 0;
}
