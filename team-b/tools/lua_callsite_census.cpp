// Call-SITE census (not definitions), requested by the orchestrator
// 2026-09-29 as a follow-up to Sec9.118/Sec9.119: neither Team A's
// sprintf/strcat exhaustive negative (spec-lua-bindings.md Sec14.7) nor
// this project's own Lua-dynamic-dispatch negative (Sec9.119) covers
// ordinary, statically-named direct calls. For every real top-level
// "<scriptStem>_<suffix>" function (suffix in start/run/success/init/
// cleanup - the 5 suffixes named in the task), find every real call site
// to that EXACT name anywhere across all 804 real scripts (sr3lua::
// Parser::analyze()'s own ScriptAnalysis::calls - untouched by the
// isTopLevel addition), and classify: called from the SAME script that
// defines it, called from a DIFFERENT script, or never called anywhere
// in this static population.
//
// Usage: lua_callsite_census <cache_dir> <out_dir>

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
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
    std::string stem;
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
        si.stem = name.substr(0, name.size() - 4);
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

bool hasSuffix(const std::string& name, const std::string& stem, const std::string& suffix, std::string& matchedFull) {
    std::string full = stem + "_" + suffix;
    if (name == full) { matchedFull = full; return true; }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: lua_callsite_census <cache_dir> <out_dir>\n";
        return 1;
    }
    fs::path cacheDir = argv[1];
    fs::path outDir = argv[2];
    fs::create_directories(outDir);

    const std::vector<std::string> kSuffixes = {"start", "run", "success", "init", "cleanup"};

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
        } catch (const std::exception&) {
            continue;
        }
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walk(st, root, archiveName, "");
        } catch (const std::exception&) {
        }
    }
    std::cout << "Archive walk complete: " << archives.size() << " archives, " << st.found.size()
              << " real .lua entries found.\n";

    // --- Parse every script once, collect defines (top-level, matching
    // our 5 suffixes) and ALL call sites (name -> script indices that
    // call it). ------------------------------------------------------
    struct DefInfo {
        std::string definingScript; // entryName of the script that defines it
        std::string definingStem;
        std::string suffix;
        int line = 0;
    };
    std::map<std::string, DefInfo> definitions; // full name -> where defined
    std::map<std::string, std::set<std::string>> callers; // full name -> set of calling script entryNames
    uint64_t parseFailures = 0;

    for (const auto& s : st.found) {
        sr3lua::ScriptAnalysis analysis;
        try {
            analysis = sr3lua::Parser::analyze(s.content);
        } catch (const sr3lua::LuaSyntaxError&) {
            ++parseFailures;
            continue;
        }

        for (const auto& def : analysis.defines) {
            if (!def.isTopLevel) continue;
            for (const auto& suf : kSuffixes) {
                std::string full = s.stem + "_" + suf;
                if (def.name == full) {
                    if (definitions.find(full) == definitions.end()) {
                        definitions[full] = DefInfo{s.entryName, s.stem, suf, def.line};
                    }
                    break;
                }
            }
        }

        // Record every call site whose NAME matches ANY "<anyStem>_<suffix>"
        // pattern for our 5 suffixes - not just calls to names this
        // SAME script defines. A call can name another script's function.
        for (const auto& call : analysis.calls) {
            for (const auto& suf : kSuffixes) {
                std::string needle = "_" + suf;
                if (call.name.size() > needle.size() &&
                    call.name.compare(call.name.size() - needle.size(), needle.size(), needle) == 0) {
                    callers[call.name].insert(s.entryName);
                    break;
                }
            }
        }
    }

    // --- Classify each real definition. -------------------------------
    std::ofstream out(outDir / "callsite_classification.tsv");
    out << "full_name\tdefining_script\tsuffix\tdefine_line\tcalled_at_all\tcalled_by_same_script\t"
           "called_by_other_script\tcalling_scripts\n";

    uint64_t totalDefs = 0, calledBySame = 0, calledByOther = 0, neverCalled = 0;

    for (const auto& kv : definitions) {
        const std::string& full = kv.first;
        const DefInfo& def = kv.second;
        ++totalDefs;

        auto it = callers.find(full);
        bool calledAtAll = (it != callers.end());
        bool bySame = false, byOther = false;
        std::string callingList;
        if (calledAtAll) {
            for (const auto& caller : it->second) {
                if (!callingList.empty()) callingList += ",";
                callingList += caller;
                if (caller == def.definingScript) bySame = true;
                else byOther = true;
            }
        }
        if (bySame) ++calledBySame;
        if (byOther) ++calledByOther;
        if (!calledAtAll) ++neverCalled;

        out << full << "\t" << def.definingScript << "\t" << def.suffix << "\t" << def.line << "\t"
            << (calledAtAll ? 1 : 0) << "\t" << (bySame ? 1 : 0) << "\t" << (byOther ? 1 : 0) << "\t"
            << callingList << "\n";
    }
    out.close();

    std::cout << "\n=== POPULATION SUMMARY (lua_callsite_census) ===\n";
    std::cout << "real_lua_scripts_found=" << st.found.size() << "\n";
    std::cout << "sr3lua_parse_failures=" << parseFailures << "\n";
    std::cout << "total_top_level_defs_for_5_suffixes=" << totalDefs << "\n";
    std::cout << "called_by_SAME_script(count, may overlap with other)=" << calledBySame << "\n";
    std::cout << "called_by_OTHER_script(count, may overlap with same)=" << calledByOther << "\n";
    std::cout << "never_called_anywhere_statically=" << neverCalled << "\n";
    std::cout << "Wrote " << (outDir / "callsite_classification.tsv").string() << "\n";
    return 0;
}
