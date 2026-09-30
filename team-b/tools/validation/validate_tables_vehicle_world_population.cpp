// Population diagnostic for sr3tables_vehicle_world (include/sr3tables_vehicle_world/,
// src/tables_vehicle_world.cpp) over the REAL shipped game archives.
//
// STANDALONE - deliberately NOT wired into CMakeLists.txt (per the task that
// produced this file). Build it yourself, e.g.:
//   cl /std:c++17 /EHsc /I include ^
//      src/tables_vehicle_world.cpp src/xtbl.cpp src/container.cpp ^
//      src/payload_locator.cpp src/format.cpp src/byte_view.cpp src/hash.cpp ^
//      tools/validation/validate_tables_vehicle_world_population.cpp ^
//      /Fe:validate_tables_vehicle_world_population.exe
// Usage:
//   validate_tables_vehicle_world_population <archive.vpp_pc> [...]
//   (pass every archive under packfiles/pc/cache/*.vpp_pc - real game data,
//   read-only, per this task's own instructions; this tool does not hardcode
//   a cache path)
//
// What this reports (the task's own explicit asks):
//   G1  LOCATE - for each of this group's 24 target filenames (case-
//       insensitive, exact name only - this group's tables do not use the
//       dlc1_/dlc2_/dlc3_ framework-prefix convention documented for OTHER
//       groups' tables, and nothing in spec-tables-vehicle-world.md states
//       that convention for this group's own DLC entry points), EVERY
//       archive location it was found at - not just the first. A past bug in
//       this project undercounted tables that exist in more than one archive
//       (e.g. a patch archive superseding a base one); this harness
//       deliberately keeps and reports every copy found, across every
//       archive path given on argv, through every nested container.
//   G2  POPULATION - for each table found, this reader's own row count
//       (summed across every found+decoded copy, so a table present in two
//       archives is NOT undercounted), reported next to whatever
//       spec-tables-vehicle-world.md's own validation sections (2.2, 3.3,
//       4.2, 5.2, 8.1/8.2, 9.3, 10, 11.2, 12.3, 13, 14.2, 15.2, 16.2, 17.2,
//       18.2, 19.2, and the section-22 summary table) state as the REAL
//       base-game row count the spec's own author already found. This is a
//       cross-check of this reader against the spec's own reported figures,
//       not a re-derivation of them - the actual game archives may differ
//       from the spec author's snapshot (different game version/patch
//       level), which is exactly the kind of thing this diagnostic exists to
//       surface, not hide.
//   G3  parser smoke check - every found, decoded copy of a target table
//       must parse without a FormatError.
//
// Cleanroom note: this tool opens and reads real .vpp_pc GAME DATA archives
// (not the game executable, not disassembly, not decompiled code) - explicitly
// permitted by the task ("read-only, fair game for testing (it's game DATA,
// not code)"). It links only this project's own clean-room vpp/xtbl/
// tables_vehicle_world code.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "sr3tables_vehicle_world/tables.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using sr3xtbl::Node;

int g_fail = 0;
#define GATE(ok, ...)                                 \
    do {                                               \
        const bool ok_ = (ok);                         \
        std::printf("  [%s] ", ok_ ? "PASS" : "FAIL"); \
        std::printf(__VA_ARGS__);                       \
        std::printf("\n");                               \
        if (!ok_) ++g_fail;                                \
    } while (0)

Bytes readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0);
    Bytes b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string baseName(const std::string& p) {
    const size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? p : p.substr(s + 1);
}
bool endsWithCi(const std::string& s, const char* x) {
    const size_t n = std::strlen(x);
    const std::string ls = lower(s);
    return ls.size() >= n && ls.compare(ls.size() - n, n, x) == 0;
}

// ---------------------------------------------------------------------------
// Archive walk (same recursive vpp::Container pattern as
// validate_tables_environment_population.cpp's G1 / validate_xtbl_population.cpp).
// ---------------------------------------------------------------------------
struct Item {
    std::string archiveChain;     // "topLevel.vpp_pc/nested.str2_pc/..."
    std::string topLevelArchive;  // just the top-level .vpp_pc file name (as given on argv)
    std::string name;             // the entry's own file name
    bool compressed = false;
    int status = 0;  // 0 = decoded / readable
    Bytes data;
};

std::vector<Item> g_items;
long long g_containers = 0;

