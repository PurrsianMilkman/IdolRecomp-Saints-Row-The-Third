// Population gates for sr3tables_trafficai (include/sr3tables_trafficai/tables.h) over
// the 31 loadable tables of spec-tables-traffic-ai.md, against REAL shipped archives.
//
// Usage: validate_tables_trafficai_population <archive.vpp_pc> [...]
//   (accepts a list of archive paths on argv, same convention as
//   validate_xtbl_population.cpp; does not hardcode the cache path.)
//
// METHODOLOGY / SCOPE (read before reading the numbers below):
//   * LOCATE: every archive is walked recursively (vpp::Container, both raw and
//     compressed entries, nested containers included) exactly like
//     validate_xtbl_population.cpp's walk(), looking for an entry whose name
//     equals (case-insensitively) one of the 31 canonical filenames of spec
//     §1.1. traffic_types.xtbl is excluded - the spec found no loader for it
//     (§3) - and is reported separately, not silently skipped.
//   * PARSE: every found entry is parsed with sr3xtbl::ParseDocument and then
//     with this project's typed reader (sr3tables_trafficai). A FormatError is
//     counted, not thrown past.
//   * PRESENCE STATISTICS ("needed the unspecified-default path"): computed for
//     every ROW-LEVEL field of each table's row struct (Always<T> and
//     optional<T> members declared directly on the row, e.g. AmbientTrafficEvent
//     or RoadblockLayout). Fields inside a NESTED repeated sub-structure
//     (Object/Child, Level/Roadblock_layout entry, human_elm/ability_elm,
//     Timed/Movement/Drug_Effects leaves, State/Action, Transition/Blend, ...)
//     are reported only in aggregate (how many sub-rows were parsed), not
//     field-by-field - naming every nested field for all 31 tables was judged
//     not to fit this harness's budget; the aggregate count still shows whether
//     real data uses the sub-structure at all.
//   * UNRECOGNISED ELEMENTS: every row's DIRECT children (not deep/nested) are
//     compared against that table's known row-level vocabulary (the exact
//     element names spec-tables-traffic-ai.md's element tree gives for that
//     row) and any name outside it is counted and named.
//   * ENUM/FLAG MISMATCHES: for every field read through sr3tables_trafficai's
//     EnumText (index from sr3xtbl::EnumIndex against a compiled-in §1.5 name
//     list) or generic_vehicles' case-sensitive equivalent, a row whose text is
//     present but index == -1 is a real value the spec's name list does not
//     cover; those texts are counted and named.
//   * TYPE MISMATCHES: applied to a representative set of fields the spec calls
//     out as integer/unsigned (not to all ~500 scalar fields in this group): a
//     '.' in the raw text of a field the spec calls an integer, or a leading
//     '-' in the raw text of a field the spec calls explicitly unsigned
//     ("no sign handling - a leading '-' parses as 0", spec 1.3).
//   * CONTROL: after every table is analysed, the one with the most rows has
//     its row count re-computed with a deliberately wrong (suffixed) row
//     element name; that must come back 0 while the real name's count is > 0 -
//     proof the counting depends on the real name, not a tautology.
//   * NOT FOUND tables are printed plainly, not skipped.
//
// Every gate/count below carries a denominator.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "sr3tables_trafficai/tables.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"

namespace {

using namespace sr3tables_trafficai;
using sr3xtbl::Document;
using sr3xtbl::Node;

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// Small shared utilities (mirrors validate_xtbl_population.cpp's style)
// ---------------------------------------------------------------------------
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool endsWithCi(const std::string& s, const char* x) {
    const size_t n = std::strlen(x);
    return s.size() >= n && lower(s.substr(s.size() - n)) == x;
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
// G1: locate. The 31 canonical filenames of spec §1.1 (traffic_types.xtbl is
// deliberately excluded - no loader, §3).
// ---------------------------------------------------------------------------
const char* kTargets[] = {
    "traffic_lanes.xtbl", "ambient_traffic_events.xtbl", "roadblock_layouts.xtbl",
    "roadblock_notoriety.xtbl", "roadblock_stag_lockdown.xtbl", "panic_reactions.xtbl",
    "ai_goals.xtbl", "ai_behavior.xtbl", "ai_personalities.xtbl", "generic_characters.xtbl",
    "generic_vehicles.xtbl", "action_node_groups.xtbl", "action_node_notoriety.xtbl",
    "action_node_npcs.xtbl", "action_nodes.xtbl", "distant_peds.xtbl", "distant_ped_colors.xtbl",
    "distant_ped_spawn_parameters.xtbl", "distant_vehicles.xtbl", "distant_vehicle_colors.xtbl",
    "distant_vehicle_spawn_parameters.xtbl", "distant_vehicle_traffic_types.xtbl", "homies.xtbl",
    "follower_heads.xtbl", "driver_bailout.xtbl", "vehicle_despawn.xtbl", "Escort_constants.xtbl",
    "human_transition.xtbl", "PEDF_Life.xtbl", "Life_default.xtbl", "node_graph_files.xtbl",
};
constexpr size_t kNumTargets = sizeof(kTargets) / sizeof(kTargets[0]);

struct FoundItem {
    std::string archive; // e.g. "vpp_misc.vpp_pc/cutscenes.str2_pc"
    std::string name;    // on-disk name, real case preserved
    Bytes data;
    bool decodeOk = false;
    std::string decodeError; // set when decompression/raw-read failed
};

std::map<std::string, std::vector<FoundItem>> g_found; // key: lower(canonical target name)
long long g_containers = 0, g_entriesSeen = 0;

void walk(vpp::ByteView bytes, const std::string& path) {
    vpp::Container c(bytes);
    ++g_containers;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        ++g_entriesSeen;
        const std::string ln = lower(e.name);
        for (const char* target : kTargets) {
            if (ln != lower(target)) continue;
            FoundItem it;
            it.archive = path;
            it.name = e.name;
            if (e.payload.kind == vpp::PayloadKind::Compressed) {
                vpp::DecompressResult r = c.decompressEntry(i);
                if (r.status == vpp::DecodeStatus::Ok) {
                    it.data = std::move(r.data);
                    it.decodeOk = true;
                } else {
                    it.decodeError = r.diagnostic.empty() ? "decompress failed" : r.diagnostic;
                }
            } else {
                try {
                    vpp::ByteView v = c.rawEntryBytes(i);
                    it.data.assign(v.data(), v.data() + v.size());
                    it.decodeOk = true;
                } catch (const std::exception& e2) {
                    it.decodeError = e2.what();
                }
            }
            g_found[ln].push_back(std::move(it));
            break;
        }
    }
    // Recurse into nested raw containers, same heuristic as validate_xtbl_population.cpp:
    // skip entries that are themselves one of our targets (leaf .xtbl content, not a container).
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        if (endsWithCi(c.entries()[i].name, "xtbl")) continue;
        try {
            walk(c.rawEntryBytes(i), path + "/" + c.entries()[i].name);
        } catch (const std::exception&) {
        }
    }
}

// ---------------------------------------------------------------------------
// Generic measurement helpers
// ---------------------------------------------------------------------------
struct PresenceStat {
    long long present = 0, absent = 0;
    long long total() const { return present + absent; }
};
using PresenceMap = std::map<std::string, PresenceStat>;
void tally(PresenceMap& m, const char* field, bool present) {
    PresenceStat& s = m[field];
    (present ? s.present : s.absent)++;
}

struct UnrecognisedScan {
    long long rowsScanned = 0;
    std::map<std::string, long long> unknown; // element name (as seen) -> count
};
void scanUnrecognised(UnrecognisedScan& u, const Node* row, const std::vector<std::string>& known) {
    if (!row) return;
    ++u.rowsScanned;
    for (const Node* c : row->children()) {
        bool ok = false;
        for (const std::string& k : known) {
            if (sr3xtbl::NameEquals(c->name(), k)) { ok = true; break; }
        }
        if (!ok) ++u.unknown[c->name()];
    }
}

