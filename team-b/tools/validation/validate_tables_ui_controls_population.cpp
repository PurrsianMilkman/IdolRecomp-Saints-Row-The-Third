// Population validator for sr3tables_ui_controls (spec-tables-ui-controls.md),
// the 17-table control-binding / QTE / camera-preset group.
//
// Usage: validate_tables_ui_controls_population <archive.vpp_pc> [...]
//   Archive paths are read from argv, never hardcoded (typically every
//   *.vpp_pc under packfiles/pc/cache). Real game archives are READ-ONLY
//   input data here - this tool never writes to them.
//
// STANDALONE DIAGNOSTIC - not wired into CMakeLists.txt (per the task that
// produced it). Build manually, e.g.:
//   cl /nologo /std:c++17 /EHsc /O2 /I include ^
//      src\xtbl.cpp src\tables_ui_controls.cpp src\container.cpp src\hash.cpp ^
//      tools\validation\validate_tables_ui_controls_population.cpp ^
//      /Fe:validate_tables_ui_controls_population.exe
//   (add whatever other vpp/ .cpp files your vpp::Container build needs -
//   check CMakeLists.txt's sr3vpp target for the exact source list at
//   integration time; this file intentionally does not touch CMakeLists.txt.)
//
// What this does (measurement-discipline.md: every gate needs a denominator
// and at least one control that could fail):
//   (a) walks every given archive recursively (vpp::Container, raw AND
//       compressed entries, nested containers - the walk()/findAll() pattern
//       already used by validate_tables_progression_population.cpp), to
//       LOCATE all 17 target filenames (case-insensitive, exact match).
//   (b) reports EVERY archive location a same-named table is found at, not
//       just the first - a past bug in this project undercounted tables that
//       exist in more than one archive (a patch archive superseding a base
//       one). spec-tables-ui-controls.md 1.1/6 explicitly documents one such
//       case in THIS group: qte.xtbl exists byte-identically at BOTH
//       misc_tables.vpp_pc AND da_tables.vpp_pc - this harness confirms that
//       directly (both locations reported, byte-for-byte comparison gated).
//   (c) for every table FOUND: parses every row with the typed reader
//       (src/tables_ui_controls.cpp) and reports row/field counts against
//       whatever spec-tables-ui-controls.md 16 itself states as validation -
//       trusting the spec's CURRENT text, not any prior retracted claim (see
//       the qte_sequences.xtbl block below re: 7.3's retraction).
//   (d) for tables NOT found, says so plainly - no fabricated zero-rows report.
//   (e) CONTROLS: a deliberately wrong filename must report 0 locations; a
//       deliberately wrong element name must report 0 hits.
//
// NOTE on the three large lookup vocabularies (CBA/CAA/key-name, spec 4.1-4.3):
// this reader deliberately does NOT model them (see include/sr3tables_ui_controls/
// tables.h's file banner - the spec itself only gives representative samples,
// not the full 166/34/80-entry lists), so this harness cannot reproduce
// spec 5 item 5's "154 of 165 CBA names, 34 of 34 CAA names" cross-check
// (that check needs the full vocabulary as a reference set, which this
// project does not have). It instead reports the COUNT of distinct raw
// Action/Key strings actually found in real data, which is the honest
// ceiling of what is checkable without fabricating the missing tables.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3tables_ui_controls/tables.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using namespace sr3xtbl;
using namespace sr3tables_ui_controls;

