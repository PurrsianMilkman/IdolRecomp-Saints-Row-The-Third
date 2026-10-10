// One-shot population analysis for sr3zone::ZoneObjects (include/sr3zone/
// zone_objects.h, spec-zone-data-format.md Sec11, 2026-10-07) - NOT a
// ctest (see tools/validation/README.md's standing rule: a whole-archive
// sweep is a diagnostic, not a gating test; this tool lives under tools/
// rather than tools/validation/ only because it is wired into the normal
// CMake build like tools/vintdoc_validate.cpp, so it needs no ad-hoc
// build_one.bat object list to stay buildable).
//
// Walks every shipped `.czn_pc` reachable (recursively, through nested
// containers) from the `.vpp_pc` archives given on argv, runs
// sr3zone::ZoneObjects::locate() over each one, and reports:
//
//  * how many files carry a located Objects section at all, and how many
//    objects (records) each one holds;
//  * a whole-population histogram of property-tail LENGTHS (the `+0x16`
//    word, Sec11.4) and of per-record decoded-entry COUNTS (this reader's
//    own best-effort tail-interior decode, Sec11.7 - explicitly HYPOTHESIS,
//    see zone_objects.h);
//  * how often that best-effort decode lands exactly on the declared tail
//    length, overruns it, or leaves an unconsumed remainder - the
//    structural signal Team A's own open U2 ("record interiors", Sec11.9
//    item 8) needs to confirm or correct the tail's packing rule;
//  * a handful of real recovered object names (Sec11.7's confirmed
//    `ctg_object_name` mechanism), as an end-to-end sanity check that is
//    independent of the generic tail-entry decode above.
//
// Never asserts pass/fail - prints statistics for a human (or another
// agent) to read, exactly like tools/validation/validate_zone_chain.cpp
// does for the Mesh-block geometry reader.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3zone/zone_objects.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
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

// --- population counters ---
long long g_cznFound = 0;
long long g_cznWithSection = 0, g_cznWithoutSection = 0;
long long g_cznMultipleSections = 0; // diagnostic only - see ZoneObjects::locate()'s own comment
long long g_sectionsIncomplete = 0;
long long g_totalObjects = 0;
long long g_maxObjectsInOneFile = 0;
std::string g_maxObjectsFile;

// tail-length histogram: exact small lengths 0-63, then power-of-2 buckets.
std::map<uint16_t, long long> g_tailLenExact; // 0..63
long long g_tailLenBucket[16] = {0}; // bucket k = [2^(k+6), 2^(k+7))-ish, see bucketIndexForLength

// per-record decoded-entry-count histogram (0, 1, 2, 3, 4, 5+).
long long g_entryCountHist[6] = {0};

long long g_tailExactFit = 0, g_tailOverrun = 0, g_tailUnderrunOther = 0;
long long g_recordsWithName = 0, g_recordsWithoutName = 0;
long long g_nameTextRecovered = 0; // hasName AND nameText actually decoded

// --- 2026-10-07 additions (Team A's own whole-population re-replay of
// Sec11, corrections 3-5): class-name coverage for the 21 of 40 named
// classes this reader can cite (ClassNameForHash(), zone_objects.h), and
// the script_npc_*/script_vehicle_* spawn-override property family
// (Sec11.7's own "CLOSED with real-data proof" figures: 1,028
// script_npc_hp, 425/6,702 script_npc_flags containing "ignore_ai", 383,346
// total objects - this tool's own run is the cross-check against those
// figures, flagged rather than silently accepted if they disagree). ---
std::map<std::string, long long> g_classNameHist; // name -> object count, for the 21 this reader can name
long long g_classUnresolved = 0; // classHash matched none of the 21 named classes

long long g_scriptNpcHpCount = 0, g_scriptNpcFlagsCount = 0, g_scriptNpcFlagsIgnoreAiCount = 0;
long long g_scriptNpcTeamCount = 0;
long long g_scriptVehicleHpCount = 0, g_scriptVehicleFlagsCount = 0, g_scriptVehicleMpTeamCount = 0;

// EXPERIMENTAL, tool-only diagnostic - NOT part of sr3zone's own library
// behaviour and NOT confirmed by spec-zone-data-format.md Sec11.7, which
// states only the unaligned {kind,size,id,data} entry shape. Tests whether
// each entry's stride is actually `alignUp(8+size, 4)` rather than the
// literal `8+size` ObjectRecord::tailEntries assumes - i.e. whether entries
// are individually padded to a 4-byte boundary. Kept separate from the
// library-level decode so this reader's own CONFIRMED/HYPOTHESIS boundary
// is never blurred by an exploratory question asked of the same bytes.
long long g_tailExactFitAligned4 = 0, g_tailOverrunAligned4 = 0, g_tailLeftoverAligned4 = 0;

size_t alignUp4(size_t v) { return (v + 3) / 4 * 4; }

