// IMPLEMENTED PRE-REVIEW, PENDING CLEARANCE (manager rule 2026-09-30): the
// spec-lua-api-behaviour.md / spec-lua-bindings.md sections this file rests on
// are marked "NOT yet cleared for implementation" by the 2026-09-30 desk
// review (115/122 and 57/58 review-status lines). Kept working, behaviour
// unchanged, until Team A clears them; no new behaviour from those sections
// before then (team-b/HANDOFF.md section A, standing rule).
//
// The Lua 5.1 host scaffold's VERDICT TOOL: loads game_lib.lua for real and
// attempts to load/run real shipped mission/UI Lua scripts against the
// stub environment built by sr3luahost (include/sr3luahost/host.h), then
// reports what REALLY happened - never an inferred or invented result.
//
// Archive-walking method: copied and adapted, deliberately, from
// tools/lua_mission_ui_census.cpp's own walk() (same recursion into nested
// `.str2_pc` containers via vpp::Container::openNested()/decompressEntry(),
// same ".lua name suffix" + "Lua bytecode magic" leaf test) rather than
// re-invented - see that file's own top comment for the full population-
// finding rationale this reuses. Not refactored into a shared header this
// pass (the census's walk() is a local, unexported function) - duplicated
// with attribution instead, matching how vpp_dump.cpp/vpp_extract.cpp
// already each carry their own walk rather than sharing one.
//
// What this tool does that the census does NOT: the census only PARSES
// (sr3lua, a clean-room parser that never executes anything). This tool
// EXECUTES: a real luaL_loadbuffer() (a real Lua 5.1 syntax check, from the
// real reference implementation - a genuine cross-check against sr3lua's
// own parse verdict on the same file) and, if that succeeds, a real
// lua_pcall() of the loaded chunk's top-level code against a real,
// persistent lua_State that already has every stub name from the current
// tagged registration list registered (originally 1,430 names, corrected
// 2026-09-29 to 1,435 - spec-lua-bindings.md Sec13.6; see this file's own
// `main()` for which file a given run actually loads - not repeated as a
// fixed number here so this comment cannot go stale the way a hardcoded
// count would) (and, for the gameplay state, game_lib.lua already loaded -
// see below).
//
// IMPORTANT, stated plainly per this task's own instructions: lua_pcall
// only executes a script's TOP-LEVEL statements. A script that consists
// mostly of `function some_hook_name() ... end` definitions (the named-
// hook pattern spec-lua-bindings.md Sec4/Sec8 documents) will load and run
// cleanly without ever calling most of its own stubs - the engine would
// call those hook functions later, by name, at real gameplay trigger
// points this scaffold does not simulate. So a script "running to
// completion" with LOW stub-hit counts is an expected, structurally
// honest result for this kind of file, not a sign the harness is broken -
// see the population summary this tool prints for the real, measured
// breakdown.
//
// HOOK-FIRING PASS (added this task, per spec-lua-bindings.md Sec8.2-8.4/
// Sec12.2-12.9): closes the gap the paragraph above describes. After each
// script's own top-level runChunk has been attempted (regardless of that
// attempt's own pcall result), this tool now also attempts every
// confirmed/pattern-based hook name (sr3luahost::confirmedHooks(),
// include/sr3luahost/hook_registry.h - originally 133 [73 Group-1 general
// hooks + 2 Group-2 unconditional lifecycle hooks + 58 Group-3
// mission-numbered pattern-based hooks], extended to 138 in a same-day
// follow-up pass with 5 more Group-1 names from spec-lua-bindings.md
// Sec14.6 - see hook_registry.h for the live, authoritative count/table,
// not repeated as a fixed number in this comment) against the SAME
// persistent state that script was loaded into (Host::fireConfirmedHooks,
// host.h). Most hooks are still called with ZERO arguments - spec Sec8.3
// confirms the call-in MECHANISM only, not per-hook argument shapes, which
// remain OPEN for most names (see hook_registry.h's own top comment) -
// stated here plainly per this project's "no invented fixes" policy, not
// silently assumed. A small, explicitly-enumerated set of hooks DOES now
// push real, spec-confirmed argument values (spec-lua-bindings.md Sec14) -
// see HookSpec::args's own doc comment (hook_registry.h) for exactly
// which ones and why.
//
// IMPORTANT, stated plainly: because gameplay_/ui_ are PERSISTENT,
// shared states across the WHOLE 804-script population (not one isolated
// state per script), a hook name multiple scripts both define will, by
// the time script N's own hook-firing runs, actually invoke whichever
// script's definition currently happens to be live in that shared global
// table - not necessarily script N's own, since a later script's
// top-level run can overwrite a name an earlier script also defined. This
// is a pre-existing property of this scaffold's persistent-state design
// (HANDOFF.md Sec9.113), not something new to this pass, and is stated
// here rather than silently papered over. Firing per-script (rather than
// once at the very end) still produces a genuinely different, meaningful
// per-row signal: each script's row reflects the cumulative state of
// "every script tried so far, in real walk order" at that point.
//
// This produces a NEW stub-hit ranking that reflects real hook-triggered
// call volume, reported ALONGSIDE (never overwriting) the original
// top-level-only ranking - see verdict_stub_hits.tsv (unchanged,
// top-level-only, historical - reproduces HANDOFF.md Sec9.113's own
// 56-call/5-name baseline exactly) vs verdict_stub_hits_with_hooks.tsv
// (new, all-inclusive). Per-hook fire counts land in
// verdict_hook_fires_by_hook.tsv (aggregate) and
// verdict_hook_fires_detail.tsv (the full per-script-per-hook-per-state
// record this task's own instructions asked to track separately from
// top-level pcall success).
//
// A real, found risk this pass's own hook-firing exposes that the
// top-level-only pass never could (nothing calls into a hook body from
// the top-level pass): a hook body may contain a busy-poll loop against
// an always-nil-returning stub (real game_lib.lua text, grepped directly,
// has the equivalent shape: `repeat thread_yield() until
// teleport_check_done(REMOTE_PLAYER)`, and every registered stub always
// returns nil/falsy). Host::fireConfirmedHooks
// installs a real Lua 5.1 instruction-count watchdog (lua_sethook +
// LUA_MASKCOUNT) around each individual hook call to convert a genuine
// infinite loop into a real, recorded Lua error instead of hanging this
// tool - see host.h's own doc comment on fireConfirmedHooks. This
// watchdog is NEVER applied to runChunk() (the top-level pass above), so
// it cannot perturb the already-independently-verified Sec9.113 numbers.
//
// The sr3luahost::ThreadScheduler-backed thread_new/thread_yield/
// thread_kill/thread_check_done/thread_close registration (host.h/
// thread_scheduler.h) is wired into Host's constructor directly (both
// states) - nothing in this tool needs to touch it beyond letting scripts
// and hook bodies call it; its own hit counts fold into the same HitLog
// ranking as every other stub (see thread_scheduler.h's own top comment
// for the real evidence and the honest scope limits of this minimal
// scheduler).
//
// Usage: lua_host_run <cache_dir> <registered_tagged.txt> <out_dir>
//        [max_scripts_to_run]
// `<registered_tagged.txt>` should be the CURRENT tagged registration list
// (tools/lua_all_registered_1435_tagged.txt as of 2026-09-29, superseding
// the now-stale tools/lua_all_registered_1430_tagged.txt - spec-lua-
// bindings.md Sec13.6); the tool reads whatever count that file actually
// contains (allNames.size()) and reports that real count everywhere below,
// never a hardcoded population number.
// `max_scripts_to_run` (default: run every script found - the real 804-
// script population, subject to whatever this run's cache_dir actually
// contains) caps how many of the FOUND scripts get load/run-attempted,
// after game_lib.lua's own dedicated first attempt; the walk itself always
// covers every archive regardless of this cap, so the population
// denominators this tool prints are always the real, whole-archive counts.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sr3asm/manifest.h"
#include "sr3lua/parser.h"
#include "sr3luahost/host.h"
#include "sr3luahost/hook_registry.h"
#include "sr3tables_cutscene/scene_table.h"
#include "sr3tables_progression/tables.h"
#include "sr3tables_vehicle_world/world_items.h"
#include "sr3tables_weapons/combat.h"
#include "sr3vehicleinfo/vehicle_entry.h"
#include "sr3vintdoc/vint_doc.h"
#include "sr3zone/zone_objects.h"
#include "vpp/container.h"

namespace fs = std::filesystem;
using sr3luahost::LoadedVintDocument;
using sr3luahost::bareGlobalRoster;
using sr3luahost::BareThreadTable;
using sr3luahost::confirmedHooks;
using sr3luahost::HookGroup;
using sr3luahost::hookGroupLabel;
using sr3luahost::hookGroupNote;
using sr3luahost::HookSpec;
using sr3luahost::Host;
using sr3luahost::loadTaggedRegistrationList;
using sr3luahost::RegisteredName;

namespace {

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("could not open: " + path.string());
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size))
        throw std::runtime_error("failed reading: " + path.string());
    return buf;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool endsWithLuaExt(const std::string& lowerName) {
    static const std::string ext = ".lua";
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

// --- The zscene / cutscene scene table (spec-lua-api-behaviour.md Sec26.25
// item 5, CONFIRMED; job nnlt) ------------------------------------------------
// 0x0073bfb0's "main" call: names from cutscene.xtbl (inside
// cutscene_tables.vpp), then one <name>.cte_xtbl per name. Where the files
// come from: the engine finds an entry by "a linear, case-insensitive,
// name-string search across every mounted container's directory"
// (spec-vpp-container.md Sec2). CHOSEN: this host searches the one container
// cutscene.xtbl is in, cutscene_tables.vpp_pc at the cache root (which
// containers the engine has mounted at that moment is not specced; every
// shipped .cte_xtbl is in that container). Patch containers: which exist and
// when they are parsed is OPEN, so only the "main" call runs. No container ->
// nothing installed, the table stays OPEN.
// Returns the summary for verdict_summary.txt's zscene_table= line.
std::string installSceneTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    fs::path archivePath;
    for (auto& ent : fs::directory_iterator(cacheDir)) {
        if (ent.is_regular_file() && toLower(ent.path().filename().string()) == "cutscene_tables.vpp_pc") {
            archivePath = ent.path();
            break;
        }
    }
    if (archivePath.empty()) return "not installed (no cutscene_tables.vpp_pc in the cache; the table stays OPEN)";
    std::vector<uint8_t> bytes;
    try {
        bytes = readFile(archivePath);
        vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
        // Entry bytes by case-insensitive name (raw or zlib).
        auto entryBytes = [&c](const std::string& wanted, std::string* foundName) -> std::optional<std::string> {
            const std::string w = toLower(wanted);
            const auto& entries = c.entries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (toLower(entries[i].name) != w) continue;
                if (foundName) *foundName = entries[i].name;
                if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
                }
                vpp::DecompressResult r = c.decompressEntry(i);
                if (r.status != vpp::DecodeStatus::Ok) return std::nullopt;
                return std::string(reinterpret_cast<const char*>(r.data.data()), r.data.size());
            }
            return std::nullopt;
        };
        std::optional<std::string> names = entryBytes("cutscene.xtbl", nullptr);
        if (!names) return "not installed (cutscene.xtbl not readable in " + archivePath.filename().string() + ")";
        sr3tables_cutscene::SceneTable table = sr3tables_cutscene::BuildMainSceneTable(
            *names, [&entryBytes](const std::string& fileName) -> std::optional<sr3tables_cutscene::OpenedFile> {
                sr3tables_cutscene::OpenedFile f;
                std::optional<std::string> b = entryBytes(fileName, &f.fileName);
                if (!b) return std::nullopt;
                f.bytes = std::move(*b);
                return f;
            });
        std::vector<sr3luahost::EngineState::ZsceneTableRow> rows;
        size_t kind0 = 0, kind1 = 0, kind2 = 0, kindOpen = 0, lightsetUnterminated = 0, soundtrackSplitOpen = 0;
        size_t cameraScriptNames = 0;
        for (const auto& e : table.entries) {
            sr3luahost::EngineState::ZsceneTableRow row;
            row.name = e.name;
            row.crc = e.crc;
            row.kind = e.fields.kind;
            row.cameraScriptName = e.fields.cameraScriptText; // Sec57.2a/Sec10.6 item 7 wiring (2026-10-08)
            if (row.cameraScriptName) ++cameraScriptNames;
            if (e.fields.resources) {
                std::vector<std::string> res;
                for (const auto& r : *e.fields.resources) {
                    res.push_back(r.source == sr3tables_cutscene::SceneResource::Source::AngelFixedNames
                                      ? std::string("(IsAngel: three fixed names, OPEN)")
                                      : (r.textPresent ? r.text : std::string("(Variant absent, OPEN)")));
                }
                row.resources = std::move(res);
            }
            rows.push_back(std::move(row));
            if (!e.fields.kind) ++kindOpen;
            else if (*e.fields.kind == 0) ++kind0;
            else if (*e.fields.kind == 1) ++kind1;
            else ++kind2;
            if (e.fields.lightsetUnterminated) ++lightsetUnterminated;
            if (e.fields.soundtrackSplitOpen) ++soundtrackSplitOpen;
        }
        const size_t installed = rows.size();
        es.installZsceneTable(std::move(rows));

        // Real camera-script byte source (spec-lua-api-behaviour.md
        // Sec57.2a / spec-cutscene-camera-format.md Sec10.6 item 7,
        // 2026-10-08 wiring task): EngineState::cutsceneCameraScriptStep()
        // calls this, by name, for a Zscene/Story-kind scene entry.
        // CHOSEN scope limitation (stated plainly, not hidden): this only
        // searches the SAME container cutscene.xtbl/the .cte_xtbl files
        // came from (archivePath, captured below by an owned COPY of its
        // bytes so the opener outlives this function) - not the real
        // engine's own "linear, case-insensitive, name-string search
        // across every mounted container's directory" (spec-vpp-
        // container.md Sec2, the same rule installSceneTableFromCache's
        // own doc comment above already simplifies this way for
        // .cte_xtbl). A name not found in this one container answers
        // nullopt, which cutsceneCameraScriptStep treats as a REAL
        // "missing script aborts the load" outcome, not a crash.
        auto archiveBytes = std::make_shared<std::vector<uint8_t>>(bytes);
        es.installCameraScriptOpener(
            [archiveBytes](const std::string& fileName) -> std::optional<std::vector<uint8_t>> {
                try {
                    vpp::Container cc(vpp::ByteView(archiveBytes->data(), archiveBytes->size()));
                    const std::string w = toLower(fileName);
                    const auto& entries = cc.entries();
                    for (size_t i = 0; i < entries.size(); ++i) {
                        if (toLower(entries[i].name) != w) continue;
                        if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                            vpp::ByteView raw = cc.rawEntryBytes(i);
                            return std::vector<uint8_t>(raw.data(), raw.data() + raw.size());
                        }
                        vpp::DecompressResult r = cc.decompressEntry(i);
                        if (r.status != vpp::DecodeStatus::Ok) return std::nullopt;
                        return r.data;
                    }
                    return std::nullopt;
                } catch (const std::exception&) {
                    return std::nullopt;
                }
            });

        return "installed source=" + archivePath.filename().string() +
               " cutscene_elements=" + std::to_string(table.names.cutsceneElements) +
               " names_accepted=" + std::to_string(table.names.accepted.size()) +
               " names_skipped=" + std::to_string(table.names.skipped.size()) +
               " names_case_ambiguous=" + std::to_string(table.names.caseAmbiguous.size()) +
               " capacity=" + std::to_string(table.capacity) + " entries=" + std::to_string(installed) +
               " missing_scene_files=" + std::to_string(table.missingSceneFiles.size()) +
               " unparseable_scene_files=" + std::to_string(table.unparseableSceneFiles.size()) +
               " kind0_designer=" + std::to_string(kind0) + " kind1_zscene=" + std::to_string(kind1) +
               " kind2_story=" + std::to_string(kind2) + " kind_open=" + std::to_string(kindOpen) +
               " lightset_unterminated=" + std::to_string(lightsetUnterminated) +
               " soundtrack_split_open=" + std::to_string(soundtrackSplitOpen) +
               " camera_script_names=" + std::to_string(cameraScriptNames);
    } catch (const std::exception& ex) {
        return std::string("not installed (") + ex.what() + ")";
    }
}

// --- continuous_explosions.xtbl definition table (spec-lua-api-behaviour.md
// Sec28.16 "`continuous_explosion_start`"; spec-tables-weapons-combat.md
// Sec10.2) --------------------------------------------------------------
// The real runtime table (0x014A0310, stride 0x64, cap 3 rows) is read once
// at start-up from continuous_explosions.xtbl via the resolver 0x005eb390 -
// a PLAIN cdecl lookup, NOT the shared 0x02442750 singleton family (Sec28.16
// Body). The file exists in TWO real archives in the real cache with
// different content: misc_tables.vpp_pc (1 row) and patch_compressed.vpp_pc
// (3 rows - exactly the loader's own cap, with zero rows rejected by any of
// Sec10.2's own range checks - spec-tables-weapons-combat.md Sec18.1/
// Sec18.5's own population census, "row count 3 = the loader's own cap
// exactly"). Which container the live game actually mounts LAST for this
// filename (so it is the one 0x005eb390's table-build reads) is not specced
// - Sec10.2's own review status is "NEEDS-DATA" on exactly this question.
// CHOSEN: this host prefers patch_compressed.vpp_pc when present in the
// cache (the copy whose row count already matches the documented cap with
// nothing rejected - the stronger of the two signals for "this is the
// operative copy"), falling back to misc_tables.vpp_pc's copy only if no
// patch_compressed.vpp_pc is in the cache. No DLC archive carries this
// table (Sec10.2), so no further container is searched. Returns the summary
// for verdict_summary.txt's continuous_explosion_table= line.
std::string installContinuousExplosionTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const char* const kPreferredArchives[] = {"patch_compressed.vpp_pc", "misc_tables.vpp_pc"};
    std::string notFoundArchives;
    for (const char* archiveName : kPreferredArchives) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == archiveName) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!notFoundArchives.empty()) notFoundArchives += ", ";
            notFoundArchives += archiveName;
            continue;
        }
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            const auto& entries = c.entries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (toLower(entries[i].name) != "continuous_explosions.xtbl") continue;
                std::string fileBytes;
                if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                } else {
                    vpp::DecompressResult r = c.decompressEntry(i);
                    if (r.status != vpp::DecodeStatus::Ok) {
                        return "not installed (continuous_explosions.xtbl in " + archivePath.filename().string() +
                               " failed to decompress)";
                    }
                    fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                }
                sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                std::vector<sr3tables_weapons::ContinuousExplosion> rows = sr3tables_weapons::ParseAllContinuousExplosions(doc);
                std::vector<std::string> ceNames;
                for (const auto& row : rows) {
                    // Every parsed row becomes a real, resolvable definition
                    // name (CONFIRMED set, this host does not reproduce
                    // Sec10.2's own per-row reject-and-drop range checks -
                    // the real population census already found both real
                    // candidate files' rows clean against them).
                    ceNames.push_back(row.name);
                }
                es.continuousExplosion().installNames(ceNames); // table COMPLETE: unknown name = silent no-op (Sec28.16)
                return "installed source=" + archivePath.filename().string() + " rows=" + std::to_string(rows.size());
            }
        } catch (const std::exception& ex) {
            return std::string("not installed (") + ex.what() + ")";
        }
    }
    return "not installed (continuous_explosions.xtbl not found in " + notFoundArchives +
           "; the table stays OPEN)";
}

// --- weapons.xtbl weapon-definition table (spec-lua-api-behaviour.md
// Sec4.10/Sec4.11 "inv_weapon_add_temporary"/"inv_weapon_remove_temporary";
// EngineState::weaponDefinitionResolves() - 2026-10-09 fast-follow, probe
// blockers `m13` ['molotov'], `m19` ['special-sonicgun'], `m24`
// ['pistol-gang']/['rifle-stag']) ----------------------------------------
// The real runtime array both natives' shared `_stricmp` scan searches
// (0x028dc4dc, stride 0x7ec) is NOT a separate thing from this project's
// own already-built, already-proven sr3tables_weapons::Weapon row reader -
// weapons.h's own file-level CONVENTIONS comment documents the exact same
// 0x7EC record stride for the SAME `weapons.xtbl` `<Weapon>` row (spec-
// tables-weapons-combat.md Sec16.4's independent structural cross-check:
// "the last field the weapons reader writes ends exactly at the record
// stride 0x7EC"), so every real `<Weapon>` row this already-existing reader
// successfully parses IS, by construction, a row that would be loaded into
// that exact runtime array - the array's own "loaded" filter bit
// (Sec4.10's own +0x18 bit 0x4) is pure loader-lifecycle bookkeeping, never
// driven by `<Flag>` XML text (weapons.h's own file-level comment already
// puts it out of scope for exactly this reason: a per-row/per-file static
// reader has no "not yet loaded" state of its own to represent). spec-
// tables-weapons-combat.md Sec18.3/Sec18.4's own real-data population
// census found all 82 real base rows structurally clean (every enum/flag
// value documented, no capacity violations, "no value outside any
// documented set was found anywhere in the 82 rows") - this host does not
// reproduce a reject-and-drop loader check for the same reason
// installContinuousExplosionTableFromCache above does not: the real
// population is already known clean against them.
//
// Archives (Sec18.1/Sec18.2, Sec16 - both CONFIRMED, empirical): the 82
// base rows live in `misc_tables.vpp_pc` and ONLY there - unlike
// continuous_explosions.xtbl/spawn_info_ranks.xtbl above, `weapons.xtbl`
// has no larger `patch_compressed.vpp_pc` copy, so there is no archive-
// precedence question to resolve here. The two real DLC extensions
// (`dlc2_weapons.xtbl`, 6 rows; `dlc3_weapons.xtbl`, 3 rows - no DLC1
// weapon-group file exists at all, Sec16) are each a raw top-level entry of
// their own numbered DLC archive ("the DLC archives store their entries
// raw, so their tables are trustworthy", Sec16 Sources). All rows found,
// across every one of these files present, are merged into ONE resolver -
// the real engine's own loader builds the base pass then applies the DLC
// passes into the SAME runtime array (Sec2/Sec2.6), never a separate table
// per archive.
std::string installWeaponDefinitionTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    struct ArchiveFile {
        const char* archive;
        const char* xtbl;
    };
    static const ArchiveFile kFiles[] = {
        {"misc_tables.vpp_pc", "weapons.xtbl"},
        {"dlc2.vpp_pc", "dlc2_weapons.xtbl"},
        {"dlc3.vpp_pc", "dlc3_weapons.xtbl"},
    };
    std::string notFound;
    size_t filesInstalled = 0;
    size_t totalRows = 0;
    std::vector<std::string> allNames;
    for (const ArchiveFile& af : kFiles) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == toLower(std::string(af.archive))) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!notFound.empty()) notFound += ", ";
            notFound += std::string(af.archive) + " (for " + af.xtbl + ")";
            continue;
        }
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            const auto& entries = c.entries();
            bool found = false;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (toLower(entries[i].name) != toLower(std::string(af.xtbl))) continue;
                found = true;
                std::string fileBytes;
                if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                } else {
                    vpp::DecompressResult r = c.decompressEntry(i);
                    if (r.status != vpp::DecodeStatus::Ok) {
                        if (!notFound.empty()) notFound += ", ";
                        notFound += std::string(af.xtbl) + " (failed to decompress in " + archivePath.filename().string() + ")";
                        break;
                    }
                    fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                }
                sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                std::vector<sr3tables_weapons::Weapon> rows = sr3tables_weapons::ParseAllWeapons(doc);
                for (const auto& row : rows) {
                    // Every parsed row becomes a real, resolvable weapon-
                    // definition name (CONFIRMED set, this function's own top
                    // comment) - case-folded via the SAME AsciiLowerFold the
                    // native stubs' own `_stricmp`-equivalent lookup already
                    // uses (engine_state.h's weaponDefinitionResolves() doc
                    // comment).
                    allNames.push_back(row.name);
                    es.setWeaponDefinitionClass(row.name, row.weaponClass);
                }
                ++filesInstalled;
                totalRows += rows.size();
                break;
            }
            if (!found) {
                if (!notFound.empty()) notFound += ", ";
                notFound += std::string(af.xtbl) + " (not found in " + archivePath.filename().string() + ")";
            }
        } catch (const std::exception& ex) {
            if (!notFound.empty()) notFound += ", ";
            notFound += std::string(af.xtbl) + " (" + ex.what() + ")";
        }
    }
    if (filesInstalled > 0) es.installWeaponDefinitionNames(allNames);
    std::string summary = "installed files=" + std::to_string(filesInstalled) + "/" +
                           std::to_string(sizeof(kFiles) / sizeof(kFiles[0])) +
                           " rows=" + std::to_string(totalRows) +
                           " names_resolved=" + std::to_string(es.weaponDefinitionResolves().knownCount());
    if (!notFound.empty()) summary += " missing=" + notFound;
    return summary;
}

// items_inventory.xtbl (+ DLC) row names -> inv_item_remove resolver (spec-lua-api-behaviour.md Sec17.25
// "Item lookup source", spec-tables-vehicle-world.md Sec15; DLC files are <framework>_items_inventory.xtbl).
std::string installInventoryItemTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const std::pair<const char*, const char*> kFiles[] = {
        {"misc_tables.vpp_pc", "items_inventory.xtbl"}, {"dlc1.vpp_pc", "dlc1_items_inventory.xtbl"},
        {"dlc2.vpp_pc", "dlc2_items_inventory.xtbl"}, {"dlc3.vpp_pc", "dlc3_items_inventory.xtbl"}};
    std::vector<std::string> names;
    size_t files = 0;
    for (const auto& af : kFiles) {
        try {
            for (auto& ent : fs::directory_iterator(cacheDir)) {
                if (!ent.is_regular_file() || toLower(ent.path().filename().string()) != af.first) continue;
                std::vector<uint8_t> bytes = readFile(ent.path());
                vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
                const auto& entries = c.entries();
                for (size_t i = 0; i < entries.size(); ++i) {
                    if (toLower(entries[i].name) != af.second) continue;
                    std::string fileBytes;
                    if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                        vpp::ByteView raw = c.rawEntryBytes(i);
                        fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                    } else {
                        vpp::DecompressResult r = c.decompressEntry(i);
                        if (r.status != vpp::DecodeStatus::Ok) break;
                        fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                    }
                    for (const auto& it : sr3tables_vehicle_world::ParseItemsInventoryTable(sr3xtbl::ParseDocument(fileBytes)))
                        if (it.name) names.push_back(*it.name);
                    ++files;
                    break;
                }
                break;
            }
        } catch (const std::exception&) {}
    }
    if (files > 0) es.installInventoryItemNames(names);
    return "installed files=" + std::to_string(files) + "/4 names=" + std::to_string(names.size());
}

// --- spawn_info_ranks.xtbl Hit_Points values (spec-tables-progression.md
// Sec10.6/Sec14.12; EngineState::drawOrGetMaxHitPoints, spec-lua-api-behaviour.md
// Sec7.12/Sec34.2 - 2026-10-07 closure of the max-hit-points random-per-
// spawn model, probe_blockers_ranked_20261007.md item 2, mission `m18`)
// -------------------------------------------------------------------------
// This is NOT a resolver (no attempt is made here to pick which <Rank>
// record applies to a given bare Lua name - that resolution is this
// project's own unmodeled gap, see CharacterState::maxHitPoints's own doc
// comment). It only installs the REAL set of distinct Hit_Points values the
// table actually ships, so drawOrGetMaxHitPoints's own uniform draw is
// grounded in real shipped data rather than an invented range.
//
// SAME two-archive situation as continuous_explosions.xtbl above, same
// archive-priority mechanism (spec-tables-progression.md Sec14.2's own
// boot/mount trace, generalized explicitly to this table in that same
// section's text, and restated at Sec14.12): `misc_tables.vpp_pc` carries a
// 54-row copy, `patch_compressed.vpp_pc` carries a 55-row superset (the
// extra row is `Homie - Kwilanna`) and the patch copy is the one the real
// shipped game actually loads. 2026-10-07 fast-follow fix (flagged, not
// fixed, by commit 50bdb29's own reviewer): this loader used to read ONLY
// misc_tables.vpp_pc, skipping that same precedence rule - non-observable
// so far only because both archives happen to yield the same 30 distinct
// Hit_Points values (`Homie - Kwilanna`'s own value is a duplicate of one
// already in the 54-row set), not because the rule didn't matter. CHOSEN:
// same kPreferredArchives order as installContinuousExplosionTableFromCache
// above - prefer patch_compressed.vpp_pc, fall back to misc_tables.vpp_pc
// only if no patch_compressed.vpp_pc is in the cache. No DLC archive
// carries this table either (same census method as the continuous-
// explosion loader above). Returns the summary for verdict_summary.txt's
// spawn_rank_hit_points= line.
std::string installSpawnRankHitPointsFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const char* const kPreferredArchives[] = {"patch_compressed.vpp_pc", "misc_tables.vpp_pc"};
    std::string notFoundArchives;
    for (const char* archiveName : kPreferredArchives) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == archiveName) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!notFoundArchives.empty()) notFoundArchives += ", ";
            notFoundArchives += archiveName;
            continue;
        }
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            const auto& entries = c.entries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (toLower(entries[i].name) != "spawn_info_ranks.xtbl") continue;
                std::string fileBytes;
                if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                } else {
                    vpp::DecompressResult r = c.decompressEntry(i);
                    if (r.status != vpp::DecodeStatus::Ok) {
                        return "not installed (spawn_info_ranks.xtbl in " + archivePath.filename().string() +
                               " failed to decompress)";
                    }
                    fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                }
                sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                std::vector<sr3tables_progression::SpawnRank> rows = sr3tables_progression::ParseSpawnInfoRanksTable(doc);
                std::set<int32_t> distinctValues;
                for (const auto& row : rows) distinctValues.insert(static_cast<int32_t>(row.hitPoints.value));
                std::vector<int32_t> values(distinctValues.begin(), distinctValues.end());
                es.installSpawnRankHitPoints(values);
                return "installed source=" + archivePath.filename().string() + " rows=" + std::to_string(rows.size()) +
                       " distinct_hit_points_values=" + std::to_string(values.size());
            }
        } catch (const std::exception& ex) {
            return std::string("not installed (") + ex.what() + ")";
        }
    }
    return "not installed (spawn_info_ranks.xtbl not found in " + notFoundArchives +
           "; the draw falls back to the CHOSEN 100)";
}

