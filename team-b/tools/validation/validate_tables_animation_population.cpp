// Population gates for sr3tables_animation (include/sr3tables_animation/,
// src/tables_animation.cpp) over the REAL shipped archives.
//
// spec-tables-animation.md's own §17 only checked 4 archives
// (misc_tables.vpp_pc, da_tables.vpp_pc, cutscene_tables.vpp_pc,
// patch_compressed.vpp_pc) and found all 16 tables in misc_tables.vpp_pc
// only. This harness does not assume that: it walks every archive passed on
// argv (pass every *.vpp_pc under packfiles/pc/cache/, per this project's
// HARD RULES - no cache path is hardcoded here) and reports EVERY location
// a same-named table is found at, not just the first - a past bug in this
// project undercounted tables that exist in more than one archive (e.g. a
// patch archive superseding a base one), so G1 below is deliberately
// exhaustive per table, matching validate_tables_environment_population.cpp's
// own G1 approach.
//
// Usage: validate_tables_animation_population <archive.vpp_pc> [...]
//
// Gates:
//   G1  locate: for each of the 16 filenames this group's spec covers
//       (14 implemented + 2 confirmed-dead, spec 15), every archive/container
//       location it is found at - the headline finding opportunity, and
//       specifically the check for "found in more than one top-level
//       archive" (the exact undercounting failure mode this harness exists
//       to catch).
//   G2  for every IMPLEMENTED table found: row count via the typed reader;
//       per known top-level child, how many real rows have it present vs the
//       total; any child element name real rows carry that this reader's
//       schema whitelist does not know about; CONTROL - anim_states.xtbl's
//       schema (Name only) applied to anim_actions.xtbl rows must show
//       nothing unusual (both are name-only, spec 5 - a CONFIRMING control,
//       not a violating one, since these two tables are confirmed
//       structurally identical by the spec itself).
//   G3  enum/flag text values: every real Flag text checked against this
//       reader's copy of the spec's name table (anim_files.xtbl's 30-name
//       Flags, anim_ik_situation.xtbl's 3-name Flags, anim_flinches.xtbl's
//       3-name Flags AND its Hit_Location/Hit_Direction/Type value tables,
//       anim_synced.xtbl's 10-phrase Flags, anim_files.xtbl's IK/Location
//       4-name enum) - anything unmatched is reported (spec 10.4's own
//       "Male" dead-flag finding on anim_flinches.xtbl is exactly the kind
//       of thing this gate is built to surface again on whatever real
//       archives are passed here).
//   G4  declared-integer/capacity sanity: real Frame/Blend/Variant text
//       checked for a decimal point (float where an integer was expected);
//       every table-level cap the spec states (anim_flinches.xtbl 149,
//       anim_triggers.xtbl 110 + 31-char Name bound,
//       creation_lipsync_animations.xtbl 7 Entry / row Trigger-presence,
//       lens_flares-style caps) reported against the real row count.
//   G5  parser smoke check: every found, decoded copy of a target table must
//       parse without FormatError.
//   SPECIAL: anim_transitions.xtbl's spec 6.3 finding (shipped <Table> is
//       empty; real rows live under <TableTemplates>) is re-checked
//       explicitly against whatever real archives are passed here, since
//       spec 6.3 itself says this was not checked beyond the 4 archives it
//       searched. anim_set_properties.xtbl / anim_prop_sets.xtbl (spec 15,
//       no loader - not implemented) still get a raw, untyped row count for
//       completeness, since the task is to report every one of the 16
//       tables' population, not just the 14 with a reader.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3tables_animation/tables.h"
#include "vpp/container.h"

using namespace sr3tables_animation;

