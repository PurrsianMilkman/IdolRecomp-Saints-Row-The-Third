// Population diagnostic for sr3tables_diversions (include/sr3tables_diversions/,
// src/tables_diversions.cpp) over the REAL shipped archives. NOT wired into
// CMakeLists.txt (per the task's HARD RULE) - a standalone tool, built with
// tools/validation/build_one.bat's cl.exe pattern once build_verify/ holds
// the usual objects, or compiled ad hoc against sr3xtbl.cpp/tables_diversions.cpp.
//
// Usage: validate_tables_diversions_population <archive.vpp_pc> [...]
//   Pass every archive under packfiles/pc/cache/*.vpp_pc - this harness does
//   not hardcode a cache path (project HARD RULES). In particular, pass
//   BOTH da_tables.vpp_pc/misc_tables.vpp_pc (this group's usual homes, per
//   spec 1.1) AND patch_compressed.vpp_pc/patch_uncompressed.vpp_pc: a past
//   bug in this project undercounted tables that exist in more than one
//   archive (e.g. a patch archive silently superseding a base one), so this
//   tool deliberately reports EVERY archive location a table is found in,
//   not just the first match.
//
// What this reports, per table:
//   G1  every archive location the table's filename is found in (walking
//       nested containers recursively), explicitly calling out any table
//       found in MORE than one top-level archive.
//   G2  structural counts (row/child-element counts this reader can see)
//       compared against whatever spec-tables-diversions.md's own text
//       states as the REAL base-game shape for that table (section 14's
//       validation summary, plus each table's own "Real base row" sentence).
//       Where the spec gives an EXACT expected count, this is a PASS/FAIL
//       gate; where the spec only gives an approximate figure ("~20") or an
//       observed-not-required value, it is reported for review, not gated.
//       NOTE: this tool does NOT attempt to reconcile the spec's own
//       "N/N real-row fields accounted for" headline tallies - two of them
//       (stunt_wheels' "7/7 top-level" and cat_and_mouse's "16/16", which
//       its own parenthetical breaks down as 25+9=34, not 16, and whose "9
//       top-level" undercounts the 11 top-level scalars the same sentence
//       names) do not add up on inspection - see the task report. Only
//       independently-checkable STRUCTURAL facts (row counts, capacities)
//       are gated here.
//   G3  a specific empirical claim from spec 9: the four activity-text files
//       (fraud_text/snatch_text/snatch_kinzie_text/escort_text.xtbl) all
//       share the row-name literal "Escort", even the two "Snatch" files.
//   G5  parser smoke check: every found, decoded copy must parse without a
//       FormatError.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3tables_diversions/tables.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using sr3xtbl::Node;