// --- Vehicle-type class table real-cache install (2026-10-10, vehicle-class
// xtbl wiring task): wires the existing, already-correct sr3vehicleinfo xtbl
// reader (include/sr3vehicleinfo/vehicle_entry.h, spec-vehicle-data.md
// section 7) into this host for the first time. Previously
// sr3vehicleinfo::ParseAllVehicles was a fully-built, standalone parser with
// nothing in this project's scope ever feeding its output into the Lua
// host's own EngineState - this closes that gap.
//
// Archive list: the 4 archives spec-vehicle-data.md Sec7.9 confirms carry
// real, readable per-vehicle-type tables once the mode-(a) container fix
// landed ("all real <name>_veh.xtbl files are now readable: 106 in
// misc_tables.vpp_pc + 17 in dlc1/2/3.vpp_pc = 123 total").
//
// MUST run before installZoneObjectsFromCache() below (see call order in
// main()) - EngineState::loadZoneObjectsSection() looks up a zone-placed
// vehicle's own `script_vehicle_type_name` property against this table at
// ingest time; installing the table after ingestion would silently miss
// every real vehicle instance ingested before it (installZoneObjectsFromCache's
// own re-ingestion is NOT performed by this tool, so the table must already
// be in place the first and only time each file is ingested).
//
// Recursive walk (same small-per-tool, duplicated-with-attribution
// convention as walkCzn() below, and validate_vehicleinfo_population.cpp's
// own walk()) rather than a flat per-archive scan - matches that tool's own
// finding that a real `<name>_veh.xtbl` entry can sit inside a nested
// container, not only at an archive's own top level.
std::string installVehicleTypeClassTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const char* const kArchives[] = {"misc_tables.vpp_pc", "dlc1.vpp_pc", "dlc2.vpp_pc", "dlc3.vpp_pc"};
    std::unordered_map<std::string, int32_t> classByLowerName;
    size_t filesParsed = 0;
    std::string notFoundArchives;

    std::function<void(vpp::Container&)> walk = [&](vpp::Container& c) {
        const auto& entries = c.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string& name = entries[i].name;
            const bool isVeh = name.size() > 9 && toLower(name.substr(name.size() - 9)) == "_veh.xtbl";
            if (isVeh) {
                try {
                    std::string fileBytes;
                    if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                        vpp::ByteView raw = c.rawEntryBytes(i);
                        fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                    } else {
                        vpp::DecompressResult r = c.decompressEntry(i);
                        if (r.status != vpp::DecodeStatus::Ok) continue;
                        fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                    }
                    sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                    std::vector<sr3vehicleinfo::VehicleEntry> rows = sr3vehicleinfo::ParseAllVehicles(doc);
                    for (const auto& row : rows) {
                        if (!row.name.has_value() || row.name->empty()) continue;
                        if (row.vehicleType.vehicleClass == sr3vehicleinfo::VehicleClass::Unknown) continue;
                        classByLowerName[sr3luahost::AsciiLowerFold(*row.name)] =
                            static_cast<int32_t>(row.vehicleType.vehicleClass);
                    }
                    ++filesParsed;
                } catch (const std::exception&) {
                    // malformed/unreadable entry - skip, never fabricated
                }
                continue;
            }
            if (entries[i].payload.kind != vpp::PayloadKind::Raw) continue;
            if (toLower(name).size() >= 4 && toLower(name).compare(toLower(name).size() - 4, 4, "xtbl") == 0) continue;
            try {
                vpp::Container nested(c.rawEntryBytes(i));
                walk(nested);
            } catch (const std::exception&) {
                // not a nested container - not every raw entry is one
            }
        }
    };

    for (const char* archiveName : kArchives) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == archiveName) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!notFoundArchives.empty()) notFoundArchives += ", ";
            notFoundArchives += archiveName;
            continue;
        }
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c);
        } catch (const std::exception& ex) {
            if (!notFoundArchives.empty()) notFoundArchives += ", ";
            notFoundArchives += std::string(archiveName) + " (" + ex.what() + ")";
        }
    }

    es.installVehicleTypeClassTable(classByLowerName);
    std::string summary = "installed files=" + std::to_string(filesParsed) +
                           " types_resolved=" + std::to_string(es.vehicleTypeClassTableSize());
    if (!notFoundArchives.empty()) summary += " missing=" + notFoundArchives;
    return summary;
}

// --- Zone Objects (0x2234) real-cache install (2026-10-08, zone-data
// fast-follow): wires sr3zone::ZoneObjects - the existing, already-proven
// `.czn_pc` Objects reader (include/sr3zone/zone_objects.h, spec-zone-
// data-format.md Sec11) - into this host's own named-object lookup
// (EngineState::loadZoneObjectsSection(), include/sr3luahost/
// zone_object_resolver.h) for the first time, against REAL shipped zone
// data. Previously NOTHING in this project's scope ever fed a real
// `.czn_pc` into the Lua host - objectResolves()/CharacterState::
// scriptNpcHp were only ever reachable via a test manually calling
// markScriptNpcBoundForTesting()/applyScriptNpcHitPointsForTesting()
// (commit bc7ff16). This closes that gap for a real mission-drive run.
//
// Archive list: the SAME 10 archives spec-zone-data-format.md's own Sec9.4
// population census uses ("every `.czn_pc`/`.gzn_pc` filename pair found
// ... in sr3_city_0.vpp_pc, sr3_city_1.vpp_pc, sr3_city_missions.vpp_pc,
// dlc1.vpp_pc, dlc2.vpp_pc, dlc3.vpp_pc, misc.vpp_pc, startup.vpp_pc,
// patch_compressed.vpp_pc, patch_uncompressed.vpp_pc - 1,083 pairs") - a
// CHOSEN, spec-backed scope (not "every archive in the cache", and not a
// guess at which archives matter).
//
// A third, dedicated recursive walk (leaf test: ".czn_pc") - same
// duplicated-with-attribution convention as the walk()/walkAsm() pair
// below this function (this file's own top comment: a small recursive
// walk per tool/per leaf-type, not factored into one shared traversal).
// Unlike those two, this walk ingests each leaf immediately (one
// sr3zone::ZoneObjects::locate() + EngineState::loadZoneObjectsSection()
// call per `.czn_pc` found) rather than collecting every buffer first -
// 1,083 real files is not large, but there is no reason to hold them all
// in memory at once when each one is fully consumed on sight.
namespace zoneobj {

bool endsWithCznExt(const std::string& lowerName) {
    static const std::string ext = ".czn_pc";
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

struct Counters {
    uint64_t cznFilesFound = 0;
    uint64_t cznWithSection = 0;
    uint64_t cznSectionsIncomplete = 0; // located but ZoneObjects' own recordCount could not be satisfied - skipped, not ingested
};

void ingestCznBuffer(sr3luahost::EngineState& es, Counters& ctr, const uint8_t* data, size_t len) {
    ++ctr.cznFilesFound;
    std::vector<sr3zone::ObjectsSection> sections;
    try {
        sections = sr3zone::ZoneObjects::locate(vpp::ByteView(data, len));
    } catch (const std::exception&) {
        return; // malformed candidate - ZoneObjects::locate() already only returns bounds-checked sections; never silently fabricated
    }
    for (const sr3zone::ObjectsSection& section : sections) {
        if (!section.complete) {
            ++ctr.cznSectionsIncomplete;
            continue; // see ObjectsSection::complete's own doc comment - never ingest a section the reader itself flagged as truncated
        }
        ++ctr.cznWithSection;
        es.loadZoneObjectsSection(section);
    }
}

void walkCzn(sr3luahost::EngineState& es, Counters& ctr, const vpp::Container& c) {
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::string name = e.name.empty() ? std::string() : e.name;
        bool isCzn = endsWithCznExt(toLower(name));

        if (e.payload.kind == vpp::PayloadKind::Raw) {
            bool nested = false;
            try {
                vpp::Container child = c.openNested(i);
                nested = true;
                walkCzn(es, ctr, child);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested && isCzn) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                ingestCznBuffer(es, ctr, raw.data(), raw.size());
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            bool ok = result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch ||
                      result.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                      result.status == vpp::DecodeStatus::ContentValidated ||
                      result.status == vpp::DecodeStatus::RecoveredSharedStream ||
                      result.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
            if (!ok) continue;
            bool nested = false;
            if (result.data.size() >= 4) {
                uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                  ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                if (magic == 0x51890ACEu) {
                    try {
                        vpp::Container child(vpp::ByteView(result.data.data(), result.data.size()));
                        nested = true;
                        walkCzn(es, ctr, child);
                    } catch (const std::exception&) {
                        nested = false;
                    }
                }
            }
            if (!nested && isCzn) {
                ingestCznBuffer(es, ctr, result.data.data(), result.data.size());
            }
        }
    }
}

} // namespace zoneobj

std::string installZoneObjectsFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es) {
    static const char* const kArchives[] = {
        "sr3_city_0.vpp_pc",        "sr3_city_1.vpp_pc", "sr3_city_missions.vpp_pc",
        "dlc1.vpp_pc",              "dlc2.vpp_pc",       "dlc3.vpp_pc",
        "misc.vpp_pc",              "startup.vpp_pc",
        "patch_compressed.vpp_pc",  "patch_uncompressed.vpp_pc",
    };
    zoneobj::Counters ctr;
    std::string archivesMissing;
    size_t archivesFound = 0;
    for (const char* archiveName : kArchives) {
        fs::path archivePath;
        for (auto& ent : fs::directory_iterator(cacheDir)) {
            if (ent.is_regular_file() && toLower(ent.path().filename().string()) == toLower(std::string(archiveName))) {
                archivePath = ent.path();
                break;
            }
        }
        if (archivePath.empty()) {
            if (!archivesMissing.empty()) archivesMissing += ", ";
            archivesMissing += archiveName;
            continue;
        }
        ++archivesFound;
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            zoneobj::walkCzn(es, ctr, c);
        } catch (const std::exception& ex) {
            return "not installed (" + std::string(archiveName) + ": " + ex.what() + ")";
        }
    }
    std::string summary = "installed archives=" + std::to_string(archivesFound) + "/" +
                           std::to_string(sizeof(kArchives) / sizeof(kArchives[0])) +
                           " czn_files_found=" + std::to_string(ctr.cznFilesFound) +
                           " objects_sections=" + std::to_string(ctr.cznWithSection) +
                           " sections_incomplete=" + std::to_string(ctr.cznSectionsIncomplete) +
                           " named_objects_resolved=" + std::to_string(es.zoneObjectsLoadedCount());
    if (!archivesMissing.empty()) summary += " missing_archives=" + archivesMissing;
    return summary;
}

struct ScriptInstance {
    std::string archive;
    std::string pathChain;
    std::string entryName;
    std::string content;
};

struct WalkStats {
    uint64_t entries = 0;
    uint64_t containersOpened = 0;
    uint64_t decompressFailed = 0;
};

// A real .vint_doc leaf found by the same walk (2026-10-03): loaded into the
// UI engine state's Vint element tree on demand (see the document-context
// resolver in main()).
struct DocInstance {
    std::string archive;
    std::string pathChain;
    std::string entryName;
    std::vector<uint8_t> bytes;
};

struct WalkState {
    std::deque<std::vector<uint8_t>> bufferPool;
    std::vector<ScriptInstance> found;
    std::vector<DocInstance> docs;
    WalkStats stats;
};

void inspectLeaf(WalkState& st, const std::string& archive, const std::string& chain, const std::string& name,
                  const uint8_t* data, size_t len) {
    std::string lower = toLower(name);
    if (lower.size() > 9 && lower.compare(lower.size() - 9, 9, ".vint_doc") == 0) {
        st.docs.push_back({archive, chain, name, std::vector<uint8_t>(data, data + len)});
        return;
    }
    if (endsWithLuaExt(lower)) {
        ScriptInstance si;
        si.archive = archive;
        si.pathChain = chain;
        si.entryName = name;
        si.content.assign(reinterpret_cast<const char*>(data), len);
        st.found.push_back(std::move(si));
    }
}

// Same recursive walk as tools/lua_mission_ui_census.cpp's own walk() - see
// this file's top comment for why it is duplicated rather than shared.
void walk(WalkState& st, const vpp::Container& c, const std::string& archive, const std::string& chain) {
    st.stats.containersOpened++;
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        st.stats.entries++;
        std::string name = e.name.empty() ? ("<unnamed hash=" + std::to_string(e.nameHash) + ">") : e.name;
        std::string childChain = chain.empty() ? name : (chain + " > " + name);

        if (e.payload.kind == vpp::PayloadKind::Raw) {
            bool nested = false;
            try {
                vpp::Container child = c.openNested(i);
                nested = true;
                walk(st, child, archive, childChain);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                inspectLeaf(st, archive, childChain, name, raw.data(), raw.size());
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            if (result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch) {
                bool nested = false;
                if (result.data.size() >= 4) {
                    uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                      ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                    if (magic == 0x51890ACEu) {
                        st.bufferPool.push_back(std::move(result.data));
                        try {
                            vpp::Container child(vpp::ByteView(st.bufferPool.back().data(), st.bufferPool.back().size()));
                            nested = true;
                            walk(st, child, archive, childChain);
                        } catch (const std::exception&) {
                            nested = false;
                        }
                        if (!nested) {
                            const auto& buf = st.bufferPool.back();
                            inspectLeaf(st, archive, childChain, name, buf.data(), buf.size());
                        }
                        continue;
                    }
                }
                inspectLeaf(st, archive, childChain, name, result.data.data(), result.data.size());
            } else {
                st.stats.decompressFailed++;
            }
        }
    }
}

// --- Real per-script state-tag lookup (this task, spec-lua-bindings.md
// Sec14.5, read in full before writing this): the two Lua script resource
// types are cleanly, exhaustively separated by CONTAINER KIND in the real
// .asm_pc manifests - type 27 ("Vint LUA script") entries under container
// kind 21 ("UI") or 22 ("UI peg") belong to the UI state; type 32
// ("Mission LUA script") entries under container kind 39 ("mission model
// data") or 40 ("large mission model data") belong to the gameplay state
// (Sec14.5's own population citation: 727 real type-27 entries, ALL under
// kind 21; 79 real type-32 entries, 76 under kind 39 + 3 under kind 40).
// Built via sr3asm::AsmManifest (include/sr3asm/manifest.h) exactly the
// way tools/mission_package_census.cpp already establishes the real usage
// pattern for this project: walk every real .asm_pc across the whole
// cache, parse with AsmManifest::parse(), and resolve BOTH
// ContainerRecord::containerKind (real numeric field, matched against the
// spec-cited 21/22/39/40 directly - no name-string dependency needed for
// the KIND side, since the spec cites the numeric ids as the confirmed
// fact) and each ManifestEntry::typeId's own name via the manifest's own
// typeTable().findName() (matched against the literal strings "Vint LUA
// script"/"Mission LUA script" - the SAME name-based check
// mission_package_census.cpp already uses for the type side, not
// reinvented). The walk()/inspectLeaf() shape below is a second,
// deliberately-duplicated copy of mission_package_census.cpp's own
// walk() (leaf test: ".asm_pc" suffix instead of ".lua") - matching this
// codebase's own established convention (this file's own top comment)
// of duplicating a small recursive walk per tool rather than factoring
// it into a shared header.
namespace statetag {

bool endsWithAsmExt(const std::string& lowerName) {
    static const std::string ext = ".asm_pc";
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

enum class Tag { Ui, Gameplay, Open, Conflict };

const char* tagLabel(Tag t) {
    switch (t) {
        case Tag::Ui: return "ui";
        case Tag::Gameplay: return "gameplay";
        case Tag::Conflict: return "conflict"; // real data disagreement across manifests - treated as OPEN by callers
        default: return "open";
    }
}

struct AsmInstance {
    std::string archive;
    std::string pathChain; // full chain INCLUDING this .asm_pc's own entry name (mission_package_census's own convention)
    std::vector<uint8_t> content;
};

// Second, dedicated recursive walk (leaf test: ".asm_pc") - deliberately
// separate from the WalkState/walk() pair above (leaf test: ".lua"),
// matching mission_package_census.cpp's own walk() almost verbatim
// (duplicated with attribution, not shared - see this block's own top
// comment).
void walkAsm(std::vector<AsmInstance>& found, std::deque<std::vector<uint8_t>>& bufferPool,
             const vpp::Container& c, const std::string& archive, const std::string& chain) {
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::string name = e.name.empty() ? ("<unnamed hash=" + std::to_string(e.nameHash) + ">") : e.name;
        std::string childChain = chain.empty() ? name : (chain + " > " + name);

        if (e.payload.kind == vpp::PayloadKind::Raw) {
            bool nested = false;
            try {
                vpp::Container child = c.openNested(i);
                nested = true;
                walkAsm(found, bufferPool, child, archive, childChain);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested && endsWithAsmExt(toLower(name))) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                found.push_back({archive, childChain, std::vector<uint8_t>(raw.data(), raw.data() + raw.size())});
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            if (result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch) {
                bool nested = false;
                if (result.data.size() >= 4) {
                    uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                      ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                    if (magic == 0x51890ACEu) {
                        bufferPool.push_back(std::move(result.data));
                        try {
                            vpp::Container child(vpp::ByteView(bufferPool.back().data(), bufferPool.back().size()));
                            nested = true;
                            walkAsm(found, bufferPool, child, archive, childChain);
                        } catch (const std::exception&) {
                            nested = false;
                        }
                        if (!nested) {
                            const auto& buf = bufferPool.back();
                            if (endsWithAsmExt(toLower(name)))
                                found.push_back({archive, childChain, std::vector<uint8_t>(buf.begin(), buf.end())});
                        }
                        continue;
                    }
                }
                if (endsWithAsmExt(toLower(name)))
                    found.push_back({archive, childChain, result.data});
            }
        }
    }
}

struct Index {
    std::unordered_map<std::string, Tag> byKey; // key: lower(containerName) + "|" + lower(entryName)
    uint64_t manifestsParsed = 0;
    uint64_t manifestParseFailures = 0;
    uint64_t asmInstancesFound = 0;
    uint64_t uiCandidateEntries = 0;      // real kind-21/22 + type-27 entries seen (population, not distinct)
    uint64_t gameplayCandidateEntries = 0; // real kind-39/40 + type-32 entries seen (population, not distinct)
    uint64_t conflicts = 0; // distinct (container,entry) keys where two manifests disagreed on tag - real, reported, not hidden
};

// A ContainerRecord's real backing .str2_pc stem (spec-asm-format.md
// Sec9.4, CONFIRMED - 8,057/8,057 records checked): "A record's backing
// container is `<name>.str2_pc` (7,442 records) or, for records named by
// a whole file name, `<name minus its extension>.str2_pc` (615 records)."
// Real, observed, checked directly against this task's own data: every
// kind-21/22 (UI/UI peg) real container record is named the SECOND way -
// as a whole ".vint_doc" file name (e.g. "cat_mouse_results.vint_doc",
// type 26's own registered extension, spec-asm-format.md Sec9.3's
// type-extension table) - its real backing container is
// "cat_mouse_results.str2_pc", not "cat_mouse_results.vint_doc.str2_pc".
// Kind-39/40 (mission) container records were already observed named the
// FIRST way (a bare stem, e.g. "dlc1_mm_06_modal", no extension) - this
// function returns that name unchanged, so this fix is additive, not a
// behavior change for the gameplay side. Detection: does `name` contain a
// '.' at all - a bare container stem never does in the real population
// this project has observed; a "whole file name" always does (its own
// real extension). Strips at most one trailing "." + suffix (the LAST
// dot), never guessed beyond that.
std::string backingStem(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return name;
    return name.substr(0, dot);
}

Index build(const std::vector<fs::path>& archives) {
    Index idx;
    std::vector<AsmInstance> found;
    std::deque<std::vector<uint8_t>> bufferPool;
    for (auto& archivePath : archives) {
        std::string archiveName = archivePath.filename().string();
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(archivePath);
        } catch (const std::exception&) {
            continue;
        }
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walkAsm(found, bufferPool, root, archiveName, "");
        } catch (const std::exception&) {
        }
    }
    idx.asmInstancesFound = found.size();

    for (const auto& inst : found) {
        sr3asm::AsmManifest manifest;
        try {
            manifest = sr3asm::AsmManifest::parse(vpp::ByteView(inst.content.data(), inst.content.size()));
        } catch (const std::exception&) {
            ++idx.manifestParseFailures;
            continue;
        }
        ++idx.manifestsParsed;

        for (const auto& record : manifest.records()) {
            // Container-kind side: real numeric field, matched directly
            // against the spec-cited 21/22/39/40 (spec-lua-bindings.md
            // Sec14.5) - no name-table dependency needed here since the
            // spec itself cites the numeric kind ids as the confirmed
            // fact (it also names them "UI"/"UI peg"/"mission model
            // data"/"large mission model data", cross-checked once below
            // per manifest via kindTable() as an extra sanity check, not
            // load-bearing).
            bool kindUi = (record.containerKind == 21 || record.containerKind == 22);
            bool kindMission = (record.containerKind == 39 || record.containerKind == 40);
            if (!kindUi && !kindMission) continue;

            for (const auto& entry : record.entries) {
                const std::string* typeName = manifest.typeTable().findName(entry.typeId);
                if (!typeName) continue;
                Tag tag;
                if (kindUi && *typeName == "Vint LUA script") {
                    tag = Tag::Ui;
                    ++idx.uiCandidateEntries;
                } else if (kindMission && *typeName == "Mission LUA script") {
                    tag = Tag::Gameplay;
                    ++idx.gameplayCandidateEntries;
                } else {
                    continue; // some other real resource type sharing this container - not a Lua script entry
                }
                std::string key = toLower(backingStem(record.name)) + "|" + toLower(entry.name);
                auto it = idx.byKey.find(key);
                if (it == idx.byKey.end()) {
                    idx.byKey.emplace(key, tag);
                } else if (it->second != tag && it->second != Tag::Conflict) {
                    // Real data disagreement across manifests for the SAME
                    // (container,entry) key - honestly recorded rather than
                    // silently overwritten either direction ("no invented
                    // fixes" policy). Not expected (Sec14.5's own "cleanly,
                    // exhaustively separated" population claim), so this
                    // counter is a real, checked-for-zero sanity gate, not
                    // an assumed negative.
                    it->second = Tag::Conflict;
                    ++idx.conflicts;
                }
                // else: identical repeat listing of the same container in a
                // second manifest (real, observed - e.g. dlc1_mm_06_modal
                // appears in both dlc1_sr3_city.asm_pc and
                // dlc1_stream_grid.asm_pc identically) - not a conflict.
            }
        }
    }
    return idx;
}

// Resolves one script's own real state tag: strips the trailing " > " +
// entryName that WalkState's own pathChain always carries (this file's
// own inspectLeaf()/walk() convention - pathChain is `childChain`, which
// includes the leaf's own name), takes the LAST remaining segment as the
// immediate containing entry's own name (its real .str2_pc, normally),
// strips a trailing ".str2_pc" (case-insensitive, spec-asm-format.md's
// own "<name>.str2_pc is normally its backing file" convention,
// include/sr3asm/manifest.h's own ContainerRecord::name doc comment), and
// looks that (containerName, entryName) pair up in `idx`. A script with
// no " > " at all in its own pathChain (sits directly at its archive's
// own root, never nested inside any .str2_pc - the real, observed case
// for the 6 UI/gameplay preload library files plus game_lib.lua/
// sr3_city.lua/m19.lua/m22.lua, 10/804 total) has no container to look
// up at all and is tagged OPEN directly, not guessed at.
Tag resolve(const Index& idx, const std::string& pathChain, const std::string& entryName,
            std::string* containerNameOut = nullptr) {
    size_t sep = pathChain.rfind(" > ");
    if (sep == std::string::npos) return Tag::Open; // no nested container at all - real, not a lookup miss
    std::string prefix = pathChain.substr(0, sep);
    size_t sep2 = prefix.rfind(" > ");
    std::string immediateParent = (sep2 == std::string::npos) ? prefix : prefix.substr(sep2 + 3);
    std::string containerName = immediateParent;
    std::string lowerParent = toLower(immediateParent);
    static const std::string kStr2Ext = ".str2_pc";
    if (lowerParent.size() >= kStr2Ext.size() &&
        lowerParent.compare(lowerParent.size() - kStr2Ext.size(), kStr2Ext.size(), kStr2Ext) == 0) {
        containerName = immediateParent.substr(0, immediateParent.size() - kStr2Ext.size());
    }
    if (containerNameOut) *containerNameOut = containerName;
    std::string key = toLower(containerName) + "|" + toLower(entryName);
    auto it = idx.byKey.find(key);
    if (it == idx.byKey.end()) return Tag::Open; // real miss - not in either confirmed shape, reported as OPEN, not forced
    return it->second;
}

} // namespace statetag

// tools/lua_reconciliation_called_and_registered_1181.tsv's own columns:
// name, total_call_sites, distinct_scripts, prefix_group, cluster - the
// real, static, call-count-ranked priority list this tool cross-references
// its own runtime hit counts against (sanity check, not an expected exact
// match - see this tool's own summary output for why).
struct StaticRank {
    uint64_t totalCallSites = 0;
    uint64_t distinctScripts = 0;
};

std::unordered_map<std::string, StaticRank> loadStaticReconciliation(const std::string& path) {
    std::unordered_map<std::string, StaticRank> out;
    std::ifstream f(path);
    if (!f) return out; // optional - tool still runs without it, just skips the cross-reference
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; } // header row
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string name, callsStr, scriptsStr, prefix, cluster;
        if (!std::getline(ss, name, '\t')) continue;
        if (!std::getline(ss, callsStr, '\t')) continue;
        if (!std::getline(ss, scriptsStr, '\t')) continue;
        StaticRank r;
        try {
            r.totalCallSites = std::stoull(callsStr);
            r.distinctScripts = std::stoull(scriptsStr);
        } catch (...) {
            continue;
        }
        out[name] = r;
    }
    return out;
}

// Real, top real Lua error messages by frequency (orchestrator's follow-up
// item, 2026-09-29): groups the real callError strings verdict_hook_fires_
// detail.tsv already captures by a NORMALIZED message text, so the SAME
// underlying error (e.g. "attempt to call a nil value (global 'foo')")
// recurring across many different scripts/hooks/lines counts as one
// bucket rather than one row per call site. Real, observed Lua 5.1
// behavior, not invented: a message raised from code executing INSIDE a
// loaded chunk is automatically prefixed by the VM with a source id and
// line number - luaL_loadbuffer's own chunk-name handling (this project
// passes a plain filename, never '='/'@'-prefixed) means Lua's real
// luaO_chunkid formats that id as `[string "name"]:line: ` - real,
// per-call-site noise for grouping purposes (the OWN chunk name embedded
// there tells you which SCRIPT currently owns the hook body that errored,
// a genuinely useful but DIFFERENT signal, not thrown away - it is still
// fully present, unstripped, in verdict_hook_fires_detail.tsv). A message
// raised directly by lua_pcall itself with no executing Lua frame to
// attribute it to (e.g. calling a nil global with no wrapping Lua call,
// the common Group-2-unconditional-and-undefined case) carries no such
// prefix at all and is left completely unchanged. Strips at most ONE
// leading `[string "..."]:<digits>: ` run; anything not matching that
// exact real shape is returned unchanged, never guessed at.
std::string stripLuaChunkLocationPrefix(const std::string& msg) {
    if (msg.size() < 2 || msg[0] != '[') return msg;
    size_t closeBracket = msg.find("]:");
    if (closeBracket == std::string::npos) return msg;
    size_t pos = closeBracket + 2;
    size_t digitsStart = pos;
    while (pos < msg.size() && msg[pos] >= '0' && msg[pos] <= '9') ++pos;
    if (pos == digitsStart || pos >= msg.size() || msg[pos] != ':') return msg;
    ++pos; // skip ':'
    if (pos < msg.size() && msg[pos] == ' ') ++pos;
    return msg.substr(pos);
}

std::string sanitizeTsv(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c == '\t') c = ' ';
        if (c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

// --- Step 1 (this task, after step 0 lands): real mission-driving pass
// helpers. -------------------------------------------------------------

// One real row of tools/mission_package_per_container.tsv (§9.125's own
// census output - columns: manifest_archive, manifest_path_chain,
// manifest_name, container_name, entry_count, mission_lua_entry_name,
// mission_lua_stem, stem_covers_a_start_script, other_entry_type_names).
// Only the 4 fields this pass actually needs are kept.
struct MissionPackageRow {
    std::string containerName;
    std::string missionLuaEntryName;
    std::string missionLuaStem;
    bool coversStartScript = false;
};

// Manual tab-split (matching this file's own no-<regex> convention, same
// as loadStaticReconciliation() above) - tolerant of a missing trailing
// empty 9th field (other_entry_type_names, unused here) since
// std::getline does not emit a trailing empty token the way some TSV
// readers do.
std::vector<MissionPackageRow> loadMissionPackageRows(const std::string& path) {
    std::vector<MissionPackageRow> out;
    std::ifstream f(path);
    if (!f) return out; // real, checked negative if missing - reported by the caller, not silently assumed
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; } // header row
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> cols;
        std::istringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t')) cols.push_back(cell);
        if (cols.size() < 8) continue; // real, malformed-row skip (not expected against this file's own real output)
        MissionPackageRow r;
        r.containerName = cols[3];
        r.missionLuaEntryName = cols[5];
        r.missionLuaStem = cols[6];
        r.coversStartScript = (cols[7] == "1");
        out.push_back(std::move(r));
    }
    return out;
}

// Extracts NAME from a real Lua 5.1 "attempt to call global 'NAME' (a nil
// value)" error string (the same real error shape this file's own
// errorMessageCounts aggregate and §9.131's own dlc2_m02 analysis already
// rely on) - empty if the message doesn't match that exact real shape.
// Used to identify each mission's own first real unimplemented/undefined
// stub hit, in real encounter order - never a guess at which name "should"
// be missing.
std::string extractNilGlobalName(const std::string& msg) {
    static const std::string needle = "attempt to call global '";
    size_t pos = msg.find(needle);
    if (pos == std::string::npos) return std::string();
    size_t start = pos + needle.size();
    size_t end = msg.find('\'', start);
    if (end == std::string::npos) return std::string();
    return msg.substr(start, end - start);
}

// Real, unmodified Lua 5.1 debug-API count hook (lua_sethook +
// LUA_MASKCOUNT) - a local twin of Host::fireConfirmedHooks' own
// watchdogHook (src/lua_host.cpp, anonymous namespace there, not exposed
// to this file) for this NEW pass's own two NEW call sites (a mission
// script's own top-level runChunk() and its own
// `<stem>_start(checkpoint, is_restart)` call) - neither was ever
// watchdog-wrapped before (runChunk()'s own doc comment in host.h
// deliberately never wraps it, so as not to perturb HANDOFF.md Sec9.113's
// already-independently-verified baseline numbers; that concern does not
// apply here since this is an entirely separate, new measurement pass).
// "So one runaway mission can't hang the whole pass" - this task's own
// instruction. Reuses the SAME CHOSEN instruction budget as host.h's own
// kHookWatchdogInstructionBudget (5,000,000) for consistency - not an
// independently-chosen new number.
constexpr int kMissionWatchdogInstructionBudget = 5'000'000;

// --probe-autopilot (2026-10-07): CHOSEN, not a spec fact - how many
// EXTRA ticks (same per-tick shape as the main mission-drive loop: one
// fade/cutscene/clock frame + kSchedulerPassesPerTick scheduler passes
// each) to run after synthesizing a callback dispatch, looking for the
// first new error/wait-location-change/completion past it. Deliberately
// much smaller than the main --tick-budget=N (default 20, often raised to
// the hundreds to rule out a long legitimate wait) - a REAL blocker that
// actually was unblocked by the synthesized event should show up almost
// immediately on the very next resume; this is "does the mission move at
// all past this point", not "is this still mid-way through a long wait"
// (that question was already answered by the real --tick-budget loop
// BEFORE this mission was classified still_suspended in the first place).
constexpr int kProbeAutopilotFollowUpTicks = 30;

