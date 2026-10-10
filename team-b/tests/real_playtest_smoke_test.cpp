// Real-data smoke test for sr3_viewer's `playtest` command (2026-10-08,
// "playtest v0" milestone). Launches the real sr3_viewer.exe playtest
// subcommand --headless --no-input for a fixed 300 frames against the real
// game cache (CRREISH_GAME_CACHE_DIR), the SAME deterministic-input
// convention runWalkDemo()'s own --no-input already establishes (see
// runPlaytest()'s own doc comment, tools/sr3_viewer.cpp), and checks:
//   1. clean exit (code 0, no crash);
//   2. the real captured frame (one PNG) was actually written and is a
//      plausible size (not a truncated/corrupt 0-byte file);
//   3. stdout's own final "frames   : 300 presented" line, confirming the
//      run reached the end of the loop rather than breaking early on some
//      internal error path that still happened to exit 0.
// Exits 77 (ctest SKIPPED, never a pass) when CRREISH_GAME_CACHE_DIR's
// archives are absent - same convention every other real_*_test uses
// (e.g. tests/real_vintdoc_document_test.cpp). Nothing from the game is
// copied into the repository; this test only reads the real install at run
// time, same as sr3_viewer itself.
//
// --bin-dir is required (the directory holding the freshly-built
// sr3_viewer.exe for THIS config - passed by CMakeLists.txt as
// $<TARGET_FILE_DIR:sr3_viewer>, the SAME genex tools/golden_scene_check.cpp's
// own ctest registration already uses, not a hardcoded path).
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// CreateProcess + CREATE_NO_WINDOW - the SAME shape tools/golden_scene_check.cpp's
// own runIn() uses (copied, not shared - this is a standalone test
// executable, same convention every other real_*_test follows), so the
// child sr3_viewer.exe process never flashes a console on top of whatever
// ctest run is driving this (its own render window is separately kept
// hidden by the --headless flag passed below).
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

    const fs::path workDir = fs::path(binDir);  // cwd = the exe's own directory, so a relative --capture lands beside it
    const fs::path capturePath = workDir / "playtest_smoke.png";
    const fs::path stdoutPath = workDir / "playtest_smoke_stdout.txt";
    std::error_code ec;
    fs::remove(capturePath, ec);
    fs::remove(stdoutPath, ec);

    const std::string exe = "\"" + (fs::path(binDir) / "sr3_viewer.exe").string() + "\"";
    // --headless: no visible window for this automated run (this project's
    // standing "hide every window by default" rule - playtest's own
    // DEFAULT is visible=true, since it exists to be played, so the
    // automated smoke test must explicitly opt back OUT of that). --no-input:
    // the deterministic scripted fallback (see runPlaytest()'s own doc
    // comment) instead of live GetAsyncKeyState/mouse polling, which would
    // read whatever this machine's real keyboard/mouse happen to be doing
    // during a ctest run - exactly the kind of non-determinism a regression
    // test cannot tolerate.
    const std::string cmd = exe + " playtest \"" + characters.string() + "\" \"" + miscTables.string() + "\" \"" +
                            cityArchive.string() + "\" --headless --no-input --frames 300 --capture \"" +
                            capturePath.string() + "\" > \"" + stdoutPath.string() + "\" 2>&1";
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
    std::string line, lastFramesLine;
    bool sawFramesLine = false;
    while (std::getline(in, line)) {
        if (line.rfind("frames   : 300 presented", 0) == 0) {
            sawFramesLine = true;
            lastFramesLine = line;
        }
    }
    if (!sawFramesLine) {
        std::printf("FAIL: stdout never printed 'frames   : 300 presented' (see %s)\n", stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("stdout   : %s\n", lastFramesLine.c_str());
    }

    std::printf("\nREAL_PLAYTEST_SMOKE_TEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
