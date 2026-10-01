// Population validator for sr3tables_weapons (spec-tables-weapons-combat.md),
// the 22-table weapons/combat group.
//
// Usage: validate_tables_weapons_population <archive.vpp_pc> [...]
//   Archive paths are read from argv, never hardcoded (they are typically
//   every *.vpp_pc under packfiles/pc/cache).
//
// What this does (measurement-discipline.md: every gate needs a denominator
// and at least one control that could fail):
//   (a) walks every archive recursively (vpp::Container, raw AND compressed
//       entries, nested containers), reusing the pattern of
//       tools/validation/validate_xtbl_population.cpp, to LOCATE each of the
//       22 target filenames (case-insensitive, exact match - DLC-prefixed
//       variants like dlc2_weapons.xtbl are reported separately as bonus
//       finds, not folded into the base table's count).
//   (b) for every table FOUND: parses every row with the typed reader
//       (src/tables_weapons.cpp) and reports row count; how many rows
//       supplied each sampled "always write" field vs the denominator (row
//       count); any top-level element name real rows carry that this
//       reader's known vocabulary does not mention; any enum/flag literal
//       text in real rows that is not in this reader's name tables; and a
//       narrow but concrete type-mismatch scan (documented-unsigned fields
//       whose real text carries a leading '-' or a '.').
//   (c) for tables NOT found, says so plainly - no fabricated zero-rows report.
//   (d) CONTROLS: a deliberately wrong element name is searched for and must
//       report 0 hits; a deliberately wrong filename is searched for and must
//       report NOT FOUND - proving the presence-counting machinery itself can
//       report a negative result, not just a vacuous positive one.
//
// store_weapon_lightset.xtbl (spec section 13.3) has no typed reader here
// (its schema is OPEN in the spec itself, "belongs to another group") - this
// harness still looks for the FILE (to report whether it exists) but does
// not attempt to parse rows against a struct that does not exist.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3tables_weapons/combat.h"
#include "sr3tables_weapons/weapons.h"
#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using namespace sr3xtbl;
using namespace sr3tables_weapons;

int g_checks = 0, g_fail = 0;
#define GATE(ok, ...)                                  \
    do {                                                \
        const bool ok_ = (ok);                          \
        ++g_checks;                                     \
        std::printf("  [%s] ", ok_ ? "PASS" : "FAIL");   \
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

// ---------------------------------------------------------------------------
// Archive walking (pattern reused from validate_xtbl_population.cpp)
// ---------------------------------------------------------------------------
struct Item {
    std::string archive;  // chain of containers, e.g. misc_tables.vpp_pc/foo.str2_pc
    std::string name;
    bool compressed = false;
    int status = 0;
    Bytes data;
};

std::vector<Item> g_items;
long long g_containers = 0, g_entriesSeen = 0;

void walk(vpp::ByteView bytes, const std::string& path) {
    vpp::Container c(bytes);
    ++g_containers;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        ++g_entriesSeen;
        const std::string ln = lower(e.name);
        if (!(ln.size() >= 4 && ln.compare(ln.size() - 4, 4, "xtbl") == 0)) continue;
        Item it;
        it.archive = path;
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
        const std::string ln = lower(c.entries()[i].name);
        if (ln.size() >= 4 && ln.compare(ln.size() - 4, 4, "xtbl") == 0) continue;
        try {
            walk(c.rawEntryBytes(i), path + "/" + c.entries()[i].name);
        } catch (const std::exception&) {
        }
    }
}