int g_checks = 0, g_fail = 0;
#define GATE(ok, ...)                                \
    do {                                              \
        const bool ok_ = (ok);                        \
        ++g_checks;                                   \
        std::printf("  [%s] ", ok_ ? "PASS" : "FAIL"); \
        std::printf(__VA_ARGS__);                      \
        std::printf("\n");                              \
        if (!ok_) ++g_fail;                               \
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

// ---------------------------------------------------------------------------
// Archive walking (pattern reused from validate_tables_progression_population.cpp,
// itself reused from validate_xtbl_population.cpp)
// ---------------------------------------------------------------------------
struct Item {
    std::string archive;  // chain of containers, e.g. misc_tables.vpp_pc
    std::string name;     // on-disk entry name (original casing)
    Bytes data;
    bool ok = false;
};

std::vector<Item> g_items;
long long g_containers = 0, g_entriesSeen = 0;

const char* const kTargetFiles[17] = {
    "control_binding_sets.xtbl", "control_filters.xtbl", "control_parameters.xtbl",
    "control_scheme_text.xtbl", "control_schemes.xtbl", "hud_qte_interface.xtbl",
    "hud_qte_interface_presets.xtbl", "qte.xtbl", "qte_sequences.xtbl",
    "user_interface.xtbl", "voice_control.xtbl", "credits_pc.xtbl",
    "item_cust_cameras.xtbl", "player_cust_cameras_new.xtbl", "vehicle_cust_cameras_new.xtbl",
    "vehicle_group_cameras.xtbl", "vehicle_cameras.xtbl",
};
std::set<std::string> targetSet() {
    std::set<std::string> s;
    for (const char* n : kTargetFiles) s.insert(lower(n));
    return s;
}

// Walks EVERY entry (not filtered by target name) so DLC-prefixed variants
// and same-named duplicates in unrelated containers are still found - only
// the reporting step (findAll) filters by name.
void walk(vpp::ByteView bytes, const std::string& path) {
    vpp::Container c(bytes);
    ++g_containers;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        ++g_entriesSeen;
        const std::string ln = lower(e.name);
        if (!(ln.size() >= 5 && ln.compare(ln.size() - 5, 5, ".xtbl") == 0)) continue;
        Item it;
        it.archive = path;
        it.name = e.name;
        if (e.payload.kind == vpp::PayloadKind::Compressed) {
            vpp::DecompressResult r = c.decompressEntry(i);
            if (r.status == vpp::DecodeStatus::Ok) {
                it.data = std::move(r.data);
                it.ok = true;
            }
        } else {
            try {
                vpp::ByteView v = c.rawEntryBytes(i);
                it.data.assign(v.data(), v.data() + v.size());
                it.ok = true;
            } catch (const std::exception&) {
            }
        }
        g_items.push_back(std::move(it));
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        const std::string ln = lower(c.entries()[i].name);
        if (ln.size() >= 5 && ln.compare(ln.size() - 5, 5, ".xtbl") == 0) continue;
        try {
            walk(c.rawEntryBytes(i), path + "/" + c.entries()[i].name);
        } catch (const std::exception&) {
        }
    }
}

std::vector<const Item*> findAll(const std::string& lowerName) {
    std::vector<const Item*> out;
    for (const Item& it : g_items)
        if (it.ok && lower(it.name) == lowerName) out.push_back(&it);
    return out;
}
// First location, for the (common) single-copy tables.
const Item* find1(const std::string& lowerName) {
    auto v = findAll(lowerName);
    return v.empty() ? nullptr : v.front();
}

void reportLocations(const char* file) {
    auto hits = findAll(lower(file));
    if (hits.empty()) {
        std::printf("  NOT FOUND: %-32s\n", file);
    } else {
        std::printf("  found: %-32s in %zu location(s):", file, hits.size());
        for (auto* it : hits) std::printf(" [%s :: %s, %zu bytes]", it->archive.c_str(), it->name.c_str(), it->data.size());
        std::printf("\n");
    }
}

