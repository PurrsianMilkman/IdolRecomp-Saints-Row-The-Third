// golden_scene_check_util.cpp
// File, PNG and text comparison helpers, process helpers.
//
// Split out of tools/golden_scene_check.cpp (Team C refactor, 2026-10-17): the
// code below moved verbatim from that file's anonymous namespace into
// namespace golden_scene_check. See golden_scene_check.cpp's header comment for the
// harness's scope, scenes and design notes.

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

#include "golden_scene_check.h"

namespace golden_scene_check {

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

void printResult(const SceneResult& r) {
    std::printf("[%s] %s\n", r.status.c_str(), r.name.c_str());
    for (const auto& d : r.details) std::printf("    %s\n", d.c_str());
}

} // namespace golden_scene_check
