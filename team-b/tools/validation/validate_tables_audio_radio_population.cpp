// Population / headline-figure validation for sr3tables_audio_radio
// (include/sr3tables_audio_radio/, src/tables_audio_radio.cpp) over the REAL
// shipped game archives. NOT wired into CMakeLists.txt - a standalone
// diagnostic, built and run by hand.
//
// spec-tables-audio-radio.md's own §2.4/§3-§17/§19 already state exactly
// what real base-game rows should look like (extracted from
// misc_tables.vpp_pc by the spec's own agent AO). This harness independently
// re-extracts the same 16 tables from whatever real archives are passed on
// argv using this project's OWN typed reader (never the spec's own
// tooling), and reports:
//
//   G1  LOCATE: every archive location a target filename is found at - not
//       just the first. A past bug in this project undercounted a table
//       that exists in more than one archive (e.g. a patch archive
//       superseding a base one); packfiles/pc/cache/ ships BOTH
//       patch_compressed.vpp_pc and patch_uncompressed.vpp_pc alongside
//       misc_tables.vpp_pc, so this is a live risk for this exact group,
//       not a hypothetical one. Every copy found, in every archive, is
//       parsed and reported; rows from every found copy are merged before
//       computing the headline counts below (matching how the real engine
//       itself layers a later archive's entries over an earlier one, per
//       spec-vpp-container.md).
//   G2  ROW COUNTS vs spec §19's own validation-summary table.
//   G3  spec's own PER-TABLE headline empirical claims, reproduced against
//       whatever this pass actually finds - most notably §5.1's "striking
//       capacity mismatch" for audio_line_tags.xtbl (4,507/4,626 rows past
//       the loader's real 119-entry effective cap), reproduced exactly, not
//       just gestured at.
//   G4  CROSS-TABLE checks this project's own typed helpers make possible:
//       commercials.xtbl <-> commercial_events.xtbl resolution (spec 16.1),
//       voc_sb_line_sit.xtbl <-> audio_banks.xtbl Soundbank/Name match
//       (spec 17.1), playlist_artist_track.xtbl WWise_ID uniqueness (spec
//       12.3).
//   G5  parser-level smoke check: every found, decoded copy of a target
//       table parses without sr3xtbl::FormatError.
//
// Usage: validate_tables_audio_radio_population <archive.vpp_pc> [...]
//   Pass every archive under packfiles/pc/cache/*.vpp_pc - the harness does
//   not hardcode a cache path (project HARD RULES). Include
//   patch_compressed.vpp_pc / patch_uncompressed.vpp_pc: they are exactly
//   the "superseding archive" case G1's own banner warns about.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3tables_audio_radio/tables.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using sr3xtbl::Node;
namespace tar = sr3tables_audio_radio;

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
// tools/validation/validate_xtbl_population.cpp / validate_tables_environment
// _population.cpp's own G1: every entry ending in "xtbl", raw or compressed,
// through every nested container, in every archive given on argv).
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
    bool dlcArchive;  // top-level archive name starts with "dlc" (case-insensitive)
};

// Every archive location a canonical filename is found at - not just the
// first (see the file banner's G1). Also matches the dlc1_/dlc2_/dlc3_
// framework-prefix convention several tables in this group use (spec
// 2.1/6/10) - harmless for the tables that have no such convention, since it
// simply never matches.
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

std::vector<sr3xtbl::Document> g_keepAlive;  // Node* point into these; kept alive for the whole run
long long g_parseAttempts = 0, g_parseFailures = 0;

