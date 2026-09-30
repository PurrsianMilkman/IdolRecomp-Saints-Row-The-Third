// Population/location diagnostic for sr3tables_customization
// (include/sr3tables_customization/, src/tables_customization_*.cpp) over
// the REAL shipped game archives.
//
// NOT wired into CMakeLists.txt (per the task's isolation rule) - compile
// standalone, e.g.:
//   cl /std:c++17 /EHsc /I include tools/validation/validate_tables_customization_population.cpp
//       src/xtbl.cpp src/container.cpp src/tables_customization_color_pools.cpp
//       src/tables_customization_slots_categories.cpp src/tables_customization_items.cpp
//       src/tables_customization_materials.cpp src/tables_customization_gang_actions.cpp
//       src/tables_customization_characters.cpp src/tables_customization_player_creation.cpp
//
// Usage: validate_tables_customization_population <archive.vpp_pc> [...]
//   Pass every archive under packfiles/pc/cache/*.vpp_pc - this harness does
//   not hardcode a cache path.
//
// What this reports, per spec-tables-customization.md §21's own validation
// table and the task brief's specific asks:
//   1. LOCATION: every archive (and nested-container chain) a target
//      filename is found in - not just the first hit. A past bug in this
//      project undercounted tables that exist in more than one archive
//      (e.g. a patch archive superseding a base one); this harness reports
//      every occurrence independently rather than merging or short-
//      circuiting on the first match.
//   2. POPULATION: for each occurrence, the file's own <Table> node's TOTAL
//      direct-child count (matching the spec's own "counting only each
//      table's own <Table> node's direct children" methodology, §1.1)
//      alongside this reader's typed row count (which, for
//      customization_default_items.xtbl, is deliberately DIFFERENT from
//      the direct-child count - §8's confirmed Defaults>Defaults_List
//      nesting - both numbers are printed so that difference is visible,
//      not hidden).
//   3. THE §6.2 CLAIM: customization_slot_defaults.xtbl's shipped <Table> is
//      asserted EMPTY by the spec - this harness prints its direct-child
//      count explicitly (expect 0) rather than assuming it.
//   4. THE §16.1 CLAIM: player_master_sliders.xtbl's shipped <Table> AND
//      <TableTemplates> are BOTH asserted empty - same treatment.
//   5. Every table this document names at all - the 18 "live" filenames
//      this library implements a typed reader for, PLUS
//      customization_outfits.xtbl (§5.1, covered by the older
//      sr3customization library, counted here only by raw row tag since
//      this harness does not link that library), PLUS the 12 confirmed-
//      dead/orphaned filenames from §18 (no code loader reaches them, but
//      they are real archive entries with real rows - included here purely
//      as a location/count cross-check against the spec's own §18 table,
//      NOT because this project implements readers for them).
//
// Every row count here is produced by the SAME parser this library ships in
// include/sr3tables_customization/ (via TypedCount below), plus a raw
// structural CountChildren/direct-children count independent of any typed
// reader - so a disagreement between the two numbers, if one ever appears,
// is itself a finding (a bug in the typed reader's row-tag assumption, most
// likely) rather than something this harness could silently paper over.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "sr3tables_customization/tables.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using sr3xtbl::Document;
using sr3xtbl::Node;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string baseName(const std::string& p) {
    const size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? p : p.substr(s + 1);
}

Bytes readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0);
    Bytes b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

// ---------------------------------------------------------------------------
// Archive walk - same recursive vpp::Container pattern as this project's
// other population validators (e.g. validate_tables_environment_population.cpp
// G1): every entry ending in "xtbl", raw or compressed, through every nested
// container.
// ---------------------------------------------------------------------------
struct Item {
    std::string archiveChain;     // "topLevel.vpp_pc/nested.str2_pc/..."
    std::string topLevelArchive;  // just the top-level .vpp_pc file name
    std::string name;             // the entry's own file name
    bool compressed = false;
    int status = 0;  // 0 = decoded / readable
    Bytes data;
};

std::vector<Item> g_items;
long long g_containers = 0;