void walk(vpp::ByteView bytes, const std::string& chain, const std::string& topLevel) {
    vpp::Container c(bytes);
    ++g_containers;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        if (!endsWithCi(e.name, "xtbl")) continue;
        Item it;
        it.archiveChain = chain;
        it.topLevelArchive = topLevel;
        it.name = e.name;
        it.compressed = e.payload.kind == vpp::PayloadKind::Compressed;
        if (it.compressed) {
            vpp::DecompressResult r = c.decompressEntry(i);
            it.status = static_cast<int>(r.status);
            if (r.status == vpp::DecodeStatus::Ok) it.data = std::move(r.data);
        } else {
            try {
                vpp::ByteView v = c.rawEntryBytes(i);
                it.data.assign(v.data(), v.data() + v.size());
            } catch (const std::exception&) {
                it.status = -1;
            }
        }
        g_items.push_back(std::move(it));
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        if (endsWithCi(c.entries()[i].name, "xtbl")) continue;
        try {
            walk(c.rawEntryBytes(i), chain + "/" + c.entries()[i].name, topLevel);
        } catch (const std::exception&) {
        }
    }
}

struct Found {
    const Item* item;
};

// Exact name match only (case-insensitive). Unlike some other groups' tables
// (spec-tables-environment.md's dlc1_/dlc2_/dlc3_ prefix convention),
// spec-tables-vehicle-world.md never documents a filename-prefix DLC
// convention for THIS group's own tables - each table's DLC entry point
// (where one is named at all: level_objects, items_3d, contacts_sr3) is a
// SEPARATE loader function, not a separate on-disk filename. So this
// deliberately does NOT try dlc1_/dlc2_/dlc3_ prefixes; a real archive that
// used one would simply show up as "not found" here, which is itself a
// useful, honestly-reported finding rather than a guess.
std::vector<Found> findMatches(const std::string& canonical) {
    std::vector<Found> out;
    const std::string lc = lower(canonical);
    for (const Item& it : g_items) {
        if (lower(it.name) == lc) out.push_back({&it});
    }
    return out;
}

// ---------------------------------------------------------------------------
// One entry per target file. `countRows` runs this library's own
// ParseXxxTable (or ParseXxx returning optional<T> for a single-block table)
// over a parsed Document and returns the row count it sees - reusing the
// real reader rather than re-implementing navigation logic here.
// `specRowCount` / `specNote` are spec-tables-vehicle-world.md's OWN stated
// real-base-game figures (section 22's summary table plus each section's own
// "Validation" subsection), copied verbatim as a reference point - not
// re-derived by this tool.
// ---------------------------------------------------------------------------
struct TableSpec {
    std::string canonicalFile;
    std::string section;
    long long specRowCount;  // -1 = spec gives no single "N rows" figure for this table
    std::string specNote;
    long long (*countRows)(const sr3xtbl::Document&);
};

long long countVehicleInteractionAnimationSet(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseVehicleInteractionAnimationSetTable(d).size());
}
long long countVehicleInteractionInfo(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseVehicleInteractionInfoTable(d).size());
}
long long countInteractionPointSets(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseInteractionPointSetsTable(d).size());
}
long long countWheelGroups(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseWheelGroupsTable(d).size());
}
long long countVehicleAnimModifiers(const sr3xtbl::Document& d) {
    std::optional<sr3tables_vehicle_world::VehicleAnimModifiers> m = sr3tables_vehicle_world::ParseVehicleAnimModifiers(d);
    return static_cast<long long>(m ? m->vehicles.size() : 0);
}
long long countExternalizedComponents(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseExternalizedVehicleComponentsTable(d).size());
}
long long countVehicleCustSlots(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseVehicleCustSlotsTable(d).size());
}
long long countVehicleCustInterface(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseVehicleCustInterfaceTable(d).size());
}
long long countColorPool(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseVehicleCustColorPoolTable(d).size());
}
long long countColorSets(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseVehicleCustColorSetsTable(d).size());
}
long long countVehicleSurfing(const sr3xtbl::Document& d) {
    return sr3tables_vehicle_world::ParseVehicleSurfing(d).has_value() ? 1 : 0;
}
long long countLightSet(const sr3xtbl::Document& d) {
    return sr3tables_vehicle_world::ParseLightSetTable(d).has_value() ? 1 : 0;
}
long long countLevelObjects(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseLevelObjectsTable(d).size());
}
long long countProps(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParsePropsTable(d).size());
}
long long countTriggers(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseTriggersTable(d).size());
}
long long countItemsInventory(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseItemsInventoryTable(d).size());
}
long long countItems3D(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseItems3DTable(d).size());
}
long long countContacts(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseContactsTable(d).size());
}
long long countActivityPersonaReplacement(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseActivityPlayerPersonaReplacementTable(d).size());
}
long long countAirplaneCurves(const sr3xtbl::Document& d) {
    return static_cast<long long>(sr3tables_vehicle_world::ParseAirplaneTakeoffLandCurvesTable(d).size());
}

