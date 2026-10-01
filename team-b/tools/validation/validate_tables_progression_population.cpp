// Population gates for sr3tables_progression over the real shipped archives.
//
// Usage: validate_tables_progression_population <archive.vpp_pc> [...]
//
// Locates each of the 26 progression/rules/world-state table filenames of
// spec-tables-progression.md (case-insensitive exact filename match) inside
// every given archive, walked recursively (raw AND compressed entries,
// nested containers - the same walk() pattern as validate_xtbl_population.cpp).
// For every table FOUND, every row is parsed with the typed reader
// (include/sr3tables_progression/tables.h) and this reports, per table:
//   - which archive(s) it was found in, and the row count
//   - for each "always write" field: how many real rows actually supply the
//     element vs how many fall back to the reader's unspecified-default path
//     (spec-tables-progression.md 1.3's caveat) - the check that matters,
//     since the spec derived "always write" purely from loader code, never
//     from a real row
//   - any child element real rows carry that the spec's schema (as
//     implemented in the typed struct) does not name
//   - for the handful of tables with a closed enum/name list in the spec
//     text: any real text value outside that list
//   - a few explicit type-shape checks (a leading '-' on a spec-unsigned
//     field; a '.' inside a spec-integer field)
// For tables NOT found, that is reported plainly - never silently skipped.
//
// Every count below carries its denominator. Two explicit CONTROLS bracket
// the file-locating mechanism (search for a name that cannot exist, and for
// one that must); see main().
//
// Real game data (read-only, per the project's HARD RULES): this harness
// never writes to the archive paths it is given.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3tables_progression/tables.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using sr3xtbl::ChildText;
using sr3xtbl::Document;
using sr3xtbl::FindChild;
using sr3xtbl::Node;
using sr3xtbl::ParseDocument;

int g_fail = 0;
#define GATE(ok, ...)                                       \
    do {                                                    \
        const bool ok_ = (ok);                              \
        std::printf("  [%s] ", ok_ ? "PASS" : "FAIL");       \
        std::printf(__VA_ARGS__);                           \
        std::printf("\n");                                  \
        if (!ok_) ++g_fail;                                 \
    } while (0)

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

// The 26 target filenames of spec-tables-progression.md S1.2, lower-cased
// for a case-insensitive exact match (spec: "element-name matching is
// case-insensitive throughout"; spec 1.2 notes Gameplay_nag_globals.xtbl's
// differing capitalisation specifically - a case-sensitive scan would miss it).
const char* const kTargetFiles[26] = {
    "stats.xtbl", "achievements.xtbl", "unlockables.xtbl", "patch_unlockables.xtbl",
    "respect_levels.xtbl", "notoriety.xtbl", "notoriety_levels.xtbl", "notoriety_spawn.xtbl",
    "difficulty_levels.xtbl", "cheats.xtbl", "collectibles.xtbl", "store_discounts.xtbl",
    "gameplay_constants.xtbl", "gameplay_nags.xtbl", "gameplay_nag_globals.xtbl",
    "mission_checkpoints.xtbl", "mission_help.xtbl", "spawn_info_categories.xtbl",
    "spawn_info_groups.xtbl", "spawn_info_ranks.xtbl", "drunk_levels.xtbl", "rank_reactions.xtbl",
    "default_global.xtbl", "tweak_table.xtbl", "metered_sprint.xtbl", "activity_types.xtbl",
};

struct Item {
    std::string archive;  // chain of containers, e.g. misc_tables.vpp_pc/foo.str2_pc
    std::string name;     // on-disk entry name (original casing)
    Bytes data;
    bool ok = false;      // decoded successfully
};

std::vector<Item> g_items;
long long g_containers = 0, g_entriesSeen = 0;

std::set<std::string> targetSet() {
    std::set<std::string> s;
    for (const char* n : kTargetFiles) s.insert(n);
    return s;
}

void walk(vpp::ByteView bytes, const std::string& path, const std::set<std::string>& targets) {
    vpp::Container c(bytes);
    ++g_containers;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        ++g_entriesSeen;
        const std::string ln = lower(e.name);
        if (!targets.count(ln)) continue;
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
    // Recurse into nested containers (skip our own target files - they are leaf xtbl content, never containers).
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        if (targets.count(lower(c.entries()[i].name))) continue;
        try {
            walk(c.rawEntryBytes(i), path + "/" + c.entries()[i].name, targets);
        } catch (const std::exception&) {
        }
    }
}

// ---------------------------------------------------------------------------
// generic report helpers
// ---------------------------------------------------------------------------

std::set<std::string> knownLower(std::initializer_list<const char*> names) {
    std::set<std::string> s;
    for (const char* n : names) s.insert(lower(n));
    return s;
}