struct EnumMismatchScan {
    long long checked = 0;
    std::map<std::string, long long> unmatched; // raw text -> count
};
void scanEnum(EnumMismatchScan& s, const Text& text, int index) {
    if (!text) return;
    ++s.checked;
    if (index == -1) ++s.unmatched[*text];
}

struct TypeMismatchScan {
    long long checked = 0, mismatched = 0;
    std::vector<std::string> examples;
};
// unsignedField: also flags a leading '-' (spec 1.3: unsigned readers do not handle sign).
// Every field checked here is one the spec calls an integer, so a '.' is always a mismatch.
void checkIntText(TypeMismatchScan& s, const Node* row, const char* field, bool unsignedField) {
    const std::string* t = sr3xtbl::ChildText(row, field);
    if (!t || t->empty()) return;
    ++s.checked;
    bool bad = t->find('.') != std::string::npos;
    if (unsignedField && (*t)[0] == '-') bad = true;
    if (bad) {
        ++s.mismatched;
        if (s.examples.size() < 10) s.examples.push_back(field + std::string("=") + *t);
    }
}

void printPresence(const PresenceMap& m) {
    for (const auto& kv : m) {
        std::printf("      %-32s present %6lld / %-6lld\n", kv.first.c_str(), kv.second.present,
                    kv.second.total());
    }
}
void printUnrecognised(const UnrecognisedScan& u) {
    if (u.unknown.empty()) {
        std::printf("      unrecognised row-level elements: 0 / %lld rows\n", u.rowsScanned);
        return;
    }
    long long total = 0;
    for (const auto& kv : u.unknown) total += kv.second;
    std::printf("      unrecognised row-level elements: %lld occurrences / %lld rows scanned:\n", total,
                u.rowsScanned);
    for (const auto& kv : u.unknown) std::printf("        %-30s x%lld\n", kv.first.c_str(), kv.second);
}
void printEnum(const char* label, const EnumMismatchScan& s) {
    std::printf("      %-32s unmatched %lld / %lld checked", label, static_cast<long long>(s.unmatched.size()),
                s.checked);
    long long total = 0;
    for (const auto& kv : s.unmatched) total += kv.second;
    if (!s.unmatched.empty()) {
        std::printf(" (%lld occurrences):\n", total);
        for (const auto& kv : s.unmatched) std::printf("        %-30s x%lld\n", kv.first.c_str(), kv.second);
    } else {
        std::printf("\n");
    }
}
void printType(const char* label, const TypeMismatchScan& s) {
    std::printf("      %-32s mismatched %lld / %lld checked", label, s.mismatched, s.checked);
    if (!s.examples.empty()) {
        std::printf(" e.g. ");
        for (auto& e : s.examples) std::printf("[%s] ", e.c_str());
    }
    std::printf("\n");
}

// A record of one table's parsed documents, kept around for the control check.
struct TableRun {
    std::string canonicalName;
    long long totalRows = 0;
    std::function<const Node*(const Document&)> containerOf;
    std::string rowName;
};
std::vector<TableRun> g_runs;
std::vector<Document> g_keptDocsForControl; // owns the Documents the winning control run needs

long long g_parseFailures = 0;
long long g_filesParsed = 0;

// Parses every FoundItem for a canonical table, returning the successfully
// parsed Documents (kept in `out`) and counting FormatError failures globally.
void parseAll(const std::vector<FoundItem>& items, std::vector<Document>& out) {
    for (const FoundItem& it : items) {
        if (!it.decodeOk) { ++g_parseFailures; continue; }
        try {
            out.push_back(sr3xtbl::ParseDocument(std::string_view(
                reinterpret_cast<const char*>(it.data.data()), it.data.size())));
            ++g_filesParsed;
        } catch (const std::exception&) {
            ++g_parseFailures;
        }
    }
}

void printFilesFound(const std::vector<FoundItem>& items) {
    std::printf("    files found: %zu\n", items.size());
    for (const FoundItem& it : items) {
        std::printf("      %s :: %s%s\n", it.archive.c_str(), it.name.c_str(),
                    it.decodeOk ? "" : (" (DECODE FAILED: " + it.decodeError + ")").c_str());
    }
}

} // namespace

// ===========================================================================
// Per-table analysis. Each function prints its own report section and
// registers a TableRun for the control check.
// ===========================================================================
namespace {

// §2 traffic_lanes.xtbl
void analyzeTrafficLanes(const std::vector<FoundItem>& items) {
    std::printf("=== traffic_lanes.xtbl (§2) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    for (const Document& d : docs) {
        TrafficLanesTable t = ParseTrafficLanesTable(d);
        rows += static_cast<long long>(t.laneSpeeds.size());
        for (const TrafficLaneSpeed& s : t.laneSpeeds) tally(pm, "Speed", s.speed.present);
        const Node* newEntity = sr3xtbl::FindChild(d.table(), "NewEntity");
        const Node* grid = sr3xtbl::FindChild(newEntity, "LaneGrid");
        for (const Node* n = sr3xtbl::FindChild(grid, "LaneSpeed"); n; n = sr3xtbl::NextSibling(grid, n, "LaneSpeed"))
            scanUnrecognised(u, n, {"Speed"});
    }
    std::printf("    LaneSpeed rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back({"traffic_lanes.xtbl", rows,
                       [](const Document& d) {
                           return sr3xtbl::FindChild(sr3xtbl::FindChild(d.table(), "NewEntity"), "LaneGrid");
                       },
                       "LaneSpeed"});
}

// §4 ambient_traffic_events.xtbl
void analyzeAmbientTrafficEvents(const std::vector<FoundItem>& items) {
    std::printf("=== ambient_traffic_events.xtbl (§4) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name", "TOD_start", "TOD_end", "Flags", "Min_life",
                                             "Max_life", "Cooldown_time", "Roadblock_layout"};
    TypeMismatchScan todMismatch;
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Ambient_traffic_event"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Ambient_traffic_event")) {
            ++rows;
            AmbientTrafficEvent e = ParseAmbientTrafficEvent(row);
            tally(pm, "Name", e.name.has_value());
            tally(pm, "TOD_start", e.todStart.present);
            tally(pm, "TOD_end", e.todEnd.present);
            tally(pm, "Flags(container)", e.flagsPresent);
            tally(pm, "Min_life", e.minLife.present);
            tally(pm, "Max_life", e.maxLife.present);
            tally(pm, "Cooldown_time", e.cooldownTime.present);
            tally(pm, "Roadblock_layout", e.roadblockLayout.has_value());
            scanUnrecognised(u, row, known);
            checkIntText(todMismatch, row, "TOD_start", false);
            checkIntText(todMismatch, row, "TOD_end", false);
        }
    }
    std::printf("    Ambient_traffic_event rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    printType("TOD_start/TOD_end", todMismatch);
    g_runs.push_back({"ambient_traffic_events.xtbl", rows, [](const Document& d) { return d.table(); },
                       "Ambient_traffic_event"});
}

