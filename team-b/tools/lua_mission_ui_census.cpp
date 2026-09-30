// Missions/UI Lua domain census - DATA side (not disassembly).
// spec-lua-bindings.md is the OTHER team's disassembly-side inventory of
// the engine's C-function-registration/named-hook binding surface for
// Lua. This tool is the independent data-side half: it finds every Lua
// script the game actually SHIPS (real archives under packfiles/pc/cache,
// vpp::Container - include/vpp/container.h/src/container.cpp), parses
// every one with a REAL Lua 5.1 parser (sr3lua - include/sr3lua,
// src/lua_lexer.cpp/src/lua_parser.cpp, built from the public Lua 5.1
// manual grammar), and classifies every GLOBAL name into:
//   - SCRIPT-DEFINED: defined (`function Name()...end` or
//     `Name = function()...end` at bare/global scope) by at least one
//     shipped script.
//   - ENGINE-PROVIDED: called by at least one shipped script, but never
//     defined by ANY shipped script - by construction, this must be a
//     name the C++ engine itself exposes into the Lua global table
//     before/around running these scripts (spec-lua-bindings.md's own
//     target, approached from the opposite, data-only direction).
//
// TWO FULL PASSES, exactly as the census requires (a name defined in
// script A and called from script B must count as script-defined, not
// engine-provided): pass 1 walks every shipped script and collects the
// COMPLETE cross-script set of script-defined global names; pass 2
// classifies every call site in every script against that complete set.
// Never classifies a name as engine-provided from a single file's own
// local view.
//
// Archive walk: identical population-finding method to
// tools/vpp_extract.cpp / tools/prototype_real_shader_draw_multishader.cpp
// - recurse into every nested `.str2_pc` (openNested() for Raw entries;
// for a hypothetical compressed nested container, decompress then
// re-parse as a Container directly - never observed in this game, but
// checked for rather than assumed not to occur), and for EVERY leaf
// entry in the WHOLE archive population check both (a) NAME evidence
// (ends with ".lua", case-insensitive) and (b) CONTENT evidence (first 4
// bytes equal Lua 5.1's own public bytecode magic \x1bLua) - this is
// what makes "this game ships plain text, not bytecode" a measured
// result (see the summary this tool prints) rather than an assumption:
// every one of the ~403,846 real directory entries in the shipped
// packfiles is decompressed (where compressed) and magic-checked, not
// just the ones that already look like scripts by name.
//
// Usage: lua_mission_ui_census <cache_dir> <out_dir>
// Writes, into <out_dir>:
//   engine_provided_names.tsv   - name, total_call_sites, distinct_scripts, prefix_group
//   per_script_usage.tsv        - archive, path_chain, entry_name, engine_names_used (comma list)
//   parse_failures.tsv          - archive, path_chain, entry_name, error message, line, col
//   population_summary.txt      - every denominator this census measured

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
#include <unordered_set>
#include <vector>

#include "sr3lua/parser.h"
#include "vpp/container.h"

namespace fs = std::filesystem;

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

bool magicIsLuaBytecode(const uint8_t* data, size_t len) {
    return len >= 4 && data[0] == 0x1B && data[1] == 'L' && data[2] == 'u' && data[3] == 'a';
}

// One physical shipped Lua entry found by the archive walk.
struct ScriptInstance {
    std::string archive;
    std::string pathChain;   // full nested breadcrumb, e.g. "dlc1_mm_06_modal.str2_pc > dlc1_mm_06.lua"
    std::string entryName;
    std::string kind;        // "Raw" or "Compressed"
    std::string content;     // full decoded text
};

struct WalkStats {
    uint64_t entries = 0;
    uint64_t containersOpened = 0;
    uint64_t rawLeaf = 0;
    uint64_t compressedLeaf = 0;
    uint64_t decompressOk = 0;
    uint64_t decompressFailed = 0;
    uint64_t magicBytecodeHits = 0; // across the WHOLE population, not just name-matched entries
};

struct WalkState {
    std::deque<std::vector<uint8_t>> bufferPool; // keeps decompressed nested-container bytes alive
    std::vector<ScriptInstance> found;
    WalkStats stats;
};

