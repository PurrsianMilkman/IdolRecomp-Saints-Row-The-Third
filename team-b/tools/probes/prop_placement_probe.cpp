// Diagnostic (2026-10-18, prop-placement-Y investigation): for a tile, compares the SR3Z placement records of
// sr3_city~f<CCRR>.czh_pc (spec-world-streaming.md Sec10.7: s16/64 m + header origin) with the world-space tail
// collision mesh of sr3_city~s<CCRR>.czn_pc (spec-zone-data-format.md Sec13.5.1). Read-only.
// Usage: prop_placement_probe <sr3_city_0.vpp_pc> <CCRR> [<CCRR> ...]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sr3zone/zone_collision_mesh.h"
#include "sr3zone/zone_header.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

static bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated || r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}
static bool find(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try { if (find(c.openNested(i), name, out)) return true; } catch (const std::exception&) {}
    }
    return false;
}
static bool groundHit(const std::vector<std::array<float, 3>>& t, double x, double z, double y0, double& outY) {
    bool found = false;
    for (size_t i = 0; i + 2 < t.size(); i += 3) {
        const auto &a = t[i], &b = t[i + 1], &c = t[i + 2];
        const double d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
        if (std::fabs(d) < 1e-12) continue;
        const double l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d;
        const double l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d;
        const double l3 = 1.0 - l1 - l2;
        if (l1 < 0 || l2 < 0 || l3 < 0) continue;
        const double y = l1 * a[1] + l2 * b[1] + l3 * c[1];
        if (y > y0 + 0.05) continue;
        if (!found || y > outY) { found = true; outY = y; }
    }
    return found;
}
static double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, (size_t)(p * (v.size() - 1) + 0.5))];
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    auto opened = vppcache::openArchive(argv[1]);
    for (int a = 2; a < argc; ++a) {
        std::string t = argv[a];
        std::vector<uint8_t> czh, czn;
        std::printf("=== tile %s ===\n", t.c_str());
        if (!find(opened->container, "sr3_city~f" + t + ".czh_pc", czh)) { std::printf("no ~f czh\n"); continue; }
        auto zh = sr3zone::ZoneHeader::parse(vpp::ByteView(czh.data(), czh.size()));
        std::printf("czh: records=%u origin=(%.2f,%.2f,%.2f)\n", zh.recordCount(), zh.headerOrigin()[0],
                    zh.headerOrigin()[1], zh.headerOrigin()[2]);
        std::vector<std::array<float, 3>> P;
        for (auto& r : zh.records()) {
            auto p = sr3zone::RecordPosition(r);
            P.push_back({p[0] + zh.headerOrigin()[0], p[1] + zh.headerOrigin()[1], p[2] + zh.headerOrigin()[2]});
        }
        std::vector<double> xs, ys, zs;
        for (auto& p : P) { xs.push_back(p[0]); ys.push_back(p[1]); zs.push_back(p[2]); }
        std::printf("records world (origin+pos): x[%.1f..%.1f] y[%.2f..%.2f] (p10 %.2f med %.2f p90 %.2f) z[%.1f..%.1f]\n",
                    pct(xs, 0), pct(xs, 1), pct(ys, 0), pct(ys, 1), pct(ys, .1), pct(ys, .5), pct(ys, .9), pct(zs, 0), pct(zs, 1));
        if (!find(opened->container, "sr3_city~s" + t + ".czn_pc", czn)) { std::printf("no ~s czn\n"); continue; }
        std::vector<sr3zone::CollisionMesh> m;
        if (!sr3zone::LocateCollisionMeshes(vpp::ByteView(czn.data(), czn.size()), m) || m.empty()) { std::printf("no collision mesh\n"); continue; }
        const auto& cm = m[0];
        std::printf("collision AABB: x[%.1f..%.1f] y[%.2f..%.2f] z[%.1f..%.1f]\n", cm.bmin[0], cm.bmax[0], cm.bmin[1], cm.bmax[1], cm.bmin[2], cm.bmax[2]);
        auto tris = sr3zone::BuildKeyTriangles(cm);
        std::vector<double> dy;
        size_t hit = 0, inAabb = 0;
        for (auto& p : P) {
            if (p[0] < cm.bmin[0] || p[0] > cm.bmax[0] || p[2] < cm.bmin[2] || p[2] > cm.bmax[2]) continue;
            ++inAabb;
            double g;
            if (groundHit(tris, p[0], p[2], cm.bmax[1] + 1.0, g)) { ++hit; dy.push_back(p[1] - g); }
        }
        std::printf("records inside collision XZ AABB: %zu/%zu; with a topmost-surface hit: %zu\n", inAabb, P.size(), hit);
        if (!dy.empty())
            std::printf("recordY - topmostCollisionY: min %.2f p10 %.2f med %.2f p90 %.2f max %.2f  (|dy|<1m: %zu)\n", pct(dy, 0), pct(dy, .1),
                        pct(dy, .5), pct(dy, .9), pct(dy, 1), (size_t)std::count_if(dy.begin(), dy.end(), [](double d) { return std::fabs(d) < 1.0; }));
    }
    return 0;
}
