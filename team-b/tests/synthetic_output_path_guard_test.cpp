// Synthetic tests for include/sr3io/output_path_guard.h and the two capture
// writers that call it (sr3render::writePng in src/png_writer.cpp,
// sr3audioruntime::writeWavPcm16 in src/wav_writer.cpp).
//
// Regression for the 2026-10-08 incident: the shared archive
// D:\Project Crreish\Saints Row 3 CRREISH\packfiles\pc\cache\sr3_city_0.vpp_pc
// was overwritten by a 40584-byte PNG byte-identical (MD5) to
// tests/golden/clmesh_airport_controltower_lit/baseline_capture.png - a
// capture tool's raw argv[1] output path (tools/prototype_lit_clmesh_tower2.cpp
// line 1104 -> writePng at line 2632) pointed at the live archive.
//
// Every file this test creates lives under a fresh directory in the system
// temp dir; the synthetic "game install root" is a temp folder passed to
// isOutputPathSafe() as its protectedRoots argument. The real default roots
// are exercised by PATH STRING ONLY (the predicate reads metadata, never
// writes) - no writer is ever pointed at a real game path here.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3audioruntime/wav_writer.h"
#include "sr3io/output_path_guard.h"
#include "sr3render/png_writer.h"

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__ << "\n";    \
            ++g_failures;                                                                   \
        }                                                                                   \
    } while (0)

std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeBytes(const fs::path& p, const std::vector<uint8_t>& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A few KB of recognisable non-PNG bytes standing in for a .vpp_pc archive.
std::vector<uint8_t> fakeArchiveBytes() {
    std::vector<uint8_t> b(4096);
    for (size_t i = 0; i < b.size(); ++i) b[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xFF);
    b[0] = 0xCE; b[1] = 0x0A; b[2] = 0x89; b[3] = 0x51; // arbitrary non-PNG magic
    return b;
}

std::vector<uint8_t> smallRgba(uint32_t w, uint32_t h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); ++i) px[i] = static_cast<uint8_t>(i & 0xFF);
    return px;
}

struct Sandbox {
    fs::path root;     // <temp>/sr3_output_path_guard_test_<tick count>
    fs::path install;  // root/Saints Row 3 CRREISH   (synthetic protected root)
    fs::path archive;  // install/packfiles/pc/cache/sr3_city_0.vpp_pc
    fs::path work;     // root/TEAM B/tests/golden/_work
};

