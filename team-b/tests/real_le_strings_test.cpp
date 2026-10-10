// Real-data test for sr3locstr (spec-localization-format.md). Reads the US
// `.le_strings` tables out of misc.vpp_pc (and patch_compressed.vpp_pc, if
// present) in CRREISH_GAME_CACHE_DIR and exits 77 - SKIPPED, never a pass -
// when the archive is not there. Nothing from the archive is copied into
// the repository.
//
// Checks: Sec1's per-table record counts (replayed); Sec2.4's own worked
// example (voice_script_us.le_strings, byte-exact); Sec4's known key->text
// answers (CONTROL_OKAY/CONTROL_YES/CONTROL_NO/MENU_TITLE_WARNING/
// FOREIGN_CHARACTER_DETECTED_WARNING_MESSAGE); Sec8's validation sample
// (MAINMENU_DOWNLOADS, MENU_GAME_PAUSED, HORDE_MODE_GAMEPLAY_TITLE, the
// plain word `default`) against the real shipped tables, multi-table
// search order included (Sec6.1).
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3locstr/le_strings.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

using namespace sr3locstr;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool readEntry(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name != name) continue;
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            vpp::ByteView r = c.rawEntryBytes(i);
            out.assign(r.data(), r.data() + r.size());
            return true;
        }
        vpp::DecompressResult r = c.decompressEntry(i);
        if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
            r.status != vpp::DecodeStatus::ContentValidated)
            return false;
        out = std::move(r.data);
        return true;
    }
    return false;
}

std::u16string u16(const char* s) {
    std::u16string out;
    for (; *s; ++s) out.push_back(static_cast<char16_t>(static_cast<unsigned char>(*s)));
    return out;
}

} // namespace

int main() {
    const fs::path cache = CRREISH_GAME_CACHE_DIR;
    const fs::path miscPath = cache / "misc.vpp_pc";
    if (!fs::exists(miscPath)) {
        std::cout << "SKIP: " << miscPath.string() << " not present\n";
        return 77;
    }
    // Through the persistent decoded-asset cache (2026-10-08 task): same
    // vpp::Container, same byte-for-byte decompressEntry() results as the
    // old loadFile()+Container(...) pair - see vppcache/archive_cache.h's
    // file comment for why this is a safe drop-in.
    auto miscOpened = vppcache::openArchive(miscPath);
    const vpp::Container& misc = miscOpened->container;

    // --- Sec2.4's own worked example, parsed directly (never loaded by
    // the lookup - voice_script is excluded, Sec2.5/Sec5.3 - but still a
    // real shipped file this parser must read byte-exactly).
    {
        std::vector<uint8_t> vb;
        CHECK(readEntry(misc, "voice_script_us.le_strings", vb));
        if (!vb.empty()) {
            CHECK(vb.size() == 302);
            LeStringsTable t = parseLeStrings(vb);
            CHECK(t.bucketCount == 32);
            CHECK(t.recordCount == 1);
            CHECK(t.byHash.size() == 1);
            CHECK(t.byHash.count(0x3893e529u) == 1);
            if (t.byHash.count(0x3893e529u)) CHECK(t.byHash.at(0x3893e529u) == u16("I need that!"));
        }
    }

    // --- Sec1's per-table record counts (US), loading in Sec5.2 step 4's
    // order into one lookup.
    struct TableSpec { const char* file; const char* name; uint32_t expectRecords; };
    const TableSpec specs[] = {
        {"static_us.le_strings", "static", 1089},
        {"hud_us.le_strings", "hud", 1621},
        {"activity_us.le_strings", "activity", 818},
        {"diversion_us.le_strings", "diversion", 672},
        {"mission_us.le_strings", "mission", 739},
        {"menu_us.le_strings", "menu", 926},
        {"multiplayer_us.le_strings", "multiplayer", 76},
        {"customize_us.le_strings", "customize", 3060},
        {"subtitle_us.le_strings", "subtitle", 675},
        {"platform_pc_us.le_strings", "platform_pc", 525},
    };
    LeStringsLookup lk;
    size_t loaded = 0;
    for (const TableSpec& s : specs) {
        std::vector<uint8_t> bytes;
        if (!readEntry(misc, s.file, bytes) || bytes.empty()) {
            std::cerr << s.file << ": not found in misc.vpp_pc\n";
            ++g_failures;
            continue;
        }
        LeStringsTable t;
        try {
            t = parseLeStrings(bytes);
        } catch (const std::exception& ex) {
            std::cerr << s.file << ": " << ex.what() << "\n";
            ++g_failures;
            continue;
        }
        CHECK(t.recordCount == s.expectRecords);
        CHECK(t.byHash.size() == s.expectRecords); // no duplicate hash within a table (Sec1)
        lk.addTable(std::move(t));
        ++loaded;
    }
    CHECK(loaded == 10);

    // patch0 lives in patch_compressed.vpp_pc (Sec1), optional for this test.
    const fs::path patchPath = cache / "patch_compressed.vpp_pc";
    if (fs::exists(patchPath)) {
        auto patchOpened = vppcache::openArchive(patchPath);
        const vpp::Container& patch = patchOpened->container;
        std::vector<uint8_t> bytes;
        if (readEntry(patch, "patch0_us.le_strings", bytes) && !bytes.empty()) {
            LeStringsTable t = parseLeStrings(bytes);
            CHECK(t.recordCount == 29);
            lk.addTable(std::move(t));
        }
    }

    // --- Sec4's known key->text answers (CONFIRMED, replayed).
    CHECK(lk.lookupByKey("CONTROL_OKAY") == u16("OK"));
    CHECK(lk.lookupByKey("CONTROL_YES") == u16("YES"));
    CHECK(lk.lookupByKey("CONTROL_NO") == u16("NO"));
    CHECK(lk.lookupByKey("MENU_TITLE_WARNING") == u16("WARNING"));
    CHECK(lk.lookupByKey("FOREIGN_CHARACTER_DETECTED_WARNING_MESSAGE") ==
          u16("A player has an ID with unsupported characters. These characters now appear as \"_\"."));
    // Case-insensitive (Sec4).
    CHECK(lk.lookupByKey("control_okay") == u16("OK"));

    // --- Sec8's validation sample, against the real tables (multi-table
    // search order exercised for real: MAINMENU_DOWNLOADS/MENU_GAME_PAUSED/
    // HORDE_MODE_GAMEPLAY_TITLE resolve from `menu`, `default` from
    // `customize`).
    CHECK(lk.lookupByKey("MAINMENU_DOWNLOADS") == u16("DOWNLOADABLE CONTENT"));
    CHECK(lk.lookupByKey("MENU_GAME_PAUSED") == u16("GAME PAUSED"));
    CHECK(lk.lookupByKey("HORDE_MODE_GAMEPLAY_TITLE") == u16("WHORED MODE"));
    CHECK(lk.lookupByKey("default") == u16("DEFAULT"));
    // Sec8's 11 key-shaped strings that resolve nowhere (sample of 3).
    CHECK(!lk.lookupByKey("CONTENT_DESCRIPTION").has_value());
    CHECK(!lk.lookupByKey("KEYS_TEXT").has_value());
    CHECK(!lk.lookupByKey("TEXT_LOCALIZED").has_value());
    // A non-key placeholder string never resolves.
    CHECK(!lk.lookupByKey("PRICE").has_value());

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real_le_strings_test: all checks passed (" << loaded << " tables loaded)\n";
    return 0;
}
