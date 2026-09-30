// Clean-side check requested by the orchestrator 2026-09-29, part 2: do
// any real shipped Lua scripts build entry-point names DYNAMICALLY (e.g.
// _G[x .. "_start"], loadstring, rawget(_G, ...), string concatenation
// building a "_start"/"_run"/"_success"/"_init"/"_cleanup" suffix) rather
// than the engine calling a statically-named hook? A raw pattern search
// over every real script's own text, not a full parse - these patterns
// are looked for as plain substrings/regex-free needles, deliberately
// simple and auditable.
//
// Usage: lua_dynamic_dispatch_grep <cache_dir> <out_dir>

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

bool endsWithExt(const std::string& lowerName, const std::string& ext) {
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

struct ScriptInstance {
    std::string archive;
    std::string pathChain;
    std::string entryName;
    std::string content;
};

struct WalkState {
    std::deque<std::vector<uint8_t>> bufferPool;
    std::vector<ScriptInstance> found;
};

void inspectLeaf(WalkState& st, const std::string& archive, const std::string& chain, const std::string& name,
                  const uint8_t* data, size_t len) {
    std::string lower = toLower(name);
    if (endsWithExt(lower, ".lua")) {
        ScriptInstance si;
        si.archive = archive;
        si.pathChain = chain;
        si.entryName = name;
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
        std::cerr << "usage: lua_dynamic_dispatch_grep <cache_dir> <out_dir>\n";
        return 1;
    }
    fs::path cacheDir = argv[1];
    fs::path outDir = argv[2];
    fs::create_directories(outDir);

    std::vector<std::string> needles = {
        "_G[", "_G.", "rawget(_G", "rawget( _G", "loadstring", "getfenv", "setfenv",
        "\"_start\"", "\"_run\"", "\"_success\"", "\"_init\"", "\"_cleanup\"",
        "'_start'", "'_run'", "'_success'", "'_init'", "'_cleanup'"
    };

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
              << " real .lua entries found (population denominator).\n";

    std::ofstream matchesOut(outDir / "dynamic_dispatch_matches.tsv");
    matchesOut << "archive\tpath_chain\tentry_name\tneedle\tcontext\n";
    uint64_t totalMatches = 0;
    for (const auto& s : st.found) {
        for (const auto& needle : needles) {
            size_t pos = 0;
            while ((pos = s.content.find(needle, pos)) != std::string::npos) {
                size_t ctxStart = (pos > 40) ? pos - 40 : 0;
                size_t ctxLen = std::min<size_t>(needle.size() + 100, s.content.size() - ctxStart);
                std::string context = s.content.substr(ctxStart, ctxLen);
                for (char& c : context) {
                    if (c == '\t') c = ' ';
                    if (c == '\n' || c == '\r') c = ' ';
                }
                matchesOut << s.archive << "\t" << s.pathChain << "\t" << s.entryName << "\t" << needle
                           << "\t" << context << "\n";
                ++totalMatches;
                pos += needle.size();
            }
        }
    }
    matchesOut.close();

    std::cout << "real_lua_scripts_scanned=" << st.found.size() << "\n";
    std::cout << "total_matches=" << totalMatches << "\n";
    std::cout << "Wrote " << (outDir / "dynamic_dispatch_matches.tsv").string() << "\n";
    return 0;
}
