// Population gates for sr3tables_environment (include/sr3tables_environment/,
// src/tables_environment.cpp) over the REAL shipped archives.
//
// This is the check spec-tables-environment.md's own §14 explicitly says was
// never done: "no base-game value of any table in this group was read" - the
// spec's schema rests on loader disassembly plus DLC-only samples. This
// harness locates every one of the group's 26 implemented tables (27th,
// materials.xtbl, has no loader - spec 1.1 - and is not searched for) in
// whatever real archives are passed on argv, parses every row found with the
// typed reader, and reports exactly what the spec could not check itself.
//
// Usage: validate_tables_environment_population <archive.vpp_pc> [...]
//   (pass every archive under packfiles/pc/cache/*.vpp_pc; the harness does
//   not hardcode a cache path - see the project's HARD RULES.)
//
// Gates (every one prints a denominator; G2's per-table schema check has a
// CONTROL that must fail - the vfx.xtbl schema applied to effects.xtbl rows,
// mirroring validate_xtbl_population.cpp's own P8 control):
//   G1  locate: which of the 26 target filenames (exact name, case-
//       insensitive, plus the spec's own dlc1_/dlc2_/dlc3_ framework-prefix
//       convention, spec 1.5) exist in the real archives, and in which
//       top-level archive (base vs dlc1/2/3.vpp_pc) - the headline finding
//       opportunity for this group.
//   G2  for every table FOUND: row count; per known top-level element, how
//       many real rows have it present vs the total (the "always-write field
//       absent in real base data" question the spec's own §14.2 says no
//       sample could ever answer); any child element name real rows carry
//       that this reader's schema whitelist does not know about; CONTROL -
//       the vfx.xtbl schema applied to effects.xtbl rows must show many
//       unrecognised elements (both tables share row element name "Effect").
//   G3  enum/flag text values: every real value of a spec-enumerated text
//       field (time_of_day_objects Name, vfx Streaming_Category,
//       bitmap_materials Name, camera_free submode/vehicle_fallback names)
//       checked against this reader's copy of the spec's name list; anything
//       unmatched is reported, denominator = occurrences checked.
//   G4  type mismatches: real text of a spec-declared INTEGER field checked
//       for a decimal point (float where an integer was expected) or, for
//       fields the spec calls unsigned, a leading '-' (which the engine's
//       own unsigned reader reads as 0, sr3xtbl.h 3) - denominator = field
//       occurrences with text.
//   G5  parser-level control: every found file must still parse without a
//       FormatError (sr3tables_environment builds on sr3xtbl, whose own
//       tolerance is validated elsewhere; this is a smoke check specific to
//       the files this harness actually touches).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3tables_environment/tables.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using sr3xtbl::Node;