void inspectLeaf(WalkState& st, const std::string& archive, const std::string& chain, const std::string& name,
                  const std::string& kind, const uint8_t* data, size_t len) {
    if (magicIsLuaBytecode(data, len)) st.stats.magicBytecodeHits++;
    std::string lower = toLower(name);
    if (endsWithLuaExt(lower)) {
        ScriptInstance si;
        si.archive = archive;
        si.pathChain = chain;
        si.entryName = name;
        si.kind = kind;
        si.content.assign(reinterpret_cast<const char*>(data), len);
        st.found.push_back(std::move(si));
    }
}

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
                st.stats.rawLeaf++;
                vpp::ByteView raw = c.rawEntryBytes(i);
                inspectLeaf(st, archive, childChain, name, "Raw", raw.data(), raw.size());
            }
        } else {
            st.stats.compressedLeaf++;
            vpp::DecompressResult result = c.decompressEntry(i);
            if (result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch) {
                st.stats.decompressOk++;
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
                            inspectLeaf(st, archive, childChain, name, "Compressed", buf.data(), buf.size());
                        }
                        continue;
                    }
                }
                inspectLeaf(st, archive, childChain, name, "Compressed", result.data.data(), result.data.size());
            } else {
                st.stats.decompressFailed++;
            }
        }
    }
}