int g_fail = 0;
#define GATE(ok, ...)                                 \
    do {                                               \
        const bool ok_ = (ok);                         \
        std::printf("  [%s] ", ok_ ? "PASS" : "FAIL"); \
        std::printf(__VA_ARGS__);                      \
        std::printf("\n");                             \
        if (!ok_) ++g_fail;                             \
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
// tools/validation/validate_tables_environment_population.cpp's G1).
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

std::vector<Found> findMatches(const std::string& canonical) {
    std::vector<Found> out;
    const std::string lc = lower(canonical);
    for (const Item& it : g_items) {
        if (lower(it.name) == lc) out.push_back({&it});
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The 25 tables assigned to this group (spec 1.1), with the STRUCTURAL
// expectations spec-tables-diversions.md states from real base-game data.
// ---------------------------------------------------------------------------
namespace sd = sr3tables_diversions;

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        std::printf("usage: validate_tables_diversions_population <archive.vpp_pc> [...]\n");
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
    std::printf("archives given: %zu, containers walked: %lld, xtbl-family entries seen: %zu\n", archives.size(),
                g_containers, g_items.size());
    GATE(g_containers > 0, "CONTROL: at least one container was actually opened (not a silent no-op run)");

    static const char* kTargetFiles[] = {
        "stunt_drifting.xtbl",     "stunt_hijacking.xtbl",  "stunt_near_miss.xtbl",
        "stunt_back_seat_driver.xtbl", "stunt_oncoming.xtbl",   "stunt_peel_out.xtbl",
        "stunt_wheels.xtbl",       "stunt_jumping_diversion.xtbl", "stunt_low_flying.xtbl",
        "barnstorming.xtbl",       "base_jumping.xtbl",     "cat_and_mouse.xtbl",
        "exploration_diversion.xtbl", "mugging_diversion.xtbl", "streaking.xtbl",
        "photo_op.xtbl",           "escort_name_generator.xtbl", "shop_names.xtbl",
        "fraud_text.xtbl",         "snatch_text.xtbl",      "snatch_kinzie_text.xtbl",
        "escort_text.xtbl",        "horde_mode.xtbl",       "horde_mode_text.xtbl",
        "fraud_globals.xtbl",
    };
    long long tablesFound = 0, tablesNotFound = 0, tablesMultiArchive = 0;
    std::map<std::string, std::vector<Found>> matchesByFile;
    for (const char* f : kTargetFiles) {
        std::vector<Found> m = findMatches(f);
        matchesByFile[f] = m;
        std::printf("--- %s ---\n", f);
        if (m.empty()) {
            std::printf("  NOT FOUND in any given archive.\n");
            ++tablesNotFound;
            continue;
        }
        ++tablesFound;
        std::map<std::string, int> perTopLevel;
        for (const Found& fo : m) {
            std::printf("  found: %s (in %s) status=%d\n", fo.item->archiveChain.c_str(),
                        fo.item->topLevelArchive.c_str(), fo.item->status);
            ++perTopLevel[lower(fo.item->topLevelArchive)];
        }
        if (perTopLevel.size() > 1) {
            ++tablesMultiArchive;
            std::printf("  ** PRESENT IN %zu DIFFERENT TOP-LEVEL ARCHIVES ** (", perTopLevel.size());
            bool first = true;
            for (auto& kv : perTopLevel) {
                if (!first) std::printf(", ");
                std::printf("%s x%d", kv.first.c_str(), kv.second);
                first = false;
            }
            std::printf(") - a naive first-match reader would silently miss the others.\n");
        }
    }
    GATE(tablesFound + tablesNotFound == static_cast<long long>(std::size(kTargetFiles)),
         "every target table was classified found/not-found: %lld + %lld == %zu", tablesFound, tablesNotFound,
         std::size(kTargetFiles));
    GATE(tablesFound > 0, "CONTROL: at least one target table was actually located (search is not vacuously empty)");
    std::printf("tables present in more than one top-level archive: %lld / %lld found\n", tablesMultiArchive,
                tablesFound);

    // -----------------------------------------------------------------------
    // G2: parse every found copy, report structural counts against the
    // spec's stated real-data shape. Uses the FIRST successfully-decoded
    // copy per table for the typed-reader checks (every copy is still listed
    // above in G1 regardless).
    // -----------------------------------------------------------------------
    std::printf("\n=== G2 structural counts vs spec-tables-diversions.md's stated real-game shape ===\n");
    long long parseAttempts = 0, parseFailures = 0;
    std::vector<sr3xtbl::Document> keepAlive;

    auto firstDoc = [&](const char* file) -> const sr3xtbl::Document* {
        for (const Found& fo : matchesByFile[file]) {
            if (fo.item->status != 0) continue;
            ++parseAttempts;
            try {
                keepAlive.push_back(sr3xtbl::ParseDocument(fo.item->data.data(), fo.item->data.size()));
            } catch (const sr3xtbl::FormatError& e) {
                ++parseFailures;
                std::printf("  PARSE FAILED %s (%s): %s\n", file, fo.item->archiveChain.c_str(), e.what());
                continue;
            }
            return &keepAlive.back();
        }
        return nullptr;
    };

    // stunt_* family: single row, spec's own field-tally per table (not
    // re-verified here - only presence of the row itself is gated).
    struct SingleRowCheck {
        const char* file;
        const char* label;
    };
    static const SingleRowCheck kSingleRowStuntTables[] = {
        {"stunt_drifting.xtbl", "Stunt_Drifting"},       {"stunt_hijacking.xtbl", "Stunt_Hijacking"},
        {"stunt_near_miss.xtbl", "Near_Miss"},            {"stunt_back_seat_driver.xtbl", "Back_Seat_Driver"},
        {"stunt_oncoming.xtbl", "Stunt_Oncoming"},        {"stunt_peel_out.xtbl", "Peel_Out"},
        {"stunt_low_flying.xtbl", "Low_Flying"},          {"barnstorming.xtbl", "Barnstorming"},
        {"exploration_diversion.xtbl", "Exploration"},    {"mugging_diversion.xtbl", "Mugging_Diversion"},
    };
    for (const auto& s : kSingleRowStuntTables) {
        const sr3xtbl::Document* doc = firstDoc(s.file);
        if (!doc) {
            std::printf("--- %s: no decoded copy to parse ---\n", s.file);
            continue;
        }
        const sr3xtbl::Node* row = sr3xtbl::FindChild(doc->table(), s.label);
        std::printf("--- %s ---\n", s.file);
        GATE(row != nullptr, "row element <%s> present (spec: every table in this group is a single row)", s.label);
    }

    if (const sr3xtbl::Document* doc = firstDoc("stunt_wheels.xtbl")) {
        std::printf("--- stunt_wheels.xtbl ---\n");
        std::optional<sd::StuntWheels> sw = sd::ParseStuntWheelsTable(*doc);
        GATE(sw.has_value(), "row Stunt_Wheels present");
        if (sw) {
            std::printf("  Two_Wheels.Max_Respect=%d Wheelie.Max_Respect=%d Stoppie.Max_Respect=%d\n",
                        sw->twoWheels.reward.maxRespect.value, sw->wheelie.reward.maxRespect.value,
                        sw->stoppie.reward.maxRespect.value);
            GATE(sw->twoWheels.reward.maxRespect.present && sw->wheelie.reward.maxRespect.present &&
                     sw->stoppie.reward.maxRespect.present,
                 "all 3 nested sub-stunt reward triads present (spec 2.8: 'nested three times')");
        }
    }

    if (const sr3xtbl::Document* doc = firstDoc("stunt_jumping_diversion.xtbl")) {
        std::printf("--- stunt_jumping_diversion.xtbl ---\n");
        std::optional<sd::StuntJumpingDiversion> sj = sd::ParseStuntJumpingDiversionTable(*doc);
        GATE(sj.has_value(), "row Stunt_Jumping present");
        if (sj) {
            std::printf("  Vehicle_Types found: %zu\n", sj->vehicleTypes.size());
            GATE(sj->vehicleTypes.size() == 3, "Vehicle_Types count == 3 (spec 2.9/14: CONFIRMED capacity exactly 3)");
            for (const auto& vt : sj->vehicleTypes)
                std::printf("    Vehicle_Type name=%s\n", vt.name.value_or("<absent>").c_str());
        }
    }

    if (const sr3xtbl::Document* doc = firstDoc("base_jumping.xtbl")) {
        std::printf("--- base_jumping.xtbl ---\n");
        std::optional<sd::BaseJumping> bj = sd::ParseBaseJumpingTable(*doc);
        GATE(bj.has_value(), "row Base_Jumping present");
        if (bj) {
            GATE(bj->genInfoPresent && bj->targetInfoPresent && bj->vehInfoPresent && bj->rewardInfoPresent,
                 "all 4 sub-blocks present (spec 4: real row has Gen_Info/Target_Info/Veh_Info/Reward_Info all present)");
            const size_t n = bj->rewardInfo.rewardTiers.size();
            std::printf("  Reward_Tier rows found: %zu\n", n);
            GATE(n == 9, "Reward_Tier count == 9 (spec 4/14: real row has 9, strictly descending Max_Distance)");
            bool descending = true;
            for (size_t i = 1; i < bj->rewardInfo.rewardTiers.size(); ++i)
                if (bj->rewardInfo.rewardTiers[i].maxDistance.value >= bj->rewardInfo.rewardTiers[i - 1].maxDistance.value)
                    descending = false;
            GATE(descending, "Reward_Tier rows are in strictly descending Max_Distance order (spec 4/14)");
        }
    }

    if (const sr3xtbl::Document* doc = firstDoc("cat_and_mouse.xtbl")) {
        std::printf("--- cat_and_mouse.xtbl ---\n");
        std::optional<sd::CatAndMouse> cm = sd::ParseCatAndMouseTable(*doc);
        GATE(cm.has_value(), "row Cat_And_Mouse present");
        if (cm) {
            std::printf("  Vehicle_Matchup rows found: %zu\n", cm->vehicleMatchups.size());
            GATE(cm->vehicleMatchups.size() == 5, "Vehicle_Matchup count == 5 (spec 5/14: real row uses 5 of 8)");
        }
    }

    if (const sr3xtbl::Document* doc = firstDoc("streaking.xtbl")) {
        std::printf("--- streaking.xtbl ---\n");
        std::optional<sd::Streaking> st = sd::ParseStreakingTable(*doc);
        GATE(st.has_value(), "row Streaking present");
        if (st) {
            std::printf("  Level rows found: %zu\n", st->levels.size());
            GATE(st->levels.size() == 8, "Level count == 8 (spec 6.3/14: real row uses 8 of 10)");
        }
    }

    if (const sr3xtbl::Document* doc = firstDoc("photo_op.xtbl")) {
        std::printf("--- photo_op.xtbl ---\n");
        std::optional<sd::PhotoOp> po = sd::ParsePhotoOpTable(*doc);
        GATE(po.has_value(), "row Photo_Op present");
        if (po) {
            std::printf("  Saints_Loved: %zu Char_Preset, %zu Line_Situation\n", po->saintsLoved.charPresetNames.size(),
                        po->saintsLoved.lineSituationNames.size());
            std::printf("  Saints_Hated: %zu Char_Preset, %zu Line_Situation\n", po->saintsHated.charPresetNames.size(),
                        po->saintsHated.lineSituationNames.size());
            GATE(po->saintsLoved.charPresetNames.size() == 6 && po->saintsHated.charPresetNames.size() == 6,
                 "6 Char_Preset entries each in Saints_Loved/Saints_Hated (spec 7)");
            GATE(po->saintsLoved.lineSituationNames.size() == 1 && po->saintsHated.lineSituationNames.size() == 1,
                 "1 Line_Situation entry each (spec 7)");
        }
    }

    if (const sr3xtbl::Document* doc = firstDoc("escort_name_generator.xtbl")) {
        std::printf("--- escort_name_generator.xtbl ---\n");
        std::optional<sd::EscortNameGenerator> eg = sd::ParseEscortNameGeneratorTable(*doc);
        GATE(eg.has_value(), "row Name_Generator present");
        if (eg) {
            std::printf("  Naughty_name rows found: %zu\n", eg->naughtyNames.size());
            GATE(eg->naughtyNames.size() == 39, "Naughty_name count == 39 (spec 11/14: real row, cap 40)");
        }
    }

    if (const sr3xtbl::Document* doc = firstDoc("shop_names.xtbl")) {
        std::printf("--- shop_names.xtbl ---\n");
        std::vector<sd::ShopName> rows = sd::ParseShopNamesTable(*doc);
        std::printf("  Shop_Names rows found: %zu\n", rows.size());
        GATE(rows.size() == 41, "Shop_Names row count == 41 (spec 12/14)");
        long long shopTypeUnmatched = 0;
        for (const sd::ShopName& r : rows) {
            if (!r.shopType) continue;
            bool ok = false;
            for (std::string_view v : sd::kShopTypeNames)
                if (sr3xtbl::NameEquals(*r.shopType, v)) { ok = true; break; }
            if (!ok) ++shopTypeUnmatched;
        }
        GATE(shopTypeUnmatched == 0, "every Shop_Type value is one of the 8 enumerated choices (spec 12/14): %lld unmatched",
             shopTypeUnmatched);
    }

    // The 4 activity-text files (spec 9) share one shape. Spec 9 states the
    // row-name literal is "Escort" for THREE of them specifically
    // (escort_text/snatch_text/snatch_kinzie_text - "Escort in both
    // escort_text.xtbl and snatch_text.xtbl/snatch_kinzie_text.xtbl"); it
    // never claims fraud_text.xtbl shares that literal (that file is its
    // own activity, Insurance Fraud), so fraud_text is reported but NOT
    // gated on "Escort" - an earlier version of this harness over-generalised
    // that check to all 4 files and got a spurious FAIL on fraud_text (whose
    // real row name is "Fraud").
    static const char* kEscortNamedFiles[] = {"snatch_text.xtbl", "snatch_kinzie_text.xtbl", "escort_text.xtbl"};
    for (const char* file : kEscortNamedFiles) {
        const sr3xtbl::Document* doc = firstDoc(file);
        if (!doc) {
            std::printf("--- %s: no decoded copy to parse ---\n", file);
            continue;
        }
        std::printf("--- %s ---\n", file);
        std::optional<sd::ActivityTextTable> t = sd::ParseActivityTextTable(*doc);
        GATE(t.has_value(), "row <String> present (spec 9)");
        if (t) {
            std::printf("  row name=%s, Text rows found: %zu\n", t->rowName.value_or("<absent>").c_str(),
                        t->texts.size());
            GATE(t->rowName.has_value() && *t->rowName == "Escort",
                 "row-name literal == \"Escort\" (spec 9's specific empirical claim)");
            std::printf("  (spec 9 states ~20 Text rows for this file - reported, not gated)\n");
        }
    }
    if (const sr3xtbl::Document* doc = firstDoc("fraud_text.xtbl")) {
        std::printf("--- fraud_text.xtbl ---\n");
        std::optional<sd::ActivityTextTable> t = sd::ParseActivityTextTable(*doc);
        GATE(t.has_value(), "row <String> present (spec 9)");
        if (t)
            std::printf("  row name=%s (spec 9 does not claim this is \"Escort\" - it's its own activity), Text "
                        "rows found: %zu (reported, not gated)\n",
                        t->rowName.value_or("<absent>").c_str(), t->texts.size());
    }

    if (const sr3xtbl::Document* doc = firstDoc("horde_mode_text.xtbl")) {
        std::printf("--- horde_mode_text.xtbl ---\n");
        std::vector<sd::HordeModeIdentifier> rows = sd::ParseHordeModeTextTable(*doc);
        std::printf("  Horde_Mode_Identifier rows found: %zu\n", rows.size());
        GATE(rows.size() == 32, "Horde_Mode_Identifier count == 32 (spec 8.2 corrected 2026-09-30 to this project's measured 32, HANDOFF 9.94; was 28)");
    }

    if (const sr3xtbl::Document* doc = firstDoc("fraud_globals.xtbl")) {
        std::printf("--- fraud_globals.xtbl ---\n");
        std::optional<sd::FraudGlobals> fg = sd::ParseFraudGlobalsTable(*doc);
        GATE(fg.has_value(), "row Fraud_Values present");
        if (fg) {
            struct NamedCat {
                const char* name;
                const sd::FraudMultiplierCategory* c;
            };
            const NamedCat cats[] = {
                {"Vehicle", &fg->vehicle},     {"Airtime", &fg->airtime},         {"Witness", &fg->witness},
                {"Cop_Witness", &fg->copWitness}, {"Linear_Dist", &fg->linearDist}, {"Civil_Vehicle", &fg->civilVehicle},
                {"Crazy_Driver", &fg->crazyDriver}, {"Windshield", &fg->windshield}, {"Surfing", &fg->surfing},
                {"Pinball", &fg->pinball},     {"Cliff_Diver", &fg->cliffDiver},
            };
            int bothFormsPresent = 0;
            for (const auto& nc : cats) {
                if (nc.c->flatPresent && nc.c->tieredElementPresent) ++bothFormsPresent;
                std::printf("    %-14s flat=%s tiered=%s\n", nc.name, nc.c->flatPresent ? "present" : "absent",
                            nc.c->tieredElementPresent ? "present" : "absent");
            }
            std::printf("  of 11 modelled tiered-multiplier categories, %d have BOTH the old flat and new tiered form present\n",
                        bothFormsPresent);
            // spec 10 claims (verbatim) that the shipped row "populates
            // side-by-side" BOTH forms "for every one of the twelve
            // fraud-combo categories simultaneously". Real data (checked
            // here) does NOT bear that out: this gate expects the
            // EMPIRICALLY-OBSERVED 5/11 (Vehicle, Airtime, Linear_Dist,
            // Crazy_Driver, Windshield have BOTH; Surfing has ONLY the
            // tiered form, no flat; Witness/Cop_Witness/Civil_Vehicle/
            // Pinball/Cliff_Diver have ONLY the flat form) - documenting
            // spec 10's claim as CONTRADICTED by real data rather than
            // re-asserting it.
            GATE(bothFormsPresent == 5,
                 "5 of 11 categories carry both schemes side-by-side (CONTRADICTS spec 10's own blanket claim of "
                 "'every one of the twelve ... simultaneously' - see the task report and tables.h)");
            std::printf("  Luxury_Cars/Vehicles/Vehicle entries: %zu\n", fg->luxuryCars.size());
            std::printf("  Adrenaline_Grid tiers: %zu, Adrenaline_During_Grid tiers: %zu\n", fg->adrenalineGrid.size(),
                        fg->adrenalineDuringGrid.size());
        }
    }

    if (const sr3xtbl::Document* doc = firstDoc("horde_mode.xtbl")) {
        std::printf("--- horde_mode.xtbl ---\n");
        std::optional<sd::HordeMode> hm = sd::ParseHordeModeTable(*doc);
        GATE(hm.has_value(), "row Horde_Mode present");
        if (hm) {
            std::printf("  Levels: %zu, Player_Characters: %zu, Special_Spawn_Conditions: %zu, Powerups: %zu, "
                        "Enemy_Data.Point_Values: %zu, Point_Multipliers.Weapons: %zu\n",
                        hm->levels.size(), hm->playerCharacters.size(), hm->specialSpawnConditions.size(),
                        hm->powerups.size(), hm->enemyData.pointValues.size(), hm->pointMultipliers.weapons.size());
            GATE(hm->levels.size() == 3, "Levels count == 3 (spec 8.1)");
            GATE(hm->playerCharacters.size() == 5,
                 "Player_Characters count == 5 (spec 8.1/14: CONFIRMED capacity exactly 5, matches exactly)");
            GATE(hm->specialSpawnConditions.size() == 10, "Special_Spawn_Conditions count == 10 (spec 8.1)");
            GATE(hm->powerups.size() == 4, "Powerups count == 4, all enumerated types (spec 8.1/14)");
            long long powerupTypeUnmatched = 0;
            for (const auto& p : hm->powerups) {
                if (!p.type) continue;
                bool ok = false;
                for (std::string_view v : sd::kHordeModePowerupTypeNames)
                    if (sr3xtbl::NameEquals(*p.type, v)) { ok = true; break; }
                if (!ok) ++powerupTypeUnmatched;
            }
            GATE(powerupTypeUnmatched == 0, "every Powerup.Type value is one of the 4 enumerated choices: %lld unmatched",
                 powerupTypeUnmatched);
            GATE(hm->enemyData.pointValues.size() == 60, "Enemy_Data.Point_Values count == 60 (spec 8.1)");
            GATE(hm->pointMultipliers.weapons.size() == 20,
                 "Point_Multipliers.Weapons count == 20 (spec 8.1: '20 weapon Point_Multiplier rows')");
        }
    }

    std::printf("\n=== G5 parser smoke check ===\n");
    std::printf("parse attempts: %lld, FormatError failures: %lld\n", parseAttempts, parseFailures);
    if (parseAttempts > 0)
        GATE(parseFailures == 0, "every found, decoded copy of a target table parses without FormatError: %lld / %lld",
             parseAttempts - parseFailures, parseAttempts);

    std::printf("\n%s (%d gate failure%s)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
