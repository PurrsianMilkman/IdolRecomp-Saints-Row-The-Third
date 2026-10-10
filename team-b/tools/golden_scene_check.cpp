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
//   tests/golden/_bin/               - HISTORICAL ONLY, no longer populated:
//                                       this used to be tests/golden/
//                                       build.bat's own stand-alone cl.exe/
//                                       link.exe build output, kept separate
//                                       from CMakeLists.txt to avoid a
//                                       shared-file race with a concurrent
//                                       task editing tools/sr3_viewer.cpp.
//                                       That race is long over: every tool
//                                       build.bat used to build
//                                       (sr3_viewer, golden_scene_check
//                                       itself, tree_baseline_render,
//                                       prototype_lit_clmesh_tower2, and the
//                                       rest) is now a real CMake target
//                                       (CMakeLists.txt's own "Fold tests/
//                                       golden/build.bat's remaining
//                                       standalone tools" comment), build.bat
//                                       has been deleted, and every
//                                       checkXScene() below resolves its
//                                       tool's path via toolExePath()/
//                                       --bin-dir (set by CMakeLists.txt's
//                                       add_test() to the real CMake build
//                                       output directory) rather than this
//                                       path. --bin-dir defaults to empty,
//                                       which still falls back to this
//                                       exact tests/golden/_bin/ location -
//                                       kept only so a human manually
//                                       placing binaries there by hand
//                                       still works, not because anything
//                                       still builds there automatically.
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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Windows.h (B3 task, 2026-10-07): RegGetValueA/GetSystemDirectoryA/
// GetFileAttributesExA for queryHostEnvironmentInfo() below (OS build +
// WARP rasterizer DLL fingerprint) - advapi32/kernel32 are already part of
// MSVC's own default link-library set, so this needs no new
// target_link_libraries entry in CMakeLists.txt. NOMINMAX: this file uses
// std::min/std::max (comparePngTolerance() below) - without it Windows.h's
// own min/max MACROS shadow those calls and fail to compile.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

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
// Z_BEST_SPEED, one IEND) - originally used ONLY by --selftest, to turn a
// decoded+deliberately-altered pixel buffer back into a real, decodable
// PNG so the self-test can demonstrate an actual pixel-level diff instead
// of an undecodable corrupt stream. Not used anywhere in the EXACT-match
// PASS/FAIL path above. B3 task (2026-10-07) reuses it a second way too:
// comparePngTolerance() below also calls this to write out its own diff-
// visualization PNG on a tolerance-check failure - still never touching
// the byte-exact scene checks above, just two different callers of the
// same "encode a real PNG from an RGBA buffer" helper.
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

// Masks wall-clock timing text before a byte-for-byte compare. `city` (9th
// golden scene) is the first command whose own stdout embeds a real
// measured duration on otherwise-deterministic lines (per-cell "NNN ms"
// load times, "indexed in N.N s" texture-cache-build time) - real disk/CPU
// jitter of a few hundred ms between two runs of the identical command,
// confirmed directly (two runs of the exact same city_corner command
// produced a byte-identical PNG - MD5 6aba93ef58eb420e0fae863d3be361c4 both
// times - but differing ONLY in these timing numbers; every other field on
// the same line, incl. prop/texture counts, stayed identical). Per this
// file's own stated COMPARISON POLICY ("a fallback is added only if one
// [concrete non-determinism] is [found]"), this masks ONLY a numeric run
// immediately followed by " ms" or " s" (replaced with the literal "<T>"),
// leaving every other character - including every count, name and
// coordinate on the same line - byte-exact.
std::string maskTimingText(const std::string& line) {
    std::string out;
    out.reserve(line.size());
    size_t i = 0;
    while (i < line.size()) {
        if (std::isdigit(static_cast<unsigned char>(line[i]))) {
            size_t j = i;
            while (j < line.size() && (std::isdigit(static_cast<unsigned char>(line[j])) || line[j] == '.')) ++j;
            size_t k = j;
            if (k < line.size() && line[k] == ' ') ++k;
            const bool isMs = line.compare(k, 2, "ms") == 0;
            const bool isS = line.compare(k, 1, "s") == 0 &&
                             !(k + 1 < line.size() && std::isalpha(static_cast<unsigned char>(line[k + 1])));
            if (isMs || isS) {
                out += "<T>";
                i = j;
                continue;
            }
        }
        out += line[i++];
    }
    return out;
}

bool compareTextMaskingTiming(const std::string& baselinePath, const std::string& freshPath, std::string& report) {
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
    while (std::getline(fa, line)) la.push_back(maskTimingText(line));
    while (std::getline(fb, line)) lb.push_back(maskTimingText(line));

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
        report = "identical after masking timing text (" + std::to_string(la.size()) + " lines)";
    } else {
        std::ostringstream head;
        head << diffCount << " line(s) differ after masking timing text (baseline " << la.size() << " lines, fresh "
             << lb.size() << " lines):\n"
             << out.str();
        report = head.str();
    }
    return pass;
}

// --- Tolerance-based image diff (B3 task, 2026-10-07) ----------------------
// This is NOT a replacement for comparePngExact() above, which remains the
// PRIMARY/authoritative check for every existing golden scene, unchanged -
// per this file's own top-of-file COMPARISON POLICY comment, this
// project's renders are deterministic and byte-exact comparison against a
// FROZEN baseline is the right check for that. This is for a DIFFERENT
// comparison the task that added it was explicit about: a NEW render
// against a baseline that was legitimately frozen under a DIFFERENT
// device (e.g. forcing WARP here when the original baseline was frozen on
// whatever hardware adapter happened to be present) - two correct renders
// of the same scene that may legitimately differ by a few LSBs without
// either one being wrong. A small, explicitly-authored reimplementation of
// pixelmatch's documented approach (https://github.com/mapbox/pixelmatch,
// ISC licence) - per-channel delta thresholding plus an overall
// differing-pixel accounting - NOT copied code, and NOT NVIDIA FLIP (BSD-3
// Clause): FLIP is a full perceptual-difference metric (its own CUDA/CPU
// implementation, HDR support, visualization pipeline) and is a lot of
// surface area to vendor for a project that just needs a fixed ±1-2/255
// per-channel band plus an overall max/mean gate - writing the documented
// idea directly is both smaller and easier to audit than vendoring it.
struct ToleranceDiffResult {
    bool pass = false;
    uint64_t diffPixelCount = 0;  // pixels where some channel exceeds perChannelTolerance
    uint64_t totalPixels = 0;
    double meanAbsDiff = 0.0;     // mean per-channel abs diff, over every channel of every pixel
    uint32_t maxAbsDiff = 0;      // worst single channel diff anywhere in the image
    std::string report;
};

// `perChannelTolerance`: a per-channel delta at or below this doesn't count
// toward diffPixelCount (the task's own "±1-2/255" band - informational,
// reported but NOT itself the pass/fail gate, same as pixelmatch's own
// "number of differing pixels" being reported output rather than the sole
// gate). `meanDiffThreshold255`/`hardMaxDiff`: the actual gate - PASS
// requires the image-wide mean per-channel diff to stay at or below
// `meanDiffThreshold255` AND no single channel anywhere to exceed
// `hardMaxDiff` (catches one badly-wrong pixel even if the image-wide mean
// looks fine). On FAIL, if `diffPngOutPath` is non-empty, writes a real,
// decodable diff-visualization PNG there (white = within the ±tolerance
// band, red = exceeds it, intensity scaled by how far over).
ToleranceDiffResult comparePngTolerance(const std::string& baselinePath, const std::string& freshPath,
                                        uint8_t perChannelTolerance, double meanDiffThreshold255,
                                        uint32_t hardMaxDiff, const std::string& diffPngOutPath) {
    ToleranceDiffResult r;
    uint32_t wA = 0, hA = 0, wB = 0, hB = 0;
    std::vector<uint8_t> pxA, pxB;
    std::string errA, errB;
    if (!decodeOwnPng(baselinePath, wA, hA, pxA, errA)) {
        r.report = "could not decode baseline '" + baselinePath + "': " + errA;
        return r;
    }
    if (!decodeOwnPng(freshPath, wB, hB, pxB, errB)) {
        r.report = "could not decode fresh render '" + freshPath + "': " + errB;
        return r;
    }
    if (wA != wB || hA != hB) {
        r.report = "dimensions differ: baseline " + std::to_string(wA) + "x" + std::to_string(hA) +
                   " vs fresh " + std::to_string(wB) + "x" + std::to_string(hB);
        return r;
    }

    r.totalPixels = static_cast<uint64_t>(wA) * hA;
    uint64_t sumAbsDiff = 0;
    std::vector<uint8_t> diffVis(pxA.size());
    for (size_t i = 0; i < pxA.size(); i += 4) {
        uint32_t pixelMaxDiff = 0;
        bool pixelDiffers = false;
        for (int c = 0; c < 4; ++c) {
            int d = std::abs(static_cast<int>(pxA[i + c]) - static_cast<int>(pxB[i + c]));
            sumAbsDiff += static_cast<uint64_t>(d);
            if (static_cast<uint32_t>(d) > pixelMaxDiff) pixelMaxDiff = static_cast<uint32_t>(d);
            if (d > perChannelTolerance) pixelDiffers = true;
        }
        if (pixelMaxDiff > r.maxAbsDiff) r.maxAbsDiff = pixelMaxDiff;
        if (pixelDiffers) {
            ++r.diffPixelCount;
            uint8_t intensity = static_cast<uint8_t>(
                std::min<uint32_t>(255u, 64u + pixelMaxDiff * 4u));
            diffVis[i + 0] = intensity;
            diffVis[i + 1] = 0;
            diffVis[i + 2] = 0;
            diffVis[i + 3] = 255;
        } else {
            diffVis[i + 0] = diffVis[i + 1] = diffVis[i + 2] = 255;
            diffVis[i + 3] = 255;
        }
    }
    r.meanAbsDiff = pxA.empty() ? 0.0 : static_cast<double>(sumAbsDiff) / static_cast<double>(pxA.size());
    r.pass = (r.maxAbsDiff <= hardMaxDiff) && (r.meanAbsDiff <= meanDiffThreshold255);

    std::ostringstream out;
    out << (r.pass ? "PASS" : "FAIL") << " - tolerance diff: " << r.diffPixelCount << "/" << r.totalPixels
        << " pixels exceed the \xC2\xB1" << static_cast<int>(perChannelTolerance)
        << "/255 per-channel band; maxAbsDiff=" << r.maxAbsDiff << " (limit " << hardMaxDiff
        << "), meanAbsDiff=" << r.meanAbsDiff << " (limit " << meanDiffThreshold255 << ")";
    if (!r.pass && !diffPngOutPath.empty()) {
        if (encodeOwnPngForSelfTest(diffPngOutPath, wA, hA, diffVis)) {
            out << "; diff PNG written to " << diffPngOutPath;
        } else {
            out << "; (failed to write diff PNG to " << diffPngOutPath << ")";
        }
    }
    r.report = out.str();
    return r;
}

