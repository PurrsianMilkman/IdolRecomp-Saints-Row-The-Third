// Clean-side check requested by the orchestrator 2026-09-29: do any real
// .xtbl table rows name mission/UI script stems or their real
// <stem>_start/_run/_success entry-point function names (found by
// tools/lua_entrypoint_census.cpp, HANDOFF Sec9.118)? A raw string search
// over every real, decompressed .xtbl file's own text (xtbl is a plain
// XML tree format, spec-tables-*.md Sec1) - not a "typed reader" search,
// so it is not limited to the handful of domains already covered by this
// project's own typed xtbl readers; it covers every real .xtbl file found
// in the real archive cache, exhaustively.
//
// Usage: xtbl_name_grep <cache_dir> <needles_file> <out_dir>
//   needles_file: one plain string per line (a stem name, or a full
//   "<stem>_start"/"_run"/"_success" name) to search for, exact substring
//   match, case-sensitive (xtbl element/attribute values in this project
//   are already known to be case-sensitive in places, spec-tables-*.md).

#include <algorithm>
#include <cctype>
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

bool endsWithExt(const std::string& lowerName, const std::string& ext) {
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

struct XtblInstance {
    std::string archive;
    std::string pathChain;
    std::string entryName;
    std::string content;
};

struct WalkState {
    std::deque<std::vector<uint8_t>> bufferPool;
    std::vector<XtblInstance> found;
    uint64_t totalEntries = 0;
};

void inspectLeaf(WalkState& st, const std::string& archive, const std::string& chain, const std::string& name,
                  const uint8_t* data, size_t len) {
    std::string lower = toLower(name);
    if (endsWithExt(lower, ".xtbl")) {
        XtblInstance xi;
        xi.archive = archive;
        xi.pathChain = chain;
        xi.entryName = name;
        xi.content.assign(reinterpret_cast<const char*>(data), len);
        st.found.push_back(std::move(xi));
    }
}

void walk(WalkState& st, const vpp::Container& c, const std::string& archive, const std::string& chain) {
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        ++st.totalEntries;
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

std::vector<std::string> readNeedles(const fs::path& path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: xtbl_name_grep <cache_dir> <needles_file> <out_dir>\n";
        return 1;
    }
    fs::path cacheDir = argv[1];
    std::vector<std::string> needles = readNeedles(argv[2]);
    fs::path outDir = argv[3];
    fs::create_directories(outDir);

    std::cout << "Loaded " << needles.size() << " needle strings.\n";

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
    std::cout << "Archive walk complete: " << archives.size() << " archives, " << st.totalEntries
              << " total directory entries, " << st.found.size() << " real .xtbl entries found "
              << "(population denominator).\n";

    std::ofstream matchesOut(outDir / "xtbl_name_matches.tsv");
    matchesOut << "archive\tpath_chain\tentry_name\tneedle\tcontext\n";
    uint64_t totalMatches = 0;

    for (const auto& x : st.found) {
        for (const auto& needle : needles) {
            size_t pos = 0;
            while ((pos = x.content.find(needle, pos)) != std::string::npos) {
                size_t ctxStart = (pos > 40) ? pos - 40 : 0;
                size_t ctxLen = std::min<size_t>(needle.size() + 80, x.content.size() - ctxStart);
                std::string context = x.content.substr(ctxStart, ctxLen);
                for (char& c : context) {
                    if (c == '\t') c = ' ';
                    if (c == '\n' || c == '\r') c = ' ';
                }
                matchesOut << x.archive << "\t" << x.pathChain << "\t" << x.entryName << "\t" << needle
                           << "\t" << context << "\n";
                ++totalMatches;
                pos += needle.size();
            }
        }
    }
    matchesOut.close();

    std::cout << "\n=== RESULT ===\n";
    std::cout << "real_xtbl_files_scanned=" << st.found.size() << "\n";
    std::cout << "needles_searched=" << needles.size() << "\n";
    std::cout << "total_substring_matches=" << totalMatches << "\n";
    std::cout << "Wrote " << (outDir / "xtbl_name_matches.tsv").string() << "\n";
    return 0;
}
