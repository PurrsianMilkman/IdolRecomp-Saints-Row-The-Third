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
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3asm/manifest.h"
#include "sr3lua/parser.h"
#include "sr3luahost/host.h"
#include "sr3luahost/hook_registry.h"
#include "vpp/container.h"

namespace fs = std::filesystem;
using sr3luahost::bareGlobalRoster;
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

struct WalkState {
    std::deque<std::vector<uint8_t>> bufferPool;
    std::vector<ScriptInstance> found;
    WalkStats stats;
};

void inspectLeaf(WalkState& st, const std::string& archive, const std::string& chain, const std::string& name,
                  const uint8_t* data, size_t len) {
    std::string lower = toLower(name);
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
void missionWatchdogHook(lua_State* L, lua_Debug*) {
    luaL_error(L, "lua_host_run mission-drive watchdog: call exceeded the %d-instruction budget "
                  "(likely an infinite loop in never-before-executed mission code) - aborted by "
                  "this tool's own safety hook, not hung",
               kMissionWatchdogInstructionBudget);
}

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
    bool callOk = false;
    std::string callError;
};

// checkpoint/isRestart are CHOSEN literal values (0 / false), per this
// task's own instruction - spec-lua-bindings.md Sec14.10/HANDOFF.md
// Sec9.123's own real, confirmed evidence for the call shape
// (`m01_start(m01_checkpoint, is_restart)` calls `m01_run(...)`
// internally) never pinned down a real, confirmed checkpoint VALUE to
// use - this is a documented engineering choice, not recovered data.
MissionStartResult callMissionStart(lua_State* L, const std::string& funcName, double checkpoint,
                                     bool isRestart) {
    MissionStartResult r;
    lua_getglobal(L, funcName.c_str());
    r.existedAsFunction = (lua_type(L, -1) == LUA_TFUNCTION);
    if (!r.existedAsFunction) {
        lua_pop(L, 1);
        return r;
    }
    lua_pop(L, 1);
    lua_getglobal(L, funcName.c_str()); // fresh reference, same convention as fireConfirmedHooks
    lua_pushnumber(L, checkpoint);
    lua_pushboolean(L, isRestart ? 1 : 0);
    r.attemptedCall = true;
    lua_sethook(L, missionWatchdogHook, LUA_MASKCOUNT, kMissionWatchdogInstructionBudget);
    int callStatus = lua_pcall(L, 2, LUA_MULTRET, 0);
    lua_sethook(L, nullptr, 0, 0);
    if (callStatus != 0) {
        const char* msg = lua_tostring(L, -1);
        r.callOk = false;
        r.callError = msg ? msg : "(no error message)";
        lua_pop(L, 1);
    } else {
        r.callOk = true;
    }
    lua_settop(L, 0); // clear any return values, same convention as runChunk()/fireConfirmedHooks
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
                     "      game's. Recorded as host_rng_option= in verdict_summary.txt.\n";
        return 1;
    }
    // Optional flags anywhere after the 3 positionals; the remaining
    // positional (if any) is max_scripts_to_run.
    bool preloadStatesSpec164 = true; // Sec16.1/Sec16.4, CONFIRMED and cleared 2026-10-01
    bool hostRng = false;      // --host-rng: only when explicitly given
    uint64_t hostRngSeed = 1;
    std::vector<std::string> extraPositional;
    for (int a = 4; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--preload-states=spec16.4" || arg == "--preload-states=spec16.4-highconf") preloadStatesSpec164 = true;
        else if (arg == "--preload-states=tag") preloadStatesSpec164 = false;
        else if (arg == "--host-rng") hostRng = true;
        else if (arg.rfind("--host-rng=", 0) == 0) {
            std::string v = arg.substr(11);
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) {
                std::cerr << "bad --host-rng seed: " << v << "\n";
                return 1;
            }
            hostRng = true;
            hostRngSeed = std::stoull(v);
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "unknown option: " << arg << "\n";
            return 1;
        } else extraPositional.push_back(arg);
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
    // `<stem>_start(0, false)` (CHOSEN, documented args - see
    // callMissionStart()'s own doc comment), watchdog-wrapped. Then tick
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
    // measured (not assumed) idempotence signal consistent with this
    // pass's own no-coroutine-resume ThreadScheduler model. Otherwise:
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

    constexpr int kMissionTickBudget = 20; // CHOSEN - see this block's own top comment for the reasoning
    constexpr double kMissionStartCheckpoint = 0.0; // CHOSEN literal, not recovered
    constexpr bool kMissionStartIsRestart = false;  // CHOSEN literal, not recovered
    // Screen fade host clock (batch 2026-10-01, spec-lua-api-behaviour.md
    // Sec26.24): each mission tick is one host frame of this many ms
    // (EngineState::screenFadeHostFrame). CHOSEN: about a third of a second,
    // and not a divisor of the engine's 1000/1500/3000/6000 ms stamp offsets,
    // so a stamp never lands exactly on a frame (that comparison is OPEN).
    constexpr int64_t kFadeHostMsPerTick = 333;

    struct MissionResult {
        std::string stem, entryName, containerName;
        bool scriptFound = false;
        bool loadOk = false, pcallOk = false;
        std::string loadError, pcallError;
        bool startExisted = false, startAttempted = false, startCallOk = false;
        std::string startCallError;
        int ticksSurvived = 0;
        std::string stopReason;
        std::string firstErrorMessage;
        std::string firstUnimplementedStub;
    };
    std::vector<MissionResult> missionResults;
    missionResults.reserve(missions.size());

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

        std::string funcName = row.missionLuaStem + "_start";
        MissionStartResult startResult = callMissionStart(host.gameplayState(), funcName, kMissionStartCheckpoint,
                                                            kMissionStartIsRestart);
        mr.startExisted = startResult.existedAsFunction;
        mr.startAttempted = startResult.attemptedCall;
        mr.startCallOk = startResult.callOk;
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
            for (int tick = 1; tick <= kMissionTickBudget; ++tick) {
                host.engineState().screenFadeHostFrame(kFadeHostMsPerTick);
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
                if (havePrevSig && sig.existed == prevSig.existed && sig.ok == prevSig.ok && sig.err == prevSig.err) {
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
        }

        if (!errorsInOrder.empty()) mr.firstErrorMessage = errorsInOrder.front();
        for (auto& e : errorsInOrder) {
            std::string name = extractNilGlobalName(e);
            if (!name.empty()) { mr.firstUnimplementedStub = name; break; }
        }

        missionResults.push_back(std::move(mr));
        if (missionResults.size() % 10 == 0 || missionResults.size() == missions.size()) {
            std::cout << "  ... " << missionResults.size() << "/" << missions.size() << " missions driven\n";
        }
    }

    std::ofstream missionOut(outDir / "verdict_mission_drive.tsv");
    missionOut << "stem\tentry_name\tcontainer_name\tscript_found\tload_ok\tload_error\tpcall_ok\tpcall_error\t"
                  "start_func_name\tstart_existed\tstart_attempted\tstart_call_ok\tstart_call_error\t"
                  "ticks_survived\tstop_reason\tfirst_error_message\tfirst_unimplemented_stub\n";
    uint64_t missionsFoundCount = 0, missionsStartExistedCount = 0, missionsStartOkCount = 0;
    uint64_t stopBudget = 0, stopError = 0, stopCompletion = 0;
    uint64_t ticksSum = 0;
    for (auto& mr : missionResults) {
        if (mr.scriptFound) missionsFoundCount++;
        if (mr.startExisted) missionsStartExistedCount++;
        if (mr.startCallOk) missionsStartOkCount++;
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
                   << sanitizeTsv(mr.firstErrorMessage) << "\t" << sanitizeTsv(mr.firstUnimplementedStub) << "\n";
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
    mline("CHOSEN_values(not recovered data, stated per this project's own discipline): mission_tick_budget=" +
          std::to_string(kMissionTickBudget) + ", start_checkpoint_arg=" + std::to_string(kMissionStartCheckpoint) +
          ", start_is_restart_arg=" + std::string(kMissionStartIsRestart ? "true" : "false") +
          ", watchdog_instruction_budget=" + std::to_string(kMissionWatchdogInstructionBudget) +
          ", fade_host_ms_per_tick=" + std::to_string(kFadeHostMsPerTick) +
          " (same constant as host.h's own kHookWatchdogInstructionBudget, not independently chosen)");
    mline("missions_with_script_found=" + std::to_string(missionsFoundCount) + "/" + std::to_string(missions.size()));
    mline("missions_with_start_existed=" + std::to_string(missionsStartExistedCount) + "/" + std::to_string(missions.size()));
    mline("missions_with_start_call_ok=" + std::to_string(missionsStartOkCount) + "/" + std::to_string(missions.size()));
    mline("stop_reason_histogram: budget_exhaustion=" + std::to_string(stopBudget) + " error=" +
          std::to_string(stopError) + " completion=" + std::to_string(stopCompletion) + " (sum should equal "
          "missions_with_script_found, since a not-found script gets its own separate \"script not found\" "
          "stop reason, counted under neither bucket above)");
    mline("total_ticks_survived_across_all_missions=" + std::to_string(ticksSum));
    mline("total_stub_calls_this_step_contributed(ALL-INCLUSIVE minus pre-step-1 snapshot, real HitLog delta)=" +
          std::to_string(missionIncrementalTotal));
    mline("zscene_is_loaded_pending_promotion_refusals(whole run, Sec26.25: promotion 0x00720410 OPEN)=" +
          std::to_string(host.engineState().zscenePendingPromotionRefusals()));
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
                << " ticks_survived=" << mr.ticksSurvived << " stop_reason=[" << mr.stopReason << "]";
            if (!mr.firstErrorMessage.empty()) row << " first_error=[" << mr.firstErrorMessage << "]";
            if (!mr.firstUnimplementedStub.empty()) row << " first_unimplemented_stub=" << mr.firstUnimplementedStub;
        }
        mline(row.str());
    }
    missionSummary.close();

    std::cout << "\nWrote " << (outDir / "verdict_mission_drive.tsv").string() << " (new), "
              << (outDir / "verdict_stub_hits_with_missions.tsv").string() << " (new), "
              << (outDir / "verdict_mission_drive_summary.txt").string() << " (new)\n";

    return 0;
}
