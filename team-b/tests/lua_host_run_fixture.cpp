// Writes the synthetic archive cache for the lua_host_run integration test
// (tests/lua_host_run_integration_test.py). Cloud phase 2026-09-30.
//
//   lua_host_run_fixture <out_dir>
//
// Three uncompressed containers (misc, dlc1 and - 2026-10-01 - a small
// cutscene_tables for the zscene scene table) laid out per spec-vpp-container.md Sec1-2 (the
// same layout tests/synthetic_archive_test.cpp builds, raw entries only). The
// Lua sources are written for this test; nothing here comes from game files.
// The mission stems (dlc1_mm_02..06) are names from this project's own
// mission list (tools/, the one lua_host_run reads), so the mission-driving
// pass picks them up.
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "vpp/container.h"

namespace {

struct Entry {
    std::string name;
    std::string bytes;
};

void putU32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>(v >> (8 * i));
}

std::vector<uint8_t> buildContainer(const std::vector<Entry>& entries) {
    std::vector<uint8_t> names;
    std::vector<uint32_t> nameOffsets;
    for (const auto& e : entries) {
        nameOffsets.push_back(static_cast<uint32_t>(names.size()));
        names.insert(names.end(), e.name.begin(), e.name.end());
        names.push_back(0);
    }
    const uint32_t count = static_cast<uint32_t>(entries.size());
    const size_t dirBase = vpp::kDirectoryOffset;
    const size_t nameBase = dirBase + vpp::roundUpBlock(count * 24);
    const size_t payloadStart = nameBase + vpp::roundUpBlock(names.size());
    std::vector<size_t> offsets;
    size_t pos = 0, end = 0;
    for (const auto& e : entries) {
        offsets.push_back(pos);
        end = pos + e.bytes.size();
        pos += vpp::roundUpBlock(e.bytes.size());
    }
    std::vector<uint8_t> blob(payloadStart + end, 0);
    putU32(blob, 0x000, vpp::kMagic);
    putU32(blob, 0x004, 6);
    putU32(blob, 0x154, count);
    putU32(blob, 0x158, static_cast<uint32_t>(blob.size()));
    putU32(blob, 0x15C, count * 24);
    putU32(blob, 0x160, static_cast<uint32_t>(names.size()));
    putU32(blob, 0x168, 0xFFFFFFFFu);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t d = dirBase + i * 24;
        putU32(blob, d + 0x00, nameOffsets[i]);
        putU32(blob, d + 0x08, static_cast<uint32_t>(offsets[i]));
        putU32(blob, d + 0x0C, static_cast<uint32_t>(entries[i].bytes.size()));
        putU32(blob, d + 0x10, vpp::kRawSentinel);
    }
    std::copy(names.begin(), names.end(), blob.begin() + nameBase);
    for (uint32_t i = 0; i < count; ++i) {
        std::copy(entries[i].bytes.begin(), entries[i].bytes.end(), blob.begin() + payloadStart + offsets[i]);
    }
    return blob;
}