// Self-contained proof that comparePngTolerance() actually catches a real
// regression without flagging genuinely-identical (or within-tolerance)
// runs (B3 task's own stated test). Deliberately does NOT depend on any
// real frozen baseline under tests/golden/ - that directory is gitignored
// ("regenerated locally", see this repo's own .gitignore) and so is absent
// from a fresh checkout/worktree entirely; this self-test synthesizes its
// own baseline image in memory instead, so it proves the comparison logic
// on every checkout, not just one that happens to already hold golden
// baselines.
bool fileExists(const std::string& path); // defined below, used here and by main()'s real scene checks

bool runToleranceSelfTest(const std::string& root) {
    const std::string workDir = root + "\\tests\\golden\\_work";
    _mkdir((root + "\\tests\\golden").c_str());
    _mkdir(workDir.c_str());

    const uint32_t w = 64, h = 64;
    std::vector<uint8_t> base(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t i = (static_cast<size_t>(y) * w + x) * 4;
            base[i + 0] = static_cast<uint8_t>(x * 4);
            base[i + 1] = static_cast<uint8_t>(y * 4);
            base[i + 2] = 128;
            base[i + 3] = 255;
        }
    }
    const std::string baselinePath = workDir + "\\selftest_tolerance_baseline.png";
    if (!encodeOwnPngForSelfTest(baselinePath, w, h, base)) {
        std::printf("SELFTEST-TOLERANCE FATAL: could not write synthetic baseline\n");
        return false;
    }

    // Shared gate for every case below: ±2/255 per-channel (the task's own
    // stated band), meanAbsDiff <= 1.5 (just above that per-channel ceiling -
    // small, UNIFORM LSB-level noise across every pixel, like case B below,
    // should still pass; a real regression's mean lands far above this, as
    // case C demonstrates), maxAbsDiff <= 32 (catches one badly-wrong pixel
    // even if the image-wide mean still looks small).
    const uint8_t kPerChannelTolerance = 2;
    const double kMeanDiffThreshold = 1.5;
    const uint32_t kHardMaxDiff = 32;

    // Case A: a genuinely byte-identical copy -> must PASS.
    const std::string identicalPath = workDir + "\\selftest_tolerance_identical.png";
    encodeOwnPngForSelfTest(identicalPath, w, h, base);
    ToleranceDiffResult rA = comparePngTolerance(baselinePath, identicalPath, kPerChannelTolerance,
                                                 kMeanDiffThreshold, kHardMaxDiff, "");
    std::printf("[selftest-tolerance] identical copy -> %s : %s\n", rA.pass ? "PASS" : "FAIL",
               rA.report.c_str());

    // Case B: every channel nudged by +1 (inside the +-2/255 band, like
    // the kind of LSB-level noise two legitimately-different-but-correct
    // renders of the same scene might show) -> must still PASS.
    std::vector<uint8_t> nearCopy = base;
    for (auto& b : nearCopy) b = static_cast<uint8_t>(std::min(255, static_cast<int>(b) + 1));
    const std::string nearPath = workDir + "\\selftest_tolerance_near.png";
    encodeOwnPngForSelfTest(nearPath, w, h, nearCopy);
    ToleranceDiffResult rB = comparePngTolerance(baselinePath, nearPath, kPerChannelTolerance,
                                                 kMeanDiffThreshold, kHardMaxDiff, "");
    std::printf("[selftest-tolerance] +1/255 nudged copy -> %s : %s\n", rB.pass ? "PASS" : "FAIL",
               rB.report.c_str());

    // Case C: a deliberate, large perturbation - a 16x16 block shoved by
    // +60/255 - well outside both the per-channel band AND the hard max -
    // must FAIL, and must produce a real, decodable diff PNG.
    std::vector<uint8_t> corrupted = base;
    for (uint32_t y = 16; y < 32; ++y) {
        for (uint32_t x = 16; x < 32; ++x) {
            size_t i = (static_cast<size_t>(y) * w + x) * 4;
            for (int c = 0; c < 3; ++c) {
                corrupted[i + c] = static_cast<uint8_t>(std::min(255, static_cast<int>(corrupted[i + c]) + 60));
            }
        }
    }
    const std::string corruptedPath = workDir + "\\selftest_tolerance_corrupted.png";
    encodeOwnPngForSelfTest(corruptedPath, w, h, corrupted);
    const std::string diffPngPath = workDir + "\\selftest_tolerance_diff.png";
    ToleranceDiffResult rC = comparePngTolerance(baselinePath, corruptedPath, kPerChannelTolerance,
                                                 kMeanDiffThreshold, kHardMaxDiff, diffPngPath);
    std::printf("[selftest-tolerance] deliberately-perturbed 16x16 block -> %s : %s\n",
               rC.pass ? "PASS (WRONG - selftest itself is broken)" : "FAIL (correct)", rC.report.c_str());
    bool diffPngExists = fileExists(diffPngPath);
    std::printf("[selftest-tolerance] diff PNG written: %s (%s)\n", diffPngExists ? "yes" : "no",
               diffPngPath.c_str());

    bool ok = rA.pass && rB.pass && !rC.pass && diffPngExists;
    std::printf("\nSELFTEST-TOLERANCE %s\n",
               ok ? "OK - tolerance diff passes two genuinely-identical/near-identical runs and "
                    "fails + visualizes a deliberately-perturbed one"
                  : "FAILED - see the per-case lines above");
    return ok;
}