void tryAligned4Decode(const std::vector<uint8_t>& tail) {
    size_t pos = 0;
    const size_t tailLen = tail.size();
    bool overrun = false;
    while (pos + 8 <= tailLen) {
        uint16_t size = static_cast<uint16_t>(tail[pos + 2] | (tail[pos + 3] << 8));
        size_t entryTotal = alignUp4(8 + static_cast<size_t>(size));
        if (pos + entryTotal > tailLen) { overrun = true; break; }
        pos += entryTotal;
    }
    if (overrun) ++g_tailOverrunAligned4;
    else if (pos == tailLen) ++g_tailExactFitAligned4;
    else ++g_tailLeftoverAligned4;
}

std::vector<std::string> g_sampleNames;
int g_filesDumped = 0;

int bucketIndexForLength(uint16_t len) {
    if (len < 64) return -1; // tracked exactly instead
    int b = 0;
    uint32_t hi = 128;
    while (hi <= 65536 && len >= hi) { ++b; hi <<= 1; }
    return std::min(b, 15);
}

void checkCznFile(const std::string& name, const std::vector<uint8_t>& cb) {
    ++g_cznFound;
    auto sections = sr3zone::ZoneObjects::locate(sr3zone::ByteView(cb.data(), cb.size()));

    if (sections.empty()) {
        ++g_cznWithoutSection;
        return;
    }
    ++g_cznWithSection;
    if (sections.size() > 1) ++g_cznMultipleSections;

    long long objectsThisFile = 0;
    for (const auto& sec : sections) {
        if (!sec.complete) ++g_sectionsIncomplete;
        objectsThisFile += static_cast<long long>(sec.records.size());

        for (const auto& rec : sec.records) {
            auto className = sr3zone::ClassNameForHash(rec.classHash);
            if (className.has_value()) g_classNameHist[*className]++;
            else ++g_classUnresolved;

            if (sr3zone::ScriptNpcHp(rec).has_value()) ++g_scriptNpcHpCount;
            auto npcFlags = sr3zone::ScriptNpcFlags(rec);
            if (npcFlags.has_value()) {
                ++g_scriptNpcFlagsCount;
                if (npcFlags->find("ignore_ai") != std::string::npos) ++g_scriptNpcFlagsIgnoreAiCount;
            }
            if (sr3zone::ScriptNpcTeam(rec).has_value()) ++g_scriptNpcTeamCount;
            if (sr3zone::ScriptVehicleHp(rec).has_value()) ++g_scriptVehicleHpCount;
            if (sr3zone::ScriptVehicleFlags(rec).has_value()) ++g_scriptVehicleFlagsCount;
            if (sr3zone::ScriptVehicleMpTeam(rec).has_value()) ++g_scriptVehicleMpTeamCount;

            if (rec.hasName) {
                ++g_recordsWithName;
                if (rec.nameText.has_value()) {
                    ++g_nameTextRecovered;
                    if (g_sampleNames.size() < 12 && !rec.nameText->empty()) {
                        g_sampleNames.push_back(*rec.nameText);
                    }
                }
            } else {
                ++g_recordsWithoutName;
            }

            if (rec.tailLength < 64) {
                g_tailLenExact[rec.tailLength]++;
            } else {
                int b = bucketIndexForLength(rec.tailLength);
                if (b >= 0) g_tailLenBucket[b]++;
            }

            size_t entryCount = rec.tailEntries.size();
            g_entryCountHist[std::min(entryCount, static_cast<size_t>(5))]++;

            if (rec.tailExactFit) ++g_tailExactFit;
            else if (rec.tailOverrun) ++g_tailOverrun;
            else ++g_tailUnderrunOther;

            tryAligned4Decode(rec.rawTail); // experimental diagnostic, see its own comment above
        }
    }

    g_totalObjects += objectsThisFile;
    if (objectsThisFile > g_maxObjectsInOneFile) {
        g_maxObjectsInOneFile = objectsThisFile;
        g_maxObjectsFile = name;
    }

    if (g_filesDumped < 25) {
        ++g_filesDumped;
        printf("  %-50s sections=%zu objects=%lld\n", name.c_str(), sections.size(), objectsThisFile);
    }
}

