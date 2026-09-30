// Gate: read and print the activity-table names (spec-save-format.md Sec9.3a/Sec9.8).
//
// The snapshot does not store names. Its 60 activity records (0x2B90) are keyed by the
// ENGINE NAME HASH of each activity instance's name (Sec9.8): CRC-32 (the Sec6.1 table),
// init 0, no final XOR, over the lower-cased ASCII name. Names therefore have to be
// RECOVERED by pre-image search. The spec did that against the executable's strings; this
// harness may not touch the executable, so it uses two sources that are allowed:
//   1. every identifier-like token found in the game's own data archives (.vpp_pc, walked
//      recursively) - real game data;
//   2. the naming pattern the spec states for the keys it recovered, "_a_<xx>_<yy>[_]<NN>"
//      (Sec9.3a), enumerated by brute force (used only for keys that step 1 did not resolve;
//      anything found this way is labelled PATTERN and is weaker evidence).
//
// Controls that CAN fail (a search is only evidence if it finds nothing when it should):
//   - the same archive scan with a WRONG hash (init 0xFFFFFFFF; final XOR 0xFFFFFFFF; hashing
//     the raw bytes without lower-casing) must resolve 0 keys (case-insensitive check aside);
//   - the expected number of accidental 32-bit collisions is printed from the token count.
//
// Independent string-hash checks from the spec text (no data involved):
//   nameHash("npc_questionmark") == 0xA10725AA and nameHash("generic") == 0x0B237EAD (Sec11.5);
//   the dlc1_* activity names quoted in Sec9.8 must hash to keys found in the second table
//   (snapshot 0x1638C, Sec11.4 row 3); the eight cheat strings quoted in Sec9.5 must hash to
//   cheat ids present in real snapshots.
//
// STRICTLY READ-ONLY on saves and game archives.
//
// usage: validate_save_activity_names [--no-archives] [archive.vpp_pc ...]
//   default archives: the small table/interface/misc archives under
//   D:\Project Crreish\Saints Row 3 CRREISH\packfiles\pc\cache

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sr3save/save_crc.h"
#include "sr3save/save_snapshot.h"
#include "vpp/container.h"

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

enum class HashKind { Correct, InitFFFF, FinalXor, NoLower };

uint32_t hashVariant(const char* s, size_t n, HashKind k) {
    const auto& t = sr3save::crcTable();
    uint32_t crc = (k == HashKind::InitFFFF) ? 0xFFFFFFFFu : 0u;
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = static_cast<uint8_t>(s[i]);
        if (k != HashKind::NoLower && b >= 'A' && b <= 'Z') b = static_cast<uint8_t>(b - 'A' + 'a');
        crc = (crc >> 8) ^ t[(crc ^ b) & 0xFFu];
    }
    if (k == HashKind::FinalXor) crc ^= 0xFFFFFFFFu;
    return crc;
}

bool tokChar(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

struct Found {
    std::string name;
    std::string where;
};

struct Scan {
    std::unordered_map<uint32_t, std::vector<Found>> hits[4]; // per HashKind
    const std::unordered_set<uint32_t>* targets = nullptr;
    uint64_t tokens = 0;
    uint64_t entries = 0, archives = 0, bytes = 0;
    std::string currentWhere;

    void scanBuffer(const uint8_t* p, size_t n) {
        bytes += n;
        size_t i = 0;
        while (i < n) {
            while (i < n && !tokChar(p[i])) ++i;
            size_t j = i;
            while (j < n && tokChar(p[j])) ++j;
            size_t len = j - i;
            if (len >= 3 && len <= 80) {
                ++tokens;
                probe(reinterpret_cast<const char*>(p + i), len);
                // also without a leading run of '_' or '-' (a token may be glued to markup)
                size_t k = 0;
                while (k < len && (p[i + k] == '_' || p[i + k] == '-')) ++k;
                if (k > 0 && len - k >= 3) probe(reinterpret_cast<const char*>(p + i + k), len - k);
            }
            i = j;
        }
    }

    void probe(const char* s, size_t len) {
        for (int k = 0; k < 4; ++k) {
            uint32_t h = hashVariant(s, len, static_cast<HashKind>(k));
            if (targets->count(h)) {
                auto& v = hits[k][h];
                if (v.size() < 4) {
                    bool dup = false;
                    for (auto& f : v) dup = dup || f.name == std::string(s, len);
                    if (!dup) v.push_back({std::string(s, len), currentWhere});
                }
            }
        }
    }
};

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated || r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

void walk(const vpp::Container& c, Scan& scan, const std::string& path, int depth) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        std::string where = path + "/" + c.entries()[i].name;
        std::vector<uint8_t> b;
        bool ok = false;
        try {
            ok = entryBytes(c, i, b);
        } catch (const std::exception&) {
            ok = false;
        }
        if (!ok) continue;
        ++scan.entries;
        scan.currentWhere = where;
        // a nested container is scanned recursively; its own bytes are also token-scanned (cheap)
        bool nested = false;
        if (depth < 4 && b.size() > 64) {
            try {
                vpp::Container inner(vpp::ByteView(b.data(), b.size()));
                nested = true;
                walk(inner, scan, where, depth + 1);
            } catch (const std::exception&) {
            }
        }
        if (!nested) scan.scanBuffer(b.data(), b.size());
    }
}