// Reports child element names of `rows` not present in `known` (case-insensitive).
void reportUnknownChildren(const char* tableLabel, const std::vector<const Node*>& rows, const std::set<std::string>& known) {
    std::map<std::string, long long> unknownCounts;
    for (const Node* row : rows) {
        for (const Node* c : row->children()) {
            if (!known.count(lower(c->name()))) ++unknownCounts[c->name()];
        }
    }
    std::printf("    unrecognised top-level child elements vs this reader's known schema (%s), %zu row(s) scanned: %zu distinct name(s)\n",
                tableLabel, rows.size(), unknownCounts.size());
    for (auto& kv : unknownCounts) std::printf("      %-32s x%lld\n", kv.first.c_str(), kv.second);
}

void reportAlways(const char* field, long long present, long long total) {
    std::printf("    always-field %-30s real rows that actually supply it: %lld / %lld\n", field, present, total);
}

// Leading '-' on a field the spec calls unsigned (u32/u8/u16); denominator = rows where the field is present.
void checkUnsignedNoMinus(const char* label, const std::vector<const Node*>& rows, const char* field) {
    long long total = 0, minus = 0;
    for (const Node* row : rows) {
        const std::string* t = ChildText(row, field);
        if (!t) continue;
        ++total;
        if (!t->empty() && (*t)[0] == '-') ++minus;
    }
    std::printf("    type-shape: %s.%s (spec: unsigned) has a leading '-' in real text: %lld / %lld\n", label, field, minus, total);
}

// '.' inside a field the spec calls a plain integer; denominator = rows where the field is present.
void checkIntNoDot(const char* label, const std::vector<const Node*>& rows, const char* field) {
    long long total = 0, dot = 0;
    for (const Node* row : rows) {
        const std::string* t = ChildText(row, field);
        if (!t) continue;
        ++total;
        if (t->find('.') != std::string::npos) ++dot;
    }
    std::printf("    type-shape: %s.%s (spec: integer) contains '.' in real text: %lld / %lld\n", label, field, dot, total);
}

// Enum/name-list check: the field's own text against a fixed accepted list (case-insensitive).
void checkEnumText(const char* label, const std::vector<const Node*>& rows, const char* field,
                    const std::vector<std::string>& accepted) {
    long long total = 0, outside = 0;
    std::set<std::string> unknownValues;
    for (const Node* row : rows) {
        const std::string* t = ChildText(row, field);
        if (!t) continue;
        ++total;
        bool found = false;
        for (const std::string& a : accepted) {
            if (a.size() == t->size()) {
                bool eq = true;
                for (size_t i = 0; i < a.size() && eq; ++i)
                    eq = std::tolower(static_cast<unsigned char>(a[i])) == std::tolower(static_cast<unsigned char>((*t)[i]));
                if (eq) { found = true; break; }
            }
        }
        if (!found) { ++outside; unknownValues.insert(*t); }
    }
    std::printf("    enum: %s.%s text outside the spec's %zu-name list: %lld / %lld\n", label, field, accepted.size(), outside, total);
    for (const std::string& v : unknownValues) std::printf("      unrecognised value: \"%s\"\n", v.c_str());
}

// NOTE: takes the Document by reference and returns Node* pointers INTO it - the
// caller must keep `doc` alive for as long as the returned pointers are used
// (Document owns its nodes in a std::deque; a Document returned by value from a
// helper and then destroyed would leave these dangling - the bug this signature avoids).
std::vector<const Node*> rowsNamed(const Document& doc, const char* rowName) {
    std::vector<const Node*> out;
    const Node* table = doc.table();
    for (const Node* row = FindChild(table, rowName); row; row = sr3xtbl::NextSibling(table, row, rowName)) out.push_back(row);
    return out;
}

