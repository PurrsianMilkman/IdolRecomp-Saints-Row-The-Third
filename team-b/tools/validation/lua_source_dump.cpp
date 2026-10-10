// REPORT-ONLY diagnostic harness (2026-10-07) - static cross-reference tool
// support. Dumps the raw TEXT of every shipped Lua script this project's
// own mission-drive harness (tools/lua_host_run.cpp) and data census
// (tools/lua_mission_ui_census.cpp) already discover, one real file per
// shipped script, into a plain directory on disk so an off-the-shelf
// static linter (luacheck, MIT-licensed, run separately - see
// results/lua_undefined_globals_ranked_20261007.md) can scan them without
// needing to link against this project's own vpp/lua libraries itself.
//
// NOT part of the build (tools/validation/README.md's own convention -
// "standalone diagnostics, deliberately not wired into CMakeLists.txt");
// this file is never add_executable()'d. Build it directly from source
// with the Developer Command Prompt, no stale build_verify\*.obj needed:
//
//   cl.exe /nologo /std:c++17 /EHsc /O2 /I include /I third_party\zlib ^
//     tools\validation\lua_source_dump.cpp ^
//     src\byte_view.cpp src\format.cpp src\hash.cpp src\payload_locator.cpp ^
//     src\container.cpp src\content_validation.cpp ^
//     third_party\zlib\adler32.c third_party\zlib\compress.c third_party\zlib\crc32.c ^
//     third_party\zlib\deflate.c third_party\zlib\gzclose.c third_party\zlib\gzlib.c ^
//     third_party\zlib\gzread.c third_party\zlib\gzwrite.c third_party\zlib\inflate.c ^
//     third_party\zlib\infback.c third_party\zlib\inftrees.c third_party\zlib\inffast.c ^
//     third_party\zlib\trees.c third_party\zlib\uncompr.c third_party\zlib\zutil.c ^
//     /Fe lua_source_dump.exe
//
// (zconf.h must exist next to zlib.h first - this project's own CMakeLists.txt
// generates it from third_party/zlib/zconf.h.cmakein via configure_file(@ONLY);
// that template has no @VAR@ substitutions left in it (checked directly - the
// only "@" in the file is the literal old SCCS tag "@(#) $Id$"), so a plain
// copy to third_party/zlib/zconf.h is equivalent and was used here instead of
// invoking cmake for this one header.)
//
// Archive walk: the SAME recursive method tools/lua_host_run.cpp and
// tools/lua_mission_ui_census.cpp each already duplicate locally (per this
// codebase's established convention, stated explicitly in lua_host_run.cpp's
// own top comment - a small walk() is copied per tool, not shared) - top-level
// *.vpp_pc files directly under <cache_dir>, recursing into nested .str2_pc
// containers via openNested() for Raw entries or re-parsing decompressed
// bytes that start with the container magic 0x51890ACE. Leaf test: entry
// name ends with ".lua" (case-insensitive) - the same test both of those
// tools use, so this dump's population is the same 804-script population
// the mission-drive harness's own top comment documents (subject to
// whatever this run's cache_dir actually contains - never hardcoded here).
//
// Usage: lua_source_dump <cache_dir> <out_dir>
// Writes into <out_dir>:
//   scripts/<NNNN>.lua         - one real file per shipped script found,
//                                raw bytes unchanged, named only by a
//                                sequential index (the real entry/archive
//                                name is NOT reused as the filename since
//                                duplicate stems across different archives
//                                and nested containers are real and
//                                observed - see manifest.tsv for identity).
//   manifest.tsv                - index, archive, path_chain, entry_name,
//                                byte_length - the ONLY place that maps a
//                                dumped file back to its real shipped
//                                identity; a consumer (e.g. a luacheck
//                                post-processing script) joins on `index`.
//   population_summary.txt      - real denominators measured this run.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

bool endsWithLuaExt(const std::string& lowerName) {
    static const std::string ext = ".lua";
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

std::string sanitizeTsv(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c == '\t') c = ' ';
        if (c == '\n' || c == '\r') c = ' ';
    }
    return out;
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
    if (!endsWithLuaExt(lower)) return;
    ScriptInstance si;
    si.archive = archive;
    si.pathChain = chain;
    si.entryName = name;
    si.content.assign(reinterpret_cast<const char*>(data), len);
    st.found.push_back(std::move(si));
}

// Duplicated, deliberately, from tools/lua_host_run.cpp's own walk() - see
// this file's top comment for why (established project convention).
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: lua_source_dump <cache_dir> <out_dir>\n";
        return 1;
    }
    fs::path cacheDir = argv[1];
    fs::path outDir = argv[2];
    fs::create_directories(outDir);
    fs::path scriptsDir = outDir / "scripts";
    fs::create_directories(scriptsDir);

    std::vector<fs::path> archives;
    for (auto& ent : fs::directory_iterator(cacheDir)) {
        if (!ent.is_regular_file()) continue;
        if (toLower(ent.path().extension().string()) == ".vpp_pc") archives.push_back(ent.path());
    }
    std::sort(archives.begin(), archives.end());

    WalkState st;
    uint64_t archivesOpened = 0, archiveOpenFailed = 0;
    for (auto& archivePath : archives) {
        std::string archiveName = archivePath.filename().string();
        try {
            std::vector<uint8_t> bytes = readFile(archivePath);
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            ++archivesOpened;
            walk(st, root, archiveName, "");
        } catch (const std::exception& ex) {
            ++archiveOpenFailed;
            std::cerr << "[warn] could not open " << archiveName << ": " << ex.what() << "\n";
        }
    }

    std::ofstream manifest(outDir / "manifest.tsv");
    manifest << "index\tarchive\tpath_chain\tentry_name\tbyte_length\n";
    uint64_t idx = 0;
    for (const auto& si : st.found) {
        std::string numStr = std::to_string(idx);
        while (numStr.size() < 5) numStr = "0" + numStr;
        fs::path outFile = scriptsDir / (numStr + ".lua");
        std::ofstream f(outFile, std::ios::binary);
        f.write(si.content.data(), static_cast<std::streamsize>(si.content.size()));
        f.close();
        manifest << idx << "\t" << sanitizeTsv(si.archive) << "\t" << sanitizeTsv(si.pathChain) << "\t"
                 << sanitizeTsv(si.entryName) << "\t" << si.content.size() << "\n";
        ++idx;
    }
    manifest.close();

    std::ofstream summary(outDir / "population_summary.txt");
    auto printBoth = [&](const std::string& line) {
        std::cout << line << "\n";
        summary << line << "\n";
    };
    printBoth("cache_dir=" + cacheDir.string());
    printBoth("archives_found=" + std::to_string(archives.size()));
    printBoth("archives_opened=" + std::to_string(archivesOpened));
    printBoth("archives_open_failed=" + std::to_string(archiveOpenFailed));
    printBoth("containers_opened=" + std::to_string(st.stats.containersOpened));
    printBoth("entries_seen=" + std::to_string(st.stats.entries));
    printBoth("decompress_failed=" + std::to_string(st.stats.decompressFailed));
    printBoth("lua_scripts_found=" + std::to_string(st.found.size()));
    printBoth("lua_scripts_written=" + std::to_string(idx));

    return 0;
}
