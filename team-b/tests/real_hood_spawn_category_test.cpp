// Real-data test for the hood -> spawn category lookup (spec-ai-runtime.md
// Sec20.6.8): hood volumes from sr3_city~fawld.czn_pc (sr3_city_0.vpp_pc) and the
// category densities from spawn_info_categories.xtbl (misc_tables.vpp_pc).
// Expected values are the Sec20.6.8 test vectors. Exit 77 when archives absent.
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3airuntime/freeroam_population.h"
#include "sr3tables_progression/tables_rules.h"
#include "sr3xtbl/xtbl.h"
#include "sr3zone/hood_volumes.h"
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
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}
bool findEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            if (findEntry(c.openNested(i), name, out)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}
}  // namespace

int main() {
#ifndef CRREISH_GAME_CACHE_DIR
    return 77;
#else
    const std::string dir = CRREISH_GAME_CACHE_DIR;
    if (!std::ifstream(dir + "/sr3_city_0.vpp_pc", std::ios::binary).good() ||
        !std::ifstream(dir + "/misc_tables.vpp_pc", std::ios::binary).good()) {
        std::cout << "archives absent - SKIPPED\n";
        return 77;
    }
    std::vector<uint8_t> czn, xtbl;
    try {
        auto city = vppcache::openArchive(dir + "/sr3_city_0.vpp_pc");
        if (!findEntry(city->container, "sr3_city~fawld.czn_pc", czn)) { std::cerr << "no fawld\n"; return 1; }
        auto misc = vppcache::openArchive(dir + "/misc_tables.vpp_pc");
        if (!findEntry(misc->container, "spawn_info_categories.xtbl", xtbl)) { std::cerr << "no spawn_info_categories\n"; return 1; }
    } catch (const std::exception& ex) {
        std::cout << "open failed - SKIPPED (" << ex.what() << ")\n";
        return 77;
    }

    const auto hoods = sr3zone::LoadHoodVolumes(vpp::ByteView(czn.data(), czn.size()));
    // 33 Hood objects: 14 Water_nn, "cyberspace roads", 18 HOOD_*.
    CHECK(hoods.size() == 33);
    size_t water = 0, hood = 0;
    for (const auto& h : hoods) {
        if (h.name.rfind("Water_", 0) == 0) ++water;
        if (h.name.rfind("HOOD_", 0) == 0) ++hood;
    }
    CHECK(water == 14);
    CHECK(hood == 18);

    struct V { double x, y, z; const char* name; const char* cat; };
    const V vecs[] = {
        {-203.5, 16.33, -511.5, "HOOD_DT_01", "sp_cat_HenryMills"},
        {-320.0, 16.0, -560.0, "HOOD_DT_01", "sp_cat_HenryMills"},
        {745.326, 2.975, -980.073, "HOOD_NW_02", "sp_cat_Ashwood"},
        {640.0, 5.0, -1120.0, "HOOD_NW_02", "sp_cat_Ashwood"},
        {0.0, 0.0, 0.0, "HOOD_DT_02", "sp_cat_LorenSquare"},
        {-1500.0, 0.0, 2000.0, "Water_03", "Water"},
    };
    for (const auto& v : vecs) {
        const auto* h = sr3zone::FindHood(hoods, v.x, v.y, v.z);
        CHECK(h != nullptr);
        if (h) {
            CHECK(h->name == v.name);
            CHECK(h->spawnCategory == v.cat);
        }
    }
    {
        const auto* h = sr3zone::FindHood(hoods, -203.5, 16.33, -511.5);
        CHECK(h && h->altSpawnCategory == "sp_cat_HenryMills_Ball");
    }

    // Category densities and the Sec20.6.8 target vectors.
    const auto doc = sr3xtbl::ParseDocument(xtbl.data(), xtbl.size());
    const auto cats = sr3tables_progression::ParseSpawnInfoCategoriesTable(doc);
    auto density = [&](const std::string& name, sr3airuntime::SpawnCategoryDensity& out) {
        for (const auto& c : cats)
            if (c.name && *c.name == name) {
                out = {c.carDay.value, c.carNight.value, c.pedDay.value, c.pedNight.value};
                return true;
            }
        return false;
    };
    sr3airuntime::SpawnCategoryDensity hm, ash;
    CHECK(density("sp_cat_HenryMills", hm));
    CHECK(density("sp_cat_Ashwood", ash));
    CHECK(std::fabs(hm.carDay - 1.0f) < 1e-4 && std::fabs(hm.carNight - 0.7f) < 1e-4);
    CHECK(std::fabs(hm.pedDay - 1.0f) < 1e-4 && std::fabs(hm.pedNight - 0.5f) < 1e-4);
    CHECK(std::fabs(ash.carDay - 0.8f) < 1e-4 && std::fabs(ash.carNight - 0.5f) < 1e-4);
    // sp_cat_HenryMills: 03:00 -> 8 / 12, ped 20; 14:00 -> 12 / 18, ped 40.
    const int night = sr3airuntime::TodDensityIndex(300), day = sr3airuntime::TodDensityIndex(1400);
    CHECK(sr3airuntime::VehicleTargetLiveCount(hm, night, 0, false) == 8);
    CHECK(sr3airuntime::VehicleTargetLiveCount(hm, night, 0, true) == 12);
    CHECK(sr3airuntime::PedTargetLiveCount(hm, night, 0, 1.0f) == 20);
    CHECK(sr3airuntime::VehicleTargetLiveCount(hm, day, 0, false) == 12);
    CHECK(sr3airuntime::VehicleTargetLiveCount(hm, day, 0, true) == 18);
    CHECK(sr3airuntime::PedTargetLiveCount(hm, day, 0, 1.0f) == 40);
    // sp_cat_Ashwood 14:00 -> 9 / 14, ped 32.
    CHECK(sr3airuntime::VehicleTargetLiveCount(ash, day, 0, false) == 9);
    CHECK(sr3airuntime::VehicleTargetLiveCount(ash, day, 0, true) == 14);
    CHECK(sr3airuntime::PedTargetLiveCount(ash, day, 0, 1.0f) == 32);

    if (g_failures == 0) std::cout << "real_hood_spawn_category_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
#endif
}
