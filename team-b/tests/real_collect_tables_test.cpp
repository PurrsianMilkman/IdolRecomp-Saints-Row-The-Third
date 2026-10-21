// sr3collect on the shipped tables (misc_tables.vpp_pc): collectibles.xtbl and
// sr3_city_challenges.xtbl against spec-tables-progression.md C.2/C.3 and the C.6 vectors.
// Exits 77 (ctest SKIPPED) without the game cache.
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3collect/challenges.h"
#include "sr3collect/collectibles.h"
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

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

bool findEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    return false;
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
    std::vector<uint8_t> col, chal;
    try {
        auto misc = vppcache::openArchive(dir + "/misc_tables.vpp_pc");
        if (!findEntry(misc->container, "collectibles.xtbl", col) ||
            !findEntry(misc->container, "sr3_city_challenges.xtbl", chal)) {
            std::cerr << "tables not found\n";
            return 1;
        }
    } catch (const std::exception& ex) {
        std::cout << "open failed - SKIPPED (" << ex.what() << ")\n";
        return 77;
    }
    using namespace sr3collect;
    const auto colDoc = sr3xtbl::ParseDocument(std::string_view(reinterpret_cast<const char*>(col.data()), col.size()));
    const auto defs = CollectibleDefsFromTable(sr3tables_progression::ParseCollectiblesTable(colDoc));
    const char* titles[] = {"DRUG_PACKAGE_COLLECTED", "MONEY_PALLET_COLLECTED", "SEX_DOLL_COLLECTED", "PHOTO_OP_COLLECTED"};
    for (int g = 0; g < 4; ++g) {
        CHECK(defs[g].title == titles[g] && defs[g].cash == 1000.0 && defs[g].respect == 0);
        CHECK(defs[g].thresholds.size() == 4);
        for (size_t k = 0; k < defs[g].thresholds.size(); ++k)
            CHECK(defs[g].thresholds[k].threshold == static_cast<int>(5 * (k + 1)) &&
                  defs[g].thresholds[k].cash == 1000.0 && !defs[g].thresholds[k].unlockable);
    }
    CollectibleRegistry reg;
    double cash = 0;
    for (int g = 0; g < 4; ++g)
        for (uint64_t h = 1; h <= 20; ++h) reg.registerHandle(g, (static_cast<uint64_t>(g) << 32) | h);
    for (int g = 0; g < 4; ++g)
        for (uint64_t h = 1; h <= 20; ++h) {
            const CollectResult r = reg.collect(g, (static_cast<uint64_t>(g) << 32) | h, defs);
            if (g == 1 && h == 5) CHECK(r.cash == 2000.0);  // vector 2
            cash += r.cash;
        }
    CHECK(cash == 96000.0);  // vector 5

    const auto cdoc = sr3xtbl::ParseDocument(std::string_view(reinterpret_cast<const char*>(chal.data()), chal.size()));
    const auto ch = ReadChallenges(cdoc);
    CHECK(ch.size() == 41);
    int respect = 0, kills = 0, destroys = 0, stats = 0, specials = 0, locked = 0;
    double money = 0;
    for (const ChallengeDef& c : ch) {
        respect += c.respect;
        money += c.cash;
        kills += c.trigger == ChallengeTrigger::Kill;
        destroys += c.trigger == ChallengeTrigger::Destroy;
        stats += c.trigger == ChallengeTrigger::Stat;
        specials += c.trigger == ChallengeTrigger::Special;
        locked += c.unlockName.has_value();
    }
    CHECK(respect == 21001 && money == 68001.0);  // vector 12 totals
    CHECK(kills == 10 && destroys == 5 && stats == 20 && specials == 6);
    std::cout << "challenges: " << ch.size() << " rows, " << locked << " with an unlock mission\n";
    int drive = -1, heads = -1, flash = -1;
    for (size_t i = 0; i < ch.size(); ++i) {
        if (ch[i].name == "Drive Distance") drive = static_cast<int>(i);
        if (ch[i].name == "Get Headshots") heads = static_cast<int>(i);
        if (ch[i].name == "Destroy Enemy Gang Operations") flash = static_cast<int>(i);
    }
    CHECK(drive >= 0 && heads >= 0 && flash >= 0);
    if (drive < 0 || heads < 0 || flash < 0) return 1;
    CHECK(ch[drive].goal == 762000.0 && ch[drive].displayUnit == ChallengeUnit::Miles);
    CHECK(std::fabs(ChallengeDisplayValue(ch[drive].displayUnit, ch[drive].goal) - 473.5) < 0.05);  // vector 10
    CHECK(ch[flash].respect == 1000 && ch[flash].cash == 3000.0 && ch[flash].goal == 32.0);

    ChallengeTracker t(ch);
    ChallengeKill head{"Brotherhood", true, "Pistol", "Pistol", "Headshot"};
    CHECK(t.kill(head).empty());  // vector 9: locked before m03
    t.missionCompleted("m03");
    bool done = false;
    for (int i = 0; i < 500; ++i)
        for (const ChallengeUpdate& u : t.kill(head)) done = done || (u.index == heads && u.completedNow);
    CHECK(done);  // vector 8
    bool driven = false;
    for (const ChallengeUpdate& u : t.statChanged("distance by vehicle", 762000.0))
        driven = driven || (u.index == drive && u.completedNow);
    CHECK(driven);  // vector 10
    t.unlockAll();
    for (int i = 0; i < 32; ++i) t.special("Flashpoints Defeated");
    CHECK(t.states()[flash].completed);  // vector 11

    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " checks FAILED.\n";
        return 1;
    }
    std::cout << g_checks << " checks passed.\n";
    return 0;
#endif
}
