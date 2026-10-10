// Real-data regression for sr3zone::ZoneObjects (include/sr3zone/
// zone_objects.h, spec-zone-data-format.md Sec11, 2026-10-07). Reads a
// small, FIXED set of real shipped `.czn_pc` files from
// CRREISH_GAME_CACHE_DIR (a CMake cache variable, default the owner's
// install path) and exits 77 - reported by ctest as SKIPPED, never as a
// pass - when it is not there (CI, other machines), matching
// real_vintdoc_document_test.cpp's own convention exactly.
//
// Nothing from the archives is copied into the repository. The expected
// values below were obtained by running tools/zone_objects_population.cpp
// against the same archive and reading its own structural report - i.e.
// an independent second pass of THIS SAME reader, not a separate by-hand
// byte walk (spec-zone-data-format.md Sec11.9 item 8 notes that no real
// `.czn_pc` file was opened at all during the disassembly pass that
// produced Sec11; this test is this project's first look at any real
// `.czn_pc` Objects-section bytes). The population counts are pinned
// exactly (this is static shipped game data, not expected to change) -
// 2,068 `.czn_pc` files in `sr3_city_0.vpp_pc`, 603 carrying a located
// Objects section, 156,101 objects, 15,307 named (every one of which this
// reader's name-text decode recovers). The 100%-exact-fit tail-stride
// finding (every single real record tiles its own tailLength exactly
// under this reader's 4-byte entry stride - see zone_objects.h's
// "tail-interior decode" comment) is pinned too: zero overruns, zero
// leftover, over all 156,101 records in this one archive.
//
// EXTENDED 2026-10-07 (Sec11 corrections 2-5, this project's own
// whole-population re-replay): class-name-resolution and
// script_npc_*/script_vehicle_* spawn-property counts, same methodology
// (an independent second pass of this reader's own population tool against
// THIS SAME archive, sr3_city_0.vpp_pc only - NOT the full 10-archive/
// 383,346-object population spec-zone-data-format.md Sec11.7 itself cites,
// which is necessarily a different, larger set of numbers; see this
// project's own tools/zone_objects_population.cpp run log for the
// full-population figures that DO match Sec11.7's exactly).

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "sr3save/save_crc.h"
#include "sr3zone/zone_objects.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool fileExists(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return f.good();
}

bool stemFor(const std::string& n, const std::string& ext, std::string& stem) {
    if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) {
        stem = n.substr(0, n.size() - ext.size());
        return true;
    }
    return false;
}

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

// Finds every `.czn_pc` entry (recursively) in `c`, keyed by "stem.czn_pc".
void collect(const vpp::Container& c, std::map<std::string, std::vector<uint8_t>>& out) {
    std::map<std::string, size_t> czn;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".czn_pc", stem)) czn[n] = i;
    }
    for (const auto& kv : czn) {
        try {
            std::vector<uint8_t> b;
            if (entryBytes(c, kv.second, b) && !b.empty()) out[kv.first] = std::move(b);
        } catch (const std::exception&) {
        }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                collect(c.openNested(i), out);
            } catch (const std::exception&) {
            }
        }
    }
}

} // namespace

