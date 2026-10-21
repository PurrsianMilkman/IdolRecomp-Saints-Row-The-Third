// sr3sky::TodBaseCurves on the shipped default_district.xtbl (misc_tables.vpp_pc) against
// spec-tables-environment.md 19.1's clear-weather vectors at 12:00 and 21:00. Exits 77 without the cache.
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3sky/tod_curves.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

namespace {

int g_checks = 0, g_failures = 0;

void Near(float got, float want, const std::string& what) {
    ++g_checks;
    if (std::fabs(got - want) > 2e-3f * std::max(1.0f, std::fabs(want))) {
        ++g_failures;
        std::cerr << "MISMATCH " << what << ": got " << got << " expected " << want << "\n";
    }
}

void Rgb(const sr3sky::TodBaseCurves& c, const char* name, float h, float r, float g, float b) {
    const auto v = c.colour(name, h);
    ++g_checks;
    if (!v) {
        ++g_failures;
        std::cerr << "missing curve " << name << "\n";
        return;
    }
    const std::string w = std::string(name) + " @" + std::to_string(h);
    Near((*v)[0], r, w + " r");
    Near((*v)[1], g, w + " g");
    Near((*v)[2], b, w + " b");
}

}  // namespace

int main() {
#ifndef CRREISH_GAME_CACHE_DIR
    return 77;
#else
    const std::string dir = CRREISH_GAME_CACHE_DIR;
    if (!std::ifstream(dir + "/misc_tables.vpp_pc", std::ios::binary).good()) {
        std::cout << "misc_tables.vpp_pc absent - SKIPPED\n";
        return 77;
    }
    std::vector<uint8_t> bytes;
    try {
        auto misc = vppcache::openArchive(dir + "/misc_tables.vpp_pc");
        const vpp::Container& c = misc->container;
        for (size_t i = 0; i < c.entries().size(); ++i) {
            if (c.entries()[i].name != "default_district.xtbl") continue;
            if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
                const vpp::ByteView r = c.rawEntryBytes(i);
                bytes.assign(r.data(), r.data() + r.size());
            } else {
                bytes = c.decompressEntry(i).data;
            }
        }
    } catch (const std::exception& ex) {
        std::cout << "open failed - SKIPPED (" << ex.what() << ")\n";
        return 77;
    }
    if (bytes.empty()) {
        std::cerr << "default_district.xtbl not found\n";
        return 1;
    }
    const auto doc = sr3xtbl::ParseDocument(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const auto curves = sr3sky::TodBaseCurves::Parse(doc);
    std::cout << "default_district.xtbl: " << curves.size() << " curves\n";
    Rgb(curves, "tod_light_color", 12.0f, 16.5f, 11.42201f, 7.97113f);
    Rgb(curves, "ambient_color", 12.0f, 0.5793f, 0.72622f, 1.0f);
    Rgb(curves, "back_ambient_color", 12.0f, 0.504f, 1.32376f, 2.0f);
    Rgb(curves, "fog_color", 12.0f, 0.70638f, 1.72389f, 2.55f);
    Rgb(curves, "zenith", 12.0f, 0.08608f, 0.54912f, 1.0f);
    Rgb(curves, "east0", 12.0f, 1.89617f, 3.16555f, 4.0f);
    Rgb(curves, "tod_light_color", 21.0f, 17.0f, 7.28773f, 5.4153f);
    Rgb(curves, "ambient_color", 21.0f, 0.7454f, 0.90466f, 1.0f);
    Rgb(curves, "zenith", 21.0f, 0.0723f, 0.53192f, 0.96612f);
    Near(curves.sample("fog_density_new", 12.0f).value_or(sr3sky::Value4{})[0], 0.00125f, "fog_density_new @12");
    Near(curves.sample("fog_density_new", 21.0f).value_or(sr3sky::Value4{})[0], 0.0015f, "fog_density_new @21");
    if (g_failures) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
#endif
}
