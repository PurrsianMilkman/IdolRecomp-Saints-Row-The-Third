// Seed-corpus generator for the MSVC-native fuzz harnesses (fuzz/msvc/).
//
// Writes REAL shipped sample bytes for each of this round's four targets
// into <out_dir>/<target>/ - reusing the exact same CRREISH_GAME_CACHE_DIR
// lookup and per-entry decode pattern every tests/real_*_test.cpp file
// already uses (readEntry() below is byte-for-byte that pattern), and the
// same wrapper-header-first/ShaderWrapper-fallback blob location
// tools/ctab_census.cpp uses for `.fxo_pc` shader blobs. Nothing here
// copies game data into the repository: this tool only ever writes to
// whatever output directory it is given on the command line (a build
// directory or scratch folder), never into fuzz/ itself.
//
// Usage: seed_gen_msvc <out_dir>
// Silently skips any archive that is not present at CRREISH_GAME_CACHE_DIR
// (same "absent cache = skip, not a failure" discipline as the real_*
// ctest targets' exit-77 convention) so this remains safe to run (and to
// leave wired into the build) on a machine without the game cache.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3fxo/shader_wrapper.h"
#include "sr3fxo/wrapper_header.h"
#include "vpp/container.h"

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& p, const uint8_t* data, size_t size) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

// Same per-entry decode pattern as every tests/real_*_test.cpp file.
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    vpp::DecompressResult r = c.decompressEntry(i);
    if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated)
        return false;
    out = std::move(r.data);
    return true;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string sanitize(std::string s) {
    for (auto& c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') c = '_';
    return s;
}

// Same two-step blob location as tools/ctab_census.cpp's locateBlobs():
// the wrapper header first, the scan-based ShaderWrapper as fallback.
struct Blob { size_t offset = 0, length = 0; };
std::vector<Blob> locateShaderBlobs(const std::vector<uint8_t>& data) {
    std::vector<Blob> out;
    vpp::ByteView view(data.data(), data.size());
    sr3fxo::WrapperHeader h;
    std::string why;
    if (sr3fxo::WrapperHeader::tryParse(view, h, why)) {
        size_t end = 0;
        for (const auto& b : h.layoutBlobs(end)) {
            if (b.length == 0 || b.offset + b.length > data.size()) continue;
            out.push_back({b.offset, b.length});
        }
        return out;
    }
    try {
        sr3fxo::ShaderWrapper sw = sr3fxo::ShaderWrapper::parse(view);
        for (const auto& es : sw.shaders()) out.push_back({es.offset, es.length});
    } catch (const std::exception&) {
    }
    return out;
}

int g_written = 0;

void writeSeedsFromArchive(const fs::path& cache, const std::string& archiveName, const fs::path& outDir,
                            const std::string& wantSuffix, size_t maxEntries) {
    fs::path p = cache / archiveName;
    if (!fs::exists(p)) {
        std::cout << "skip (absent): " << p.string() << "\n";
        return;
    }
    std::vector<uint8_t> bytes = readFile(p);
    if (bytes.empty()) return;
    vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
    size_t found = 0;
    for (size_t i = 0; i < c.entries().size() && found < maxEntries; ++i) {
        if (!endsWith(c.entries()[i].name, wantSuffix)) continue;
        std::vector<uint8_t> out;
        if (!entryBytes(c, i, out) || out.empty()) continue;
        fs::path dest = outDir / (sanitize(archiveName) + "__" + sanitize(c.entries()[i].name));
        writeFile(dest, out.data(), out.size());
        ++found;
        ++g_written;
    }
    std::cout << "  " << archiveName << ": " << found << " x " << wantSuffix << "\n";
}

void writeShaderSeeds(const fs::path& cache, const std::vector<std::string>& archives, const fs::path& outDir,
                      size_t maxBlobsTotal) {
    size_t total = 0;
    for (const auto& archiveName : archives) {
        if (total >= maxBlobsTotal) break;
        fs::path p = cache / archiveName;
        if (!fs::exists(p)) {
            std::cout << "skip (absent): " << p.string() << "\n";
            continue;
        }
        std::vector<uint8_t> archiveBytes = readFile(p);
        if (archiveBytes.empty()) continue;
        vpp::Container c(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));
        size_t fromThisArchive = 0;
        for (size_t i = 0; i < c.entries().size() && total < maxBlobsTotal; ++i) {
            if (!endsWith(c.entries()[i].name, ".fxo_pc")) continue;
            std::vector<uint8_t> fxoBytes;
            if (!entryBytes(c, i, fxoBytes) || fxoBytes.empty()) continue;
            std::vector<Blob> blobs = locateShaderBlobs(fxoBytes);
            for (size_t bi = 0; bi < blobs.size() && total < maxBlobsTotal; ++bi) {
                const Blob& b = blobs[bi];
                fs::path dest = outDir / (sanitize(archiveName) + "__" + sanitize(c.entries()[i].name) + "__" +
                                           std::to_string(bi) + ".bin");
                writeFile(dest, fxoBytes.data() + b.offset, b.length);
                ++total;
                ++fromThisArchive;
                ++g_written;
            }
        }
        std::cout << "  " << archiveName << ": " << fromThisArchive << " shader blobs\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: seed_gen_msvc <out_dir>\n";
        return 2;
    }
    fs::path out = argv[1];
    const fs::path cache = CRREISH_GAME_CACHE_DIR;
    std::cout << "cache dir: " << cache.string() << "\n";

    // vpp: whole small real archives, copied verbatim - the container
    // format's own entry point takes a whole archive buffer.
    std::cout << "vpp seeds:\n";
    for (const char* name : {"startup.vpp_pc", "high_mips.vpp_pc"}) {
        fs::path p = cache / name;
        if (!fs::exists(p)) {
            std::cout << "  skip (absent): " << p.string() << "\n";
            continue;
        }
        std::vector<uint8_t> bytes = readFile(p);
        writeFile(out / "vpp" / name, bytes.data(), bytes.size());
        std::cout << "  " << name << ": " << bytes.size() << " bytes\n";
        ++g_written;
    }

    // xtbl: every real `.xtbl` entry in misc_tables.vpp_pc (capped).
    std::cout << "xtbl seeds:\n";
    writeSeedsFromArchive(cache, "misc_tables.vpp_pc", out / "xtbl", ".xtbl", 40);

    // le_strings: every real `.le_strings` entry in misc.vpp_pc (10 tables,
    // same set real_le_strings_test.cpp validates).
    std::cout << "le_strings seeds:\n";
    writeSeedsFromArchive(cache, "misc.vpp_pc", out / "le_strings", ".le_strings", 16);

    // d3d9bc: real shader bytecode blobs out of a few small/medium archives
    // that carry `.fxo_pc` entries.
    std::cout << "d3d9bc seeds:\n";
    writeShaderSeeds(cache, {"effects.vpp_pc", "decals.vpp_pc", "sr3_city_missions.vpp_pc", "shaders.vpp_pc"},
                      out / "d3d9bc", 40);

    std::cout << "total seed files written: " << g_written << "\n";
    return 0;
}