namespace {

using Bytes = std::vector<uint8_t>;
using sr3xtbl::Node;

int g_fail = 0;
#define GATE(ok, ...)                                 \
    do {                                                \
        const bool ok_ = (ok);                          \
        std::printf("  [%s] ", ok_ ? "PASS" : "FAIL");  \
        std::printf(__VA_ARGS__);                        \
        std::printf("\n");                                \
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
// validate_tables_environment_population.cpp's G1: every entry ending in
// "xtbl", raw or compressed, through every nested container).
// ---------------------------------------------------------------------------
struct Item {
    std::string archiveChain;      // "topLevel.vpp_pc/nested.str2_pc/..."
    std::string topLevelArchive;   // just the top-level .vpp_pc file name
    std::string name;              // the entry's own file name
    bool compressed = false;
    int status = 0;                // 0 = decoded / readable
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

// A found real copy of a target table.
struct Found {
    const Item* item;
};

// Every item whose own filename matches `canonical` case-insensitively -
// EVERY one, across every archive and every nested container, not just the
// first (this is the whole point of G1, see the file banner).
std::vector<Found> findMatches(const std::string& canonical) {
    std::vector<Found> out;
    const std::string lc = lower(canonical);
    for (const Item& it : g_items)
        if (lower(it.name) == lc) out.push_back({&it});
    return out;
}

// ---------------------------------------------------------------------------
// Generic per-row child-name census, same shape as
// validate_tables_environment_population.cpp's scanKnownChildren.
// ---------------------------------------------------------------------------
struct ChildScanResult {
    long long rows = 0;
    long long totalChildInstances = 0;
    std::map<std::string, long long> presentCount;
    std::map<std::string, long long> unrecognised;
};

ChildScanResult scanKnownChildren(const std::vector<const Node*>& rows, const std::vector<std::string>& known) {
    ChildScanResult r;
    r.rows = static_cast<long long>(rows.size());
    for (const Node* row : rows) {
        std::set<std::string> matchedHere;
        for (const Node* child : row->children()) {
            std::string matched;
            for (const std::string& k : known) {
                if (sr3xtbl::NameEquals(child->name(), k)) { matched = lower(k); break; }
            }
            ++r.totalChildInstances;
            if (!matched.empty()) matchedHere.insert(matched);
            else ++r.unrecognised[child->name()];
        }
        for (const std::string& k : matchedHere) ++r.presentCount[k];
    }
    return r;
}

void printCensus(const ChildScanResult& cs, const std::vector<std::string>& known) {
    std::printf("  G2 element census over %lld rows (%lld child-element instances scanned):\n", cs.rows,
                cs.totalChildInstances);
    for (const std::string& k : known) {
        const std::string kl = lower(k);
        const long long present = cs.presentCount.count(kl) ? cs.presentCount.at(kl) : 0;
        std::printf("    %-24s present in %lld / %lld rows\n", k.c_str(), present, cs.rows);
    }
    if (cs.unrecognised.empty()) {
        std::printf("  G2 unrecognised elements: 0 / %lld instances\n", cs.totalChildInstances);
    } else {
        std::printf("  G2 UNRECOGNISED elements (not in this reader's schema whitelist):\n");
        for (auto& kv : cs.unrecognised) std::printf("    %-24s x%lld\n", kv.first.c_str(), kv.second);
    }
}

// Flag-text census: every "Flag" child of each row's `containerName`
// container, checked against `validNames` (case-insensitive, exact text
// match - exactly sr3xtbl::FlagMask's own matching rule).
void scanFlags(const std::vector<const Node*>& rows, const char* containerName,
               const std::vector<std::string_view>& validNames, const char* label) {
    long long occurrences = 0;
    std::map<std::string, long long> unmatched;
    for (const Node* row : rows) {
        const Node* container = sr3xtbl::FindChild(row, containerName);
        for (const Node* flag : sr3xtbl::Children(container, "Flag")) {
            if (!flag->text()) continue;
            ++occurrences;
            bool ok = false;
            for (auto v : validNames)
                if (sr3xtbl::NameEquals(*flag->text(), v)) { ok = true; break; }
            if (!ok) ++unmatched[*flag->text()];
        }
    }
    std::printf("  G3 flag field '%s/Flag' (%s): %lld occurrences, %zu unmatched value(s)\n", containerName, label,
                occurrences, unmatched.size());
    for (auto& kv : unmatched) std::printf("      unmatched text %-32s x%lld\n", kv.first.c_str(), kv.second);
}

// Enum-text census over a direct child field (sequential-index tables).
void scanEnumField(const std::vector<const Node*>& rows, const char* fieldName,
                    const std::vector<std::string_view>& validNames, const char* label) {
    long long occurrences = 0;
    std::map<std::string, long long> unmatched;
    for (const Node* row : rows) {
        const std::string* t = sr3xtbl::ChildText(row, fieldName);
        if (!t || t->empty()) continue;
        ++occurrences;
        bool ok = false;
        for (auto v : validNames)
            if (sr3xtbl::NameEquals(*t, v)) { ok = true; break; }
        if (!ok) ++unmatched[*t];
    }
    std::printf("  G3 enum field '%s' (%s): %lld occurrences, %zu unmatched value(s)\n", fieldName, label,
                occurrences, unmatched.size());
    for (auto& kv : unmatched) std::printf("      unmatched text %-32s x%lld\n", kv.first.c_str(), kv.second);
}

std::vector<const Node*> descend(const sr3xtbl::Document& doc, const std::vector<std::string>& path,
                                  const std::string& rowElement) {
    const Node* target = doc.table();
    for (const std::string& seg : path) target = sr3xtbl::FindChild(target, seg);
    return sr3xtbl::Children(target, rowElement);
}

// ---------------------------------------------------------------------------
// One "located and parsed" table, kept alive with its owning documents so
// Node* stay valid across the whole run.
// ---------------------------------------------------------------------------
struct LocatedTable {
    std::string canonicalFile;
    std::vector<Found> matches;
    std::vector<const Node*> rows;   // aggregated across every found, decoded copy
    long long parseFailures = 0;
};

std::vector<sr3xtbl::Document> g_docs;  // keep-alive for every Node* handed out below

LocatedTable locate(const std::string& canonical, const std::vector<std::string>& descendPath,
                     const std::string& rowElement) {
    LocatedTable lt;
    lt.canonicalFile = canonical;
    lt.matches = findMatches(canonical);
    for (const Found& f : lt.matches) {
        if (f.item->status != 0) continue;
        try {
            g_docs.push_back(sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size()));
        } catch (const sr3xtbl::FormatError&) {
            ++lt.parseFailures;
            continue;
        }
        const sr3xtbl::Document& doc = g_docs.back();
        std::vector<const Node*> rows = descend(doc, descendPath, rowElement);
        lt.rows.insert(lt.rows.end(), rows.begin(), rows.end());
    }
    return lt;
}

void printLocationsRaw(const std::vector<Found>& matches) {
    if (matches.empty()) {
        std::printf("  NOT FOUND in any given archive.\n");
        return;
    }
    std::set<std::string> topLevelArchives;
    for (const Found& f : matches) {
        std::printf("  found: %s (in %s)  status=%d\n", f.item->archiveChain.c_str(),
                    f.item->topLevelArchive.c_str(), f.item->status);
        topLevelArchives.insert(lower(f.item->topLevelArchive));
    }
    std::printf("  location summary: %zu copy/copies across %zu distinct top-level archive(s)%s\n",
                matches.size(), topLevelArchives.size(),
                topLevelArchives.size() > 1 ? "  <-- MULTIPLE TOP-LEVEL ARCHIVES (the exact scenario"
                                               " this gate exists to catch - a base archive plus a"
                                               " superseding patch/DLC archive, or similar)"
                                             : "");
}

void printLocations(const LocatedTable& lt) { printLocationsRaw(lt.matches); }

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        std::printf("usage: validate_tables_animation_population <archive.vpp_pc> [...]\n");
        return 2;
    }