// --trace-host-calls-before-error=N (DIAGNOSTIC ONLY, CHOSEN tooling, off
// by default, N=0): when N > 0, every single resume this tool makes (the
// mission's own initial _start call AND every later scheduler-pass resume
// - both re-arm this SAME hook fresh per resume, see runChunkWithWatchdog/
// callMissionStart/the schedulerPass `before` callback below) keeps a ring
// of the last N global-call names (C stub or Lua, via LUA_HOOKCALL) made
// during THAT ONE resume. Reset at the start of each resume and, if that
// resume's own call ends in a Lua error, printed right there before being
// discarded - so it always reflects "what led to this specific error,"
// never a stale trace from an unrelated resume. Not a spec fact, not a
// behavior change - purely an investigation aid for "what host call fed a
// script-level nil/error," the same spirit as --probe-unresolved-names.
// Real engine behavior is never altered by this flag; only stderr output
// changes.
//
// UPDATED 2026-10-03 (peer follow-up, per-coroutine keying): the first
// version used a single global ring, which is wrong for any question that
// isn't "what led to THIS error" - this mission-drive loop advances the
// WHOLE scheduler every tick (host.runScriptThreadSchedulerPass resumes
// every live thread, not just one mission's own), so by the time a mission
// is classified still_suspended at budget exhaustion, a single global ring
// holds whatever OTHER thread's resume happened to run last that tick, not
// necessarily this mission's own (results/trace_host_calls_20261003.md's
// own "NOT answered" section has the full original finding). Keying by the
// resuming lua_State* (lua_sethook is itself already per-thread in Lua
// 5.1, so the L the hook fires with IS the specific coroutine) fixes this:
// each coroutine gets its own ring, so reading back by its own co is
// always that thread's own last resume, regardless of what else resumed
// around it. Stale entries for a finished/released thread are simply left
// in the map (bounded by the mission count this tool ever touches in one
// run - never reused across tool invocations, so no real leak concern for
// a short-lived diagnostic).
size_t g_traceRingSize = 0;
std::unordered_map<lua_State*, std::vector<std::string>> g_traceRings;

// --probe-autopilot (2026-10-07): declared here (ahead of its own full doc
// comment, with g_firstResumeRegistrations/isCallbackRegistrationNative
// below) ONLY because resetTraceRing() needs it a few lines down - see the
// full doc comment at g_firstResumeRegistrations' own declaration, further
// below, for why this capture exists at all and what it holds.
struct CapturedRegistration {
    std::string nativeName;    // e.g. "on_trigger" - always one of isCallbackRegistrationNative's set
    std::string callbackName;  // arg 1 on every member of this family: the opaque callback-name string
    std::string targetName;    // arg 2 (trigger/character/vehicle name); empty for on_qte_animation_trigger,
                                // which per spec-lua-api-behaviour.md Sec10.7 is single-argument and global
};
// Per-coroutine, reset at the top of every resume (resetTraceRing, right
// below) - holds only THIS resume's own registration calls.
std::unordered_map<lua_State*, std::vector<CapturedRegistration>> g_registrationCallsThisResume;

void resetTraceRing(lua_State* co) {
    g_traceRings[co].clear();
    // --probe-autopilot (2026-10-07): same per-resume reset as the ring
    // above, purely for memory hygiene across a long-running mission's
    // many later resumes - by the time any LATER reset runs, a mission's
    // own FIRST resume's captures have already been copied out into
    // g_firstResumeRegistrations (callMissionStart, below) and are never
    // read from this map again.
    g_registrationCallsThisResume[co].clear();
}

// First-resume ring (2026-10-07, peer follow-up to this file's own
// "architectural wait" investigation): g_traceRings above is overwritten by
// resetTraceRing() at the top of EVERY resume of a coroutine, so it only
// ever holds a mission's MOST RECENT resume - by the time a mission is
// classified still_suspended at budget exhaustion (after many later
// scheduler-pass resumes of its own thread_yield poll), whatever that
// mission called on its very FIRST resume - its own _start call, where a
// mission typically does its one-time setup (registering a callback such
// as trigger_enable/on_trigger/on_vehicle_enter) before ever reaching its
// wait loop - has long since been discarded. callMissionStart (below) is
// only ever invoked once per mission's coroutine (this harness only ever
// drives a fresh start, never a resume/retry - see callMissionStart's own
// top doc comment), so the ring g_traceRings[co] holds at the instant that
// one call returns IS, completely, that coroutine's first resume. Captured
// there into this second map, once, and never reset or overwritten again -
// purely additive to the existing per-coroutine ring, same diagnostic-only
// contract (opt-in via the same --trace-host-calls-before-error=N flag;
// N=0 keeps this empty and unused, same as g_traceRings).
std::unordered_map<lua_State*, std::vector<std::string>> g_firstResumeTraceRings;

// --probe-autopilot (2026-10-07, this task): a THIRD, further-additive
// extension of the same ring machinery above, opt-in via its own flag
// (g_probeAutopilotEnabled), gated the same way (never touches the Lua
// stack unless explicitly enabled, never changes real engine behavior).
//
// The still_suspended classification site (below, in main()) already
// reads g_firstResumeTraceRings[co] to know WHICH native names a mission
// called on its own first resume - but that ring, like g_traceRings
// itself, only ever records a call's NAME (missionWatchdogHook's own
// LUA_HOOKCALL branch: `info.name`), never its arguments. "Did this
// mission register a callback" needs the name; "what callback, against
// what trigger/character/vehicle" - the exact thing --probe-autopilot's
// own task brief requires ("use only the callback and the exact
// arguments the script itself registered") - needs the arguments too.
// This is new: nothing in this project previously retained a registration
// call's own argument VALUES anywhere retrievable after the call returns
// (genericStubTrampoline, src/lua_stub_registry.cpp, logs only argument
// COUNT and TYPE strings, same for src/lua_spec_confirmed_stubs.cpp's
// logCall(); the few dedicated stubs that DO store a callback name
// (stub_on_take_damage/stub_on_revived) key it per-character in
// EngineState, not per-coroutine/per-call, so reading it back for "what
// did THIS mission's first resume register" would require guessing which
// character it was for).
//
// Measured directly off the Lua stack at the exact LUA_HOOKCALL instant:
// Lua 5.1's call hook fires with the CALLEE already the active function
// (lua_getstack(L, 0, ...) below already relies on this to name it), so
// for the callee - C function or Lua function alike - stack positions
// 1..argc are already exactly its own arguments at that instant; this is
// the same stack state genericStubTrampoline's own body reads when IT
// runs as that callee (`lua_gettop(L)`/`lua_type(L, i)` for i=1..argc,
// src/lua_stub_registry.cpp:70-92) - not a new or different technique,
// just applied from a hook instead of from inside the callee itself.
//
// Only the ONE family of natives this project has independently verified
// (spec-lua-api-behaviour.md, CONFIRMED-structure sections, cross-checked
// against this task's own named examples - see isCallbackRegistrationNative's
// own doc comment for the full citation list and, importantly, which of
// the task's own named examples were checked and EXCLUDED after
// verification) is captured; every other call is untouched, exactly as
// before. (CapturedRegistration/g_registrationCallsThisResume themselves
// are declared earlier, next to g_traceRings/resetTraceRing, purely
// because resetTraceRing needs the map - this comment block covers both.)
//
// Captured once, at the same instant/call site g_firstResumeTraceRings is
// (callMissionStart, below) - never overwritten afterward. Empty unless
// --probe-autopilot is given.
std::unordered_map<lua_State*, std::vector<CapturedRegistration>> g_firstResumeRegistrations;
bool g_probeAutopilotEnabled = false; // set only by --probe-autopilot (main(), below)

// The callback-registration native family --probe-autopilot acts on -
// see g_registrationCallsThisResume's own doc comment above for why this
// capture exists at all. Every entry below is CONFIRMED by direct reading
// of spec-lua-api-behaviour.md (not inferred from this project's own
// earlier summaries) to take a callback-name string as one argument,
// registering it for a LATER dispatch this project's own engine state
// models via the real coroutine-spawn mechanism (UPDATE 2026-10-09,
// spec-lua-api-behaviour.md Sec1.14's own "GAP CLOSED" finding, commit
// 383f653: the real dispatcher 0x00a29e40 hands off to reader 0x00a1fe10,
// which spawns a NEW Lua coroutine for the resolved name rather than
// calling Lua synchronously - the body only runs later, when the scheduler
// 0x00e0cba0 resumes it; the consuming/firing side used to be OPEN when
// this comment was first written, hence the earlier "firing the named Lua
// global with zero arguments" synchronous stand-in below this function's
// own callers used - that stand-in is superseded, see the dispatch block's
// own 2026-10-09 doc comment for the replacement):
//   on_trigger (Sec1.2), on_trigger_exit (Sec1.7), on_death (Sec3.2),
//   on_vehicle_destroyed (Sec3.3), on_attack_performed (Sec3.12),
//   on_take_damage (Sec3.13), on_revived (Sec10.8), on_vehicle_enter
//   (Sec4.12, added 2026-10-08 - see note below), on_vehicle_enter_water
//   (Sec14.30, added 2026-10-08 - see note below): arg 1 callback name,
//   arg 2 trigger/character/vehicle name (`hasTargetArg=true`).
//   on_qte_animation_trigger (Sec10.7): arg 1 callback name ONLY - a
//   single fixed GLOBAL slot, not per-object (`hasTargetArg=false`).
//
// UPDATE 2026-10-08: on_vehicle_enter (Sec4.12) was RE-RESOLVED by commit
// e07a46a (full re-disassembly of 0x005e4660/0x00a56d50, batch
// lua_spec_confirmed_stubs_b31_continuous_spawn_vehicle_enter_tutorial.cpp) -
// the self-contradiction this note used to flag is gone: the earlier
// "IMMEDIATE numeric-event post" reading was simply wrong, refuted and
// superseded, not left genuinely ambiguous. Arg 1 IS a callback/hook name,
// full stop, and the native registers it into one of 3 hook slots
// depending on how arg 2 resolves - the exact same two-string registration
// shape as every other entry in this family. Moved into kTwoArgFamily
// below. on_vehicle_enter_water's OWN exclusion above was only ever "by
// analogy to on_vehicle_enter's uncertainty" - since that uncertainty is
// now resolved (and on_vehicle_enter_water's own Sec14.30 text was already
// independently CONFIRMED on its own terms, and already implemented,
// b11), it is moved into kTwoArgFamily too, for the same reason.
//
// EXPLICITLY CHECKED AND EXCLUDED (this task's own example list named
// three of these four; verified directly against spec-lua-api-behaviour.md
// rather than assumed from the list):
//   trigger_enable (Sec1.1) - has NO callback argument at all (string
//     trigger name, bool enable, numeric flag) - a toggle on an ALREADY-
//     registered trigger, not a registration call itself.
//   on_cellphone - does not exist anywhere in this project's spec docs,
//     source, or the 1490-name tagged registration list; the only
//     cellphone-related natives are audio_play_for_mission_cellphone/
//     audio_remove_mission_cellphone (immediate audio actions, not
//     registration) and cellphone_animate_start_do/_stop_do.
// Also surveyed and excluded as a DIFFERENT, more complex mechanism
// shape (a stored responder/property name or an actual Lua closure, not
// a simple "callback name string" the way every entry above is):
// game_sign_into_network, vint_internal_dataresponder_request,
// game_peg_load_with_cb, vint_set_property's callback-typed case.
bool isCallbackRegistrationNative(const std::string& name, bool* hasTargetArg) {
    static const std::unordered_set<std::string> kTwoArgFamily = {
        "on_trigger", "on_trigger_exit", "on_death", "on_vehicle_destroyed",
        "on_attack_performed", "on_take_damage", "on_revived",
        "on_vehicle_enter", "on_vehicle_enter_water"};
    if (kTwoArgFamily.count(name)) {
        if (hasTargetArg) *hasTargetArg = true;
        return true;
    }
    if (name == "on_qte_animation_trigger") {
        if (hasTargetArg) *hasTargetArg = false;
        return true;
    }
    return false;
}

void printTraceRingIfAny(lua_State* co, const std::string& label, const std::string& errorMessage) {
    if (g_traceRingSize == 0) return;
    const auto& ring = g_traceRings[co];
    std::cerr << "[trace-host-calls] " << label << " error: " << errorMessage << "\n[trace-host-calls] last "
              << ring.size() << " global calls before it:";
    for (const auto& n : ring) std::cerr << " " << n;
    std::cerr << "\n";
}
// Prints a coroutine's own ring with no error - the still_suspended case
// (2026-10-03): a mission that never errors (a thread_yield poll loop) has
// nothing for printTraceRingIfAny's own error-gated print to fire on, so
// "what does this mission's own wait poll" needed this separate entry
// point, reading the SAME per-coroutine ring at budget-exhaustion time
// instead of at an error.
void printTraceRingStillSuspended(lua_State* co, const std::string& label) {
    if (g_traceRingSize == 0) return;
    const auto& ring = g_traceRings[co];
    std::cerr << "[trace-host-calls] " << label << " still suspended (no error) - last " << ring.size()
              << " global calls of its own last resume:";
    for (const auto& n : ring) std::cerr << " " << n;
    std::cerr << "\n";
}
// Prints a coroutine's FIRST-ever resume ring (2026-10-07, see
// g_firstResumeTraceRings' own doc comment above) - answers "what did this
// mission call in its own setup, before it ever reached its wait loop",
// which printTraceRingStillSuspended's most-recent-resume-only ring cannot
// once a mission has yielded more than once (every later poll resume
// overwrites g_traceRings[co] with just that poll's own calls, typically a
// lone thread_yield). Paired with printTraceRingStillSuspended at the same
// still_suspended classification call site, never on its own.
void printTraceRingFirstResume(lua_State* co, const std::string& label) {
    if (g_traceRingSize == 0) return;
    auto it = g_firstResumeTraceRings.find(co);
    if (it == g_firstResumeTraceRings.end()) return;
    const auto& ring = it->second;
    std::cerr << "[trace-host-calls] " << label << " first resume (own _start call) - last " << ring.size()
              << " global calls before it first yielded/finished/errored:";
    for (const auto& n : ring) std::cerr << " " << n;
    std::cerr << "\n";
}
void missionWatchdogHook(lua_State* L, lua_Debug* ar) {
    if (ar->event == LUA_HOOKCALL) {
        lua_Debug info{};
        if (lua_getstack(L, 0, &info) && lua_getinfo(L, "n", &info)) {
            auto& ring = g_traceRings[L];
            ring.push_back(info.name ? info.name : "?");
            if (ring.size() > g_traceRingSize) ring.erase(ring.begin());
            // --probe-autopilot (2026-10-07): see g_registrationCallsThisResume's
            // own doc comment above - gated on g_probeAutopilotEnabled so this
            // never runs (never touches the Lua stack beyond what the ring
            // capture above already does) unless --probe-autopilot was given.
            if (g_probeAutopilotEnabled && info.name) {
                bool hasTargetArg = false;
                if (isCallbackRegistrationNative(info.name, &hasTargetArg)) {
                    CapturedRegistration cr;
                    cr.nativeName = info.name;
                    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) cr.callbackName = lua_tostring(L, 1);
                    if (hasTargetArg && lua_gettop(L) >= 2 && lua_isstring(L, 2))
                        cr.targetName = lua_tostring(L, 2);
                    g_registrationCallsThisResume[L].push_back(std::move(cr));
                }
            }
        }
        return;
    }
    luaL_error(L, "lua_host_run mission-drive watchdog: call exceeded the %d-instruction budget "
                  "(likely an infinite loop in never-before-executed mission code) - aborted by "
                  "this tool's own safety hook, not hung",
               kMissionWatchdogInstructionBudget);
}
int missionWatchdogHookMask() { return LUA_MASKCOUNT | (g_traceRingSize > 0 ? LUA_MASKCALL : 0); }

// Watchdog-wrapped host.runChunk() - same instruction-budget convention as
// missionWatchdogHook above, applied around the ONE real Lua API call
// (runChunk's own internal lua_pcall) this task's own step-1 pass makes
// that was not already individually watchdog-protected.
Host::RunResult runChunkWithWatchdog(Host& host, lua_State* L, const std::string& source,
                                      const std::string& chunkName) {
    lua_sethook(L, missionWatchdogHook, LUA_MASKCOUNT, kMissionWatchdogInstructionBudget);
    Host::RunResult r = host.runChunk(L, source, chunkName);
    lua_sethook(L, nullptr, 0, 0); // always clear - never leaves the watchdog armed for a later, unrelated call
    return r;
}

// Real per-call outcome of watchdog-wrapped `funcName(checkpoint,
// is_restart)` - the SAME real shape as Host::HookFireResult
// (existedAsFunction/attemptedCall/callOk/callError), duplicated here
// (not added to Host's own public API - a mission-specific, 2-argument,
// non-hook call, genuinely different from fireConfirmedHooks' own
// zero-arg-by-default contract) rather than bent to fit an unrelated
// existing method.
struct MissionStartResult {
    bool existedAsFunction = false;
    bool attemptedCall = false;
    bool callOk = false;    // no error: the record finished, or yielded and is still alive
    bool suspended = false; // callOk and the record yielded (runner step 6: "alive")
    // When suspended: the record's id, so the mission loop can follow it
    // through the scheduler passes (BareThreadTable::schedulerPass).
    uint16_t recordId = BareThreadTable::kNoThread;
    std::string callError;
    // When suspended: the Lua source location that actually called thread_yield
    // (coroutine stack level 1 - level 0 is bareGuard's own C frame, where the
    // real lua_yield() call lives, src/lua_bare_globals.cpp; level 1 is the
    // mission script's own call site). Real, measured via lua_getstack/
    // lua_getinfo on the still-suspended coroutine, not inferred. Empty if not
    // suspended, or if neither stack level could be read.
    std::string suspendedAt;
};

// checkpoint/isRestart's call-shape values are now CONFIRMED, not chosen
// (spec-lua-api-behaviour.md Sec36.4, spec-lua-bindings.md Sec14.11, synced
// 2026-10-02): the engine's own two native call sites for a mission's
// `<stem>_start` push exactly two Lua values onto the freshly created
// coroutine - a checkpoint STRING (the literal `"mission start"` on a
// genuine fresh start; the mission's own last-reported checkpoint name,
// e.g. `mission_checkpoints.xtbl`'s `CheckpointName` column, on a resume) -
// and a boolean `is_restart` (`false` on a fresh start, `true` on a
// resume/retry). **Never the number 0** - Sec36.4 is explicit that nothing
// in this mechanism ever produces anything but that string and that
// boolean on a fresh start, and no `mission_checkpoints.xtbl` row is ever
// named `"mission start"` or indexed `0`. This project's host previously
// passed the CHOSEN literal number `0.0` here, which is exactly the real
// `dlc2_m02` "attempt to index local 'cp_data' (a nil value)" bug shape:
// a mission script that only ever keys its checkpoint table by name
// strings (e.g. `checkpoints["mission start"]`/`checkpoints["1"]`) gets
// nil back when indexed by the number `0`.
//
// This harness only ever drives a FRESH start (one `callMissionStart` call
// per mission, below) - there is no resume/retry call site here yet, so
// Sec36.4's second half (`start_fn(<checkpoint_name_string>, true)`,
// reading the current-checkpoint-name global `0x014c8354`) is honestly
// NOT implemented: there is nothing in this harness that would ever call
// it with `isRestart=true`, so modelling that global here would be dead
// code, not a real fix. If a resume/retry drive path is added later, it
// must read the checkpoint-name string from that global (CONFIRMED;
// Sec36.3) rather than inventing one.
//
// The call goes through the script-thread runner, not a bare lua_pcall
// (spec-lua-api-behaviour.md Sec26.27 thread table, 2026-10-01 answer to
// Team B request 11, CONFIRMED: the only writers of the current-thread
// stack 0x02a44d10/0x02a44d14 are the runner 0x00e0cba0's push/pop and a
// start-up zero-out, so every Lua code the engine reaches from mission or
// trigger code runs inside a pushed record; "route every such invocation
// through a resume-shaped call (push a record, call, pop)"). So `_start`
// runs in its own coroutine (a yield inside it is legal) and is the
// current record for the call (thread_new inside it finds a parent).
// BareThreadTable::allocate()/run() is the host's model of that runner;
// `threads` must be the table registered into L (Host::bareGlobals()).
// This already matches Sec36.2/Sec36.4's "as a new coroutine through the
// thread runner, never a plain direct call" requirement - it was the
// checkpoint ARGUMENT that was wrong, not this call mechanism. Which of
// the runner's callers SR3 uses for mission hooks is OPEN and is not
// modelled; only the mechanism is.
MissionStartResult callMissionStart(lua_State* L, BareThreadTable& threads, const std::string& funcName,
                                     const std::string& checkpoint, bool isRestart) {
    MissionStartResult r;
    lua_getglobal(L, funcName.c_str());
    r.existedAsFunction = (lua_type(L, -1) == LUA_TFUNCTION);
    if (!r.existedAsFunction) {
        lua_pop(L, 1);
        return r;
    }
    lua_pop(L, 1);
    r.attemptedCall = true;
    // The runner's step 4 fetches the global by name below the arguments, so
    // only the two arguments go on L's stack (allocate() moves them).
    lua_pushstring(L, checkpoint.c_str());
    lua_pushboolean(L, isRestart ? 1 : 0);
    // hasParent=false: nothing is current before a mission's `_start` runs
    // (the drive loop calls it from the host, not from inside a record), the
    // same root-level situation BareThreadTable::startThread() models. The
    // spec gives no record a mission `_start` would be nested in.
    BareThreadTable::Record* rec = threads.allocate(L, funcName, /*nargs=*/2, /*hasParent=*/false);
    if (!rec) {
        lua_pop(L, 2); // allocate() leaves the arguments when the table is full
        r.callOk = false;
        r.callError = "lua_host_run: script-thread table full (" + std::to_string(BareThreadTable::kCapacity) +
                      " records, HIGH CONFIDENCE capacity) - " + funcName + " not started";
        lua_settop(L, 0);
        return r;
    }
    // Keep the coroutine reachable across run(): when the record finishes or
    // fails, run() releases it (the Record is freed and its registry ref
    // dropped), and its own status / error message is read from it below.
    lua_State* co = rec->co;
    lua_rawgeti(L, LUA_REGISTRYINDEX, rec->ref);
    const int coRef = luaL_ref(L, LUA_REGISTRYINDEX);
    // The watchdog goes on the coroutine: a count hook is per lua_State, and
    // the mission code now runs on `co`, not on L. Threads that thread_new
    // creates inside it inherit it (Lua 5.1 lua_newthread copies the hook).
    resetTraceRing(co); // see g_traceRings' own doc comment above - fresh per resume, keyed by this coroutine
    lua_sethook(co, missionWatchdogHook, missionWatchdogHookMask(), kMissionWatchdogInstructionBudget);
    std::string fetchError;
    const uint16_t recordId = rec->id;
    const bool alive = threads.run(L, rec, fetchError); // rec is dangling once this returns false
    lua_sethook(co, nullptr, 0, 0);
    // Save this coroutine's first-ever resume ring (2026-10-07) - see
    // g_firstResumeTraceRings' own doc comment above. threads.run() just
    // above is this function's one and only lua_resume of `co`, and
    // callMissionStart itself is only ever called once per mission's co
    // (this function's own top doc comment), so g_traceRings[co] right
    // here holds exactly, and completely, that coroutine's first resume -
    // written once into the separate map and never touched again,
    // regardless of what the mission-drive loop's later scheduler-pass
    // resumes go on to do to g_traceRings[co] itself.
    if (g_traceRingSize > 0) g_firstResumeTraceRings[co] = g_traceRings[co];
    // --probe-autopilot (2026-10-07): same first-resume snapshot, same
    // instant, for the registration-argument captures - see
    // g_firstResumeRegistrations' own doc comment above.
    if (g_probeAutopilotEnabled) g_firstResumeRegistrations[co] = g_registrationCallsThisResume[co];
    if (!fetchError.empty()) {
        r.callOk = false;
        r.callError = "lua_host_run: runner could not fetch " + funcName + ": " + fetchError;
    } else if (alive) {
        // Runner step 6: yielded without error -> alive. Not an error; the
        // record stays in the table, and the mission loop's scheduler passes
        // (0x00e0cf50, Sec26.27 RESOLVED 2026-10-02) resume it from then on.
        r.callOk = true;
        r.suspended = true;
        r.recordId = recordId;
        // Capture where it's actually waiting: level 1 is the mission script's
        // own frame that called thread_yield (level 0 is bareGuard's C frame -
        // see this function's own doc comment above). Falls back to level 0 if
        // level 1 isn't available (a yield from somewhere other than the
        // bareGuard<thread_yield> path, if one is ever added).
        lua_Debug ar;
        if (lua_getstack(co, 1, &ar) && lua_getinfo(co, "Sln", &ar)) {
            r.suspendedAt = (ar.short_src[0] ? ar.short_src : "?") + std::string(":") +
                             std::to_string(ar.currentline) +
                             (ar.name ? (std::string(" (in ") + ar.name + ")") : "");
        } else if (lua_getstack(co, 0, &ar) && lua_getinfo(co, "Sln", &ar)) {
            r.suspendedAt = (ar.short_src[0] ? ar.short_src : "?") + std::string(":") +
                             std::to_string(ar.currentline) + " (C frame, no Lua caller level)";
        }
    } else {
        // Finished or failed. The error is read from the coroutine itself
        // (lua_resume marks a failed coroutine dead with the message on its
        // stack), not from threads.errors(): that vector is shared with any
        // thread_new child that failed during the call, so "it grew" would
        // misattribute a child's error to `_start`. This gives the bare
        // message, as the old lua_tostring(L, -1) did, without the runner's
        // "<name>: " prefix - the per-mission error columns and bridge_diff
        // comparisons keep their meaning across runs.
        const int st = lua_status(co);
        if (st != 0 && st != LUA_YIELD) {
            const char* msg = lua_tostring(co, -1);
            r.callOk = false;
            r.callError = msg ? msg : "(no error message)";
            printTraceRingIfAny(co, funcName, r.callError);
        } else {
            r.callOk = true;
        }
    }
    luaL_unref(L, LUA_REGISTRYINDEX, coRef);
    // Same end state as the old bare-pcall version: L's stack empty. Measured
    // (2026-10-02, fixture and real cache): L is already at 0 here - the
    // call's results stay on `co` - so this only guards the contract.
    lua_settop(L, 0);
    return r;
}

} // namespace

