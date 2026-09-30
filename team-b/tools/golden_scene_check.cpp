// Golden-scene regression harness (AI-WORKFLOW-INSTRUCTIONS.md Sec13 - "end-
// to-end validation that doesn't need a human": freeze a fixed scene's
// image + draw-stream summary, then re-render and diff against it on every
// future change).
//
// CONVENTION (read this before adding another scene):
//   tests/golden/<scene>/            - the FROZEN baseline artifacts for one
//                                       scene, committed/kept permanently.
//     COMMAND.txt or STATUS.txt      - which real command produced this
//                                       baseline (or, if the scene is not
//                                       frozen yet, why not - see tree/
//                                       STATUS.txt for a worked example of
//                                       an honestly-reported gap).
//     baseline_*.png                 - the frozen output image(s), byte-
//                                       exact PNGs from this project's own
//                                       sr3render::writePng (or a tool that
//                                       calls it).
//     baseline_stdout.txt            - the frozen real draw-stream summary
//                                       (that command's own real stdout,
//                                       never a format invented for this
//                                       harness).
//   tests/golden/_work/              - scratch: where a re-render's fresh
//                                       output lands before being diffed
//                                       against the frozen baseline. Not
//                                       part of the frozen record; safe to
//                                       delete any time.
//   tests/golden/_bin/               - this harness's own build output (see
//                                       tests/golden/build.bat) - kept
//                                       separate from build_verify/ and from
//                                       CMakeLists.txt deliberately: a
//                                       different task was concurrently
//                                       editing tools/sr3_viewer.cpp (and
//                                       plausibly CMakeLists.txt's linkage
//                                       for it) while this harness was
//                                       built, so this stays a self-
//                                       contained cl.exe build via a batch
//                                       script instead of a new CMake
//                                       target, to avoid a shared-file race.
//                                       A future session with no such
//                                       conflict in flight can fold
//                                       tools/tree_baseline_render.cpp and
//                                       this file into CMakeLists.txt
//                                       properly (add_executable + a
//                                       ctest add_test) instead.
//
// To add a new scene: freeze its real command's PNG(s) + stdout under
// tests/golden/<scene>/ (see brad/COMMAND.txt for the documented example),
// then add one checkXScene() function below plus one call to it in main() -
// same shape as checkBradScene().
//
// COMPARISON POLICY: byte-exact file comparison is the primary, authoritative
// check for both the PNG and the stdout summary - this project's own
// renders are deterministic (no RNG, no wall-clock input, a fixed WARP-or-
// hardware device path reported in stdout rather than hidden) and this was
// directly confirmed by running brad's own baseline command twice in a row
// and diffing (see the harness build notes) - MD5-identical both times,
// matching HANDOFF.md Sec9.105's own independently-recorded hashes exactly.
// No tolerance/fuzzy-diff fallback is used anywhere in this file because no
// concrete non-determinism has been found - per this task's own instruction,
// a fallback is added only if one is.
//
// When a byte-exact PNG compare fails, this tool ALSO decodes both PNGs (a
// small decoder tailored to exactly this project's own sr3render::writePng
// output shape - single IHDR/IDAT/IEND, 8-bit RGBA, no interlace, filter-
// type 0 on every scanline; NOT a general PNG reader) so the failure report
// names actual differing pixels and a bounding box, not just "differs".

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "zlib.h"

namespace {

std::vector<uint8_t> readFileBytes(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        ok = false;
        return {};
    }
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n < 0 ? 0 : n));
    if (!b.empty() && !f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()))) {
        ok = false;
        return {};
    }
    ok = true;
    return b;
}

uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Decodes exactly the shape sr3render::writePng (src/png_writer.cpp) emits:
// signature, one IHDR (8-bit, colour type 6 = RGBA, no interlace), one IDAT
// (a single zlib stream, filter-type-0 on every scanline), one IEND. Refuses
// (returns false) rather than guess at anything writePng() itself never
// produces - this is the inverse of one specific writer, not a general PNG
// reader.
bool decodeOwnPng(const std::string& path, uint32_t& width, uint32_t& height,
                  std::vector<uint8_t>& rgbaOut, std::string& error) {
    bool ok = false;
    std::vector<uint8_t> file = readFileBytes(path, ok);
    if (!ok) {
        error = "could not read '" + path + "'";
        return false;
    }
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (file.size() < 8 || std::memcmp(file.data(), kSig, 8) != 0) {
        error = "not a PNG (bad signature): " + path;
        return false;
    }
    size_t cursor = 8;
    bool haveIhdr = false;
    std::vector<uint8_t> idat;
    while (cursor + 8 <= file.size()) {
        uint32_t len = readU32BE(&file[cursor]);
        if (cursor + 8 + len + 4 > file.size()) {
            error = "truncated chunk in " + path;
            return false;
        }
        const char* type = reinterpret_cast<const char*>(&file[cursor + 4]);
        const uint8_t* data = &file[cursor + 8];
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) {
                error = "unexpected IHDR length in " + path;
                return false;
            }
            width = readU32BE(data);
            height = readU32BE(data + 4);
            uint8_t bitDepth = data[8], colourType = data[9], interlace = data[12];
            if (bitDepth != 8 || colourType != 6 || interlace != 0) {
                error = "PNG in " + path + " is not 8-bit RGBA / non-interlaced (this decoder "
                        "only inverts sr3render::writePng's own output shape)";
                return false;
            }
            haveIhdr = true;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        cursor += 8 + len + 4;
    }
    if (!haveIhdr || idat.empty()) {
        error = "missing IHDR/IDAT in " + path;
        return false;
    }
    const size_t rawSize = static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 4);
    std::vector<uint8_t> raw(rawSize);
    uLongf destLen = static_cast<uLongf>(rawSize);
    int zr = uncompress(raw.data(), &destLen, idat.data(), static_cast<uLong>(idat.size()));
    if (zr != Z_OK || destLen != rawSize) {
        error = "zlib uncompress failed (" + std::to_string(zr) + ") for " + path;
        return false;
    }
    rgbaOut.resize(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = &raw[static_cast<size_t>(y) * (1 + static_cast<size_t>(width) * 4)];
        uint8_t filterType = row[0];
        if (filterType != 0) {
            error = "row " + std::to_string(y) + " of " + path + " uses filter type " +
                    std::to_string(filterType) + " (only type 0/None is supported - not something "
                    "sr3render::writePng ever emits)";
            return false;
        }
        std::memcpy(&rgbaOut[static_cast<size_t>(y) * width * 4], row + 1, static_cast<size_t>(width) * 4);
    }
    return true;
}

// Mirrors src/png_writer.cpp's sr3render::writePng() exactly (signature,
// one IHDR, filter-type-0 rows, one IDAT via zlib compress2 at
// Z_BEST_SPEED, one IEND) - used ONLY by --selftest, to turn a decoded+
// deliberately-altered pixel buffer back into a real, decodable PNG so the
// self-test can demonstrate an actual pixel-level diff instead of an
// undecodable corrupt stream. Not used anywhere in the real PASS/FAIL path.
void appendU32BEPng(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}
bool encodeOwnPngForSelfTest(const std::string& path, uint32_t width, uint32_t height,
                            const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 4));
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint8_t* row = &rgba[static_cast<size_t>(y) * width * 4];
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4);
    }
    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()),
                 Z_BEST_SPEED) != Z_OK) {
        return false;
    }
    compressed.resize(compressedSize);
    std::vector<uint8_t> png;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);
    auto appendChunk = [&](const char type[4], const std::vector<uint8_t>& data) {
        appendU32BEPng(png, static_cast<uint32_t>(data.size()));
        size_t crcStart = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());
        uLong crc = crc32(0L, Z_NULL, 0);
        crc = crc32(crc, png.data() + crcStart, static_cast<uInt>(4 + data.size()));
        appendU32BEPng(png, static_cast<uint32_t>(crc));
    };
    std::vector<uint8_t> ihdr;
    appendU32BEPng(ihdr, width);
    appendU32BEPng(ihdr, height);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendChunk("IHDR", ihdr);
    appendChunk("IDAT", compressed);
    appendChunk("IEND", {});
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return true;
}

// Byte-exact compare (the primary, authoritative check). On mismatch, tries
// to decode both as this project's own PNG shape and reports the first
// differing pixel, the total differing-pixel count and a bounding box - a
// real diff, not just "differs". Returns true (PASS) only on an exact byte
// match.
bool comparePngExact(const std::string& baselinePath, const std::string& freshPath, std::string& report) {
    bool okA = false, okB = false;
    std::vector<uint8_t> a = readFileBytes(baselinePath, okA);
    std::vector<uint8_t> b = readFileBytes(freshPath, okB);
    if (!okA) {
        report = "could not read baseline '" + baselinePath + "'";
        return false;
    }
    if (!okB) {
        report = "could not read fresh render '" + freshPath + "'";
        return false;
    }
    if (a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0) {
        report = "byte-exact match (" + std::to_string(a.size()) + " bytes)";
        return true;
    }

    std::ostringstream out;
    out << "BYTE MISMATCH: baseline " << a.size() << " bytes, fresh " << b.size() << " bytes";

    uint32_t wA = 0, hA = 0, wB = 0, hB = 0;
    std::vector<uint8_t> pxA, pxB;
    std::string errA, errB;
    if (decodeOwnPng(baselinePath, wA, hA, pxA, errA) && decodeOwnPng(freshPath, wB, hB, pxB, errB)) {
        if (wA != wB || hA != hB) {
            out << "; dimensions differ: baseline " << wA << "x" << hA << " vs fresh " << wB << "x" << hB;
        } else {
            uint64_t diffPixels = 0;
            int32_t minX = -1, minY = -1, maxX = -1, maxY = -1;
            int32_t firstX = -1, firstY = -1;
            for (uint32_t y = 0; y < hA; ++y) {
                for (uint32_t x = 0; x < wA; ++x) {
                    size_t idx = (static_cast<size_t>(y) * wA + x) * 4;
                    if (std::memcmp(&pxA[idx], &pxB[idx], 4) != 0) {
                        if (firstX < 0) {
                            firstX = static_cast<int32_t>(x);
                            firstY = static_cast<int32_t>(y);
                        }
                        if (minX < 0 || static_cast<int32_t>(x) < minX) minX = static_cast<int32_t>(x);
                        if (minY < 0 || static_cast<int32_t>(y) < minY) minY = static_cast<int32_t>(y);
                        if (static_cast<int32_t>(x) > maxX) maxX = static_cast<int32_t>(x);
                        if (static_cast<int32_t>(y) > maxY) maxY = static_cast<int32_t>(y);
                        ++diffPixels;
                    }
                }
            }
            out << "; " << diffPixels << " / " << (static_cast<uint64_t>(wA) * hA) << " pixels differ";
            if (diffPixels > 0) {
                out << " (first at x=" << firstX << " y=" << firstY << "; bounding box x[" << minX << ".."
                    << maxX << "] y[" << minY << ".." << maxY << "])";
            }
        }
    } else {
        out << "; (could not decode for a pixel-level diff: " << (errA.empty() ? errB : errA) << ")";
    }
    report = out.str();
    return false;
}