    for (const std::string& a : archives) {
        Bytes b = readFile(a);
        if (b.empty()) { std::printf("cannot read %s\n", a.c_str()); continue; }
        const std::string top = baseName(a);
        try {
            walk(vpp::ByteView(b.data(), b.size()), top, top);
        } catch (const std::exception& e) {
            std::printf("open failed %s: %s\n", a.c_str(), e.what());
        }
    }
    std::printf("=== G1 locate ===\n");
    std::printf("archives given: %zu, containers walked: %lld, xtbl-family entries seen: %zu\n", archives.size(),
                g_containers, g_items.size());
    GATE(g_containers > 0, "CONTROL: at least one container was actually opened (not a silent no-op run)");

    long long parseAttempts = 0, parseFailures = 0, tablesFound = 0, tablesNotFound = 0;

    // ---- 14 implemented tables -------------------------------------------
    struct Spec {
        std::string file;
        std::vector<std::string> descendPath;
        std::string rowElement;
        std::vector<std::string> knownChildren;
    };
    const std::vector<Spec> specs = {
        {"anim_set_filenames.xtbl", {}, "Anim_Set_Filename", {"Name", "Path", "Default_model", "Default_rig"}},
        {"anim_files.xtbl", {"Files", "Anim_files"}, "Anim_file",
         {"Animation", "Triggers", "IKs", "Sounds", "Voice_Lines", "misc", "Flags"}},
        {"anim_groups.xtbl", {}, "Anim_Group", {"Name", "Parent"}},
        {"anim_states.xtbl", {}, "State", {"Name"}},
        {"anim_actions.xtbl", {}, "Action", {"Name", "_Editor"}},
        {"anim_transitions.xtbl", {}, "Anim_Transition", {"From_State", "To_State", "Action"}},
        {"anim_blend_trees.xtbl", {}, "Blend_trees",
         {"Name", "Control_input_low", "Control_input_high", "Ramp_speed", "States", "Actions"}},
        {"anim_ik_situation.xtbl", {}, "IK_Situation", {"Name", "Prop_Name", "Morphed_Surface_Max_Adjustment", "Flags"}},
        {"anim_triggers.xtbl", {}, "Trigger", {"Name"}},
        {"anim_flinches.xtbl", {}, "Flinch", {"Name", "Hit_Location", "Hit_Direction", "Type", "Flags"}},
        {"anim_synced.xtbl", {}, "SyncedMove", {"Name", "AttackerAnim", "VictimAnim", "VictimOffsets", "Flags"}},
        {"anim_correction_offsets.xtbl", {}, "Correction_Offset", {"Name", "Offset", "_Editor", "File"}},
        {"creation_lipsync_animations.xtbl", {}, "Entry", {"Name", "Chances"}},
        {"character_visemes.xtbl", {}, "Viseme", {"Name", "Targets"}},
    };