bool endsWithCi(const std::string& s, const char* x) {
    const size_t n = std::strlen(x);
    const std::string ls = lower(s);
    return ls.size() >= n && ls.compare(ls.size() - n, n, x) == 0;
}

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

// ---------------------------------------------------------------------------
// Per-table description.
// ---------------------------------------------------------------------------
struct TableCheck {
    std::string filename;
    std::vector<std::string> descendPath;  // FindChild steps from <Table> before counting rowTag (e.g. §8's
                                             // Defaults > Defaults_List); empty = count directly under <Table>
    std::string rowTag;                     // row element tag to CountChildren() against, after descending
    long long expectedCount = -1;           // spec §21/§18's own stated real base-game row count; -1 = not stated
    std::string note;                       // section citation / caveat
    bool live = true;                       // true = this library (or, for §5.1, sr3customization) has a typed
                                              // reader; false = §18 dead file, raw count only
    // Typed row count via this library's own parser, where one exists (nullptr for §5.1's outfits, which lives
    // in sr3customization, and for every §18 dead file, which has no reader anywhere in this project).
    std::function<size_t(const Document&)> typedCount;
};

template <class T>
std::function<size_t(const Document&)> Counter(std::vector<T> (*fn)(const Document&)) {
    return [fn](const Document& doc) { return fn(doc).size(); };
}

