// Diagnostic (2026-10-18, freeroam ground-height investigation): compares road/sidewalk node Y
// (spec-zone-data-format.md 13.2a) against the Heightmap (0x2244, Sec13.5) read at the same x/z.
// Read-only; prints measurements. Usage: ground_height_probe <sr3_city_0.vpp_pc> [spawnX spawnZ]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3zone/road_graph.h"
#include "sr3zone/zone_heightmap.h"
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

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    double sx = -1280, sz = -560;
    if (argc >= 4) { sx = atof(argv[2]); sz = atof(argv[3]); }
    auto opened = vppcache::openArchive(argv[1]);
    std::vector<uint8_t> czn;
    if (!find(opened->container, "sr3_city~fawld.czn_pc", czn)) { std::printf("no fawld\n"); return 1; }
    sr3zone::RoadGraph g = sr3zone::RoadGraph::Parse(vpp::ByteView(czn.data(), czn.size()));
    struct N { double x, y, z; bool side; };
    std::vector<N> nodes;
    for (auto& n : g.nodes) nodes.push_back({n.x, n.y, n.z, false});
    for (auto& n : g.sidewalkNodes) nodes.push_back({n.pos.x, n.pos.y, n.pos.z, true});
    auto tileOf = sr3zone::RoadTileOf(sx, sz);
    std::printf("spawn (%.1f,%.1f) road-lattice tile %02d%02d\n", sx, sz, tileOf.col, tileOf.row);
    std::vector<std::pair<double, size_t>> d;
    for (size_t i = 0; i < nodes.size(); ++i) d.push_back({std::hypot(nodes[i].x - sx, nodes[i].z - sz), i});
    std::sort(d.begin(), d.end());
    for (int k = 0; k < 8; ++k) {
        auto& n = nodes[d[k].second];
        std::printf("  nearest[%d] %s dist %.1f m  pos (%.1f, %.2f, %.1f)\n", k, n.side ? "sidewalk" : "road    ", d[k].first, n.x, n.y, n.z);
    }
    for (double R : {100.0, 300.0}) {
        std::vector<double> ys;
        for (auto& n : nodes) if (std::hypot(n.x - sx, n.z - sz) <= R) ys.push_back(n.y);
        std::sort(ys.begin(), ys.end());
        if (ys.empty()) { std::printf("  within %.0fm: none\n", R); continue; }
        std::printf("  nodes within %.0fm: n=%zu Y min %.2f median %.2f max %.2f\n", R, ys.size(), ys.front(), ys[ys.size() / 2], ys.back());
    }
    std::map<std::string, std::vector<sr3zone::HeightmapRecord>> hm;
    auto getHm = [&](int col, int row, const char* fam) -> const sr3zone::HeightmapRecord* {
        char nm[64]; std::snprintf(nm, sizeof nm, "sr3_city~%s%02d%02d.czn_pc", fam, col, row);
        auto it = hm.find(nm);
        if (it == hm.end()) {
            std::vector<uint8_t> b;
            std::vector<sr3zone::HeightmapRecord> v;
            if (find(opened->container, nm, b))
                v = sr3zone::Heightmap::locate(vpp::ByteView(b.data(), b.size()), sr3zone::HeightmapContainerKind::Ordinary);
            it = hm.emplace(nm, v).first;
        }
        return it->second.empty() ? nullptr : &it->second.front();
    };
    std::vector<size_t> pick;
    for (auto& pr : d) { if (pr.first > 450) break; pick.push_back(pr.second); }
    size_t step = std::max<size_t>(1, pick.size() / 24);
    std::printf("\n%-9s %9s %9s %7s | %5s %7s %7s | %5s %7s %7s\n", "kind", "x", "z", "nodeY", "tile", "hm~f", "dY", "tile", "hm~s", "dY");
    std::vector<double> dfv, dsv;
    int shown = 0;
    for (size_t i = 0; i < pick.size() && shown < 24; i += step, ++shown) {
        auto& n = nodes[pick[i]];
        auto t = sr3zone::RoadTileOf(n.x, n.z);
        float hf = 0, hs = 0;
        const auto* rf = getHm(t.col, t.row, "f");
        const auto* rs = getHm(t.col, t.row, "s");
        bool okf = rf && sr3zone::SampleHeightmapHeight(*rf, n.x, n.z, hf);
        bool oks = rs && sr3zone::SampleHeightmapHeight(*rs, n.x, n.z, hs);
        std::printf("%-9s %9.1f %9.1f %7.2f | %02d%02d ", n.side ? "sidewalk" : "road", n.x, n.z, n.y, t.col, t.row);
        if (okf) { std::printf("%7.1f %7.1f | ", hf, hf - n.y); dfv.push_back(hf - n.y); } else std::printf("%7s %7s | ", rf ? "oob" : "none", "-");
        std::printf("%02d%02d ", t.col, t.row);
        if (oks) { std::printf("%7.1f %7.1f\n", hs, hs - n.y); dsv.push_back(hs - n.y); } else std::printf("%7s %7s\n", rs ? "oob" : "none", "-");
    }
    auto stat = [](const char* nm, std::vector<double> v) {
        if (v.empty()) { std::printf("%s: no samples\n", nm); return; }
        std::sort(v.begin(), v.end());
        double s = 0; for (double x : v) s += x;
        std::printf("%s: n=%zu dY(hm-node) min %.1f p25 %.1f median %.1f p75 %.1f max %.1f mean %.1f\n", nm, v.size(), v.front(),
                    v[v.size() / 4], v[v.size() / 2], v[v.size() * 3 / 4], v.back(), s / v.size());
    };
    stat("heightmap ~f", dfv);
    stat("heightmap ~s", dsv);
    if (argc >= 5 && std::string(argv[4]) == "survey") {
        std::map<std::pair<int,int>, int> tiles;
        for (auto& n : nodes) { auto t = sr3zone::RoadTileOf(n.x, n.z); tiles[{t.col, t.row}]++; }
        for (auto& kv : tiles) {
            for (const char* fam : {"f", "s"}) {
                char nm[64]; std::snprintf(nm, sizeof nm, "sr3_city~%s%02d%02d.czn_pc", fam, kv.first.first, kv.first.second);
                getHm(kv.first.first, kv.first.second, fam);
                auto& v = hm[nm];
                if (v.empty()) continue;
                int mn = 99999, mx = -99999; size_t nm5 = 0;
                for (auto& rec : v) for (auto h : rec.heights) { mn = std::min<int>(mn, h); mx = std::max<int>(mx, h); if (h == -5) ++nm5; }
                std::printf("TILE %02d%02d ~%s records=%zu rec0 origin(%d,%d) %ux%u  min %d max %d  count(-5)=%zu/%zu\n", kv.first.first, kv.first.second, fam, v.size(), v[0].originX, v[0].originZ, v[0].gridW, v[0].gridH, mn, mx, nm5, v[0].heights.size() * v.size());
            }
        }
    }
    // Global survey: every road+sidewalk node vs its tile's ~f / ~s heightmap.
    if (argc >= 5 && std::string(argv[4]) == "survey") {
        for (const char* fam : {"f", "s"}) {
            std::vector<double> xs, ys, dd;
            size_t none = 0, flatm5 = 0, total = 0;
            for (auto& n : nodes) {
                auto t = sr3zone::RoadTileOf(n.x, n.z);
                ++total;
                const auto* r = getHm(t.col, t.row, fam);
                float h = 0;
                if (!r || !sr3zone::SampleHeightmapHeight(*r, n.x, n.z, h)) { ++none; continue; }
                if (h == -5.0f) { ++flatm5; continue; }
                xs.push_back(n.y); ys.push_back(h); dd.push_back(h - n.y);
            }
            double mx = 0, my = 0; for (size_t i = 0; i < xs.size(); ++i) { mx += xs[i]; my += ys[i]; }
            mx /= xs.size(); my /= xs.size();
            double sxx = 0, syy = 0, sxy = 0; for (size_t i = 0; i < xs.size(); ++i) { sxx += (xs[i]-mx)*(xs[i]-mx); syy += (ys[i]-my)*(ys[i]-my); sxy += (xs[i]-mx)*(ys[i]-my); }
            std::sort(dd.begin(), dd.end());
            std::printf("SURVEY ~%s: nodes=%zu no-record/oob=%zu exactly-(-5)=%zu used=%zu | corr(nodeY,hm)=%.3f slope=%.3f intercept=%.2f | hm-node: min %.1f p10 %.1f median %.1f p90 %.1f max %.1f | frac(hm>=node)=%.3f\n",
                        fam, total, none, flatm5, xs.size(), sxy / std::sqrt(sxx * syy), sxy / sxx, my - sxy / sxx * mx,
                        dd.front(), dd[dd.size()/10], dd[dd.size()/2], dd[dd.size()*9/10], dd.back(),
                        (double)(dd.end() - std::lower_bound(dd.begin(), dd.end(), 0.0)) / dd.size());
        }
    }
    if (argc >= 5 && std::string(argv[4]) == "survey") {
        // lower-envelope test: min of ~s heightmap over the +/-R metre neighbourhood vs node Y
        for (double R : {0.0, 10.0, 20.0, 40.0}) {
            std::vector<double> dd;
            for (auto& n : nodes) {
                auto t = sr3zone::RoadTileOf(n.x, n.z);
                const auto* r = getHm(t.col, t.row, "s");
                if (!r) continue;
                float best = 1e9f; bool any = false;
                for (double ox = -R; ox <= R; ox += 10.0) for (double oz = -R; oz <= R; oz += 10.0) {
                    float h; if (sr3zone::SampleHeightmapHeight(*r, n.x + ox, n.z + oz, h)) { best = std::min(best, h); any = true; }
                }
                if (any) dd.push_back(best - n.y);
            }
            std::sort(dd.begin(), dd.end());
            std::printf("ENVELOPE ~s min over +/-%.0fm: n=%zu (minhm - nodeY) min %.1f p10 %.1f p25 %.1f median %.1f p75 %.1f p90 %.1f max %.1f\n", R, dd.size(),
                        dd.front(), dd[dd.size()/10], dd[dd.size()/4], dd[dd.size()/2], dd[dd.size()*3/4], dd[dd.size()*9/10], dd.back());
        }
    }
    for (const char* fam : {"f", "s"}) {
        const auto* r = getHm(tileOf.col, tileOf.row, fam);
        if (!r) { std::printf("tile %02d%02d ~%s: no heightmap record\n", tileOf.col, tileOf.row, fam); continue; }
        int mn = 99999, mx = -99999; for (auto h : r->heights) { mn = std::min<int>(mn, h); mx = std::max<int>(mx, h); }
        std::printf("tile %02d%02d ~%s heightmap: origin (%d,%d) grid %ux%u raw min %d max %d\n", tileOf.col, tileOf.row, fam, r->originX, r->originZ, r->gridW, r->gridH, mn, mx);
        for (int row = 0; row < r->gridH; row += 4) {
            std::printf("   row %2d:", row);
            for (int col = 0; col < r->gridW; col += 4) std::printf(" %5d", r->heights[row * r->gridW + col]);
            std::printf("\n");
        }
    }
    return 0;
}