// §5 roadblock_layouts.xtbl
void analyzeRoadblockLayouts(const std::vector<FoundItem>& items) {
    std::printf("=== roadblock_layouts.xtbl (§5) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0, objects = 0, children = 0;
    PresenceMap pm;
    UnrecognisedScan u, uObj, uChild;
    EnumMismatchScan enumObjType, enumChildType;
    const std::vector<std::string> knownRow = {"Name", "Num_lanes", "Flags", "Objects"};
    const std::vector<std::string> knownObj = {"Type", "Resource_name", "Variant_Name", "Lane_index",
                                                "X_offset", "Z_offset", "Heading", "Hitpoints", "Children"};
    const std::vector<std::string> knownChild = {"Type", "Resource_name", "Variant_Name", "X_offset",
                                                  "Z_offset", "Heading", "Seat_Index", "Hitpoints"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Roadblock_layouts"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Roadblock_layouts")) {
            ++rows;
            RoadblockLayout l = ParseRoadblockLayout(row);
            tally(pm, "Name", l.name.has_value());
            tally(pm, "Num_lanes", l.numLanes.present);
            scanUnrecognised(u, row, knownRow);
            const Node* objs = sr3xtbl::FindChild(row, "Objects");
            for (const Node* orow = sr3xtbl::FindChild(objs, "Object"); orow;
                 orow = sr3xtbl::NextSibling(objs, orow, "Object")) {
                ++objects;
                RoadblockObject o = ParseRoadblockObject(orow);
                scanEnum(enumObjType, o.type.text, o.type.index);
                scanUnrecognised(uObj, orow, knownObj);
                const Node* kids = sr3xtbl::FindChild(orow, "Children");
                for (const Node* crow = sr3xtbl::FindChild(kids, "Child"); crow;
                     crow = sr3xtbl::NextSibling(kids, crow, "Child")) {
                    ++children;
                    RoadblockChild c = ParseRoadblockChild(crow);
                    scanEnum(enumChildType, c.type.text, c.type.index);
                    scanUnrecognised(uChild, crow, knownChild);
                }
            }
        }
    }
    std::printf("    Roadblock_layouts rows: %lld, Objects: %lld, Children: %lld\n", rows, objects, children);
    printPresence(pm);
    printUnrecognised(u);
    std::printf("    Object level:\n");
    printUnrecognised(uObj);
    printEnum("Object/Type", enumObjType);
    std::printf("    Child level:\n");
    printUnrecognised(uChild);
    printEnum("Child/Type", enumChildType);
    g_runs.push_back({"roadblock_layouts.xtbl", rows, [](const Document& d) { return d.table(); },
                       "Roadblock_layouts"});
}

// §6 roadblock_notoriety.xtbl
void analyzeRoadblockNotoriety(const std::vector<FoundItem>& items) {
    std::printf("=== roadblock_notoriety.xtbl (§6) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0, levels = 0, layoutEntries = 0;
    PresenceMap pm, levelPm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name", "Team", "Levels"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Roadblock_notoriety"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Roadblock_notoriety")) {
            ++rows;
            RoadblockNotoriety r = ParseRoadblockNotoriety(row);
            tally(pm, "Name", r.name.has_value());
            tally(pm, "Team", r.team.has_value());
            scanUnrecognised(u, row, known);
            for (const RoadblockNotorietyLevel& lvl : r.levels) {
                ++levels;
                tally(levelPm, "Notoriety_level", lvl.notorietyLevel.present);
                tally(levelPm, "Max_active_roadblocks", lvl.maxActiveRoadblocks.present);
                tally(levelPm, "Min_time", lvl.minTime.present);
                tally(levelPm, "Max_time", lvl.maxTime.present);
                tally(levelPm, "Roadblock_layouts(container)", lvl.roadblockLayoutsPresent);
                layoutEntries += static_cast<long long>(lvl.layouts.size());
            }
        }
    }
    std::printf("    Roadblock_notoriety rows: %lld, Levels: %lld, layout entries: %lld\n", rows, levels,
                layoutEntries);
    printPresence(pm);
    std::printf("    Level fields:\n");
    printPresence(levelPm);
    printUnrecognised(u);
    g_runs.push_back({"roadblock_notoriety.xtbl", rows, [](const Document& d) { return d.table(); },
                       "Roadblock_notoriety"});
}

// §7 roadblock_stag_lockdown.xtbl
void analyzeRoadblockStagLockdown(const std::vector<FoundItem>& items) {
    std::printf("=== roadblock_stag_lockdown.xtbl (§7) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long entries = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Roadblock_Layout", "Spawn_Point"};
    for (const Document& d : docs) {
        RoadblockStagLockdownTable t = ParseRoadblockStagLockdownTable(d);
        std::printf("    rowPresent: %s\n", t.rowPresent ? "true" : "false");
        entries += static_cast<long long>(t.entries.size());
        const Node* outer = sr3xtbl::FindChild(d.table(), "Roadblock_stag_lockdown");
        const Node* list = sr3xtbl::FindChild(outer, "Roadblock_List");
        // CONTROL for the "sic" misspelling: count correctly-spelled "Roadblock" rows too,
        // so the report shows whether real data uses the correct spelling (which this
        // reader, faithfully to the engine, would never see).
        long long correctlySpelled = static_cast<long long>(sr3xtbl::CountChildren(list, "Roadblock"));
        if (correctlySpelled > 0)
            std::printf("    NOTE: %lld correctly-spelled <Roadblock> row(s) exist and are invisible to the "
                        "engine's own reader (spec §7 sic) - not counted above\n",
                        correctlySpelled);
        for (const Node* row = sr3xtbl::FindChild(list, "Roadbock"); row;
             row = sr3xtbl::NextSibling(list, row, "Roadbock")) {
            RoadblockStagLockdownEntry e = ParseRoadblockStagLockdownEntry(row);
            tally(pm, "Roadblock_Layout", e.roadblockLayout.has_value());
            tally(pm, "Spawn_Point", e.spawnPoint.has_value());
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Roadbock (sic) entries: %lld\n", entries);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back({"roadblock_stag_lockdown.xtbl", entries,
                       [](const Document& d) {
                           return sr3xtbl::FindChild(sr3xtbl::FindChild(d.table(), "Roadblock_stag_lockdown"),
                                                      "Roadblock_List");
                       },
                       "Roadbock"});
}

// §8 panic_reactions.xtbl
void analyzePanicReactions(const std::vector<FoundItem>& items) {
    std::printf("=== panic_reactions.xtbl (§8) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name",     "Priority", "Timed",           "Movement",
                                             "Player_Animations", "NPC_Animations",    "Drug_Effects",
                                             "Persona_Situation"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Panic"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Panic")) {
            ++rows;
            PanicReaction r = ParsePanicReaction(row);
            tally(pm, "Name", r.name.has_value());
            tally(pm, "Priority", r.priority.present);
            tally(pm, "Timed(container)", r.timedPresent);
            tally(pm, "Movement(container)", r.movementPresent);
            tally(pm, "Drug_Effects(container)", r.drugEffectsPresent);
            tally(pm, "Persona_Situation", r.personaSituation.has_value());
            tally(pm, "Player_Animations(container)", r.playerAnimationsPresent);
            tally(pm, "NPC_Animations(container)", r.npcAnimationsPresent);
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Panic rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back({"panic_reactions.xtbl", rows, [](const Document& d) { return d.table(); }, "Panic"});
}

// §9 ai_goals.xtbl
void analyzeAiGoals(const std::vector<FoundItem>& items) {
    std::printf("=== ai_goals.xtbl (§9) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0, actions = 0;
    UnrecognisedScan u;
    EnumMismatchScan nameEnum, actionEnum;
    const std::vector<std::string> known = {"Name", "order_of_actions"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "ai_goal"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "ai_goal")) {
            ++rows;
            AiGoal g = ParseAiGoal(row);
            scanEnum(nameEnum, g.name.text, g.name.index);
            scanUnrecognised(u, row, known);
            for (const AiGoalAction& a : g.actions) { ++actions; scanEnum(actionEnum, a.action.text, a.action.index); }
        }
    }
    std::printf("    ai_goal rows: %lld, actions: %lld\n", rows, actions);
    printUnrecognised(u);
    printEnum("Name (vs kAiGoalNames)", nameEnum);
    printEnum("action (vs kCombatActionNames)", actionEnum);
    g_runs.push_back({"ai_goals.xtbl", rows, [](const Document& d) { return d.table(); }, "ai_goal"});
}