    std::map<std::string, LocatedTable> located;
    for (const Spec& s : specs) {
        std::printf("--- %s ---\n", s.file.c_str());
        LocatedTable lt = locate(s.file, s.descendPath, s.rowElement);
        printLocations(lt);
        parseFailures += lt.parseFailures;
        if (lt.matches.empty()) { ++tablesNotFound; continue; }
        ++tablesFound;
        parseAttempts += static_cast<long long>(lt.matches.size());
        std::printf("  rows parsed (all found copies combined): %zu\n", lt.rows.size());
        if (!lt.rows.empty()) {
            ChildScanResult cs = scanKnownChildren(lt.rows, s.knownChildren);
            printCensus(cs, s.knownChildren);
        }
        located.emplace(s.file, std::move(lt));
    }

    // ---- G2 CONTROL: anim_states.xtbl's name-only schema applied to
    // anim_actions.xtbl rows - both are confirmed name-only (spec 5), so
    // this is a CONFIRMING control: it should show _Editor as the only
    // "unrecognised" element (anim_states.xtbl's whitelist doesn't include
    // it), never Name itself.
    std::printf("=== G2 CONTROL: anim_states.xtbl's schema applied to anim_actions.xtbl rows ===\n");
    if (located.count("anim_actions.xtbl")) {
        ChildScanResult ctrl = scanKnownChildren(located.at("anim_actions.xtbl").rows, {"Name"});
        std::printf("  anim_actions.xtbl rows scanned against anim_states.xtbl's {Name}-only whitelist: "
                    "%lld unrecognised element instance(s) (_Editor expected)\n",
                    ctrl.totalChildInstances - (ctrl.presentCount.count("name") ? ctrl.presentCount.at("name") : 0));
        GATE(ctrl.presentCount.count("name") && ctrl.presentCount.at("name") == ctrl.rows,
             "CONTROL: Name itself is recognised in every row (the control is checking the RIGHT schema, not a "
             "vacuously different one)");
    } else {
        std::printf("  (skipped: no anim_actions.xtbl rows were found)\n");
    }

