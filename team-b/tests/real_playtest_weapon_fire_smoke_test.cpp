// Real-data smoke test for sr3_viewer's `playtest` command's new weapon
// fire -> hitscan -> damage -> ped reaction wiring (2026-10-10,
// playtest-weapon-fire-wiring task). A sibling of
// tests/real_playtest_vehicle_smoke_test.cpp/
// tests/real_playtest_traffic_roadblock_smoke_test.cpp (copied shape, not
// shared - same convention every other real_*_test follows).
//
// Launches the real sr3_viewer.exe `playtest` subcommand --headless
// --no-input --script-fire against the real game cache
// (CRREISH_GAME_CACHE_DIR). Checks:
//   1. clean exit (code 0, no crash);
//   2. stdout shows Brad was actually equipped with a REAL weapons.xtbl row
//      ("weapon   : Brad equipped with real weapons.xtbl row") - proving
//      this is real authored data, not a fabricated placeholder;
//   3. stdout shows the scripted shot actually reached the real dispatch
//      body ("[fire] frame 60: reachedItemUse=1 ... hit=1 ...") - proving
//      the vehicle-class short-circuit/refuse/refire gates all passed and a
//      hit against the always-on near-field ambient-ped population was
//      actually detected (the --script-fire aiming convenience, see
//      runPlaytest()'s own "WEAPON FIRE" block doc comment, aims directly
//      at the nearest live ped so this is deterministic);
//   4. stdout shows the hit ped's own isDeadHighConfidence flag actually
//      flipped false -> true through the real EngineState::
//      applyGuaranteedLethalHit() sink ("isDeadHighConfidence before=0
//      after=1") - the real observable death result, not just a printed
//      "it compiled";
//   5. stdout shows the real gunfire-to-flee reaction chain
//      (EvaluateGunfireReaction/ChooseFleeOrCower) actually ran and
//      produced a reaction mode for that same ped;
//   6. the real captured frame (one PNG, taken at the fire frame) was
//      written and is a plausible size.
// Exits 77 (ctest SKIPPED, never a pass) when CRREISH_GAME_CACHE_DIR's
// archives are absent, same convention every other real_*_test uses.
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// CreateProcess + CREATE_NO_WINDOW - same shape as
// real_playtest_vehicle_smoke_test.cpp's own runIn() (copied, not shared).
int runIn(const std::string& cwd, const std::string& command) {
    std::string fullCmd = "cmd.exe /c \"" + command + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmdLineBuf(fullCmd.begin(), fullCmd.end());
    cmdLineBuf.push_back('\0');
    BOOL ok = CreateProcessA(nullptr, cmdLineBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             cwd.c_str(), &si, &pi);
    if (!ok) {
        std::printf("FATAL: CreateProcess failed for '%s' (cwd '%s', GetLastError=%lu)\n", fullCmd.c_str(),
                   cwd.c_str(), static_cast<unsigned long>(GetLastError()));
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

} // namespace

int main(int argc, char** argv) {
    std::string binDir;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bin-dir" && i + 1 < argc) binDir = argv[++i];
    }
    if (binDir.empty()) {
        std::printf("FATAL: --bin-dir not given (expected $<TARGET_FILE_DIR:sr3_viewer> from CMakeLists.txt)\n");
        return 1;
    }

    const fs::path dataRoot = CRREISH_GAME_CACHE_DIR;
    const fs::path characters = dataRoot / "characters.vpp_pc";
    const fs::path miscTables = dataRoot / "misc_tables.vpp_pc";
    const fs::path cityArchive = dataRoot / "sr3_city_0.vpp_pc";
    if (!fs::exists(characters) || !fs::exists(miscTables) || !fs::exists(cityArchive)) {
        std::printf("SKIPPED: real game cache not found under '%s' (set CRREISH_GAME_CACHE_DIR)\n",
                   dataRoot.string().c_str());
        return 77;
    }

    const fs::path workDir = fs::path(binDir);
    const fs::path capturePath = workDir / "playtest_weapon_fire_smoke.png";
    const fs::path stdoutPath = workDir / "playtest_weapon_fire_smoke_stdout.txt";
    std::error_code ec;
    fs::remove(capturePath, ec);
    fs::remove(stdoutPath, ec);

    const std::string exe = "\"" + (fs::path(binDir) / "sr3_viewer.exe").string() + "\"";
    // kScriptFireFrame is 60 (see runPlaytest()'s own declaration) - well
    // inside the always-on near-field ped spawner's own 200-300ms tick
    // cadence at 30fps (--no-input's fixed 1/30s step), so at least one ped
    // should already be live by then. --script-forward-frames/--script-
    // turn-frames left at their own defaults (character movement does not
    // matter for this test - the fire aim itself is scripted separately,
    // see the --script-fire doc comment).
    const std::string cmd = exe + " playtest \"" + characters.string() + "\" \"" + miscTables.string() + "\" \"" +
                            cityArchive.string() +
                            "\" --headless --no-input --script-fire --frames 120"
                            " --capture \"" + capturePath.string() + "\" --capture-frame 60" +
                            " > \"" + stdoutPath.string() + "\" 2>&1";
    const int rc = runIn(workDir.string(), cmd);
    if (rc != 0) {
        std::printf("FAIL: sr3_viewer.exe playtest returned exit code %d (see %s)\n", rc, stdoutPath.string().c_str());
        return 1;
    }

    bool ok = true;
    if (!fs::exists(capturePath) || fs::file_size(capturePath) < 1024) {
        std::printf("FAIL: capture PNG missing or implausibly small: %s\n", capturePath.string().c_str());
        ok = false;
    } else {
        std::printf("capture  : %s (%llu bytes)\n", capturePath.string().c_str(),
                   static_cast<unsigned long long>(fs::file_size(capturePath)));
    }

    std::ifstream in(stdoutPath);
    std::string line, weaponLine, fireLine, hitLine, deathLine, reactionLine;
    bool sawWeaponEquipped = false, sawFireReached = false, sawHit = false, sawDeathFlip = false, sawReaction = false,
         sawFramesLine = false;
    while (std::getline(in, line)) {
        if (line.rfind("weapon   : Brad equipped with real weapons.xtbl row", 0) == 0) {
            sawWeaponEquipped = true;
            weaponLine = line;
        }
        if (line.rfind("[fire] frame", 0) == 0 && line.find("reachedItemUse=1") != std::string::npos) {
            sawFireReached = true;
            fireLine = line;
            if (line.find("hit=1") != std::string::npos) sawHit = true;
        }
        if (line.rfind("[fire] hit '", 0) == 0) {
            hitLine = line;
            if (line.find("before=0") != std::string::npos && line.find("after=1") != std::string::npos) {
                sawDeathFlip = true;
            }
        }
        if (line.rfind("[fire] ped id=", 0) == 0) {
            sawReaction = true;
            reactionLine = line;
        }
        if (line.rfind("frames   : 120 presented", 0) == 0) sawFramesLine = true;
    }

    if (!sawWeaponEquipped) {
        std::printf("FAIL: stdout never printed a real weapons.xtbl row equip line (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("stdout   : %s\n", weaponLine.c_str());
    }
    if (!sawFireReached) {
        std::printf("FAIL: stdout never showed the scripted shot reaching the real dispatch body "
                   "(reachedItemUse=1, see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("stdout   : %s\n", fireLine.c_str());
    }
    if (!sawHit) {
        std::printf("FAIL: the scripted shot never registered a hit against the near-field ped population "
                   "(see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawDeathFlip) {
        std::printf("FAIL: the hit ped's isDeadHighConfidence flag never flipped false->true through the real "
                   "damage sink (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("stdout   : %s\n", hitLine.c_str());
    }
    if (!sawReaction) {
        std::printf("FAIL: the real gunfire-to-flee reaction chain never produced a mode for the hit ped "
                   "(see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("stdout   : %s\n", reactionLine.c_str());
    }
    if (!sawFramesLine) {
        std::printf("FAIL: stdout never printed 'frames   : 120 presented' (see %s)\n", stdoutPath.string().c_str());
        ok = false;
    }

    std::printf("\nREAL_PLAYTEST_WEAPON_FIRE_SMOKE_TEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