void walk(const vpp::Container& c, const std::string& containerLabel) {
    std::map<std::string, size_t> czn;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".czn_pc", stem)) czn[stem] = i;
    }

    for (const auto& kv : czn) {
        try {
            std::vector<uint8_t> cb;
            if (!entryBytes(c, kv.second, cb) || cb.empty()) continue;
            checkCznFile(containerLabel + "/" + kv.first + ".czn_pc", cb);
        } catch (const std::exception&) {
            continue; // one bad stem must not take out its siblings
        }
    }

    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                walk(c.openNested(i), containerLabel + "/" + c.entries()[i].name);
            } catch (const std::exception&) {
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: zone_objects_population <archive.vpp_pc> [more archives...]\n");
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) {
            printf("skip (empty/unreadable): %s\n", argv[i]);
            continue;
        }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            printf("scanning %s\n", argv[i]);
            walk(c, argv[i]);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED to open %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n=== sr3zone::ZoneObjects population report (spec-zone-data-format.md Sec11) ===\n");
    printf(".czn_pc files scanned              : %lld\n", g_cznFound);
    printf("  with >=1 Objects section located : %lld\n", g_cznWithSection);
    printf("  with 0 located (absent, or not reachable by this reader's\n");
    printf("    locate-by-signature scan - see zone_objects.h)  : %lld\n", g_cznWithoutSection);
    printf("  files with >1 section located (diagnostic only)   : %lld\n", g_cznMultipleSections);
    printf("sections reported incomplete (declared recordCount\n");
    printf("  exceeded what the buffer actually held)           : %lld\n", g_sectionsIncomplete);
    printf("\ntotal objects (records) parsed      : %lld\n", g_totalObjects);
    printf("max objects in one file             : %lld  (%s)\n", g_maxObjectsInOneFile,
           g_maxObjectsFile.c_str());

    printf("\n--- per-record name property (Sec11.4 +0x18 / Sec11.7 ctg_object_name) ---\n");
    printf("records with a name offset (hasName)   : %lld\n", g_recordsWithName);
    printf("records with no name (unnamed)          : %lld\n", g_recordsWithoutName);
    printf("  of which name text actually recovered : %lld\n", g_nameTextRecovered);
    printf("sample recovered names (up to 12): ");
    for (size_t i = 0; i < g_sampleNames.size(); ++i) {
        printf("%s\"%s\"", i == 0 ? "" : ", ", g_sampleNames[i].c_str());
    }
    printf("\n");

    printf("\n--- property-tail LENGTH histogram (the +0x16 word, Sec11.4) ---\n");
    printf("exact lengths 0-63:\n");
    for (const auto& kv : g_tailLenExact) {
        printf("  %4u bytes: %lld\n", kv.first, kv.second);
    }
    printf("power-of-2 buckets (length >= 64):\n");
    uint32_t lo = 128;
    for (int b = 0; b < 16; ++b) {
        if (g_tailLenBucket[b] > 0) {
            printf("  [%6u, %6u): %lld\n", b == 0 ? 64u : lo, lo * 2, g_tailLenBucket[b]);
        }
        lo <<= 1;
    }

    printf("\n--- per-record decoded property-entry COUNT (this reader's own\n");
    printf("    best-effort tail-interior decode - HYPOTHESIS, see zone_objects.h) ---\n");
    for (int i = 0; i < 6; ++i) {
        printf("  %d%s entries: %lld\n", i, i == 5 ? "+" : "", g_entryCountHist[i]);
    }
    printf("tail decode: exact fit=%lld  overrun=%lld  other-leftover=%lld\n", g_tailExactFit,
           g_tailOverrun, g_tailUnderrunOther);

    printf("\n--- class-name resolution (Sec11.7/Sec11.9 item 4, 2026-10-07: 40 of 42\n");
    printf("    real class hashes named; this reader cites 21 of those 40 by literal\n");
    printf("    string, ClassNameForHash() - see zone_objects.h for exactly why not\n");
    printf("    all 40) ---\n");
    long long namedTotal = 0;
    for (const auto& kv : g_classNameHist) {
        printf("  %-22s : %lld\n", kv.first.c_str(), kv.second);
        namedTotal += kv.second;
    }
    printf("  (total named)          : %lld\n", namedTotal);
    printf("  unresolved (not one of the 21 named here) : %lld\n", g_classUnresolved);

    printf("\n--- script_npc_*/script_vehicle_* spawn-override family (Sec11.7,\n");
    printf("    'CLOSED with real-data proof' 2026-10-07; compare against Team A's own\n");
    printf("    figures: script_npc_hp=1028, script_npc_flags=6702 (425 w/ ignore_ai),\n");
    printf("    script_vehicle_hp=354, total objects=383346 - any mismatch here against\n");
    printf("    those figures is flagged, not silently accepted) ---\n");
    printf("  script_npc_hp          : %lld\n", g_scriptNpcHpCount);
    printf("  script_npc_flags       : %lld  (containing \"ignore_ai\": %lld)\n",
           g_scriptNpcFlagsCount, g_scriptNpcFlagsIgnoreAiCount);
    printf("  script_npc_team        : %lld\n", g_scriptNpcTeamCount);
    printf("  script_vehicle_hp      : %lld\n", g_scriptVehicleHpCount);
    printf("  script_vehicle_flags   : %lld\n", g_scriptVehicleFlagsCount);
    printf("  script_vehicle_mp_team : %lld\n", g_scriptVehicleMpTeamCount);

    printf("\n--- EXPERIMENTAL diagnostic only (NOT sr3zone library behaviour, NOT\n");
    printf("    confirmed by spec-zone-data-format.md Sec11.7): same walk, but each\n");
    printf("    entry's stride is alignUp(8+size, 4) instead of the literal 8+size ---\n");
    printf("tail decode (4-aligned entries): exact fit=%lld  overrun=%lld  other-leftover=%lld\n",
           g_tailExactFitAligned4, g_tailOverrunAligned4, g_tailLeftoverAligned4);

    return 0;
}