    // ---- G3 enum/flag checks ----------------------------------------------
    std::printf("=== G3 enum/flag value checks ===\n");
    if (located.count("anim_files.xtbl")) {
        const std::vector<const Node*>& rows = located.at("anim_files.xtbl").rows;
        scanFlags(rows, "Flags", {kAnimFileFlagNames.begin(), kAnimFileFlagNames.end()}, "anim_files.xtbl 30-name table");
        // IK/Location is nested per-item within each row's IKs container.
        long long ikOccurrences = 0;
        std::map<std::string, long long> ikUnmatched;
        for (const Node* row : rows) {
            const Node* iks = sr3xtbl::FindChild(row, "IKs");
            for (const Node* ik : sr3xtbl::Children(iks, "IK")) {
                const std::string* t = sr3xtbl::ChildText(ik, "Location");
                if (!t || t->empty()) continue;
                ++ikOccurrences;
                bool ok = false;
                for (auto v : kAnimFileIkLocationNames)
                    if (sr3xtbl::NameEquals(*t, v)) { ok = true; break; }
                if (!ok) ++ikUnmatched[*t];
            }
        }
        std::printf("  G3 enum field 'IKs/IK/Location' (anim_files.xtbl 4-name table): %lld occurrences, "
                    "%zu unmatched value(s)\n", ikOccurrences, ikUnmatched.size());
        for (auto& kv : ikUnmatched) std::printf("      unmatched text %-32s x%lld\n", kv.first.c_str(), kv.second);
    }
    if (located.count("anim_ik_situation.xtbl")) {
        scanFlags(located.at("anim_ik_situation.xtbl").rows, "Flags",
                  {kAnimIkSituationFlagNames.begin(), kAnimIkSituationFlagNames.end()}, "anim_ik_situation.xtbl 3-name table");
    }
    if (located.count("anim_flinches.xtbl")) {
        const std::vector<const Node*>& rows = located.at("anim_flinches.xtbl").rows;
        scanFlags(rows, "Flags", {kAnimFlinchFlagNames.begin(), kAnimFlinchFlagNames.end()}, "anim_flinches.xtbl 3-name table");
        scanEnumField(rows, "Hit_Direction", {kAnimFlinchHitDirectionNames.begin(), kAnimFlinchHitDirectionNames.end()},
                      "anim_flinches.xtbl sequential 4-name table");
        {
            long long occurrences = 0;
            std::map<std::string, long long> unmatched;
            for (const Node* row : rows) {
                const std::string* t = sr3xtbl::ChildText(row, "Hit_Location");
                if (!t || t->empty()) continue;
                ++occurrences;
                if (ResolveFlinchHitLocation(*t) == 0) ++unmatched[*t];
            }
            std::printf("  G3 enum field 'Hit_Location' (anim_flinches.xtbl non-sequential value table): "
                        "%lld occurrences, %zu unmatched (zero-default) value(s)\n", occurrences, unmatched.size());
            for (auto& kv : unmatched) std::printf("      unmatched text %-32s x%lld\n", kv.first.c_str(), kv.second);
        }
        {
            long long occurrences = 0;
            std::map<std::string, long long> unmatched;
            for (const Node* row : rows) {
                const std::string* t = sr3xtbl::ChildText(row, "Type");
                if (!t || t->empty()) continue;
                ++occurrences;
                bool ok = false;
                for (const AnimFlinchNamedValue& e : kAnimFlinchTypeTable)
                    if (sr3xtbl::NameEquals(*t, e.name)) { ok = true; break; }
                if (!ok) ++unmatched[*t];
            }
            std::printf("  G3 enum field 'Type' (anim_flinches.xtbl 7-name table): %lld occurrences, "
                        "%zu unmatched value(s)\n", occurrences, unmatched.size());
            for (auto& kv : unmatched) std::printf("      unmatched text %-32s x%lld\n", kv.first.c_str(), kv.second);
        }
    }
    if (located.count("anim_synced.xtbl")) {
        scanFlags(located.at("anim_synced.xtbl").rows, "Flags", {kAnimSyncedFlagNames.begin(), kAnimSyncedFlagNames.end()},
                  "anim_synced.xtbl 10-phrase table, CASE-INSENSITIVE reference count");
        // NEW 2026-10-02: spec 11.2 RE-DERIVED the real engine's compare to be
        // CASE-SENSITIVE for this one table (CORRECTED from this project's
        // former case-insensitive assumption, fixed in ParseAnimSyncedMove).
        // Measure the delta directly: any real Flag text that matches a
        // phrase case-insensitively but NOT case-sensitively would have
        // silently been counted before this fix and is silently dropped
        // (matches nothing) by the real engine and by this reader now.
        {
            long long caseSensitiveOccurrences = 0;
            std::map<std::string, long long> caseOnlyMismatch;
            for (const Node* row : located.at("anim_synced.xtbl").rows) {
                const Node* flags = sr3xtbl::FindChild(row, "Flags");
                for (const Node* flag : sr3xtbl::Children(flags, "Flag")) {
                    if (!flag->text()) continue;
                    bool exact = false, ciOnly = false;
                    for (auto v : kAnimSyncedFlagNames) {
                        if (*flag->text() == v) { exact = true; break; }
                        if (sr3xtbl::NameEquals(*flag->text(), v)) ciOnly = true;
                    }
                    if (exact) ++caseSensitiveOccurrences;
                    else if (ciOnly) ++caseOnlyMismatch[*flag->text()];
                }
            }
            std::printf("  G3 flag field 'Flags/Flag' (anim_synced.xtbl 10-phrase table, CASE-SENSITIVE - "
                        "matches the real engine, spec 11.2 RE-DERIVED 2026-10-02): %lld exact occurrences, "
                        "%zu case-only-mismatch value(s) (would match case-insensitively but NOT in the real "
                        "engine or this reader)\n", caseSensitiveOccurrences, caseOnlyMismatch.size());
            for (auto& kv : caseOnlyMismatch) std::printf("      case-only mismatch %-32s x%lld\n", kv.first.c_str(), kv.second);
        }
    }

