// Gate: the save-data CRC-32 (spec-save-format.md Sec6.1) recomputed over every
// real snapshot and directory found on this machine.
//
//   spec claim:   init 0, NO final XOR, reflected 0xEDB88320.
//                 directory hash covers file[0x008, 0x260)  (4/4 in spec)
//                 snapshot  hash covers file[0x004, 0x1A8E8) + 0x118 zero bytes
//                 (16/16 in spec); four alternative variants 0/16 each.
//
// A count is only meaningful with its denominator and with controls that CAN
// fail, so this prints, for every variant, matched/total. The correct variant
// must match all and every control must match none (a control that matched
// would mean the test cannot tell the variants apart).
//
// The library's table-driven CRC is cross-checked against an independent
// bit-by-bit implementation written here, and against the spec's own
// table anchors (T[1] = 0x77073096, T[2] = 0xEE0E612C).
//
// STRICTLY READ-ONLY on the save files: they are opened for reading only.
//
// usage: validate_save_crc [dir ...]
//   with no arguments, searches the known real-save folders (spec Sec6 list).

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3save/save_crc.h"

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

// Independent reference: bitwise reflected CRC-32, init 0, no final XOR.
uint32_t crcBitwise(const uint8_t* p, size_t n, uint32_t crc = 0) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
    return crc;
}
uint32_t crcBitwiseZeros(size_t n, uint32_t crc) {
    for (size_t i = 0; i < n; ++i) {
        for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
    return crc;
}
// The familiar zlib/PNG CRC (init 0xFFFFFFFF, complemented), as a control.
uint32_t crcZlibStyle(const uint8_t* p, size_t n, size_t zeroTail) {
    uint32_t crc = 0xFFFFFFFFu;
    crc = crcBitwise(p, n, crc);
    crc = crcBitwiseZeros(zeroTail, crc);
    return ~crc;
}
uint32_t u32(const std::vector<uint8_t>& b, size_t o) {
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
}

struct Source {
    std::string label;
    fs::path dir;
};

struct Counter {
    int match = 0;
    int total = 0;
    void add(bool m) { ++total; if (m) ++match; }
};

} // namespace

