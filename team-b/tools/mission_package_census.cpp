// Mission-package census, requested by the orchestrator 2026-09-29
// (parallel to the stub-implementation agent, does not depend on it):
// Team A traced mission-script streaming to stream_grid.asm_pc - 79
// real type-32 ("Mission LUA script") entries, each inside a
// "<mission-code>_modal" container. This tool answers three real,
// data-only questions over that same manifest (and any other real
// .asm_pc manifests found, for completeness):
//   1. which missions have a modal container;
//   2. what else each modal container holds (other real entry types);
//   3. which of the 54 real distinct "_start"-defining script stems
//      (HANDOFF Sec9.118's own population) are covered by a modal
//      container's own Mission-LUA-script entry.
//
// Uses sr3asm::AsmManifest (spec-asm-format.md Sec6-10, already
// population-validated, HANDOFF's own existing reader) directly - no new
// parsing logic, this is a read-only census over an already-proven
// reader's own output.
//
// Usage: mission_package_census <cache_dir> <start_stems_file> <out_dir>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3asm/manifest.h"
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

bool endsWithCI(const std::string& name, const std::string& extLower) {
    std::string lower = toLower(name);
    if (lower.size() < extLower.size()) return false;
    return lower.compare(lower.size() - extLower.size(), extLower.size(), extLower) == 0;
}

std::vector<std::string> readLines(const fs::path& path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

struct AsmInstance {
    std::string archive;
    std::string pathChain;
    std::string entryName;
    std::vector<uint8_t> content;
};

std::vector<AsmInstance> g_found;

void walk(const vpp::Container& c, const std::string& archive, const std::string& chain) {
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
                walk(child, archive, childChain);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested && endsWithCI(name, ".asm_pc")) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                g_found.push_back({archive, childChain, name, std::vector<uint8_t>(raw.data(), raw.data() + raw.size())});
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            if (result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch) {
                bool nested = false;
                if (result.data.size() >= 4) {
                    uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                      ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                    if (magic == 0x51890ACEu) {
                        try {
                            vpp::Container child(vpp::ByteView(result.data.data(), result.data.size()));
                            walk(child, archive, childChain);
                            nested = true;
                        } catch (const std::exception&) {
                            nested = false;
                        }
                    }
                }
                if (!nested && endsWithCI(name, ".asm_pc")) {
                    g_found.push_back({archive, childChain, name, std::move(result.data)});
                }
            }
        }
    }
}

std::string stemOf(const std::string& name) {
    auto pos = name.find_last_of('.');
    return pos == std::string::npos ? name : name.substr(0, pos);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: mission_package_census <cache_dir> <start_stems_file> <out_dir>\n";
        return 1;
    }
    fs::path cacheDir = argv[1];
    std::set<std::string> startStems;
    for (auto& s : readLines(argv[2])) startStems.insert(s);
    fs::path outDir = argv[3];
    fs::create_directories(outDir);

    std::cout << "Loaded " << startStems.size() << " real _start-defining stems.\n";

    std::vector<fs::path> archives;
    for (auto& ent : fs::directory_iterator(cacheDir)) {
        if (!ent.is_regular_file()) continue;
        if (toLower(ent.path().extension().string()) == ".vpp_pc") archives.push_back(ent.path());
    }
    std::sort(archives.begin(), archives.end());

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
            walk(root, archiveName, "");
        } catch (const std::exception&) {
        }
    }
    std::cout << "Archive walk complete: " << archives.size() << " archives, " << g_found.size()
              << " real .asm_pc entries found.\n";

    // Find stream_grid.asm_pc specifically (Team A's own citation), plus
    // report on every OTHER real manifest found for completeness.
    std::ofstream perContainer(outDir / "mission_package_per_container.tsv");
    perContainer << "manifest_archive\tmanifest_path_chain\tmanifest_name\tcontainer_name\t"
                    "entry_count\tmission_lua_entry_name\tmission_lua_stem\tstem_covers_a_start_script\t"
                    "other_entry_type_names\n";

    uint64_t totalManifestsParsed = 0, totalManifestParseFailures = 0;
    uint64_t totalMissionLuaEntries = 0;
    std::set<std::string> coveredStems;

    for (const auto& inst : g_found) {
        sr3asm::AsmManifest manifest;
        try {
            manifest = sr3asm::AsmManifest::parse(vpp::ByteView(inst.content.data(), inst.content.size()));
        } catch (const std::exception& ex) {
            ++totalManifestParseFailures;
            std::cerr << "PARSE FAILED " << inst.archive << " :: " << inst.pathChain << " : " << ex.what() << "\n";
            continue;
        }
        ++totalManifestsParsed;

        for (const auto& record : manifest.records()) {
            std::string missionLuaEntryName, missionLuaStem;
            std::vector<std::string> otherTypeNames;
            bool hasMissionLua = false;

            for (const auto& entry : record.entries) {
                const std::string* typeName = manifest.typeTable().findName(entry.typeId);
                std::string typeNameStr = typeName ? *typeName : ("<typeId=" + std::to_string(entry.typeId) + ">");
                if (typeNameStr == "Mission LUA script") {
                    hasMissionLua = true;
                    missionLuaEntryName = entry.name;
                    missionLuaStem = stemOf(entry.name);
                    ++totalMissionLuaEntries;
                } else {
                    otherTypeNames.push_back(typeNameStr);
                }
            }

            if (!hasMissionLua) continue; // only report containers that actually hold a mission script

            bool covers = startStems.count(missionLuaStem) > 0;
            if (covers) coveredStems.insert(missionLuaStem);

            std::string otherJoined;
            std::set<std::string> distinctOther(otherTypeNames.begin(), otherTypeNames.end());
            for (auto& t : distinctOther) {
                if (!otherJoined.empty()) otherJoined += ",";
                otherJoined += t;
            }

            perContainer << inst.archive << "\t" << inst.pathChain << "\t" << inst.entryName << "\t"
                         << record.name << "\t" << record.entries.size() << "\t" << missionLuaEntryName
                         << "\t" << missionLuaStem << "\t" << (covers ? 1 : 0) << "\t" << otherJoined << "\n";
        }
    }
    perContainer.close();

    std::cout << "\n=== RESULT (mission_package_census) ===\n";
    std::cout << "real_asm_pc_manifests_found=" << g_found.size() << "\n";
    std::cout << "manifests_parsed_ok=" << totalManifestsParsed << "\n";
    std::cout << "manifest_parse_failures=" << totalManifestParseFailures << "\n";
    std::cout << "total_mission_lua_entries_found(pooled, all manifests)=" << totalMissionLuaEntries << "\n";
    std::cout << "distinct_start_stems_covered_by_a_mission_lua_container=" << coveredStems.size()
              << " / " << startStems.size() << "\n";
    std::cout << "Wrote " << (outDir / "mission_package_per_container.tsv").string() << "\n";
    return 0;
}