// Line-by-line text compare. Returns true (PASS) only if every line matches.
bool compareTextExact(const std::string& baselinePath, const std::string& freshPath, std::string& report) {
    std::ifstream fa(baselinePath), fb(freshPath);
    if (!fa) {
        report = "could not read baseline '" + baselinePath + "'";
        return false;
    }
    if (!fb) {
        report = "could not read fresh capture '" + freshPath + "'";
        return false;
    }
    std::vector<std::string> la, lb;
    std::string line;
    while (std::getline(fa, line)) la.push_back(line);
    while (std::getline(fb, line)) lb.push_back(line);

    std::ostringstream out;
    bool pass = true;
    size_t n = la.size() > lb.size() ? la.size() : lb.size();
    size_t diffCount = 0;
    for (size_t i = 0; i < n; ++i) {
        const std::string a = i < la.size() ? la[i] : "<no line>";
        const std::string b = i < lb.size() ? lb[i] : "<no line>";
        if (a != b) {
            pass = false;
            ++diffCount;
            if (diffCount <= 8) {
                out << "  line " << (i + 1) << ":\n    baseline: " << a << "\n    fresh   : " << b << "\n";
            }
        }
    }
    if (pass) {
        report = "byte-for-byte identical (" + std::to_string(la.size()) + " lines)";
    } else {
        std::ostringstream head;
        head << diffCount << " line(s) differ (baseline " << la.size() << " lines, fresh " << lb.size()
             << " lines):\n"
             << out.str();
        report = head.str();
    }
    return pass;
}

// Runs `command` with the process working directory set to `cwd`, restoring
// the harness's own cwd afterward. Uses plain std::system() (which already
// goes through cmd.exe on Windows, so `>`/`2>&1` redirection just works) -
// no new process-management code, this is the same shape every build_*.bat
// script in this project's own scratchpad already uses, just called from
// C++ instead of a batch file.
int runIn(const std::string& cwd, const std::string& command) {
    char prevCwd[4096];
    if (_getcwd(prevCwd, sizeof(prevCwd)) == nullptr) prevCwd[0] = '\0';
    if (_chdir(cwd.c_str()) != 0) {
        std::printf("FATAL: could not chdir to '%s'\n", cwd.c_str());
        return -1;
    }
    // cmd.exe's own quoting rule: when the command starts with a quoted,
    // space-containing path, the WHOLE line must be wrapped in one more
    // pair of quotes or cmd mis-splits it on the first space (observed
    // directly: without this, "D:\Project Crreish\..." was parsed as the
    // command "D:\Project" with the rest as arguments). system() passes our
    // string straight to `cmd /c <string>`, so this wrapping has to happen
    // here, not be assumed away.
    std::string wrapped = "\"" + command + "\"";
    int rc = std::system(wrapped.c_str());
    if (prevCwd[0] != '\0') _chdir(prevCwd);
    return rc;
}

bool fileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

struct SceneResult {
    std::string name;
    // "PASS", "FAIL", or "SKIP"
    std::string status;
    std::vector<std::string> details;
};

void printResult(const SceneResult& r) {
    std::printf("[%s] %s\n", r.status.c_str(), r.name.c_str());
    for (const auto& d : r.details) std::printf("    %s\n", d.c_str());
}