const std::vector<TableCheck>& tableChecks() {
    using namespace sr3tables_customization;
    static const std::vector<TableCheck> checks = {
        // --- §2 shared colour pool family (one reader, four files) ---
        {"character_color_pool.xtbl", {}, "Color_Entry", 126, "spec 2.4 (#71)", true, Counter(ParseColorPoolTable)},
        {"hair_color_pool.xtbl", {}, "Color_Entry", 17, "spec 2.4 (#149)", true, Counter(ParseColorPoolTable)},
        {"makeup_color_pool.xtbl", {}, "Color_Entry", 117, "spec 2.4 (#174)", true, Counter(ParseColorPoolTable)},
        {"tattoo_color_pool.xtbl", {}, "Color_Entry", 11, "spec 2.4 (#251)", true, Counter(ParseColorPoolTable)},
        {"char_color_balance.xtbl", {}, "ColorBalance", 1, "spec 2.4 (#77) - singleton", true,
         [](const Document& d) { return ParseColorBalance(d).has_value() ? size_t{1} : size_t{0}; }},
        // --- §3 ---
        {"items_color_pool.xtbl", {}, "Item_Color", 30, "spec 3.1 (#160)", true, Counter(ParseItemsColorPoolTable)},
        {"npc_color_palette.xtbl", {}, "Palette", 34, "spec 3.2 (#191)", true, Counter(ParseNpcColorPaletteTable)},
        // --- §4/§5 ---
        {"customization_items.xtbl", {}, "Customization_Item", 574, "spec 21 (858-row cap, 67% full)", true,
         Counter(ParseCustomizationItemsTable)},
        {"customization_outfits.xtbl", {}, "Outfit", 70, "spec 5.1 (#102) - covered by sr3customization, not this library; raw count only", false, nullptr},
        {"customization_stores.xtbl", {}, "Store", 15, "spec 5.2 (#105)", true, Counter(ParseCustomizationStoresTable)},
        // --- §6 ---
        {"customization_slots.xtbl", {}, "Slot", 22, "spec 6.1 (#103); 22 of 24 canonical names matched", true,
         Counter(ParseCustomizationSlotsTable)},
        {"customization_slot_defaults.xtbl", {}, "Slot_Default", 0,
         "spec 6.2 (#104) - SHIPPED <Table> CLAIMED EMPTY; see the directChildCount column, not just this row tag", true,
         Counter(ParseCustomizationSlotDefaultsTable)},
        // --- §7 ---
        {"customization_categories.xtbl", {}, "Category", 39, "spec 7.1 (#94)", true, Counter(ParseCustomizationCategoriesTable)},
        {"customization_flags.xtbl", {}, "Flag", 39, "spec 7.2 (#97)", true, Counter(ParseCustomizationFlagsTable)},
        {"customization_icons.xtbl", {}, "Icon", 10, "spec 7.3 (#98)", true, Counter(ParseCustomizationIconsTable)},
        // --- §8 ---
        {"customization_default_items.xtbl", {"Defaults", "Defaults_List"}, "Default", 55,
         "spec 8 (#96) - direct-<Table>-child count is 1 (the Defaults wrapper), NOT 55; real rows nest two levels deeper", true,
         Counter(ParseCustomizationDefaultItemsTable)},
        // --- §9 ---
        {"customization_materials.xtbl", {}, "Cust_Material", 7, "spec 9.1 (#100)", true, Counter(ParseCustomizationMaterialsTable)},
        {"customization_normals.xtbl", {}, "Normals", 8, "spec 9.2 (#101) - AT the 8-row cap", true, Counter(ParseCustomizationNormalsTable)},
        {"customization_compositing.xtbl", {}, "Composite_Layer", 374, "spec 9.3 (#95, 600-row cap)", true,
         Counter(ParseCustomizationCompositingTable)},
        // --- §10/§11 ---
        {"gang_customization.xtbl", {}, "GangCustomization", 1, "spec 10 (#141) - sentinel-gated singleton", true,
         [](const Document& d) { return ParseGangCustomizationTable(d).has_value() ? size_t{1} : size_t{0}; }},
        {"customizable_action.xtbl", {}, "CustomizableActions", 56, "spec 11 (#93)", true, Counter(ParseCustomizableActionTable)},
        // --- §12/§13 ---
        {"character_definitions.xtbl", {}, "Character", 282, "spec 12.1 (#73)", true, Counter(ParseCharacterDefinitionsTable)},
        {"character_height.xtbl", {}, "Height_Class", 3, "spec 12.2 (#74)", true, Counter(ParseCharacterHeightTable)},
        {"character_customization_categories.xtbl", {}, "category", 16, "spec 13.1 (#72) - row locator only, no field schema", true,
         [](const Document& d) { return FindCharacterCustomizationCategoryRows(d).size(); }},
        {"character_types.xtbl", {}, "Type", 96, "spec 13.2 (#75, 120-row cap)", true, Counter(ParseCharacterTypesTable)},
        {"character.xtbl", {}, "Character", 382, "spec 13.3 (#70) - row locator only, no field schema; SAME tag as character_definitions.xtbl but a separate file", true,
         [](const Document& d) { return FindCharacterRows(d).size(); }},
        // --- §14-17 player creation subsystem ---
        {"player_creation.xtbl", {}, "Morph_Set", 12, "spec 14.1 (#201); 12 of 14 canonical categories matched", true, Counter(ParsePlayerCreationTable)},
        {"player_creation_morph_groups.xtbl", {}, "morph_group", 13, "spec 14.2 (#204)", true, Counter(ParsePlayerCreationMorphGroupsTable)},
        {"player_creation_normal_maps.xtbl", {}, "Normal_Map_Settings", 5, "spec 15.1 (#205); 5 of 8 canonical body types matched", true,
         Counter(ParsePlayerCreationNormalMapsTable)},
        {"player_creation_hair_color.xtbl", {}, "Hair_Color", 35, "spec 15.2 (#203)", true, Counter(ParsePlayerCreationHairColorTable)},
        {"player_creation_skin_colors.xtbl", {}, "Entry", 55, "spec 15.3 (#206)", true, Counter(ParsePlayerCreationSkinColorsTable)},
        {"player_master_sliders.xtbl", {}, "MasterSliders", 0,
         "spec 16.1 (#211) - SHIPPED <Table> AND <TableTemplates> BOTH CLAIMED EMPTY", true, Counter(ParsePlayerMasterSlidersTable)},
        {"player_presets.xtbl", {}, "Preset", 8, "spec 16.2 (#212)", true, Counter(ParsePlayerPresetsTable)},
        {"player_regional_presets.xtbl", {}, "RegionalPresets", 1, "spec 16.3 (#213)", true, Counter(ParsePlayerRegionalPresetsTable)},
        {"player_cust_shot_map.xtbl", {}, "NewEntity", 5, "spec 17 (#210)", true, Counter(ParsePlayerCustShotMapTable)},
        // --- §18 dead/unreferenced files (no code loader reaches these anywhere in the executable, per the
        // spec's own exhaustive literal scan) - included here ONLY as a location/count cross-check against the
        // spec's own table; this project implements NO reader for any of them. ---
        {"player_creation_hair.xtbl", {}, "Hair", 1, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"player_choice_tutorial.xtbl", {}, "Entity", 1, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"player_cust_camera_anim_positions.xtbl", {}, "NewEntity", 6, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"player_cust_camera_shots.xtbl", {}, "NewEntity", 38, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"ui_bms_store_nobody.xtbl", {}, "BitmapSheets", 1, "spec 18 - DEAD FILE, no loader (misleadingly named)", false, nullptr},
        {"custcharactercolors.xtbl", {}, "CustCharacterColors_Identifier", 126, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"custcolorpool.xtbl", {}, "CustColorPool_Identifier", 52, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"custvehiclecolors.xtbl", {}, "CustVehicleColors_Identifier", 121, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"custvehiclecolorslots.xtbl", {}, "CustVehicleColorSlots_Identifier", 84, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"gang_customization-lightset.xtbl", {}, "LightSet", 1, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"anim_customization.xtbl", {}, "Skeleton_Set", 1, "spec 18 - DEAD FILE, no loader", false, nullptr},
        {"anim_gang_customization.xtbl", {}, "Skeleton_Set", 1, "spec 18 - DEAD FILE, no loader", false, nullptr},
    };
    return checks;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <archive.vpp_pc> [...]\n", argv[0]);
        return 2;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string path = argv[i];
        Bytes bytes = readFile(path);
        if (bytes.empty()) {
            std::fprintf(stderr, "WARNING: could not read %s (missing or empty)\n", path.c_str());
            continue;
        }
        const std::string top = baseName(path);
        try {
            walk(vpp::ByteView(bytes.data(), bytes.size()), top, top);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WARNING: %s failed to parse as a container: %s\n", path.c_str(), e.what());
        }
        // Keep the backing bytes alive: g_items may hold rawEntryBytes() views
        // aliasing nested containers' own backing buffers, which in turn
        // alias this top-level buffer - leak it deliberately for the
        // lifetime of this short-lived diagnostic process rather than reason
        // about container-internal aliasing lifetimes.
        (void)new Bytes(std::move(bytes));
    }

    std::printf("Scanned %d archive(s), %lld container(s), found %zu .xtbl entries total.\n\n", argc - 1, g_containers,
                g_items.size());

    long long liveTablesChecked = 0, liveTablesFound = 0;
    long long otherLibTablesChecked = 0, otherLibTablesFound = 0;  // §5.1 outfits (sr3customization)
    long long deadTablesChecked = 0, deadTablesFound = 0;          // §18, no reader anywhere in this project
    long long countMismatches = 0;
    long long typedVsRawMismatches = 0;

    for (const TableCheck& tc : tableChecks()) {
        std::vector<const Item*> matches;
        for (const Item& it : g_items)
            if (lower(it.name) == lower(tc.filename)) matches.push_back(&it);

        const bool isDeadFile = tc.note.find("DEAD FILE") != std::string::npos;
        if (tc.live)
            ++liveTablesChecked;
        else if (isDeadFile)
            ++deadTablesChecked;
        else
            ++otherLibTablesChecked;

        std::printf("=== %s (%s) ===\n", tc.filename.c_str(), tc.note.c_str());
        if (matches.empty()) {
            std::printf("  NOT FOUND in any scanned archive.\n\n");
            continue;
        }
        if (tc.live)
            ++liveTablesFound;
        else if (isDeadFile)
            ++deadTablesFound;
        else
            ++otherLibTablesFound;

        std::printf("  found in %zu location(s):\n", matches.size());
        for (const Item* m : matches) {
            std::printf("    - %s (chain: %s), %zu byte(s), status=%d%s\n", m->topLevelArchive.c_str(),
                        m->archiveChain.c_str(), m->data.size(), m->status, m->compressed ? " [compressed]" : "");
            if (m->data.empty()) {
                std::printf("      (no bytes decoded - skipping parse)\n");
                continue;
            }
            try {
                Document doc = sr3xtbl::ParseDocument(m->data.data(), m->data.size());
                const Node* table = doc.table();
                const size_t directChildCount = table ? table->children().size() : 0;

                const Node* descendTarget = table;
                for (const std::string& step : tc.descendPath) descendTarget = sr3xtbl::FindChild(descendTarget, step);
                const size_t rowTagCount = sr3xtbl::CountChildren(descendTarget, tc.rowTag);

                std::printf("      <Table> direct children: %zu | %s count (after descending %zu level(s)): %zu",
                            directChildCount, tc.rowTag.c_str(), tc.descendPath.size(), rowTagCount);
                if (tc.expectedCount >= 0) {
                    const bool matchesSpec = static_cast<long long>(rowTagCount) == tc.expectedCount;
                    std::printf(" | spec says %lld -> %s", tc.expectedCount, matchesSpec ? "MATCH" : "MISMATCH");
                    if (!matchesSpec) ++countMismatches;
                }
                std::printf("\n");

                if (tc.typedCount) {
                    const size_t typed = tc.typedCount(doc);
                    std::printf("      typed reader count: %zu%s\n", typed,
                                typed == rowTagCount ? "" : "  <-- DISAGREES WITH RAW CountChildren, see note below");
                    if (typed != rowTagCount) ++typedVsRawMismatches;
                }

                // §6.2 / §16.1's specific claim: the WHOLE <Table> is empty,
                // not just this row tag - print unconditionally for these two.
                if (lower(tc.filename) == "customization_slot_defaults.xtbl") {
                    std::printf("      [%s] §6.2 claim (shipped <Table> is EMPTY): %s\n",
                                directChildCount == 0 ? "CONFIRMED" : "CONTRADICTED",
                                directChildCount == 0 ? "0 direct children, as the spec states"
                                                       : "direct children present - spec's claim does NOT hold for this archive");
                }
                if (lower(tc.filename) == "player_master_sliders.xtbl") {
                    const Node* root = doc.root();
                    const Node* templates = sr3xtbl::FindChild(root, "TableTemplates");
                    const size_t templatesChildCount = templates ? templates->children().size() : 0;
                    std::printf("      [%s] §16.1 claim (shipped <Table> AND <TableTemplates> BOTH EMPTY): Table=%zu child(ren), TableTemplates=%zu child(ren) -> %s\n",
                                (directChildCount == 0 && templatesChildCount == 0) ? "CONFIRMED" : "CONTRADICTED", directChildCount,
                                templatesChildCount,
                                (directChildCount == 0 && templatesChildCount == 0) ? "matches the spec" : "does NOT match the spec for this archive");
                }
            } catch (const std::exception& e) {
                std::printf("      PARSE FAILED: %s\n", e.what());
            }
        }
        std::printf("\n");
    }

    std::printf("=========================================================\n");
    std::printf("Live tables (sr3tables_customization implements a typed reader): %lld/%lld located.\n",
                liveTablesFound, liveTablesChecked);
    std::printf("Other-library tables (sr3customization implements the reader, e.g. §5.1 outfits): %lld/%lld located.\n",
                otherLibTablesFound, otherLibTablesChecked);
    std::printf("Dead files (spec 18, no reader anywhere in this project): %lld/%lld located.\n", deadTablesFound,
                deadTablesChecked);
    std::printf("Row-count mismatches vs spec 21/18's stated real counts: %lld.\n", countMismatches);
    std::printf("Typed-reader-vs-raw-CountChildren disagreements: %lld.\n", typedVsRawMismatches);
    std::printf("(A mismatch is not necessarily a bug - it may mean the archive set passed on argv differs from\n");
    std::printf(" the exact misc_tables.vpp_pc build the spec's own §21 table was measured against; investigate\n");
    std::printf(" per-table before assuming either side is wrong.)\n");
    return 0;
}