const Item* findExact(const std::string& nameLower) {
    for (const Item& it : g_items)
        if (it.status == 0 && lower(it.name) == nameLower) return &it;
    return nullptr;
}
std::vector<const Item*> findDlcVariants(const std::string& baseNameLower) {
    std::vector<const Item*> out;
    for (const Item& it : g_items) {
        if (it.status != 0) continue;
        std::string ln = lower(it.name);
        if (ln == baseNameLower) continue;
        if (ln.size() > baseNameLower.size() && ln.compare(ln.size() - baseNameLower.size(), baseNameLower.size(), baseNameLower) == 0)
            out.push_back(&it);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Generic per-table helpers
// ---------------------------------------------------------------------------
std::vector<const Node*> rowsOf(const Document& doc, const char* rowTag) {
    std::vector<const Node*> out;
    const Node* table = doc.table();
    for (const Node* r = FindChild(table, rowTag); r; r = NextSibling(table, r, rowTag)) out.push_back(r);
    return out;
}

// Every element name (lowercased) appearing anywhere in a row's subtree.
void censusRecursive(const Node* n, std::set<std::string>& out) {
    for (const Node* c : n->children()) {
        out.insert(lower(c->name()));
        censusRecursive(c, out);
    }
}
std::set<std::string> unknownNames(const std::vector<const Node*>& rows, const std::set<std::string>& known, bool recursive) {
    std::set<std::string> seen, unknown;
    for (const Node* r : rows) {
        if (recursive) {
            censusRecursive(r, seen);
        } else {
            for (const Node* c : r->children()) seen.insert(lower(c->name()));
        }
    }
    for (const std::string& s : seen)
        if (!known.count(s)) unknown.insert(s);
    return unknown;
}
void printUnknown(const char* table, const std::set<std::string>& unknown, size_t seenTotal, bool recursive) {
    std::printf("  %s: element-name census (%s) - unrecognised names: %zu\n", table, recursive ? "recursive" : "top-level only", unknown.size());
    if (!unknown.empty()) {
        std::printf("    unrecognised:");
        int shown = 0;
        for (const std::string& s : unknown)
            if (shown++ < 30) std::printf(" %s", s.c_str());
        std::printf("%s\n", unknown.size() > 30 ? " ..." : "");
    }
    (void)seenTotal;
}

template <size_t N>
void censusEnumMismatches(const std::vector<const Node*>& rows, const char* elemName, const std::string_view (&names)[N], std::map<std::string, int>& mismatches,
                           long long& denom) {
    for (const Node* r : rows) {
        const Node* e = FindChild(r, elemName);
        if (!e || !e->text()) continue;
        ++denom;
        if (EnumIndex(e, names, N) < 0) ++mismatches[*e->text()];
    }
}

template <size_t N>
bool matchesAnyLiteral(std::string_view text, const std::string_view (&names)[N]) {
    for (auto n : names)
        if (!n.empty() && NameEquals(text, n)) return true;
    return false;
}

// Scans every <Flag> child text under `wrapperElem` of each row against the
// union of the given literal tables; returns unmatched (text -> count).
template <size_t NA, size_t NB, size_t NC>
std::map<std::string, int> censusFlagMismatches(const std::vector<const Node*>& rows, const char* wrapperElem, const std::string_view (&a)[NA],
                                                 const std::string_view (&b)[NB], const std::string_view (&c)[NC], long long& denom) {
    std::map<std::string, int> mism;
    for (const Node* r : rows) {
        const Node* w = FindChild(r, wrapperElem);
        if (!w) continue;
        for (const Node* f = FindChild(w, "Flag"); f; f = NextSibling(w, f, "Flag")) {
            if (!f->text()) continue;
            ++denom;
            if (!matchesAnyLiteral(*f->text(), a) && !matchesAnyLiteral(*f->text(), b) && !matchesAnyLiteral(*f->text(), c)) ++mism[*f->text()];
        }
    }
    return mism;
}

void printMismatchMap(const char* label, const std::map<std::string, int>& mism, long long denom) {
    long long bad = 0;
    for (auto& kv : mism) bad += kv.second;
    GATE(mism.empty(), "%s: literal text matches a known name: %lld / %lld", label, denom - bad, denom);
    for (auto& kv : mism) std::printf("    unmatched literal (x%d): \"%s\"\n", kv.second, kv.first.c_str());
}

// Type-mismatch scan: documented-UNSIGNED integer element whose real text
// has a leading '-' (sign on an unsigned field) or a '.' (float text).
void censusTypeMismatch(const std::vector<const Node*>& rows, const char* elemName, long long& denom, long long& badSign, long long& badFloat) {
    for (const Node* r : rows) {
        const std::string* t = ChildText(r, elemName);
        if (!t || t->empty()) continue;
        ++denom;
        if ((*t)[0] == '-') ++badSign;
        if (t->find('.') != std::string::npos) ++badFloat;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        std::printf("usage: validate_tables_weapons_population <archive.vpp_pc> [...]\n");
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
        // readFile's buffer must outlive g_items' Bytes copies - walk() already copied decoded payloads.
    }
    std::printf("=== survey ===\n");
    std::printf("archives given: %zu, containers walked: %lld, directory entries seen: %lld, xtbl-family entries decoded: %zu\n", archives.size(),
                g_containers, g_entriesSeen, g_items.size());
    GATE(g_containers > 0 && g_entriesSeen > 0, "CONTROL: the walk is not vacuous (%lld containers, %lld entries)", g_containers, g_entriesSeen);

    // ---- CONTROL: a filename that cannot exist must be reported NOT FOUND ----
    GATE(findExact("totally_bogus_weapons_table_xyz.xtbl") == nullptr,
         "CONTROL: a deliberately wrong filename is correctly reported as not found");

    struct TableRow {
        const char* file;
        const char* section;
        bool implemented;
    };
    const TableRow kTables[] = {
        {"weapons.xtbl", "2", true},
        {"weapon_categories.xtbl", "3", true},
        {"weapon_upgrades.xtbl", "4", true},
        {"ammo.xtbl", "5", true},
        {"weapon_melee_attacks.xtbl", "6", true},
        {"melee.xtbl", "7.1", true},
        {"melee_transition_states.xtbl", "7.2", true},
        {"aim_drift.xtbl", "8", true},
        {"aim_assist.xtbl", "9", true},
        {"explosions.xtbl", "10.1", true},
        {"continuous_explosions.xtbl", "10.2", true},
        {"combat_actions.xtbl", "11.1", true},
        {"combat_tricks.xtbl", "11.2", true},
        {"weapon_tracers.xtbl", "12.1", true},
        {"weapon_tracer_materials.xtbl", "12.2", true},
        {"crib_weapons.xtbl", "13.1", true},
        {"store_weapons.xtbl", "13.2", true},
        {"store_weapon_lightset.xtbl", "13.3", false},  // schema OPEN in the spec itself - not implemented
        {"strafe_angles.xtbl", "14.1", true},
        {"taunting.xtbl", "14.2", true},
        {"hostage.xtbl", "14.3", true},
        {"windshield_cannon.xtbl", "14.4", true},
    };
    std::printf("\n=== locate (22 target filenames) ===\n");
    int foundCount = 0;
    for (const TableRow& t : kTables) {
        const Item* it = findExact(t.file);
        auto dlc = findDlcVariants(t.file);
        if (it) ++foundCount;
        std::printf("  %-32s section %-5s %-9s in %s%s; DLC-prefixed variants found: %zu\n", t.file, t.section, t.implemented ? "" : "(no reader)",
                    it ? it->archive.c_str() : "NOT FOUND", it ? (std::string(" :: ") + it->name).c_str() : "", dlc.size());
    }
    GATE(foundCount > 0, "at least one of the 22 target tables was located: %d / 22", foundCount);

    // ------------------------------------------------------------------
    // weapons.xtbl - full analysis
    // ------------------------------------------------------------------
    std::printf("\n=== weapons.xtbl (spec section 2) ===\n");
    if (const Item* it = findExact("weapons.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto rowNodes = rowsOf(doc, "Weapon");
        std::vector<Weapon> parsed;
        parsed.reserve(rowNodes.size());
        for (const Node* r : rowNodes) parsed.push_back(ParseWeapon(r));
        const long long n = static_cast<long long>(parsed.size());
        std::printf("  found in %s :: %s, %lld <Weapon> rows\n", it->archive.c_str(), it->name.c_str(), n);
        GATE(n > 0, "row count is nonzero: %lld", n);

        // CONTROL: a deliberately wrong element name must find 0 hits.
        {
            long long hits = 0;
            for (const Node* r : rowNodes)
                if (ChildText(r, "Name_Wrong_Element_XYZ")) ++hits;
            GATE(hits == 0, "CONTROL: a deliberately misspelled element name matches 0 / %lld rows", n);
        }

        // Always-write field presence census: how many real rows supply each
        // sampled field vs how many fall back to the unspecified-on-absent
        // path (this is exactly the check the spec itself could never do -
        // it only read the loader, never the base-game rows).
        auto census = [&](const char* label, auto extractor) {
            long long present = 0;
            for (const Weapon& w : parsed) present += extractor(w) ? 1 : 0;
            std::printf("  always-write field '%s': present in real rows %lld / %lld (unspecified-default path taken: %lld)\n", label, present, n,
                        n - present);
        };
        census("Magazine_Size", [](const Weapon& w) { return w.magazineSize.present; });
        census("Ammo_per_Shot", [](const Weapon& w) { return w.ammoPerShot.present; });
        census("Ammo_Regeneration", [](const Weapon& w) { return w.ammoRegeneration.present; });
        census("Range_Max", [](const Weapon& w) { return w.rangeMax.present; });
        census("AI_Ideal_Range_Min", [](const Weapon& w) { return w.aiIdealRangeMin.present; });
        census("AI_Ideal_Range_Max", [](const Weapon& w) { return w.aiIdealRangeMax.present; });
        census("Damage_Max/NPC_Damage", [](const Weapon& w) { return w.damageMaxNpc.present; });
        census("Damage_Max/Player_Damage", [](const Weapon& w) { return w.damageMaxPlayer.present; });
        census("Riot_Shield_Damage_Multiplier", [](const Weapon& w) { return w.riotShieldDamageMultiplier.present; });
        census("Time_Management/Refire_Delay", [](const Weapon& w) { return w.timeManagement.refireDelayMs.present; });
        census("Burst_Fire_Info/Shots", [](const Weapon& w) { return w.burstFireInfo.shots.present; });
        census("Ragdoll_Info/Chance", [](const Weapon& w) { return w.ragdollInfo.chance.present; });
        census("Projectile_Info/Mass (when Projectile_Info present)", [](const Weapon& w) { return w.projectileInfo.present && w.projectileInfo.mass.present; });
        census("Fire_Cone_Angle", [](const Weapon& w) { return w.fireConeAngleCos.present; });

        // Enum literal cross-check: every real Weapon_Class/Category/Inv_Slot/
        // Grenade_Type/Trigger_Type/Special_Case_Type text against this
        // reader's name tables (which are the spec's own literal lists).
        {
            std::map<std::string, int> mism;
            long long denom = 0;
            censusEnumMismatches(rowNodes, "Weapon_Class", kWeaponClassNames, mism, denom);
            printMismatchMap("Weapon_Class", mism, denom);
        }
        {
            std::map<std::string, int> mism;
            long long denom = 0;
            censusEnumMismatches(rowNodes, "Category", kWeaponCategoryNames, mism, denom);
            printMismatchMap("Category", mism, denom);
        }
        {
            std::map<std::string, int> mism;
            long long denom = 0;
            censusEnumMismatches(rowNodes, "Inv_Slot", kInvSlotNames, mism, denom);
            printMismatchMap("Inv_Slot", mism, denom);
        }
        {
            std::map<std::string, int> mism;
            long long denom = 0;
            censusEnumMismatches(rowNodes, "Grenade_Type", kGrenadeTypeNames, mism, denom);
            printMismatchMap("Grenade_Type", mism, denom);
        }
        {
            std::map<std::string, int> mism;
            long long denom = 0;
            censusEnumMismatches(rowNodes, "Trigger_Type", kTriggerTypeNames, mism, denom);
            printMismatchMap("Trigger_Type", mism, denom);
        }
        {
            std::map<std::string, int> mism;
            long long denom = 0;
            censusEnumMismatches(rowNodes, "Special_Case_Type", kSpecialCaseTypeNames, mism, denom);
            printMismatchMap("Special_Case_Type", mism, denom);
        }

        // Flags literal cross-check (63 literals across words A/B/C).
        {
            long long denom = 0;
            auto mism = censusFlagMismatches(rowNodes, "Flags", kFlagsAWordNames, kFlagsBWordNames, kFlagsCWordNames, denom);
            printMismatchMap("top-level Flags/Flag", mism, denom);
        }
        // Projectile_Flags literal cross-check (24 literals, one word).
        {
            long long denom = 0;
            std::map<std::string, int> mism;
            for (const Node* r : rowNodes) {
                const Node* pi = FindChild(r, "Projectile_Info");
                const Node* pf = FindChild(pi, "Projectile_Flags");
                if (!pf) continue;
                for (const Node* f = FindChild(pf, "Flag"); f; f = NextSibling(pf, f, "Flag")) {
                    if (!f->text()) continue;
                    ++denom;
                    if (!matchesAnyLiteral(*f->text(), kProjectileFlagNames)) ++mism[*f->text()];
                }
            }
            printMismatchMap("Projectile_Info/Projectile_Flags/Flag", mism, denom);
        }

        // Type-mismatch scan on documented-unsigned integer fields.
        {
            long long denom = 0, badSign = 0, badFloat = 0;
            censusTypeMismatch(rowNodes, "Magazine_Size", denom, badSign, badFloat);
            GATE(badSign == 0 && badFloat == 0, "Magazine_Size (documented u16): no leading '-' or '.' in real text: %lld / %lld clean", denom - badSign - badFloat, denom);
        }
        {
            long long denom = 0, badSign = 0, badFloat = 0;
            censusTypeMismatch(rowNodes, "Ammo_per_Shot", denom, badSign, badFloat);
            GATE(badSign == 0 && badFloat == 0, "Ammo_per_Shot (documented u16): no leading '-' or '.' in real text: %lld / %lld clean", denom - badSign - badFloat, denom);
        }

        // Element-name census: this reader's known vocabulary (transcribed
        // from every FindChild/ChildText/GetX/ReadXAlways call in
        // src/tables_weapons.cpp's ParseWeapon and its sub-block parsers,
        // at every depth) vs what real rows actually carry.
        {
            static const char* kKnown[] = {
                "name", "framework", "info_slot_index", "base_version", "is_dlc", "flags", "flag", "special_case_type",
                "melee_damage_overrides", "npc", "player", "online", "animation_group", "fine_aim_animation_group",
                "reload_animation_group", "reload_override_time_sec", "warmup_delay", "cooldown_delay", "grenade_type",
                "strafe_angles", "weapon_class", "category", "inv_slot", "muzzle_effect", "alt_muzzle_effect", "melee_effect",
                "fire_cone_impact_effect", "offhand_weapon_mesh", "tracer_info", "tracer", "tracer_npc", "alt_tracer",
                "alt_tracer_npc", "tracer_frequency", "constant_effects", "constant_effect", "effect", "weapon_prop_point",
                "condition", "brass", "diversion_kill_multiplier", "audio", "weapon_model", "soundbank_name",
                "stop_override_event", "alt_fire_stop_override_event", "sound_radius", "looping", "alt_looping",
                "target_lockon", "lockon_time_ms", "locking_size_multiplier", "locked_size_multiplier", "beep_timing_slowest",
                "beep_timing_fastest", "angle_from_reticle", "angle_from_reticle_lose_target", "locking_rotation_furthest_angle",
                "trigger_type", "alt_trigger_type", "ammo", "alt_ammo", "magazine_size", "ammo_per_shot", "ammo_regeneration",
                "range_max", "ai_ideal_range_min", "ai_ideal_range_max", "npc_aim_drift", "time_management", "refire_delay",
                "npc_refire_delay", "min", "max", "npc_refire_type", "post_detonate_delay", "pre_detonate_delay",
                "firecone_ramp_in_time", "alt_time_management", "damage_max", "npc_damage", "player_damage", "damage_min",
                "riot_shield_damage_multiplier", "explosion", "alt_explosion", "npc_explosion", "npc_alt_explosion",
                "underwater_explosion", "damage_max_dist", "damage_min_dist", "operator_damage_multiplier",
                "wieldable_prop_death_vfx", "wieldable_prop_hits_allowed", "flat_spread_metrics", "flat_spread_width_angle",
                "flat_spread_height_angle", "flat_spread_rotation", "flat_spread_rotation_per_shot", "fire_cone_angle",
                "fire_cone_length", "fire_cone_metrics", "metric_type", "min_max", "near_radius", "far_radius", "angle",
                "shots_per_round", "playerweaponspread", "spreadminmax", "player_spread_min", "player_spread_max",
                "spreadmovementmultipliers", "movement_multiplier_crouch", "movement_multiplier_walk", "movement_multiplier_run",
                "movement_multiplier_sprint", "movement_multiplier_vehicle", "movement_multiplier_fine_aim", "spreaddynamics",
                "player_to_spread_max", "player_to_spread_min", "spreaddynamicmultipliers", "playeraltweaponspread",
                "npcweaponspread", "npc_spread_min", "npc_spread_max", "npc_to_spread_max", "npc_to_spread_min",
                "spreadtargetmovementmultipliers", "target_movement_multiplier_walk", "target_movement_multiplier_run",
                "target_movement_multiplier_sprint", "target_movement_multiplier_vehicle", "npcaltweaponspread",
                "ragdoll_force_shoot", "object_bullet_hit_impulse_magnitude", "ragdoll_info", "chance",
                "death_velocity_horizontal", "death_velocity_vertical", "death_point_velocity",
                "death_angular_velocity_horizontal", "death_angular_velocity_vertical", "death_range_min", "death_range_max",
                "projectile_info", "model", "speed", "speed_npc", "post_ignition_speed", "post_ignition_speed_npc", "gravity",
                "launch_pitch_change_deg", "attached_effect", "creation_effect", "attached_effect_prop_point", "fuse_time",
                "npc_fuse_time", "fade_out_time", "projectile_ignition_delay_ms", "ai_can_guide", "mass", "linear_damp",
                "angular_damp", "restitution", "friction", "angular_velocity", "x", "y", "z", "blow_tire_radius",
                "ignition_effect", "sound", "foleycollision", "projectile_flags", "melee_material_effects", "material_effect",
                "material", "melee_damage_to_anchored_scaler", "vehicle_damage_scale", "player_vehicle_damage_scale",
                "melee_attack_info", "waterspray_info", "water_stream_force", "refill_rate_per_second",
                "radius_expansion_rate", "waterspray_effect", "pressure_increase_rate", "pressure_decrease_rate",
                "pressure_restore_time", "charge_release_info", "charge_time_sec", "min_charge_percent", "charge_base",
                "auto_release", "min_range", "pre_charge_delay", "charge_cooldown_time", "charging_camera_shake",
                "charged_camera_shake", "charge_flags", "vehicle_weapon", "primary_weapons", "weapon", "uid",
                "muzzle_explosion", "middle_component", "top_component", "max_force", "firing_angle_speed",
                "unmanned_speed", "damp_angle", "max_speed", "max_angle", "min_angle", "alt_weapons", "camera_info",
                "primary_fire_camera_shake", "primary_fire_camera_shake_intensity", "primary_fine_aim_camera_shake",
                "primary_fire_fine_aim_camera_shake_intensity", "secondary_fire_camera_shake",
                "secondary_fire_camera_shake_intensity", "secondary_fire_fine_aim_camera_shake",
                "secondary_fire_fine_aim_camera_shake_intensity", "melee_hard_camera_shake",
                "melee_hard_camera_shake_intensity", "melee_soft_camera_shake", "melee_soft_camera_shake_intensity",
                "player_hit_camera_shake", "player_hit_camera_shake_intensity", "primary_recoil_multiplier",
                "primary_fine_aim_recoil_multiplier", "primary_recoil_delay_ms", "primary_recoil_ramped",
                "secondary_recoil_multiplier", "secondary_fine_aim_recoil_multiplier", "zoom_type", "minimum_fov",
                "maximum_fov", "zoom_steps", "fov_rate", "burst_fire_info", "shots", "burst_delay_ms",
                "npc_desired_burst_size", "override_bullet_impact_effect", "alt_override_bullet_impact_effect",
                "override_bullet_impact_effect_npc", "alt_override_bullet_impact_effect_npc",
                "penetrating_end_point_explosion", "alt_penetrating_end_point_explosion", "overheat_info",
                "percent_increase_per_shot", "percent_decrease_per_second", "percent_decrease_per_reload",
                "percent_decrease_per_second_overheated", "percent_decrease_per_reload_overheated", "overheat_flags",
                "effect_situations", "situation", "max_melee_impacts", "blood_decal_scale", "blood_decal_delay",
                // Non-reader elements this project already knows about from the spec text itself
                // (section 2.6): included so the census reports them as "known but dead", not "unknown".
                "_editor", "effect_situation", "hit_wall_sound", "spinning_snd_pitch_end", "foley_name",
                "fire_damage_per_second", "npc_burst_time",
            };
            std::set<std::string> known(std::begin(kKnown), std::end(kKnown));
            auto unk = unknownNames(rowNodes, known, /*recursive=*/true);
            printUnknown("weapons.xtbl", unk, 0, true);
        }
    } else {
        std::printf("  NOT FOUND in the given archives.\n");
    }

    // ------------------------------------------------------------------
    // Remaining 20 tables: presence, row count, top-level element census,
    // and (where cheap) enum literal cross-checks.
    // ------------------------------------------------------------------
    auto reportSimple = [&](const char* file, const char* rowTag, std::initializer_list<const char*> knownTopLevel) {
        std::printf("\n=== %s ===\n", file);
        const Item* it = findExact(file);
        if (!it) {
            std::printf("  NOT FOUND in the given archives.\n");
            return;
        }
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto rows = rowsOf(doc, rowTag);
        std::printf("  found in %s :: %s, %zu <%s> rows\n", it->archive.c_str(), it->name.c_str(), rows.size(), rowTag);
        std::set<std::string> known(knownTopLevel.begin(), knownTopLevel.end());
        auto unk = unknownNames(rows, known, /*recursive=*/false);
        printUnknown(file, unk, 0, false);
    };

    reportSimple("weapon_categories.xtbl", "Categories", {"name"});
    reportSimple("ammo.xtbl", "Ammo",
                 {"name", "flags", "max_in_reserve", "inv_slot", "upgradable_ammo", "store"});
    // Bespoke deep check: does the real row ALSO carry a top-level cost/clip_size
    // outside <store>, and does it disagree with the nested store value? (spec
    // section 5.1 documents ONLY store/cost and store/clip_size.)
    if (const Item* it = findExact("ammo.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto rows = rowsOf(doc, "Ammo");
        long long n = static_cast<long long>(rows.size());
        long long rowLevelCost = 0, rowLevelClipSize = 0, differsFromStore = 0;
        for (const Node* r : rows) {
            const std::string* rowCost = ChildText(r, "cost");
            const std::string* rowClip = ChildText(r, "clip_size");
            if (rowCost) ++rowLevelCost;
            if (rowClip) ++rowLevelClipSize;
            const Node* store = FindChild(r, "store");
            const std::string* storeCost = ChildText(store, "cost");
            if (rowCost && storeCost && *rowCost != *storeCost) ++differsFromStore;
        }
        std::printf("  ammo.xtbl DISAGREEMENT CHECK (spec section 5.1 documents ONLY store/cost, store/clip_size):\n");
        std::printf("    row-level (non-store) <cost> present: %lld / %lld; row-level <clip_size> present: %lld / %lld\n", rowLevelCost, n,
                     rowLevelClipSize, n);
        std::printf("    of those, row-level <cost> text differs from <store><cost>: %lld / %lld\n", differsFromStore, rowLevelCost);
    }
    reportSimple("weapon_melee_attacks.xtbl", "Melee_Attack_Set",
                 {"name", "standingprimary", "standingsecondary", "movingprimary", "movingsecondary", "crouching",
                  "crouchmoving", "proneattackprimary", "proneattacksecondary", "hard_primary", "hard_secondary",
                  "non_fancy_primary", "non_fancy_secondary", "standingsynced", "movingsynced", "target_search_range"});
    reportSimple("melee.xtbl", "MeleeMove",
                 {"name", "attackanim", "syncedmove", "impact", "defaultdamage", "ragdoll_getup_time_ms", "explosion",
                  "active_attack_infos", "processing_flags", "attack_is_for", "attack_results", "testicular_assault",
                  "combo", "blood_effect"});
    // Bespoke deep check for melee.xtbl: spec section 7.1 documents ImpactFX,
    // Impact_Human_Effect, ImpactDir and AttackLimb as children of <Impact>
    // only. Real rows also carry top-level (non-Impact-nested) copies, plus
    // entirely undocumented victim_pre_condition/victim_post_condition/
    // req_prev_hits/NextPrimary/NextSecondary/NextSpecial/Notes.
    if (const Item* it = findExact("melee.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto rows = rowsOf(doc, "MeleeMove");
        long long n = static_cast<long long>(rows.size());
        long long topImpactFx = 0, topImpactHuman = 0, topImpactDir = 0, topAttackLimb = 0, topImpactForce = 0;
        long long hasImpactBlock = 0, forceDiffers = 0, attackLimbDiffers = 0;
        long long vPre = 0, vPost = 0, reqPrevHits = 0, nextPrimary = 0, nextSecondary = 0, nextSpecial = 0, notes = 0;
        for (const Node* r : rows) {
            if (ChildText(r, "ImpactFX")) ++topImpactFx;
            if (ChildText(r, "Impact_Human_Effect")) ++topImpactHuman;
            if (FindChild(r, "ImpactDir")) ++topImpactDir;
            const std::string* topAl = ChildText(r, "AttackLimb");
            if (topAl) ++topAttackLimb;
            const std::string* topForce = ChildText(r, "ImpactForce");
            if (topForce) ++topImpactForce;
            const Node* impact = FindChild(r, "Impact");
            if (impact) {
                ++hasImpactBlock;
                const std::string* nestedForce = ChildText(impact, "ImpactForce");
                if (topForce && nestedForce && *topForce != *nestedForce) ++forceDiffers;
                const std::string* nestedAl = ChildText(impact, "AttackLimb");
                if (topAl && nestedAl && !NameEquals(*topAl, *nestedAl)) ++attackLimbDiffers;
            }
            if (ChildText(r, "victim_pre_condition")) ++vPre;
            if (ChildText(r, "victim_post_condition")) ++vPost;
            if (ChildText(r, "req_prev_hits")) ++reqPrevHits;
            if (FindChild(r, "NextPrimary")) ++nextPrimary;
            if (FindChild(r, "NextSecondary")) ++nextSecondary;
            if (FindChild(r, "NextSpecial")) ++nextSpecial;
            if (FindChild(r, "Notes")) ++notes;
        }
        std::printf("  melee.xtbl DISAGREEMENT CHECK (spec section 7.1 documents these ONLY nested under <Impact>):\n");
        std::printf("    top-level (non-nested) ImpactFX: %lld / %lld; Impact_Human_Effect: %lld / %lld; ImpactDir: %lld / %lld; "
                     "ImpactForce: %lld / %lld; AttackLimb: %lld / %lld\n",
                     topImpactFx, n, topImpactHuman, n, topImpactDir, n, topImpactForce, n, topAttackLimb, n);
        std::printf("    rows with BOTH a top-level and a nested <Impact> block: %lld / %lld; of those, ImpactForce values differ: "
                     "%lld; AttackLimb values differ: %lld\n",
                     hasImpactBlock, n, forceDiffers, attackLimbDiffers);
        std::printf("  melee.xtbl UNDOCUMENTED ELEMENTS (not in spec section 7.1's vocabulary at all):\n");
        std::printf("    victim_pre_condition: %lld / %lld; victim_post_condition: %lld / %lld; req_prev_hits: %lld / %lld; "
                     "NextPrimary: %lld / %lld; NextSecondary: %lld / %lld; NextSpecial: %lld / %lld; Notes: %lld / %lld\n",
                     vPre, n, vPost, n, reqPrevHits, n, nextPrimary, n, nextSecondary, n, nextSpecial, n, notes, n);
    }
    reportSimple("melee_transition_states.xtbl", "Melee_Transition_State",
                 {"name", "animation_state", "return_action", "player_combo_time_ms", "npc_combo_min_time_ms",
                  "npc_combo_max_time_ms"});
    reportSimple("aim_drift.xtbl", "Profile", {"name", "turn_speed", "bullet_miss", "explosive_miss"});
    // aim_drift.xtbl: spec section 8 [OPEN / NEEDS-EXE] "`Recovery` in 0/20 real rows
    // (section 18.6) and five undocumented elements in 20/20 (Team B 9.83)"; spec
    // section 18.6 [corrected 2026-09-30]: row-level MinTime/MaxTime in 19 of the 20
    // real Profile rows (was 20/20). Printed, not gated: the spec marks these OPEN.
    if (const Item* it = findExact("aim_drift.xtbl")) {
        Document doc = ParseDocument(it->data.data(), it->data.size());
        auto rows = rowsOf(doc, "Profile");
        long long n = static_cast<long long>(rows.size());
        long long recoveryReports = 0, minTime = 0, maxTime = 0, penalties = 0, bonuses = 0, lagAmount = 0, lagTime = 0,
                  vertOffset = 0;
        for (const Node* r : rows) {
            for (const std::string& d : sr3tables_weapons::ParseAimDriftProfile(r).diagnostics)
                if (d == sr3tables_weapons::kAimDriftRecoveryEmptyDiagnostic) ++recoveryReports;
            if (ChildText(r, "MinTime")) ++minTime;
            if (ChildText(r, "MaxTime")) ++maxTime;
            if (FindChild(r, "Penalties")) ++penalties;
            if (FindChild(r, "Bonuses")) ++bonuses;
            if (FindChild(r, "lag_amount")) ++lagAmount;
            if (FindChild(r, "lag_time")) ++lagTime;
            if (FindChild(r, "vertical_offset")) ++vertOffset;
        }
        std::printf("  aim_drift.xtbl REPORT '%s' (spec section 8 NEEDS-EXE; spec 18.6: Recovery in 0/20 rows): %lld / %lld rows\n",
                    sr3tables_weapons::kAimDriftRecoveryEmptyDiagnostic, recoveryReports, n);
        std::printf("  aim_drift.xtbl row-level MinTime: %lld / %lld, MaxTime: %lld / %lld (spec 18.6 current figure: 19/20)\n", minTime, n,
                    maxTime, n);
        std::printf("  aim_drift.xtbl undocumented (spec section 8 OPEN, 20/20): Penalties %lld, Bonuses %lld, lag_amount %lld, "
                    "lag_time %lld, vertical_offset %lld (each / %lld)\n",
                    penalties, bonuses, lagAmount, lagTime, vertOffset, n);
    }
    reportSimple("aim_assist.xtbl", "Aiming", {"name", "steering", "slowing"});
    reportSimple("explosions.xtbl", "Explosion",
                 {"name", "panic_reaction", "radius", "decal_radius_override", "cone_angle", "fireradius",
                  "ai_sound_radius", "damage_min", "damage_max", "damage_min_player", "damage_max_player",
                  "player_vehicle_damage_scalar", "impulse", "redundant_effect_distance", "effect", "sticky_fire",
                  "groundfire", "screen_effects", "refraction_screen_effect", "flags", "camera_shake_info", "framework"});
    reportSimple("continuous_explosions.xtbl", "Continuous_explosion",
                 {"name", "explosion_data", "approach_info", "target_info", "audio_info"});
    reportSimple("combat_actions.xtbl", "actions",
                 {"name", "min_repeat_time", "auto_abort_ms", "no_interrupt_ms", "refresh_desire_interval", "precondition"});
    reportSimple("combat_tricks.xtbl", "Combat_Tricks",
                 {"default_duration", "record_display_time", "record_queue_time", "record_threshold", "gang_kill",
                  "gang_vehicle_kill", "one_hit_kill", "human_shield_kill", "head_shot_kill", "nut_shot_kill", "throwing",
                  "multi_kill", "specialist_kill", "brute_beat_kill", "brute_kill", "stag_kill", "explosive_kill",
                  "testicular_assault", "sprint_attack", "stag_vehicle_kill", "heli_or_vtol_kill", "tank_kill"});
    reportSimple("weapon_tracers.xtbl", "Weapon_Tracers",
                 {"name", "effect", "emitter_effect", "max_particles", "lifetime", "ricochet", "velocity_scale",
                  "hot_length_size", "framework"});
    reportSimple("weapon_tracer_materials.xtbl", "Tracer_Material", {"name", "tracer_dampener"});
    reportSimple("crib_weapons.xtbl", "Crib_Weapons", {"weapons_list"});
    reportSimple("store_weapons.xtbl", "Store_Weapons", {"weapons_list"});
    reportSimple("strafe_angles.xtbl", "Strafe_Angles",
                 {"name", "forward", "right", "backward_right", "backward", "backward_left", "left", "use_turn_limits"});
    reportSimple("taunting.xtbl", "Taunting",
                 {"max_taunts", "min_taunts", "aggro_taunt_value", "death_taunt_value", "death_taunt_delay",
                  "max_time_between_taunts", "death_taunt_distance", "taunt_complet_percent", "award_multiplier",
                  "first_taunt_cash", "first_taunt_respect", "max_lifetime_respect"});
    reportSimple("hostage.xtbl", "Hostage", {"min_notoriety", "max_lifetime_respect", "vehicle_classes"});
    reportSimple("windshield_cannon.xtbl", "Windshield_Cannon",
                 {"max_distance", "min_distance", "record_threshold", "record_display_time", "record_queue_time",
                  "max_respect", "max_lifetime_respect", "max_cash"});

    // weapon_upgrades.xtbl gets its own block: the OM-suffixed sibling names
    // are dynamic (per overridden field), so the known-set is generated.
    {
        std::printf("\n=== weapon_upgrades.xtbl ===\n");
        if (const Item* it = findExact("weapon_upgrades.xtbl")) {
            Document doc = ParseDocument(it->data.data(), it->data.size());
            auto rows = rowsOf(doc, "Weapon_Upgrade");
            std::printf("  found in %s :: %s, %zu <Weapon_Upgrade> rows\n", it->archive.c_str(), it->name.c_str(), rows.size());
            auto upgrades = std::vector<WeaponUpgrade>();
            for (const Node* r : rows) upgrades.push_back(ParseWeaponUpgrade(r));
            long long withTarget = 0;
            for (auto& u : upgrades) withTarget += u.targetWeaponName.has_value() ? 1 : 0;
            std::printf("  _Editor/Category present and yielded a target weapon name: %lld / %zu\n", withTarget, rows.size());
        } else {
            std::printf("  NOT FOUND in the given archives.\n");
        }
    }

    // store_weapon_lightset.xtbl: report presence only (no typed reader - spec section 13.3, OPEN elsewhere).
    {
        std::printf("\n=== store_weapon_lightset.xtbl (not implemented - spec section 13.3: schema OPEN, belongs to another group) ===\n");
        if (const Item* it = findExact("store_weapon_lightset.xtbl"))
            std::printf("  file IS present in %s :: %s (%zu bytes) - not parsed by this harness\n", it->archive.c_str(), it->name.c_str(), it->data.size());
        else
            std::printf("  NOT FOUND in the given archives.\n");
    }

    std::printf("\n%s (%d gate failure%s of %d checks)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s", g_checks);
    return g_fail ? 1 : 0;
}