const Item* find1(const std::string& lowerName) {
    for (const Item& it : g_items)
        if (it.ok && lower(it.name) == lowerName) return &it;
    return nullptr;
}
std::vector<const Item*> findAll(const std::string& lowerName) {
    std::vector<const Item*> out;
    for (const Item& it : g_items)
        if (it.ok && lower(it.name) == lowerName) out.push_back(&it);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);

    const std::set<std::string> targets = targetSet();
    for (const std::string& a : archives) {
        Bytes b = readFile(a);
        if (b.empty()) { std::printf("cannot read %s\n", a.c_str()); continue; }
        try {
            walk(vpp::ByteView(b.data(), b.size()), baseName(a), targets);
        } catch (const std::exception& e) {
            std::printf("open failed %s: %s\n", a.c_str(), e.what());
        }
    }

    std::printf("=== survey ===\n");
    std::printf("archives given %zu, containers walked %lld, directory entries seen %lld, target-file hits %zu\n",
                archives.size(), g_containers, g_entriesSeen, g_items.size());
    long long decodedOk = 0;
    for (const Item& it : g_items) decodedOk += it.ok;
    GATE(archives.size() > 0 && g_containers > 0, "walk is not vacuous: %zu archives, %lld containers", archives.size(), g_containers);
    GATE(g_items.empty() || decodedOk == static_cast<long long>(g_items.size()), "every located target-file entry decoded: %lld / %zu", decodedOk, g_items.size());

    // CONTROLS on the file-locating mechanism itself: a name that cannot exist must not be
    // found; at least one of the 26 real spec names must be found (assuming any archive was given).
    {
        bool bogusFound = find1("this_file_cannot_possibly_exist_control.xtbl") != nullptr;
        GATE(!bogusFound, "CONTROL: a filename that cannot exist in the real data is not found");
        int foundCount = 0;
        for (const char* n : kTargetFiles) if (find1(lower(n))) ++foundCount;
        GATE(archives.empty() || foundCount > 0, "CONTROL: the search mechanism finds at least one of the 26 real names (%d/26 found)", foundCount);
    }

    std::printf("=== per-table census (26 target filenames) ===\n");
    int foundTables = 0;
    for (const char* n : kTargetFiles) {
        std::vector<const Item*> hits = findAll(lower(n));
        if (hits.empty()) {
            std::printf("  NOT FOUND: %s\n", n);
        } else {
            ++foundTables;
            std::printf("  found: %-28s in %zu location(s):", n, hits.size());
            for (auto* it : hits) std::printf(" [%s :: %s]", it->archive.c_str(), it->name.c_str());
            std::printf("\n");
        }
    }
    GATE(true, "26 target filenames: %d found, %d not found (denominator 26)", foundTables, 26 - foundTables);

    // ----------------------------------------------------------------- stats.xtbl
    if (const Item* it = find1("stats.xtbl")) {
        std::printf("=== stats.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Stat");
        std::vector<sr3tables_progression::Stat> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseStat(r));
        std::printf("  rows: %zu\n", rows.size());
        long long allowPresent = 0;
        for (auto& s : parsed) allowPresent += s.allowUpdateByServer.present;
        reportAlways("Allow_Update_By_Server", allowPresent, static_cast<long long>(parsed.size()));
        long long liveProp = 0, liveLb = 0;
        for (auto& s : parsed) { liveProp += s.livePropertyId.has_value(); liveLb += s.liveLeaderboardId.has_value(); }
        std::printf("    if-present LivePropertyID supplied: %lld / %zu; LiveLeaderboardID supplied: %lld / %zu\n",
                    liveProp, parsed.size(), liveLb, parsed.size());
        long long none = 0, percent = 0, complex_ = 0;
        for (auto& s : parsed) {
            none += s.valueKind == sr3tables_progression::StatValueKind::None;
            percent += s.valueKind == sr3tables_progression::StatValueKind::Percent;
            complex_ += s.valueKind == sr3tables_progression::StatValueKind::Complex;
        }
        std::printf("    Value_Type census: none(no child matched) %lld, percent %lld, complex %lld, of %zu\n", none, percent, complex_, parsed.size());
        reportUnknownChildren("Stat", rows,
            knownLower({"Name", "DisplayName", "Value_Type", "Allow_Update_By_Server", "LivePropertyID", "LiveLeaderboardID"}));
    } else {
        std::printf("stats.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- achievements.xtbl
    if (const Item* it = find1("achievements.xtbl")) {
        std::printf("=== achievements.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Achievement");
        std::vector<sr3tables_progression::Achievement> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseAchievement(r));
        std::printf("  rows: %zu\n", rows.size());
        long long hf = 0, ns = 0;
        for (auto& a : parsed) { hf += a.hudUpdateFrequency.present; ns += a.hudUpdateNumSuppress.present; }
        reportAlways("HudUpdateFrequency", hf, static_cast<long long>(parsed.size()));
        reportAlways("HudUpdateNumSuppress", ns, static_cast<long long>(parsed.size()));
        checkIntNoDot("Achievement", rows, "HudUpdateFrequency");
        long long pcOnly = 0;
        for (auto r : rows) if (FindChild(r, "PC_Only")) ++pcOnly;
        std::printf("    PC_Only present (spec: authoring-only, never read by the loader): %lld / %zu\n", pcOnly, rows.size());
        reportUnknownChildren("Achievement", rows,
            knownLower({"Name", "DisplayName", "Image", "Requirements", "HudUpdateFrequency", "HudUpdateNumSuppress",
                        "Avatar_award", "PC_Only", "Framework", "_Editor"}));
    } else {
        std::printf("achievements.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- unlockables.xtbl / patch_unlockables.xtbl
    for (const char* fname : {"unlockables.xtbl", "patch_unlockables.xtbl"}) {
        const Item* it = find1(lower(fname));
        if (!it) { std::printf("%s: NOT FOUND in the given archives - no population data.\n", fname); continue; }
        std::printf("=== %s ===\n", fname);
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Unlockable");
        std::vector<sr3tables_progression::Unlockable> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseUnlockable(r));
        std::printf("  rows: %zu\n", rows.size());
        long long typeMatched = 0;
        for (auto& u : parsed) typeMatched += u.typeIndex >= 0;
        std::printf("    rows whose <Type> matched one of the 61 spec names: %lld / %zu (unmatched -> dropped by the real loader, spec 4.1)\n",
                    typeMatched, parsed.size());
        std::map<std::string, long long> typeCensus;
        for (auto& u : parsed) if (!u.typeName.empty()) ++typeCensus[u.typeName];
        std::printf("    type census (%zu distinct types used):", typeCensus.size());
        for (auto& kv : typeCensus) std::printf(" %s=%lld", kv.first.c_str(), kv.second);
        std::printf("\n");
        long long price = 0;
        for (auto& u : parsed) price += u.price.present;
        reportAlways("Price", price, static_cast<long long>(parsed.size()));
        checkUnsignedNoMinus("Unlockable", rows, "Price");
        checkEnumText("Unlockable", rows, "Category",
                      {"Player Abilities", "Health", "Damage", "Weapons", "Vehicles", "Homies", "Discounts",
                       "Customization", "Strongholds", "Activities"});
        checkEnumText("Unlockable", rows, "Auto_Unlock", {"Silently", "Loudly", "Loudly (Helipad)", "Silently (Last)"});
        checkEnumText("Unlockable", rows, "Is_DLC", {"False", "True", "Sometimes"});
        reportUnknownChildren("Unlockable", rows,
            knownLower({"Name", "Type", "DisplayName", "Description", "Image_Source", "Detailed_Description_Text",
                        "Event_Text", "Category", "Price", "Priority", "Auto_Unlock", "Is_DLC", "Framework", "_Editor"}));
    }

    // ----------------------------------------------------------------- respect_levels.xtbl
    if (const Item* it = find1("respect_levels.xtbl")) {
        std::printf("=== respect_levels.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<sr3tables_progression::RespectLevel> levels = sr3tables_progression::ParseRespectLevelsTable(doc);
        std::printf("  levels: %zu (spec: at most 50 stored by the real loader)\n", levels.size());
        long long respectPresent = 0, rankPresent = 0;
        for (auto& l : levels) { respectPresent += l.respect.present; rankPresent += l.rank.has_value(); }
        reportAlways("Respect", respectPresent, static_cast<long long>(levels.size()));
        std::printf("    if-present Rank supplied: %lld / %zu\n", rankPresent, levels.size());
    } else {
        std::printf("respect_levels.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- notoriety.xtbl
    if (const Item* it = find1("notoriety.xtbl")) {
        std::printf("=== notoriety.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<sr3tables_progression::NotorietyEntry> entries = sr3tables_progression::ParseNotorietyTable(doc);
        long long matched = 0;
        for (auto& e : entries) matched += e.activityIndex >= 0;
        std::printf("  rows: %zu, matched one of the 30 static activity names: %lld / %zu\n", entries.size(), matched, entries.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Entry");
        checkEnumText("Entry", rows, "Name", {
            "armored truck assault", "burglary", "carjacking", "car assault", "civilian assault",
            "civilian kill", "civilian shooting", "civilian squirting", "drug use", "firearm discharge",
            "gang alert", "gang assault", "gang kill", "gang shooting", "generic gang",
            "generic police", "govt car destroy", "govt car theft", "govt heli theft", "helicopter destroy",
            "mover dislodge", "public nudity", "police alert", "police assault", "police kill",
            "police shooting", "robbery", "atm extortion", "vandalism", "enter owned store"});
    } else {
        std::printf("notoriety.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- notoriety_levels.xtbl
    if (const Item* it = find1("notoriety_levels.xtbl")) {
        std::printf("=== notoriety_levels.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        sr3tables_progression::NotorietyLevelsTable t = sr3tables_progression::ParseNotorietyLevelsTable(doc);
        std::printf("  setA present: %s (%zu levels); setB present: %s (%zu levels) [spec: exactly two sets expected]\n",
                    t.setA ? "yes" : "no", t.setA ? t.setA->levels.size() : 0u,
                    t.setB ? "yes" : "no", t.setB ? t.setB->levels.size() : 0u);
    } else {
        std::printf("notoriety_levels.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- notoriety_spawn.xtbl
    if (const Item* it = find1("notoriety_spawn.xtbl")) {
        std::printf("=== notoriety_spawn.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<sr3tables_progression::NotorietySpawnRow> rows = sr3tables_progression::ParseNotorietySpawnTable(doc);
        std::printf("  rows (children literally named 'Table', spec 6.3, confirmed on real data per S14.6): %zu (spec 6.3 lists 24 names; S14.6 reports 25 real rows)\n", rows.size());
        long long matched = 0;
        for (auto& r : rows) {
            bool found = false;
            for (const char* n : sr3tables_progression::kNotorietySpawnGroupNames)
                if (r.name == n) { found = true; break; }
            if (found) ++matched;
            else std::printf("      unmatched row Name: \"%s\"\n", r.name.c_str());
        }
        std::printf("    row Name matches one of the 24 static group names: %lld / %zu\n", matched, rows.size());

        // Records via spec S14.6's nested shape (one wrapper per kind whose
        // same-named children are the records; S6.3's flat tree is struck).
        // S14.6 states totals of 80 level_info / 254 group_info / 263
        // group_details across 25 groups, but the spec's own desk review marks
        // the 80 as OPEN (conflicts with "22/25 groups cover levels 2-5") - so
        // these are REPORTED, not gated.
        long long totLi = 0, totGi = 0, totGd = 0, rowsWithLi = 0;
        std::printf("    per-row records via the S14.6 nested reader (level_info / group_info / group_details):\n");
        for (auto& r : rows) {
            std::printf("      %-22s %zu / %zu / %zu", r.name.c_str(), r.levelInfos.size(), r.groupInfos.size(), r.groupDetails.size());
            if (!r.levelInfos.empty())
                std::printf("   levels %d..%d", r.levelInfos.front().level.value, r.levelInfos.back().level.value);
            std::printf("\n");
            totLi += static_cast<long long>(r.levelInfos.size());
            totGi += static_cast<long long>(r.groupInfos.size());
            totGd += static_cast<long long>(r.groupDetails.size());
            if (!r.levelInfos.empty()) ++rowsWithLi;
        }
        std::printf("    TOTAL records via S14.6 nesting: level_info %lld, group_info %lld, group_details %lld (spec S14.6 text: 80 / 254 / 263; the 80 is OPEN per the spec's desk review)\n",
                    totLi, totGi, totGd);
        GATE(rows.empty() || rowsWithLi == static_cast<long long>(rows.size()),
             "every notoriety_spawn row yields >= 1 level_info record via the S14.6 nested reader: %lld / %zu", rowsWithLi, rows.size());

        // Shape audit on the raw tree: for each kind, how many rows carry a
        // wrapper with >= 1 same-named child (S14.6 shape) vs. a wrapper with
        // NO same-named child (would be the struck S6.3 flat shape or empty).
        // Also: leaf elements inside inner records that the reader does not name.
        std::vector<const Node*> rowNodes = rowsNamed(doc, "Table");
        const char* kinds[3] = {"level_info", "group_info", "group_details"};
        const std::set<std::string> knownLeaves[3] = {
            {"level", "min_spawn_time", "max_spawn_time", "veh_min_spawn_time", "veh_max_spawn_time",
             "max_vehicle_occupants", "max_vehicles", "npc_cap", "specialist_cap", "brute_cap"},
            {"level", "chance", "tag_name", "vehicle_name", "variant_name"},
            {"tag_name", "seat_name", "npc_name", "melee_brute_seat", "weapons_brute_seat", "rollerbladers_seat", "outside_seat"},
        };
        for (int k = 0; k < 3; ++k) {
            long long withWrap = 0, nested = 0, notNested = 0, multiWrap = 0;
            std::vector<const Node*> inner;
            for (const Node* row : rowNodes) {
                const Node* w = FindChild(row, kinds[k]);
                if (!w) continue;
                ++withWrap;
                if (sr3xtbl::NextSibling(row, w, kinds[k])) ++multiWrap;
                const Node* c = FindChild(w, kinds[k]);
                if (c) ++nested; else ++notNested;
                for (; c; c = sr3xtbl::NextSibling(w, c, kinds[k])) inner.push_back(c);
            }
            std::printf("    shape: %s wrapper present in %lld / %zu rows; nested (S14.6) %lld, NOT nested %lld; rows with a 2nd same-named wrapper (unread) %lld\n",
                        kinds[k], withWrap, rowNodes.size(), nested, notNested, multiWrap);
            reportUnknownChildren(kinds[k], inner, knownLeaves[k]);
        }
    } else {
        std::printf("notoriety_spawn.xtbl: NOT FOUND in the given archives - no population data (spec 6.4: no DLC/save sample exists either).\n");
    }

    // ----------------------------------------------------------------- difficulty_levels.xtbl
    if (const Item* it = find1("difficulty_levels.xtbl")) {
        std::printf("=== difficulty_levels.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        sr3tables_progression::DifficultyLevelsTable t = sr3tables_progression::ParseDifficultyLevelsTable(doc);
        std::printf("  setA present: %s (%zu levels, %zu rank-occurrence elements); setB present: %s\n",
                    t.setA ? "yes" : "no", t.setA ? t.setA->levels.size() : 0u, t.setA ? t.setA->rankOccurrences.size() : 0u,
                    t.setB ? "yes" : "no");
        if (t.setA) {
            long long rank1 = 0, rank3 = 0;
            for (auto& r : t.setA->rankOccurrences) { rank1 += r.rank1.present; rank3 += r.rank3.present; }
            std::printf("    always-field Rank1 supplied: %lld / %zu; Rank3 supplied: %lld / %zu (spec 7.2: absent is NOT guaranteed to keep the 0.25 preset)\n",
                        rank1, t.setA->rankOccurrences.size(), rank3, t.setA->rankOccurrences.size());
        }
    } else {
        std::printf("difficulty_levels.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- cheats.xtbl
    if (const Item* it = find1("cheats.xtbl")) {
        std::printf("=== cheats.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Cheats");
        std::vector<sr3tables_progression::Cheat> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseCheat(r));
        std::printf("  rows: %zu (spec: capacity 200)\n", rows.size());
        long long dfac = 0, dlc = 0;
        for (auto& c : parsed) { dfac += c.dontFlagAsCheating.present; dlc += c.isDlc.present; }
        reportAlways("Dont_Flag_As_Cheating", dfac, static_cast<long long>(parsed.size()));
        reportAlways("Is_DLC", dlc, static_cast<long long>(parsed.size()));
        std::map<std::string, long long> typeCensus;
        long long typeMatchedTotal = 0;
        for (auto& c : parsed) if (!c.typeName.empty()) { ++typeCensus[c.typeName]; ++typeMatchedTotal; }
        std::printf("    <Type> census (spec: 6 types AI/Gameplay_Physics/Time/Vehicles/Weapon/Weather):");
        for (auto& kv : typeCensus) std::printf(" %s=%lld", kv.first.c_str(), kv.second);
        std::printf("  (%lld / %zu rows matched none of the 6)\n", static_cast<long long>(rows.size()) - typeMatchedTotal, rows.size());
        {
            std::vector<const Node*> aiActionNodes;
            std::vector<const Node*> gpActionNodes;
            for (auto r : rows) {
                const Node* type = FindChild(r, "Type");
                if (!type) continue;
                if (const Node* ai = FindChild(type, "AI")) aiActionNodes.push_back(ai);
                if (const Node* gp = FindChild(type, "Gameplay_Physics")) gpActionNodes.push_back(gp);
            }
            std::vector<std::string> aiNames(std::begin(sr3tables_progression::kCheatAiActions), std::end(sr3tables_progression::kCheatAiActions));
            std::vector<std::string> gpNames(std::begin(sr3tables_progression::kCheatGameplayPhysicsActions), std::end(sr3tables_progression::kCheatGameplayPhysicsActions));
            checkEnumText("Cheats/AI", aiActionNodes, "Action", aiNames);
            checkEnumText("Cheats/Gameplay_Physics", gpActionNodes, "Action", gpNames);
        }
        reportUnknownChildren("Cheats", rows,
            knownLower({"Name", "UnlockString", "DisplayName", "Cheat_Description", "Interface_Category",
                        "Dont_Flag_As_Cheating", "Is_DLC", "Type", "Framework"}));
    } else {
        std::printf("cheats.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- collectibles.xtbl
    if (const Item* it = find1("collectibles.xtbl")) {
        std::printf("=== collectibles.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "collectible");
        std::vector<sr3tables_progression::Collectible> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseCollectible(r));
        std::printf("  rows: %zu\n", rows.size());
        std::vector<std::string> names = {"Drug Package", "Money Pallet", "Sex Doll", "Photo Op"};
        checkEnumText("collectible", rows, "Name", names);
        long long over6 = 0;
        for (auto& c : parsed) over6 += c.thresholdRewards.size() >= 6;
        std::printf("    rows with 6+ Threshold_Reward entries (spec: 7th+ never read by the real loader): %lld / %zu\n", over6, parsed.size());
    } else {
        std::printf("collectibles.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- store_discounts.xtbl
    if (const Item* it = find1("store_discounts.xtbl")) {
        std::printf("=== store_discounts.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "StoreDiscounts");
        std::vector<sr3tables_progression::StoreDiscount> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseStoreDiscount(r));
        std::printf("  rows: %zu\n", rows.size());
        long long elemTotal = 0, amountPresent = 0, hoursPresent = 0;
        for (auto& d : parsed) for (auto& e : d.discounts) { ++elemTotal; amountPresent += e.amount.has_value(); hoursPresent += e.hours.has_value(); }
        std::printf("    DiscountElement total %lld; if-present Amount supplied %lld / %lld; Hours supplied %lld / %lld\n",
                    elemTotal, amountPresent, elemTotal, hoursPresent, elemTotal);
        reportUnknownChildren("StoreDiscounts", rows, knownLower({"Name", "Discounts"}));
    } else {
        std::printf("store_discounts.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- gameplay_constants.xtbl
    if (const Item* it = find1("gameplay_constants.xtbl")) {
        std::printf("=== gameplay_constants.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::optional<sr3tables_progression::GameplayConstants> gc = sr3tables_progression::ParseGameplayConstants(doc);
        std::printf("  Gameplay_Constants element present: %s\n", gc.has_value() ? "yes" : "no");
        if (gc) {
            std::printf("    panic_fall_velocity present: %s; FriendlyFirePercentage present: %s\n",
                        gc->panicFallVelocity.present ? "yes" : "no", gc->friendlyFirePercentage.present ? "yes" : "no");
        }
    } else {
        std::printf("gameplay_constants.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- gameplay_nags.xtbl / Gameplay_nag_globals.xtbl
    if (const Item* it = find1("gameplay_nags.xtbl")) {
        std::printf("=== gameplay_nags.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<sr3tables_progression::GameplayNagEntry> rows = sr3tables_progression::ParseGameplayNagsTable(doc);
        long long matched = 0;
        for (auto& r : rows) matched += r.nagIndex >= 0;
        std::printf("  rows: %zu, matched one of the 20 static nag names: %lld / %zu\n", rows.size(), matched, rows.size());
    } else {
        std::printf("gameplay_nags.xtbl: NOT FOUND in the given archives - no population data.\n");
    }
    if (const Item* it = find1("gameplay_nag_globals.xtbl")) {
        std::printf("=== gameplay_nag_globals.xtbl (spec: real on-disk capitalisation may differ - matched case-insensitively) ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::optional<sr3tables_progression::GameplayNagGlobals> g = sr3tables_progression::ParseGameplayNagGlobals(doc);
        std::printf("  Nag_Data row present: %s\n", g.has_value() ? "yes" : "no");
    } else {
        std::printf("gameplay_nag_globals.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- mission_checkpoints.xtbl
    if (const Item* it = find1("mission_checkpoints.xtbl")) {
        std::printf("=== mission_checkpoints.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "MissionCheckpoints");
        std::vector<sr3tables_progression::MissionCheckpoint> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseMissionCheckpoint(r));
        std::printf("  rows: %zu (spec: at most 166 kept)\n", rows.size());
        long long idx = 0, dbg = 0;
        for (auto& m : parsed) { idx += m.index.present; dbg += m.debug.present; }
        reportAlways("Index", idx, static_cast<long long>(parsed.size()));
        reportAlways("Debug", dbg, static_cast<long long>(parsed.size()));
    } else {
        std::printf("mission_checkpoints.xtbl: NOT FOUND in the given archives - no population data (spec 10.4: only loaded for sr3_city* worlds).\n");
    }

    // ----------------------------------------------------------------- mission_help.xtbl
    if (const Item* it = find1("mission_help.xtbl")) {
        std::printf("=== mission_help.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<sr3tables_progression::MissionHelpText> rows = sr3tables_progression::ParseMissionHelpTable(doc);
        std::printf("  flattened <Text> records: %zu\n", rows.size());
        long long english = 0;
        for (auto& r : rows) english += r.english.has_value();
        std::printf("    English (or DisplayText fallback) supplied: %lld / %zu\n", english, rows.size());
    } else {
        std::printf("mission_help.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- spawn_info_categories.xtbl
    if (const Item* it = find1("spawn_info_categories.xtbl")) {
        std::printf("=== spawn_info_categories.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Category");
        std::vector<sr3tables_progression::SpawnCategory> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseSpawnCategory(r));
        std::printf("  rows: %zu\n", rows.size());
        reportUnknownChildren("Category", rows,
            knownLower({"Name", "Groups", "Flags", "CarDay", "CarNight", "PedDay", "PedNight", "Law_Spawn_Group",
                        "LawCap", "LawDelay", "General_Ped_Slot_Day", "General_Ped_Slot_Night", "Special_Ped_Slot_Day",
                        "Special_Ped_Slot_Night", "Special_Vehicle_Slot_Day", "Special_Vehicle_Slot_Night"}));
    } else {
        std::printf("spawn_info_categories.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- spawn_info_groups.xtbl
    if (const Item* it = find1("spawn_info_groups.xtbl")) {
        std::printf("=== spawn_info_groups.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Group");
        std::vector<sr3tables_progression::SpawnGroup> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseSpawnGroup(r));
        std::printf("  rows: %zu\n", rows.size());
        long long vehOnly = 0;
        for (auto& g : parsed) vehOnly += g.vehicleOnly;
        std::printf("    vehicle_only rows (spec: dropped by the real loader if they have no Vehicles): %lld / %zu\n", vehOnly, parsed.size());
        checkEnumText("Group", rows, "Spline_Type", {"Highway Only", "Boat", "Offroad", "Indoor", "Baggage", "Taxi"});
        reportUnknownChildren("Group", rows,
            knownLower({"Name", "GeneralFlags", "Team", "Spline_Type", "Characters", "spawn_drunk_pct_day",
                        "spawn_drunk_pct_night", "Vehicles"}));
    } else {
        std::printf("spawn_info_groups.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- spawn_info_ranks.xtbl
    if (const Item* it = find1("spawn_info_ranks.xtbl")) {
        std::printf("=== spawn_info_ranks.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Rank");
        std::vector<sr3tables_progression::SpawnRank> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseSpawnRank(r));
        std::printf("  rows: %zu (spec: room for 55)\n", rows.size());
        checkEnumText("Rank", rows, "type",
                      {"Civilian", "Gang", "Homie", "Law enforcement", "Stag", "National Guard", "Other", "Specialist"});
        checkUnsignedNoMinus("Rank", rows, "Hit_Points");
        checkUnsignedNoMinus("Rank", rows, "rank_numeric");
    } else {
        std::printf("spawn_info_ranks.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- drunk_levels.xtbl
    if (const Item* it = find1("drunk_levels.xtbl")) {
        std::printf("=== drunk_levels.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Drunk_Levels");
        std::vector<sr3tables_progression::DrunkLevelsEntry> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseDrunkLevelsEntry(r));
        std::printf("  rows: %zu (spec: 3 effect slots expected)\n", rows.size());
        checkEnumText("Drunk_Levels", rows, "Drug_Type", {"drunk", "weed", "escort tiger"});
        long long levelsOver5 = 0;
        for (auto& e : parsed) levelsOver5 += e.levels.size() >= 5;
        std::printf("    rows with 5+ Level entries (spec: at most 5 kept): %lld / %zu\n", levelsOver5, parsed.size());
    } else {
        std::printf("drunk_levels.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- rank_reactions.xtbl
    if (const Item* it = find1("rank_reactions.xtbl")) {
        std::printf("=== rank_reactions.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "NPCGroup");
        std::printf("  rows: %zu (spec: 5 group names expected)\n", rows.size());
        checkEnumText("NPCGroup", rows, "Name",
                      {"Civilian", "Civilian Saints Hated", "Gang, Enemy", "Gang, Friendly", "Police"});
    } else {
        std::printf("rank_reactions.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- default_global.xtbl
    if (const Item* it = find1("default_global.xtbl")) {
        std::printf("=== default_global.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        sr3tables_progression::DefaultGlobal g = sr3tables_progression::ParseDefaultGlobal(doc);
        std::printf("  skyboxMeshFilename=\"%s\" cloudMeshFilename=\"%s\" orbitals=%zu (spec: at most 15)\n",
                    g.skyboxMeshFilename.c_str(), g.cloudMeshFilename.c_str(), g.orbitalMapNames.size());
    } else {
        std::printf("default_global.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- tweak_table.xtbl
    if (const Item* it = find1("tweak_table.xtbl")) {
        std::printf("=== tweak_table.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Tweak_Table_Entry");
        std::vector<sr3tables_progression::TweakTableEntry> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseTweakTableEntry(r));
        std::printf("  rows: %zu\n", rows.size());
        long long valuePresent = 0;
        for (auto& e : parsed) valuePresent += e.value.present;
        reportAlways("Value", valuePresent, static_cast<long long>(parsed.size()));
        reportUnknownChildren("Tweak_Table_Entry", rows, knownLower({"Name", "Value", "Description", "Framework"}));
    } else {
        std::printf("tweak_table.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- metered_sprint.xtbl
    if (const Item* it = find1("metered_sprint.xtbl")) {
        std::printf("=== metered_sprint.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "SprintEntry");
        std::printf("  rows: %zu\n", rows.size());
        checkUnsignedNoMinus("SprintEntry", rows, "UseTime");
    } else {
        std::printf("metered_sprint.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    // ----------------------------------------------------------------- activity_types.xtbl
    if (const Item* it = find1("activity_types.xtbl")) {
        std::printf("=== activity_types.xtbl ===\n");
        Document doc = ParseDocument(it->data.data(), it->data.size());
        std::vector<const Node*> rows = rowsNamed(doc, "Activity");
        std::vector<sr3tables_progression::Activity> parsed;
        for (auto r : rows) parsed.push_back(sr3tables_progression::ParseActivity(r));
        std::printf("  rows: %zu\n", rows.size());
        long long disableFlagsPresent = 0;
        for (auto& a : parsed) disableFlagsPresent += a.disableFlags.has_value();
        std::printf("    rows with a Disable_flags element: %lld / %zu\n", disableFlagsPresent, parsed.size());
        reportUnknownChildren("Activity", rows,
            knownLower({"Name", "Framework", "Unlockables", "Completion_Image", "Activity_type_flags",
                        "Disable_flags", "Soundbank_Name", "Type", "Additional_Resources", "_Editor"}));
        for (auto r : rows) {
            for (const Node* c : r->children()) {
                if (sr3xtbl::NameEquals(c->name(), "Unlockable")) { // singular, direct child (spec: only inside <Unlockables>)
                    const std::string* nm = ChildText(r, "Name");
                    std::printf("      row with a direct (non-wrapped) <Unlockable> child: Activity Name=\"%s\"\n",
                                nm ? nm->c_str() : "?");
                }
            }
        }
    } else {
        std::printf("activity_types.xtbl: NOT FOUND in the given archives - no population data.\n");
    }

    std::printf("\n%s (%d gate failure%s)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
