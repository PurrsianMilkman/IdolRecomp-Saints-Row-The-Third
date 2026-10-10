// Real-data test for ResolveDriverHideOccupant()/FindSeatByResolverIndex()
// (vehicle-occupant-hide-flag task, 2026-10-09, spec-render-pipeline.md
// Sec23.14, citing spec-tables-vehicle-world.md Sec3.2's `+0x09` flags-
// byte-2 bit6): loads the REAL `vehicle_interaction_info.xtbl` out of
// `misc_tables.vpp_pc` in CRREISH_GAME_CACHE_DIR (exit 77 - ctest SKIPPED,
// never a pass - when the archive is absent; nothing from it is copied into
// the repository, same convention every other real_*_test uses) and checks,
// against the real shipped data, the exact claim this task's own render-
// condition change depends on:
//
//   1. Exactly 2 of the real rows have `Hide Occupant` set on their DRIVER
//      seat, and they are named "Tank NatGrd" and "Tank STAG" - not just
//      "some 2 rows", the SAME 2 real rows spec-render-pipeline.md Sec23.14
//      names.
//   2. Both tanks' own gunner seat (resolver index 1, "Passenger 1") does
//      NOT have Hide Occupant - the driver-only asymmetry the spec's own
//      "both tanks" finding depends on.
//   3. Every OTHER real row (all 49 remaining base rows) resolves to
//      hideOccupant == false on its own driver seat - a full real-data
//      census, not just a spot check of one ordinary vehicle, directly
//      backing this task's "shown is the correct default" claim.
//
// Mirrors tests/real_vehicle_engine_torque_crossmap_test.cpp's own
// archive-open/skip-when-absent shape.
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "sr3tables_vehicle_world/tables.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

#include <filesystem>

using namespace sr3tables_vehicle_world;
using namespace sr3xtbl;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

}  // namespace

int main() {
    const fs::path archivePath = fs::path(CRREISH_GAME_CACHE_DIR) / "misc_tables.vpp_pc";
    if (!fs::exists(archivePath)) {
        std::cout << "SKIP: " << archivePath.string() << " not present\n";
        return 77;
    }

    auto opened = vppcache::openArchive(archivePath);
    const vpp::Container& top = opened->container;

    std::vector<uint8_t> xtblBytes;
    bool found = false;
    for (size_t i = 0; i < top.entries().size(); ++i) {
        if (top.entries()[i].name != "vehicle_interaction_info.xtbl") continue;
        vpp::DecompressResult r = top.decompressEntry(i);
        if (r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
            r.status == vpp::DecodeStatus::ContentValidated) {
            xtblBytes = std::move(r.data);
            found = true;
        }
        break;
    }
    if (!found) {
        std::cout << "SKIP: vehicle_interaction_info.xtbl not found/decoded in " << archivePath.string() << "\n";
        return 77;
    }

    Document doc = ParseDocument(xtblBytes.data(), xtblBytes.size());
    const std::vector<VehicleInteractionInfo> rows = ParseVehicleInteractionInfoTable(doc);
    std::cout << "Loaded " << rows.size() << " real Vehicle_Interaction_Info rows from " << archivePath.string()
              << "\n";
    // spec-tables-vehicle-world.md Sec3.3: 51 real rows in the base misc_tables.vpp_pc copy.
    CHECK(rows.size() == 51);

    std::vector<std::string> hidingRowNames;
    for (const auto& row : rows) {
        if (!row.name) continue;
        if (ResolveDriverHideOccupant(rows, *row.name)) hidingRowNames.push_back(*row.name);
    }
    std::cout << "Rows whose driver seat has Hide Occupant: ";
    for (const auto& n : hidingRowNames) std::cout << "'" << n << "' ";
    std::cout << "\n";

    // Check 1: exactly the 2 real tank rows, by name - not a count coincidence.
    CHECK(hidingRowNames.size() == 2);
    CHECK(std::find(hidingRowNames.begin(), hidingRowNames.end(), "Tank NatGrd") != hidingRowNames.end());
    CHECK(std::find(hidingRowNames.begin(), hidingRowNames.end(), "Tank STAG") != hidingRowNames.end());

    // Check 2: both tanks' own gunner seat (index 1) does NOT hide.
    for (const char* tankName : {"Tank NatGrd", "Tank STAG"}) {
        const VehicleInteractionInfo* row = nullptr;
        for (const auto& r : rows) {
            if (r.name && *r.name == tankName) { row = &r; break; }
        }
        CHECK(row != nullptr);
        if (!row) continue;
        const VehicleInteractionSeatInfo* gunner = FindSeatByResolverIndex(*row, /*seatIndex=*/1);
        CHECK(gunner != nullptr && !gunner->hideOccupant);
    }

    // Check 3: full real-data census - every other row's own driver seat
    // (all 49 remaining real base rows) resolves to "shown", not just one
    // spot-checked ordinary vehicle.
    int otherRowsChecked = 0;
    for (const auto& row : rows) {
        if (!row.name) continue;
        if (*row.name == "Tank NatGrd" || *row.name == "Tank STAG") continue;
        ++otherRowsChecked;
        CHECK(ResolveDriverHideOccupant(rows, *row.name) == false);
    }
    std::cout << otherRowsChecked << " non-tank rows checked, all resolve to driver-seat Hide Occupant = false\n";
    CHECK(otherRowsChecked == 49);

    std::cout << "\n" << g_failures << " failures.\n";
    return g_failures == 0 ? 0 : 1;
}