bool save(const std::string& path, const std::vector<uint8_t>& b) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(f);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: lua_host_run_fixture <out_dir>\n";
        return 2;
    }
    const std::string dir = argv[1];
    const std::vector<Entry> misc = {
        {"game_lib.lua",
         "function helper_wait_scene(n)\n  while not zscene_is_loaded(n) do thread_yield() end\nend\n"},
        // thread_new looks its target up through _GetAnyGlobalSilent (real
        // scripts get it from system_lib.lua); a minimal one for this test.
        {"system_lib.lua", "-- synthetic preload\nfunction _GetAnyGlobalSilent(n) return rawget(_G, n) end\n"},
    };
    // The zscene scene table (spec-lua-api-behaviour.md Sec26.25 item 5):
    // cutscene.xtbl gives the names ("main": names starting with "dlc" are
    // skipped), one <name>.cte_xtbl per name gives the fields; a name with no
    // scene file has no entry. Written for this test, shaped like the shipped
    // files (<root><Table><Cutscene>...).
    const std::vector<Entry> cutsceneTables = {
        {"cutscene.xtbl",
         "<root>\n<Table>\n"
         "<Cutscene><Name>Scene_A</Name></Cutscene>\n"
         "<Cutscene><Name>story_b</Name></Cutscene>\n"
         "<Cutscene><Name>dlc_skipped</Name></Cutscene>\n"
         "<Cutscene><Name>no_file_c</Name></Cutscene>\n"
         "</Table>\n</root>\n"},
        {"scene_a.cte_xtbl",
         "<root><Table><Cutscene><CutsceneType>Zscene</CutsceneType>"
         "<Soundtrack>CINEMATIC : FIXTURE_01</Soundtrack></Cutscene></Table></root>\n"},
        {"story_b.cte_xtbl",
         "<root><Table><Cutscene><CutsceneType>Story</CutsceneType><Characters><Character>"
         "<Name>X</Name><Mesh>npc_fixture</Mesh></Character></Characters></Cutscene></Table></root>\n"},
        {"dlc_skipped.cte_xtbl", "<root><Table><Cutscene><CutsceneType>Zscene</CutsceneType></Cutscene></Table></root>\n"},
    };
    const std::vector<Entry> dlc1 = {
        // 'scene_a' is a kind-1 entry of the fixture's scene table, so the
        // next read is the skip_all_cutscenes byte 0x0153b556 (false at start,
        // Sec26.25 Globals 2026-10-01), then the current scene entry 0x0153b530
        // (null at start, Sec26.25 Globals 2026-10-02): not current -> false,
        // so _start yields and the scheduler passes keep re-polling it.
        {"dlc1_mm_06.lua", "function dlc1_mm_06_start(cp, restart)\n  helper_wait_scene('scene_a')\nend\n"},
        // CONFIRMED stubs only: _start succeeds. fade_out(0) starts a fade-out
        // (Sec26.24); no UI script defines screen_fade_do here, so the host's
        // labelled fallback completes it on the first mission tick.
        {"dlc1_mm_05.lua",
         "function dlc1_mm_05_start(cp, restart)\n  set_mission_author()\n  fade_out(0)\nend\n"},
        // A plain Lua runtime error inside _start.
        {"dlc1_mm_04.lua", "function dlc1_mm_04_start(cp, restart)\n  local t = nil\n  return t.field\nend\n"},
        // Request 11 (Sec26.27): _start runs as a script-thread record, so
        // thread_new inside it has a parent and the child runs at once; the
        // child yields (alive), then _start itself yields: suspended, no
        // error. A bare lua_pcall raised "no script thread is current" here.
        {"dlc1_mm_03.lua",
         "function dlc1_mm_03_child(tag)\n  dlc1_mm_03_child_ran = tag\n  thread_yield()\nend\n"
         "function dlc1_mm_03_start(cp, restart)\n"
         "  local id = thread_new('dlc1_mm_03_child', 'ran')\n"
         "  if id == 65535 or dlc1_mm_03_child_ran ~= 'ran' then error('child thread did not start') end\n"
         "  thread_yield()\n"
         "end\n"},
        // A runaway _start: the watchdog must still stop it now that it runs
        // on its own coroutine.
        {"dlc1_mm_02.lua", "function dlc1_mm_02_start(cp, restart)\n  while true do end\nend\n"},
        // Not loadable: both real Lua and sr3lua must reject it.
        {"broken_syntax.lua", "function broken(\n"},
    };
    if (!save(dir + "/misc.vpp_pc", buildContainer(misc)) || !save(dir + "/dlc1.vpp_pc", buildContainer(dlc1)) ||
        !save(dir + "/cutscene_tables.vpp_pc", buildContainer(cutsceneTables))) {
        std::cerr << "lua_host_run_fixture: cannot write into " << dir << "\n";
        return 1;
    }
    return 0;
}