// §10 ai_behavior.xtbl
void analyzeAiBehavior(const std::vector<FoundItem>& items) {
    std::printf("=== ai_behavior.xtbl (§10) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0, humanDesc = 0, abilities = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    EnumMismatchScan weaponEnum, actionEnum;
    const std::vector<std::string> known = {"Name",       "Scripted_Only", "combat_aggression", "Human_Description",
                                             "Riotshield_Only", "Actions", "Options", "Goals",
                                             "goals_can_process_early"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Behavior"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Behavior")) {
            ++rows;
            AiBehavior b = ParseAiBehavior(row);
            tally(pm, "Name", b.name.has_value());
            tally(pm, "Scripted_Only", b.scriptedOnly.present);
            tally(pm, "combat_aggression", b.combatAggression.has_value());
            tally(pm, "Riotshield_Only", b.riotshieldOnly.present);
            tally(pm, "Options(container)", b.options.present);
            tally(pm, "Goals/Survive", b.survivePresent);
            tally(pm, "Goals/Suppress", b.suppressPresent);
            tally(pm, "Goals/Cover", b.cover.present);
            tally(pm, "Goals/Taking_damage", b.takingDamagePresent);
            tally(pm, "Goals/Group", b.group.present);
            tally(pm, "Goals/Advance", b.advancePresent);
            tally(pm, "Goals/Avoid_brute", b.avoidBrutePresent);
            tally(pm, "Goals/Kill_near_player", b.killNearPlayerPresent);
            tally(pm, "Goals/Retreat", b.retreatPresent);
            tally(pm, "Goals/Kill_Enemy", b.killEnemy.present);
            tally(pm, "Goals/Reload", b.reloadPresent);
            tally(pm, "Goals/Flush_Out", b.flushOut.present);
            tally(pm, "Goals/Acquire_Target", b.acquireTarget.present);
            tally(pm, "Goals/Investigate", b.investigate.present);
            tally(pm, "goals_can_process_early", b.goalsCanProcessEarly.present);
            scanUnrecognised(u, row, known);
            humanDesc += static_cast<long long>(b.humanDescription.size());
            for (const AiHumanDescEntry& h : b.humanDescription) scanEnum(weaponEnum, h.weaponClass.text, h.weaponClass.index);
            abilities += static_cast<long long>(b.actions.size());
            for (const AiAbilityEntry& a : b.actions) scanEnum(actionEnum, a.action.text, a.action.index);
        }
    }
    std::printf("    Behavior rows: %lld, human_elm: %lld, ability_elm: %lld\n", rows, humanDesc, abilities);
    printPresence(pm);
    printUnrecognised(u);
    printEnum("Weapon_Class", weaponEnum);
    printEnum("Action (ability_elm)", actionEnum);
    g_runs.push_back({"ai_behavior.xtbl", rows, [](const Document& d) { return d.table(); }, "Behavior"});
}

// §11 ai_personalities.xtbl
void analyzeAiPersonalities(const std::vector<FoundItem>& items) {
    std::printf("=== ai_personalities.xtbl (§11) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name", "combat_aggression", "Flee_tweaks", "Ambient_tweaks"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Personality"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Personality")) {
            ++rows;
            AiPersonality p = ParseAiPersonality(row);
            tally(pm, "Name", p.name.has_value());
            tally(pm, "combat_aggression", p.combatAggression.has_value());
            tally(pm, "Flee_tweaks(container)", p.fleeTweaksPresent);
            tally(pm, "Ambient_tweaks(container)", p.ambientTweaksPresent);
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Personality rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back({"ai_personalities.xtbl", rows, [](const Document& d) { return d.table(); }, "Personality"});
}