// Locates every copy of `canonical`, reports each location (G1), parses
// every successfully-decoded copy, and returns the merged rows across every
// found copy (matching how a later-loaded archive layers over an earlier
// one). `foundOut`, if non-null, collects every match (for callers that need
// the raw Found list too, e.g. the single-block tables).
template <class RowT>
std::vector<RowT> locateParseMerge(const std::string& canonical, std::vector<RowT> (*parseTable)(const sr3xtbl::Document&),
                                    std::vector<Found>* foundOut = nullptr) {
    std::printf("--- %s ---\n", canonical.c_str());
    std::vector<Found> matches = findMatches(canonical);
    if (matches.empty()) {
        std::printf("  NOT FOUND in any given archive.\n");
        return {};
    }
    bool anyBase = false, anyDlc = false;
    for (const Found& f : matches) {
        std::printf("  found: %s (in %s)  %s  status=%d\n", f.item->archiveChain.c_str(), f.item->topLevelArchive.c_str(),
                    f.dlcArchive ? "[DLC archive]" : "[BASE archive]", f.item->status);
        (f.dlcArchive ? anyDlc : anyBase) = true;
        if (foundOut) foundOut->push_back(f);
    }
    std::printf("  location summary: %zu location(s); %s%s%s\n", matches.size(), anyBase ? "BASE" : "",
                anyBase && anyDlc ? " + " : "", anyDlc ? "DLC" : "");

    std::vector<RowT> merged;
    for (const Found& f : matches) {
        if (f.item->status != 0) continue;
        ++g_parseAttempts;
        try {
            g_keepAlive.push_back(sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size()));
        } catch (const sr3xtbl::FormatError& e) {
            ++g_parseFailures;
            std::printf("  PARSE FAILED %s: %s\n", f.item->name.c_str(), e.what());
            continue;
        }
        std::vector<RowT> rows = parseTable(g_keepAlive.back());
        std::printf("  rows parsed from this copy: %zu\n", rows.size());
        merged.insert(merged.end(), rows.begin(), rows.end());
    }
    std::printf("  TOTAL merged rows (all found copies combined): %zu\n", merged.size());
    return merged;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        std::printf("usage: validate_tables_audio_radio_population <archive.vpp_pc> [...]\n");
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
    std::printf("=== G1 setup ===\n");
    std::printf("archives given: %zu, containers walked: %lld, xtbl-family entries seen: %zu\n", archives.size(), g_containers,
                g_items.size());
    GATE(g_containers > 0, "CONTROL: at least one container was actually opened (not a silent no-op run)");

    // ===== 2. audio_banks.xtbl =====
    // Uses direct Node access (not just the typed reader) so the wwise_id GRAMMAR itself can be checked against
    // the RAW text, independent of ParseWwiseIdDigitPrefix() - re-running the same function under test would be
    // tautological (spec 2.4's own claim is about the raw shipped TEXT, not about this reader's output).
    std::vector<tar::AudioBank> banks;
    {
        std::vector<Found> matches;
        std::vector<tar::AudioBank> merged = locateParseMerge<tar::AudioBank>("audio_banks.xtbl", tar::ParseAudioBanksTable, &matches);
        banks = merged;
        long long haveName = 0, haveId = 0, initCount = 0, initCountCi = 0, loadAtBootTrue = 0, loadAtBootFalse = 0;
        long long streamingTrue = 0, streamingFalse = 0;
        for (const tar::AudioBank& b : banks) {
            if (b.name) ++haveName;
            if (b.wwiseId.present) ++haveId;
            if (b.IsInit()) ++initCount;
            if (b.name && sr3xtbl::NameEquals(*b.name, "Init")) ++initCountCi;
            if (b.LoadAtBootOrDefault()) ++loadAtBootTrue; else ++loadAtBootFalse;
            if (b.StreamingOrDefault()) ++streamingTrue; else ++streamingFalse;
        }
        // Raw-text grammar check (spec 1.3 item 1 / 2.4): a wwise_id value "matches the grammar" cleanly when its
        // raw text is either all-ASCII-digits, or all-ASCII-digits followed by exactly one '.' with nothing (or
        // more digits, which the grammar ignores) after - i.e. the character immediately after the longest
        // leading digit run is '\0' or '.'. Anything else (a leading '-', a letter before any digit run ends,
        // etc.) is a real GRAMMAR EXCEPTION worth flagging even though ParseWwiseIdDigitPrefix() still returns a
        // defined (0) result for it.
        long long grammarExceptions = 0, wwiseIdTextsSeen = 0;
        for (const Found& f : matches) {
            if (f.item->status != 0) continue;
            sr3xtbl::Document doc;
            try {
                doc = sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size());
            } catch (const sr3xtbl::FormatError&) {
                continue;
            }
            for (const Node* row : sr3xtbl::Children(doc.table(), "NewEntity")) {
                const std::string* t = sr3xtbl::ChildText(row, "wwise_id");
                if (!t) continue;
                ++wwiseIdTextsSeen;
                size_t i = 0;
                while (i < t->size() && (*t)[i] >= '0' && (*t)[i] <= '9') ++i;
                const char stop = (i < t->size()) ? (*t)[i] : '\0';
                if (i == 0 || (stop != '\0' && stop != '.')) ++grammarExceptions;
            }
        }
        std::printf("  Name present: %lld/%zu, wwise_id present: %lld/%zu (spec 2.4: 496/496 both)\n", haveName, banks.size(), haveId, banks.size());
        std::printf("  Name==\"Init\" (case-sensitive): %lld; case-insensitive count: %lld (spec 2.4: exactly 1, no near-miss casing)\n", initCount, initCountCi);
        std::printf("  load_at_boot: True=%lld False=%lld (spec 2.4 base file: True=23 False=473)\n", loadAtBootTrue, loadAtBootFalse);
        std::printf("  streaming:    True=%lld False=%lld (spec 2.4 base file: True=494 False=2)\n", streamingTrue, streamingFalse);
        std::printf("  wwise_id raw-text grammar exceptions: %lld/%lld (spec 2.4: 0/496 exceptions)\n", grammarExceptions, wwiseIdTextsSeen);
        GATE(banks.size() >= 496, "row count >= spec's base-file 496 (merged across every found copy): %zu", banks.size());
        GATE(initCount == initCountCi, "CONTROL: case-sensitive and case-insensitive \"Init\" counts agree (spec 2.4: no near-miss casing)");
        GATE(grammarExceptions == 0, "REPRODUCES spec 2.4: 0 wwise_id raw-text values are a grammar exception");
    }

    // ===== 3. audio_constants.xtbl (single-row globals) =====
    {
        std::printf("--- audio_constants.xtbl ---\n");
        std::vector<Found> matches = findMatches("audio_constants.xtbl");
        if (matches.empty()) {
            std::printf("  NOT FOUND in any given archive.\n");
        } else {
            for (const Found& f : matches)
                std::printf("  found: %s (in %s) status=%d\n", f.item->archiveChain.c_str(), f.item->topLevelArchive.c_str(), f.item->status);
            bool any = false;
            for (const Found& f : matches) {
                if (f.item->status != 0) continue;
                ++g_parseAttempts;
                try {
                    g_keepAlive.push_back(sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size()));
                } catch (const sr3xtbl::FormatError&) {
                    ++g_parseFailures;
                    continue;
                }
                std::optional<tar::AudioConstants> ac = tar::ParseAudioConstants(g_keepAlive.back());
                if (ac) {
                    any = true;
                    // spec-tables-audio-radio.md §3: PlayTimers has 12 values (count corrected from 11 by desk review
                    // 2026-09-30); real-data leaf presence was counted only for these two (spec §3 validation note).
                    std::printf("  PlayTimers.BrassCollision=%g VehicleImpactDistance=%g LargeDeformation=%g (spec 3: 200 / 12 / 1000)\n",
                                ac->playTimerBrassCollision.value, ac->playTimerVehicleImpactDistance.value, ac->playTimerLargeDeformation.value);
                    GATE(ac->playTimerBrassCollision.present && ac->playTimerLargeDeformation.present,
                         "AudioConstants sub-blocks present and readable");
                }
            }
            GATE(any, "at least one copy of audio_constants.xtbl produced a present <AudioConstants> block");
        }
    }

    // ===== 4. audio_settings.xtbl (single-row globals) =====
    {
        std::printf("--- audio_settings.xtbl ---\n");
        std::vector<Found> matches = findMatches("audio_settings.xtbl");
        if (matches.empty()) {
            std::printf("  NOT FOUND in any given archive.\n");
        } else {
            for (const Found& f : matches)
                std::printf("  found: %s (in %s) status=%d\n", f.item->archiveChain.c_str(), f.item->topLevelArchive.c_str(), f.item->status);
            for (const Found& f : matches) {
                if (f.item->status != 0) continue;
                ++g_parseAttempts;
                try {
                    g_keepAlive.push_back(sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size()));
                } catch (const sr3xtbl::FormatError&) {
                    ++g_parseFailures;
                    continue;
                }
                std::optional<tar::AudioSettings> as = tar::ParseAudioSettings(g_keepAlive.back());
                if (as) {
                    std::printf("  Speed_of_sound present=%s (spec 4.1: GENUINELY absent in the base row -> coded default 343.5 applies) value(or default)=%g\n",
                                as->speedOfSound.has_value() ? "yes" : "no", as->SpeedOfSoundOrDefault());
                    std::printf("  Health_adjust_rate=%s Doppler_multiplier=%s (spec 4.1: 0.5 / 11.0)\n",
                                as->healthAdjustRate.has_value() ? std::to_string(*as->healthAdjustRate).c_str() : "(absent)",
                                as->dopplerMultiplier.has_value() ? std::to_string(*as->dopplerMultiplier).c_str() : "(absent)");
                    GATE(!as->speedOfSound.has_value(), "reproduces spec 4.1: Speed_of_sound is genuinely absent from the shipped row");
                }
            }
        }
    }

    // ===== 5. audio_line_tags.xtbl - the capacity mismatch =====
    std::vector<tar::AudioLineTag> lineTags = locateParseMerge<tar::AudioLineTag>("audio_line_tags.xtbl", tar::ParseAudioLineTagsTable);
    if (!lineTags.empty()) {
        // spec 5: the loop breaks once the stored count EXCEEDS 0x76 (118) - i.e. after storing index 118
        // (the 119th entry, 0-based), an effective cap of 119 out of a 120-slot allocation.
        // LABEL: spec-tables-audio-radio.md §5, [OPEN - desk review 2026-09-30]: the 119-cap break condition is
        // not settled against the executable (119 vs 120); the 4,507/4,626 figure below is spec-derived
        // arithmetic, not an observation of the cap.
        constexpr size_t kEffectiveCap = 119;
        const size_t reachable = lineTags.size() < kEffectiveCap ? lineTags.size() : kEffectiveCap;
        const size_t unreachable = lineTags.size() - reachable;
        std::printf("  real row count: %zu; loader's effective cap: %zu; UNREACHABLE rows: %zu (%.1f%%)\n", lineTags.size(),
                    kEffectiveCap, unreachable, lineTags.size() ? 100.0 * static_cast<double>(unreachable) / static_cast<double>(lineTags.size()) : 0.0);
        std::printf("  spec 5.1's own figure: 4,626 real rows, 4,507 (97%%) unreachable past the 119-entry cap\n");
        GATE(lineTags.size() == 4626, "row count matches spec 5.1's cited 4,626 exactly: %zu", lineTags.size());
        GATE(unreachable == 4507, "REPRODUCES spec 5.1's own 'striking capacity mismatch' figure exactly: %zu unreachable", unreachable);
    }

    // ===== 6. audio_personas.xtbl =====
    std::vector<tar::AudioPersona> personas = locateParseMerge<tar::AudioPersona>("audio_personas.xtbl", tar::ParseAudioPersonasTable);
    if (!personas.empty()) {
        long long suffixMatch = 0, ageMatch = 0;
        std::map<std::string, long long> suffixCounts, ageCounts;
        std::set<uint32_t> seenWwiseIds;
        long long duplicateWwiseIds = 0;
        for (const tar::AudioPersona& p : personas) {
            const int g = p.DeriveGenderFromName();
            const int e = p.DeriveEthnicityFromName();
            if (g != 0 || e != 0) {
                ++suffixMatch;
                for (size_t i = 0; i < tar::kAudioPersonaDemographicSuffixes.size(); ++i) {
                    const int wantG = (static_cast<int>(i) % 2 == 0) ? 1 : 2;
                    const int wantE = static_cast<int>(i / 2) + 1;
                    if (g == wantG && e == wantE) ++suffixCounts[std::string(tar::kAudioPersonaDemographicSuffixes[i])];
                }
            }
            const int age = p.DeriveAgeFromName();
            if (age != 0) {
                ++ageMatch;
                ++ageCounts[std::string(tar::kAudioPersonaAgeTokens[static_cast<size_t>(age - 1)])];
            }
            if (p.wwiseId) {
                if (!seenWwiseIds.insert(*p.wwiseId).second) ++duplicateWwiseIds;
            }
        }
        // LABEL: spec-tables-audio-radio.md §6, [OPEN - desk review 2026-09-30]: case sensitivity of the
        // suffix-match helper FUN_00EA48B0 is OPEN (never decompiled); our matcher is case-sensitive by assumption.
        std::printf("  %zu rows; demographic-suffix matches: %lld (spec 6.1: 233/265, 88%%); age-token matches: %lld (spec 6.1: 84/265, 32%%)\n",
                    personas.size(), suffixMatch, ageMatch);
        for (auto& kv : suffixCounts) std::printf("    suffix %-4s x%lld\n", kv.first.c_str(), kv.second);
        for (auto& kv : ageCounts) std::printf("    age token %-8s x%lld\n", kv.first.c_str(), kv.second);
        std::printf("  duplicate wwise_id occurrences (loader dedups these, NOT enforced by this reader - see the header banner): %lld\n",
                    duplicateWwiseIds);
        GATE(personas.size() >= 265, "row count >= spec 6.1's base-file 265: %zu", personas.size());
    }

    // ===== 7. persona_radio_prefs.xtbl =====
    std::vector<tar::PersonaRadioPref> prefs = locateParseMerge<tar::PersonaRadioPref>("persona_radio_prefs.xtbl", tar::ParsePersonaRadioPrefsTable);
    if (!prefs.empty()) {
        long long bothPresent = 0;
        std::map<std::string, long long> stationCounts;
        for (const tar::PersonaRadioPref& p : prefs) {
            if (p.name && p.radioStation) { ++bothPresent; ++stationCounts[*p.radioStation]; }
        }
        std::printf("  %zu rows, both Name+Radio_Station present: %lld (spec 7.1: 242/242)\n", prefs.size(), bothPresent);
        GATE(bothPresent == static_cast<long long>(prefs.size()), "every row has both Name and Radio_Station (spec 7.1: 242/242 all)");
    }

    // ===== 8. foley_collision.xtbl =====
    std::vector<tar::FoleyCollision> foleyCollisions = locateParseMerge<tar::FoleyCollision>("foley_collision.xtbl", tar::ParseFoleyCollisionTable);
    if (!foleyCollisions.empty()) {
        long long full = 0;
        for (const tar::FoleyCollision& f : foleyCollisions)
            if (f.name && f.frequency && f.wwiseSwitch && f.minimumSpeedRaw.present && f.maximumSpeedRaw.present) ++full;
        std::printf("  %zu rows, all-fields-present: %lld (spec 8: 55/55)\n", foleyCollisions.size(), full);
        GATE(full == static_cast<long long>(foleyCollisions.size()), "every row has every field present (spec 8: 55/55 100%% coverage)");
    }

    // ===== 9. foley_touch.xtbl =====
    std::vector<tar::FoleyTouch> foleyTouches = locateParseMerge<tar::FoleyTouch>("foley_touch.xtbl", tar::ParseFoleyTouchTable);
    if (!foleyTouches.empty()) {
        long long full = 0;
        for (const tar::FoleyTouch& f : foleyTouches)
            if (f.name && f.frequency && f.wwiseSwitch) ++full;
        std::printf("  %zu rows, all-fields-present: %lld (spec 9: 8/8)\n", foleyTouches.size(), full);
        GATE(full == static_cast<long long>(foleyTouches.size()), "every row has every field present (spec 9: 8/8)");
    }

    // ===== 10. foley_engine.xtbl =====
    std::vector<tar::FoleyEngine> foleyEngines = locateParseMerge<tar::FoleyEngine>("foley_engine.xtbl", tar::ParseFoleyEngineTable);
    if (!foleyEngines.empty()) {
        long long haveVehicleModel = 0, haveNpcOnly = 0, haveDlcId = 0;
        for (const tar::FoleyEngine& f : foleyEngines) {
            if (f.vehicleModel) ++haveVehicleModel;
            if (f.npcOnly) ++haveNpcOnly;
            if (f.dlcFrameworkId) ++haveDlcId;
        }
        std::printf("  %zu rows; Vehicle_Model present: %lld (spec 10: 84/84); NPC_Only used: %lld (spec 10: 0/84); dlc_framework_id used: %lld (spec 10: 0/84 in base game)\n",
                    foleyEngines.size(), haveVehicleModel, haveNpcOnly, haveDlcId);
    }

    // ===== 11. radio_stations.xtbl (single settings row + nested Info list) =====
    std::vector<tar::RadioStationInfo> allStations;
    {
        std::printf("--- radio_stations.xtbl ---\n");
        std::vector<Found> matches = findMatches("radio_stations.xtbl");
        if (matches.empty()) {
            std::printf("  NOT FOUND in any given archive.\n");
        } else {
            for (const Found& f : matches)
                std::printf("  found: %s (in %s) status=%d\n", f.item->archiveChain.c_str(), f.item->topLevelArchive.c_str(), f.item->status);
            for (const Found& f : matches) {
                if (f.item->status != 0) continue;
                ++g_parseAttempts;
                try {
                    g_keepAlive.push_back(sr3xtbl::ParseDocument(f.item->data.data(), f.item->data.size()));
                } catch (const sr3xtbl::FormatError&) {
                    ++g_parseFailures;
                    continue;
                }
                std::optional<tar::RadioSettings> rs = tar::ParseRadioSettings(g_keepAlive.back());
                if (rs) allStations.insert(allStations.end(), rs->stations.begin(), rs->stations.end());
            }
        }
        if (!allStations.empty()) {
            long long selectable = 0, police = 0, fbi = 0, news = 0, haveWwiseId = 0, haveFilename = 0;
            for (const tar::RadioStationInfo& s : allStations) {
                if (s.selectable) ++selectable;
                if (s.policeStation) ++police;
                if (s.fbiStation) ++fbi;
                if (s.newsStation) ++news;
                if (s.wwiseId) ++haveWwiseId;
                if (s.filename) ++haveFilename;
            }
            std::printf("  %zu Info rows (-> %zu total station slots incl. hard-coded station 0). selectable=%lld police=%lld fbi=%lld news=%lld\n",
                        allStations.size(), allStations.size() + 1, selectable, police, fbi, news);
            std::printf("  spec 11.3: 11 Info rows -> 12 slots; selectable=9 police=0 fbi=0 news=1; wwise_id 9/11; filename 11/11\n");
            std::printf("  wwise_id present: %lld/%zu; filename present: %lld/%zu\n", haveWwiseId, allStations.size(), haveFilename, allStations.size());
            GATE(allStations.size() >= 11, "Info row count >= spec 11.3's base-file 11: %zu", allStations.size());
        }
    }

    // ===== 12. playlist_artist_track.xtbl =====
    std::vector<tar::PlaylistTrack> tracks = locateParseMerge<tar::PlaylistTrack>("playlist_artist_track.xtbl", tar::ParsePlaylistArtistTrackTable);
    if (!tracks.empty()) {
        std::set<uint32_t> seen;
        long long duplicates = 0, haveId = 0;
        for (const tar::PlaylistTrack& t : tracks) {
            if (t.wwiseId) {
                ++haveId;
                if (!seen.insert(*t.wwiseId).second) ++duplicates;
            }
        }
        std::printf("  %zu rows, WWise_ID present: %lld, duplicate WWise_ID values: %lld (spec 12.3: 138 rows, 0 duplicates)\n",
                    tracks.size(), haveId, duplicates);
        GATE(duplicates == 0, "0 duplicate WWise_ID values (spec 12.3's own empirical claim)");
        GATE(tracks.size() <= 145, "row count stays under the loader's own 145-row cap (spec 12): %zu", tracks.size());
    }

    // ===== 13. radio_activities.xtbl =====
    std::vector<tar::RadioActivity> activities = locateParseMerge<tar::RadioActivity>("radio_activities.xtbl", tar::ParseRadioActivitiesTable);
    if (!activities.empty()) {
        std::vector<uint32_t> levels;
        for (const tar::RadioActivity& a : activities)
            if (a.level) levels.push_back(*a.level);
        std::sort(levels.begin(), levels.end());
        std::string levelsStr;
        for (uint32_t lv : levels) levelsStr += std::to_string(lv) + " ";
        std::printf("  %zu rows; Level values: %s(spec 13.1: exactly 8 rows, Levels 1..8)\n", activities.size(), levelsStr.c_str());
        GATE(activities.size() == 8, "REPRODUCES spec 13.1: exactly 8 rows against the loader's unchecked 8-slot bound: %zu", activities.size());
    }

    // ===== 14. radio_events.xtbl =====
    std::vector<tar::RadioEvent> events = locateParseMerge<tar::RadioEvent>("radio_events.xtbl", tar::ParseRadioEventsTable);
    if (!events.empty()) {
        std::map<std::string, long long> typeCounts;
        long long full = 0;
        for (const tar::RadioEvent& e : events) {
            if (e.eventType) ++typeCounts[*e.eventType];
            if (e.name && e.postTime && e.maxTimesPlayed) ++full;
        }
        std::printf("  %zu rows, all-fields-present: %lld (spec 14.1: 13/13). EventType distribution:\n", events.size(), full);
        for (auto& kv : typeCounts) std::printf("    %-12s x%lld\n", kv.first.c_str(), kv.second);
        std::printf("  spec 14.1: all 13 real rows use EventType=News, zero occurrences of the other 4 enum values\n");
        GATE(typeCounts.size() == 1 && typeCounts.count("News") == 1, "REPRODUCES spec 14.1: only \"News\" occurs among real EventType values");
    }

    // ===== 15. commercial_events.xtbl =====
    std::vector<tar::CommercialEvent> commercialEvents = locateParseMerge<tar::CommercialEvent>("commercial_events.xtbl", tar::ParseCommercialEventsTable);
    if (!commercialEvents.empty()) {
        std::set<int32_t> values;
        long long duplicates = 0, over30 = 0;
        for (const tar::CommercialEvent& e : commercialEvents) {
            if (!e.eventValue.present) continue;
            if (e.eventValue.value >= 30) ++over30;
            if (!values.insert(e.eventValue.value).second) ++duplicates;
        }
        std::string valuesStr;
        for (int32_t v : values) valuesStr += std::to_string(v) + " ";
        std::printf("  %zu rows; EventValue set: { %s} (spec 15.1: {1,2,6,10,13,14,16,17,21,23,24,25,26,27,28})\n", commercialEvents.size(), valuesStr.c_str());
        GATE(commercialEvents.size() == 15, "row count matches spec 15.1's cited 15 exactly: %zu", commercialEvents.size());
        GATE(duplicates == 0, "0 duplicate EventValue slots (spec 15.1)");
        GATE(over30 == 0, "0 EventValue >= 30 in real data (the loader would silently DROP them, spec 15)");
        const bool hasFlags1_2_6_27 = values.count(1) && values.count(2) && values.count(6) && values.count(27);
        GATE(hasFlags1_2_6_27, "REPRODUCES the independent cross-check with spec-save-format.md's own 'new game has flags 1, 2, 6, 27 set' claim (spec 15.1)");
    }

    // ===== 16. commercials.xtbl (cross-referenced against commercial_events.xtbl) =====
    std::vector<tar::Commercial> commercials = locateParseMerge<tar::Commercial>("commercials.xtbl", tar::ParseCommercialsTable);
    if (!commercials.empty()) {
        long long enabled = 0, disabled = 0, haveEnableEvent = 0, enableResolves = 0, haveDisableEvent = 0, disableResolves = 0;
        for (const tar::Commercial& c : commercials) {
            if (c.EnabledOrDefault()) ++enabled; else ++disabled;
            if (c.enableEvent) {
                ++haveEnableEvent;
                if (tar::ResolveCommercialEventValue(*c.enableEvent, commercialEvents) != -1) ++enableResolves;
            }
            if (c.disableEvent) {
                ++haveDisableEvent;
                if (tar::ResolveCommercialEventValue(*c.disableEvent, commercialEvents) != -1) ++disableResolves;
            }
        }
        std::printf("  %zu rows; InitialState Enabled=%lld Disabled=%lld (spec 16.1: 48/33)\n", commercials.size(), enabled, disabled);
        std::printf("  EnableEvent present: %lld, resolves against commercial_events.xtbl: %lld/%lld (spec 16.1: 33/33 clean match)\n",
                    haveEnableEvent, enableResolves, haveEnableEvent);
        std::printf("  DisableEvent present: %lld, resolves: %lld/%lld (spec 16.1: 28/28 clean match)\n", haveDisableEvent, disableResolves, haveDisableEvent);
        GATE(commercialEvents.empty() || enableResolves == haveEnableEvent, "REPRODUCES spec 16.1: every EnableEvent resolves against commercial_events.xtbl (%lld/%lld)", enableResolves, haveEnableEvent);
        GATE(commercialEvents.empty() || disableResolves == haveDisableEvent, "REPRODUCES spec 16.1: every DisableEvent resolves against commercial_events.xtbl (%lld/%lld)", disableResolves, haveDisableEvent);
    }

    // ===== 17. voc_sb_line_sit.xtbl (cross-referenced against audio_banks.xtbl) =====
    std::vector<tar::VocSbLineSit> vocEntries = locateParseMerge<tar::VocSbLineSit>("voc_sb_line_sit.xtbl", tar::ParseVocSbLineSitTable);
    if (!vocEntries.empty()) {
        std::set<std::string> bankNames;
        for (const tar::AudioBank& b : banks)
            if (b.name) bankNames.insert(*b.name);
        long long full = 0, distinctSoundbanks = 0, matches = 0;
        std::set<std::string> seenSoundbanks;
        for (const tar::VocSbLineSit& e : vocEntries) {
            if (e.personaId && e.soundbank && e.numLineSituations) ++full;
            if (e.soundbank) {
                if (seenSoundbanks.insert(*e.soundbank).second) {
                    ++distinctSoundbanks;
                    if (bankNames.count(*e.soundbank)) ++matches;
                }
            }
        }
        std::printf("  %zu rows, all-fields-present: %lld (spec 17.1: 265/265). Distinct Soundbank values: %lld, matching a real audio_banks.xtbl Name: %lld\n",
                    vocEntries.size(), full, distinctSoundbanks, matches);
        std::printf("  spec 17.1: 265 distinct Soundbank values, all 265 found in audio_banks.xtbl - a clean 100%% match\n");
        if (!banks.empty()) GATE(matches == distinctSoundbanks, "REPRODUCES spec 17.1: every distinct Soundbank value matches a real audio_banks.xtbl Name (%lld/%lld)", matches, distinctSoundbanks);
    }

    std::printf("=== G5 parser smoke check ===\n");
    std::printf("parse attempts: %lld, FormatError failures: %lld\n", g_parseAttempts, g_parseFailures);
    if (g_parseAttempts > 0)
        GATE(g_parseFailures == 0, "every found, decoded copy of a target table parses without FormatError: %lld / %lld", g_parseAttempts - g_parseFailures,
             g_parseAttempts);

    std::printf("\n%s (%d gate failure%s)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