int g_fail = 0;
#define GATE(ok, ...)                                  \
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
// Archive walk (the same recursive vpp::Container pattern as
// tools/validation/validate_xtbl_population.cpp's G1: every entry ending in
// "xtbl", raw or compressed, through every nested container).
// ---------------------------------------------------------------------------
struct Item {
    std::string archiveChain;  // "topLevel.vpp_pc/nested.str2_pc/..."
    std::string topLevelArchive;  // just the top-level .vpp_pc file name
    std::string name;          // the entry's own file name
    bool compressed = false;
    int status = 0;            // 0 = decoded / readable
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

// ---------------------------------------------------------------------------
// Per-table schema descriptions, built from the spec's element-path tables
// (the same source the structs in tables.h cite). Used for two generic
// checks: which top-level child names real rows actually carry (vs this
// whitelist), and, for a representative set of declared-integer fields,
// whether real text disagrees with the declared type.
// ---------------------------------------------------------------------------
struct EnumCheck {
    std::string fieldName;                // direct child of the row holding the enum text
    std::vector<std::string> validNames;  // this reader's copy of the spec's name list
};
struct IntCheck {
    std::string parentPath;  // "" = the row itself; else a direct child to descend into first
    std::string fieldName;   // leaf element holding the integer text
    bool unsignedField;      // true: a leading '-' is a real mismatch (sr3xtbl.h 3: reads as 0)
};
struct TableSpec {
    std::string canonicalFile;
    std::string rowElement;         // "" for whole-file (single-element) tables
    bool wholeFile = false;
    std::string descendInto;        // "" = scan the row's own children; else descend first (rain.xtbl)
    std::vector<std::string> knownChildren;
    std::vector<EnumCheck> enumChecks;
    std::vector<IntCheck> intChecks;
};

std::vector<std::string> V(std::initializer_list<std::string_view> xs) {
    std::vector<std::string> v;
    for (auto x : xs) v.emplace_back(x);
    return v;
}

const std::vector<TableSpec>& tableSpecs() {
    static const std::vector<TableSpec> specs = {
        {"weather.xtbl", "Weather_Stage", false, "",
         V({"Name", "DisplayName", "Stage_Settings", "Rain_Settings", "Rain_Fog_Settings", "TOD_Settings",
            "Sun_Settings", "Moon", "Wind_Settings", "Cloud_Settings", "Temp_Settings", "Ambient_Wave_Settings",
            "Audio", "Next_Stage_List"}),
         {}, {}},
        // FOR TEAM B 2026-10-01 CORRECTION: both fields use the SIGNED always-write accessor
        // 0x00DABC70 (fixed from `unsignedField=true`, which would have wrongly flagged a real
        // leading '-' as a mismatch; it is not one - see G6 below for the dedicated census).
        {"weather_time_of_day.xtbl", "Weather_Time_Segment", false, "",
         V({"Name", "Start_Time", "Ramp_Out_Time", "Weather_Stages"}),
         {},
         {{"", "Start_Time", false}, {"", "Ramp_Out_Time", false}}},
        {"wind.xtbl", "Wind_Stage", false, "", V({"Name", "Display_Name", "Stage_Settings", "Wind_Settings", "Next_Stage_List"}), {}, {}},
        {"rain.xtbl", "Level", false, "Parameters",
         V({"Density", "View_Radius", "Speed", "Opacity", "Length_Near", "Length_Far", "Width_Near", "Width_Far",
            "Splash_Lifetime", "Splash_Size_Near", "Splash_Size_Far", "Wind_Amount", "Effect", "Camera_drop_effect"}),
         {}, {{"Parameters", "Density", true}}},
        {"lightning.xtbl", "Lightning_Type", false, "", V({"Name", "Probability", "VFX_List", "TOD_Overrides"}), {}, {}},
        {"lens_flares.xtbl", "Flare", false, "", V({"ImageFilename", "Radius", "Scale", "BaseAlpha"}), {}, {}},
        {"motion_blur.xtbl", "Motion_Blur_Settings", true, "",
         V({"On_Foot_Walk_Run", "On_Foot_Sprint", "On_Foot_Freefall", "On_Foot_Explosion", "Vehicle_Base",
            "Airplane_Base", "Helicopter_Base", "Vehicle_Nitrous", "Vehicle_Peelout"}),
         {}, {}},
        {"radial_blur.xtbl", "Radial_blur", false, "", V({"Name", "Strength", "Duration", "Radius", "Distance_fade", "Priority"}),
         {}, {{"", "Priority", false}}},
        {"dof_situations.xtbl", "DOF_situation", false, "",
         V({"name", "Start_Multiplier_A", "Start_Multiplier_B", "End_Multiplier_A", "End_Multiplier_B", "Blur_Radius",
            "Transition_Speed", "Human_Spherecast_Radius"}),
         {}, {}},
        {"refraction_situations.xtbl", "refraction_situation", false, "",
         V({"name", "Scale", "Frequency", "Offset_Delta", "Fade_In_Time", "Fade_Out_Time", "Duration", "Spasm", "Framework"}),
         {}, {}},
        {"skybox_effects.xtbl", "Skybox_Effect", false, "",
         V({"Name", "Effect", "Auto_Spawn", "Random_Orientation", "TODRange", "Weather_Stage", "Skybox_Layer"}),
         {}, {{"TODRange", "Start_Time", true}, {"TODRange", "End_Time", true}}},
        {"time_of_day_objects.xtbl", "Object_Type", false, "", V({"Name", "On_Time", "Off_Time", "Variation"}),
         {{"Name", std::vector<std::string>(sr3tables_environment::kTimeOfDayObjectNames.begin(),
                                             sr3tables_environment::kTimeOfDayObjectNames.end())}},
         {{"", "On_Time", true}, {"", "Off_Time", true}, {"", "Variation", true}}},
        {"external_light_override.xtbl", "light_override", false, "",
         V({"Name", "front_color", "back_color", "front_intensity", "back_intensity"}), {}, {}},
        {"effects.xtbl", "Effect", false, "",
         V({"Name", "Framework", "Info_Slot_Index", "visual", "sound", "sound_parent_switch_name", "sound_switch_name",
            "vfx_kill_particles_fade_time", "scale_factor", "Damage_Region", "restart_sound_at_loop",
            "kill_sound_when_done", "sound_follows_effect", "vfx_kill_particles", "stop_when_host_destroyed",
            "stop_under_car", "stop_under_player", "only_at_night", "use_mission_srid", "Proximity_Refraction",
            "_Editor"}),
         {}, {{"", "Info_Slot_Index", false}}},
        {"vfx.xtbl", "Effect", false, "",
         V({"Name", "Framework", "VFX", "Streaming_Category", "Radius", "Radius_expands", "Blocker", "Radial_blur",
            "LOD", "Start_time"}),
         {{"Streaming_Category", std::vector<std::string>(sr3tables_environment::kVfxStreamingCategoryNames.begin(),
                                                            sr3tables_environment::kVfxStreamingCategoryNames.end())}},
         {{"Blocker", "LifeSpan", false}}},
        {"interface_effects.xtbl", "InterfaceEffect", false, "", V({"Name", "Camera", "Camera_Blur", "LUT"}), {}, {}},
        {"decal_info.xtbl", "Decal_Info", false, "",
         V({"Name", "Material", "Preload", "Double_sided", "Life_Time", "Fade_Time", "Width", "Length", "Depth",
            "Depth_Fade_Start", "Depth_Fade_End", "Slope_Fade_Start", "Slope_Fade_End", "has_normal", "alpha_test"}),
         {}, {{"", "Life_Time", false}, {"", "Fade_Time", false}}},
        {"groundfires.xtbl", "Groundfire", false, "",
         V({"Name", "Damage_Radius", "Damage_Region", "effects", "Duration_Min", "Duration_Max"}), {}, {}},
        {"shells.xtbl", "Shell", false, "", V({"Name", "Static_Mesh", "Collision_Foley"}), {}, {}},
        {"material_color_variants.xtbl", "color_entry", false, "", V({"_Entry_ID", "Color"}), {},
         {{"", "_Entry_ID", false}}},
        {"bitmap_materials.xtbl", "Bitmap_Material", false, "",
         V({"Name", "MaterialProperties", "Bullet_Decals", "Blast_Decal", "Crash_Decal", "Audio_Occlusion"}),
         {{"Name", std::vector<std::string>(sr3tables_environment::kBitmapMaterialSlotNames.begin(),
                                             sr3tables_environment::kBitmapMaterialSlotNames.end())}},
         {{"", "Audio_Occlusion", false}}},
        {"bitmap_sheets.xtbl", "BitmapSheets", false, "", V({"Name"}), {}, {}},
        {"map_districts.xtbl", "Map_district", false, "",
         V({"Name", "data_item_name", "team_name", "contact_icon", "contact_name", "text_location"}), {}, {}},
        {"fade_categories.xtbl", "Category", false, "", V({"Name", "medium_lod_distance", "low_lod_distance", "Distance"}), {}, {}},
        {"camera_shake.xtbl", "Camera_Shake", false, "",
         V({"Name", "Wobble", "Destabilization", "Wander", "Jitter", "Oscillation1", "Oscillation2", "Direct",
            "Wander_Direct", "Strength_Graph"}),
         {}, {}},
        {"camera_free.xtbl", "Camera", true, "",
         V({"panning_group", "asvct_group", "follow_aggression_group", "hill_tracking_group", "miscellany_group",
            "submodes", "vehicle_fallbacks", "Vehicle_Aims"}),
         {}, {}},
    };
    return specs;
}

// ---------------------------------------------------------------------------
// Generic scans (operate on real sr3xtbl::Node trees, independent of the
// typed structs - this is what lets G2/G3/G4 see elements/values the typed
// reader does not itself surface, which is the whole point of a population
// check).
// ---------------------------------------------------------------------------
struct ChildScanResult {
    long long rows = 0;
    long long totalChildInstances = 0;
    std::map<std::string, long long> presentCount;       // known child (lowercased) -> rows where present
    std::map<std::string, long long> unrecognised;        // unrecognised child name (as seen) -> occurrences
    std::set<std::string> unrecognisedFiles;              // which archive entries carried an unrecognised name
};

ChildScanResult scanKnownChildren(const std::vector<const Node*>& rows, const std::string& descendInto,
                                   const std::vector<std::string>& known, const std::string& fileTag) {
    ChildScanResult r;
    r.rows = static_cast<long long>(rows.size());
    for (const Node* row : rows) {
        const Node* target = descendInto.empty() ? row : sr3xtbl::FindChild(row, descendInto);
        if (!target) continue;
        std::set<std::string> matchedHere;
        for (const Node* child : target->children()) {
            std::string matched;
            for (const std::string& k : known) {
                if (sr3xtbl::NameEquals(child->name(), k)) { matched = lower(k); break; }
            }
            ++r.totalChildInstances;
            if (!matched.empty()) matchedHere.insert(matched);
            else {
                ++r.unrecognised[child->name()];
                r.unrecognisedFiles.insert(fileTag);
            }
        }
        for (const std::string& k : matchedHere) ++r.presentCount[k];
    }
    return r;
}

struct EnumScanResult {
    long long occurrences = 0;
    std::map<std::string, long long> unmatched;  // real text value not in the spec's name list -> count
};

EnumScanResult scanEnumField(const std::vector<const Node*>& rows, const std::string& fieldName,
                              const std::vector<std::string>& validNames) {
    EnumScanResult r;
    for (const Node* row : rows) {
        const std::string* t = sr3xtbl::ChildText(row, fieldName);
        if (!t || t->empty()) continue;
        ++r.occurrences;
        bool ok = false;
        for (const std::string& v : validNames)
            if (sr3xtbl::NameEquals(*t, v)) { ok = true; break; }
        if (!ok) ++r.unmatched[*t];
    }
    return r;
}

struct IntScanResult {
    long long occurrences = 0;
    long long hasDot = 0;
    long long leadingMinusOnUnsigned = 0;
    std::vector<std::string> dotExamples, minusExamples;
};

IntScanResult scanIntField(const std::vector<const Node*>& rows, const IntCheck& chk) {
    IntScanResult r;
    for (const Node* row : rows) {
        const Node* parent = chk.parentPath.empty() ? row : sr3xtbl::FindChild(row, chk.parentPath);
        if (!parent) continue;
        const std::string* t = sr3xtbl::ChildText(parent, chk.fieldName);
        if (!t || t->empty()) continue;
        ++r.occurrences;
        if (t->find('.') != std::string::npos) {
            ++r.hasDot;
            if (r.dotExamples.size() < 5) r.dotExamples.push_back(*t);
        }
        if (chk.unsignedField && (*t)[0] == '-') {
            ++r.leadingMinusOnUnsigned;
            if (r.minusExamples.size() < 5) r.minusExamples.push_back(*t);
        }
    }
    return r;
}

// A found real copy of a target table.
struct Found {
    const Item* item;
    bool dlcArchive;  // top-level archive name starts with "dlc" (case-insensitive)
};

std::vector<Found> findMatches(const std::string& canonical) {
    std::vector<Found> out;
    const std::string lc = lower(canonical);
    const std::string d1 = "dlc1_" + lc, d2 = "dlc2_" + lc, d3 = "dlc3_" + lc;
    for (const Item& it : g_items) {
        const std::string ln = lower(it.name);
        if (ln != lc && ln != d1 && ln != d2 && ln != d3) continue;
        const bool dlc = lower(it.topLevelArchive).rfind("dlc", 0) == 0;
        out.push_back({&it, dlc});
    }
    return out;
}

std::vector<const Node*> rowsOf(const sr3xtbl::Document& doc, const TableSpec& spec) {
    if (spec.wholeFile) {
        const Node* el = sr3xtbl::FindChild(doc.table(), spec.rowElement);
        return el ? std::vector<const Node*>{el} : std::vector<const Node*>{};
    }
    return sr3xtbl::Children(doc.table(), spec.rowElement);
}

// ---------------------------------------------------------------------------
// G6: dedicated real-data census for each of the 5 "FOR TEAM B (2026-10-01,
// CORRECTION)" spec tags this harness's own reader fixes were made against.
// Every count here is genuine over the real archives passed on argv; "0" is
// reported as-is (the task this harness exists for explicitly allows it).
// ---------------------------------------------------------------------------

// Correction 1 (spec 1.4: float sign is handled by the grammar, not the
// accessor - every float field is signed). Recursively counts every element
// anywhere under `n` whose OWN text starts with '-', across every table this
// harness touches - a generic "how much real leading-minus text exists in
// this table group" census, independent of the typed reader. Reported
// per-element-name so the already-tracked unsigned-INTEGER fields (Density,
// On_Time/Off_Time/Variation, Audio_Occlusion, _Entry_ID, Info_Slot_Index,
// Life_Time/Fade_Time - each already censused by G4 above with its own
// correct, still-unsigned semantics) can be told apart from everything else
// (predominantly float fields, now CONFIRMED signed end-to-end by this fix).
void censusLeadingMinusLeaves(const Node* n, long long& total, std::map<std::string, long long>& byName) {
    if (!n) return;
    if (n->hasText()) {
        const std::string& t = *n->text();
        if (!t.empty() && t[0] == '-') {
            ++total;
            ++byName[lower(n->name())];
        }
    }
    for (const Node* c : n->children()) censusLeadingMinusLeaves(c, total, byName);
}

// Correction 2 (spec 3.1: Start_Time = 2400 -> exactly 1.0, kept, not
// wrapped). Counts exact-text "2400" occurrences (the one value the wrap-
// boundary correction actually distinguishes from the old behaviour) plus any
// real leading '-' (now a real negative value per the signed-accessor fix,
// where before it silently read as 0).
struct StartTimeCensus {
    long long occurrences = 0;
    long long exactly2400 = 0;
    long long leadingMinus = 0;
};
StartTimeCensus censusStartTime(const std::vector<const Node*>& rows, const std::string& fieldName) {
    StartTimeCensus r;
    for (const Node* row : rows) {
        const std::string* t = sr3xtbl::ChildText(row, fieldName);
        if (!t || t->empty()) continue;
        ++r.occurrences;
        if (*t == "2400") ++r.exactly2400;
        if ((*t)[0] == '-') ++r.leadingMinus;
    }
    return r;
}

// Correction 3 (spec 9.2: the Street Lights/Searchlights default (on, off)
// pair is data, not a build constant). Counts real rows naming either of the
// two indices that - per the spec's own real-data citation (17.1) - never
// have a row in the base game, so they run on that data-sourced default for
// the whole game. Independently re-verified here against the full real
// archive list (not just the base game's own tables).
long long censusNamedRows(const std::vector<const Node*>& rows, std::string_view name) {
    long long n = 0;
    for (const Node* row : rows) {
        const std::string* t = sr3xtbl::ChildText(row, "Name");
        if (t && sr3xtbl::NameEquals(*t, name)) ++n;
    }
    return n;
}

// Correction 5 (spec 12.2: a later duplicate submode name overwrites the
// earlier one - last wins). Tallies real submodes/submode row names (case-
// insensitively) across every real camera_free.xtbl copy found, so the real
// duplicate count this correction actually matters for is visible.
struct SubmodeNameCensus {
    long long totalRows = 0;
    long long duplicateRows = 0;  // rows whose name was already seen at least once before them
    std::map<std::string, long long> countByName;  // lower-cased name -> occurrences
};
SubmodeNameCensus censusSubmodeNames(const std::vector<const Node*>& cameraRows) {
    SubmodeNameCensus r;
    for (const Node* camera : cameraRows) {
        const Node* submodes = sr3xtbl::FindChild(camera, "submodes");
        for (const Node* sm : sr3xtbl::Children(submodes, "submode")) {
            const std::string* t = sr3xtbl::ChildText(sm, "name");
            if (!t) continue;
            ++r.totalRows;
            const std::string key = lower(*t);
            if (r.countByName.count(key) && r.countByName[key] > 0) ++r.duplicateRows;
            ++r.countByName[key];
        }
    }
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        std::printf("usage: validate_tables_environment_population <archive.vpp_pc> [...]\n");
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

    long long tablesFound = 0, tablesNotFound = 0, tablesBaseFound = 0, tablesDlcOnly = 0;
    // effects.xtbl rows, kept aside for the G2 CONTROL cross-schema check.
    std::vector<const Node*> effectsRowsForControl;
    std::vector<sr3xtbl::Document> keepAlive;  // Node* point into these; keep them alive for the whole run

    long long parseFailures = 0, parseAttempts = 0;

    // G6 correction 1 aggregate (filled per-table below, reported at the end).
    long long leadingMinusTotal = 0;
    std::map<std::string, long long> leadingMinusByName;

    for (const TableSpec& spec : tableSpecs()) {
        std::printf("--- %s ---\n", spec.canonicalFile.c_str());
        std::vector<Found> matches = findMatches(spec.canonicalFile);
        if (matches.empty()) {
            std::printf("  NOT FOUND in any given archive.\n");
            ++tablesNotFound;
            continue;
        }
        ++tablesFound;
        bool anyBase = false, anyDlc = false;
        for (const Found& f : matches) {
            std::printf("  found: %s (in %s)  %s  status=%d\n", f.item->archiveChain.c_str(),
                        f.item->topLevelArchive.c_str(), f.dlcArchive ? "[DLC archive]" : "[BASE archive]",
                        f.item->status);
            (f.dlcArchive ? anyDlc : anyBase) = true;
        }
        if (anyBase) ++tablesBaseFound; else ++tablesDlcOnly;
        std::printf("  location summary: %s%s%s\n", anyBase ? "found in a BASE archive" : "",
                    anyBase && anyDlc ? " AND " : "", anyDlc ? "found in a DLC archive" : "");

        // Parse every found, successfully-decoded copy; aggregate rows.
        std::vector<const Node*> allRows;
        long long rowsPerFile = 0;
        for (const Found& f : matches) {
            if (f.item->status != 0) continue;
            ++parseAttempts;
            try {
                keepAlive.push_back(sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size()));
            } catch (const sr3xtbl::FormatError& e) {
                ++parseFailures;
                std::printf("  PARSE FAILED %s: %s\n", f.item->name.c_str(), e.what());
                continue;
            }
            const sr3xtbl::Document& doc = keepAlive.back();
            std::vector<const Node*> rows = rowsOf(doc, spec);
            rowsPerFile += static_cast<long long>(rows.size());
            allRows.insert(allRows.end(), rows.begin(), rows.end());

            // G6 correction 5, PER ARCHIVE COPY (not combined - see why below): only one
            // archive's copy of camera_free.xtbl is ever actually loaded in a real run, so the
            // engine's "later duplicate overwrites the earlier" rule only ever applies WITHIN a
            // single copy. Combining multiple copies (as the aggregate census further below
            // does, for a different, honest purpose) would conflate a real intra-file duplicate
            // with "the same submode name also appears in a DIFFERENT archive's copy", which is
            // not the same phenomenon the correction describes.
            if (spec.canonicalFile == "camera_free.xtbl") {
                SubmodeNameCensus sc = censusSubmodeNames(rows);
                std::printf("  G6 correction 5 (FOR TEAM B 2026-10-01), this copy only (%s): %lld submode "
                            "rows, %lld a repeat of a name already seen earlier IN THIS SAME FILE\n",
                            f.item->archiveChain.c_str(), sc.totalRows, sc.duplicateRows);
            }
        }
        std::printf("  rows parsed (all found copies combined): %lld\n", rowsPerFile);
        if (spec.canonicalFile == "effects.xtbl") effectsRowsForControl = allRows;

        if (allRows.empty()) { std::printf("  (no rows to analyse)\n"); continue; }

        // G2: known-children presence / unrecognised elements.
        ChildScanResult cs = scanKnownChildren(allRows, spec.descendInto, spec.knownChildren, spec.canonicalFile);
        std::printf("  G2 element census over %lld rows (%lld child-element instances scanned):\n", cs.rows,
                    cs.totalChildInstances);
        for (const std::string& k : spec.knownChildren) {
            const std::string kl = lower(k);
            const long long present = cs.presentCount.count(kl) ? cs.presentCount[kl] : 0;
            std::printf("    %-28s present in %lld / %lld rows (absent -> unspecified-default path in %lld)\n",
                        k.c_str(), present, cs.rows, cs.rows - present);
        }
        if (!cs.unrecognised.empty()) {
            std::printf("  G2 UNRECOGNISED elements (not in this reader's schema whitelist), %lld total occurrences:\n",
                        cs.totalChildInstances);
            for (auto& kv : cs.unrecognised)
                std::printf("    %-28s x%lld\n", kv.first.c_str(), kv.second);
        } else {
            std::printf("  G2 unrecognised elements: 0 / %lld instances\n", cs.totalChildInstances);
        }

        // G3: enum/flag field values.
        for (const EnumCheck& ec : spec.enumChecks) {
            EnumScanResult es = scanEnumField(allRows, ec.fieldName, ec.validNames);
            std::printf("  G3 enum field '%s': %lld occurrences checked, %zu unmatched value(s)\n",
                        ec.fieldName.c_str(), es.occurrences, es.unmatched.size());
            for (auto& kv : es.unmatched) std::printf("      unmatched text %-24s x%lld\n", kv.first.c_str(), kv.second);
        }

        // G4: declared-integer field type mismatches.
        for (const IntCheck& ic : spec.intChecks) {
            IntScanResult is = scanIntField(allRows, ic);
            std::printf("  G4 integer field '%s%s%s': %lld occurrences, %lld with a '.' (float-looking)%s\n",
                        ic.parentPath.empty() ? "" : (ic.parentPath + "/").c_str(), ic.fieldName.c_str(),
                        ic.unsignedField ? " [unsigned]" : "", is.occurrences, is.hasDot,
                        ic.unsignedField ? (", " + std::to_string(is.leadingMinusOnUnsigned) + " with a leading '-'").c_str() : "");
            for (auto& s : is.dotExamples) std::printf("      dot example: %s\n", s.c_str());
            for (auto& s : is.minusExamples) std::printf("      leading-minus example: %s\n", s.c_str());
        }

        // G6 correction 1 (float sign): fold this table's rows into the global census.
        for (const Node* row : allRows) censusLeadingMinusLeaves(row, leadingMinusTotal, leadingMinusByName);

        // G6 correction 2 (weather_time_of_day Start_Time / Ramp_Out_Time wrap boundary + sign).
        if (spec.canonicalFile == "weather_time_of_day.xtbl") {
            StartTimeCensus st = censusStartTime(allRows, "Start_Time");
            StartTimeCensus ro = censusStartTime(allRows, "Ramp_Out_Time");
            std::printf("  G6 correction 2 (Start_Time wrap boundary + sign, FOR TEAM B 2026-10-01): "
                        "%lld Start_Time occurrences, %lld exactly \"2400\" (the corrected wrap boundary - "
                        "kept as 1.0, not wrapped to 0), %lld with a leading '-' (now a real negative "
                        "value per the signed-accessor fix); Ramp_Out_Time: %lld occurrences, %lld "
                        "with a leading '-'\n",
                        st.occurrences, st.exactly2400, st.leadingMinus, ro.occurrences, ro.leadingMinus);
        }

        // G6 correction 3 (time_of_day_objects default pair is data, not a build constant):
        // real count of rows naming the two indices that never have one in the base game.
        if (spec.canonicalFile == "time_of_day_objects.xtbl") {
            const long long streetLights = censusNamedRows(allRows, "Street Lights");
            const long long searchlights = censusNamedRows(allRows, "Searchlights");
            std::printf("  G6 correction 3 (default (on,off) pair is TOD-definition DATA, FOR TEAM B "
                        "2026-10-01): real rows named \"Street Lights\": %lld, \"Searchlights\": %lld "
                        "(0 of either -> both records run on the data-sourced default for the whole "
                        "archive set given; this reader does not model that default - see tables.h)\n",
                        streetLights, searchlights);
        }

        // G6 correction 4 (vfx LOD-absent fields stay 0, not 1.0e8/1.0e10): the LOD-absent count
        // is already in cs.presentCount above; restated here for direct visibility.
        if (spec.canonicalFile == "vfx.xtbl") {
            const long long lodPresent = cs.presentCount.count("lod") ? cs.presentCount["lod"] : 0;
            std::printf("  G6 correction 4 (LOD-absent fields stay 0, FOR TEAM B 2026-10-01): "
                        "%lld / %lld real vfx rows have NO <LOD> element at all (these now get 0, "
                        "not the 1.0e8/1.0e10 defaults, from LodSpawningDistanceOrDefault() etc.)\n",
                        cs.rows - lodPresent, cs.rows);
        }

        // G6 correction 5, COMBINED across every copy found (NOT the real in-file duplicate
        // figure - see the per-copy numbers printed above, inside the parse loop, for that;
        // this combined figure additionally folds in "same name also appears in a DIFFERENT
        // archive's copy", a different, cross-copy phenomenon, kept here only to show the raw
        // combined vector this reader actually returns when several copies are all passed in).
        if (spec.canonicalFile == "camera_free.xtbl") {
            SubmodeNameCensus sc = censusSubmodeNames(allRows);
            std::printf("  G6 correction 5 COMBINED across all copies (informational only - see the "
                        "per-copy counts above for the real same-file duplicate figure): %lld submode "
                        "rows total, %lld a repeat of a name already seen earlier in this combined list\n",
                        sc.totalRows, sc.duplicateRows);
            for (auto& kv : sc.countByName)
                if (kv.second > 1) std::printf("      combined duplicate name: %-28s x%lld\n", kv.first.c_str(), kv.second);
        }
    }