// Runs `command` with the child process's working directory set to `cwd`.
//
// Window-focus-stealing fix (2026-10-08): this used to be a plain
// std::system() call. system() itself spawns through cmd.exe without
// CREATE_NEW_CONSOLE, so in practice it reuses the calling process's own
// console rather than popping a new one up - but that depends on this
// process already having a visible/attachable console, which isn't
// guaranteed for every context this harness can be launched from (ctest
// normally gives it one; a GUI-launched or detached caller might not).
// CreateProcess + CREATE_NO_WINDOW below is unconditional: the child
// (cmd.exe, and in turn tests/golden/_bin/sr3_viewer.exe, which this
// harness's whole job is to drive) gets no console window at all, every
// time, regardless of how THIS process itself was launched - the one
// unconditional way to guarantee no console flash from this subprocess.
// lpCurrentDirectory sets the child's cwd directly, which also replaces the
// previous manual _getcwd/_chdir/_chdir-back dance with no behaviour change
// (this process's own cwd is never touched either way).
int runIn(const std::string& cwd, const std::string& command) {
    // cmd.exe's own quoting rule: when the command starts with a quoted,
    // space-containing path, the WHOLE line must be wrapped in one more
    // pair of quotes or cmd mis-splits it on the first space (observed
    // directly: without this, "D:\Project Crreish\..." was parsed as the
    // command "D:\Project" with the rest as arguments). cmd /c gets our
    // string exactly the same way system() used to hand it off, so `>`/
    // `2>&1` redirection inside `command` still just works.
    std::string fullCmd = "cmd.exe /c \"" + command + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // CreateProcess can write into the command-line buffer, so it needs a
    // mutable, null-terminated copy rather than fullCmd.c_str() directly.
    std::vector<char> cmdLineBuf(fullCmd.begin(), fullCmd.end());
    cmdLineBuf.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, cmdLineBuf.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi);
    if (!ok) {
        std::printf("FATAL: CreateProcess failed for '%s' (cwd '%s', GetLastError=%lu)\n",
                   fullCmd.c_str(), cwd.c_str(), static_cast<unsigned long>(GetLastError()));
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

bool fileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

// --bin-dir override (CMake-fold task, 2026-10-08): every checkXScene()
// below used to hardcode tests/golden/_bin/<exe> - the directory ONLY
// tests/golden/build.bat's own cl.exe/link.exe steps ever populated (see
// this file's own top-comment description of _bin/, now stale). Now that
// sr3_viewer/golden_scene_check/tree_baseline_render/
// prototype_lit_clmesh_tower2 (and the rest of build.bat's old tool list)
// are real CMake targets (CMakeLists.txt's own "Fold tests/golden/
// build.bat's remaining standalone tools into real CMake targets"
// comment), a genuine `cmake --build` puts them all in the generator's own
// per-config output directory instead (e.g. <build>/Release/ for the VS
// generator - the same directory golden_scene_check.exe itself runs from),
// which tests/golden/_bin/ never sees. Without this, every scene below
// would FAIL "not found" on a fresh CMake-only build even though the real
// binary sits right next to this very executable. g_binDir is set from
// --bin-dir (CMakeLists.txt's add_test() passes
// $<TARGET_FILE_DIR:sr3_viewer>); toolExePath() resolves there when set,
// and falls back to the OLD tests/golden/_bin/ lookup when empty, so
// anyone still invoking this by hand without --bin-dir keeps working
// exactly as before.
std::string g_binDir;

std::string toolExePath(const std::string& root, const std::string& exeName) {
    if (!g_binDir.empty()) return g_binDir + "\\" + exeName;
    return root + "\\tests\\golden\\_bin\\" + exeName;
}

// Records the OS build + WARP rasterizer DLL fingerprint ALONGSIDE each
// golden's own existing metadata (B3 task, 2026-10-07) - additive, and
// deliberately NOT folded into the byte-exact stdout any scene's
// comparePngExact()/compareTextExact() checks above: those stay
// untouched (this project's own COMPARISON POLICY, see this file's top
// comment, is byte-exact against a FROZEN baseline - mixing a
// host-specific line into that stream would make every existing baseline
// fail on a different machine for a reason that has nothing to do with a
// real regression). Instead this is written to its own sidecar file
// (_work/ENV.txt, scratch like every other _work/ output) that whoever
// re-freezes a golden under forced WARP (see SR3_FORCE_WARP below) can
// copy alongside that scene's own COMMAND.txt as a permanent ENV.txt,
// recording which WARP build actually produced the frozen pixels.
//
// OS build: HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion's own
// CurrentBuildNumber + UBR (the real, registry-documented way to get a
// precise Windows build - GetVersionEx() is manifest-dependent and lies
// by design on modern Windows, so this project doesn't use it).
// WARP version: no DXGI/D3D11 API exposes a WARP-specific version
// number directly: this reports the real d3d10warp.dll's file size +
// last-write FILETIME from %SystemRoot%\System32 instead - the actual
// DLL D3D11 loads for a WARP device, so this fingerprint changes if and
// only if Windows Update actually replaces that file. Deliberately NOT
// parsed from the DLL's VERSIONINFO resource (GetFileVersionInfo) to
// avoid adding a new version.lib link dependency for this one metadata
// nicety - a size+timestamp fingerprint is sufficient to tell "same WARP
// binary" from "different WARP binary" across two machines/runs.
std::string queryHostEnvironmentInfo() {
    std::ostringstream out;

    char buildNumber[64] = {};
    DWORD buildNumberSize = sizeof(buildNumber);
    LONG rc1 = RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                           "CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, buildNumber, &buildNumberSize);
    DWORD ubr = 0;
    DWORD ubrSize = sizeof(ubr);
    LONG rc2 = RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                           "UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &ubrSize);
    out << "OS build: " << (rc1 == ERROR_SUCCESS ? buildNumber : "unknown") << "."
        << (rc2 == ERROR_SUCCESS ? std::to_string(ubr) : "?");

    char sysDir[MAX_PATH] = {};
    UINT sysDirLen = GetSystemDirectoryA(sysDir, MAX_PATH);
    out << "; WARP (d3d10warp.dll): ";
    if (sysDirLen == 0 || sysDirLen >= MAX_PATH) {
        out << "could not resolve System32 directory";
    } else {
        std::string warpPath = std::string(sysDir) + "\\d3d10warp.dll";
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExA(warpPath.c_str(), GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER size{};
            size.LowPart = fad.nFileSizeLow;
            size.HighPart = fad.nFileSizeHigh;
            ULARGE_INTEGER writeTime{};
            writeTime.LowPart = fad.ftLastWriteTime.dwLowDateTime;
            writeTime.HighPart = fad.ftLastWriteTime.dwHighDateTime;
            out << "size=" << size.QuadPart << " bytes, last-write-FILETIME=" << writeTime.QuadPart;
        } else {
            out << "not found at '" << warpPath << "' (GetLastError=" << GetLastError() << ")";
        }
    }
    return out.str();
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
    // tests/golden/_bin/sr3_viewer.exe (WARP-migration task, 2026-10-07),
    // NOT build_verify/sr3_viewer.exe: this is the one scene in this file
    // that had been left on the stale build_verify/ binary since before
    // every other scene here moved to the fresh tests/golden/_bin/ build
    // (see checkVehicleScene()'s own comment on exactly this same lesson
    // for `vehicle`) - confirmed directly a real problem for THIS task
    // specifically: build_verify/sr3_viewer.exe (last built 2026-09-29)
    // predates src/device.cpp's SR3_FORCE_WARP support entirely, so it
    // silently ignores SR3_FORCE_WARP and always renders hardware
    // regardless of this harness's own forced-WARP env var - the first
    // freeze attempt here under this task produced a baseline whose own
    // "device" stdout line read HARDWARE even with SR3_FORCE_WARP=1 set,
    // caught by re-reading the frozen baseline_stdout.txt right after
    // freezing it rather than trusting the process silently. Pointing
    // brad at the same fresh, WARP-aware tests/golden/_bin/sr3_viewer.exe
    // every other scene already uses fixes this and brings brad in line
    // with this file's own standing convention.
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
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
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string archive = dataRoot + "\\" + vehicleArchive;
    const std::string shadersArchive = dataRoot + "\\shaders.vpp_pc";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
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

// ---------------------------------------------------------------------------
// drive_demo coverage/bbox regression (this task, 2026-10-08 - the
// orchestrator's own explicit request after the drive_demo v2 mesh fix
// merged to main with an incomplete follow-through: it masked the vehicle's
// single upload()/draw() call down to draw-group-0's genuinely-channel-0
// ranges, which stopped an index-buffer corruption bug but ALSO silently
// dropped the OTHER channels' real geometry - the car's middle body
// (doors/cabin walls, submeshIndex 1/2) - with nothing automated catching
// it. A human had to notice a missing car body from a screenshot
// (HANDOFF.md). This check exists so that class of regression fails a
// build, not a glance.
//
// WHY coverage/bbox, not byte-exact (every OTHER scene in this file IS
// byte-exact - see this file's own COMPARISON POLICY note up top): a
// byte-exact compare is strictly stronger when it is usable, and drive_demo
// frame 0 of the scripted straight-phase sequence IS fully deterministic
// (fixed VehicleTuning fixture + fixed start pose, confirmed by this
// project's own existing 125/125 ctest baseline never flaking) - so this
// check ALSO freezes and compares its own self-baseline byte-exactly
// (comparePngExact below), the real PRIMARY regression gate. What a
// self-only compare cannot give is an independent cross-check: this file's
// own MeshRenderer/D3D11 state/shader source is shared code every scene
// depends on, so a self-only compare could pass even if some OTHER shared
// regression broke BOTH the frozen baseline and every fresh render the same
// way. tests/golden/vehicle_genki/baseline_capture.png renders the EXACT
// SAME real vehicle (car_4dr_genki_0.ccar_pc) through a genuinely
// independent path (the `vehicle` command's own loadVehicleObject(), a
// different camera - a fixed yaw/pitch/distance orbit around the mesh's own
// bounds, not a trailing chase - and a different resolution), so comparing
// COVERAGE/BBOX FRACTIONS (of each image's own frame, not absolute pixel
// counts - the two renders are not otherwise comparable) against it is a
// genuinely independent secondary signal: this is the orchestrator's own
// explicitly requested cross-check, deliberately loose (see
// kMinCrossVehicleCoverageRatio below) since it exists to catch "the car is
// essentially gone", not to replace the tight self-baseline gate above.
//
// Background pixels are detected by sampling the image's own corner pixel
// (every scene in this file clears to one flat colour before drawing
// anything - confirmed by reading every swapChain.clear() call site - and
// drive_demo's own --no-city car-alone frame never covers its own corner)
// rather than hardcoding a clear-colour constant here, so this check does
// not need to track runDriveDemo()'s own clear-colour literal separately.
struct CoverageStats {
    bool ok = false;
    uint32_t width = 0, height = 0;
    uint64_t coveredPixels = 0;
    uint32_t minX = 0, minY = 0, maxX = 0, maxY = 0; // valid only when coveredPixels > 0
    double coverageFraction() const {
        return (width > 0 && height > 0)
                   ? static_cast<double>(coveredPixels) / (static_cast<double>(width) * static_cast<double>(height))
                   : 0.0;
    }
    double bboxWidthFraction() const {
        return (width > 0 && coveredPixels > 0) ? static_cast<double>(maxX - minX + 1) / static_cast<double>(width)
                                                 : 0.0;
    }
    double bboxHeightFraction() const {
        return (height > 0 && coveredPixels > 0)
                   ? static_cast<double>(maxY - minY + 1) / static_cast<double>(height)
                   : 0.0;
    }
};

CoverageStats computeCoverageStats(const std::string& pngPath, std::string& error) {
    CoverageStats s;
    std::vector<uint8_t> rgba;
    if (!decodeOwnPng(pngPath, s.width, s.height, rgba, error)) return s;
    if (s.width == 0 || s.height == 0 || rgba.size() < 4) {
        error = "zero-sized image: " + pngPath;
        return s;
    }
    const uint8_t bg[4] = {rgba[0], rgba[1], rgba[2], rgba[3]}; // top-left corner, see this block's own doc comment
    uint32_t minX = s.width, minY = s.height, maxX = 0, maxY = 0;
    uint64_t covered = 0;
    for (uint32_t y = 0; y < s.height; ++y) {
        for (uint32_t x = 0; x < s.width; ++x) {
            const uint8_t* p = &rgba[(static_cast<size_t>(y) * s.width + x) * 4];
            if (p[0] != bg[0] || p[1] != bg[1] || p[2] != bg[2] || p[3] != bg[3]) {
                ++covered;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    s.coveredPixels = covered;
    if (covered > 0) {
        s.minX = minX;
        s.minY = minY;
        s.maxX = maxX;
        s.maxY = maxY;
    }
    s.ok = true;
    return s;
}

// ---------------------------------------------------------------------
// Wheel-specific coverage gate (this task, 2026-10-08 - the orchestrator's
// own direct instruction: "a car with no wheels now FAILS it", since the
// existing coverage/bbox gates above all passed on a wheel-less car - the
// drive_demo_coverage self-baseline itself was frozen BEFORE wheels were
// ever attached). Unlike the gates above (which compare the FRESH capture
// against a frozen baseline/cross-vehicle reference), this one inspects
// the FRESH capture's OWN bbox SHAPE directly, independent of any
// baseline - so it keeps failing a future wheel-less regression even if
// some later change ever re-freezes a baseline without wheels in it.
//
// Heuristic, chosen AFTER a simpler one was tried and measured to fail
// (see below): wheels sit BELOW the car body, so a car WITH wheels has a
// taller bounding box RELATIVE TO ITS OWN WIDTH than the same car WITHOUT
// them - the body's own width barely changes (wheels tuck inside the
// body's own left/right silhouette from this side-on camera), only the
// height grows. kMinWheelHeightToWidthRatio is CHOSEN but MEASURED, not
// guessed: this task's own real captures of the exact same vehicle/camera
// (sr3_viewer drive_demo --no-city --frames 1 --capture-frames 0) gave a
// bbox of 478x208 px (ratio 0.435) WITH wheels attached and 478x136 px
// (ratio 0.285) when run with --no-wheels (simulating exactly the
// regression this gate exists to catch) - width is UNCHANGED (478 both
// times) and height alone carries the whole signal. 0.35 sits
// comfortably between the two with margin on both sides.
//
// A FIRST VERSION OF THIS GATE WAS WRONG, not hypothetically but
// measured: it sampled the bottom 35% of the vehicle's own bbox HEIGHT,
// split left/right, and required each half to carry some minimum pixel
// count - this looked reasonable but FAILED TO DISCRIMINATE when actually
// run against a real --no-wheels capture (left=9558px, right=9739px,
// comfortably "passing" a car with zero wheels), because removing the
// wheels also SHRINKS the bbox the band itself is measured against, so
// "the bottom 35%" silently re-anchors onto the solid body instead of the
// now-absent wheel region. Replaced with this absolute-ratio approach
// instead of only widening the old thresholds, once the first version's
// own false-pass was directly observed (not just reasoned about).
//
// RECALIBRATED (2026-10-08, the wheel-PLACEMENT task, not the original
// wheel-ATTACHMENT task above): the original 0.35 threshold was measured
// against a build with a real double-translation bug (see
// tools/sr3_viewer.cpp's own vehicleBasePositions doc comment) that
// dragged the car's body down and sideways, artificially INFLATING the
// with-wheels bbox height (478x208px, ratio 0.435) relative to a correctly
// fitted one - once that bug was fixed, the genuinely correct "with
// wheels" capture measures SMALLER (520x170px, ratio 0.327, this task's
// own direct measurement) because a wheel properly tucked inside its arch
// contributes much less extra silhouette height than one hanging
// disconnected below the body - and 0.327 now sits BELOW the old 0.35
// threshold, which would make this gate FAIL on the correct, fixed render.
// Re-measured both ends on the FIXED binary: with wheels 520x170px (ratio
// 0.327) vs --no-wheels 520x153px (ratio 0.294, width unchanged as before)
// - a much narrower real margin (0.033, vs the original 0.15) because
// correctly-fitted wheels are mostly HIDDEN behind the body from this
// side-on camera, which is itself evidence the fix is working, not gate
// fragility. 0.31 sits in that narrower real band with a small margin on
// both sides. This gate's own original purpose - catching "wheels entirely
// absent" - is now a secondary signal: the silhouette-connectivity gate
// below is the PRIMARY, much stronger check for the specific "wheels
// attached but disconnected/misplaced" regression class this task fixed.
constexpr double kMinWheelHeightToWidthRatio = 0.31;

// ---------------------------------------------------------------------
// Silhouette connectivity gate (this task, 2026-10-08 - the orchestrator's
// own direct instruction: "tighten this scene's own coverage test so
// wheels sitting outside the body's expected lower bbox band FAIL the
// test", after a real wheel-placement regression (a double-translation
// bug - see tools/sr3_viewer.cpp's own vehicleBasePositions doc comment)
// shipped with every existing gate above, including the wheel-aspect-ratio
// one, still PASSING: a wheel dangling disconnected well below the body is
// STILL "taller" than a wheel-less car, so that gate's own signal (bbox
// height/width ratio) cannot tell "wheels attached in the right place"
// apart from "wheels attached in the wrong place" - only "wheels attached
// somewhere" vs "no wheels at all". This gate measures the thing the
// orchestrator actually asked about: silhouette CONNECTIVITY.
//
// MEASURED (this task, real captures, not guessed): an 8-connected flood
// fill over the fresh capture's own silhouette (same background-corner-
// sample rule computeCoverageStats already uses). On this task's own
// FIXED capture (sr3_viewer drive_demo --no-city --frames 1
// --capture-frames 0, the exact command this scene runs below), the whole
// vehicle - body AND both wheels - is ONE connected silhouette piece: 1
// component, area 58,579 px, bbox (362,350)-(881,519). The real pre-fix
// regression (temporarily reintroduced this task to prove this gate
// catches it - see the commit/report for the restore step) produces THREE
// disjoint components instead on the identical command: the body alone
// (area 45,192 px, bbox (460,312)-(937,452)) plus two separately-floating
// wheel blobs, each fully surrounded by background with not one shared
// pixel against the body (area 3,089 px, bbox (777,456)-(838,519); area
// 3,031 px, bbox (478,454)-(537,516)). kMinSilhouetteComponentAreaPx is
// set with a wide margin below the smaller real wheel blob (3,031 px) and
// well above plausible anti-aliasing noise - the measured noise floor is
// exactly ZERO in both real captures (no stray small components were
// found at all; antenna flags/trim render pixel-connected to the body
// already, not as separate flecks).
constexpr uint64_t kMinSilhouetteComponentAreaPx = 200;

struct SilhouetteComponent {
    uint64_t area = 0;
    uint32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
};

// Counts disjoint (8-connected) non-background regions at or above
// kMinSilhouetteComponentAreaPx, largest-first - see
// kMinSilhouetteComponentAreaPx's own doc comment for the real measured
// numbers behind the threshold. Same background-detection rule as
// computeCoverageStats (top-left corner pixel; every scene in this file
// clears to one flat colour before drawing, see that function's own doc
// comment).
std::vector<SilhouetteComponent> computeSilhouetteComponents(const std::string& pngPath, std::string& error) {
    std::vector<SilhouetteComponent> result;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
    if (!decodeOwnPng(pngPath, width, height, rgba, error)) return result;
    if (width == 0 || height == 0 || rgba.size() < 4) {
        error = "zero-sized image: " + pngPath;
        return result;
    }
    const uint8_t bg[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    auto isBg = [&](uint32_t x, uint32_t y) {
        const uint8_t* p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
        return p[0] == bg[0] && p[1] == bg[1] && p[2] == bg[2] && p[3] == bg[3];
    };
    std::vector<uint8_t> visited(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    std::vector<uint32_t> stackX, stackY;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (visited[idx] || isBg(x, y)) continue;
            SilhouetteComponent comp;
            comp.minX = comp.maxX = x;
            comp.minY = comp.maxY = y;
            stackX.clear();
            stackY.clear();
            stackX.push_back(x);
            stackY.push_back(y);
            visited[idx] = 1;
            while (!stackX.empty()) {
                const uint32_t cx = stackX.back();
                stackX.pop_back();
                const uint32_t cy = stackY.back();
                stackY.pop_back();
                ++comp.area;
                if (cx < comp.minX) comp.minX = cx;
                if (cx > comp.maxX) comp.maxX = cx;
                if (cy < comp.minY) comp.minY = cy;
                if (cy > comp.maxY) comp.maxY = cy;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int64_t nx = static_cast<int64_t>(cx) + dx;
                        const int64_t ny = static_cast<int64_t>(cy) + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int64_t>(width) || ny >= static_cast<int64_t>(height))
                            continue;
                        const size_t nidx = static_cast<size_t>(ny) * width + static_cast<size_t>(nx);
                        if (visited[nidx] || isBg(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))) continue;
                        visited[nidx] = 1;
                        stackX.push_back(static_cast<uint32_t>(nx));
                        stackY.push_back(static_cast<uint32_t>(ny));
                    }
                }
            }
            if (comp.area >= kMinSilhouetteComponentAreaPx) result.push_back(comp);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const SilhouetteComponent& a, const SilhouetteComponent& b) { return a.area > b.area; });
    return result;
}

SceneResult checkDriveDemoCoverageScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "drive_demo_coverage (sr3_viewer drive_demo, multi-channel mesh coverage/bbox regression)";

    const std::string goldenDir = root + "\\tests\\golden\\drive_demo_coverage";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    // Same real archive/ccar pair drive_demo defaults to, and vehicle_genki's
    // own golden uses - the SAME real vehicle, see this scene's own top doc
    // comment for why that matters for the cross-vehicle sanity check below.
    const std::string archive = dataRoot + "\\dlc1.vpp_pc";
    const std::string ccarName = "car_4dr_genki_0.ccar_pc";
    const std::string vehicleGenkiBaseline = root + "\\tests\\golden\\vehicle_genki\\baseline_capture.png";

    if (!fileExists(goldenDir + "\\baseline_capture.png")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(vehicleGenkiBaseline)) {
        r.status = "SKIP";
        r.details.push_back("no frozen vehicle_genki baseline (needed as this scene's cross-vehicle sanity "
                            "reference) under " + root + "\\tests\\golden\\vehicle_genki");
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    const std::string workStem = "drive_demo_coverage";
    // --no-city (this scene's own task brief: "render the car alone"): no
    // real sr3_city_0.vpp_pc dependency at all. Frame 0 of the scripted
    // straight-phase sequence is the same "single static frame" convention
    // every other scene in this file already uses - deterministic here too
    // (fixed VehicleTuning fixture + fixed start pose, not sampled from any
    // RNG/wall-clock input - see runDriveDemo()'s own doc comment).
    // --capture-frames 0 (NOT --capture-frame, confirmed directly by testing
    // this exact command): drive_demo's own --capture-frame alone only sets
    // the FALLBACK frame used when --capture-frames resolves empty: its
    // default --capture-frames value is the non-empty 3-phase list {89, 179,
    // 239} (see runDriveDemo()'s own captureFrames default), which still
    // wins over a bare --capture-frame 0 and would capture nothing at all
    // under --frames 1. Every OTHER scene in this file uses a different
    // command (`vehicle`/`zone`/...) whose own --capture-frame has no such
    // list to be overridden by - this is specific to drive_demo's own
    // multi-phase default.
    std::string cmd = "\"" + viewerExe + "\" drive_demo \"" + archive + "\" " + ccarName +
                      " --no-city --frames 1 --capture _work/" + workStem + ".png --capture-frames 0 > _work\\" +
                      workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    // drive_demo's own capture-path naming (runDriveDemo(), the per-frame
    // capture block): a NON-EMPTY --capture-frames list - true here even
    // though it names only one frame - always appends "_frameN.png" to the
    // --capture path given, rather than writing that path verbatim (that
    // bare form is only used when --capture-frames resolves empty). The
    // real written file is therefore "<workStem>.png_frame0.png", not
    // "<workStem>.png" - confirmed directly by running this exact command.
    const std::string freshPngPath = workDir + "\\_work\\" + workStem + ".png_frame0.png";

    std::string error;
    bool allPass = true;

    // PRIMARY gate: byte-exact against this scene's OWN frozen baseline -
    // same camera, same resolution, same command, so this is strictly the
    // strongest check available (same convention as every other scene in
    // this file).
    std::string pngReport;
    bool pExact = comparePngExact(goldenDir + "\\baseline_capture.png", freshPngPath, pngReport);
    r.details.push_back(std::string("self-baseline byte-exact: ") + (pExact ? "PASS - " : "FAIL - ") + pngReport);
    allPass = allPass && pExact;

    CoverageStats fresh = computeCoverageStats(freshPngPath, error);
    if (!fresh.ok) {
        r.status = "FAIL";
        r.details.push_back("could not decode fresh capture for coverage/bbox: " + error);
        return r;
    }
    CoverageStats selfBaseline = computeCoverageStats(goldenDir + "\\baseline_capture.png", error);
    if (!selfBaseline.ok) {
        r.status = "FAIL";
        r.details.push_back("could not decode self baseline for coverage/bbox: " + error);
        return r;
    }
    CoverageStats vehicleGenki = computeCoverageStats(vehicleGenkiBaseline, error);
    if (!vehicleGenki.ok) {
        r.status = "FAIL";
        r.details.push_back("could not decode vehicle_genki cross-check baseline: " + error);
        return r;
    }

    // SECONDARY gate (still against the self-baseline): coverage/bbox,
    // computed EVEN WHEN the byte-exact gate above already passed - this is
    // deliberate belt-and-suspenders, not redundant: the orchestrator's own
    // request was specifically for a coverage/bbox signal that degrades
    // gracefully (and still reports a concrete number) under a FUTURE
    // legitimate pixel-level change (say, a lighting tweak) that a byte-
    // exact gate would reject outright - kMinCoverageRatio/kMinBboxRatio are
    // the orchestrator's own stated thresholds ("drops more than ~5%"
    // coverage; bbox "shrinks significantly" - CHOSEN as 10%, a margin
    // wider than the 5% coverage gate since a bbox edge can move a few
    // pixels from a legitimate rendering difference without the underlying
    // geometry actually being wrong).
    const double kMinCoverageRatio = 0.95;
    const double kMinBboxRatio = 0.90;

    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "self-baseline coverage: fresh %.2f%% (%llu px) vs baseline %.2f%% (%llu px) of %ux%u",
                  fresh.coverageFraction() * 100.0, static_cast<unsigned long long>(fresh.coveredPixels),
                  selfBaseline.coverageFraction() * 100.0,
                  static_cast<unsigned long long>(selfBaseline.coveredPixels), fresh.width, fresh.height);
    r.details.push_back(buf);
    bool pCoverage = selfBaseline.coveredPixels == 0 ||
                     static_cast<double>(fresh.coveredPixels) >=
                         kMinCoverageRatio * static_cast<double>(selfBaseline.coveredPixels);
    r.details.push_back(std::string("  coverage vs self-baseline: ") +
                        (pCoverage ? "PASS" : "FAIL (coverage dropped more than ~5% vs the frozen baseline)"));
    allPass = allPass && pCoverage;

    const double freshBW = fresh.bboxWidthFraction(), freshBH = fresh.bboxHeightFraction();
    const double baseBW = selfBaseline.bboxWidthFraction(), baseBH = selfBaseline.bboxHeightFraction();
    std::snprintf(buf, sizeof(buf),
                  "self-baseline bbox: fresh %ux%u px (%.1f%% x %.1f%% of frame) vs baseline %ux%u px (%.1f%% x "
                  "%.1f%% of frame)",
                  fresh.coveredPixels ? (fresh.maxX - fresh.minX + 1) : 0u,
                  fresh.coveredPixels ? (fresh.maxY - fresh.minY + 1) : 0u, freshBW * 100.0, freshBH * 100.0,
                  selfBaseline.coveredPixels ? (selfBaseline.maxX - selfBaseline.minX + 1) : 0u,
                  selfBaseline.coveredPixels ? (selfBaseline.maxY - selfBaseline.minY + 1) : 0u, baseBW * 100.0,
                  baseBH * 100.0);
    r.details.push_back(buf);
    bool pBbox = (baseBW <= 0.0 || freshBW >= kMinBboxRatio * baseBW) &&
                (baseBH <= 0.0 || freshBH >= kMinBboxRatio * baseBH);
    r.details.push_back(
        std::string("  bbox vs self-baseline: ") +
        (pBbox ? "PASS" : "FAIL (bounding box shrank significantly - a body panel/channel is likely missing again)"));
    allPass = allPass && pBbox;

    // SECONDARY, cross-vehicle sanity signal (the orchestrator's own
    // explicit request - see this scene's own top doc comment for why this
    // is deliberately loose rather than a second tight gate).
    const double kMinCrossVehicleCoverageRatio = 0.5;
    std::snprintf(buf, sizeof(buf),
                  "cross-vehicle sanity: drive_demo coverage %.2f%% vs vehicle_genki coverage %.2f%% (loose "
                  "cross-check across a different camera/resolution/render-path, see top doc comment)",
                  fresh.coverageFraction() * 100.0, vehicleGenki.coverageFraction() * 100.0);
    r.details.push_back(buf);
    bool pCross = vehicleGenki.coverageFraction() <= 0.0 ||
                  fresh.coverageFraction() >= kMinCrossVehicleCoverageRatio * vehicleGenki.coverageFraction();
    r.details.push_back(
        std::string("  cross-vehicle sanity: ") +
        (pCross ? "PASS" : "FAIL (car coverage implausibly small vs the independent vehicle_genki render)"));
    allPass = allPass && pCross;

    // WHEEL-SPECIFIC gate (this task, 2026-10-08 - see
    // kMinWheelHeightToWidthRatio's own doc comment above for the full
    // why/calibration, including the first, measured-broken version of
    // this gate). Independent of the self-baseline/cross-vehicle gates
    // above: inspects the FRESH capture's own bbox shape only, so it
    // keeps catching a wheel-less car even if a future baseline update
    // ever re-freezes one without wheels.
    if (fresh.coveredPixels == 0 || fresh.maxX < fresh.minX) {
        r.details.push_back("wheel-aspect-ratio: could not compute (empty fresh bbox)");
        allPass = false;
    } else {
        const double freshWidthPx = static_cast<double>(fresh.maxX - fresh.minX + 1);
        const double freshHeightPx = static_cast<double>(fresh.maxY - fresh.minY + 1);
        const double freshRatio = freshWidthPx > 0.0 ? freshHeightPx / freshWidthPx : 0.0;
        std::snprintf(buf, sizeof(buf),
                      "wheel-aspect-ratio: fresh bbox %.0fx%.0f px, height/width ratio %.3f (need >=%.2f)",
                      freshWidthPx, freshHeightPx, freshRatio, kMinWheelHeightToWidthRatio);
        r.details.push_back(buf);
        bool pWheelAspect = freshRatio >= kMinWheelHeightToWidthRatio;
        r.details.push_back(std::string("  wheel-aspect-ratio: ") +
                            (pWheelAspect ? "PASS"
                                          : "FAIL (bbox is too flat/wide for its own width - wheels are likely "
                                            "missing, which removes the car's own lower height without "
                                            "shrinking its width)"));
        allPass = allPass && pWheelAspect;
    }

    // SILHOUETTE-CONNECTIVITY gate (this task, 2026-10-08) - see
    // kMinSilhouetteComponentAreaPx's own doc comment above for the full
    // why/calibration and the real measured before/after component counts.
    // Independent of the self-baseline/cross-vehicle/aspect-ratio gates
    // above, same as those: inspects the FRESH capture's own silhouette
    // structure directly, so it keeps catching a disconnected-wheel
    // regression even if a future baseline update ever re-freezes one with
    // the bug back in it.
    {
        std::string compError;
        std::vector<SilhouetteComponent> comps = computeSilhouetteComponents(freshPngPath, compError);
        if (comps.empty() && !compError.empty()) {
            r.details.push_back("silhouette-connectivity: could not compute (" + compError + ")");
            allPass = false;
        } else {
            std::string compList;
            for (size_t i = 0; i < comps.size() && i < 6; ++i) {
                char cbuf[160];
                std::snprintf(cbuf, sizeof(cbuf), "%s#%zu area=%llu bbox=(%u,%u)-(%u,%u)", i == 0 ? "" : ", ", i,
                              static_cast<unsigned long long>(comps[i].area), comps[i].minX, comps[i].minY,
                              comps[i].maxX, comps[i].maxY);
                compList += cbuf;
            }
            std::snprintf(buf, sizeof(buf),
                          "silhouette-connectivity: %zu significant component(s) (>= %llu px) %s", comps.size(),
                          static_cast<unsigned long long>(kMinSilhouetteComponentAreaPx), compList.c_str());
            r.details.push_back(buf);
            bool pConnectivity = comps.size() <= 1;
            r.details.push_back(
                std::string("  silhouette-connectivity: ") +
                (pConnectivity ? "PASS"
                                : "FAIL (vehicle silhouette is split into multiple disjoint pieces - a wheel (or "
                                  "other part) is likely floating disconnected from the body, not tucked into its "
                                  "own arch/socket)"));
            allPass = allPass && pConnectivity;
        }
    }

    r.status = allPass ? "PASS" : "FAIL";
    return r;
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
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
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
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
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
    const std::string toolExe = toolExePath(root, "tree_baseline_render.exe");
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
        r.details.push_back("tree_baseline_render.exe not found at " + toolExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
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
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
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
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
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
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
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
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
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

// The 9th golden scene (2026-10-03, orchestrator request): the new `city`
// command (tools/sr3_viewer.cpp's multi-tile streaming viewer, see
// runCity()'s own top comment) at a fixed camera position over the real
// corner where 4 real hN cells from 3 different tiles (0916, 1016, 1015)
// meet - the box-overlap corner at focus (-960, -700), where cell 1016h0's
// box [-960,-800]x[-840,-700], 0916h1's [-1120,-960]x[-840,-700], 1015h2's
// [-1120,-960]x[-700,-560] and 1015h3's [-960,-800]x[-700,-560] all touch,
// confirmed directly from `city --list`'s own real centres before freezing
// (not assumed). A single still frame, not a scripted flight: this is a
// STATIC regression scene like every other golden here, not a re-run of the
// implementing agent's own flight-log evidence. Same shape as
// checkZoneTileScene()/checkClmeshLiteFixhScene() above (one real `city`
// subcommand, one --capture PNG, one stdout capture, through the SAME fresh
// tests/golden/_bin/sr3_viewer.exe build.bat already produces - no build.bat
// change needed, `city` uses only readers that script already compiles for
// `zone`/`clmesh`) - EXCEPT the stdout comparison uses
// compareTextMaskingTiming() instead of compareTextExact(): `city` is the
// first golden command whose own stdout embeds a real measured wall-clock
// duration per line (the per-cell "NNN ms" load time, the one-time texture-
// cache "indexed in N.N s" build time), a genuine, directly-confirmed source
// of run-to-run non-determinism in TEXT even though the rendered PNG itself
// is still byte-identical (see maskTimingText()'s own doc comment for the
// confirmation). Within-cell prop placement is still the CHOSEN fake local
// grid every other clmesh/city scene already uses (NOT real - see
// runCity()'s own top comment) - this golden catches a regression in BOTH
// which real cells stream in/out (tile lattice, box rule) AND how their real
// textures resolve (the cross-archive texture-fallback this merge added),
// the orchestrator's own stated reason for freezing this scene.
SceneResult checkCityCornerScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "city_corner (sr3_viewer city, 4-cell real tile-boundary corner, real textures)";

    const std::string goldenDir = root + "\\tests\\golden\\city_corner";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string workStem = "city_corner";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/city_corner.png") and the same
    // --eye/--yaw/--pitch the baseline was frozen with (tests/golden/
    // city_corner/COMMAND.txt records the exact command) - sr3_viewer echoes
    // the --capture argument back verbatim in its own "captured:" line, same
    // caller-chosen-path convention every other scene here documents.
    // --no-input makes the run deterministic (ignores the live keyboard);
    // --frames 1 --capture-frame 0 captures the first (only) frame.
    std::string cmd = "\"" + viewerExe + "\" city \"" + archive +
                      "\" --eye -960 250 -700 --yaw 0 --pitch -75 --no-input --frames 1 --capture _work/" +
                      workStem + ".png --capture-frame 0 > _work\\" + workStem + "_check_stdout.txt 2>&1";
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

    bool p2 = compareTextMaskingTiming(goldenDir + "\\baseline_stdout.txt",
                                       workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The `ambient_population_demo` golden scene (spec-ai-runtime.md Sec17,
// this task 2026-10-08): a dedicated, deterministic, headless demo tool
// (tools/ambient_population_demo.cpp, NOT sr3_viewer - this scene needs no
// archive/dataRoot at all, see that tool's own top comment for why a NEW
// scene rather than extending city_corner above) exercising the real
// distant ambient pedestrian/vehicle spawn-despawn algorithm
// (include/sr3airuntime/ambient_population.h) end to end against a
// clearly-labelled synthetic stand-in road/sidewalk grid, and capturing a
// schematic top-down frame of the result. `dataRoot` is accepted only for
// call-site uniformity with every other checkXScene() here - this tool
// takes no archive argument.
SceneResult checkAmbientPopulationDemoScene(const std::string& root, const std::string& dataRoot) {
    (void)dataRoot;
    SceneResult r;
    r.name = "ambient_population_demo (distant ped/vehicle spawn-despawn, spec-ai-runtime.md Sec17, schematic top-down)";

    const std::string goldenDir = root + "\\tests\\golden\\ambient_population_demo";
    const std::string workDir = root + "\\tests\\golden";
    const std::string toolExe = toolExePath(root, "ambient_population_demo.exe");
    const std::string workStem = "ambient_population_demo";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(toolExe)) {
        r.status = "FAIL";
        r.details.push_back("ambient_population_demo.exe not found at " + toolExe +
                            " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same --frames/--capture arguments (forward-slash "_work/
    // ambient_population_demo") that tests/golden/ambient_population_demo/
    // COMMAND.txt records were used to freeze the baseline - this tool
    // takes no archive/eye/yaw/pitch arguments at all (fully synthetic, no
    // real zone data read), same caller-chosen-path convention every other
    // scene here documents.
    std::string cmd = "\"" + toolExe + "\" --frames 1200 --capture _work/" + workStem + " > _work\\" + workStem +
                      "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("ambient_population_demo.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    // Plain byte-exact compare (NOT compareTextMaskingTiming) - this tool
    // creates no D3D11 device and reads no wall clock anywhere (confirmed
    // directly: tests/golden/ambient_population_demo/COMMAND.txt records
    // running the exact same command twice and diffing both outputs
    // byte-identical), so there is no timing text to mask in the first
    // place.
    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt", workDir + "\\_work\\" + workStem + "_check_stdout.txt",
                               report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The `walk_demo` golden scene (2026-10-07): the new `walk` command
// (tools/sr3_viewer.cpp's runWalkDemo() - see its own doc comment for the
// full REAL/CHOSEN/judgement-call breakdown) - brad driven by the real
// sr3playeranim::PlayerAnimBridge/"player state machine" node graph through
// a short scripted on-foot sequence (40 frames forward, 20 "turning", 20
// stopped), placed on the real city_corner ground, captured at the last
// frame (fully stopped). Same shape as checkCityCornerScene() above - one
// real `walk` subcommand, one --capture PNG, one stdout capture, through
// the SAME tests/golden/_bin/sr3_viewer.exe build.bat already produces
// (build.bat DOES need a change for this one, unlike `city`/`zone`/
// `clmesh`: `walk` is the first golden command needing sr3player's
// movement/locomotion/camera translation units [player_follow_camera.cpp
// included - see build.bat's own comment on why a command that never calls
// CameraSystem::update() still needs it] + sr3playeranim + sr3animruntime's
// own node-graph sources + sr3tables_animation) - and the SAME
// compareTextMaskingTiming() stdout treatment as `city_corner`: `walk`
// reuses runCity()'s own loadCell() verbatim, so its stdout embeds the
// identical real per-cell "NNN ms" load time lines city_corner's own
// baseline already established as a genuine, directly-confirmed source of
// run-to-run timing jitter even though the rendered PNG itself stays
// byte-identical.
//
// CAMERA - CHOSEN DEVIATION from the task's own initial reuse recipe
// (documented here, not silently done): the literal recipe was
// city_corner's own framing, `--eye -960 250 -700 --yaw 0 --pitch -75`.
// Measured directly (decoding the captured PNG and locating the character
// by its own real simulated world position via the SAME eye/right/up/
// forward math runWalkDemo()'s own buildFreeViewProjection uses): at that
// literal framing the character is a real, correctly-positioned, but
// EXACTLY 1x4 pixel sliver in a 1400x900 frame - present and provably
// right (its on-screen location matches the hand-computed camera
// projection of its own real end-of-sequence position to the pixel), but
// not a "visible milestone" by any human-legible standard. Since this
// specific scene's whole point (per the task brief) is a VISIBLE character-
// walking demo, not a ground/texture regression check (`city_corner`
// already owns that job), the capture here instead uses a close, oblique
// framing chosen by direct iteration against the real render (7 candidates
// tried; recorded in this session's own working notes) - `--eye -957.66 15
// -691.66 --yaw -20 --pitch -55` - which renders brad as a clearly
// recognisable ~40x70 px checkered humanoid figure on real city_corner
// ground (a highway-ramp support wall, a dumpster, a building - all real
// streamed geometry/textures, not fabricated), still right next to the
// SAME real start corner city_corner's own golden names. The command's own
// --eye/--yaw/--pitch default flags still match city_corner's literal
// framing (so a caller who wants that framing back gets it with no flags),
// and the `walk` command's own stdout banner states this measurement
// plainly - this override is specific to freezing a legible golden image,
// not a claim that the default framing is wrong for its own purpose.
SceneResult checkWalkDemoScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "walk_demo (sr3_viewer walk, player<->animation bridge, city_corner ground)";

    const std::string goldenDir = root + "\\tests\\golden\\walk_demo";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string charactersArchive = dataRoot + "\\characters.vpp_pc";
    const std::string miscTablesArchive = dataRoot + "\\misc_tables.vpp_pc";
    const std::string cityArchive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string workStem = "walk_demo";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path / --eye/--yaw/--pitch/--frames/--capture-frame the
    // baseline was frozen with (tests/golden/walk_demo/COMMAND.txt records
    // the exact command) - sr3_viewer echoes --capture back verbatim in its
    // own "captured:" line, same caller-chosen-path convention every other
    // scene here documents. --no-input makes the run deterministic.
    std::string cmd = "\"" + viewerExe + "\" walk \"" + charactersArchive + "\" \"" + miscTablesArchive + "\" \"" +
                      cityArchive +
                      "\" --eye -957.66 15 -691.66 --yaw -20 --pitch -55 --no-input --frames 80 --capture _work/" +
                      workStem + ".png --capture-frame 79 > _work\\" + workStem + "_check_stdout.txt 2>&1";
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

    bool p2 = compareTextMaskingTiming(goldenDir + "\\baseline_stdout.txt",
                                       workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The `walk_demo_v2` golden scene (2026-10-07 follow-up task): `walk --v2`
// (tools/sr3_viewer.cpp's runWalkDemo() - see its own "V2" doc block for the
// full REAL/CHOSEN/FIX breakdown) fixes three real problems a review of the
// v1 walk_demo baseline above found - brad's real per-material diffuse
// textures bound instead of flat Checker (FIX 1), the capture rendered from
// the real sr3player::CameraSystem follow camera's own COMMITTED output
// instead of a hand-picked debug --eye/--yaw/--pitch (FIX 2), and the city
// streaming keyed off brad's own live position instead of the free-fly
// debug eye (FIX 3) - captured across FOUR frames (0/15/30/45) via the new
// --capture-frames flag, not just one end-state frame, so the walk is
// actually visible as a short sequence. This is a NEW golden directory, not
// a re-freeze of tests/golden/walk_demo/ in place - see
// tests/golden/walk_demo_v2/COMMAND.txt's own header for why (the artifact
// SHAPE changed, four PNGs instead of one, matching this project's
// "separate directory per real iteration" precedent rather than the
// suffixed-files-in-place one). checkWalkDemoScene() above is UNCHANGED and
// continues to check the untouched v1 baseline - confirmed directly this
// session (see that COMMAND.txt's own "Regression" section): the exact v1
// command re-run against this task's own rebuilt sr3_viewer.exe is
// byte-identical to tests/golden/walk_demo/baseline_capture.png.
// build.bat needed NO edit for --v2 (checked directly, see
// tests/golden/walk_demo_v2/COMMAND.txt): every object --v2's new code
// calls into (player_camera.obj/player_follow_camera.obj,
// tables_environment.obj/xtbl.obj, material_block.obj/geometry_block.obj/
// material_binding.obj) was already linked into sr3_viewer.exe for v1's own
// `walk` command; the one new header (camera_free_seed.h) is header-only.
SceneResult checkWalkDemoV2Scene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "walk_demo_v2 (sr3_viewer walk --v2, real follow camera + real brad textures + position-driven streaming)";

    const std::string goldenDir = root + "\\tests\\golden\\walk_demo_v2";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string charactersArchive = dataRoot + "\\characters.vpp_pc";
    const std::string miscTablesArchive = dataRoot + "\\misc_tables.vpp_pc";
    const std::string cityArchive = dataRoot + "\\sr3_city_0.vpp_pc";
    // SAME stem the baseline was frozen with (tests/golden/walk_demo_v2/
    // COMMAND.txt: "--capture _work/walk_demo_v2") - sr3_viewer echoes
    // --capture back verbatim in its own "captured:" line per frame, so a
    // different stem here would make an otherwise byte-identical stdout a
    // false mismatch (the same lesson checkWalkDemoScene() above already
    // documents for its own single-frame workStem).
    const std::string workStem = "walk_demo_v2";
    static const int kFrames[] = {0, 15, 30, 45};

    bool haveBaseline = fileExists(goldenDir + "\\baseline_stdout.txt");
    for (int f : kFrames)
        haveBaseline = haveBaseline && fileExists(goldenDir + "\\baseline_capture_frame" + std::to_string(f) + ".png");
    if (!haveBaseline) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same --v2/--capture-frames shape tests/golden/walk_demo_v2/COMMAND.txt
    // records was used to freeze the baseline - no --eye/--yaw/--pitch
    // (--v2 ignores them for the render view, see that COMMAND.txt's own
    // note). sr3_viewer echoes --capture back verbatim per captured frame
    // ("_frameN.png" appended), same caller-chosen-path convention every
    // other scene here documents.
    std::string cmd = "\"" + viewerExe + "\" walk \"" + charactersArchive + "\" \"" + miscTablesArchive + "\" \"" +
                      cityArchive + "\" --no-input --frames 80 --v2 --capture _work/" + workStem +
                      " --capture-frames 0,15,30,45 > _work\\" + workStem + "_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    for (int f : kFrames) {
        const std::string suffix = "_frame" + std::to_string(f) + ".png";
        bool p = comparePngExact(goldenDir + "\\baseline_capture_frame" + std::to_string(f) + ".png",
                                 workDir + "\\_work\\" + workStem + suffix, report);
        r.details.push_back("capture frame " + std::to_string(f) + " : " + (p ? "PASS - " : "FAIL - ") + report);
        allPass = allPass && p;
    }

    bool pStdout = compareTextMaskingTiming(goldenDir + "\\baseline_stdout.txt",
                                            workDir + "\\_work\\" + workStem + "_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (pStdout ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && pStdout;

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
    const std::string toolExe = toolExePath(root, "prototype_lit_clmesh_tower2.exe");
    const std::string workStem = "clmesh_airport_controltower_lit";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(toolExe)) {
        r.status = "FAIL";
        r.details.push_back("prototype_lit_clmesh_tower2.exe not found at " + toolExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
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

// The 10th golden scene (2026-10-03): the FIRST Vint UI render - `sr3_viewer
// vint-doc` on the real shipped HUD document hud_running_man.vint_doc
// (interface_startup.vpp_pc), every `bitmap` element drawn as a real textured
// quad from real bitmap-sheet sub-images (spec-vint-render.md Sec12.1/
// Sec12.4, sheets per spec-tables-environment.md Sec17.4), `group` drawing
// nothing (Sec12.3), `text` only a labelled PLACEHOLDER marker - see
// tests/golden/vint_hud_running_man/COMMAND.txt for the real command, why
// this document, and every CHOSEN value the frame depends on, and
// runVintDoc()'s own top comment in tools/sr3_viewer.cpp. Same shape as
// checkClmeshAirportControlTowerScene() (one real sr3_viewer subcommand, a
// positional out-path PNG, one stdout capture, byte-exact on both - this
// command prints no wall-clock timing, two runs were byte-identical apart
// from the echoed out path), through the SAME fresh tests/golden/_bin/
// sr3_viewer.exe build.bat produces (extended with a `vint-doc` section:
// sr3vintdoc + sr3luahost + Lua 5.1, packed into vint_luahost.lib).
SceneResult checkVintHudRunningManScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "vint_hud_running_man (sr3_viewer vint-doc, Vint UI render: real bitmap quads + real text, Sec12/Sec14)";

    const std::string goldenDir = root + "\\tests\\golden\\vint_hud_running_man";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string workStem = "vint_hud_running_man";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path ("_work/vint_hud_running_man.png", echoed verbatim in the
    // command's own "wrote" line) and the same archives/flags the baseline was
    // frozen with (COMMAND.txt records the exact command).
    std::string cmd = "\"" + viewerExe + "\" vint-doc \"" + dataRoot + "\\interface_startup.vpp_pc\" " +
                      "hud_running_man.vint_doc \"" + dataRoot + "\\misc_tables.vpp_pc\" _work/" + workStem +
                      ".png --tables \"" + dataRoot + "\\patch_compressed.vpp_pc\" --textures \"" + dataRoot +
                      "\\misc.vpp_pc\" --textures \"" + dataRoot + "\\interface.vpp_pc\" > _work\\" + workStem +
                      "_check_stdout.txt 2>&1";
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

// hud.vint_doc: the main HUD document, covering real stencil masking (7
// mask elements) and non-filter render_mode blend codes (15 elements) -
// spec-vint-render.md Sec15 (UI draw unit U6). hud_running_man.vint_doc's
// own golden exercises neither (COMMAND.txt's own "why this document"
// note), so this is the dedicated regression coverage for both of U6's
// biggest additions, same pattern as checkVintHudRunningManScene above.
SceneResult checkHudMaskScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "hud_mask (sr3_viewer vint-doc hud.vint_doc: real stencil masking + non-filter render_mode blend codes + real text, Sec15.4/15.6/Sec14)";

    const std::string goldenDir = root + "\\tests\\golden\\hud_mask";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string workStem = "hud_mask";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    std::string cmd = "\"" + viewerExe + "\" vint-doc \"" + dataRoot + "\\interface_startup.vpp_pc\" " +
                      "hud.vint_doc \"" + dataRoot + "\\misc_tables.vpp_pc\" _work/" + workStem +
                      ".png --tables \"" + dataRoot + "\\patch_compressed.vpp_pc\" --textures \"" + dataRoot +
                      "\\misc.vpp_pc\" --textures \"" + dataRoot + "\\interface.vpp_pc\" > _work\\" + workStem +
                      "_check_stdout.txt 2>&1";
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

// hud-live-demo (spec-hud-runtime.md, feature/hud-live-values task): the
// focused health/ammo/money/notoriety overlay, driven by the REAL producer
// natives (get_current_hit_points/set_current_hit_points/get_max_hit_points,
// cash_add, inv_weapon_add_temporary/game_hud_update_inventory,
// notoriety_set/on_notoriety_event) through a real Host/EngineState, NOT the
// hud_display_*/hud_bar_* pools spec-hud-runtime.md Sec1 CONFIRMS are ruled
// out as the core-stat path. 6 frames in one run (see
// tests/golden/hud_live_demo/COMMAND.txt for what each one proves,
// frame3_ammo_pending's own byte-identical-to-frame2_cash pixel proof of the
// event-driven ammo cadence in particular) - every PNG plus the one shared
// stdout compared exactly, same convention as checkHudMaskScene() above.
SceneResult checkHudLiveDemoScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "hud_live_demo (sr3_viewer hud-live-demo: live core-stat HUD overlay, spec-hud-runtime.md)";

    const std::string goldenDir = root + "\\tests\\golden\\hud_live_demo";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string workStem = "hud_live_demo";
    static const char* const kFrames[] = {"frame0_baseline", "frame1_damage",       "frame2_cash",
                                          "frame3_ammo_pending", "frame4_ammo_updated", "frame5_notoriety"};

    if (!fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    for (const char* f : kFrames) {
        if (!fileExists(goldenDir + "\\baseline_" + f + ".png")) {
            r.status = "SKIP";
            r.details.push_back("no frozen baseline_" + std::string(f) + ".png under " + goldenDir);
            return r;
        }
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    std::string cmd = "\"" + viewerExe + "\" hud-live-demo \"" + root + "\\tools\\lua_all_registered_1490_tagged.txt\" " +
                      "_work/" + workStem + " --textures \"" + dataRoot + "\\misc.vpp_pc\" > _work\\" + workStem +
                      "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    for (const char* f : kFrames) {
        bool p = comparePngExact(goldenDir + "\\baseline_" + f + ".png",
                                 workDir + "\\_work\\" + workStem + "_" + f + ".png", report);
        r.details.push_back(std::string(f) + ".png : " + (p ? "PASS - " : "FAIL - ") + report);
        allPass = allPass && p;
    }
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

    bool okVintHud = selfTestOneBaseline("vint_hud_running_man",
                                         root + "\\tests\\golden\\vint_hud_running_man\\baseline_capture.png", workDir,
                                         "vint_hud_running_man");
    bool okHudLiveDemo = selfTestOneBaseline(
        "hud_live_demo", root + "\\tests\\golden\\hud_live_demo\\baseline_frame1_damage.png", workDir, "hud_live_demo");

    bool selfTestOk = okBrad && okGenki && okStandard && okZoneTile && okTree && okClmesh && okClmeshAirport &&
                       okClmeshAirportLit && okVintHud && okHudLiveDemo;
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
    bool selfTestTolerance = false;
    bool forceHw = false;

    // --diff-tolerance (WARP-migration task, 2026-10-07): a standalone
    // utility mode, NOT part of the golden-scene regression flow below -
    // exposes comparePngTolerance() (B3 task) directly on the command line
    // so a hardware-vs-WARP migration (or any other "two legitimately-
    // different renders of the same scene" comparison) can be driven from
    // a script without writing new C++ for it, reusing the exact same
    // vetted comparison logic runToleranceSelfTest() already proves
    // correct. Takes two PNG paths positionally; everything else is an
    // optional override of comparePngTolerance()'s own named parameters
    // (same defaults runToleranceSelfTest() uses: +-2/255 per-channel band,
    // meanAbsDiff <= 1.5, maxAbsDiff <= 32).
    bool diffToleranceMode = false;
    std::string diffBaseline, diffFresh, diffOut;
    uint8_t diffPerChannelTol = 2;
    double diffMeanTol = 1.5;
    uint32_t diffHardMax = 32;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--root" && i + 1 < argc) root = argv[++i];
        else if (arg == "--data-root" && i + 1 < argc) dataRoot = argv[++i];
        // --bin-dir (CMake-fold task, 2026-10-08): see toolExePath()'s own
        // doc comment above - points every checkXScene() at the real CMake
        // build output directory instead of the old build.bat-only
        // tests/golden/_bin/. CMakeLists.txt's add_test() passes
        // $<TARGET_FILE_DIR:sr3_viewer> here.
        else if (arg == "--bin-dir" && i + 1 < argc) g_binDir = argv[++i];
        else if (arg == "--selftest") selfTest = true;
        else if (arg == "--selftest-tolerance") selfTestTolerance = true;
        else if (arg == "--hw") forceHw = true;
        else if (arg == "--diff-tolerance" && i + 2 < argc) {
            diffToleranceMode = true;
            diffBaseline = argv[++i];
            diffFresh = argv[++i];
        }
        else if (arg == "--diff-out" && i + 1 < argc) diffOut = argv[++i];
        else if (arg == "--per-channel-tol" && i + 1 < argc) diffPerChannelTol = static_cast<uint8_t>(std::atoi(argv[++i]));
        else if (arg == "--mean-tol" && i + 1 < argc) diffMeanTol = std::atof(argv[++i]);
        else if (arg == "--hard-max" && i + 1 < argc) diffHardMax = static_cast<uint32_t>(std::atoi(argv[++i]));
        else {
            std::printf("unknown argument '%s'\n", arg.c_str());
            return 1;
        }
    }

    if (diffToleranceMode) {
        ToleranceDiffResult r = comparePngTolerance(diffBaseline, diffFresh, diffPerChannelTol, diffMeanTol,
                                                     diffHardMax, diffOut);
        std::printf("%s\n", r.report.c_str());
        return r.pass ? 0 : 1;
    }

    if (selfTest) return runSelfTest(root);
    if (selfTestTolerance) return runToleranceSelfTest(root) ? 0 : 1;

    // SR3_FORCE_WARP (B3 task, 2026-10-07): this harness - and only this
    // harness - forces every subprocess it spawns below onto the WARP
    // software rasterizer (src/device.cpp's own initialise() reads this
    // env var; see that file's own comment for the full reasoning). This
    // is the "golden harness sets an env var" path the task asked for,
    // NOT a global engine change - sr3_viewer run directly by a human, or
    // by any other tool, still prefers real hardware by default, exactly
    // as before. _putenv_s affects this process's own environment block,
    // which runIn()'s std::system() calls below inherit into every
    // subprocess they spawn.
    //
    // --hw / SR3_FORCE_HW (WARP-migration task, 2026-10-07): a permanent
    // escape hatch in the OPPOSITE direction, OFF by default - every
    // golden scene still defaults to forced WARP exactly as the B3 merge
    // established (this project's now-standing policy: goldens are frozen
    // under WARP, not whatever hardware adapter happens to be present).
    // This exists so hardware rendering stays available ON DEMAND: visual
    // review of a FAIL, or producing a real-hardware image to show the
    // user, which per this task's own instruction MUST come from real
    // hardware, never WARP. Does NOT touch src/device.cpp - that file
    // already supports both directions unconditionally (hardware-
    // preferred when SR3_FORCE_WARP is unset/falsy, WARP-forced when it's
    // set truthy); this flag only controls what THIS harness's own main()
    // puts in that env var before spawning its subprocesses.
    if (!forceHw) {
        const char* hwEnv = std::getenv("SR3_FORCE_HW");
        forceHw = (hwEnv != nullptr && hwEnv[0] != '\0' && hwEnv[0] != '0');
    }
    if (!forceHw) {
        _putenv_s("SR3_FORCE_WARP", "1");
    } else {
        // Explicit hardware request: clear any SR3_FORCE_WARP=1 this
        // process might have inherited from its own parent shell/CI step -
        // "force", not "prefer", has to mean the same thing in both
        // directions, so --hw/SR3_FORCE_HW must be able to override an
        // inherited SR3_FORCE_WARP, not just fail to add to it.
        _putenv_s("SR3_FORCE_WARP", "0");
        std::printf("--hw / SR3_FORCE_HW: forcing HARDWARE rendering for this run (WARP will not be used).\n");
    }

    // Record the OS build + WARP DLL fingerprint ALONGSIDE this run's own
    // metadata (B3 task) - see queryHostEnvironmentInfo()'s own doc
    // comment for why this is a sidecar file rather than folded into any
    // byte-exact stdout comparison. Written once per run (the host/WARP
    // identity doesn't vary per scene); whoever re-freezes a golden under
    // forced WARP should copy this alongside that scene's own COMMAND.txt.
    {
        _mkdir((root + "\\tests\\golden").c_str());
        _mkdir((root + "\\tests\\golden\\_work").c_str());
        std::ofstream envFile(root + "\\tests\\golden\\_work\\ENV.txt");
        if (envFile) {
            envFile << (forceHw ? "SR3_FORCE_HW=1 (this run, hardware forced)\n" : "SR3_FORCE_WARP=1 (this run)\n")
                    << queryHostEnvironmentInfo() << "\n";
        }
    }

    std::vector<SceneResult> results;
    results.push_back(checkBradScene(root, dataRoot));
    results.push_back(checkVehicleGenkiScene(root, dataRoot));
    results.push_back(checkVehicleStandardScene(root, dataRoot));
    results.push_back(checkDriveDemoCoverageScene(root, dataRoot));
    results.push_back(checkZoneTileScene(root, dataRoot));
    results.push_back(checkTreeScene(root, dataRoot));
    results.push_back(checkClmeshLiteFixhScene(root, dataRoot));
    results.push_back(checkClmeshAirportControlTowerScene(root, dataRoot));
    results.push_back(checkClmeshAirportControlTowerLitScene(root, dataRoot));
    results.push_back(checkCityCornerScene(root, dataRoot));
    results.push_back(checkAmbientPopulationDemoScene(root, dataRoot));
    results.push_back(checkWalkDemoScene(root, dataRoot));
    results.push_back(checkWalkDemoV2Scene(root, dataRoot));
    results.push_back(checkVintHudRunningManScene(root, dataRoot));
    results.push_back(checkHudMaskScene(root, dataRoot));
    results.push_back(checkHudLiveDemoScene(root, dataRoot));

    int failures = 0;
    std::printf("\n=== golden-scene regression check ===\n");
    for (const auto& r : results) {
        printResult(r);
        if (r.status == "FAIL") ++failures;
    }
    std::printf("\n%d scene(s) FAIL, exit code = failure count.\n", failures);
    return failures;
}