// Pattern brute force (Sec9.3a): "_a_<xx>_<yy>[_]<NN>", xx 2-4 lowercase letters, yy in {ne,nw,sw,dt},
// NN in 01..03. Also tries the same without the leading underscore and with the "dlc<d>" prefixes of Sec9.8.
void patternSearch(const std::unordered_set<uint32_t>& targets, std::map<uint32_t, std::string>& found) {
    const char* yys[] = {"ne", "nw", "sw", "dt"};
    const char* nns[] = {"01", "02", "03"};
    const char* prefixes[] = {"_a_", "a_", "dlc1_a_", "dlc2_a_", "dlc3_a_"};
    std::string xx;
    auto tryName = [&](const std::string& name) {
        uint32_t h = sr3save::nameHash(name);
        if (targets.count(h) && !found.count(h)) found[h] = name;
    };
    auto body = [&](const std::string& x) {
        for (const char* pre : prefixes) {
            for (const char* yy : yys) {
                for (const char* nn : nns) {
                    tryName(std::string(pre) + x + "_" + yy + "_" + nn);
                    tryName(std::string(pre) + x + "_" + yy + nn);
                }
            }
            for (const char* nn : nns) tryName(std::string(pre) + x + "_" + nn); // dlc form: dlc1_a_es_01
        }
    };
    for (char a = 'a'; a <= 'z'; ++a) {
        for (char b = 'a'; b <= 'z'; ++b) {
            body(std::string() + a + b);
            for (char c = 'a'; c <= 'z'; ++c) {
                body(std::string() + a + b + c);
                for (char d = 'a'; d <= 'z'; ++d) body(std::string() + a + b + c + d);
            }
        }
    }
}

// Near-pattern search (exploratory, WEAKEST evidence): for keys that neither the archives nor the exact
// pattern resolved, try every name within edit distance 2 (substitute / insert / delete one character of
// [a-z0-9_-]) of each not-yet-resolved member of the same naming grid (codes seen x {ne,nw,sw,dt} x 01..03).
// Expected accidental hits ~ (bases x ~700k variants x targets) / 2^32, printed by the caller.
void neighbours(const std::string& s, std::vector<std::string>& out) {
    static const std::string alpha = "abcdefghijklmnopqrstuvwxyz0123456789_-";
    for (size_t i = 0; i < s.size(); ++i) {
        for (char c : alpha) {
            if (c != s[i]) {
                std::string t = s;
                t[i] = c;
                out.push_back(t);
            }
        }
        std::string d = s;
        d.erase(i, 1);
        out.push_back(d);
    }
    for (size_t i = 0; i <= s.size(); ++i) {
        for (char c : alpha) {
            std::string t = s;
            t.insert(i, 1, c);
            out.push_back(t);
        }
    }
}