int main(int argc, char** argv) {
    std::vector<Source> sources;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) sources.push_back({argv[i], argv[i]});
    } else {
        sources = {
            {"Documents", "C:/Users/Purrsian/Documents/Saints Row The Third"},
            {"LocalAppData", "C:/Users/Purrsian/AppData/Local/Saints Row The Third"},
            {"CloudSyncBackup(CRREISH)",
             "D:/Project Crreish/Saints Row 3 CRREISH/!Downloads/!Cloud Sync Backup/saves"},
            {"CloudSyncBackup(RTXREMIX)",
             "D:/SR3RTXREMIXCOMP/Saints Row 3/!Downloads/!Cloud Sync Backup/saves"},
            {"TEAM-B fixtures", "D:/Project Crreish/TEAM B/test-fixtures/saves"},
        };
    }

    int failures = 0;

    // ---- 0. the hash routine itself -------------------------------------
    std::printf("== 0. CRC routine self-checks ==\n");
    {
        const auto& t = sr3save::crcTable();
        bool anchors = t[1] == 0x77073096u && t[2] == 0xEE0E612Cu;
        std::printf("table anchors T[1]=%08X T[2]=%08X (spec: 77073096 / EE0E612C): %s\n", t[1], t[2],
                    anchors ? "OK" : "FAIL");
        if (!anchors) ++failures;

        // library (table) vs independent bitwise on pseudo-random data.
        std::vector<uint8_t> rnd(5000);
        uint32_t s = 12345;
        for (auto& b : rnd) { s = s * 1664525u + 1013904223u; b = static_cast<uint8_t>(s >> 24); }
        bool same = sr3save::crcUpdate(0, rnd.data(), rnd.size()) == crcBitwise(rnd.data(), rnd.size());
        std::printf("library table CRC == independent bitwise CRC on 5000 random bytes: %s\n",
                    same ? "OK" : "FAIL");
        if (!same) ++failures;
        // Known value for the *zlib-style* CRC of "123456789" = 0xCBF43926 validates our bitwise core.
        const char* nine = "123456789";
        uint32_t z = crcZlibStyle(reinterpret_cast<const uint8_t*>(nine), 9, 0);
        std::printf("bitwise core, zlib-style init/xorout on \"123456789\": %08X (well-known 0xCBF43926): %s\n",
                    z, z == 0xCBF43926u ? "OK" : "FAIL");
        if (z != 0xCBF43926u) ++failures;
        // The same bytes with init 0 / no final xor are different (that is the point of the control).
        std::printf("init-0/no-xorout CRC of \"123456789\": %08X (differs from zlib-style, as it must)\n",
                    sr3save::crcUpdate(0, reinterpret_cast<const uint8_t*>(nine), 9));
        // Empty directory table hashes to 0 (spec Sec6.1).
        std::vector<uint8_t> zeros(600, 0);
        uint32_t zc = sr3save::crcUpdate(0, zeros.data(), zeros.size());
        std::printf("CRC of 600 zero bytes (empty directory table): %08X (spec: 0): %s\n", zc,
                    zc == 0 ? "OK" : "FAIL");
        if (zc != 0) ++failures;
    }

    // ---- collect distinct files ------------------------------------------
    struct Item {
        std::string where; // first source it was seen in
        std::string name;
        std::vector<uint8_t> bytes;
        std::vector<std::string> alsoIn;
    };
    std::vector<Item> snaps, dirs;
    std::map<std::vector<uint8_t>, size_t> snapIndex, dirIndex;
    int snapFiles = 0, dirFiles = 0;
    for (const auto& src : sources) {
        std::error_code ec;
        if (!fs::is_directory(src.dir, ec)) {
            std::printf("source not found (skipped): %s\n", src.dir.string().c_str());
            continue;
        }
        for (const auto& de : fs::directory_iterator(src.dir)) {
            std::string fn = de.path().filename().string();
            bool isSnap = fn.size() == 18 && fn.rfind("sr3save_", 0) == 0 && fn.substr(10) == ".sr3s_pc";
            bool isDir = fn == "savedir.sr3d_pc";
            if (!isSnap && !isDir) continue;
            std::vector<uint8_t> b = readFile(de.path());
            if (isSnap && b.size() == sr3save::kSnapshotFileSize) {
                ++snapFiles;
                auto it = snapIndex.find(b);
                if (it == snapIndex.end()) {
                    snapIndex[b] = snaps.size();
                    snaps.push_back({src.label, fn, b, {}});
                } else {
                    snaps[it->second].alsoIn.push_back(src.label + "/" + fn);
                }
            } else if (isDir && b.size() == 0x260) {
                ++dirFiles;
                auto it = dirIndex.find(b);
                if (it == dirIndex.end()) {
                    dirIndex[b] = dirs.size();
                    dirs.push_back({src.label, fn, b, {}});
                } else {
                    dirs[it->second].alsoIn.push_back(src.label + "/" + fn);
                }
            } else {
                std::printf("unexpected size %zu for %s (skipped)\n", b.size(), de.path().string().c_str());
            }
        }
    }
    std::printf("\nfound %d snapshot files -> %zu distinct;  %d directory files -> %zu distinct\n", snapFiles,
                snaps.size(), dirFiles, dirs.size());
    if (snaps.empty() || dirs.empty()) {
        std::printf("NO REAL SAVES FOUND - cannot run the real-data gate.\n");
        return 2;
    }

    // ---- 1. directories -----------------------------------------------------
    std::printf("\n== 1. directory hash (spec: init 0, no xorout, over file[0x008,0x260)) ==\n");
    struct Var {
        const char* name;
        Counter c;
        bool expectAll;
    };
    {
        std::vector<Var> vars = {
            {"CORRECT  [0x008,0x260) init0 noxor", {}, true},
            {"control  [0x004,0x260) (includes count)", {}, false},
            {"control  [0x009,0x260) (start+1)", {}, false},
            {"control  [0x008,0x25F) (end-1)", {}, false},
            {"control  [0x000,0x260) with field zeroed", {}, false},
            {"control  zlib-style init FFFFFFFF + complement", {}, false},
            {"NON-DISCRIMINATING start [0x005,0x260) (bytes 5..7 of count are 0)", {}, true},
        };
        for (const auto& d : dirs) {
            const auto& b = d.bytes;
            uint32_t stored = u32(b, 0);
            uint32_t cor = crcBitwise(b.data() + 8, 0x260 - 8);
            uint32_t libv = sr3save::computeDirectoryHash(vpp::ByteView(b.data(), b.size()));
            std::vector<uint8_t> z = b;
            z[0] = z[1] = z[2] = z[3] = 0;
            uint32_t c[7] = {cor,
                             crcBitwise(b.data() + 4, 0x260 - 4),
                             crcBitwise(b.data() + 9, 0x260 - 9),
                             crcBitwise(b.data() + 8, 0x25F - 8),
                             crcBitwise(z.data(), 0x260),
                             crcZlibStyle(b.data() + 8, 0x260 - 8, 0),
                             crcBitwise(b.data() + 5, 0x260 - 5)};
            for (int i = 0; i < 7; ++i) vars[static_cast<size_t>(i)].c.add(c[i] == stored);
            // recount active flags vs count field
            int active = 0;
            for (int s = 0; s < 24; ++s) if (b[8 + 25 * static_cast<size_t>(s)] != 0) ++active;
            std::printf("  dir %-26s stored=%08X computed=%08X lib=%08X %s | count field=%u recount=%d %s | seen in %zu other place(s)\n",
                        d.where.c_str(), stored, cor, libv, stored == cor ? "MATCH" : "MISMATCH", u32(b, 4),
                        active, u32(b, 4) == static_cast<uint32_t>(active) ? "==" : "!=", d.alsoIn.size());
            if (libv != cor) { std::printf("  !! library disagrees with bitwise reference\n"); ++failures; }
        }
        for (const auto& v : vars) {
            bool ok = v.expectAll ? v.c.match == v.c.total : v.c.match == 0;
            std::printf("  %-52s %d/%d %s\n", v.name, v.c.match, v.c.total,
                        ok ? (v.expectAll ? "(expected all)" : "(control: 0 as required)") : "*** UNEXPECTED ***");
            if (!ok) ++failures;
        }
    }

    // ---- 2. snapshots -------------------------------------------------------
    std::printf("\n== 2. snapshot hash (spec: init 0, no xorout, file[0x004,0x1A8E8) + 0x118 zero bytes) ==\n");
    {
        std::vector<Var> vars = {
            {"CORRECT  [4,0x1A8E8) + 0x118 zeros", {}, true},
            {"control  same range WITHOUT the zero tail", {}, false},
            {"control  whole file, hash field zeroed, NO tail", {}, false},
            {"control  zlib-style (FFFFFFFF, complemented), correct range", {}, false},
            {"control  start shifted BACK 1 byte: [3,0x1A8E8) + zeros", {}, false},
            {"control  start shifted FWD 5 bytes: [9,0x1A8E8) + zeros", {}, false},
            {"control  end shifted: [4,0x1A8E7) + 0x118 zeros", {}, false},
            {"control  zero tail 0x117 (one short)", {}, false},
            {"control  zero tail 0x119 (one long)", {}, false},
            {"control  one body byte flipped (in-memory copy)", {}, false},
            {"NON-DISCRIMINATING start [5,..)+tail (bytes 4..7 are 0)", {}, true},
            {"NON-DISCRIMINATING start [8,..)+tail (bytes 4..7 are 0)", {}, true},
            {"NON-DISCRIMINATING whole file, field zeroed, WITH tail", {}, true},
        };
        const size_t E = sr3save::kSnapshotFileSize;
        int libDisagree = 0;
        int nonzeroLeadBytes = 0;
        for (const auto& s : snaps) {
            const auto& b = s.bytes;
            uint32_t stored = u32(b, 0);
            auto run = [&](size_t from, size_t to, size_t tail, const std::vector<uint8_t>& src) {
                uint32_t c = crcBitwise(src.data() + from, to - from);
                return crcBitwiseZeros(tail, c);
            };
            std::vector<uint8_t> zf = b;
            zf[0] = zf[1] = zf[2] = zf[3] = 0;
            std::vector<uint8_t> flipped = b;
            flipped[0x5000] ^= 0x01;
            uint32_t cor = run(4, E, 0x118, b);
            uint32_t c[13] = {cor,
                              run(4, E, 0, b),
                              crcBitwise(zf.data(), E),
                              crcZlibStyle(b.data() + 4, E - 4, 0x118),
                              run(3, E, 0x118, b),
                              run(9, E, 0x118, b),
                              run(4, E - 1, 0x118, b),
                              run(4, E, 0x117, b),
                              run(4, E, 0x119, b),
                              run(4, E, 0x118, flipped),
                              run(5, E, 0x118, b),
                              run(8, E, 0x118, b),
                              crcBitwiseZeros(0x118, crcBitwise(zf.data(), E))};
            for (int i = 0; i < 13; ++i) vars[static_cast<size_t>(i)].c.add(c[i] == stored);
            for (size_t k = 4; k < 8; ++k) if (b[k] != 0) ++nonzeroLeadBytes;
            uint32_t libv = sr3save::computeSnapshotHash(vpp::ByteView(b.data(), b.size()));
            if (libv != cor) ++libDisagree;
            std::printf("  snap %-26s %s stored=%08X computed=%08X %s | date %s | in %zu other place(s)\n",
                        s.where.c_str(), s.name.c_str(), stored, cor, stored == cor ? "MATCH   " : "MISMATCH",
                        // date/time straight from the UTF-16 strings, for orientation only
                        [&] {
                            std::string d, t;
                            for (int i = 0; i < 8; ++i) d.push_back(static_cast<char>(b[0x72 + 2 * static_cast<size_t>(i)]));
                            for (int i = 0; i < 8; ++i) t.push_back(static_cast<char>(b[0x60 + 2 * static_cast<size_t>(i)]));
                            return d + " " + t;
                        }().c_str(),
                        s.alsoIn.size());
        }
        for (const auto& v : vars) {
            bool ok = v.expectAll ? v.c.match == v.c.total : v.c.match == 0;
            std::printf("  %-62s %2d/%d %s\n", v.name, v.c.match, v.c.total,
                        ok ? (v.expectAll ? "(all, as expected)" : "(control: 0 as required)") : "*** UNEXPECTED ***");
            if (!ok) ++failures;
        }
        std::printf("  non-zero bytes among file[4..8) over all %zu snapshots: %d (the reason a leading-edge shift inside [4,8] "
                    "is invisible to ANY init-0 CRC: leading zero bytes leave the state at 0)\n",
                    snaps.size(), nonzeroLeadBytes);
        if (nonzeroLeadBytes) ++failures;
        std::printf("  library computeSnapshotHash disagrees with bitwise reference on %d/%zu\n", libDisagree, snaps.size());
        if (libDisagree) ++failures;
    }

    std::printf("\nRESULT: %s (%d unexpected)\n", failures == 0 ? "ALL GATES AS EXPECTED" : "DISAGREEMENT", failures);
    return failures == 0 ? 0 : 1;
}