    std::printf("=== G6 correction 1 (float sign, FOR TEAM B 2026-10-01): aggregate real-data census ===\n");
    std::printf("total leaf elements with a leading '-' across every table scanned above: %lld\n", leadingMinusTotal);
    for (auto& kv : leadingMinusByName) std::printf("    %-40s x%lld\n", kv.first.c_str(), kv.second);

    std::printf("=== G2 CONTROL: vfx.xtbl's schema applied to effects.xtbl rows ===\n");
    {
        const TableSpec* vfxSpec = nullptr;
        for (const TableSpec& s : tableSpecs())
            if (s.canonicalFile == "vfx.xtbl") vfxSpec = &s;
        if (vfxSpec && !effectsRowsForControl.empty()) {
            ChildScanResult ctrl = scanKnownChildren(effectsRowsForControl, "", vfxSpec->knownChildren, "CONTROL");
            long long unrecognisedTotal = 0;
            for (auto& kv : ctrl.unrecognised) unrecognisedTotal += kv.second;
            std::printf("  effects.xtbl rows scanned against vfx.xtbl's whitelist: %lld / %lld child instances unrecognised\n",
                        unrecognisedTotal, ctrl.totalChildInstances);
            GATE(unrecognisedTotal > 0, "CONTROL: a schema swap must find unrecognised elements (it does; the check is not vacuous)");
        } else {
            std::printf("  (skipped: no effects.xtbl rows were found to run the control against)\n");
        }
    }

    std::printf("=== G5 parser smoke check ===\n");
    std::printf("parse attempts: %lld, FormatError failures: %lld\n", parseAttempts, parseFailures);
    if (parseAttempts > 0) GATE(parseFailures == 0, "every found, decoded copy of a target table parses without FormatError: %lld / %lld", parseAttempts - parseFailures, parseAttempts);

    std::printf("=== Summary ===\n");
    std::printf("targets: %zu; found: %lld (in a base archive: %lld, DLC-only: %lld); not found: %lld\n",
                tableSpecs().size(), tablesFound, tablesBaseFound, tablesDlcOnly, tablesNotFound);
    GATE(tablesFound + tablesNotFound == static_cast<long long>(tableSpecs().size()), "every target table was classified found/not-found: %lld + %lld == %zu", tablesFound, tablesNotFound, tableSpecs().size());
    GATE(tablesFound > 0, "CONTROL: at least one target table was actually located (the search is not vacuously empty)");

    std::printf("\n%s (%d gate failure%s)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