std::vector<const Node*> rowsNamed(const Document& doc, const char* rowName) {
    std::vector<const Node*> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, rowName); row; row = NextSibling(table, row, rowName)) out.push_back(row);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        std::printf("usage: validate_tables_ui_controls_population <archive.vpp_pc> [...]\n");
        return 2;
    }

    for (const std::string& a : archives) {
        Bytes b = readFile(a);
        if (b.empty()) {
            std::printf("cannot read %s\n", a.c_str());
            continue;
        }
        try {
            walk(vpp::ByteView(b.data(), b.size()), baseName(a));
        } catch (const std::exception& e) {
            std::printf("open failed %s: %s\n", a.c_str(), e.what());
        }
    }

    std::printf("=== survey ===\n");
    std::printf("archives given: %zu, containers walked: %lld, directory entries seen: %lld, .xtbl-family entries decoded: %zu\n",
                archives.size(), g_containers, g_entriesSeen, g_items.size());
    GATE(g_containers > 0 && g_entriesSeen > 0, "CONTROL: the walk is not vacuous (%lld containers, %lld entries)", g_containers, g_entriesSeen);

    // ---- CONTROLS on the locating mechanism itself ----
    GATE(findAll("totally_bogus_ui_controls_table_xyz.xtbl").empty(),
         "CONTROL: a deliberately wrong filename is correctly reported as not found (0 locations)");
    {
        int foundCount = 0;
        for (const char* n : kTargetFiles)
            if (!findAll(lower(n)).empty()) ++foundCount;
        GATE(foundCount > 0, "CONTROL: the search mechanism finds at least one of the 17 real names (%d/17)", foundCount);
    }

    std::printf("\n=== locate (17 target filenames, every archive location reported) ===\n");
    int foundTables = 0;
    for (const char* n : kTargetFiles) {
        if (!findAll(lower(n)).empty()) ++foundTables;
        reportLocations(n);
    }
    GATE(foundTables > 0, "at least one of the 17 target tables was located: %d / 17", foundTables);

    // ------------------------------------------------------------------
    // qte.xtbl (spec 6) - the spec's own explicit multi-archive case:
    // "byte-identical copy at index 10 of da_tables.vpp_pc" (1.1). This is
    // the exact bug class the task flagged: report EVERY location, not just
    // the first, and verify they really are byte-identical.
    // ------------------------------------------------------------------
    std::printf("\n=== qte.xtbl (spec 6, 1.1 - multi-archive case) ===\n");
    {
        auto hits = findAll("qte.xtbl");
        GATE(hits.size() >= 2, "qte.xtbl located in >= 2 archives (spec 1.1 says misc_tables.vpp_pc AND da_tables.vpp_pc): %zu found", hits.size());
        if (hits.size() >= 2) {
            bool allIdentical = true;
            for (size_t i = 1; i < hits.size(); ++i)
                if (hits[i]->data != hits[0]->data) allIdentical = false;
            GATE(allIdentical, "all %zu located copies of qte.xtbl are byte-identical (spec 1.1's claim)", hits.size());
        }
        if (const Item* it = find1("qte.xtbl")) {
            Document doc = ParseDocument(it->data.data(), it->data.size());
            auto q = ParseQteTable(doc);
            GATE(q.has_value(), "qte.xtbl: a <QTE> row was parsed");
            if (q) {
                std::printf("  Hud_Time=%g Cooldown_Time=%g Respect=%d Max_Lifetime_Respect=%d Cash=%g\n", q->hudTime.value,
                            q->cooldownTime.value, q->respect.value, q->maxLifetimeRespect.value, q->cash.value);
                // Real-data values transcribed verbatim from spec 6.
                GATE(q->cooldownTime.present && q->cooldownTime.value == 6.0f, "Cooldown_Time == 6 (spec 6)");
                GATE(q->respect.present && q->respect.value == 50, "Respect == 50 (spec 6)");
                GATE(q->maxLifetimeRespect.present && q->maxLifetimeRespect.value == 10000, "Max_Lifetime_Respect == 10000 (spec 6)");
                GATE(q->cash.present && q->cash.value == 100.0f, "Cash == 100 (spec 6)");
                GATE(q->hudTime.present && q->hudTime.value == 4.0f, "Hud_Time == 4 (spec 6)");
            }
        }
    }

    // ------------------------------------------------------------------
    // control_binding_sets.xtbl (spec 2) - "7 Binding_Set rows, 96 Control +
    // 18 Axis = 114 total bindings, 91 distinct button action names, 18
    // distinct axis action names" (16).
    // ------------------------------------------------------------------
    std::printf("\n=== control_binding_sets.xtbl (spec 2) ===\n");
    if (const Item* it = find1("control_binding_sets.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto sets = ParseControlBindingSetsTable(doc);
        long long controls = 0, axes = 0;
        std::set<std::string> distinctButtonActions, distinctAxisActions;
        for (auto& s : sets) {
            controls += static_cast<long long>(s.controls.size());
            axes += static_cast<long long>(s.axes.size());
            for (auto& c : s.controls)
                if (c.action) distinctButtonActions.insert(*c.action);
            for (auto& a : s.axes)
                if (a.action) distinctAxisActions.insert(*a.action);
        }
        std::printf("  %zu Binding_Set rows, %lld Control rows, %lld Axis rows (%lld total bindings)\n", sets.size(), controls, axes,
                    controls + axes);
        std::printf("  distinct button Action strings: %zu, distinct axis Action strings: %zu (informational only - the CBA/CAA\n"
                    "    vocabulary itself is a documented gap here, so this cannot reproduce spec 5 item 5's 154/165, 34/34 figures)\n",
                    distinctButtonActions.size(), distinctAxisActions.size());
        GATE(sets.size() == 7, "7 Binding_Set rows (spec 2, 16): %zu", sets.size());
        GATE(controls == 96, "96 Control (button) rows (spec 2, 16): %lld", controls);
        GATE(axes == 18, "18 Axis rows (spec 2, 16): %lld", axes);

        // Field-order regression: at least one real Axis row should populate
        // all four key slots and the direction selector (proves the 2.3
        // field-order mapping actually reads real data, not just synthetic).
        long long axisRowsWithAllFourKeys = 0;
        for (auto& s : sets)
            for (auto& a : s.axes)
                if (a.key1 && a.key2 && a.key3 && a.key4) ++axisRowsWithAllFourKeys;
        std::printf("  Axis rows with all four key slots (Key_Pos/Neg, Alt_Key_Pos/Neg) present: %lld / %lld\n", axisRowsWithAllFourKeys, axes);

        // CONTROL: a deliberately wrong element name must find 0 hits.
        auto controlRows = rowsNamed(doc, "Binding_Set");
        long long bogusHits = 0;
        for (auto* r : controlRows)
            if (ChildText(r, "Totally_Bogus_Element_XYZ")) ++bogusHits;
        GATE(bogusHits == 0, "CONTROL: a deliberately wrong element name matches 0 / %zu rows", controlRows.size());
    } else {
        std::printf("  NOT FOUND in the given archives.\n");
    }

    // ------------------------------------------------------------------
    // control_schemes.xtbl (spec 3) - "13 Control_Scheme rows"; "509/509/509
    // <X360>/<PS3>/<PC> tag-presence" (3.1, empirical).
    // ------------------------------------------------------------------
    std::printf("\n=== control_schemes.xtbl (spec 3) ===\n");
    if (const Item* it = find1("control_schemes.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto schemes = ParseControlSchemesTable(doc);
        auto rows = rowsNamed(doc, "Control_Scheme");
        long long x360Tags = 0, ps3Tags = 0, pcTags = 0, controlRows = 0;
        for (auto* r : rows)
            for (const Node* c = FindChild(r, "Controls"); c; c = nullptr)
                for (const Node* ctrl = FindChild(c, "Control"); ctrl; ctrl = NextSibling(c, ctrl, "Control")) {
                    ++controlRows;
                    if (FindChild(ctrl, "X360")) ++x360Tags;
                    if (FindChild(ctrl, "PS3")) ++ps3Tags;
                    if (FindChild(ctrl, "PC")) ++pcTags;
                }
        std::printf("  %zu Control_Scheme rows\n", schemes.size());
        GATE(schemes.size() == 13, "13 Control_Scheme rows (spec 3, 16): %zu", schemes.size());
        std::printf("  raw <Control> rows scanned: %lld; <X360> tag present: %lld, <PS3>: %lld, <PC>: %lld (spec 3.1: 509/509/509 in the\n"
                    "    real base file - the loader reads only X360 and ignores PS3/PC, both of which still exist as data)\n",
                    controlRows, x360Tags, ps3Tags, pcTags);
        long long x360FalseCount = 0, x360AbsentCount = 0;
        for (auto& s : schemes)
            for (auto& c : s.controls) {
                if (!c.x360) ++x360AbsentCount;
                else if (!*c.x360) ++x360FalseCount;
            }
        std::printf("  parsed Control rows: X360 absent (default true applies): %lld, X360 explicitly false: %lld\n", x360AbsentCount,
                    x360FalseCount);
    } else {
        std::printf("  NOT FOUND in the given archives.\n");
    }

    // ------------------------------------------------------------------
    // qte_sequences.xtbl (spec 7) - "26 <QTE> rows, 90 total <QTE_Node>,
    // largest single row 12" (7, 16). The 7.3 RETRACTION means this harness
    // must NOT check for "33 rows / 167 nodes" - only the CURRENT figures.
    // ------------------------------------------------------------------
    std::printf("\n=== qte_sequences.xtbl (spec 7 - note 7.3's retraction) ===\n");
    if (const Item* it = find1("qte_sequences.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto seqs = ParseQteSequencesTable(doc);
        long long totalNodes = 0, maxNodesInRow = 0;
        for (auto& s : seqs) {
            totalNodes += static_cast<long long>(s.nodes.size());
            maxNodesInRow = std::max<long long>(maxNodesInRow, static_cast<long long>(s.nodes.size()));
        }
        std::printf("  %zu QTE rows (from doc.table() only - NOT TableTemplates), %lld total QTE_Node, largest single row %lld nodes\n",
                    seqs.size(), totalNodes, maxNodesInRow);
        GATE(seqs.size() == 26, "26 QTE rows under <Table> (spec 7, 16 - post-retraction figure): %zu", seqs.size());
        GATE(totalNodes == 90, "90 total QTE_Node (spec 7, 16 - post-retraction figure): %lld", totalNodes);
        GATE(maxNodesInRow == 12, "largest single row has 12 nodes, in Mission16_FinalQTE (spec 7): %lld", maxNodesInRow);
        GATE(totalNodes < 160, "CONTROL: real data (%lld nodes) sits nowhere near the 160-slot global capacity - confirms the retraction (7.3)",
             totalNodes);
        // CONTROL: TableTemplates must NOT be swept in - doc.table() already
        // guarantees this structurally, but confirm root really does carry a
        // sibling TableTemplates element (else the retraction's premise
        // itself would not apply to this file).
        GATE(FindChild(doc.root(), "TableTemplates") != nullptr, "CONTROL: root carries a sibling <TableTemplates> (7.3) - confirms doc.table() is excluding real scaffold data, not nothing");
        // Confirm the "Succes_State" (one 's') tag really is what real data uses.
        long long succesTag = 0, correctSpellingTag = 0;
        for (auto* row = FindChild(doc.table(), "QTE"); row; row = NextSibling(doc.table(), row, "QTE")) {
            if (FindChild(row, "Succes_State")) ++succesTag;
            if (FindChild(row, "Success_State")) ++correctSpellingTag;
        }
        std::printf("  rows with <Succes_State> (one 's', spec 7.1): %lld; rows with <Success_State> (correct spelling): %lld\n", succesTag,
                    correctSpellingTag);
    } else {
        std::printf("  NOT FOUND in the given archives.\n");
    }

    // ------------------------------------------------------------------
    // hud_qte_interface_presets.xtbl + hud_qte_interface.xtbl (spec 9) -
    // "11 presets, 13 Hud_qte rows", both under the 16-row cap; "every
    // Position/Position_PC value resolves to a real preset name (0 unmatched)".
    // ------------------------------------------------------------------
    std::printf("\n=== hud_qte_interface_presets.xtbl + hud_qte_interface.xtbl (spec 9) ===\n");
    {
        const Item* presetsIt = find1("hud_qte_interface_presets.xtbl");
        const Item* ifaceIt = find1("hud_qte_interface.xtbl");
        std::vector<HudQtePreset> presets;
        if (presetsIt) {
            Document doc = ParseDocument(presetsIt->data.data(), presetsIt->data.size());
            presets = ParseHudQteInterfacePresetsTable(doc);
            std::printf("  %zu Hud_qte_preset rows (row tag \"Hud_qte_preset\" - see the header's JUDGEMENT CALLS note)\n", presets.size());
            GATE(presets.size() == 11, "11 presets (spec 9, 16): %zu", presets.size());
            GATE(presets.size() < 16, "under the 16-row cap (spec 9): %zu", presets.size());
        } else {
            std::printf("  hud_qte_interface_presets.xtbl NOT FOUND in the given archives.\n");
        }
        if (ifaceIt) {
            Document doc = ParseDocument(ifaceIt->data.data(), ifaceIt->data.size());
            auto ifaces = ParseHudQteInterfaceTable(doc);
            std::printf("  %zu Hud_qte rows (row tag \"Hud_qte\" - see the header's JUDGEMENT CALLS note)\n", ifaces.size());
            GATE(ifaces.size() == 13, "13 Hud_qte rows (spec 9, 16): %zu", ifaces.size());
            GATE(ifaces.size() < 16, "under the 16-row cap (spec 9): %zu", ifaces.size());

            std::set<std::string> presetNames;
            for (auto& p : presets)
                if (p.name) presetNames.insert(*p.name);
            long long posUnmatched = 0, posPcUnmatched = 0, posTotal = 0, posPcTotal = 0;
            // Button_Animation_Type (CORRECTED 2026-10-01, spec 9.2): raw text now,
            // not a closed EnumIndex - the real table has 9 slots, only 3 names
            // known (kButtonAnimationTypeNames). Report distinct values and how
            // many fall outside that known-3 set (informational, NOT a gate - an
            // unknown-but-real 4th-9th name would be expected, not a bug).
            long long buttonTypeUnmatched = 0, buttonAnimOutsideKnown3 = 0, buttonAnimPresent = 0;
            std::set<std::string> buttonAnimValues;
            for (auto& h : ifaces) {
                if (h.position) {
                    ++posTotal;
                    if (!presetNames.count(*h.position)) ++posUnmatched;
                }
                if (h.positionPC) {
                    ++posPcTotal;
                    if (!presetNames.count(*h.positionPC)) ++posPcUnmatched;
                }
                if (h.buttonType < 0) ++buttonTypeUnmatched;
                if (h.buttonAnimationType) {
                    ++buttonAnimPresent;
                    buttonAnimValues.insert(*h.buttonAnimationType);
                    bool known3 = *h.buttonAnimationType == "Mash Standard" || *h.buttonAnimationType == "Mash Fast" ||
                                  *h.buttonAnimationType == "Alternate Triggers";
                    if (!known3) ++buttonAnimOutsideKnown3;
                }
            }
            GATE(posUnmatched == 0, "every Position value resolves to a real preset name (spec 9.2, 16): %lld / %lld unmatched", posUnmatched,
                 posTotal);
            GATE(posPcUnmatched == 0, "every Position_PC value resolves to a real preset name (spec 9.2, 16): %lld / %lld unmatched", posPcUnmatched,
                 posPcTotal);
            std::printf("  Button_Type unmatched (outside the 9-entry table): %lld / %zu; Button_Animation_Type: %lld/%zu present, "
                        "%zu distinct value(s) (",
                        buttonTypeUnmatched, ifaces.size(), buttonAnimPresent, ifaces.size(), buttonAnimValues.size());
            for (auto& v : buttonAnimValues) std::printf("%s ", v.c_str());
            std::printf("), %lld outside the known-3 names\n", buttonAnimOutsideKnown3);
        } else {
            std::printf("  hud_qte_interface.xtbl NOT FOUND in the given archives.\n");
        }
    }

    // ------------------------------------------------------------------
    // Camera-preset group (spec 8): vehicle_cameras.xtbl,
    // vehicle_group_cameras.xtbl, item_cust_cameras.xtbl,
    // player_cust_cameras_new.xtbl, vehicle_cust_cameras_new.xtbl.
    // ------------------------------------------------------------------
    std::printf("\n=== camera-preset group (spec 8) ===\n");
    if (const Item* it = find1("vehicle_cameras.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto rows = ParseVehicleCamerasTable(doc);
        long long posBased = 0, sphereBased = 0, neither = 0, skipTrans = 0, altFreckle = 0;
        for (auto& r : rows) {
            if (r.camera.positionBasedPresent) ++posBased;
            else if (r.camera.sphereBasedPresent) ++sphereBased;
            else ++neither;
            if (r.camera.skipCameraTransition) ++skipTrans;
            if (r.camera.useAltFreckleCam) ++altFreckle;
        }
        std::printf("  vehicle_cameras.xtbl: %zu <Vehicle> rows (Position_Based: %lld, Sphere_Based: %lld, neither: %lld)\n", rows.size(),
                    posBased, sphereBased, neither);
        // CORRECTED 2026-10-01 (8.1): skip_camera_transition/use_alt_freckle_cam
        // are child elements, not row attributes (real fix, see tables.h). Row
        // attributes essentially never occur in this project's real XML (see
        // sr3xtbl.h's own node-model note: only one real attribute use exists
        // project-wide, unrelated to this table), so BEFORE this fix these two
        // fields were silently always false on every real row; this count is
        // the direct measure of what the fix recovers.
        std::printf("  vehicle_cameras.xtbl: skip_camera_transition=true: %lld / %zu, use_alt_freckle_cam=true: %lld / %zu "
                    "(CORRECTED 2026-10-01 attribute->element fix - was always 0/%zu before)\n",
                    skipTrans, rows.size(), altFreckle, rows.size(), rows.size());
    } else {
        std::printf("  vehicle_cameras.xtbl NOT FOUND in the given archives.\n");
    }
    if (const Item* it = find1("vehicle_group_cameras.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto rows = ParseVehicleGroupCamerasTable(doc);
        long long skipTrans = 0, altFreckle = 0;
        for (auto& r : rows) {
            if (r.camera.skipCameraTransition) ++skipTrans;
            if (r.camera.useAltFreckleCam) ++altFreckle;
        }
        std::printf("  vehicle_group_cameras.xtbl: %zu <Vehicle_Group> rows\n", rows.size());
        std::printf("  vehicle_group_cameras.xtbl: skip_camera_transition=true: %lld / %zu, use_alt_freckle_cam=true: %lld / %zu "
                    "(CORRECTED 2026-10-01 attribute->element fix - was always 0/%zu before)\n",
                    skipTrans, rows.size(), altFreckle, rows.size(), rows.size());
    } else {
        std::printf("  vehicle_group_cameras.xtbl NOT FOUND in the given archives.\n");
    }

    auto reportCustCameras = [&](const char* file, size_t expectedSets, long long expectedViews, long long expectedStandardDef) {
        if (const Item* it = find1(lower(file))) {
            Document doc = ParseDocument(it->data.data(), it->data.size());
            auto sets = ParseCustCamerasTable(doc);
            long long views = 0, standardDef = 0;
            for (auto& s : sets) {
                views += static_cast<long long>(s.views.size());
                for (auto& v : s.views) standardDef += v.standardDef;
            }
            std::printf("  %s: %zu <camera> rows, %lld camera_view total (%lld standard_def-tagged)\n", file, sets.size(), views, standardDef);
            if (expectedSets > 0) GATE(sets.size() == expectedSets, "%s: %zu <camera> rows (spec 8.2, 16)", file, expectedSets);
            if (expectedViews > 0) GATE(views == expectedViews, "%s: %lld camera_view rows (spec 8.2, 16)", file, expectedViews);
            if (expectedStandardDef > 0) GATE(standardDef == expectedStandardDef, "%s: %lld standard_def-tagged (spec 8.2, 16)", file, expectedStandardDef);
        } else {
            std::printf("  %s NOT FOUND in the given archives.\n", file);
        }
    };
    reportCustCameras("item_cust_cameras.xtbl", 1, 76, 34);
    reportCustCameras("player_cust_cameras_new.xtbl", 1, 88, 44);
    reportCustCameras("vehicle_cust_cameras_new.xtbl", 96, 4751, 1102);

    // ------------------------------------------------------------------
    // user_interface.xtbl (spec 10) - the PC build selects the row named
    // "XBox2"; "PC_old - not used" ships as dead data on this platform.
    // ------------------------------------------------------------------
    std::printf("\n=== user_interface.xtbl (spec 10) ===\n");
    if (const Item* it = find1("user_interface.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto rows = ParseUserInterfaceTable(doc);
        std::printf("  %zu <UserInterface> rows: ", rows.size());
        for (auto& r : rows) std::printf("\"%s\" ", r.name ? r.name->c_str() : "(no name)");
        std::printf("\n");
        const UserInterfaceRow* active = FindActiveUserInterfaceRow(rows);
        GATE(active != nullptr, "a row named \"XBox2\" exists and is selected (spec 10)");
        bool hasPcOld = false;
        for (auto& r : rows)
            if (r.name && *r.name == "PC_old - not used") hasPcOld = true;
        GATE(hasPcOld, "the dead-data row \"PC_old - not used\" is present (spec 10)");
        if (active) {
            std::printf("  active row's ResolutionList/Resolutions/Resolution count: %zu, UIClusters/Cluster count: %zu\n",
                        active->resolutions.size(), active->clusters.size());
        }
    } else {
        std::printf("  NOT FOUND in the given archives.\n");
    }

    // ------------------------------------------------------------------
    // control_scheme_text.xtbl (spec 11) - no exact row count stated by the
    // spec; report raw counts only.
    // ------------------------------------------------------------------
    std::printf("\n=== control_scheme_text.xtbl (spec 11 - no exact count stated by the spec) ===\n");
    if (const Item* it = find1("control_scheme_text.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());

        // RAW pre-filter count: walk <ControlScheme>/<Controls>/<Control>
        // directly off the node tree (NOT through the typed reader, which
        // now applies the Platform filter - spec 11, CORRECTED 2026-10-01)
        // to measure what that filter actually drops in real data: "only
        // Controls whose Platform text is exactly 360 or All ... are kept;
        // every other value (e.g. PS3) is dropped at load."
        long long rawControls = 0;
        std::map<std::string, long long> rawPlatformCounts;
        for (const Node* scheme : Children(doc.table(), "ControlScheme")) {
            const Node* controls = FindChild(scheme, "Controls");
            for (const Node* c : Children(controls, "Control")) {
                ++rawControls;
                const std::string* p = ChildText(c, "Platform");
                ++rawPlatformCounts[p ? *p : std::string("(absent)")];
            }
        }

        auto schemes = ParseControlSchemeTextTable(doc);
        long long entries = 0;
        std::set<std::string> platforms;
        for (auto& s : schemes) {
            entries += static_cast<long long>(s.controls.size());
            for (auto& c : s.controls)
                if (c.platform) platforms.insert(*c.platform);
        }
        std::printf("  %zu <ControlScheme> rows, %lld total Control entries (POST Platform filter), distinct Platform values kept: %zu (",
                    schemes.size(), entries, platforms.size());
        for (auto& p : platforms) std::printf("%s ", p.c_str());
        std::printf(")\n");
        std::printf("  RAW (pre-filter): %lld <Control> rows total, by Platform value: ", rawControls);
        for (auto& kv : rawPlatformCounts) std::printf("%s=%lld ", kv.first.c_str(), kv.second);
        std::printf("\n");
        GATE(entries <= rawControls, "Platform filter never increases the Control count (%lld kept of %lld raw)", entries, rawControls);
        std::printf("  Platform filter: dropped %lld of %lld raw Control rows, kept %lld (360/All only)\n", rawControls - entries,
                     rawControls, entries);
    } else {
        std::printf("  NOT FOUND in the given archives.\n");
    }

    // ------------------------------------------------------------------
    // voice_control.xtbl (spec 12) - no exact row count stated; report raw
    // counts and a bitfield sanity spot-check (every field within its
    // documented bit-width range by construction, so this just reports the
    // clamp-triggered fraction as an informational stat).
    // ------------------------------------------------------------------
    std::printf("\n=== voice_control.xtbl (spec 12 - no exact count stated by the spec) ===\n");
    if (const Item* it = find1("voice_control.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto entries = ParseVoiceControlTable(doc);
        long long priorityPresent = 0, priorityClamped = 0;
        for (auto& e : entries) {
            if (e.priority) {
                ++priorityPresent;
                if (*e.priority < 0 || *e.priority > 31) ++priorityClamped;
            }
        }
        std::printf("  %zu <Entry> rows; Priority present: %lld / %zu (of those, outside the 0-31 5-bit range: %lld)\n", entries.size(),
                    priorityPresent, entries.size(), priorityClamped);
    } else {
        std::printf("  NOT FOUND in the given archives.\n");
    }

    // ------------------------------------------------------------------
    // credits_pc.xtbl (spec 13) - "7 Credits sections, 41 heading rows (37 titled
    // + 4 titleless), 1,047 items; type values used: Music, Name - Role, Centered Single Line
    // (3 of 4 defined)".
    // ------------------------------------------------------------------
    std::printf("\n=== credits_pc.xtbl (spec 13) ===\n");
    if (const Item* it = find1("credits_pc.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto sections = ParseCreditsTable(doc);
        long long headings = 0, items = 0;
        bool usesNameRole = false, usesMusic = false, usesImage = false, usesCentered = false;
        for (auto& s : sections) {
            headings += static_cast<long long>(s.headings.size());
            for (auto& h : s.headings) {
                items += static_cast<long long>(h.items.size());
                usesNameRole |= (h.type == 0);
                usesMusic |= (h.type == 1);
                usesImage |= (h.type == 2);
                usesCentered |= (h.type == 3);
            }
        }
        std::printf("  %zu Credits sections, %lld headings, %lld items\n", sections.size(), headings, items);
        GATE(sections.size() == 7, "7 Credits sections (spec 13, 16): %zu", sections.size());
        // spec-tables-ui-controls.md 13 / 16 [CONFIRMED - empirical, "confirmed by direct
        // count"]: "7 Credits sections, 41 heading rows (37 titled + 4 titleless -
        // corrected from an original undifferentiated '37'), 1,047 items". Gate updated
        // from the superseded 37 to the spec's current 41 (this harness's own hand count
        // of 1+10+13+11+3+2+1 = 41 already agreed).
        GATE(headings == 41, "41 headings (spec 13, 16: 37 titled + 4 titleless): %lld", headings);
        GATE(items == 1047, "1047 items (spec 13, 16): %lld", items);
        GATE(usesNameRole && usesMusic && usesCentered, "type values used include Name - Role, Music, Centered Single Line (spec 13, 16)");
        GATE(!usesImage, "\"Image\" (the 4th type) is unused in the shipped file (spec 13, 16)");
    } else {
        std::printf("  NOT FOUND in the given archives.\n");
    }

    // ------------------------------------------------------------------
    // control_filters.xtbl + control_parameters.xtbl (spec 14) - "22 rows,
    // non-<Table> <root><control_filters> shape" for control_filters.xtbl;
    // no exact count stated for control_parameters.xtbl.
    // ------------------------------------------------------------------
    std::printf("\n=== control_filters.xtbl + control_parameters.xtbl (spec 14) ===\n");
    if (const Item* it = find1("control_filters.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        GATE(doc.table() == nullptr, "control_filters.xtbl has NO <Table> element (spec 14's non-<Table> shape)");
        auto filters = ParseControlFiltersTable(doc);
        std::printf("  %zu <control_filter> rows\n", filters.size());
        // spec-tables-ui-controls.md 14 / 16 [CONFIRMED - empirical]: "22 rows in the real file
        // (corrected 2026-09-28, Team B: this document originally said 23, an off-by-one;
        // re-verified ... 22/22 exact)". Gate updated from the superseded 23 to 22.
        GATE(filters.size() == 22, "22 control_filter rows (spec 14, 16, corrected from 23): %zu", filters.size());
        std::set<std::string> types;
        for (auto& f : filters)
            if (f.type) types.insert(*f.type);
        std::printf("  distinct <type> values: ");
        for (auto& t : types) std::printf("\"%s\" ", t.c_str());
        std::printf("\n");
    } else {
        std::printf("  control_filters.xtbl NOT FOUND in the given archives.\n");
    }
    if (const Item* it = find1("control_parameters.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        GATE(doc.table() == nullptr, "control_parameters.xtbl has NO <Table> element (spec 14's non-<Table> shape)");
        auto params = ParseControlParametersTable(doc);
        std::printf("  %zu <control_parameter> rows (no exact count stated by the spec)\n", params.size());
        std::set<std::string> dataTypes;
        for (auto& p : params)
            if (p.dataType) dataTypes.insert(*p.dataType);
        std::printf("  distinct <data_type> values: ");
        for (auto& d : dataTypes) std::printf("\"%s\" ", d.c_str());
        std::printf("\n");
    } else {
        std::printf("  control_parameters.xtbl NOT FOUND in the given archives.\n");
    }

    std::printf("\n%s (%d gate failure%s of %d checks)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s", g_checks);
    return g_fail ? 1 : 0;
}