// Globals the host registers beyond the tagged list: the 24 bare globals of
// 0x00e0f900 (spec-lua-bindings.md Sec13.2) and the scaffold's thread_close.
const size_t kHostExtraGlobals = 24 + 1;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: lua_host_run <cache_dir> <registered_tagged.txt> <out_dir> [max_scripts_to_run]\n"
                     "                    [--preload-states=spec16.4|tag]\n"
                     "  --preload-states=spec16.4  (default) run the spec-lua-bindings.md Sec16.4 preloads only in\n"
                     "      the state Sec16.4 names (vint_lib/game_ui_globals/vdo_base_object/vdo_anim_object/\n"
                     "      vdo_input_tracker: ui only; game_lib: gameplay only; system_lib: both). CONFIRMED and\n"
                     "      cleared for implementation 2026-10-01 (Sec16.1/Sec16.4). spec16.4-highconf is accepted\n"
                     "      as an alias.\n"
                     "  --preload-states=tag  the old behaviour: these files follow the per-script state tag like\n"
                     "      any other script (both states when the tag is OPEN).\n"
                     "  --host-rng[=SEED]  HYPOTHESIS / HOST SUBSTITUTE, off by default: rand_int/rand_float draw\n"
                     "      from a ring filled once by a deterministic host generator (default seed 1) instead of\n"
                     "      the CONFIRMED file-image ring, where every draw returns lo until a fill (the engine\n"
                     "      generator and fill are OPEN, spec-lua-api-behaviour.md Sec26.27). Values are not the\n"
                     "      game's. Recorded as host_rng_option= in verdict_summary.txt.\n"
                     "  --display=WxH | --display=none  the display resolution handed to the UI subsystem init\n"
                     "      0x00e23910 at front-end bring-up (spec-lua-api-behaviour.md Sec26.26): the width/height\n"
                     "      globals and the per-thread record vint_is_std_res reads, the display mode and the layout\n"
                     "      index. A HOST INPUT, not an engine value: default 1280x720 (CHOSEN, the spec's first\n"
                     "      worked example); none = the init does not run and those values stay OPEN. Recorded as\n"
                     "      display_option= in verdict_summary.txt.\n"
                     "  --tick-budget=N  CHOSEN override of the mission-drive loop's per-mission tick budget\n"
                     "      (default 20). One tick models kFadeHostMsPerTick (333 ms, CHOSEN) of this host's own\n"
                     "      simulated time (see that constant's own doc comment for the full cadence reasoning).\n"
                     "      Raise this to distinguish a 'still suspended' mission that is a real blocker from one\n"
                     "      that is just mid-way through a long, legitimate wait; mission_simulated_seconds_budget=\n"
                     "      in verdict_summary.txt reports what this host modelled, and start_still_suspended_at=\n"
                     "      (per-mission output / TSV) reports where it ended up parked.\n"
                     "  --auto-answer-hud-prompts[=N]  THIS TASK (hud-prompt-autoanswer-harness): each tick,\n"
                     "      any live hud_prompt(target, promptId, callbackName) request (Sec13.3) that has been\n"
                     "      showing for at least N ticks (default 15) is answered by spawning a coroutine for\n"
                     "      its own callbackName - the SAME BareThreadTable allocate()-then-let-the-scheduler-\n"
                     "      resume-it mechanism --probe-autopilot already uses for on_trigger/on_death/etc,\n"
                     "      reused here for hud_prompt. Generic over every currently showing prompt, not\n"
                     "      hardcoded to M05_JUMP. Fires LIVE during the normal tick loop (not a post-hoc probe\n"
                     "      like --probe-autopilot) so a mission blocked only on a HUD-prompt wait can keep\n"
                     "      advancing within its own --tick-budget= instead of tick-budget-stalling on it\n"
                     "      forever and masking whatever its own NEXT real stall point is.\n"
                     "  --probe-unresolved-names  DIAGNOSTIC ONLY, off by default, NEVER a real measurement: a\n"
                     "      named-object resolution miss (the OPEN gate behind most 'Killbane'-style blockers)\n"
                     "      returns a synthetic \"resolves\" placeholder instead of refusing, so a mission can\n"
                     "      keep running past it to show what blocks it NEXT. Every probed (synthetic) name is\n"
                     "      listed in verdict_summary.txt as probed_unresolved_names=; every other OPEN item a\n"
                     "      mission still hits past that point (another named-object check, a per-character/\n"
                     "      per-vehicle field, an unimplemented stub) is still genuinely OPEN and reported as\n"
                     "      such - this flag widens exactly one gate, nothing else.\n"
                     "  --no-vint-docs  turn OFF real .vint_doc loading (on by default since 2026-10-03): every\n"
                     "      .vint_doc found is parsed (layout CONFIRMED, spec-vint-doc-format.md) and, the first\n"
                     "      time a chunk named by a document's lua_script_file runs (or a hook it defined fires),\n"
                     "      loaded into the Vint object registry, with that document as the call's current\n"
                     "      document (CHOSEN association). Recorded as vint_docs= in verdict_summary.txt.\n"
                     "  --trace-host-calls-before-error=N  DIAGNOSTIC ONLY, off by default (N=0): on any\n"
                     "      mission-thread Lua error (the initial _start call or a later scheduler-pass resume),\n"
                     "      print to stderr, prefixed [trace-host-calls], the last N global calls (name only,\n"
                     "      via LUA_HOOKCALL) made during that specific resume before it errored. The ring resets\n"
                     "      fresh before every individual resume and is discarded unless that resume's own call\n"
                     "      ends in a Lua error, so it always reflects what led to one specific error, never a\n"
                     "      stale trace from an unrelated resume. A mission classified still_suspended at budget\n"
                     "      exhaustion instead prints TWO rings (2026-10-07): its own LAST resume (what its wait\n"
                     "      loop polls right now) and its own FIRST resume (its one-time _start-call setup, e.g.\n"
                     "      a trigger_enable/on_trigger/on_vehicle_enter callback registration, before it ever\n"
                     "      reached that wait loop) - the first-resume ring is captured once, right when\n"
                     "      callMissionStart's single resume of that coroutine returns, and is never overwritten\n"
                     "      afterward, unlike the last-resume ring. Never alters real engine behavior.\n"
                     "  --probe-autopilot  DIAGNOSTIC ONLY, off by default, NEVER on by default, NEVER changes\n"
                     "      baseline drive behavior: for EVERY mission classified still_suspended at budget\n"
                     "      exhaustion, checks that mission's own first-resume trace (g_firstResumeTraceRings,\n"
                     "      same data --trace-host-calls-before-error=N already prints). If it shows a CONFIRMED\n"
                     "      callback-registration native (on_trigger/on_trigger_exit/on_death/\n"
                     "      on_vehicle_destroyed/on_attack_performed/on_take_damage/on_revived/\n"
                     "      on_qte_animation_trigger - see isCallbackRegistrationNative's own doc comment for the\n"
                     "      full citation list, and which of this flag's own candidate natives were checked and\n"
                     "      EXCLUDED, e.g. trigger_enable has no callback argument at all) with its callback-name\n"
                     "      argument recovered, this flag spawns a new coroutine for that one Lua global, zero\n"
                     "      arguments (no spec section establishes what arguments a FIRED, as opposed to\n"
                     "      registered, callback receives - a CHOSEN, flagged judgment call, not invented state),\n"
                     "      matching the real dispatcher's own confirmed mechanism (spec-lua-api-behaviour.md\n"
                     "      Sec1.14, 2026-10-09: spawn via BareThreadTable::allocate(), never a synchronous\n"
                     "      lua_call/lua_pcall) - it is NOT resumed here; only the follow-up window's scheduler\n"
                     "      pass(es), below, resume it. Tagged SYNTHETIC in [probe-autopilot] stderr output and in\n"
                     "      verdict_mission_drive.tsv/summary, then runs a\n"
                     "      bounded follow-up window to see what breaks NEXT. If no such native is recoverable,\n"
                     "      records \"nothing to synthesize\" explicitly rather than a silent empty result - itself\n"
                     "      a real finding (the wait is most likely satisfied by polled world state, not a\n"
                     "      callback). Auto-raises --trace-host-calls-before-error to 200 if not already set (so\n"
                     "      this flag is self-sufficient), printed to stdout so that is never silent either.\n";
        return 1;
    }
    // Optional flags anywhere after the 3 positionals; the remaining
    // positional (if any) is max_scripts_to_run.
    bool preloadStatesSpec164 = true; // Sec16.1/Sec16.4, CONFIRMED and cleared 2026-10-01
    bool hostRng = false;      // --host-rng: only when explicitly given
    uint64_t hostRngSeed = 1;
    // --display (Sec26.26): CHOSEN default 1280x720, a host input (see usage).
    bool displayInit = true;
    int32_t displayWidth = 1280, displayHeight = 720;
    // --tick-budget (CHOSEN override, not a spec fact): how many mission-drive
    // ticks to run per mission before "budget exhaustion" - see
    // kMissionTickBudget's own doc comment below for what one tick models.
    // Default unchanged (20); a caller investigating whether a "still
    // suspended" mission is a real blocker or just a long, legitimate wait
    // (e.g. a scripted delay/fade) should pass a much larger value and read
    // start_still_suspended_at in the per-mission output / TSV to see where
    // it parked.
    int missionTickBudget = 20;
    // --probe-unresolved-names: DIAGNOSTIC ONLY, off by default (see
    // OpenValueMap::enableProbeFallback's own doc comment, open_state.h).
    // Never feeds a real baseline measurement - every number it influences
    // is a synthetic stand-in for the real, still-genuinely-OPEN named-
    // object resolution map, used only to see what blocks a mission NEXT
    // once that one gate stops refusing.
    bool probeUnresolvedNames = false;
    // --no-vint-docs: turn off real .vint_doc loading + the per-chunk
    // document context (2026-10-03, see the block after the archive walk).
    bool vintDocs = true;
    // --probe-autopilot: DIAGNOSTIC ONLY, off by default - see its own
    // usage text above and g_probeAutopilotEnabled's own doc comment.
    bool probeAutopilot = false;
    // --auto-answer-hud-prompts[=N] (THIS TASK, hud-prompt-autoanswer-
    // harness): this mission-drive loop has no keyboard/player-input concept
    // at all (it drives a mission's own Lua coroutines directly, tick by
    // tick - there is no "--no-input" flag here because there is no OTHER
    // mode; every run through this tool already IS the scripted/automated
    // case tools/sr3_viewer.cpp's own --no-input covers for its separate,
    // interactive --mission playtest path). A genuine player-input-gated
    // wait - e.g. m05.lua:1141's `while not M05_jumped do thread_yield() end`,
    // unblocked only by the registered callback a live
    // hud_prompt(target, promptId, callbackName) call names (spec-lua-api-
    // behaviour.md Sec13.3) - has no scripted way through this loop today,
    // so a mission stuck on one tick-budget-stalls regardless of how large
    // --tick-budget= is set, masking whatever that mission's own NEXT real
    // stall point is further down its script. When this flag is given, each
    // tick, any EngineState::hudPromptRequests() entry that has been showing
    // for at least N ticks (default 15; --auto-answer-hud-prompts=N
    // overrides; same semantics as --tick-budget='s own "=N" convention)
    // is answered by spawning a coroutine for its own registered callback
    // name via BareThreadTable::allocate() - the SAME "spawn, let the next
    // scheduler pass resume it" mechanism this file's own --probe-autopilot
    // dispatch block (isCallbackRegistrationNative family, further above)
    // already established for on_trigger/on_death/on_vehicle_enter, reused
    // here for hud_prompt rather than inventing a parallel mechanism -
    // generic over ANY currently showing prompt, not hardcoded to M05_JUMP.
    // Unlike --probe-autopilot (a post-hoc, one-shot diagnostic fired only
    // AFTER a mission is already classified still_suspended at budget
    // exhaustion), this fires LIVE, every tick, while the main tick loop is
    // still running - so a mission that is only blocked on a HUD prompt can
    // actually keep advancing within its own normal tick budget, rather than
    // needing a separate follow-up probe after the fact.
    bool autoAnswerHudPrompts = false;
    int hudPromptAutoAnswerDelayTicks = 15; // CHOSEN default, overridden by --auto-answer-hud-prompts=N
    std::vector<std::string> extraPositional;
    for (int a = 4; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--preload-states=spec16.4" || arg == "--preload-states=spec16.4-highconf") preloadStatesSpec164 = true;
        else if (arg == "--preload-states=tag") preloadStatesSpec164 = false;
        else if (arg.rfind("--tick-budget=", 0) == 0) {
            std::string v = arg.substr(14);
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos || std::stol(v) <= 0) {
                std::cerr << "bad --tick-budget value (want a positive integer): " << v << "\n";
                return 1;
            }
            missionTickBudget = std::stoi(v);
        }
        else if (arg == "--probe-unresolved-names") probeUnresolvedNames = true;
        else if (arg == "--probe-autopilot") probeAutopilot = true;
        else if (arg == "--auto-answer-hud-prompts") autoAnswerHudPrompts = true;
        else if (arg.rfind("--auto-answer-hud-prompts=", 0) == 0) {
            std::string v = arg.substr(std::string("--auto-answer-hud-prompts=").size());
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) {
                std::cerr << "bad --auto-answer-hud-prompts value (want a non-negative integer): " << v << "\n";
                return 1;
            }
            autoAnswerHudPrompts = true;
            hudPromptAutoAnswerDelayTicks = std::stoi(v);
        }
        else if (arg == "--no-vint-docs") vintDocs = false;
        else if (arg.rfind("--trace-host-calls-before-error=", 0) == 0) {
            std::string v = arg.substr(32);
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) {
                std::cerr << "bad --trace-host-calls-before-error value (want a non-negative integer): " << v << "\n";
                return 1;
            }
            g_traceRingSize = std::stoul(v);
        }
        else if (arg == "--host-rng") hostRng = true;
        else if (arg.rfind("--host-rng=", 0) == 0) {
            std::string v = arg.substr(11);
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) {
                std::cerr << "bad --host-rng seed: " << v << "\n";
                return 1;
            }
            hostRng = true;
            hostRngSeed = std::stoull(v);
        } else if (arg == "--display=none") {
            displayInit = false;
        } else if (arg.rfind("--display=", 0) == 0) {
            const std::string v = arg.substr(10);
            const size_t x = v.find('x');
            const std::string ws = x == std::string::npos ? std::string() : v.substr(0, x);
            const std::string hs = x == std::string::npos ? std::string() : v.substr(x + 1);
            auto digits = [](const std::string& s) {
                return !s.empty() && s.size() <= 6 && s.find_first_not_of("0123456789") == std::string::npos;
            };
            if (!digits(ws) || !digits(hs) || std::stoi(ws) == 0 || std::stoi(hs) == 0) {
                std::cerr << "bad --display value (want WxH, e.g. 1920x1080, or none): " << v << "\n";
                return 1;
            }
            displayInit = true;
            displayWidth = std::stoi(ws);
            displayHeight = std::stoi(hs);
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "unknown option: " << arg << "\n";
            return 1;
        } else extraPositional.push_back(arg);
    }
    // --probe-autopilot setup (2026-10-07): enable the capture gate, and
    // auto-raise the trace ring to a size this project's own earlier pass
    // (results/trace_host_calls_firstresume_20261007.md) already measured
    // as large enough to avoid truncating a mission's real first-resume
    // sequence (that pass's own =15 run hit the cap for 3 of 6 missions;
    // =200 did not, for any of them) - not re-chosen here, reused. Only
    // when the caller didn't already pass their own
    // --trace-host-calls-before-error=N, so an explicit N is never
    // silently overridden; printed to stdout (below, with the rest of
    // this run's own option echo) so the auto-raise is never silent.
    g_probeAutopilotEnabled = probeAutopilot;
    constexpr size_t kProbeAutopilotAutoRingSize = 200;
    bool probeAutopilotAutoRaisedRingSize = false;
    if (probeAutopilot && g_traceRingSize == 0) {
        g_traceRingSize = kProbeAutopilotAutoRingSize;
        probeAutopilotAutoRaisedRingSize = true;
    }
    fs::path cacheDir = argv[1];
    std::string registrationListPath = argv[2];
    fs::path outDir = argv[3];
    // The two further tools/ inputs below are read from the same directory as
    // the registration list (cloud phase, 2026-09-30: the PC bridge runs tools
    // with cwd = the job's output dir, so a bare "tools/..." would not
    // resolve). Unchanged for the usual `tools/lua_all_registered_*.txt` run
    // from team-b/.
    fs::path toolsDir = fs::path(registrationListPath).parent_path();
    uint64_t maxScriptsToRun = extraPositional.empty() ? UINT64_MAX : std::stoull(extraPositional[0]);
    fs::create_directories(outDir);

    auto t0 = std::chrono::steady_clock::now();

    // --- Load the real (current-count) registration list and build the host ---
    std::vector<RegisteredName> allNames = loadTaggedRegistrationList(registrationListPath);
    std::cout << "Loaded " << allNames.size() << " registered names from " << registrationListPath << "\n";
    Host host(allNames);
    // --host-rng (HYPOTHESIS / host substitute, opt-in): before any script draws.
    if (hostRng) host.useHostRng(hostRngSeed);
    // --probe-unresolved-names (DIAGNOSTIC ONLY, opt-in, see usage text above
    // and EngineState::enableUnresolvedNameProbe()/probedFieldOr()'s own doc
    // comments): widens the named-object resolution gate, and lets specific
    // per-character/per-vehicle OPEN fields (those routed through
    // probedFieldOr(), e.g. max hit points/ignore-AI) also fall back to a
    // labelled placeholder for a name this same run already probed.
    if (probeUnresolvedNames) {
        host.engineState().enableUnresolvedNameProbe();
        // Same single switch also widens get_dist's own worldPosition gate
        // (EngineState::worldPositionOr(), 2026-10-03 peer follow-up) - a
        // second, deliberately separate DIAGNOSTIC ONLY extension, not
        // gated on probedFieldOr()'s "owner name was itself probed" rule
        // (see that method's own doc comment for why).
        host.engineState().enableWorldPositionProbe();
    }
    // Front-end bring-up, before any script (Sec26.24/Sec26.26: the UI
    // subsystem init 0x008489e0 -> 0x00e23910 runs before the screen_fade
    // init and before UI scripts): the display resolution (host input).
    std::string displayOption;
    if (displayInit) {
        host.engineState().vintUiSubsystemInit(displayWidth, displayHeight);
        displayOption = std::to_string(displayWidth) + "x" + std::to_string(displayHeight) +
                        " (UI subsystem init 0x00e23910 run with this host display - CHOSEN default 1280x720 unless "
                        "--display=WxH; a host input, not an engine value; display_mode=" +
                        (host.engineState().vintDisplayMode().known()
                             ? std::to_string(host.engineState().vintDisplayMode().get())
                             : std::string("OPEN")) +
                        ")";
    } else {
        displayOption = "none (--display=none: UI subsystem init not run, the width/height record stays OPEN)";
    }
    // The scene table (Sec26.25 item 5): real data from the cache.
    const std::string sceneTableSummary = installSceneTableFromCache(cacheDir, host.engineState());
    // The continuous-explosion definition table (Sec28.16): real data from the cache.
    const std::string continuousExplosionTableSummary = installContinuousExplosionTableFromCache(cacheDir, host.engineState());
    // The spawn-rank Hit_Points value set (Sec7.12/Sec34.2): real data from the cache.
    const std::string spawnRankHitPointsSummary = installSpawnRankHitPointsFromCache(cacheDir, host.engineState());
    // The weapon-definition name table (Sec4.10/Sec4.11): real data from the cache.
    const std::string weaponDefinitionTableSummary = installWeaponDefinitionTableFromCache(cacheDir, host.engineState());
    (void)installInventoryItemTableFromCache(cacheDir, host.engineState());
    // The vehicle-type class table (spec-vehicle-data.md Sec7, vehicle-class
    // xtbl wiring task): real data from the cache. MUST run before
    // installZoneObjectsFromCache() below - see that function's own top
    // comment for why.
    const std::string vehicleTypeClassTableSummary =
        installVehicleTypeClassTableFromCache(cacheDir, host.engineState());
    // Real `.czn_pc` Objects (0x2234) data (spec-zone-data-format.md Sec11):
    // the real name<->ObjectRecord resolver (2026-10-08 zone-data
    // fast-follow) - see installZoneObjectsFromCache's own top comment.
    const std::string zoneObjectsSummary = installZoneObjectsFromCache(cacheDir, host.engineState());
    std::cout << "display_option=" << displayOption << "\nzscene_table=" << sceneTableSummary << "\n";
    std::cout << "continuous_explosion_table=" << continuousExplosionTableSummary << "\n";
    std::cout << "spawn_rank_hit_points=" << spawnRankHitPointsSummary << "\n";
    std::cout << "weapon_definition_table=" << weaponDefinitionTableSummary << "\n";
    std::cout << "vehicle_type_class_table=" << vehicleTypeClassTableSummary << "\n";
    std::cout << "zone_objects=" << zoneObjectsSummary << "\n";
    // NEW 2026-10-10 (vehicle-class xtbl wiring task): a direct, named
    // real-data check for the one vehicle instance the removed
    // "m05_heli_pierce" hardcoded special case used to cover unconditionally
    // - reports whether it now resolves its real class via the general,
    // data-driven path (real ingested zone data joined against the real
    // xtbl-sourced vehicle-type table) rather than assuming it still does
    // just because the old special case is gone from the source. Printed
    // unconditionally (cheap, one lookup) so every real-cache run's own
    // stdout/verdict_summary.txt makes this fact plain either way.
    {
        const sr3zone::ObjectRecord* m05Heli = host.engineState().resolveZoneObject("m05_heli_pierce");
        std::string m05HeliStatus;
        if (!m05Heli) {
            m05HeliStatus = "not found in any ingested real zone data";
        } else {
            std::optional<std::string> typeName = sr3zone::ScriptVehicleTypeName(*m05Heli);
            std::optional<std::string> className = sr3zone::ClassNameForHash(m05Heli->classHash);
            bool flyingTypeKnown = host.engineState().hasVehicle("m05_heli_pierce") &&
                                   host.engineState().getOrCreateVehicle("m05_heli_pierce").flyingType.known();
            m05HeliStatus = "found class=" + (className.has_value() ? *className : std::string("(unresolved)")) +
                            " script_vehicle_type_name=" + (typeName.has_value() ? *typeName : std::string("(none)")) +
                            " flyingType_known=" + (flyingTypeKnown ? "true" : "false");
            if (flyingTypeKnown) {
                m05HeliStatus += " flyingType_value=" +
                                  std::to_string(host.engineState().getOrCreateVehicle("m05_heli_pierce").flyingType.get());
            }
        }
        std::cout << "m05_heli_pierce_check=" << m05HeliStatus << "\n";
    }
    std::cout << "Host built: gameplay state has " << host.gameplayStubCount()
              << " stubs, UI state has " << host.uiStubCount() << " stubs ("
              << (host.gameplayStubCount() + host.uiStubCount()) << " total, matching the "
              << allNames.size() << "-name input list)\n";
    std::cout << "  plus the " << bareGlobalRoster().size() << " bare globals of 0x00e0f900 (spec-lua-bindings.md "
              << "Sec13.2/Sec16.4, thread_new/_yield/_kill/_check_done among them) and the scaffold's "
              << "thread_close, all into BOTH states - see include/sr3luahost/bare_globals.h.\n";

    // --- OPEN engine-state slots at start (after applySpecInitialState) -----
    // verdict_open_state.tsv: which slots a synced Team A answer has filled.
    std::string openStateSummary;
    {
        std::ofstream os(outDir / "verdict_open_state.tsv");
        os << "area\tglobal\tspec\tkind\tknown\tknown_keys\n";
        size_t known = 0, total = 0;
        for (const auto& r : host.engineState().openSlotInventory()) {
            os << r.area << '\t' << r.global << '\t' << r.spec << '\t' << r.kind << '\t' << (r.known ? 1 : 0) << '\t'
               << r.knownKeys << '\n';
            ++total;
            known += (r.known || r.knownKeys > 0) ? 1 : 0;
        }
        openStateSummary = std::to_string(known) + "/" + std::to_string(total) +
                           " (slots with any value at start; per slot: verdict_open_state.tsv)";
    }

    // --- The confirmed/pattern-based hook census this run fires ----------
    const std::vector<HookSpec>& hooks = confirmedHooks();
    {
        size_t g1 = 0, g2 = 0, g3 = 0;
        for (auto& h : hooks) {
            if (h.group == HookGroup::Group1_GeneralNamed) ++g1;
            else if (h.group == HookGroup::Group2_LifecycleUnconditional) ++g2;
            else ++g3;
        }
        std::cout << "\nLoaded " << hooks.size() << " confirmed/pattern-based hook names to fire "
                  << "per script (spec-lua-bindings.md Sec8.2-8.4/Sec12.2-12.9): " << g1
                  << " Group-1 general (existence-gated, CONFIRMED), " << g2
                  << " Group-2 lifecycle (UNCONDITIONAL real call, CONFIRMED), " << g3
                  << " Group-3 mission-numbered (existence-gated, HIGH CONFIDENCE / pattern-based, "
                  << "NOT individually dispatcher-confirmed).\n";
        std::cout << "  Every hook is called with ZERO arguments - spec Sec8.3 confirms the call-in "
                  << "MECHANISM only, not per-hook argument shapes, which remain OPEN. NOT fired: "
                  << "Group 4 (7 sprintf-templated per-widget-instance families, spec Sec8.4) and "
                  << "Group 5 (2 data-driven runtime-populated tables, spec Sec8.5) - both are "
                  << "explicitly unbounded/runtime-only per the spec itself, so no static name list "
                  << "exists to fire; see include/sr3luahost/hook_registry.h's own top comment.\n";
    }

    // --- A real, checked subtlety worth stating plainly rather than
    // leaving implicit: does any confirmed/pattern-based hook name ALSO
    // appear in the registered stub list? If so, that hook's "existence"
    // check can read true from the moment Host is constructed - via the
    // pre-registered generic logging stub sitting in that name already -
    // even before any real script defines its own version of that name.
    // Once a real script DOES assign its own function to that same
    // global, ordinary Lua semantics mean the real function replaces the
    // stub for every later firing pass (shared, persistent globals - see
    // this file's own top comment) - so such a hook's own ok/err split
    // over the whole run can be a genuine mixture of "hit the always-
    // succeeding generic stub" and "hit a real, possibly-erroring
    // script-defined override," not one single, uniform behavior
    // throughout. Computed here, not asserted - see the printed names
    // below (0 names is a real, checked negative, not an unexamined one). --
    std::vector<std::string> hookNamesAlsoRegisteredAsStubs;
    {
        std::unordered_map<std::string, std::string> registeredCluster;
        for (auto& rn : allNames) registeredCluster[rn.name] = rn.cluster;
        for (auto& h : hooks) {
            auto it = registeredCluster.find(h.name);
            if (it != registeredCluster.end()) {
                hookNamesAlsoRegisteredAsStubs.push_back(h.name + " (registered cluster=" + it->second + ")");
            }
        }
    }
    std::cout << "  Checked overlap between the " << hooks.size() << " hook names and the " << allNames.size()
              << "-name registered stub list: " << hookNamesAlsoRegisteredAsStubs.size() << " name(s) appear in "
              << "BOTH (see this block's own comment for why that matters to how that hook's own numbers "
              << "should be read).";
    if (!hookNamesAlsoRegisteredAsStubs.empty()) {
        std::cout << " Name(s): ";
        for (size_t i = 0; i < hookNamesAlsoRegisteredAsStubs.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << hookNamesAlsoRegisteredAsStubs[i];
        }
    }
    std::cout << "\n";

    // --- Walk every real archive in cache_dir, exactly like the census ---
    std::vector<fs::path> archives;
    for (auto& ent : fs::directory_iterator(cacheDir)) {
        if (!ent.is_regular_file()) continue;
        if (toLower(ent.path().extension().string()) == ".vpp_pc") archives.push_back(ent.path());
    }
    std::sort(archives.begin(), archives.end());

    WalkState st;
    uint64_t grandBytes = 0;
    for (auto& archivePath : archives) {
        std::string archiveName = archivePath.filename().string();
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(archivePath);
        } catch (const std::exception& ex) {
            std::cerr << "READ FAILED " << archiveName << ": " << ex.what() << "\n";
            continue;
        }
        grandBytes += bytes.size();
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walk(st, root, archiveName, "");
        } catch (const std::exception& ex) {
            std::cerr << "PARSE FAILED " << archiveName << ": " << ex.what() << "\n";
        }
        std::cout << "  walked " << archiveName << " (" << bytes.size() << " bytes) - "
                  << st.found.size() << " .lua entries found so far\n";
    }

    std::cout << "Archive walk complete: " << archives.size() << " archives, " << grandBytes
              << " bytes, " << st.stats.entries << " directory entries, " << st.found.size()
              << " real .lua entries found (population denominator for everything below).\n";

    // --- Real .vint_doc documents -> the UI state's Vint element tree
    // (2026-10-03). Every .vint_doc leaf the walk found is parsed
    // (sr3vintdoc::parseDocument, layout CONFIRMED in spec-vint-doc-format.md
    // at main 67455c3) and indexed by its `lua_script_file` metadata (the
    // value is stored with or without ".lua" in real files; compared
    // case-insensitively without it). Host's document-context resolver then
    // runs each chunk - and each hook function, by the chunk that defined
    // it - in the context of the document naming that chunk as its script,
    // loading the document into EngineState the first time it is needed
    // (CHOSEN association, see Host::setDocumentContextResolver). A chunk no
    // document names keeps the OPEN current document, as before.
    // --no-vint-docs disables all of this (the pre-2026-10-03 behaviour) for
    // A/B measurement.
    auto scriptKey = [](std::string s) {
        s = toLower(s);
        size_t slash = s.find_last_of("/\\");
        if (slash != std::string::npos) s = s.substr(slash + 1);
        if (s.size() > 4 && s.compare(s.size() - 4, 4, ".lua") == 0) s.resize(s.size() - 4);
        return s;
    };
    std::vector<sr3vintdoc::Document> parsedDocs;
    std::vector<size_t> parsedDocSource; // index into st.docs
    std::unordered_map<std::string, size_t> docByScript; // scriptKey -> index into parsedDocs (first in walk order)
    std::vector<uint32_t> docHandleByParsed;
    uint64_t docsParseFailed = 0, docsWithoutScript = 0, docScriptDuplicates = 0, docsLoaded = 0;
    uint64_t docObjectsLoaded = 0, docPropertiesLoaded = 0, docContextHits = 0, docContextMisses = 0;
    if (vintDocs) {
        for (size_t i = 0; i < st.docs.size(); ++i) {
            const auto& d = st.docs[i];
            try {
                parsedDocs.push_back(sr3vintdoc::parseDocument(vpp::ByteView(d.bytes.data(), d.bytes.size())));
                parsedDocSource.push_back(i);
            } catch (const std::exception&) {
                ++docsParseFailed;
                continue;
            }
            const std::string* script = parsedDocs.back().metadataValue("lua_script_file");
            if (!script || script->empty()) { ++docsWithoutScript; continue; }
            if (!docByScript.emplace(scriptKey(*script), parsedDocs.size() - 1).second) ++docScriptDuplicates;
        }
        docHandleByParsed.assign(parsedDocs.size(), 0);
        host.setDocumentContextResolver([&](const std::string& chunkName) -> std::optional<uint32_t> {
            auto it = docByScript.find(scriptKey(chunkName));
            if (it == docByScript.end()) { ++docContextMisses; return std::nullopt; }
            ++docContextHits;
            uint32_t& h = docHandleByParsed[it->second];
            if (h == 0) {
                const auto& src = st.docs[parsedDocSource[it->second]];
                h = host.engineState().loadVintDocument(src.entryName, parsedDocs[it->second]);
                const LoadedVintDocument* ld = host.engineState().loadedVintDocument(h);
                ++docsLoaded;
                docObjectsLoaded += ld ? ld->objectCount : 0;
                docPropertiesLoaded += ld ? ld->propertyCount : 0;
            }
            return h;
        });
    }
    std::cout << "vint_docs: " << st.docs.size() << " .vint_doc entries found, " << parsedDocs.size() << " parsed, "
              << docsParseFailed << " failed to parse, " << docByScript.size() << " distinct lua_script_file names ("
              << docsWithoutScript << " documents name no script, " << docScriptDuplicates
              << " later documents repeat an already-indexed script and are not used)"
              << (vintDocs ? "" : " - DISABLED by --no-vint-docs") << "\n";

    // --- Real per-script state-tag resolution (this task, spec-lua-
    // bindings.md Sec14.5 - see the `statetag` namespace above for the
    // full method/citation). Built once, over the SAME `archives` list
    // already walked for .lua scripts above, then resolved per script so
    // the hook-firing loop below can restrict each script's own hooks to
    // its own real state. -------------------------------------------
    std::cout << "\n=== Real per-script state-tag resolution (spec-lua-bindings.md Sec14.5) ===\n";
    statetag::Index stateIdx = statetag::build(archives);
    std::cout << "Walked " << stateIdx.asmInstancesFound << " real .asm_pc manifests, parsed "
              << stateIdx.manifestsParsed << " OK, " << stateIdx.manifestParseFailures << " parse failure(s).\n";
    std::cout << "Real candidate entries seen: " << stateIdx.uiCandidateEntries
              << " (kind 21/22 + type 27 \"Vint LUA script\"), " << stateIdx.gameplayCandidateEntries
              << " (kind 39/40 + type 32 \"Mission LUA script\"); " << stateIdx.byKey.size()
              << " distinct (container,entry) keys indexed, " << stateIdx.conflicts
              << " real cross-manifest conflict(s) (expected 0 per Sec14.5's own \"cleanly, exhaustively "
              << "separated\" population claim - reported, not hidden, either way).\n";

    std::vector<statetag::Tag> scriptTag(st.found.size(), statetag::Tag::Open);
    std::vector<std::string> scriptContainerName(st.found.size());
    uint64_t tagUi = 0, tagGameplay = 0, tagOpen = 0, tagConflict = 0;
    for (size_t i = 0; i < st.found.size(); ++i) {
        scriptTag[i] = statetag::resolve(stateIdx, st.found[i].pathChain, st.found[i].entryName, &scriptContainerName[i]);
        switch (scriptTag[i]) {
            case statetag::Tag::Ui: ++tagUi; break;
            case statetag::Tag::Gameplay: ++tagGameplay; break;
            case statetag::Tag::Conflict: ++tagConflict; break;
            default: ++tagOpen; break;
        }
    }
    std::cout << "Real population split over " << st.found.size() << " found scripts: ui=" << tagUi
              << " gameplay=" << tagGameplay << " OPEN=" << tagOpen << " conflict=" << tagConflict
              << " (conflict is folded into OPEN's firing behavior below, reported separately here per "
              << "the \"no invented fixes\" policy - a real data disagreement is not the same finding as "
              << "a real absence of data).\n";

    // Real, per-script audit trail - written so this split can be checked
    // row-by-row independently, not just trusted from the aggregate above.
    {
        std::ofstream tagOut(outDir / "verdict_script_state_tags.tsv");
        tagOut << "archive\tpath_chain\tentry_name\tresolved_container_name\tresolved_state_tag\n";
        for (size_t i = 0; i < st.found.size(); ++i) {
            tagOut << sanitizeTsv(st.found[i].archive) << "\t" << sanitizeTsv(st.found[i].pathChain) << "\t"
                   << sanitizeTsv(st.found[i].entryName) << "\t" << sanitizeTsv(scriptContainerName[i]) << "\t"
                   << statetag::tagLabel(scriptTag[i]) << "\n";
        }
    }
    std::cout << "Wrote " << (outDir / "verdict_script_state_tags.tsv").string() << "\n";

    // --- Real, disassembly-confirmed preload order for both states
    // (spec-lua-bindings.md Sec16.4, folded in 2026-09-30).
    // CONFIRMED and CLEARED for implementation 2026-10-01 (Sec16.1/Sec16.4
    // re-derived from the executable; HANDOFF Requests to Team A item 8
    // answered). Per Sec16.1 a failed preload is silent and later preloads
    // still run, which is what this sequence does. Before this,
    // this host only ever preloaded game_lib.lua, alone, into gameplay -
    // missing system_lib.lua on BOTH states and the entire UI-state chain.
    //   UI state, in order:       system_lib.lua -> vint_lib.lua ->
    //                             game_ui_globals.lua -> vdo_base_object.lua
    //                             -> vdo_anim_object.lua -> vdo_input_tracker.lua
    //   Gameplay state, in order: system_lib.lua -> game_lib.lua (below)
    // Each file is preloaded via the SAME first-found-instance/runChunk
    // pattern game_lib.lua's own block already used - generalized into one
    // small helper here rather than duplicated per file, since it is now
    // used 7 times (was 1). A later/natural instance of any of these names
    // (if one exists elsewhere in the walk) is still tried normally in the
    // main loop below, unchanged - same precedent as game_lib.lua's own
    // "further instances" note.
    struct PreloadResult {
        std::string name;
        std::string stateLabel;
        size_t instancesFound = 0;
        bool loadOk = false;
        bool pcallOk = false;
        std::string loadError, pcallError;
    };
    // The deferred include queue (spec-lua-bindings.md Sec16.4) loads files by
    // name after a successful load: resolved here against the walked
    // scripts, first found instance, case-insensitively - the same
    // convention as the preloads below (how the engine's file opener treats
    // case is not specced).
    host.setIncludeResolver([&st](const std::string& fileName, std::string& source) {
        std::string want = toLower(fileName);
        for (const auto& f : st.found) {
            if (toLower(f.entryName) == want) {
                source = f.content;
                return true;
            }
        }
        return false;
    });
    auto preloadNamed = [&](const std::string& name, lua_State* L, const std::string& stateLabel) -> PreloadResult {
        PreloadResult pr;
        pr.name = name;
        pr.stateLabel = stateLabel;
        std::string nameLower = toLower(name);
        size_t chosen = SIZE_MAX;
        for (size_t i = 0; i < st.found.size(); ++i) {
            if (toLower(st.found[i].entryName) == nameLower) {
                ++pr.instancesFound;
                if (chosen == SIZE_MAX) chosen = i;
            }
        }
        std::cout << "\n=== preload " << name << " -> " << stateLabel << " state ===\n";
        std::cout << "Found " << pr.instancesFound << " real shipped instance(s) named " << name << ".\n";
        if (chosen == SIZE_MAX) {
            std::cout << "BLOCKING FINDING: no entry named " << name << " was found anywhere in this walk's "
                       << archives.size() << "-archive, " << st.found.size() << "-script population - the real, "
                       << "disassembly-confirmed preload (spec-lua-bindings.md Sec16.4) for the " << stateLabel
                       << " state was skipped for this file.\n";
            return pr;
        }
        const auto& f = st.found[chosen];
        std::cout << "Loading the first found instance for real: archive=" << f.archive << " pathChain=["
                   << f.pathChain << "] (" << f.content.size() << " bytes)\n";
        auto r = host.runChunk(L, f.content, f.entryName);
        pr.loadOk = r.loadOk;
        pr.loadError = r.loadError;
        pr.pcallOk = r.pcallOk;
        pr.pcallError = r.pcallError;
        std::cout << "  luaL_loadbuffer: " << (r.loadOk ? "OK" : ("FAILED: " + r.loadError)) << "\n";
        if (r.loadOk) {
            std::cout << "  lua_pcall: " << (r.pcallOk ? "OK (ran to completion)" : ("FAILED: " + r.pcallError)) << "\n";
        }
        if (pr.instancesFound > 1) {
            std::cout << "  (" << (pr.instancesFound - 1) << " further instance(s) exist in other archives/DLCs - "
                       << "only the first was given this dedicated preload treatment; the rest are tried like any "
                       << "other script in the main loop below, in their own natural walk order.)\n";
        }
        return pr;
    };

    PreloadResult pre_system_ui = preloadNamed("system_lib.lua", host.uiState(), "ui");
    PreloadResult pre_vint_lib = preloadNamed("vint_lib.lua", host.uiState(), "ui");
    PreloadResult pre_game_ui_globals = preloadNamed("game_ui_globals.lua", host.uiState(), "ui");
    PreloadResult pre_vdo_base = preloadNamed("vdo_base_object.lua", host.uiState(), "ui");
    PreloadResult pre_vdo_anim = preloadNamed("vdo_anim_object.lua", host.uiState(), "ui");
    PreloadResult pre_vdo_input = preloadNamed("vdo_input_tracker.lua", host.uiState(), "ui");
    // cell_foreground.lua: NOT part of the real, disassembly-confirmed
    // fixed UI-state preload chain above (spec-lua-bindings.md Sec16.4) -
    // this is this project's OWN stated engineering choice, not a measured
    // fact. Real finding (spec-lua-bindings.md Sec17, 2026-09-30): cell_
    // menu_main.lua's own real parent/container document, cell_foreground
    // .lua, defines an 11-name plain Lua enum (ID_MISSIONS et al, NAME =
    // value, no local) that cell_menu_main.lua depends on at its own very
    // first executable line - not a missing native registration (Sec17's
    // own exhaustive negative: zero native mechanism anywhere in this
    // binary pushes a bare number/boolean global by name at all). No real
    // sibling-document load-order data was found in the asm container this
    // pass, so this is a labelled choice (load cell_foreground.lua first),
    // not a recovered fact - loaded into the UI state only (cell_menu_main
    // .lua's own real home is almost certainly the UI/interface state, a
    // cell-phone menu system, matching the rest of this preload chain), NOT
    // gameplay - so a gameplay-state run of cell_menu_main.lua is expected
    // to keep failing on ID_MISSIONS, a real wrong-state-test artifact per
    // this project's own established Sec9.114 framework, not a new bug.
    PreloadResult pre_cell_foreground = preloadNamed("cell_foreground.lua", host.uiState(), "ui");
    PreloadResult pre_system_gp = preloadNamed("system_lib.lua", host.gameplayState(), "gameplay");

    // --- Locate and load game_lib.lua, into the persistent gameplay
    // state, AFTER system_lib.lua above (real order, spec-lua-bindings.md
    // Sec16.4) and before any mission script - matching the real engine's
    // own documented sequence (Sec13.2 Item 2 / Sec16.2: the "game play"
    // state registers its 1,014-name roster, loads system_lib.lua, THEN
    // loads "game_lib.lua", before any mission script runs against it). ---
    std::vector<size_t> gameLibIndices;
    for (size_t i = 0; i < st.found.size(); ++i) {
        if (toLower(st.found[i].entryName) == "game_lib.lua") gameLibIndices.push_back(i);
    }
    std::cout << "\n=== game_lib.lua ===\n";
    std::cout << "Found " << gameLibIndices.size() << " real shipped instance(s) named game_lib.lua.\n";

    bool gameLibLoadedOk = false;
    bool gameLibRanOk = false;
    std::string gameLibLoadError, gameLibRunError;
    size_t gameLibChosenIndex = SIZE_MAX;
    if (!gameLibIndices.empty()) {
        gameLibChosenIndex = gameLibIndices.front();
        const auto& gl = st.found[gameLibChosenIndex];
        std::cout << "Loading the first found instance for real: archive=" << gl.archive
                  << " pathChain=[" << gl.pathChain << "] (" << gl.content.size() << " bytes)\n";
        auto r = host.runChunk(host.gameplayState(), gl.content, gl.entryName);
        gameLibLoadedOk = r.loadOk;
        gameLibLoadError = r.loadError;
        gameLibRanOk = r.pcallOk;
        gameLibRunError = r.pcallError;
        std::cout << "  luaL_loadbuffer: " << (r.loadOk ? "OK" : ("FAILED: " + r.loadError)) << "\n";
        if (r.loadOk) {
            std::cout << "  lua_pcall: " << (r.pcallOk ? "OK (ran to completion)" : ("FAILED: " + r.pcallError)) << "\n";
        }
        if (gameLibIndices.size() > 1) {
            std::cout << "  (" << (gameLibIndices.size() - 1) << " further game_lib.lua instance(s) exist in other "
                      << "archives/DLCs - only the first was given this dedicated first-load treatment; the rest "
                      << "are tried like any other script in the main loop below, in their own natural walk order.)\n";
        }
    } else {
        std::cout << "BLOCKING FINDING: no entry named game_lib.lua was found anywhere in this walk's "
                  << archives.size() << "-archive, " << st.found.size() << "-script population. The gameplay "
                  << "state's stub roster is registered, but the library the real engine loads into that same "
                  << "state before running any mission script (spec-lua-bindings.md Sec13.2) was never located, "
                  << "so nothing further in this run should be read as \"game_lib.lua's own helpers were present.\"\n";
    }

    // --- Cross-reference against the static call-count ranking -----------
    fs::path reconciliationPath = toolsDir / "lua_reconciliation_called_and_registered_1181.tsv";
    auto staticRank = loadStaticReconciliation(reconciliationPath.string());
    std::cout << "\nLoaded " << staticRank.size() << " rows from " << reconciliationPath.string()
              << " for the static-vs-runtime cross-check (0 means that file wasn't found from this working "
              << "directory - the cross-check is skipped, not fabricated).\n";

    // --- Main loop: try every found script's own top-level runChunk()
    // pass, RESTRICTED (this task, step 0 - spec-lua-bindings.md Sec14.5)
    // to each script's own real resolved statetag, in walk order, up to
    // maxScriptsToRun. -----------------------------------------------
    // Real cause this restriction closes (HANDOFF.md Sec9.140's own traced
    // finding): before this task, runChunk() ran EVERY script's own
    // top-level code (including every `function some_hook_name() ... end`
    // definition it contains) in BOTH persistent states unconditionally,
    // regardless of the script's own real state tag - so a UI-tagged
    // script's own hook-function definitions leaked into the gameplay
    // state's shared global table (and vice versa) even though Sec9.140's
    // own EARLIER fix already restricted fireConfirmedHooks()'s CALL SITE
    // to each script's own real state. Restricting THIS call site too
    // (the actual cause Sec9.140 traced, concretely confirmed for
    // horde_results_populate: its gameplay existedAsFunction flipped true
    // at script #645, the first OPEN-tagged script walked after the real
    // UI definer ran) closes that leak at its real source. `ui`/`gameplay`
    // tagged scripts now only run their own top-level code in their own
    // real state; `OPEN`/`conflict` tagged scripts (10/0 of 804) still run
    // in BOTH states - the SAME "maximum observation, stated engineering
    // choice" convention Sec9.140 already established for the hook-firing
    // restriction (see `fireGp`/`fireUi` below, now computed ONCE per
    // script and reused for both this call site and the hook-firing call
    // site, rather than duplicated). -----------------------------------
    uint64_t toRun = std::min<uint64_t>(maxScriptsToRun, st.found.size());
    std::cout << "\n=== Running " << toRun << " of " << st.found.size()
              << " found scripts (population denominator) - top-level runChunk() now RESTRICTED per-script "
              << "to its own real statetag (ui/gameplay -> own state only; OPEN/conflict -> both states) ===\n";

    std::ofstream perScript(outDir / "verdict_per_script.tsv");
    perScript << "archive\tpath_chain\tentry_name\tbytes\tsr3lua_parse_ok\tsr3lua_error\t"
                 "gameplay_runChunk_attempted\tgameplay_load_ok\tgameplay_load_error\tgameplay_pcall_ok\tgameplay_pcall_error\t"
                 "ui_runChunk_attempted\tui_load_ok\tui_load_error\tui_pcall_ok\tui_pcall_error\t"
                 "resolved_state_tag\n";

    uint64_t loadOkCount = 0, pcallOkCount_gp = 0, pcallOkCount_ui = 0;
    uint64_t sr3luaParseOkCount = 0;
    uint64_t agreeCount = 0, disagreeCount = 0; // sr3lua parse-clean vs real Lua load-clean

    // --- Historical (top-level-only) stub-hit tracking, kept SEPARATE from
    // the hook-firing pass below so verdict_stub_hits.tsv keeps meaning
    // EXACTLY what it always meant (HANDOFF.md Sec9.113's own 56-call/
    // 5-name baseline) even though the SAME shared host.hitLog() the hook
    // firing pass below also writes into. Method: seed from whatever
    // game_lib.lua's own dedicated pre-loop run above already produced,
    // then for each script take a call-count snapshot immediately BEFORE
    // and immediately AFTER its own top-level runChunk pair (before any
    // hook firing happens), and add only that delta - which is
    // necessarily identical to what an unmodified, hook-firing-free build
    // of this same loop would have produced, since nothing about the
    // runChunk calls themselves changed. -----------------------------
    struct HistHit {
        uint64_t callCount = 0;
        int lastArgc = 0;
        std::string lastArgTypes;
    };
    std::unordered_map<std::string, HistHit> historical;
    uint64_t historicalTotal = 0;
    for (auto& [name, hit] : host.hitLog().hits()) {
        auto& hh = historical[name];
        hh.callCount = hit.callCount;
        hh.lastArgc = hit.lastArgc;
        hh.lastArgTypes = hit.lastArgTypes;
    }
    historicalTotal = host.hitLog().totalCalls();

    // --- Per-hook aggregate accumulators, parallel to `hooks` (same
    // order, so index i here always corresponds to hooks[i]). ----------
    struct HookAgg {
        uint64_t gpDefined = 0, gpCalledOk = 0, gpCalledErr = 0;
        uint64_t uiDefined = 0, uiCalledOk = 0, uiCalledErr = 0;
    };
    std::vector<HookAgg> hookAgg(hooks.size());
    uint64_t watchdogAbortCount = 0;
    // Real counts of (script, hooks-as-a-whole-state-pass) combinations
    // this task's own state-tag restriction skipped entirely (this task,
    // spec-lua-bindings.md Sec14.5) - i.e. how many scripts had their
    // gameplay-state or ui-state hook-firing pass skipped because their
    // own resolved tag pointed at the OTHER state. Each increment here
    // corresponds to `hooks.size()` real call attempts that did NOT
    // happen this run (previously always fired, HANDOFF.md Sec9.135's own
    // 21,041/32,852 baseline).
    uint64_t skippedGpFireCount = 0, skippedUiFireCount = 0;
    // Real counts of scripts whose own top-level runChunk() pass (this
    // task, step 0) was skipped entirely for one state because their own
    // resolved statetag pointed at the OTHER state - the new, parallel
    // restriction to the hook-firing skip counters just above.
    uint64_t skippedGpRunChunkCount = 0, skippedUiRunChunkCount = 0;
    uint64_t spec164Overrides = 0;

    // Real aggregate over every real hook-call error string this run
    // actually captures (gh.callError/uh.callError below), keyed by the
    // normalized message (stripLuaChunkLocationPrefix() above) - see this
    // tool's own top comment addition and the new
    // verdict_hook_error_messages.tsv output for the full rationale.
    std::unordered_map<std::string, uint64_t> errorMessageCounts;

    std::ofstream hookDetail(outDir / "verdict_hook_fires_detail.tsv");
    hookDetail << "archive\tpath_chain\tentry_name\tstate\thook_name\thook_group\t"
                  "existed_as_function\tattempted_call\tcall_ok\tcall_error\t"
                  "resolved_state_tag\tstate_matches_resolved_tag\n";

    for (uint64_t i = 0; i < toRun; ++i) {
        const auto& s = st.found[i];

        bool sr3luaOk = true;
        std::string sr3luaErr;
        try {
            sr3lua::Parser::analyze(s.content);
        } catch (const sr3lua::LuaSyntaxError& ex) {
            sr3luaOk = false;
            sr3luaErr = ex.what();
        }
        if (sr3luaOk) sr3luaParseOkCount++;

        // --- Real per-script state restriction, computed ONCE per script
        // and reused at BOTH real call sites below (this task, step 0 +
        // Sec9.140's own earlier hook-firing restriction): a script tagged
        // `ui` (kind 21/22 + type 27) only ever runs/fires into
        // host.uiState(); a script tagged `gameplay` (kind 39/40 + type 32)
        // only ever runs/fires into host.gameplayState(). A script tagged
        // OPEN or `conflict` (its own container entry didn't resolve
        // cleanly to either confirmed shape - see statetag::resolve())
        // still runs/fires against BOTH states, unchanged from this
        // harness's original "maximum observation" design
        // (include/sr3luahost/host.h's own top comment) - a deliberate,
        // stated engineering choice for this task (not the only defensible
        // one - "skip OPEN entirely" was the other real option
        // considered): since OPEN genuinely means "no real data either
        // way," running/firing both preserves this scaffold's original
        // safety property (never silently reducing coverage) for exactly
        // the population this task's own real data couldn't resolve,
        // while still applying the real restriction to every script THIS
        // TASK'S OWN real data resolved. s.entryName is the real, on-hand
        // per-script filename (already ends in ".lua" - see
        // endsWithLuaExt() above) - the real value threaded through for
        // HookArgKind::ScriptFilenameString (_PrepareForDynamicGlobals,
        // spec-lua-bindings.md Sec14.1), never a placeholder.
        statetag::Tag tag = scriptTag[i];
        bool fireGp = (tag == statetag::Tag::Gameplay) || (tag == statetag::Tag::Open) ||
                      (tag == statetag::Tag::Conflict);
        bool fireUi = (tag == statetag::Tag::Ui) || (tag == statetag::Tag::Open) ||
                      (tag == statetag::Tag::Conflict);
        // Sec16.4 (CONFIRMED, cleared 2026-10-01; default, --preload-states=tag
        // restores the old behaviour): the preloads run only in the state
        // Sec16.4 names, overriding the per-script tag.
        if (preloadStatesSpec164) {
            std::string lower = toLower(s.entryName);
            static const char* const kUiOnly[] = {"vint_lib.lua", "game_ui_globals.lua", "vdo_base_object.lua",
                                                  "vdo_anim_object.lua", "vdo_input_tracker.lua"};
            bool uiOnly = false;
            for (const char* n : kUiOnly) uiOnly = uiOnly || lower == n;
            if (uiOnly && (fireGp || !fireUi)) { fireGp = false; fireUi = true; ++spec164Overrides; }
            if (lower == "game_lib.lua" && (fireUi || !fireGp)) { fireUi = false; fireGp = true; ++spec164Overrides; }
            // system_lib.lua: both states, which is what an OPEN/conflict tag already gives;
            // forced here so the assignment is exactly Sec16.4's whatever the tag says.
            if (lower == "system_lib.lua" && !(fireGp && fireUi)) { fireGp = fireUi = true; ++spec164Overrides; }
        }
        if (!fireGp) { skippedGpFireCount++; skippedGpRunChunkCount++; }
        if (!fireUi) { skippedUiFireCount++; skippedUiRunChunkCount++; }

        // Snapshot immediately before this script's own top-level runs -
        // the "before" half of the historical (top-level-only) delta.
        std::unordered_map<std::string, uint64_t> beforeTopCounts;
        for (auto& [name, hit] : host.hitLog().hits()) beforeTopCounts[name] = hit.callCount;
        uint64_t beforeTopTotal = host.hitLog().totalCalls();

        // --- Top-level runChunk() pass, RESTRICTED (this task, step 0) to
        // `fireGp`/`fireUi` computed just above - see this loop's own
        // comment block above and this file's own top-of-loop comment for
        // the full method/citation. A skipped state gets a real, empty
        // default-constructed RunResult (loadOk=false, ranPcall=false) -
        // NOT a claim the script fails to load in that state, simply "not
        // attempted this pass," exactly the same honest-skip convention
        // already used for the hook-firing restriction below. At least one
        // of fireGp/fireUi is always true (tag is always exactly one of
        // Ui/Gameplay/Open/Conflict), so `realLoadOk` below can always be
        // read from whichever side actually ran.
        Host::RunResult gpResult = fireGp ? host.runChunk(host.gameplayState(), s.content, s.entryName)
                                           : Host::RunResult{};
        Host::RunResult uiResult = fireUi ? host.runChunk(host.uiState(), s.content, s.entryName)
                                           : Host::RunResult{};

        // Snapshot immediately after (before any hook firing below) - the
        // "after" half. Only this delta is added to `historical`.
        for (auto& [name, hit] : host.hitLog().hits()) {
            uint64_t before = 0;
            auto bIt = beforeTopCounts.find(name);
            if (bIt != beforeTopCounts.end()) before = bIt->second;
            if (hit.callCount > before) {
                auto& hh = historical[name];
                hh.callCount += (hit.callCount - before);
                hh.lastArgc = hit.lastArgc;
                hh.lastArgTypes = hit.lastArgTypes;
            }
        }
        historicalTotal += (host.hitLog().totalCalls() - beforeTopTotal);

        // load result is state-independent (pure syntax check, luaL_
        // loadbuffer does not touch either state's own globals) - read
        // from whichever side actually ran this pass (fireGp preferred,
        // falling back to uiResult - both are byte-identical when both
        // ran, which only OPEN/conflict scripts still do this task).
        bool realLoadOk = fireGp ? gpResult.loadOk : uiResult.loadOk;
        if (realLoadOk) loadOkCount++;
        if (gpResult.pcallOk) pcallOkCount_gp++;
        if (uiResult.pcallOk) pcallOkCount_ui++;

        if (sr3luaOk == realLoadOk) agreeCount++; else disagreeCount++;

        perScript << sanitizeTsv(s.archive) << "\t" << sanitizeTsv(s.pathChain) << "\t"
                  << sanitizeTsv(s.entryName) << "\t" << s.content.size() << "\t"
                  << (sr3luaOk ? 1 : 0) << "\t" << sanitizeTsv(sr3luaErr) << "\t"
                  << (fireGp ? 1 : 0) << "\t"
                  << (gpResult.loadOk ? 1 : 0) << "\t" << sanitizeTsv(gpResult.loadError) << "\t"
                  << (gpResult.pcallOk ? 1 : 0) << "\t" << sanitizeTsv(gpResult.pcallError) << "\t"
                  << (fireUi ? 1 : 0) << "\t"
                  << (uiResult.loadOk ? 1 : 0) << "\t" << sanitizeTsv(uiResult.loadError) << "\t"
                  << (uiResult.pcallOk ? 1 : 0) << "\t" << sanitizeTsv(uiResult.pcallError) << "\t"
                  << statetag::tagLabel(tag) << "\n";

        // --- Hook-firing pass, RESTRICTED to each script's own real state
        // (Sec9.140, unchanged this task): after this script's own
        // top-level attempt (win or lose), attempt every confirmed/
        // pattern-based hook name against ONLY the persistent state(s)
        // `fireGp`/`fireUi` (computed once, above, and now also governing
        // the runChunk() call site just above - this task's own step 0
        // change) say this script really belongs to.

        // A real, empty "not fired this pass" result for a skipped state -
        // NOT a claim the hook doesn't exist there (existedAsFunction is
        // simply not measured when the call site itself is skipped, a
        // real, stated tradeoff of restricting the CALL SITE rather than
        // adding a separate cheap existence-only check to Host - see this
        // block's own comment above). attemptedCall stays false, so this
        // synthesized result can never contribute to hookAgg's ok/err
        // tallies or errorMessageCounts below (both gated on
        // attemptedCall), exactly as intended.
        auto skipped = [&]() {
            std::vector<Host::HookFireResult> out;
            out.reserve(hooks.size());
            for (auto& h : hooks) {
                Host::HookFireResult r;
                r.hookName = h.name;
                r.group = h.group;
                out.push_back(r);
            }
            return out;
        };
        auto gpHooks = fireGp ? host.fireConfirmedHooks(host.gameplayState(), hooks, s.entryName) : skipped();
        auto uiHooks = fireUi ? host.fireConfirmedHooks(host.uiState(), hooks, s.entryName) : skipped();
        for (size_t hi = 0; hi < hooks.size(); ++hi) {
            const auto& gh = gpHooks[hi];
            const auto& uh = uiHooks[hi];
            auto& agg = hookAgg[hi];

            if (gh.existedAsFunction) agg.gpDefined++;
            if (gh.attemptedCall) {
                if (gh.callOk) agg.gpCalledOk++; else agg.gpCalledErr++;
                if (!gh.callOk && gh.callError.find("watchdog") != std::string::npos) watchdogAbortCount++;
                if (!gh.callOk) errorMessageCounts[stripLuaChunkLocationPrefix(gh.callError)]++;
            }
            if (uh.existedAsFunction) agg.uiDefined++;
            if (uh.attemptedCall) {
                if (uh.callOk) agg.uiCalledOk++; else agg.uiCalledErr++;
                if (!uh.callOk && uh.callError.find("watchdog") != std::string::npos) watchdogAbortCount++;
                if (!uh.callOk) errorMessageCounts[stripLuaChunkLocationPrefix(uh.callError)]++;
            }

            hookDetail << sanitizeTsv(s.archive) << "\t" << sanitizeTsv(s.pathChain) << "\t"
                       << sanitizeTsv(s.entryName) << "\t" << "gameplay" << "\t" << gh.hookName << "\t"
                       << hookGroupLabel(gh.group) << "\t" << (gh.existedAsFunction ? 1 : 0) << "\t"
                       << (gh.attemptedCall ? 1 : 0) << "\t" << (gh.callOk ? 1 : 0) << "\t"
                       << sanitizeTsv(gh.callError) << "\t" << statetag::tagLabel(tag) << "\t"
                       << (fireGp ? 1 : 0) << "\n";
            hookDetail << sanitizeTsv(s.archive) << "\t" << sanitizeTsv(s.pathChain) << "\t"
                       << sanitizeTsv(s.entryName) << "\t" << "ui" << "\t" << uh.hookName << "\t"
                       << hookGroupLabel(uh.group) << "\t" << (uh.existedAsFunction ? 1 : 0) << "\t"
                       << (uh.attemptedCall ? 1 : 0) << "\t" << (uh.callOk ? 1 : 0) << "\t"
                       << sanitizeTsv(uh.callError) << "\t" << statetag::tagLabel(tag) << "\t"
                       << (fireUi ? 1 : 0) << "\n";
        }

        if ((i + 1) % 100 == 0 || i + 1 == toRun) {
            std::cout << "  ... " << (i + 1) << "/" << toRun << " tried\n";
        }
    }
    perScript.close();
    hookDetail.close();

    // --- Stub hit aggregate #1 (UNCHANGED file/format/meaning): the
    // HISTORICAL, top-level-only ranking - built from `historical`, NOT
    // from host.hitLog() directly (which now also holds hook-triggered
    // calls) - so this file keeps reproducing HANDOFF.md Sec9.113's own
    // 56-call/5-name baseline exactly, never silently overwritten by this
    // task's own new hook-firing pass. -------------------------------
    std::ofstream hitsOut(outDir / "verdict_stub_hits.tsv");
    hitsOut << "name\tcall_count\tlast_argc\tlast_arg_types\tstatic_total_call_sites\tstatic_distinct_scripts\n";
    std::vector<std::pair<std::string, HistHit>> hitRows(historical.begin(), historical.end());
    std::sort(hitRows.begin(), hitRows.end(), [](const auto& a, const auto& b) {
        if (a.second.callCount != b.second.callCount) return a.second.callCount > b.second.callCount;
        return a.first < b.first;
    });
    for (auto& [name, hit] : hitRows) {
        auto it = staticRank.find(name);
        uint64_t sCalls = (it != staticRank.end()) ? it->second.totalCallSites : 0;
        uint64_t sScripts = (it != staticRank.end()) ? it->second.distinctScripts : 0;
        hitsOut << name << "\t" << hit.callCount << "\t" << hit.lastArgc << "\t" << hit.lastArgTypes
                << "\t" << sCalls << "\t" << sScripts << "\n";
    }
    hitsOut.close();

    // --- Stub hit aggregate #2 (NEW): the FINAL, all-inclusive ranking -
    // top-level AND hook-triggered calls together, straight from
    // host.hitLog() as it stands at the very end of the whole run. This
    // is the "MUCH more representative stub-hit ranking" this task's own
    // brief asked for, reported ALONGSIDE (never replacing) the file
    // above. --------------------------------------------------------
    std::ofstream hitsWithHooksOut(outDir / "verdict_stub_hits_with_hooks.tsv");
    hitsWithHooksOut << "name\tcall_count_with_hooks\thistorical_top_level_only_call_count\t"
                        "incremental_from_hook_firing\tlast_argc\tlast_arg_types\t"
                        "static_total_call_sites\tstatic_distinct_scripts\t"
                        "gameplay_call_count\tui_call_count\n";
    std::vector<std::pair<std::string, sr3luahost::StubHit>> hitRowsWithHooks(host.hitLog().hits().begin(), host.hitLog().hits().end());
    std::sort(hitRowsWithHooks.begin(), hitRowsWithHooks.end(), [](const auto& a, const auto& b) {
        if (a.second.callCount != b.second.callCount) return a.second.callCount > b.second.callCount;
        return a.first < b.first;
    });
    for (auto& [name, hit] : hitRowsWithHooks) {
        auto histIt = historical.find(name);
        uint64_t histCount = (histIt != historical.end()) ? histIt->second.callCount : 0;
        auto it = staticRank.find(name);
        uint64_t sCalls = (it != staticRank.end()) ? it->second.totalCallSites : 0;
        uint64_t sScripts = (it != staticRank.end()) ? it->second.distinctScripts : 0;
        // gameplay_call_count/ui_call_count: REAL per-state call counts
        // (StubHit::gameplayCallCount/uiCallCount, stub_registry.h),
        // measured directly at each real call site - never inferred from
        // this name's own registered cluster tag (a name can genuinely be
        // called from a state its cluster tag doesn't match, e.g.
        // tutorial_advance - see HANDOFF.md Sec9.117/spec-lua-bindings.md
        // Sec14).
        hitsWithHooksOut << name << "\t" << hit.callCount << "\t" << histCount << "\t"
                         << (hit.callCount - histCount) << "\t" << hit.lastArgc << "\t" << hit.lastArgTypes
                         << "\t" << sCalls << "\t" << sScripts << "\t" << hit.gameplayCallCount << "\t"
                         << hit.uiCallCount << "\n";
    }
    hitsWithHooksOut.close();

    // --- Per-hook fire aggregate (NEW): which of the confirmed hooks were ever
    // defined+called, in how many scripts, ok vs error, per state. -----
    std::ofstream hookByHookOut(outDir / "verdict_hook_fires_by_hook.tsv");
    hookByHookOut << "hook_name\thook_group\tgameplay_defined_in_N_scripts\tgameplay_called_ok\t"
                     "gameplay_called_error\tui_defined_in_N_scripts\tui_called_ok\tui_called_error\t"
                     "total_calls_attempted_both_states\thook_evidence\n";
    uint64_t hooksEverDefinedCount = 0, hooksEverCalledOkCount = 0, hooksEverCalledErrCount = 0;
    for (size_t hi = 0; hi < hooks.size(); ++hi) {
        const auto& h = hooks[hi];
        const auto& agg = hookAgg[hi];
        uint64_t totalAttempted = agg.gpCalledOk + agg.gpCalledErr + agg.uiCalledOk + agg.uiCalledErr;
        hookByHookOut << h.name << "\t" << hookGroupLabel(h.group) << "\t" << agg.gpDefined << "\t"
                      << agg.gpCalledOk << "\t" << agg.gpCalledErr << "\t" << agg.uiDefined << "\t"
                      << agg.uiCalledOk << "\t" << agg.uiCalledErr << "\t" << totalAttempted << "\t"
                      << hookEvidenceLabel(h.evidence) << "\n";
        if (agg.gpDefined > 0 || agg.uiDefined > 0) hooksEverDefinedCount++;
        hooksEverCalledOkCount += (agg.gpCalledOk + agg.uiCalledOk);
        hooksEverCalledErrCount += (agg.gpCalledErr + agg.uiCalledErr);
    }
    hookByHookOut.close();

    // --- Top real Lua error messages by frequency (NEW, orchestrator's
    // follow-up item): a real count-and-sort over every real hook-call
    // callError string this run captured, grouped by
    // stripLuaChunkLocationPrefix()'s own normalized text (see that
    // function's own doc comment for exactly what it strips and why) -
    // this file's OWN top comment addition has the full rationale. A real
    // aggregate over REAL captured error strings, not a guess at what
    // errors "should" exist. ------------------------------------------
    std::vector<std::pair<std::string, uint64_t>> errorMessageRows(errorMessageCounts.begin(), errorMessageCounts.end());
    std::sort(errorMessageRows.begin(), errorMessageRows.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    std::ofstream errorMsgOut(outDir / "verdict_hook_error_messages.tsv");
    errorMsgOut << "normalized_message\tcount\n";
    for (auto& [msg, count] : errorMessageRows) {
        errorMsgOut << sanitizeTsv(msg) << "\t" << count << "\n";
    }
    errorMsgOut.close();

    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();

    // --- Population summary, printed AND written -------------------------
    std::ofstream summary(outDir / "verdict_summary.txt");
    auto line = [&](const std::string& s) { summary << s << "\n"; std::cout << s << "\n"; };
    line("\n=== POPULATION SUMMARY (lua_host_run) ===");
    line("archives_scanned=" + std::to_string(archives.size()));
    line("open_state_slots_with_values_at_start=" + openStateSummary);
    line("display_option=" + displayOption);
    line("zscene_table=" + sceneTableSummary);
    line("continuous_explosion_table=" + continuousExplosionTableSummary);
    line("spawn_rank_hit_points=" + spawnRankHitPointsSummary);
    line("weapon_definition_table=" + weaponDefinitionTableSummary);
    line("vehicle_type_class_table=" + vehicleTypeClassTableSummary);
    line("vint_docs=" + std::string(vintDocs ? "on" : "off (--no-vint-docs)") + " found=" +
         std::to_string(st.docs.size()) + " parsed=" + std::to_string(parsedDocs.size()) +
         " parse_failed=" + std::to_string(docsParseFailed) + " scripts_indexed=" + std::to_string(docByScript.size()) +
         " loaded_so_far=" + std::to_string(docsLoaded) + " objects=" + std::to_string(docObjectsLoaded) +
         " properties=" + std::to_string(docPropertiesLoaded) + " context_hits=" + std::to_string(docContextHits) +
         " context_misses=" + std::to_string(docContextMisses) +
         " (real .vint_doc element trees in the Vint object registry; per-call current document = the "
         "document naming the defining chunk as lua_script_file - CHOSEN; see --no-vint-docs)");
    line("auto_answer_hud_prompts=" +
         std::string(autoAnswerHudPrompts ? ("on delay_ticks=" + std::to_string(hudPromptAutoAnswerDelayTicks))
                                           : "off"));
    {
        // Typed property path counters (EngineState::setVintProperty/
        // getVintProperty doc comments), cumulative at this point of the run.
        const auto& vp = host.engineState().vintPropertyStats();
        line("vint_properties=set_bad_handle:" + std::to_string(vp.setBadHandle) +
             " set_typed:" + std::to_string(vp.setTyped) +
             " set_tween_borrow_typed:" + std::to_string(vp.setTweenBorrowTyped) +
             " set_raw_open_code:" + std::to_string(vp.setRawOpenCode) +
             " set_raw_untyped_object:" + std::to_string(vp.setRawUntypedObject) +
             " set_raw_unknown_type:" + std::to_string(vp.setRawUnknownType) +
             " set_raw_enum_unresolved:" + std::to_string(vp.setRawEnumUnresolved) +
             " set_raw_tween_borrow_unresolved:" + std::to_string(vp.setRawTweenBorrowUnresolved) +
             " set_name_miss:" + std::to_string(vp.setNameMiss) +
             " set_read_only_noop:" + std::to_string(vp.setReadOnlyNoop) +
             " set_shape_refusals:" + std::to_string(vp.setShapeRefusals) +
             " set_callback_names_stored:" + std::to_string(vp.setCallbackNamesStored) +
             " screen_size_sets:" + std::to_string(vp.screenSizeSets) +
             " screen_size_div_zero_guards:" + std::to_string(vp.screenSizeDivZeroGuards) +
             " get_bad_handle:" + std::to_string(vp.getBadHandle) + " get_typed:" + std::to_string(vp.getTyped) + " get_raw_echo:" + std::to_string(vp.getRawEcho) +
             " get_computed:" + std::to_string(vp.getComputed) + " get_name_miss:" + std::to_string(vp.getNameMiss) +
             " get_open_refusals:" + std::to_string(vp.getOpenRefusals) +
             " doc_records_typed:" + std::to_string(vp.docRecordsTyped) +
             " doc_records_raw:" + std::to_string(vp.docRecordsRaw) +
             " doc_records_skipped_no_descriptor:" + std::to_string(vp.docRecordsSkippedNoDescriptor) +
             " doc_records_skipped_read_only:" + std::to_string(vp.docRecordsSkippedReadOnly) +
             " doc_objects_seeded:" + std::to_string(vp.docObjectsSeeded) +
             " (typed path = spec-lua-bindings.md Sec9/Sec20 descriptor tables + spec-vint-render.md Sec7/Sec8; "
             "raw = CHOSEN pass-through where clean spec text gives no code)");
    }
    line("total_bytes_read=" + std::to_string(grandBytes));
    line("total_directory_entries=" + std::to_string(st.stats.entries));
    line("real_lua_scripts_found(name ends '.lua')=" + std::to_string(st.found.size()));
    // --- Real preload results, both states (spec-lua-bindings.md Sec16.4) ---
    line("\n=== PRELOAD SUMMARY (spec-lua-bindings.md Sec16.4, real confirmed order) ===");
    for (const PreloadResult* pr : {&pre_system_ui, &pre_vint_lib, &pre_game_ui_globals, &pre_vdo_base,
                                     &pre_vdo_anim, &pre_vdo_input, &pre_cell_foreground, &pre_system_gp}) {
        std::string tag = "preload_" + pr->stateLabel + "_" + pr->name;
        line(tag + "_instances_found=" + std::to_string(pr->instancesFound));
        line(tag + "_load_ok=" + std::string(pr->loadOk ? "true" : "false"));
        line(tag + "_pcall_ok=" + std::string(pr->pcallOk ? "true" : "false"));
        if (!pr->loadOk && pr->instancesFound > 0) line(tag + "_load_error=" + sanitizeTsv(pr->loadError));
        else if (pr->loadOk && !pr->pcallOk) line(tag + "_pcall_error=" + sanitizeTsv(pr->pcallError));
    }
    line("game_lib_lua_instances_found=" + std::to_string(gameLibIndices.size()));
    line("game_lib_lua_first_instance_load_ok=" + std::string(gameLibLoadedOk ? "true" : "false"));
    line("game_lib_lua_first_instance_pcall_ok=" + std::string(gameLibRanOk ? "true" : "false"));
    if (!gameLibLoadedOk) line("game_lib_lua_load_error=" + sanitizeTsv(gameLibLoadError));
    else if (!gameLibRanOk) line("game_lib_lua_pcall_error=" + sanitizeTsv(gameLibRunError));
    line("scripts_attempted_this_run=" + std::to_string(toRun) + " (of " + std::to_string(st.found.size()) + " found)");
    line("real_luaL_loadbuffer_ok=" + std::to_string(loadOkCount) + "/" + std::to_string(toRun) +
         " (load is a pure syntax check, read from whichever state's runChunk() actually ran this script - "
         "see STATE-TAG RESTRICTION SUMMARY below; state-independent, so this denominator is unaffected by "
         "this task's own step-0 runChunk restriction)");
    line("real_lua_pcall_ok_gameplay_state=" + std::to_string(pcallOkCount_gp) + "/" +
         std::to_string(toRun - skippedGpRunChunkCount) + " of those whose gameplay-state runChunk() was "
         "actually attempted this run (this task, step 0: NOT loadOkCount any more - a ui-tagged script's "
         "gameplay-state runChunk() is now skipped by design, not attempted-and-failed; see "
         "scripts_with_gameplay_runChunk_skipped below)");
    line("real_lua_pcall_ok_ui_state=" + std::to_string(pcallOkCount_ui) + "/" +
         std::to_string(toRun - skippedUiRunChunkCount) + " of those whose ui-state runChunk() was actually "
         "attempted this run (same denominator fix as the gameplay line above)");
    line("sr3lua_parser_agrees_with_real_lua_load_result=" + std::to_string(agreeCount) + "/" + std::to_string(toRun));
    line("sr3lua_vs_real_lua_disagreements=" + std::to_string(disagreeCount));
    // NOTE on the two lines below: this project's own pre-existing metric
    // name ("total_stub_calls_recorded(both states, whole run)") is kept,
    // but its value now reflects host.hitLog() as it stands at the very
    // END of this run - i.e. ALL-INCLUSIVE (top-level + hook-triggered) -
    // since hook firing (new this task) shares the SAME HitLog. Relabeled
    // explicitly, rather than left to silently mean something different
    // than it did before this task, per this task's own "don't overwrite
    // the historical one silently" instruction. The TOP-LEVEL-ONLY
    // (historical, pre-this-task) figure is reported right after it,
    // under its own distinctly-named metric, computed from the separate
    // `historical` tracker above (never from host.hitLog() directly).
    line("total_stub_calls_recorded(ALL-INCLUSIVE: top-level + hook-triggered, both states, whole run)=" +
         std::to_string(host.hitLog().totalCalls()));
    line("total_stub_calls_recorded(TOP-LEVEL-ONLY, historical, matches Sec9.113's original baseline, "
         "both states, whole run)=" + std::to_string(historicalTotal));
    line("distinct_stub_names_that_fired_at_least_once(TOP-LEVEL-ONLY, historical, matches Sec9.113's "
         "original baseline)=" + std::to_string(historical.size()) + " (of " + std::to_string(allNames.size() + kHostExtraGlobals) +
         " registered - the +" + std::to_string(kHostExtraGlobals) + " are the 24 bare globals and thread_close, not in the " +
         std::to_string(allNames.size()) + "-name registration list file)");
    line("elapsed_seconds=" + std::to_string(seconds));

    line("\n=== HOOK-FIRING PASS SUMMARY (new this task; spec-lua-bindings.md Sec8.2-8.4/Sec12.2-12.9) ===");
    line("confirmed_hooks_fired_per_script=" + std::to_string(hooks.size()) +
         " (73 Group-1 general [existence-gated, CONFIRMED] + 2 Group-2 lifecycle [UNCONDITIONAL real "
         "call, CONFIRMED] + 58 Group-3 mission-numbered [existence-gated, HIGH CONFIDENCE / pattern-"
         "based, NOT individually dispatcher-confirmed - only 2 of the underlying 60 were, and those 2 "
         "are already counted in Group 1])");
    line("hook_argument_count_used=0 for every call (spec Sec8.3 confirms the call-in MECHANISM only, "
         "not per-hook argument shapes, which remain OPEN - see include/sr3luahost/hook_registry.h). Do "
         "NOT read a hook 'called ok' below as proof the real engine would also see it succeed with its "
         "own real (unconfirmed) arguments.");
    line("hooks_out_of_scope_by_design=Group 4 (7 sprintf-templated per-widget-instance families, spec "
         "Sec8.4) + Group 5 (2 data-driven runtime-populated tables, spec Sec8.5) - both explicitly "
         "unbounded/runtime-only per the spec itself, no static name list exists to fire, NOT counted "
         "anywhere in this run's numbers.");
    line("hook_fire_attempts_theoretical_max_pre_statetag_restriction(scripts x hooks x 2 states, what "
         "this run WOULD have attempted with no per-script state restriction, matching every prior "
         "baseline's own formula)=" + std::to_string(toRun * hooks.size() * 2));
    line("\n=== STATE-TAG RESTRICTION SUMMARY (this task; spec-lua-bindings.md Sec14.5) ===");
    line(std::string("host_rng_option=") +
         (hostRng ? "on seed=" + std::to_string(hostRngSeed) +
                        " (HYPOTHESIS / HOST SUBSTITUTE: deterministic host generator fills the 8192-entry ring once; "
                        "values are not the game's, spec-lua-api-behaviour.md Sec26.27)"
                  : std::string("off (default: CONFIRMED file-image ring - every rand_int/rand_float draw returns lo; "
                                "whether the engine fills the ring before scripts draw is OPEN)")) +
         " draws=" + std::to_string(host.bareGlobals().random().draws()));
    // probe_unresolved_names_option (DIAGNOSTIC ONLY, see usage text above
    // and OpenValueMap::enableProbeFallback's own doc comment, open_state.h):
    // reported here so any run's own verdict_summary.txt makes plain whether
    // its numbers used the real named-object resolution gate or the
    // synthetic probe stand-in - never ambiguous after the fact.
    line(std::string("probe_unresolved_names_option=") +
         (probeUnresolvedNames
              ? "on (DIAGNOSTIC: every miss on the named-object resolution map returns a synthetic "
                "\"resolves\" placeholder instead of refusing - every number below past such a miss is "
                "a synthetic stand-in, not a real measurement)"
              : "off (default: the real, genuinely-OPEN named-object resolution map refuses as normal)"));
    // probe_autopilot_option (DIAGNOSTIC ONLY, see --probe-autopilot's own
    // usage text above): same transparency convention as
    // probe_unresolved_names_option just above - whether this run's
    // still_suspended missions got a synthesized next-blocker probe, and
    // whether the trace ring size was auto-raised to get it (never silent).
    line(std::string("probe_autopilot_option=") +
         (probeAutopilot
              ? "on (DIAGNOSTIC: still_suspended missions get a synthesized callback dispatch, tagged "
                "SYNTHETIC, to find the next blocker past their wait - see probe_autopilot_* lines below "
                "and verdict_mission_drive.tsv's probe_autopilot_* columns)" +
                    std::string(probeAutopilotAutoRaisedRingSize
                                    ? " trace_ring_size auto-raised to " +
                                          std::to_string(kProbeAutopilotAutoRingSize) +
                                          " (--trace-host-calls-before-error was not explicitly given)"
                                    : " trace_ring_size=" + std::to_string(g_traceRingSize) +
                                          " (explicitly given via --trace-host-calls-before-error)")
              : "off (default: no synthesized events, no change to any mission's drive)"));
    {
        size_t resolved = 0, ran = 0;
        for (const auto& il : host.bareGlobals().includeQueue().log()) {
            resolved += il.resolved ? 1 : 0;
            ran += il.runOk ? 1 : 0;
        }
        line("include_loads=" + std::to_string(host.bareGlobals().includeQueue().log().size()) +
             " resolved=" + std::to_string(resolved) + " ran_ok=" + std::to_string(ran) +
             " (deferred include queue, spec-lua-bindings.md Sec16.4)");
        line("script_thread_errors=" + std::to_string(host.bareGlobals().threads().errors().size()) +
             " live_script_threads_at_end=" + std::to_string(host.bareGlobals().threads().liveCount()));
    }
    line(std::string("preload_states_option=") +
         (preloadStatesSpec164 ? "spec16.4 (default; Sec16.1/Sec16.4 CONFIRMED, cleared 2026-10-01)"
                               : "tag (opt-in old behaviour: preload files follow the per-script state tag)") +
         " scripts_rerouted=" + std::to_string(spec164Overrides));
    line("real_population_split(of " + std::to_string(st.found.size()) + " found scripts)=ui:" +
         std::to_string(tagUi) + " gameplay:" + std::to_string(tagGameplay) + " OPEN:" + std::to_string(tagOpen) +
         " conflict:" + std::to_string(tagConflict));
    line("engineering_choice_for_OPEN/conflict_scripts=RUN/FIRE AGAINST BOTH STATES (unchanged, original "
         "\"maximum observation\" behavior, now applying to BOTH the top-level runChunk() call site [this "
         "task, step 0] AND the hook-firing call site [Sec9.140]) - stated choice, not the only defensible "
         "one; see this file's own source comment near the top of the main loop for the full reasoning.");
    line("\n--- Step 0 (this task): top-level runChunk() restriction, the NEW call site this task closes ---");
    line("scripts_with_gameplay_runChunk_skipped(tagged ui - own top-level code no longer runs in the "
         "gameplay state at all, closing Sec9.140's own traced definition-leak cause)=" +
         std::to_string(skippedGpRunChunkCount));
    line("scripts_with_ui_runChunk_skipped(tagged gameplay - own top-level code no longer runs in the ui "
         "state at all)=" + std::to_string(skippedUiRunChunkCount));
    line("\n--- Hook-firing call site (Sec9.140, unchanged this task) ---");
    line("scripts_with_gameplay_hook_firing_skipped(tagged ui, hooks.size()=" + std::to_string(hooks.size()) +
         " real calls not attempted per skipped script)=" + std::to_string(skippedGpFireCount));
    line("scripts_with_ui_hook_firing_skipped(tagged gameplay, hooks.size()=" + std::to_string(hooks.size()) +
         " real calls not attempted per skipped script)=" + std::to_string(skippedUiFireCount));
    line("real_hook_fire_attempts_not_made_this_run(sum of the two skip counts above x " + std::to_string(hooks.size()) +
         " hooks each - the real gap between the theoretical-max line above and the real ok+erred total "
         "below)=" + std::to_string((skippedGpFireCount + skippedUiFireCount) * hooks.size()));
    line("KNOWN, STATED COVERAGE TRADEOFF: existedAsFunction is NOT measured for a skipped state (this "
         "task restricts fireConfirmedHooks()'s own CALL SITE, not just whether it acts on the result - "
         "adding a separate cheap existence-only check to Host was out of this task's own stated scope). "
         "gameplay_defined_in_N_scripts/ui_defined_in_N_scripts in verdict_hook_fires_by_hook.tsv can "
         "therefore read LOWER than a prior baseline for a hook whose OTHER-state definition used to be "
         "observed incidentally on a wrong-state script - a real, honest measurement-coverage change, "
         "separate from the ok/err tally change below.");
    line("hooks_defined_in_at_least_one_script(of " + std::to_string(hooks.size()) + " confirmed/pattern-based)=" +
         std::to_string(hooksEverDefinedCount));
    line("hook_calls_attempted_and_ok(both states, whole run)=" + std::to_string(hooksEverCalledOkCount));
    line("hook_calls_attempted_and_erred(both states, whole run)=" + std::to_string(hooksEverCalledErrCount));
    line("hook_calls_aborted_by_instruction_watchdog(subset of the erred count above; real, found risk - "
         "see host.h's own fireConfirmedHooks doc comment)=" + std::to_string(watchdogAbortCount));
    line("NOTE: hook firing runs against the SAME persistent gameplay_/ui_ states every script's own "
         "top-level run already shares - a hook name multiple scripts define will, by the time script N's "
         "own firing runs, invoke whichever script's definition is currently live in that shared global "
         "table, not necessarily script N's own. Pre-existing property of this scaffold's persistent-"
         "state design (Sec9.113), not new to this pass - stated here rather than silently papered over.");
    line("hook_names_also_present_in_the_registered_stub_list=" + std::to_string(hookNamesAlsoRegisteredAsStubs.size()) +
         " (of " + std::to_string(hooks.size()) + " hooks, against the " + std::to_string(allNames.size()) +
         "-name registered stub list) - a real, checked overlap, not assumed 0: for any name in this list, its own "
         "existedAsFunction/callOk numbers above are a genuine mixture of \"hit the always-succeeding "
         "generic logging stub already sitting in that global from Host construction\" and \"hit a real, "
         "possibly-erroring script-defined override that later replaced it\" - see this tool's own source "
         "comment (tools/lua_host_run.cpp) for the full reasoning.");
    if (!hookNamesAlsoRegisteredAsStubs.empty()) {
        std::string joined;
        for (size_t i = 0; i < hookNamesAlsoRegisteredAsStubs.size(); ++i) {
            if (i) joined += ", ";
            joined += hookNamesAlsoRegisteredAsStubs[i];
        }
        line("  overlapping name(s): " + joined);
    }

    line("\n=== NEW STUB-HIT RANKING INCLUDING HOOK-TRIGGERED CALLS (replaces the old ranking as the "
         "'real' one - the OLD, top-level-only ranking is kept, unmodified, in verdict_stub_hits.tsv; "
         "its own total/distinct-name figures are printed above, under their own TOP-LEVEL-ONLY-labeled "
         "lines, not repeated here) ===");
    line("distinct_stub_names_that_fired_at_least_once(ALL-INCLUSIVE)=" + std::to_string(host.hitLog().hits().size()) +
         " (of " + std::to_string(allNames.size() + kHostExtraGlobals) + " registered, +" + std::to_string(kHostExtraGlobals) +
         " being the 24 bare globals and thread_close)");

    line("\n=== TOP 25 STUB NAMES BY REAL RUNTIME HIT COUNT - TOP-LEVEL ONLY (historical, matches "
         "Sec9.113's original baseline exactly) ===");
    for (size_t i = 0; i < hitRows.size() && i < 25; ++i) {
        auto& [name, hit] = hitRows[i];
        auto it = staticRank.find(name);
        std::ostringstream row;
        row << "  " << (i + 1) << ". " << name << " runtime_calls=" << hit.callCount;
        if (it != staticRank.end()) {
            row << "  (static census: " << it->second.totalCallSites << " call sites / "
                << it->second.distinctScripts << " scripts)";
        } else {
            row << "  (not present in the static reconciliation file)";
        }
        line(row.str());
    }

    line("\n=== TOP 25 STUB NAMES BY REAL RUNTIME HIT COUNT - INCLUDING HOOK-TRIGGERED CALLS (NEW, this "
         "task's own 'MUCH more representative' ranking) ===");
    for (size_t i = 0; i < hitRowsWithHooks.size() && i < 25; ++i) {
        auto& [name, hit] = hitRowsWithHooks[i];
        auto histIt = historical.find(name);
        uint64_t histCount = (histIt != historical.end()) ? histIt->second.callCount : 0;
        std::ostringstream row;
        row << "  " << (i + 1) << ". " << name << " runtime_calls=" << hit.callCount
            << " (top-level-only was " << histCount << ", +" << (hit.callCount - histCount) << " from hook firing)";
        line(row.str());
    }

    line("\n=== TOP 15 CONFIRMED HOOKS BY TOTAL REAL CALLS ATTEMPTED (both states) ===");
    {
        std::vector<size_t> order(hooks.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            uint64_t ta = hookAgg[a].gpCalledOk + hookAgg[a].gpCalledErr + hookAgg[a].uiCalledOk + hookAgg[a].uiCalledErr;
            uint64_t tb = hookAgg[b].gpCalledOk + hookAgg[b].gpCalledErr + hookAgg[b].uiCalledOk + hookAgg[b].uiCalledErr;
            if (ta != tb) return ta > tb;
            return hooks[a].name < hooks[b].name;
        });
        for (size_t rank = 0; rank < order.size() && rank < 15; ++rank) {
            size_t hi = order[rank];
            const auto& agg = hookAgg[hi];
            uint64_t total = agg.gpCalledOk + agg.gpCalledErr + agg.uiCalledOk + agg.uiCalledErr;
            std::ostringstream row;
            row << "  " << (rank + 1) << ". " << hooks[hi].name << " [" << hookGroupLabel(hooks[hi].group)
                << "] total_calls_attempted=" << total << " (gameplay: defined_in=" << agg.gpDefined
                << " ok=" << agg.gpCalledOk << " err=" << agg.gpCalledErr << "; ui: defined_in="
                << agg.uiDefined << " ok=" << agg.uiCalledOk << " err=" << agg.uiCalledErr << ")";
            line(row.str());
        }
    }

    line("\n=== TOP 25 REAL LUA ERROR MESSAGES BY FREQUENCY (NEW - real count-and-sort over every real "
         "hook-call callError string this run captured, grouped by normalized text; see "
         "verdict_hook_error_messages.tsv for the full list and this file's own top comment for exactly "
         "what normalization strips) ===");
    line("distinct_normalized_error_messages=" + std::to_string(errorMessageRows.size()) +
         " (total error occurrences=" + std::to_string(hooksEverCalledErrCount) + ", matches "
         "hook_calls_attempted_and_erred above)");
    for (size_t i = 0; i < errorMessageRows.size() && i < 25; ++i) {
        auto& [msg, count] = errorMessageRows[i];
        std::ostringstream row;
        row << "  " << (i + 1) << ". count=" << count << "  " << msg;
        line(row.str());
    }
    summary.close();

    std::cout << "\nWrote " << (outDir / "verdict_per_script.tsv").string() << ", "
              << (outDir / "verdict_stub_hits.tsv").string() << " (historical, unchanged), "
              << (outDir / "verdict_stub_hits_with_hooks.tsv").string() << " (new), "
              << (outDir / "verdict_hook_fires_by_hook.tsv").string() << " (new), "
              << (outDir / "verdict_hook_fires_detail.tsv").string() << " (new), "
              << (outDir / "verdict_hook_error_messages.tsv").string() << " (new), "
              << (outDir / "verdict_summary.txt").string() << "\n";

    // ======================================================================
    // STEP 1 (this task, run only after step 0 above has landed and been
    // measured): drive real missions. Real stem population:
    // tools/mission_package_per_container.tsv (HANDOFF.md Sec9.125),
    // deduped by its own mission_lua_stem column, keeping only rows where
    // stem_covers_a_start_script==1 (a real, confirmed `<stem>_start` entry
    // point exists - spec-lua-bindings.md Sec14.10/HANDOFF Sec9.123,
    // verified e.g. `m01_start(m01_checkpoint, is_restart)` calls
    // `m01_run(...)` internally). HANDOFF cites "49/54 stems" - this pass
    // reports the actual count its own dedup+filter finds, never assumed.
    //
    // For each real mission stem: load that mission's own real, found
    // `.lua` script into the gameplay state (already carrying the real
    // preload chain's own effects from earlier in this same run -
    // system_lib.lua then game_lib.lua, per preloadNamed() above - plus
    // everything the full 804-script population pass above already ran),
    // via the SAME runChunk()/load pattern this file already uses
    // elsewhere, watchdog-wrapped (this task's own NEW call site, never
    // watchdog-protected before - see runChunkWithWatchdog() above's own
    // doc comment for why that's safe here). Then call
    // `<stem>_start("mission start", false)` (CONFIRMED call shape as of
    // 2026-10-02 - spec-lua-api-behaviour.md Sec36.4, spec-lua-bindings.md
    // Sec14.11; see callMissionStart()'s own doc comment for the full
    // citation and for why this harness's remaining tick budget/watchdog
    // numbers below stay CHOSEN), watchdog-wrapped, as a
    // script-thread record through the runner (request 11, Sec26.27: the
    // record is current for the call and the call may yield; a yielded
    // record stays in the thread table and every tick's scheduler passes -
    // 0x00e0cf50, see kSchedulerPassesPerTick below - resume it). Then tick
    // the confirmed-hook-firing pass (the real, already-built mechanism
    // that exercises sr3luahost::ThreadScheduler - thread_new/thread_yield
    // etc. are registered into both states and reachable from any hook
    // body a mission script now defines; see thread_scheduler.h's own
    // documented SCOPE note that thread_check_done() resolves immediately,
    // since nothing in this project ever lua_resume()s a created thread -
    // so a single pcall already runs each hook body to completion; ticking
    // repeats the WHOLE confirmed-hook-firing pass, each individual hook
    // call already watchdog-protected internally by fireConfirmedHooks
    // itself) for a CHOSEN fixed budget of kMissionTickBudget=20 ticks per
    // mission - a round number, not a recovered real per-mission frame
    // count (no such fact exists anywhere in this project's own data):
    // large enough to let a mission's own newly-defined hooks be dispatched
    // several times each (real stateful hook bodies - e.g. a "first_time"
    // init flag - could genuinely behave differently tick-to-tick, so this
    // is a real, not rhetorical, budget) while keeping total added work
    // small (49 missions x 20 ticks x up to hooks.size() hooks is a small
    // fraction of the main pass's own already-measured hook-attempt
    // volume). Stops a given mission's own ticking early, before the full
    // budget, on either of 2 real, CHOSEN (not recovered) conditions,
    // checked and reported per mission below: (a) "error" - a NEW watchdog
    // abort appears on some hook this tick (a genuine runaway-loop signal
    // already individually contained by fireConfirmedHooks' own per-hook
    // watchdog - stopping early here just avoids burning the rest of the
    // budget on a call already known to be hitting the same infinite
    // loop); (b) "completion" - 2 consecutive ticks produce an IDENTICAL
    // (existed/ok/err) signature across the whole hook set, a real,
    // measured (not assumed) idempotence signal - suppressed while a fade is
    // in flight or while the tick's scheduler passes resumed any of this
    // mission's own script threads (both are progress the signature can't
    // see). Otherwise:
    // "budget exhaustion" (all 20 ticks ran).
    //
    // A real, stated caveat, same shape as this file's own top-comment
    // caveat about the main 804-script pass: host.gameplayState() is ONE
    // PERSISTENT state shared across every mission this loop drives, in
    // walk order (not a fresh state per mission) - a later mission's own
    // tick-firing pass can therefore still see an EARLIER mission's own
    // Group-3 mission-numbered hook definitions (e.g. `m02_*` names)
    // still live in the shared global table while driving a later mission
    // (e.g. `m05`), and a later mission's own top-level runChunk()/`_start`
    // call can overwrite a name an earlier mission also defined. Reported
    // here rather than silently papered over; per-mission rows below are
    // each still a real, honest measurement of "what happened when this
    // mission was driven at its own real position in the walk," exactly
    // analogous to how the main pass's own per-script rows already work.
    std::cout << "\n\n=== STEP 1 (this task): real mission-driving pass ===\n";
    fs::path missionTsvPath = toolsDir / "mission_package_per_container.tsv";
    std::vector<MissionPackageRow> missionRows = loadMissionPackageRows(missionTsvPath.string());
    std::cout << "Loaded " << missionRows.size() << " real rows from " << missionTsvPath.string()
              << " (0 means that file wasn't found from this working directory - step 1 is skipped "
              << "entirely below, not fabricated, if so).\n";

    std::set<std::string> seenStems;
    std::vector<MissionPackageRow> missions; // deduped, in first-seen TSV order
    for (auto& row : missionRows) {
        if (!row.coversStartScript) continue;
        if (seenStems.count(row.missionLuaStem)) continue;
        seenStems.insert(row.missionLuaStem);
        missions.push_back(row);
    }
    std::cout << "Real, deduped-by-mission_lua_stem population with stem_covers_a_start_script==1: "
              << missions.size() << " distinct stems (HANDOFF.md's own citation is \"49/54\" - this is "
              << "this run's own actual, independently-derived count, reported as-is whether or not it "
              << "matches).\n";

    const int kMissionTickBudget = missionTickBudget; // CHOSEN (default 20, --tick-budget= override) - see this block's own top comment for the reasoning
    // CONFIRMED, not chosen (spec-lua-api-behaviour.md Sec36.4, spec-lua-bindings.md
    // Sec14.11, synced 2026-10-02): a genuine fresh start's two native call sites
    // always push the literal string "mission start" and the boolean false - see
    // callMissionStart()'s own doc comment for the full citation and for why this
    // harness (fresh-start-only, no resume/retry call site) never needs any other
    // checkpoint-name value. Previously this harness passed the CHOSEN number 0.0
    // here, which reproduced the real dlc2_m02 "attempt to index local 'cp_data'
    // (a nil value)" bug: `checkpoints[0]` is nil when a mission only ever keys
    // its checkpoint table by name strings.
    const std::string kMissionStartCheckpoint = "mission start";
    constexpr bool kMissionStartIsRestart = false;  // CONFIRMED false on a fresh start (Sec36.3)
    // Screen fade host clock (batch 2026-10-01, spec-lua-api-behaviour.md
    // Sec26.24): each mission tick is one host frame of this many ms
    // (EngineState::screenFadeHostFrame). CHOSEN: about a third of a second,
    // and not a divisor of the engine's 1000/1500/3000/6000 ms stamp offsets,
    // so a stamp never lands exactly on a frame (that comparison is OPEN).
    constexpr int64_t kFadeHostMsPerTick = 333;
    // Script-thread scheduler cadence (spec-lua-api-behaviour.md thread-table
    // section, "MAJOR CORRECTION (2026-10-02, stuck-delay-mission
    // investigation)", CONFIRMED - disassembly, re-derived a second time -
    // supersedes the "[RESOLVED 2026-10-02 ...]" pass immediately before it,
    // which this one corrects rather than contradicts): 0x00702070 is NOT a
    // steady background pump - it's the boot-only "Boot_render" loading-
    // screen thread, which terminates before the first gameplay frame and
    // never runs again. The real steady-state driver is 0x00e0dfc0, on the
    // MAIN thread, called once per rendered/game frame from each active game
    // state's own per-frame update (0x007b05f0 for the in-game state, the
    // front-end state, and the cutscene driver 0x00702a50), which itself
    // unconditionally calls the scheduler 0x00e0cf50 once per frame (no
    // budget - every live, non-exempt record is offered a resume on that one
    // call) - except while 0x00dafa90 reports a blocking stream load in
    // progress (not modelled here; this host never simulates a blocking
    // load). There never was an independent ~30 ms pump to approximate
    // outside the boot screen; the spec states plainly that a host which
    // "resumes 0x00e0cf50 once per rendered frame ... reproduces the real
    // engine exactly."
    //
    // CHOSEN (engineering decision, not spec fact - supersedes the old
    // floor(333 ms / 30 ms) = 11-passes-per-tick mapping this comment
    // previously derived from the now-corrected "independent ~30 ms pump"
    // reading): this host already treats one mission tick as one host frame
    // (kFadeHostMsPerTick above - "each mission tick is one host frame of
    // this many ms"), and the real mechanism is now confirmed to run exactly
    // once per rendered/game frame, so the faithful mapping is 1:1 - one
    // scheduler pass per tick. The old 11x figure was sized to approximate a
    // ~30 ms cadence that the corrected spec text confirms never existed
    // outside the boot screen; running it that many times no longer has any
    // engine cadence behind it, so it is simplified away rather than kept as
    // a "safe superset" (nothing here is being approximated anymore - this
    // IS the mechanism, 1 pass per frame, not a bound on it).
    constexpr int kSchedulerPassesPerTick = 1; // CHOSEN: 1 host tick == 1 rendered/game frame == 1 scheduler pass
    // get_frame_time (spec-lua-api-behaviour.md Sec37.2/Sec37.5, CONFIRMED
    // writer: a raw, unscaled, uncapped, unpaused measured wall-clock delta,
    // refreshed every main-loop frame): since this host now resumes each
    // thread's single scheduler pass exactly once per tick (above), a call
    // to get_frame_time() during that resume must report that same tick's
    // own modelled elapsed time to keep the script's own wall-clock reading
    // in step with the host's other per-tick clocks (the fade/cutscene host
    // frame above already use kFadeHostMsPerTick). CHOSEN: reuse that same
    // constant here rather than leave get_frame_time() at the file image's
    // unrelated 1/30 s startup default, which has no relation to this host's
    // own tick length and was the root cause of a stuck-delay mission (e.g.
    // m02) never accumulating enough modelled time to finish - Sec37.5's own
    // "For a host" paragraph names exactly this mismatch shape. Set once,
    // before the mission loop: the value never varies tick-to-tick here.
    host.bareGlobals().setFrameTime(static_cast<float>(kFadeHostMsPerTick) / 1000.0f);

    struct MissionResult {
        std::string stem, entryName, containerName;
        bool scriptFound = false;
        bool loadOk = false, pcallOk = false;
        std::string loadError, pcallError;
        bool startExisted = false, startAttempted = false, startCallOk = false;
        bool startSuspended = false; // _start's record yielded (no error) and is still alive
        std::string startSuspendedAt; // see MissionStartResult::suspendedAt
        std::string startCallError;
        // What became of a suspended _start record over the tick loop's
        // scheduler passes: "" (not suspended), still_suspended, finished
        // (resumed and ran to its end), killed (released by the runner's step
        // 2), errored (its own Lua error, in startAfterResumeError).
        std::string startAfterTicks;
        std::string startAfterResumeError;
        std::string startStillSuspendedAt; // still_suspended: where it is now waiting
        uint64_t schedulerResumes = 0;      // records this mission's passes resumed
        uint64_t schedulerThreadErrors = 0; // this mission's records that raised on a resume
        uint64_t schedulerForeignResumes = 0; // resumes of records left over from earlier missions
        int ticksSurvived = 0;
        std::string stopReason;
        std::string firstErrorMessage;
        std::string firstUnimplementedStub;
        // --probe-autopilot (2026-10-07) fields - all left at their default
        // (empty/false) unless --probe-autopilot was given AND this mission
        // was classified still_suspended above; see that flag's own usage
        // text and the classification-site block below for how they're
        // filled. Purely additive reporting - never read by, or fed back
        // into, any of the REAL fields above.
        bool probeAutopilotAttempted = false;
        bool probeAutopilotHadCallback = false;
        std::string probeAutopilotNative;         // e.g. "on_trigger"; empty if nothing to synthesize
        std::string probeAutopilotCallbackName;   // the recovered callback-name argument
        std::string probeAutopilotTargetName;     // the recovered trigger/character/vehicle name (may be empty)
        std::string probeAutopilotDispatchResult; // outcome of calling the callback-name global once
        std::string probeAutopilotNextBlocker;    // first new error/wait/completion found in the follow-up window
        std::string probeAutopilotNote;           // set to the "nothing to synthesize" finding when applicable
        // THE headline measurement this task (2026-10-08, Sec21.27a/
        // Sec21.27b) adds: did THIS mission's own mission handle actually
        // reach the real completion state - EngineState::
        // missionCompletionFlagBitSet(), the real `+0x88` bit 0x4
        // (Sec15.10/Sec21.27b's own writer, 0x006d6870) - by the time this
        // mission's own tick loop stopped (budget exhaustion or the
        // idempotent-signature "completion" stop below)? Deliberately NOT
        // named `completion` or folded into stopReason above: stopReason's
        // own "completion" value only means "2 consecutive identical tick
        // signatures," a Lua-coroutine-idle measurement that pre-dates this
        // task and is NOT the same thing as the mission's own completion
        // flag - conflating the two is exactly the "no error" vs. "really
        // FINISHED" gap this task was asked to measure honestly.
        bool missionCompletionFlagSet = false;
    };
    std::vector<MissionResult> missionResults;
    missionResults.reserve(missions.size());
    uint64_t schedulerForeignErrorsTotal = 0; // leftover records' own errors on a resume (not in any mission's row)
    // --auto-answer-hud-prompts (THIS TASK): persists across the WHOLE
    // mission loop below (same reason schedulerForeignErrorsTotal does,
    // immediately above) - EngineState::hudPromptRequests() is a single
    // host-wide vector that only ever grows (same convention as
    // tools/sr3_viewer.cpp's own identical tracker), never reset per
    // mission, so these must not be either (resetting per mission would
    // re-dispatch an already-answered earlier mission's own old requests).
    // globalTick increments once per tick of every mission in this run (NOT
    // the per-mission `tick` loop variable, which restarts at 1 each
    // mission) so "N ticks since first seen" stays correct even if a prompt
    // happens to still be unanswered at a mission boundary (should not
    // happen in practice within a sane delay, but is harmless either way).
    size_t hudPromptRespondedCount = 0;
    std::vector<long long> hudPromptFirstSeenGlobalTick;
    long long globalTick = 0;

    // Snapshot host.hitLog() right before step 1 begins - the "before"
    // half of the NEW stub-hit-with-missions ranking's own incremental
    // column below, same snapshot/delta convention as `historical` above.
    std::unordered_map<std::string, uint64_t> preMissionCounts;
    for (auto& [name, hit] : host.hitLog().hits()) preMissionCounts[name] = hit.callCount;

    for (auto& row : missions) {
        MissionResult mr;
        mr.stem = row.missionLuaStem;
        mr.entryName = row.missionLuaEntryName;
        mr.containerName = row.containerName;

        size_t chosen = SIZE_MAX;
        std::string entryLower = toLower(row.missionLuaEntryName);
        for (size_t i = 0; i < st.found.size(); ++i) {
            if (toLower(st.found[i].entryName) == entryLower) { chosen = i; break; }
        }
        if (chosen == SIZE_MAX) {
            mr.scriptFound = false;
            mr.stopReason = "error: mission script entry not found in this run's own archive walk";
            missionResults.push_back(std::move(mr));
            continue;
        }
        mr.scriptFound = true;
        const auto& script = st.found[chosen];

        std::vector<std::string> errorsInOrder;

        Host::RunResult loadResult = runChunkWithWatchdog(host, host.gameplayState(), script.content, script.entryName);
        mr.loadOk = loadResult.loadOk;
        mr.loadError = loadResult.loadError;
        mr.pcallOk = loadResult.pcallOk;
        mr.pcallError = loadResult.pcallError;
        if (!loadResult.loadOk) errorsInOrder.push_back(loadResult.loadError);
        else if (loadResult.ranPcall && !loadResult.pcallOk) errorsInOrder.push_back(loadResult.pcallError);

        // DOCUMENTED ENGINEERING CHOICE, not a confirmed spec mechanism: the real
        // bound function name is a data-borne string property
        // (`script_mission_start`) read off the mission's own `scripted_mission_start`
        // placement/definition record (spec-lua-api-behaviour.md Sec36.1,
        // spec-lua-bindings.md Sec14.11) - this project does not have that record's
        // data (it lives in the still-parked `.czn_pc` interior, or another unread
        // carrier) to look up the true name. Deriving it as `<stem>_start` from this
        // row's own `mission_lua_stem` column is HYPOTHESIS per Sec36.4/Sec14.11 (a
        // partner team's own census found shipped missions follow this pattern, but
        // the object data itself was never read to confirm it) - kept here as a
        // reasonable stand-in, per Sec36.4's own framing, but never presented as
        // itself a confirmed spec fact.
        std::string funcName = row.missionLuaStem + "_start";
        // Records still live from earlier missions (the gameplay state is
        // shared across the walk - see this block's top caveat). The
        // scheduler resumes them too, as the engine's would; their resumes
        // and errors are counted apart from this mission's own. Measurement
        // only: nothing about the passes depends on this set. A record a
        // leftover thread creates during this mission counts as this
        // mission's (not distinguishable here).
        std::set<uint16_t> foreignIds;
        for (uint16_t id : host.bareGlobals().threads().liveIds()) foreignIds.insert(id);
        // FIX 2026-10-08 (root cause of probe_blockers_ranked_20261008_
        // post_resolver_merge.md item 2, 12/49 missions): spec-lua-api-
        // behaviour.md Sec21.27a confirms phase 8 (the real engine's
        // literal start-up-adjacent "mid-bind" value) auto-resolves to
        // phase 1 well before any script code is reachable - so the
        // instant this driver is about to invoke a mission's own `_start`,
        // the real "mission active, phase != 8" gates are CONFIRMED true,
        // not an unmodelled fact. See EngineState::beginMissionPhaseTracking
        // ()'s own doc comment (include/sr3luahost/engine_state.h) for the
        // full root-cause history and why this is the one correct hook
        // point ("the instant a mission's own `_start` is invoked").
        //
        // UPGRADED this task (2026-10-08, Sec21.27a/Sec21.27b): now also
        // drives the REAL integer phase machine (bindMissionPhaseMachine())
        // through its real 8->(2)->1->(3 or 5) sequence for THIS mission's
        // own name, instead of only flipping the three legacy boolean
        // facades. `type` is the CHOSEN default 0 (no real +0xa0 population
        // table for any of these 49 missions, see that method's own doc
        // comment); `isRestart` is the SAME kMissionStartIsRestart=false
        // this loop already passes to callMissionStart() just below -
        // every mission this harness drives is a fresh start.
        // MISSION-HOST PHASE 3, part (b) (this task, 2026-10-09): m05's own
        // real script-named vehicles, registered as live objects BEFORE this
        // mission's own `_start` runs - closing the real-world-state gap
        // behind the vehicle enter-submitter blocker (`0x00af22a0`, spec-
        // player-runtime.md Sec7.4/Sec38.1) for this one mission. Names are
        // real, read verbatim off the real shipped m05.lua (tools/validation/
        // lua_source_dump.cpp against the real cache, this task's own
        // extraction) - its `M05_groups` table's own `.heli` fields:
        // `m05_heli_host`, `m05_heli_client`, `m05_heli_pierce`,
        // `m05_heli_chase_host` (lines 15/20/25/135 of the real script). NOT
        // fabricated - every one of these 4 is a name m05's own real Lua
        // text passes into a vehicle-enter native (`on_vehicle_enter`,
        // `vehicle_enter_teleport` - a game_lib.lua wrapper over
        // `vehicle_enter_do` - see this task's own commit for the full call-
        // site citation).
        //
        // Two separate writes, because this project's vehicle-enter chain
        // (resolveEnterVehicle(), src/lua_spec_confirmed_stubs_b15_wave2_
        // core_dispatchers.cpp) reads the PRE-EXISTING, hand-seeded
        // objectResolves() map, NOT this task's own Phase 2 liveObjectKind()/
        // isLiveObject() resolver - that resolver has no production
        // consumer anywhere in this codebase yet (verified: grepped every
        // src/ caller). Per spec-zone-data-format.md §11.10 ("resolving a
        // name and finding it live are the SAME event" in the real engine),
        // being live SHOULD be sufficient to resolve - but reproducing that
        // unification is a bigger mechanism change than this task's own
        // scope (closing one mission's blocker), so both facts are written
        // explicitly here instead: objectResolves().set(name, true) (the
        // mechanism vehicle_enter_do's own resolver actually reads today)
        // AND bridgeLiveVehiclePosition() (Phase 2's own mechanism, so a
        // later native that DOES start consuming liveObjectKind() also sees
        // these 4 names as live, and so this is honestly reusing, not
        // duplicating, that Phase 2 mechanism per this task's own ask).
        //
        // Positions are CHOSEN placeholders, NOT spec-confirmed real zone-
        // placement coordinates: this project's own existing `.czn_pc`
        // readers (tools/zone_object_resolver_probe.cpp, hardcoded to look
        // up only "Killbane"; tools/zone_objects_population.cpp, capped at a
        // 12-name population sample) do not support searching for an
        // arbitrary real placed-object name, so the real penthouse-rooftop
        // coordinates these 4 helicopters are actually placed at could not
        // be read without a code change this task did not make (a genuine,
        // reported tooling-coverage gap, not a data-access blocker - see
        // this task's own report). A small fixed offset per vehicle keeps
        // the 4 names distinguishable rather than literally co-located.
        // seat0Occupied=false (CHOSEN): an empty driver seat is the one
        // honestly-reachable state for a vehicle nothing in this minimal
        // host has ever "seated" anything into (same convention this
        // project already uses for every other freshly-created vehicle).
        if (row.missionLuaStem == "m05") {
            static const struct { const char* name; double x, y, z; } kM05RealVehicles[] = {
                {"m05_heli_host", 0.0, 0.0, 0.0},
                {"m05_heli_client", 10.0, 0.0, 0.0},
                {"m05_heli_pierce", 20.0, 0.0, 0.0},
                {"m05_heli_chase_host", 30.0, 0.0, 0.0},
            };
            for (const auto& v : kM05RealVehicles) {
                host.engineState().objectResolves().set(v.name, true);
                host.engineState().bridgeLiveVehiclePosition(v.name, sr3luahost::WorldPosition{v.x, v.y, v.z});
                host.engineState().getOrCreateVehicle(v.name).seat0Occupied.set(false);
            }
        }
        host.engineState().beginMissionPhaseTracking(row.missionLuaStem, /*type=*/0, kMissionStartIsRestart);
        MissionStartResult startResult = callMissionStart(host.gameplayState(), host.bareGlobals().threads(), funcName,
                                                            kMissionStartCheckpoint, kMissionStartIsRestart);
        mr.startExisted = startResult.existedAsFunction;
        mr.startAttempted = startResult.attemptedCall;
        mr.startCallOk = startResult.callOk;
        mr.startSuspended = startResult.suspended;
        mr.startSuspendedAt = startResult.suspendedAt;
        mr.startCallError = startResult.callError;
        if (startResult.attemptedCall && !startResult.callOk) errorsInOrder.push_back(startResult.callError);

        if (!startResult.existedAsFunction) {
            mr.ticksSurvived = 0;
            mr.stopReason = "error: " + funcName + " not defined (real check, spec-lua-bindings.md Sec14.10's "
                             "own confirmed call shape not reachable for this stem in this run)";
        } else {
            struct TickSig {
                uint64_t existed = 0, ok = 0, err = 0;
                bool watchdogAbort = false;
            };
            TickSig prevSig;
            bool havePrevSig = false;
            bool stopped = false;
            bool startTracked = startResult.suspended;
            for (int tick = 1; tick <= kMissionTickBudget; ++tick) {
                host.engineState().screenFadeHostFrame(kFadeHostMsPerTick);
                // The cutscene machine and the zscene lifecycle (Sec26.25 Host
                // summary: promotion then completion, every frame), on the same
                // host clock; after the fade frame (CHOSEN order).
                host.engineState().cutsceneHostFrame();
                // The game clock (Sec48.3, CONFIRMED mechanism): advances by
                // this same tick's own modelled frame time, matching
                // get_frame_time's own per-resume value (kFadeHostMsPerTick/
                // 1000 - see that constant's and setFrameTime()'s own doc
                // comments above). CHOSEN call order: alongside the other
                // per-tick host clocks.
                host.engineState().gameClockAdvanceFrame(static_cast<double>(kFadeHostMsPerTick) / 1000.0);
                // The script-thread scheduler 0x00e0cf50, kSchedulerPassesPerTick
                // times (CHOSEN mapping, see that constant). The watchdog is
                // re-armed on each coroutine right before its resume
                // (lua_sethook resets the instruction count), so it bounds one
                // resume, not a thread's whole life.
                bool ownResumeThisTick = false;
                // PassEvent carries no lua_State* of its own (bare_globals.h)
                // - before/after are called in strict pairs for the same
                // record within one schedulerPass call (src/lua_bare_
                // globals.cpp's own single-threaded while loop, no
                // interleaving), so this one local safely bridges the two.
                lua_State* currentResumeCo = nullptr;
                for (int pass = 0; pass < kSchedulerPassesPerTick; ++pass) {
                    host.runScriptThreadSchedulerPass(
                        [&](const BareThreadTable::Record& rec) {
                            currentResumeCo = rec.co;
                            resetTraceRing(rec.co); // see g_traceRings' own doc comment above - fresh per resume
                            lua_sethook(rec.co, missionWatchdogHook, missionWatchdogHookMask(),
                                        kMissionWatchdogInstructionBudget);
                        },
                        [&](const BareThreadTable::PassEvent& ev) {
                            if (foreignIds.count(ev.id)) {
                                if (ev.resumed) mr.schedulerForeignResumes++;
                                if (!ev.error.empty()) schedulerForeignErrorsTotal++;
                                if (!ev.aliveAfter) foreignIds.erase(ev.id); // its id may be reused later
                                return;
                            }
                            if (ev.resumed) {
                                mr.schedulerResumes++;
                                ownResumeThisTick = true;
                            }
                            if (!ev.error.empty()) {
                                mr.schedulerThreadErrors++;
                                errorsInOrder.push_back(ev.error); // the runner's "<name>: <message>"
                                printTraceRingIfAny(currentResumeCo, ev.name, ev.error);
                            }
                            if (!ev.fetchError.empty())
                                errorsInOrder.push_back("lua_host_run: runner could not fetch " + ev.name + ": " +
                                                        ev.fetchError);
                            if (startTracked && ev.id == startResult.recordId && !ev.aliveAfter) {
                                startTracked = false;
                                if (!ev.error.empty()) {
                                    mr.startAfterTicks = "errored";
                                    mr.startAfterResumeError = ev.error;
                                } else if (ev.killedBefore) {
                                    mr.startAfterTicks = "killed";
                                } else {
                                    mr.startAfterTicks = "finished";
                                }
                            }
                        });
                }
                ++globalTick;
                // ===================== AUTO-ANSWER-HUD-PROMPTS (THIS TASK) =====================
                // See --auto-answer-hud-prompts's own usage text (above) for the
                // full citation. Any hud_prompt request recorded this tick (or
                // any earlier tick not yet answered - hudPromptRespondedCount
                // tracks the boundary) is answered once it has been showing for
                // at least hudPromptAutoAnswerDelayTicks ticks: spawn a
                // coroutine for its own registered callback name via the SAME
                // BareThreadTable mechanism --probe-autopilot's own dispatch
                // block (further above) already uses. Spawn only - the callback
                // body runs on the NEXT tick's own scheduler pass, same
                // decoupled-in-time shape Sec1.14 confirms for the real
                // dispatcher, not a synchronous call here.
                if (autoAnswerHudPrompts) {
                    auto& hudPromptReqs = host.engineState().hudPromptRequests();
                    while (hudPromptFirstSeenGlobalTick.size() < hudPromptReqs.size()) {
                        hudPromptFirstSeenGlobalTick.push_back(globalTick);
                    }
                    while (hudPromptRespondedCount < hudPromptReqs.size()) {
                        const auto& req = hudPromptReqs[hudPromptRespondedCount];
                        const long long firstSeen = hudPromptFirstSeenGlobalTick[hudPromptRespondedCount];
                        if (globalTick - firstSeen < hudPromptAutoAnswerDelayTicks) {
                            // Not due yet - later requests (recorded even more
                            // recently) can't be due either, so stop here and
                            // re-check next tick.
                            break;
                        }
                        ++hudPromptRespondedCount;
                        if (req.arg3.empty()) continue; // no callback name to respond with
                        // CHOSEN stand-in (hud-prompt-direct-callback-standin task, 2026-10-12):
                        // the real dispatch path that resolves hud_prompt's arg3 to a callback is
                        // not yet traced (spec-lua-api-behaviour.md Sec13.3, 2026-10-12 update -
                        // confirmed arg3 genuinely IS the callback name the engine calls, e.g.
                        // "m05_jump_local" in m05.lua, but via a mechanism NOT inside
                        // 0x00a4cff0/0x00a37210/0x00a370c0's own call chain). Measured
                        // (results/verify_m05_hud_prompt_autoanswer_20261012.md): spawning a
                        // coroutine for arg3 via BareThreadTable::allocate() (the prior approach)
                        // does NOT advance m05 past m05.lua:1141, even given 185 further ticks for
                        // the spawned coroutine to run. This directly calls the named global Lua
                        // function by string lookup instead (Host::callGlobalFunctionByName(),
                        // lua_host.cpp) - NOT how the real engine dispatches it, but produces the
                        // same observable effect (the callback body actually running, setting
                        // whatever flag the waiting script polls) for any script that only cares
                        // about the callback having run. Synchronous, not spawn-and-defer: there is
                        // no real-engine decoupled-dispatch evidence for THIS specific call (unlike
                        // on_trigger/on_death, Sec1.14), so no coroutine shape is assumed either way.
                        std::cerr << "[auto-answer-hud-prompts] hud_prompt(\"" << req.target << "\", \""
                                  << req.arg2 << "\", \"" << req.arg3 << "\") seen " << (globalTick - firstSeen)
                                  << " tick(s) ago - CHOSEN stand-in: calling global '" << req.arg3
                                  << "' directly\n";
                        std::string callErr;
                        bool called = host.callGlobalFunctionByName(host.gameplayState(), req.arg3, callErr);
                        if (!called) {
                            std::cerr << "[auto-answer-hud-prompts] callback '" << req.arg3
                                      << "' not called: " << (callErr.empty() ? "no such global function" : callErr)
                                      << "\n";
                        }
                    }
                }
                auto tickHooks = host.fireConfirmedHooks(host.gameplayState(), hooks, script.entryName);
                TickSig sig;
                bool newWatchdogThisTick = false;
                for (auto& hr : tickHooks) {
                    if (hr.existedAsFunction) sig.existed++;
                    if (hr.attemptedCall) {
                        if (hr.callOk) {
                            sig.ok++;
                        } else {
                            sig.err++;
                            errorsInOrder.push_back(hr.callError);
                            if (hr.callError.find("watchdog") != std::string::npos) {
                                sig.watchdogAbort = true;
                                newWatchdogThisTick = true;
                            }
                        }
                    }
                }
                mr.ticksSurvived = tick;
                if (newWatchdogThisTick) {
                    mr.stopReason = "error (a hook call newly hit the instruction watchdog this tick)";
                    stopped = true;
                    break;
                }
                // A fade transition still in flight (state != target) is real,
                // silent progress the hook-firing signature above can't see -
                // screenFadeHostFrame() keeps advancing toward its CONFIRMED
                // (Sec26.24) host-side timeout completion every tick, but
                // nothing about that shows up in sig.{existed,ok,err} while the
                // suspended mission thread that's waiting on it isn't resumed.
                // Without this check the loop declared "completion" after 2
                // ticks (666ms) even when the fade's own request duration was
                // longer, so the host-side substitute never got the chance to
                // fire (found 2026-10-02, purrsian-d3's own next-blocker
                // follow-up). Only suppresses the early-stop; never extends the
                // loop past kMissionTickBudget.
                const auto& fade = host.engineState().screenFade();
                const bool fadePending = fade.state.known() && fade.target.known() &&
                                          fade.state.get() != fade.target.get();
                // Same reasoning for this mission's own script threads: a
                // resumed thread may be making progress the hook signature
                // can't see (a poll loop, a countdown), so a tick that resumed
                // any of them never counts toward "completion".
                if (havePrevSig && sig.existed == prevSig.existed && sig.ok == prevSig.ok && sig.err == prevSig.err &&
                    !fadePending && !ownResumeThisTick) {
                    mr.stopReason = "completion (2 consecutive identical tick signatures - real, measured "
                                     "idempotence, CHOSEN stopping convention, see this block's own top comment)";
                    stopped = true;
                    break;
                }
                prevSig = sig;
                havePrevSig = true;
            }
            if (!stopped) {
                mr.stopReason = "budget exhaustion (" + std::to_string(kMissionTickBudget) + " ticks)";
            }
            if (startTracked) {
                const BareThreadTable::Record* rec = host.bareGlobals().threads().findAlive(startResult.recordId);
                if (rec && rec->name == funcName) {
                    mr.startAfterTicks = "still_suspended";
                    lua_Debug ar;
                    if (lua_getstack(rec->co, 1, &ar) && lua_getinfo(rec->co, "Sln", &ar)) {
                        mr.startStillSuspendedAt = (ar.short_src[0] ? ar.short_src : "?") + std::string(":") +
                                                   std::to_string(ar.currentline) +
                                                   (ar.name ? (std::string(" (in ") + ar.name + ")") : "");
                    }
                    // Per-coroutine ring (2026-10-03): this mission's own
                    // thread was resumed on its own schedulerPass passes
                    // throughout the tick loop above, each resume
                    // overwriting g_traceRings[rec->co] fresh - so reading
                    // it back here by this SAME co is always that thread's
                    // own last resume, never another thread's, answering
                    // "what does this mission's own wait poll" for the
                    // first time (the global-ring version could not).
                    printTraceRingStillSuspended(rec->co, funcName);
                    // (2026-10-07) what this same coroutine called on its
                    // own FIRST resume - its _start call's own setup, before
                    // it ever reached the wait loop printed just above - see
                    // g_firstResumeTraceRings' own doc comment above.
                    printTraceRingFirstResume(rec->co, funcName);

                    // --probe-autopilot (2026-10-07, this task): DIAGNOSTIC
                    // ONLY, this entire block is skipped unless --probe-
                    // autopilot was given - never changes mr.startAfterTicks/
                    // startStillSuspendedAt/any REAL field above, only the
                    // separate probeAutopilot* fields. See that flag's own
                    // usage text for the full citation list of which natives
                    // this synthesizes and which were checked and excluded.
                    if (probeAutopilot) {
                        mr.probeAutopilotAttempted = true;
                        lua_State* missionCo = rec->co; // captured now - `rec` itself is not held past this point
                        auto regIt = g_firstResumeRegistrations.find(missionCo);
                        const CapturedRegistration* chosenReg = nullptr;
                        if (regIt != g_firstResumeRegistrations.end()) {
                            for (const auto& cand : regIt->second) {
                                if (!cand.callbackName.empty()) { chosenReg = &cand; break; }
                            }
                        }
                        if (!chosenReg) {
                            // Real finding, not an omission (this task's own
                            // instruction): the first-resume trace shows no
                            // CONFIRMED callback-registration native with a
                            // recoverable callback-name argument before this
                            // mission's wait loop - matches the already-
                            // established answer for m06/m10/m11/m13/m14/m15
                            // (results/trace_host_calls_firstresume_20261007.md).
                            mr.probeAutopilotNote =
                                "nothing to synthesize - wait is likely satisfied by polled world state, not a "
                                "callback (first-resume trace shows no confirmed callback-registration native "
                                "call with a recoverable callback-name argument before the wait loop)";
                            std::cerr << "[probe-autopilot] " << funcName << " nothing to synthesize: "
                                      << mr.probeAutopilotNote << "\n";
                        } else {
                            mr.probeAutopilotHadCallback = true;
                            mr.probeAutopilotNative = chosenReg->nativeName;
                            mr.probeAutopilotCallbackName = chosenReg->callbackName;
                            mr.probeAutopilotTargetName = chosenReg->targetName;

                            // Dispatch, CORRECTED 2026-10-09 (spec-lua-api-
                            // behaviour.md Sec1.14's own "GAP CLOSED" finding,
                            // 2026-10-09 changelog entry, commit 383f653):
                            // the real dispatcher 0x00a29e40 does NOT call
                            // lua_call/lua_pcall synchronously. It hands off
                            // to the reader 0x00a1fe10, which SPAWNS A NEW
                            // LUA COROUTINE for the resolved callback name
                            // (0x00e0ca80->0x00e0c720, the same mechanism
                            // Sec26.27 already documents for thread_new) -
                            // the body only actually runs LATER, when the
                            // scheduler 0x00e0cba0 resumes it. The previous
                            // version of this block (a bare lua_getglobal +
                            // lua_pcall, matching Host::fireConfirmedHooks'
                            // shape) was this project's own CHOSEN stand-in,
                            // made before Sec1.14's dispatch mechanism was
                            // known - superseded now that it is. This reuses
                            // the EXACT same coroutine mechanism
                            // callMissionStart() above already goes through
                            // (BareThreadTable::allocate()/run(), the host's
                            // model of runner 0x00e0cba0) - no second
                            // coroutine mechanism invented for this. Spawn
                            // only: allocate() never resumes the coroutine by
                            // itself, so the callback body provably has NOT
                            // run yet at this point in the code - only the
                            // follow-up window's own schedulerPass() calls,
                            // below, can resume it (see
                            // BareThreadTable::schedulerPass's own doc
                            // comment, bare_globals.h, for why one call per
                            // tick already matches the real engine's own
                            // steady-state cadence). hasParent=false: nothing
                            // is "current" when an engine-side trigger/hook
                            // fires from outside any running script thread -
                            // the same root-level situation
                            // BareThreadTable::startThread() models for a
                            // mission's own top-level entry. Zero arguments:
                            // unchanged from before (isCallbackRegistrationNative's
                            // own doc comment - no spec section establishes the
                            // FIRED callback's own argument shape, only the
                            // registration call's).
                            lua_State* L = host.gameplayState();
                            std::string targetNote =
                                chosenReg->targetName.empty() ? std::string() : (", \"" + chosenReg->targetName + "\"");
                            std::cerr << "[probe-autopilot] " << funcName << " SYNTHETIC dispatch: first-resume "
                                      << "trace shows " << chosenReg->nativeName << "(\"" << chosenReg->callbackName
                                      << "\"" << targetNote << ") registered before the wait loop - spawning a "
                                      << "coroutine for global '" << chosenReg->callbackName << "', zero args "
                                      << "(Sec1.14: real dispatch spawns a coroutine, never a synchronous call - "
                                      << "it only runs on a later scheduler pass)\n";
                            BareThreadTable::Record* spawnedRec = host.bareGlobals().threads().allocate(
                                L, chosenReg->callbackName, /*nargs=*/0, /*hasParent=*/false);
                            uint16_t spawnedId = BareThreadTable::kNoThread;
                            if (!spawnedRec) {
                                mr.probeAutopilotDispatchResult =
                                    "SYNTHETIC: script-thread table full (" +
                                    std::to_string(BareThreadTable::kCapacity) +
                                    " records, HIGH CONFIDENCE capacity) - coroutine for '" +
                                    chosenReg->callbackName + "' not spawned";
                            } else {
                                spawnedId = spawnedRec->id;
                                mr.probeAutopilotDispatchResult =
                                    "SYNTHETIC: coroutine spawned for '" + chosenReg->callbackName +
                                    "' (record id " + std::to_string(spawnedId) +
                                    ") - not run synchronously; the follow-up window's scheduler pass(es) below "
                                    "resume it";
                            }

                            // Follow-up window: bounded, CHOSEN
                            // (kProbeAutopilotFollowUpTicks) - same per-tick
                            // shape as the main loop above, just shorter.
                            // Never feeds errorsInOrder/mr.firstErrorMessage/
                            // mr.startAfterTicks (those are this mission's
                            // REAL, already-final measurement); collected
                            // separately into probeErrors below.
                            std::vector<std::string> probeErrors;
                            bool ownRecordResumedInFollowup = false;
                            // Tracking for the SPAWNED callback coroutine itself (spawnedId,
                            // above) - separate from ownRecordResumedInFollowup, which is
                            // about whether the MISSION's own wait unblocked. These three
                            // answer "did the real scheduler mechanism actually carry the
                            // spawn through to a real resume", the thing this whole change
                            // is about proving.
                            bool spawnedResumedInFollowup = false;
                            bool spawnedBodyRan = false;
                            std::string spawnedFetchError;
                            std::string spawnedOwnError;
                            for (int ftick = 0; ftick < kProbeAutopilotFollowUpTicks; ++ftick) {
                                host.engineState().screenFadeHostFrame(kFadeHostMsPerTick);
                                host.engineState().cutsceneHostFrame();
                                host.engineState().gameClockAdvanceFrame(static_cast<double>(kFadeHostMsPerTick) /
                                                                          1000.0);
                                for (int pass = 0; pass < kSchedulerPassesPerTick; ++pass) {
                                    host.runScriptThreadSchedulerPass(
                                        [&](const BareThreadTable::Record& r2) {
                                            resetTraceRing(r2.co);
                                            lua_sethook(r2.co, missionWatchdogHook, missionWatchdogHookMask(),
                                                        kMissionWatchdogInstructionBudget);
                                        },
                                        [&](const BareThreadTable::PassEvent& ev) {
                                            if (ev.id == spawnedId && spawnedId != BareThreadTable::kNoThread) {
                                                if (ev.resumed) spawnedResumedInFollowup = true;
                                                if (ev.bodyRan) spawnedBodyRan = true;
                                                if (!ev.fetchError.empty()) spawnedFetchError = ev.fetchError;
                                                if (!ev.error.empty()) spawnedOwnError = ev.error;
                                            }
                                            if (foreignIds.count(ev.id)) return; // not this mission's own concern
                                            if (ev.id == startResult.recordId && ev.resumed)
                                                ownRecordResumedInFollowup = true;
                                            if (!ev.error.empty()) probeErrors.push_back(ev.error);
                                            if (!ev.fetchError.empty())
                                                probeErrors.push_back("lua_host_run: runner could not fetch " +
                                                                       ev.name + ": " + ev.fetchError);
                                        });
                                }
                            }
                            // Finalize the spawned coroutine's own outcome now that the
                            // follow-up window's scheduler passes have had a chance to
                            // resume it - this is the part that genuinely only becomes
                            // knowable LATER, matching the real decoupled-in-time mechanism
                            // (Sec1.14): at the point of the spawn itself, above, nothing
                            // could yet be said about whether the callback ran or what it did.
                            if (spawnedId != BareThreadTable::kNoThread) {
                                if (!spawnedResumedInFollowup) {
                                    mr.probeAutopilotDispatchResult +=
                                        " / still not resumed after " + std::to_string(kProbeAutopilotFollowUpTicks) +
                                        " follow-up ticks";
                                } else if (!spawnedFetchError.empty()) {
                                    mr.probeAutopilotDispatchResult =
                                        "SYNTHETIC: callback name '" + chosenReg->callbackName +
                                        "' registered by " + chosenReg->nativeName +
                                        " could not be resolved to a global function when its spawned coroutine "
                                        "was resumed (" +
                                        spawnedFetchError + ") - nothing ran";
                                } else if (!spawnedOwnError.empty()) {
                                    mr.probeAutopilotDispatchResult =
                                        "SYNTHETIC dispatch error (on later resume): " + spawnedOwnError;
                                } else if (spawnedBodyRan) {
                                    mr.probeAutopilotDispatchResult =
                                        "SYNTHETIC dispatch ok (ran on a later scheduler pass, no Lua error)";
                                }
                            }
                            if (!probeErrors.empty()) {
                                mr.probeAutopilotNextBlocker = "SYNTHETIC-probe next error: " + probeErrors.front();
                            } else {
                                const BareThreadTable::Record* rec2 =
                                    host.bareGlobals().threads().findAlive(startResult.recordId);
                                if (rec2 && rec2->name == funcName) {
                                    std::string nowAt;
                                    lua_Debug ar2;
                                    if (lua_getstack(rec2->co, 1, &ar2) && lua_getinfo(rec2->co, "Sln", &ar2)) {
                                        nowAt = (ar2.short_src[0] ? ar2.short_src : "?") + std::string(":") +
                                                std::to_string(ar2.currentline) +
                                                (ar2.name ? (std::string(" (in ") + ar2.name + ")") : "");
                                    }
                                    if (!nowAt.empty() && nowAt != mr.startStillSuspendedAt) {
                                        mr.probeAutopilotNextBlocker =
                                            "SYNTHETIC-probe: advanced to a new wait, no new error - now parked "
                                            "at " +
                                            nowAt;
                                    } else if (ownRecordResumedInFollowup) {
                                        mr.probeAutopilotNextBlocker =
                                            "SYNTHETIC-probe: resumed but parked at the SAME wait location after " +
                                            std::to_string(kProbeAutopilotFollowUpTicks) +
                                            " follow-up ticks - the synthesized event did not change where it "
                                            "waits";
                                    } else {
                                        mr.probeAutopilotNextBlocker =
                                            "SYNTHETIC-probe: thread was never resumed again in " +
                                            std::to_string(kProbeAutopilotFollowUpTicks) +
                                            " follow-up ticks after the synthesized dispatch";
                                    }
                                } else {
                                    mr.probeAutopilotNextBlocker =
                                        "SYNTHETIC-probe: thread no longer in the table after the synthesized "
                                        "dispatch (finished or released), no error recorded";
                                }
                            }
                            std::cerr << "[probe-autopilot] " << funcName << " dispatch_result=["
                                      << mr.probeAutopilotDispatchResult << "] next_blocker=["
                                      << mr.probeAutopilotNextBlocker << "]\n";
                        }
                    }
                } else {
                    mr.startAfterTicks = "released_unobserved"; // left the table outside a scheduler pass
                }
            }
        }

        if (!errorsInOrder.empty()) mr.firstErrorMessage = errorsInOrder.front();
        for (auto& e : errorsInOrder) {
            std::string name = extractNilGlobalName(e);
            if (!name.empty()) { mr.firstUnimplementedStub = name; break; }
        }

        // The headline measurement (see MissionResult::missionCompletionFlagSet's
        // own doc comment above): read back THIS mission's own real +0x88
        // bit 0x4 now that its tick loop has stopped - whatever got it
        // there (mission_set_completed called by the mission's own script,
        // or the organic missionEndDispatch() type-1/2 paths, Sec21.27b).
        mr.missionCompletionFlagSet = host.engineState().missionCompletionFlagBitSet(mr.stem);

        missionResults.push_back(std::move(mr));
        if (missionResults.size() % 10 == 0 || missionResults.size() == missions.size()) {
            std::cout << "  ... " << missionResults.size() << "/" << missions.size() << " missions driven\n";
        }
    }

    std::ofstream missionOut(outDir / "verdict_mission_drive.tsv");
    missionOut << "stem\tentry_name\tcontainer_name\tscript_found\tload_ok\tload_error\tpcall_ok\tpcall_error\t"
                  "start_func_name\tstart_existed\tstart_attempted\tstart_call_ok\tstart_call_error\t"
                  "ticks_survived\tstop_reason\tfirst_error_message\tfirst_unimplemented_stub\tstart_suspended\t"
                  "start_suspended_at\tstart_after_ticks\tstart_after_resume_error\tstart_still_suspended_at\t"
                  "scheduler_resumes\tscheduler_thread_errors\tscheduler_foreign_resumes\t"
                  "probe_autopilot_attempted\tprobe_autopilot_native\tprobe_autopilot_callback_name\t"
                  "probe_autopilot_target_name\tprobe_autopilot_dispatch_result\tprobe_autopilot_next_blocker\t"
                  "probe_autopilot_note\tmission_completion_flag_0x4_set\n";
    uint64_t missionsFoundCount = 0, missionsStartExistedCount = 0, missionsStartOkCount = 0;
    uint64_t missionsStartSuspendedCount = 0;
    uint64_t missionsStartStillSuspended = 0, missionsStartFinishedAfterResume = 0;
    uint64_t missionsStartErroredAfterResume = 0, missionsStartKilledAfterResume = 0;
    uint64_t schedulerResumesTotal = 0, schedulerOwnErrorsTotal = 0, schedulerForeignResumesTotal = 0;
    uint64_t stopBudget = 0, stopError = 0, stopCompletion = 0;
    uint64_t ticksSum = 0;
    // THE headline count this task (2026-10-08) was asked for: how many of
    // the 49 missions' own mission handles actually reached the real
    // completion flag (+0x88 bit 0x4, Sec15.10/Sec21.27b) - see
    // MissionResult::missionCompletionFlagSet's own doc comment above for
    // why this is deliberately separate from stopReason's own "completion"
    // bucket (a different, pre-existing, Lua-coroutine-idle measurement).
    uint64_t missionsCompletionFlagSetCount = 0;
    for (auto& mr : missionResults) {
        if (mr.scriptFound) missionsFoundCount++;
        if (mr.startExisted) missionsStartExistedCount++;
        if (mr.startCallOk) missionsStartOkCount++;
        if (mr.startSuspended) missionsStartSuspendedCount++;
        if (mr.missionCompletionFlagSet) missionsCompletionFlagSetCount++;
        ticksSum += (uint64_t)mr.ticksSurvived;
        if (mr.stopReason.rfind("budget exhaustion", 0) == 0) stopBudget++;
        else if (mr.stopReason.rfind("error", 0) == 0) stopError++;
        else if (mr.stopReason.rfind("completion", 0) == 0) stopCompletion++;
        missionOut << sanitizeTsv(mr.stem) << "\t" << sanitizeTsv(mr.entryName) << "\t"
                   << sanitizeTsv(mr.containerName) << "\t" << (mr.scriptFound ? 1 : 0) << "\t"
                   << (mr.loadOk ? 1 : 0) << "\t" << sanitizeTsv(mr.loadError) << "\t" << (mr.pcallOk ? 1 : 0)
                   << "\t" << sanitizeTsv(mr.pcallError) << "\t" << sanitizeTsv(mr.stem + "_start") << "\t"
                   << (mr.startExisted ? 1 : 0) << "\t" << (mr.startAttempted ? 1 : 0) << "\t"
                   << (mr.startCallOk ? 1 : 0) << "\t" << sanitizeTsv(mr.startCallError) << "\t"
                   << mr.ticksSurvived << "\t" << sanitizeTsv(mr.stopReason) << "\t"
                   << sanitizeTsv(mr.firstErrorMessage) << "\t" << sanitizeTsv(mr.firstUnimplementedStub) << "\t"
                   << (mr.startSuspended ? 1 : 0) << "\t" << sanitizeTsv(mr.startSuspendedAt) << "\t"
                   << sanitizeTsv(mr.startAfterTicks) << "\t" << sanitizeTsv(mr.startAfterResumeError) << "\t"
                   << sanitizeTsv(mr.startStillSuspendedAt) << "\t" << mr.schedulerResumes << "\t"
                   << mr.schedulerThreadErrors << "\t" << mr.schedulerForeignResumes << "\t"
                   << (mr.probeAutopilotAttempted ? 1 : 0) << "\t" << sanitizeTsv(mr.probeAutopilotNative) << "\t"
                   << sanitizeTsv(mr.probeAutopilotCallbackName) << "\t"
                   << sanitizeTsv(mr.probeAutopilotTargetName) << "\t"
                   << sanitizeTsv(mr.probeAutopilotDispatchResult) << "\t"
                   << sanitizeTsv(mr.probeAutopilotNextBlocker) << "\t" << sanitizeTsv(mr.probeAutopilotNote)
                   << "\t" << (mr.missionCompletionFlagSet ? 1 : 0) << "\n";
        if (mr.startAfterTicks == "still_suspended") missionsStartStillSuspended++;
        else if (mr.startAfterTicks == "finished") missionsStartFinishedAfterResume++;
        else if (mr.startAfterTicks == "errored") missionsStartErroredAfterResume++;
        else if (mr.startAfterTicks == "killed") missionsStartKilledAfterResume++;
        schedulerResumesTotal += mr.schedulerResumes;
        schedulerOwnErrorsTotal += mr.schedulerThreadErrors;
        schedulerForeignResumesTotal += mr.schedulerForeignResumes;
    }
    missionOut.close();

    // --- NEW runtime stub-hit ranking folding in the mission-driven calls
    // (this task's own explicit deliverable "meant to directly guide Team
    // A's next spec tranches") - SAME shape as verdict_stub_hits_with_
    // hooks.tsv above, plus one new incremental_from_missions column (the
    // SAME before/after HitLog-snapshot-delta convention already used
    // throughout this file). Reads host.hitLog() at the very end of this
    // whole run - i.e. genuinely ALL-INCLUSIVE: top-level (step 0,
    // restricted) + hook-firing (Sec9.140, restricted) + this step's own
    // mission-driven runChunk/`_start`/tick calls, all folded together,
    // never silently overwriting either pre-existing ranking file. -------
    std::ofstream missionHitsOut(outDir / "verdict_stub_hits_with_missions.tsv");
    missionHitsOut << "name\tcall_count_all_inclusive\tcall_count_before_missions(top_level+hooks)\t"
                      "incremental_from_missions\tlast_argc\tlast_arg_types\tstatic_total_call_sites\t"
                      "static_distinct_scripts\tgameplay_call_count\tui_call_count\n";
    std::vector<std::pair<std::string, sr3luahost::StubHit>> hitRowsWithMissions(host.hitLog().hits().begin(),
                                                                                  host.hitLog().hits().end());
    std::sort(hitRowsWithMissions.begin(), hitRowsWithMissions.end(), [](const auto& a, const auto& b) {
        if (a.second.callCount != b.second.callCount) return a.second.callCount > b.second.callCount;
        return a.first < b.first;
    });
    uint64_t missionIncrementalTotal = 0;
    for (auto& [name, hit] : hitRowsWithMissions) {
        auto beforeIt = preMissionCounts.find(name);
        uint64_t before = (beforeIt != preMissionCounts.end()) ? beforeIt->second : 0;
        uint64_t incremental = hit.callCount - before; // never negative: HitLog only ever accumulates
        missionIncrementalTotal += incremental;
        auto it = staticRank.find(name);
        uint64_t sCalls = (it != staticRank.end()) ? it->second.totalCallSites : 0;
        uint64_t sScripts = (it != staticRank.end()) ? it->second.distinctScripts : 0;
        missionHitsOut << name << "\t" << hit.callCount << "\t" << before << "\t" << incremental << "\t"
                       << hit.lastArgc << "\t" << hit.lastArgTypes << "\t" << sCalls << "\t" << sScripts << "\t"
                       << hit.gameplayCallCount << "\t" << hit.uiCallCount << "\n";
    }
    missionHitsOut.close();

    std::ofstream missionSummary(outDir / "verdict_mission_drive_summary.txt");
    auto mline = [&](const std::string& s) { missionSummary << s << "\n"; std::cout << s << "\n"; };
    mline("\n=== STEP 1 SUMMARY (real mission-driving pass) ===");
    mline("real_rows_loaded_from_mission_package_per_container_tsv=" + std::to_string(missionRows.size()));
    mline("distinct_stems_after_dedup_and_stem_covers_a_start_script_filter=" + std::to_string(missions.size()) +
          " (HANDOFF.md's own citation: \"49/54\" - reported here as this run's own actual count, not assumed)");
    // start_checkpoint_arg/start_is_restart_arg are CONFIRMED call-shape values
    // (spec-lua-api-behaviour.md Sec36.4, spec-lua-bindings.md Sec14.11, synced
    // 2026-10-02) - no longer CHOSEN, so reported on their own line, separate
    // from the genuinely CHOSEN values below (not recovered data).
    mline("CONFIRMED_values(spec-lua-api-behaviour.md Sec36.4/spec-lua-bindings.md Sec14.11 call shape, "
          "fresh start only - this harness has no resume/retry call site): start_checkpoint_arg=\"" +
          kMissionStartCheckpoint + "\", start_is_restart_arg=" +
          std::string(kMissionStartIsRestart ? "true" : "false"));
    // mission_simulated_seconds_budget: the tick budget re-expressed in this
    // host's own simulated time, so a caller doesn't have to multiply ticks
    // by fade_host_ms_per_tick by hand to judge whether "still suspended"
    // missions had enough modelled time to finish a long but legitimate wait
    // (e.g. a scripted delay/fade) before assuming they're stuck on a real
    // blocker - see --tick-budget='s own usage text.
    mline("mission_simulated_seconds_budget=" +
          std::to_string(static_cast<double>(kMissionTickBudget) * kFadeHostMsPerTick / 1000.0) +
          " (CHOSEN: mission_tick_budget * fade_host_ms_per_tick / 1000 - this host's own per-tick frame "
          "time, not a real-engine frame rate; raise via --tick-budget= to tell a long legitimate wait "
          "apart from a missing value, then read start_still_suspended_at per mission below)");
    mline("CHOSEN_values(not recovered data, stated per this project's own discipline): mission_tick_budget=" +
          std::to_string(kMissionTickBudget) +
          ", watchdog_instruction_budget=" + std::to_string(kMissionWatchdogInstructionBudget) +
          ", fade_host_ms_per_tick=" + std::to_string(kFadeHostMsPerTick) +
          ", scheduler_passes_per_tick=" + std::to_string(kSchedulerPassesPerTick) +
          " (CHOSEN: 1 tick == 1 rendered/game frame == 1 scheduler pass, matching the corrected spec's "
          "once-per-frame driver 0x00e0dfc0 - the old 11x figure approximated a ~30 ms independent pump "
          "the spec now confirms was boot-only, see this block's own top comment)" +
          ", get_frame_time_per_resume_s=" + std::to_string(static_cast<float>(kFadeHostMsPerTick) / 1000.0f) +
          " (CHOSEN: tied to fade_host_ms_per_tick so one resume's wall-clock delta matches one tick, see "
          "setFrameTime() call above)" +
          " (watchdog: same constant as host.h's own kHookWatchdogInstructionBudget, not independently chosen)");
    mline("missions_with_script_found=" + std::to_string(missionsFoundCount) + "/" + std::to_string(missions.size()));
    mline("missions_with_start_existed=" + std::to_string(missionsStartExistedCount) + "/" + std::to_string(missions.size()));
    mline("missions_with_start_call_ok=" + std::to_string(missionsStartOkCount) + "/" + std::to_string(missions.size()));
    // _start runs as a script-thread record (Sec26.27 runner, request 11): a
    // call that yielded without error counts as ok above and is listed here
    // (measured right after the call, before any tick). The tick loop's
    // scheduler passes (0x00e0cf50) then resume it; what became of it is the
    // missions_start_after_ticks line.
    mline("missions_with_start_suspended=" + std::to_string(missionsStartSuspendedCount) + "/" +
          std::to_string(missions.size()) + " (subset of start_call_ok: yielded on the first run, before any tick)");
    mline("missions_start_after_ticks=still_suspended:" + std::to_string(missionsStartStillSuspended) +
          " finished:" + std::to_string(missionsStartFinishedAfterResume) +
          " errored:" + std::to_string(missionsStartErroredAfterResume) +
          " killed:" + std::to_string(missionsStartKilledAfterResume) +
          " (of the missions_with_start_suspended records, after the tick loop's scheduler passes)");
    // probed_unresolved_names (DIAGNOSTIC ONLY, see --probe-unresolved-names'
    // own usage text above): every distinct name the named-object resolution
    // gate synthetically resolved during this run, so the probe's own effect
    // on the numbers above is never silently invisible. Empty/omitted when
    // the flag is off - this line only appears to avoid implying a probe
    // happened when it didn't.
    if (probeUnresolvedNames) {
        const auto& probed = host.engineState().probedNames();
        std::string namesJoined;
        for (const auto& n : probed) {
            if (!namesJoined.empty()) namesJoined += ", ";
            namesJoined += n;
        }
        mline("probed_unresolved_names=" + std::to_string(probed.size()) +
              " distinct name(s) synthetically resolved (DIAGNOSTIC, not real data): [" + namesJoined + "]");
        mline("probed_field_reads=" + std::to_string(host.engineState().probedFieldReadCount()) +
              " (DIAGNOSTIC: per-character/per-vehicle OPEN field reads that fell back to a labelled "
              "placeholder because their owner was itself a probed name - see probedFieldOr()'s own doc "
              "comment, engine_state.h)");
        mline("probed_world_position_reads=" + std::to_string(host.engineState().worldPositionProbeReadCount()) +
              " (DIAGNOSTIC: get_dist's own worldPosition OPEN gate, widened regardless of whether the "
              "owner name was itself probed - a fixed synthetic (0,0,0) position, never real data - see "
              "worldPositionOr()'s own doc comment, engine_state.h)");
        const auto& csm = host.engineState().controlStateMachine();
        mline("control_state_stack_after_missions=depth:" + std::to_string(csm.stack().size()) +
              " handles_issued:" + std::to_string(csm.handleCounter()) +
              " (spec-player-runtime.md Sec2.5 request stack, capacity 5; pushes are player_controls_disable's only)");
    }
    // probe_autopilot summary (DIAGNOSTIC ONLY, see --probe-autopilot's own
    // usage text above): aggregate outcome across every still_suspended
    // mission this run attempted it on - same "never silent, never implies
    // a probe happened when it didn't" convention as probed_unresolved_names
    // just above.
    if (probeAutopilot) {
        uint64_t attempted = 0, hadCallback = 0, nothingToSynthesize = 0;
        std::string missionsWithCallback, missionsWithNothing;
        for (const auto& mr : missionResults) {
            if (!mr.probeAutopilotAttempted) continue;
            attempted++;
            if (mr.probeAutopilotHadCallback) {
                hadCallback++;
                if (!missionsWithCallback.empty()) missionsWithCallback += ", ";
                missionsWithCallback += mr.stem + "(" + mr.probeAutopilotNative + ")";
            } else {
                nothingToSynthesize++;
                if (!missionsWithNothing.empty()) missionsWithNothing += ", ";
                missionsWithNothing += mr.stem;
            }
        }
        mline("probe_autopilot_missions_attempted=" + std::to_string(attempted) +
              " (of this run's own still_suspended missions)");
        mline("probe_autopilot_synthesized_events=" + std::to_string(hadCallback) +
              " distinct mission(s) with a SYNTHETIC callback dispatch (DIAGNOSTIC, not real data): [" +
              missionsWithCallback + "]");
        mline("probe_autopilot_nothing_to_synthesize=" + std::to_string(nothingToSynthesize) +
              " mission(s) whose first-resume trace showed no confirmed callback-registration native - a real "
              "finding (the wait is most likely satisfied by polled world state, not a callback), not an "
              "omission: [" + missionsWithNothing + "]");
    }
    mline("scheduler_passes=own_resumes:" + std::to_string(schedulerResumesTotal) +
          " own_thread_errors:" + std::to_string(schedulerOwnErrorsTotal) +
          " leftover_resumes:" + std::to_string(schedulerForeignResumesTotal) +
          " leftover_thread_errors:" + std::to_string(schedulerForeignErrorsTotal) +
          " (leftover = records still live from an earlier mission in the shared gameplay state)");
    mline("live_script_threads_after_missions=" + std::to_string(host.bareGlobals().threads().liveCount()) +
          " (capacity " + std::to_string(BareThreadTable::kCapacity) + ")");
    mline("stop_reason_histogram: budget_exhaustion=" + std::to_string(stopBudget) + " error=" +
          std::to_string(stopError) + " completion=" + std::to_string(stopCompletion) + " (sum should equal "
          "missions_with_script_found, since a not-found script gets its own separate \"script not found\" "
          "stop reason, counted under neither bucket above)");
    // THE headline number this task (2026-10-08, Sec21.27a/Sec21.27b real
    // phase machine + completion-flag writer) was asked to report:
    // genuinely distinct from stop_reason_histogram's own "completion"
    // bucket just above (that one only means "2 consecutive identical tick
    // signatures" - a Lua-idle measurement, not the mission's own
    // completion state). This counts missions whose own mission handle
    // actually reached +0x88 bit 0x4 (EngineState::missionCompletionFlagBitSet()).
    mline("missions_with_real_completion_flag_0x4_set=" + std::to_string(missionsCompletionFlagSetCount) + "/" +
          std::to_string(missions.size()) +
          " (the mission's own handle actually marked complete - mission_set_completed called by its own "
          "script, or an organic missionEndDispatch() type-1/2 path, Sec21.27b - NOT the same thing as "
          "stop_reason=completion above, which only means the tick loop went idle)");
    mline("total_ticks_survived_across_all_missions=" + std::to_string(ticksSum));
    mline("total_stub_calls_this_step_contributed(ALL-INCLUSIVE minus pre-step-1 snapshot, real HitLog delta)=" +
          std::to_string(missionIncrementalTotal));
    {
        // Sec26.25: the per-frame cutscene machine + zscene promotion/completion.
        // Also appended to verdict_summary.txt.
        const std::string csLine = "cutscene_frame=" + host.engineState().cutsceneFrameSummary();
        mline(csLine);
        std::ofstream summaryAppend(outDir / "verdict_summary.txt", std::ios::app);
        summaryAppend << csLine << "\n";
    }
    {
        // Screen fade (Sec26.24): which path completed each transition. real =
        // the UI script called Screen_fade_transition_complete; fallback_* =
        // the labelled HOST-SIDE SUBSTITUTE timer (screen_fade_do undefined /
        // called but never completing). Also appended to verdict_summary.txt.
        const auto& fc = host.engineState().screenFadeCounters();
        std::string fadeLine = "fade_completion_path=" + host.engineState().screenFadeCompletionPathSummary();
        std::string fadeDetail = "fade_detail=screen_fade_do_calls:" + std::to_string(fc.screenFadeDoCalls) +
                                 " screen_fade_do_errors:" + std::to_string(fc.screenFadeDoErrors) +
                                 " host_frames:" + std::to_string(fc.framesRun) +
                                 " frames_blocked_on_open:" + std::to_string(fc.framesBlockedOnOpen) +
                                 (fc.lastFrameBlocker.empty() ? std::string() : " last_blocker=[" + fc.lastFrameBlocker + "]");
        mline(fadeLine);
        mline(fadeDetail);
        std::ofstream summaryAppend(outDir / "verdict_summary.txt", std::ios::app);
        summaryAppend << fadeLine << "\n" << fadeDetail << "\n";
    }
    mline("\n--- Per-mission detail (also in verdict_mission_drive.tsv) ---");
    for (auto& mr : missionResults) {
        std::ostringstream row;
        row << "  " << mr.stem << ": script_found=" << (mr.scriptFound ? "yes" : "NO");
        if (mr.scriptFound) {
            row << " load_ok=" << (mr.loadOk ? 1 : 0) << " pcall_ok=" << (mr.pcallOk ? 1 : 0)
                << " start_existed=" << (mr.startExisted ? 1 : 0) << " start_call_ok=" << (mr.startCallOk ? 1 : 0)
                << (mr.startSuspended ? " start_suspended=1" : "") << " ticks_survived=" << mr.ticksSurvived << " stop_reason=[" << mr.stopReason << "]";
            if (mr.startAfterTicks == "still_suspended")
                row << " start_after_ticks=still_suspended waiting_at=["
                    << (mr.startStillSuspendedAt.empty() ? "?" : mr.startStillSuspendedAt) << "]";
            if (!mr.firstErrorMessage.empty()) row << " first_error=[" << mr.firstErrorMessage << "]";
            if (!mr.firstUnimplementedStub.empty()) row << " first_unimplemented_stub=" << mr.firstUnimplementedStub;
            if (mr.probeAutopilotAttempted) {
                if (mr.probeAutopilotHadCallback)
                    row << " probe_autopilot=[" << mr.probeAutopilotNative << "(\"" << mr.probeAutopilotCallbackName
                        << "\")" << " dispatch=[" << mr.probeAutopilotDispatchResult << "]"
                        << " next_blocker=[" << mr.probeAutopilotNextBlocker << "]]";
                else
                    row << " probe_autopilot=[" << mr.probeAutopilotNote << "]";
            }
        }
        mline(row.str());
    }
    missionSummary.close();

    std::cout << "\nWrote " << (outDir / "verdict_mission_drive.tsv").string() << " (new), "
              << (outDir / "verdict_stub_hits_with_missions.tsv").string() << " (new), "
              << (outDir / "verdict_mission_drive_summary.txt").string() << " (new)\n";

    return 0;
}
