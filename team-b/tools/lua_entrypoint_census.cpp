// Per-script entry-point naming census, requested by the orchestrator
// 2026-09-29 as a real, data-driven way to find how missions/UI screens
// are entered, given spec-lua-bindings.md's own findings that no
// dedicated mission_start/end hook exists anywhere in the engine-side
// census (Sec12.8/Sec14) - but the spec does document templated
// "%s_init"/"%s_cleanup" hooks (Sec8.4), suggesting scripts may be
// entered BY NAME, using their own filename as a prefix.
//
// Method: for each of the 804 real shipped .lua scripts, parse it with
// sr3lua::Parser::analyze() (the real, clean-room, scope-aware Lua 5.1
// parser already validated against this exact population, HANDOFF
// Sec9.108), and for every GlobalDefine that is BOTH top-level
// (isTopLevel == true - a new, additive parser field added for this
// task, regression-tested against sr3lua's own synthetic suite before
// use) AND whose name starts with "<scriptStem>_" (the script's own
// filename, stem only, case-sensitive exact prefix match), record the
// suffix (the name with that literal prefix stripped). Histograms the
// suffixes pooled across all scripts, and reports, per script, which
// suffixes it defines.
//
// Archive-walking method: same recursive walk as every other .lua census
// tool this project already has (tools/lua_mission_ui_census.cpp,
// tools/lua_host_run.cpp) - duplicated with attribution rather than
// shared, matching those tools own stated convention.
//
// Usage: lua_entrypoint_census <cache_dir> <out_dir>

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
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

struct ScriptInstance {
    std::string archive;
    std::string pathChain;
    std::string entryName;
    std::string stem; // entryName with the trailing ".lua" removed, exact case
    std::string content;
};

struct WalkState {
    std::deque<std::vector<uint8_t>> bufferPool;
    std::vector<ScriptInstance> found;
};

void inspectLeaf(WalkState& st, const std::string& archive, const std::string& chain, const std::string& name,
                  const uint8_t* data, size_t len) {
    std::string lower = toLower(name);
    if (endsWithLuaExt(lower)) {
        ScriptInstance si;
        si.archive = archive;
        si.pathChain = chain;
        si.entryName = name;
        si.stem = name.substr(0, name.size() - 4); // strip ".lua", exact case preserved
        si.content.assign(reinterpret_cast<const char*>(data), len);
        st.found.push_back(std::move(si));
    }
}

void walk(WalkState& st, const vpp::Container& c, const std::string& archive, const std::string& chain) {
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
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: lua_entrypoint_census <cache_dir> <out_dir>\n";
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
    for (auto& archivePath : archives) {
        std::string archiveName = archivePath.filename().string();
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(archivePath);
        } catch (const std::exception& ex) {
            std::cerr << "READ FAILED " << archiveName << ": " << ex.what() << "\n";
            continue;
        }
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walk(st, root, archiveName, "");
        } catch (const std::exception& ex) {
            std::cerr << "PARSE FAILED " << archiveName << ": " << ex.what() << "\n";
        }
    }
    std::cout << "Archive walk complete: " << archives.size() << " archives, " << st.found.size()
              << " real .lua entries found (population denominator).\n";

    // --- Per-script scan: every top-level GlobalDefine whose name starts
    // with "<stem>_", suffix = name with that prefix stripped. -----------
    std::ofstream perScript(outDir / "entrypoint_per_script.tsv");
    perScript << "archive\tpath_chain\tentry_name\tstem\tsuffix\tfull_name\tline\tkind\n";

    std::map<std::string, uint64_t> suffixHist; // suffix -> pooled count across all scripts
    std::map<std::string, uint64_t> scriptsDefiningAtLeastOne; // suffix -> distinct script count
    uint64_t totalMatches = 0;
    uint64_t scriptsWithAtLeastOneMatch = 0;
    uint64_t parseFailures = 0;

    for (const auto& s : st.found) {
        std::string prefix = s.stem + "_";
        bool thisScriptMatched = false;
        std::vector<std::string> suffixesThisScript; // for the distinct-script-count pass below

        try {
            sr3lua::ScriptAnalysis analysis = sr3lua::Parser::analyze(s.content);
            for (const auto& def : analysis.defines) {
                if (!def.isTopLevel) continue;
                if (def.name.size() <= prefix.size()) continue;
                if (def.name.compare(0, prefix.size(), prefix) != 0) continue;
                std::string suffix = def.name.substr(prefix.size());
                ++totalMatches;
                suffixHist[suffix]++;
                thisScriptMatched = true;
                suffixesThisScript.push_back(suffix);
                perScript << s.archive << "\t" << s.pathChain << "\t" << s.entryName << "\t" << s.stem
                          << "\t" << suffix << "\t" << def.name << "\t" << def.line << "\t"
                          << (def.kind == sr3lua::DefineKind::FunctionStatement ? "FunctionStatement" : "AssignFunction")
                          << "\n";
            }
        } catch (const sr3lua::LuaSyntaxError&) {
            ++parseFailures; // real shipped scripts are 804/804 parse-clean per Sec9.108 - should be 0
            continue;
        }

        if (thisScriptMatched) ++scriptsWithAtLeastOneMatch;
        // Dedupe per-script before the distinct-script-count histogram -
        // a script defining the SAME suffix twice (unusual, but do not
        // silently double count "distinct scripts" for it).
        std::sort(suffixesThisScript.begin(), suffixesThisScript.end());
        suffixesThisScript.erase(std::unique(suffixesThisScript.begin(), suffixesThisScript.end()),
                                  suffixesThisScript.end());
        for (const auto& suf : suffixesThisScript) scriptsDefiningAtLeastOne[suf]++;
    }
    perScript.close();

    std::ofstream hist(outDir / "entrypoint_suffix_histogram.tsv");
    hist << "suffix\tpooled_call_count\tdistinct_scripts_defining_it\n";
    std::vector<std::pair<std::string, uint64_t>> histRows(suffixHist.begin(), suffixHist.end());
    std::sort(histRows.begin(), histRows.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    for (auto& kv : histRows) {
        hist << kv.first << "\t" << kv.second << "\t" << scriptsDefiningAtLeastOne[kv.first] << "\n";
    }
    hist.close();

    std::cout << "\n=== POPULATION SUMMARY (lua_entrypoint_census) ===\n";
    std::cout << "real_lua_scripts_found=" << st.found.size() << "\n";
    std::cout << "sr3lua_parse_failures=" << parseFailures << " (expect 0, per HANDOFF Sec9.108 own "
                 "804/804 clean-parse population)\n";
    std::cout << "scripts_with_at_least_one_top_level_stem_suffix_match=" << scriptsWithAtLeastOneMatch
              << " / " << st.found.size() << "\n";
    std::cout << "total_top_level_stem_suffix_matches_pooled=" << totalMatches << "\n";
    std::cout << "distinct_suffixes_found=" << histRows.size() << "\n";

    std::cout << "\n=== TOP 30 SUFFIXES BY POOLED COUNT ===\n";
    for (size_t i = 0; i < histRows.size() && i < 30; ++i) {
        std::cout << "  " << (i + 1) << ". " << histRows[i].first << " pooled_count=" << histRows[i].second
                  << " distinct_scripts=" << scriptsDefiningAtLeastOne[histRows[i].first] << "\n";
    }

    std::cout << "\nWrote " << (outDir / "entrypoint_per_script.tsv").string() << ", "
              << (outDir / "entrypoint_suffix_histogram.tsv").string() << "\n";
    return 0;
}