// §12 generic_characters.xtbl / generic_vehicles.xtbl
void analyzeGenericCharacters(const std::vector<FoundItem>& items) {
    std::printf("=== generic_characters.xtbl (§12) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name", "Spawn_Name"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Generics_Table"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Generics_Table")) {
            ++rows;
            GenericCharacterRow r = ParseGenericCharacterRow(row);
            tally(pm, "Name", r.name.has_value());
            tally(pm, "Spawn_Name", r.spawnName.has_value());
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Generics_Table rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back(
        {"generic_characters.xtbl", rows, [](const Document& d) { return d.table(); }, "Generics_Table"});
}
void analyzeGenericVehicles(const std::vector<FoundItem>& items) {
    std::printf("=== generic_vehicles.xtbl (§12) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    EnumMismatchScan nameEnum;
    const std::vector<std::string> known = {"Name", "Spawn_Name", "Variant_Name"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Generics_Table"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Generics_Table")) {
            ++rows;
            GenericVehicleRow r = ParseGenericVehicleRow(row);
            tally(pm, "Name", r.name.has_value());
            tally(pm, "Spawn_Name", r.spawnName.has_value());
            tally(pm, "Variant_Name", r.variantName.has_value());
            scanEnum(nameEnum, r.name, r.index); // case-SENSITIVE match (spec §12) - see ParseGenericVehicleRow
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Generics_Table rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    printEnum("Name (case-sensitive vs kGenericVehicleSlotNames)", nameEnum);
    g_runs.push_back(
        {"generic_vehicles.xtbl", rows, [](const Document& d) { return d.table(); }, "Generics_Table"});
}

// §13.1 action_node_groups.xtbl
void analyzeActionNodeGroups(const std::vector<FoundItem>& items) {
    std::printf("=== action_node_groups.xtbl (§13.1) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {
        "Name", "Min_spacing", "Convert_to_ped_min_delay", "Convert_to_ped_max_delay", "Spawning",
        "Capture_peds", "Outdoor_node", "Use_In_Rain", "Store_browsing", "Store_ownership_spawn",
        "Exclusive_Actions", "sequence_nodes", "On_Water", "Player_Allowed", "Spawn_During_High_Notoriety",
        "GroupDisabled", "Stag_Node", "Anti_Saints_Node", "Reference_object", "Spawn_Timer", "Instance_Cap",
        "MaxSpawns", "npc_list", "Notoriety_Info", "Music_Emitter", "Min_Player_Rank"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "action_node_group"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "action_node_group")) {
            ++rows;
            ActionNodeGroup g = ParseActionNodeGroup(row);
            tally(pm, "Name", g.name.has_value());
            tally(pm, "Min_spacing", g.minSpacing.present);
            tally(pm, "Convert_to_ped_min_delay", g.convertToPedMinDelay.has_value());
            tally(pm, "Convert_to_ped_max_delay", g.convertToPedMaxDelay.present);
            tally(pm, "Capture_peds", g.capturePeds.present);
            tally(pm, "Outdoor_node", g.outdoorNode.present);
            tally(pm, "Reference_object", g.referenceObjectPresent);
            tally(pm, "Spawn_Timer", g.spawnTimer.present);
            tally(pm, "Instance_Cap", g.instanceCap.has_value());
            tally(pm, "MaxSpawns", g.maxSpawns.has_value());
            tally(pm, "Notoriety_Info", g.notorietyInfo.has_value());
            tally(pm, "Music_Emitter", g.musicEmitter.has_value());
            tally(pm, "Min_Player_Rank", g.minPlayerRank.has_value());
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    action_node_group rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back(
        {"action_node_groups.xtbl", rows, [](const Document& d) { return d.table(); }, "action_node_group"});
}

// §13.2 action_node_notoriety.xtbl
void analyzeActionNodeNotoriety(const std::vector<FoundItem>& items) {
    std::printf("=== action_node_notoriety.xtbl (§13.2) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name", "Spawn_Flags", "Min_Notoriety", "Max_Notoriety", "Weapon"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Action_Node_Notoriety"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Action_Node_Notoriety")) {
            ++rows;
            ActionNodeNotoriety n = ParseActionNodeNotoriety(row);
            tally(pm, "Name", n.name.has_value());
            tally(pm, "Min_Notoriety", n.minNotoriety.present);
            tally(pm, "Max_Notoriety", n.maxNotoriety.present);
            tally(pm, "Weapon", n.weapon.has_value());
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Action_Node_Notoriety rows found in file: %lld (spec §13.2: the engine keeps only the "
                "first)\n",
                rows);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back({"action_node_notoriety.xtbl", rows, [](const Document& d) { return d.table(); },
                       "Action_Node_Notoriety"});
}

// §13.3 action_node_npcs.xtbl
void analyzeActionNodeNpcs(const std::vector<FoundItem>& items) {
    std::printf("=== action_node_npcs.xtbl (§13.3) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    EnumMismatchScan sexEnum, teamEnum, priorityEnum;
    const std::vector<std::string> known = {
        "Name", "Persona_situation", "Enter_anim", "Exit_anim", "Startle_anim", "Animations", "CowerAnims",
        "Min_anim_delay", "Max_anim_delay", "Synced_Animation", "Single_Use", "Flee_On_Exit", "Get_Bag",
        "Reverse_Enter", "Can_Use_With_Bag", "Never_Exit", "Can_Not_Exit", "Spawn_Only", "Standardize_Height",
        "React_To_Peds", "Required_NPC", "NPC_Dies", "Drunk_Node", "Can_Be_Bumped", "Stationary_Node",
        "Spawn_Priority", "ExitOnItemDislodge", "SpawnInAir", "SpawnOffNavmesh", "SupportsNonHeroic",
        "SupportsHeroic", "EquipWeapon", "RequiresRifle", "Follow_Nodes", "Random_destination_percent",
        "Model_List", "Num_Resource_Requests", "Sex", "Team"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "action_node_npc"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "action_node_npc")) {
            ++rows;
            ActionNodeNpc n = ParseActionNodeNpc(row);
            tally(pm, "Name", n.name.has_value());
            tally(pm, "Min_anim_delay", n.minAnimDelay.present);
            tally(pm, "Max_anim_delay", n.maxAnimDelay.present);
            tally(pm, "Random_destination_percent", n.randomDestinationPercent.present);
            tally(pm, "Num_Resource_Requests", n.numResourceRequests.present);
            scanEnum(sexEnum, n.sex.text, n.sex.index);
            scanEnum(teamEnum, n.team.text, n.team.index);
            scanEnum(priorityEnum, n.spawnPriority.text, n.spawnPriority.index);
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    action_node_npc rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    printEnum("Sex", sexEnum);
    printEnum("Team", teamEnum);
    printEnum("Spawn_Priority", priorityEnum);
    g_runs.push_back(
        {"action_node_npcs.xtbl", rows, [](const Document& d) { return d.table(); }, "action_node_npc"});
}

// §13.4 action_nodes.xtbl (no <Table> wrapper)
void analyzeActionNodes(const std::vector<FoundItem>& items) {
    std::printf("=== action_nodes.xtbl (§13.4, no <Table> wrapper) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0, actionNodes = 0, spawnNodes = 0, vehicles = 0;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"name", "num_action_nodes", "num_spawn_nodes", "num_vehicles",
                                             "action_node", "spawn_node", "vehicle"};
    for (const Document& d : docs) {
        ActionNodesFile f = ParseActionNodesFile(d);
        rows += static_cast<long long>(f.objects.size());
        for (const auto& o : f.objects) {
            actionNodes += static_cast<long long>(o.actionNodes.size());
            spawnNodes += static_cast<long long>(o.spawnNodes.size());
            vehicles += static_cast<long long>(o.vehicles.size());
        }
        for (const Node* row = sr3xtbl::FindChild(d.root(), "object"); row;
             row = sr3xtbl::NextSibling(d.root(), row, "object"))
            scanUnrecognised(u, row, known);
    }
    std::printf("    object rows: %lld, action_node: %lld, spawn_node: %lld, vehicle: %lld\n", rows, actionNodes,
                spawnNodes, vehicles);
    printUnrecognised(u);
    g_runs.push_back({"action_nodes.xtbl", rows, [](const Document& d) { return d.root(); }, "object"});
}

// §14.1 distant_peds.xtbl
void analyzeDistantPeds(const std::vector<FoundItem>& items) {
    std::printf("=== distant_peds.xtbl (§14.1) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name", "Mesh"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Distant_Pedestrian"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Distant_Pedestrian")) {
            ++rows;
            DistantPedestrian p = ParseDistantPedestrian(row);
            tally(pm, "Name", p.name.has_value());
            tally(pm, "Mesh", p.mesh.has_value());
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Distant_Pedestrian rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back(
        {"distant_peds.xtbl", rows, [](const Document& d) { return d.table(); }, "Distant_Pedestrian"});
}

// §14.2 distant_ped_colors.xtbl
void analyzeDistantPedColors(const std::vector<FoundItem>& items) {
    std::printf("=== distant_ped_colors.xtbl (§14.2) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0, skin = 0, hair = 0, clothing = 0;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Skin_Colors", "Hair_Colors", "Clothing_Colors"};
    for (const Document& d : docs) {
        DistantPedColorsTable t = ParseDistantPedColorsTable(d);
        rows += static_cast<long long>(t.colorSets.size());
        for (const auto& s : t.colorSets) {
            skin += static_cast<long long>(s.skinColors.size());
            hair += static_cast<long long>(s.hairColors.size());
            clothing += static_cast<long long>(s.clothingColors.size());
        }
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Color_Set"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Color_Set"))
            scanUnrecognised(u, row, known);
    }
    std::printf("    Color_Set rows: %lld, skin: %lld, hair: %lld, clothing sets: %lld\n", rows, skin, hair,
                clothing);
    printUnrecognised(u);
    g_runs.push_back({"distant_ped_colors.xtbl", rows, [](const Document& d) { return d.table(); }, "Color_Set"});
}

// §14.3 distant_ped_spawn_parameters.xtbl / distant_vehicle_spawn_parameters.xtbl
void analyzeDistantSpawnParameters(const std::vector<FoundItem>& pedItems,
                                    const std::vector<FoundItem>& vehItems) {
    std::printf("=== distant_ped_spawn_parameters.xtbl / distant_vehicle_spawn_parameters.xtbl (§14.3) ===\n");
    std::printf("  -- ped --\n");
    printFilesFound(pedItems);
    static std::vector<Document> pedDocs;
    parseAll(pedItems, pedDocs);
    long long pedFound = 0;
    PresenceMap pedPm;
    for (const Document& d : pedDocs) {
        auto p = ParseDistantPedSpawnParameters(d);
        if (!p) continue;
        ++pedFound;
        tally(pedPm, "Length_Per_Spawn", p->lengthPerSpawn.present);
        tally(pedPm, "Base_Spawning_Dist", p->baseSpawningDist.present);
        tally(pedPm, "Spawn_Angle", p->spawnAngle.present);
        tally(pedPm, "Despawn_Angle", p->despawnAngle.present);
    }
    std::printf("    Distant_Ped_Spawn_Parameters present: %lld / %zu files\n", pedFound, pedDocs.size());
    printPresence(pedPm);

    std::printf("  -- vehicle --\n");
    printFilesFound(vehItems);
    static std::vector<Document> vehDocs;
    parseAll(vehItems, vehDocs);
    long long vehFound = 0;
    PresenceMap vehPm;
    for (const Document& d : vehDocs) {
        auto p = ParseDistantVehicleSpawnParameters(d);
        if (!p) continue;
        ++vehFound;
        tally(vehPm, "Base_Spawning_Dist", p->baseSpawningDist.present);
        tally(vehPm, "Spawn_Angle", p->spawnAngle.present);
        tally(vehPm, "Despawn_Angle", p->despawnAngle.present);
    }
    std::printf("    Distant_Vehicle_Spawn_Parameters present: %lld / %zu files\n", vehFound, vehDocs.size());
    printPresence(vehPm);
    g_runs.push_back({"distant_ped_spawn_parameters.xtbl", pedFound, [](const Document& d) { return d.table(); },
                       "Distant_Ped_Spawn_Parameters"});
    g_runs.push_back({"distant_vehicle_spawn_parameters.xtbl", vehFound,
                       [](const Document& d) { return d.table(); }, "Distant_Vehicle_Spawn_Parameters"});
}