const std::vector<TableSpec>& tableSpecs() {
    static const std::vector<TableSpec> specs = {
        {"vi_enter.xtbl", "2.2", 106, "106 Vehicle_Interaction_Animation_Set rows / 657 Elements", countVehicleInteractionAnimationSet},
        {"vi_exit.xtbl", "2.2", 99, "99 rows / 813 Elements", countVehicleInteractionAnimationSet},
        {"vi_ride.xtbl", "2.2", 69, "69 rows / 454 Elements", countVehicleInteractionAnimationSet},
        {"vehicle_interaction_info.xtbl", "3.3", 51, "51 Vehicle_Interaction_Info rows / 209 Seat_Info Elements", countVehicleInteractionInfo},
        {"vehicle_interaction_point_sets.xtbl", "4.2", 130, "130 Interaction_Point_Set rows / 843 Set_Elements", countInteractionPointSets},
        {"vehicle_wheel_groups.xtbl", "5.2", 7, "7 Wheel_Group rows / 51 Rim_Element / 0 S_Element (base ships no spinners)", countWheelGroups},
        {"vehicle_animation_modifiers.xtbl", "6.2", -1, "5,759 bytes extracted; not per-field validated by the spec (a patching table, no independent row array)", countVehicleAnimModifiers},
        {"externalized_vehicle_components.xtbl", "7.2", -1, "133,707 bytes extracted; not structurally validated by the spec beyond byte-length", countExternalizedComponents},
        {"vehicle_cust_slots.xtbl", "8.1", 144, "144 Vehicle_slot rows", countVehicleCustSlots},
        {"vehicle_cust_interface.xtbl", "8.2", 4, "4 vehicle_cust_interface rows / 77 slots/slots/name children", countVehicleCustInterface},
        {"vehicle_cust_color_pool.xtbl", "9.3", 694, "694 Color rows", countColorPool},
        {"vehicle_cust_color_sets.xtbl", "9.3", 29, "29 Color_Set rows / 874 Color_Element children", countColorSets},
        {"vehicle_surfing_style_two.xtbl", "10", 1, "single global Vehicle_Surfing element (not a repeated row table)", countVehicleSurfing},
        {"Vehicle-Customization-Lightset.xtbl", "11.2", 1, "1 LightSet row", countLightSet},
        {"store_gang_lightset.xtbl", "11.2", 1, "1 LightSet row", countLightSet},
        {"test_shop_light_01.xtbl", "11.2", 1, "1 LightSet row", countLightSet},
        {"level_objects.xtbl", "12.3", 149, "149 Level_Object rows / 357 Flag children", countLevelObjects},
        {"props.xtbl", "13", 14, "14 Activity rows (all 14 recognised names)", countProps},
        {"triggers.xtbl", "14.2", 20, "20 Trigger rows / 10 Flag children", countTriggers},
        {"items_inventory.xtbl", "15.2", 94, "94 Inventory_Item rows (of 110-slot capacity)", countItemsInventory},
        {"items_3d.xtbl", "16.2", 391, "391 Item rows (partial schema - see tables.h)", countItems3D},
        {"contacts_sr3.xtbl", "17.2", 31, "31 Contact rows (of 35-slot capacity)", countContacts},
        {"activity_player_persona_replacement.xtbl", "18.2", 3, "3 Activity_Persona_Replacement rows / 6 Persona_Replacement children", countActivityPersonaReplacement},
        {"airplane_takeoff_land_curves.xtbl", "19.2", 2, "2 Curve_Params rows (Landing / Take Off; 11 total Point children)", countAirplaneCurves},
    };
    return specs;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        std::printf("usage: validate_tables_vehicle_world_population <archive.vpp_pc> [...]\n");
        return 2;
    }

    for (const std::string& a : archives) {
        Bytes b = readFile(a);
        if (b.empty()) {
            std::printf("cannot read %s\n", a.c_str());
            continue;
        }
        const std::string top = baseName(a);
        try {
            walk(vpp::ByteView(b.data(), b.size()), top, top);
        } catch (const std::exception& e) {
            std::printf("open failed %s: %s\n", a.c_str(), e.what());
        }
    }

    std::printf("=== G1 locate ===\n");
    std::printf("archives given: %zu, containers walked: %lld, xtbl-family entries seen: %zu\n", archives.size(), g_containers,
                g_items.size());
    GATE(g_containers > 0, "CONTROL: at least one container was actually opened (not a silent no-op run)");

    long long tablesFound = 0, tablesNotFound = 0, tablesInMultipleArchives = 0;
    long long parseAttempts = 0, parseFailures = 0;
    std::vector<sr3xtbl::Document> keepAlive;  // Node* point into these; keep alive for the whole run

    for (const TableSpec& spec : tableSpecs()) {
        std::printf("--- %s (spec section %s) ---\n", spec.canonicalFile.c_str(), spec.section.c_str());
        std::vector<Found> matches = findMatches(spec.canonicalFile);
        if (matches.empty()) {
            std::printf("  NOT FOUND in any given archive.\n");
            ++tablesNotFound;
            continue;
        }
        ++tablesFound;

        // Every archive location this table was found at - NOT just the
        // first (the specific past-bug class this harness exists to catch).
        std::printf("  found in %zu location(s):\n", matches.size());
        for (const Found& f : matches) {
            std::printf("    %s  (top-level archive: %s)  %s  status=%d\n", f.item->archiveChain.c_str(),
                        f.item->topLevelArchive.c_str(), f.item->compressed ? "[compressed]" : "[raw]", f.item->status);
        }
        {
            std::set<std::string> topLevels;
            for (const Found& f : matches) topLevels.insert(lower(f.item->topLevelArchive));
            if (matches.size() > 1) {
                ++tablesInMultipleArchives;
                std::printf("  NOTE: %zu copies found across %zu distinct top-level archive(s) - a patch/DLC archive may\n",
                            matches.size(), topLevels.size());
                std::printf("        supersede a base one; every copy is counted below, not just the first.\n");
            }
        }

        // Parse and count rows across EVERY found, successfully-decoded copy.
        long long totalRows = 0;
        for (const Found& f : matches) {
            if (f.item->status != 0) {
                std::printf("  (skipped, decode status != 0: %s)\n", f.item->name.c_str());
                continue;
            }
            ++parseAttempts;
            long long rows = -1;
            try {
                keepAlive.push_back(sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size()));
                const sr3xtbl::Document& doc = keepAlive.back();
                rows = spec.countRows(doc);
                totalRows += rows;
            } catch (const sr3xtbl::FormatError& e) {
                ++parseFailures;
                std::printf("  PARSE FAILED %s (%s): %s\n", f.item->name.c_str(), f.item->archiveChain.c_str(), e.what());
                continue;
            }
            std::printf("    rows in %s: %lld\n", f.item->archiveChain.c_str(), rows);
        }
        std::printf("  TOTAL rows across all found copies: %lld\n", totalRows);
        if (spec.specRowCount >= 0) {
            std::printf("  spec-tables-vehicle-world.md %s states: %s (%lld)\n", spec.section.c_str(), spec.specNote.c_str(),
                        spec.specRowCount);
            std::printf("  %s\n", totalRows == spec.specRowCount ? "MATCHES the spec's own figure exactly"
                                                                   : "DIFFERS from the spec's own figure (see note above; a real, "
                                                                     "honestly-reported difference - possibly a different game/patch "
                                                                     "version than the spec author's snapshot, or a genuine reader gap)");
        } else {
            std::printf("  spec-tables-vehicle-world.md %s states: %s (no single row-count figure given)\n", spec.section.c_str(),
                        spec.specNote.c_str());
        }
    }

    std::printf("=== G3 parser smoke check ===\n");
    std::printf("parse attempts: %lld, FormatError failures: %lld\n", parseAttempts, parseFailures);
    if (parseAttempts > 0)
        GATE(parseFailures == 0, "every found, decoded copy of a target table parses without FormatError: %lld / %lld",
             parseAttempts - parseFailures, parseAttempts);

    std::printf("=== Summary ===\n");
    std::printf("targets: %zu; found: %lld; not found: %lld; found in >1 archive location: %lld\n", tableSpecs().size(), tablesFound,
                tablesNotFound, tablesInMultipleArchives);
    GATE(tablesFound + tablesNotFound == static_cast<long long>(tableSpecs().size()),
         "every target table was classified found/not-found: %lld + %lld == %zu", tablesFound, tablesNotFound, tableSpecs().size());
    GATE(tablesFound > 0, "CONTROL: at least one target table was actually located (the search is not vacuously empty)");

    std::printf("\n%s (%d gate failure%s)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