    // ---- G4 capacity / integer-looking-like-float sanity -------------------
    std::printf("=== G4 capacity and type-mismatch checks ===\n");
    auto reportCap = [&](const char* file, long long cap) {
        if (!located.count(file)) return;
        long long n = static_cast<long long>(located.at(file).rows.size());
        std::printf("  %-34s %lld row(s) parsed vs spec cap %lld%s\n", file, n, cap,
                    n > cap ? "  <-- OVER THE SPEC'S STATED CAP (loader would silently drop the excess)" : "");
    };
    reportCap("anim_flinches.xtbl", 149);
    reportCap("anim_triggers.xtbl", 110);
    reportCap("creation_lipsync_animations.xtbl", 7);
    if (located.count("anim_triggers.xtbl")) {
        long long over31 = 0;
        for (const Node* row : located.at("anim_triggers.xtbl").rows) {
            const std::string* t = sr3xtbl::ChildText(row, "Name");
            if (t && t->size() > 31) ++over31;
        }
        std::printf("  anim_triggers.xtbl Name > 31 chars (loader's bounded-copy limit): %lld / %zu\n", over31,
                    located.at("anim_triggers.xtbl").rows.size());
    }
    if (located.count("anim_files.xtbl")) {
        long long voiceLinesWithChildren = 0, rowsWithVoiceLines = 0;
        for (const Node* row : located.at("anim_files.xtbl").rows) {
            const Node* vl = sr3xtbl::FindChild(row, "Voice_Lines");
            if (!vl) continue;
            ++rowsWithVoiceLines;
            if (!vl->children().empty()) ++voiceLinesWithChildren;
        }
        std::printf("  anim_files.xtbl Voice_Lines: present in %lld rows, %lld of those carry a child "
                    "(spec 3.2/3.5 says this should be 0 in the base game)\n", rowsWithVoiceLines,
                    voiceLinesWithChildren);

        // <Trigger> shape census (spec 3.1 direct text vs 3.2 Name child; OPEN,
        // NEEDS-DATA). Raw tree, independent of the reader, plus the reader's
        // own unread count.
        long long trig = 0, withName = 0, withText = 0, both = 0, neither = 0, otherChildren = 0;
        long long readerRead = 0, readerUnread = 0;
        std::map<std::string, long long> childNames;
        for (const Node* row : located.at("anim_files.xtbl").rows) {
            for (const Node* t : sr3xtbl::Children(sr3xtbl::FindChild(row, "Triggers"), "Trigger")) {
                ++trig;
                bool name = sr3xtbl::FindChild(t, "Name") != nullptr;
                const std::string* tx = t->text();
                bool text = tx && tx->find_first_not_of(" \t\r\n") != std::string::npos;
                for (const Node* c : t->children()) {
                    ++childNames[c->name()];
                    if (c->name() != "Name") ++otherChildren;
                }
                if (name && text) ++both;
                else if (name) ++withName;
                else if (text) ++withText;
                else ++neither;
            }
            sr3tables_animation::AnimFile f = sr3tables_animation::ParseAnimFile(row);
            readerRead += static_cast<long long>(f.triggerNames.size());
            readerUnread += static_cast<long long>(f.triggerElementsUnread);
        }
        std::printf("  anim_files.xtbl Trigger shape (spec 3.1 text vs 3.2 Name child, OPEN): %lld elements: "
                    "Name child only %lld, direct text only %lld, both %lld, neither %lld; other child "
                    "elements %lld\n", trig, withName, withText, both, neither, otherChildren);
        std::printf("    child element names:");
        for (const auto& kv : childNames) std::printf(" %s x%lld", kv.first.c_str(), kv.second);
        std::printf("\n    reader: %lld read (Name child), %lld counted unread\n", readerRead, readerUnread);
    }
    if (located.count("anim_ik_situation.xtbl")) {
        long long hardCoded = 0;
        for (const Node* row : located.at("anim_ik_situation.xtbl").rows) {
            const Node* flags = sr3xtbl::FindChild(row, "Flags");
            if (sr3xtbl::HasFlag(flags, "Hard Coded")) ++hardCoded;
        }
        std::printf("  anim_ik_situation.xtbl rows authored with 'Hard Coded' set: %lld (spec 8.2: these are "
                    "discarded at load - written then immediately overwritten)\n", hardCoded);
    }
    if (located.count("anim_blend_trees.xtbl")) {
        // NEW 2026-10-02: spec 7.4 itself flags that the original pass's census
        // did NOT count Actions/Action under Blend_trees nor the max
        // Control_point count per State - "both are needed" (a non-resolving
        // Action/Animation would hang the ORIGINAL loader, spec 7's WARNING;
        // more than 5 Control_points per State overflows the real 0x3C-byte
        // State record, spec 7.2/7.3). This is that first real-data
        // measurement (the "Team B census" the spec itself asks for).
        long long rowsWithActions = 0, totalActions = 0, actionsWithAnimation = 0;
        long long totalStates = 0, maxControlPointsPerState = 0;
        std::map<long long, long long> controlPointCountHistogram;
        for (const Node* row : located.at("anim_blend_trees.xtbl").rows) {
            const Node* actionsNode = sr3xtbl::FindChild(row, "Actions");
            std::vector<const Node*> actionRows = sr3xtbl::Children(actionsNode, "Action");
            if (!actionRows.empty()) ++rowsWithActions;
            totalActions += static_cast<long long>(actionRows.size());
            for (const Node* a : actionRows) {
                const std::string* fn = sr3xtbl::ChildText(sr3xtbl::FindChild(a, "Animation"), "Filename");
                if (fn && !fn->empty()) ++actionsWithAnimation;
            }
            const Node* statesNode = sr3xtbl::FindChild(row, "States");
            for (const Node* s : sr3xtbl::Children(statesNode, "State")) {
                ++totalStates;
                const Node* cps = sr3xtbl::FindChild(s, "Control_points");
                long long n = static_cast<long long>(sr3xtbl::Children(cps, "Control_point").size());
                ++controlPointCountHistogram[n];
                if (n > maxControlPointsPerState) maxControlPointsPerState = n;
            }
        }
        std::printf("  anim_blend_trees.xtbl Actions/Action census (spec 7.4, previously uncounted): "
                    "%lld / %zu rows carry at least one Action; %lld Action elements total, %lld of those "
                    "have a non-empty Animation/Filename\n", rowsWithActions,
                    located.at("anim_blend_trees.xtbl").rows.size(), totalActions, actionsWithAnimation);
        std::printf("  anim_blend_trees.xtbl Control_point-per-State census (spec 7.4, previously uncounted): "
                    "%lld States total, max %lld Control_points in a single State (real cap is 5, spec 7.2 "
                    "RE-DERIVED 2026-10-02)%s\n", totalStates, maxControlPointsPerState,
                    maxControlPointsPerState > 5 ? "  <-- OVER THE RE-DERIVED 5-PER-STATE CAP" : "");
        for (auto& kv : controlPointCountHistogram)
            std::printf("      %lld Control_point(s): %lld State(s)\n", kv.first, kv.second);
    }
    {
        long long floatLookingInts = 0;
        std::vector<std::string> examples;
        auto checkIntField = [&](const std::vector<const Node*>& rows, const char* container, const char* field) {
            for (const Node* row : rows) {
                const Node* c = container ? sr3xtbl::FindChild(row, container) : row;
                const std::string* t = sr3xtbl::ChildText(c, field);
                if (t && t->find('.') != std::string::npos) {
                    ++floatLookingInts;
                    if (examples.size() < 5) examples.push_back(*t);
                }
            }
        };
        if (located.count("anim_files.xtbl")) {
            for (const Node* row : located.at("anim_files.xtbl").rows) {
                const Node* iks = sr3xtbl::FindChild(row, "IKs");
                for (const Node* ik : sr3xtbl::Children(iks, "IK")) checkIntField({ik}, nullptr, "Frame");
                for (const Node* ik : sr3xtbl::Children(iks, "IK")) checkIntField({ik}, nullptr, "Blend");
                const Node* sounds = sr3xtbl::FindChild(row, "Sounds");
                for (const Node* s : sr3xtbl::Children(sounds, "Sound")) checkIntField({s}, nullptr, "Frame");
            }
        }
        if (located.count("creation_lipsync_animations.xtbl")) {
            for (const Node* row : located.at("creation_lipsync_animations.xtbl").rows) {
                const Node* chances = sr3xtbl::FindChild(row, "Chances");
                for (const Node* c : sr3xtbl::Children(chances, "Chance")) checkIntField({c}, nullptr, "Variant");
            }
        }
        std::printf("  declared-integer fields (Frame/Blend/Variant) with a '.' (float-looking): %lld\n",
                    floatLookingInts);
        for (auto& s : examples) std::printf("      example: %s\n", s.c_str());
    }