// §14.4 distant_vehicle_traffic_types.xtbl
void analyzeDistantVehicleTrafficTypes(const std::vector<FoundItem>& items) {
    std::printf("=== distant_vehicle_traffic_types.xtbl (§14.4) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    TypeMismatchScan splineMismatch;
    const std::vector<std::string> known = {"Name", "Spline_Type", "Length_Per_Spawn", "No_Spawn_Zone",
                                             "Dummy_Car_Length"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Distant_Vehicle_Traffic_Type"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Distant_Vehicle_Traffic_Type")) {
            ++rows;
            DistantVehicleTrafficType t = ParseDistantVehicleTrafficType(row);
            tally(pm, "Name", t.name.has_value());
            tally(pm, "Spline_Type", t.splineType.present);
            tally(pm, "Length_Per_Spawn", t.lengthPerSpawn.present);
            tally(pm, "No_Spawn_Zone", t.noSpawnZone.present);
            tally(pm, "Dummy_Car_Length", t.dummyCarLength.present);
            scanUnrecognised(u, row, known);
            checkIntText(splineMismatch, row, "Spline_Type", true);
        }
    }
    std::printf("    Distant_Vehicle_Traffic_Type rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    printType("Spline_Type (unsigned)", splineMismatch);
    g_runs.push_back({"distant_vehicle_traffic_types.xtbl", rows, [](const Document& d) { return d.table(); },
                       "Distant_Vehicle_Traffic_Type"});
}

// §14.5 / §14.6
void analyzeDistantVehicleColorsAndVehicles(const std::vector<FoundItem>& colorItems,
                                             const std::vector<FoundItem>& vehItems) {
    std::printf("=== distant_vehicle_colors.xtbl (§14.5) ===\n");
    printFilesFound(colorItems);
    static std::vector<Document> colorDocs;
    parseAll(colorItems, colorDocs);
    long long colorRows = 0;
    PresenceMap colorPm;
    for (const Document& d : colorDocs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Distant_Vehicle_Color"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Distant_Vehicle_Color")) {
            ++colorRows;
            DistantVehicleColorRow c = ParseDistantVehicleColorRow(row);
            tally(colorPm, "Color/R", c.color.r.present);
            tally(colorPm, "Color/G", c.color.g.present);
            tally(colorPm, "Color/B", c.color.b.present);
        }
    }
    std::printf("    Distant_Vehicle_Color rows: %lld\n", colorRows);
    printPresence(colorPm);
    g_runs.push_back({"distant_vehicle_colors.xtbl", colorRows, [](const Document& d) { return d.table(); },
                       "Distant_Vehicle_Color"});

    std::printf("=== distant_vehicles.xtbl (§14.6) ===\n");
    printFilesFound(vehItems);
    static std::vector<Document> vehDocs;
    parseAll(vehItems, vehDocs);
    long long vehRows = 0;
    PresenceMap vehPm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name", "Mesh", "Traffic_Type"};
    for (const Document& d : vehDocs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Distant_Vehicle"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Distant_Vehicle")) {
            ++vehRows;
            DistantVehicleRow r = ParseDistantVehicleRow(row);
            tally(vehPm, "Name", r.name.has_value());
            tally(vehPm, "Mesh", r.mesh.has_value());
            tally(vehPm, "Traffic_Type", r.trafficType.has_value());
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Distant_Vehicle rows: %lld\n", vehRows);
    printPresence(vehPm);
    printUnrecognised(u);
    g_runs.push_back(
        {"distant_vehicles.xtbl", vehRows, [](const Document& d) { return d.table(); }, "Distant_Vehicle"});
}

// §15 homies.xtbl
void analyzeHomies(const std::vector<FoundItem>& items) {
    std::printf("=== homies.xtbl (§15) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0, humans = 0, vehicles = 0, blocked = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name",   "Display_Name", "Display_Desc", "Humans",
                                             "Vehicles", "Flags",      "Image_name",   "Audio",
                                             "Blocked_for_Mission", "Framework", "Is_DLC", "_Editor"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Homie"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Homie")) {
            ++rows;
            Homie h = ParseHomie(row);
            tally(pm, "Name", h.name.has_value());
            tally(pm, "Image_name", h.imageName.has_value());
            tally(pm, "Display_Name", h.displayName.has_value());
            tally(pm, "Display_Desc", h.displayDesc.has_value());
            tally(pm, "Audio", h.audio.has_value());
            tally(pm, "Framework", h.framework.has_value());
            tally(pm, "Is_DLC", h.isDlc.present);
            humans += static_cast<long long>(h.humans.size());
            vehicles += static_cast<long long>(h.vehicles.size());
            blocked += static_cast<long long>(h.blockedForMission.size());
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Homie rows: %lld, humans: %lld, vehicles: %lld, blocked_for_mission entries: %lld\n", rows,
                humans, vehicles, blocked);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back({"homies.xtbl", rows, [](const Document& d) { return d.table(); }, "Homie"});
}