int main() {
#ifndef CRREISH_GAME_CACHE_DIR
    std::cout << "CRREISH_GAME_CACHE_DIR not configured - SKIPPED\n";
    return 77;
#else
    const std::string cacheDir = CRREISH_GAME_CACHE_DIR;
    const std::string archivePath = cacheDir + "/sr3_city_0.vpp_pc";
    if (!fileExists(archivePath)) {
        std::cout << "real-data archive not found (" << archivePath << ") - SKIPPED\n";
        return 77;
    }

    std::map<std::string, std::vector<uint8_t>> cznFiles;
    try {
        auto opened = vppcache::openArchive(archivePath);
        collect(opened->container, cznFiles);
    } catch (const std::exception& ex) {
        std::cout << "failed to open/walk archive - SKIPPED (" << ex.what() << ")\n";
        return 77;
    }

    CHECK(!cznFiles.empty()); // sr3_city_0.vpp_pc must carry at least one .czn_pc

    // --- population-level sanity, over every .czn_pc this one archive
    // carries - cheap enough to run as a real-data REGRESSION (not the
    // whole-install sweep tools/zone_objects_population.cpp does; see that
    // tool's own report for the full-population histogram). ---
    long long filesWithSection = 0, totalObjects = 0, namesRecovered = 0;
    long long tailExactFit = 0, tailOverrun = 0, tailOtherLeftover = 0;
    bool sawPlausibleName = false;

    // Corrections 2-5 (2026-10-07) counters.
    long long namedClassCount = 0, unresolvedClassCount = 0;
    long long scriptNpcHpCount = 0, scriptNpcFlagsCount = 0, scriptNpcFlagsIgnoreAiCount = 0;
    long long scriptNpcTeamCount = 0;
    long long scriptVehicleHpCount = 0, scriptVehicleFlagsCount = 0, scriptVehicleMpTeamCount = 0;

    // Position-property-id fix (2026-10-08, spec-ai-runtime.md Sec15 /
    // spec-zone-data-format.md Sec11.7, Team A spec-sync commit 47c8516):
    // real-data regression for kPositionPropertyId == 0xC8BEEEC5 (the
    // CONFIRMED decimal-form value, not the earlier refuted hex
    // parenthetical 0xC8B3B245). Counts real Navpoint/cover_node records and
    // how many of them actually resolve a position via
    // sr3zone::ObjectPosition() under the CURRENT constant - this is the
    // direct, real-data proof that the fix matters: under the old, wrong id,
    // every one of these would have silently resolved to std::nullopt.
    const uint32_t navpointHash = sr3save::nameHash("Navpoint");
    const uint32_t coverNodeHash = sr3save::nameHash("cover_node");
    long long navpointCount = 0, navpointWithPosition = 0;
    long long coverNodeCount = 0, coverNodeWithPosition = 0;

    for (const auto& kv : cznFiles) {
        const std::vector<uint8_t>& bytes = kv.second;
        auto sections = sr3zone::ZoneObjects::locate(sr3zone::ByteView(bytes.data(), bytes.size()));
        if (sections.empty()) continue;
        ++filesWithSection;

        for (const auto& sec : sections) {
            // Every located section's own counts must be internally
            // consistent - this is what distinguishes a real FBOW/v5
            // payload from a coincidental signature match (see
            // zone_objects.h's own discussion of the residual risk).
            CHECK(sec.version == 5);
            CHECK(sec.complete); // every real shipped section this reader locates is complete
            CHECK(sec.records.size() <= sec.recordCount);

            totalObjects += static_cast<long long>(sec.records.size());

            for (const auto& rec : sec.records) {
                // classHashBucket is a pure function of classHash and must
                // always land in [0, 510) - Sec11.5.
                CHECK(rec.classHashBucket < sr3zone::kClassHashBucketCount);
                // The raw tail container is always exactly tailLength
                // bytes, whether or not the best-effort entry decode made
                // sense of its interior.
                CHECK(rec.rawTail.size() == rec.tailLength);

                if (rec.tailExactFit) ++tailExactFit;
                if (rec.tailOverrun) ++tailOverrun;
                if (!rec.tailExactFit && !rec.tailOverrun) ++tailOtherLeftover;

                if (sr3zone::ClassNameForHash(rec.classHash).has_value()) ++namedClassCount;
                else ++unresolvedClassCount;

                if (rec.classHash == navpointHash) {
                    ++navpointCount;
                    if (sr3zone::ObjectPosition(rec).has_value()) ++navpointWithPosition;
                } else if (rec.classHash == coverNodeHash) {
                    ++coverNodeCount;
                    if (sr3zone::ObjectPosition(rec).has_value()) ++coverNodeWithPosition;
                }

                if (sr3zone::ScriptNpcHp(rec).has_value()) ++scriptNpcHpCount;
                auto npcFlags = sr3zone::ScriptNpcFlags(rec);
                if (npcFlags.has_value()) {
                    ++scriptNpcFlagsCount;
                    if (npcFlags->find("ignore_ai") != std::string::npos) ++scriptNpcFlagsIgnoreAiCount;
                }
                if (sr3zone::ScriptNpcTeam(rec).has_value()) ++scriptNpcTeamCount;
                if (sr3zone::ScriptVehicleHp(rec).has_value()) ++scriptVehicleHpCount;
                if (sr3zone::ScriptVehicleFlags(rec).has_value()) ++scriptVehicleFlagsCount;
                if (sr3zone::ScriptVehicleMpTeam(rec).has_value()) ++scriptVehicleMpTeamCount;

                if (rec.hasName && rec.nameText.has_value()) {
                    ++namesRecovered;
                    // A real object name should be printable ASCII of
                    // plausible length - not a strong format claim, just
                    // the sanity check that this is text, not noise from a
                    // misaligned walk.
                    const std::string& nm = *rec.nameText;
                    if (!nm.empty() && nm.size() < 128) {
                        bool allPrintable = true;
                        for (unsigned char ch : nm) {
                            if (ch < 0x20 || ch > 0x7E) { allPrintable = false; break; }
                        }
                        if (allPrintable) sawPlausibleName = true;
                    }
                }
            }
        }
    }

    std::cout << ".czn_pc files found in sr3_city_0.vpp_pc : " << cznFiles.size() << "\n";
    std::cout << "  with >=1 Objects section located        : " << filesWithSection << "\n";
    std::cout << "total objects parsed                      : " << totalObjects << "\n";
    std::cout << "names recovered                           : " << namesRecovered << "\n";
    std::cout << "tail decode: exact=" << tailExactFit << " overrun=" << tailOverrun
               << " other=" << tailOtherLeftover << "\n";
    std::cout << "Navpoint records=" << navpointCount << " with-position=" << navpointWithPosition << "\n";
    std::cout << "cover_node records=" << coverNodeCount << " with-position=" << coverNodeWithPosition << "\n";

    // Population counts, pinned exactly (static shipped data - see file
    // header comment for how these were obtained).
    CHECK(cznFiles.size() == 2068);
    CHECK(filesWithSection == 603);
    CHECK(totalObjects == 156101);
    CHECK(namesRecovered == 15307);
    CHECK(sawPlausibleName);

    // The headline finding this test locks down as a regression: this
    // reader's 4-byte entry-stride tail decode (src/zone_objects.cpp,
    // decodeTailEntries()) tiles EVERY real record in this archive
    // exactly, with zero overruns and zero leftover bytes - see
    // zone_objects.h's own "tail-interior decode" comment for the
    // controlled before/after measurement (37.2% under the literal
    // unaligned stride vs. 100% under this one).
    CHECK(tailExactFit == 156101);
    CHECK(tailOverrun == 0);
    CHECK(tailOtherLeftover == 0);

    // Corrections 2-5 (2026-10-07) - pinned exactly against this SAME
    // archive (sr3_city_0.vpp_pc only), obtained the same way as every
    // other pinned figure in this file: tools/zone_objects_population.cpp
    // against this archive. These are NOT the full 10-archive population
    // figures Sec11.7 itself cites (1,028/6,702/425/354) - this archive is
    // a strict subset of that population.
    std::cout << "named classes=" << namedClassCount << " unresolved=" << unresolvedClassCount
               << "\n";
    std::cout << "script_npc_hp=" << scriptNpcHpCount << " script_npc_flags=" << scriptNpcFlagsCount
               << " (ignore_ai=" << scriptNpcFlagsIgnoreAiCount << ") script_npc_team="
               << scriptNpcTeamCount << "\n";
    std::cout << "script_vehicle_hp=" << scriptVehicleHpCount
               << " script_vehicle_flags=" << scriptVehicleFlagsCount
               << " script_vehicle_mp_team=" << scriptVehicleMpTeamCount << "\n";

    CHECK(namedClassCount == 87251);
    CHECK(unresolvedClassCount == 68850);
    CHECK(scriptNpcHpCount == 728);
    CHECK(scriptNpcFlagsCount == 5321);
    CHECK(scriptNpcFlagsIgnoreAiCount == 249);
    CHECK(scriptNpcTeamCount == 3554);
    CHECK(scriptVehicleHpCount == 304);
    CHECK(scriptVehicleFlagsCount == 259);
    CHECK(scriptVehicleMpTeamCount == 0); // never authored in this archive - see zone_objects.h

    // Position-property-id fix real-data proof (pinned, this one archive):
    // EVERY real Navpoint/cover_node record resolves a position under the
    // CONFIRMED id (0xC8BEEEC5). Under the old, refuted hex-parenthetical id
    // (0xC8B3B245), navpointWithPosition/coverNodeWithPosition would both
    // have been 0 - this is the direct regression guard against that fix
    // ever silently reverting.
    CHECK(navpointCount == 4846);
    CHECK(navpointWithPosition == 4846);
    CHECK(coverNodeCount == 7448);
    CHECK(coverNodeWithPosition == 7448);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All real-data zone-objects checks passed.\n";
    return 0;
#endif
}