Sandbox makeSandbox() {
    Sandbox s;
    s.root = fs::temp_directory_path() /
             ("sr3_output_path_guard_test_" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::remove_all(s.root, ec);
    s.install = s.root / "Saints Row 3 CRREISH";
    s.archive = s.install / "packfiles" / "pc" / "cache" / "sr3_city_0.vpp_pc";
    s.work = s.root / "TEAM B" / "tests" / "golden" / "_work";
    writeBytes(s.archive, fakeArchiveBytes());
    fs::create_directories(s.work);
    return s;
}

// ---------------------------------------------------------------------
// Predicate, condition (a): inside a protected root.
// ---------------------------------------------------------------------
void testPredicate_RefusesInsideProtectedRoot(const Sandbox& s) {
    const std::vector<std::string> roots = {s.install.string()};
    std::string why;

    // The incident itself: PNG output path == the archive.
    CHECK(!sr3io::isOutputPathSafe(s.archive.string(), ".png", sr3io::kMaxOverwriteBytesPng, why, roots));
    CHECK(why.find("protected game-install root") != std::string::npos);

    // A not-yet-existing .png anywhere under the root (e.g. videos/) is still refused.
    why.clear();
    CHECK(!sr3io::isOutputPathSafe((s.install / "videos" / "new_capture.png").string(), ".png",
                                   sr3io::kMaxOverwriteBytesPng, why, roots));
    CHECK(!why.empty());

    // Case- and separator-insensitive: upper-case + mixed slashes.
    std::string shouty = s.archive.generic_string();
    for (char& c : shouty) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    for (size_t i = 0; i < shouty.size(); i += 2) {
        if (shouty[i] == '/') shouty[i] = '\\';
    }
    CHECK(!sr3io::isOutputPathSafe(shouty, ".png", sr3io::kMaxOverwriteBytesPng, why, roots));

    // ".." traversal from a sibling folder back into the root.
    const std::string traversal = (s.root / "TEAM B" / ".." / "Saints Row 3 CRREISH" / "x.png").string();
    CHECK(!sr3io::isOutputPathSafe(traversal, ".png", sr3io::kMaxOverwriteBytesPng, why, roots));

    // Whole-component boundary: a sibling whose name merely STARTS with the
    // root's name is not inside it (not a naive substring/prefix check).
    why.clear();
    CHECK(sr3io::isOutputPathSafe((s.root / "Saints Row 3 CRREISH Backup" / "x.png").string(), ".png",
                                  sr3io::kMaxOverwriteBytesPng, why, roots));
    CHECK(why.empty());
}

// ---------------------------------------------------------------------
// Predicate, condition (b): existing file with a non-matching extension,
// regardless of path (no protected roots at all).
// ---------------------------------------------------------------------
void testPredicate_RefusesWrongExtensionAnywhere(const Sandbox& s) {
    const std::vector<std::string> noRoots;
    std::string why;
    CHECK(!sr3io::isOutputPathSafe(s.archive.string(), ".png", sr3io::kMaxOverwriteBytesPng, why, noRoots));
    CHECK(why.find(".vpp_pc") != std::string::npos);

    for (const char* ext : {".str2_pc", ".cpeg_pc", ".fxo_pc", ".txt"}) {
        const fs::path other = s.work / (std::string("victim") + ext);
        writeBytes(other, {1, 2, 3, 4});
        why.clear();
        CHECK(!sr3io::isOutputPathSafe(other.string(), ".png", sr3io::kMaxOverwriteBytesPng, why, noRoots));
        CHECK(!sr3io::isOutputPathSafe(other.string(), ".wav", sr3io::kMaxOverwriteBytesWav, why, noRoots));
    }

    // An existing .png is not a valid .wav target and vice versa.
    const fs::path png = s.work / "existing.png";
    writeBytes(png, {0x89, 'P', 'N', 'G'});
    CHECK(!sr3io::isOutputPathSafe(png.string(), ".wav", sr3io::kMaxOverwriteBytesWav, why, noRoots));

    // Matching extension (any case) is fine.
    const fs::path upper = s.work / "existing_upper.PNG";
    writeBytes(upper, {0x89, 'P', 'N', 'G'});
    why.clear();
    CHECK(sr3io::isOutputPathSafe(png.string(), ".png", sr3io::kMaxOverwriteBytesPng, why, noRoots));
    CHECK(sr3io::isOutputPathSafe(upper.string(), ".png", sr3io::kMaxOverwriteBytesPng, why, noRoots));
    CHECK(why.empty());
}

// ---------------------------------------------------------------------
// Predicate, condition (c): existing correctly-named file over the ceiling.
// ---------------------------------------------------------------------
void testPredicate_RefusesOversizedExistingFile(const Sandbox& s) {
    const std::vector<std::string> noRoots;
    const fs::path big = s.work / "renamed_archive.png";
    writeBytes(big, {0});
    fs::resize_file(big, sr3io::kMaxOverwriteBytesPng + 1);
    std::string why;
    CHECK(!sr3io::isOutputPathSafe(big.string(), ".png", sr3io::kMaxOverwriteBytesPng, why, noRoots));
    CHECK(why.find("overwrite ceiling") != std::string::npos);

    fs::resize_file(big, sr3io::kMaxOverwriteBytesPng); // exactly at the ceiling: allowed
    why.clear();
    CHECK(sr3io::isOutputPathSafe(big.string(), ".png", sr3io::kMaxOverwriteBytesPng, why, noRoots));
    std::error_code ec;
    fs::remove(big, ec);
}

// ---------------------------------------------------------------------
// The real default roots, by path string only (metadata reads, no writes).
// ---------------------------------------------------------------------
void testPredicate_DefaultRootsCoverRealInstall() {
    std::string why;
    for (const char* p : {
             "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache/sr3_city_0.vpp_pc",
             "d:\\project crreish\\saints row 3 crreish\\packfiles\\pc\\cache\\sr3_city_0.vpp_pc",
             "D:\\Project Crreish\\Saints Row 3 CRREISH\\videos\\capture_that_does_not_exist.png",
             "D:/Project Crreish/TEAM B/../Saints Row 3 CRREISH/capture_that_does_not_exist.png",
             "C:/Games/Saints Row 3 CRREISH/packfiles/pc/cache/sr3_city_0.vpp_pc",
         }) {
        why.clear();
        CHECK(!sr3io::isOutputPathSafe(p, ".png", sr3io::kMaxOverwriteBytesPng, why));
        CHECK(why.find("protected game-install root") != std::string::npos);
    }
    // This repo's own capture locations are not under any default root.
    why.clear();
    CHECK(sr3io::isOutputPathSafe("D:/Project Crreish/TEAM B/tests/golden/_work/capture_that_does_not_exist.png",
                                  ".png", sr3io::kMaxOverwriteBytesPng, why));
    CHECK(sr3io::isOutputPathSafe("_work/capture_that_does_not_exist.png", ".png", sr3io::kMaxOverwriteBytesPng,
                                  why));
    CHECK(why.empty());
}

// ---------------------------------------------------------------------
// End to end through the real writers: refuse over a .vpp_pc (bytes left
// untouched), allow a normal _work/*.png / *.wav capture and its re-capture.
// ---------------------------------------------------------------------
void testWritePng_EndToEnd(const Sandbox& s) {
    const std::vector<uint8_t> before = readAll(s.archive);
    const std::vector<uint8_t> rgba = smallRgba(8, 6);
    std::string err;
    // Default roots do not cover the temp sandbox, so this refusal is
    // condition (b) alone - the path-independent backstop.
    CHECK(!sr3render::writePng(s.archive.string(), 8, 6, rgba, err));
    CHECK(!err.empty());
    CHECK(readAll(s.archive) == before);

    const fs::path capture = s.work / "clmesh_airport_controltower_lit.png";
    err.clear();
    CHECK(sr3render::writePng(capture.string(), 8, 6, rgba, err));
    CHECK(err.empty());
    std::vector<uint8_t> png = readAll(capture);
    CHECK(png.size() > 8 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    // Re-capturing over the previous run's own PNG still works.
    CHECK(sr3render::writePng(capture.string(), 8, 6, rgba, err));
    CHECK(readAll(capture) == png);
}

void testWriteWav_EndToEnd(const Sandbox& s) {
    const std::vector<uint8_t> before = readAll(s.archive);
    const std::vector<int16_t> pcm(480, 1000);
    std::string err;
    CHECK(!sr3audioruntime::writeWavPcm16(s.archive.string(), pcm, 48000, 1, err));
    CHECK(!err.empty());
    CHECK(!sr3audioruntime::writeWavPcm16(s.archive.string(), pcm, 48000, 1)); // legacy overload
    CHECK(readAll(s.archive) == before);

    const fs::path capture = s.work / "audio_capture.wav";
    err.clear();
    CHECK(sr3audioruntime::writeWavPcm16(capture.string(), pcm, 48000, 1, err));
    CHECK(err.empty());
    CHECK(sr3audioruntime::writeWavPcm16(capture.string(), pcm, 48000, 1));
    bool ok = false;
    double seconds = sr3audioruntime::wavDurationSeconds(capture.string(), ok);
    CHECK(ok && seconds > 0.0099 && seconds < 0.0101);
}

} // namespace

int main() {
    Sandbox s = makeSandbox();
    testPredicate_RefusesInsideProtectedRoot(s);
    testPredicate_RefusesWrongExtensionAnywhere(s);
    testPredicate_RefusesOversizedExistingFile(s);
    testPredicate_DefaultRootsCoverRealInstall();
    testWritePng_EndToEnd(s);
    testWriteWav_EndToEnd(s);

    std::error_code ec;
    fs::remove_all(s.root, ec);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_output_path_guard_test: all checks passed\n";
    return 0;
}