uint64_t nearPatternSearch(const std::vector<std::string>& bases, const std::unordered_set<uint32_t>& targets,
                           std::map<uint32_t, std::string>& found) {
    uint64_t tried = 0;
    for (const auto& base : bases) {
        std::vector<std::string> d1;
        neighbours(base, d1);
        std::vector<std::string> d2;
        for (const auto& s : d1) neighbours(s, d2);
        for (const auto& v : {std::vector<std::string>{base}, d1, d2}) {
            for (const auto& s : v) {
                ++tried;
                uint32_t h = sr3save::nameHash(s);
                if (targets.count(h) && !found.count(h)) found[h] = s + "   (near " + base + ")";
            }
        }
    }
    return tried;
}

uint32_t rd32(const std::vector<uint8_t>& b, size_t o) {
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
}

} // namespace

int main(int argc, char** argv) {
    bool noArchives = false;
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-archives") noArchives = true;
        else archives.push_back(a);
    }
    if (archives.empty()) {
        const std::string root = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/";
        for (const char* n : {"misc_tables", "cutscene_tables", "da_tables", "decals", "effects", "interface_startup",
                              "patch_compressed", "patch_uncompressed", "preload_items", "preload_effects", "preload_rigs",
                              "sr3_city_missions", "startup", "vehicles_preload", "misc", "items", "shaders",
                              "player_morph", "sound_turbo", "soundboot"}) {
            archives.push_back(root + n + ".vpp_pc");
        }
    }

    int unexpected = 0;

    // ---- independent string-hash checks straight from the spec text ---------------
    std::printf("== 0. string-hash sanity (spec text only) ==\n");
    {
        uint32_t a = sr3save::nameHash("npc_questionmark"), b = sr3save::nameHash("generic");
        std::printf("  hash(\"npc_questionmark\")=%08X (spec Sec11.5: A10725AA)  hash(\"generic\")=%08X (spec: 0B237EAD)  %s\n", a, b,
                    (a == 0xA10725AAu && b == 0x0B237EADu) ? "OK" : "MISMATCH");
        if (a != 0xA10725AAu || b != 0x0B237EADu) ++unexpected;
        std::printf("  case folding: hash(\"Generic\")==hash(\"generic\"): %s\n",
                    sr3save::nameHash("Generic") == b ? "OK" : "MISMATCH");
    }

    // ---- load real snapshots ------------------------------------------------------
    struct Snap {
        std::string where;
        std::vector<uint8_t> bytes;
        sr3save::SaveSnapshot s;
    };
    std::vector<Snap> snaps;
    {
        std::map<std::vector<uint8_t>, int> seen;
        const std::pair<const char*, const char*> folders[] = {
            {"Documents", "C:/Users/Purrsian/Documents/Saints Row The Third"},
            {"LocalAppData", "C:/Users/Purrsian/AppData/Local/Saints Row The Third"},
            {"CloudSync", "D:/Project Crreish/Saints Row 3 CRREISH/!Downloads/!Cloud Sync Backup/saves"},
            {"TEAM-B fixtures", "D:/Project Crreish/TEAM B/test-fixtures/saves"},
        };
        for (auto& f : folders) {
            std::error_code ec;
            if (!fs::is_directory(f.second, ec)) continue;
            for (const auto& de : fs::directory_iterator(f.second)) {
                std::string fn = de.path().filename().string();
                if (fn.size() != 18 || fn.rfind("sr3save_", 0) != 0) continue;
                std::vector<uint8_t> b = readFile(de.path());
                if (b.size() != sr3save::kSaveSnapshotSize || seen.count(b)) continue;
                seen[b] = 1;
                snaps.push_back({std::string(f.first) + "/" + fn, b,
                                 sr3save::SaveSnapshot::parse(vpp::ByteView(b.data(), b.size()))});
            }
        }
    }
    std::printf("\nloaded %zu distinct real snapshots\n", snaps.size());
    if (snaps.empty()) {
        std::printf("NO REAL SNAPSHOTS FOUND\n");
        return 2;
    }

    // ---- 1. the activity table: same 60 keys, same order, in every snapshot ---------
    std::printf("\n== 1. activity table (0x2B90): count and key identity across snapshots ==\n");
    std::vector<uint32_t> keys;
    {
        int count60 = 0, sameKeys = 0, inBounds = 0;
        for (const auto& s : snaps) {
            if (s.s.activityTableCount() == 60) ++count60;
            if (s.s.activityTableInBounds()) ++inBounds;
        }
        const auto& ref = snaps[0].s.activityRecords();
        for (const auto& s : snaps) {
            const auto& r = s.s.activityRecords();
            bool same = r.size() == ref.size();
            for (size_t i = 0; same && i < r.size(); ++i) same = r[i].nameHash == ref[i].nameHash;
            if (same) ++sameKeys;
        }
        std::printf("  used-record count == 60: %d/%zu;  keys identical and in the same order as snapshot 0: %d/%zu\n", count60,
                    snaps.size(), sameKeys, snaps.size());
        std::printf("  (spec Sec9.3a: 60 in 16/16 and identical in 16/16)\n");
        if (count60 != static_cast<int>(snaps.size()) || sameKeys != static_cast<int>(snaps.size())) ++unexpected;
        for (const auto& r : ref) keys.push_back(r.nameHash);
        std::set<uint32_t> uniq(keys.begin(), keys.end());
        std::printf("  distinct keys among the 60: %zu\n", uniq.size());
        // levels-completed is 0/1 everywhere; second mask subset of first
        long recs = 0, lvlOk = 0, maskOk = 0, padOk = 0, runOk = 0;
        for (const auto& s : snaps) {
            for (const auto& r : s.s.activityRecords()) {
                ++recs;
                if (r.levelsCompleted <= 1) ++lvlOk;
                if ((r.secondMask & ~r.completedMask) == 0) ++maskOk;
                if (r.pad[0] == 0 && r.pad[1] == 0) ++padOk;
                // levelsCompleted == run length of set low bits of completedMask
                uint32_t run = 0;
                while (run < 8 && ((r.completedMask >> run) & 1u)) ++run;
                if (run == r.levelsCompleted) ++runOk;
            }
        }
        std::printf("  over %ld records: levelsCompleted<=1 %ld/%ld; secondMask subset of mask %ld/%ld; pad zero %ld/%ld; "
                    "levelsCompleted == run of set low mask bits %ld/%ld (spec: 960/960 each)\n",
                    recs, lvlOk, recs, maskOk, recs, padOk, recs, runOk, recs);
        if (lvlOk != recs || maskOk != recs || padOk != recs || runOk != recs) ++unexpected;
    }

    // second (DLC) table at 0x1638C and cheat ids, read straight from the spec's offsets
    std::vector<uint32_t> dlcKeys, cheatIds;
    {
        std::set<uint32_t> d, c;
        for (const auto& s : snaps) {
            uint32_t cnt = rd32(s.bytes, 0x1638C + 0x304);
            if (cnt <= 64) {
                for (uint32_t i = 0; i < cnt; ++i) d.insert(rd32(s.bytes, 0x1638C + 4 + 12 * static_cast<size_t>(i)));
            }
            for (uint32_t id : s.s.activeCheatIds()) c.insert(id);
        }
        dlcKeys.assign(d.begin(), d.end());
        cheatIds.assign(c.begin(), c.end());
        std::printf("  second activity table (0x1638C, entry 28): %zu distinct keys in the corpus (spec: 9); distinct active cheat ids: %zu (spec: 23)\n",
                    dlcKeys.size(), cheatIds.size());
        if (dlcKeys.size() != 9 || cheatIds.size() != 23) ++unexpected;
    }

    // ---- 2. name recovery: spec-quoted strings first ---------------------------------
    std::printf("\n== 2. names quoted in the spec text, hashed with the Sec9.8 routine ==\n");
    {
        const char* dlcNames[] = {"dlc1_rm_01",     "dlc1_rm_02",    "dlc1_a_es_01",    "dlc1_a_es_02",
                                  "dlc1_a_tbp_01",  "dlc1_a_tbp_02", "dlc1_a_bm_nw_01", "dlc1_a_bm_nw_02"};
        int hit = 0;
        for (const char* n : dlcNames) {
            uint32_t h = sr3save::nameHash(n);
            bool present = std::find(dlcKeys.begin(), dlcKeys.end(), h) != dlcKeys.end();
            hit += present ? 1 : 0;
            std::printf("  %-18s -> %08X  %s\n", n, h, present ? "IS a key of the second activity table" : "not in the second table");
        }
        std::printf("  => %d/8 quoted dlc names hash to keys of the second table (spec Sec9.8: 8 of its 9 keys)\n", hit);
        if (hit != 8) ++unexpected;
        const char* cheatNames[] = {"dlc_car_mass",     "dlc_super_saints",       "dlc_never_die", "dlc_super_explosions",
                                    "dlc_player_pratfalls", "dlc_unlimited_ammo", "dlc_unlimited_clip", "hohoho"};
        int chit = 0;
        for (const char* n : cheatNames) {
            uint32_t h = sr3save::nameHash(n);
            bool present = std::find(cheatIds.begin(), cheatIds.end(), h) != cheatIds.end();
            chit += present ? 1 : 0;
        }
        std::printf("  => %d/8 quoted cheat strings hash to active cheat ids present in the snapshots (spec Sec9.5: 8 of 23)\n", chit);
        if (chit != 8) ++unexpected;
    }

    // ---- 3. archive scan ----------------------------------------------------------------
    std::unordered_set<uint32_t> targets;
    for (uint32_t k : keys) targets.insert(k);
    for (uint32_t k : dlcKeys) targets.insert(k);
    for (uint32_t k : cheatIds) targets.insert(k);
    std::map<uint32_t, std::string> resolved; // key -> name (correct hash, archives)
    std::map<uint32_t, std::string> resolvedWhere;
    if (!noArchives) {
        std::printf("\n== 3. token scan of game archives (correct hash + 3 wrong-hash controls) ==\n");
        Scan scan;
        scan.targets = &targets;
        for (const auto& a : archives) {
            std::error_code ec;
            if (!fs::is_regular_file(a, ec)) {
                std::printf("  (missing) %s\n", a.c_str());
                continue;
            }
            std::vector<uint8_t> b = readFile(a);
            try {
                vpp::Container c(vpp::ByteView(b.data(), b.size()));
                uint64_t before = scan.entries;
                walk(c, scan, fs::path(a).filename().string(), 0);
                ++scan.archives;
                std::printf("  scanned %-24s %8llu entries, tokens so far %llu\n", fs::path(a).filename().string().c_str(),
                            static_cast<unsigned long long>(scan.entries - before), static_cast<unsigned long long>(scan.tokens));
                std::fflush(stdout);
            } catch (const std::exception& ex) {
                std::printf("  could not open %s: %s\n", a.c_str(), ex.what());
            }
        }
        std::printf("  archives=%llu entries=%llu bytes=%llu tokens hashed=%llu; expected accidental collisions per hash variant "
                    "= tokens x targets / 2^32 = %.3f\n",
                    static_cast<unsigned long long>(scan.archives), static_cast<unsigned long long>(scan.entries),
                    static_cast<unsigned long long>(scan.bytes), static_cast<unsigned long long>(scan.tokens),
                    static_cast<double>(scan.tokens) * static_cast<double>(targets.size()) / 4294967296.0);
        const char* kindName[4] = {"CORRECT (init 0, lowercased)", "control: init FFFFFFFF", "control: final XOR FFFFFFFF",
                                   "control: no lowercasing"};
        for (int k = 0; k < 4; ++k) {
            std::set<uint32_t> keysHit;
            for (auto& kv : scan.hits[k]) keysHit.insert(kv.first);
            std::printf("  %-32s resolves %zu of %zu target keys\n", kindName[k], keysHit.size(), targets.size());
        }
        // the two wrong-hash controls must resolve nothing
        if (scan.hits[1].size() + scan.hits[2].size() > 2) { // > expected accidental collisions (see above) + slack
            std::printf("  *** a wrong-hash control resolved keys: the search cannot tell hashes apart ***\n");
            ++unexpected;
        }
        for (auto& kv : scan.hits[0]) {
            resolved[kv.first] = kv.second.front().name;
            resolvedWhere[kv.first] = kv.second.front().where;
        }
        // keys that the case-preserving control resolves but the correct hash does not, would indicate an all-lowercase corpus issue
        int extraNoLower = 0;
        for (auto& kv : scan.hits[3]) if (!scan.hits[0].count(kv.first)) ++extraNoLower;
        std::printf("  keys found only by the no-lowercase control: %d (mixed-case tokens whose lowercase form is the real name)\n",
                    extraNoLower);
    }

    // ---- 4. pattern brute force over ALL 60 keys (independent of the archive scan) -----------
    std::unordered_set<uint32_t> allActs(keys.begin(), keys.end());
    std::map<uint32_t, std::string> byPattern;
    std::printf("\n== 4. pattern search (spec Sec9.3a shape) over all %zu activity keys ==\n", allActs.size());
    patternSearch(allActs, byPattern);
    std::printf("  resolved %zu of %zu by pattern (spec: 56 of 60). Evidence strength: a pattern hit is one 32-bit coincidence "
                "among ~60M candidate strings, so the expected number of ACCIDENTAL hits is ~0.9 overall; hits that also lie on the "
                "regular grid (2-letter code, ne/nw/sw/dt, 01-03) are far likelier real than accidental.\n",
                byPattern.size(), allActs.size());
    // archive-vs-pattern agreement: an archive token is real game data, so where both resolve a key they must agree
    {
        int both = 0, agree = 0;
        for (auto& kv : resolved) {
            auto p = byPattern.find(kv.first);
            if (p == byPattern.end()) continue;
            ++both;
            std::string low = kv.second;
            for (auto& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (low == p->second) ++agree;
        }
        std::printf("  keys resolved by BOTH game-archive tokens and the pattern search: %d; identical names (case-folded): %d/%d\n",
                    both, agree, both);
        if (agree != both) ++unexpected;
    }
    // grid regularity: every recovered name is _a_<2 letters>_<ne|nw|sw|dt>[_]0<1-3>
    {
        int regular = 0;
        for (auto& kv : byPattern) {
            const std::string& n = kv.second;
            bool ok = n.rfind("_a_", 0) == 0 && n.size() >= 11 && n[5] == '_';
            if (ok) {
                std::string yy = n.substr(6, 2);
                ok = (yy == "ne" || yy == "nw" || yy == "sw" || yy == "dt") && n[n.size() - 2] == '0' &&
                     n[n.size() - 1] >= '1' && n[n.size() - 1] <= '3';
            }
            regular += ok ? 1 : 0;
        }
        std::printf("  recovered names on the 2-letter/ne-nw-sw-dt/01-03 grid: %d/%zu\n", regular, byPattern.size());
    }

    // ---- 4b. near-pattern search for what is STILL unresolved -------------------------------
    std::map<uint32_t, std::string> byNear;
    {
        std::unordered_set<uint32_t> still;
        for (uint32_t k : keys) if (!byPattern.count(k) && !resolved.count(k)) still.insert(k);
        if (!still.empty()) {
            // grid bases: every (code, yy, nn) whose exact name is not already a recovered one
            std::set<std::string> codes, have;
            for (auto& kv : byPattern) {
                have.insert(kv.second);
                const std::string& n = kv.second; // "_a_xx_yy_NN" / "_a_xx_yyNN"
                if (n.size() >= 6 && n.rfind("_a_", 0) == 0) codes.insert(n.substr(3, 2));
            }
            std::vector<std::string> bases;
            for (const auto& c : codes) {
                for (const char* yy : {"ne", "nw", "sw", "dt"}) {
                    for (const char* nn : {"01", "02", "03"}) {
                        std::string a = "_a_" + c + "_" + yy + "_" + nn, b2 = "_a_" + c + "_" + yy + nn;
                        if (!have.count(a) && !have.count(b2)) bases.push_back(a);
                    }
                }
            }
            std::printf("\n== 4b. near-pattern search (exploratory): %zu still-unresolved keys, %zu grid names not yet recovered as bases ==\n",
                        still.size(), bases.size());
            std::printf("  grid names absent from the recovered set:");
            for (auto& b : bases) std::printf(" %s", b.c_str());
            std::printf("\n");
            uint64_t tried = nearPatternSearch(bases, still, byNear);
            std::printf("  tried %llu candidate strings; expected accidental hits ~ %.4f; found %zu\n",
                        static_cast<unsigned long long>(tried),
                        static_cast<double>(tried) * static_cast<double>(still.size()) / 4294967296.0, byNear.size());
            for (auto& kv : byNear) std::printf("    %08X  %s\n", kv.first, kv.second.c_str());
        }
    }

    // ---- 5. print the table ---------------------------------------------------------------
    std::printf("\n== 5. activity table, in table order ==\n");
    std::printf("  src: G = a token in game archive data hashes to the key, P = spec-pattern search, N = near-pattern (exploratory), ? = unresolved\n");
    std::printf("  idx  key       src name                                   done in each snapshot (count of %zu with levelsCompleted=1)\n", snaps.size());
    int nG = 0, nP = 0, nU = 0, nN = 0, nGP = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        uint32_t k = keys[i];
        const char* src = "?";
        std::string name = "(unresolved)";
        bool g = resolved.count(k) != 0, p = byPattern.count(k) != 0;
        if (p) { src = g ? "GP" : "P "; name = byPattern[k]; ++nP; }
        else if (g) { src = "G "; name = resolved[k]; }
        else if (byNear.count(k)) { src = "N "; name = byNear[k]; ++nN; }
        else { src = "? "; ++nU; }
        if (g) ++nG;
        if (g && p) ++nGP;
        int done = 0;
        for (const auto& s : snaps) {
            if (i < s.s.activityRecords().size() && s.s.activityRecords()[i].levelsCompleted == 1) ++done;
        }
        std::printf("  %3zu  %08X  %s %-40s %d/%zu%s%s\n", i, k, src, name.c_str(), done, snaps.size(),
                    g ? "   <- " : "", g ? resolvedWhere[k].c_str() : "");
    }
    std::printf("\n  RESULT: of %zu activity keys: %d resolved by the spec-pattern search (spec: 56 of 60), %d confirmed additionally by a token in game data (G), %d resolved only by the exploratory near-pattern search (N), %d unresolved\n",
                keys.size(), nP, nGP, nN, nU);
    if (nG > nGP) std::printf("  (%d key(s) resolved by game data but NOT by the pattern search)\n", nG - nGP);

    // second table and cheats
    std::printf("\n== 6. second activity table (0x1638C) and active cheat ids ==\n");
    for (uint32_t k : dlcKeys) {
        std::string nm = "(unresolved)";
        for (const char* n : {"dlc1_rm_01", "dlc1_rm_02", "dlc1_a_es_01", "dlc1_a_es_02", "dlc1_a_tbp_01", "dlc1_a_tbp_02",
                              "dlc1_a_bm_nw_01", "dlc1_a_bm_nw_02"})
            if (sr3save::nameHash(n) == k) nm = std::string(n) + "  (spec-quoted)";
        if (resolved.count(k) && nm == "(unresolved)") nm = resolved[k] + "  (archive)";
        std::printf("  %08X  %s\n", k, nm.c_str());
    }
    {
        int cs = 0;
        for (uint32_t k : cheatIds) {
            std::string nm;
            for (const char* n : {"dlc_car_mass", "dlc_super_saints", "dlc_never_die", "dlc_super_explosions",
                                  "dlc_player_pratfalls", "dlc_unlimited_ammo", "dlc_unlimited_clip", "hohoho"})
                if (sr3save::nameHash(n) == k) nm = std::string(n) + "  (spec-quoted)";
            if (nm.empty() && resolved.count(k)) nm = resolved[k] + "  (archive)";
            if (!nm.empty()) { std::printf("  cheat %08X  %s\n", k, nm.c_str()); ++cs; }
        }
        std::printf("  cheat ids with a recovered name: %d of %zu\n", cs, cheatIds.size());
    }
    if (nU + nP + nN + (nG - nGP) != static_cast<int>(keys.size())) ++unexpected;

    std::printf("\nRESULT: %s (%d unexpected)\n", unexpected == 0 ? "ALL GATES AS EXPECTED" : "DISAGREEMENT", unexpected);
    return unexpected == 0 ? 0 : 1;
}