std::string prefixGroupOf(const std::string& name) {
    size_t us = name.find('_');
    if (us == std::string::npos || us == 0) return name; // no real prefix - report the whole name as its own group
    return name.substr(0, us);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: lua_mission_ui_census <cache_dir> <out_dir>\n";
        return 1;
    }
    fs::path cacheDir = argv[1];
    fs::path outDir = argv[2];
    fs::create_directories(outDir);

    std::vector<fs::path> archives;
    for (auto& ent : fs::directory_iterator(cacheDir)) {
        if (!ent.is_regular_file()) continue;
        if (toLower(ent.path().extension().string()) == ".vpp_pc") archives.push_back(ent.path());
    }
    std::sort(archives.begin(), archives.end());

    WalkState st;
    uint64_t grandDirBytes = 0;
    for (auto& archivePath : archives) {
        std::string archiveName = archivePath.filename().string();
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(archivePath);
        } catch (const std::exception& ex) {
            std::cerr << "READ FAILED " << archiveName << ": " << ex.what() << "\n";
            continue;
        }
        grandDirBytes += bytes.size();
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walk(st, root, archiveName, "");
        } catch (const std::exception& ex) {
            std::cerr << "PARSE FAILED " << archiveName << ": " << ex.what() << "\n";
        }
    }

    // ---- parse every found instance with the REAL sr3lua parser ----------
    struct Parsed {
        const ScriptInstance* inst;
        sr3lua::ScriptAnalysis analysis;
    };
    std::vector<Parsed> parsed;
    std::vector<std::string> failures; // "archive\tchain\tentry\tmessage\tline\tcol"
    parsed.reserve(st.found.size());
    for (auto& inst : st.found) {
        try {
            sr3lua::ScriptAnalysis a = sr3lua::Parser::analyze(inst.content);
            parsed.push_back(Parsed{&inst, std::move(a)});
        } catch (const sr3lua::LuaSyntaxError& ex) {
            std::ostringstream f;
            f << inst.archive << "\t" << inst.pathChain << "\t" << inst.entryName << "\t" << ex.what() << "\t"
              << ex.line << "\t" << ex.col;
            failures.push_back(f.str());
        }
    }

    // ---- PASS 1: complete cross-script set of script-defined globals -----
    std::unordered_set<std::string> scriptDefined;
    for (auto& p : parsed) {
        for (auto& d : p.analysis.defines) scriptDefined.insert(d.name);
    }

    // ---- PASS 2: classify every call site against the COMPLETE set above -
    struct EngineNameStats {
        uint64_t totalCallSites = 0;
        std::set<std::string> distinctScriptKeys; // archive + "\x1f" + pathChain, i.e. each physical shipped instance
    };
    std::map<std::string, EngineNameStats> engineNames;
    // Per-script usage (only engine-provided names, deduped per script).
    std::map<std::string, std::set<std::string>> perScriptEngineNames; // scriptKey -> set of engine names it calls
    std::map<std::string, std::string> scriptKeyToDisplay; // scriptKey -> "archive\tchain\tentry"
    uint64_t totalCallSitesAll = 0;
    uint64_t scriptDefinedCallSites = 0;

    for (auto& p : parsed) {
        std::string scriptKey = p.inst->archive + "\x1f" + p.inst->pathChain;
        scriptKeyToDisplay[scriptKey] = p.inst->archive + "\t" + p.inst->pathChain + "\t" + p.inst->entryName;
        for (auto& c : p.analysis.calls) {
            totalCallSitesAll++;
            if (scriptDefined.count(c.name)) {
                scriptDefinedCallSites++;
                continue;
            }
            auto& es = engineNames[c.name];
            es.totalCallSites++;
            es.distinctScriptKeys.insert(scriptKey);
            perScriptEngineNames[scriptKey].insert(c.name);
        }
    }

    // ---- content-duplication view (informational) -------------------------
    std::map<std::string, int> contentClusters; // content -> occurrence count
    for (auto& inst : st.found) contentClusters[inst.content]++;
    std::set<std::string> distinctNames;
    for (auto& inst : st.found) distinctNames.insert(inst.entryName);

    // ---- write outputs ------------------------------------------------------
    {
        std::ofstream out(outDir / "engine_provided_names.tsv");
        out << "name\ttotal_call_sites\tdistinct_scripts\tprefix_group\n";
        std::vector<std::pair<std::string, EngineNameStats>> rows(engineNames.begin(), engineNames.end());
        std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) {
            if (a.second.totalCallSites != b.second.totalCallSites) return a.second.totalCallSites > b.second.totalCallSites;
            return a.first < b.first;
        });
        for (auto& [name, es] : rows) {
            out << name << "\t" << es.totalCallSites << "\t" << es.distinctScriptKeys.size() << "\t"
                << prefixGroupOf(name) << "\n";
        }
    }
    {
        std::ofstream out(outDir / "prefix_groups.tsv");
        out << "prefix\tdistinct_engine_names\ttotal_call_sites\n";
        std::map<std::string, std::pair<int, uint64_t>> groups; // prefix -> (distinct name count, total calls)
        for (auto& [name, es] : engineNames) {
            auto& g = groups[prefixGroupOf(name)];
            g.first++;
            g.second += es.totalCallSites;
        }
        std::vector<std::pair<std::string, std::pair<int, uint64_t>>> rows(groups.begin(), groups.end());
        std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.second.first > b.second.first; });
        for (auto& [prefix, g] : rows) out << prefix << "\t" << g.first << "\t" << g.second << "\n";
    }
    {
        std::ofstream out(outDir / "per_script_usage.tsv");
        out << "archive\tpath_chain\tentry_name\tdistinct_engine_names_used\tengine_names_used\n";
        for (auto& [key, names] : perScriptEngineNames) {
            std::string display = scriptKeyToDisplay[key];
            std::ostringstream joined;
            bool first = true;
            for (auto& n : names) {
                if (!first) joined << ",";
                joined << n;
                first = false;
            }
            out << display << "\t" << names.size() << "\t" << joined.str() << "\n";
        }
    }
    {
        std::ofstream out(outDir / "parse_failures.tsv");
        out << "archive\tpath_chain\tentry_name\tmessage\tline\tcol\n";
        for (auto& f : failures) out << f << "\n";
    }
    {
        std::ofstream out(outDir / "script_defined_names.tsv");
        out << "name\n";
        std::vector<std::string> names(scriptDefined.begin(), scriptDefined.end());
        std::sort(names.begin(), names.end());
        for (auto& n : names) out << n << "\n";
    }
    {
        std::ofstream out(outDir / "population_summary.txt");
        auto line = [&](const std::string& s) { out << s << "\n"; std::cout << s << "\n"; };
        line("=== Archive/container population ===");
        line("archives_scanned=" + std::to_string(archives.size()));
        line("total_bytes_read=" + std::to_string(grandDirBytes));
        line("total_directory_entries=" + std::to_string(st.stats.entries));
        line("total_containers_opened(incl top-level+nested)=" + std::to_string(st.stats.containersOpened));
        line("total_raw_leaf_entries=" + std::to_string(st.stats.rawLeaf));
        line("total_compressed_leaf_entries=" + std::to_string(st.stats.compressedLeaf));
        line("total_decompress_ok=" + std::to_string(st.stats.decompressOk));
        line("total_decompress_failed=" + std::to_string(st.stats.decompressFailed));
        line("total_leaf_entries_checked_for_lua_bytecode_magic=" +
             std::to_string(st.stats.rawLeaf + st.stats.decompressOk));
        line("lua_bytecode_magic_hits(WHOLE population, any name)=" + std::to_string(st.stats.magicBytecodeHits));
        line("");
        line("=== Shipped .lua script population ===");
        line("physical_lua_entries_found(name ends '.lua')=" + std::to_string(st.found.size()));
        line("distinct_entry_names=" + std::to_string(distinctNames.size()));
        line("distinct_content_clusters=" + std::to_string(contentClusters.size()));
        line("parsed_clean=" + std::to_string(parsed.size()));
        line("parse_failed=" + std::to_string(failures.size()));
        line("");
        line("=== Global name classification (two-pass) ===");
        line("total_bare_global_call_sites=" + std::to_string(totalCallSitesAll));
        line("call_sites_to_script_defined_names=" + std::to_string(scriptDefinedCallSites));
        line("call_sites_to_engine_provided_names=" +
             std::to_string(totalCallSitesAll - scriptDefinedCallSites));
        line("distinct_script_defined_names=" + std::to_string(scriptDefined.size()));
        line("distinct_engine_provided_names=" + std::to_string(engineNames.size()));
    }

    return 0;
}