// §16 follower_heads.xtbl
void analyzeFollowerHeads(const std::vector<FoundItem>& items) {
    std::printf("=== follower_heads.xtbl (§16) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Persona", "Charname", "Bitmap"};
    for (const Document& d : docs) {
        const Node* heads = sr3xtbl::FindChild(d.table(), "Heads");
        const Node* assoc = sr3xtbl::FindChild(heads, "Associations");
        for (const Node* row = sr3xtbl::FindChild(assoc, "Follower"); row;
             row = sr3xtbl::NextSibling(assoc, row, "Follower")) {
            ++rows;
            FollowerHead f = ParseFollowerHead(row);
            tally(pm, "Persona", f.persona.has_value());
            tally(pm, "Charname", f.charname.has_value());
            tally(pm, "Bitmap", f.bitmap.has_value());
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Follower rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back({"follower_heads.xtbl", rows,
                       [](const Document& d) {
                           return sr3xtbl::FindChild(sr3xtbl::FindChild(d.table(), "Heads"), "Associations");
                       },
                       "Follower"});
}

// §17 driver_bailout.xtbl
void analyzeDriverBailout(const std::vector<FoundItem>& items) {
    std::printf("=== driver_bailout.xtbl (§17) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long found = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {
        "Max_Distance", "Min_Distance", "Max_Damage", "Min_Damage", "Max_Damage_Percent_Reward", "Min_Velocity",
        "End_Time", "Record_Display_Time", "Record_Queue_Time", "Record_Threshold", "Show_Damage_Record",
        "Max_Respect", "Max_Lifetime_Respect", "Max_Cash", "Vehicle_Damage_Multiplier"};
    for (const Document& d : docs) {
        auto b = ParseDriverBailout(d);
        if (!b) continue;
        ++found;
        tally(pm, "Max_Distance", b->maxDistance.present);
        tally(pm, "Min_Distance", b->minDistance.present);
        tally(pm, "Max_Damage", b->maxDamage.present);
        tally(pm, "Min_Damage", b->minDamage.present);
        tally(pm, "Min_Velocity", b->minVelocity.present);
        tally(pm, "End_Time", b->endTime.present);
        tally(pm, "Record_Display_Time", b->recordDisplayTime.has_value());
        tally(pm, "Record_Queue_Time", b->recordQueueTime.has_value());
        tally(pm, "Show_Damage_Record", b->showDamageRecord.present);
        tally(pm, "Max_Respect", b->maxRespect.present);
        tally(pm, "Max_Cash", b->maxCash.present);
        const Node* n = sr3xtbl::FindChild(d.table(), "Driver_Bailout");
        scanUnrecognised(u, n, known);
    }
    std::printf("    Driver_Bailout present: %lld / %zu files\n", found, docs.size());
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back(
        {"driver_bailout.xtbl", found, [](const Document& d) { return d.table(); }, "Driver_Bailout"});
}

// §18 vehicle_despawn.xtbl
void analyzeVehicleDespawn(const std::vector<FoundItem>& items) {
    std::printf("=== vehicle_despawn.xtbl (§18) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long found = 0, inView = 0, outView = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"in_view_despawn_info", "out_of_view_despawn_info",
                                             "abandon_despawn_delay", "player_vehicle_despawn_distance",
                                             "safety_despawn_time"};
    for (const Document& d : docs) {
        auto t = ParseVehicleDespawnTable(d);
        if (!t) continue;
        ++found;
        inView += static_cast<long long>(t->inViewDespawnInfo.size());
        outView += static_cast<long long>(t->outOfViewDespawnInfo.size());
        tally(pm, "abandon_despawn_delay", t->abandonDespawnDelay.present);
        tally(pm, "player_vehicle_despawn_distance", t->playerVehicleDespawnDistance.present);
        const Node* inner = sr3xtbl::FindChild(d.table(), "Table");
        scanUnrecognised(u, inner, known);
    }
    std::printf("    inner <Table> present: %lld / %zu files, in_view entries: %lld, out_of_view entries: %lld\n",
                found, docs.size(), inView, outView);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back({"vehicle_despawn.xtbl", found,
                       [](const Document& d) { return sr3xtbl::FindChild(d.table(), "Table"); },
                       "in_view_despawn_info"});
}

// §19 Escort_constants.xtbl
void analyzeEscortConstants(const std::vector<FoundItem>& items) {
    std::printf("=== Escort_constants.xtbl (§19) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long found = 0;
    PresenceMap pm;
    for (const Document& d : docs) {
        auto c = ParseEscortConstants(d);
        if (!c) continue;
        ++found;
        tally(pm, "Rage(group)", c->rage.present);
        tally(pm, "Rage/Desired_speed_MPS", c->rage.desiredSpeedMps.present);
        tally(pm, "Penalties/Vehicles(group)", c->vehicles.present);
        tally(pm, "Penalties/Humans(group)", c->humans.present);
        tally(pm, "Penalties/Movers(group)", c->movers.present);
        tally(pm, "Penalties/World(group)", c->world.present);
    }
    std::printf("    Tiger_Constants present: %lld / %zu files\n", found, docs.size());
    printPresence(pm);
    g_runs.push_back(
        {"Escort_constants.xtbl", found,
         [](const Document& d) { return sr3xtbl::FindChild(d.table(), "Escort_Constants"); }, "Tiger_Constants"});
}

// §20 human_transition.xtbl
void analyzeHumanTransition(const std::vector<FoundItem>& items) {
    std::printf("=== human_transition.xtbl (§20) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0, transitions = 0, blends = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"Name", "start_state_grid", "default_transition_from_stand",
                                             "Transitions", "Blends"};
    for (const Document& d : docs) {
        for (const Node* row = sr3xtbl::FindChild(d.table(), "Human_Transition"); row;
             row = sr3xtbl::NextSibling(d.table(), row, "Human_Transition")) {
            ++rows;
            HumanTransition h = ParseHumanTransition(row);
            tally(pm, "Name", h.name.has_value());
            tally(pm, "default_transition_from_stand", h.defaultTransitionFromStand.present);
            transitions += static_cast<long long>(h.transitions.size());
            blends += static_cast<long long>(h.blends.size());
            scanUnrecognised(u, row, known);
        }
    }
    std::printf("    Human_Transition rows: %lld, Transitions: %lld, Blends: %lld\n", rows, transitions, blends);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back(
        {"human_transition.xtbl", rows, [](const Document& d) { return d.table(); }, "Human_Transition"});
}

// §21 PEDF_Life.xtbl / Life_default.xtbl
void analyzeLifeTable(const char* label, const std::vector<FoundItem>& items) {
    std::printf("=== %s (§21) ===\n", label);
    printFilesFound(items);
    static std::vector<Document> docsA, docsB;
    std::vector<Document>& docs = (std::string(label).find("PEDF") != std::string::npos) ? docsA : docsB;
    parseAll(items, docs);
    long long sets = 0, groups = 0, states = 0, actions = 0;
    for (const Document& d : docs) {
        LifeTable t = ParseLifeTable(d);
        sets += static_cast<long long>(t.skeletonSets.size());
        for (const auto& s : t.skeletonSets) {
            groups += static_cast<long long>(s.groups.size());
            for (const auto& g : s.groups) {
                states += static_cast<long long>(g.states.size());
                actions += static_cast<long long>(g.actions.size());
            }
        }
    }
    std::printf("    Skeleton_Set: %lld, Group: %lld, State: %lld, Action: %lld\n", sets, groups, states, actions);
    g_runs.push_back({label, sets, [](const Document& d) { return d.table(); }, "Skeleton_Set"});
}

// §22 node_graph_files.xtbl (no <Table> wrapper)
void analyzeNodeGraphFiles(const std::vector<FoundItem>& items) {
    std::printf("=== node_graph_files.xtbl (§22, no <Table> wrapper) ===\n");
    printFilesFound(items);
    static std::vector<Document> docs;
    parseAll(items, docs);
    long long rows = 0;
    PresenceMap pm;
    UnrecognisedScan u;
    const std::vector<std::string> known = {"name"};
    for (const Document& d : docs) {
        NodeGraphFilesFile f = ParseNodeGraphFilesFile(d);
        rows += static_cast<long long>(f.files.size());
        for (const auto& e : f.files) tally(pm, "name", e.name.has_value());
        const Node* wrapper = sr3xtbl::FindChild(d.root(), "node_graph_files");
        for (const Node* row = sr3xtbl::FindChild(wrapper, "node_graph_file"); row;
             row = sr3xtbl::NextSibling(wrapper, row, "node_graph_file"))
            scanUnrecognised(u, row, known);
    }
    std::printf("    node_graph_file rows: %lld\n", rows);
    printPresence(pm);
    printUnrecognised(u);
    g_runs.push_back({"node_graph_files.xtbl", rows,
                       [](const Document& d) { return sr3xtbl::FindChild(d.root(), "node_graph_files"); },
                       "node_graph_file"});
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        std::printf("usage: validate_tables_trafficai_population <archive.vpp_pc> [...]\n");
        return 2;
    }

    for (const std::string& a : archives) {
        Bytes b = readFile(a);
        if (b.empty()) { std::printf("cannot read %s\n", a.c_str()); continue; }
        try {
            walk(vpp::ByteView(b.data(), b.size()), baseName(a));
        } catch (const std::exception& e) {
            std::printf("open failed %s: %s\n", a.c_str(), e.what());
        }
    }

    std::printf("=== G1 locate ===\n");
    std::printf("archives given: %zu, containers walked: %lld, directory entries seen: %lld\n", archives.size(),
                g_containers, g_entriesSeen);
    std::printf("traffic_types.xtbl: NOT LOADED BY THE EXE (spec §3 - no loader literal exists); skipped by "
                "design, not searched for\n\n");

    auto found = [](const char* name) -> const std::vector<FoundItem>& {
        static std::vector<FoundItem> empty;
        auto it = g_found.find(lower(name));
        return it == g_found.end() ? empty : it->second;
    };

    // Run every table's analysis (or report NOT FOUND plainly).
    struct Slot { const char* name; std::function<void()> run; };
    std::vector<Slot> slots = {
        {"traffic_lanes.xtbl", [&] { analyzeTrafficLanes(found("traffic_lanes.xtbl")); }},
        {"ambient_traffic_events.xtbl", [&] { analyzeAmbientTrafficEvents(found("ambient_traffic_events.xtbl")); }},
        {"roadblock_layouts.xtbl", [&] { analyzeRoadblockLayouts(found("roadblock_layouts.xtbl")); }},
        {"roadblock_notoriety.xtbl", [&] { analyzeRoadblockNotoriety(found("roadblock_notoriety.xtbl")); }},
        {"roadblock_stag_lockdown.xtbl",
         [&] { analyzeRoadblockStagLockdown(found("roadblock_stag_lockdown.xtbl")); }},
        {"panic_reactions.xtbl", [&] { analyzePanicReactions(found("panic_reactions.xtbl")); }},
        {"ai_goals.xtbl", [&] { analyzeAiGoals(found("ai_goals.xtbl")); }},
        {"ai_behavior.xtbl", [&] { analyzeAiBehavior(found("ai_behavior.xtbl")); }},
        {"ai_personalities.xtbl", [&] { analyzeAiPersonalities(found("ai_personalities.xtbl")); }},
        {"generic_characters.xtbl", [&] { analyzeGenericCharacters(found("generic_characters.xtbl")); }},
        {"generic_vehicles.xtbl", [&] { analyzeGenericVehicles(found("generic_vehicles.xtbl")); }},
        {"action_node_groups.xtbl", [&] { analyzeActionNodeGroups(found("action_node_groups.xtbl")); }},
        {"action_node_notoriety.xtbl", [&] { analyzeActionNodeNotoriety(found("action_node_notoriety.xtbl")); }},
        {"action_node_npcs.xtbl", [&] { analyzeActionNodeNpcs(found("action_node_npcs.xtbl")); }},
        {"action_nodes.xtbl", [&] { analyzeActionNodes(found("action_nodes.xtbl")); }},
        {"distant_peds.xtbl", [&] { analyzeDistantPeds(found("distant_peds.xtbl")); }},
        {"distant_ped_colors.xtbl", [&] { analyzeDistantPedColors(found("distant_ped_colors.xtbl")); }},
        {"distant_ped_spawn_parameters.xtbl / distant_vehicle_spawn_parameters.xtbl",
         [&] {
             analyzeDistantSpawnParameters(found("distant_ped_spawn_parameters.xtbl"),
                                            found("distant_vehicle_spawn_parameters.xtbl"));
         }},
        {"distant_vehicle_traffic_types.xtbl",
         [&] { analyzeDistantVehicleTrafficTypes(found("distant_vehicle_traffic_types.xtbl")); }},
        {"distant_vehicle_colors.xtbl / distant_vehicles.xtbl",
         [&] {
             analyzeDistantVehicleColorsAndVehicles(found("distant_vehicle_colors.xtbl"),
                                                     found("distant_vehicles.xtbl"));
         }},
        {"homies.xtbl", [&] { analyzeHomies(found("homies.xtbl")); }},
        {"follower_heads.xtbl", [&] { analyzeFollowerHeads(found("follower_heads.xtbl")); }},
        {"driver_bailout.xtbl", [&] { analyzeDriverBailout(found("driver_bailout.xtbl")); }},
        {"vehicle_despawn.xtbl", [&] { analyzeVehicleDespawn(found("vehicle_despawn.xtbl")); }},
        {"Escort_constants.xtbl", [&] { analyzeEscortConstants(found("Escort_constants.xtbl")); }},
        {"human_transition.xtbl", [&] { analyzeHumanTransition(found("human_transition.xtbl")); }},
        {"PEDF_Life.xtbl", [&] { analyzeLifeTable("PEDF_Life.xtbl", found("PEDF_Life.xtbl")); }},
        {"Life_default.xtbl", [&] { analyzeLifeTable("Life_default.xtbl", found("Life_default.xtbl")); }},
        {"node_graph_files.xtbl", [&] { analyzeNodeGraphFiles(found("node_graph_files.xtbl")); }},
    };

    long long tablesFound = 0, tablesMissing = 0;
    for (const Slot& s : slots) {
        s.run();
        std::printf("\n");
    }

    std::printf("=== G2 found/missing summary (denominator: %zu canonical tables) ===\n", kNumTargets);
    for (const char* t : kTargets) {
        bool f = !found(t).empty();
        std::printf("  [%s] %s\n", f ? "FOUND" : "NOT FOUND", t);
        (f ? tablesFound : tablesMissing)++;
    }
    std::printf("tables found: %lld / %zu, missing: %lld / %zu\n\n", tablesFound, kNumTargets, tablesMissing,
                kNumTargets);

    std::printf("=== G3 parse summary ===\n");
    std::printf("files parsed OK: %lld, parse/decode failures: %lld\n\n", g_filesParsed, g_parseFailures);

    // ------------------------------------------------------------------ CONTROL
    std::printf("=== G4 control: counting must depend on the real element name ===\n");
    int bestIdx = -1;
    for (size_t i = 0; i < g_runs.size(); ++i) {
        if (g_runs[i].totalRows > 0 && (bestIdx == -1 || g_runs[i].totalRows > g_runs[bestIdx].totalRows))
            bestIdx = static_cast<int>(i);
    }
    bool controlOk = false;
    if (bestIdx == -1) {
        std::printf("  [SKIPPED] no table with rows > 0 was found in the given archives - control not "
                    "applicable\n");
    } else {
        const TableRun& run = g_runs[bestIdx];
        // Re-parse the found files for this table and recount with the real vs a
        // deliberately wrong row name.
        const std::vector<FoundItem>& items = found(run.canonicalName.c_str());
        std::vector<Document> docs;
        parseAll(items, docs);
        long long realCount = 0, wrongCount = 0;
        std::string wrongName = run.rowName + "_DOES_NOT_EXIST_CONTROL";
        for (const Document& d : docs) {
            const Node* c = run.containerOf(d);
            realCount += static_cast<long long>(sr3xtbl::CountChildren(c, run.rowName));
            wrongCount += static_cast<long long>(sr3xtbl::CountChildren(c, wrongName));
        }
        controlOk = realCount > 0 && wrongCount == 0;
        std::printf("  table under test: %s (row element \"%s\")\n", run.canonicalName.c_str(),
                    run.rowName.c_str());
        std::printf("  [%s] real row name count = %lld (> 0 expected); wrong row name count = %lld (0 "
                    "expected)\n",
                    controlOk ? "PASS" : "FAIL", realCount, wrongCount);
    }

    std::printf("\n=== SUMMARY ===\n");
    std::printf("tables implemented and checked: %zu (of 31 loadable; traffic_types.xtbl has no loader, §3)\n",
                kNumTargets);
    std::printf("found in given archives: %lld / %zu\n", tablesFound, kNumTargets);
    std::printf("parse failures: %lld\n", g_parseFailures);
    std::printf("control: %s\n", bestIdx == -1 ? "SKIPPED (no data)" : (controlOk ? "PASS" : "FAIL"));

    return (bestIdx != -1 && !controlOk) ? 1 : 0;
}