    // ---- SPECIAL: anim_transitions.xtbl Table-vs-TableTemplates -----------
    std::printf("=== SPECIAL: anim_transitions.xtbl <Table> vs <TableTemplates> (spec 6.3) ===\n");
    {
        std::vector<Found> matches = findMatches("anim_transitions.xtbl");
        long long tableRows = 0, templateRows = 0;
        for (const Found& f : matches) {
            if (f.item->status != 0) continue;
            try {
                g_docs.push_back(sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size()));
            } catch (const sr3xtbl::FormatError&) {
                continue;
            }
            const sr3xtbl::Document& doc = g_docs.back();
            tableRows += static_cast<long long>(sr3xtbl::Children(doc.table(), "Anim_Transition").size());
            const Node* templates = sr3xtbl::FindChild(doc.root(), "TableTemplates");
            templateRows += static_cast<long long>(sr3xtbl::Children(templates, "Anim_Transition").size());
        }
        std::printf("  <Table>/Anim_Transition rows (what the real loader reads): %lld\n", tableRows);
        std::printf("  <TableTemplates>/Anim_Transition rows (authored, never read by the real loader): %lld\n",
                    templateRows);
        if (!matches.empty())
            std::printf("  %s\n", tableRows == 0 && templateRows > 0
                                       ? "CONFIRMS spec 6.3's finding on these archives: the loader-visible "
                                         "<Table> is empty while authored rows sit under <TableTemplates>."
                                       : "DIFFERS from spec 6.3's base-game finding on these archives - worth "
                                         "a closer look (e.g. a patch archive may supply a populated <Table>).");
    }

    // ---- SPECIAL: the two confirmed-dead tables, informational only -------
    std::printf("=== SPECIAL: anim_set_properties.xtbl / anim_prop_sets.xtbl (spec 15, no loader - not "
                "implemented; reported for completeness only) ===\n");
    for (auto& pr : std::vector<std::pair<std::string, std::string>>{
             {"anim_set_properties.xtbl", "AnimSet"}, {"anim_prop_sets.xtbl", "Anim_Prop_Set"}}) {
        std::vector<Found> matches = findMatches(pr.first);
        printLocationsRaw(matches);
        long long rows = 0;
        for (const Found& f : matches) {
            if (f.item->status != 0) continue;
            try {
                g_docs.push_back(sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size()));
            } catch (const sr3xtbl::FormatError&) {
                continue;
            }
            rows += static_cast<long long>(sr3xtbl::Children(g_docs.back().table(), pr.second).size());
        }
        std::printf("  %s: %lld raw <%s> row(s) (untyped - no reader exists for this table)\n", pr.first.c_str(),
                    rows, pr.second.c_str());
    }

    // ---- G5 parser smoke check ---------------------------------------------
    std::printf("=== G5 parser smoke check ===\n");
    std::printf("parse attempts: %lld, FormatError failures: %lld\n", parseAttempts, parseFailures);
    if (parseAttempts > 0)
        GATE(parseFailures == 0, "every found, decoded copy of a target table parses without FormatError: %lld / %lld",
             parseAttempts - parseFailures, parseAttempts);

    std::printf("=== Summary ===\n");
    std::printf("implemented targets: %zu; found: %lld; not found: %lld (2 more tables are confirmed dead - no "
                "loader - and reported separately above)\n", specs.size(), tablesFound, tablesNotFound);
    GATE(tablesFound + tablesNotFound == static_cast<long long>(specs.size()),
         "every implemented target table was classified found/not-found: %lld + %lld == %zu", tablesFound,
         tablesNotFound, specs.size());
    GATE(tablesFound > 0, "CONTROL: at least one target table was actually located (the search is not vacuously empty)");

    std::printf("\n%s (%d gate failure%s)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
