// sr3frontend::BindingModel on the shipped control_binding_sets.xtbl (misc_tables.vpp_pc):
// spec-tables-ui-controls.md Sec18.3 structure and the Sec18.5 rebinding vectors 7-9.
// Exits 77 (ctest SKIPPED) without the game cache.
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3frontend/bindings.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        ++g_checks;                                                                             \
        if (!(cond)) {                                                                          \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n";        \
            ++g_failures;                                                                       \
        }                                                                                       \
    } while (0)

bool ReadEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name != name) continue;
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            vpp::ByteView r = c.rawEntryBytes(i);
            out.assign(r.data(), r.data() + r.size());
            return true;
        }
        auto r = c.decompressEntry(i);
        out = std::move(r.data);
        return !out.empty();
    }
    return false;
}

using namespace sr3frontend;

int Find(const BindCategory& c, const std::string& action, bool negative = false) {
    for (size_t i = 0; i < c.rows.size(); ++i)
        if (c.rows[i].action == action && c.rows[i].negative == negative) return static_cast<int>(i);
    return -1;
}

int CountKey(const BindCategory& c, const std::string& key) {
    int n = 0;
    for (const BindRow& r : c.rows) n += (r.primary.key == key) + (r.secondary.key == key);
    return n;
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
        if (!ReadEntry(misc->container, "control_binding_sets.xtbl", bytes)) {
            std::cerr << "control_binding_sets.xtbl not found\n";
            return 1;
        }
    } catch (const std::exception& ex) {
        std::cout << "open failed - SKIPPED (" << ex.what() << ")\n";
        return 77;
    }
    const auto doc = sr3xtbl::ParseDocument(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    BindingModel m = BindingModel::FromTable(sr3tables_ui_controls::ParseControlBindingSetsTable(doc));
    const auto& cats = m.categories();
    CHECK(cats.size() == 6);
    if (cats.size() != 6) return 1;
    const char* labels[] = {"General", "On Foot", "Car", "Helicopter", "Plane", "Tank"};
    for (int i = 0; i < 6; ++i) CHECK(cats[static_cast<size_t>(i)].label == labels[i]);
    CHECK(cats[0].common && !cats[1].common);
    const int pause = Find(cats[0], "CBA_GAC_PAUSE_MENU");
    const int map = Find(cats[0], "CBA_GAC_MAP_MENU");
    CHECK(pause >= 0 && cats[0].rows[static_cast<size_t>(pause)].primary.key == "ESC");
    CHECK(map >= 0 && cats[0].rows[static_cast<size_t>(map)].primary.key == "TAB");
    const int reload = Find(cats[1], "CBA_OFC_PICKUP_RELOAD");
    CHECK(reload >= 0 && cats[1].rows[static_cast<size_t>(reload)].primary.key == "R");
    size_t rows = 0;
    for (const auto& c : cats) rows += c.rows.size();
    std::cout << "binding rows shown: " << rows << " in 6 categories; On Foot keys using Q: " << CountKey(cats[1], "Q")
              << ", W in all sets: ";
    int wAll = 0;
    for (const auto& c : cats) wAll += CountKey(c, "W");
    std::cout << wAll << "\n";

    // Vector 9: mouse button 7 or key code 0x100 is rejected, nothing written.
    CHECK(!m.bind(1, reload, false, {true, 7}) && !m.bind(1, reload, false, {false, 0x100}));
    CHECK(m.categories()[1].rows[static_cast<size_t>(reload)].primary.key == "R");

    // Vector 7: On Foot Reload primary -> Q: Q leaves the other On Foot actions; General untouched.
    const int generalQ = CountKey(cats[0], "Q");
    CHECK(m.bind(1, reload, false, {false, ScanCodeForKeyName("Q")}));
    CHECK(m.categories()[1].rows[static_cast<size_t>(reload)].primary.key == "Q");
    CHECK(CountKey(m.categories()[1], "Q") == 1);
    CHECK(CountKey(m.categories()[0], "Q") == generalQ);
    for (const BindConflict& c : m.lastConflicts()) CHECK(c.setName == "On_Foot_Bindings");

    // Vector 8: General Map -> W: W leaves every set.
    CHECK(m.bind(0, map, false, {false, ScanCodeForKeyName("W")}));
    int wLeft = 0;
    for (const auto& c : m.categories()) wLeft += CountKey(c, "W");
    CHECK(wLeft == 1 && m.categories()[0].rows[static_cast<size_t>(map)].primary.key == "W");
    CHECK(!m.lastConflicts().empty());

    // A mouse button replaces the cell's key; reset restores the shipped table.
    CHECK(m.bind(1, reload, true, {true, 4}) && m.categories()[1].rows[static_cast<size_t>(reload)].secondary.mouse == 4);
    m.resetAll();
    CHECK(m.categories()[1].rows[static_cast<size_t>(reload)].primary.key == "R" &&
          m.categories()[0].rows[static_cast<size_t>(map)].primary.key == "TAB");
    CHECK(KeyNameForScanCode(0x39) == "SPACEBAR" && ScanCodeForKeyName("LEFT CTRL") == 0x1D &&
          MouseButtonName(5) == "WHEEL UP");

    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
#endif
}
