// One-shot real-data probe (2026-10-08, zone-data fast-follow task): confirms,
// against the REAL shipped `sr3_city_0.vpp_pc` archive, that
// EngineState::loadZoneObjectsSection()/resolveZoneObject() (include/
// sr3luahost/zone_object_resolver.h) resolves the real boss character
// `Killbane` to her own real sr3zone::ObjectRecord - the concrete
// before/after example this task's own orchestrator report cites. NOT a
// ctest (same "one-shot population analysis, not a gating test"
// convention as tools/zone_objects_population.cpp) - prints facts for a
// human/agent to read, never asserts pass/fail.
//
// A fourth, dedicated, deliberately minimal recursive container walk
// (leaf test: ".czn_pc", scoped to ONE archive only) - same
// duplicated-with-attribution convention tools/lua_host_run.cpp's own top
// comment already establishes for this codebase, not factored into a
// shared header (this is a one-off diagnostic, not reused elsewhere).
//
// Usage: zone_object_resolver_probe <cache_dir>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3zone/zone_objects.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("could not open: " + path);
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size > 0 ? size : 0));
    if (size > 0) f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool endsWithCznExt(const std::string& lowerName) {
    static const std::string ext = ".czn_pc";
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

uint64_t g_cznFilesFound = 0;
uint64_t g_cznWithSection = 0;

void ingestCznBuffer(sr3luahost::EngineState& es, const uint8_t* data, size_t len) {
    ++g_cznFilesFound;
    std::vector<sr3zone::ObjectsSection> sections;
    try {
        sections = sr3zone::ZoneObjects::locate(vpp::ByteView(data, len));
    } catch (const std::exception&) {
        return;
    }
    for (const sr3zone::ObjectsSection& section : sections) {
        if (!section.complete) continue;
        ++g_cznWithSection;
        es.loadZoneObjectsSection(section);
    }
}

void walkCzn(sr3luahost::EngineState& es, const vpp::Container& c) {
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
                walkCzn(es, child);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested && isCzn) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                ingestCznBuffer(es, raw.data(), raw.size());
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
                        walkCzn(es, child);
                    } catch (const std::exception&) {
                        nested = false;
                    }
                }
            }
            if (!nested && isCzn) ingestCznBuffer(es, result.data.data(), result.data.size());
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: zone_object_resolver_probe <cache_dir>\n";
        return 1;
    }
    std::string cacheDir = argv[1];
    std::string archivePath = cacheDir + "/sr3_city_0.vpp_pc";

    sr3luahost::EngineState es;

    // BEFORE: this project's documented gap, demonstrated directly -
    // nothing has loaded any zone data yet, so "Killbane" is exactly as
    // unresolvable as every other arbitrary mission-authored name.
    std::cout << "=== BEFORE loadZoneObjectsSection ===\n";
    std::cout << "objectResolves().known(\"Killbane\") = " << es.objectResolves().known("Killbane") << "\n";
    std::cout << "resolveZoneObject(\"Killbane\") = " << (es.resolveZoneObject("Killbane") ? "FOUND" : "nullptr") << "\n";
    bool threwBefore = false;
    try {
        (void)es.drawOrGetMaxHitPoints("Killbane");
    } catch (const sr3luahost::OpenStateError& ex) {
        threwBefore = true;
        std::cout << "drawOrGetMaxHitPoints(\"Killbane\") threw OpenStateError: " << ex.what() << "\n";
    }
    std::cout << "(threw: " << threwBefore << ")\n\n";

    std::vector<uint8_t> bytes = readFile(archivePath);
    vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
    walkCzn(es, c);

    std::cout << "=== AFTER loadZoneObjectsSection (sr3_city_0.vpp_pc walked) ===\n";
    std::cout << "czn_files_found=" << g_cznFilesFound << " objects_sections=" << g_cznWithSection
              << " named_objects_resolved=" << es.zoneObjectsLoadedCount() << "\n";
    std::cout << "objectResolves().known(\"Killbane\") = " << es.objectResolves().known("Killbane") << "\n";
    if (es.objectResolves().known("Killbane")) {
        std::cout << "objectResolves().get(\"Killbane\") = " << es.objectResolves().get("Killbane") << "\n";
    }
    const sr3zone::ObjectRecord* rec = es.resolveZoneObject("Killbane");
    std::cout << "resolveZoneObject(\"Killbane\") = " << (rec ? "FOUND" : "nullptr") << "\n";
    if (rec) {
        std::cout << "  nameText=\"" << rec->nameText.value_or("") << "\" classHash=0x" << std::hex << rec->classHash
                   << std::dec << " className=" << sr3zone::ClassNameForHash(rec->classHash).value_or("<unresolved>")
                   << "\n";
        auto hp = sr3zone::ScriptNpcHp(*rec);
        std::cout << "  script_npc_hp=" << (hp ? std::to_string(*hp) : std::string("<none>")) << "\n";
    }
    std::cout << "lowercase lookup resolveZoneObject(\"killbane\") = "
              << (es.resolveZoneObject("killbane") ? "FOUND" : "nullptr") << " (case-insensitive check)\n";
    std::cout << "CharacterState(\"Killbane\").scriptNpcBound = " << es.getOrCreateCharacter("Killbane").scriptNpcBound
              << "\n";
    std::cout << "CharacterState(\"Killbane\").scriptNpcHp.known() = "
              << es.getOrCreateCharacter("Killbane").scriptNpcHp.known() << "\n";
    if (es.getOrCreateCharacter("Killbane").scriptNpcHp.known()) {
        std::cout << "CharacterState(\"Killbane\").scriptNpcHp.get() = " << es.getOrCreateCharacter("Killbane").scriptNpcHp.get()
                  << "\n";
    }
    try {
        int32_t hp = es.drawOrGetMaxHitPoints("Killbane");
        std::cout << "drawOrGetMaxHitPoints(\"Killbane\") = " << hp << " (no throw)\n";
    } catch (const sr3luahost::OpenStateError& ex) {
        std::cout << "drawOrGetMaxHitPoints(\"Killbane\") STILL throws: " << ex.what() << "\n";
    }
    return 0;
}