SceneResult checkBradScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "brad (single-bone pose, sr3_viewer)";

    const std::string goldenDir = root + "\\tests\\golden\\brad";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = root + "\\build_verify\\sr3_viewer.exe";
    const std::string archive = dataRoot + "\\characters.vpp_pc";

    if (!fileExists(goldenDir + "\\baseline_bind.png") || !fileExists(goldenDir + "\\baseline_posed.png") ||
        !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe);
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out_prefix ("_work/brad", forward slash) that COMMAND.txt records
    // was used to freeze the baseline - sr3_viewer echoes this argument back
    // verbatim in its "wrote:" lines, so using anything else (a different
    // name, or backslashes) would make an otherwise-identical stdout report
    // a false mismatch. This is the one caller-chosen-path detail in an
    // otherwise fully real, unedited stdout capture.
    std::string cmd = "\"" + viewerExe + "\" pose \"" + archive +
                      "\" brad.ccmesh_pc brad.rig_pc _work/brad > _work\\brad_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_bind.png", workDir + "\\_work\\brad_bind.png", report);
    r.details.push_back(std::string("bind.png  : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 =
        comparePngExact(goldenDir + "\\baseline_posed.png", workDir + "\\_work\\brad_posed.png", report);
    r.details.push_back(std::string("posed.png : ") + (p2 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p2;

    bool p3 = compareTextExact(goldenDir + "\\baseline_stdout.txt", workDir + "\\_work\\brad_check_stdout.txt",
                               report);
    r.details.push_back(std::string("stdout    : ") + (p3 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p3;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// Shared by checkVehicleGenkiScene()/checkVehicleStandardScene() below - both
// are the same shape as checkBradScene() above (one archive, one .ccar_pc,
// the shared shaders.vpp_pc, one --capture PNG, one stdout capture), just
// parameterised over the bits that differ per vehicle. See
// tests/golden/vehicle_genki/COMMAND.txt and tests/golden/vehicle_standard/
// COMMAND.txt for why each vehicle's own archive/group was chosen.
SceneResult checkVehicleScene(const std::string& root, const std::string& dataRoot, const std::string& sceneName,
                              const std::string& sceneDir, const std::string& vehicleArchive,
                              const std::string& ccarName, const std::string& workStem) {
    SceneResult r;
    r.name = sceneName + " (sr3_viewer vehicle)";

    const std::string goldenDir = root + "\\tests\\golden\\" + sceneDir;
    const std::string workDir = root + "\\tests\\golden";
    // NOT build_verify\sr3_viewer.exe: that binary predates this session's
    // `vehicle` command and does not recognise it ("unknown command:
    // vehicle", confirmed directly) - see tests/golden/build.bat, which
    // builds this exe fresh from the unmodified tools/sr3_viewer.cpp instead.
    const std::string viewerExe = root + "\\tests\\golden\\_bin\\sr3_viewer.exe";
    const std::string archive = dataRoot + "\\" + vehicleArchive;
    const std::string shadersArchive = dataRoot + "\\shaders.vpp_pc";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - run tests/golden/build.bat");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/<workStem>.png") that each
    // scene's own COMMAND.txt records was used to freeze the baseline -
    // sr3_viewer echoes this argument back verbatim in its "captured:" line,
    // so using anything else would make an otherwise-identical stdout report
    // a false mismatch (the same one caller-chosen-path detail
    // brad/COMMAND.txt already documents for its own baseline).
    std::string cmd = "\"" + viewerExe + "\" vehicle \"" + archive + "\" " + ccarName + " \"" + shadersArchive +
                      "\" --group 0 --frames 1 --capture _work/" + workStem + ".png --capture-frame 0 > _work\\" +
                      workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

SceneResult checkVehicleGenkiScene(const std::string& root, const std::string& dataRoot) {
    return checkVehicleScene(root, dataRoot, "vehicle_genki", "vehicle_genki", "dlc1.vpp_pc",
                             "car_4dr_genki_0.ccar_pc", "vehicle_genki");
}

SceneResult checkVehicleStandardScene(const std::string& root, const std::string& dataRoot) {
    return checkVehicleScene(root, dataRoot, "vehicle_standard", "vehicle_standard", "vehicles.vpp_pc",
                             "car_4dr_standard03_4.ccar_pc", "vehicle_standard");
}

// The 4th golden scene (extends checkBradScene()/checkVehicleScene() above
// to the `zone` command - see tests/golden/zone_tile/COMMAND.txt for the
// real archive/tile/why). Same shape as checkVehicleScene(): one
// --capture PNG + one stdout capture, run through the SAME fresh
// tests/golden/_bin/sr3_viewer.exe build.bat produces (build_verify/
// sr3_viewer.exe predates the `zone` command entirely and does not
// recognise it, exactly like it predates `vehicle` - see build.bat's own
// comments). No shaders archive is needed here (no confirmed real
// material/texture binding exists for zone data - see runZoneTile()'s own
// doc comment - so this is not routed through checkVehicleScene()'s shared
// per-material-shader helper).
//
// RE-FROZEN 2026-09-30 (HANDOFF.md Sec9.126/Sec9.127): switched from vertex
// layout code 24 (a real, UV-less companion stream; ROLE OPEN - an earlier
// "probable proxy" guess was tested by a direct composite render against
// layout code 0 and did not hold up, see runZoneTile()'s own doc comment's
// 2026-09-30 update and runZoneTileComposite()'s own doc comment for the
// real visual evidence) to layout code 0
// (the real UV-bearing render geometry) - see runZoneTile()'s own doc
// comment for the full rationale, including why `--group 0` was DROPPED
// from the real command here (channel 1's own real draw ranges are split,
// gap-free, across BOTH located LOD groups for this tile, not contained in
// group 0 alone; the command's new default auto-unions every located
// group's own ranges for the resolved channel). The superseded code-24
// baseline is preserved verbatim under `_code24_proxy`-suffixed filenames
// in the same directory (COMMAND_code24_proxy.txt,
// baseline_capture_code24_proxy.png, baseline_stdout_code24_proxy.txt) -
// same convention tests/golden/tree/COMMAND.txt already established with
// its own `_group0_only` suffix.
SceneResult checkZoneTileScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "zone_tile (sr3_viewer zone, vertex layout code 0)";

    const std::string goldenDir = root + "\\tests\\golden\\zone_tile";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = root + "\\tests\\golden\\_bin\\sr3_viewer.exe";
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string cznName = "sr3_city~f0816~al.czn_pc";
    const std::string workStem = "zone_tile";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - run tests/golden/build.bat");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/zone_tile.png") that
    // tests/golden/zone_tile/COMMAND.txt records was used to freeze the
    // baseline - sr3_viewer echoes this argument back verbatim in its
    // "captured:" line, so using anything else would make an otherwise-
    // identical stdout report a false mismatch (the same caller-chosen-
    // path detail brad/vehicle_genki's own COMMAND.txt files document). NO
    // --group is passed (deliberately, since the 2026-09-30 re-freeze -
    // see this function's own comment above): the command's new default
    // auto-unions every located LOD group's own ranges for the resolved
    // channel, which is what the real command that froze this baseline
    // actually used.
    std::string cmd = "\"" + viewerExe + "\" zone \"" + archive + "\" " + cznName +
                      " --frames 1 --capture _work/" + workStem + ".png --capture-frame 0 > _work\\" +
                      workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 5th golden scene: a real tree, layout codes 11/12 (Position now
// decodable via FLOAT16x3 at +0, spec-vertex-format.md Sec12.12 - HIGH
// CONFIDENCE, NOT CONFIRMED; Normal/Tangent at +8/+12 are CONFIRMED - see
// tests/golden/tree/COMMAND.txt for the full, plainly-stated confidence
// breakdown, deliberately one tier lower than zone_tile's own Position
// field). Supersedes the honest STATUS.txt gap this directory held before
// (HANDOFF.md Sec9.110 keeps that history verbatim). Same shape as
// checkZoneTileScene() above - one real command, one PNG + one stdout
// capture - except the real tool here is tools/tree_baseline_render.cpp.
//
// RE-FROZEN A SECOND TIME the same day (still Sec9.120's own thread,
// closing that section's own Addendum): the FIRST freeze drew only the
// lowest-index present LOD slot's group 0, which turned out (measured
// directly, not assumed - see tests/golden/tree/COMMAND.txt's own "WHAT
// CHANGED AND WHY") to be ONLY the trunk (materialId 0) - st_pine_tall's
// other 2 present materials (branch, materialId 1; pineneedles, materialId
// 2) live in their OWN, SEPARATE tree-level LOD slots (1 and 2), not as
// extra ranges inside slot 0's own draw-group array. tools/
// tree_baseline_render.cpp was extended (this time - the prior freeze's
// own "Files" list explicitly said this tool was untouched) to draw group
// 0 of EVERY PRESENT LOD slot, composited into one frame. Material 3 (the
// billboard impostor) has no present LOD slot in this tree's data at all -
// a real, checked data absence, not a bug. The file paths this function
// checks against did NOT change (same baseline_render.png/baseline_stdout.txt
// names) - only their CONTENT did; the superseded group-0-only baseline is
// preserved alongside under `_group0_only`-suffixed filenames in the same
// directory, per tests/golden/tree/COMMAND.txt's own "Preserved here" list.
SceneResult checkTreeScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "tree (tools/tree_baseline_render, all present LOD slots/materials, layout codes 11/12)";

    const std::string goldenDir = root + "\\tests\\golden\\tree";
    const std::string workDir = root + "\\tests\\golden";
    const std::string toolExe = root + "\\tests\\golden\\_bin\\tree_baseline_render.exe";
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string stem = "st_pine_tall";
    const std::string workStem = "tree";

    if (!fileExists(goldenDir + "\\baseline_render.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(toolExe)) {
        r.status = "FAIL";
        r.details.push_back("tree_baseline_render.exe not found at " + toolExe + " - run tests/golden/build.bat");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path ("_work/tree.png", forward slash, positional - this
    // tool takes <out.png> directly, not a --capture flag) that
    // tests/golden/tree/COMMAND.txt records was used to freeze the
    // baseline - tree_baseline_render echoes this argument's basename back
    // verbatim in its own "wrote:" line, so using anything else would make
    // an otherwise-identical stdout report a false mismatch (the same one
    // caller-chosen-path detail every other scene's own COMMAND.txt in
    // this directory already documents).
    std::string cmd = "\"" + toolExe + "\" \"" + archive + "\" " + stem + " _work/" + workStem +
                      ".png > _work\\" + workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("tree_baseline_render.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_render.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("render.png  : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 6th golden scene (follow-on task, 2026-09-30): a real `.clmesh_pc`/
// `.glmesh_pc` static-prop render with real per-material textures - see
// tests/golden/clmesh_lite_fixh/COMMAND.txt for the full real-command/why
// writeup (the spec's own worked example, real texture-binding mechanism
// measurement, honest shader-scope note). NEW scene, not a re-freeze - same
// shape as checkVehicleScene()/checkZoneTileScene() above (one real command,
// one --capture PNG, one stdout capture, through the SAME fresh
// tests/golden/_bin/sr3_viewer.exe build.bat produces - extended this
// session to also compile src/level_mesh.cpp/sr3clmesh, see build.bat's own
// new section).
SceneResult checkClmeshLiteFixhScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "clmesh_lite_fixh (sr3_viewer clmesh, real .clmesh_pc texture binding)";

    const std::string goldenDir = root + "\\tests\\golden\\clmesh_lite_fixh";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = root + "\\tests\\golden\\_bin\\sr3_viewer.exe";
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string clmeshName = "lite_fixh.clmesh_pc";
    const std::string workStem = "clmesh_lite_fixh";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - run tests/golden/build.bat");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/clmesh_lite_fixh.png") that
    // tests/golden/clmesh_lite_fixh/COMMAND.txt records was used to freeze
    // the baseline - sr3_viewer echoes this argument back verbatim in its
    // "captured:" line, so using anything else would make an otherwise-
    // identical stdout report a false mismatch (the same caller-chosen-path
    // detail every other scene's own COMMAND.txt in this directory
    // documents).
    std::string cmd = "\"" + viewerExe + "\" clmesh \"" + archive + "\" " + clmeshName +
                      " --rendergroup 0 --frames 1 --capture _work/" + workStem + ".png --capture-frame 0 > _work\\" +
                      workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 7th golden scene (redispatch of agent `aecbcba5573eda88c`, 2026-09-30):
// a real `.clmesh_pc`/`.glmesh_pc` BUILDING render - see tests/golden/
// clmesh_airport_controltower/COMMAND.txt for the full real-command/why
// writeup (same prop HANDOFF.md Sec9.133/Sec9.136 already rendered and
// independently viewed, just never frozen as its own golden scene until
// now). Exact same shape as checkClmeshLiteFixhScene() above (one real
// command, one --capture PNG, one stdout capture, through the SAME fresh
// tests/golden/_bin/sr3_viewer.exe build.bat produces - no build.bat change
// needed, it already compiles src/level_mesh.cpp/sr3clmesh from the Sec9.136
// freeze).
SceneResult checkClmeshAirportControlTowerScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "clmesh_airport_controltower (sr3_viewer clmesh, real .clmesh_pc BUILDING texture binding)";

    const std::string goldenDir = root + "\\tests\\golden\\clmesh_airport_controltower";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = root + "\\tests\\golden\\_bin\\sr3_viewer.exe";
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string clmeshName = "airport_controltower.clmesh_pc";
    const std::string workStem = "clmesh_airport_controltower";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - run tests/golden/build.bat");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/clmesh_airport_controltower.png")
    // that tests/golden/clmesh_airport_controltower/COMMAND.txt records was
    // used to freeze the baseline - sr3_viewer echoes this argument back
    // verbatim in its "captured:" line, so using anything else would make an
    // otherwise-identical stdout report a false mismatch (the same caller-
    // chosen-path detail every other scene's own COMMAND.txt in this
    // directory documents).
    std::string cmd = "\"" + viewerExe + "\" clmesh \"" + archive + "\" " + clmeshName +
                      " --rendergroup 0 --frames 1 --capture _work/" + workStem + ".png --capture-frame 0 > _work\\" +
                      workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 8th golden scene, RE-FROZEN (orchestrator task, 2026-09-30, verifying
// two leads against real bytes): a real 3-pass deferred-LIT `.clmesh_pc`
// BUILDING render (G-buffer prepass -> real directional light -> real
// material pass) - see tests/golden/clmesh_airport_controltower_lit/
// COMMAND.txt for the full real-command/why writeup of the CURRENT baseline,
// and COMMAND_sparse_silhouette_v1.txt for the ORIGINAL (preserved, not
// overwritten - this project's own re-freeze convention, precedent
// HANDOFF.md Sec9.124/Sec9.127) sparse-silhouette baseline this one
// replaces. The tool changed too: tools/prototype_lit_clmesh_tower2.cpp (a
// NEW fork of tools/prototype_lit_clmesh_tower.cpp, which is itself
// untouched - "fork, don't modify") generalizes VS/PS resolution (Lead 1)
// and wires real per-material B/C shader constants (Lead 2) into the same
// pipeline shape - see that file's own top comment. Same shape as
// checkTreeScene() above otherwise (one real STANDALONE tool taking a
// single positional out-path argument, no --capture flag), through the SAME
// fresh tests/golden/_bin build tests/golden/build.bat produces (extended
// this session with a new section for prototype_lit_clmesh_tower2.exe).
SceneResult checkClmeshAirportControlTowerLitScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "clmesh_airport_controltower_lit (prototype_lit_clmesh_tower2, re-frozen real 3-pass deferred-lit .clmesh_pc render)";
    (void)dataRoot; // this tool hardcodes its own real archive path, like every prototype_*.cpp in this family

    const std::string goldenDir = root + "\\tests\\golden\\clmesh_airport_controltower_lit";
    const std::string workDir = root + "\\tests\\golden";
    const std::string toolExe = root + "\\tests\\golden\\_bin\\prototype_lit_clmesh_tower2.exe";
    const std::string workStem = "clmesh_airport_controltower_lit";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(toolExe)) {
        r.status = "FAIL";
        r.details.push_back("prototype_lit_clmesh_tower2.exe not found at " + toolExe + " - run tests/golden/build.bat");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/clmesh_airport_controltower_lit
    // .png") that tests/golden/clmesh_airport_controltower_lit/COMMAND.txt
    // records was used to freeze the baseline - this tool echoes this
    // argument back verbatim in its own "[png] wrote ..." line, so using
    // anything else would make an otherwise-identical stdout report a false
    // mismatch (the same caller-chosen-path detail every other scene in
    // this directory already documents).
    std::string cmd = "\"" + toolExe + "\" _work/" + workStem + ".png > _work\\" + workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("prototype_lit_clmesh_tower.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// --selftest: proves the comparison logic itself catches a real regression.
// Makes a byte-identical COPY of a frozen baseline PNG, flips one pixel's
// worth of bytes in the copy (a trivial, deliberate break), confirms
// comparePngExact() reports FAIL with a real differing-pixel location, then
// confirms comparing the TRUE unmodified baseline against itself reports
// PASS. Never modifies the real baseline file itself - both copies live
// under tests/golden/_work/, named from `workStem` so brad/vehicle_genki/
// vehicle_standard's own selftest artifacts don't collide.
bool selfTestOneBaseline(const std::string& label, const std::string& baseline, const std::string& workDir,
                        const std::string& workStem) {
    const std::string corrupted = workDir + "\\selftest_corrupted_" + workStem + ".png";
    const std::string clean = workDir + "\\selftest_clean_" + workStem + ".png";

    bool ok = false;
    std::vector<uint8_t> bytes = readFileBytes(baseline, ok);
    if (!ok || bytes.empty()) {
        std::printf("SELFTEST FATAL [%s]: could not read baseline '%s'\n", label.c_str(), baseline.c_str());
        return false;
    }

    // Clean copy: byte-for-byte identical -> must PASS.
    {
        std::ofstream f(clean, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // Corrupted copy: decode the real baseline to real pixels, flip ONE
    // real pixel's colour (a trivial, deliberate break at a known image
    // location), then re-encode - a real, decodable PNG that differs by
    // exactly one pixel, so the FAIL report below can show an actual
    // pixel-level diff (location + bounding box), not just an undecodable
    // corrupt stream.
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    std::string decodeErr;
    if (!decodeOwnPng(baseline, w, h, px, decodeErr)) {
        std::printf("SELFTEST FATAL [%s]: could not decode baseline for pixel-level corruption: %s\n",
                   label.c_str(), decodeErr.c_str());
        return false;
    }
    const uint32_t brokenX = w / 3, brokenY = h / 3;
    size_t brokenIdx = (static_cast<size_t>(brokenY) * w + brokenX) * 4;
    px[brokenIdx + 0] ^= 0xFF;
    px[brokenIdx + 1] ^= 0xFF;
    px[brokenIdx + 2] ^= 0xFF;
    if (!encodeOwnPngForSelfTest(corrupted, w, h, px)) {
        std::printf("SELFTEST FATAL [%s]: could not re-encode corrupted PNG\n", label.c_str());
        return false;
    }
    std::printf("[selftest:%s] flipped pixel (x=%u, y=%u) in a %ux%u image\n", label.c_str(), brokenX, brokenY, w, h);

    std::string report;
    bool passClean = comparePngExact(baseline, clean, report);
    std::printf("[selftest:%s] baseline vs UNMODIFIED copy -> %s : %s\n", label.c_str(),
               passClean ? "PASS" : "FAIL", report.c_str());

    bool passCorrupted = comparePngExact(baseline, corrupted, report);
    std::printf("[selftest:%s] baseline vs DELIBERATELY-CORRUPTED copy -> %s : %s\n", label.c_str(),
               passCorrupted ? "PASS (WRONG - selftest itself is broken)" : "FAIL (correct)", report.c_str());

    bool passRecheck = comparePngExact(baseline, clean, report);
    std::printf("[selftest:%s] baseline vs unmodified copy AGAIN -> %s : %s\n", label.c_str(),
               passRecheck ? "PASS" : "FAIL", report.c_str());

    bool ok2 = passClean && !passCorrupted && passRecheck;
    std::printf("SELFTEST[%s] %s\n", label.c_str(),
               ok2 ? "OK - harness correctly detects a real regression and is clean against the true baseline"
                   : "FAILED - the comparison logic itself is not behaving as expected");
    return ok2;
}

int runSelfTest(const std::string& root) {
    const std::string workDir = root + "\\tests\\golden\\_work";
    _mkdir(workDir.c_str());

    bool okBrad = selfTestOneBaseline("brad", root + "\\tests\\golden\\brad\\baseline_bind.png", workDir, "bind");
    bool okGenki = selfTestOneBaseline("vehicle_genki",
                                       root + "\\tests\\golden\\vehicle_genki\\baseline_capture.png", workDir,
                                       "vehicle_genki");
    bool okStandard = selfTestOneBaseline("vehicle_standard",
                                          root + "\\tests\\golden\\vehicle_standard\\baseline_capture.png", workDir,
                                          "vehicle_standard");
    bool okZoneTile = selfTestOneBaseline("zone_tile",
                                          root + "\\tests\\golden\\zone_tile\\baseline_capture.png", workDir,
                                          "zone_tile");
    bool okTree = selfTestOneBaseline("tree", root + "\\tests\\golden\\tree\\baseline_render.png", workDir,
                                      "tree");
    bool okClmesh = selfTestOneBaseline("clmesh_lite_fixh",
                                        root + "\\tests\\golden\\clmesh_lite_fixh\\baseline_capture.png", workDir,
                                        "clmesh_lite_fixh");
    bool okClmeshAirport = selfTestOneBaseline(
        "clmesh_airport_controltower",
        root + "\\tests\\golden\\clmesh_airport_controltower\\baseline_capture.png", workDir,
        "clmesh_airport_controltower");
    bool okClmeshAirportLit = selfTestOneBaseline(
        "clmesh_airport_controltower_lit",
        root + "\\tests\\golden\\clmesh_airport_controltower_lit\\baseline_capture.png", workDir,
        "clmesh_airport_controltower_lit");

    bool selfTestOk = okBrad && okGenki && okStandard && okZoneTile && okTree && okClmesh && okClmeshAirport &&
                       okClmeshAirportLit;
    std::printf("\nSELFTEST %s\n", selfTestOk ? "OK - every scene's comparison logic correctly detects a real "
                                                 "regression and is clean against its true baseline"
                                              : "FAILED - see the per-scene SELFTEST[...] lines above");
    return selfTestOk ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::string root = "D:\\Project Crreish\\TEAM B";
    std::string dataRoot = "D:\\Project Crreish\\Saints Row 3 CRREISH\\packfiles\\pc\\cache";
    bool selfTest = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--root" && i + 1 < argc) root = argv[++i];
        else if (arg == "--data-root" && i + 1 < argc) dataRoot = argv[++i];
        else if (arg == "--selftest") selfTest = true;
        else {
            std::printf("unknown argument '%s'\n", arg.c_str());
            return 1;
        }
    }

    if (selfTest) return runSelfTest(root);

    std::vector<SceneResult> results;
    results.push_back(checkBradScene(root, dataRoot));
    results.push_back(checkVehicleGenkiScene(root, dataRoot));
    results.push_back(checkVehicleStandardScene(root, dataRoot));
    results.push_back(checkZoneTileScene(root, dataRoot));
    results.push_back(checkTreeScene(root, dataRoot));
    results.push_back(checkClmeshLiteFixhScene(root, dataRoot));
    results.push_back(checkClmeshAirportControlTowerScene(root, dataRoot));
    results.push_back(checkClmeshAirportControlTowerLitScene(root, dataRoot));

    int failures = 0;
    std::printf("\n=== golden-scene regression check ===\n");
    for (const auto& r : results) {
        printResult(r);
        if (r.status == "FAIL") ++failures;
    }
    std::printf("\n%d scene(s) FAIL, exit code = failure count.\n", failures);
    return failures;
}
